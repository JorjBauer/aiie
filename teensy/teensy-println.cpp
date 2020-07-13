#include "teensy-println.h"

namespace arduino_preprocessor_is_buggy {
  
  bool serialavailable() {
    //    Threads::Scope locker(getSerialLock());
    return Serial.available();
  }
  
  char serialgetch() {
    //    Threads::Scope locker(getSerialLock());
    return Serial.read();
  }

};
