#ifndef __TEENSY_USB
#define __TEENSY_USB

#include <Arduino.h>
#include <USBHost_t36.h>

typedef void (*keyboardCallback)(int unicode);

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

#endif
