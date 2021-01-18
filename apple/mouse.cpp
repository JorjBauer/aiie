#include "mouse.h"
#include <string.h>
#include "globals.h"

// This ROM is part of the Aiie source code, but it was compiled and
// bundled as a binary. If you want to see what it's doing, take a
// look at mouserom.asm.
#include "mouse-rom.h"

enum {
  SW_W_INITPR     = 0x00,
  SW_W_HANDLEIN   = 0x01,

  SW_R_ERR        = 0x07,
  
  SW_R_HOMEMOUSE  = 0x08,
  SW_R_POSMOUSE   = 0x09,
  SW_R_CLEARMOUSE = 0x0A,
  SW_R_READMOUSE  = 0x0B,
  SW_R_INITMOUSE  = 0x0C,
  SW_W_CLAMPMOUSE = 0x0D,
  SW_R_SERVEMOUSE = 0x0E,
  SW_W_SETMOUSE   = 0x0F
};

enum {
  ST_MOUSEENABLE   = 0x01,
  ST_INTMOUSE      = 0x02,
  ST_INTBUTTON     = 0x04,
  ST_INTVBL        = 0x08,
  // 0x10 is reserved
  ST_XYCHANGED     = 0x20,
  ST_BUTTONWASDOWN = 0x40,
  ST_BUTTONDOWN    = 0x80
};

Mouse::Mouse()
{
  status = 0;
  interruptsTriggered = 0;
  lastX = lastY = 0;
  lastXForInt = lastYForInt = 0;
  lastButton = false;
  lastButtonForInt = false;
}

Mouse::~Mouse()
{
}

bool Mouse::Serialize(int8_t fd)
{
  return true;
}

bool Mouse::Deserialize(int8_t fd)
{
  return true;
}

void Mouse::Reset()
{
}

void Mouse::performHack()
{
  char buf[20];
  uint16_t xpos, ypos;
  g_mouse->getPosition(&xpos, &ypos);
  bool curButton = g_mouse->getButton();
  static bool prevButton = false;
  bool isKeyPressed = ((AppleMMU *)(g_vm->getMMU()))->readDirect(0xC010, 0) & 0x80 ? true : false;
  uint8_t buttonPressData = (curButton ? 1 : 3) + (prevButton ? 0 : 1);
  sprintf(buf, "%d,%d,%s%d\015", xpos, ypos,
	  isKeyPressed ? "-" : "",
	  buttonPressData);
  for (int i=0; i<strlen(buf); i++) {
    g_vm->getMMU()->write(0x200 + i, buf[i] | 0x80);
  }

  // Put the string length in 0x638+$c0+n == $6FC for slot #4.
  // The caller will pick that up and return it as the length of the buffer,
  // which that caller will interpret as the next byte it has to display to
  // the screen in the GETLN call; and since it's a return, it will display
  // *nothing* and start parsing the string immediately.
  g_vm->getMMU()->write(0x6fc, strlen(buf)-1);
}

uint8_t Mouse::readSwitches(uint8_t s)
{
  return 0xFF;
}

void Mouse::writeSwitches(uint8_t s, uint8_t v)
{
  switch (s) {
    /* Many of these were designed to be reads, because they don't have to 
     * modify any state inside the VM directly -- but it's important (per docs)
     * that we return A, X, and Y as they were when we were called. So these
     * are all now writes, which don't modify A/X/Y. */

  case SW_W_INITPR:
    v &= 0x01;
    //printf("Simple init: value is 0x%X\n", v);
    g_vm->getMMU()->write(0x7f8 + 4, v);
    break;
    
  case SW_W_HANDLEIN:
    performHack();    
    break;

  case SW_R_HOMEMOUSE:
    g_mouse->setPosition( (g_vm->getMMU()->read(0x578) << 8) | g_vm->getMMU()->read(0x478),
			  (g_vm->getMMU()->read(0x5F8) << 8) | g_vm->getMMU()->read(0x4F8)
			  );
    break;
  case SW_R_POSMOUSE:
    g_mouse->setPosition( (g_vm->getMMU()->read(0x578+4) << 8) | g_vm->getMMU()->read(0x478+4),
			  (g_vm->getMMU()->read(0x5F8+4) << 8) | g_vm->getMMU()->read(0x4F8+4)
			  );
    break;
  case SW_R_CLEARMOUSE:
    g_vm->getMMU()->write(0x578+4, 0);
    g_vm->getMMU()->write(0x478+4, 0);
    g_vm->getMMU()->write(0x5F8+4, 0);
    g_vm->getMMU()->write(0x4F8+4, 0);
    g_mouse->setPosition(0,0);
    break;
  case SW_R_READMOUSE:
    {
      uint16_t xpos, ypos;
      g_mouse->getPosition(&xpos, &ypos);
      curButton = g_mouse->getButton();
      uint8_t newStatus = g_vm->getMMU()->read(0x778+4) & ~0xC7; // clears low 3 bits per docs
      if (curButton) { newStatus |= ST_BUTTONDOWN; };
      if (lastButton) { newStatus |= ST_BUTTONWASDOWN; };
      lastButton = curButton;
      if (lastX != xpos || lastY != ypos) {
	newStatus |= ST_XYCHANGED;
	lastX = xpos; lastY = ypos;
      }
      
      MMU *m = g_vm->getMMU();
      m->write(0x578+4, (xpos >> 8) & 0xFF); // high X
      m->write(0x478+4, xpos & 0xFF); // low X
      m->write(0x5F8+4, (ypos >> 8) & 0xFF); // high Y
      m->write(0x4F8+4, ypos); // low Y
      m->write(0x778+4, newStatus);
    }
    break;
  case SW_R_INITMOUSE:
    // Set clamp to (0,0) - (1023,1023)
    g_vm->getMMU()->write(0x578, 0); // high of lowclamp
    g_vm->getMMU()->write(0x478, 0); // low of lowclamp
    g_vm->getMMU()->write(0x5F8, 0x03); // high of highclamp
    g_vm->getMMU()->write(0x4F8, 0xFF); // low of highclamp
    g_mouse->setClamp(XCLAMP, 0, 1023);
    g_mouse->setClamp(YCLAMP, 0, 1023);
    break;
  case SW_R_SERVEMOUSE:
    g_vm->getMMU()->write(0x778+4, interruptsTriggered);
    g_vm->getMMU()->write(0x6B8+4, interruptsTriggered); // hack to appease ROM
    interruptsTriggered = 0;
    g_cpu->deassertIrq();
    break;
  case SW_W_CLAMPMOUSE:
    {
      uint16_t lowval = (g_vm->getMMU()->read(0x578) << 8) | (g_vm->getMMU()->read(0x478));
      uint16_t highval = (g_vm->getMMU()->read(0x5F8) << 8) | (g_vm->getMMU()->read(0x4F8));

      // Fix for Blazing Paddles, which requests a negative lowval,
      // but we're casting them as unsigned
      if (lowval > highval) {
        highval = (lowval + highval) & 0xFFFF;
        lowval = 0;
      }
      
      if (v) {
	g_mouse->setClamp(YCLAMP, lowval, highval);
      } else {
	// X is clamping
	g_mouse->setClamp(XCLAMP, lowval, highval);
      }
    }
    break;
  case SW_W_SETMOUSE:
    status = v;
    g_vm->getMMU()->write(0x7f8 + 4, v);
    break;
  default:
    printf("mouse: unknown switch write 0x%X = 0x%2X\n", s, v);
    break;
  }
}

void Mouse::loadROM(uint8_t *toWhere)
{
#ifdef TEENSYDUINO
  Serial.println("loading Mouse rom");
  for (uint16_t i=0; i<=0xFF; i++) {
    toWhere[i] = pgm_read_byte(&romData[i]);
  }
#else
  printf("loading Mouse rom\n");
  memcpy(toWhere, romData, 256);
#endif
}

bool Mouse::hasExtendedRom()
{
  return false;
}

void Mouse::loadExtendedRom(uint8_t *toWhere, uint16_t byteOffset)
{
}

void Mouse::maintainMouse(int64_t cycleCount)
{
  // Fake a 60Hz VBL in case we need it for our interrupts
  static int64_t nextInterruptTime = cycleCount + 17050;
  if ( (status & ST_MOUSEENABLE) &&
       (status & ST_INTVBL)  &&
       (cycleCount >= nextInterruptTime) ) {
    g_cpu->assertIrq();
    
    interruptsTriggered |= ST_INTVBL;
    
    nextInterruptTime += 17050;
  } else {
    uint16_t xpos, ypos;
    g_mouse->getPosition(&xpos, &ypos);

    if ( (status & ST_MOUSEENABLE) &&
	 (status & ST_INTMOUSE) &&
	 (xpos != lastXForInt || ypos != lastYForInt) ) {
      g_cpu->assertIrq();
      
      interruptsTriggered |= ST_INTMOUSE;      
      lastXForInt = xpos; lastYForInt = ypos;
    } else if ( (status & ST_MOUSEENABLE) &&
		(status & ST_INTBUTTON) &&
		lastButtonForInt != g_mouse->getButton()) {
      g_cpu->assertIrq();

      interruptsTriggered |= ST_INTBUTTON;
      lastButtonForInt = g_mouse->getButton();
    }
  }
}

bool Mouse::isEnabled()
{
  return status & ST_MOUSEENABLE;
}
