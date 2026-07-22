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
Uthernet2Interface *g_uthernet = NULL;
VMui *g_ui;
int8_t g_volume = 7;
uint8_t g_displayType = 3; // FIXME m_perfectcolor
VMRam g_ram;
volatile uint8_t g_debugMode = D_NONE;
volatile bool g_biosInterrupt = false;
uint32_t g_speed = 1023000; // Hz
bool g_cycleBeacon = false;
bool g_invertPaddleX = false;
bool g_invertPaddleY = false;

uint8_t g_luminanceCutoff = 122;

uint8_t g_slotDiskII = 6;
uint8_t g_slotParallel = 1;
uint8_t g_slotHD32 = 7;
uint8_t g_slotMouse = 0;      // mouse ROM is slot-4-only; 0 = off. Valid: 0 or 4.
uint8_t g_slotMockingboard = 4;
uint8_t g_slotUthernet = 0; // 0 = disabled; set to a slot number to enable
char g_wifiSSID[33] = {0};   // ESP WiFi credentials (Teensy Uthernet MAC-RAW)
char g_wifiPass[64] = {0};

char g_natFwd[48] = "80,23";        // inbound NAT: Apple ports to expose, e.g. "6580,23"
uint16_t g_natPortOffset = 8000; // SDL-only: bump privileged Apple ports on the host
char g_natSubnet[16] = "10.0.2.0";  // user-mode NAT /24 network (gw .2, dns .3, guest .15)

uint8_t g_ramworksSize = 0; // 0=none, else total aux MB (1, 3, 16)

#ifdef TEENSYDUINO
EXTMEM
#endif
char debugBuf[255];

#ifdef TEENSYDUINO
EXTMEM char fsbuf[200];
#endif
