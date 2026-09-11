#ifndef __APPLEDISPLAY_H
#define __APPLEDISPLAY_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdlib.h>
#endif

#include "vmdisplay.h"

enum {
  c_black     = 0,
  c_magenta   = 1,
  c_darkblue  = 2,
  c_purple    = 3,
  c_darkgreen = 4,
  c_darkgrey  = 5,
  c_medblue   = 6,
  c_lightblue = 7,
  c_brown     = 8,
  c_orange    = 9,
  c_lightgray = 10,
  c_pink      = 11,
  c_green     = 12,
  c_yellow    = 13,
  c_aqua      = 14,
  c_white     = 15
};

enum {
  m_blackAndWhite = 0,
  m_monochrome    = 1,
  m_ntsclike      = 2,
  m_perfectcolor  = 3
};

class AppleMMU;

class AppleDisplay : public VMDisplay{
 public:
  AppleDisplay();
  virtual ~AppleDisplay();
  virtual bool needsRedraw();
  virtual void didRedraw();
  virtual AiieRect getDirtyRect();
  virtual void lockDisplay();
  virtual void unlockDisplay();

  void modeChange(); // FIXME: rename 'redraw'?
  void setSwitches(uint16_t *switches);

  void writeLores(uint16_t address, uint8_t v);
  void writeHires(uint16_t address, uint8_t v);

  void displayTypeChanged();

  const unsigned char *xlateChar(uint8_t c, bool *invert);

 private:

  bool deinterlaceAddress(uint16_t address, uint8_t *row, uint8_t *col);
  bool deinterlaceHiresAddress(uint16_t address, uint8_t *row, uint16_t *col);

  void Draw14DoubleHiresPixelsAt(uint16_t addr);
  void Draw14HiresPixelsAt(uint16_t addr);
  void Draw80LoresPixelAt(uint8_t c, uint8_t x, uint8_t y, uint8_t offset);

  void redraw40ColumnText(uint8_t startingY);
  void redraw80ColumnText(uint8_t startingY);
  void redrawHires();
  void redrawLores();

  // Draw one whole frame from whatever (*switches) currently points at.
  // This is the original needsRedraw() body, factored out so it can be
  // called once (a frame with a single mode) or once per horizontal band
  // (a frame with mid-frame mode changes), with the switches pointer aimed
  // at the band's state and the clip narrowed to the band.
  void renderFrameWithSwitches();

 private:
  volatile bool dirty;
  AiieRect dirtyRect;

  uint16_t *switches; // pointer to the MMU's switches

  // Pixel clip band, in Apple scanline coordinates [top, bottom). Full
  // screen (0..191) except while rendering a single band; see needsRedraw()
  // and the drawApplePixel / cachePixelClipped macros.
  int clipTop;
  int clipBottom;
};

#endif
