#include <Arduino.h>
#include <SPI.h>
#include <TimeLib.h>
#include <TimerOne.h>
#include "bios.h"
#include "cpu.h"
#include "applevm.h"
#include "teensy-display.h"
#include "teensy-keyboard.h"
#include "teensy-speaker.h"
#include "teensy-paddles.h"
#include "teensy-filemanager.h"
#include "appleui.h"
#include "teensy-prefs.h"
#include "teensy-println.h"

#if F_CPU < 240000000
#pragma AiiE warning: performance will improve if you overclock the Teensy to 240MHz (F_CPU=240MHz) or 256MHz (F_CPU=256MHz)
#endif

#define RESETPIN 39
#define BATTERYPIN 32
#define SPEAKERPIN A21

#include "globals.h"
#include "teensy-crash.h"

uint32_t nextInstructionMicros;
uint32_t startMicros;

BIOS bios;

// How many microseconds per cycle
#define SPEEDCTL ((float)1000000/(float)g_speed)

static   time_t getTeensy3Time() {  return Teensy3Clock.get(); }

#define ESP_TXD 51
#define ESP_CHPD 52
#define ESP_RST 53
#define ESP_RXD 40
#define ESP_GPIO0 41
#define ESP_GPIO2 42

void setup()
{
  Serial.begin(230400);
  /*
  while (!Serial) {
    yield();
  }*/
  delay(100); // let the power settle

  enableFaultHandler();

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

  analogReference(EXTERNAL); // 3.3v external, or 1.7v internal. We need 1.7 internal for the battery level, which means we're gonna have to do something about the paddles :/  
  analogReadRes(8); // We only need 8 bits of resolution (0-255) for battery & paddles
  analogReadAveraging(4); // ?? dunno if we need this or not.
  analogWriteResolution(12);
  
  pinMode(SPEAKERPIN, OUTPUT); // analog speaker output, used as digital volume control
  pinMode(BATTERYPIN, INPUT);

  println("creating virtual hardware");
  g_speaker = new TeensySpeaker(SPEAKERPIN);

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

  // Create the virtual machine. This may read from g_filemanager to
  // get ROMs if necessary.  (The actual Apple VM we've built has them
  // compiled in, though.) It will create its virutal hardware (MMU,
  // video driver, floppy, paddles, whatever).
  println(" vm");
  g_vm = new AppleVM();

  // Now that the VM exists and it has created an MMU, we tell the CPU
  // how to access memory through the MMU.
  println(" [setMMU]");
  g_cpu->SetMMU(g_vm->getMMU());

  // And the physical keyboard needs hooks in to the virtual keyboard...
  println(" keyboard");
  g_keyboard = new TeensyKeyboard(g_vm->getKeyboard());

  println(" paddles");
  g_paddles = new TeensyPaddles(A23, A24, 1, 1);

  // Now that all the virtual hardware is glued together, reset the VM
  println("Resetting VM");
  g_vm->Reset();

  g_display->redraw();
//  g_display->blit();

  println("Reading prefs");
  readPrefs(); // read from eeprom and set anything we need setting

  startMicros = nextInstructionMicros = micros();

  // Debugging: insert a disk on startup...
  //  ((AppleVM *)g_vm)->insertDisk(0, "/A2DISKS/UTIL/mock2dem.dsk", false);
  //  ((AppleVM *)g_vm)->insertDisk(0, "/A2DISKS/JORJ/disk_s6d1.dsk", false);
  //  ((AppleVM *)g_vm)->insertDisk(0, "/A2DISKS/GAMES/ALIBABA.DSK", false);

  //  pinMode(56, OUTPUT);
  //  pinMode(57, OUTPUT);

  Serial.print("Free RAM: ");
  println(FreeRamEstimate());

  println("free-running");

  Timer1.initialize(3);
  Timer1.attachInterrupt(runCPU);
  Timer1.start();
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
  Timer1.stop();

  // wait for the interrupt button to be released
  while (digitalRead(RESETPIN) == LOW)
    ;

  // invoke the BIOS
  if (bios.runUntilDone()) {
    // if it returned true, we have something to store persistently in EEPROM.
    writePrefs();
  }

  // if we turned off debugMode, make sure to clear the debugMsg
  if (g_debugMode == D_NONE) {
    g_display->debugMsg("");
  }

  // clear the CPU next-step counters
  g_cpu->cycles = 0;
  nextInstructionMicros = micros();
  startMicros = micros();
  // Drain the speaker queue (FIXME: a little hacky)
  g_speaker->maintainSpeaker(-1, -1);

  // Force the display to redraw
  g_display->redraw();
  ((AppleDisplay*)(g_vm->vmdisplay))->modeChange();

  // Poll the keyboard before we start, so we can do selftest on startup
  g_keyboard->maintainKeyboard();

  Timer1.start();
}

//bool debugState = false;
//bool debugLCDState = false;


void runCPU()
{
  g_inInterrupt = true;
  // Debugging: to watch when the speaker is triggered...
  //  static bool debugState = false;
  //  debugState = !debugState;
  //  digitalWrite(56, debugState);
   
  // Relatively critical timing: CPU needs to run ahead at least 4
  // cycles, b/c we're calling this interrupt (runCPU, that is) just
  // about 1/3 as fast as we should; and the speaker is updated
  // directly from within it, so it needs to be real-ish time.
  if (micros() > nextInstructionMicros) {
    // Debugging: to watch when the CPU is triggered...
    //    static bool debugState = false;
    //    debugState = !debugState;
    //    digitalWrite(56, debugState);
    
    uint8_t executed = g_cpu->Run(24);

    // The CPU of the Apple //e ran at 1.023 MHz. Adjust when we think
    // the next instruction should run based on how long the execution
    // was ((1000/1023) * numberOfCycles) - which is about 97.8%.
    nextInstructionMicros = startMicros + ((double)g_cpu->cycles * (double)SPEEDCTL);

    ((AppleVM *)g_vm)->cpuMaintenance(g_cpu->cycles);
  }

  g_inInterrupt = false;
}

void loop()
{
  if (digitalRead(RESETPIN) == LOW) {
    // This is the BIOS interrupt. We immediately act on it.
    biosInterrupt();
  } 

  g_keyboard->maintainKeyboard();

  //debugLCDState = !debugLCDState;
  //digitalWrite(57, debugLCDState);

  doDebugging();

  // Only redraw if the CPU is caught up; and then we'll suspend the
  // CPU to draw a full frame.

  // Note that this breaks audio, b/c it's real-time and requires the
  // CPU running to change the audio line's value. So we need to EITHER
  //
  //   - delay the audio line by at least the time it takes for one
  //     display update, OR
  //   - lock display updates so the CPU can update the memory, but we
  //     keep drawing what was going to be displayed
  // 
  // The Timer1.stop()/start() is bad. Using it, the display doesn't
  // tear; but the audio is also broken. Taking it out, audio is good
  // but the display tears. So there's a global - g_prioritizeDisplay - 
  // which lets the user pick which they want.

  if (g_prioritizeDisplay)
    Timer1.stop();
  g_ui->blit();
  g_vm->vmdisplay->lockDisplay();
  if (g_vm->vmdisplay->needsRedraw()) {
    AiieRect what = g_vm->vmdisplay->getDirtyRect();
    g_vm->vmdisplay->didRedraw();
    g_display->blit(what);
  }
  g_vm->vmdisplay->unlockDisplay();
  if (g_prioritizeDisplay)
    Timer1.start();
  
  static unsigned long nextBattCheck = millis() + 30;// debugging
  static int batteryLevel = 0; // static for debugging code! When done
			       // debugging, this can become a local
			       // in the appropriate block below
  if (millis() >= nextBattCheck) {
    // FIXME: what about rollover?
    nextBattCheck = millis() + 3 * 1000; // check every 3 seconds

    // This is a bit disruptive - but the external 3.3v will drop along with the battery level, so we should use the more stable (I hope) internal 1.7v.
    // The alternative is to build a more stable buck/boost regulator for reference...
    analogReference(INTERNAL);
    batteryLevel = analogRead(BATTERYPIN);
    analogReference(EXTERNAL);

    /* LiIon charge to a max of 4.2v; and we should not let them discharge below about 3.5v.
     *  With a resistor voltage divider of Z1=39k, Z2=10k we're looking at roughly 20.4% of 
     *  those values: (10/49) * 4.2 = 0.857v, and (10/49) * 3.5 = 0.714v. Since the external 
     *  voltage reference flags as the battery drops, we can't use that as an absolute 
     *  reference. So using the INTERNAL 1.1v reference, that should give us a reasonable 
     *  range, in theory; the math shows the internal reference to be about 1.27v (assuming 
     *  the resistors are indeed 39k and 10k, which is almost certainly also wrong). But 
     *  then the high end would be 172, and the low end is about 142, which matches my 
     *  actual readings here very well.
     *  
     *  Actual measurements: 
     *    3.46v = 144 - 146
     *    4.21v = 172
     */
#if 0
    Serial.print("battery: ");
    println(batteryLevel);
#endif
    
    if (batteryLevel < 146)
      batteryLevel = 146;
    if (batteryLevel > 168)
      batteryLevel = 168;

    batteryLevel = map(batteryLevel, 146, 168, 0, 100);
    g_ui->drawPercentageUIElement(UIePowerPercentage, batteryLevel);
  }
}

void doDebugging()
{
  char buf[25];
  switch (g_debugMode) {
  case D_SHOWFPS:
    // display some FPS data
    static uint32_t startAt = millis();
    static uint32_t loopCount = 0;
    loopCount++;
    time_t lenSecs;
    lenSecs = (millis() - startAt) / 1000;
    if (lenSecs >= 5) {
      sprintf(buf, "%lu FPS", loopCount / lenSecs);
      g_display->debugMsg(buf);
      startAt = millis();
      loopCount = 0;
    }
    break;
  case D_SHOWMEMFREE:
    sprintf(buf, "%lu %u", FreeRamEstimate(), heapSize());
    g_display->debugMsg(buf);
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
    sprintf(buf, "%lX", g_cpu->cycles);
    g_display->debugMsg(buf);
    break;
  case D_SHOWBATTERY:
    sprintf(buf, "BAT %d", analogRead(BATTERYPIN));
    g_display->debugMsg(buf);
    break;
  case D_SHOWTIME:
    sprintf(buf, "%.2d:%.2d:%.2d", hour(), minute(), second());
    g_display->debugMsg(buf);
    break;
  case D_SHOWDSK:
    {
      uint8_t sd = ((AppleVM *)g_vm)->disk6->selectedDrive();
      sprintf(buf, "s %d t %d",
	      sd,
	      ((AppleVM *)g_vm)->disk6->headPosition(sd));
      g_display->debugMsg(buf);
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
    g_prioritizeDisplay = p.priorityMode;
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
}

void writePrefs()
{
  TeensyPrefs np;
  prefs_t p;

  g_display->clrScr();
  g_display->drawString(M_SELECTED, 80, 100,"Writing prefs...");
  g_display->flush();

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

  Timer1.stop();
  bool ret = np.writePrefs(&p);
  Timer1.start();
}
