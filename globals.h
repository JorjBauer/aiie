#ifndef __GLOBALS_H
#define __GLOBALS_H

#include <stdint.h>

#include "filemanager.h"
#include "cpu.h"
#include "vm.h"
#include "physicaldisplay.h"
#include "physicalkeyboard.h"
#include "physicalspeaker.h"
#include "physicalpaddles.h"
#include "physicalprinter.h"
#include "vmui.h"
#include "vmram.h"

// display modes
enum {
  M_NORMAL = 0,
  M_SELECTED = 1,
  M_DISABLED = 2,
  M_SELECTDISABLED = 3
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
  D_SHOWDSK     = 8
};

extern FileManager *g_filemanager;
extern Cpu *g_cpu;
extern VM *g_vm;
extern PhysicalDisplay *g_display;
extern PhysicalKeyboard *g_keyboard;
extern PhysicalSpeaker *g_speaker;
extern PhysicalPaddles *g_paddles;
extern PhysicalPrinter *g_printer;
extern VMui *g_ui;
extern int8_t g_volume;
extern uint8_t g_displayType;
extern VMRam g_ram;
extern volatile uint8_t g_debugMode;
extern volatile bool g_biosInterrupt;
extern uint32_t g_speed;
extern bool g_invertPaddleX;
extern bool g_invertPaddleY;

extern char debugBuf[255];

#ifdef TEENSYDUINO
#include <TeensyThreads.h>
extern Threads::Mutex spi_lock;
#endif

#endif
