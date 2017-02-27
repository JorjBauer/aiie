#include "bios.h"

#include "applevm.h"
#include "physicalkeyboard.h"
#include "teensy-keyboard.h"
#include "cpu.h"
#include "teensy-filemanager.h"
#include "teensy-display.h"

#include "globals.h"

enum {
  ACT_EXIT = 0,
  ACT_RESET = 1,
  ACT_REBOOT = 2,
  ACT_MONITOR = 3,
  ACT_DISPLAYTYPE = 4,
  ACT_DEBUG = 5,
  ACT_DISK1 = 6,
  ACT_DISK2 = 7,
  ACT_VOLPLUS = 8,
  ACT_VOLMINUS = 9,

  NUM_ACTIONS = 10
};

const char *titles[NUM_ACTIONS] = { "Resume",
				    "Reset",
				    "Cold Reboot",
				    "Drop to Monitor",
				    "Display: %s",
				    "Debug: %s",
				    "%s Disk 1",
				    "%s Disk 2",
				    "Volume +",
				    "Volume -"
};

// FIXME: abstract the pin # rather than repeating it here
#define RESETPIN 39

extern int16_t g_volume; // FIXME: external global. icky.
extern uint8_t debugMode; // and another. :/
// FIXME: and these need abstracting out of the main .ino !
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

const char *staticPathConcat(const char *rootPath, const char *filePath)
{
  static char buf[MAXPATH];
  strncpy(buf, rootPath, sizeof(buf)-1);
  strncat(buf, filePath, sizeof(buf)-strlen(buf)-1);

  return buf;
}


BIOS::BIOS()
{
  strcpy(rootPath, "/A2DISKS/");

  selectedFile = -1;
  for (int8_t i=0; i<BIOS_MAXFILES; i++) {
    // Put end terminators in place; strncpy won't copy over them
    fileDirectory[i][BIOS_MAXPATH] = '\0';
  }
}

BIOS::~BIOS()
{
}

bool BIOS::runUntilDone()
{
  int8_t prevAction = ACT_EXIT;
  bool volumeDidChange = 0;
  while (1) {
    switch (prevAction = GetAction(prevAction)) {
    case ACT_EXIT:
      goto done;
    case ACT_REBOOT:
      ColdReboot();
      goto done;
    case ACT_RESET:
      WarmReset();
      goto done;
    case ACT_MONITOR:
      ((AppleVM *)g_vm)->Monitor();
      goto done;
    case ACT_DISPLAYTYPE:
      g_displayType++;
      g_displayType %= 4; // FIXME: abstract max #
      ((AppleDisplay*)g_display)->displayTypeChanged();
      break;
    case ACT_DEBUG:
      debugMode++;
      debugMode %= 8; // FIXME: abstract max #
      break;
    case ACT_DISK1:
      if (((AppleVM *)g_vm)->DiskName(0)[0] != '\0') {
	((AppleVM *)g_vm)->ejectDisk(0);
      } else {
	if (SelectDiskImage()) {
	  ((AppleVM *)g_vm)->insertDisk(0, staticPathConcat(rootPath, fileDirectory[selectedFile]), false);
	  goto done;
	}
      }
      break;
    case ACT_DISK2:
      if (((AppleVM *)g_vm)->DiskName(1)[0] != '\0') {
	((AppleVM *)g_vm)->ejectDisk(1);
      } else {
	if (SelectDiskImage()) {
	  ((AppleVM *)g_vm)->insertDisk(1, staticPathConcat(rootPath, fileDirectory[selectedFile]), false);
	  goto done;
	}
      }
      break;
    case ACT_VOLPLUS:
      g_volume ++;
      if (g_volume > 15) {
	g_volume = 15;
      }
      volumeDidChange = true;
      break;
    case ACT_VOLMINUS:
      g_volume--;
      if (g_volume < 0) {
	g_volume = 0;
      }
      volumeDidChange = true;
      break;
    }
  }

 done:
  // Undo whatever damage we've done to the screen
  g_display->redraw();
  g_display->blit({0, 0, 279, 191});

  // return true if any persistent setting changed that we want to store in eeprom
  return volumeDidChange;
}

void BIOS::WarmReset()
{
  g_cpu->Reset();
}

void BIOS::ColdReboot()
{
  g_vm->Reset();
  g_cpu->Reset();
}

uint8_t BIOS::GetAction(int8_t selection)
{
  while (1) {
    DrawMainMenu(selection);
    while (!((TeensyKeyboard *)g_keyboard)->kbhit() &&
	   (digitalRead(RESETPIN) == HIGH)) {
      ;
      // Wait for either a keypress or the reset button to be pressed
    }

    if (digitalRead(RESETPIN) == LOW) {
      // wait until it's no longer pressed
      while (digitalRead(RESETPIN) == HIGH)
	;
      delay(100); // wait long enough for it to debounce
      // then return an exit code
      return ACT_EXIT;
    }
    
    switch (((TeensyKeyboard *)g_keyboard)->read()) {
    case DARR:
      selection++;
      selection %= NUM_ACTIONS;
      break;
    case UARR:
      selection--;
      if (selection < 0)
	selection = NUM_ACTIONS-1;
      break;
    case RET:
      if (isActionActive(selection))
	return selection;
      break;
    }
  }
}

bool BIOS::isActionActive(int8_t action)
{
  // don't return true for disk events that aren't valid
  switch (action) {
  case ACT_EXIT:
  case ACT_RESET:
  case ACT_REBOOT:
  case ACT_MONITOR:
  case ACT_DISPLAYTYPE:
  case ACT_DEBUG:
  case ACT_DISK1:
  case ACT_DISK2:
    return true;

  case ACT_VOLPLUS:
    return (g_volume < 15);
  case ACT_VOLMINUS:
    return (g_volume > 0);
  }

  /* NOTREACHED */
  return false;
}

void BIOS::DrawMainMenu(int8_t selection)
{
  ((TeensyDisplay *)g_display)->clrScr();
  g_display->drawString(M_NORMAL, 0, 12, "BIOS Configuration");
  for (int i=0; i<NUM_ACTIONS; i++) {
    char buf[25];
    if (i == ACT_DISK1 || i == ACT_DISK2) {
      sprintf(buf, titles[i], ((AppleVM *)g_vm)->DiskName(i - ACT_DISK1)[0] ? "Eject" : "Insert");
    } else if (i == ACT_DISPLAYTYPE) {
      switch (g_displayType) {
      case m_blackAndWhite:
	sprintf(buf, titles[i], "B&W");
	break;
      case m_monochrome:
	sprintf(buf, titles[i], "Mono");
	break;
      case m_ntsclike:
	sprintf(buf, titles[i], "NTSC-like");
	break;
      case m_perfectcolor:
	sprintf(buf, titles[i], "RGB");
	break;
      }
    } else if (i == ACT_DEBUG) {
      switch (debugMode) {
      case D_NONE:
	sprintf(buf, titles[i], "off");
	break;
      case D_SHOWFPS:
	sprintf(buf, titles[i], "Show FPS");
	break;
      case D_SHOWMEMFREE:
	sprintf(buf, titles[i], "Show mem free");
	break;
      case D_SHOWPADDLES:
	sprintf(buf, titles[i], "Show paddles");
	break;
      case D_SHOWPC:
	sprintf(buf, titles[i], "Show PC");
	break;
      case D_SHOWCYCLES:
	sprintf(buf, titles[i], "Show cycles");
	break;
      case D_SHOWBATTERY:
	sprintf(buf, titles[i], "Show battery");
	break;
      case D_SHOWTIME:
	sprintf(buf, titles[i], "Show time");
	break;
      }
    } else {
      strcpy(buf, titles[i]);
    }

    if (isActionActive(i)) {
      g_display->drawString(selection == i ? M_SELECTED : M_NORMAL, 10, 50 + 14 * i, buf);
    } else {
      g_display->drawString(selection == i ? M_SELECTDISABLED : M_DISABLED, 10, 50 + 14 * i, buf);
    }
  }

  // draw the volume bar
  uint16_t volCutoff = 300.0 * (float)((float) g_volume / 15.0);
  for (uint8_t y=200; y<=210; y++) {
    ((TeensyDisplay *)g_display)->moveTo(10, y);
    for (uint16_t x = 0; x< 300; x++) {
      ((TeensyDisplay *)g_display)->drawNextPixel( x <= volCutoff ? 0xFFFF : 0x0010 );
    }
  }
}


// return true if the user selects an image
// sets selectedFile (index; -1 = "nope") and fileDirectory[][] (names of up to BIOS_MAXFILES files)
bool BIOS::SelectDiskImage()
{
  int8_t sel = 0;
  int8_t page = 0;

  while (1) {
    DrawDiskNames(page, sel);

    while (!((TeensyKeyboard *)g_keyboard)->kbhit())
      ;
    switch (((TeensyKeyboard *)g_keyboard)->read()) {
    case DARR:
      sel++;
      sel %= BIOS_MAXFILES + 2;
      break;
    case UARR:
      sel--;
      if (sel < 0)
	sel = BIOS_MAXFILES + 1;
      break;
    case RET:
      if (sel == 0) {
	page--;
	if (page < 0) page = 0;
	//	else sel = BIOS_MAXFILES + 1;
      }
      else if (sel == BIOS_MAXFILES+1) {
	page++;
	//sel = 0;
      } else {
	if (strcmp(fileDirectory[sel-1], "../") == 0) {
	  // Go up a directory (strip a directory name from rootPath)
	  stripDirectory();
	  page = 0;
	  //sel = 0;
	  continue;
	} else if (fileDirectory[sel-1][strlen(fileDirectory[sel-1])-1] == '/') {
	  // Descend in to the directory. FIXME: file path length?
	  strcat(rootPath, fileDirectory[sel-1]);
	  sel = 0;
	  page = 0;
	  continue;
	} else {
	  selectedFile = sel - 1;
	  return true;
	}
      }
      break;
    }
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

void BIOS::DrawDiskNames(uint8_t page, int8_t selection)
{
  uint8_t fileCount = GatherFilenames(page);
  ((TeensyDisplay *)g_display)->clrScr();
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
  if (fileCount == BIOS_MAXFILES || fileCount == 0) {
    g_display->drawString((i+1 == selection) ? M_SELECTDISABLED : M_DISABLED, 10, 50 + 14 * (i+1), "<Next>");
  } else {
    g_display->drawString(i+1 == selection ? M_SELECTED : M_NORMAL, 10, 50 + 14 * (i+1), "<Next>");
  }
}


uint8_t BIOS::GatherFilenames(uint8_t pageOffset)
{
  uint8_t startNum = 10 * pageOffset;
  uint8_t count = 0; // number we're including in our listing

  while (1) {
    char fn[BIOS_MAXPATH];
    // FIXME: add po, nib
    int8_t idx = g_filemanager->readDir(rootPath, "dsk", fn, startNum + count, BIOS_MAXPATH);

    if (idx == -1) {
      return count;
    }

    idx++;

    strncpy(fileDirectory[count], fn, BIOS_MAXPATH);
    count++;

    if (count >= BIOS_MAXFILES) {
      return count;
    }
  }
}

