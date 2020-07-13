#include <Arduino.h>
#include <Audio.h>
#include <SPI.h>
#include <TimeLib.h>
#include <Bounce2.h>
#include "bios.h"
#include "cpu.h"
#include "applevm.h"
#include "teensy-display.h"
#include "teensy-keyboard.h"
#include "teensy-speaker.h"
#include "teensy-paddles.h"
#include "teensy-filemanager.h"
#include "teensy-usb.h"
#include "appleui.h"
#include "teensy-prefs.h"
#include "teensy-println.h"

//#define DEBUG_TIMING

#define THREADED if (1)

#if F_CPU < 240000000
#pragma AiiE warning: performance will improve if you overclock the Teensy to 240MHz (F_CPU=240MHz) or 256MHz (F_CPU=256MHz)
#endif

#define RESETPIN 38
#define DEBUGPIN 23

#include "globals.h"
#include "teensy-crash.h"

BIOS bios;

// How many microseconds per cycle
#define SPEEDCTL ((float)1000000/(float)g_speed)

static   time_t getTeensy3Time() {  return Teensy3Clock.get(); }

TeensyUSB usb;

Bounce resetButtonDebouncer = Bounce();

void onKeypress(int unicode)
{
  /*
shift/control/command are automatically applied
caps lock is oemkey 57
  set the keyboard LED w/ ::capsLock(bool)
modifiers are <<8 bits for the right side:
  command: 0x08; option/alt: 0x04; shift: 0x02; control: 0x01
F1..F12 are 194..205
Arrows: l/r/u/d 216/215/218/217
Delete: 127 (control-delete is 31)
home/pgup/down/delete/end: 210,211,214,212,213
numlock: oem 83
keypad: 210..218 as arrows &c, or digit ascii values w/ numlock on
  enter: 10
   */

  //  vmkeyboard->keyDepressed(keypad.key[i].kchar);
}

void onKeyrelease(int unicode)
{
  //  vmkeyboard->keyReleased(keypad.key[i].kchar);
}

void setup()
{
  Serial.begin(230400);
#if 1
  // Wait for USB serial connection before booting while debugging
  while (!Serial) {
    yield();
  }
#endif
  delay(120); // let the power settle

  pinMode(DEBUGPIN, OUTPUT); // for debugging

//  enableFaultHandler();
//  SCB_SHCSR |= SCB_SHCSR_BUSFAULTENA | SCB_SHCSR_USGFAULTENA | SCB_SHCSR_MEMFAULTENA;


  // set the Time library to use Teensy 3.0's RTC to keep time
  setSyncProvider(getTeensy3Time);
  delay(100); // don't know if we need this
  if (timeStatus() == timeSet) {
    println("RTC set from Teensy");
  } else {
    println("Error while setting RTC");
  }

  pinMode(RESETPIN, INPUT);
  digitalWrite(RESETPIN, HIGH);

  analogReadRes(8); // We only need 8 bits of resolution (0-255) for paddles
  analogReadAveraging(4); // ?? dunno if we need this or not.
  
  println("creating virtual hardware");
  g_speaker = new TeensySpeaker(18, 19); // FIXME abstract constants

  println(" fm");
  // First create the filemanager - the interface to the host file system.
  g_filemanager = new TeensyFileManager();

  // Construct the interface to the host display. This will need the
  // VM's video buffer in order to draw the VM, but we don't have that
  // yet. 
  println(" display");
  g_display = new TeensyDisplay();

  println(" UI");
  g_ui = new AppleUI();

  // Next create the virtual CPU. This needs the VM's MMU in order to
  // run, but we don't have that yet.
  println(" cpu");
  g_cpu = new Cpu();

  println(" usb");
  usb.init();
  usb.attachKeypress(onKeypress);
  usb.attachKeyrelease(onKeyrelease);

  // Create the virtual machine. This may read from g_filemanager to
  // get ROMs if necessary.  (The actual Apple VM we've built has them
  // compiled in, though.) It will create its virutal hardware (MMU,
  // video driver, floppy, paddles, whatever).
  println(" vm");
  Serial.flush();
  g_vm = new AppleVM();

  // Now that the VM exists and it has created an MMU, we tell the CPU
  // how to access memory through the MMU.
  println("  [setMMU]");
  g_cpu->SetMMU(g_vm->getMMU());

  // And the physical keyboard needs hooks in to the virtual keyboard...
  println(" keyboard");
  g_keyboard = new TeensyKeyboard(g_vm->getKeyboard());

  println(" paddles");
  g_paddles = new TeensyPaddles(A3, A2, g_invertPaddleX, g_invertPaddleY);

  // Now that all the virtual hardware is glued together, reset the VM
  println("Resetting VM");
  g_vm->Reset();

  println("Reading prefs");
  readPrefs(); // read from eeprom and set anything we need setting

  // Debugging: insert a disk on startup...
  //((AppleVM *)g_vm)->insertDisk(0, "/A2DISKS/UTIL/mock2dem.dsk", false);
  //((AppleVM *)g_vm)->insertDisk(0, "/A2DISKS/JORJ/disk_s6d1.dsk", false);
  //  ((AppleVM *)g_vm)->insertDisk(0, "/A2DISKS/GAMES/ALIBABA.DSK", false);

  resetButtonDebouncer.attach(RESETPIN);
  resetButtonDebouncer.interval(5); // ms

  println("Drawing UI border");
  g_display->redraw();
  
  println("free-running");
  Serial.flush();

//  threads.setMicroTimer(); // use a 100uS timer instead of a 1mS timer
  //  threads.setSliceMicros(5);
//  threads.addThread(runDebouncer);
}

// FIXME: move these memory-related functions elsewhere...

// This only gives you an estimated free mem size. It's not perfect.
uint32_t FreeRamEstimate()
{
  uint32_t stackTop;
  uint32_t heapTop;

  // current position of the stack.
  stackTop = (uint32_t) &stackTop;

  // current position of heap.
  void* hTop = malloc(1);
  heapTop = (uint32_t) hTop;
  free(hTop);

  // The difference is the free, available ram.
  return stackTop - heapTop;
}

#include "malloc.h"

int heapSize(){
  return mallinfo().uordblks;
}

void biosInterrupt()
{
  // wait for the interrupt button to be released
  while (!resetButtonDebouncer.read())
    ;

  // invoke the BIOS
  if (bios.runUntilDone()) {
    // if it returned true, we have something to store persistently in EEPROM.
    writePrefs();

    // Also might have changed the paddles state
    TeensyPaddles *tmp = (TeensyPaddles *)g_paddles;
    tmp->setRev(g_invertPaddleX, g_invertPaddleY);
  }

  // if we turned off debugMode, make sure to clear the debugMsg
  if (g_debugMode == D_NONE) {
    g_display->debugMsg("");
  }

  // Drain the speaker queue (FIXME: a little hacky)
  g_speaker->maintainSpeaker(-1, -1);

  // Force the display to redraw
  g_display->redraw(); // Redraw the UI
  ((AppleDisplay*)(g_vm->vmdisplay))->modeChange(); // force a full re-draw and blit

  // Poll the keyboard before we start, so we can do selftest on startup
  g_keyboard->maintainKeyboard();
}

void runMaintenance(uint32_t now)
{
  static uint32_t nextRuntime = 0;
  
  THREADED {
    if (now >= nextRuntime) {
      nextRuntime = now + 100000; // FIXME: what's a good time here? 1/10 sec?

      if (!resetButtonDebouncer.read()) {
	// This is the BIOS interrupt. We immediately act on it.
	biosInterrupt();
      }
      
      g_keyboard->maintainKeyboard();
      usb.maintain();
    }
  }
}

#define TARGET_FPS 30
void runDisplay(uint32_t now)
{
  // When do we want to reset our expectation of "normal"?
  static uint32_t nextResetMicros = 0;
  // how many full display refreshes have we managed in this second?
  static uint32_t refreshCount = 0;
  // how many micros until the next frame refresh?
  static uint32_t microsAtStart = 0;
  static uint32_t microsForNext = micros();
  static uint32_t lastFps = 0;
  
  THREADED {
    // If it's time to draw the next frame, then do so
    if (now >= microsForNext) {
      refreshCount++;
      microsForNext = microsAtStart + (1000000.0*((float)refreshCount/(float)TARGET_FPS));

      doDebugging(lastFps);
      
      g_ui->blit();
      g_vm->vmdisplay->lockDisplay();
      if (g_vm->vmdisplay->needsRedraw()) { // necessary for the VM to redraw
	// Used to get the dirty rect and blit just that rect. Could still do,
	// but instead, I'm just wildly wasting resources. MWAHAHAHA
	//    AiieRect what = g_vm->vmdisplay->getDirtyRect();
	g_vm->vmdisplay->didRedraw();
	//    g_display->blit(what);
      }
      g_display->blit(); // Blit the whole thing, including UI area
	g_vm->vmdisplay->unlockDisplay();
    }
    
    // Once a second, start counting all over again
    if (now >= nextResetMicros) {
      lastFps = refreshCount;
#ifdef DEBUG_TIMING
      println("Display running at ", lastFps, " FPS");
#endif
      nextResetMicros = now + 1000000;
      refreshCount = 0;
      microsAtStart = now;
      microsForNext = microsAtStart + (1000000.0*((float)refreshCount/(float)TARGET_FPS));
    }
  }
}

// The debouncer is used in the bios, which blocks the main loop
// execution; so this thread updates the debouncer instead.
void runDebouncer()
{
  static uint32_t nextRuntime = 0;
  while (1) {
    if (millis() >= nextRuntime) {
      nextRuntime = millis() + 10;
      resetButtonDebouncer.update();
    } else {
    yield();
//      threads.yield();
    }
  }
}

void runCPU(uint32_t now)
{
  static uint32_t nextResetMicros = 0;
  static uint32_t countSinceLast = 0;
  static uint32_t microsAtStart = micros();
  static uint32_t microsForNext = microsAtStart + (countSinceLast * SPEEDCTL);
  
  THREADED {
    if (now >= microsForNext) {
      countSinceLast += g_cpu->Run(24); // The CPU runs in bursts of cycles. This '24' is the max burst we perform.
      ((AppleVM *)g_vm)->cpuMaintenance(g_cpu->cycles);

      microsForNext = microsAtStart + (countSinceLast * SPEEDCTL);
    }

    if (now >= nextResetMicros) {
      nextResetMicros = now + 1000000;
#ifdef DEBUG_TIMING
      float pct = (100.0 * (float)countSinceLast) / (float)g_speed;
      sprintf(debugBuf, "CPU running at %f%%", pct);
      println(debugBuf);
#endif      
      countSinceLast = 0;
      microsAtStart = now;
      microsForNext = microsAtStart + (countSinceLast * SPEEDCTL);
    }
  }
}

void loop()
{
  uint32_t now = micros();
  runCPU(now);
  runDisplay(now);
  runMaintenance(now);
}

void doDebugging(uint32_t lastFps)
{
  switch (g_debugMode) {
  case D_SHOWFPS:
    sprintf(debugBuf, "%lu FPS", lastFps);
    g_display->debugMsg(debugBuf);
    break;
  case D_SHOWMEMFREE:
    sprintf(debugBuf, "%lu %u", FreeRamEstimate(), heapSize());
    g_display->debugMsg(debugBuf);
    break;
  case D_SHOWPADDLES:
    sprintf(debugBuf, "%u %u", g_paddles->paddle0(), g_paddles->paddle1());
    g_display->debugMsg(debugBuf);
    break;
  case D_SHOWPC:
    sprintf(debugBuf, "%X", g_cpu->pc);
    g_display->debugMsg(debugBuf);
    break;
  case D_SHOWCYCLES:
    sprintf(debugBuf, "%lX", g_cpu->cycles);
    g_display->debugMsg(debugBuf);
    break;
  case D_SHOWTIME:
    sprintf(debugBuf, "%.2d:%.2d:%.2d", hour(), minute(), second());
    g_display->debugMsg(debugBuf);
    break;
  case D_SHOWDSK:
    {
      uint8_t sd = ((AppleVM *)g_vm)->disk6->selectedDrive();
      sprintf(debugBuf, "s %d t %d",
	      sd,
	      ((AppleVM *)g_vm)->disk6->headPosition(sd));
      g_display->debugMsg(debugBuf);
    }
    break;
  }
}

void readPrefs()
{
  TeensyPrefs np;
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
    }
    if (p.disk2[0]) {
      ((AppleVM *)g_vm)->insertDisk(1, p.disk2);
    }

    if (p.hd1[0]) {
      ((AppleVM *)g_vm)->insertHD(0, p.hd1);
    }

    if (p.hd2[0]) {
      ((AppleVM *)g_vm)->insertHD(1, p.hd2);
    }
  }

  g_invertPaddleX = p.invertPaddleX;
  g_invertPaddleY = p.invertPaddleY;

  // Update the paddles with the new inversion state
  ((TeensyPaddles *)g_paddles)->setRev(g_invertPaddleX, g_invertPaddleY);
}

void writePrefs()
{
  TeensyPrefs np;
  prefs_t p;

  p.magic = PREFSMAGIC;
  p.prefsSize = sizeof(prefs_t);
  p.version = PREFSVERSION;

  p.invertPaddleX = g_invertPaddleX;
  p.invertPaddleY = g_invertPaddleY;

  p.volume = g_volume;
  p.displayType = g_displayType;
  p.debug = g_debugMode;
  p.speed = g_speed / (1023000/2);
  strcpy(p.disk1, ((AppleVM *)g_vm)->DiskName(0));
  strcpy(p.disk2, ((AppleVM *)g_vm)->DiskName(1));
  strcpy(p.hd1, ((AppleVM *)g_vm)->HDName(0));
  strcpy(p.hd2, ((AppleVM *)g_vm)->HDName(1));

  bool ret = np.writePrefs(&p);
}
