#include "RA8875_t4.h"
#include "RA8875_registers.h"

// Discussion about DMA channels: http://forum.pjrc.com/threads/25778-Could-there-be-something-like-an-ISR-template-function/page3
// Thread discussing the way to get started: https://forum.pjrc.com/threads/63353-Teensy-4-1-How-to-start-using-DMA
// Thread of someone writing an LCD interface: https://forum.pjrc.com/threads/67247-Teensy-4-0-DMA-SPI
// And these libraries use it:
// https://github.com/PaulStoffregen/Audio                                      
// https://github.com/PaulStoffregen/OctoWS2811                                 
// https://github.com/pedvide/ADC                                               
// https://github.com/duff2013/SerialEvent                                      
// https://github.com/pixelmatix/SmartMatrix                                    
// https://github.com/crteensy/DmaSpi <-- DmaSpi has adopted this scheme        
//

/* The ST7735 and ILI9341 have both been modified in the Teensyduino
 * distribution to use DMA + SPI the way that I want to do this here.
 * The major differences here:
 *   - Both the 7735 and 9341 use 16-bit transfers. Since this has a 
 *     substantially larger framebuffer that doesn't fit in the DMAMEM
 *     region, that won't work -- so I'm using 8-bpp mode.
 *   - Both the ILI9341 and ST7735 have a D/C pin to tell the display 
 *     if the data being sent is a command or is data. The 8875 does
 *     not have a D/C pin.
 *   - Both the 7735 and 9341 drivers have a lot of #ifdef overhead 
 *     to support multiple different Teensys. I'm targeting the 4.1
 *     so I'm not including any of that abstraction.
 *   - The RA8875 driver supports multiple different pixel-size 
 *     displays (and vendors' interfaces). Aiie only supports 800x480
 *     using the Adafruit RA8875 display module.
 *   - Each dmasettings looks like it can address 32767 bytes of data,
 *     so while the 320*240 16-bit structure needs 3 structs (well, 
 *     2.34 structs) the 800*480 8-bit structure needs 12 structs.
 *
 *
 * The initialization sets us to 8bpp color mode, so that 800*480 fits
 * within 512k of RAM (800*480 bytes = 375k; if this were 16bpp, then
 * it would overflow by 238k.) This is important because, if I
 * understand it rightly, only one of the 512k banks on the Teensy is
 * coupled to DMA. (The other bank of memory is tightly coupled to the
 * processor for faster access.) It's possible to further reduce our
 * memory footprint if necessary by just caching the Apple's display
 * size of 560x240 (reducing it to about 132k) and initializing the
 * display with a "display window" for that area of the screen,
 * turning off DMA and drawing outside the window when necessary (to
 * update a drive indicator, debug message, or the UI outline itself).
 *
 * The core drawing operation uses the RA8875's MRWC command to send 
 * data in a transaction, driven by the DMA layer. It will do this:
 *   start
 *     RA8875_CMDWRITE
 *     RA8875_MRWC
 *   end
 *   start
 *     RA8875_DATAWRITE
 *     <all the data>
 *   end
 * 
 * It's unclear to me whether or not we'll have to end and restart
 * after every frame, but I'll find out as it's built...
 *
 * The one abstraction I did leave in here is which SPI pins are being
 * used. This still supports moving to any of the three SPI busses.
 *
 * WIP: using the LED and Serial to debug why interrupts haven't been
 * firing the way I expected. I think it may be because I didn't have a
 * transaction running.
 * ... no, that didn't seem to solve the problem.
 *
 * I don't know if the way I'm maybeUpdateTCR-ing with
 * LPSPI_TCR_PCS(3) is correct or not -- I haven't fully comprehended
 * what the code in 9341/7735 are doing there, because it's combined
 * with the D/C pin functionality. But LPSPI_TCR_PCS refers to 
 * the SPI peripheral chip select: 00 = LPSPI_PCS[0]; 01 = 1; etc.
 * From what I see in the original maybeUpdateTCR method, it's using 
 * LPSPI_TCR_PCS(3) as a CARRIER of the expected state of the _dc 
 * pin. It is masked back out before setting TCR. So I think the 
 * right thing to do here is completely remove all of the LPSPI_TCR_PCS
 * bits. ... but it still isn't getting to an interrupt state.
 *
 * Some pages I've been reading:
 *   https://forum.pjrc.com/threads/57280-RA8875-from-Buydisplay/page9?highlight=lpspi_tcr_pcs (pg9)
 *   https://www.displayfuture.com/Display/datasheet/controller/ST7735.pdf
 *   https://github.com/ElectroTechnique/TSynth-for-Teensy3.6/blob/master/ST7735_t3.cpp
 *   https://github-wiki-see.page/m/TeensyUser/doc/wiki/Memory-Mapping
 */

// at 8bpp, each pixel is 1 byte
#define COUNT_PIXELS_WRITE (RA8875_WIDTH * RA8875_HEIGHT)

#define TCR_MASK  ( LPSPI_TCR_FRAMESZ(31) | LPSPI_TCR_CONT | LPSPI_TCR_RXMSK )
#define _565toR(c) ( ((c) & 0xF800) >> 8 )
#define _565toG(c) ( ((c) & 0x07E0) >> 3 )
#define _565toB(c) ( ((c) & 0x001F) << 3 )

// 3 of these, one for each of the 3 busses, so that 3 separate
// displays could be driven. FIXME: I don't really need all 3 in this
// application, so this can be pared down.
static  RA8875_t4 *_dmaActiveDisplay = NULL;

RA8875_t4::RA8875_t4(const uint8_t cs_pin, const uint8_t rst_pin, const uint8_t mosi_pin, const uint8_t sck_pin, const uint8_t miso_pin)
{
  _mosi = mosi_pin;
  _miso = miso_pin;
  _cs = cs_pin;
  _rst = rst_pin;
  _sck = sck_pin;

  _pspi = NULL;
  _pfbtft = NULL;
  _dma_state = 0;
  _dma_frame_count = 0;
  _dmaActiveDisplay = NULL;
}

RA8875_t4::~RA8875_t4()
{
}

void RA8875_t4::begin(uint32_t spi_clock, uint32_t spi_clock_read)
{
  _spi_clock = spi_clock;
  _spi_clock_read = spi_clock_read;
  _clock = 4000000UL; // start at low speed

  // figure out which SPI bus we're using
  if (SPI.pinIsMOSI(_mosi) && ((_miso == 0xff) || SPI.pinIsMISO(_miso)) && SPI.pinIsSCK(_sck)) {
    _pspi = &SPI;
    _pimxrt_spi = &IMXRT_LPSPI4_S;
  } else if (SPI1.pinIsMOSI(_mosi) && ((_miso == 0xff) || SPI1.pinIsMISO(_miso)) && SPI1.pinIsSCK(_sck)) {
    _pspi = &SPI1;
    _pimxrt_spi = &IMXRT_LPSPI3_S;
  } else if (SPI2.pinIsMOSI(_mosi) && ((_miso == 0xff) || SPI2.pinIsMISO(_miso)) && SPI2.pinIsSCK(_sck)) {
    _pspi = &SPI2;
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

  pinMode(_cs, OUTPUT);
  digitalWriteFast(_cs, HIGH);

  _spi_tcr_current = _pimxrt_spi->TCR; // get the current TCR value
  
  maybeUpdateTCR(LPSPI_TCR_FRAMESZ(7));

  _initializeTFT();
}

void RA8875_t4::_initializeTFT()
{
  // toggle RST low to reset
  if (_rst < 255) {
    pinMode(_rst, OUTPUT);
    digitalWriteFast(_rst, HIGH);
    delay(10);
    digitalWriteFast(_rst, LOW);
    delay(220);
    digitalWriteFast(_rst, HIGH);
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

  delay(100);

  // clear memory
  uint8_t temp;
  temp = _readRegister(RA8875_MCLR);
  temp |= (1<<7);
  _writeData(temp);
  _waitBusy(0x80);

  delay(1);
  
  // Update the sysclock. 300MHz Fpll; 150MHz sysclk; 18.75 MHz pixel
  // clock.  That's the fastest that seems to reliably work for me. It
  // also allows a SPI bus transfer at just under 80MHz (literally
  // 79.999... there must be a constant somewhere that's breaking the
  // SPI module at 80, or a Teensy 4.1 bus limit, or ... ?)
  _writeRegister(RA8875_PLLC1, 0x0E);
  delay(1);
  _writeRegister(RA8875_PLLC1+1, 0x01);
  delay(1);
  _writeRegister(RA8875_PCSR, 0x82);
  delay(1);

  _clock = _spi_clock; // speed up to full speed now

  delay(10);

  // turn on the display
  _writeRegister(RA8875_PWRR, RA8875_PWRR_NORMAL | RA8875_PWRR_DISPON);
  delay(1);
  fillWindow(); // defaults to black

  // turn on backlight
  _writeRegister(RA8875_P1CR, (RA8875_PxCR_ENABLE | (RA8875_PWM_CLK_DIV2048 & 0xF)));
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
  return (_dma_state & RA8875_DMA_ACTIVE);
}

void RA8875_t4::initDMASettings()
{
  if (_dma_state & RA8875_DMA_INIT)
    return;

  uint8_t dmaTXevent = _spi_hardware->tx_dma_channel;

  // Each DMA structure can only track 32767 words written (where a
  // word here is 8 bits). So we need 12 of these to cover the whole
  // set of 800*480 display data. And we're assuming that they are
  // evenly divisible, and that the DMA engine won't care if the
  // data isn't all aligned to 2^15 boundaries.
  uint16_t pixelsWrittenPerDMAreq = COUNT_PIXELS_WRITE / 12;
  
  if (_dma_state & RA8875_DMA_EVER_INIT) {
    // Just quickly reset the pointers and sizes
    for (int i=0; i<12; i++) {
      _dmasettings[i].sourceBuffer(&_pfbtft[pixelsWrittenPerDMAreq*i], pixelsWrittenPerDMAreq);
    }
  } else {
    for (int i=0; i<12; i++) {
      _dmasettings[i].sourceBuffer(&_pfbtft[pixelsWrittenPerDMAreq*i], pixelsWrittenPerDMAreq);
      _dmasettings[i].destination(_pimxrt_spi->TDR); // DMA sends data to LPSPI's transmit data register
      _dmasettings[i].TCD->ATTR_DST = 0; // 8-bit destination size (%000)
      _dmasettings[i].replaceSettingsOnCompletion(_dmasettings[(i+1)%12]);
    }
    // "half done" for 12 is at the end of index 5, so we don't have to set up interruptAtHalf()
    // but we do have to change the way we deal with sub-frame counting. If we need it. I don't
    // think we do, so I'm leaving this here as a comment for now...
    // _dmasettings[5].interruptAtCompletion();
    _dmasettings[11].interruptAtCompletion();

    // Not sure we need this...
    //_dmasettings[11].TCD->CSR &= ~(DMA_TCD_CSR_DREQ); // DMA_TCDn_CSR[3] -- If this flag is set, the eDMA hardware automatically clears the corresponding ERQ bit when the current major iteration count reaches zero.

    _dmatx = _dmasettings[0];
    _dmatx.begin(true);
    _dmatx.triggerAtHardwareEvent(dmaTXevent);
    _dmatx.attachInterrupt(dmaInterrupt);
    _dma_state = RA8875_DMA_INIT | RA8875_DMA_EVER_INIT;
  }
}

bool RA8875_t4::updateScreenAsync(bool update_cont)
{
  if (!_pfbtft) return false;

  // Half of main ram has a 32k cache. This tells it to flush the cache if necessary.
  if ((uint32_t)_pfbtft >= 0x20200000u) arm_dcache_flush(_pfbtft, RA8875_WIDTH*RA8875_HEIGHT);

  if (_dma_state & RA8875_DMA_ACTIVE) {
    return false;
  }
  initDMASettings();
  
  // Don't need to reset the window b/c we never change it; but set the X/Y cursor back to the origin
  _writeRegister(RA8875_CURV0, 0);
  _writeRegister(RA8875_CURV0+1, 0);
  _writeRegister(RA8875_CURH0, 0);
  _writeRegister(RA8875_CURH0+1, 0);

  // Start it sending data
  writeCommand(RA8875_MRWC);
  _startSend();
  _pspi->transfer(RA8875_DATAWRITE);

  _spi_fcr_save = _pimxrt_spi->FCR; // FIFO Control Register
  _pimxrt_spi->FCR=0; // turn off FIFO watermarks
 
  // Set transmit command register: disable RX ("mask out RX"), enable
  // TX from FIFO (b/c it's not masked out), and 8-bit data transfers
  // (7+1).
  maybeUpdateTCR(LPSPI_TCR_FRAMESZ(7) | LPSPI_TCR_RXMSK /*| LPSPI_TCR_CONT*/);
  // Set up the DMA Enable Register to enable transmit DMA (and not receive DMA)
  _pimxrt_spi->DER = LPSPI_DER_TDDE;
  _pimxrt_spi->SR &= 0x3f00; // clear status flags RDF and TDF (rx and tx data flags), but leave error flags
  _dmatx.triggerAtHardwareEvent( _spi_hardware->tx_dma_channel );
  _dmatx = _dmasettings[0];
  
  _dma_frame_count = 0;
  _dmaActiveDisplay  = this;
  
  _dmatx.begin(false);
  _dmatx.enable();
  if (update_cont) {
    _dma_state |= RA8875_DMA_CONT;
  } else {
    _dmasettings[11].disableOnCompletion();
    _dma_state &= ~RA8875_DMA_CONT;
  }
  _dma_state |= RA8875_DMA_ACTIVE;
  
  return true;
}

void RA8875_t4::fillWindow(uint16_t color)
{
  // FIXME: reduce color & fill appropriately
  memset(_pfbtft, RA8875_WIDTH*RA8875_HEIGHT, 0);
}

// *** Remove this and convert to native 8-bit? Or make it inline?
uint8_t _color16To8bpp(uint16_t color) {
  return ((color & 0xe000) >> 8) | ((color & 0x700) >> 6) | ((color & 0x18) >> 3);
}
  
void RA8875_t4::drawPixel(int16_t x, int16_t y, uint16_t color)
{
  // FIXME: bounds checking
  _pfbtft[y*RA8875_WIDTH+x] = _color16To8bpp(color);
}

uint32_t RA8875_t4::frameCount()
{
  return _dma_frame_count;
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
  if (_dmaActiveDisplay)
    _dmaActiveDisplay->process_dma_interrupt();
}

void RA8875_t4::process_dma_interrupt(void) {
  _dmatx.clearInterrupt();
 
  _dma_frame_count++;
  if ((_dma_state & RA8875_DMA_CONT) == 0) {
    // Single refresh, or the user canceled, so release the CS pin
    while (_pimxrt_spi->FSR & 0x1f) ; // wait until transfer is done
    while (_pimxrt_spi->SR & LPSPI_SR_MBF) ; // ... and the module is not busy
    _dmatx.clearComplete();
    _pimxrt_spi->FCR = _spi_fcr_save;
    _pimxrt_spi->DER = 0; // turn off tx and rx DMA
    _pimxrt_spi->CR = LPSPI_CR_MEN | LPSPI_CR_RRF | LPSPI_CR_RTF; //RRF: reset receive FIFO; RTF: reset transmit FIFO; MEN: enable module
    _pimxrt_spi->SR &= 0x3f00; // clear status flags RDF and TDF (rx and tx data flags), but leave error flags
    maybeUpdateTCR(LPSPI_TCR_FRAMESZ(7));
    _endSend();
    _dma_state &= ~RA8875_DMA_ACTIVE;
    _dmaActiveDisplay = 0;
  } else {
    // Try to flush memory
    if ((uint32_t)_pfbtft >= 0x20200000u)
      arm_dcache_flush(_pfbtft, RA8875_WIDTH*RA8875_HEIGHT);
  }
  // make sure the code is synchronized - memory access must be
  // complete before we continue
  asm("dsb");
}

// Other possible methods, that I don't think we'll need:
// void RA8875_t4::setFrameCompleteCB(void (*pcb)(), bool fCallAlsoHalfDone)

