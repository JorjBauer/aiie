#ifndef __TEENSY_USB
#define __TEENSY_USB

#include <Arduino.h>
#include <USBHost_t36.h>

typedef void (*keyboardCallback)(uint8_t keycode);

class TeensyUSB {
 public:
  TeensyUSB();
  ~TeensyUSB();

  void init();
  void attachKeypress(keyboardCallback cb);
  void attachKeyrelease(keyboardCallback cb);

  uint8_t getModifiers();
  uint8_t getOemKey();
  
  void maintain();
};

// Service the input devices (USB host + keyboard) once. Call this from inside
// any blocking loop that must still see keypresses (e.g. a confirmation
// prompt); the main loop normally does this every pass.
void teensyServiceInput();

#endif
