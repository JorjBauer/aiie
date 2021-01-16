	.ORG	$C400
Entry:
	BIT	$FF58		; well-known RTS location - where RTS is $60
	BVS	PREntry		; always branches (used to relocatably branch)

	.ORG	$C405
INEntry:
	SEC
	BCC	PREntry		; never happens, but has a magic ID byte @ C407 == 18
	CLV
	BVC	PREntry		; always branches

	;; lookup table
	.ORG	$C40B
	.byte	$01		; magic identifier
	.byte	$20		; magic identifier
	.byte	.LOBYTE(ExitWithError)
	.byte	.LOBYTE(ExitWithError)
	.byte	.LOBYTE(ExitWithError)
	.byte	.LOBYTE(ExitWithError)
	.byte	$00
	.byte	.LOBYTE(SetMouse)
	.byte	.LOBYTE(ServeMouse)
	.byte	.LOBYTE(ReadMouse)
	.byte	.LOBYTE(ClearMouse)
	.byte	.LOBYTE(PosMouse)
	.byte	.LOBYTE(ClampMouse)
	.byte	.LOBYTE(HomeMouse)
	.byte	.LOBYTE(InitMouse)
	.byte	.LOBYTE(ExitWithError)
	.byte	.LOBYTE(ExitWithError)
	.byte	.LOBYTE(ExitWithError)
	.byte	.LOBYTE(ExitWithError)
	.byte	.LOBYTE(ExitWithError)
	.byte	.LOBYTE(ExitWithError)

	.ORG	$C420
PREntry:
	PHA			; save Y, X, A, S on the stack
	TYA
	PHA
	TXA
	PHA
	PHP
	SEI			; turn off interrupts while we find our address via the stack
	JSR	$FF58		; well-known RTS
	TSX			; grab stack pointer
	LDA	$0100,X		; inspect the stack frame
	TAX			; now A and X hold the high byte of the return address to *us*
	ASL			; A <<= 4
	ASL
	ASL
	ASL	
	TAY			; so Y = $n0, while X = $Cn for whatever slot we're in
	PLP			; pull Status off the stack
	BVC	lastChance
	LDA	$38		; We're being invoked in an IN# context, not a PR# context.
	BNE	PopAndReturn	; if $38/$39 don't point to us, then exit
	TXA
	EOR	$39		; is $39 == $Cn?
	BNE	PopAndReturn
InstallInHandler:	
	LDA	#$05		; Update IN handler to point to $Cn05 instead of $Cn00, so we know we've
	STA	$38		;initialized it
	BNE	INHandler
lastChance:	
	BCS	INHandler
PopAndReturn:	
	PLA
	TAX
	PLA
	NOP
	PLA
	STA	$c080,Y		; call soft-switch #0 for our slot to set mouse state
	RTS
INHandler:
	STA	$c081,Y		; call soft-switch #1 for our slot to fill the keyboard buffer w/ mouse info
	PLA
	LDA	$0638,X		; pull the string length from this magic screen hole (X == $Cn)
	TAX			;   which we'll return to the caller in X
	PLA			; restore Y
	TAY
	PLA			; restore A
	LDA	$200,X		; Grab the last character entered in to the buffer by the magic switch call
	RTS

SetMouse:
	CMP	#$10		; values >= $10 are invalid
	BCS	ExitWithError
	STA	$C0CF		; soft switch 0x0F, hard-coded slot 4 for now
	RTS

ExitWithError:
	SEC			; the spec says carry is set on errors
	RTS
	
ServeMouse:
	PHA
	SEI			; disable interrupts while we find out about interrupts
	STA	$C0CE		; soft switch 0x0E, hard-coded slot 4 for now
	LDX	#$04
	LDA	$6B8,X		; check what interrupts we serviced
	AND	#$0E
	BNE	_sm1		; if we serviced any, leave carry clear
	SEC			; ... but set carry if we serviced none
_sm1:	
	PLA
	RTS

ReadMouse:
	STA	$C0CB		; soft switch 0x0B, hard-coded slot 4 for now
	CLC
	RTS

ClearMouse:
	STA	$C0CA		; soft switch 0x0A, hard-coded slot 4 for now
	CLC
	RTS

PosMouse:
	STA	$C0C9		; soft switch 0x09, hard-coded slot 4 for now
	CLC
	RTS

ClampMouse:
	CMP	#$02
	BCS	ExitWithError
	STA	$C0CD		; soft switch 0x0D, hard-coded slot 4 for now
	CLC
	RTS

HomeMouse:
	STA	$C0C8		; soft switch 0x08, hard-coded slot 4 for now
	CLC
	RTS

InitMouse:	
	STA	$C0CC		; soft switch 0x0C, hard-coded slot 4 for now
	CLC
	RTS

	.ORG	$C4FB
	.byte	$D6		; magic identifier
	.ORG 	$C4FF
	.byte	$01		; version
	
