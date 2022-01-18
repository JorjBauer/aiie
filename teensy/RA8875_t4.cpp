#include "RA8875_t4.h"
#include "RA8875_registers.h"

RA8875_t4::RA8875_t4(const uint8_t cs_pin, const uint8_t rst_pin, const uint8_t mosi_pin, const uint8_t sck_pin, const uint8_t miso_pin)
{
  _mosi = mosi_pin;
  _miso = miso_pin;
  _cs = cs_pin;
  _rst = rst_pin;
  _sck = sck_pin;
  _interruptStates = 0b00000000;

  _pspi = NULL;
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
  digitalWrite(_cs, HIGH);

  
  /*
  pending_rx_count = 0; // Make sure it is zero if we we do a second begin...                                                                              
  _spi_tcr_current = _pimxrt_spi->TCR; // get the current TCR value                                                                                    
  
  // TODO:  Need to setup DC to actually work.                                                                                                         
  if (_pspi->pinIsChipSelect(_dc)) {
    uint8_t dc_cs_index = _pspi->setCS(_dc);
    _dcport = 0;
    _dcpinmask = 0;
    // will depend on which PCS but first get this to work...                                                                                    
    dc_cs_index--;  // convert to 0 based                                                                                                        
    _tcr_dc_assert = LPSPI_TCR_PCS(dc_cs_index);
    _tcr_dc_not_assert = LPSPI_TCR_PCS(3);
  } else {
    _dcport = portOutputRegister(_dc);
    _dcpinmask = digitalPinToBitMask(_dc);
    pinMode(_dc, OUTPUT);
    DIRECT_WRITE_HIGH(_dcport, _dcpinmask);
    _tcr_dc_assert = LPSPI_TCR_PCS(0);
    _tcr_dc_not_assert = LPSPI_TCR_PCS(1);
  }
  maybeUpdateTCR(_tcr_dc_not_assert | LPSPI_TCR_FRAMESZ(7));
  */
  
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
  _writeRegister(RA8875_SYSR, 0x0C); // 65k

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
  
  // Set up the gpio
  _writeRegister(RA8875_GPIOX, true);
}

void RA8875_t4::setFrameBuffer(uint16_t *frame_buffer)
{
}

bool RA8875_t4::asyncUpdateActive()
{
  return false;
}

bool RA8875_t4::updateScreenAsync(bool update_cont)
{
  return false;
}

void RA8875_t4::fillWindow(uint16_t color)
{
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
  writeData16(color);
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
  _startSend();
  _pspi->transfer(stat ? RA8875_CMDREAD : RA8875_DATAREAD);
  _pspi->transfer(0x00);
  _endSend();
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
