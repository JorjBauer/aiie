#include "ILI9341_wrap.h"

ILI9341_Wrap::ILI9341_Wrap(const uint8_t cs_pin, const uint8_t rst_pin, const uint8_t mosi_pin, const uint8_t sck_pin, const uint8_t miso_pin, const uint8_t dc_pin) : BaseDisplay(cs_pin, rst_pin, mosi_pin, sck_pin, miso_pin, dc_pin)
{
  tft = NULL;

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
  tft->setFrameBuffer((uint16_t *)frame_buffer);
  tft->useFrameBuffer(true);
  tft->fillScreen(ILI9341_BLACK);
}

bool ILI9341_Wrap::asyncUpdateActive()
{
  return tft->asyncUpdateActive();
}
  
bool ILI9341_Wrap::updateScreenAsync(bool update_cont)
{
  return tft->updateScreenAsync(update_cont);
}

void ILI9341_Wrap::drawPixel(int16_t x, int16_t y, uint16_t color)
{
  tft->drawPixel(x, y, color);
}

uint32_t ILI9341_Wrap::frameCount()
{
  return tft->frameCount();
}
