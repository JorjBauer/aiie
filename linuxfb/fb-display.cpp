#include <ctype.h> // isgraph
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include "fb-display.h"

#include "bios-font.h"
#include "images.h"

#include "globals.h"
#include "applevm.h"

#include "apple/appleui.h"

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

FBDisplay::FBDisplay()
{
  fb_fd = open("/dev/fb0",O_RDWR);
  //Get variable screen information
  ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);

  printf("bpp: %d\n", vinfo.bits_per_pixel);
  
  //Get fixed screen information
  ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo);

  // This shouldn't be necessary, but I'm not seeing FBIOPUT working yet
  system("fbset -xres 320 -yres 240 -depth 16");
  
  // request what we want, rather than hoping we got it
  // 16bpp 320x240 (for now)
  vinfo.width = 320;
  vinfo.height = 240;
  vinfo.bits_per_pixel = 16;
  int ret = ioctl(fb_fd, FBIOPUT_VSCREENINFO, &vinfo);
  printf("Return from FBIOPUT_VSCREENINFO: %d\n", ret);
  
  ret = ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo);
  printf("Return from FBIOGET_VSCREENINFO: %d\n", ret);
  
  printf("Screen is %d x %d @ %d\n", vinfo.width, vinfo.height, vinfo.bits_per_pixel);
  
  screensize = vinfo.yres_virtual * finfo.line_length;
  fbp = (uint8_t *)mmap(0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, (off_t)0);
}

FBDisplay::~FBDisplay()
{
}

void FBDisplay::redraw()
{
  // primarily for the device, where it's in and out of the
  // bios. Draws the background image.
  printf("redraw background\n");
  g_ui->drawStaticUIElement(UIeOverlay);
  printf("static done\n");
  if (g_vm && g_ui) {
    // determine whether or not a disk is inserted & redraw each drive
    g_ui->drawOnOffUIElement(UIeDisk1_state, ((AppleVM *)g_vm)->DiskName(0)[0] == '\0');
    g_ui->drawOnOffUIElement(UIeDisk2_state, ((AppleVM *)g_vm)->DiskName(1)[0] == '\0');
  }
  printf("return\n");
}

void FBDisplay::drawImageOfSizeAt(const uint8_t *img,
				   uint16_t sizex, uint8_t sizey,
				   uint16_t wherex, uint8_t wherey)
{
  for (uint8_t y=0; y<sizey; y++) {
    for (uint16_t x=0; x<sizex; x++) {
      const uint8_t *p = &img[(y * sizex + x)*3];
      drawPixel(x+wherex, y+wherey, p[0], p[1], p[2]);
    }
  }
}

#define BASEX 18
#define BASEY 13

void FBDisplay::blit(AiieRect r)
{
  uint8_t *videoBuffer = g_vm->videoBuffer; // FIXME: poking deep

  for (uint8_t y=0; y<192; y++) {
    for (uint16_t x=0; x<280; x++) {
      uint16_t pixel = (y*DISPLAYRUN+x)/2;
      uint8_t colorIdx;
      if (x & 1) {
	colorIdx = videoBuffer[pixel] & 0x0F;
      } else {
	colorIdx = videoBuffer[pixel] >> 4;
      }
      long location = (x+vinfo.xoffset+BASEX) * (vinfo.bits_per_pixel/8) + (y+vinfo.yoffset+BASEY) * finfo.line_length;
      *((uint16_t*)(fbp + location)) = loresPixelColors[colorIdx];
    }
  }

  if (overlayMessage[0]) {
    drawString(M_SELECTDISABLED, 1, 240 - 16 - 12, overlayMessage);
  }

}

inline uint16_t _888to565(uint8_t r, uint8_t g, uint8_t b)
{
  return ( (r & 0xF8) << 8 |
	   ( (g & 0xFC) << 3) |
	   ( (b & 0xF8) >> 3 ) );
}

// external method
void FBDisplay::drawPixel(uint16_t x, uint16_t y, uint16_t color)
{
  long location = (x+vinfo.xoffset) * (vinfo.bits_per_pixel/8) + (y+vinfo.yoffset) * finfo.line_length;
  *((uint16_t*)(fbp + location)) = color;
}

// external method
void FBDisplay::drawPixel(uint16_t x, uint16_t y, uint8_t r, uint8_t g, uint8_t b)
{
  long location = (x+vinfo.xoffset) * (vinfo.bits_per_pixel/8) + (y+vinfo.yoffset) * finfo.line_length;
  *((uint16_t*)(fbp + location)) = _888to565(r,g,b);
}

void FBDisplay::drawCharacter(uint8_t mode, uint16_t x, uint8_t y, char c)
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
    uint8_t ch = BiosFont[temp];
    for (int8_t x_off = 0; x_off <= xsize; x_off++) {
      if (ch & (1 << (7-x_off))) {
	drawPixel(x + x_off, y + y_off, onPixel);
      } else {
	drawPixel(x + x_off, y + y_off, offPixel);
      }
    }
    temp++;
  }

}

void FBDisplay::drawString(uint8_t mode, uint16_t x, uint8_t y, const char *str)
{
  int8_t xsize = 8; // width of a char in this font
  
  for (int8_t i=0; i<strlen(str); i++) {
    drawCharacter(mode, x, y, str[i]);
    x += xsize; // fixme: any inter-char spacing?
  }
}

void FBDisplay::flush()
{
}

void FBDisplay::clrScr()
{
  for (uint8_t y=0; y<vinfo.height; y++) {
    for (uint16_t x=0; x<vinfo.width; x++) {
      drawPixel(x, y, 0x0000);
    }
  }
}

