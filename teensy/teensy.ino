#include <Arduino.h>
#include <SPI.h>
#include <TimeLib.h>
#include <TeensyThreads.h>
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

#if F_CPU < 240000000
#pragma AiiE warning: performance will improve if you overclock the Teensy to 240MHz (F_CPU=240MHz) or 256MHz (F_CPU=256MHz)
#endif

#define RESETPIN 39
#define BATTERYPIN 38
#define SPEAKERPIN A16 // aka digital 40

#include "globals.h"
#include "teensy-crash.h"

BIOS bios;

// How many microseconds per cycle
#define SPEEDCTL ((float)1000000/(float)g_speed)

static   time_t getTeensy3Time() {  return Teensy3Clock.get(); }

TeensyUSB usb;

int cpuThreadId;
int displayThreadId;
int maintenanceThreadId;
int biosThreadId = -1;

Bounce resetButtonDebouncer = Bounce();
Threads::Mutex cpulock; // For the BIOS to suspend CPU cleanly
Threads::Mutex displaylock; // For the BIOS to shut down the display cleanly

volatile bool g_writePrefsFromMainLoop = false;

void onKeypress(int unicode)
{
  Serial.print("onKeypress:");
  Serial.println(unicode);
  uint8_t modifiers = usb.getModifiers();
  Serial.print("Modifiers: ");
  Serial.println(modifiers, HEX);
  if (unicode == 0) {
    unicode = usb.getOemKey();
    Serial.print("oemKey: ");
    Serial.println(unicode);
  }
  //  vmkeyboard->keyDepressed(keypad.key[i].kchar);
}

void onKeyrelease(int unicode)
{
  Serial.print("onKeyrelease: ");
  Serial.println(unicode);
  uint8_t modifiers = usb.getModifiers();
  Serial.print("Modifiers: ");
  Serial.println(modifiers, HEX);
  if (unicode == 0) {
    unicode = usb.getOemKey();
    Serial.print("oemKey: ");
    Serial.println(unicode);
  }
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

//  enableFaultHandler();
  SCB_SHCSR |= SCB_SHCSR_BUSFAULTENA | SCB_SHCSR_USGFAULTENA | SCB_SHCSR_MEMFAULTENA;


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

  analogReadRes(8); // We only need 8 bits of resolution (0-255) for battery & paddles
  analogReadAveraging(4); // ?? dunno if we need this or not.
  
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
  g_paddles = new TeensyPaddles(A3, A4, g_invertPaddleX, g_invertPaddleY);

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
  
  println("free-running");
  Serial.flush();

  threads.setMicroTimer(); // use a 100uS timer instead of a 1mS timer
  cpuThreadId = threads.addThread(runCPU);
  displayThreadId = threads.addThread(runDisplay);
  maintenanceThreadId = threads.addThread(runMaintenance);
  // Set the relative priorities of the threads by defining how long a "slice"
  // is for each (in 100uS "ticks")
  // At a ratio of 50:10:1, we get about 30FPS and 100% CPU speed.
  threads.setTimeSlice(displayThreadId, 100);
  threads.setTimeSlice(cpuThreadId, 20);
  threads.setTimeSlice(maintenanceThreadId, 1);
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
  // Make sure the CPU and display don't run while we're in interrupt.
  Threads::Scope lock1(cpulock);
  Threads::Scope lock2(displaylock);

  // wait for the interrupt button to be released
  while (!resetButtonDebouncer.read())
    ;

  // invoke the BIOS
  if (bios.runUntilDone()) {
    // if it returned true, we have something to store persistently in EEPROM.
    // The EEPROM doesn't like to be written to from a thread?
    g_writePrefsFromMainLoop = true;
    while (g_writePrefsFromMainLoop) {
      delay(100);
      // wait for write to complete
    }
    // Also might have changed the paddles state
    TeensyPaddles *tmp = (TeensyPaddles *)g_paddles;
    tmp->setRev(g_invertPaddleX, g_invertPaddleY);
  }

  // if we turned off debugMode, make sure to clear the debugMsg
  if (g_debugMode == D_NONE) {
    g_display->debugMsg("");
  }

  // clear the CPU next-step counters
  #if 0
  // FIXME: this is to prevent the CPU from racing to catch up, and we need sth in the threads world
  g_cpu->cycles = 0;
  nextInstructionMicros = micros();
  startMicros = micros();
  #endif
  // Drain the speaker queue (FIXME: a little hacky)
  g_speaker->maintainSpeaker(-1, -1);

  // Force the display to redraw
  g_display->redraw(); // Redraw the UI
  ((AppleDisplay*)(g_vm->vmdisplay))->modeChange(); // force a full re-draw and blit

  // Poll the keyboard before we start, so we can do selftest on startup
  g_keyboard->maintainKeyboard();
}

//bool debugState = false;
//bool debugLCDState = false;

// FIXME: how often does this really need to run? We can threads.yield() when we're running too quickly
void runMaintenance()
{
  uint32_t nextRuntime = 0;
  
  while (1) {
    if (millis() > nextRuntime) {
      nextRuntime = millis() + 100; // FIXME: what's a good time here

      if (biosThreadId == -1) {
	// bios is not running; see if it should be
	  if (!resetButtonDebouncer.read()) {
	    // This is the BIOS interrupt. We immediately act on it.

	    biosThreadId = threads.addThread(biosInterrupt);
	  }
      } else if (threads.getState(biosThreadId) != Threads::RUNNING) {
	// When the BIOS thread exits, we clean up
	threads.wait(biosThreadId);
	biosThreadId = -1;
      }
      
      g_keyboard->maintainKeyboard();
      usb.maintain();
      
      static unsigned long nextBattCheck = millis() + 30;// debugging
      static int batteryLevel = 0; // static for debugging code! When done
      // debugging, this can become a local
      // in the appropriate block below
      if (millis() >= nextBattCheck) {
	// FIXME: what about rollover?
	nextBattCheck = millis() + 3 * 1000; // check every 3 seconds
	
	// This is a bit disruptive - but the external 3.3v will drop along with the battery level, so we should use the more stable (I hope) internal 1.7v.
	// The alternative is to build a more stable buck/boost regulator for reference...
	
	batteryLevel = analogRead(BATTERYPIN);
	
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
    } else {
      threads.delay(10);
      //      threads.yield();
    }
  }
}

// FIXME: figure out how to limit this to 30 FPS (or whatver) so we can
// appropriately use threads.yield()
void runDisplay()
{
  g_display->redraw(); // Redraw the UI; don't blit to the physical device
  
  while (1) {
    {
      Threads::Scope lock(displaylock);
      doDebugging();

      uint32_t startDisp = millis();
      uint32_t cpuBefore = g_cpu->cycles;
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
      uint32_t dispTime = millis() - startDisp;
      uint32_t cpuAfter = g_cpu->cycles;
      if (dispTime > 75) {
	print("Slow blit: ");
	print(dispTime);
	print(" cpu ran: ");
	println(cpuAfter - cpuBefore);
      }
    }
  }
}

void runCPU()
{
  uint32_t nextInstructionMicros;
  uint32_t startMicros;
  startMicros = nextInstructionMicros = micros();

  uint32_t startMillis = millis();
  
  while (1) {
    // Relatively critical timing: CPU needs to run ahead at least 4
    // cycles, b/c we're calling this interrupt (runCPU, that is) just
    // about 1/3 as fast as we should; and the speaker is updated
    // directly from within it, so it needs to be real-ish time.
    if (micros() >= nextInstructionMicros) {
      uint32_t expectedCycles = (micros() - startMicros) * SPEEDCTL;
      
      uint8_t executed;
      cpulock.lock(); // Blocking; if the BIOS is running, we stall here
      executed = g_cpu->Run(24);
      cpulock.unlock();
      
      // The CPU of the Apple //e ran at 1.023 MHz. Adjust when we think
      // the next instruction should run based on how long the execution
      // was ((1000/1023) * numberOfCycles) - which is about 97.8%.
      if (expectedCycles > g_cpu->cycles) {
	nextInstructionMicros = micros();
#if 1
	// show a warning on serial about our current performance
	double percentage = ((double)g_cpu->cycles / (double)expectedCycles) * 100.0;
	static uint32_t nextWarningTime = 0;
	if (millis() > nextWarningTime) {
	  static char buf[100];
	  sprintf(buf, "CPU running at %f%% of %d", percentage, g_speed);
	  println(buf);
	  nextWarningTime = millis() + 1000;
	}
#endif
      } else {
	nextInstructionMicros = startMicros + ((double)g_cpu->cycles * (double)SPEEDCTL);
      }
      
      ((AppleVM *)g_vm)->cpuMaintenance(g_cpu->cycles);
    } else {
      //      threads.yield();
      threads.delay(1);
    }
  }
}

void loop()
{
  resetButtonDebouncer.update();

  if (g_writePrefsFromMainLoop) {
    writePrefs();
    g_writePrefsFromMainLoop = false;
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
  p.priorityMode = g_prioritizeDisplay;
  p.speed = g_speed / (1023000/2);
  strcpy(p.disk1, ((AppleVM *)g_vm)->DiskName(0));
  strcpy(p.disk2, ((AppleVM *)g_vm)->DiskName(1));
  strcpy(p.hd1, ((AppleVM *)g_vm)->HDName(0));
  strcpy(p.hd2, ((AppleVM *)g_vm)->HDName(1));

  bool ret = np.writePrefs(&p);
}
