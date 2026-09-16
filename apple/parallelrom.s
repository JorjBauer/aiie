; parallelrom.s: the slot ROM for the parallel printer card, 65C02 only.
;
; Assemble with ca65 (see the firmware rule in the Makefile):
;     ca65 --cpu 65C02 -l apple/parallelrom.lst -o apple/parallelrom.o apple/parallelrom.s
;     ld65 -C apple/parallelrom.cfg -o parallel.rom apple/parallelrom.o
;
; WHAT THE ROM HAS TO PROVIDE
;
; The path that matters here is PR#n: Applesoft and ProDOS point the
; output hook (CSW, $36/$37) at $Cn00 and every printed character arrives
; in A, high bit set. The ROM sends it to the printer and, until told
; otherwise, echoes it to the screen. It also understands the printer
; card's Control-I commands, which the emulated FX-80's own test uses:
;
;   ^I I or ^I M   reset: echo on, 40 columns, line feed after CR on
;   ^I K or ^I O   echo on at 40 columns; toggle the line feed
;   ^I nH or nL    echo off, line feed off; n (40..255) sets the width
;   ^I nJ or nN    echo off, line feed as it was; n sets the width
;   ^I ^X          ^X (any control character but return) is the command
;                  character from now on, so ^I itself can reach the printer
;
; The width is where the card ends a line itself, for programs that never
; send a return; the line feed after a return is what actually advances
; the paper, since the FX-80 treats a return as return-only. Defaults:
; echo on, 40 columns, line feed on.
;
; Applesoft moves the cursor without printing: a comma in PRINT and HTAB
; set CH ($24), and TAB( prints "target minus CH" spaces. So the ROM keeps
; CH and the printer's column in step both ways: with the echo off it
; advances CH itself and zeroes it at a return (with the echo on, COUT1
; does that), and whenever CH is ahead of the printer it sends spaces
; until they agree. That is what makes PRINT A,B line up on paper.
;
; A command character followed by anything else is swallowed.
;
; Entry points (n = slot number):
;
;   $Cn00  PR#n. Resets the settings above, then behaves as $Cn02, and
;          moves CSW to $Cn02 so the next character skips the reset.
;   $Cn02  output, once initialized.
;
; Settings live in the slot's screen holes, so they survive between calls
; without touching zero page (only $24 and $36/$37 are used).
;
; THE CARD
;
; A write to register 0 ($C080 + slot*16) hands the byte to the printer;
; there is no strobe and no busy state. The byte goes through unchanged:
; the printer model strips the high bit where it matters and takes all
; eight for graphics.
;
; SLOT INDEPENDENCE
;
; The hooks are entered knowing nothing about the slot, and the ROM has no
; way to learn its own address without an absolute reference, so the
; emulator fills in two immediates at $Cn07 and $Cn09 when it loads the ROM
; (ParallelCard::loadROM). Nothing else in the image depends on the slot.

        .setcpu "65C02"
        .org    $C100

DATA    = $C080         ; card register 0, indexed by Y = slot*16
COUT1   = $FDF0         ; the monitor's screen output

; screen holes, indexed by X = $Cn
LEN     = $04B8         ; $0578+n: the line width
NUM     = $0538         ; $05F8+n: the number being typed after the command character
COL     = $05B8         ; $0678+n: the printer's column
FLAGS   = $0638         ; $06F8+n: bit 7 = echo, bit 6 = line feed after CR,
                        ;          bit 0 = a command is being typed
CMDCH   = $06B8         ; $0778+n: the command character

CH      = $24
CSWL    = $36
CSWH    = $37

CTRLI   = $09
CR      = $0D
SPACE   = ' '|$80

rom:

; ---- $Cn00: PR#n; $Cn02: output once initialized ------------------------

        clc                     ; C clear: reset first
        .byte   $24             ; BIT zp: swallows the sec below as its operand
noreset:
        sec                     ; C set: no reset

; ---- the output hook. A = the character; A, X, Y come back. -------------

common:
        phy
        phx
        pha
slotx:  ldx     #$C1            ; $Cn06: X = $Cn, filled in by ParallelCard::loadROM
sloty:  ldy     #$10            ; $Cn08: Y = $n0, likewise
        bcs     output
        cpx     CSWH            ; PR#n just pointed CSW here?
        bne     reset
        lda     #<noreset       ; then point it past the reset from now on
        sta     CSWL
reset:  lda     #40
        sta     LEN,x
        lda     #$C0            ; echo on, line feed on, no command pending
        sta     FLAGS,x
        lda     #CTRLI
        sta     CMDCH,x
        stz     COL,x

output:
        lda     FLAGS,x
        lsr     a               ; C = a command is being typed
        pla                     ; the character (pla and and leave C alone)
        pha
        and     #$7F
        bcs     command
        cmp     CMDCH,x
        bne     send
        inc     FLAGS,x         ; a command follows; the command character goes nowhere
        stz     NUM,x
        bra     done

send:   cmp     #' '
        bcc     sendit          ; control characters: no catching up
catch:  lda     COL,x           ; the cursor has moved without printing (a comma,
        cmp     CH              ; HTAB): pad the printer out to it
        bcs     sendit
        lda     #SPACE
        sta     DATA,y
        inc     COL,x
        bra     catch
sendit: pla
        pha
        sta     DATA,y          ; to the printer, all eight bits
        bit     FLAGS,x
        bpl     classify
        jsr     COUT1           ; and to the screen; COUT1 keeps X and Y
classify:
        pla
        pha
        and     #$7F
        cmp     #CR
        beq     endline
        cmp     #' '
        bcc     done            ; other control characters take no column
        inc     COL,x
        bit     FLAGS,x
        bmi     counted         ; echo on: COUT1 moved the cursor
        inc     CH              ; echo off: move it ourselves
counted:
        lda     COL,x
        cmp     LEN,x
        bcc     done
        lda     #CR|$80         ; the line is full: end it for the printer
        sta     DATA,y
endline:
        stz     COL,x
        stz     CH              ; (with the echo on, COUT1 did this already)
        bit     FLAGS,x
        bvc     done            ; no line feed wanted
        lda     #$8A
        sta     DATA,y
done:   pla
        plx
        ply
        rts

xdone:  bra     done            ; the command code is too far for one branch
cancel: dec     FLAGS,x         ; a return cancels the command and is printed
        bra     send

; ---- a command in progress. A = the character, 7 bits. -------------------

command:
        cmp     #CR
        beq     cancel
        cmp     #' '
        bcc     newcmd          ; a control character becomes the command character
        sbc     #'0'            ; C is set from the compare
        cmp     #10
        bcs     letter
        pha                     ; a digit: NUM = NUM * 10 + digit
        lda     NUM,x
        asl     a
        asl     a
        adc     NUM,x
        asl     a
        sta     NUM,x
        pla
        adc     NUM,x
        sta     NUM,x
        bra     xdone

newcmd: sta     CMDCH,x
        lda     #$FF            ; and then it is no letter either
letter: dec     FLAGS,x         ; whatever it is, the command ends here
        sbc     #'H'-'0'        ; C is set from the compare above
        cmp     #8
        bcs     xdone           ; not H..O: swallowed
        and     #3              ; H..O pair up: H/L = 0, I/M = 1, J/N = 2, K/O = 3
        beq     cmdquiet
        dec     a
        beq     cmdreset
        dec     a
        beq     cmdnoecho
                                ; K or O: toggle the line feed, echo on at 40
        lda     FLAGS,x
        eor     #$40
        ora     #$80
        .byte   $2C             ; BIT abs: steps over the lda below
cmdreset:
        lda     #$C0
        sta     FLAGS,x
        lda     #40
        bra     setwidth

cmdnoecho:
        lda     FLAGS,x
        and     #$7F
        .byte   $2C             ; BIT abs: steps over the lda below
cmdquiet:
        lda     #0
        sta     FLAGS,x
setlen: lda     NUM,x
        cmp     #40
        bcc     xdone           ; no number, or one too small: keep the width
setwidth:
        sta     LEN,x
        bra     xdone

; The offsets are a contract with ParallelCard::loadROM; refuse to
; assemble if anything moved.
        .assert noreset = rom+$02, error, "the no-reset entry must be at $Cn02"
        .assert slotx   = rom+$06, error, "the ldx slot byte must be at $Cn07"
        .assert sloty   = rom+$08, error, "the ldy slot byte must be at $Cn09"
        .assert *      <= rom+$100, error, "ROM must fit in 256 bytes"
