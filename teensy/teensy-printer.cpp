#include <Arduino.h>
#include "teensy-printer.h"

#define WIDTH (384)       // width of printer, in dots
#define NATIVEWIDTH 960 // 

#define RXPIN 57
#define TXPIN 56

TeensyPrinter::TeensyPrinter()
{
  ser = new SoftwareSerial(RXPIN, TXPIN, false);
  ser->begin(19200);
  char buf[6] = { 27, '@',          // init command '@'
		  27, '3', 8,       // ESC-3 is "set line spacing"; default 30
		  0                 // terminator
  };
  ser->print(buf);
}

TeensyPrinter::~TeensyPrinter()
{
  delete ser;
}

void TeensyPrinter::update()
{
}

void TeensyPrinter::addLine(uint8_t *rowOfBits)
{
  static uint8_t linebuf[WIDTH/8]; // output data for one line of pixels
  
  // The rowOfBits is a set of *rows* of bits. The printer needs *columns*.
  // Convert and send as necessary.

  // Send bitmap command, followed by bitmap data, followed by linefeed?
  //  ser->write(27); // set line spacing
  //  ser->write('3');
  //  ser->write((uint8_t)30);

#define DC2 18

  // FIXME: is this 0-6, or 1-7? One of them is empty..
  // FIXME: also read this off of the print head size/line feed size?
  for (int yoff=1; yoff<8; yoff++) {
    memset(linebuf, 0, sizeof(linebuf)); // start clear...
    for (int i=0; i<(NATIVEWIDTH/8); i++) {
      uint8_t bv = rowOfBits[yoff*120+i];
      // Process the 8 bits in this byte
      for (int xoff=0; xoff<8; xoff++) {
	// scale X from "actual FX80" coordinates to "real printer" coordinates
	uint16_t actualX = (uint16_t)(((float)(i*8+xoff) * (float)WIDTH) / (float)NATIVEWIDTH);

	if (bv & (1 << (7-xoff))) { // if it's on in the original
	  // then turn it on in our copy
	  uint8_t bitNum = actualX & 0x07;
	  linebuf[actualX>>3] |= (1<<(7-bitNum));
	}
      }
    }

    // Send this line to the printer
    ser->write(DC2); // send this line as a bitmap
    ser->write('*');
    ser->write(1); // FIXME: height
    ser->write(48); // FIXME: width, in bytes
    for (int i=0; i<WIDTH/8; i++) {
      ser->write(linebuf[i]);
    }
  }

  //  ser->write(10); // linefeed @ the end
}

void TeensyPrinter::moveDownPixels(uint8_t p)
{
}
