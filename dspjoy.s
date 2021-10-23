			.include "jaguar.inc"
			.include "blitcode.inc"

			.globl	_startdsp
			.globl	_stopdsp
			.globl	_butsmem0
			.globl	_butsmem1

dspstack	.equ	D_ENDRAM

			.68000
			.text

			; Blit the DSP code to DSP RAM and start the DSP
_startdsp:
			move.l	#dspcodex, d0		; Calculate size of DSP code in dwords
			sub.l	#dspcode, d0
			move.l	#dspcode, a0
			move.l	#D_RAM, a1

			jsr		blitcode

			clr.w	dspsem				; Initialize the DSP semaphore
			move.l	#dspmain, D_PC		; Start the DSP init code
			move.l	#DSPGO, D_CTRL

.waitinit:	move.w	dspsem, d0			; Wait for the DSP init code to finish
			beq		.waitinit

			rts

_stopdsp:
			clr.w	dspsem				; Tell the DSP its time to stop

.waitdsp:	move.l	D_CTRL, d0
			andi.l	#DSPGO, d0
			bne		.waitdsp

			rts

			.phrase
dspcode:
			.dsp

isr_sp		.equr	r31
rjoy		.equr	r20
rbuts0		.equr	r21
rbuts1		.equr	r22
rmask0		.equr	r23
rmask1		.equr	r24
rmask2		.equr	r25
rbutsmem0	.equr	r26
rbutsmem1	.equr	r27
rsem		.equr	r28

;;
;; Each GPU interrupt vector entry is 16 bytes (8 16-bit words)
;; Most GPU/DSP instructions are one word. movei is 1 word + 2 words data
;; Hence, interrupt vector entries are generally 8 instructions long.
;;
			.org	D_RAM

; 68k/GPU->DSP interrupt
			nop
			nop
			nop
			nop
			nop
			nop
			nop
			nop

; I2S Serial Interface
			nop
			nop
			nop
			nop
			nop
			nop
			nop
			nop

; Timer Interrupt 1
			nop
			nop
			nop
			nop
			nop
			nop
			nop
			nop

; Timer Interrupt 2
			nop
			nop
			nop
			nop
			nop
			nop
			nop
			nop

; External Interrupt 0
			nop
			nop
			nop
			nop
			nop
			nop
			nop
			nop

; External Interrupt 1
			nop
			nop
			nop
			nop
			nop
			nop
			nop
			nop

dspmain:
			movei	#dspstack, isr_sp

			movei	#dspsem, rsem	; Notify 68k we are initialized.
			moveq	#1, r2
			loadw	(rsem), r0		; Will load 0 into r0
			or		r2, r0			; Set r0 to 1, WAR DSP external write bug
			storew	r0, (rsem)

			movei	#infinite, r2
			movei	#JOYSTICK, rjoy ; (+ 2 = JOYBUTS)

			; TODO Detect TeamTap by reading B0/B2 on row 1 of socket #3:
			; Will both be 0 if TeamTap is present, both 1 if not.
			; Note all rows should probably be read (Ignoring those != 1) to
			; avoid throwing off bank switching controllers.

			movei	#$00000003, rmask0
			movei	#$000003fc, rmask1
			movei	#$0000003f, rmask2

			movei	#_butsmem0, rbutsmem0
			movei	#_butsmem1, rbutsmem1

			jump	(r2)

; PARSEBUTNS - Parse a row of joystick buttons and shift them into a slot
;
; This macro parses a raw dword of the form JOYSTICK:JOYBUTS, extracting the 6
; bits of joystick state it contains for each port, and packs those 6 bits into
; the bottom of a register for each port, then optionally shifts that register
; right by some amount.
;
; The intended use case is calling this macro 4 times on the same set of
; registers for each possible pair of joysticks (1 pair without a team tap, 4
; pairs with two team taps), passing the same registers for each pair's rows.
; The result will be 24 bits of packed data in a corresponding register for each
; joystick, with each register having the same layout:
;
;   XXXX ot36 9#Cs 2580 147* BrRL DUAp
;
; Where:
;
;   r = C1
;   s = C2
;   t = C3
;   0 - 9, *, # = The corresponding keypad buttons
;   A - C = The corresponding buttons
;   U = Up
;   D = Down
;   L = Left
;   R = Right
;   o = Option
;   p = Pause
;
; Parameters (All passed as registers, all clobbered):
;
;   raw - The raw JOYSTICK:JOYBUTS data read from the corresponding HW registers
;   tmp0 - A temporary register used within the macro
;   tmp1 - Another temporary register used within the macro
;   buts0 - Packed output for port 0/joypad 0
;   buts1 - Packed output for port 1/joypad 1
;   shift - [Optional] buts0/buts1 are shifted right this # of bits if present
.macro PARSEBUTNS raw, tmp0, tmp1, buts0, buts1, shift
			not		\raw			; 0 == pressed? Hard to use. Invert it.
			move	\raw, \tmp0		; Move b1-b0 into rbuts0 1-0
			shrq	#2, \raw 		; Shift b3-b2 -> r1 bits 1-0
			and		rmask0, \tmp0	; Clear garbage out of rbuts0 31-2
			move	\raw, \tmp1		; Move b3-b2 -> rbuts1 1-0
			shrq	#20, \raw		; Shift j11-j8 -> r1 bits 5-2
			and		rmask1, \raw	; Clear garbage out of r1 1-0
			and		rmask0, \tmp1	; Clear garbage out of rbuts1 31-2
			or		\raw, \tmp0		; Or j11-j8 -> rbuts0 5-2
			shrq	#4, \raw		; Shift j15-j12 -> r1 bits 5-2
			and		rmask2, \tmp0	; Clear garbage out of rbuts0 9-6
			and		rmask1, \raw	; Clear garbage out of r1 1-0
			or		\raw, \tmp1		; Or j153-j12 -> rbuts1 5-2
.if \?shift
			shlq	#\shift, \tmp0	; Shift bits requested amount
			shlq	#\shift, \tmp1	; Shift bits requested amount
.endif
			or		\tmp0, \buts0	; Store port0 result
			or		\tmp1, \buts1	; Store port1 result
.endm

mainloop:	moveq	#0, rbuts0		; Clear rbuts0
			moveq	#0, rbuts1		; Clear rbuts1
			load	(rjoy), r1		; row0 JOYSTICK:JOYBUTS -> r1(hi):r1(lo)
			movei	#$80BD, r0		; Select row 1 (NOTE! Audio muted)
			storew	r0, (rjoy)

			; Process row0 button state from port 0 & 1 in r1
			PARSEBUTNS	r1, r2, r3, rbuts0, rbuts1

			load	(rjoy), r1		; row1 JOYSTICK:JOYBUTS -> r1(hi):r1(lo)
			movei	#$80DB, r0		; Select row 2 (NOTE! Audio muted)
			storew	r0, (rjoy)

			; Process row1 button state from port 0 & 1 in r1
			PARSEBUTNS	r1, r2, r3, rbuts0, rbuts1, 6

			load	(rjoy), r1		; row2 JOYSTICK:JOYBUTS -> r1(hi):r1(lo)
			movei	#$80E7, r0		; Select row 3 (NOTE! Audio muted)
			storew	r0, (rjoy)

			; Process row2 button state from port 0 & 1 in r1
			PARSEBUTNS	r1, r2, r3, rbuts0, rbuts1, 12

			load	(rjoy), r1		; row3 JOYSTICK:JOYBUTS -> r1(hi):r1(lo)

			; Process row3 button state from port 0 & 1 in r1
			PARSEBUTNS	r1, r2, r3, rbuts0, rbuts1, 18

infinite:	load	(rsem), r1		; See if we need to exit
			movei	#mainloop, r2
			movei	#$807E, r0		; Select row 0 (NOTE! Audio muted)

			; TODO: Build an actual event queue, diff Vs. previous iteration,
			; generate events from diff and associate timestamps with them.

			cmpq	#0, r1			; Has dspsem been cleared?
			store	rbuts0, (rbutsmem0)	; Save rbuts0 to DSP memory
			jr		EQ, stopdsp		; If dspsem was cleared, stop the GPU
			store	rbuts1, (rbutsmem1)	; Save rbuts1 to DSP memory

			jump	(r2)
			storew	r0, (rjoy)

stopdsp:	movei	#D_CTRL, r1
			load	(r1), r2		; WAR DSP store bug?
			moveq	#0, r0
			and		r2, r0			; WAR DSP store bug?
			store	r0, (r1)
			nop
			nop

			.phrase
; Data stored in DSP memory
_butsmem0:	.ds.l	1
_butsmem1:	.ds.l	1


			.68000
dspcodex:

			.bss
			.long

dspsem:		.ds.w	1
