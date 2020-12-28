#include <stdio.h>
#include <unistd.h>
#include <curses.h>
#include <termios.h>
#include <pthread.h>

#include "applevm.h"
#include "sdl-display.h"
#include "sdl-keyboard.h"
#include "sdl-speaker.h"
#include "sdl-paddles.h"
#include "nix-filemanager.h"
#include "sdl-printer.h"
#include "appleui.h"
#include "bios.h"
#include "nix-prefs.h"
#include "debugger.h"

#include "globals.h"

#include "timeutil.h"

BIOS bios;
Debugger debugger;

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

  timespec_add_us(&startTime, 100000*cycleCount, &nextRuntime); // FIXME: what's a good time here? 1/10 sec?

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
  uint8_t executed = 0;
  if (debugger.active()) {
    // With the debugger running, we need to single-step through
    // instructions.
    executed = g_cpu->Run(1);
  } else {
    // Otherwise we can run a bunch of instructions at once to
    // save on the overhead.
    executed = g_cpu->Run(24);
  }

  // The paddles need to be triggered in real-time on the CPU
  // clock. That happens from the VM's CPU maintenance poller.
  ((AppleVM *)g_vm)->cpuMaintenance(g_cpu->cycles);
  
  if (debugger.active()) {
    debugger.step();
    // FIXME need to reset starttime for this and g_cpu->cycles
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
      AiieRect what = g_vm->vmdisplay->getDirtyRect();
      g_vm->vmdisplay->didRedraw();
      g_display->blit(what);
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
	sprintf(buf, "%u FPS", loopCount / lenSecs);
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
    sprintf(buf, "%u %u", g_paddles->paddle0(), g_paddles->paddle1());
    g_display->debugMsg(buf);
    break;
  case D_SHOWPC:
    sprintf(buf, "%X", g_cpu->pc);
    g_display->debugMsg(buf);
    break;
  case D_SHOWCYCLES:
    sprintf(buf, "%llX", g_cpu->cycles);
    g_display->debugMsg(buf);
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

  timespec_add_us(&startTime, 100000*cycleCount, &nextRuntime); // FIXME: what's a good time here? 1/10 sec?

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

      cpuClockInitialized = false; // force it to reset so it doesn't fast-forward
      wasBios = false;
    }
  }

  if (!g_biosInterrupt) {
    shortest = runCPU(now); // about 13% CPU utilization on my laptop
  }
  struct timespec diff;
  diff = runDisplay(now); // about 47% CPU utilization on my laptop
  if (tsCompare(&shortest, &diff) > 0)
        shortest = diff;
  diff = runMaintenance(now); // about 1% CPU utilization on my laptop
  if (tsCompare(&shortest, &diff) > 0)
    shortest = diff;

  // If they all have time remaining then sleep until one is ready
  if (shortest.tv_sec || shortest.tv_nsec) {
    nanosleep(&shortest, NULL);
  }
}

int main(int argc, char *argv[])
{
  _init_darwin_shim();
  
  SDL_Init(SDL_INIT_EVERYTHING);

  g_speaker = new SDLSpeaker();
  g_printer = new SDLPrinter();

  // create the filemanager - the interface to the host file system.
  g_filemanager = new NixFileManager();

  g_display = new SDLDisplay();
  //  g_displayType = m_blackAndWhite;

  g_ui = new AppleUI();

  // paddles have to be created after g_display created the window
  g_paddles = new SDLPaddles();

  // Next create the virtual CPU. This needs the VM's MMU in order to run, but we don't have that yet.
  g_cpu = new Cpu();

  // Create the virtual machine. This may read from g_filemanager to get ROMs if necessary.
  // (The actual Apple VM we've built has them compiled in, though.) It will create its virutal 
  // hardware (MMU, video driver, floppy, paddles, whatever).
  g_vm = new AppleVM();

  g_keyboard = new SDLKeyboard(g_vm->getKeyboard());

  // Now that the VM exists and it has created an MMU, we tell the CPU how to access memory through the MMU.
  g_cpu->SetMMU(g_vm->getMMU());

  // Now that all the virtual hardware is glued together, reset the VM
  g_vm->Reset();
  g_cpu->rst();

  //  g_display->blit();
  g_display->redraw();

  /* Load prefs & reset globals appropriately now */
  readPrefs();

  if (argc >= 2) {
    printf("Inserting disk %s\n", argv[1]);
    ((AppleVM *)g_vm)->insertDisk(0, argv[1]);
    strcpy(disk1name, argv[1]);
  }

  if (argc == 3) {
    printf("Inserting disk %s\n", argv[2]);
    ((AppleVM *)g_vm)->insertDisk(1, argv[2]);
    strcpy(disk2name, argv[2]);
  }

  // FIXME: fixed test disk...
  //  ((AppleVM *)g_vm)->insertHD(0, "hd32.img");
  
  nonblock(NB_ENABLE);

  signal(SIGINT, sigint_handler);
  signal(SIGPIPE, SIG_IGN); // debugger might have a SIGPIPE happen if the remote end drops

  g_speaker->begin();

  printf("Starting loop\n");
  while (1) {
    loop();
  }
}

void readPrefs()
{
  NixPrefs np;
  prefs_t p;
  if (np.readPrefs(&p)) {
    g_volume = p.volume;
    g_displayType = p.displayType;
    g_debugMode = p.debug;
    g_speed = (p.speed * (1023000/2)); // steps of half normal speed
    if (g_speed < (1023000/2))
      g_speed = (1023000/2);
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
  }
}

void writePrefs()
{
  NixPrefs np;
  prefs_t p;
  
  p.magic = PREFSMAGIC;
  p.prefsSize = sizeof(prefs_t);
  p.version = PREFSVERSION;

  p.volume = g_volume;
  p.displayType = g_displayType;
  p.debug = g_debugMode;
  p.speed = g_speed / (1023000/2);
  strcpy(p.disk1, ((AppleVM *)g_vm)->DiskName(0));
  strcpy(p.disk2, ((AppleVM *)g_vm)->DiskName(1));
  strcpy(p.hd1, ((AppleVM *)g_vm)->HDName(0));
  strcpy(p.hd2, ((AppleVM *)g_vm)->HDName(1));

  bool ret = np.writePrefs(&p);
  printf("writePrefs returns %s\n", ret ? "true" : "false");
}
