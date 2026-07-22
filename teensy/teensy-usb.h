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

  // True while a USB keyboard is enumerated.
  bool keyboardConnected();
  // Drive the keyboard's caps-lock LED so it can match the emulator's caps
  // state rather than reading inverted. No-op if no keyboard is present.
  void setCapsLED(bool on);

  void maintain();
};

// Service the input devices (USB host + keyboard) once. Call this from inside
// any blocking loop that must still see keypresses (e.g. a confirmation
// prompt); the main loop normally does this every pass.
void teensyServiceInput();

#endif
