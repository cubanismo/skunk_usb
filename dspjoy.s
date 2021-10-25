			.include "jaguar.inc"
			.include "blitcode.inc"

			.globl	_startdsp
			.globl	_stopdsp
			.globl	_butsmem0
			.globl	_butsmem1
			.globl	_joyevover
			.globl	_joyevput
			.globl	_joyevget
			.globl	_joyevbuf
			.globl	_joydbuf

dspstack	.equ	D_ENDRAM
EVBUFSIZE	.equ	1024

			.68000
			.text

			; Blit the DSP code to DSP RAM and start the DSP
_startdsp:
			move.l	#dspcodex, d0		; Calculate size of DSP code in dwords
			sub.l	#dspcode, d0
			move.l	#dspcode, a0
			move.l	#D_RAM, a1

			jsr		blitcode

			; This sets up a timer period of approximately 100us
			move.w	#1329, JPIT1		; divider1 = 1330
			move.w	#1, JPIT2			; divider2 = 2

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

rdbtime		.equr	r10
rputidx		.equr	r11
rgetidx		.equr	r12
rtime		.equr	r13
rdbuf		.equr	r14
revbuf		.equr	r15
rold0		.equr	r16
rold1		.equr	r17
rjoy		.equr	r18
rbuts0		.equr	r19
rbuts1		.equr	r20
rmask0		.equr	r21
rmask1		.equr	r22
rmask2		.equr	r23
rbutsmem0	.equr	r24
rbutsmem1	.equr	r25
rsem		.equr	r26
roverflow	.equr	r27
risrflags	.equr	r28
risrtmp0	.equr	r29
risrflgptr	.equr	r30
isr_sp		.equr	r31

GT			ccdef	%10101

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
			movei	#dsptmr1, risrflags
			movei	#D_FLAGS, risrflgptr
			jump	(risrflags)
			load	(risrflgptr), risrflags

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

			movei	#_joyevput, r4
			moveq	#0, rputidx
			movei	#_joyevget, r5
			moveq	#0, rgetidx
			movei	#D_FLAGS, r6
			storew	rputidx, (r4)
			moveq	#0, rtime

			storew	rgetidx, (r5)
			load	(r6), r7
			bset	#6, r7

			movei	#dspsem, rsem	; Notify 68k we are initialized.
			moveq	#1, r2
			loadw	(rsem), r0		; Will load 0 into r0
			or		r2, r0			; Set r0 to 1, WAR DSP external write bug
			storew	r0, (rsem)
			store	r7, (r6)		; Enable D_TIM1ENA (Must be after rsem set)

			movei	#infinite, r2
			movei	#JOYSTICK, rjoy ; (+ 2 = JOYBUTS)
			movei	#D_MOD, r1
			movei	#_joyevbuf, revbuf
			movei	#overflow, roverflow

			; TODO Detect TeamTap by reading B0/B2 on row 1 of socket #3:
			; Will both be 0 if TeamTap is present, both 1 if not.
			; Note all rows should probably be read (Ignoring those != 1) to
			; avoid throwing off bank switching controllers.

			movei	#$00000003, rmask0
			movei	#$000003fc, rmask1
			movei	#$0000003f, rmask2

			movei	#_butsmem0, rbutsmem0
			movei	#_butsmem1, rbutsmem1

			movei	#~(EVBUFSIZE-1), r0	; (EVBUFSIZE/2) entry 2xDWORD event buf

			moveq	#0, rold0
			moveq	#0, rold1

			movei	#100, rdbtime
			movei	#_joydbuf, rdbuf
			moveq	#24, r3				; 24 buttons
			shlq	#2, r3				; * sizeof(dword)
.initdb:	subq	#4, r3
			jr		NE, .initdb
			store	rold0, (rdbuf+r3)

			store	r0, (r1)

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
;   XXXX 369# ot25 80Cs 147* BrRL DUAp
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

			; Build Debounce mask
			moveq	#23, r3
			shlq	#2, r3			; Multiply starting index by sizeof(dword)
			moveq	#0, r0
.builddb:	shlq	#1, r0			; Shift prev iter bit (if any) out of way
			load	(rdbuf+r3), r5
			cmp		rtime, r5
			jr		GT, .bouncing	; If r5/bounce timeout > rtime, ignore input
			nop
			bset	#0, r0			; Else, set the "change recognized" bit.
			store	rtime, (rdbuf+r3)	; Avoid timer wrapping issues
.bouncing:	subq	#4, r3
			jr		PL, .builddb
			nop

			movei	#_joyevget, r4
			loadw	(r4), rgetidx
			or		rgetidx, rgetidx	; WAR DSP store bug?
			store	rbuts0, (rbutsmem0)	; Save rbuts0 to DSP memory
			moveq	#0, r4
			xor		rbuts0, rold0	; diff against data from last iteration
			move	rold0, r6		; Copy the diff into r6
			and		r0, rold0		; Debounce the state diff
			xor		rold0, r6		; Diff the raw diff Vs the debounced diff
			xor		r6, rbuts0		; Debounce rbuts0
			store	rbuts1, (rbutsmem1)	; Save rbuts1 to DSP memory
			or		r4, r4			; In case no events, WAR unused reg bug
.butn0loop:	moveq	#1, r3
			and		rold0, r3		; See if current bit changed
			movei	#.butn0loop, r6	; Save loop target in r6
			jr		EQ, .cont0		; If not, continue
			move	rbuts0, r5		; Copy rbuts0 to r5
			and		r3, r5			; Set bit 0 to 0 if down->up, 1 if up->down
			shlq	#12, r5			; Stash it in bit 12
			or		r4, r5			; Stash the button number in first byte
			store	r5, (revbuf+rputidx)	; Store event
			addq	#4, rputidx		; Move to the time dword
			move	rtime, r7		; Make a copy of current time
			store	r7, (revbuf+rputidx)	; Store event timestamp
			move	r4, r8			; Get a copy of button number in r8
			add		rdbtime, r7		; Ignore changes on this button for ~10ms
			shlq	#2, r8			; Multiply button number by sizeof(dword)
			store	r7, (rdbuf+r8)	; Store bounce timeout in _joydbuf[button]
			addqmod	#4, rputidx		; Increment put pointer. Wrap if needed.
			cmp		rputidx, rgetidx; Check if we're caught up with getter
			jump	EQ, (roverflow)	; Go to overflow handler if so.
.cont0:		shrq	#1, rold0		; Executed even on overflow
			jr		EQ, .done0		; If no deltas left, exit.
			nop						; Must not rotate if r4 not incremented
			rorq	#1, rbuts0
			jump	(r6)
			addq	#1, r4

.done0:		neg		r4				; Restore rbuts0
			addq	#32, r4
			ror		r4, rbuts0
			move	rbuts0, rold0	; And save it in rold0

.butn1:		move	rbuts1, rold1	; Save rbuts1 in rold0
			; TODO Make above a macro, stamp it out again here for port 1

			movei	#_joyevput, r4
			jr		infinite
			storew	rputidx, (r4)

overflow:	movei	#_joyevover, r3	; Load address of overflow flag
			loadw	(r3), r4		; Load it (WAR DSP store bug)
			moveq	#1, r2			; Load a 1 in r2
			or		r4, r4			; Wait for load (WAR DSP store bug)
			storew	r2, (r3)		; Store 1 in overflow flag.

infinite:	loadw	(rsem), r1		; See if we need to exit
			movei	#$807E, r0		; Select row 0 (NOTE! Audio muted)

			cmpq	#0, r1			; Has dspsem been cleared?
			jr		EQ, stopdsp		; If dspsem was cleared, stop the GPU
			nop

			movei	#mainloop, r2
			jump	(r2)
			storew	r0, (rjoy)

stopdsp:	movei	#D_FLAGS, r1	; Wait for intr handler to disable itself
.idleintr:	load	(r1), r2
			movei	#D_TIM1ENA, r0
			and		r2, r0
			jr		NE, .idleintr
			nop
			movei	#D_CTRL, r1		; Then stop the processor
			load	(r1), r2		; WAR DSP store bug?
			moveq	#0, r0
			and		r2, r0			; WAR DSP store bug?
			store	r0, (r1)
			nop
			nop

dsptmr1:	addq	#1, rtime		; Add one to time value
			loadw	(rsem), risrtmp0; Check rsem value

			cmpq	#0, risrtmp0

			jr		NE, .endintr
			nop
			bclr	#6, risrflags	; Disable this interrupt

.endintr:	; Get out of here,
			bclr	#3, risrflags
			bset	#11, risrflags
			load	(isr_sp), risrtmp0
			addq	#2, risrtmp0
			addq	#4, isr_sp
			jump	(risrtmp0)
			store	risrflags, (risrflgptr)
dspdata:

			.phrase
; Data stored in DSP memory
_butsmem0:	.ds.l	1
_butsmem1:	.ds.l	1
_joydbuf:	.ds.l	24
_joyevbuf:	.ds.l	EVBUFSIZE

			.68000
dspcodex:

.print "dspcode size: ",/u/w (dspdata-D_RAM), " bytes."
.print "dspcode+dspdata size: ",/u/w (dspcodex-dspcode), " bytes."

			.bss
			.long

dspsem:		.ds.w	1
_joyevover:	.ds.w	1
_joyevput:	.ds.w	1
_joyevget:	.ds.w	1
