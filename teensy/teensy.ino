#include <Arduino.h>
#include <TimerOne.h>
#include <ff.h> // uSDFS
#include <SPI.h>
#include <EEPROM.h>
#include <TimeLib.h>
#include "bios.h"
#include "cpu.h"
#include "applevm.h"
#include "teensy-display.h"
#include "teensy-keyboard.h"
#include "teensy-speaker.h"
#include "teensy-paddles.h"
#include "teensy-filemanager.h"

#define RESETPIN 39
#define BATTERYPIN A19
#define SPEAKERPIN A21

//#define DEBUGCPU

#include "globals.h"
#include "teensy-crash.h"

volatile float nextInstructionMicros;
volatile float startMicros;

FATFS fatfs;      /* File system object */
BIOS bios;

uint8_t videoBuffer[320*240/2];

enum {
  D_NONE        = 0,
  D_SHOWFPS     = 1,
  D_SHOWMEMFREE = 2,
  D_SHOWPADDLES = 3,
  D_SHOWPC      = 4,
  D_SHOWCYCLES  = 5,
  D_SHOWBATTERY = 6,
  D_SHOWTIME    = 7
};
uint8_t debugMode = D_NONE;

static   time_t getTeensy3Time() {  return Teensy3Clock.get(); }

/* Totally messing around with the RadioHead library */
#include <SPI.h>
#include <RH_NRF24.h>
#include <RHSoftwareSPI.h>
RHSoftwareSPI spi;

#define RF_CSN 40
#define RF_MOSI 41
#define RF_IRQ 42
#define RF_CE 53
#define RF_SCK 52
#define RF_MISO 51

RH_NRF24 nrf24(RF_CE, RF_CSN, spi);

void setup()
{
  Serial.begin(230400);

  /* while (!Serial) {
    ; // wait for serial port to connect. Needed for Leonardo only
  }
  Serial.println("hi");
  */
  delay(100); // let the serial port connect if it's gonna

  enableFaultHandler();

  // set the Time library to use Teensy 3.0's RTC to keep time
  setSyncProvider(getTeensy3Time);
  delay(100); // don't know if we need this
  if (timeStatus() == timeSet) {
    Serial.println("RTC set from Teensy");
  } else {
    Serial.println("Error while setting RTC");
  }

  spi.setPins(RF_MISO, RF_MOSI, RF_SCK);
  if (!nrf24.init())
    Serial.println("init failed");
  // Defaults after init are 2.402 GHz (channel 2), 2Mbps, 0dBm
  if (!nrf24.setChannel(1))
    Serial.println("setChannel failed");
  if (!nrf24.setRF(RH_NRF24::DataRate2Mbps, RH_NRF24::TransmitPower0dBm))
    Serial.println("setRF failed");    
  Serial.println("nrf24 initialized");
  
  TCHAR *device = (TCHAR *)_T("0:/");
  f_mount (&fatfs, device, 0);      /* Mount/Unmount a logical drive */

  pinMode(RESETPIN, INPUT);
  digitalWrite(RESETPIN, HIGH);

  analogReference(EXTERNAL); // 3.3v external, instead of 1.7v internal
  analogReadRes(8); // We only need 8 bits of resolution (0-255) for battery & paddles
  analogReadAveraging(4); // ?? dunno if we need this or not.

  pinMode(SPEAKERPIN, OUTPUT); // analog speaker output, used as digital volume control
  pinMode(BATTERYPIN, INPUT);

  Serial.println("creating virtual hardware");
  g_speaker = new TeensySpeaker(SPEAKERPIN);

  Serial.println(" fm");
  // First create the filemanager - the interface to the host file system.
  g_filemanager = new TeensyFileManager();

  // Construct the interface to the host display. This will need the
  // VM's video buffer in order to draw the VM, but we don't have that
  // yet. 
  Serial.println(" display");
  g_display = new TeensyDisplay();

  // Next create the virtual CPU. This needs the VM's MMU in order to
  // run, but we don't have that yet.
  Serial.println(" cpu");
  g_cpu = new Cpu();

  // Create the virtual machine. This may read from g_filemanager to
  // get ROMs if necessary.  (The actual Apple VM we've built has them
  // compiled in, though.) It will create its virutal hardware (MMU,
  // video driver, floppy, paddles, whatever).
  Serial.println(" vm");
  g_vm = new AppleVM();

  // Now that the VM exists and it has created an MMU, we tell the CPU
  // how to access memory through the MMU.
  Serial.println(" [setMMU]");
  g_cpu->SetMMU(g_vm->getMMU());

  // And the physical keyboard needs hooks in to the virtual keyboard...
  Serial.println(" keyboard");
  g_keyboard = new TeensyKeyboard(g_vm->getKeyboard());

  Serial.println(" paddles");
  g_paddles = new TeensyPaddles();

  // Now that all the virtual hardware is glued together, reset the VM
  Serial.println("Resetting VM");
  g_vm->Reset();

  g_display->redraw();
  g_display->blit();

  Serial.println("Reading prefs");
  readPrefs(); // read from eeprom and set anything we need setting

  Serial.println("free-running");

  startMicros = 0;
  nextInstructionMicros = micros();
  Timer1.initialize(3);
  Timer1.attachInterrupt(runCPU);
  Timer1.start();
}

/* We're running the timer that calls this at 1/3 "normal" speed, and
 * then asking runCPU to run 48 steps (individual opcodes executed) of
 * the CPU before returning. Then we figure out how many cycles
 * elapsed during that run, and keep track of how many cycles we now
 * have to "drain off" (how many extra ran during this attempt -- we
 * expected at least 3, but might have gotten more).  Then the next
 * call here from the interrupt subtracts 3 cycles, on the assumption
 * that 3 have passed, and we're good to go.
 *
 * This approach is reasonable: the 6502 instruction set takes an
 * average of 4 clock cycles to execute. This compromise keeps us from
 * chewing up the entire CPU on interrupt overhead, allowing us to
 * focus on refreshing the LCD as fast as possible while sacrificing
 * some small timing differences. Experimentally, paddle values seem
 * to still read well up to 48 steps. At 2*48, the paddles drift at
 * the low end, meaning there's probably an issue with timing.
 */
void runCPU()
{
  //  static bool outputState = false;
  //  outputState = !outputState;
  //  digitalWrite(56, outputState);

  if (micros() >= nextInstructionMicros) {
#ifdef DEBUGCPU
    g_cpu->Run(1);
#else
    g_cpu->Run(24);
#endif

    // The CPU of the Apple //e ran at 1.023 MHz. Adjust when we think
    // the next instruction should run based on how long the execution
    // was ((1000/1023) * numberOfCycles) - which is about 97.8%.

#ifdef DEBUGCPU
    // ... have to slow down so the printing all works
    nextInstructionMicros = startMicros + (float)g_cpu->cycles * 50;
#else
    nextInstructionMicros = startMicros + (float)g_cpu->cycles * 0.978;
#endif

#ifdef DEBUGCPU
  {
    uint8_t p = g_cpu->flags;
    Serial.printf("OP: $%02x A: %02x  X: %02x  Y: %02x  PC: $%04x  SP: %02x  Flags: %c%cx%c%c%c%c%c\n",
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
  }

  g_speaker->beginMixing();
  ((AppleVM *)g_vm)->cpuMaintenance(g_cpu->cycles);
  g_speaker->maintainSpeaker(g_cpu->cycles);
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
  // Shut down the CPU
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
  if (debugMode == D_NONE) {
    g_display->debugMsg("");
  }

  // clear the CPU next-step counters
  g_cpu->cycles = 0;
  nextInstructionMicros = micros();
  startMicros = micros();

  // Force the display to redraw
  ((AppleDisplay*)(g_vm->vmdisplay))->modeChange();

  // Poll the keyboard before we start, so we can do selftest on startup
  g_keyboard->maintainKeyboard();

  // Restart the CPU
  Timer1.start();
}


void loop()
{
  static uint16_t ctr = -1;

  /* testing the fault handler? uncomment this and it'll crash. */
  //  *((int*)0x0) = 1;

  static unsigned long nextBattCheck = 0;
  static int batteryLevel = 0; // static for debugging code! When done
			       // debugging, this can become a local
			       // in the appropriate block below
  if (millis() >= nextBattCheck) {
    // FIXME: what about rollover?
    nextBattCheck = millis() + 1 * 1000; // once a minute? maybe? FIXME: Right now 1/sec

    // FIXME: scale appropriately.
    batteryLevel = analogRead(BATTERYPIN);

    /* 205 is "near dead, do something about it right now" - 3.2v and lower.
     * What's the top end? 216-ish?
     *
     * The reading fluctuates quite a lot - we should probably capture
     * more and average it over a longer period before showing
     * anything (FIXME)
     */
    if (batteryLevel < 205)
      batteryLevel = 205;
    if (batteryLevel > 216)
      batteryLevel = 216;

    batteryLevel = map(batteryLevel, 205, 216, 0, 100);
    ((AppleVM *)g_vm)->batteryLevel( batteryLevel );
  }

  if ((++ctr & 0xFF) ==  0) {
    if (digitalRead(RESETPIN) == LOW) {
      // This is the BIOS interrupt. We immediately act on it.
      biosInterrupt();
    } 

    if (g_vm->vmdisplay->needsRedraw()) {
      // make sure to clear the flag before drawing; there's no lock
      // on didRedraw, so the other thread might update it
      g_vm->vmdisplay->didRedraw();
      g_display->blit();
    }

    g_keyboard->maintainKeyboard();

    {
      char buf[25];
      switch (debugMode) {
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
      }
    }
  }
}

typedef struct _prefs {
  uint32_t magic;
  int16_t volume;
} prefs;

// Fun trivia: the Apple //e was in production from January 1983 to
// November 1993. And the 65C02 in them supported weird BCD math modes.
#define MAGIC 0x01831093

void readPrefs()
{
  prefs p;
  uint8_t *pp = (uint8_t *)&p;

  Serial.println("reading prefs");

  for (uint8_t i=0; i<sizeof(prefs); i++) {
    *pp++ = EEPROM.read(i);
  }

  if (p.magic == MAGIC) {
    // looks valid! Use it.
    Serial.println("prefs valid! Restoring volume");
    if (p.volume > 4095) {
      p.volume = 4095;
    }
    if (p.volume < 0) {
      p.volume = 0;
    }

    g_volume = p.volume;
    return;
  }

  // use defaults
  g_volume = 0;
}

// Writes to EEPROM slow down the Teensy 3.6's CPU to 120MHz automatically. Disable our timer 
// while we're doing it and we'll just see a pause.
void writePrefs()
{
  Timer1.stop();

  Serial.println("writing prefs");

  prefs p;
  uint8_t *pp = (uint8_t *)&p;

  p.magic = MAGIC;
  p.volume = g_volume;

  for (uint8_t i=0; i<sizeof(prefs); i++) {
    EEPROM.write(i, *pp++);
  }

  Timer1.start();
}
