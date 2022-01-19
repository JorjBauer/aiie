#include "RA8875_t4.h"
#include "RA8875_registers.h"

// Discussion about DMA channels: http://forum.pjrc.com/threads/25778-Could-there-be-something-like-an-ISR-template-function/page3
// And these libraries use it:
// https://github.com/PaulStoffregen/Audio                                      
// https://github.com/PaulStoffregen/OctoWS2811                                 
// https://github.com/pedvide/ADC                                               
// https://github.com/duff2013/SerialEvent                                      
// https://github.com/pixelmatix/SmartMatrix                                    
// https://github.com/crteensy/DmaSpi <-- DmaSpi has adopted this scheme        


#define COUNT_PIXELS_WRITE (RA8875_WIDTH * RA8875_HEIGHT)
// at 8bpp, each pixel is 1 byte
#define PIXELSIZE 1

#define TCR_MASK  (LPSPI_TCR_PCS(3) | LPSPI_TCR_FRAMESZ(31) | LPSPI_TCR_CONT | LPSPI_TCR_RXMSK )

static  RA8875_t4 *_dmaActiveDisplay[3];

RA8875_t4::RA8875_t4(const uint8_t cs_pin, const uint8_t rst_pin, const uint8_t mosi_pin, const uint8_t sck_pin, const uint8_t miso_pin)
{
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);
  _mosi = mosi_pin;
  _miso = miso_pin;
  _cs = cs_pin;
  _rst = rst_pin;
  _sck = sck_pin;
  _interruptStates = 0b00000000;

  _pspi = NULL;
  _pfbtft = NULL;
  _dma_state = 0;
  pending_rx_count = 0;
  _frame_complete_callback = NULL;
  _frame_callback_on_HalfDone = false;
  _dma_frame_count = 0;
  _dma_sub_frame_count = 0;
  _dmaActiveDisplay[0] = _dmaActiveDisplay[1] = _dmaActiveDisplay[2] = NULL;
}

RA8875_t4::~RA8875_t4()
{
}

void RA8875_t4::begin(uint32_t spi_clock, uint32_t spi_clock_read)
{
  _interruptStates = 0b00000000;
  _spi_clock = spi_clock;
  _spi_clock_read = spi_clock_read;
  _clock = 4000000UL; // start at low speed

  // figure out which SPI bus we're using
  if (SPI.pinIsMOSI(_mosi) && ((_miso == 0xff) || SPI.pinIsMISO(_miso)) && SPI.pinIsSCK(_sck)) {
    _pspi = &SPI;
    _spi_num = 0;
    _pimxrt_spi = &IMXRT_LPSPI4_S;
  } else if (SPI1.pinIsMOSI(_mosi) && ((_miso == 0xff) || SPI1.pinIsMISO(_miso)) && SPI1.pinIsSCK(_sck)) {
    _pspi = &SPI1;
    _spi_num = 1;
    _pimxrt_spi = &IMXRT_LPSPI3_S;
  } else if (SPI2.pinIsMOSI(_mosi) && ((_miso == 0xff) || SPI2.pinIsMISO(_miso)) && SPI2.pinIsSCK(_sck)) {
    _pspi = &SPI2;
    _spi_num = 2;
    _pimxrt_spi = &IMXRT_LPSPI1_S;
  } else {
    Serial.println("Pins given are not valid SPI bus pins");
    return;
  }

  _pspi->setMOSI(_mosi);
  _pspi->setSCK(_sck);
  if (_miso != 0xff) _pspi->setMISO(_miso);
  uint32_t *pa = (uint32_t *)_pspi;
  _spi_hardware = (SPIClass::SPI_Hardware_t *)pa[1];

  _pspi->begin();

  _csport = portOutputRegister(_cs);
  _cspinmask = digitalPinToBitMask(_cs);
  pinMode(_cs, OUTPUT);
  //  DIRECT_WRITE_HIGH(_csport, _cspinmask);
  digitalWriteFast(_cs, HIGH);

  pending_rx_count = 0; // Make sure it is zero if we we do a second begin...
  _spi_tcr_current = _pimxrt_spi->TCR; // get the current TCR value
  
  maybeUpdateTCR(LPSPI_TCR_FRAMESZ(7));
  
  _initializeTFT();
}

void RA8875_t4::_initializeTFT()
{
  // toggle RST low to reset
  if (_rst < 255) {
    pinMode(_rst, OUTPUT);
    digitalWrite(_rst, HIGH);
    delay(10);
    digitalWrite(_rst, LOW);
    delay(220);
    digitalWrite(_rst, HIGH);
    delay(300);
  } else {
    // Try a soft reset
    writeCommand(RA8875_PWRR);
    _writeData(RA8875_PWRR_SOFTRESET);
    delay(20);
    _writeData(RA8875_PWRR_NORMAL);
    delay(200);
  }
  
  // Set the sysclock
  _writeRegister(RA8875_PLLC1, 0x07); // same as default value: %0000 0111 == pre /=1; input /=7
  delay(1);
  _writeRegister(RA8875_PLLC1+1, 0x03); // same as default value: %0000 0011 == output /8
  delay(1);
  _writeRegister(RA8875_PCSR, 0x81); // pixel clock setting register: %1000 0001 == PDAT at PCLK falling edge; PCLK period is 2* system clock period
  delay(1);

  // colorspace
  _writeRegister(RA8875_SYSR, 0x00); // 8-bit (0x0C == 16-bit)

  _writeRegister(RA8875_HDWR, 0x63); // LCD horizontal display width == (v+1)*8
  _writeRegister(RA8875_HNDFTR, 0x00); // Horizontal non-display period fine tuning
  _writeRegister(RA8875_HNDR, 0x03); // LCD Horizontal non-display period register; period (in pixels) = (v+1)*8 + HNDFTR+2 == 32
  _writeRegister(RA8875_HSTR, 0x03); // HSYNC start position register; start position (in pixels) = (v+1)*8 == 24
  _writeRegister(RA8875_HPWR, 0x0B); // HSYNC pulse width register; %0000 1011 == HSYNC low active, hsync pulse width = ((v&32)+1)*8 = 88
  _writeRegister(RA8875_VDHR0, 0xDF); // LCD Vertical display height register 0 (low byte of height, where height = this value + 1)
  _writeRegister(RA8875_VDHR0+1, 0x01); // LCD Vertical display height register 1 (high byte of height)
  _writeRegister(RA8875_VNDR0, 0x1F); // LCD vertical non-display period register 0 (low byte of non-display period in lines, where period=(VNDR+1) == 32)
  _writeRegister(RA8875_VNDR0+1, 0x00); // LCD vertical non-display period register 1 (high byte of non-display period in lines)
  _writeRegister(RA8875_VSTR0, 0x16); // VSYNC start position register 0; low byte, where start pos(line) = (VSTR+1) == 23
  _writeRegister(RA8875_VSTR0+1, 0x00); // VSYNC start position register 1; high byte
  _writeRegister(RA8875_VPWR, 0x01); // VSYNC pulse width register; %0000 0001 == low active, pulse width (in lines) = (v&0x7F)+1 == 2

  // Set the entire screen as the active window
  _writeRegister(RA8875_HSAW0, 0x00); // horizontal start point of active window
  _writeRegister(RA8875_HSAW0+1, 0x00);
  _writeRegister(RA8875_HEAW0, (RA8875_WIDTH-1) & 0xFF);
  _writeRegister(RA8875_HEAW0+1, (RA8875_WIDTH-1) >> 8); // horizontal end point of active window
  _writeRegister(RA8875_VSAW0, 0x00); // vertical start point of active window
  _writeRegister(RA8875_VSAW0+1, 0x00);
  _writeRegister(RA8875_VEAW0, (RA8875_HEIGHT-1) & 0xFF); // vertical end point of active window
  _writeRegister(RA8875_VEAW0+1, (RA8875_HEIGHT-1) >> 8);

  delay(10);

  // Update the sysclock
  _writeRegister(RA8875_PLLC1, 0x0B); // %0000 1011 == pre-drive /1; input /11
  delay(1);
  _writeRegister(RA8875_PLLC1+1, 0x02); // %0000 0010 == PLL output /4
  delay(1);
  _writeRegister(RA8875_PCSR, 0x81); // %1000 0001 == PDAT at PCLK falling edge; PCLK period is 2* system clock period
  delay(1);

  _clock = _spi_clock; // speed up to full speed now

  delay(1);
  // clear memory
  uint8_t temp;
  temp = _readRegister(RA8875_MCLR);
  temp |= (1<<7);
  _writeData(temp);
  _waitBusy(0x80);

  delay(1);
  // turn on the display
  _writeRegister(RA8875_PWRR, RA8875_PWRR_NORMAL | RA8875_PWRR_DISPON);
  delay(1);
  fillWindow(); // defaults to black

  // turn on backlight
  _writeRegister(RA8875_P1CR, (RA8875_PxCR_ENABLE | (RA8875_PWM_CLK_DIV1024 & 0xF)));
  _writeRegister(RA8875_P1DCR, 255); // brightness
  
  // set graphics mode & default memory write order/behavior
  _writeRegister(RA8875_MWCR0, 0x00);

  // *** rotation?
  
  // Not sure we have to do this a second time, but set active window...
  _writeRegister(RA8875_HSAW0, 0x00); // horizontal start point of active window
  _writeRegister(RA8875_HSAW0+1, 0x00);
  _writeRegister(RA8875_HEAW0, (RA8875_WIDTH-1) & 0xFF);
  _writeRegister(RA8875_HEAW0+1, (RA8875_WIDTH-1) >> 8); // horizontal end point of active window
  _writeRegister(RA8875_VSAW0, 0x00); // vertical start point of active window
  _writeRegister(RA8875_VSAW0+1, 0x00);
  _writeRegister(RA8875_VEAW0, (RA8875_HEIGHT-1) & 0xFF); // vertical end point of active window
  _writeRegister(RA8875_VEAW0+1, (RA8875_HEIGHT-1) >> 8);
  
  // set foreground color
  _writeRegister(RA8875_FGCR0, 0);
  _writeRegister(RA8875_FGCR0+1, 0);
  _writeRegister(RA8875_FGCR0+2, 0);
  // set background color
  _writeRegister(RA8875_BGCR0, 0);
  _writeRegister(RA8875_BGCR0+1, 0);
  _writeRegister(RA8875_BGCR0+2, 0);

//  _writeRegister(RA8875_FNCR1, 0); // probably not necessary since we're not using built-in fonts
//  setCursor(0,0);
  
  _writeRegister(RA8875_GPIOX, true); // turn on backlight

}

void RA8875_t4::setFrameBuffer(uint8_t *frame_buffer)
{
  _pfbtft = frame_buffer;
  _dma_state &= ~RA8875_DMA_INIT;
}

bool RA8875_t4::asyncUpdateActive()
{
  return false;
}

void RA8875_t4::initDMASettings()
{
  if (_dma_state & RA8875_DMA_INIT)
    return;

  Serial.println("Initializing DMA");
  
  uint8_t dmaTXevent = _spi_hardware->tx_dma_channel;
  if (_dma_state & RA8875_DMA_EVER_INIT) {
    _dmasettings[0].sourceBuffer(_pfbtft, COUNT_PIXELS_WRITE * PIXELSIZE);
    // FIXME: I don't know if arm_dcache_flush is sufficient to break the cache? cf https://github-wiki-see.page/m/TeensyUser/doc/wiki/Memory-Mapping
    // The ILI9341 code uses 3 buffers and "copies the data" - but _pfbtft is malloc'd a 2* the size of the pixels (+32, presumably for alignment?) - and it's 16-bit, so 2* pixels is just the data itself.
    // So how is it that dmasettings[0] -> _pfbtft, and
    // dmasettings[1] -> _pfbtft[middle] and
    // dmasettings[2] -> _pfbtft[end] yet [1] and [2] also refer to the full buffer size? That would mean
    // _pfbtft is malloc'd to double the size it needs and there's some floating copying going on?
    // I see no memcpy() calls... and in "first time we init" it looks diferent too, so I'm very confused
    _dmasettings[1].sourceBuffer(&_pfbtft[COUNT_PIXELS_WRITE], COUNT_PIXELS_WRITE*PIXELSIZE);
    _dmasettings[2].sourceBuffer(&_pfbtft[COUNT_PIXELS_WRITE*2], COUNT_PIXELS_WRITE*PIXELSIZE);
    if (_frame_callback_on_HalfDone) _dmasettings[1].interruptAtHalf();
    else _dmasettings[1].TCD->CSR &= ~DMA_TCD_CSR_INTHALF;
  } else {
    _dmasettings[0].sourceBuffer(_pfbtft, COUNT_PIXELS_WRITE * PIXELSIZE);
    _dmasettings[0].destination(_pimxrt_spi->TDR);
    _dmasettings[0].TCD->ATTR_DST = 1;
    _dmasettings[0].replaceSettingsOnCompletion(_dmasettings[1]);
    
    _dmasettings[1].sourceBuffer(&_pfbtft[COUNT_PIXELS_WRITE], COUNT_PIXELS_WRITE*PIXELSIZE);
    _dmasettings[1].destination(_pimxrt_spi->TDR);
    _dmasettings[1].TCD->ATTR_DST = 1;
    if (_frame_callback_on_HalfDone) _dmasettings[1].interruptAtHalf();
    else _dmasettings[1].TCD->CSR &= ~DMA_TCD_CSR_INTHALF;
    _dmasettings[1].replaceSettingsOnCompletion(_dmasettings[2]);

    _dmasettings[2].sourceBuffer(&_pfbtft[COUNT_PIXELS_WRITE*2], COUNT_PIXELS_WRITE*PIXELSIZE);
    _dmasettings[2].destination(_pimxrt_spi->TDR);
    _dmasettings[2].TCD->ATTR_DST = 1;
    _dmasettings[2].replaceSettingsOnCompletion(_dmasettings[0]);
    _dmasettings[2].interruptAtCompletion();

    _dmatx = _dmasettings[0];
    _dmatx.begin(true);
    _dmatx.triggerAtHardwareEvent(dmaTXevent);
    if (_spi_num == 0) _dmatx.attachInterrupt(dmaInterrupt);
    else if (_spi_num == 1) _dmatx.attachInterrupt(dmaInterrupt1);
    else _dmatx.attachInterrupt(dmaInterrupt2);
    Serial.print("DMA is set up on SPI ");
    Serial.println(_spi_num);
    _dma_state = RA8875_DMA_INIT | RA8875_DMA_EVER_INIT;
  }
}

bool RA8875_t4::updateScreenAsync(bool update_cont)
{
  if (!_pfbtft) return false;

  initDMASettings();
  if (_dma_state & RA8875_DMA_ACTIVE)
    return false;

  Serial.println("-");
  // Half of main ram has a 32k cache. This tells it to flush the cache if necessary.
  if ((uint32_t)_pfbtft >= 0x20200000u) arm_dcache_flush(_pfbtft, RA8875_WIDTH*RA8875_HEIGHT);
  
  _dmasettings[2].TCD->CSR &= ~(DMA_TCD_CSR_DREQ);
  _startSend();

  // Don't need to reset the window b/c we never change it; but set the X/Y cursor back to the origin
  _writeRegister(RA8875_CURV0, 0);
  _writeRegister(RA8875_CURV0+1, 0);
  _writeRegister(RA8875_CURH0, 0);
  _writeRegister(RA8875_CURH0+1, 0);

  // And start a ram write command
  writeCommand(RA8875_MRWC);

  _spi_fcr_save = _pimxrt_spi->FCR; // FIFO Control Register
  _pimxrt_spi->FCR=0; // turn off FIFO watermarks
 
  // Set transmit command register: disable RX ("mask out RX"), enable
  // TX from FIFO (b/c it's not masked out), and 8-bit data transfers
  // (7+1).
  maybeUpdateTCR(LPSPI_TCR_FRAMESZ(7) | LPSPI_TCR_RXMSK /*| LPSPI_TCR_CONT*/);
  // Set up the DMA Enable Register to enable transmit DMA (and not receive DMA)
  _pimxrt_spi->DER = LPSPI_DER_TDDE;
  // Clear the status register %0011 1111 0000 0000 == set DMF, REF,
  //   TEF, TCF, FCF, WCF; clear MBF, RDF, TDF.  MBF: busy flag; DMF:
  //   data match; REF: rec error flag; TEF: xmit error flag; TCF:
  //   xmit complete flag; FCF: frame complete flag; WCF: word
  //   complete flag; RDF: rec data flag; TDF: xmit data flag
  _pimxrt_spi->SR = 0x3f00;       // clear out all of the other status... 
  _dmatx.triggerAtHardwareEvent( _spi_hardware->tx_dma_channel );
  _dmatx = _dmasettings[0];
  _dmatx.begin(false);
  _dmatx.enable();
  _dma_frame_count = 0;
  _dmaActiveDisplay[_spi_num]  = this;
  if (update_cont) {
    _dma_state |= RA8875_DMA_CONT;
  } else {
    _dmasettings[2].disableOnCompletion();
    _dma_state &= ~RA8875_DMA_CONT;
  }
  _dma_state |= RA8875_DMA_ACTIVE;

  return true;
}

void RA8875_t4::fillWindow(uint16_t color)
{
  int x0=0, y0=0;
  int x1=RA8875_WIDTH-1,y1=RA8875_HEIGHT-1;
  //X0                                                                    
  _writeRegister(RA8875_DLHSR0,    x0 & 0xFF);
  _writeRegister(RA8875_DLHSR0 + 1,x0 >> 8);
  //Y0                                                                    
  _writeRegister(RA8875_DLVSR0,    y0 & 0xFF);
  _writeRegister(RA8875_DLVSR0 + 1,y0 >> 8);
  //X1                                                                    
  _writeRegister(RA8875_DLHER0,    x1 & 0xFF);
  _writeRegister(RA8875_DLHER0 + 1,x1 >> 8);
  //Y1                                                                    
  _writeRegister(RA8875_DLVER0,    y1 & 0xFF);
  _writeRegister(RA8875_DLVER0 + 1,y1 >> 8);
  
  // Set the color
  _writeRegister(RA8875_FGCR0,((color & 0xF800) >> 11)); // 5 bits red
  _writeRegister(RA8875_FGCR0+1,((color & 0x07E0) >> 5)); // 6 bits green
  _writeRegister(RA8875_FGCR0+2,((color & 0x001F)     )); // 5 bits blue

  // Send fill
  writeCommand(RA8875_DCR); // draw control register
  _writeData(0xB0); // %1011 0000 == start draw; stop circle; fill shape; draw square; draw square (yes two different bits for draw square)

  // Wait for completion (when DCR_LINESQUTRI_STATUS bit it set in read result, before TIMEOUT happens)
  _waitPoll(RA8875_DCR, RA8875_DCR_LINESQUTRI_STATUS, _RA8875_WAITPOLL_TIMEOUT_DCR_LINESQUTRI_STATUS);
}

// *** Remove this and convert to native 8-bit? Or make it inline?
uint8_t _color16To8bpp(uint16_t color) {
  return ((color & 0xe000) >> 8) | ((color & 0x700) >> 6) | ((color & 0x18) >> 3);
}
  
void RA8875_t4::drawPixel(int16_t x, int16_t y, uint16_t color)
{
  // FIXME: bounds checking

  // Set Y
  _writeRegister(RA8875_CURV0, y & 0xFF); // cursor vertical location
  _writeRegister(RA8875_CURV0+1, y >> 8);
  // Set X
  _writeRegister(RA8875_CURH0, x & 0xFF); // cursor horiz location
  _writeRegister(RA8875_CURH0+1, (x >> 8));

  // Send pixel data
  writeCommand(RA8875_MRWC); // write to wherever MWCR1 says (which we expect to be default graphics layer)
  //  writeData16(color);
  _writeData(_color16To8bpp(color));
}

uint32_t RA8875_t4::frameCount()
{
  return 0;
}

void RA8875_t4::writeCommand(const uint8_t d)
{
  _startSend();
  _pspi->transfer(RA8875_CMDWRITE);
  _pspi->transfer(d);
  _endSend();
}

void RA8875_t4::writeData16(uint16_t data)
{
  _startSend();
  _pspi->transfer(RA8875_DATAWRITE);
  _pspi->transfer16(data);
  _endSend();
}

void RA8875_t4::_writeData(uint8_t data)
{
  _startSend();
  _pspi->transfer(RA8875_DATAWRITE);
  _pspi->transfer(data);
  _endSend();
}

uint8_t RA8875_t4::_readData(bool stat)
{
  // FIXME do we need to slow down for reads?
  _startSend();
  _pspi->transfer(stat ? RA8875_CMDREAD : RA8875_DATAREAD);
  uint8_t x = _pspi->transfer(0x00);
  _endSend();
  return x;
}

void RA8875_t4::_writeRegister(const uint8_t reg, uint8_t val)
{
  writeCommand(reg);
  _writeData(val);
}

uint8_t RA8875_t4::_readRegister(const uint8_t reg)
{
  writeCommand(reg);
  return _readData(false);
}

boolean RA8875_t4::_waitPoll(uint8_t regname, uint8_t waitflag, uint8_t timeout)
{
  uint8_t temp;
  unsigned long start_time = millis();
  
  while (1) {
    temp = _readRegister(regname);
    if (!(temp & waitflag)) {
      return true;
    }
    if ((millis() - start_time) > timeout) {
      // timeout
      return false;
    }
  }
  /* NOTREACHED */
}

void RA8875_t4::_waitBusy(uint8_t res)
{
  uint8_t temp;
  unsigned long start = millis();//M.Sandercock
  do {
    if (res == 0x01) writeCommand(RA8875_DMACR);//dma
    temp = _readData(true);
    if ((millis() - start) > 10) return;
  } while ((temp & res) == res);
}

void RA8875_t4::maybeUpdateTCR(uint32_t requested_tcr_state)
{
  if ((_spi_tcr_current & TCR_MASK) != requested_tcr_state) {
    // PCS is the peripheral chip select used for the transfer
    _spi_tcr_current = (_spi_tcr_current & ~TCR_MASK) | requested_tcr_state ;
    // only output when Transfer queue is empty.            
    while ((_pimxrt_spi->FSR & 0x1f) )      ;
    _pimxrt_spi->TCR = _spi_tcr_current;    // update the TCR
  }
}

void RA8875_t4::dmaInterrupt(void) {
  if (_dmaActiveDisplay[0]) {
    _dmaActiveDisplay[0]->process_dma_interrupt();
  }
}

void RA8875_t4::dmaInterrupt1(void) {
  // FIXME this isn't being called
  digitalWrite(13, LOW);
  if (_dmaActiveDisplay[1]) {
    _dmaActiveDisplay[1]->process_dma_interrupt();
  }
}

void RA8875_t4::dmaInterrupt2(void) {
  if (_dmaActiveDisplay[2]) {
    _dmaActiveDisplay[2]->process_dma_interrupt();
  }
}

void RA8875_t4::process_dma_interrupt(void) {
  _dmatx.clearInterrupt();
  if (_frame_callback_on_HalfDone && (_dmatx.TCD->SADDR > _dmasettings[1].TCD->SADDR)) {
    _dma_sub_frame_count = 1; // set as partial frame.
    if (_frame_complete_callback) (*_frame_complete_callback)();
  } else {
    _dma_frame_count++;
    _dma_sub_frame_count = 0; // this is a full frame
    if ((_dma_state & RA8875_DMA_CONT) == 0) {
      // Single refresh, or the user canceled, so release the CS pin
      while (_pimxrt_spi->FSR & 0x1f) ; // wait until transfer is done
      while (_pimxrt_spi->SR & LPSPI_SR_MBF) ; // ... what's the MBF?
      _dmatx.clearComplete();
      _pimxrt_spi->FCR = LPSPI_FCR_TXWATER(15);
      _pimxrt_spi->DER = 0; // turn off tx and rx DMA
      _pimxrt_spi->CR = LPSPI_CR_MEN | LPSPI_CR_RRF | LPSPI_CR_RTF; // ??
      _pimxrt_spi->SR = 0x3f00;
      maybeUpdateTCR(LPSPI_TCR_FRAMESZ(7));
      _endSend();
      _dma_state &= ~RA8875_DMA_ACTIVE;
      _dmaActiveDisplay[_spi_num] = 0;
    } else {
      if (_frame_complete_callback) (*_frame_complete_callback)();
      else {
        // Try to flush memory
        if ((uint32_t)_pfbtft >= 0x20200000u) arm_dcache_flush(_pfbtft, RA8875_WIDTH*RA8875_HEIGHT);
      }
    }
    // make sure the code is synchronized - memory access must be
    // complete before we continue
    asm("dsb");
  }
}
