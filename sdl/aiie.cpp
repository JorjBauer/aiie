#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <pthread.h>
#else
#include <curses.h>
#include <termios.h>
#include <pthread.h>
#endif

#include "applevm.h"
#include "sdl-display.h"
#include "sdl-keyboard.h"
#include "sdl-mouse.h"
#include "sdl-speaker.h"
#include "sdl-paddles.h"
#include "nix-filemanager.h"
#include "sdl-printer.h"
#include "sdl-uthernet2.h"
#include "appleui.h"
#include "bios.h"
#include "nix-prefs.h"
#include "debugger.h"

#include "globals.h"

#include "timeutil.h"

BIOS bios;
Debugger debugger;

#ifdef __EMSCRIPTEN__
#include "applekeyboard.h"
#include "woz.h"
#include <fcntl.h>
#include <unistd.h>
// Reach Woz's protected tracks[] (the nibblized GCR bitstreams) to export them to JS, which then
// assembles the WOZ2 container itself.  aiie's own file-based .woz writer seeks by track offset,
// which Emscripten MEMFS rejects; sequential writes (below) are fine, so we dump bits + bitCounts.
#include "nibutil.h"
namespace { struct WozBits : public Woz {
  WozBits() : Woz(false, 0) {}
  // Reads a .po, nibblizes all 35 tracks, and writes them sequentially to `out` as
  // 35 records of [uint32 bitCount][NIBTRACKSIZE bytes].
  int dump(const char *in, const char *out) {
    if (!readFile(in, true, T_PO)) return 1;
    int fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 3;
    static uint8_t zero[NIBTRACKSIZE];
    for (int t = 0; t < 35; t++) {
      uint32_t bc = tracks[t].bitCount;
      if (write(fd, &bc, 4) != 4) { close(fd); return 2; }
      uint8_t *td = tracks[t].trackData ? tracks[t].trackData : zero;
      if (write(fd, td, NIBTRACKSIZE) != NIBTRACKSIZE) { close(fd); return 2; }
    }
    close(fd);
    return 0;
  }
}; }
extern "C" {
// Inject one 7-bit key from JS (browser keydown). Bypasses SDL's event system,
// which is null-stubbed under Emscripten. Delivered on the //e keyboard strobe.
EMSCRIPTEN_KEEPALIVE void aiie_inject(int c) {
  if (g_vm) ((AppleKeyboard *)g_vm->getKeyboard())->injectByte((uint8_t)(c & 0x7f));
}
EMSCRIPTEN_KEEPALIVE double aiie_cycles(void) { return g_cpu ? (double)g_cpu->cycles : 0.0; }

// ---- machine bridge: let JS read/write the //e's RAM and save/restore whole-machine state.
// This is what the playground uses to inject a freshly compiled .L2E straight into RAM and run
// it, and to snapshot a booted "ready" machine so every run starts from a known-good state.
EMSCRIPTEN_KEEPALIVE int aiie_peek(int addr) {
  return g_vm ? g_vm->getMMU()->read((uint16_t)addr) : 0;
}
EMSCRIPTEN_KEEPALIVE void aiie_poke(int addr, int val) {
  if (g_vm) g_vm->getMMU()->write((uint16_t)addr, (uint8_t)(val & 0xff));
}
// bulk RAM write: `data` is a pointer into the wasm heap (JS _malloc + HEAPU8.set).
EMSCRIPTEN_KEEPALIVE void aiie_poke_block(int addr, uint8_t *data, int len) {
  if (!g_vm) return;
  MMU *m = g_vm->getMMU();
  for (int i = 0; i < len; i++) m->write((uint16_t)(addr + i), data[i]);
}
// whole-machine save/restore (RAM + aux banks + soft switches + CPU) to an Emscripten FS file.
EMSCRIPTEN_KEEPALIVE int aiie_save_state(const char *path) {
  return (g_vm && g_vm->Suspend(path)) ? 0 : 1;
}
EMSCRIPTEN_KEEPALIVE int aiie_load_state(const char *path) {
  return (g_vm && g_vm->Resume(path)) ? 0 : 1;
}
EMSCRIPTEN_KEEPALIVE int  aiie_get_pc(void)   { return g_cpu ? g_cpu->pc : 0; }
EMSCRIPTEN_KEEPALIVE void aiie_set_pc(int pc)  { if (g_cpu) g_cpu->pc = (uint16_t)pc; }
// Emulator speed multiplier (1.0 = real ~1MHz).  Boost for non-real-time programs (turtle, big text).
EMSCRIPTEN_KEEPALIVE void aiie_set_speed(double mult) { extern double g_speedMult; g_speedMult = (mult > 0.0) ? mult : 1.0; }
EMSCRIPTEN_KEEPALIVE double aiie_get_speed(void) { extern double g_speedMult; return g_speedMult; }
// Speaker volume, 0 (silent) .. 15 (loudest).  The page exposes this as aiieVolume(n).
// g_volume is declared in globals.h (already included), so use it directly.
EMSCRIPTEN_KEEPALIVE void aiie_set_volume(int v) { g_volume = (v < 0) ? 0 : (v > 15 ? 15 : (int8_t)v); }
EMSCRIPTEN_KEEPALIVE int  aiie_get_volume(void)  { return g_volume; }
// ---- audio-driven timing --------------------------------------------------------------------------
// When g_audioPaced is set, loop() skips its own wall-clock CPU pacing and the playground's audio fill
// loop drives the CPU instead via aiie_run_cycles(), passing exactly the cycles for the samples the
// audio worklet just consumed.  Emulation then advances in lockstep with audio consumption, so the
// producer (the //e speaker) and consumer (the audio device) share one clock: no drift, no under/
// over-run.  Used only for 1x programs (sound, breakout); boosted programs keep wall-clock pacing.
EMSCRIPTEN_KEEPALIVE void aiie_set_audiopaced(int on) { extern int g_audioPaced; g_audioPaced = on ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE void aiie_run_cycles(int cycles) {
  if (!g_cpu || cycles <= 0) return;
  uint64_t target = g_cpu->cycles + (uint64_t)cycles;
  int guard = 0, guardMax = cycles / 24 + 64;
  while (g_cpu->cycles < target && ++guard < guardMax) {
    g_cpu->Run(24);
    ((AppleVM *)g_vm)->cpuMaintenance(g_cpu->cycles);
  }
}
// Convert a ProDOS-order .po (in the FS) to a bootable .woz (in the FS), reusing aiie's own
// tested GCR nibblizer.  Used by the playground's "Download" to hand out a portable disk.
EMSCRIPTEN_KEEPALIVE int aiie_track_bits(const char *inpath, const char *outpath) {
  WozBits w;
  return w.dump(inpath, outpath);
}
}
#endif

double g_speedMult = 1.0;   // emulator speed multiplier (set via aiie_set_speed / window.aiieSpeed)
int g_audioPaced = 0;       // 1 = external audio-driven pacing (loop() skips wall-clock CPU pacing)

#define NB_ENABLE 1
#define NB_DISABLE 0

int send_rst = 0;

char disk1name[256] = "\0";
char disk2name[256] = "\0";

volatile bool wantSuspend = false;
volatile bool wantResume = false;

volatile bool cpuDebuggerRunning = false;

volatile bool cpuClockInitialized = false;

void doDebugging();
void readPrefs();
void writePrefs();

void sigint_handler(int n)
{
  // If we want control-C to reset the machine, then set this here...
  send_rst = 1;

  //  ((AppleVM*)g_vm)->disk6->disk[0]->dumpInfo();
}

void nonblock(int state)
{
#ifdef __EMSCRIPTEN__
  (void)state;
#else
  struct termios ttystate;
 
  //get the terminal state
  tcgetattr(STDIN_FILENO, &ttystate);
 
  if (state==NB_ENABLE)
    {
      //turn off canonical mode
      ttystate.c_lflag &= ~ICANON;
      //minimum of number input read.
      ttystate.c_cc[VMIN] = 1;
    }
  else if (state==NB_DISABLE)
    {
      //turn on canonical mode
      ttystate.c_lflag |= ICANON;
    }
  //set the terminal attributes.
  tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);
#endif
 
}

uint8_t read(void *arg, uint16_t address)
{
  // no action; this is a dummy function until we've finished initializing...
  return 0x00;
}

void write(void *arg, uint16_t address, uint8_t v)
{
  // no action; this is a dummy function until we've finished initializing...
}

static struct timespec runBIOS(struct timespec now)
{
  static bool initialized = false;
  static struct timespec startTime;
  static struct timespec nextRuntime;
  static uint64_t cycleCount = 0;

  if (!initialized) {
    do_gettime(&startTime);
    do_gettime(&nextRuntime);
    initialized = true;
  }

  timespec_add_us(&startTime, 33333*cycleCount, &nextRuntime); // ~30Hz: snappy BIOS input without busy-spinning

  // Check if it's time to run - and if not, return how long it will
  // be until we need to run
  struct timespec diff = tsSubtract(nextRuntime, now);
  if (diff.tv_sec > 0 || diff.tv_nsec > 0) {
    // The caller can decide to nanosleep(&diff, NULL)
    return diff;
  }

  cycleCount++;

  if (!bios.loop()) {
    printf("BIOS loop has exited\n");
    g_biosInterrupt = false; // that's all she wrote!
  }

  // Reset timers!
  cpuClockInitialized = false;
  g_cpu->cycles = 0;
  
  return diff;
}

static struct timespec runCPU(struct timespec now)
{
  static struct timespec startTime;
  static struct timespec nextInstructionTime;
  
  if (!cpuClockInitialized) {
    do_gettime(&startTime);
    do_gettime(&nextInstructionTime);
    cpuClockInitialized = true;
  }

  // Check for interrupt-like actions before running the CPU
  if (wantSuspend) {
    printf("CPU halted; suspending VM\n");
    g_vm->Suspend("suspend.vm");
    printf("... done; resuming CPU.\n");
    wantSuspend = false;
  }
  if (wantResume) {
    printf("CPU halted; resuming VM\n");
    g_vm->Resume("suspend.vm");
    printf("... done. resuming CPU.\n");
    wantResume = false;
  }

  // Determine correct time for next CPU cycle
  timespec_add_cycles(&startTime, g_cpu->cycles, &nextInstructionTime);

  // Check if it's time to run - and if not, return how long it will be until we need to run
  struct timespec diff = tsSubtract(nextInstructionTime, now);
  if (diff.tv_sec > 0 || diff.tv_nsec > 0) {
    // The caller can decide to nanosleep(&diff, NULL)
    return diff;
  }

  // Run the CPU
  bool debuggerWasActive = false;
  if (debugger.active()) {
    // With the debugger running, we need to single-step through
    // instructions.
    (void)g_cpu->Run(1);
    debuggerWasActive = true;
  } else {
    // Otherwise we can run a bunch of instructions at once to
    // save on the overhead.
    (void)g_cpu->Run(24);
    if (debuggerWasActive) {
      cpuClockInitialized = false;
      g_cpu->cycles = 0;
      debuggerWasActive = false;
    }
  }

  // The paddles need to be triggered in real-time on the CPU
  // clock. That happens from the VM's CPU maintenance poller.
  ((AppleVM *)g_vm)->cpuMaintenance(g_cpu->cycles);
  
  if (debugger.active()) {
    debugger.step();
  }
  
  if (send_rst) {
    cpuDebuggerRunning = true;
    
    printf("Sending reset\n");
    g_cpu->Reset();
    
    send_rst = 0;
  }
  
  return diff;
}

#define TARGET_FPS 30
struct timespec runDisplay(struct timespec now)
{
  static bool initialized = false;
  static struct timespec startTime;
  static struct timespec nextRuntime;
  static uint64_t cycleCount = 0;

  if (!initialized) {
    do_gettime(&startTime);
    do_gettime(&nextRuntime);
    initialized = true;
  }
  
  timespec_add_us(&startTime, (1000000/TARGET_FPS)*cycleCount, &nextRuntime); // 1000000 uS/S and 30fps target

  // Check if it's time to run - and if not, return how long it will
  // be until we need to run
  struct timespec diff = tsSubtract(nextRuntime, now);
  if (diff.tv_sec > 0 || diff.tv_nsec > 0) {
    // The caller can decide to nanosleep(&diff, NULL)
    return diff;
  }

  cycleCount++;

  if (!g_biosInterrupt) {
    g_ui->blit();
    g_vm->vmdisplay->lockDisplay();
    if (g_vm->vmdisplay->needsRedraw()) {
      g_vm->vmdisplay->didRedraw();
      g_display->blit();
    }
    g_vm->vmdisplay->unlockDisplay();
    
    // For SDL, I'm throwing the printer update in with the display update...
    g_printer->update();
  }
  
  return diff;
}


void doDebugging()
{
  char buf[25];
  static time_t startAt = time(NULL);
  static uint32_t loopCount = 0;

  switch (g_debugMode) {
  case D_SHOWFPS:
    {
      // display some FPS data
      loopCount++;
      uint32_t lenSecs = time(NULL) - startAt;
      if (lenSecs >= 5) {
	snprintf(buf, sizeof(buf), "%u FPS", loopCount / lenSecs);
	g_display->debugMsg(buf);
	startAt = time(NULL);
	loopCount = 0;
      }
    }
    break;
  case D_SHOWMEMFREE:
    //    sprintf(buf, "%lu %u", FreeRamEstimate(), heapSize());
    //    g_display->debugMsg(buf);
    break;
  case D_SHOWPADDLES:
    snprintf(buf, sizeof(buf), "%u %u", g_paddles->paddle0(), g_paddles->paddle1());
    g_display->debugMsg(buf);
    break;
  case D_SHOWPC:
    snprintf(buf, sizeof(buf), "%X", g_cpu->pc);
    g_display->debugMsg(buf);
    break;
  case D_SHOWCYCLES:
    snprintf(buf, sizeof(buf), "%llX", g_cpu->cycles);
    g_display->debugMsg(buf);
    break;
  case D_SHOWNET:
    if (g_uthernet) {
      char nb[48];
      snprintf(nb, sizeof(nb), "TX%lu RX%lu CE%lu RT%lu TO%lu",
               (unsigned long)g_uthernet->statFramesSent(),
               (unsigned long)g_uthernet->statFramesReceived(),
               (unsigned long)g_uthernet->statCrcErrors(),
               (unsigned long)g_uthernet->statRetries(),
               (unsigned long)g_uthernet->statTimeouts());
      g_display->debugMsg(nb);
    }
    break;
    /*
  case D_SHOWBATTERY:
    //    sprintf(buf, "BAT %d", analogRead(BATTERYPIN));
    //    g_display->debugMsg(buf);
    break;
  case D_SHOWTIME:
    //    sprintf(buf, "%.2d:%.2d:%.2d", hour(), minute(), second());
    //    g_display->debugMsg(buf);
    break;*/
  }
}

struct timespec runMaintenance(struct timespec now)
{
  static bool initialized = false;
  static struct timespec startTime;
  static struct timespec nextRuntime;
  static uint64_t cycleCount = 0;

  if (!initialized) {
    do_gettime(&startTime);
    do_gettime(&nextRuntime);
    initialized = true;
  }

  timespec_add_us(&startTime, 16667*cycleCount, &nextRuntime); // FIXME: what's a good time here? 60 Hz?

  // Check if it's time to run - and if not, return how long it will
  // be until we need to run
  struct timespec diff = tsSubtract(nextRuntime, now);
  if (diff.tv_sec > 0 || diff.tv_nsec > 0) {
    // The caller can decide to nanosleep(&diff, NULL)
    return diff;
  }

  cycleCount++;
  if (!g_biosInterrupt) {
    // If the BIOS is running, then let it handle the keyboard directly
    g_keyboard->maintainKeyboard();
    g_mouse->maintainMouse();
  }

  doDebugging();
  g_ui->drawPercentageUIElement(UIePowerPercentage, 100);

  return diff;
}

void loop()
{
  struct timespec now;
  do_gettime(&now);

  struct timespec shortest;
  shortest.tv_sec = 0;
  shortest.tv_nsec = 5000000; // 5ms idle when the VM isn't running (BIOS or printer-full)

  static bool wasBios = false; // so we can tell when it's done
  if (g_biosInterrupt) {
    shortest = runBIOS(now);
    wasBios = true;
  } else {
    if (wasBios) {
      // bios has just exited
      writePrefs();

      // if we turned off debugMode, make sure to clear the debugMsg
      if (g_debugMode == D_NONE) {
        g_display->debugMsg("");
      }
      
      // Force the display to redraw
      g_display->redraw(); // Redraw the UI
      ((AppleDisplay*)(g_vm->vmdisplay))->modeChange(); // force a full re-draw	and blit

      // The BIOS reset g_cpu->cycles to 0, and the speed may have
      // changed (possibly across the audio-mute threshold). The
      // speaker tracks cycle numbers, so resync it to the new clock.
      g_speaker->reset();

      cpuClockInitialized = false; // force it to reset so it doesn't fast-forward
      wasBios = false;
    }
  }

  if (!g_biosInterrupt && !((SDLPrinter *)g_printer)->isHalted()) {
    // Freeze the VM while the printer roll is full and waiting to be saved/cleared.
#ifdef __EMSCRIPTEN__
    // The browser calls loop() at the DISPLAY refresh rate (requestAnimationFrame),
    // which is not necessarily 60Hz (ProMotion/120Hz, 144Hz externals, etc.). Pace
    // the CPU by REAL elapsed wall-clock time so the //e stays at its true ~1MHz
    // regardless of refresh rate.
    if (!g_audioPaced) {   // audio-paced runs the CPU from the audio fill loop via aiie_run_cycles instead
      static struct timespec last = {0, 0};
      double dt;
      if (last.tv_sec == 0 && last.tv_nsec == 0) dt = 1.0 / 60.0;
      else dt = (double)(now.tv_sec - last.tv_sec) + (double)(now.tv_nsec - last.tv_nsec) * 1e-9;
      last = now;
      if (dt > 0.1) dt = 0.1;   // cap catch-up after a stall / backgrounded tab
      uint64_t target = g_cpu->cycles + (uint64_t)((double)g_speed * g_speedMult * dt);
      int guard = 0;
      while (g_cpu->cycles < target && ++guard < 4000000) {   // guard scales with the speed multiplier
        (void)g_cpu->Run(24);
        ((AppleVM *)g_vm)->cpuMaintenance(g_cpu->cycles);
      }
    }
#else
    shortest = runCPU(now); // about 13% CPU utilization on my laptop
#endif
  }
  struct timespec diff;
  diff = runDisplay(now); // about 47% CPU utilization on my laptop
  if (tsCompare(&shortest, &diff) > 0)
        shortest = diff;
  diff = runMaintenance(now); // about 1% CPU utilization on my laptop
  if (tsCompare(&shortest, &diff) > 0)
    shortest = diff;

  // If they all have time remaining then sleep until one is ready
#ifndef __EMSCRIPTEN__
  if (shortest.tv_sec || shortest.tv_nsec) {
    nanosleep(&shortest, NULL);
  }
#endif
}

bool use8875 = true;

int main(int argc, char *argv[])
{
  _init_darwin_shim();

  /* Parse the command line. Flags and positional filenames may be mixed:
   *   -8 / -9         select the 8875 / 9341 display
   *   -hd <image>     connect a hard-drive image (repeat for the 2nd HD drive)
   *   --cycle-beacon  print a cycle-count line to stderr on writes to $C074
   *   <image>         positional floppy disk image (drive 1, then drive 2)
   * The images are stashed here and actually inserted once the VM exists. */
  const char *floppy[2] = { NULL, NULL };
  const char *hd[2]     = { NULL, NULL };
  int numFloppy = 0, numHd = 0;
  bool noHd = false;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "-9")) {
      use8875 = false;
    }
    else if (!strcmp(argv[i], "-8")) {
      use8875 = true;
    }
    else if (!strcmp(argv[i], "-nohd") || !strcmp(argv[i], "--no-hd")) {
      noHd = true; // disconnect any hard drives restored from prefs
    }
    else if (!strcmp(argv[i], "-hd") || !strcmp(argv[i], "--hd")) {
      if (i + 1 >= argc) {
        fprintf(stderr, "Error: %s requires a hard-drive image filename\n", argv[i]);
        exit(1);
      }
      if (numHd >= 2) {
        fprintf(stderr, "Error: at most 2 hard drives are supported\n");
        exit(1);
      }
      hd[numHd++] = argv[++i];
    }
    else if (!strcmp(argv[i], "-cycle-beacon") || !strcmp(argv[i], "--cycle-beacon")) {
      g_cycleBeacon = true;
    }
    else if (argv[i][0] == '-') {
      fprintf(stderr, "Unknown option '%s'\n", argv[i]);
      fprintf(stderr, "Usage: %s [-8|-9] [-hd <image>] [-hd <image>] [-nohd] [--cycle-beacon] [floppy1] [floppy2]\n", argv[0]);
      exit(1);
    }
    else if (numFloppy < 2) {
      floppy[numFloppy++] = argv[i];
    }
    else {
      fprintf(stderr, "Error: at most 2 floppy disks are supported\n");
      exit(1);
    }
  }

#ifdef __EMSCRIPTEN__
  // Scope SDL's DOM keyboard listeners to the canvas only (default is the whole document, which
  // swallows keystrokes meant for the page's <textarea> editor).  We inject keys to the //e via
  // our own canvas keydown handler + aiie_inject, so SDL's keyboard is unused anyway.
  SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, "#canvas");
  SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO);
#else
  SDL_Init(SDL_INIT_EVERYTHING);
#endif

  g_speaker = new SDLSpeaker();
  g_printer = new SDLPrinter();
#ifdef __EMSCRIPTEN__
  g_uthernet = NULL;
#else
  g_uthernet = new SDLUthernet2();
#endif

  // create the filemanager - the interface to the host file system.
  g_filemanager = new NixFileManager();

  g_display = new SDLDisplay();
  //  g_displayType = m_blackAndWhite;

  g_ui = new AppleUI();

  // paddles have to be created after g_display created the window
  g_paddles = new SDLPaddles();

  // Read prefs early so slot assignments are correct before the VM is created.
  // Disk/HD image loading happens later in readPrefs() once g_vm exists.
  {
    NixPrefs np;
    prefs_t p;
    if (np.readPrefs(&p)) {
      g_volume = p.volume;
      g_displayType = p.displayType;
      g_luminanceCutoff = p.luminanceCutoff;
      g_debugMode = p.debug;
      // v8+ stores speed in the 16-bit field (needed for >= 128x)
      uint32_t speedSteps = (p.version >= 8) ? p.speed16 : p.speed;
      if (speedSteps < 1) speedSteps = 2; // 1x
      if (speedSteps > 512) speedSteps = 512; // 256x
      g_speed = speedSteps * (1023000/2);
      if (p.version >= 6) {
        g_slotDiskII = p.slotDiskII;
        g_slotParallel = p.slotParallel;
        g_slotHD32 = p.slotHD32;
        g_slotMouse = p.slotMouse;
        g_slotMockingboard = p.slotMockingboard;
      }
      if (p.version >= 7) {
        g_ramworksSize = p.ramworksSize;
      }
      if (p.version >= 9) {
        g_slotUthernet = p.slotUthernet;
      if (p.version >= 10) {
        strncpy(g_wifiSSID, p.wifiSSID, sizeof(g_wifiSSID)-1); g_wifiSSID[sizeof(g_wifiSSID)-1]=0;
        strncpy(g_wifiPass, p.wifiPass, sizeof(g_wifiPass)-1); g_wifiPass[sizeof(g_wifiPass)-1]=0;
      }
      if (p.version >= 11) {
        strncpy(g_natFwd, p.natFwd, sizeof(g_natFwd)-1); g_natFwd[sizeof(g_natFwd)-1]=0;
        g_natPortOffset = p.natPortOffset;
      }
      if (p.version >= 12) {
        strncpy(g_natSubnet, p.natSubnet, sizeof(g_natSubnet)-1); g_natSubnet[sizeof(g_natSubnet)-1]=0;
      }
      }
    }
  }

  // Headless RamWorks override for automated testing: AIIE_RW=<1|3|16> (MB).
  // Set before the VM/MMU is created so setRamworksSize() picks it up.
  if (getenv("AIIE_RW")) g_ramworksSize = (uint8_t)atoi(getenv("AIIE_RW"));
#ifdef __EMSCRIPTEN__
  if (!g_ramworksSize) g_ramworksSize = 3; // default RamWorks in the browser
#endif
  // Headless speed override for long batch runs: AIIE_SPEED=<steps> (2=1x, 512=256x).
  if (getenv("AIIE_SPEED")) g_speed = (uint32_t)atoi(getenv("AIIE_SPEED")) * (1023000/2);

  // Next create the virtual CPU. This needs the VM's MMU in order to run, but we don't have that yet.
  g_cpu = new Cpu();

  // Create the virtual machine. This may read from g_filemanager to get ROMs if necessary.
  // (The actual Apple VM we've built has them compiled in, though.) It will create its virutal
  // hardware (MMU, video driver, floppy, paddles, whatever).
  g_vm = new AppleVM();

  g_keyboard = new SDLKeyboard(g_vm->getKeyboard());
  g_mouse = new SDLMouse();

  // Now that the VM exists and it has created an MMU, we tell the CPU how to access memory through the MMU.
  g_cpu->SetMMU(g_vm->getMMU());

  // Now that all the virtual hardware is glued together, reset the VM
  g_vm->Reset();
  g_cpu->rst();

  //  g_display->blit();
  g_display->redraw();

  /* Load remaining prefs (disk images, window size) now that the VM exists */
  readPrefs();
  /* -nohd disconnects any hard drives that prefs restored (e.g. from an earlier
   * -hd run), for a clean floppy-only boot. */
  if (noHd) {
    ((AppleVM *)g_vm)->ejectHD(0);
    ((AppleVM *)g_vm)->ejectHD(1);
    printf("Hard drives disconnected\n");
  }
  /* Images named on the command line override whatever prefs restored. */
  if (floppy[0]) {
    printf("Inserting disk %s\n", floppy[0]);
    ((AppleVM *)g_vm)->insertDisk(0, floppy[0]);
    strcpy(disk1name, floppy[0]);
  }

  if (floppy[1]) {
    printf("Inserting disk %s\n", floppy[1]);
    ((AppleVM *)g_vm)->insertDisk(1, floppy[1]);
    strcpy(disk2name, floppy[1]);
  }

  if (hd[0]) {
    printf("Connecting hard drive %s\n", hd[0]);
    ((AppleVM *)g_vm)->insertHD(0, hd[0]);
  }

  if (hd[1]) {
    printf("Connecting hard drive %s\n", hd[1]);
    ((AppleVM *)g_vm)->insertHD(1, hd[1]);
  }

  nonblock(NB_ENABLE);

#ifndef __EMSCRIPTEN__
  signal(SIGINT, sigint_handler);
  signal(SIGPIPE, SIG_IGN); // debugger might have a SIGPIPE happen if the remote end drops

  atexit(writePrefs);
#endif

#ifdef __EMSCRIPTEN__
  g_volume = 13;   // no prefs UI in the browser; default to a clearly audible level (desktop uses saved prefs)
#endif
  g_speaker->begin();

  printf("Starting loop\n");
#ifdef __EMSCRIPTEN__
  emscripten_set_main_loop(loop, 0, 1);
#else
  while (1) {
    loop();
  }
#endif
}

void readPrefs()
{
  NixPrefs np;
  prefs_t p;
  if (np.readPrefs(&p)) {
    g_volume = p.volume;
    g_displayType = p.displayType;
    g_luminanceCutoff = p.luminanceCutoff;

    g_debugMode = p.debug;
    // steps of half normal speed; v8+ stores them in the 16-bit field
    // (needed for >= 128x)
    uint32_t speedSteps = (p.version >= 8) ? p.speed16 : p.speed;
    if (speedSteps < 1) speedSteps = 2; // 1x
    if (speedSteps > 512) speedSteps = 512; // 256x
    g_speed = speedSteps * (1023000/2);

    if (p.version >= 6) {
      g_slotDiskII = p.slotDiskII;
      g_slotParallel = p.slotParallel;
      g_slotHD32 = p.slotHD32;
      g_slotMouse = p.slotMouse;
      g_slotMockingboard = p.slotMockingboard;
    }
    if (p.version >= 7) {
      g_ramworksSize = p.ramworksSize;
    }
    if (p.version >= 9) {
      g_slotUthernet = p.slotUthernet;
      if (p.version >= 10) {
        strncpy(g_wifiSSID, p.wifiSSID, sizeof(g_wifiSSID)-1); g_wifiSSID[sizeof(g_wifiSSID)-1]=0;
        strncpy(g_wifiPass, p.wifiPass, sizeof(g_wifiPass)-1); g_wifiPass[sizeof(g_wifiPass)-1]=0;
      }
      if (p.version >= 11) {
        strncpy(g_natFwd, p.natFwd, sizeof(g_natFwd)-1); g_natFwd[sizeof(g_natFwd)-1]=0;
        g_natPortOffset = p.natPortOffset;
      }
      if (p.version >= 12) {
        strncpy(g_natSubnet, p.natSubnet, sizeof(g_natSubnet)-1); g_natSubnet[sizeof(g_natSubnet)-1]=0;
      }
    }
    if (p.disk1[0]) {
      ((AppleVM *)g_vm)->insertDisk(0, p.disk1);
      strcpy(disk1name, p.disk1);
    }
    if (p.disk2[0]) {
      ((AppleVM *)g_vm)->insertDisk(1, p.disk2);
      strcpy(disk2name, p.disk2);
    }

    if (p.hd1[0]) {
      ((AppleVM *)g_vm)->insertHD(0, p.hd1);
    }

    if (p.hd2[0]) {
      ((AppleVM *)g_vm)->insertHD(1, p.hd2);
    }

    ((SDLDisplay *)g_display)->setWindowSize(p.windowWidth, p.windowHeight);
  }
}

void writePrefs()
{
  NixPrefs np;
  prefs_t p;

  memset(&p, 0, sizeof(p));
  p.magic = PREFSMAGIC;
  p.prefsSize = sizeof(prefs_t);
  p.version = PREFSVERSION;

  p.volume = g_volume;

  p.displayType = g_displayType;
  p.luminanceCutoff = g_luminanceCutoff;

  p.debug = g_debugMode;
  {
    uint32_t speedSteps = g_speed / (1023000/2);
    p.speed16 = speedSteps;
    // legacy field for older readers; saturates at 127.5x
    p.speed = (speedSteps > 255) ? 255 : speedSteps;
  }

  p.slotDiskII = g_slotDiskII;
  p.slotParallel = g_slotParallel;
  p.slotHD32 = g_slotHD32;
  p.slotMouse = g_slotMouse;
  p.slotMockingboard = g_slotMockingboard;
  p.slotUthernet = g_slotUthernet;
  strncpy(p.wifiSSID, g_wifiSSID, sizeof(p.wifiSSID)); p.wifiSSID[sizeof(p.wifiSSID)-1]=0;
  strncpy(p.wifiPass, g_wifiPass, sizeof(p.wifiPass)); p.wifiPass[sizeof(p.wifiPass)-1]=0;
  strncpy(p.natFwd, g_natFwd, sizeof(p.natFwd)); p.natFwd[sizeof(p.natFwd)-1]=0;
  p.natPortOffset = g_natPortOffset;
  strncpy(p.natSubnet, g_natSubnet, sizeof(p.natSubnet)); p.natSubnet[sizeof(p.natSubnet)-1]=0;

  p.ramworksSize = g_ramworksSize;

  strcpy(p.disk1, ((AppleVM *)g_vm)->DiskName(0));
  strcpy(p.disk2, ((AppleVM *)g_vm)->DiskName(1));
  strcpy(p.hd1, ((AppleVM *)g_vm)->HDName(0));
  strcpy(p.hd2, ((AppleVM *)g_vm)->HDName(1));

  int w, h;
  SDL_GetWindowSize(((SDLDisplay *)g_display)->getWindow(), &w, &h);
  p.windowWidth = w;
  p.windowHeight = h;

  bool ret = np.writePrefs(&p);
  printf("writePrefs returns %s\n", ret ? "true" : "false");
}
