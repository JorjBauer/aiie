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
#include "sdl-filemanager.h"
#include "sdl-printer.h"

#include "globals.h"

#include "timeutil.h"

//#define SHOWFPS
//#define SHOWPC
//#define DEBUGCPU
//#define SHOWMEMPAGE

static struct timespec nextInstructionTime, startTime;
uint64_t hitcount = 0;
uint64_t misscount = 0;

#define NB_ENABLE 1
#define NB_DISABLE 0

int send_rst = 0;

pthread_t cpuThreadID;

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

#if 0
  int policy;
  struct sched_param param;
  pthread_getschedparam(pthread_self(), &policy, &param);
  param.sched_priority = sched_get_priority_max(policy);
  pthread_setschedparam(pthread_self(), policy, &param);
#endif
    
  _init_darwin_shim();
  do_gettime(&startTime);
  do_gettime(&nextInstructionTime);

  printf("free-running\n");
  while (1) {
    // cycle down the CPU...
    do_gettime(&currentTime);
    struct timespec diff = tsSubtract(nextInstructionTime, currentTime);
    if (diff.tv_sec >= 0 && diff.tv_nsec >= 0) {
      hitcount++;
      nanosleep(&diff, NULL);
    } else {
      misscount++;
    }

#ifdef DEBUGCPU
    uint8_t executed = g_cpu->Run(1);
#else
    uint8_t executed = g_cpu->Run(24);
#endif
    timespec_add_cycles(&startTime, g_cpu->cycles + executed, &nextInstructionTime);

    g_speaker->beginMixing();

    // The paddles need to be triggered in real-time on the CPU
    // clock. That happens from the VM's CPU maintenance poller.
    ((AppleVM *)g_vm)->cpuMaintenance(g_cpu->cycles);

    // cpuMaintenance also maintained the sound card; update the speaker after
    g_speaker->maintainSpeaker(g_cpu->cycles);
    
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
#if 1
      printf("Sending reset\n");
      g_cpu->Reset();
      
      // testing startup keyboard presses - perform Apple //e self-test
      //g_vm->getKeyboard()->keyDepressed(RA);
      //g_vm->Reset();
      //g_cpu->Reset();
      //((AppleVM *)g_vm)->insertDisk(0, "disks/DIAGS.DSK");
      
#else
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

int main(int argc, char *argv[])
{
  SDL_Init(SDL_INIT_EVERYTHING);

  g_speaker = new SDLSpeaker();
  g_printer = new SDLPrinter();

  // create the filemanager - the interface to the host file system.
  g_filemanager = new SDLFileManager();

  g_display = new SDLDisplay();

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

  if (argc >= 2) {
    printf("Inserting disk %s\n", argv[1]);
    ((AppleVM *)g_vm)->insertDisk(0, argv[1]);
  }

  if (argc == 3) {
    printf("Inserting disk %s\n", argv[2]);
    ((AppleVM *)g_vm)->insertDisk(1, argv[2]);
  }
  
  nonblock(NB_ENABLE);

  signal(SIGINT, sigint_handler);

  printf("creating CPU thread\n");
  if (!pthread_create(&cpuThreadID, NULL, &cpu_thread, (void *)NULL)) {
    printf("thread created\n");
    //    pthread_setschedparam(cpuThreadID, SCHED_RR, PTHREAD_MAX_PRIORITY);
  }

  while (1) {
    static uint32_t ctr = 0;
    if (++ctr == 0) {
      printf("hit: %llu; miss: %llu; pct: %f\n", hitcount, misscount, (double)misscount / (double)(misscount + hitcount));
    }

    // fill disk buffer when needed
    ((AppleVM*)g_vm)->disk6->fillDiskBuffer();

    // Make this a little friendlier, and the expense of some framerate?
    // usleep(10000);
    if (g_vm->vmdisplay->needsRedraw()) {
      AiieRect what = g_vm->vmdisplay->getDirtyRect();
      // make sure to clear the flag before drawing; there's no lock
      // on didRedraw, so the other thread might update it
      g_vm->vmdisplay->didRedraw();
      g_display->blit(what);
    }

    g_printer->update();
    g_keyboard->maintainKeyboard();
    g_display->drawBatteryStatus(100);

#ifdef SHOWFPS
    static time_t startAt = time(NULL);
    static uint32_t loopCount = 0;
    loopCount++;

    time_t lenSecs = time(NULL) - startAt;
    if (lenSecs >= 10) {
      char buf[25];
      sprintf(buf, "%lu FPS", loopCount / lenSecs);
      g_display->debugMsg(buf);
      if (lenSecs >= 60) {
	startAt = time(NULL);
	loopCount = 0;
      }
    }
#endif
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

