#include <ctype.h> // isgraph
#include <string.h> // strlen
#include "appledisplay.h"
#include "applemmu.h" // for switch constants

#include "font.h"

/* Four possible Hi-Res color-drawing modes..
   MONOCHROME: show all the pixels, but only in green;
   BLACKANDWHITE: monochrome, but use B&W instead of B&G;
   NTSCLIKE: reduce the resolution to 140 pixels wide, similar to how an NTSC monitor would blend it
   PERFECTCOLOR: as the Apple RGB monitor shows it, which means you can't have a solid color field

   The only two we have to worry about here are NTSCLIKE and PERFECTCOLOR. The mono and B&W modes 
   are handled in the individual display drivers, where colors are changed to one or the other.
   The NTSCLIKE and PERFECTCOLOR modes change which actual pixels are set on or off, though, 
   and that's a quirk specific to the Apple 2...
*/

#define extendDirtyRect(x,y) {      \
    if (!dirty) {                   \
      dirtyRect.left = x;           \
      dirtyRect.right = x;          \
      dirtyRect.top = y;            \
      dirtyRect.bottom = y;         \
      dirty = true;                 \
    } else {                        \
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
    }                               \
}

#define drawApplePixel(c,x,y) { g_display->cacheDoubleWidePixel(x,y,c); }

#define DrawLoresPixelAt(c, x, y) {     \
  uint8_t pixel = c & 0x0F;             \
  for (uint8_t y2 = 0; y2<4; y2++) {    \
    for (int8_t x2 = 6; x2>=0; x2--) {  \
      drawApplePixel(pixel, x*7+x2, y*8+y2); \
    }                                   \
  }                                     \
  pixel = (c >> 4);                     \
  for (uint8_t y2 = 4; y2<8; y2++) {    \
    for (int8_t x2 = 6; x2>=0; x2--) {  \
      drawApplePixel(pixel, x*7+x2, y*8+y2); \
    }                                   \
  }                                     \
}

#include "globals.h"

AppleDisplay::AppleDisplay() : VMDisplay()
{
  this->switches = NULL;

  modeChange();
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
  
inline void AppleDisplay::Draw14DoubleHiresPixelsAt(uint16_t addr)
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
#define UNSWIZ(x) ((((x)&0x77)<<1) | (((x)&0x88)>>3))
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
    // Now we pop groups of 4 bits off the bottom and draw.

    for (int8_t xoff = 0; xoff < 14; xoff += 2) {
      uint8_t color = bitTrain & 0x0F;
      color = UNSWIZ(color); //((color & 7) << 1) | ((color & 8) >> 3); // un-swizzle the bits
      if (g_displayType == m_ntsclike) {
	// NTSC-like color - use drawApplePixel to show the messy NTSC color bleeds.
	// This draws two doubled pixels with greater color, but lower pixel, resolution.
	drawApplePixel(color, col+xoff, row);
	drawApplePixel(color, col+xoff+1,row);
      } else {
	// Perfect color, B&W, monochrome. Draw an exact version of the pixels, and let 
	// the physical display figure out if they need to be reduced to B&W or not
	// (for the most part - the m_blackAndWhite piece here allows full-res displays
	// to give the crispest resolution.)

	if (g_displayType == m_blackAndWhite) { color = c_white; } 

	g_display->cachePixel((col*2)+(xoff*2), row, 
			      ((bitTrain & 0x01) ? color : c_black));
	
	g_display->cachePixel((col*2)+(xoff*2)+1, row, 
			      ((bitTrain & 0x02) ? color : c_black));
	
	g_display->cachePixel((col*2)+(xoff*2)+2, row, 
			      ((bitTrain & 0x04 )? color : c_black));

	g_display->cachePixel((col*2)+(xoff*2)+3, row, 
			      ((bitTrain & 0x08 ) ? color : c_black));
      }

      bitTrain >>= 4;
    } // for

  }
}


// Whenever we change a byte, it's possible that it will have an affect on the byte next to it - 
// because between two bytes there is a shared bit.
// FIXME: what happens when the high bit of the left doesn't match the right? Which high bit does 
// the overlap bit get?
inline void AppleDisplay::Draw14HiresPixelsAt(uint16_t addr)
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

      So each even byte turns in to 3 bits; and each odd byte turns in
      to 4. Our effective output is therefore 140 pixels (half the 
      actual B&W resolution).

      In practice, it's not that way, though: white isn't decided by a
      simple 11, because, if you consider its righthand neighbor bit,
      it could be 011 which would also be white. So this bit is
      influenced by the bit on the left, and also influences the bit
      on the right. Which means we have to keep a rolling bit train
      and watch for edge conditions when we're painting this pixel.
     */

    uint32_t b1 = mmu->readDirect(addr, 0);
    uint32_t b2 = mmu->readDirect(addr+1, 0);

    // Get the neighboring pixel states
    // FIXME I think there's a minor error here in b0/b3's condition checking - review carefully
    uint32_t b0 = ((col < 14) ? 0 : mmu->readDirect(addr-1, 0));
    uint32_t b3 = ((col >= (280-14)) ? 0 : mmu->readDirect(addr+2, 0));

    // Used for color modes.
    bool highBitOne = (b1 & 0x80);
    bool highBitTwo = (b2 & 0x80);

    uint32_t bitTrain = (b0 & 0x7F) | ((b1 & 0x7F) << 7) |
      ((b2 & 0x7F) << 14) | ((b3 & 0x7F) << 21);
    bool odd = (col % 2); // we need to know odd/even column so we can tell color
    
    uint8_t color = c_black;
    for (int8_t xoff = 0; xoff < 14; xoff++) {
      
      bool highBitSet = (xoff >= 7 ? highBitTwo : highBitOne);
      
      // Check neighbor pixels to see what color needs to be onscreen
      uint32_t mask  =  0x01C0;
      uint32_t neighborMask = 0x140;
      uint32_t ourMask = 0x080;

      // Now we need to talk about what video mode we're representing.
      // If we're doing "true" m_perfectcolor, then it's simple:
      // either the pixel is on or off. If it's on, then the color is
      // either the color we asked for or it's white (if we have a
      // neighbor that's on).
      // 
      // If we're doing NTSC, then we draw even when this pixel is
      // off, if both of our neighboring pixels are on (and we use
      // their color, or white).
      //
      // If we're doing black and white or monochrome, then we follow
      // rules for perfectcolor - except the color we draw is either
      // black or (white/green, depending on mode).

      // If our pixel is on, then adopt this pixel's color
      if (bitTrain & ourMask) {
	if (g_displayType == m_monochrome || g_displayType == m_blackAndWhite) {
	  // The actual display will turn white into green if necessary for m_monochrome
	  color = c_white;
	} else {
	  color = odd ? (highBitSet ? c_orange : c_green) : (highBitSet ? c_medblue : c_purple);
	}
      } else if (g_displayType == m_ntsclike) {
	// If our bit is off, we might still have to display our
	// neighbor's color - if the bit to our right is on, and we're
	// the first bit in the train. So preset based on the
	// alternate color.
	color = (!odd) ? (highBitSet ? c_orange : c_green) : (highBitSet ? c_medblue : c_purple);
      }

      if ((bitTrain & mask) == ourMask) {
	// In all color modes, if our pixel is on but our neighbors
	// are off, then we draw our color.
	drawApplePixel(color, col+xoff, row);
      } else if ((bitTrain & mask) == neighborMask) {
	if (g_displayType == m_ntsclike) {
	  // If it's NTSCLIKE and our neighbors are on, then we are also on
	  drawApplePixel(color, col+xoff, row);
	} else {
	  // For all others: if we're off, then draw black
	  drawApplePixel(c_black, col+xoff, row);
	}
      } else {
	// otherwise it's black-or-white
	if (bitTrain & ourMask) {
	  // It's either 110 or 011, either way is white
	  drawApplePixel(c_white, col+xoff, row);
	} else {
	  // Must be 100, 001, or 000 - in all cases it's black
	  drawApplePixel(c_black, col+xoff, row);
	}
      }
      
      bitTrain >>= 1;
      odd = !odd;
    }
  }
}

void AppleDisplay::redraw80ColumnText(uint8_t startingY)
{
  uint8_t row, col;
  col = -1; // will force us to deinterlaceAddress()
  bool invert;
  const uint8_t *cptr;

  // FIXME: is there ever a case for 0x800, like in redraw40ColumnText?
  uint16_t start = 0x400;

  // Every time through this loop, we increment the column. That's going to be correct most of the time.
  // Sometimes we'll get beyond the end (40 columns), and wind up on another line 8 rows down.
  // Sometimes we'll get beyond the end, and we'll wind up in unused RAM.
  // But this is an optimization (for speed) over just calling DrawCharacter() for every one.
  for (uint16_t addr = start; addr <= start + 0x3FF; addr++,col++) {
    if (col > 39 || row > 23) {
      // Could be blanking space; we'll try to re-confirm...
      deinterlaceAddress(addr, &row, &col);      
    }

    // Only draw onscreen locations
    if (row >= startingY && col <= 39 && row <= 23) {
      // Even characters are in bank 0 ram. Odd characters are in bank
      // 1 ram. Draw to the physical display and let it figure out 
      // whether or not there are enough physical pixels to display 
      // the 560 columns we'd need for this.

      // Draw the first of two characters
      cptr = xlateChar(mmu->readDirect(addr, 1), &invert);
      for (uint8_t y2 = 0; y2<8; y2++) {
	uint8_t d = *(cptr + y2);
	for (uint8_t x2 = 0; x2 < 7; x2++) {
	  uint16_t basex = (col*2)*7;
	  bool pixelOn = (d & (1<<x2));
	  if (pixelOn) {
	    uint8_t val = (invert ? c_black : c_white);
	    g_display->cachePixel(basex + x2, row*8+y2, val);
	  } else {
	    uint8_t val = (invert ? c_white : c_black);
	    g_display->cachePixel(basex + x2, row*8+y2, val);
	  }
	}
      }

      // Draw the second of two characters
      cptr = xlateChar(mmu->readDirect(addr, 0), &invert);
      for (uint8_t y2 = 0; y2<8; y2++) {
	uint8_t d = *(cptr + y2);
	for (uint8_t x2 = 0; x2 < 7; x2++) {
	  uint16_t basex = (col*2+1)*7;
	  bool pixelOn = (d & (1<<x2));
	  if (pixelOn) {
	    uint8_t val = (invert ? c_black : c_white);
	    g_display->cachePixel(basex + x2, row*8+y2, val);
	  } else {
	    uint8_t val = (invert ? c_white : c_black);
	    g_display->cachePixel(basex + x2, row*8+y2, val);
	  }
	}
      }
    }
  }
}

void AppleDisplay::redraw40ColumnText(uint8_t startingY)
{
  bool invert;

  uint16_t start = ((*switches) & S_PAGE2) ? 0x800 : 0x400;
  uint8_t row, col;
  col = -1; // will force us to deinterlaceAddress()
  
  // Every time through this loop, we increment the column. That's going to be correct most of the time.
  // Sometimes we'll get beyond the end (40 columns), and wind up on another line 8 rows down.
  // Sometimes we'll get beyond the end, and we'll wind up in unused RAM.
  // But this is an optimization (for speed) over just calling DrawCharacter() for every one.
  for (uint16_t addr = start; addr <= start + 0x3FF; addr++,col++) {
    if (col > 39 || row > 23) {
      // Could be blanking space; we'll try to re-confirm...
      deinterlaceAddress(addr, &row, &col);      
    }

    // Only draw onscreen locations
    if (row >= startingY && col <= 39 && row <= 23) {
      const uint8_t *cptr = xlateChar(mmu->readDirect(addr, 0), &invert);
      for (uint8_t y2 = 0; y2<8; y2++) {
	uint8_t d = *(cptr + y2);
	for (uint8_t x2 = 0; x2 < 7; x2++) {
	  if (d & 1) {
	    uint8_t val = (invert ? c_black : c_white);
	    drawApplePixel(val, col*7+x2, row*8+y2);
	  } else {
	    uint8_t val = (invert ? c_white : c_black);
	    drawApplePixel(val, col*7+x2, row*8+y2);
	  }
	  d >>= 1;
	}
      }
    }
  }
}

void AppleDisplay::redrawHires()
{
  uint16_t start = ((*switches) & S_PAGE2) ? 0x4000 : 0x2000;
  if ((*switches) & S_80STORE) {
    // Apple IIe, technical nodes #3: 80STORE must be OFF to display Page 2
    start = 0x2000;
  }

  // S_MIXED is checked inside Draw14HiresPixelsAt and
  // Draw14DoubleHiresPixelsAt, so no need to check it here
  for (uint16_t addr = start; addr <= start + 0x1FFF; addr+=2) {
    if ((*switches) & S_DHIRES) {
      // FIXME: inline & optimize
      Draw14DoubleHiresPixelsAt(addr);
    } else {
      // FIXME: inline & optimize
      Draw14HiresPixelsAt(addr);
    }
  }
}

void AppleDisplay::redrawLores()
{
  if (((*switches) & S_80COL) && ((*switches) & S_DHIRES)) {
    for (uint16_t addr = 0x400; addr <= 0x400 + 0x3ff; addr++) {
      uint8_t row, col;
      deinterlaceAddress(addr, &row, &col);
      if (col <= 39 && row <= 23) {
        if (((*switches) & S_MIXED) && row >= 20) { // ***@@@ is 20 right?
          // Don't draw this row, we're in MIXED mode
        } else {
          Draw80LoresPixelAt(mmu->readDirect(addr, 0), col, row, 1);
          Draw80LoresPixelAt(mmu->readDirect(addr, 1), col, row, 0);
        }
      }
    }
  } else {
    uint16_t start = ((*switches) & S_PAGE2) ? 0x800 : 0x400;
    for (uint16_t addr = start; addr <= start + 0x3FF; addr++) {
      uint8_t row, col;
      deinterlaceAddress(addr, &row, &col);
      if (((*switches) & S_MIXED) && row >= 20) { // ***@@@ is 20 right?
        // Don't draw this row, we're in MIXED mode
      } else {
        if (col <= 39 && row <= 23) {
          DrawLoresPixelAt(mmu->readDirect(addr, 0), col, row);
        }
      }
    }
  }
}

void AppleDisplay::modeChange()
{
  dirty = true;
  dirtyRect.left = dirtyRect.top = 0;
  dirtyRect.right = 279;
  dirtyRect.bottom = 191;
}

void AppleDisplay::Draw80LoresPixelAt(uint8_t c, uint8_t x, uint8_t y, uint8_t offset)
{
  // Just like 80-column text, this has a minor problem; we're taking
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

      if ( !(*switches & S_MIXED) ||
           y < 20 ) {
        drawApplePixel(pixel, x*7+x2+offset*3, y*8+y2);
      }
    }
  }

  pixel = (c >> 4);
  for (uint8_t y2 = 4; y2<8; y2++) {
    for (int8_t x2 = 3; x2>=offset; x2--) {
      if ( !(*switches & S_MIXED) ||
           y < 20 ) {
        drawApplePixel(pixel, x*7+x2+offset*3, y*8+y2);
      }
    }
  }
}

void AppleDisplay::setSwitches(uint16_t *switches)
{
  this->switches = switches;
  modeChange();
}

AiieRect AppleDisplay::getDirtyRect()
{
  return dirtyRect;
}

bool AppleDisplay::needsRedraw()
{
  modeChange(); // FIXME: this shouldn't be necessary.
  /* It should work like this:
   *
   *   When currently active video ram is written to, it calls the display.
   *     Display detects whether or not it's currently locked.
   *     If it's currently locked, then it notes the rect in a "locked update" rect
   *     If it's not locked, then it pulls in the locked rect + this rect and extends the current dirty rect appropriately
   *
   *   Then when we start drawing, we take a snapshot of video ram &
   *   blit the appropriate rect.
   *
   * Alternately: we could have multiple copies of the video areas of
   * RAM and swap between them when drawing starts. But to do that,
   * we'd need another (1 + 1 + 8 + 8) * 2 = 36k of RAM, which we don't have.
   *
   * I'm not sure either approach fixes tearing, though. We see
   * tearing because there's no snapshot when the mode flags change, I
   * think.
   */

  if (dirty) {
    // Figure out what graphics mode we're in and redraw it in its entirety.

    if ((*switches) & S_TEXT) {
      if ((*switches) & S_80COL) {
	redraw80ColumnText(0);
      } else {
	redraw40ColumnText(0);
      }
      
      return true;
    }

    // Not text mode - what mode are we in?
    if ((*switches) & S_HIRES) {
      redrawHires();
    } else {
      redrawLores();
    }

    // Mixed graphics modes: draw text @ bottom
    if ((*switches) & S_MIXED) {
      if ((*switches) & S_80COL) {
	redraw80ColumnText(20);
      } else {
	redraw40ColumnText(20);
      }
    }
  }

  return dirty;
}

void AppleDisplay::didRedraw()
{
  dirty = false;
}

void AppleDisplay::displayTypeChanged()
{
  modeChange();
}

void AppleDisplay::lockDisplay()
{
}

void AppleDisplay::unlockDisplay()
{
}
