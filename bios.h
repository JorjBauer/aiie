#ifndef __BIOS_H
#define __BIOS_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#endif

#define BIOS_MAXPATH 128 // maximum length of a single filename that we'll support

class BIOS {
 public:
  BIOS();
  ~BIOS();

  // return true as long as it's still running
  bool loop();

  // Reboot the emulated machine (as the BIOS "Reboot" menu item does): reset the
  // VM and CPU while keeping inserted media. Public so the firmware can bind it
  // to a host key (e.g. Ctrl-Alt-Del on the USB keyboard).
  void RebootAsIs();

 private:
  uint8_t mainScreen(bool redraw, int key);
  uint8_t cardsScreen(bool redraw, int key);
  uint8_t displayScreen(bool redraw, int key);
  uint8_t paddlesScreen(bool redraw, int key);
  uint8_t networkScreen(bool redraw, int key);
  uint8_t advancedScreen(bool redraw, int key);
  uint8_t aboutScreen(bool redraw, int key);
  uint8_t browseScreen(bool redraw, int key);
  uint8_t messageScreen(bool redraw, int key);

  void drawMain();
  void drawCards();
  void drawDisplay();
  void drawPaddles();
  void drawNetwork();
  void drawAdvanced();
  void drawBrowser();

  uint8_t mainAction(int row);
  uint8_t leaveBIOS();

  void WarmReset();
  void ColdReboot();

  void openBrowser(uint8_t kind, uint8_t drive);
  void refilter();
  void cacheDirectory();
  void enterDirectory(const char *name);
  void stripDirectory();

  uint8_t notice(const char *a, const char *b, uint8_t back);

 private:
  uint8_t screen;
  uint8_t mainSel;
  uint8_t sel;
  uint8_t speedIndex;

  uint8_t browseKind;
  uint8_t browseDrive;
  uint16_t browseTop;
  uint16_t browseSel;
  char findText[16];
  char rootPath[255 - BIOS_MAXPATH];

  const char *noticeA, *noticeB;
  uint8_t noticeBack;
};

#endif
