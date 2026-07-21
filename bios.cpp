#include <string.h>
#include "globals.h"
#include "bios.h"
#include "usernet.h"   // unParseSubnet (validate the NAT subnet field)

#include "applevm.h"
#include "physicalkeyboard.h"
#include "physicaldisplay.h"
#include "cpu.h"
#include "appledisplay.h"

#ifdef TEENSYDUINO
#include <Bounce2.h>
#include "teensy-paddles.h"
#include "teensy-selfupdate.h"
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
#define BIOSCACHESIZE 2048
EXTMEM char cachedPath[BIOS_MAXPATH] = {0};
EXTMEM char cachedFilter[BIOS_MAXPATH] = {0};
EXTMEM struct _cacheEntry biosCache[BIOSCACHESIZE];
uint16_t numCacheEntries = 0;

// When selecting files...
char fileFilter[16]; // FIXME length & Strcpy -> strncpy
uint16_t fileSelectionFor; // define what the returned name is for

#define LINEHEIGHT 10
#define MENUINDENT 10
#define MAXFILESPERPAGE BIOS_MAXFILES
#define FILEMENUSTARTAT (LINEHEIGHT+1)

// menu screen enums
enum {
  // Tabs shown in the menu bar (0 .. NUM_TITLES-1), navigated with left/right.
  BIOS_AIIE = 0,
  BIOS_VM = 1,
  BIOS_HARDWARE = 2,
  BIOS_CARDS = 3,
  BIOS_WIFI = 4,        // the "Net" tab (network / Uthernet / WiFi setup)
  BIOS_DISKS = 5,

  // Modal sub-screens, reached from within a tab (must be >= NUM_TITLES so the
  // left/right tab navigation skips them).
  BIOS_ABOUT = 6,
  BIOS_PADDLES = 7,
  BIOS_SELECTFILE = 8,

  BIOS_DONE = 99,
};
  

enum {
  ACT_EXIT = 1,
  ACT_RESET = 2,
  ACT_REBOOT = 3,
  ACT_REBOOTANDEJECT = 4,
  ACT_MONITOR = 5,
  ACT_DISPLAYTYPE = 6,
  ACT_LUMINANCEUP = 7,
  ACT_LUMINANCEDOWN = 8,
  ACT_DEBUG = 9,
  ACT_DISK1 = 10,
  ACT_DISK2 = 11,
  ACT_HD1 = 12,
  ACT_HD2 = 13,
  ACT_VOLPLUS = 14,
  ACT_VOLMINUS = 15,
  ACT_SUSPEND = 16,
  ACT_RESTORE = 17,
  ACT_PADX_INV = 18,
  ACT_PADY_INV = 19,
  ACT_PADDLES = 20,
  ACT_SPEED = 21,
  ACT_ABOUT = 22,
  ACT_SLOT_DISKII = 23,
  ACT_SLOT_PARALLEL = 24,
  ACT_SLOT_HD32 = 25,
  ACT_SLOT_MOUSE = 26,
  ACT_SLOT_MOCKINGBOARD = 27,
  ACT_SLOT_DEFAULTS = 28,
  ACT_SLOT_RAMWORKS = 29,
  ACT_UPDATEFW = 30,
  ACT_SLOT_UTHERNET = 31,
  ACT_WIFI = 32,
};

// The tab bar is drawn across a 320px-wide display with a fixed 8px font, so the
// sum of (titleWidths[i] + 2*XPADDING) must stay under 320. The five original
// tabs use ~266px; "Net" is kept short so the sixth tab still fits.
#define NUM_TITLES 6
const char *menuTitles[NUM_TITLES] = { "Aiie", "VM", "Hardware", "Cards", "Net", "Disks" };
const uint8_t titleWidths[NUM_TITLES] = {45, 28, 80, 48, 32, 45 };

const uint8_t aiieActions[] = { ACT_ABOUT };

const uint8_t vmActions[] = { ACT_EXIT, ACT_RESET, ACT_REBOOT, ACT_REBOOTANDEJECT,
                              ACT_MONITOR,
			      ACT_DEBUG, ACT_SUSPEND, ACT_RESTORE,
#ifdef TEENSYDUINO
			      ACT_UPDATEFW,
#endif
};
const uint8_t hardwareActions[] = { ACT_DISPLAYTYPE,  ACT_LUMINANCEUP,
                                    ACT_LUMINANCEDOWN, ACT_SPEED,
				    ACT_PADX_INV, ACT_PADY_INV,
				    ACT_PADDLES, ACT_VOLPLUS, ACT_VOLMINUS };
// Network / WiFi setup moved to its own "Net" tab (BIOS_WIFI); it is no longer
// a row in the Cards menu.
const uint8_t cardsActions[] = { ACT_SLOT_DISKII, ACT_SLOT_PARALLEL,
				 ACT_SLOT_HD32, ACT_SLOT_MOUSE,
				 ACT_SLOT_MOCKINGBOARD, ACT_SLOT_UTHERNET,
				 ACT_SLOT_RAMWORKS,
				 ACT_SLOT_DEFAULTS };
const uint8_t diskActions[] = { ACT_DISK1, ACT_DISK2,
				ACT_HD1, ACT_HD2 };

static uint8_t savedSlotDiskII;
static uint8_t savedSlotParallel;
static uint8_t savedSlotHD32;
static uint8_t savedSlotMouse;
static uint8_t savedSlotMockingboard;
static uint8_t savedSlotUthernet;
static uint8_t savedRamworksSize;
static bool cardsConfigChanged = false;
static bool cardsConfigSaved = false;

// Slots a card can be assigned to. 0 means "disabled". Slot 3 is allowed: an
// I/O-only card (e.g. the Uthernet) coexists with the internal 80-column
// firmware, which the MMU now keeps in a separate bank from a slot-3 card's ROM
// (see _slotRomPageForSlot in applemmu.cpp). A card WITH a boot ROM in slot 3 is
// reachable only via SETC3ROM, so it is not seen by the boot scan.
static const uint8_t kSelectableSlots[] = { 0, 1, 2, 3, 4, 5, 6, 7 };

static bool isSelectableSlot(uint8_t n)
{
  for (size_t i = 0; i < sizeof(kSelectableSlots); i++) {
    if (kSelectableSlots[i] == n) return true;
  }
  return false;
}

#define CPUSPEED_HALF 0
#define CPUSPEED_FULL 1
#define CPUSPEED_DOUBLE 2
#define CPUSPEED_QUAD 3
#ifdef TEENSYDUINO
// The Teensy can't sustain more than 4x
#define NUM_CPUSPEEDS 4
#else
#define CPUSPEED_8X 4
#define CPUSPEED_16X 5
// Audio is muted at 128x and beyond -- the speaker would have to
// time-compress 128 seconds of toggles into 1, which is just noise.
#define CPUSPEED_128X 6
#define CPUSPEED_256X 7
#define NUM_CPUSPEEDS 8
#endif

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

  savedSlotDiskII = g_slotDiskII;
  savedSlotParallel = g_slotParallel;
  savedSlotHD32 = g_slotHD32;
  savedSlotMouse = g_slotMouse;
  savedSlotMockingboard = g_slotMockingboard;
  savedSlotUthernet = g_slotUthernet;
  savedRamworksSize = g_ramworksSize;
  cardsConfigChanged = false;
  cardsConfigSaved = false;
}

BIOS::~BIOS()
{
}

void BIOS::DrawMenuBar()
{
  // Wide enough to hold the full tab-bar width: six tabs run past 255px, so a
  // uint8_t here would wrap and draw the last tab back over the first.
  uint16_t xpos = 0;

  if (selectedMenu < 0) {
    selectedMenu = NUM_TITLES-1;
  }
  selectedMenu %= NUM_TITLES;

#define XPADDING 2

  for (int i=0; i<NUM_TITLES; i++) {
    for (int x=0; x<titleWidths[i] + 2*XPADDING; x++) {
      g_display->drawPixel(xpos+x, 0, 0xFFFF);
      g_display->drawPixel(xpos+x, LINEHEIGHT, 0xFFFF);
    }
    for (int y=0; y<=LINEHEIGHT; y++) {
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

    savedSlotDiskII = g_slotDiskII;
    savedSlotParallel = g_slotParallel;
    savedSlotHD32 = g_slotHD32;
    savedSlotMouse = g_slotMouse;
    savedSlotMockingboard = g_slotMockingboard;
    savedSlotUthernet = g_slotUthernet;
    savedRamworksSize = g_ramworksSize;
    cardsConfigChanged = false;
  }

#ifdef TEENSYDUINO
  if (resetButtonDebouncer.read() == LOW) {
    // wait until it's no longer pressed
    while (resetButtonDebouncer.read() == LOW)
      runDebouncer();
    delay(100); // wait long enough for it to debounce
    return (BIOS_DONE != 0);
  }
#endif

  bool hitReturn = false;
  int8_t lastKey = PK_NONE;

  uint16_t rv = BIOS_DONE;
  bool changingMenu = false;
  uint16_t enteredMenu = selectedMenu;   // to notice when we leave the Net tab
  if (g_keyboard->kbhit()) {
    lastKey = g_keyboard->read();
    switch (lastKey) {
    case PK_ESC:
      if (selectedMenu == BIOS_VM) {
        // On the VM tab, ESC does exactly what Return on "Resume" does: attempt
        // to resume. Highlight Resume (item 0) first so that if it ever cannot
        // resume (e.g. a card change disables it), the user sees the same result
        // as pressing Return on Resume rather than silently doing nothing.
        selectedMenuItem = 0;   // Resume == vmActions[0] (ACT_EXIT)
        hitReturn = true;
      } else {
        // From any other menu, jump to the VM tab with Resume selected.
        selectedMenu = BIOS_VM;
        selectedMenuItem = 0;
        changingMenu = true;
      }
      needsRedraw = true;
      break;
    case PK_DARR:
      selectedMenuItem++; // modded by current action
      needsRedraw = true;
      break;
    case PK_UARR:
      selectedMenuItem--; // modded by current action
      needsRedraw = true;
      break;
    case PK_RARR:
      if (selectedMenu < NUM_TITLES) {
	selectedMenuItem = 0;
	selectedMenu++;
	selectedMenu %= NUM_TITLES;
	changingMenu = true;
	needsRedraw = true;
      }
      break;
    case PK_LARR:
      if (selectedMenu < NUM_TITLES) {
	selectedMenuItem = 0;
	selectedMenu--;
	changingMenu = true;
	if (selectedMenu < 0) {
	  selectedMenu = NUM_TITLES-1;
	}
	needsRedraw = true;
      }
      break;
    case PK_RET:
      hitReturn = true;
      needsRedraw = true;
      break;
    default:
      break;
    }
  }

  if (changingMenu && selectedMenu == BIOS_HARDWARE) {
    // Need to initialize the CPU speed from g_speed
    switch (g_speed) {
    case 1023000:
      currentCPUSpeedIndex = CPUSPEED_FULL;
      break;
    case 1023000/2:
      currentCPUSpeedIndex = CPUSPEED_HALF;
      break;
    case 1023000*2:
      currentCPUSpeedIndex = CPUSPEED_DOUBLE;
      break;
    case 1023000*4:
      currentCPUSpeedIndex = CPUSPEED_QUAD;
      break;
#ifndef TEENSYDUINO
    case 1023000*8:
      currentCPUSpeedIndex = CPUSPEED_8X;
      break;
    case 1023000*16:
      currentCPUSpeedIndex = CPUSPEED_16X;
      break;
    case 1023000*128:
      currentCPUSpeedIndex = CPUSPEED_128X;
      break;
    case 1023000*256:
      currentCPUSpeedIndex = CPUSPEED_256X;
      break;
#endif
    default:
      // Dunno what happened, but we'll default back to full (normal) speed
      currentCPUSpeedIndex = CPUSPEED_FULL;
      g_speed = 1023000;
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
  case BIOS_CARDS:
    rv = CardsMenuHandler(needsRedraw, hitReturn, lastKey);
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
  case BIOS_WIFI:
    rv = WiFiScreenHandler(needsRedraw, hitReturn, lastKey);
    break;
  }

  if (rv != selectedMenu) {
    selectedMenuItem = 0;
    needsRedraw = true;
    selectedMenu = rv;
  }
  else
    needsRedraw = false; // assume the handler drew

  if (enteredMenu == BIOS_WIFI && selectedMenu != BIOS_WIFI) {
    // Left the Net tab (via ESC, a tab switch, or resume): push any edited
    // inbound-forward list / port offset to the live NAT so it takes effect
    // this session without a restart.
    if (g_uthernet) g_uthernet->applyForwardConfig();
  }

  if (selectedMenu == BIOS_DONE && cardsConfigChanged) {
    g_display->clrScr(c_darkblue);
    g_display->drawString(M_SELECTED, 80, 100, "Reconfiguring slots...");
    g_display->flush();
    ((AppleVM *)g_vm)->reassignSlots();
    cardsConfigChanged = false;
  }

  return ((selectedMenu == BIOS_DONE) ? false : true);
}

uint16_t BIOS::AiieMenuHandler(bool needsRedraw, bool performAction)
{
  static bool localRedraw = true;
  if (selectedMenuItem < 0)
    selectedMenuItem = sizeof(aiieActions)-1;
  selectedMenuItem %= sizeof(aiieActions);
  
  if (needsRedraw || localRedraw) {
    g_display->clrScr(c_darkblue);
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
    g_display->clrScr(c_darkblue);
    DrawMenuBar();
    DrawVMMenu();

    g_display->flush();

    localRedraw = false;
  }

  if (performAction) {
    if (isActionActive(vmActions[selectedMenuItem])) {
      switch (vmActions[selectedMenuItem]) {
      case ACT_EXIT:
	if (cardsConfigChanged) {
	  g_display->clrScr(c_darkblue);
	  g_display->drawString(M_SELECTED, 80, 100, "Reconfiguring slots...");
	  g_display->flush();
	  ((AppleVM *)g_vm)->reassignSlots();
	  cardsConfigChanged = false;
	}
	return BIOS_DONE;
      case ACT_RESET:
	if (cardsConfigChanged) {
	  ((AppleVM *)g_vm)->reassignSlots();
	  cardsConfigChanged = false;
	}
	WarmReset();
	return BIOS_DONE;
      case ACT_REBOOT:
	// Reboot, but don't eject disks
	if (cardsConfigChanged) {
	  ((AppleVM *)g_vm)->reassignSlots();
	  cardsConfigChanged = false;
	}
	RebootAsIs();
	return BIOS_DONE;
      case ACT_REBOOTANDEJECT:
	// Power off and on, ejecting disks
	if (cardsConfigChanged) {
	  ((AppleVM *)g_vm)->reassignSlots();
	  cardsConfigChanged = false;
	}
	ColdReboot();
	return BIOS_DONE;
      case ACT_MONITOR:
	((AppleVM *)g_vm)->Monitor();
	return BIOS_DONE;
      case ACT_DEBUG:
	g_debugMode++;
	g_debugMode %= 10; // FIXME: abstract max #
	localRedraw = true;
	return BIOS_VM;
      case ACT_SUSPEND:
	g_display->clrScr(c_darkblue);
	g_display->drawString(M_SELECTED, 80, 100,"Suspending VM...");
	g_display->flush();
	// CPU is already suspended, so this is safe...
	((AppleVM *)g_vm)->Suspend("suspend.vm");
	localRedraw = true;
	return BIOS_VM;
      case ACT_RESTORE:
	g_display->clrScr(c_darkblue);
	g_display->drawString(M_SELECTED, 80, 100,"Resuming VM...");
	g_display->flush();
	((AppleVM *)g_vm)->Resume("suspend.vm");
	return BIOS_DONE;
#ifdef TEENSYDUINO
      case ACT_UPDATEFW:
	// Reflash from /AIIE.HEX on the SD card. On success this reboots into
	// the new firmware and never returns; on failure (missing/invalid
	// file) it leaves the machine untouched and we redraw the menu.
	teensySelfUpdateFromSD("/AIIE.HEX");
	localRedraw = true;
	return BIOS_VM;
#endif
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
    g_display->clrScr(c_darkblue);
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

     case ACT_LUMINANCEUP:
       if (g_luminanceCutoff < 255)
	 g_luminanceCutoff++;
	((AppleDisplay*)g_display)->displayTypeChanged();
	localRedraw = true;
       break;
       
     case ACT_LUMINANCEDOWN:
       if (g_luminanceCutoff > 0)
	 g_luminanceCutoff--;
	((AppleDisplay*)g_display)->displayTypeChanged();
	localRedraw = true;
       break;
	
      case ACT_SPEED:
	currentCPUSpeedIndex++;
	currentCPUSpeedIndex %= NUM_CPUSPEEDS;
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
#ifndef TEENSYDUINO
	case CPUSPEED_8X:
	  g_speed = 1023000*8;
	  break;
	case CPUSPEED_16X:
	  g_speed = 1023000*16;
	  break;
	case CPUSPEED_128X:
	  g_speed = 1023000*128;
	  break;
	case CPUSPEED_256X:
	  g_speed = 1023000*256;
	  break;
#endif
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

static uint8_t *slotVarForAction(uint8_t action)
{
  switch (action) {
  case ACT_SLOT_DISKII: return &g_slotDiskII;
  case ACT_SLOT_PARALLEL: return &g_slotParallel;
  case ACT_SLOT_HD32: return &g_slotHD32;
  case ACT_SLOT_MOUSE: return &g_slotMouse;
  case ACT_SLOT_MOCKINGBOARD: return &g_slotMockingboard;
  case ACT_SLOT_UTHERNET: return &g_slotUthernet;
  }
  return NULL;
}

static void resolveSlotConflict(uint8_t *changedVar)
{
  uint8_t *allSlots[] = { &g_slotDiskII, &g_slotParallel, &g_slotHD32,
                          &g_slotMouse, &g_slotMockingboard, &g_slotUthernet };
  const int nSlots = sizeof(allSlots) / sizeof(allSlots[0]);
  uint8_t newSlot = *changedVar;
  if (newSlot == 0) return;

  for (int i = 0; i < nSlots; i++) {
    if (allSlots[i] == changedVar) continue;
    if (*allSlots[i] == newSlot) {
      // The mouse only works in slot 4; if something takes slot 4, disable the
      // mouse rather than relocate it to a slot where its ROM won't work.
      if (allSlots[i] == &g_slotMouse) {
        *allSlots[i] = 0;
        continue;
      }
      // Find an available slot for the displaced card
      uint8_t validSlots[] = { 1, 2, 4, 5, 6, 7 };
      bool found = false;
      for (int s = 0; s < 6 && !found; s++) {
        uint8_t candidate = validSlots[s];
        bool taken = false;
        for (int j = 0; j < nSlots; j++) {
          if (allSlots[j] != allSlots[i] && *allSlots[j] == candidate) {
            taken = true;
            break;
          }
        }
        if (!taken) {
          *allSlots[i] = candidate;
          found = true;
        }
      }
      if (!found) {
        *allSlots[i] = 0;
      }
    }
  }
}

static bool slotsMatchSaved()
{
  return (g_slotDiskII == savedSlotDiskII &&
          g_slotParallel == savedSlotParallel &&
          g_slotHD32 == savedSlotHD32 &&
          g_slotMouse == savedSlotMouse &&
          g_slotMockingboard == savedSlotMockingboard &&
          g_slotUthernet == savedSlotUthernet &&
          g_ramworksSize == savedRamworksSize);
}

uint16_t BIOS::CardsMenuHandler(bool needsRedraw, bool performAction, int8_t key)
{
  static bool localRedraw = true;

  if (selectedMenuItem < 0)
    selectedMenuItem = sizeof(cardsActions)-1;
  selectedMenuItem %= sizeof(cardsActions);

  // Press a digit to put the selected card directly in that slot (0 disables
  // it). Only acts on a card row, and only for a selectable slot; other digits
  // (8, 9) are ignored rather than cycling anything.
  if (key >= '0' && key <= '9') {
    uint8_t newSlot = (uint8_t)(key - '0');
    uint8_t action = cardsActions[selectedMenuItem];
    if (action == ACT_SLOT_RAMWORKS) {
      // The RamWorks row has no slot; '0' clears it, matching how '0' disables
      // the slot cards. Other digits do nothing here.
      if (key == '0') {
        g_ramworksSize = 0;
        cardsConfigChanged = !slotsMatchSaved();
        localRedraw = true;
      }
    } else {
      uint8_t *var = slotVarForAction(action);
      // The mouse card only works in slot 4 (its firmware ROM is slot-4-only),
      // so restrict it to slot 4 or 0 (disabled). Others take any selectable slot.
      bool slotOk = (action == ACT_SLOT_MOUSE) ? (newSlot == 0 || newSlot == 4)
                                               : isSelectableSlot(newSlot);
      if (var && slotOk) {
        g_display->clrScr(c_darkblue);
        g_display->drawString(M_SELECTED, 80, 100, "Updating slots...");
        g_display->flush();
        *var = newSlot;
        if (newSlot != 0) resolveSlotConflict(var); // move any card already there
        cardsConfigChanged = !slotsMatchSaved();
        localRedraw = true;
      }
    }
  }

  if (needsRedraw || localRedraw) {
    g_display->clrScr(c_darkblue);
    DrawMenuBar();
    DrawCardsMenu();
    g_display->flush();
    localRedraw = false;
  }

  // Return, '+' and '=' all advance the selected entry to its next value; '-'
  // steps backward. This gives the slot/size rows a two-way cycle from the
  // keyboard, on top of the direct digit entry above.
  int cycleStep = performAction ? 1 : 0;
  if (key == '+' || key == '=') { cycleStep = 1; performAction = true; }
  else if (key == '-')          { cycleStep = -1; performAction = true; }

  if (performAction) {
    if (isActionActive(cardsActions[selectedMenuItem])) {
      switch (cardsActions[selectedMenuItem]) {
      case ACT_SLOT_MOUSE:
        {
          // The mouse only works in slot 4 (slot-4-only ROM); toggle 4 <-> off.
          uint8_t *var = slotVarForAction(ACT_SLOT_MOUSE);
          if (var) {
            g_display->clrScr(c_darkblue);
            g_display->drawString(M_SELECTED, 80, 100, "Updating slots...");
            g_display->flush();
            *var = (*var == 4) ? 0 : 4;
            if (*var != 0) resolveSlotConflict(var);
            cardsConfigChanged = !slotsMatchSaved();
          }
          localRedraw = true;
        }
        break;
      case ACT_SLOT_DISKII:
      case ACT_SLOT_PARALLEL:
      case ACT_SLOT_HD32:
      case ACT_SLOT_MOCKINGBOARD:
      case ACT_SLOT_UTHERNET:
        {
          uint8_t *var = slotVarForAction(cardsActions[selectedMenuItem]);
          if (var) {
            g_display->clrScr(c_darkblue);
            g_display->drawString(M_SELECTED, 80, 100, "Updating slots...");
            g_display->flush();
            const int n = sizeof(kSelectableSlots);
            uint8_t cur = *var;
            int idx = 0;
            for (int i = 0; i < n; i++) {
              if (kSelectableSlots[i] == cur) { idx = i; break; }
            }
            idx = (idx + cycleStep + n) % n;
            *var = kSelectableSlots[idx];
            if (*var != 0) resolveSlotConflict(var);
            cardsConfigChanged = !slotsMatchSaved();
          }
          localRedraw = true;
        }
        break;
      case ACT_SLOT_RAMWORKS:
        {
          // Cycle through the available RamWorks sizes. Embedded builds
          // are capped at 1MB; the desktop offers the full range.
#ifdef TEENSYDUINO
          static const uint8_t rwSizes[] = { 0, 1 };
#else
          static const uint8_t rwSizes[] = { 0, 1, 3, 16 };
#endif
          int n = sizeof(rwSizes);
          int idx = 0;
          for (int i = 0; i < n; i++) {
            if (rwSizes[i] == g_ramworksSize) { idx = i; break; }
          }
          g_ramworksSize = rwSizes[(idx + cycleStep + n) % n];
          cardsConfigChanged = !slotsMatchSaved();
          localRedraw = true;
        }
        break;
      case ACT_SLOT_DEFAULTS:
        g_slotDiskII = 6;
        g_slotParallel = 1;
        g_slotHD32 = 7;
        g_slotMouse = 0;   // mouse works only in slot 4; off by default
        g_slotMockingboard = 4;
        g_slotUthernet = 0;
        g_ramworksSize = 0;
        cardsConfigChanged = !slotsMatchSaved();
        localRedraw = true;
        break;
      }
    }
  }

  return BIOS_CARDS;
}

uint16_t BIOS::DisksMenuHandler(bool needsRedraw, bool performAction)
{
  static bool localRedraw = true;

  if (selectedMenuItem < 0)
    selectedMenuItem = sizeof(diskActions)-1;
  selectedMenuItem %= sizeof(diskActions);
  
  if (needsRedraw || localRedraw) {
    g_display->clrScr(c_darkblue);
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
	strcpy(fileFilter, "img,hdv");
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
	strcpy(fileFilter, "img,hdv");
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
    g_display->clrScr(c_darkblue);

    // Draw a black area where we're going to "boot" a fake //e for the about screen. Don't put the whole graphic around it so it's obvious it's not a //e.
    for (uint8_t y=12; y<12+192; y++) {
      for (uint16_t x=20; x<280+20; x++) {
	g_display->drawPixel( x, y, 0x0000 );
      }
    }
    /*
    g_display->drawString(M_SELECTED,
			  0,
			  0,
			  "Aiie! - an Apple //e emulator");
    
    g_display->drawString(M_NORMAL, 
			  15, 20,
			  "(c) 2017-2026 Jorj Bauer");
    
    g_display->drawString(M_NORMAL,
			  15, 38,
			  "https://github.com/JorjBauer/aiie/");
    
    g_display->drawString(M_NORMAL,
			  0,
			  200,
			  "Press return");
    */
    g_display->flush();

    localRedraw = false;
  }

  const char *str =
    "                                   "
    "               Aiie!               "
    "                                   "
    "                                   "
    "                                   "
    "                                   "
    "                                   "
    "                                   "
    "  ... an Apple //e emulator        "
    "      written by                   "
    "        Jorj Bauer <jorj@jorj.org> "
    "                                   "
    "                                   "
    "                                   "
    "  (c) 2017-2026 Jorj Bauer         "
    "                                   "
    "                                   "
    " Source code is available at       "
    "        github.com/JorjBauer/aiie/ "
    "                                   "
    "                                   "
    "                                   "
    " Press <Return>... " // intentionally short so cursor stays here
    ;

  static uint16_t ptr = 0;
  static bool didFinish = false;

  if (!didFinish) {
    // Draw the next character
    bool didOne = false;
    while (!didOne || ptr < 35*2) { // draw the first 2 lines in one go, no matter what
      char charToDraw = str[ptr];
      didOne = true;
      int xpos = ptr % 35;
      int ypos = (int)(ptr / 35);
      if (charToDraw != ' ') {
	// First 2 lines have a blue background on any text; others are black
	g_display->drawCharacter(ptr < 70 ? M_NORMAL : M_PLAIN, xpos * 8 + 20, ypos * 8 + 12, charToDraw);
      }
      ptr++;
      if (ptr >= strlen(str)) {
	didFinish = true;
      } else {
	if (charToDraw == ' ') {
	  // Just blep the spaces to the screen toot-sweet
	  didOne = false;
	}
      }
    }
  } else {
    // Flash the cursor until the user exits
    static bool cursorOn = false;
    static bool flopTime = false;
    flopTime = !flopTime;
    if (flopTime) {
      cursorOn = !cursorOn;
    }
    int xpos = strlen(str) % 35;
    int ypos = (int)(strlen(str) / 35);
    g_display->drawCharacter(M_PLAIN, xpos * 8 + 20, ypos * 8 + 12, cursorOn ? 127 : 32);
  }
  g_display->flush();
  
  if (performAction) {
    ptr = 0;
    didFinish = false;
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

  uint8_t paddle = g_paddles->paddle0();
  if (paddle != lastPaddleX) {
    lastPaddleX = paddle;
    localRedraw = true;
  }
  paddle = g_paddles->paddle1();
  if (paddle != lastPaddleY) {
    lastPaddleY = paddle;
    localRedraw = true;
  }
  
  if (needsRedraw || localRedraw) {
    char buf[50];
    g_display->clrScr(c_darkblue);
    snprintf(buf, sizeof(buf), "Paddle X: %d    ", lastPaddleX);
    g_display->drawString(M_NORMAL, 0, 12, buf);
    snprintf(buf, sizeof(buf), "Paddle Y: %d    ", lastPaddleY);
    g_display->drawString(M_NORMAL, 0, 42, buf);
    g_display->drawString(M_NORMAL, 0, 132, "Press return to exit");

    // Draw the target for the paddle position
    for (uint16_t y=10; y<=110; y++) {
      for (uint16_t x=160; x<=260; x++) {
	g_display->drawPixel(x, y, 0x0000);
	g_display->drawPixel(x, 10, 0xFFFF);
	g_display->drawPixel(x, 110, 0xFFFF);
      }
      g_display->drawPixel(160, y, 0xFFFF);
      g_display->drawPixel(260, y, 0xFFFF);
    }

    for (uint16_t y=57; y<=63; y++) {
      g_display->drawPixel(207,y,0xFFFF);
      g_display->drawPixel(213,y,0xFFFF);
    }
    for (uint16_t x=207; x<=213; x++) {
      g_display->drawPixel(x,57,0xFFFF);
      g_display->drawPixel(x,63,0xFFFF);
    }

    float drawX = ((float)lastPaddleX/255.0)*100.0;
    float drawY = ((float)lastPaddleY/255.0)*100.0;
    g_display->drawPixel(160+drawX, 10+drawY, 0xFFFF);
    
    g_display->flush();

    localRedraw = false;
  }

  if (performAction) {
    return BIOS_HARDWARE;
  }

  return BIOS_PADDLES;
}

// The "Net" tab. What it shows depends on the platform, because the two builds
// reach the network very differently:
//   Teensy - a real ESP-01 WiFi co-processor, so it needs the SSID/password and
//            a Connect action, and it reports live link status.
//   SDL    - rides the host's own network through a built-in user-mode NAT, so
//            there is no radio to configure (no SSID, password, or Connect); it
//            does have a port offset knob for exposing privileged ports.
// Common to both: the Uthernet card's slot, and the inbound forward-port list.
// Up/Down move between fields; on a text field typing edits it and DEL erases;
// on the Slot field a digit or +/- picks the slot. Return advances to the next
// field (or, on the Teensy Connect field, joins). Left/right switch tabs; ESC
// returns to the VM menu. Edits go straight into the globals (persisted to prefs
// on BIOS exit); the forward list is pushed to the live NAT when you leave this
// tab (see BIOS::loop). Changing the slot marks the card config changed, so the
// slots are reassigned when the VM resumes.
uint16_t BIOS::WiFiScreenHandler(bool needsRedraw, bool performAction, int8_t key)
{
  static bool localRedraw = true;
#ifdef TEENSYDUINO
  static bool refreshStatus = true;
  static bool connecting = false;
  static int  cachedSt = 0;
  static uint8_t cachedIp[4] = {0, 0, 0, 0};
#endif

#ifdef TEENSYDUINO
  const int F_SLOT = 0, F_NET = 1, F_DNS = 2, F_SSID = 3, F_PASS = 4, F_FWD = 5, F_CONNECT = 6, F_COUNT = 7;
#else
  const int F_SLOT = 0, F_NET = 1, F_DNS = 2, F_FWD = 3, F_OFFSET = 4, F_COUNT = 5;
#endif

  if (selectedMenuItem < 0) selectedMenuItem = F_COUNT - 1;
  selectedMenuItem %= F_COUNT;

  // --- per-field key handling ----------------------------------------------
  if (selectedMenuItem == F_SLOT) {
    // Assign the Uthernet card's slot here (same effect as the Cards menu): a
    // digit picks a slot directly, +/= steps forward, - steps back, 0 disables.
    uint8_t *var = &g_slotUthernet;
    int step = 0;
    if (key >= '0' && key <= '9') {
      uint8_t ns = (uint8_t)(key - '0');
      if (isSelectableSlot(ns)) {
        *var = ns;
        if (ns != 0) resolveSlotConflict(var);   // move any card already there
        cardsConfigChanged = !slotsMatchSaved();
        localRedraw = true;
      }
    } else if (key == '+' || key == '=') step = 1;
    else if (key == '-')                 step = -1;
    if (step) {
      const int n = sizeof(kSelectableSlots);
      int idx = 0;
      for (int i = 0; i < n; i++) if (kSelectableSlots[i] == *var) { idx = i; break; }
      idx = (idx + step + n) % n;
      *var = kSelectableSlots[idx];
      if (*var != 0) resolveSlotConflict(var);
      cardsConfigChanged = !slotsMatchSaved();
      localRedraw = true;
    }
  }

  // Text entry: the forward-port list on both platforms, plus SSID/Password on
  // the Teensy.
  {
    char *tfield = nullptr; size_t tcap = 0; bool restrictFwd = false, restrictIp = false;
    if (selectedMenuItem == F_FWD) { tfield = g_natFwd; tcap = sizeof(g_natFwd) - 1; restrictFwd = true; }
    else if (selectedMenuItem == F_NET) { tfield = g_natSubnet; tcap = sizeof(g_natSubnet) - 1; restrictIp = true; }
    else if (selectedMenuItem == F_DNS) { tfield = g_natDns; tcap = sizeof(g_natDns) - 1; restrictIp = true; }
#ifdef TEENSYDUINO
    else if (selectedMenuItem == F_SSID) { tfield = g_wifiSSID; tcap = 32; }
    else if (selectedMenuItem == F_PASS) { tfield = g_wifiPass; tcap = 63; }
#endif
    if (tfield) {
      size_t len = strlen(tfield);
      bool accept;
      if (restrictIp)       accept = (key >= '0' && key <= '9') || key == '.';
      else if (restrictFwd) accept = (key >= '0' && key <= '9') || key == ',' || key == ' ';
      else                  accept = true;
      if (key == PK_DEL) {
        if (len > 0) { tfield[len - 1] = 0; localRedraw = true; }
      } else if (key >= 0x20 && key <= 0x7E && accept) {
        if (len < tcap) { tfield[len] = (char)key; tfield[len + 1] = 0; localRedraw = true; }
      }
    }
  }

#ifndef TEENSYDUINO
  // The SDL-only port offset is a number: digits append, DEL trims a digit.
  if (selectedMenuItem == F_OFFSET) {
    if (key == PK_DEL) { g_natPortOffset /= 10; localRedraw = true; }
    else if (key >= '0' && key <= '9') {
      uint32_t v = (uint32_t)g_natPortOffset * 10 + (uint32_t)(key - '0');
      g_natPortOffset = (v > 65535) ? 65535 : (uint16_t)v;
      localRedraw = true;
    }
  }
#endif

  if (performAction) {
#ifdef TEENSYDUINO
    if (selectedMenuItem == F_CONNECT) {
      if (g_uthernet) {
        g_display->clrScr(c_darkblue);
        g_display->drawString(M_SELECTED, MENUINDENT, 60, "Connecting...");
        g_display->flush();
        g_uthernet->wifiJoin(g_wifiSSID, g_wifiPass);
      }
      connecting = true; refreshStatus = true; localRedraw = true;
    } else
#endif
    {
      selectedMenuItem++;   // Return advances to the next field
      localRedraw = true;
    }
  }

#ifdef TEENSYDUINO
  // Poll the ESP link about once a second so the status line and counters update
  // live, turning this tab into a link tester. This handler runs on every BIOS
  // loop tick whether or not a key was pressed; the ESP probe itself is throttled
  // inside the backend.
  static uint8_t refreshTick = 0;
  if (++refreshTick >= 30) {
    refreshTick = 0;
    refreshStatus = true;
    localRedraw = true;
  }
#endif

  if (needsRedraw || localRedraw) {
    char buf[80];
#ifdef TEENSYDUINO
    if (refreshStatus) {
      if (g_uthernet) cachedSt = g_uthernet->wifiStatus(cachedIp);
      else            cachedSt = -1;
      refreshStatus = false;
    }
#endif

    g_display->clrScr(c_darkblue);
    DrawMenuBar();

    // Lay the screen out with a running y so the platform-specific rows do not
    // need every following line renumbered. NL = next line; GAP = a blank line.
    int y = 8 + LINEHEIGHT * 2;
#define NL  do { y += LINEHEIGHT; } while (0)
#define GAP do { y += LINEHEIGHT + LINEHEIGHT / 2; } while (0)

    // --- the emulated card (both platforms) ---
    g_display->drawString(M_NORMAL, MENUINDENT, y, "Uthernet II network card"); NL;
    if (g_slotUthernet == 0)
      strcpy(buf, "  Slot: off   (0-7 or +/-)");
    else
      snprintf(buf, sizeof(buf), "  Slot: %d     (0-7 or +/-)", g_slotUthernet);
    g_display->drawString(selectedMenuItem == F_SLOT ? M_SELECTED : M_NORMAL,
                          MENUINDENT, y, buf); NL;
    g_display->drawString(M_DISABLED, MENUINDENT, y,
                          "  The network card itself. 0 = off."); GAP;

    // --- the virtual LAN the NAT hands the Apple (both platforms) ---
    g_display->drawString(M_NORMAL, MENUINDENT, y, "Virtual network"); NL;
    snprintf(buf, sizeof(buf), "  Subnet: %s", g_natSubnet);
    g_display->drawString(selectedMenuItem == F_NET ? M_SELECTED : M_NORMAL,
                          MENUINDENT, y, buf); NL;
    {
      uint8_t net[4];
      if (unParseSubnet(g_natSubnet, net))
        g_display->drawString(M_DISABLED, MENUINDENT, y,
                              "  A /24: Apple .15, gateway .2, DNS .3.");
      else
        g_display->drawString(M_SELECTED, MENUINDENT, y,
                              "  Enter a full address, e.g. 10.0.2.0.");
    } NL;
    snprintf(buf, sizeof(buf), "  Resolver: %s", g_natDns[0] ? g_natDns : "(auto)");
    g_display->drawString(selectedMenuItem == F_DNS ? M_SELECTED : M_NORMAL,
                          MENUINDENT, y, buf); NL;
    {
      uint8_t dns[4];
      if (!g_natDns[0])
        g_display->drawString(M_DISABLED, MENUINDENT, y,
                              "  Auto: the network's own resolver.");
      else if (unParseSubnet(g_natDns, dns))
        g_display->drawString(M_DISABLED, MENUINDENT, y,
                              "  Upstream DNS. DEL to clear = auto.");
      else
        g_display->drawString(M_SELECTED, MENUINDENT, y,
                              "  Enter a DNS address, e.g. 1.1.1.1.");
    } GAP;

#ifdef TEENSYDUINO
    // --- the WiFi radio it talks through (Teensy only) ---
    g_display->drawString(M_NORMAL, MENUINDENT, y, "WiFi radio (needs an ESP-01)"); NL;
    snprintf(buf, sizeof(buf), "  SSID:     %s", g_wifiSSID);
    g_display->drawString(selectedMenuItem == F_SSID ? M_SELECTED : M_NORMAL,
                          MENUINDENT, y, buf); NL;
    snprintf(buf, sizeof(buf), "  Password: %s", g_wifiPass);
    g_display->drawString(selectedMenuItem == F_PASS ? M_SELECTED : M_NORMAL,
                          MENUINDENT, y, buf); GAP;
#endif

    // --- inbound: let the outside reach a server on the Apple (both) ---
    g_display->drawString(M_NORMAL, MENUINDENT, y, "Inbound port forwarding"); NL;
    snprintf(buf, sizeof(buf), "  Fwd ports: %s", g_natFwd);
    g_display->drawString(selectedMenuItem == F_FWD ? M_SELECTED : M_NORMAL,
                          MENUINDENT, y, buf); NL;
    g_display->drawString(M_DISABLED, MENUINDENT, y,
                          "  Comma-separated, e.g. 80,23."); NL;
    g_display->drawString(M_DISABLED, MENUINDENT, y,
                          "  Apple listen ports; up to 4."); NL;
    // Each forward permanently holds one of the ESP's four sockets, and every
    // outbound flow needs a free one, so surface the budget as it gets tight
    // rather than silently dropping ports (the NAT only opens the first four).
    {
      int nports = 0;
      for (const char *p = g_natFwd; *p; ) {
        if (*p >= '0' && *p <= '9') { nports++; while (*p >= '0' && *p <= '9') p++; }
        else p++;
      }
      if (nports > 4) {
        g_display->drawString(M_SELECTED, MENUINDENT, y,
                              "  Too many: only the first 4 open."); NL;
      }
#ifdef TEENSYDUINO
      else if (nports == 4) {
        g_display->drawString(M_SELECTED, MENUINDENT, y,
                              "  4 ports: no outbound flows left"); NL;
        g_display->drawString(M_SELECTED, MENUINDENT, y,
                              "  (all 4 ESP sockets are listeners)."); NL;
      }
      else if (nports == 3) {
        g_display->drawString(M_SELECTED, MENUINDENT, y,
                              "  3 ports: only 1 outbound flow at"); NL;
        g_display->drawString(M_SELECTED, MENUINDENT, y,
                              "  a time (each needs a free slot)."); NL;
      }
      else {
        g_display->drawString(M_DISABLED, MENUINDENT, y,
                              "  Each uses 1 of the ESP's 4 sockets."); NL;
      }
#endif
    }
#ifndef TEENSYDUINO
    snprintf(buf, sizeof(buf), "  Offset:    %u", (unsigned)g_natPortOffset);
    g_display->drawString(selectedMenuItem == F_OFFSET ? M_SELECTED : M_NORMAL,
                          MENUINDENT, y, buf); NL;
    g_display->drawString(M_DISABLED, MENUINDENT, y,
                          "  Added to ports <1024 (avoids sudo)."); NL;
#endif
    GAP;

#ifdef TEENSYDUINO
    // --- action + live ESP/WiFi status (Teensy only) ---
    g_display->drawString(selectedMenuItem == F_CONNECT ? M_SELECTED : M_NORMAL,
                          MENUINDENT, y, "[ Connect to WiFi ]"); GAP;

    if (cachedSt < 0)       strcpy(buf, "Status: card off (set a Slot above)");
    else if (cachedSt == 0) strcpy(buf, "Status: ESP-01 not responding");
    else if (cachedSt == 2) snprintf(buf, sizeof(buf), "Status: connected  %d.%d.%d.%d",
                                     cachedIp[0], cachedIp[1], cachedIp[2], cachedIp[3]);
    else                    strcpy(buf, connecting ? "Status: WiFi not joined (password?)"
                                                    : "Status: WiFi not joined");
    g_display->drawString(M_DISABLED, MENUINDENT, y, buf); NL;

    // Live link counters, to see which side is dead: TX = commands sent to the
    // ESP, RX = valid replies, err = bytes that arrived but failed CRC.
    if (g_uthernet && cachedSt >= 0) {
      snprintf(buf, sizeof(buf), "Link:   TX:%lu RX:%lu err:%lu",
               (unsigned long)g_uthernet->statFramesSent(),
               (unsigned long)g_uthernet->statFramesReceived(),
               (unsigned long)g_uthernet->statCrcErrors());
      g_display->drawString(M_DISABLED, MENUINDENT, y, buf); NL;
    }
    GAP;
#else
    // --- desktop note (SDL): no radio to configure ---
    g_display->drawString(M_DISABLED, MENUINDENT, y,
                          "Networking uses the host's own"); NL;
    g_display->drawString(M_DISABLED, MENUINDENT, y,
                          "connection - no WiFi name or"); NL;
    g_display->drawString(M_DISABLED, MENUINDENT, y,
                          "password is needed on the desktop."); GAP;
#endif

    g_display->drawString(M_DISABLED, MENUINDENT, y,
                          "Return=next  arrows=move  ESC=menu"); NL;

#undef NL
#undef GAP
    g_display->flush();
    localRedraw = false;
  }

  return BIOS_WIFI;
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
  static uint8_t page = 0;
  static uint16_t fileCount = 0;
  
  if (needsRedraw || localRedraw) {
    fileCount = DrawDiskNames(page, selectedMenuItem, fileFilter);

    localRedraw = false;
  }
  
  if (performAction) {
    if (selectedMenuItem == 0) {
      if (page > 0) page--;
      //      else sel = BIOS_MAXFILES + 1;
      localRedraw = true;
    }
    else if (selectedMenuItem == BIOS_MAXFILES+1) {
      // FIXME what if there are no files on the next page? We
      // shouldn't show a blank page.
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

void BIOS::WarmReset()
{
  g_cpu->Reset();
}

void BIOS::RebootAsIs()
{
  // g_vm->Reset() will eject disks. We don't want to do that, so we need to
  // grab the inserted disk names; reset the VM; then restore the disks.
  char *disk6s1 = strdup(((AppleVM *)g_vm)->DiskName(0) ? ((AppleVM *)g_vm)->DiskName(0) : "");
  char *disk6s2 = strdup(((AppleVM *)g_vm)->DiskName(1) ? ((AppleVM *)g_vm)->DiskName(1) : "");
  char *hdd1 = strdup(((AppleVM *)g_vm)->HDName(0) ? ((AppleVM *)g_vm)->HDName(0) : "");
  char *hdd2 = strdup(((AppleVM *)g_vm)->HDName(1) ? ((AppleVM *)g_vm)->HDName(1) : "");

  g_vm->Reset();
  g_cpu->Reset();

  if (disk6s1[0])
    ((AppleVM *)g_vm)->insertDisk(0, disk6s1);
  if (disk6s2[0])
    ((AppleVM *)g_vm)->insertDisk(1, disk6s2);
  if (hdd1[0])
    ((AppleVM *)g_vm)->insertHD(0, hdd1);
  if (hdd2[0])
    ((AppleVM *)g_vm)->insertHD(2, hdd2);

  free(disk6s1);
  free(disk6s2);
  free(hdd1);
  free(hdd2);
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
    // Resume continues the running VM. That is unsafe once a card slot (or the
    // RamWorks size) has changed under it, because the booted OS still sees the
    // old hardware -- so disable Resume until the user resets or reboots.
    return !cardsConfigChanged;

  case ACT_RESET:
  case ACT_REBOOT:
  case ACT_REBOOTANDEJECT:
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
  case ACT_UPDATEFW:
  case ACT_PADX_INV:
  case ACT_PADY_INV:
  case ACT_PADDLES:
  case ACT_SLOT_DISKII:
  case ACT_SLOT_PARALLEL:
  case ACT_SLOT_HD32:
  case ACT_SLOT_MOUSE:
  case ACT_SLOT_MOCKINGBOARD:
  case ACT_SLOT_UTHERNET:
  case ACT_SLOT_RAMWORKS:
  case ACT_SLOT_DEFAULTS:
  case ACT_WIFI:
    return true;

  case ACT_LUMINANCEUP:
    return (g_luminanceCutoff < 255);
  case ACT_LUMINANCEDOWN:
    return (g_luminanceCutoff > 0);
    
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
  for (size_t i=0; i<sizeof(aiieActions); i++) {
    switch (aiieActions[i]) {
    case ACT_ABOUT:
      snprintf(buf, sizeof(buf), "About...");
      break;
    }

    if (isActionActive(aiieActions[i])) {
      g_display->drawString(selectedMenuItem == (int8_t)i ? M_SELECTED : M_NORMAL, MENUINDENT, 20 + LINEHEIGHT * i, buf);
    } else {
      g_display->drawString(selectedMenuItem == (int8_t)i ? M_SELECTDISABLED : M_DISABLED, MENUINDENT, 20 + LINEHEIGHT * i,
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
  for (size_t i=0; i<sizeof(vmActions); i++) {
    switch (vmActions[i]) {
    case ACT_DEBUG:
      {
	const char *templateString = "Debug: %s";
	switch (g_debugMode) {
	case D_NONE:
	  snprintf(buf, sizeof(buf), templateString, "off");
	  break;
	case D_SHOWFPS:
	  snprintf(buf, sizeof(buf), templateString, "Show FPS");
	  break;
	case D_SHOWMEMFREE:
	  snprintf(buf, sizeof(buf), templateString, "Show mem free");
	  break;
	case D_SHOWPADDLES:
	  snprintf(buf, sizeof(buf), templateString, "Show paddles");
	  break;
	case D_SHOWPC:
	  snprintf(buf, sizeof(buf), templateString, "Show PC");
	  break;
	case D_SHOWCYCLES:
	  snprintf(buf, sizeof(buf), templateString, "Show cycles");
	  break;
	case D_SHOWBATTERY:
	  snprintf(buf, sizeof(buf), templateString, "Show battery");
	  break;
	case D_SHOWTIME:
	  snprintf(buf, sizeof(buf), templateString, "Show time");
	  break;
	case D_SHOWDSK:
	  snprintf(buf, sizeof(buf), templateString, "Show Disk");
	  break;
	case D_SHOWNET:
	  snprintf(buf, sizeof(buf), templateString, "Show network");
	  break;
	}
      }
      break;
    case ACT_EXIT:
      strcpy(buf, "Resume");
      break;
    case ACT_RESET:
      strcpy(buf, "Reset (press Reset key)");
      break;
    case ACT_REBOOT:
      strcpy(buf, "Reboot (reboot emulator)");
      break;
    case ACT_REBOOTANDEJECT:
      strcpy(buf, "Reboot and eject disks");
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
    case ACT_UPDATEFW:
      strcpy(buf, "Update firmware from SD");
      break;
    }

    if (isActionActive(vmActions[i])) {
      g_display->drawString(selectedMenuItem == (int8_t)i ? M_SELECTED : M_NORMAL, MENUINDENT, 20 + LINEHEIGHT * i, buf);
    } else {
      g_display->drawString(selectedMenuItem == (int8_t)i ? M_SELECTDISABLED : M_DISABLED, MENUINDENT, 20 + LINEHEIGHT * i, buf);
    }
  }

  // Explain why Resume is greyed out after a card/RamWorks change.
  if (cardsConfigChanged) {
    int y = 20 + LINEHEIGHT * (sizeof(vmActions) + 1);
    g_display->drawString(M_SELECTED, MENUINDENT, y,
                          "Card config changed.");
    g_display->drawString(M_SELECTED, MENUINDENT, y + LINEHEIGHT,
                          "Reset or Reboot to apply it.");
  }
}

void BIOS::DrawHardwareMenu()
{
  if (selectedMenuItem < 0)
    selectedMenuItem = sizeof(hardwareActions)-1;

  selectedMenuItem %= sizeof(hardwareActions);

  char buf[40];
  for (size_t i=0; i<sizeof(hardwareActions); i++) {
    switch (hardwareActions[i]) {
    case ACT_DISPLAYTYPE:
      {
	const char *templateString = "Display: %s";
	switch (g_displayType) {
	case m_blackAndWhite:
	  snprintf(buf, sizeof(buf), templateString, "B&W");
	  break;
	case m_monochrome:
	  snprintf(buf, sizeof(buf), templateString, "Mono");
	  break;
	case m_ntsclike:
	  snprintf(buf, sizeof(buf), templateString, "NTSC-like");
	  break;
	case m_perfectcolor:
	  snprintf(buf, sizeof(buf), templateString, "RGB");
	  break;
	}
      }
      break;
      
    case ACT_LUMINANCEUP:
      snprintf(buf, sizeof(buf), "Luminance+: %d", g_luminanceCutoff);
      break;
    case ACT_LUMINANCEDOWN:
      snprintf(buf, sizeof(buf), "Luminance-: %d", g_luminanceCutoff);
      break;
      
    case ACT_SPEED:
      {
	const char *templateString = "CPU Speed: %s";
	switch (currentCPUSpeedIndex) {
	case CPUSPEED_HALF:
	  snprintf(buf, sizeof(buf), templateString, "Half [511.5 kHz]");
	  break;
	case CPUSPEED_DOUBLE:
	  snprintf(buf, sizeof(buf), templateString, "Double (2.046 MHz)");
	  break;
	case CPUSPEED_QUAD:
	  snprintf(buf, sizeof(buf), templateString, "Quad (4.092 MHz)");
	  break;
#ifndef TEENSYDUINO
	case CPUSPEED_8X:
	  snprintf(buf, sizeof(buf), templateString, "8x (8.184 MHz)");
	  break;
	case CPUSPEED_16X:
	  snprintf(buf, sizeof(buf), templateString, "16x (16.368 MHz)");
	  break;
	case CPUSPEED_128X:
	  snprintf(buf, sizeof(buf), templateString, "128x (no audio)");
	  break;
	case CPUSPEED_256X:
	  snprintf(buf, sizeof(buf), templateString, "256x (no audio)");
	  break;
#endif
	default:
	  snprintf(buf, sizeof(buf), templateString, "Normal (1.023 MHz)");
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
      g_display->drawString(selectedMenuItem == (int8_t)i ? M_SELECTED : M_NORMAL, MENUINDENT, 20 + LINEHEIGHT * i, buf);
    } else {
      g_display->drawString(selectedMenuItem == (int8_t)i ? M_SELECTDISABLED : M_DISABLED, MENUINDENT, 20 + LINEHEIGHT * i, buf);
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

void BIOS::DrawCardsMenu()
{
  if (selectedMenuItem < 0)
    selectedMenuItem = sizeof(cardsActions)-1;
  selectedMenuItem %= sizeof(cardsActions);

  char buf[50];
  for (size_t i=0; i<sizeof(cardsActions); i++) {
    uint8_t slot = 0;
    const char *name = "";
    switch (cardsActions[i]) {
    case ACT_SLOT_DISKII:
      name = "Disk II";
      slot = g_slotDiskII;
      break;
    case ACT_SLOT_PARALLEL:
      name = "Parallel";
      slot = g_slotParallel;
      break;
    case ACT_SLOT_HD32:
      name = "HD32";
      slot = g_slotHD32;
      break;
    case ACT_SLOT_MOUSE:
      name = "Mouse";
      slot = g_slotMouse;
      break;
    case ACT_SLOT_MOCKINGBOARD:
      name = "Mockingboard";
      slot = g_slotMockingboard;
      break;
    case ACT_SLOT_UTHERNET:
      name = "Uthernet";
      slot = g_slotUthernet;
      break;
    case ACT_SLOT_RAMWORKS:
      {
        const char *sz;
        switch (g_ramworksSize) {
        case 1:  sz = "1MB";  break;
        case 3:  sz = "3MB";  break;
        case 16: sz = "16MB"; break;
        default: sz = "None"; break;
        }
        snprintf(buf, sizeof(buf), "%-14s %s", "RamWorks", sz);
      }
      goto drawit;
    case ACT_SLOT_DEFAULTS:
      strcpy(buf, "Reset to defaults");
      goto drawit;
    }

    if (slot == 0)
      snprintf(buf, sizeof(buf), "%-14s Disabled", name);
    else
      snprintf(buf, sizeof(buf), "%-14s Slot %d", name, slot);

  drawit:
    if (isActionActive(cardsActions[i])) {
      g_display->drawString(selectedMenuItem == (int8_t)i ? M_SELECTED : M_NORMAL, MENUINDENT, 20 + LINEHEIGHT * i, buf);
    } else {
      g_display->drawString(selectedMenuItem == (int8_t)i ? M_SELECTDISABLED : M_DISABLED, MENUINDENT, 20 + LINEHEIGHT * i, buf);
    }
  }

  if (cardsConfigChanged) {
    g_display->drawString(M_DISABLED, MENUINDENT, 20 + LINEHEIGHT * (sizeof(cardsActions) + 1),
                          "Card changes require reboot.");
    g_display->drawString(M_DISABLED, MENUINDENT, 20 + LINEHEIGHT * (sizeof(cardsActions) + 2),
                          "Machine will cold restart on exit.");
  }

  g_display->drawString(M_DISABLED, MENUINDENT, 20 + LINEHEIGHT * (sizeof(cardsActions) + 4),
                        "Type 0-7 to set slot (not 3)");
  g_display->drawString(M_DISABLED, MENUINDENT, 20 + LINEHEIGHT * (sizeof(cardsActions) + 5),
                        "Return cycles through slots");
}

void BIOS::DrawDisksMenu()
{
  if (selectedMenuItem < 0)
    selectedMenuItem = sizeof(diskActions)-1;

  selectedMenuItem %= sizeof(diskActions);

  char buf[80];
  for (size_t i=0; i<sizeof(diskActions); i++) {
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
	  snprintf(buf, sizeof(buf), "Insert Disk %d", diskActions[i]==ACT_DISK2 ? 2 : 1);
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
	  snprintf(buf, sizeof(buf), "Connect HD %d", diskActions[i]==ACT_HD2 ? 2 : 1);
	}
      }
      break;
    }

    if (isActionActive(diskActions[i])) {
      g_display->drawString(selectedMenuItem == (int8_t)i ? M_SELECTED : M_NORMAL, MENUINDENT, 20 + LINEHEIGHT * i, buf);
    } else {
      g_display->drawString(selectedMenuItem == (int8_t)i ? M_SELECTDISABLED : M_DISABLED, MENUINDENT, 20 + LINEHEIGHT * i, buf);
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
  case 3: // Cards
    DrawCardsMenu();
    break;
  case 4: // Disks
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
  g_display->clrScr(c_darkblue);
  const char *title="BIOS Configuration - pick disk image";
  g_display->drawString(M_NORMAL, 0, 0, title);
  
  for (size_t x=0; x<strlen(title)*8; x++) {
      g_display->drawPixel(x, LINEHEIGHT-1, 0xFFFF);
  }

  uint8_t vpos = FILEMENUSTARTAT;
  g_display->drawString(page==0 ? (selection == 0 ? M_SELECTDISABLED : M_DISABLED) :
			          (selection == 0 ? M_SELECTED : M_NORMAL),
			MENUINDENT, vpos, "<Prev>");
  vpos += LINEHEIGHT * 1.5;
  
  bool endsHere = false;
  uint8_t i;

  for (i=0; i<BIOS_MAXFILES; i++) {
    // If the file name is less than 39 characters, it fits on one
    // line; but if it's longer, we need to use two lines.
    const char *name = "-";
    if (i < fileCount) {
      name = fileDirectory[i];
    }
    g_display->drawString(
			  (i < fileCount) ? ((i == selection-1) ? M_SELECTED : M_NORMAL) :
			  (i == selection-1) ? M_SELECTDISABLED : M_DISABLED,
			  
			  MENUINDENT, vpos,
			  
			  name);
    vpos += LINEHEIGHT;
    
    if (strlen(name) > 39) {
      // Break the string at 39 characters and start drawing the second line indented more
      char restOfString[BIOS_MAXPATH-39+1];
      strcpy(restOfString, (char *)&name[39]);
      g_display->drawString(
			    (i < fileCount) ? ((i == selection-1) ? M_SELECTED : M_NORMAL) :
			    (i == selection-1) ? M_SELECTDISABLED : M_DISABLED,
			    
			    MENUINDENT+15,
			    vpos,
			    
			    restOfString);
      vpos += LINEHEIGHT;
      
    }
    if (i+1 >= fileCount) {
      endsHere = true;
    }
  }

  vpos += LINEHEIGHT/2;
  if (endsHere || fileCount < BIOS_MAXFILES) {
    g_display->drawString((i+1 == selection) ? M_SELECTDISABLED : M_DISABLED,
			  MENUINDENT, vpos,
			  "<Next>");
  } else {
    g_display->drawString(i+1 == selection ? M_SELECTED : M_NORMAL,
			  MENUINDENT, vpos,
			  "<Next>");
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
  g_display->clrScr(c_darkblue);
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
      // add a terminating entry
      biosCache[numCacheEntries].fn[0] = '\0';
      return numCacheEntries;
    }
    idx++;
    numCacheEntries++;
    if (numCacheEntries >= BIOSCACHESIZE-2) {
      // need a terminating entry
      biosCache[BIOSCACHESIZE-1].fn[0] = '\0';
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
  uint16_t startNum = MAXFILESPERPAGE * (uint16_t)pageOffset;
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
	
