#ifndef __BIOS_H
#define __BIOS_H

#include <Arduino.h>

#define BIOS_MAXFILES 10 // number of files in a page of listing
#define BIOS_MAXPATH 40  // maximum length of a single filename that we'll support

class BIOS {
 public:
  BIOS();
  ~BIOS();

  // return true if a persistent change needs to be stored in EEPROM
  bool runUntilDone();

 private:
  uint8_t GetAction(int8_t prevAction);
  bool isActionActive(int8_t action);
  void DrawMainMenu(int8_t selection);

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
};

#endif
