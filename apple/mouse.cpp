#include "mouse.h"
#include <string.h>
#include "globals.h"

#include "mouse-rom.h"

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
  case SW_R_HOMEMOUSE:
    g_mouse->setPosition( (g_ram.readByte(0x578) << 8) | g_ram.readByte(0x478),
			  (g_ram.readByte(0x5F8) << 8) | g_ram.readByte(0x4F8)
			  );
    break;
  case SW_R_POSMOUSE:
    g_mouse->setPosition( (g_ram.readByte(0x578+4) << 8) | g_ram.readByte(0x478+4),
			  (g_ram.readByte(0x5F8+4) << 8) | g_ram.readByte(0x4F8+4)
			  );
    break;
  case SW_R_CLEARMOUSE:
    g_ram.writeByte(0x578+4, 0);
    g_ram.writeByte(0x478+4, 0);
    g_ram.writeByte(0x5F8+4, 0);
    g_ram.writeByte(0x4F8+4, 0);
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
      uint8_t newStatus = g_ram.readByte(0x778+4) & ~0xC0;
      if (curButton) { newStatus |= 0x80; };
      if (lastButton) { newStatus |= 0x40; };

      uint16_t xv = xpos >> 8; xv &= 0xFF;
      printf("XPOS: %d => 0x%X 0x%X\n", xpos, xv, xpos & 0xFF);
      
      g_ram.writeByte(0x578+4, xv); // high X
      g_ram.writeByte(0x478+4, xpos & 0xFF); // low X
      g_ram.writeByte(0x5F8+4, (ypos >> 8) & 0xFF); // high Y
      g_ram.writeByte(0x4F8+4, ypos); // low Y
    }
    break;
  case SW_R_INITMOUSE:
    // Set clamp to (0,0) - (1023,1023)
    g_ram.writeByte(0x578, 0); // high of lowclamp
    g_ram.writeByte(0x478, 0); // low of lowclamp
    g_ram.writeByte(0x5F8, 0x03); // high of highclamp
    g_ram.writeByte(0x4F8, 0xFF); // low of highclamp
    g_mouse->setClamp(XCLAMP, 0, 1023);
    g_mouse->setClamp(YCLAMP, 0, 1023);
    break;
  case SW_R_SERVEMOUSE:
    if (lastButton) interruptsTriggered |= 0x40;
    if (curButton != lastButton) {
      interruptsTriggered |= 0x80;
      lastButton = curButton;
    }
    g_ram.writeByte(0x778+4, interruptsTriggered);
    g_ram.writeByte(0x6B8+4, interruptsTriggered); // hack to appease ROM
    interruptsTriggered = 0;
    break;
  case SW_W_INIT:
    v &= 0x03; // just the low 3 bits apparently?
    printf("Simple init: value is 0x%X\n", v);
    status = v;
    g_ram.writeByte(0x7f8 + 4, v);
    break;
  case SW_W_CLAMPMOUSE:
    {
      uint16_t lowval = (g_ram.readByte(0x578) << 8) | (g_ram.readByte(0x478));
      uint16_t highval = (g_ram.readByte(0x5F8) << 8) | (g_ram.readByte(0x4F8));
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
    g_ram.writeByte(0x7f8 + 4, v);
    break;
  default:
    printf("mouse: unknown switch write 0x%X = 0x%2X\n", s, v);
    break;
  }
}

void Mouse::loadROM(uint8_t *toWhere)
{
  /* Actual mouse ROM Disassembly:
C400-   2C 58 FF    BIT   $FF58   ; test bits in FF58 w/ A
C403-   70 1B       BVS   $C420   ; V will contain bit 6 from $FF58, which should be $60 on boot-up (RTS), which has $40 set, so should branch here
C405-   38          SEC
C406-   90 18       BCC   $C420   ; no-op; unless called @ $C406 directly?

C408-   B8          CLV           ; clear overflow
C409-   50 15       BVC   $C420   ; always branches

; unused... ? more lookup table... ?
C40B-   01 20       ORA   ($20,X)
C40D-   AE AE AE    LDX   $AEAE

; This is the lookup table for entry addresses
C410-   AE
C411-   00
C412-   6D ;(setmouse @ c46d)
C413-   75 ;(servemouse @ C475)
C414-   8E ;(readmouse @ C48E)
C415-   9F ;(clearmouse @ C49F)
C416-   A4 ;(posmouse @ C4A4)
C417-   86 ;(clampmouse @ C486)
C418-   A9 ;(homemouse @ C4A9)
C419-   97 ;(initmouse @ C497)
C41A-   AE
C41B-   AE
C41C-   AE ;(semi-documented: sets mouse frequency handler to 60 or 50 hz)
C41D-   AE
C41E-   AE
C41F-   AE

; Main (installs KSW for IN# handling)
C420-   48          PHA  ; push accumulator to stack
C421-   98          TYA
C422-   48          PHA  ; push Y to stack
C423-   8A          TXA  
C424-   48          PHA  ; push X to stack
C425-   08          PHP  ; push status to stack
C426-   78          SEI  ; disable interrupts

; This JSR $FF58 is a trick to get the address we're calling from. By
; default, $FF58 contains a RTS (until patched as the DOS '&' vector,
; or ProDOS does something similar). As long as this executes before
; the DOS takes over, it's safe; but if we're doing this after ProDOS
; loads, a 64k machine might have garbage after the language card RAM
; is loaded here. A safer alternative would be something like
;   LDA #$60     ; Write an RTS ($60) to a temporary memory 
;   STA WORK     ;  address, and then
;   JSR WORK     ;  jump to it
;   TSX
;   LDA $100, X  ; grab the return address off the stack

C427-   20 58 FF    JSR   $FF58  ; call something that will RTS
C42A-   BA          TSX          ; pull stack pointer to X
C42B-   BD 00 01    LDA   $0100,X ; grab A from the current stack pointer, which has our return addr
C42E-   AA          TAX          ; X = return addr
C42F-   0A          ASL
C430-   0A          ASL
C431-   0A          ASL
C432-   0A          ASL
C433-   A8          TAY          ; Y = (return addr) << 4
C434-   28          PLP          ; restore status from stack (includes V,C)
C435-   50 0F       BVC   $C446  ; overflow is clear from when we were called?
C437-   A5 38       LDA   $38    ; >> $38 is the IN# vector ("KSW") low byte
C439-   D0 0D       BNE   $C448  ; restore stack & return
C43B-   8A          TXA
C43C-   45 39       EOR   $39    ; >> KSW high byte
C43E-   D0 08       BNE   $C448  ; restore stack & return
C440-   A9 05       LDA   #$05
C442-   85 38       STA   $38    ; ($38) = $05
C444-   D0 0B       BNE   $C451

;; we wind up here when the entry vector $c405 is called directly (from $9eba)
C446-   B0 09       BCS   $C451 ; carry set if ... ?

;; entry point when called as PR# so BASIC/DOS can init the mouse
C448-   68          PLA         ; pull X from stack
C449-   AA          TAX
C44A-   68          PLA         ; pull Y from stack, but
C44B-   EA          NOP         ;    throw it away
C44C-   68          PLA         ; pull A from stack
C44D-   99 80 C0    STA   $C080,Y ; LC RAM bank2, read and wr-protect
C450-   60          RTS

C451-   99 81 C0    STA   $C081,Y ; LC RAM bank 2, read ROM
C454-   68          PLA           ; 
C455-   BD 38 06    LDA   $0638,X
C458-   AA          TAX
C459-   68          PLA
C45A-   A8          TAY
C45B-   68          PLA
C45C-   BD 00 02    LDA   $0200,X ; keyboard character buffer?
C45F-   60          RTS

C460-   00          BRK
C461-   00          BRK
C462-   00          BRK
C463-   00          BRK
C464-   00          BRK
C465-   00          BRK
C466-   00          BRK
C467-   00          BRK
C468-   00          BRK
C469-   00          BRK
C46A-   00          BRK
C46B-   00          BRK
C46C-   00          BRK

; SetMouse
C46D-   C9 10       CMP   #$10    ; interrupt on VBL + interrupt on mouse move?
C46F-   B0 3F       BCS   $C4B0   ; RTS if A >= 0x10
C471-   99 82 C0    STA   $C082,Y ; LC RAM bank 2, read ROM, wr-protect RAM
C474-   60          RTS
; ServeMouse
C475-   48          PHA           ; save A
C476-   18          CLC
C477-   90 39       BCC   $C4B2   ; always branches
; ServeMouse cleanup and exit
C479-   99 83 C0    STA   $C083,Y ; LC RAM bank 2, read RAM
C47C-   BD B8 06    LDA   $06B8,X ; apparently this is where the new status wound up, also $7f8 + <slot#>
C47F-   29 0E       AND   #$0E
C481-   D0 01       BNE   $C484   ; if the interrupt bits don't show it was a mouse interrupt, set error (C) before returning
C483-   38          SEC
C484-   68          PLA           ; restore A
C485-   60          RTS
; ClampMouse
C486-   C9 02       CMP   #$02
C488-   B0 26       BCS   $C4B0   ; RTS if 2 <= the A register
C48A-   99 83 C0    STA   $C083,Y ; LC RAM bank 2, read RAM
C48D-   60          RTS
; ReadMouse
C48E-   A9 04       LDA   #$04
C490-   99 83 C0    STA   $C083,Y ; LC RAM bank 2, read RAM
C493-   18          CLC
C494-   EA          NOP
C495-   EA          NOP
C496-   60          RTS
; InitMouse
C497-   EA          NOP
C498-   A9 02       LDA   #$02
C49A-   99 83 C0    STA   $C083,Y ; LC RAM bank 2, read RAM
C49D-   18          CLC
C49E-   60          RTS
; ClearMouse
C49F-   EA          NOP
C4A0-   A9 05       LDA   #$05
C4A2-   D0 F6       BNE   $C49A
; PosMouse
C4A4-   EA          NOP
C4A5-   A9 06       LDA   #$06
C4A7-   D0 F1       BNE   $C49A
; HomeMouse
C4A9-   EA          NOP
C4AA-   A9 07       LDA   #$07
C4AC-   D0 EC       BNE   $C49A
; "TimeData" to set freq before init to 50/60 Hz? Doesn't look like it...
C4AE-   A2 03       LDX   #$03
C4B0-   38          SEC
C4B1-   60          RTS
; ServeMouse main worker
C4B2-   08          PHP
C4B3-   A5 00       LDA   $00
C4B5-   48          PHA
C4B6-   A9 60       LDA   #$60
C4B8-   85 00       STA   $00
C4BA-   78          SEI
C4BB-   20 00 00    JSR   $0000
C4BE-   BA          TSX
C4BF-   68          PLA
C4C0-   85 00       STA   $00
C4C2-   BD 00 01    LDA   $0100,X
C4C5-   28          PLP
C4C6-   AA          TAX
C4C7-   0A          ASL
C4C8-   0A          ASL
C4C9-   0A          ASL
C4CA-   0A          ASL
C4CB-   A8          TAY
C4CC-   A9 03       LDA   #$03
C4CE-   18          CLC
C4CF-   90 A8       BCC   $C479 ; always branches

... but the version below is patched so it uses soft switches to call
back here.

SETMOUSE:
C471- 8D CF C0  STA $C0CF    ; use soft switch 0xF to handle it
            60  RTS

Patch: C471 from 99 82 C0 60 => 8D CF C0 60

SERVEMOUSE @ $C4B2:
        78 SEI
  AD CE C0 LDA $C0CE   ; use soft switch 0xE (read) to trigger this - expect new value to be placed in $7f8+4 and $6b8+4
     A2 04 LDX #$04    ; our slot number, for cleanup code
        18 CLC
     90 C1 BCC $C47C

Patch: C4B2 from 08 A5 00 48 A9 60 85 00 78 =>
                 78 AD CE C0 A2 04 18 90 C1

READMOUSE
C48E-  AD CB C0 LDA $C0CB  ; soft switch 0x0B for readmouse
             18 CLC
             60 RTS

Patch: from A9 04 99 83 C0 =>
            AD CB C0 18 60

CLEARMOUSE
C49F-  AD CA C0 LDA $C0CA  ; soft switch 0x0A for clearmouse
             18 CLC
             60 RTS

Patch: from EA A9 05 D0 F6 =>
            AD CA C0 18 60
POSMOUSE
C4a4-  AD C9 C0 LDA $C0C9  ; soft switch 0x09 for posmouse
             18 CLC
             60 RTS

Patch: from EA A9 06 D0 F1 =>
            AD C9 C0 18 60

CLAMPMOUSE
C48A- 8D CD C0  STA $C04D ; use write to soft switch 0x0D to trigger clamp
            60  RTS

Patch: from 99 83 C0 60 => 8D CD C0 60

HOMEMOUSE
C4A9- AD C8 C0 LDA $c048  ; soft switch 0x08 for homemouse (read)
            18      CLC
            60       RTS

Patch: from EA A9 07 D0 EC => AD C8 C0 18 60

INITMOUSE
C498- AD CC C0 LDA $C04C ; soft switch 0x0C read for initmouse
            18 CLC
            60 RTS

Patch: from A9 02 99 83 C0 => AD CC C0 18 60
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
#if 0
  printf("loading extended rom for the mouse\n");
  for (int i=0; i<256; i++) {
    toWhere[i] = romData[i + byteOffset];
  }
#endif
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
