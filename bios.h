#ifndef __BIOS_H
#define __BIOS_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#endif

#define BIOS_MAXFILES 10 // number of files in a page of listing
#define BIOS_MAXPATH 60  // maximum length of a single filename that we'll support

class BIOS {
 public:
  BIOS();
  ~BIOS();

  // return true as long as it's still running
  bool loop();

 private:
  uint16_t MainMenuHandler();
  
  void DrawMenuBar();
  void DrawCurrentMenu();
  void DrawAiieMenu();
  void DrawVMMenu();
  void DrawHardwareMenu();
  void DrawDisksMenu();

  uint16_t AiieMenuHandler(bool needsRedraw, bool performAction);
  uint16_t VmMenuHandler(bool needsRedraw, bool performAction);
  uint16_t HardwareMenuHandler(bool needsRedraw, bool performAction);
  uint16_t DisksMenuHandler(bool needsRedraw, bool performAction);
  uint16_t AboutScreenHandler(bool needsRedraw, bool performAction);
  uint16_t PaddlesScreenHandler(bool needsRedraw, bool performAction);
  uint16_t SelectFileScreenHandler(bool needsRedraw, bool performAction);

  uint8_t GetAction(int8_t prevAction);
  bool isActionActive(int8_t action);

  int8_t getCurrentMenuAction();

  void WarmReset();
  void RebootAsIs();
  void ColdReboot();

  uint16_t DrawDiskNames(uint8_t page, int8_t selection, const char *filter);
  uint16_t GatherFilenames(uint8_t pageOffset, const char *filter);

  void stripDirectory();

  uint16_t cacheAllEntries(const char *filter);
  void sortCachedEntries();
  void swapCacheEntries(int a, int b);

 private:
  int8_t selectedFile;
  char fileDirectory[BIOS_MAXFILES][BIOS_MAXPATH+1];

  char rootPath[255-BIOS_MAXPATH];

  int8_t selectedMenu;
  int8_t selectedMenuItem;
  uint8_t currentCPUSpeedIndex;
};

#endif
