#include <ctype.h> // isgraph
#include "teensy-display.h"

#include "bios-font.h"
#include "appleui.h"

#define RS 16
#define WR 17
#define CS 18
#define RST 19

// Ports C&D of the Teensy connected to DB of the display
#define DB_0 15
#define DB_1 22
#define DB_2 23
#define DB_3 9
#define DB_4 10
#define DB_5 13
#define DB_6 11
#define DB_7 12
#define DB_8 2
#define DB_9 14
#define DB_10 7
#define DB_11 8
#define DB_12 6
#define DB_13 20
#define DB_14 21
#define DB_15 5

#define disp_x_size 239
#define disp_y_size 319

#define setPixel(color) { LCD_Write_DATA(((color)>>8),((color)&0xFF)); } // 565 RGB

#include "globals.h"
#include "applevm.h"

// RGB map of each of the lowres colors
const uint16_t loresPixelColors[16] = { 0x0000, // 0 black
					0xC006, // 1 magenta
					0x0010, // 2 dark blue
					0xA1B5, // 3 purple
					0x0480, // 4 dark green
					0x6B4D, // 5 dark grey
					0x1B9F, // 6 med blue
					0x0DFD, // 7 light blue
					0x92A5, // 8 brown
					0xF8C5, // 9 orange
					0x9555, // 10 light gray
					0xFCF2, // 11 pink
					0x07E0, // 12 green
					0xFFE0, // 13 yellow
					0x87F0, // 14 aqua
					0xFFFF  // 15 white
};

TeensyDisplay::TeensyDisplay()
{
  pinMode(DB_8, OUTPUT);
  pinMode(DB_9, OUTPUT);
  pinMode(DB_10, OUTPUT);
  pinMode(DB_11, OUTPUT);
  pinMode(DB_12, OUTPUT);
  pinMode(DB_13, OUTPUT);
  pinMode(DB_14, OUTPUT);
  pinMode(DB_15, OUTPUT);
  pinMode(DB_0, OUTPUT);
  pinMode(DB_1, OUTPUT);
  pinMode(DB_2, OUTPUT);
  pinMode(DB_3, OUTPUT);
  pinMode(DB_4, OUTPUT);
  pinMode(DB_5, OUTPUT);
  pinMode(DB_6, OUTPUT);
  pinMode(DB_7, OUTPUT);

  P_RS    = portOutputRegister(digitalPinToPort(RS));
  B_RS    = digitalPinToBitMask(RS);
  P_WR    = portOutputRegister(digitalPinToPort(WR));
  B_WR    = digitalPinToBitMask(WR);
  P_CS    = portOutputRegister(digitalPinToPort(CS));
  B_CS    = digitalPinToBitMask(CS);
  P_RST   = portOutputRegister(digitalPinToPort(RST));
  B_RST   = digitalPinToBitMask(RST);

  pinMode(RS,OUTPUT);
  pinMode(WR,OUTPUT);
  pinMode(CS,OUTPUT);
  pinMode(RST,OUTPUT);

  // begin initialization

  sbi(P_RST, B_RST);
  delay(5);
  cbi(P_RST, B_RST);
  delay(15);
  sbi(P_RST, B_RST);
  delay(15);

  cbi(P_CS, B_CS);

  LCD_Write_COM_DATA(0x00,0x0001); // oscillator start [enable]
  LCD_Write_COM_DATA(0x03,0xA8A4); // power control [%1010 1000 1010 1000] == DCT3, DCT1, BT2, DC3, DC1, AP2
  LCD_Write_COM_DATA(0x0C,0x0000); // power control2 [0]
  LCD_Write_COM_DATA(0x0D,0x080C); // power control3 [VRH3, VRH2, invalid bits]
  LCD_Write_COM_DATA(0x0E,0x2B00); // power control4 VCOMG, VDV3, VDV1, VDV0
  LCD_Write_COM_DATA(0x1E,0x00B7); // power control5 nOTP, VCM5, VCM4, VCM2, VCM1, VCM0
  //  LCD_Write_COM_DATA(0x01,0x2B3F); // driver control output REV, BGR, TB, MUX8, MUX5, MUX4, MUX3, MUX2, MUX1, MUX0
  // Change that: TB = 0 please
  LCD_Write_COM_DATA(0x01,0x293F); // driver control output REV, BGR, TB, MUX8, MUX5, MUX4, MUX3, MUX2, MUX1, MUX0
  //  LCD_Write_COM_DATA(0x01,0x693F); // driver control output RL, REV, BGR, TB, MUX8, MUX5, MUX4, MUX3, MUX2, MUX1, MUX0


  LCD_Write_COM_DATA(0x02,0x0600); // LCD drive AC control B/C, EOR                                                        
  LCD_Write_COM_DATA(0x10,0x0000); // sleep mode 0                                                                         
  // Change the (Y) order here to match above (TB=0)
  //LCD_Write_COM_DATA(0x11,0x6070); // Entry mode DFM1, DFM0, TY0, ID1, ID0
  //LCD_Write_COM_DATA(0x11,0x6050); // Entry mode DFM1, DFM0, TY0, ID0
  LCD_Write_COM_DATA(0x11,0x6078); // Entry mode DFM1, DFM0, TY0, ID1, ID0, AM

  LCD_Write_COM_DATA(0x05,0x0000); // compare reg1                                                                         
  LCD_Write_COM_DATA(0x06,0x0000); // compare reg2                                                                         
  LCD_Write_COM_DATA(0x16,0xEF1C); // horiz porch (default)                                                                
  LCD_Write_COM_DATA(0x17,0x0003); // vertical porch                                                                       
  LCD_Write_COM_DATA(0x07,0x0233); // display control VLE1, GON, DTE, D1, D0                                               
  LCD_Write_COM_DATA(0x0B,0x0000); // frame cycle control                                                                  
  LCD_Write_COM_DATA(0x0F,0x0000); // gate scan start posn                                                                 
  LCD_Write_COM_DATA(0x41,0x0000); // vertical scroll control1                                                             
  LCD_Write_COM_DATA(0x42,0x0000); // vertical scroll control2                                                             
  LCD_Write_COM_DATA(0x48,0x0000); // first window start                                                                   
  LCD_Write_COM_DATA(0x49,0x013F); // first window end (0x13f == 319)                                                      
  LCD_Write_COM_DATA(0x4A,0x0000); // second window start                                                                  
  LCD_Write_COM_DATA(0x4B,0x0000); // second window end                                                                    
  LCD_Write_COM_DATA(0x44,0xEF00); // horiz ram addr posn                                                                  
  LCD_Write_COM_DATA(0x45,0x0000); // vert ram start posn                                                                  
  LCD_Write_COM_DATA(0x46,0x013F); // vert ram end posn                                                                    
  LCD_Write_COM_DATA(0x30,0x0707); // γ control                                                                            
  LCD_Write_COM_DATA(0x31,0x0204);//                                                                                       
  LCD_Write_COM_DATA(0x32,0x0204);//                                                                                       
  LCD_Write_COM_DATA(0x33,0x0502);//                                                                                       
  LCD_Write_COM_DATA(0x34,0x0507);//                                                                                       
  LCD_Write_COM_DATA(0x35,0x0204);//                                                                                       
  LCD_Write_COM_DATA(0x36,0x0204);//                                                                                       
  LCD_Write_COM_DATA(0x37,0x0502);//                                                                                       
  LCD_Write_COM_DATA(0x3A,0x0302);//                                                                                       
  LCD_Write_COM_DATA(0x3B,0x0302);//                                                                                       
  LCD_Write_COM_DATA(0x23,0x0000);// RAM write data mask1                                                                  
  LCD_Write_COM_DATA(0x24,0x0000); // RAM write data mask2                                                                 
  LCD_Write_COM_DATA(0x25,0x8000); // frame frequency (OSC3)                                                               
  LCD_Write_COM_DATA(0x4f,0x0000); // Set GDDRAM Y address counter                                                         
  LCD_Write_COM_DATA(0x4e,0x0000); // Set GDDRAM X address counter                                                         
#if 0
  // Set data access speed optimization (?)
  LCD_Write_COM_DATA(0x28, 0x0006);
  LCD_Write_COM_DATA(0x2F, 0x12BE);
  LCD_Write_COM_DATA(0x12, 0x6CEB);
#endif

  LCD_Write_COM(0x22);   // RAM data write                                                                                 
  sbi(P_CS, B_CS);

  // LCD initialization complete

  setColor(255, 255, 255);

  clrScr();

  driveIndicator[0] = driveIndicator[1] = false;
  driveIndicatorDirty = true;
}

TeensyDisplay::~TeensyDisplay()
{
}

void TeensyDisplay::_fast_fill_16(int ch, int cl, long pix)
{
  *(volatile uint8_t *)(&GPIOD_PDOR) = ch;
  *(volatile uint8_t *)(&GPIOC_PDOR) = cl;
  uint16_t blocks = pix / 16;

  for (uint16_t i=0; i<blocks; i++) {
    pulse_low(P_WR, B_WR);
    pulse_low(P_WR, B_WR);
    pulse_low(P_WR, B_WR);
    pulse_low(P_WR, B_WR);
    pulse_low(P_WR, B_WR);
    pulse_low(P_WR, B_WR);
    pulse_low(P_WR, B_WR);
    pulse_low(P_WR, B_WR);
    pulse_low(P_WR, B_WR);
    pulse_low(P_WR, B_WR);
    pulse_low(P_WR, B_WR);
    pulse_low(P_WR, B_WR);
    pulse_low(P_WR, B_WR);
    pulse_low(P_WR, B_WR);
    pulse_low(P_WR, B_WR);
    pulse_low(P_WR, B_WR);
  }
  if ((pix % 16) != 0) {
    for (int i=0; i<(pix % 16); i++)
      {
	pulse_low(P_WR, B_WR);
      }
  }
}

void TeensyDisplay::redraw()
{
  cbi(P_CS, B_CS);
  clrXY();
  sbi(P_RS, B_RS);

  moveTo(0, 0);

  g_ui->drawStaticUIElement(UIeOverlay);

  if (g_vm) {
    g_ui->drawOnOffUIElement(UIeDisk1_state, ((AppleVM *)g_vm)->DiskName(0)[0] == '\0');
    g_ui->drawOnOffUIElement(UIeDisk2_state, ((AppleVM *)g_vm)->DiskName(1)[0] == '\0');
  }

  cbi(P_CS, B_CS);
  clrXY();
  sbi(P_RS, B_RS);
}

void TeensyDisplay::clrScr()
{
  cbi(P_CS, B_CS);
  clrXY();
  sbi(P_RS, B_RS);
  _fast_fill_16(0, 0, ((disp_x_size+1)*(disp_y_size+1)));
  sbi(P_CS, B_CS);
}

// The display flips X and Y, so expect to see "x" as "vertical"
// and "y" as "horizontal" here...
void TeensyDisplay::setYX(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
 LCD_Write_COM_DATA(0x44, (y2<<8)+y1); // Horiz start addr, Horiz end addr
 LCD_Write_COM_DATA(0x45, x1); // vert start pos
 LCD_Write_COM_DATA(0x46, x2); // vert end pos
 LCD_Write_COM_DATA(0x4e,y1); // RAM address set (horiz) 
 LCD_Write_COM_DATA(0x4f,x1); // RAM address set (vert)
 LCD_Write_COM(0x22);
}

void TeensyDisplay::clrXY()
{
  setYX(0, 0, disp_y_size, disp_x_size);
}

void TeensyDisplay::setColor(byte r, byte g, byte b)
{
  fch=((r&248)|g>>5);
  fcl=((g&28)<<3|b>>3);
}

void TeensyDisplay::setColor(uint16_t color)
{
  fch = (uint8_t)(color >> 8);
  fcl = (uint8_t)(color & 0xFF);
}

void TeensyDisplay::fillRect(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
  if (x1>x2) {
    swap(uint16_t, x1, x2);
  }
  if (y1 > y2) {
    swap(uint16_t, y1, y2);
  }

  cbi(P_CS, B_CS);
  setYX(x1, y1, x2, y2);
  sbi(P_RS, B_RS);
  _fast_fill_16(fch,fcl,((long(x2-x1)+1)*(long(y2-y1)+1)));
  sbi(P_CS, B_CS);
}

void TeensyDisplay::drawPixel(uint16_t x, uint16_t y)
{
  cbi(P_CS, B_CS);
  setYX(x, y, x, y);
  setPixel((fch<<8)|fcl);
  sbi(P_CS, B_CS);
  clrXY();
}

void TeensyDisplay::drawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  cbi(P_CS, B_CS);
  setYX(x, y, x, y);
  setPixel(color);
  sbi(P_CS, B_CS);
  clrXY();
}

void TeensyDisplay::drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
  uint16_t color16 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);

  cbi(P_CS, B_CS);
  setYX(x, y, x, y);
  setPixel(color16);
  sbi(P_CS, B_CS);
  clrXY();
}

void TeensyDisplay::LCD_Writ_Bus(uint8_t ch, uint8_t cl)
{
  *(volatile uint8_t *)(&GPIOD_PDOR) = ch;
  *(volatile uint8_t *)(&GPIOC_PDOR) = cl;
  pulse_low(P_WR, B_WR);
}

void TeensyDisplay::LCD_Write_COM(uint8_t VL)
{
  cbi(P_RS, B_RS);
  LCD_Writ_Bus(0x00, VL);
}

void TeensyDisplay::LCD_Write_DATA(uint8_t VH, uint8_t VL)
{
  sbi(P_RS, B_RS);
  LCD_Writ_Bus(VH,VL);
}

void TeensyDisplay::LCD_Write_DATA(uint8_t VL)
{
  sbi(P_RS, B_RS);
  LCD_Writ_Bus(0x00, VL);
}

void TeensyDisplay::LCD_Write_COM_DATA(uint8_t com1, uint16_t dat1)
{
  LCD_Write_COM(com1);
  LCD_Write_DATA(dat1>>8, dat1);
}

void TeensyDisplay::moveTo(uint16_t col, uint16_t row)
{
  cbi(P_CS, B_CS);

  // FIXME: eventually set drawing to the whole screen and leave it that way

  // set drawing to the whole screen
  //  setYX(0, 0, disp_y_size, disp_x_size);
  LCD_Write_COM_DATA(0x4e,row); // RAM address set (horiz) 
  LCD_Write_COM_DATA(0x4f,col); // RAM address set (vert)

  LCD_Write_COM(0x22);
}

void TeensyDisplay::drawNextPixel(uint16_t color)
{
  // Anything inside this object should call setPixel directly. This
  // is primarily for the BIOS.
  setPixel(color);
}

void TeensyDisplay::blit(AiieRect r)
{
  uint8_t *videoBuffer = g_vm->videoBuffer; // FIXME: poking deep

  // remember these are "starts at pixel number" values, where 0 is the first.
  #define HOFFSET 18
  #define VOFFSET 13

  // Define the horizontal area that we're going to draw in
  LCD_Write_COM_DATA(0x45, HOFFSET+r.left); // offset by 20 to center it...
  LCD_Write_COM_DATA(0x46, HOFFSET+r.right);

  // position the "write" address
  LCD_Write_COM_DATA(0x4e,VOFFSET+r.top); // row
  LCD_Write_COM_DATA(0x4f,HOFFSET+r.left); // col

  // prepare the LCD to receive data bytes for its RAM
  LCD_Write_COM(0x22);

  // send the pixel data
  sbi(P_RS, B_RS);
  uint16_t pixel;
  for (uint8_t y=r.top; y<=r.bottom; y++) {
    for (uint16_t x=r.left; x<=r.right; x++) {
      pixel = y * (DISPLAYRUN >> 1) + (x >> 1);
      uint8_t colorIdx;
      if (!(x & 0x01)) {
	colorIdx = videoBuffer[pixel] >> 4;
      } else {
	colorIdx = videoBuffer[pixel] & 0x0F;
      }
      LCD_Writ_Bus(loresPixelColors[colorIdx]>>8,loresPixelColors[colorIdx]);
    }
  }
  cbi(P_CS, B_CS);

  // draw overlay, if any
  if (overlayMessage[0]) {
    // reset the viewport in order to draw the overlay...
    LCD_Write_COM_DATA(0x45, 0);
    LCD_Write_COM_DATA(0x46, 319);
  
    drawString(M_SELECTDISABLED, 1, 240 - 16 - 12, overlayMessage);
  }
}

void TeensyDisplay::drawCharacter(uint8_t mode, uint16_t x, uint8_t y, char c)
{
  int8_t xsize = 8,
    ysize = 0x0C,
    offset = 0x20;
  uint16_t temp;

  c -= offset;// font starts with a space

  uint16_t offPixel, onPixel;
  switch (mode) {
  case M_NORMAL:
    onPixel = 0xFFFF;
    offPixel = 0x0010;
    break;
  case M_SELECTED:
    onPixel = 0x0000;
    offPixel = 0xFFFF;
    break;
  case M_DISABLED:
  default:
    onPixel = 0x7BEF;
    offPixel = 0x0000;
    break;
  case M_SELECTDISABLED:
    onPixel = 0x7BEF;
    offPixel = 0xFFE0;
    break;
  }

  temp=(c*ysize);
  for (int8_t y_off = 0; y_off <= ysize; y_off++) {
    moveTo(x, y + y_off);
    uint8_t ch = pgm_read_byte(&BiosFont[temp]);
    for (int8_t x_off = 0; x_off <= xsize; x_off++) {
      if (ch & (1 << (7-x_off))) {
	setPixel(onPixel);
      } else {
	setPixel(offPixel);
      }
    }
    temp++;
  }
}

void TeensyDisplay::drawString(uint8_t mode, uint16_t x, uint8_t y, const char *str)
{
  int8_t xsize = 8; // width of a char in this font

  for (int8_t i=0; i<strlen(str); i++) {
    drawCharacter(mode, x, y, str[i]);
    x += xsize; // fixme: any inter-char spacing?
  }
}

void TeensyDisplay::drawImageOfSizeAt(const uint8_t *img, 
				      uint16_t sizex, uint8_t sizey, 
				      uint16_t wherex, uint8_t wherey)
{
  uint8_t r, g, b;

  if (sizex == DISPLAYWIDTH) {
    moveTo(0,0);
  }

  for (uint8_t y=0; y<sizey; y++) {
    if (sizex != DISPLAYWIDTH) {
      moveTo(wherex, wherey + y);
    }
    for (uint16_t x=0; x<sizex; x++) {
      r = pgm_read_byte(&img[(y*sizex + x)*3 + 0]);
      g = pgm_read_byte(&img[(y*sizex + x)*3 + 1]);
      b = pgm_read_byte(&img[(y*sizex + x)*3 + 2]);
      setPixel((((r&248)|g>>5) << 8) | ((g&28)<<3|b>>3));
    }
  }
}
