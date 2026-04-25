#include "globals.h"

FileManager *g_filemanager = NULL;
Cpu *g_cpu = NULL;
VM *g_vm = NULL;
PhysicalDisplay *g_display = NULL;
PhysicalKeyboard *g_keyboard = NULL;
PhysicalMouse *g_mouse = NULL;
PhysicalSpeaker *g_speaker = NULL;
PhysicalPaddles *g_paddles = NULL;
PhysicalPrinter *g_printer = NULL;
VMui *g_ui;
int8_t g_volume = 7;
uint8_t g_displayType = 3; // FIXME m_perfectcolor
VMRam g_ram;
volatile uint8_t g_debugMode = D_NONE;
volatile bool g_biosInterrupt = false;
uint32_t g_speed = 1023000; // Hz
bool g_invertPaddleX = false;
bool g_invertPaddleY = false;

uint8_t g_luminanceCutoff = 122;

uint8_t g_slotDiskII = 6;
uint8_t g_slotParallel = 1;
uint8_t g_slotHD32 = 7;
uint8_t g_slotMouse = 2;
uint8_t g_slotMockingboard = 4;

#ifdef TEENSYDUINO
EXTMEM
#endif
char debugBuf[255];

#ifdef TEENSYDUINO
EXTMEM char fsbuf[200];
#endif
