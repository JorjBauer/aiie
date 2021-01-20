#include <Arduino.h>
#include <TimeLib.h>
#include <Bounce2.h>
#include "bios.h"
#include "cpu.h"
#include "applevm.h"
#include "teensy-display.h"
#include "teensy-keyboard.h"
#include "teensy-mouse.h"
#include "teensy-speaker.h"
#include "teensy-paddles.h"
#include "teensy-filemanager.h"
#include "teensy-usb.h"
#include "appleui.h"
#include "teensy-prefs.h"
#include "teensy-println.h"
#include "smalloc.h"

//#define DEBUG_TIMING

#if F_CPU < 240000000
#pragma AiiE warning: performance will improve if you overclock the Teensy to 240MHz (F_CPU=240MHz) or 256MHz (F_CPU=256MHz)
#endif

#if F_CPU == 600000000
#pragma AiiE suggestion: if you underclock to 528MHz (F_CPU=528MHz) then it will use significantly less power, and still perform perfectly
#endif

#define RESETPIN 38
#define DEBUGPIN 23
#define BATTERYLEVEL 20 // analog reading of battery voltage (scaled to half)
#define BATTERYSELECT 21 // digital select that turns on the power reading ckt

#include "globals.h"
#include "teensy-crash.h"

BIOS bios;

// How many microseconds per cycle
#define SPEEDCTL ((float)1000000/(float)g_speed)

static   time_t getTeensy3Time() {  return Teensy3Clock.get(); }

TeensyUSB usb;

Bounce resetButtonDebouncer = Bounce();

volatile bool cpuClockInitialized = false;

// The battery voltage measurement comes through a 50% ratio voltage
// divider; and the analog resolution is set to 8 bits (so a max of
// 256); with a fixed voltage reference of 3.3v (standard in the
// Teensy 4.1).  Since the voltage of a 16550 battery is 4.2v (at
// 100%) to 2.5v (at 0%), that means we should expect the
// currentBatteryReading to be about 97 - 163.  Since this is
// imperfect due to tolerance in the resistors and whatnot, we might
// as well call that 100 - 160.
volatile uint16_t currentBatteryReading = 0;
volatile uint16_t currentBatteryCount = 0;
volatile uint16_t currentBatterySum = 0;

#define BATTERYMIN 100
#define BATTERYMAX 160
// how often should we read the battery level?
#define BATTERYPERIOD (60 * 100000)

// FIXME: abstract this into the USB code; doesn't belong in the root...
#include "physicalkeyboard.h"
// https://www.win.tue.nl/~aeb/linux/kbd/scancodes-14.html
static uint8_t usb_scanmap[256] = {
  0, 0, 0, 0, // 0-3 don't exist
  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', // keycodes 4-29
  'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v',
  'w', 'x', 'y', 'z',
  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', // keycodes 30-39
  PK_RET, // keycode 40
  PK_ESC, // 41
  PK_DEL, // 42
  PK_TAB,
  ' ', // space bar
  '-', '=',
  '[', ']', '\\',
  0, // 50
  ';', '\'', '`', ',', '.', '/',
  PK_LOCK, // 57
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 58-69, F1-F12 keys
  0, 0, 0, 0, 0, 0, 0, 0, 0, // PrtScr, scroll lock, pause, insert, home, PgUp, Delete, End, PgDown
  PK_RARR, PK_LARR, PK_DARR, PK_UARR, // 79-82, arrow keys
  0, // 83 num lock
  '/', '*', '-', '+', PK_RET, '0', '1', '2', // 84-99 keypad, which we just...
  '3', '4', '5', '6', '7', '8', '9', '.',  //   ... use as their "normal" keys
  0, // 100 undefined
  PK_RA, // 101: "application" key
  0, // 102 "power" key
  PK_CTRL, // 103 keypad '=' but it's my left control key
  PK_LSHFT, // 104, "f13" but it's my left shift key
  PK_LA, // 105: "f14" but it's my left alt key
  PK_LA, // 106: "f15" but it's the windows/command key
  PK_CTRL, // 107: "f16" but it's my right control key
  PK_RSHFT, // 108: "f17" but it's my right shift key
  PK_RA, // 109: "f18" but it's my right alt key
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 110-119
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 120-129
  0, 0, 0, // 130-132
  ',', // 133: keypad ,
  '=', // 134: keypad =
  0, 0, 0, 0, 0,  // 135-139
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 140-149
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 150-159
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 160-169
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 170-179
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 180-189
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 190-199
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 200-209
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 210-219
  0, 0, 0, 0, // 220-223
  PK_CTRL, // 224: left control (but not on my keyboard)
  PK_LSHFT, // 225: left shift (but not on my keyboard)
  PK_LA, PK_LA, // 226, 227: left alt, left GUI (but not on my keyboard)
  PK_CTRL, // 228: right control (but not on my keyboard)
  PK_RSHFT, // 229: right shift (but not on my keyboard)
  PK_RA, PK_RA, // 230, 231: right alt, right GUI (but not on my keyboard)
  0, 0, 0, 0, 0, 0, 0, 0, // 232-239
  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // 240-249
  0, 0, 0, 0, 0, 0 // 250-255
};
	
EXTMEM uint8_t keysPressed[256]; // FIXME: if we need to save RAM, make this bitflags

void onKeypress(uint8_t keycode)
{
  if (keysPressed[keycode])
    return; // defeat auto-repeat
  if (!usb_scanmap[keycode])
    return; // skip undefined keys

  if (keycode == 67 || keycode == 70) {
    // F10 or PrtSc/SysRq are interrupt buttons. Probably needs to be
    // configurable somehow...
    g_biosInterrupt = true;
  } else {
    keysPressed[keycode] = 1;
    ((TeensyKeyboard *)g_keyboard)->pressedKey(usb_scanmap[keycode]);
  }
}

void onKeyrelease(uint8_t keycode)
{
  if (!keysPressed[keycode])
    return; // defeat auto-repeat
  if (!usb_scanmap[keycode])
    return; // skip undefined keys

  keysPressed[keycode] = 0;
  ((TeensyKeyboard *)g_keyboard)->releasedKey(usb_scanmap[keycode]);
}

void setup()
{
  Serial.begin(230400);
#if 0
  // Wait for USB serial connection before booting while debugging
  while (!Serial) {
    yield();
  }
  delay(2000);
#endif
  delay(200); // let the power settle & serial to get its bearings

  pinMode(DEBUGPIN, OUTPUT); // for debugging
  pinMode(BATTERYSELECT, OUTPUT);
  digitalWrite(BATTERYSELECT, false); // leave it off by default
  pinMode(BATTERYLEVEL, INPUT);

//  enableFaultHandler();
//  SCB_SHCSR |= SCB_SHCSR_BUSFAULTENA | SCB_SHCSR_USGFAULTENA | SCB_SHCSR_MEMFAULTENA;

  memset(keysPressed, 0, sizeof(keysPressed));

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

  // the paddles are used by the teensy mouse
  println(" paddles");
  g_paddles = new TeensyPaddles(A3, A2, g_invertPaddleX, g_invertPaddleY);

  // The keyboard reaches in to the mouse
  println(" mouse");
  g_mouse = new TeensyMouse();
  
  // And the physical keyboard needs hooks in to the virtual keyboard...
  println(" keyboard");
  g_keyboard = new TeensyKeyboard(g_vm->getKeyboard());

  // the usb keyboard piggybacks on g_keyboard
  println(" usb");
  usb.init();
  usb.attachKeypress(onKeypress);
  usb.attachKeyrelease(onKeyrelease);

  // Now that all the virtual hardware is glued together, reset the VM
  println("Resetting VM");
  g_vm->Reset();

  println("Reading prefs");
  readPrefs(); // read from eeprom and set anything we need setting
  g_speaker->begin(); // let the speaker reset its volume from g_volume
  
  resetButtonDebouncer.attach(RESETPIN);
  resetButtonDebouncer.interval(5); // ms

  println("Drawing UI border");
  g_display->redraw();
  
  println("free-running");
  Serial.flush();
}

// FIXME: move these memory-related functions elsewhere...

// This only gives you an estimated free mem size. It's not perfect.
uint32_t FreeIntRamEstimate()
{
  uint32_t heapTop;

  // The Teensy 4.1 has different memory regions; the stack grows down
  // from the top of RAM1, and the heap gros up from the start of
  // RAM2. The end of RAM2 is 0x20280000, so if we malloc a byte we
  // should be able to calculate a gross estimate (ignoring memory
  // holes created by fragmentation of course).

  void* hTop = malloc(1);
  heapTop = (uint32_t) hTop;
  free(hTop);

  return 0x20280000 - heapTop;
}

uint32_t FreeExtRamEstimate()
{
  // EXTMEM uses a different thing entirely - the smalloc library is
  // embedded in TeensyDuino (as of this writing) and we should be
  // able to query it to see how much ram exists, is in use, and is
  // free. However, at some point this will break, and we'll have to
  // figure out what new library Teensyduino moved to...
  
  size_t total = 0, totalUser = 0, freespace = 0;
  int blocks; // number of blocks allocated
  sm_malloc_stats_pool(&extmem_smalloc_pool, &total, &totalUser, &freespace, &blocks);

  // total and totalUser always seem to be 0. So is blocks. But freespace might be real?

  return freespace;
}

#include "malloc.h"

int heapSize(){
  return mallinfo().uordblks;
}

void runMaintenance(uint32_t now)
{
  static uint32_t nextRuntime = 0;
  
  if (now >= nextRuntime) {
    // Run maintenance at 60 Hz because the mouse will need it
    nextRuntime = now + 16667;
    
    if (!resetButtonDebouncer.read()) {
      // This is the BIOS interrupt. Wait for it to clear and process it.
      while (!resetButtonDebouncer.read())
	resetButtonDebouncer.update();

      g_biosInterrupt = true;
    }

    if (!g_biosInterrupt) {
        g_mouse->maintainMouse();
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
  static uint32_t displayFrameCount = 0;
  
  // If it's time to draw the next frame, then do so
  if (now >= microsForNext) {
    refreshCount++;
    microsForNext = microsAtStart + (1000000.0*((float)refreshCount/(float)TARGET_FPS));

    { static uint32_t nextDebugTime = 0;
      if (millis() > nextDebugTime) {
	doDebugging(lastFps);
	nextDebugTime = millis() + 1000;
      }
    }

    if (!g_biosInterrupt) {
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
  }
  
  // Once a second, start counting all over again
  if (now >= nextResetMicros) {
    uint32_t newFrameCount = ((TeensyDisplay *)g_display)->frameCount();
    
    // There are two "FPS" counters here, actually. One is how often
    // we're polling the Apple //e memory to refresh the DMA buffer,
    // and to show that, we'd use this:
    //      lastFps = refreshCount;
    // The other is how often the DMA code is refreshing the actual
    // display, and to show that, we'd use this:
    lastFps =  newFrameCount - displayFrameCount;
#ifdef DEBUG_TIMING
    // ... and this debugging code shows both.
    println("DMA buffer refresh at ", refreshCount, " FPS");
    println("Display refresh at ", newFrameCount - displayFrameCount, " FPS");
#endif
    displayFrameCount = newFrameCount;
    nextResetMicros = now + 1000000;
    refreshCount = 0;
    microsAtStart = now;
    microsForNext = microsAtStart + (1000000.0*((float)refreshCount/(float)TARGET_FPS));
  }
}

// The debouncer is used in the bios, which blocks the main loop
// execution; so this function updates the debouncer instead. It used
// to be a thread of its own, but now that this is single-threaded
// again, it's a standalone method.
void runDebouncer()
{
  static uint32_t nextRuntime = 0;
  if (millis() >= nextRuntime) {
    nextRuntime = millis() + 10;
    resetButtonDebouncer.update();
  } else {
    yield();
  }
}

void runBIOS(uint32_t now)
{
  static uint32_t nextResetMicros = 0;
  static uint32_t countSinceLast = 0;
  static uint32_t microsAtStart = micros();
  static uint32_t microsForNext = microsAtStart + 100000; // 1/10 second

  if (now >= microsForNext) {
    microsForNext = now + 100000; // 1/10 second
    if (!bios.loop()) {
      g_biosInterrupt = false;
    }
  }
}

void runCPU(uint32_t now)
{
  static uint32_t nextResetMicros = 0;
  static uint32_t countSinceLast = 0;
  static uint32_t microsAtStart = micros();
  static uint32_t microsForNext = microsAtStart + (countSinceLast * SPEEDCTL);

  // Allow the BIOS to reset our timing
  if (!cpuClockInitialized) {
    nextResetMicros = 0;
    countSinceLast = 0;
    microsAtStart = micros();
    microsForNext = microsAtStart + (countSinceLast * SPEEDCTL);

    cpuClockInitialized = true;
  }
  
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

void loop()
{
  static uint32_t readingBattery = 0; // set to millis() + a settle time constant when we start reading
  static uint32_t nextReadBattery = micros() + BATTERYPERIOD;
  
  uint32_t now = micros();

  if (readingBattery && now >= readingBattery) {
    // Take 10 readings over a second and average them
    currentBatterySum += analogRead(BATTERYLEVEL);
    readingBattery = now + 100000; // 100 ms
    if (++currentBatteryCount >= 10) {
      currentBatteryReading = currentBatterySum / currentBatteryCount;
      readingBattery = 0;
      digitalWrite(BATTERYSELECT, false);
      nextReadBattery = now + BATTERYPERIOD;

      // Set up the displayed battery level
      if (currentBatteryReading < BATTERYMIN)
	currentBatteryReading = BATTERYMIN;
      if (currentBatteryReading > BATTERYMAX)
	currentBatteryReading = BATTERYMAX;
	
      ((AppleUI *)g_ui)->drawBatteryStatus(map(currentBatteryReading,
					       BATTERYMIN, BATTERYMAX,
					       0, 100));
    }
  }
  else if (!readingBattery && now >= nextReadBattery) {
    // start reading the battery
    readingBattery = now + 1 * 1000000; // let it settle for 1 second
    currentBatterySum = 0;
    currentBatteryCount = 0;
    digitalWrite(BATTERYSELECT, true);
  }

  static bool wasBios = false; // so we can tell when it's done
  if (g_biosInterrupt) {
    runBIOS(now);
    wasBios = true;
  } else {
    if (wasBios) {
      // bios has just exited
      writePrefs();

      // Also might have changed the paddles state
      TeensyPaddles *tmp = (TeensyPaddles *)g_paddles;
      tmp->setRev(g_invertPaddleX, g_invertPaddleY);

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

      // Reset the CPU clock so it doesn't fast-forward
      cpuClockInitialized = false;

      // Reset the speaker so it picks up its new volume (FIXME kinda hacky)
      g_speaker->begin();

      wasBios = false;
    }
  }

  if (!g_biosInterrupt) {
    runCPU(now);
  }
  runDisplay(now);
  runMaintenance(now);
  runDebouncer();
}

void doDebugging(uint32_t lastFps)
{
  switch (g_debugMode) {
  case D_SHOWFPS:
    sprintf(debugBuf, "%lu FPS", lastFps);
    g_display->debugMsg(debugBuf);
    break;
  case D_SHOWMEMFREE:
    sprintf(debugBuf, "%lu %lu", FreeIntRamEstimate(), FreeExtRamEstimate());
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
    sprintf(debugBuf, "%llX", g_cpu->cycles);
    g_display->debugMsg(debugBuf);
    break;
  case D_SHOWBATTERY:
    sprintf(debugBuf, "B: %d %d%%     ", currentBatteryReading,
	    map(currentBatteryReading, BATTERYMIN, BATTERYMAX, 0, 100));
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
    
    g_luminanceCutoff = p.luminanceCutoff;
    
    g_invertPaddleX = p.invertPaddleX;
    g_invertPaddleY = p.invertPaddleY;
    
  } else {
    // Set some defaults!
    g_volume = 7;
    g_displayType = 3; // FIXME constant
    g_debugMode = D_NONE;
    g_speed = 1023000;
    g_luminanceCutoff = 127;
    g_invertPaddleX = g_invertPaddleY = false;
    
  }
  // Update the paddles with the new inversion state
  ((TeensyPaddles *)g_paddles)->setRev(g_invertPaddleX, g_invertPaddleY);
}

void writePrefs()
{
  TeensyPrefs np;
  prefs_t p;

  p.magic = PREFSMAGIC;
  p.magicFooter = PREFSMAGIC;
  p.prefsSize = sizeof(prefs_t);
  p.version = PREFSVERSION;

  p.invertPaddleX = g_invertPaddleX;
  p.invertPaddleY = g_invertPaddleY;

  p.volume = g_volume;
  p.displayType = g_displayType;
  p.luminanceCutoff = g_luminanceCutoff;
  p.debug = g_debugMode;
  p.speed = g_speed / (1023000/2);
  strcpy(p.disk1, ((AppleVM *)g_vm)->DiskName(0));
  strcpy(p.disk2, ((AppleVM *)g_vm)->DiskName(1));
  strcpy(p.hd1, ((AppleVM *)g_vm)->HDName(0));
  strcpy(p.hd2, ((AppleVM *)g_vm)->HDName(1));

  bool ret = np.writePrefs(&p);
}
