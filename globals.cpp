#include "globals.h"

FileManager *g_filemanager = NULL;
Cpu *g_cpu = NULL;
VM *g_vm = NULL;
PhysicalDisplay *g_display = NULL;
PhysicalKeyboard *g_keyboard = NULL;
PhysicalSpeaker *g_speaker = NULL;
PhysicalPaddles *g_paddles = NULL;
PhysicalPrinter *g_printer = NULL;
int16_t g_volume;
uint8_t g_displayType = 3; // FIXME m_perfectcolor
