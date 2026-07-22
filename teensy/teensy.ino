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
// (ESP link is Serial3, a built-in hardware UART - no extra include needed.)
#include "teensy-uthernet2.h"

//#define DEBUG_TIMING

#if F_CPU < 240000000
#warning "AiiE: performance will improve if you overclock the Teensy to 240MHz or higher"
#endif

#if F_CPU == 600000000
#warning "AiiE: if you underclock to 528MHz it will use significantly less power, and still perform perfectly"
#endif

#define RESETPIN 38
#define DEBUGPIN 23
#define BATTERYLEVEL 20 // analog reading of battery voltage (scaled to half)
#define BATTERYSELECT 21 // digital select that turns on the power reading ckt

#include "globals.h"
#include "teensy-fwversion.h"

// Firmware version, embedded so the SD self-update can find it in a raw image.
// __attribute__((used)) keeps the linker from discarding it (nothing references
// the blob directly). See teensy-fwversion.h for the on-flash layout.
extern "C" const char aiie_fw_version_blob[] __attribute__((used)) =
    AIIE_FW_MAGIC AIIE_FW_VERSION "\x1e";
const char *g_fwVersion = AIIE_FW_VERSION;

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

// Pump the input devices once, for blocking loops (e.g. the firmware-update
// confirmation) that would otherwise starve the USB host of Task() calls and
// never see a keypress. Mirrors what the main loop does each pass.
void teensyServiceInput()
{
  g_keyboard->maintainKeyboard();
  usb.maintain();
}

// Link to the ESP8266 network co-processor over Serial3, a real hardware UART:
//   Teensy TX3 = pin 14 -> ESP RXD
//   Teensy RX3 = pin 15 <- ESP TXD
// A hardware UART gives us proper interrupt-driven RX with a ring buffer, so
// none of the SoftwareSerial (no receive) / FlexIO (resource juggling) trouble
// on pins 18/19 applies. The transport is just a Stream, unchanged downstream.
static HardwareSerial &espLink = Serial3;

// Bench-test WiFi credentials for the ESP Uthernet link. Fill these in with
// your AP to test on hardware. Left empty so no real credentials are committed;
// when empty the ESP joins nothing. A shipping build should source these from
// prefs or an SD config instead of hardcoding them here.
#ifndef UTHERNET_WIFI_SSID
#define UTHERNET_WIFI_SSID ""
#define UTHERNET_WIFI_PASS ""
#endif

// Inbound port forwarding for MAC-RAW own-stack software running a server (e.g.
// a Contiki/IP65 webserver on the Apple). Format "espPort:applePort[,...]":
// e.g. "80:80" lets a LAN client reach the ESP's IP:80 and hit the Apple's
// webserver on port 80. NULL disables it.
#ifndef UTHERNET_HOSTFWD
#define UTHERNET_HOSTFWD NULL
#endif

// Bring up the ESP8266 network co-processor and its transport backend.
// Idempotent: does nothing if the Uthernet card has no slot, or if the backend
// is already running. Runs at boot (from setup) and again when the BIOS exits,
// so enabling the card in the BIOS takes effect without a physical power-cycle.
// The emulated Uthernet2 card reads g_uthernet lazily on every access (each use
// guarded by a NULL check), so constructing the backend after the card is safe.
void bringUpUthernet()
{
  if (g_uthernet || !g_slotUthernet) return;
  println(" uthernet");
  espLink.begin(230400);
  // Enlarge the Serial3 receive buffer to hold a full ESP reply frame (~1.6KB).
  // The async engine drains the UART from tick() (inside runCPU), but between CPU
  // bursts the main loop spends several ms blitting the display -- during which
  // nothing reads the UART. With the default ~64-byte buffer a large reply (a
  // 1460-byte socket-poll) arriving during a blit overflows and is lost, showing
  // up as CRC errors, retries, timeouts, and dropped MAC-RAW receive data. A
  // buffer bigger than the largest frame rides out any blit. (The blocking
  // hardware-socket path is immune because command() tight-loops the drain.)
  // addMemoryForRead lives on the concrete Serial3 (HardwareSerialIMXRT), not the
  // HardwareSerial& alias; espLink is Serial3, so enlarge it directly.
  static uint8_t espRxBuffer[AIIE_ESP_MAX_FRAME + 512];
  Serial3.addMemoryForRead(espRxBuffer, sizeof(espRxBuffer));
  TeensyUthernet2 *u2 = new TeensyUthernet2(&espLink, UTHERNET_HOSTFWD);
  // Credentials come from the BIOS (persisted in prefs). The compile-time
  // UTHERNET_WIFI_* is only a fallback for bench builds with nothing saved.
  if (g_wifiSSID[0])              u2->setNetwork(g_wifiSSID, g_wifiPass);
  else if (UTHERNET_WIFI_SSID[0]) u2->setNetwork(UTHERNET_WIFI_SSID, UTHERNET_WIFI_PASS);
  g_uthernet = u2;
  g_uthernet->begin();  // joins the AP if credentials were set
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
  // A pending crash report from the previous run is saved to the SD card once
  // the filemanager is up (this device has no usable serial console at boot,
  // and the old code blocked here for 5 seconds printing to a port nobody could
  // read - which showed up as a multi-second boot delay whenever a stale report
  // was present). See the crash-report save just after the filemanager below.
  if (CrashReport) {
    Serial.print(CrashReport); // only useful on the bench with serial attached
  }

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

  // Persist any crash report from the previous run to the SD card so it can be
  // read after the fact (no usable serial console on this device at boot), then
  // clear it. Clearing also stops a stale report from re-triggering every boot.
  if (CrashReport) {
    FsFile cf = ((TeensyFileManager *)g_filemanager)->getSdFat()->open("/CRASH.TXT", O_WRITE | O_CREAT | O_TRUNC);
    if (cf) {
      cf.print(CrashReport);
      cf.close();
    }
    CrashReport.clear();
  }

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

  // Card configuration (slot assignments + RamWorks size) must be known
  // BEFORE the VM is constructed: AppleVM's constructor wires up the slots
  // and sizes the RamWorks aux RAM from these globals. Reading prefs after
  // construction and re-applying with reassignSlots() clobbered a
  // freshly-inserted disk on cold boot, so read the card settings here.
  // (The full readPrefs() below still loads disks, speed, display, etc.)
  {
    TeensyPrefs early;
    prefs_t p;
    if (early.readPrefs(&p)) {
      if (p.slotDiskII <= 7)       g_slotDiskII = p.slotDiskII;
      if (p.slotParallel <= 7)     g_slotParallel = p.slotParallel;
      if (p.slotHD32 <= 7)         g_slotHD32 = p.slotHD32;
      if (p.slotMouse <= 7)        g_slotMouse = p.slotMouse;
      if (p.slotMockingboard <= 7) g_slotMockingboard = p.slotMockingboard;
      if (p.slotUthernet <= 7)     g_slotUthernet = p.slotUthernet;
      if (p.version >= 10) {
        strncpy(g_wifiSSID, p.wifiSSID, sizeof(g_wifiSSID)-1); g_wifiSSID[sizeof(g_wifiSSID)-1]=0;
        strncpy(g_wifiPass, p.wifiPass, sizeof(g_wifiPass)-1); g_wifiPass[sizeof(g_wifiPass)-1]=0;
      }
      if (p.version >= 11) {
        strncpy(g_natFwd, p.natFwd, sizeof(g_natFwd)-1); g_natFwd[sizeof(g_natFwd)-1]=0;
      }
      if (p.version >= 12) {
        strncpy(g_natSubnet, p.natSubnet, sizeof(g_natSubnet)-1); g_natSubnet[sizeof(g_natSubnet)-1]=0;
      }
      if (p.version >= 7)          g_ramworksSize = p.ramworksSize;
    }
  }

  // Bring up the ESP8266 network co-processor if the Uthernet card is enabled.
  // The link is half-duplex SoftwareSerial on the free A4/A5 pins:
  //   ESP TXD -> Teensy pin 19 (we receive), ESP RXD -> Teensy pin 18 (we send).
  // This happens before AppleVM is constructed so the card sees its backend on
  // the first access; if the card is enabled later (from the BIOS) the exit
  // path calls bringUpUthernet() again.
  bringUpUthernet();

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
  // NB: card config (slots + RamWorks) was already applied before the VM was
  // constructed (see above), so we deliberately do NOT reassignSlots() here.

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
      // FIXME this needs some love. It could be efficient, but parts are removed, so it's doing duplicative work.
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
  
  while (now >= microsForNext) {
    countSinceLast += g_cpu->Run(24);
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
      // bios has just exited — do all cleanup before the CPU runs.
      g_display->clrScr(0x0010);
      g_display->drawString(M_SELECTED, 80, 100, "Resuming...");
      g_display->flush();
      g_display->blit();

      g_speaker->reset();
      writePrefs();

      // If the Uthernet card was just enabled in the BIOS, its transport
      // backend does not exist yet (it is built at boot from g_slotUthernet,
      // and the BIOS "reboot" only resets the emulated Apple, not this
      // firmware). Bring it up now so the card works without a power-cycle.
      bringUpUthernet();

      TeensyPaddles *tmp = (TeensyPaddles *)g_paddles;
      tmp->setRev(g_invertPaddleX, g_invertPaddleY);

      if (g_debugMode == D_NONE) {
	g_display->debugMsg("");
      }

      g_keyboard->maintainKeyboard();

      g_speaker->begin();

      // Force the display to show the Apple's screen.
      g_display->redraw();
      ((AppleDisplay*)(g_vm->vmdisplay))->modeChange();
      g_ui->blit();
      g_vm->vmdisplay->lockDisplay();
      g_vm->vmdisplay->didRedraw();
      g_display->blit();
      g_vm->vmdisplay->unlockDisplay();

      // Reset the CPU clock last so it doesn't try to catch up
      // for time spent in cleanup.
      cpuClockInitialized = false;

      wasBios = false;
    }
  }

  if (!g_biosInterrupt && !wasBios) {
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
  case D_SHOWNET:
    if (g_uthernet) {
      ((TeensyUthernet2 *)g_uthernet)->debugNetState(debugBuf, sizeof(debugBuf));
      g_display->debugMsg(debugBuf);
    }
    break;
  case D_SHOWBATTERY:
    sprintf(debugBuf, "B: %d %ld%%     ", currentBatteryReading,
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
    // steps of half normal speed; v8+ stores them in the 16-bit field
    uint32_t speedSteps = (p.version >= 8) ? p.speed16 : p.speed;
    if (speedSteps < 1) speedSteps = 2; // 1x
    if (speedSteps > 8) speedSteps = 8; // Teensy tops out at 4x
    g_speed = speedSteps * (1023000/2);
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

    if (p.slotDiskII <= 7) g_slotDiskII = p.slotDiskII;
    if (p.slotParallel <= 7) g_slotParallel = p.slotParallel;
    if (p.slotHD32 <= 7) g_slotHD32 = p.slotHD32;
    if (p.slotMouse <= 7) g_slotMouse = p.slotMouse;
    if (p.slotMockingboard <= 7) g_slotMockingboard = p.slotMockingboard;
    if (p.slotUthernet <= 7) g_slotUthernet = p.slotUthernet;
    if (p.version >= 10) {
      strncpy(g_wifiSSID, p.wifiSSID, sizeof(g_wifiSSID)-1); g_wifiSSID[sizeof(g_wifiSSID)-1]=0;
      strncpy(g_wifiPass, p.wifiPass, sizeof(g_wifiPass)-1); g_wifiPass[sizeof(g_wifiPass)-1]=0;
    }
    if (p.version >= 11) {
      strncpy(g_natFwd, p.natFwd, sizeof(g_natFwd)-1); g_natFwd[sizeof(g_natFwd)-1]=0;
    }
    if (p.version >= 12) {
      strncpy(g_natSubnet, p.natSubnet, sizeof(g_natSubnet)-1); g_natSubnet[sizeof(g_natSubnet)-1]=0;
    }

    g_ramworksSize = p.ramworksSize;

  } else {
    // Set some defaults!
    g_volume = 7;
    g_displayType = 3; // FIXME constant
    g_debugMode = D_NONE;
    g_speed = 1023000;
    g_luminanceCutoff = 127;
    g_invertPaddleX = g_invertPaddleY = false;
    g_ramworksSize = 0;

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
  {
    uint32_t speedSteps = g_speed / (1023000/2);
    p.speed16 = speedSteps;
    // legacy field for older readers; saturates at 127.5x
    p.speed = (speedSteps > 255) ? 255 : speedSteps;
  }
  strcpy(p.disk1, ((AppleVM *)g_vm)->DiskName(0));
  strcpy(p.disk2, ((AppleVM *)g_vm)->DiskName(1));
  strcpy(p.hd1, ((AppleVM *)g_vm)->HDName(0));
  strcpy(p.hd2, ((AppleVM *)g_vm)->HDName(1));

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

  np.writePrefs(&p);
}
