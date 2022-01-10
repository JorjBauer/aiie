.feature labels_without_colons
.feature leading_dot_in_identifiers
.feature c_comments
.P02    ; normal 6502
        
.macro ADR val
    .addr val
.endmacro

.macro BLT val
        BCC val
.endmacro

.macro BGE val        
        BCS val
.endmacro        
        
; Force APPLE 'text' to have high bit on; Will display as NORMAL characters
.macro ASC text
    .repeat .strlen(text), I
        .byte   .strat(text, I) | $80
    .endrep
.endmacro

a_cr	= $0d		; Carriage return.

F8ROM_YXHEX = $F940        
F8ROM_AXHEX = $F941
F8ROM_XHEX  = $F944              ; print X register in hex -- kills X,A
F8ROM_AHEX  = $FDDA              ; print A register in hex
        
F8ROM_INIT  = $FB2F            
F8ROM_HOME  = $FC58             ; Kills A,Y
F8ROM_CROUT = $FC62             ; carriage return out
F8ROM_MONWAIT = $FCA8           ; wait for (A*A*2.5 + A*13.5 + 7) * 0.980 usec
F8ROM_COUT  = $FDED             ; Load A with character to print
F8ROM_RDKEY = $FD0C
        
WRVEC       = $03D0             ; warm re-entry point

PHSOFF      = $C080
PHSON       = $C081
DISKOFF     = $C088
DISKON      = $C089
DRIVEA      = $C08A
DISK_LATCHR = $C08C
DISK_LATCHW = $C08D
DISK_MODER  = $C08E
DISK_MODEW  = $C08F
        

        ;; Zero-page addresses used
        ;;    00/01: storage pointer for loops in nybble code
        ;;    02/03: second storage pointer for loops in nybble code
        ;;    04/05: lookup table indexer for the _trans table
        ;;    FA/FB: DST pointer for memset
        ;;    FC/FD: pointer to sector buffer
        ;;    FF: scratch, used when erasing a track
ZP_STORPTR = $00
ZP_STOR2   = $02
ZP_TRANSP  = $04
DST        = $FA
ZP_SECTP   = $FC
ZP_SCRATCH = $FF

.segment "CODE"
START
        JMP Entry

Entry
        JSR     F8ROM_INIT
        JSR     F8ROM_HOME
        LDY     #>STARTMSG
        LDA     #<STARTMSG
        JSR     Prtmsg

        JSR     F8ROM_RDKEY     ; wait for a keypress

        ;; set up pointers to SECDATA and TRANS62
        LDA     #>SECDATA
        STA     ZP_SECTP+1
        LDA     #<SECDATA
        STA     ZP_SECTP
        
        LDA     #>TRANS62       ; set up ZP_TRANSP to point at TRANS62
        STA     ZP_TRANSP+1     ; (a 64-byte lookup table)
        LDA     #<TRANS62
        STA     ZP_TRANSP

        ;; initialize the buffer to start, so we can see what's going on
        ;; DEBUGGING *** - don't really need to do this
        LDA     #$FE
        STA     VALUE
        LDA     #>NYBDATA
        STA     DST+1
        LDA     #<NYBDATA
        STA     DST
        LDA     #$00
        STA     CNT
        LDA     #$1A
        STA     CNT+1
        JSR     memset

        
        ;;  Seek to track 0

        ;;  Fill track 0 with as many FF bytes as we think fit. Per
        ;; https://retrocomputing.stackexchange.com/questions/503/absolute-maximum-number-of-nibbles-on-an-apple-ii-floppy-disk-track
        ;; track 0 can't reasonably fit more than about 8300 nybbles, so
        ;; we will write 8400 nybbles to be sure to have cleared any possible
        ;; track (track 0 is the physically largest, on the outside of
        ;; the disk; so track 35 will be multiply covered easily).

        ;; Turn on motor for slot 6, drive 1
        LDX     #$60            ; slot 6
        LDA     DISKON,X
        LDA     DRIVEA,X
        ;; and seek out to track 0
        JSR     RecalibrateTrack 

        ;; Wait for it to come to speed
        JSR     WaitMotor

        ;; Fill the track with 0xFF sync bytes (ensuring we've wiped the track)
        JSR     EraseTrack

        ;; Write out a track of nybblized sectors to Track 0 for physical 
        ;; sectors 0 through 15. The data in each is algorithmic, same as
        ;; the wozzle test disk pattern test #7 -- 256 incrementing bytes
        ;; starting at (sector + track).

        ;; Precompute one track of nybblized data at NYBDATA for track 0 (A)
        LDA     #$00
        JSR     MakeTrackData

        ;; Write it to the track
        JSR     WriteTrack

        ;; using RWTS, validate the sector contents of each sector on the track

        ;; ...

        ;; Find sector # 6 (arbitrarily chosen) and replace its data
        ;; with 256 bytes of 0xFF (nybbles 0xFF 0x96 0x96 0x96 ... )

        ;; ...

        ;; using RWTS, validate the sector contents of all 16 sectors

        ;; ...

        ;; Turn off motor for slot 6, drive 1
        LDX     #$60            ; slot 6
        LDA     DISKOFF,X

TestsDone
        LDY     #>ENDMSG        ; All done, tell the user
        LDA     #<ENDMSG
        JSR     Prtmsg

        LDA     #>SECDATA
        JSR     F8ROM_AHEX
        LDA     #<SECDATA
        JSR     F8ROM_AHEX
        LDY     #>SECDATAMSG
        LDA     #<SECDATAMSG
        JSR     Prtmsg
        
        LDA     #>NYBDATA
        JSR     F8ROM_AHEX
        LDA     #<NYBDATA
        JSR     F8ROM_AHEX
        LDY     #>NYBDATAMSG
        LDA     #<NYBDATAMSG
        JSR     Prtmsg
        
Exit        
        JMP     WRVEC           ; done; warm-start for the user

WriteProtected
        LDY     #>WPMSG
        LDA     #<WPMSG
        JSR     Prtmsg
        JMP     WRVEC
        
;;; ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Start of subroutines
;;; ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        ;; Prtmsg: message address is in Y (high) and A (low)
        ;; Prints until it finds a 00 byte; then emits a CR
        
Prtmsg  STA $42 ;Store the msg loc.
        STY $43

        LDY #$00 ;Print the message.
@Loop   LDA ($42),Y
        BEQ @Done
        JSR F8ROM_COUT
        INY
        JMP @Loop
@Done
        JSR F8ROM_CROUT
        RTS

        ;; WaitMotor: delay to wait for a drive to come up to speed
WaitMotor
        LDA     #$EF
        STA     WAITCTR
        LDA     #$D8
        STA     WAITCTR+1
@LoopA  LDY     #$12
@LoopB  DEY        
        BNE     @LoopB
        INC     WAITCTR
        BNE     @LoopA
        INC     WAITCTR+1
        BNE     @LoopA
        RTS

        ;; MoveTrack: move from CURHALFTRK to DSTHALFTRK
        ;; Note that those are (as the names imply) half-tracks; be
        ;; sure to multiply actual track numbers by two

MoveTrack
        LDA     CURHALFTRK
        CMP     DSTHALFTRK
        BNE     @MoreWork
        RTS                     ; all done, return
@MoreWork
        BCC     @MoveUp         ; If we want to move up, then go there
        ;; otherwise fall through to movedown
@MoveDown
        DEC     CURHALFTRK
        JMP     @MoveHead
@MoveUp
        INC     CURHALFTRK
@MoveHead
        LDA     CURHALFTRK      ;compute phase number for new track
        AND     #$03
        ASL
        ORA     #$60            ; slot number 6
        TAY
        LDA     PHSON,Y         ; turn on phase to move
        LDA     #$56
        JSR     F8ROM_MONWAIT   ; delay for physical action
        LDA     PHSOFF, Y       ; turn off phase
        JMP     MoveTrack       ; and see if there's more work to do

        ;; RecalibrateTrack: make sure we're on track 0 by moving more tracks
        ;; (outward) than exist on the drive
RecalibrateTrack
        LDA     #$80            ; move more half-tracks than exist
        STA     CURHALFTRK
        LDA     #$00
        STA     DSTHALFTRK
        JMP     MoveTrack

        ;; EraseTrack: Write 0x3000 nybbles of 0xFF to the current track.
        ;; Assumes disk is running and up to speed, and on the destination
        ;; track already.
        ;; Tromps A, Y, and zero-page ZP_SCRATCH
        ;; FIXME: need to be sure EraseTrack does not span pages or the timing
        ;;  will be broken
EraseTrack
        LDA     #$30            ; 0x3000 nybbles to write
        STA     NYBCOUNT
        LDX     #$60            ; Slot 6
        LDA     DISK_LATCHW,X
        LDA     DISK_MODER,X
        BPL     @NOTWP
        JMP     WriteProtected  ; Disk is write protected, so bail
@NOTWP
        LDA     #$FF            ; nybble data to write
        STA     DISK_MODEW,X
        CMP     DISK_LATCHR,X   ;4
        BIT     ZP_SCRATCH      ;+3=7
        LDY     #$00            ;+2=9
@LoopA
        DEC     ZP_SCRATCH      ;+5=14
        NOP                     ;+2=16
@LoopB                          ;       (always 16 here)
        DEC     ZP_SCRATCH      ;+5=21
        CMP     $FFFF           ;+4=25
        NOP                     ;+2=27
        STA     DISK_LATCHW,X   ;+5=32
        CMP     DISK_LATCHR,X   ;4
        DEY                     ;+2=6
        BNE     @LoopA          ;+3 (branch)=9 or +2=8
        DEC     NYBCOUNT        ;8+5=13
        BNE     @LoopB          ;+3(branch)=16 or +2=15
        JSR     @rts            ; waste +12=27
        DEC     ZP_SCRATCH      ;+5=32
        LDA     DISK_MODER,X
        LDA     DISK_LATCHR,X
@rts
        RTS

        ;; WriteTrack: write <6656 ($1A00) nybbles to the current track.
        ;; Assumes drive is on and up to speed, and on the right track.
        ;; The data to write is at NYBDATA.
        ;; FIXME: need to be sure WriteTrack does not span pages or the timing
        ;;  will be broken
WriteTrack
        LDY     #>WRITEMSG
        LDA     #<WRITEMSG
        JSR     Prtmsg
        
        LDA     #>NYBDATA       ; set up ZP_STORPTR to the start of NYBDATA
        STA     ZP_STORPTR+1
        LDA     #<NYBDATA
        STA     ZP_STORPTR

        ;; Bytes must be sent to the controller precisely once every 32
        ;; clock cycles.
        ;; 
        ;; Copy <$1A00 bytes from (ZP_STORPTR) to the disk drive. We don't
        ;;  count the bytes - instead, there's a 00 sentinel at the end of
        ;;  the track data. When we get that, we branch to @doneWriting.

        LDX     #$60            ; Slot 6
        LDA     DISK_LATCHW,X
        LDA     DISK_MODER,X
        BPL     @NOTWP
        JMP     WriteProtected  ; Disk is write protected, so bail
@NOTWP

        LDY     #$00
        LDA     (ZP_STORPTR),Y

        STA     DISK_MODEW,X
        ;; Start of timing critical section: one byte every 32 cycles
        CMP     DISK_LATCHR,X   ; Start of first write... 4

        JSR     @rts            ; waste +12 cycles = 16
        NOP                     ; +2 = 18
@loopA                          ;coming in to loopA, we're at 18
        NOP                     ; +2 = 20
        NOP                     ; +2 = 22
        CMP     $00             ; waste +3 cycles = 25
@loopB                          ;coming in to loopB, we're at 25
        NOP                     ; +2 = 27
        
        STA     DISK_LATCHW,X   ;+5 = 32
        CMP     DISK_LATCHR,X   ; Start of new write... 4
        
        INY                     ;+2=6
        BEQ     @nextPage       ; +2 (no branch)=8/+3 (branch)=9
@LoadNext
        LDA     (ZP_STORPTR),Y  ;8+5=13
        BEQ     @doneWriting    ;+2=15 / +3=16
        JMP     @loopA          ;+3=18
@nextPage
        INC     ZP_STORPTR+1    ;9+6=15
        LDA     (ZP_STORPTR),Y  ;+5=20
        BEQ     @doneWriting2   ;+2=22 / +3=23
        JMP     @loopB          ;+3=25

@doneWriting                    ;coming in to @doneWriting we're at 16
        CMP     $00             ; waste +3 = 19
        NOP                     ; +2 = 21
        NOP                     ; +2 = 23
@doneWriting2                   ; on entry here we're at 23
        NOP                     ; +2 = 25
        NOP                     ; +2 = 27
        NOP                     ; +2 = 29
        CMP     $00             ; waste +3 = 32
        ;; Need to hit exactly 32 cycles *before* this DISK_MODER read
        LDA     DISK_MODER,X
        LDA     DISK_LATCHR,X
@rts        
        RTS

        ;; MakeTrackData: precompute a full track of nybblized data for
        ;; track (A), and store it at NYBDATA ($2000).
MakeTrackData
        STA     TARGETTRK
        ASL
        ASL
        ASL
        ASL
        STA     TARGETSEC
        LDA     #16
        STA     SECCOUNT        ; we want to write 16 sectors of nyb'd data
        
        LDA     #>NYBDATA
        STA     ZP_STORPTR+1     ; high address
        LDA     #<NYBDATA
        STA     ZP_STORPTR       ; low address

@NextSector
        JSR     MakeSectorData  ; will increment ZP_STORPTR by 388 as it goes

        INC     TARGETSEC
        DEC     SECCOUNT
        BNE     @NextSector

        ;; Stick a 00 at the end -- it's an illegal byte for writing to the
        ;;  disk controller, so we'll use it as the sentinel for when we've
        ;;  reached the end of the data to write
        LDY     #$01
        LDA     #$00
        STA     (ZP_STORPTR),Y
        RTS

        ;; MakeSectorData: Create one sector of nybblized data for
        ;;  track TARGETTRK, sector TARGETSEC and store it at
        ;;    ZP_STORPTR (low) +1 (high)
        ;; Increments ZP_STORPTR as it goes
        ;; trashes Y/A
MakeSectorData
        LDA     TARGETSEC
        JSR     F8ROM_AHEX
        LDY     #>SECTORMSG
        LDA     #<SECTORMSG
        JSR     Prtmsg
        
        LDY     #16             ; write 16 sync bytes
        LDA     #$FF
@LoopA
        DEY
        STA     (ZP_STORPTR),Y
        CPY     #00
        BNE     @LoopA

        ;; add 20 to ZP_STORPTR
        LDA     #16
        CLC
        ADC     ZP_STORPTR
        STA     ZP_STORPTR
        BCC     @SectorHeader
        INC     ZP_STORPTR+1
@SectorHeader
        ;; Emit the sector header
        LDY     #$00
        LDA     #$D5            ; 3 bytes of header prolog
        STA     (ZP_STORPTR),Y
        INY
        LDA     #$AA
        STA     (ZP_STORPTR),Y
        INY
        LDA     #$96
        STA     (ZP_STORPTR),Y
        INY

        LDA     #$FF            ; Volume number (255), 4-and-4 encoded
        STA     (ZP_STORPTR),Y
        INY
        STA     (ZP_STORPTR),Y
        INY

        LDA     TARGETTRK       ; Track number, 4-and-4 encoded
        JSR     _Store44
        LDA     TARGETSEC       ; Sector number, 4-and-4 encoded
        JSR     _Store44
        
        LDA     TARGETTRK       ; compute checksum
        EOR     TARGETSEC
        EOR     #$FF            ; (volume number)
        JSR     _Store44         ; Store it, 4-and-4 encoded

        LDA     #$DE            ; Sector header epilog
        STA     (ZP_STORPTR),Y
        INY
        LDA     #$AA
        STA     (ZP_STORPTR),Y
        INY
        LDA     #$EB
        STA     (ZP_STORPTR),Y
        INY

        ;; 3 gap bytes
        LDA     #$FF
        STA     (ZP_STORPTR),Y
        INY
        STA     (ZP_STORPTR),Y
        INY
        STA     (ZP_STORPTR),Y
        INY
        
        LDA     #$D5            ; Data prolog
        STA     (ZP_STORPTR),Y
        INY
        LDA     #$AA
        STA     (ZP_STORPTR),Y
        INY
        LDA     #$AD
        STA     (ZP_STORPTR),Y
        INY

        ;; Add 20 to ZP_STORPTR
        LDA     ZP_STORPTR
        CLC
        ADC     #20
        STA     ZP_STORPTR
        BCC     @SectorDataTime
        INC     ZP_STORPTR+1
        
@SectorDataTime
        LDY     #>SECTORMSG2
        LDA     #<SECTORMSG2
        JSR     Prtmsg

        LDY     #00
        
        ;; Now for the fun bit - 343 bytes of data! Prep a 256 byte sector
        ;;  and then translate it to 6-and-2 encoding (plus checksum),
        ;;  and store it at ZP_STORPTR
        ;; 
        ;; The test sector data is the value of (track + sector),
        ;;  incrementing 256 times. Easy to generate...
        LDA     TARGETTRK
        CLC
        ADC     TARGETSEC
@SectorLoop        
        STA     (ZP_SECTP),Y
        TAX
        INX
        TXA
        INY
        BNE     @SectorLoop     ; store 256 bytes

        ;; Convert and store as 6-and-2 with checksum
        JSR     Encode6and2
        ;; Encode6and2 added 0x157 to ZP_STORPTR, so we don't have to

        ;; data epilog
        LDY     #$00
        LDA     #$DE            ; Data prolog
        STA     (ZP_STORPTR),Y
        INY
        LDA     #$AA
        STA     (ZP_STORPTR),Y
        INY
        LDA     #$EB
        STA     (ZP_STORPTR),Y
        INY

        ;;  add 3 bytes to ZP_STORPTR
        LDA     #$03
        CLC
        ADC     ZP_STORPTR
        BCC     @finishnocarry
        INC     ZP_STORPTR+1
@finishnocarry
        STA     ZP_STORPTR
        RTS

        ;; Store A in (ZP_STORPTR),Y with 4-and-4 encoding (2 bytes)
        ;; increments Y by 2, trashes A and ZP_SCRATCH
_Store44
        STA     ZP_SCRATCH
        AND     #$AA
        LSR
        ORA     #$AA
        STA     (ZP_STORPTR),Y
        INY

        LDA     ZP_SCRATCH
        AND     #$55
        ORA     #$AA
        STA     (ZP_STORPTR),Y
        INY
        RTS

        ;; Encode6and2: given 256 bytes of data at SECDATA, 6-and-2
        ;; encode it in to 0x156 nybble-bytes at (ZP_STORPTR) with a
        ;; 1-byte checksum after (== 0x157 total).
        ;; Trashes A and Y and X
        ;; Updates ZP_STORPTR
        ;; trashes ZP_STOR2, ZP_TRANSP, IDX6, IDX2, CKSUM,
        ;;         ZP_SECTP
Encode6and2
        LDA     ZP_STORPTR      ; set up ZP_STOR2 to point 256 bytes above
        STA     ZP_STOR2        ; ZP_STORPTR for convenience, since we're 
        LDY     ZP_STORPTR+1    ; addressing an output buffer of > 0x100
        INY                     ; bytes
        STY     ZP_STOR2+1

        ;; Clear output buffer 0x157 bytes
        LDY     #>SECTORMSG3
        LDA     #<SECTORMSG3
        JSR     Prtmsg
        
        LDY     #$00
        LDA     #$00
        STA     CKSUM           ; convenient place to clear the checksum for later
@ClearLoop
        STA     (ZP_STORPTR),Y
        CPY     #$57            ; also clear at +0x100 if < 0x157
        BLT     @AlsoClearHigh  ;Y < #$57? Branch
@ClearNext
        INY
        BEQ     @ClearDone      ; if Y became 0, we rolled over and are done
        JMP     @ClearLoop
@AlsoClearHigh
        STA     (ZP_STOR2),Y
        JMP     @ClearNext
@ClearDone
        LDY     #>SECTORMSG4
        LDA     #<SECTORMSG4
        JSR     Prtmsg

        ;; Work through the 256 bytes of data to construct the 6and2 data,
        ;; with ZP_SECTP as the input and ZP_STORPTR as the output
        ;; 
        LDA     #$55
        STA     IDX2
        ;; for (idx6 = 0x0101; idx6 >= 0; idx6--)
        LDA     #$01
        STA     IDX6
        STA     IDX6+1          ; IDX6 = 0x0101
@WorkLoopA
        LDY     IDX6            ; val6 = input[idx6 & 0xFF]
        LDA     (ZP_SECTP),Y
        STA     VAL6

        LDY     IDX2            ; val2 = output[idx2];
        LDA     (ZP_STORPTR),Y
        STA     VAL2

        ;; val2 = (val2 << 1) | (val6 & 1); val6 >>= 1;
        LSR     VAL6+1
        ROR     VAL6            ; val6 >>= 1, and C = old (val6&1)
        LDA     VAL2
        ROL                     ; A = (val2 << 1) | C
        STA     VAL2            ; val2 = all that jazz

        ;; another round of the same
        LSR     VAL6+1
        ROR     VAL6            ; val6 >>= 1, and C = old (val6&1)
        LDA     VAL2
        ROL                     ; A = (val2 << 1) | C
        STA     VAL2            ; val2 = all that jazz
        
        LDY     IDX2            ; output[idx2] = val2
        LDA     VAL2
        STA     (ZP_STORPTR),Y
        
        ;; if (idx6 < 0x100) { output[0x56+idx6] = val6; }
        LDA     IDX6+1
        CMP     #01
        BEQ     @decidx2
        LDA     #$56
        CLC
        ADC     IDX6
        BCC     @storeLT100

        ;; we need to store in ZP_STOR2 (the result overflowed)
        TAY                     ; Y = idx6 + 0x56 and is >= 0x100
        LDA     VAL6
        STA     (ZP_STOR2),Y
        JMP     @decidx2

@storeLT100
        TAY                     ; Y = idx6 + 0x56 and is < 0x100
        LDA     VAL6
        STA     (ZP_STORPTR),Y
        JMP     @decidx2

@decidx2        
        ;; if (--idx2 < 0) { idx2 = 0x55; }
        ;; IDX2 never exceeds $55, so we can test using DEC and BMI/BPL
        DEC     IDX2
        BPL     @dontresetidx2
        ;; ... high bit is set, so we underflowed; reset IDX2 back to $55

        LDA     #$55
        STA     IDX2
@dontresetidx2

        ;; End of WorkLoopA: 16-bit decrement idx6, and loop to @WorkLoopA
        ;;  if it is >= 0
        LDA     IDX6            ; sets Z if it's zero
        BNE     @simpledeclo    ; if not zero, just decrement and continue
        LDA     IDX6+1          ; low was zero, so repeat w/ high
        BEQ     @DoneWorkloopA  ; if high is also zero we're done
        DEC     IDX6+1
@simpledeclo
        DEC     IDX6

        ;; continue loop
        JMP     @WorkLoopA

        ;; Both IDX6 and IDX6+1 reached 0, so we are done the loop
@DoneWorkloopA
        ;; Mask out the "extra" 2-bit data:
        ;; output[0x54] &= 0x0F; output[0x55] &= 0x0F;
        LDY     #$54
        LDA     (ZP_STORPTR),Y
        AND     #$0F
        STA     (ZP_STORPTR),Y
        INY
        AND     #$0F
        STA     (ZP_STORPTR),Y

        ;; Loop over the data one more time to construct the actual output
        ;; and compute the checksum
        ;; Checksum is initialized to 0 above
        ;; for (int idx6=0; idx6<0x156; idx6++)
        LDY     #>SECTORMSG5
        LDA     #<SECTORMSG5
        JSR     Prtmsg

        LDA     #$00
        STA     IDX6+1
        STA     IDX6
@WorkLoopB
        LDY     IDX6            ; val = output[idx]
        LDA     IDX6+1
        EOR     #$01
        BEQ     @LoadFromHi
        LDA     (ZP_STORPTR),Y
        JMP     @c
@LoadFromHi
        LDA     (ZP_STOR2),Y
@c
        STA     ZP_SCRATCH

        ;; output[idx] = _trans[cksum^val]
        LDA     CKSUM           ; Y = cksum ^ val
        EOR     ZP_SCRATCH
        TAY
        LDA     IDX6+1
        EOR     #$01            ; to set C for BEQ coming up
        BEQ     @StoreToHi      ; branch based on EOR (if IDX6+1 == 1)
        
        LDA     (ZP_TRANSP),Y   ; A = trans[cksum^val]
        LDY     IDX6            ; restore Y
        STA     (ZP_STORPTR),Y
        JMP     @d
@StoreToHi
        LDA     (ZP_TRANSP),Y   ; A = trans[cksum^val]
        LDY     IDX6            ; restore Y
        STA     (ZP_STOR2),Y
@d        
        ;; cksum = val;
        LDA     ZP_SCRATCH
        STA     CKSUM

        ;; Complete for loop: idx6++ and loop if idx6 < 0x156
        INC     IDX6
        BEQ     @incHigh
@f
        LDA     IDX6+1
        EOR     #$01
        BNE     @WorkLoopB      ; high byte isn't set yet, so continue looping
        LDA     IDX6
        CMP     #$56
        BGE     @setCksum       ; high byte set, and low >= 0x56 - done loop
        JMP     @WorkLoopB
@incHigh        
        INC     IDX6+1
        JMP     @f
        
@setCksum
        ;; output[342] = _trans[cksum]
        LDY     CKSUM           ; A = _trans[cksum]
        LDA     (ZP_TRANSP),Y
        LDY     #$56
        STA     (ZP_STOR2),Y    ; output[0x100 + Y] = A

        ;; Add 0x157 to ZP_STORPTR (number of bytes we added to the buffer)
        INC     ZP_STORPTR+1    ; add 0x100
        LDA     ZP_STORPTR
        CLC
        ADC     #$57
        BCC     @nocarry
        INC     ZP_STORPTR+1
@nocarry
        STA     ZP_STORPTR
        
@done        
        RTS

        
        ;; set CNT, CNT+1, DST, DST+1, VALUE before calling
memset
@a
	LDY	#0
	LDA	CNT
	ORA	CNT+1
	BEQ	@fin
	LDA	VALUE
	STA	(DST),y
	INC	DST
	BNE	@b
	INC	DST+1
@b
	DEC	CNT
	LDA	CNT
	CMP	#$FF
	BNE	@c
	DEC	CNT+1
@c
	SEC
	BCS	@a
@fin
	RTS
;;; ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; Start of data segment
;;; ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
STARTMSG
        ASC "Insert a blank floppy in s6d1 and press a key..."
        .byte   $8D, $00

SECTORMSG
        ASC " : Making this sector of data..."
        .byte $00
SECTORMSG2
        ASC "Building sector data chunk..."
        .byte $00
SECTORMSG3
        ASC "Clearing output buffer..."
        .byte $00
SECTORMSG4
        ASC "Constructing 6-and-2 data..."
        .byte $00
SECTORMSG5
        ASC "Building checksum..."
        .byte $00
ENDMSG
        ASC "Test complete, no errors found."
        .byte   $00
SECDATAMSG
        ASC " : sector data buffer"
        .byte   $00
NYBDATAMSG
        ASC " : nybble data buffer"
        .byte   $00

WPMSG
        ASC "Disk is write protected; aborting"
        .byte   $00
WRITEMSG
        ASC "Writing fresh track..."
        .byte   $00
        
;;; ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        ;; variable space
        ;;
        ;;  Many/all of these could be zero-page and not here.
        ;;  They also don't need to be initialized to zero so they
        ;;  could just be memory locations instead of byte defines.
;;; ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        
WAITCTR
        .byte $00, $00

CURHALFTRK
        .byte $00

DSTHALFTRK
        .byte $00

NYBCOUNT
        .byte $00

TARGETTRK
        .byte $00
TARGETSEC
        .byte $00
SECCOUNT
        .byte $00

IDX6
        .byte $00, $00
IDX2
        .byte $00
CKSUM
        .byte $00
VAL2
        .byte $00
VAL6
        .byte $00

CNT
        .byte $00, $00
VALUE
        .byte $00
        
;;; ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;       
        ;; Block data area
;;; ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
.align  256

        ;; 6-and-2 DOS3.3 translation table (here so it doesn't
        ;; cross a page boundary)
TRANS62
        .byte $96, $97, $9a, $9b, $9d, $9e, $9f, $a6
        .byte $a7, $ab, $ac, $ad, $ae, $af, $b2, $b3
        .byte $b4, $b5, $b6, $b7, $b9, $ba, $bb, $bc
        .byte $bd, $be, $bf, $cb, $cd, $ce, $cf, $d3
        .byte $d6, $d7, $d9, $da, $db, $dc, $dd, $de
        .byte $df, $e5, $e6, $e7, $e9, $ea, $eb, $ec
        .byte $ed, $ee, $ef, $f2, $f3, $f4, $f5, $f6
        .byte $f7, $f9, $fa, $fb, $fc, $fd, $fe, $ff

        ;; scratch space for a sector of data
        ;; FIXME: is there an easier way to define a block so that
        ;; ca65 honors it and shows it properly in the lst?
.align  256
SECDATA
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00
        .byte $00, $00, $00, $00, $00, $00, $00, $00

        ;; nybblized data that we precalculate
        ;; and then write to the track; or that we read in from the track
        ;; and store here
.align  256
NYBDATA
        .byte $00
        

