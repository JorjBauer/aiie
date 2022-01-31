#ifndef __BASE_DISPLAY_H
#define __BASE_DISPLAY_H

const uint16_t loresPixelColors[16];

#define RGBto565(r,g,b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define _565toR(c) ( ((c) & 0xF800) >> 8 )
#define _565toG(c) ( ((c) & 0x07E0) >> 3 )
#define _565toB(c) ( ((c) & 0x001F) << 3 )
#define RGBto332(r,g,b) ((((r) & 0xE0)) | (((g) & 0xE0) >> 3) | ((b) >> 6))
#define luminanceFromRGB(r,g,b) ( ((r)*0.2126) + ((g)*0.7152) + ((b)*0.0722) )
#define _565To332(c) ((((c) & 0xe000) >> 8) | (((c) & 0x700) >> 6) | (((c) & 0x18) >> 3))
#define _332To565(c) ((((c) & 0xe0) << 8) | (((c) & 0x1c) << 6) | ((c) & 0x03))

#define blendColors(a,b) RGBto565( (_565toR(a) + _565toR(b))/2, (_565toG(a) + _565toG(b))/2, (_565toB(a) + _565toB(b))/2  )

class BaseDisplay {
 public:
  BaseDisplay(uint8_t cs_pin, uint8_t rst_pin, uint8_t mosi_pin, uint8_t sck_pin, uint8_t miso_pin, uint8_t dc_pin=255) {};
  
  ~BaseDisplay() {};

  virtual void begin(uint32_t spi_clock=30000000u, uint32_t spi_clock_read=2000000) = 0;

  virtual void fillWindow(uint16_t color = 0x0000) = 0;
  
  virtual void setFrameBuffer(uint8_t *frame_buffer) = 0;
  
  virtual bool asyncUpdateActive();
  virtual bool updateScreenAsync(bool update_cont = false) = 0;

  virtual void drawPixel(int16_t x, int16_t y, uint16_t color) = 0;
  virtual void drawPixel(int16_t x, int16_t y, uint8_t color) = 0;

  // Apple interface methods
  virtual void cacheApplePixel(uint16_t x, uint16_t y, uint16_t color) = 0;
  virtual void cacheDoubleWideApplePixel(uint16_t x, uint16_t y, uint16_t color16) = 0;
  
  virtual uint32_t frameCount() = 0;
};

#endif
