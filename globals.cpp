#include "globals.h"

FileManager *g_filemanager = NULL;
Cpu *g_cpu = NULL;
VM *g_vm = NULL;
PhysicalDisplay *g_display = NULL;
PhysicalKeyboard *g_keyboard = NULL;
PhysicalSpeaker *g_speaker = NULL;
PhysicalPaddles *g_paddles = NULL;
PhysicalPrinter *g_printer = NULL;
VMui *g_ui;
int16_t g_volume = 15;
uint8_t g_displayType = 3; // FIXME m_perfectcolor
VMRam g_ram;
volatile bool g_inInterrupt = false;
volatile uint8_t g_debugMode = D_NONE;
bool g_prioritizeDisplay = false;
volatile bool g_biosInterrupt = false;
uint32_t g_speed = 1023000; // Hz
