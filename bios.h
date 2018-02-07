#ifndef __BIOS_H
#define __BIOS_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#endif

#define BIOS_MAXFILES 10 // number of files in a page of listing
#define BIOS_MAXPATH 40  // maximum length of a single filename that we'll support

class BIOS {
 public:
  BIOS();
  ~BIOS();

  // return true if a persistent change needs to be stored in EEPROM
  bool runUntilDone();

 private:
  void DrawMenuBar();
  void DrawCurrentMenu();
  void DrawVMMenu();
  void DrawHardwareMenu();
  void DrawDisksMenu();

  uint8_t GetAction(int8_t prevAction);
  bool isActionActive(int8_t action);
  void DrawMainMenu();

  int8_t getCurrentMenuAction();

  void WarmReset();
  void ColdReboot();

  bool SelectDiskImage();
  void DrawDiskNames(uint8_t page, int8_t selection);
  uint8_t GatherFilenames(uint8_t pageOffset);

  void stripDirectory();

 private:
  int8_t selectedFile;
  char fileDirectory[BIOS_MAXFILES][BIOS_MAXPATH+1];

  char rootPath[255-BIOS_MAXPATH];

  int8_t selectedMenu;
  int8_t selectedMenuItem;
  uint8_t currentCPUSpeedIndex;
};

#endif
