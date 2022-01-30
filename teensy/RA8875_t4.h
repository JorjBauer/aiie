#ifndef _RA8875_T4_H
#define _RA8875_T4_H

#include <Arduino.h>
#include <SPI.h>
#include <DMAChannel.h>
#include <stdint.h>

#include "basedisplay.h"

#define RA8875_WIDTH 800
#define RA8875_HEIGHT 480

#define _RA8875_WAITPOLL_TIMEOUT_DCR_LINESQUTRI_STATUS          20

enum {
  RA8875_DMA_INIT=0x01,
  RA8875_DMA_EVER_INIT=0x08,
  RA8875_DMA_CONT=0x02,
  RA8875_DMA_FINISH=0x04,
  RA8875_DMA_ACTIVE=0x80
};

class RA8875_t4 : public BaseDisplay {
 public:
  RA8875_t4(uint8_t cs_pin, uint8_t rst_pin, uint8_t mosi_pin, uint8_t sck_pin, uint8_t miso_pin, uint8_t dc_pin=255); // dc pin is unused for this display but it's needed for the ILI and base class.

  ~RA8875_t4();

  virtual void begin(uint32_t spi_clock=30000000u, uint32_t spi_clock_read=2000000);

  virtual void fillWindow(uint16_t color = 0x0000);
  
  virtual void setFrameBuffer(uint8_t *frame_buffer);
  
  virtual bool asyncUpdateActive();
  virtual bool updateScreenAsync(bool update_cont = false);
  
  virtual void drawPixel(int16_t x, int16_t y, uint16_t color);
  virtual uint32_t frameCount();

  uint8_t color16To8bpp(uint16_t color) __attribute__((always_inline)) {
    return ((color & 0xe000) >> 8) | ((color & 0x700) >> 6) | ((color & 0x18) >> 3);
  }
  
private:
  void _initializeTFT();
  void initDMASettings();

  // These are the old style RA8875 calls -- replace them ***
  void writeCommand(const uint8_t d);
  void writeData16(uint16_t data);
  
  void _writeData(uint8_t data);
  void _writeRegister(const uint8_t reg, uint8_t val);

  uint8_t _readData(bool stat);
  uint8_t _readRegister(const uint8_t reg);

  void _waitBusy(uint8_t res);
  
  boolean _waitPoll(uint8_t regname, uint8_t waitflag, uint8_t timeout);

  void maybeUpdateTCR(uint32_t requested_tcr_state);

  static void dmaInterrupt(void);
  void process_dma_interrupt(void);
  
 protected:
  uint8_t _cs, _miso, _mosi, _sck, _rst;

  SPIClass *_pspi;
  IMXRT_LPSPI_t *_pimxrt_spi;
  SPIClass::SPI_Hardware_t *_spi_hardware;
  uint32_t _spi_clock; // desired clock
  uint32_t _spi_clock_read;
  uint32_t _clock; // current clock, used in starting transactions (b/c we have to slow down sometimes)

  // DMA stuff
  DMASetting _dmasettings[12];
  DMAChannel _dmatx;
  uint32_t _spi_fcr_save;
  uint8_t *_pfbtft;
  volatile uint8_t _dma_state;
  uint32_t _spi_tcr_current;
  volatile uint32_t _dma_frame_count;

protected:
  void DIRECT_WRITE_LOW(volatile uint32_t * base, uint32_t mask)  __attribute__((always_inline)) {
    *(base+34) = mask;
  }
  void DIRECT_WRITE_HIGH(volatile uint32_t * base, uint32_t mask)  __attribute__((always_inline)) {
    *(base+33) = mask;
  }

  /* These are old-style function names, but with new-style contents */
  void _startSend() __attribute__((always_inline)) {
    _pspi->beginTransaction(SPISettings(_clock, MSBFIRST, SPI_MODE3));
    _spi_tcr_current = _pimxrt_spi->TCR;
    //    DIRECT_WRITE_LOW(_csport, _cspinmask);
    digitalWriteFast(_cs, LOW);
  }

  void _endSend() __attribute__((always_inline)) {
    //    DIRECT_WRITE_HIGH(_csport, _cspinmask);
    digitalWriteFast(_cs, HIGH);
    _pspi->endTransaction();
  }


};


#endif
