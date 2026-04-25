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
  oneLineExpanded = false;
  underlineMode = false;
  doubleStrike = false;
  scriptMode = 0;
  formLengthLines = 66;
  currentLine = 0;

  numTabStops = 0;
  for (int i = 8; i <= 80; i += 8) {
    tabStops[numTabStops++] = i;
  }
  numVtabStops = 0;

  leftMarginDot = 0;
  rightMarginDot = FX80_MAXWIDTH;
}

void Fx80::cancelOneLineExpanded()
{
  if (oneLineExpanded) {
    fontMode &= ~FM_Expanded;
    oneLineExpanded = false;
  }
}

void Fx80::handleEscape(uint8_t c)
{
  switch (c) {
  case 75: // single-density graphics (480 dots per 8" line)
    graphicsWidth = 480;
    escapeModeActive = c;
    escapeModeExpectingBytes = -1;
    escapeModeLengthByteCount = 0;
    break;
  case 76: // low-speed double-density graphics
  case 89: // high-speed double-density graphics
    graphicsWidth = 960;
    escapeModeActive = c;
    escapeModeExpectingBytes = -1;
    escapeModeLengthByteCount = 0;
    break;
  case 90: // quadruple-density graphics (1920 dots per 8" line)
    graphicsWidth = 1920;
    escapeModeActive = c;
    escapeModeExpectingBytes = -1;
    escapeModeLengthByteCount = 0;
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
  case 48: // ESC 0: set line spacing to 1/8" (27/216")
    twoSixteenthsLineSpacing = 27;
    break;
  case 49: // ESC 1: set line spacing to 7/72" (21/216")
    twoSixteenthsLineSpacing = 21;
    break;
  case 50: // ESC 2: set line spacing to 1/6" default (36/216")
    twoSixteenthsLineSpacing = 36;
    break;
  case 51: // ESC 3: set line spacing to n/216"
    escapeModeActive = c;
    escapeModeExpectingBytes = 1;
    break;
  case 33: // ESC !: Master Select (1-byte bit field)
    escapeModeActive = c;
    escapeModeExpectingBytes = 1;
    break;
  case 35: // FIXME: enable 8-bit reception from computer
  case 37: // FIXME: pick charset from ROM
  case 38: // FIXME: define chars in RAM
    break;
  case 42: // ESC *: master graphics mode (mode byte + 2-byte length + data)
    escapeModeActive = 42;
    escapeModeExpectingBytes = 1;
    escapeModeLengthByteCount = 0;
    break;
  case 45: // ESC -: underline on/off (1-byte param)
    escapeModeActive = c;
    escapeModeExpectingBytes = 1;
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
  case 66: // ESC B: set vertical tabs (NUL-terminated list)
    escapeModeActive = c;
    escapeModeExpectingBytes = 17;
    numVtabStops = 0;
    break;
  case 67: // ESC C: set form length
    escapeModeActive = c;
    escapeModeExpectingBytes = 1;
    break;
  case 68: // ESC D: set horizontal tabs (NUL-terminated list)
    escapeModeActive = c;
    escapeModeExpectingBytes = 33;
    numTabStops = 0;
    break;
  case 69: // emphasized mode on
    fontMode |= FM_Emphasized;
    break;
  case 70: // emphasized mode off
    fontMode &= ~FM_Emphasized;
    break;
  case 71: // ESC G: double-strike on
    doubleStrike = true;
    break;
  case 72: // ESC H: double-strike off
    doubleStrike = false;
    break;
  case 73: // FIXME: enable printing chr[0..31] except control codes
    break;
  case 74: // ESC J: immediate line feed n/216" without CR
    escapeModeActive = c;
    escapeModeExpectingBytes = 1;
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
  case 81: // ESC Q: set right margin (1-byte param)
    escapeModeActive = c;
    escapeModeExpectingBytes = 1;
    break;
  case 82: // FIXME: select international charset
    escapeModeExpectingBytes = 1;
    escapeModeActive = c;
    break;
  case 83: // ESC S: script mode on (1-byte param: 0=super, 1=sub)
    escapeModeActive = c;
    escapeModeExpectingBytes = 1;
    break;
  case 84: // ESC T: script mode off
    scriptMode = 0;
    break;
  case 85: // FIXME: unidirectional mode on/off
    break;
  case 87: // expanded mode (1=on; 0=off)
    escapeModeExpectingBytes = 1;
    escapeModeActive = c;
    break;
  case 98: // FIXME: set vertical tab channel
  case 105: // FIXME: immediate mode on
  case 106: // FIXME: immediate reverse linefeed 1/216"
    break;
  case 108: // ESC l: set left margin (1-byte param)
    escapeModeActive = c;
    escapeModeExpectingBytes = 1;
    break;
  case 112: // ESC p: proportional mode on/off (1-byte param)
    escapeModeActive = c;
    escapeModeExpectingBytes = 1;
    break;
  case 115: // FIXME: print speed
    break;

  }
}

void Fx80::handleActiveEscapeMode(uint8_t c)
{
  switch (escapeModeActive) {
  case 42: // ESC *: mode byte selects density, then switch to length+data
    switch (c) {
    case 0: graphicsWidth = 480;  escapeModeActive = 75; break;
    case 1: graphicsWidth = 960;  escapeModeActive = 76; break;
    case 2: graphicsWidth = 960;  escapeModeActive = 89; break;
    case 3: graphicsWidth = 1920; escapeModeActive = 90; break;
    case 4: graphicsWidth = 640;  escapeModeActive = 75; break;
    case 5: graphicsWidth = 576;  escapeModeActive = 76; break;
    case 6: graphicsWidth = 720;  escapeModeActive = 76; break;
    default: graphicsWidth = 960; escapeModeActive = 76; break;
    }
    escapeModeExpectingBytes = -1;
    escapeModeLengthByteCount = 0;
    break;
  case 75: // single-density: each graphics dot spans 2 horizontal dots
  case 76: // double-density
  case 89: // high-speed double-density
  case 90: // quadruple-density: 2 graphics dots per horizontal dot
    {
      bool singleDensity = (escapeModeActive == 75);
      bool quadDensity = (escapeModeActive == 90);
      int dotsPerColumn = singleDensity ? 2 : (quadDensity ? 1 : 1);

      for (int i = 0; i < 8; i++) {
	if (c & (1 << (7 - i))) {
	  for (int d = 0; d < dotsPerColumn; d++) {
	    uint16_t x = carriageDot + d;
	    if (x < FX80_MAXWIDTH) {
	      uint16_t byteIdx = x / 8 + (FX80_MAXWIDTH / 8) * i;
	      uint8_t bitIdx = x % 8;
	      rowOfBits[byteIdx] |= (1 << (7 - bitIdx));
	    }
	  }
	}
      }
      carriageDot += dotsPerColumn;
    }
    break;
  case 51: // ESC 3: set line spacing to n/216"
    twoSixteenthsLineSpacing = c;
    break;
  case 65: // set line spacing to n/72ths of an inch (n=0-85)
    twoSixteenthsLineSpacing = 3 * c;
    break;
  case 74: // ESC J: immediate line feed of n/216" (no CR, doesn't change spacing)
    emitLine();
    clearLine();
    g_printer->moveDownPixels(c / 3);
    break;
  case 33: // ESC !: Master Select bit field
    fontMode = FM_Pica;
    if (c & 0x01) fontMode |= FM_Elite;
    if (c & 0x02) proportionalMode = true; else proportionalMode = false;
    if (c & 0x04) fontMode |= FM_Compressed;
    if (c & 0x08) fontMode |= FM_Emphasized;
    if (c & 0x10) doubleStrike = true; else doubleStrike = false;
    if (c & 0x20) fontMode |= FM_Expanded;
    break;
  case 45: // ESC -: underline
    underlineMode = (c & 1);
    break;
  case 66: // ESC B: accumulate vertical tab stops
    if (c == 0 || numVtabStops >= 16) {
      escapeModeExpectingBytes = 0;
    } else {
      vtabStops[numVtabStops++] = c;
    }
    break;
  case 67: // ESC C: set form length
    if (c == 0) {
      // ESC C NUL n: inches form. Read one more byte.
      escapeModeActive = 200;
      escapeModeExpectingBytes = 1;
    } else {
      formLengthLines = c;
      currentLine = 0;
    }
    break;
  case 200: // ESC C NUL n: form length in inches
    formLengthLines = c * 6; // approximate: 6 lines per inch at default spacing
    currentLine = 0;
    break;
  case 68: // ESC D: accumulate horizontal tab stops
    if (c == 0 || numTabStops >= 32) {
      escapeModeExpectingBytes = 0;
    } else {
      tabStops[numTabStops++] = c;
    }
    break;
  case 81: { // ESC Q: set right margin (column number)
    uint8_t charWidth = characterWidthOfSelectedFont(' ');
    rightMarginDot = (uint16_t)c * charWidth;
    if (rightMarginDot > FX80_MAXWIDTH || rightMarginDot == 0)
      rightMarginDot = FX80_MAXWIDTH;
    break;
  }
  case 82:
    charsetEnabled = (Charset) (c % 9);
    break;
  case 83: // ESC S: script mode (0=superscript, 1=subscript)
    scriptMode = (c & 1) ? 2 : 1;
    break;
  case 87:
    if (c == 1)
      fontMode |= FM_Expanded;
    else if (c == 0)
      fontMode &= ~FM_Expanded;
    break;
  case 108: { // ESC l: set left margin (column number)
    uint8_t charWidth = characterWidthOfSelectedFont(' ');
    leftMarginDot = (uint16_t)c * charWidth;
    if (leftMarginDot >= FX80_MAXWIDTH)
      leftMarginDot = 0;
    break;
  }
  case 112: // ESC p: proportional mode
    proportionalMode = (c & 1);
    break;
  default:
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

  // Control codes 128-155 mirror 0-27
  if (c >= 128 && c <= 155)
    c -= 128;

  switch (c) {
  case 0: // FIXME: terminate horiz/vert tab setting
    return;
  case 7: // beep
    return;
  case 8: { // backspace one character width
    uint8_t w = characterWidthOfSelectedFont(' ');
    if (carriageDot >= w)
      carriageDot -= w;
    else
      carriageDot = 0;
    return;
  }
  case 9: { // horizontal tab
    uint8_t charWidth = characterWidthOfSelectedFont(' ');
    uint8_t currentCol = (charWidth > 0) ? (carriageDot / charWidth) : 0;
    for (uint8_t i = 0; i < numTabStops; i++) {
      if (tabStops[i] > currentCol) {
	carriageDot = tabStops[i] * charWidth;
	if (carriageDot >= FX80_MAXWIDTH)
	  carriageDot = FX80_MAXWIDTH - 1;
	break;
      }
    }
    return;
  }
  case 10:
    cancelOneLineExpanded();
    lineFeed();
    return;
  case 11: // vertical tab
    if (numVtabStops > 0) {
      for (uint8_t i = 0; i < numVtabStops; i++) {
	if (vtabStops[i] > currentLine) {
	  uint16_t linesToAdvance = vtabStops[i] - currentLine;
	  for (uint16_t j = 0; j < linesToAdvance; j++)
	    lineFeed();
	  break;
	}
      }
    }
    return;
  case 12: // form feed
    cancelOneLineExpanded();
    emitLine();
    clearLine();
    carriageDot = 0;
    if (currentLine > 0) {
      uint16_t linesRemaining = formLengthLines - currentLine;
      g_printer->moveDownPixels(linesRemaining * (twoSixteenthsLineSpacing / 3));
    }
    currentLine = 0;
    return;
  case 13:
    cancelOneLineExpanded();
    carriageDot = leftMarginDot;
    return;
  case 14: // SO: one-line expanded mode ON
    if (!(fontMode & FM_Expanded)) {
      fontMode |= FM_Expanded;
      oneLineExpanded = true;
    }
    return;
  case 15: // SI: compressed mode ON
    fontMode |= FM_Compressed;
    return;
  case 17: // FIXME: DC1 - activate printer
  case 19: // FIXME: DC3 - deactivate printer
    return;
  case 18: // DC2: compressed mode OFF
    fontMode &= ~FM_Compressed;
    return;
  case 20: // DC4: one-line expanded mode OFF
    cancelOneLineExpanded();
    return;
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

  uint8_t charWidth = characterWidthOfSelectedFont(c);

  if (carriageDot + charWidth > rightMarginDot)
    return;

  const uint8_t *charPtr = &Fx80Font[c * 19];
  charPtr++; // skip proportional width byte

  for (uint8_t fontRow = 0; fontRow < 9; fontRow++) {
    // Determine which output row this font row maps to
    uint8_t outRow;
    if (scriptMode == 1) { // superscript: compress 9 rows into rows 0-4
      outRow = fontRow / 2;
      if ((fontRow & 1) && fontRow > 0) continue; // skip odd rows
    } else if (scriptMode == 2) { // subscript: compress into rows 4-8
      outRow = 4 + fontRow / 2;
      if ((fontRow & 1) && fontRow > 0) continue;
    } else {
      outRow = fontRow;
    }

    uint16_t rowData = (charPtr[2*fontRow] << 1) | (charPtr[2*fontRow+1]>>7);
    float xoffTo = 0;
    float pw = pixelWidthOfSelectedFont();

    for (uint8_t xoff = 0; xoff <= 8; xoff++) {
      if (carriageDot + (uint16_t)xoffTo >= FX80_MAXWIDTH)
	break;

      xoffTo += pw;
      uint16_t dotX = carriageDot + (uint16_t)xoffTo;
      if (dotX >= FX80_MAXWIDTH) break;

      if (rowData & (1 << (8 - xoff))) {
	uint16_t byteIdx = dotX / 8 + (FX80_MAXWIDTH/8) * outRow;
	uint8_t bitIdx = dotX % 8;
	rowOfBits[byteIdx] |= (1 << (7 - bitIdx));

	// Double-strike: also set the dot one row below
	if (doubleStrike && outRow < 8) {
	  uint16_t dsIdx = dotX / 8 + (FX80_MAXWIDTH/8) * (outRow + 1);
	  rowOfBits[dsIdx] |= (1 << (7 - bitIdx));
	}
      }

      // Emphasized: repeat dot one position to the right
      if (fontMode & FM_Emphasized) {
	uint16_t empX = carriageDot + (uint16_t)(xoffTo + pw);
	if (empX < FX80_MAXWIDTH && (rowData & (1 << (8 - xoff)))) {
	  uint16_t byteIdx = empX / 8 + (FX80_MAXWIDTH/8) * outRow;
	  uint8_t bitIdx = empX % 8;
	  rowOfBits[byteIdx] |= (1 << (7 - bitIdx));
	}
      }

      // Expanded: double-width by repeating and advancing
      if (fontMode & FM_Expanded) {
	xoffTo += pw;
	uint16_t expX = carriageDot + (uint16_t)xoffTo;
	if (expX < FX80_MAXWIDTH && (rowData & (1 << (8 - xoff)))) {
	  uint16_t byteIdx = expX / 8 + (FX80_MAXWIDTH/8) * outRow;
	  uint8_t bitIdx = expX % 8;
	  rowOfBits[byteIdx] |= (1 << (7 - bitIdx));
	}
      }
    }
  }

  // Underline: draw continuous line across character width in row 8
  if (underlineMode) {
    uint8_t ulRow = (scriptMode == 1) ? 4 : 8;
    uint16_t totalDots = charWidth;
    for (uint16_t d = 0; d < totalDots; d++) {
      uint16_t dotX = carriageDot + d;
      if (dotX < FX80_MAXWIDTH) {
	uint16_t byteIdx = dotX / 8 + (FX80_MAXWIDTH/8) * ulRow;
	uint8_t bitIdx = dotX % 8;
	rowOfBits[byteIdx] |= (1 << (7 - bitIdx));
      }
    }
  }

  carriageDot += charWidth;
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

  if (proportionalMode) {
    ret = Fx80Font[c * 19]; // first byte of font entry is proportional width
    if (ret == 0) ret = 12;
  } else if (fontMode & FM_Elite) {
    ret = 10;
  } else if (fontMode & FM_Compressed) {
    ret = 7;
  } else {
    ret = 12;
  }

  if (fontMode & FM_Expanded)
    ret *= 2;

  return ret;
}

void Fx80::lineFeed()
{
  emitLine();
  g_printer->moveDownPixels(twoSixteenthsLineSpacing / 3);
  clearLine();
  carriageDot = leftMarginDot;
  currentLine++;
  if (currentLine >= formLengthLines)
    currentLine = 0;
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

