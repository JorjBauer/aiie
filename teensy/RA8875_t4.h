#ifndef _RA8875_T4_H
#define _RA8875_T4_H

#define SCREEN_DMA_NUM_SETTINGS 3

#include <Arduino.h>
#include <SPI.h>
#include <DMAChannel.h>
#include <stdint.h>

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

class RA8875_t4 {
 public:
  RA8875_t4(const uint8_t cs_pin, const uint8_t rst_pin, const uint8_t mosi_pin, const uint8_t sck_pin, const uint8_t miso_pin);
  ~RA8875_t4();

  void begin(uint32_t spi_clock=30000000u, uint32_t spi_clock_read=2000000);

  void fillWindow(uint16_t color = 0x0000);
  
  void setFrameBuffer(uint8_t *frame_buffer);
  
  bool asyncUpdateActive();
  bool updateScreenAsync(bool update_cont = false);

  void drawPixel(int16_t x, int16_t y, uint16_t color);
  uint32_t frameCount();

  uint8_t color16To8bpp(uint16_t color) __attribute__((always_inline)) {
    return ((color & 0xe000) >> 8) | ((color & 0x700) >> 6) | ((color & 0x18) >> 3);
  }
  
private:
  void _initializeTFT();
  void initDMASettings();

  // These are the old style RA8875 calls -- replace them ***
  void writeCommand(const uint8_t d);
  void writeData16(uint16_t data);
  void waitTransmitComplete(void);
  
  void _writeData(uint8_t data);
  void _writeRegister(const uint8_t reg, uint8_t val);

  uint8_t _readData(bool stat);
  uint8_t _readRegister(const uint8_t reg);

  void _waitBusy(uint8_t res);
  
  boolean _waitPoll(uint8_t regname, uint8_t waitflag, uint8_t timeout);

  void maybeUpdateTCR(uint32_t requested_tcr_state);

  static void dmaInterrupt(void);
  static void dmaInterrupt1(void);
  static void dmaInterrupt2(void);
  void process_dma_interrupt(void);
  
 protected:
  uint8_t _cs, _miso, _mosi, _sck, _rst;
  volatile uint8_t _interruptStates;

  SPIClass *_pspi;
  IMXRT_LPSPI_t *_pimxrt_spi;
  SPIClass::SPI_Hardware_t *_spi_hardware;
  uint8_t _spi_num;
  uint32_t _spi_clock; // desired clock
  uint32_t _spi_clock_read;
  uint32_t _clock; // current clock, used in starting transactions (b/c we have to slow down sometimes)
  volatile uint32_t *_csport;
  uint32_t _cspinmask;

  // DMA stuff
  DMASetting              _dmasettings[3];
  DMAChannel              _dmatx;
  volatile    uint32_t _dma_pixel_index = 0;
  uint16_t _dma_buffer_size;
  uint16_t _dma_cnt_sub_frames_per_frame;
  uint32_t _spi_fcr_save;
  uint8_t *_pfbtft;
  volatile uint8_t _dma_state;
  uint8_t pending_rx_count;
  uint32_t _spi_tcr_current;
  volatile uint32_t _dma_frame_count;
  volatile uint16_t _dma_sub_frame_count;

  void (*_frame_complete_callback)();
  bool _frame_callback_on_HalfDone;
  
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
