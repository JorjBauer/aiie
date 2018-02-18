#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <pthread.h>
#include <sys/vt.h>
#include <sys/ioctl.h>
#include <signal.h>

#include "applevm.h"
#include "fb-display.h"
#include "linux-keyboard.h"
#include "linux-speaker.h"
#include "fb-paddles.h"
#include "nix-filemanager.h"
#include "linux-printer.h"
#include "appleui.h"
#include "bios.h"
#include "nix-prefs.h"

#include "globals.h"

#include "timeutil.h"

//#define SHOWFPS
//#define SHOWPC
//#define DEBUGCPU
//#define SHOWMEMPAGE

BIOS bios;

static struct timespec nextInstructionTime, startTime;

#define NB_ENABLE 1
#define NB_DISABLE 0

int send_rst = 0;

pthread_t cpuThreadID;

char disk1name[256] = "\0";
char disk2name[256] = "\0";

volatile bool wantSuspend = false;
volatile bool wantResume = false;

void doDebugging();
void readPrefs();
void writePrefs();

void sigint_handler(int n)
{
  send_rst = 1;
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
  struct timespec nextCycleTime;
  uint32_t nextSpeakerCycle = 0;

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

    /* The speaker is our priority. The CPU runs in batches anyway,
       sometimes a little behind and sometimes a little ahead; but the
       speaker has to be right on time. */

    struct timespec diff;
    
#if 0
    // Wait until nextSpeakerCycle
    timespec_add_cycles(&startTime, nextSpeakerCycle, &nextCycleTime);
    
    diff = tsSubtract(nextCycleTime, currentTime);
    if (diff.tv_sec >= 0 || diff.tv_nsec >= 0) {
      nanosleep(&diff, NULL);
    }

    // Speaker runs 48 cycles behind the CPU (an arbitrary number)
    if (nextSpeakerCycle >= 48) {
      timespec_add_cycles(&startTime, nextSpeakerCycle-48, &nextCycleTime);
      uint64_t microseconds = nextCycleTime.tv_sec * 1000000 +
	(double)nextCycleTime.tv_nsec / 1000.0;
      g_speaker->maintainSpeaker(nextSpeakerCycle-48, microseconds);
    }

    // Bump speaker cycle for next go-round
    nextSpeakerCycle++;
#endif

    /* Next up is the CPU. */

    // tsSubtract doesn't return negatives; it bounds at 0.
    diff = tsSubtract(nextInstructionTime, currentTime);

    uint8_t executed = 0;
    if (diff.tv_sec == 0 && diff.tv_nsec == 0) {
#ifdef DEBUGCPU
      executed = g_cpu->Run(1);
#else
      executed = g_cpu->Run(24);
#endif
      // calculate the real time that we should be at now, and schedule
      // that as our next instruction time
      timespec_add_cycles(&startTime, g_cpu->cycles, &nextInstructionTime);

      // The paddles need to be triggered in real-time on the CPU
      // clock. That happens from the VM's CPU maintenance poller.
      ((AppleVM *)g_vm)->cpuMaintenance(g_cpu->cycles);

#ifdef DEBUGCPU
      {
	uint8_t p = g_cpu->flags;
	printf("OP: $%02x A: %02x  X: %02x  Y: %02x  PC: $%04x  SP: %02x  Flags: %c%cx%c%c%c%c%c\n",
	       g_vm->getMMU()->read(g_cpu->pc),
	       g_cpu->a, g_cpu->x, g_cpu->y, g_cpu->pc, g_cpu->sp,
	       p & (1<<7) ? 'N':' ',
	       p & (1<<6) ? 'V':' ',
	       p & (1<<4) ? 'B':' ',
	       p & (1<<3) ? 'D':' ',
	       p & (1<<2) ? 'I':' ',
	       p & (1<<1) ? 'Z':' ',
	       p & (1<<0) ? 'C':' '
	       );
      }
#endif
      
      if (send_rst) {
#if 0
	printf("Scheduling suspend request...\n");
	wantSuspend = true;
#endif
#if 0
	printf("Scheduling resume resume request...\n");
	wantResume = true;
#endif
	
#if 0
	printf("Sending reset\n");
	g_cpu->Reset();
	
	// testing startup keyboard presses - perform Apple //e self-test
	//g_vm->getKeyboard()->keyDepressed(RA);
	//g_vm->Reset();
	//g_cpu->Reset();
	//((AppleVM *)g_vm)->insertDisk(0, "disks/DIAGS.DSK");
#endif
	
#if 0
	// Swap disks
	if (disk1name[0] && disk2name[0]) {
	  printf("Swapping disks\n");
	  
	  printf("Inserting disk %s in drive 1\n", disk2name);
	  ((AppleVM *)g_vm)->insertDisk(0, disk2name);
	  printf("Inserting disk %s in drive 2\n", disk1name);
	  ((AppleVM *)g_vm)->insertDisk(1, disk1name);
	}
#endif
	
#if 0
	MMU *mmu = g_vm->getMMU();
	
	printf("PC: 0x%X\n", g_cpu->pc);
	for (int i=g_cpu->pc; i<g_cpu->pc + 0x100; i++) {
	  printf("0x%X ", mmu->read(i));
	}
	printf("\n");
	
	printf("Dropping to monitor\n");
	// drop directly to monitor.
	g_cpu->pc = 0xff69; // "call -151"
	mmu->read(0xC054); // make sure we're in page 1
	mmu->read(0xC056); // and that hires is off
	mmu->read(0xC051); // and text mode is on
	mmu->read(0xC08A); // and we have proper rom in place
	mmu->read(0xc008); // main zero-page
	mmu->read(0xc006); // rom from cards
	mmu->write(0xc002 +  mmu->read(0xc014)? 1 : 0, 0xff); // make sure aux ram read and write match
	mmu->write(0x20, 0); // text window
	mmu->write(0x21, 40);
	mmu->write(0x22, 0);
	mmu->write(0x23, 24);
	mmu->write(0x33, '>');
	mmu->write(0x48, 0);  // from 0xfb2f: part of text init
#endif
	  
	  send_rst = 0;
      }
    }
  }
}

int main(int argc, char *argv[])
{
  int fd;
  struct vt_stat vts;
  int newVT;
  int initialVT;
  
  if ((fd=open("/dev/console", O_WRONLY)) < 0) {
    perror("opening /dev/console");
    exit(1);
  }

  ioctl(fd, VT_GETSTATE, &vts);
  initialVT = vts.v_active; // find what VT we were on originally
  ioctl(fd, VT_OPENQRY, &newVT);
  if (newVT == -1) {
    printf("No VTs available");
    exit(1);
  }

  // Switch to new VT
  ioctl(fd, VT_ACTIVATE, newVT);
  ioctl(0, VT_WAITACTIVE, newVT);

  printf("Now on VT %d\n", newVT);
  
  // If we want stdout/stderr to move with us to the new VT, do this sorta thing, but to the right TTY
  //  freopen("/dev/tty2", "w", stdout);
  //  freopen("/dev/tty2", "w", stderr);
  
  // Turn off cursor
  system("echo 0 > /sys/class/graphics/fbcon/cursor_blink");
  
#if 0
  // Timing consistency check

  sleep(2); // kinda random, hopefully sloppy? - to make startTime != 0,0
  printf("starting time consistency check\n");
  do_gettime(&startTime);
  for (int i=0; i<10000000; i++) {

    // Calculate the time delta from startTime to cycle # i
    timespec_add_cycles(&startTime, i, &nextInstructionTime);

    // Recalculate the time difference between nextInstructionTime and startTime
    struct timespec runtime = tsSubtract(nextInstructionTime, startTime);

    // See if it's the same as cycles_since_time
    double guesstimate = cycles_since_time(&runtime);
    printf("cycle %d guesstimate %f\n", i, guesstimate);
    if (guesstimate != i) {
      printf("FAILED: cycle %d has guesstimate %f\n", i, guesstimate);
      exit(1);
    }
  }

  printf("All ok\n");

  exit(1);
#endif

  g_speaker = new LinuxSpeaker();
  g_printer = new LinuxPrinter();

  // create the filemanager - the interface to the host file system.
  g_filemanager = new NixFileManager();

  g_display = new FBDisplay();
  //  g_displayType = m_blackAndWhite;

  g_ui = new AppleUI();

  // paddles have to be created after g_display created the window
  g_paddles = new FBPaddles();

  // Next create the virtual CPU. This needs the VM's MMU in order to run, but we don't have that yet.
  g_cpu = new Cpu();

  // Create the virtual machine. This may read from g_filemanager to get ROMs if necessary.
  // (The actual Apple VM we've built has them compiled in, though.) It will create its virutal 
  // hardware (MMU, video driver, floppy, paddles, whatever).
  g_vm = new AppleVM();

  g_keyboard = new LinuxKeyboard(g_vm->getKeyboard());

  // Now that the VM exists and it has created an MMU, we tell the CPU how to access memory through the MMU.
  g_cpu->SetMMU(g_vm->getMMU());

  // Now that all the virtual hardware is glued together, reset the VM
  g_vm->Reset();
  g_cpu->rst();

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

  printf("creating CPU thread\n");
  if (!pthread_create(&cpuThreadID, NULL, &cpu_thread, (void *)NULL)) {
    printf("thread created\n");
    //    pthread_setschedparam(cpuThreadID, SCHED_RR, PTHREAD_MAX_PRIORITY);
  }

  while (1) {
    if (g_biosInterrupt) {
      printf("Invoking BIOS\n");
      if (bios.runUntilDone()) {
	// if it returned true, we have something to store
	// persistently in EEPROM.
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

      // Drain the speaker queue (FIXME: a little hacky)
      g_speaker->maintainSpeaker(-1, -1);

      /* FIXME                                                                  
      // Force the display to redraw                                            
      ((AppleDisplay*)(g_vm->vmdisplay))->modeChange();                         
      */

      // Poll the keyboard before we start, so we can do selftest on startup
      g_keyboard->maintainKeyboard();
    }
    
    
    static uint32_t usleepcycles = 16384; // step-down for display drawing. Dynamically updated based on FPS calculations.

    // fill disk buffer when needed
    ((AppleVM*)g_vm)->disk6->fillDiskBuffer();

    g_ui->blit();
    if (g_vm->vmdisplay->needsRedraw()) {
      AiieRect what = g_vm->vmdisplay->getDirtyRect();
      // make sure to clear the flag before drawing; there's no lock
      // on didRedraw, so the other thread might update it
      g_vm->vmdisplay->didRedraw();
      g_display->blit(what);
    }

    g_printer->update();
    g_keyboard->maintainKeyboard();
    g_ui->drawPercentageUIElement(UIePowerPercentage, 100);

    doDebugging();
    
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

      if (fps > 60) {
	usleepcycles *= 2;
      } else if (fps < 40) {
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
    sprintf(buf, "%X", g_cpu->cycles);
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
    g_prioritizeDisplay = p.priorityMode;
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
  p.priorityMode = g_prioritizeDisplay;
  p.speed = g_speed / (1023000/2);
  strcpy(p.disk1, ((AppleVM *)g_vm)->DiskName(0));
  strcpy(p.disk2, ((AppleVM *)g_vm)->DiskName(1));
  strcpy(p.hd1, ((AppleVM *)g_vm)->HDName(0));
  strcpy(p.hd2, ((AppleVM *)g_vm)->HDName(1));

  bool ret = np.writePrefs(&p);
  printf("writePrefs returns %s\n", ret ? "true" : "false");
}
