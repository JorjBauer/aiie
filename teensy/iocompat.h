#ifndef __IOCOMPAT_H
#define __IOCOMPAT_H

#include "globals.h"
#include <Arduino.h>

#define printf(x, ...) {snprintf(fsbuf, sizeof(fsbuf), x, ##__VA_ARGS__); Serial.println(fsbuf); Serial.flush(); Serial.send_now();}
#define fprintf(f, x, ...) {snprintf(fsbuf, sizeof(fsbuf), x, ##__VA_ARGS__); Serial.println(fsbuf); Serial.flush(); Serial.send_now();}
#define perror(x) {Serial.println(x);Serial.flush(); Serial.send_now();}
  

#endif
