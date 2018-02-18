#ifndef __APPLEUI_H
#define __APPLEUI_H

#include "vmui.h"

// Element IDs
enum {
  UIeOverlay         = 0,
  UIeDisk1_state     = 1,
  UIeDisk2_state     = 2,
  UIeDisk1_activity  = 3,
  UIeDisk2_activity  = 4,
  UIePowerPercentage = 5,
};

class AppleUI : public VMui {
 public:
  AppleUI();
  ~AppleUI();

  virtual void drawStaticUIElement(uint8_t element);
  virtual void drawOnOffUIElement(uint8_t element, bool state);
  virtual void drawPercentageUIElement(uint8_t element, uint8_t percent);

  void drawBatteryStatus(uint8_t percent);

  virtual void blit();

 private:
  bool redrawFrame;
  bool redrawDriveLatches;
  bool redrawDriveActivity;
  bool driveInserted[2];
  bool driveActivity[2];
};


#endif
