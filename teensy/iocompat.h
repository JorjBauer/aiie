#ifndef __IOCOMPAT_H
#define __IOCOMPAT_H

#include "globals.h"
#include <Arduino.h>

#define printf(x, ...) {sprintf(fsbuf, x, ##__VA_ARGS__); Serial.println(fsbuf); Serial.flush(); Serial.send_now();}
#define fprintf(f, x, ...) {sprintf(fsbuf, x, ##__VA_ARGS__); Serial.println(fsbuf); Serial.flush(); Serial.send_now();}
#define perror(x) {Serial.println(x);Serial.flush(); Serial.send_now();}
  

#endif
