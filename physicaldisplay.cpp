#include "physicaldisplay.h"
#include "globals.h"
#include "applevm.h"
#include "appleui.h"

#include "font.h"

void PhysicalDisplay::drawCharacter(uint8_t mode, uint16_t x, uint16_t y, char c)
{
  int8_t xsize = 8,
    ysize = 0x07;

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
  case M_PLAIN:
    onPixel = 0xFFFF;
    offPixel = 0x0000;
    break;
  }

  const unsigned char *ch = asciiToAppleGlyph(c);
  for (int8_t y_off = 0; y_off <= ysize; y_off++) {
    for (int8_t x_off = 0; x_off <= xsize; x_off++) {
      if (*ch & (1 << (x_off))) {
        drawPixel(x+x_off, y+y_off, onPixel);
      } else {
        drawPixel(x+x_off, y+y_off, offPixel);
      }
    }
    ch++;
  }
}

void PhysicalDisplay::drawString(uint8_t mode, uint16_t x, uint16_t y, const char *str)
{
  int8_t xsize = 8; // width of a char in this font
  
  for (int8_t i=0; i<strlen(str); i++) {
    drawCharacter(mode, x, y, str[i]);
    x += xsize;
    if (x >= (320-xsize)/2) break; // FIXME this is a
                                            // pre-scaled number, b/c
                                            // drawCharacter is
                                            // scaling. Klutzy. It's
                                            // also using the ILI
                                            // constant; what about
                                            // the RA8875?
  }
}

void PhysicalDisplay::redraw()
{
  if (g_ui) {
    g_ui->drawStaticUIElement(UIeOverlay);
    
    if (g_vm) {
      g_ui->drawOnOffUIElement(UIeDisk1_state, ((AppleVM *)g_vm)->DiskName(0)[0] == '\0');
      g_ui->drawOnOffUIElement(UIeDisk2_state, ((AppleVM *)g_vm)->DiskName(1)[0] == '\0');
    }
  }
}
