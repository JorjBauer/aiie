#include <string.h>
#include "globals.h"
#include "bios.h"

#include "applevm.h"
#include "physicalkeyboard.h"
#include "physicaldisplay.h"
#include "cpu.h"

#ifdef TEENSYDUINO
#include <Bounce2.h>
#include "teensy-paddles.h"
extern Bounce resetButtonDebouncer;
extern void runDebouncer();
#endif

// using EXTMEM to cache all the filenames in a directory
#ifndef TEENSYDUINO
#define EXTMEM
#endif

struct _cacheEntry {
  char fn[BIOS_MAXPATH];
};
#define BIOSCACHESIZE 1024 // hope that's enough files?
EXTMEM char cachedPath[BIOS_MAXPATH] = {0};
EXTMEM char cachedFilter[BIOS_MAXPATH] = {0};
EXTMEM struct _cacheEntry biosCache[BIOSCACHESIZE];
uint16_t numCacheEntries = 0;

// When selecting files...
char fileFilter[16]; // FIXME length & Strcpy -> strncpy
uint16_t fileSelectionFor; // define what the returned name is for

// menu screen enums
enum {
  BIOS_AIIE = 0,
  BIOS_VM = 1,
  BIOS_HARDWARE = 2,
  BIOS_DISKS = 3,
  
  BIOS_ABOUT = 4,
  BIOS_PADDLES = 5,
  BIOS_SELECTFILE = 6,
  
  BIOS_DONE = 99,
};
  

enum {
  ACT_EXIT = 1,
  ACT_RESET = 2,
  ACT_COLDBOOT = 3,
  ACT_MONITOR = 4,
  ACT_DISPLAYTYPE = 5,
  ACT_DEBUG = 6,
  ACT_DISK1 = 7,
  ACT_DISK2 = 8,
  ACT_HD1 = 9,
  ACT_HD2 = 10,
  ACT_VOLPLUS = 11,
  ACT_VOLMINUS = 12,
  ACT_SUSPEND = 13,
  ACT_RESTORE = 14,
  ACT_PADX_INV = 15,
  ACT_PADY_INV = 16,
  ACT_PADDLES = 17,
  ACT_SPEED = 18,
  ACT_ABOUT = 19,
};

#define NUM_TITLES 4
const char *menuTitles[NUM_TITLES] = { "Aiie", "VM", "Hardware", "Disks" };
const uint8_t titleWidths[NUM_TITLES] = {45, 28, 80, 45 };

const uint8_t aiieActions[] = { ACT_ABOUT };

const uint8_t vmActions[] = { ACT_EXIT, ACT_RESET, ACT_COLDBOOT, ACT_MONITOR,
			      ACT_DEBUG, ACT_SUSPEND, ACT_RESTORE };
const uint8_t hardwareActions[] = { ACT_DISPLAYTYPE,  ACT_SPEED,
				    ACT_PADX_INV, ACT_PADY_INV,
				    ACT_PADDLES, ACT_VOLPLUS, ACT_VOLMINUS };
const uint8_t diskActions[] = { ACT_DISK1, ACT_DISK2, 
				ACT_HD1, ACT_HD2 };

#define CPUSPEED_HALF 0
#define CPUSPEED_FULL 1
#define CPUSPEED_DOUBLE 2
#define CPUSPEED_QUAD 3

const char *staticPathConcat(const char *rootPath, const char *filePath)
{
  static char buf[MAXPATH];
  strncpy(buf, rootPath, sizeof(buf)-1);
  strncat(buf, filePath, sizeof(buf)-strlen(buf)-1);

  return buf;
}

BIOS::BIOS()
{
  selectedMenu = BIOS_VM;
  selectedMenuItem = 0;

  selectedFile = -1;
  for (int8_t i=0; i<BIOS_MAXFILES; i++) {
    // Put end terminators in place; strncpy won't copy over them
    fileDirectory[i][BIOS_MAXPATH] = '\0';
  }
}

BIOS::~BIOS()
{
}

void BIOS::DrawMenuBar()
{
  uint8_t xpos = 0;

  if (selectedMenu < 0) {
    selectedMenu = NUM_TITLES-1;
  }
  selectedMenu %= NUM_TITLES;

#define XPADDING 2

  for (int i=0; i<NUM_TITLES; i++) {
    for (int x=0; x<titleWidths[i] + 2*XPADDING; x++) {
      g_display->drawPixel(xpos+x, 0, 0xFFFF);
      g_display->drawPixel(xpos+x, 16, 0xFFFF);
    }
    for (int y=0; y<=16; y++) {
      g_display->drawPixel(xpos, y, 0xFFFF);
      g_display->drawPixel(xpos + titleWidths[i] + 2*XPADDING, y, 0xFFFF);
    }

    xpos += XPADDING;

    g_display->drawString(selectedMenu == i ? M_SELECTDISABLED : M_DISABLED,
			  xpos, 2, menuTitles[i]);
    xpos += titleWidths[i] + XPADDING;
  }
}

bool BIOS::loop()
{
  static bool needsinit = true;
  if (needsinit) {
    g_filemanager->getRootPath(rootPath, sizeof(rootPath));
    needsinit = false;
  }

  static bool needsRedraw = true;

  if (selectedMenu == BIOS_DONE) {
    // We're returning to the bios a second time
    selectedMenu = BIOS_VM;
    needsRedraw = true;
  }

#ifdef TEENSYDUINO
  if (resetButtonDebouncer.read() == LOW) {
    // wait until it's no longer pressed
    while (resetButtonDebouncer.read() == LOW)
      runDebouncer();
    delay(100); // wait long enough for it to debounce
    return BIOS_DONE;
  }
#endif

  bool hitReturn = false;
  
  uint16_t rv;
  if (g_keyboard->kbhit()) {
    switch (g_keyboard->read()) {
    case PK_DARR:
      selectedMenuItem++; // modded by current action
      needsRedraw = true;
      break;
    case PK_UARR:
      selectedMenuItem--; // modded by current action
      needsRedraw = true;
      break;
    case PK_RARR:
      selectedMenuItem = 0;
      selectedMenu++;
      selectedMenu %= NUM_TITLES;
      needsRedraw = true;
      break;
    case PK_LARR:
      selectedMenuItem = 0;
      selectedMenu--;
      if (selectedMenu < 0) {
	selectedMenu = NUM_TITLES-1;
      }
      needsRedraw = true;
      break;
    case PK_RET:
      hitReturn = true;
      needsRedraw = true;
      break;
    default:
      break;
    }
  }
  
  switch (selectedMenu) {
  case BIOS_AIIE:
    rv = AiieMenuHandler(needsRedraw, hitReturn);
    break;
  case BIOS_VM:
    rv = VmMenuHandler(needsRedraw, hitReturn);
    break;
  case BIOS_HARDWARE:
    rv = HardwareMenuHandler(needsRedraw, hitReturn);
    break;
  case BIOS_DISKS:
    rv = DisksMenuHandler(needsRedraw, hitReturn);
    break;
  case BIOS_ABOUT:
    rv = AboutScreenHandler(needsRedraw, hitReturn);
    break;
  case BIOS_PADDLES:
    rv = PaddlesScreenHandler(needsRedraw, hitReturn);
    break;
  case BIOS_SELECTFILE:
    rv = SelectFileScreenHandler(needsRedraw, hitReturn);
    break;
  }

  if (rv != selectedMenu) {
    selectedMenuItem = 0;
    needsRedraw = true;
    selectedMenu = rv;
  }
  else
    needsRedraw = false; // assume the handler drew

  return ((selectedMenu == BIOS_DONE) ? false : true);
}

uint16_t BIOS::AiieMenuHandler(bool needsRedraw, bool performAction)
{
  static bool localRedraw = true;
  if (selectedMenuItem < 0)
    selectedMenuItem = sizeof(aiieActions)-1;
  selectedMenuItem %= sizeof(aiieActions);
  
  if (needsRedraw || localRedraw) {
    g_display->clrScr();
    DrawMenuBar();
    DrawAiieMenu();
    g_display->flush();

    localRedraw = false;
  }

  if (performAction) {
    // there is only ACT_ABOUT
    return BIOS_ABOUT;
  }
  
  return BIOS_AIIE;
}

uint16_t BIOS::VmMenuHandler(bool needsRedraw, bool performAction)
{
  static bool localRedraw = true;

  if (selectedMenuItem < 0)
    selectedMenuItem = sizeof(vmActions)-1;
  selectedMenuItem %= sizeof(vmActions);
  
  if (needsRedraw || localRedraw) {
    g_display->clrScr();
    DrawMenuBar();
    DrawVMMenu();

    g_display->flush();

    localRedraw = false;
  }

  if (performAction) {
    if (isActionActive(vmActions[selectedMenuItem])) {
      switch (vmActions[selectedMenuItem]) {
      case ACT_EXIT:
	return BIOS_DONE;
      case ACT_RESET:
	WarmReset();
	return BIOS_DONE;
      case ACT_COLDBOOT:
	ColdReboot();
	return BIOS_DONE;
      case ACT_MONITOR:
	((AppleVM *)g_vm)->Monitor();
	return BIOS_DONE;
      case ACT_DEBUG:
	g_debugMode++;
	g_debugMode %= 9; // FIXME: abstract max #
	localRedraw = true;
	return BIOS_VM;
      case ACT_SUSPEND:
	g_display->clrScr();
	g_display->drawString(M_SELECTED, 80, 100,"Suspending VM...");
	g_display->flush();
	// CPU is already suspended, so this is safe...
	((AppleVM *)g_vm)->Suspend("suspend.vm");
	localRedraw = true;
	return BIOS_VM;
      case ACT_RESTORE:
	g_display->clrScr();
	g_display->drawString(M_SELECTED, 80, 100,"Resuming VM...");
	g_display->flush();
	((AppleVM *)g_vm)->Resume("suspend.vm");
	return BIOS_DONE;
      }
    }
  }

  return BIOS_VM;
}

uint16_t BIOS::HardwareMenuHandler(bool needsRedraw, bool performAction)
{
  static bool localRedraw = true;

  if (selectedMenuItem < 0)
    selectedMenuItem = sizeof(hardwareActions)-1;
  selectedMenuItem %= sizeof(hardwareActions);
  
  if (needsRedraw || localRedraw) {
    g_display->clrScr();
    DrawMenuBar();
    DrawHardwareMenu();
    g_display->flush();

    localRedraw = false;
  }

  if (performAction) {
    if (isActionActive(hardwareActions[selectedMenuItem])) {
     switch (hardwareActions[selectedMenuItem]) {
      case ACT_DISPLAYTYPE:
	g_displayType++;
	g_displayType %= 4; // FIXME: abstract max #
	((AppleDisplay*)g_display)->displayTypeChanged();
	localRedraw = true;
	break;
	
      case ACT_SPEED:
	currentCPUSpeedIndex++;
	currentCPUSpeedIndex %= 4;
	switch (currentCPUSpeedIndex) {
	case CPUSPEED_HALF:
	  g_speed = 1023000/2;
	  break;
	case CPUSPEED_DOUBLE:
	  g_speed = 1023000*2;
	  break;
	case CPUSPEED_QUAD:
	  g_speed = 1023000*4;
	  break;
	default:
	  g_speed = 1023000;
	  break;
	}
	localRedraw = true;
	break;

      case ACT_PADX_INV:
	g_invertPaddleX = !g_invertPaddleX;
#ifdef TEENSYDUINO
	((TeensyPaddles *)g_paddles)->setRev(g_invertPaddleX, g_invertPaddleY);
#endif
	localRedraw = true;
	break;

      case ACT_PADY_INV:
	g_invertPaddleY = !g_invertPaddleY;
#ifdef TEENSYDUINO
	((TeensyPaddles *)g_paddles)->setRev(g_invertPaddleX, g_invertPaddleY);
#endif
	localRedraw = true;
	break;

     case ACT_PADDLES:
       return BIOS_PADDLES;
       
     case ACT_VOLPLUS:
       g_volume ++;
       if (g_volume > 15) {
	g_volume = 15;
       }
       localRedraw = true;
       break;
       
     case ACT_VOLMINUS:
       g_volume--;
       if (g_volume < 0) {
	 g_volume = 0;
       }
       localRedraw = true;
       break;
     }
    }
  }

  return BIOS_HARDWARE;
}

uint16_t BIOS::DisksMenuHandler(bool needsRedraw, bool performAction)
{
  static bool localRedraw = true;

  if (selectedMenuItem < 0)
    selectedMenuItem = sizeof(diskActions)-1;
  selectedMenuItem %= sizeof(diskActions);
  
  if (needsRedraw || localRedraw) {
    g_display->clrScr();
    DrawMenuBar();
    DrawDisksMenu();
    g_display->flush();

    localRedraw = false;
  }

  if (performAction) {
    if (isActionActive(diskActions[selectedMenuItem])) {
     switch (diskActions[selectedMenuItem]) {
    case ACT_DISK1:
      if (((AppleVM *)g_vm)->DiskName(0)[0] != '\0') {
	((AppleVM *)g_vm)->ejectDisk(0);
	localRedraw = true;
	break;
      } else {
	strcpy(fileFilter, "dsk,.po,nib,woz");
	fileSelectionFor = ACT_DISK1;
	return BIOS_SELECTFILE;
      }
      break;
    case ACT_DISK2:
      if (((AppleVM *)g_vm)->DiskName(1)[0] != '\0') {
	((AppleVM *)g_vm)->ejectDisk(1);
	localRedraw = true;
	break;
      } else {
	strcpy(fileFilter, "dsk,.po,nib,woz");
	fileSelectionFor = ACT_DISK2;
	return BIOS_SELECTFILE;
      }
      break;
    case ACT_HD1:
      if (((AppleVM *)g_vm)->HDName(0)[0] != '\0') {
	((AppleVM *)g_vm)->ejectHD(0);
	localRedraw = true;
	break;
      } else {
	strcpy(fileFilter, "img");
	fileSelectionFor = ACT_HD1;
	return BIOS_SELECTFILE;
      }
      break;
    case ACT_HD2:
      if (((AppleVM *)g_vm)->HDName(1)[0] != '\0') {
	((AppleVM *)g_vm)->ejectHD(1);
	localRedraw = true;
	break;
      } else {
	strcpy(fileFilter, "img");
	fileSelectionFor = ACT_HD2;
	return BIOS_SELECTFILE;
      }
      break;
     }
    }
  }
  
  return BIOS_DISKS;
};

uint16_t BIOS::AboutScreenHandler(bool needsRedraw, bool performAction)
{
  static bool localRedraw = true;
  selectedMenuItem = 0;

  if (needsRedraw || localRedraw) {
    g_display->clrScr();

    g_display->drawString(M_SELECTED,
			  0,
			  0,
			  "Aiie! - an Apple //e emulator");
    
    g_display->drawString(M_NORMAL, 
			  15, 20,
			  "(c) 2017-2020 Jorj Bauer");
    
    g_display->drawString(M_NORMAL,
			  15, 38,
			  "https://github.com/JorjBauer/aiie/");
    
    g_display->drawString(M_NORMAL,
			  0,
			  200,
			  "Press return");
    
    g_display->flush();

    localRedraw = false;
  }

  if (performAction) {
    return BIOS_AIIE;
  }

  return BIOS_ABOUT;
}

uint16_t BIOS::PaddlesScreenHandler(bool needsRedraw, bool performAction)
{
  static bool localRedraw = true;
  selectedMenuItem = 0;
  static uint8_t lastPaddleX = g_paddles->paddle0();
  static uint8_t lastPaddleY = g_paddles->paddle1();

  if (g_paddles->paddle0() != lastPaddleX) {
    lastPaddleX = g_paddles->paddle0();
    localRedraw = true;
  }
  if (g_paddles->paddle1() != lastPaddleY) {
    lastPaddleY = g_paddles->paddle1();
    localRedraw = true;
  }
  
  if (needsRedraw || localRedraw) {
    char buf[50];
    g_display->clrScr();
    sprintf(buf, "Paddle X: %d    ", lastPaddleX);
    g_display->drawString(M_NORMAL, 0, 12, buf);
    sprintf(buf, "Paddle Y: %d    ", lastPaddleY);
    g_display->drawString(M_NORMAL, 0, 42, buf);
    g_display->drawString(M_NORMAL, 0, 92, "Press return to exit");
    g_display->flush();

    localRedraw = false;
  }

  if (performAction) {
    return BIOS_HARDWARE;
  }
  
  return BIOS_PADDLES;
}

static void insertDisk(int forWhat, const char *path,
		       const char *fileName)
{
  // drawIt is false b/c we don't want to draw it immediately -- that
  // would draw over the bios screen
  if (forWhat == ACT_DISK1 || forWhat == ACT_DISK2) {
    ((AppleVM *)g_vm)->insertDisk(forWhat == ACT_DISK1 ? 0 : 1, staticPathConcat(path, fileName), false);
  } else {
    // must be a hard drive
    ((AppleVM *)g_vm)->insertHD(forWhat == ACT_HD1 ? 0 : 1, staticPathConcat(path, fileName));
  }
}

uint16_t BIOS::SelectFileScreenHandler(bool needsRedraw, bool performAction)
{
  if (selectedMenuItem < 0)
    selectedMenuItem = BIOS_MAXFILES + 1;
  selectedMenuItem %= BIOS_MAXFILES + 2;

  static bool localRedraw = true;
  static int8_t page = 0;
  static uint16_t fileCount = 0;
  
  if (needsRedraw || localRedraw) {
    fileCount = DrawDiskNames(page, selectedMenuItem, fileFilter);

    localRedraw = false;
  }
  
  if (performAction) {
    if (selectedMenuItem == 0) {
      page--;
      if (page < 0) page = 0;
      //      else sel = BIOS_MAXFILES + 1;
      localRedraw = true;
    }
    else if (selectedMenuItem == BIOS_MAXFILES+1) {
      if (fileCount == BIOS_MAXFILES) { // don't let them select
					// 'Next' if there were no
					// files in the list or if the
					// list isn't full
	page++;
	//sel = 0;                                                                                                                      
      localRedraw = true;
      }
    } else if (strcmp(fileDirectory[selectedMenuItem-1], "../") == 0) {
      // Go up a directory (strip a directory name from rootPath)
      stripDirectory();
      page = 0;
      //sel = 0;                                                                                                                        
      localRedraw = true;
    } else if (fileDirectory[selectedMenuItem-1][strlen(fileDirectory[selectedMenuItem-1])-1] == '/') {
      // Descend in to the directory. FIXME: file path length?
      strcat(rootPath, fileDirectory[selectedMenuItem-1]);
      selectedMenuItem = 0;
      page = 0;
      localRedraw = true;
    } else {
      selectedFile = selectedMenuItem - 1;
      insertDisk(fileSelectionFor, rootPath, fileDirectory[selectedFile]);

      g_display->flush();
      return BIOS_DISKS;
    }
  }
  return BIOS_SELECTFILE;
}

/*
bool BIOS::runUntilDone()
{
  // Reset the cache
  cachedPath[0] = 0;
  numCacheEntries = 0;

  g_filemanager->getRootPath(rootPath, sizeof(rootPath));

  // FIXME: abstract these constant speeds
  currentCPUSpeedIndex = CPUSPEED_FULL;
  if (g_speed == 1023000/2)
    currentCPUSpeedIndex = CPUSPEED_HALF;
  if (g_speed == 1023000*2)
    currentCPUSpeedIndex = CPUSPEED_DOUBLE;
  if (g_speed == 1023000*4)
    currentCPUSpeedIndex = CPUSPEED_QUAD;

  int8_t prevAction = ACT_EXIT;
  while (1) {
    switch (prevAction = GetAction(prevAction)) {

      ***
      //      ConfigurePaddles();
***
    }
  }

 done:
  // Undo whatever damage we've done to the screen
  g_display->redraw();
  AiieRect r = { 0, 0, 191, 279 };
  g_display->blit(r);

  // return true if any persistent setting changed that we want to store in eeprom
  return true;
}
*/

void BIOS::WarmReset()
{
  g_cpu->Reset();
}

void BIOS::ColdReboot()
{
  g_vm->Reset();
  g_cpu->Reset();
}

bool BIOS::isActionActive(int8_t action)
{
  // don't return true for disk events that aren't valid
  switch (action) {
  case ACT_EXIT:
  case ACT_RESET:
  case ACT_COLDBOOT:
  case ACT_MONITOR:
  case ACT_DISPLAYTYPE:
  case ACT_SPEED:
  case ACT_ABOUT:
  case ACT_DEBUG:
  case ACT_DISK1:
  case ACT_DISK2:
  case ACT_HD1:
  case ACT_HD2:
  case ACT_SUSPEND:
  case ACT_RESTORE:
  case ACT_PADX_INV:
  case ACT_PADY_INV:
  case ACT_PADDLES:
    return true;

  case ACT_VOLPLUS:
    return (g_volume < 15);
  case ACT_VOLMINUS:
    return (g_volume > 0);
  }

  /* NOTREACHED */
  return false;
}

void BIOS::DrawAiieMenu()
{
  if (selectedMenuItem < 0)
    selectedMenuItem = sizeof(aiieActions)-1;
  selectedMenuItem %= sizeof(aiieActions);

  char buf[40];
  for (int i=0; i<sizeof(aiieActions); i++) {
    switch (aiieActions[i]) {
    case ACT_ABOUT:
      sprintf(buf, "About...");
      break;
    }

    if (isActionActive(aiieActions[i])) {
      g_display->drawString(selectedMenuItem == i ? M_SELECTED : M_NORMAL, 10, 20 + 14 * i, buf);
    } else {
      g_display->drawString(selectedMenuItem == i ? M_SELECTDISABLED : M_DISABLED, 10, 20 + 14 * i,
			    buf);
    }
  }
}

void BIOS::DrawVMMenu()
{
  if (selectedMenuItem < 0)
    selectedMenuItem = sizeof(vmActions)-1;

  selectedMenuItem %= sizeof(vmActions);

  char buf[40];
  for (int i=0; i<sizeof(vmActions); i++) {
    switch (vmActions[i]) {
    case ACT_DEBUG:
      {
	const char *templateString = "Debug: %s";
	switch (g_debugMode) {
	case D_NONE:
	  sprintf(buf, templateString, "off");
	  break;
	case D_SHOWFPS:
	  sprintf(buf, templateString, "Show FPS");
	  break;
	case D_SHOWMEMFREE:
	  sprintf(buf, templateString, "Show mem free");
	  break;
	case D_SHOWPADDLES:
	  sprintf(buf, templateString, "Show paddles");
	  break;
	case D_SHOWPC:
	  sprintf(buf, templateString, "Show PC");
	  break;
	case D_SHOWCYCLES:
	  sprintf(buf, templateString, "Show cycles");
	  break;
	case D_SHOWBATTERY:
	  sprintf(buf, templateString, "Show battery");
	  break;
	case D_SHOWTIME:
	  sprintf(buf, templateString, "Show time");
	  break;
	case D_SHOWDSK:
	  sprintf(buf, templateString, "Show Disk");
	  break;
	}
      }
      break;
    case ACT_EXIT:
      strcpy(buf, "Resume");
      break;
    case ACT_RESET:
      strcpy(buf, "Reset");
      break;
    case ACT_COLDBOOT:
      strcpy(buf, "Cold Reboot");
      break;
    case ACT_MONITOR:
      strcpy(buf, "Drop to Monitor");
      break;
    case ACT_SUSPEND:
      strcpy(buf, "Suspend VM");
      break;
    case ACT_RESTORE:
      strcpy(buf, "Restore VM");
      break;
    }

    if (isActionActive(vmActions[i])) {
      g_display->drawString(selectedMenuItem == i ? M_SELECTED : M_NORMAL, 10, 20 + 14 * i, buf);
    } else {
      g_display->drawString(selectedMenuItem == i ? M_SELECTDISABLED : M_DISABLED, 10, 20 + 14 * i, buf);
    }
  }
}

void BIOS::DrawHardwareMenu()
{
  if (selectedMenuItem < 0)
    selectedMenuItem = sizeof(hardwareActions)-1;

  selectedMenuItem %= sizeof(hardwareActions);

  char buf[40];
  for (int i=0; i<sizeof(hardwareActions); i++) {
    switch (hardwareActions[i]) {
    case ACT_DISPLAYTYPE:
      {
	const char *templateString = "Display: %s";
	switch (g_displayType) {
	case m_blackAndWhite:
	  sprintf(buf, templateString, "B&W");
	  break;
	case m_monochrome:
	  sprintf(buf, templateString, "Mono");
	  break;
	case m_ntsclike:
	  sprintf(buf, templateString, "NTSC-like");
	  break;
	case m_perfectcolor:
	  sprintf(buf, templateString, "RGB");
	  break;
	}
      }
      break;
    case ACT_SPEED:
      {
	const char *templateString = "CPU Speed: %s";
	switch (currentCPUSpeedIndex) {
	case CPUSPEED_HALF:
	  sprintf(buf, templateString, "Half [511.5 kHz]");
	  break;
	case CPUSPEED_DOUBLE:
	  sprintf(buf, templateString, "Double (2.046 MHz)");
	  break;
	case CPUSPEED_QUAD:
	  sprintf(buf, templateString, "Quad (4.092 MHz)");
	  break;
	default:
	  sprintf(buf, templateString, "Normal (1.023 MHz)");
	  break;
	}
      }
      break;
    case ACT_PADX_INV:
      if (g_invertPaddleX)
	strcpy(buf, "Paddle X inverted");
      else
	strcpy(buf, "Paddle X normal");
      break;
    case ACT_PADY_INV:
      if (g_invertPaddleY)
	strcpy(buf, "Paddle Y inverted");
      else
	strcpy(buf, "Paddle Y normal");
      break;
    case ACT_PADDLES:
      strcpy(buf, "Configure paddles");
      break;
    case ACT_VOLPLUS:
      strcpy(buf, "Volume +");
      break;
    case ACT_VOLMINUS:
      strcpy(buf, "Volume -");
      break;
    }

    if (isActionActive(hardwareActions[i])) {
      g_display->drawString(selectedMenuItem == i ? M_SELECTED : M_NORMAL, 10, 20 + 14 * i, buf);
    } else {
      g_display->drawString(selectedMenuItem == i ? M_SELECTDISABLED : M_DISABLED, 10, 20 + 14 * i, buf);
    }
  }
  
  // draw the volume bar                                                                            
  uint16_t volCutoff = 300.0 * (float)((float) g_volume / 15.0);
  for (uint8_t y=234; y<=235; y++) {
    for (uint16_t x = 0; x< 300; x++) {
      g_display->drawPixel( x, y, x <= volCutoff ? 0xFFFF : 0x0010 );
    }
  }
}

void BIOS::DrawDisksMenu()
{
  if (selectedMenuItem < 0)
    selectedMenuItem = sizeof(diskActions)-1;

  selectedMenuItem %= sizeof(diskActions);

  char buf[80];
  for (int i=0; i<sizeof(diskActions); i++) {
    switch (diskActions[i]) {
    case ACT_DISK1:
    case ACT_DISK2:
      {
	const char *insertedDiskName = ((AppleVM *)g_vm)->DiskName(diskActions[i]==ACT_DISK2 ? 1 : 0);
	// Get the name of the file; strip off the directory
	const char *endPtr = &insertedDiskName[strlen(insertedDiskName)-1];
	while (endPtr != insertedDiskName &&
	       *endPtr != '/') {
	  endPtr--;
	}
	if (*endPtr == '/') {
	  endPtr++;
	}

	if (insertedDiskName[0]) {
	  snprintf(buf, sizeof(buf), "Eject Disk %d [%s]", diskActions[i]==ACT_DISK2 ? 2 : 1, endPtr);
	} else {
	  sprintf(buf, "Insert Disk %d", diskActions[i]==ACT_DISK2 ? 2 : 1);
	}
      }
      break;
    case ACT_HD1:
    case ACT_HD2:
      {
	const char *insertedDiskName = ((AppleVM *)g_vm)->HDName(diskActions[i]==ACT_HD2 ? 1 : 0);
	// Get the name of the file; strip off the directory
	const char *endPtr = &insertedDiskName[strlen(insertedDiskName)-1];
	while (endPtr != insertedDiskName &&
	       *endPtr != '/') {
	  endPtr--;
	}
	if (*endPtr == '/') {
	  endPtr++;
	}

	if (insertedDiskName[0]) {
	  snprintf(buf, sizeof(buf), "Remove HD %d [%s]", diskActions[i]==ACT_HD2 ? 2 : 1, endPtr);
	} else {
	  sprintf(buf, "Connect HD %d", diskActions[i]==ACT_HD2 ? 2 : 1);
	}
      }
      break;
    }

    if (isActionActive(diskActions[i])) {
      g_display->drawString(selectedMenuItem == i ? M_SELECTED : M_NORMAL, 10, 20 + 14 * i, buf);
    } else {
      g_display->drawString(selectedMenuItem == i ? M_SELECTDISABLED : M_DISABLED, 10, 20 + 14 * i, buf);
    }
  }
}


void BIOS::DrawCurrentMenu()
{
  switch (selectedMenu) {
  case 0: // Aiie
    DrawAiieMenu();
    break;
  case 1: // VM
    DrawVMMenu();
    break;
  case 2: // Hardware
    DrawHardwareMenu();
    break;
  case 3: // Disks
    DrawDisksMenu();
    break;
  }
}

void BIOS::stripDirectory()
{
  rootPath[strlen(rootPath)-1] = '\0'; // remove the last character

  while (rootPath[0] && rootPath[strlen(rootPath)-1] != '/') {
    rootPath[strlen(rootPath)-1] = '\0'; // remove the last character again
  }

  // We're either at the previous directory, or we've nulled out the whole thing.

  if (rootPath[0] == '\0') {
    // Never go beyond this
    strcpy(rootPath, "/");
  }
}

uint16_t BIOS::DrawDiskNames(uint8_t page, int8_t selection, const char *filter)
{
  uint16_t fileCount = GatherFilenames(page, filter);
  g_display->clrScr();
  g_display->drawString(M_NORMAL, 0, 12, "BIOS Configuration - pick disk");

  if (page == 0) {
    g_display->drawString(selection == 0 ? M_SELECTDISABLED : M_DISABLED, 10, 50, "<Prev>");
  } else {
    g_display->drawString(selection == 0 ? M_SELECTED : M_NORMAL, 10, 50, "<Prev>");
  }

  uint8_t i;
  for (i=0; i<BIOS_MAXFILES; i++) {
    if (i < fileCount) {
      g_display->drawString((i == selection-1) ? M_SELECTED : M_NORMAL, 10, 50 + 14 * (i+1), fileDirectory[i]);
    } else {
      g_display->drawString((i == selection-1) ? M_SELECTDISABLED : M_DISABLED, 10, 50+14*(i+1), "-");
    }

  }

  // FIXME: this doesn't accurately say whether or not there *are* more.
  if (fileCount < BIOS_MAXFILES) {
    g_display->drawString((i+1 == selection) ? M_SELECTDISABLED : M_DISABLED, 10, 50 + 14 * (i+1), "<Next>");
  } else {
    g_display->drawString(i+1 == selection ? M_SELECTED : M_NORMAL, 10, 50 + 14 * (i+1), "<Next>");
  }

  g_display->flush();
  return fileCount;
}

// Read a directory, cache all the entries
uint16_t BIOS::cacheAllEntries(const char *filter)
{
  // If we've already cached this directory, then just return it
  if (numCacheEntries && !strcmp(cachedPath, rootPath) && !strcmp(cachedFilter, filter))
    return numCacheEntries;

  // Otherwise flush the cache and start over
  numCacheEntries = 0;
  strcpy(cachedPath, rootPath);
  strcpy(cachedFilter, filter);
  
  // This could be a lengthy process, so...
  g_display->clrScr();
  g_display->drawString(M_SELECTED,
                        0,
                        0,
                        "Loading...");
  g_display->flush();
  
  // read all the entries we can find
  int16_t idx = 0;
  while (1) {
    struct _cacheEntry *ce = &biosCache[numCacheEntries];
    idx = g_filemanager->readDir(rootPath, filter, ce->fn, idx, BIOS_MAXPATH);
    if (idx == -1) {
      return numCacheEntries;
    }
    idx++;
    numCacheEntries++;
    if (numCacheEntries >= BIOSCACHESIZE-1) {
      return numCacheEntries;
    }
  }
  /* NOTREACHED */
}

void BIOS::swapCacheEntries(int a, int b)
{
  struct _cacheEntry tmpEntry;
  strcpy(tmpEntry.fn, biosCache[a].fn);
  strcpy(biosCache[a].fn, biosCache[b].fn);
  strcpy(biosCache[b].fn, tmpEntry.fn);
}

// Take all the entries in the cache and sort htem
void BIOS::sortCachedEntries()
{
  if (numCacheEntries <= 1)
    return;

  bool changedAnything = true;
  while (changedAnything) {
    changedAnything = false;
    for (int i=0; i<numCacheEntries-1; i++) {
      if (strcmp(biosCache[i].fn, biosCache[i+1].fn) > 0) {
	swapCacheEntries(i, i+1);
	changedAnything = true;
      }
    }
  }  
}

uint16_t BIOS::GatherFilenames(uint8_t pageOffset, const char *filter)
{
  uint8_t startNum = 10 * pageOffset;
  uint8_t count = 0; // number we're including in our listing

  uint16_t numEntriesTotal = cacheAllEntries(filter);
  sortCachedEntries();
  if (numEntriesTotal > BIOSCACHESIZE) {
    // ... umm, this is a problem. FIXME?
  }
  struct _cacheEntry *nextEntry = biosCache;
  while (startNum) {
    nextEntry++;
    startNum--;
  }

  while (1) {
    if (nextEntry->fn[0] == 0)
      return count;

    strncpy(fileDirectory[count], nextEntry->fn, BIOS_MAXPATH);
    count++;

    if (count >= BIOS_MAXFILES) {
      return count;
    }
    nextEntry++;
  }
}
	
