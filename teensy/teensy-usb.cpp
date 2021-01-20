#include <Arduino.h>
#include <USBHost_t36.h>
#include "teensy-usb.h"

// There are multiple hubs here because without them, USB ports won't work
// if it's a chained hub device. From what I've read, most hubs are 4-port
// devices -- but there are 7-port hubs, which use 2 chained hub chips, so
// for all the USB ports to work on those hubs we have to have 2 hub objects
// managing them. I've decided to limit this to 2 hub objects.
USBHost myusb;
USBHub hub1(myusb);
USBHub hub2(myusb);

// One could have multiple keyboards? I think I'm not going to support that
// just yet, until I understand all of this better.
KeyboardController keyboard1(myusb);
//KeyboardController keyboard2(myusb);

TeensyUSB::TeensyUSB()
{
}

TeensyUSB::~TeensyUSB()
{
}

void TeensyUSB::init()
{
  myusb.begin();
}

void TeensyUSB::attachKeypress(keyboardCallback cb)
{
  keyboard1.attachRawPress(cb);
  //  keyboard2.attachRawPress(cb);
}

void TeensyUSB::attachKeyrelease(keyboardCallback cb)
{
  keyboard1.attachRawRelease(cb);
  //  keyboard2.attachRawRelease(cb);
}

void TeensyUSB::maintain()
{
  myusb.Task();
}

uint8_t TeensyUSB::getModifiers()
{
  // FIXME: specifically keyboard1? guess the callbacks need a kb #
  return keyboard1.getModifiers();
}

uint8_t TeensyUSB::getOemKey()
{
  // same FIXME as getModifiers
  return keyboard1.getOemKey();
}
