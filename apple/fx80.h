#ifndef __FX80_H
#define __FX80_H

#include <stdint.h>

/* maximum width, in dots, that we're supporting. The FX80 supported
 * up to 1920 ("quadruple density") -- which I'm not, b/c that's
 * overkill for what I'm doing at the moment. I'm supporting "double density"
 * (960 dots per 8-inch line).
 *
 * My general strategy here is to fill up a line with bits until the
 * printer thinks it needs to move to the next line; and then do
 * something with the bits (send them to a printer, a screen, a file,
 * whatever). This gets both graphics and text modes in one swell foop.
 *
 * There is the troublesome "9-pin graphics mode" where each column of
 * bits is > 1 byte of data; for that, we keep an extra "rowOfPin9"
 * bits, which is just a bit stream across the width. It would be
 * easier to make rowOfBits be uint16_t but it would consume more RAM,
 * and I'm trying to minimize that as I'm down to about 13k of free
 * space in the Teensy!
 */

#define FX80_MAXWIDTH 960

#ifdef TEENSYDUINO
class TeensyPrinter;
#else
class OpenCVPrinter;
#endif

enum Charset {
  CS_USA     = 0,
  CS_France  = 1,
  CS_Germany = 2,
  CS_UK      = 3,
  CS_Denmark = 4,
  CS_Sweden  = 5,
  CS_Italy   = 6,
  CS_Spain   = 7,
  CS_Japan   = 8
};

class Fx80 {
 public:
  Fx80();
  ~Fx80();

  void Reset();

  void input(uint8_t c);

  void update();

 private:
  void lineFeed();
  void clearLine();

  void addCharacter(uint8_t c);

  void handleEscape(uint8_t c);
  void handleActiveEscapeMode(uint8_t c);
  void emitLine();

 protected:
  bool escapeMode;
  bool proportionalMode;

  uint16_t carriageDot; // what dot-column we are at

  // Line spacing. 1/216th of an inch is 1/3 of a dot, which is the minimum 
  // supported by the FX-80. This is usually set in terms of 72nds, and 
  // it's unlikely that I'll render anything at 1/216, but might as
  // well interpret it the way the printer understands it.
  // (That means 8/72, which is a setting of 24 here, is 8 dots tall.)
  uint16_t twoSixteenthsLineSpacing;

  uint16_t graphicsWidth;
  bool ninePinGraphics;
  uint8_t escapeModeActive;
  int32_t escapeModeExpectingBytes;
  uint8_t escapeModeLengthByteCount;
  uint16_t escapeModeLength;

  // 9 pixel-rows of (FX80_MAXWIDTH) bits (stuffed in 8-bit bytes)
  uint8_t rowOfBits[(FX80_MAXWIDTH/8)*9];

  Charset charsetEnabled;
  bool italicsMode;
};

#endif
