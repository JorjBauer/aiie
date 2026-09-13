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
  UIeHD_state        = 6,   // true: no image mounted on the hard drive card
  UIeHD_activity     = 7,
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
  volatile bool redrawFrame;
  volatile bool redrawDriveLatches;
  volatile bool redrawDriveActivity;
  bool driveEmpty[2];   // the UIeDiskN_state protocol: true means NO disk in the drive
  bool hdEmpty;
  bool hdActivity;
  bool driveActivity[2];
};


#endif
