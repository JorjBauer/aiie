#ifndef __VMUI_H
#define __VMUI_H

//#ifndef TEENSYDUINO
#include <stdint.h>
//#endif

/* Abstraction of VM-specific UI element drawing. 
 *
 * UI elements are things like
 *   - disk drive 1/inserted, disk drive 1/ejected;
 *   - power icon percentage;
 *   - disk drive 1/activity indicator, on/off;
 *   - parallel port activity;
 *   - network activity;
 *  etc. (No, not all of those things exist yet.)
 * 
 * This encapsulates the VM positioning and icon data. The actual
 * drawing routines are in the physical implementation (TeensyDisplay
 * or SDLDisplay).
 * 
 * Most of those are on/off indicators. Some are percentage indicators.
 * The element IDs are in the VM's UI class (cf. AppleUI).
 */

class VMui
{
 public:
  VMui() {};
  ~VMui() {};

  virtual void drawStaticUIElement(uint8_t element) = 0;
  virtual void drawOnOffUIElement(uint8_t element, bool state) = 0; // on or off
  virtual void drawPercentageUIElement(uint8_t element, uint8_t percent) = 0; // 0-100
};

#endif
