#ifndef __ILI9341_WRAP_H
#define __ILI9341_WRAP_H

#include <Arduino.h>
#include <SPI.h>
#include <DMAChannel.h>
#include <stdint.h>
#include <ILI9341_t3n.h>

#include "basedisplay.h"

#define ILI9341_WIDTH 320
#define ILI9341_HEIGHT 240

class ILI9341_Wrap : public BaseDisplay {
 public:
  ILI9341_Wrap(uint8_t cs_pin, uint8_t rst_pin, uint8_t mosi_pin, uint8_t sck_pin, uint8_t miso_pin, uint8_t dc_pin=255);
  ~ILI9341_Wrap();

  virtual void begin(uint32_t spi_clock=30000000u, uint32_t spi_clock_read=2000000);

  virtual void fillWindow(uint8_t coloridx = 0x00);
  
  virtual void setFrameBuffer(uint8_t *frame_buffer);
  
  virtual bool asyncUpdateActive();
  virtual bool updateScreenAsync(bool update_cont = false);
  
  virtual void drawPixel(int16_t x, int16_t y, uint16_t color);

  virtual void cacheApplePixel(uint16_t x, uint16_t y, uint8_t coloridx);
  virtual void cacheDoubleWideApplePixel(uint16_t x, uint16_t y, uint8_t coloridx);

  void cacheBlendedPixel(uint16_t x, uint16_t y, uint16_t color16);

  virtual uint32_t frameCount();

private:
  ILI9341_t3n *tft;
  uint8_t _cs, _dc, _rst, _mosi, _sck, _miso;
  uint16_t *frame_buffer;
};


#endif
