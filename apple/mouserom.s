; mouserom.s: the slot ROM for the mouse card, 65C02 only.
;
; Assemble with ca65 (see the mouse.rom rule in the Makefile):
;     ca65 --cpu 65C02 -l apple/mouserom.lst -o apple/mouserom.o apple/mouserom.s
;     ld65 -C apple/mouserom.cfg -o mouse.rom apple/mouserom.o
;
; WHAT THE ROM HAS TO PROVIDE
;
; Software finds a mouse card by its ID bytes and calls its routines through
; a table of offsets, so these positions are fixed (n = slot number):
;
;   $Cn00  the output hook. PR#n points CSW here; every character printed
;          arrives in A. IN#n also calls here first, because IN#n points KSW
;          at $Cn00 too: the ROM notices that, moves KSW to $Cn05, and
;          answers the input request.
;   $Cn05  the input hook proper, once installed. Returns a line describing
;          the mouse ("x,y,button" and a return) in the input buffer at
;          $0200, which is how Applesoft programs read the mouse.
;   $Cn05 = $38, $Cn07 = $18, $Cn0B = $01, $Cn0C = $20, $CnFB = $D6
;          the ID bytes. Two of them are the SEC and the BCC operand of
;          the input entry, so that entry's shape is fixed as well.
;   $Cn12..$Cn19  the low bytes of SETMOUSE, SERVEMOUSE, READMOUSE,
;          CLEARMOUSE, POSMOUSE, CLAMPMOUSE, HOMEMOUSE and INITMOUSE. A
;          caller loads X with $Cn and Y with $n0 and JSRs to $Cn00 plus
;          the byte. Each returns with C set on an error, and (SERVEMOUSE)
;          C set when the interrupt was not the mouse's.
;   $CnFF  the firmware version.
;
; Results travel through the slot's screen holes: X in $0478+n (low) and
; $0578+n (high), Y in $04F8+n and $05F8+n, the status byte in $0778+n and
; the mode in $07F8+n. CLAMPMOUSE takes its bounds from the slot-0 holes
; $0478, $04F8, $0578 and $05F8, as the protocol says.
;
; THE CARD
;
; The card does all the work. Each routine here stores to one register at
; $C080 + slot*16 + k and the card carries out the call, reading and
; writing the screen holes itself. Register 1 fills the input buffer for
; the input hook; register 0 notes the output mode.
;
; SLOT INDEPENDENCE
;
; The API routines are told the slot by their caller (X = $Cn, Y = $n0),
; with one exception: SERVEMOUSE is called from interrupt handlers that
; do not know the slot (A2osX does a bare JSR to it, because the //c's
; built-in mouse needs nothing), so it must find its own registers. The
; two hooks are not told the slot either. The ROM has no way to learn its
; own address without an absolute reference, so the emulator fills in
; four immediates when it loads the ROM (Mouse::loadROM): the hooks' at
; $Cn23 and $Cn25, SERVEMOUSE's at $Cn65 and $Cn67. Nothing else in the
; image depends on the slot.

        .setcpu "65C02"
        .org    $C400

; card registers, indexed by Y = slot*16
MR_OUTPUT     = $C080   ; the output hook: A = the character
MR_INPUT      = $C081   ; the input hook: fill the buffer
MR_HOMEMOUSE  = $C088
MR_POSMOUSE   = $C089
MR_CLEARMOUSE = $C08A
MR_READMOUSE  = $C08B
MR_INITMOUSE  = $C08C
MR_CLAMPMOUSE = $C08D   ; A = 0 for X, 1 for Y
MR_SERVEMOUSE = $C08E
MR_SETMOUSE   = $C08F   ; A = the mode

; screen holes, indexed by X = $Cn
LENHOLE    = $0638      ; $06F8+n: the length of the line in the input buffer
STATHOLE   = $06B8      ; $0778+n: the interrupts the card just served

KSWL    = $38           ; the input hook vector
KSWH    = $39
INBUF   = $0200

rom:

; ---- $Cn00: the output hook --------------------------------------------

        clc
        bcc     common          ; C clear: output
        .res    2, $EA

; ---- $Cn05: the input hook. Its first two bytes are ID bytes. ----------

inentry:
        sec                     ; $38
        bcc     common          ; never taken; its operand is the ID byte $18
        bcs     common          ; C set: input
        .byte   $00

; ---- $Cn0B: the ID bytes and the entry table ----------------------------

        .byte   $01, $20
        .byte   <err, <err, <err, <err
        .byte   $00
        .byte   <setmouse       ; $Cn12
        .byte   <servemouse     ; $Cn13
        .byte   <readmouse      ; $Cn14
        .byte   <clearmouse     ; $Cn15
        .byte   <posmouse       ; $Cn16
        .byte   <clampmouse     ; $Cn17
        .byte   <homemouse      ; $Cn18
        .byte   <initmouse      ; $Cn19
        .byte   <err, <err, <err, <err, <err, <err

; ---- $Cn20: the two hooks ------------------------------------------------

common:
        phy
        phx
        pha
slotx:  ldx     #$C4            ; $Cn23: X = $Cn, filled in by Mouse::loadROM
sloty:  ldy     #$40            ; $Cn25: Y = $n0, likewise
        bcs     input           ; entered at $Cn05
        lda     KSWL            ; entered at $Cn00. Did IN#n just point KSW here?
        bne     output
        txa
        eor     KSWH
        bne     output
        lda     #<inentry       ; then hook it to $Cn05 for good
        sta     KSWL
        bra     input           ; and answer this first request
output:
        pla
        sta     MR_OUTPUT,y     ; PR#n: the card notes the mode
        plx
        ply
        rts
input:
        sta     MR_INPUT,y      ; the card writes the line into the buffer
        lda     LENHOLE,x       ; and its length into the slot's screen hole
        plx                     ; the saved A and X are not wanted back
        plx
        ply
        tax                     ; X = the length
        lda     INBUF,x         ; A = the last character
        rts

; ---- the API. In: X = $Cn, Y = $n0. Out: C set on an error. -------------

setmouse:
        cmp     #$10            ; modes $10 and up do not exist
        bcs     err
        sta     MR_SETMOUSE,y
        rts                     ; C clear from the compare

; ---- $Cn60: SERVEMOUSE, which cannot trust X and Y (see the header) ----

        .res    rom+$60-*
servemouse:
        phx
        phy
        pha
        php
        sei
servex: ldx     #$C4            ; $Cn65: X = $Cn, filled in by Mouse::loadROM
servey: ldy     #$40            ; $Cn67: Y = $n0, likewise
        sta     MR_SERVEMOUSE,y
        lda     STATHOLE,x
        plp
        and     #$0E            ; VBL, button or movement served?
        clc
        bne     :+
        sec                     ; none: not our interrupt
:       pla
        ply
        plx
        rts

readmouse:
        sta     MR_READMOUSE,y
        clc
        rts

clearmouse:
        sta     MR_CLEARMOUSE,y
        clc
        rts

posmouse:
        sta     MR_POSMOUSE,y
        clc
        rts

clampmouse:
        cmp     #2              ; 0 = X, 1 = Y
        bcs     err
        sta     MR_CLAMPMOUSE,y
        rts                     ; C clear from the compare

homemouse:
        sta     MR_HOMEMOUSE,y
        clc
        rts

initmouse:
        sta     MR_INITMOUSE,y
        clc
        rts

err:
        sec
        rts

; ---- $CnFB: the ID byte, $CnFF: the version ------------------------------

        .res    rom+$FB-*
        .byte   $D6
        .res    3
        .byte   $01

; The offsets are a contract with every mouse-aware program and with
; Mouse::loadROM; refuse to assemble if anything moved.
        .assert inentry = rom+$05, error, "input hook must be at $Cn05"
        .assert common  = rom+$20, error, "common entry must be at $Cn20 (the ID byte $18 is its branch operand)"
        .assert slotx   = rom+$23, error, "the ldx slot byte must be at $Cn24"
        .assert sloty   = rom+$25, error, "the ldy slot byte must be at $Cn26"
        .assert servex  = rom+$65, error, "SERVEMOUSE's ldx must be at $Cn65"
        .assert servey  = rom+$67, error, "SERVEMOUSE's ldy must be at $Cn67"
        .assert *       = rom+$100, error, "ROM must be exactly 256 bytes"
