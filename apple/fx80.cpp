#include <string.h> // memset
#include <stdint.h>
#include <stdio.h> // while debugging

#include "fx80.h"
#include "fx80-font.h"

#include "globals.h"

static const
uint8_t intlCharsetMap[9][12] = { { '#', '$', '@', '[', '\\', ']', '^', '`', '{', '|', '}', '~' }, // USA
				  { '#', '$',   0,   5,   15,  16, '^', '`',  30,   2,   1,  22 }, // France
				  { '#', '$',  16,  23,   24,  25, '^', '`',  26,  27,  28,  17 }, // Germany
				  {   6, '$', '@', '[', '\\', ']', '^', '`', '{', '|', '}', '~' }, // UK
				  { '#', '$', '@',  18,   20,  13, '^', '`',  19,  21,  14, '~' }, // Denmark
				  { '#',  11,  29,  23,   24,  13,  25,  30,  26,  27,  14,  28 }, // Sweden
				  { '#', '$', '@',   5, '\\',  30, '^',   2,   0,   3,   1,   4 }, // Italy
				  {  12, '$', '@',   7,    9,   8, '^', '`',  22,  10, '}', '~' }, // Spain
				  { '#', '$', '@', '[',   31, ']', '^', '`', '{', '|', '}', '~' }, // Japan
};

Fx80::Fx80()
{
  Reset();
}

Fx80::~Fx80()
{
}

void Fx80::Reset()
{
  charsetEnabled = CS_USA;
  fontMode = FM_Pica;
  fontMode |= FM_Emphasized;

  clearLine();
  escapeMode = false;
  proportionalMode = false;
  carriageDot = 0;
  twoSixteenthsLineSpacing = 36; // 1/6" line spacing is the default (12 pixels)
  graphicsWidth = 960;
  ninePinGraphics = false;
  escapeModeActive = 0;
  escapeModeExpectingBytes = -1;
  escapeModeLengthByteCount = 0;
  escapeModeLength = 0;
  italicsMode = false;
}

void Fx80::handleEscape(uint8_t c)
{
  switch (c) {
  case 75: // FIXME: single-density 480 dpi graphics line
    graphicsWidth = 480;
    break;
  case 76: // FIXME: low-speed double-density graphics line
  case 89: // FIXME: high-speed double-density graphics
    graphicsWidth = 960;
    escapeModeActive = c;
    escapeModeExpectingBytes = -1;
    escapeModeLengthByteCount = 0;
    break;
  case 90: // FIXME: quadruple-density graphics
    graphicsWidth = 960 * 2;
    break;
  case 94: // FIXME: enable 9-pin graphics
    escapeModeActive = c;
    escapeModeExpectingBytes = -1;
    escapeModeLengthByteCount = 0;
    break;
  case 65: // set line spacing
    escapeModeActive = c;
    escapeModeExpectingBytes = 1;
    break;
  case 33: // FIXME: mode select
  case 35: // FIXME: enable 8-bit reception from computer
  case 37: // FIXME: pick charset from ROM
  case 38: // FIXME: define chars in RAM
  case 42: // FIXME: set vertical tabs
  case 43: // FIXME: set form length (default: 66 lines, 11 inches)
    break;
  case 52: // italics on
    italicsMode = true;
    break;
  case 53: // italics off
    italicsMode = false;
    break;
  case 64: // Reset
    Reset();
    break;
  case 68: // FIXME: reset current tabs, pitch
    break;
  case 69: // emphasized mode on
    fontMode |= FM_Emphasized;
    break;
  case 70: // emphasized mode off
    fontMode &= ~FM_Emphasized;
    break;
  case 71: // FIXME: double-strike on
  case 72: // FIXME: double-strike off
  case 73: // FIXME: enable printing chr[0..31] except control codes
  case 74: // FIXME: line feed immediately, n/216th of an inch
    break;
  case 77: // elite mode (12cpi)
    fontMode |= FM_Elite;
    break;
  case 78: // FIXME: turn on skip-over perforation
  case 79: // FIXME: turn off skip-over perforation
    break;
  case 80: // disable elite; enable pica mode (unless compressed is enabled)
    fontMode &= ~FM_Elite;
    break;
  case 81: // FIXME: cancel print buffer, set right margin
    break;
  case 82: // FIXME: select international charset
    escapeModeExpectingBytes = 1;
    escapeModeActive = c;
    break;
  case 83: // FIXME: script mode on
  case 84: // FIXME: script mode off
  case 85: // FIXME: unidirecitonal mode on/off
    break;
  case 87: // expanded mode (1=on; 0=off)
    escapeModeExpectingBytes = 1;
    escapeModeActive = c;
    break;
  case 98: // FIXME: set vertical tab
  case 105: // FIXME: immediate mode on
  case 106: // FIXME: immediate reverse linefeed 1/216"
  case 108: // FIXME: set left margin
  case 112: // FIXME: turn on proportional mode
  case 115: // FIXME: print speed
    printf("unhandled escape code %d\n", c);
    break;

  }
}

void Fx80::handleActiveEscapeMode(uint8_t c)
{
  switch (escapeModeActive) {
  case 76:
  case 89:
    // one column of double-density graphics
    {
      // FIXME: abstract this - second time it's being used
      uint16_t byteIdx = (carriageDot+0) / 8 + (FX80_MAXWIDTH/8) * 0;
      uint8_t bitIdx = (carriageDot+0) % 8;
      for (int i=0; i<8; i++) {
	if (c & (1 << (7-i))) {
	  rowOfBits[byteIdx] |= (1 << (7-bitIdx));
	}
	byteIdx += (FX80_MAXWIDTH/8);
      }
    }
    carriageDot++;
    break;
  case 65: // set line spacing to n/72ths of an inch (n=0-85)
    twoSixteenthsLineSpacing = 3 * c;

    break;
  case 82:
    charsetEnabled = (Charset) (c % 9);
    break;
  case 87:
    if (c == 1) 
      fontMode |= FM_Expanded;
    else if (c == 0)
      fontMode &= ~FM_Expanded;
    break;
  default:
    printf("unhandled active escape mode %d\n", c);
    break;
  }
}

void Fx80::input(uint8_t c)
{
  if (escapeMode) {
    handleEscape(c);
    escapeMode = false;
    return;
  }

  // Is this an escape mode that gets a fixed amount of input?
  if (escapeModeActive) {
    if (escapeModeExpectingBytes < 0) {
      // We're reading 2 bytes of length
      escapeModeLengthByteCount++;
      if (escapeModeLengthByteCount == 1) {
	escapeModeLength = c;
      } else {
	escapeModeLength |= (c << 8);
	escapeModeExpectingBytes = escapeModeLength;
      }
      return;
    } else {
      escapeModeExpectingBytes--;
    }

    handleActiveEscapeMode(c);
    if (escapeModeExpectingBytes == 0) {
      escapeModeActive = 0;
    }
    return;
  }

  if (c == 27) {
    escapeMode = true;
    return;
  }

  // FIXME: all these also work as 128 + c
  switch (c) {
  case 0: // FIXME: terminate horiz/vert tab setting
    return;
  case 7: // beep
    return;
  case 8: // FIXME: "at current width" instead of this fixed-12
    if (carriageDot >= 12) 
      carriageDot -= 12;
    return;
  case 9: // FIXME: HTAB
    return;
  case 10:
    lineFeed();
    return;
  case 11: // FIXME: VTAB
    return;
  case 12: // FIXME: Form Feed
    lineFeed();
    return;
  case 13:
    carriageDot = 0;
    // lineFeed(); // FIXME: this was controlled by a switch
    return;
  case 14: // FIXME: Shift Out - turns on "Expanded Mode"
  case 15: // FIXME: Shift In - turns on "Compressed Mode"
  case 17: // FIXME: DC1
  case 18: // FIXME: DC2
  case 19: // FIXME: DC3
  case 20: // FIXME: DC4
  case 24: // FIXME: cancel text in print buffer; not the same as "clear line"
  case 127: // FIXME: delete last char in the print buffer
    return;
    
  }

  // normal print - send the character verbatim...
  addCharacter(c);
}

// add the given character on the line at the current carriage dot position
void Fx80::addCharacter(uint8_t c)
{
  // Jigger up the character if we're in an international mode
  if (charsetEnabled != CS_USA) {
    switch (c) {
    case 35:
      c = intlCharsetMap[charsetEnabled][0];
      break;
    case 36:
      c = intlCharsetMap[charsetEnabled][1];
      break;
    case 64:
      c = intlCharsetMap[charsetEnabled][2];
      break;
    case 91:
      c = intlCharsetMap[charsetEnabled][3];
      break;
    case 92:
      c = intlCharsetMap[charsetEnabled][4];
      break;
    case 93:
      c = intlCharsetMap[charsetEnabled][5];
      break;
    case 94:
      c = intlCharsetMap[charsetEnabled][6];
      break;
    case 96:
      c = intlCharsetMap[charsetEnabled][7];
      break;
    case 123:
      c = intlCharsetMap[charsetEnabled][8];
      break;
    case 124:
      c = intlCharsetMap[charsetEnabled][9];
      break;
    case 125:
      c = intlCharsetMap[charsetEnabled][10];
      break;
    case 126:
      c = intlCharsetMap[charsetEnabled][11];
      break;
    }
  }

  if (italicsMode) {
    c += 128;
  }

  /*uint8_t width =*/ Fx80Font[c * 19];
  // FIXME: is 12 right for non-proportional mode?
  /*  if (!proportionalMode)
      width = 12;*/

  // Each row for this char has two bytes of bits, left-to-right, high
  // bit leftmost.

  const uint8_t *charPtr = &Fx80Font[c * 19];
  charPtr++;

  for (uint8_t row=0; row<9; row++) {
    uint16_t rowData = (charPtr[2*row] << 1) | (charPtr[2*row+1]>>7);
    float xoffTo = 0;
    for (uint8_t xoff = 0; xoff <= 8; xoff++) {

      // Don't print beyond end of line!
      if (carriageDot+xoff >= FX80_MAXWIDTH)
	continue;

      float pw = pixelWidthOfSelectedFont();

      // Figure out where we're drawing *to*
      xoffTo += pw;
      uint16_t byteIdx = (carriageDot+xoffTo) / 8 + (FX80_MAXWIDTH/8) * row;
      uint8_t bitIdx = (uint8_t)((float)carriageDot+xoffTo) % 8;

      // We never clear bits - it's possible to overstrike, so just add more...
      if (rowData & (1 << (8-xoff))) {
	rowOfBits[byteIdx] |= (1 << (7-bitIdx));
      }

      // If we're in emphasized mode, then repeat the pixel one to the right-
      //   without changing xOffTo, so we'll strike over
      if (fontMode & FM_Emphasized) {
	byteIdx = (carriageDot+xoffTo+pw) / 8 + (FX80_MAXWIDTH/8) * row;
	bitIdx = (uint8_t)((float)carriageDot+xoffTo+pw) % 8;

	// Add this bit too
	if (rowData & (1 << (8-xoff))) {
	  rowOfBits[byteIdx] |= (1 << (7-bitIdx));
	}
      }

      // If we're in expanded mode, then repeat the pixel offset to
      // the right one dot - while changing xOffTo, moving the print head
      if (fontMode & FM_Expanded) {
	xoffTo += pw;
	byteIdx = (carriageDot+xoffTo) / 8 + (FX80_MAXWIDTH/8) * row;
	bitIdx = (uint8_t)((float)carriageDot+xoffTo) % 8;

	// Add this bit too
	if (rowData & (1 << (8-xoff))) {
	  rowOfBits[byteIdx] |= (1 << (7-bitIdx));
	}
      }
      
    }
  }

  carriageDot += characterWidthOfSelectedFont(c);
}

  // Determine our dot spacing. We have 960 dots across a page, and we
  // want to align the various font types so they are similar to the
  // original:
  //
  // Pica: 60 col/inch; 10 chars/inch  -- 85 chars across a page
  // Elite: 72 col/inch; 12 chars/inch -- 102 chars across a page
  // Compressed: 17.16 chars/inch      -- 145.86 chars across a page
  // Expanded - double width w/ repeating columns
  //
  // If we use 12 dots across for one Pica character, that gives us 80
  // characters. I'm going to call that close enough, although it's
  // not quite right; otherwise we have to quadruple the width, which
  // just makes the buffers too big for this project.

// This is how wide one of the pixels is at the given spacing.
float Fx80::pixelWidthOfSelectedFont()
{
  float ret;

  if (fontMode & FM_Elite)
    ret = (10.0/12.0);
  else if (fontMode & FM_Compressed)
    ret = (7.0/12.0);
  else /*if (fontMode & FM_Pica)*/
    ret = (12.0/12.0);

  // Don't double for expanded mode; we do that inline

  return ret;
}

uint8_t Fx80::characterWidthOfSelectedFont(uint8_t c)
{
  uint8_t ret;
  
  if (fontMode & FM_Elite)
    ret = 10;
  else if (fontMode & FM_Compressed)
    ret = 7;
  else /*if (fontMode & FM_Pica)*/
    ret = 12;

  if (fontMode & FM_Expanded)
    ret *= 2;

  return ret;
}

void Fx80::lineFeed()
{
  emitLine();
  g_printer->moveDownPixels(twoSixteenthsLineSpacing / 3); // pixel estimation
  clearLine();
}

void Fx80::clearLine()
{
  memset(rowOfBits, 0, sizeof(rowOfBits));
}

void Fx80::emitLine()
{
  // FIXME: this is very wrong. Doesn't deal with line advance
  // properly. But it's good enough for debugging basic operation.
  g_printer->addLine(rowOfBits);
}

