#include "ILI9341_wrap.h"

#include "images.h"
#include "globals.h"
#include "appledisplay.h"

#define _332To565(c) ((((c) & 0xe0) << 8) | (((c) & 0x1c) << 6) | ((c) & 0x03))

ILI9341_Wrap::ILI9341_Wrap(const uint8_t cs_pin, const uint8_t rst_pin, const uint8_t mosi_pin, const uint8_t sck_pin, const uint8_t miso_pin, const uint8_t dc_pin) : BaseDisplay(cs_pin, rst_pin, mosi_pin, sck_pin, miso_pin, dc_pin)
{
  tft = NULL;
  frame_buffer = NULL;

  _cs = cs_pin;
  _dc = dc_pin;
  _rst = rst_pin;
  _mosi = mosi_pin;
  _sck = sck_pin;
  _miso = miso_pin;
}

ILI9341_Wrap::~ILI9341_Wrap()
{
  if (tft)
    delete tft;
}

void ILI9341_Wrap::begin(uint32_t spi_clock=30000000u, uint32_t spi_clock_read=2000000)
{
  if (!tft) {
    tft = new ILI9341_t3n(_cs, _dc, _rst, _mosi, _sck, _miso);
    tft->begin(spi_clock, spi_clock_read);
    tft->setRotation(3);
  }
}

void ILI9341_Wrap::fillWindow(uint16_t color = 0x0000)
{
}

void ILI9341_Wrap::setFrameBuffer(uint8_t *frame_buffer)
{
  if (tft) {
    tft->fillScreen(ILI9341_BLACK);
    this->frame_buffer = (uint16_t *)frame_buffer;
    tft->setFrameBuffer((uint16_t *)frame_buffer);
    tft->useFrameBuffer(true);
  }
}

bool ILI9341_Wrap::asyncUpdateActive()
{
  return tft ? tft->asyncUpdateActive() : false;
}
  
bool ILI9341_Wrap::updateScreenAsync(bool update_cont)
{
  return tft ? tft->updateScreenAsync(update_cont) : false;
}

void ILI9341_Wrap::drawPixel(int16_t x, int16_t y, uint16_t color)
{
  frame_buffer[y*ILI9341_WIDTH+x] = color;
}

void ILI9341_Wrap::drawPixel(int16_t x, int16_t y, uint8_t color)
{
  frame_buffer[y*ILI9341_WIDTH+x] = _332To565(color);
}

// The 9341 is half the width we need, so this jumps through hoops to
// reduce the resolution in a way that's reasonable by blending pixels
void ILI9341_Wrap::cacheApplePixel(uint16_t x, uint16_t y, uint16_t color)
{
  if (x&1) {
    uint16_t origColor =frame_buffer[(y+SCREENINSET_9341_Y)*ILI9341_WIDTH+(x>>1)+SCREENINSET_9341_X];
      if (g_displayType == m_blackAndWhite) {
        // There are four reasonable decisions here: if either pixel
        // *was* on, then it's on; if both pixels *were* on, then it's
        // on; and if the blended value of the two pixels were on,
        // then it's on; or if the blended value of the two is above
        // some certain overall brightness, then it's on. This is the
        // last of those - where the brightness cutoff is defined in
        // the bios as g_luminanceCutoff.
        uint16_t blendedColor = blendColors(origColor, color);
        uint16_t luminance = luminanceFromRGB(_565toR(blendedColor),
                                              _565toG(blendedColor),
                                              _565toB(blendedColor));
        cacheDoubleWideApplePixel(x>>1,y,(uint16_t)((luminance >= g_luminanceCutoff) ? 0xFFFF : 0x0000));
      } else {
        cacheDoubleWideApplePixel(x>>1, y, color);
      }
  } else {
    // All of the even pixels get drawn...
    cacheDoubleWideApplePixel(x>>1, y, color);
  }
}

void ILI9341_Wrap::cacheDoubleWideApplePixel(uint16_t x, uint16_t y, uint16_t color16)
{
  frame_buffer[(y+SCREENINSET_9341_Y)*ILI9341_WIDTH + (x) + SCREENINSET_9341_X] = color16;
}

uint32_t ILI9341_Wrap::frameCount()
{
  return tft ? tft->frameCount() : 0;
}
