#include "mouse.h"
#include <string.h>
#include "globals.h"

enum {
  SW_W_INIT       = 0x00,
  SW_R_HOMEMOUSE  = 0x08,
  SW_R_POSMOUSE   = 0x09,
  SW_R_CLEARMOUSE = 0x0A,
  SW_R_READMOUSE  = 0x0B,
  SW_R_INITMOUSE  = 0x0C,
  SW_W_CLAMPMOUSE = 0x0D,
  SW_R_SERVEMOUSE = 0x0E,
  SW_W_SETMOUSE   = 0x0F
};

// The first 3 soft switch bits technically should pass directly to
// the PIA6821.  In practice, so far, I've only seen the first (SW_W_INIT)
// used when PR#4 is invoked to initialize the mouse interface from DOS,
// so for the moment I'll just catch that specifically.

enum {
  ST_MOUSEENABLE = 1,
  ST_INTMOUSE    = 2,
  ST_INTBUTTON   = 4,
  ST_INTVBL      = 8
};

Mouse::Mouse()
{
  status = 0;
  interruptsTriggered = 0;
  lastX = lastY = 0;
  lastButton = false;
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

uint8_t Mouse::readSwitches(uint8_t s)
{
  switch (s) {
  default:
    printf("mouse: unknown switch read 0x%X\n", s);
  };
  return 0xFF;
}

void Mouse::writeSwitches(uint8_t s, uint8_t v)
{
  switch (s) {
    /* Many of these were designed to be reads, because they don't have to 
     * modify any state inside the VM directly -- but it's important (per docs)
     * that we return A, X, and Y as they were when we were called. So these
     * are all now writes, which don't modify A/X/Y. */
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
      if (lastX != xpos || lastY != ypos) {
	interruptsTriggered |= 0x20; // "x or y changed since last reading"
	lastX = xpos; lastY = ypos;
      }
      curButton = g_mouse->getButton();
      uint8_t newStatus = g_vm->getMMU()->read(0x778+4) & ~0xC0;
      if (curButton) { newStatus |= 0x80; };
      if (lastButton) { newStatus |= 0x40; };

      g_vm->getMMU()->write(0x578+4, (xpos >> 8) & 0xFF); // high X
      g_vm->getMMU()->write(0x478+4, xpos & 0xFF); // low X
      g_vm->getMMU()->write(0x5F8+4, (ypos >> 8) & 0xFF); // high Y
      g_vm->getMMU()->write(0x4F8+4, ypos); // low Y
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
    if (lastButton) interruptsTriggered |= 0x40;
    if (curButton != lastButton) {
      interruptsTriggered |= 0x80;
      lastButton = curButton;
    }
    g_vm->getMMU()->write(0x778+4, interruptsTriggered);
    g_vm->getMMU()->write(0x6B8+4, interruptsTriggered); // hack to appease ROM
    interruptsTriggered = 0;
    break;
  case SW_W_INIT:
    v &= 0x03; // just the low 3 bits apparently?
    printf("Simple init: value is 0x%X\n", v);
    status = v;
    g_vm->getMMU()->write(0x7f8 + 4, v);
    break;
  case SW_W_CLAMPMOUSE:
    {
      uint16_t lowval = (g_vm->getMMU()->read(0x578) << 8) | (g_vm->getMMU()->read(0x478));
      uint16_t highval = (g_vm->getMMU()->read(0x5F8) << 8) | (g_vm->getMMU()->read(0x4F8));
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
  /* This is a custom-built ROM which hands off control to the C++ code via
   * soft switch writes. It's hard-coded to work with the mouse in Slot 4.
   *
   *    ; $c400 is the entry point for PR#4
   * $C400 2C 58 FF   BIT    $FF58
   * $C403    70 1B   BVS    $C420
   *    ; $c405 is the entry point for IN#4 when we set KSW
   * $C405       38   SEC    
   * $C406    90 18   BCC    $C420
   * $C408       B8   CLV    
   * $C409    50 15   BVC    $C420 ; always branch
   * $C40B    01 20 8D 8D 8D       ; data (ID bytes & such)
   * $C410 8D 00 60 68 76 7B 80 85 ; data (lookup table of entrypoints)
   * $C418 8F 94 8D 8D 8D 8D 8D 8D ; data (lookup table of entrypoints)
   *   ; $c420 is the I/O handler
   * $C420       48   PHA
   * $C421       98   TYA    
   * $C422       48   PHA    
   * $C423       8A   TXA    
   * $C424       48   PHA    
   * $C425       08   PHP    
   * $C426       78   SEI    
   * $C427 20 58 FF   JSR    $FF58
   * $C42A       BA   TSX    
   * $C42B BD 00 01   LDA    $100,X
   * $C42E       AA   TAX    
   * $C42F       0A   ASL    
   * $C430       0A   ASL    
   * $C431       0A   ASL    
   * $C432       0A   ASL    
   * $C433       A8   TAY    
   * $C434       28   PLP    
   * $C435    50 0F   BVC    $C446
   * $C437    A5 38   LDA    $0
   * $C439    D0 0D   BNE    $C448
   * $C43B       8A   TXA    
   * $C43C    45 39   EOR    $0
   * $C43E    D0 08   BNE    $C448
   * $C440    A9 05   LDA    #$0
   * $C442    85 38   STA    $0
   * $C444    D0 0B   BNE    $C451
   * $C446    B0 09   BCS    $C451
   * $C448       68   PLA    
   * $C449       AA   TAX    
   * $C44A       68   PLA    
   * $C44B       EA   NOP    
   * $C44C       68   PLA    
   * $C44D       EA   NOP    
   * $C44E       EA   NOP    
   * $C44F       EA   NOP    
   * $C450       60   RTS    
   * $C451       EA   NOP    
   * $C452       EA   NOP    
   * $C453       EA   NOP    
   * $C454       68   PLA    
   * $C455 BD 38 06   LDA    $638,X
   * $C458       AA   TAX    
   * $C459       68   PLA    
   * $C45A       A8   TAY    
   * $C45B       68   PLA    
   * $C45C BD 00 02   LDA    $200,X
   * $C45F       60   RTS    
   *   ; $c460 is SetMouse
   * $C460    C9 10   CMP    #$0
   * $C462    B0 29   BCS    $C48D
   * $C464 8D CF C0   STA    $C0CF
   * $C467       60   RTS    
   *   ; $c468 is ServeMouse
   * $C468       48   PHA    
   * $C469       18   CLC    
   * $C46A    90 2D   BCC    $C499   ; jump to ServeMouseWorker
   *   ; $c46c is ServeMouseExit
   * $C46C BD B8 06   LDA    $6B8,X
   * $C46F    29 0E   AND    #$0
   * $C471    D0 01   BNE    $C474
   * $C473       38   SEC    
   * $C474       68   PLA    
   * $C475       60   RTS    
   *   ; $c476 is ReadMouse
   * $C476 8D CB C0   STA    $C0CB
   * $C479       18   CLC    
   * $C47A       60   RTS    
   *   ; $c47b is ClearMouse
   * $C47B 8D CA C0   STA    $C0CA
   * $C47E       18   CLC    
   * $C47F       60   RTS   
   *   ; $c480 is PosMouse 
   * $C480 8D C9 C0   STA    $C0C9
   * $C483       18   CLC    
   * $C484       60   RTS    
   *   ; $c485 is ClampMouse
   * $C485    C9 02   CMP    #$0
   * $C487    B0 04   BCS    $C48D
   * $C489 8D CD C0   STA    $C0CD
   * $C48C       60   RTS    
   *   ; $c48d is an error exit point
   * $C48D       38   SEC    
   * $C48E       60   RTS    
   *   ; $c48f is HomeMouse
   * $C48F 8D C8 C0   STA    $C0C8
   * $C492       18   CLC    
   * $C493       60   RTS    
   *   ; $c494 is InitMouse
   * $C494 8D CC C0   STA    $C0CC
   * $C497       18   CLC    
   * $C498       60   RTS    
   *   ; $c499 is ServeMouse
   * $C499       78   SEI    
   * $C49A 8D CE C0   STA    $C0CE
   * $C49D    A2 04   LDX    #$0
   * $C49F       18   CLC    
   * $C4A0    90 CA   BCC    $C46C
   *   ; $C4A2..C4FA is dead space (all $FF)
   * $C4FB D6 FF FF FF 01   ; data (ID bytes)
   */
  
  uint8_t rom[256] = { 0x2c, 0x58, 0xff, 0x70, 0x1B, 0x38, 0x90, 0x18,  // C400
		       0xb8, 0x50, 0x15, 0x01, 0x20, 0x8d, 0x8d, 0x8d,
		       0x8d, 0x00, 0x60, 0x68, 0x76, 0x7b, 0x80, 0x85,  // C410
		       0x8f, 0x94, 0x8d, 0x8d, 0x8d, 0x8d, 0x8d, 0x8d,
		       0x48, 0x98, 0x48, 0x8a, 0x48, 0x08, 0x78, 0x20,  // C420
		       0x58, 0xFF, 0xBA, 0xBD, 0x00, 0x01, 0xAA, 0x0A,
		       0x0A, 0x0A, 0x0A, 0xA8, 0x28, 0x50, 0x0F, 0xA5,  // C430
		       0x38, 0xd0, 0x0d, 0x8a, 0x45, 0x39, 0xd0, 0x08,
		       0xa9, 0x05, 0x85, 0x38, 0xd0, 0x0b, 0xb0, 0x09,  // C440
		       0x68, 0xaa, 0x68, 0xea, 0x68, 0xea, 0xea, 0xea,
		       0x60, 0xea, 0xea, 0xea, 0x68, 0xbd, 0x38, 0x06,  // C450
		       0xaa, 0x68, 0xa8, 0x68, 0xbd, 0x00, 0x02, 0x60,
		       0xc9, 0x10, 0xb0, 0x29, 0x8d, 0xcf, 0xc0, 0x60,  // C460
		       0x48, 0x18, 0x90, 0x2d, 0xbd, 0xb8, 0x06, 0x29,
		       0x0e, 0xd0, 0x01, 0x38, 0x68, 0x60, 0x8d, 0xcb,  // C470
		       0xc0, 0x18, 0x60, 0x8d, 0xca, 0xc0, 0x18, 0x60,
		       
		       0x8d, 0xc9, 0xc0, 0x18, 0x60, 0xc9, 0x02, 0xb0,  // C480
		       0x04, 0x8d, 0xcd, 0xc0, 0x60, 0x38, 0x60, 0x8d,
		       0xc8, 0xc0, 0x18, 0x60, 0x8d, 0xcc, 0xc0, 0x18,  // C490
		       0x60, 0x78, 0x8d, 0xce, 0xc0, 0xa2, 0x04, 0x18,
		       0x90, 0xCA, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // C4A0
		       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
		       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // C4B0
		       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
		       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, // C4C0
		       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
		       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, //  C4D0
		       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
		       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // C4E0
		       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
		       0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,  // C4F0
		       0xff, 0xff, 0xff, 0xd6, 0xff, 0xff, 0xff, 0x01 };
  memcpy(toWhere, rom, 256);
}

bool Mouse::hasExtendedRom()
{
  return true;
}

void Mouse::loadExtendedRom(uint8_t *toWhere, uint16_t byteOffset)
{
  // There's no extended ROM needed, b/c we do the extended ROM work
  // directly in C++
}

void Mouse::maintainMouse(int64_t cycleCount)
{
  //  static int64_t startTime = cycleCount;

  // Fake a 60Hz VBL in case we need it for our interrupts
  static int64_t nextInterruptTime = cycleCount + 17050;

  if ( (status & ST_MOUSEENABLE) &&
       (status & ST_INTVBL)  &&
       (cycleCount >= nextInterruptTime) ) {
    g_cpu->irq();
    
    interruptsTriggered |= ST_INTVBL;
    
    nextInterruptTime += 17050;
  } else {
    uint16_t xpos, ypos;
    g_mouse->getPosition(&xpos, &ypos);

    if ( (status & ST_MOUSEENABLE) &&
	 (status & ST_INTMOUSE) &&
	 (xpos != lastX || ypos != lastY) ) {
      g_cpu->irq();
      
      interruptsTriggered |= ST_INTMOUSE;
      lastX = xpos; lastY = ypos;
    }
  }
  /* FIXME: still need button */
}
