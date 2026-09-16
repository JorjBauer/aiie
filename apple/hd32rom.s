; hd32rom.s: the slot ROM for the HD32 hard-disk card, 65C02 only.
;
; Assemble with ca65 (see the hd32.rom rule in the Makefile):
;     ca65 --cpu 65C02 -l apple/hd32rom.lst -o apple/hd32rom.o apple/hd32rom.s
;     ld65 -C apple/hd32rom.cfg -o hd32.rom apple/hd32rom.o
;
; WHAT THE ROM HAS TO PROVIDE
;
; The machine ROM, ProDOS and SmartPort-style loaders find a block device
; by looking at fixed bytes in its slot ROM, so the entry points are pinned
; to these offsets (n = slot number):
;
;   $Cn00  boot. The machine ROM's slot scan jumps here when it finds the
;          signature bytes ($20 at $Cn01, $00 at $Cn03, $03 at $Cn05); so does
;          PR#n / IN#n / Cn00G. Loads block 0 of drive 1 to $0800 and runs it.
;   $Cn0A  ProDOS block-device call. $CnFF holds this offset. ProDOS sets
;          $42 = command (0 status, 1 read, 2 write, 3 format), $43 = unit
;          (DSSS0000: bit 7 = drive, bits 4-6 = slot), $44/45 = buffer,
;          $46/47 = block, and JSRs here. Return: C clear and A = 0 on
;          success; C set and A = a ProDOS error code on failure; STATUS
;          also returns the volume size in blocks in X (low) and Y (high).
;   $Cn0D  SmartPort call, always the ProDOS entry plus 3. Called as
;          JSR $Cn0D / .byte command / .word parameter-list, and returns
;          past those three bytes. Only the block-shaped calls exist here:
;          status, read and write with a 3-parameter list. This is what a
;          3.5" or IIc+ style boot loader uses to read the disk directly.
;   $CnFB  SmartPort ID type byte ($00 = generic block device).
;   $CnFC  volume size in blocks; $0000 means "ask STATUS", which is right
;          for a card whose images can be any size.
;   $CnFE  device attributes: $D7 = removable, interruptable, two volumes,
;          write, read, status.
;   $CnFF  low byte of the ProDOS entry point ($0A).
;
; $Cn07 is deliberately $3C rather than the $00 that would advertise a real
; SmartPort controller: the card answers ProDOS block calls only, and $00
; would make ProDOS 8 issue SmartPort status calls this ROM does not honor.
;
; THE CARD
;
; Registers are at $C080 + slot*16. Writing the command, unit, buffer and
; block registers and then reading EXEC performs the whole command at once:
; the 512 bytes have already moved when the read returns, and the byte read
; is the ProDOS result code (0 = success). Two read-only registers report
; the mounted image's size in blocks. There is no busy state to poll.
;
; WHAT WAS LEFT OUT
;
; Compared with the 6502 firmware this replaces: the two extra entry points
; that only existed for DOS images made with older firmware ($Cn46, $Cn61);
; the byte-at-a-time read port; the busy poll; and the stack trick that
; found the slot at run time. The emulator loads this ROM and already knows
; the slot, so it fills the slot byte in (see HD32::loadROM), as it does
; the floppy fallback below.
;
; The code makes no absolute reference to itself, so the same image works
; in any slot. Subroutine calls within the ROM would need its address, so
; the boot path reaches the command code by branching and gets back by
; setting bit 7 of the command byte, which the command code checks.

        .setcpu "65C02"
        .org    $C700

; card registers, indexed by X = slot*16
HD_EXEC   = $C080       ; read: run the command, returns the ProDOS result
HD_CMD    = $C082
HD_UNIT   = $C083       ; bit 7 selects the drive
HD_BUF    = $C084       ; 2 bytes
HD_BLOCK  = $C086       ; 2 bytes
HD_NBLKLO = $C089       ; read: image size in blocks, low byte
HD_NBLKHI = $C08A       ;       high byte

; the ProDOS block-device call
CMD     = $42
UNIT    = $43
BUF     = $44
BLOCK   = $46

BUTTON0 = $C061         ; Open-Apple
BOOTBUF = $0800         ; where block 0 lands
BOOTRUN = $0801         ; and where it is entered
DISK2   = $C600         ; the floppy controller; HD32::loadROM fills in the slot

rom:

; ---- $Cn00: boot, and the signature bytes ------------------------------

        lda     #$20
        lda     #$00
        lda     #$03
        lda     #$3C
        bra     boot

; ---- $Cn0A: ProDOS block-device call -----------------------------------

prodos:
        sec
        bcs     entry

; ---- $Cn0D: SmartPort call ---------------------------------------------

smartport:
        clc

; ---- $Cn0E: common entry; the slot byte lives at $Cn0F ------------------

entry:
        ldx     #$70            ; X = slot*16; HD32::loadROM overwrites the $70
        bcs     cmdproc         ; C set: ProDOS (and the boot path)
        bra     smartcall       ; C clear: SmartPort

; ---- boot ---------------------------------------------------------------
; STATUS drive 1; read its block 0 to $0800; if it is a boot block and
; Open-Apple is not held, run it with X = slot*16 as the loader expects.
; Anything else boots the floppy instead.

boot:
        stz     UNIT            ; drive 1
        stz     BUF
        lda     #>BOOTBUF
        sta     BUF+1
        stz     BLOCK
        stz     BLOCK+1
        lda     #$80            ; STATUS, marked as the boot path
        sta     CMD
        bra     prodos          ; picks up X on the way to cmdproc
boot2:                          ; A = result, X = slot*16
        tay
        bne     nodisk          ; no image, or the read failed
        inc     CMD             ; $80 status -> $81 read block 0 -> $82 loaded
        lda     CMD
        cmp     #$82
        bcc     cmdproc
        lda     BOOTBUF         ; a ProDOS boot block starts with $01
        cmp     #1
        bne     nodisk
        bit     BUTTON0         ; Open-Apple held: the user wants the floppy
        bmi     nodisk
        jmp     BOOTRUN

; ---- $Cn40: the floppy fallback; HD32::loadROM fills in the slot --------

nodisk:
        jmp     DISK2

; ---- the ProDOS call proper --------------------------------------------
; In:  X = slot*16, $42..$47 = the call. Bit 7 of CMD marks the boot path.
; Out: C = error flag, A = result (0 = success); STATUS returns X/Y = size.

cmdproc:
        lda     CMD
        and     #$7F
        sta     HD_CMD,x
        lda     UNIT
        sta     HD_UNIT,x
        lda     BUF
        sta     HD_BUF,x
        lda     BUF+1
        sta     HD_BUF+1,x
        lda     BLOCK
        sta     HD_BLOCK,x
        lda     BLOCK+1
        sta     HD_BLOCK+1,x
        lda     HD_EXEC,x       ; the block has moved by the time this returns
        bit     CMD
        bmi     boot2           ; booting: back to the boot sequence
        cmp     #1              ; C = (result != 0)
        bcs     done
        ldy     CMD
        bne     done            ; read or write: A = 0, C clear
        ldy     HD_NBLKHI,x     ; status: the volume size in blocks
        lda     HD_NBLKLO,x
        tax
        lda     #0
done:
        rts

; ---- SmartPort ----------------------------------------------------------
; Turn the inline call into a ProDOS call. The JSR left the address of its
; own last byte on the stack; the command byte follows it, then the
; parameter-list pointer, and the caller resumes after that. C is clear.

smartcall:
        ply                     ; return address, low
        pla                     ;                 high
        sta     BLOCK+1
        sty     BLOCK           ; (BLOCK) -> the byte before the command
        tya
        adc     #3
        tay
        lda     BLOCK+1
        adc     #0
        pha
        phy                     ; return to the byte after the parameter pointer
        ldy     #1
        lda     (BLOCK),y       ; command
        sta     CMD
        iny
        lda     (BLOCK),y       ; parameter list, low
        pha
        iny
        lda     (BLOCK),y       ;                 high
        sta     BLOCK+1
        pla
        sta     BLOCK           ; (BLOCK) -> the parameter list
        ldy     #1              ; [0] is the parameter count, 3 for these calls
        lda     (BLOCK),y       ; unit: 1 = drive 1, 2 = drive 2 (0 = the card)
        cmp     #2
        ror     a               ; bit 7 = drive 2
        and     #$80
        sta     UNIT
        iny
        lda     (BLOCK),y       ; buffer, low
        sta     BUF
        iny
        lda     (BLOCK),y       ;         high
        sta     BUF+1
        iny
        lda     (BLOCK),y       ; block, low
        pha
        iny
        lda     (BLOCK),y       ;        middle (an image is at most 32MB)
        sta     BLOCK+1
        pla
        sta     BLOCK
        bra     cmdproc

; ---- $CnFB: the ID bytes -------------------------------------------------

        .res    rom+$FB-*
        .byte   $00             ; SmartPort ID type: generic
        .word   $0000           ; blocks: ask STATUS
        .byte   $D7             ; removable, interruptable, 2 volumes, w/r/status
        .byte   <prodos         ; ProDOS entry offset

; The offsets above are a contract with the machine ROM, ProDOS and
; HD32::loadROM; refuse to assemble if anything moved.
        .assert prodos    = rom+$0A, error, "ProDOS entry must be at $Cn0A"
        .assert smartport = rom+$0D, error, "SmartPort entry must be at $Cn0D"
        .assert entry     = rom+$0E, error, "slot byte must be at $Cn0F"
        .assert nodisk    = rom+$40, error, "floppy fallback must be at $Cn40"
        .assert *         = rom+$100, error, "ROM must be exactly 256 bytes"
