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

//#define SHOWFPS
//#define SHOWPC
//#define SHOWMEMPAGE

BIOS bios;
Debugger debugger;

struct timespec nextInstructionTime, startTime;

#define NB_ENABLE 1
#define NB_DISABLE 0

int send_rst = 0;

pthread_t cpuThreadID;

char disk1name[256] = "\0";
char disk2name[256] = "\0";

volatile bool wantSuspend = false;
volatile bool wantResume = false;

volatile bool cpuDebuggerRunning = false;

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

static void *cpu_thread(void *dummyptr) {
  struct timespec currentTime;

#if 0
  int policy;
  struct sched_param param;
  pthread_getschedparam(pthread_self(), &policy, &param);
  param.sched_priority = sched_get_priority_max(policy);
  pthread_setschedparam(pthread_self(), policy, &param);
#endif
    
  _init_darwin_shim();
  do_gettime(&startTime);
      printf("Start time: %lu,%lu\n", startTime.tv_sec, startTime.tv_nsec);
  do_gettime(&nextInstructionTime);

  printf("free-running\n");

  // In this loop, we determine when the next CPU event is; sleep until 
  // that event; and then perform the event. There are also peripheral 
  // maintenance calls embedded in the loop...

  while (1) {
    if (g_biosInterrupt) {
      printf("BIOS blocking\n");
      while (g_biosInterrupt) {
	usleep(100);
      }
      printf("BIOS block complete\n");
    }

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

    do_gettime(&currentTime);

    // Determine the next CPU runtime (nextInstructionTime)
    timespec_add_cycles(&startTime, g_cpu->cycles, &nextInstructionTime);

    // Sleep until the CPU is ready to run.

    // tsSubtract doesn't return negatives; it bounds at zero. So if
    // either result is zero then it's time to run something.

    struct timespec cpudiff = tsSubtract(nextInstructionTime, currentTime);

    if (cpudiff.tv_sec > 0 || cpudiff.tv_nsec > 0) {
      // Sleep until the it's ready and loop...
      nanosleep(&cpudiff, NULL);
      continue;
    }

    if (cpudiff.tv_sec == 0 && cpudiff.tv_nsec == 0) {
      // Run the CPU; it's caught up to "real time"

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
      }

      if (send_rst) {
	cpuDebuggerRunning = true;
	
	printf("Sending reset\n");
	g_cpu->Reset();
	
	send_rst = 0;
      }
    }
  }
}

int main(int argc, char *argv[])
{
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

  printf("creating CPU thread\n");
  if (!pthread_create(&cpuThreadID, NULL, &cpu_thread, (void *)NULL)) {
    printf("thread created\n");
    //    pthread_setschedparam(cpuThreadID, SCHED_RR, PTHREAD_MAX_PRIORITY);
  }

  g_speaker->begin();

  int64_t lastCycleCount = -1;
  while (1) {

    if (g_biosInterrupt) {
      printf("Invoking BIOS\n");
      if (bios.runUntilDone()) {
	// if it returned true, we have something to store persistently in EEPROM.
	writePrefs();
      }
      printf("BIOS done\n");
      
      // if we turned off debugMode, make sure to clear the debugMsg
      if (g_debugMode == D_NONE) {
	g_display->debugMsg("");
      }

      g_biosInterrupt = false;

      // clear the CPU next-step counters
      g_cpu->cycles = 0;
      do_gettime(&startTime);
      do_gettime(&nextInstructionTime);

      // FIXME: drain whatever's in the speaker queue

      /* FIXME
      // Force the display to redraw
      ((AppleDisplay*)(g_vm->vmdisplay))->modeChange();
      */

      // Poll the keyboard before we start, so we can do selftest on startup
      g_keyboard->maintainKeyboard();
    }


    static int64_t usleepcycles = 16384*4; // step-down for display drawing. Dynamically updated based on FPS calculations.

    if (g_vm->vmdisplay->needsRedraw()) {
      AiieRect what = g_vm->vmdisplay->getDirtyRect();
      // make sure to clear the flag before drawing; there's no lock
      // on didRedraw, so the other thread might update it
      g_vm->vmdisplay->didRedraw();
      g_display->blit(what);
    }
    g_ui->blit();

    g_printer->update();
    g_keyboard->maintainKeyboard();

    doDebugging();

    g_ui->drawPercentageUIElement(UIePowerPercentage, 100);

    // calculate FPS & dynamically step up/down as necessary
    static time_t startAt = time(NULL);
    static uint32_t loopCount = 0;
    loopCount++;
    uint32_t lenSecs = time(NULL) - startAt;
    if (lenSecs >= 5) {
      float fps = loopCount / lenSecs;

#ifdef SHOWFPS
      char buf[25];
      sprintf(buf, "%f FPS [delay %u]", fps, usleepcycles);
      g_display->debugMsg(buf);
#endif

      if (fps > 30 && usleepcycles < 0x3FFFFFFF) {
	usleepcycles *= 2;
      } else if (fps < 20 && usleepcycles > 0xF) {
	usleepcycles /= 2;
      }

      // reset the counter & we'll adjust again in 5 seconds
      loopCount = 0;
      startAt = time(NULL);
    }
    if (usleepcycles >= 2) {
      usleep(usleepcycles);
    }

#ifdef SHOWPC
    {
      char buf[25];
      sprintf(buf, "%X", g_cpu->pc);
      g_display->debugMsg(buf);
    }
#endif
#ifdef SHOWMEMPAGE
    { 
      char buf[40];
      sprintf(buf, "AUX %c/%c BNK %d BSR %c/%c ZP %c 80 %c INT %c",
	      g_vm->auxRamRead?'R':'_',
	      g_vm->auxRamWrite?'W':'_',
	      g_vm->bank1,
	      g_vm->readbsr ? 'R':'_',
	      g_vm->writebsr ? 'W':'_',
	      g_vm->altzp ? 'Y':'_',
	      g_vm->_80store ? 'Y' : '_',
	      g_vm->intcxrom ? 'Y' : '_');
      g_display->debugMsg(buf);
    }

#endif

    if (g_cpu->cycles == lastCycleCount) {
      // If the CPU didn't advance during our last loop, then delay
      // here; there can't be any substantial updates, so no need to
      // beat up the host machine

      usleep(100000);
    } else {
      lastCycleCount = g_cpu->cycles;
    }

  }
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
