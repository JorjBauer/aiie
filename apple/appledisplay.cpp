#include <ctype.h> // isgraph
#include <stdlib.h> // calloc
#include <string.h> // strlen
#include "appledisplay.h"
#include "applemmu.h" // for switch constants

#include "font.h"

/* Fourpossible Hi-Res color-drawing modes..
   MONOCHROME: show all the pixels, but only in green;
   BLACKANDWHITE: monochrome, but use B&W instead of B&G;
   NTSCLIKE: reduce the resolution to 140 pixels wide, similar to how an NTSC monitor would blend it
   PERFECTCOLOR: as the Apple RGB monitor shows it, which means you can't have a solid color field
*/



#define extendDirtyRect(x,y) {    \
    if (dirtyRect.left > x) {     \
      dirtyRect.left = x;         \
    }                             \
    if (dirtyRect.right < x) {    \
      dirtyRect.right = x;        \
    }                             \
    if (dirtyRect.top > y) {      \
      dirtyRect.top = y;          \
    }                             \
    if (dirtyRect.bottom < y) {   \
      dirtyRect.bottom = y;       \
    }                             \
}

#include "globals.h"

AppleDisplay::AppleDisplay(uint8_t *vb) : VMDisplay(vb)
{
  this->switches = NULL;
  this->dirty = true;
  this->dirtyRect.left = this->dirtyRect.top = 0;
  this->dirtyRect.right = 279;
  this->dirtyRect.bottom = 191;

  textColor = g_displayType == m_monochrome?c_green:c_white;
}

AppleDisplay::~AppleDisplay()
{
}

bool AppleDisplay::deinterlaceAddress(uint16_t address, uint8_t *row, uint8_t *col)
{
  if (address >= 0x800 && address < 0xC00) {
    address -= 0x400;
  }

  uint8_t block = (address >> 7) - 0x08;
  uint8_t blockOffset = (address & 0x00FF) - ((block & 0x01) ? 0x80 : 0x00);
  if (blockOffset < 0x28) {
    *row = block;
    *col = blockOffset;
  } else if (blockOffset < 0x50) {
    *row = block + 8;
    *col = blockOffset - 0x28;
  } else {
    *row = block + 16;
    *col = blockOffset - 0x50;
  }

  return true;
}

// calculate x/y pixel offsets from a memory address.
// Note that this is the first of 7 pixels that will be affected by this write;
// we'll need to update all 7 starting at this x.
bool AppleDisplay::deinterlaceHiresAddress(uint16_t address, uint8_t *row, uint16_t *col)
{
  // each row is 40 bytes, for 7 pixels each, totalling 128
  // pixels wide.
  // They are grouped in to 3 "runs" of 40-byte blocks, where 
  // each group is 64 lines after the one before.

  // Then repeat at +400, +800, +c00, +1000, +1400, +1800, +1c00 for
  // the other 7 pixels tall.

  // Repeat the whole shebang at +0x80, +0x100, +0x180, ... to +280
  // for each 8-pixel tall group.

  // There are 8 bytes at the end of each run that we ignore. Skip them.
  if ((address & 0x07f) >= 0x78 &&
      (address & 0x7f) <= 0x7f) {
    *row = 255;
    *col = 65535;
    return false;
  }

  *row = ((address & 0x380) >> 4) +
    ((address & 0x1c00)>>10) + 
    64 * ((address & 0x7f) / 40);

  *col = ((address & 0x7f) % 40) * 7;

  return true;
}


void AppleDisplay::writeLores(uint16_t address, uint8_t v)
{
  if (address >= 0x800 && !((*switches) & S_80COL)) {
    if (!((*switches) & S_PAGE2)) {
      // writing to page2 text/lores, but that's not displayed right now, so nothing to do
      return;
    }
  } 

  uint8_t row, col;
  deinterlaceAddress(address, &row, &col);

  if (col <= 39) {
    if ((*switches) & S_TEXT ||
	(((*switches) & S_MIXED) && row >= 20)) {
      if ((*switches) & S_80COL) {
	Draw80CharacterAt(v, col, row, ((*switches) & S_PAGE2) ? 0 : 1);
      } else {
	DrawCharacterAt(v, col, row);
      }
    }
  }

  if (!((*switches) & S_TEXT) &&
      !((*switches) & S_HIRES)) {
    if (col <= 39) {
      if (row < 20 ||
	  (! ((*switches) & S_MIXED))) {
	// low-res graphics mode. Each character has two 4-bit 
	// values in it: first half is the "top" pixel, and second "bottom".
	if (((*switches) & S_80COL) && ((*switches) & S_DHIRES)) {
	  Draw80LoresPixelAt(v, col, row, ((*switches) & S_PAGE2) ? 0 : 1);
	} else {
	  DrawLoresPixelAt(v, col, row);
	}
      }
    }
  }
  dirty = true;
}

void AppleDisplay::writeHires(uint16_t address, uint8_t v)
{
  if ((*switches) & S_HIRES) {
    if ((*switches) & S_DHIRES) {
      // Double hires: make sure we're drawing to the page that's visible.
      // If S_80STORE is on, then it's $2000 (and S_PAGE2 controls main/aux bank r/w).
      // If S_80STORE is off, then it's $4000 (and RAMRD/RAMWT are used).

      if (((*switches) & S_80STORE) && address >= 0x4000 && address <= 0x5FFF)
	return;
      if ( !((*switches) & S_80STORE) && address >= 0x2000 && address <= 0x3FFF)
	return;

      Draw14DoubleHiresPixelsAt(address);
      dirty = true;

      return;
    }

    // If it's a write to single hires but the 80store byte is on, then what do we do?
    if ((*switches) & S_80STORE) {
      // FIXME
      return;
    }

    // Make sure we're writing to the page that's visible before we draw
    if (address >= 0x2000 && address <= 0x3FFF && ((*switches) & S_PAGE2)) 
      return;
    if (address >= 0x4000 && address <= 0x5FFF && !((*switches) & S_PAGE2)) 
      return;
    
    Draw14HiresPixelsAt(address & 0xFFFE);
    dirty = true;
  }
}

// return a pointer to the right glyph, and set *invert appropriately
const unsigned char *AppleDisplay::xlateChar(uint8_t c, bool *invert)
{
  if (c <= 0x3F) {
    // 0-3f: inverted @ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_ !"#$%&'()*+,-./0123456789:;<=>?
    // (same w/o mousetext, actually)
    *invert = true;
    return &ucase_glyphs[c * 8];
  } else if (c <= 0x5F) {
    // 40-5f: normal mousetext
    // (these are flashing @ABCDEFG..[\]^_ when not in mousetext mode)
    if ((*switches) & S_ALTCH) {
      *invert = false;
      return &mousetext_glyphs[(c - 0x40) * 8];
    } else {
      *invert = true;
      return &ucase_glyphs[(c - 0x40) * 8];
    }
  } else if (c <= 0x7F) {
    // 60-7f: inverted   `abcdefghijklmnopqrstuvwxyz{|}~*
    // (these are flashing (sp)!"#$%...<=>? when not in mousetext)
    if ((*switches) & S_ALTCH) {
      *invert = true;
      return &lcase_glyphs[(c - 0x60) * 8];
    } else {
      *invert = true;
      return &ucase_glyphs[((c-0x60) + 0x20) * 8];
    }
  } else if (c <= 0xBF) {
    // 80-BF: normal @ABCD... <=>? in both character sets
    *invert = false;
    return &ucase_glyphs[(c - 0x80) * 8];
  } else if (c <= 0xDF) {
    // C0-DF: normal @ABCD...Z[\]^_ in both character sets
    *invert = false;
    return &ucase_glyphs[(c - 0xC0) * 8];
  } else {
    // E0-  : normal `abcdef... in both character sets
    *invert = false;
    return &lcase_glyphs[(c - 0xE0) * 8];
  }

  /* NOTREACHED */
}
  
void AppleDisplay::Draw80CharacterAt(uint8_t c, uint8_t x, uint8_t y, uint8_t offset)
{
  bool invert;
  const uint8_t *cptr = xlateChar(c, &invert);

  // Even characters are in bank 0 ram. Odd characters are in bank 1 ram.
  // Technically, this would need 560 columns to work correctly - and I 
  // don't have that, so it's going to be a bit wonky. 
  // 
  // First pass: draw two pixels on top of each other, clearing only
  // if both are black. This would be blocky but probably passable if
  // it weren't for the fact that characters are 7 pixels wide, so we
  // wind up sharing a half-pixel between two characters. So we'll
  // render these as 3-pixel-wide characters and make sure they always
  // even-align the drawing on the left side so we don't overwrite
  // every other one on the left or right side.

  for (uint8_t y2 = 0; y2<8; y2++) {
    uint8_t d = *(cptr + y2);
    for (uint8_t x2 = 0; x2 <= 7; x2+=2) {
      uint16_t basex = ((x * 2 + offset) * 7) & 0xFFFE; // even aligned
      bool pixelOn = ( (d & (1<<x2)) | (d & (1<<(x2+1))) );
      if (pixelOn) {
	uint8_t val = (invert ? c_black : textColor);
	drawPixel(val, (basex+x2)/2, y*8+y2);
      } else {
	uint8_t val = (invert ? textColor : c_black);
	drawPixel(val, (basex+x2)/2, y*8+y2);
      }
    }
  }
}

void AppleDisplay::DrawCharacterAt(uint8_t c, uint8_t x, uint8_t y)
{
  bool invert;
  const uint8_t *cptr = xlateChar(c, &invert);

  for (uint8_t y2 = 0; y2<8; y2++) {
    uint8_t d = *(cptr + y2);
    for (uint8_t x2 = 0; x2 < 7; x2++) {
      if (d & 1) {
	uint8_t val = (invert ? c_black : textColor);
	drawPixel(val, x*7+x2, y*8+y2);
      } else {
	uint8_t val = (invert ? textColor : c_black);
	drawPixel(val, x*7+x2, y*8+y2);
      }
      d >>= 1;
    }
  }
}

void AppleDisplay::Draw14DoubleHiresPixelsAt(uint16_t addr)
{
  // We will consult 4 bytes (2 in main, 2 in aux) for any single-byte
  // write. Align to the first byte in that series based on what
  // address we were given...
  addr &= ~0x01;

  // Figure out the position of that address on the "normal" hires screen
  uint8_t row;
  uint16_t col;
  deinterlaceHiresAddress(addr, &row, &col);
  if (row >= 160 && 
      ((*switches) & S_MIXED)) {
    // displaying text, so don't have to draw this line
    return;
  }

  // Make sure it's a valid graphics area, not a dead hole
  if (col <= 280 && row <= 192) {
    // Grab the 4 bytes we care about
    uint8_t b1A = mmu->readDirect(addr, 0);
    uint8_t b2A = mmu->readDirect(addr+1, 0);
    uint8_t b1B = mmu->readDirect(addr, 1);
    uint8_t b2B = mmu->readDirect(addr+1, 1);

    // Construct the 28 bit wide bitstream, like we do for the simpler 14 Hires pixel draw
    uint32_t bitTrain = b2A & 0x7F;
    bitTrain <<= 7;
    bitTrain |= (b2B & 0x7F);
    bitTrain <<= 7;
    bitTrain |= (b1A & 0x7F);
    bitTrain <<= 7;
    bitTrain |= (b1B & 0x7F);
    
    // Now we pop groups of 4 bits off the bottom and draw our
    // NTSC-style-only color.  The display for this project only has
    // 320 columns, so it's silly to try to do 560 columns of
    // monochrome; and likewise, we can't do "perfect" representation
    // of shifted color pixels. So NTSC it is, and we'll draw two screen 
    // pixels for every color.

    for (int8_t xoff = 0; xoff < 14; xoff += 2) {
      drawPixel(bitTrain & 0x0F, col+xoff, row);
      drawPixel(bitTrain & 0x0F, col+xoff+1, row);

      bitTrain >>= 4;
    }
  }
}


// Whenever we change a byte, it's possible that it will have an affect on the byte next to it - 
// because between two bytes there is a shared bit.
// FIXME: what happens when the high bit of the left doesn't match the right? Which high bit does 
// the overlap bit get?
void AppleDisplay::Draw14HiresPixelsAt(uint16_t addr)
{
  uint8_t row;
  uint16_t col;

  deinterlaceHiresAddress(addr, &row, &col);
  if (row >= 160 && 
      ((*switches) & S_MIXED)) {
    return;
  }

  if (col <= 280 && row <= 192) {
    /*
      The high bit only selects the color palette.

      There are only really two bits here, and they can be one of six colors.
      
      color    highbit even    odd    restriction
      black       x      0x80,0x00
      green       0    0x2A    0x55    odd only
      violet      0    0x55    0x2A    even only
      white       x      0xFF,0x7F    
      orange      1    0xAA    0xD5    odd only
      blue        1    0xD5    0xAA    even only

      in other words, we can look at the pixels in pairs and we get

      00 black
      01 green/orange
      10 violet/blue
      11 white

      When the horizontal byte number is even, we ignore the last
      bit. When the horizontal byte number is odd, we use that dropped
      bit.

      So each even byte turns in to 3 bits; and each odd byte turns in
      to 4. Our effective output is therefore 140 pixels (half the 
      actual B&W resolution).

      (Note that I swap 0x02 and 0x01 below, because we're running the
      bit train backward, so the bits are reversed.)
     */

    uint8_t b1 = mmu->read(addr);
    uint8_t b2 = mmu->read(addr+1);

    // Used for color modes...
    bool highBitOne = (b1 & 0x80);
    bool highBitTwo = (b2 & 0x80);

    uint16_t bitTrain = (b1 & 0x7F) | ((b2 & 0x7F) << 7);

    for (int8_t xoff = 0; xoff < 14; xoff += 2) {

      if (g_displayType == m_monochrome) {
	draw2Pixels(((bitTrain & 0x01 ? c_green : c_black) << 4) |
		    (bitTrain & 0x02 ? c_green : c_black),
		    col+xoff, row);
      } else if (g_displayType == m_blackAndWhite) {
	draw2Pixels(((bitTrain & 0x01 ? c_white : c_black) << 4) |
		    (bitTrain & 0x02 ? c_white : c_black),
		    col+xoff, row);
      } else if (g_displayType == m_ntsclike) {
	// Use the NTSC-like color mode, where we're only 140 pixels wide.
	
	bool highBitSet = (xoff >= 7 ? highBitTwo : highBitOne);
	uint8_t color;
	switch (bitTrain & 0x03) {
	case 0x00:
	  color = c_black;
	  break;
	case 0x02:
	  color = (highBitSet ? c_orange : c_green);
	  break;
	case 0x01:
	  color = (highBitSet ? c_medblue : c_purple);
	  break;
	case 0x03:
	  color = c_white;
	  break;
	}
	
	draw2Pixels( (color << 4) | color, col+xoff, row );
      } else {
	// Use the "perfect" color mode, like the Apple RGB monitor showed.
	bool highBitSet = (xoff >= 7 ? highBitTwo : highBitOne);
	uint8_t color;
	switch (bitTrain & 0x03) {
	case 0x00:
	  color = c_black;
	  break;
	case 0x02:
	  color = (highBitSet ? c_orange : c_green);
	  break;
	case 0x01:
	  color = (highBitSet ? c_medblue : c_purple);
	  break;
	case 0x03:
	  color = c_white;
	  break;
	}
	
	uint16_t twoColors;
	
	if (color == c_black || color == c_white || bitTrain & 0x01) {
	  twoColors = color;
	} else {
	  twoColors = c_black;
	}
	twoColors <<= 4;
	
	if (color == c_black || color == c_white || bitTrain & 0x02) {
	  twoColors |= color;
	} else {
	  twoColors |= c_black;
	}
	draw2Pixels(twoColors, col+xoff, row);
      }
      bitTrain >>= 2;
    }
  }
}

void AppleDisplay::modeChange()
{
  if ((*switches) & S_TEXT) {
    if ((*switches) & S_80COL) {
      for (uint16_t addr = 0x400; addr <= 0x400 + 0x3FF; addr++) {
	uint8_t row, col;
	deinterlaceAddress(addr, &row, &col);
	if (col <= 39 && row <= 23) {
	  Draw80CharacterAt(mmu->readDirect(addr, 0), col, row, 1);
	  Draw80CharacterAt(mmu->readDirect(addr, 1), col, row, 0);
	}
      }
    } else {
      uint16_t start = ((*switches) & S_PAGE2) ? 0x800 : 0x400;
      for (uint16_t addr = start; addr <= start + 0x3FF; addr++) {
	uint8_t row, col;
	deinterlaceAddress(addr, &row, &col);
	if (col <= 39 && row <= 23) {
	  DrawCharacterAt(mmu->read(addr), col, row);
	}
      }
    }

    return;
  }

  // Not text mode - what mode are we in?
  if ((*switches) & S_HIRES) {
    // Hires
    // FIXME: make this draw a row efficiently
    // FIXME: can make more efficient by checking S_MIXED for lower bound
    uint16_t start = ((*switches) & S_PAGE2) ? 0x4000 : 0x2000;
    if ((*switches) & S_80STORE) {
      // Apple IIe, technical nodes #3: 80STORE must be OFF to display Page 2
      start = 0x2000;
    }

    for (uint16_t addr = start; addr <= start + 0x1FFF; addr+=2) {
      if ((*switches) & S_DHIRES) {
	Draw14DoubleHiresPixelsAt(addr);
      } else {
	Draw14HiresPixelsAt(addr);
      }
    }
  } else {
    // Lores
    // FIXME: can make more efficient by checking S_MIXED for lower bound

    if (((*switches) & S_80COL) && ((*switches) & S_DHIRES)) {
      for (uint16_t addr = 0x400; addr <= 0x400 + 0x3ff; addr++) {
	uint8_t row, col;
	deinterlaceAddress(addr, &row, &col);
	if (col <= 39 && row <= 23) {
	  Draw80LoresPixelAt(mmu->readDirect(addr, 0), col, row, 1);
	  Draw80LoresPixelAt(mmu->readDirect(addr, 1), col, row, 0);
	}
      }
    } else {
      uint16_t start = ((*switches) & S_PAGE2) ? 0x800 : 0x400;
      for (uint16_t addr = start; addr <= start + 0x3FF; addr++) {
	uint8_t row, col;
	deinterlaceAddress(addr, &row, &col);
	if (col <= 39 && row <= 23) {
	  DrawLoresPixelAt(mmu->read(addr), col, row);
	}
      }
    }
  }

  if ((*switches) & S_MIXED) {
    // Text at the bottom of the screen...
    // FIXME: deal with 80-char properly
    uint16_t start = ((*switches) & S_PAGE2) ? 0x800 : 0x400;
    for (uint16_t addr = start; addr <= start + 0x3FF; addr++) {
      uint8_t row, col;
      deinterlaceAddress(addr, &row, &col);
      if (col <= 39 && row >= 20 && row <= 23) {
	if ((*switches) & S_80COL) {
	  Draw80CharacterAt(mmu->read(addr), col, row, ((*switches) & S_PAGE2) ? 0 : 1);
	} else {
	  DrawCharacterAt(mmu->read(addr), col, row);
	}
      }
    }
  }
}

void AppleDisplay::Draw80LoresPixelAt(uint8_t c, uint8_t x, uint8_t y, uint8_t offset)
{
  // Just like 80-column text, this has a minor problem; we're talimg
  // a 7-pixel-wide space and dividing it in half. Here I'm drawing
  // every other column 1 pixel narrower (the ">= offset" in the for
  // loop condition).
  //
  // Make those ">= 0" and change the "*7" to "*8" and you've got
  // 320-pixel-wide slightly distorted but cleaner double-lores...

  if (!offset) {
    // The colors in every other column are swizzled. Un-swizzle.
    c = ((c & 0x77) << 1) | ((c & 0x88) >> 3);
  }
  uint8_t pixel = c & 0x0F;
  for (uint8_t y2 = 0; y2<4; y2++) {
    for (int8_t x2 = 3; x2>=offset; x2--) {
      drawPixel(pixel, x*7+x2+offset*3, y*8+y2);
    }
  }

  pixel = (c >> 4);
  for (uint8_t y2 = 4; y2<8; y2++) {
    for (int8_t x2 = 3; x2>=offset; x2--) {
      drawPixel(pixel, x*7+x2+offset*3, y*8+y2);
    }
  }
}

// col, row are still character-like positions, as in DrawCharacterAt.
// Each holds two pixels (one on top of the other).
void AppleDisplay::DrawLoresPixelAt(uint8_t c, uint8_t x, uint8_t y)
{
  uint8_t pixel = c & 0x0F;
  for (uint8_t y2 = 0; y2<4; y2++) {
    for (int8_t x2 = 6; x2>=0; x2--) {
      drawPixel(pixel, x*7+x2, y*8+y2);
    }
  }

  pixel = (c >> 4);
  for (uint8_t y2 = 4; y2<8; y2++) {
    for (int8_t x2 = 6; x2>=0; x2--) {
      drawPixel(pixel, x*7+x2, y*8+y2);
    }
  }
}

void AppleDisplay::draw2Pixels(uint16_t two4bitColors, uint16_t x, uint8_t y)
{
  if (!dirty) {
    dirty = true;
    dirtyRect.left = x; dirtyRect.right = x + 1;
    dirtyRect.top = dirtyRect.bottom = y;
  } else {
    extendDirtyRect(x, y);
    extendDirtyRect(x+1, y);
  }

  videoBuffer[(y * DISPLAYWIDTH + x) /2] = two4bitColors;
}

inline void AppleDisplay::drawPixel(uint8_t color4bit, uint16_t x, uint8_t y)
{
  if (!dirty) {
    dirty = true;
    dirtyRect.left = dirtyRect.right = x;
    dirtyRect.top = dirtyRect.bottom = y;
  } else {
    extendDirtyRect(x, y);
  }

  uint16_t idx = (y * DISPLAYWIDTH + x) / 2;
  if (x & 1) {
    videoBuffer[idx] = (videoBuffer[idx] & 0xF0) | color4bit;
  } else {
    videoBuffer[idx] = (videoBuffer[idx] & 0x0F) | (color4bit << 4);
  }
}

void AppleDisplay::setSwitches(uint16_t *switches)
{
  dirty = true;
  dirtyRect.left = 0;
  dirtyRect.right = 279;
  dirtyRect.top = 0;
  dirtyRect.bottom = 191;

  this->switches = switches;
}

AiieRect AppleDisplay::getDirtyRect()
{
  return dirtyRect;
}

bool AppleDisplay::needsRedraw()
{
  return dirty;
}

void AppleDisplay::didRedraw()
{
  dirty = false;
}

void AppleDisplay::displayTypeChanged()
{
  textColor = g_displayType == m_monochrome?c_green:c_white;
}
