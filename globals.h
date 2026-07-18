#ifndef __GLOBALS_H
#define __GLOBALS_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#endif

#include <stdint.h>

#include "filemanager.h"
#include "cpu.h"
#include "vm.h"
#include "physicaldisplay.h"
#include "physicalkeyboard.h"
#include "physicalmouse.h"
#include "physicalspeaker.h"
#include "physicalpaddles.h"
#include "physicalprinter.h"
#include "vmui.h"
#include "vmram.h"
#include "uthernet2interface.h"

// display modes
enum {
  M_NORMAL = 0,
  M_SELECTED = 1,
  M_DISABLED = 2,
  M_SELECTDISABLED = 3,
  M_PLAIN = 4
};

// debug modes
enum {
  D_NONE        = 0,
  D_SHOWFPS     = 1,
  D_SHOWMEMFREE = 2,
  D_SHOWPADDLES = 3,
  D_SHOWPC      = 4,
  D_SHOWCYCLES  = 5,
  D_SHOWBATTERY = 6,
  D_SHOWTIME    = 7,
  D_SHOWDSK     = 8,
  D_SHOWNET     = 9
};

extern FileManager *g_filemanager;
extern Cpu *g_cpu;
extern VM *g_vm;
extern PhysicalDisplay *g_display;
extern PhysicalKeyboard *g_keyboard;
extern PhysicalMouse *g_mouse;
extern PhysicalSpeaker *g_speaker;
extern PhysicalPaddles *g_paddles;
extern PhysicalPrinter *g_printer;
extern Uthernet2Interface *g_uthernet;
extern VMui *g_ui;
extern int8_t g_volume;
extern uint8_t g_displayType;
extern VMRam g_ram;
extern volatile uint8_t g_debugMode;
extern volatile bool g_biosInterrupt;
extern uint32_t g_speed;

// When true (SDL --cycle-beacon), a write to $C074 prints the cumulative
// cycle count to stderr so instrumented programs can be timed in
// 1x-equivalent seconds regardless of the current speed multiplier.
extern bool g_cycleBeacon;

// At and above this speed the speaker is muted: audio would have to be
// time-compressed >= 128:1, which is unlistenable, and generating it
// costs CPU that we'd rather spend on emulation.
#define AUDIO_MUTE_SPEED (1023000*128)
extern bool g_invertPaddleX;
extern bool g_invertPaddleY;

extern uint8_t g_luminanceCutoff;

extern uint8_t g_slotDiskII;
extern uint8_t g_slotParallel;
extern uint8_t g_slotHD32;
extern uint8_t g_slotMouse;
extern uint8_t g_slotMockingboard;
extern uint8_t g_slotUthernet;

// WiFi credentials for the Teensy's ESP co-processor (Uthernet MAC-RAW mode).
// Empty SSID means "not configured"; the ESP joins nothing.
extern char g_wifiSSID[33];
extern char g_wifiPass[64];

// RamWorks-compatible aux-memory expansion size, in megabytes.
// 0 = none (stock Extended 80-column card); 1, 3, or 16 = total aux RAM.
extern uint8_t g_ramworksSize;

extern char debugBuf[255];

#ifdef TEENSYDUINO
extern char fsbuf[200];
#endif

#endif
