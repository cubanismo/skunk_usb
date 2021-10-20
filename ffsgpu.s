			.include "jaguar.inc"
			.include "ffsobj.inc"

			.globl	gpucode
			.globl	gpucodex
			.globl	gpuinit

			.globl	_startgpu
			.globl	_testgpu
			.globl	_stopgpu
			.globl	_clrgamelst
			.globl	_drawstring
			.globl	_invertrect

; These are all hard-coded from the clr6x12.jft font for now.
CHR_WIDTH	.equ	6
CHR_HEIGHT	.equ	12
FNTWIDTH	.equ	WID96				; WID768 / 8 per comment in drawstring
FNTFIRSTCHR	.equ	$20
FNTLASTCHR	.equ	$7f

gpustack	.equ	G_ENDRAM

			.68000
			.text

			; Blit the GPU code to GPU RAM and start the GPU
_startgpu:
			move.l	#gpucodex, d0		; Calculate size of GPU code in dwords
			sub.l	#gpucode, d0
			add.l	#3, d0
			lsr.l	#2, d0

			move.l	#0, A1_CLIP			; Don't clip blitter writes

			move.l	#G_RAM+$8000, A1_BASE	; destination = 32b view of G_RAM
			move.l	#gpucode, A2_BASE	; source

			move.l	#XADDPHR|PIXEL32|WID2048|PITCH1, A1_FLAGS
			move.l	#XADDPHR|PIXEL32|WID2048|PITCH1, A2_FLAGS
			move.l	#0, A1_PIXEL
			move.l	#0, A2_PIXEL

			or.l	#1<<16, d0			; Loop 1x(gpu code size in dwords) times
			move.l	d0, B_COUNT

			move.l	#SRCEN|UPDA1|UPDA2|LFU_REPLACE, B_CMD

.waitblit:	move.l	B_CMD, d0			; Wait for the GPU code blit to complete
			andi.l	#1, d0
			beq		.waitblit

			move.l	#gputocpuint, LEVEL0	; Install GPU->CPU interrupt handler
			move.w	#C_GPUENA, INT1		; Enable the GPU interrupt

			clr.w	gpucmd				; Clear the gpucmd message
			clr.w	_gpusem				; Initialize the GPU semaphore
			move.l	#gpuinit, G_PC		; Start the GPU init code
			move.l	#RISCGO, G_CTRL

.waitinit:	move.w	_gpusem, d0			; Wait for the GPU init code to finish
			beq		.waitinit

			rts

_testgpu:
			moveq	#0, d0				; Clear high word of d0
			move.w	_gpusem, d1			; Load initial value of _gpusem to d0
			move.l	#RISCGO|FORCEINT0, G_CTRL	; Force a CPU interrupt on GPU
.waitintr:	move.w	_gpusem, d0			; Wait for _gpusem to change
			cmp.w	d1, d0
			beq		.waitintr

			rts

_stopgpu:	; Tell the GPU to stop
			move.w	#GCMD_STOP, gpucmd

.waitgpu:	move.l	G_CTRL, d0			; Wait for the GPU to go to sleep
			andi.l	#RISCGO, d0
			bne		.waitgpu

			rts

_clrgamelst: ; Tell the GPU to clear _gamelstbm
			move.w	#GCMD_CLRLIST, gpucmd
			jmp		waitgpu

_drawstring: ; Draw a string
			move.l	4(sp), surfaddr	; Param 0: font data address
			move.l	8(sp), coords	; Param 1: coordinates, packed as (y<<16)|x
			move.l	12(sp), stringaddr	; Param 2: NUL-terminated string address

			move.w	#GCMD_DRAWSTRING, gpucmd
			jmp		waitgpu

_invertrect: ; Invert a rectangle of 1bpp pixels
			move.l	4(sp), surfaddr	; Param 0: font data address
			move.l	8(sp), coords	; Param 1: coordinates, packed as (y<<16)|x
			move.l	12(sp), size	; Param 2: rect size, packedas (h<<16|w)

			move.w	#GCMD_INVERTRECT, gpucmd
			jmp		waitgpu

waitgpu:	stop	#$2000			; Enable 68k interrupt then halt
			;
			; An interrupt will resume the 68k on the next instr after the
			; appropriate interrupt handler runs.
			;
			move.w	#$2700, sr		; Disable 68k interrupts
			rts

; 68k Interrupt handler. Doesn't need to do anything but clear the interrupt
; bits. The only interrupt enabled in INT1 should be the GPU one, and its only
; purpose is to bring the 68k out of the STOP state when the GPU is done with
; some work.
gputocpuint:
			move.w	#C_GPUCLR|C_GPUENA, INT1	; Clear GPU interrupt
			move.w	#0, INT2					; Lower 68k bus priority
			rte

			.phrase
gpucode:
			.gpu
			.org G_RAM


isr_sp		.equr	r31
isr_reg0	.equr	r30
isr_reg1	.equr	r29
isr_reg2	.equr	r28
isr_reg3	.equr	r27
isr_reg4	.equr	r26
isr_reg5	.equr	r25
isr_reg6	.equr	r24
isr_regi	.equr	r15

rgpucmd		.equr	r13

; The GPU code will recognize these commands when stored in gpucmd
GCMD_STOP		.equ	1
GCMD_CLRLIST	.equ	2
GCMD_DRAWSTRING	.equ	3
GCMD_INVERTRECT	.equ	4

;;
;; Each GPU interrupt vector entry is 16 bytes (8 16-bit words)
;; Most GPU/DSP instructions are one word. movei is 1 word + 2 words data
;; Hence, interrupt vector entries are generally 8 instructions long.
;;

; 68k/CPU->GPU interrupt
			movei	#gpucpuint, isr_reg0
			jump	(isr_reg0)
			nop
			nop
			nop
			nop

; DSP interrupt
			nop
			nop
			nop
			nop
			nop
			nop
			nop
			nop

; PIT (timer) interrupt
			nop
			nop
			nop
			nop
			nop
			nop
			nop
			nop

; Object Processor interrupt
			movei	#gpuopint, isr_reg0
			jump	(isr_reg0)
			nop
			nop
			nop
			nop

; Blitter interrupt
			nop
			nop
			nop
			nop
			nop
			nop
			nop
			nop
; End GPU interrupt vector

; GPU init routine
gpuinit:
			movei	#gpustack, isr_sp

			; Initialize ticks
			movei	#_ticks, r3
			moveq	#0, r2
			store	r2, (r3)

			; Enable CPU and Object Processorinterrupts
			movei	#G_FLAGS, r0
			movei	#G_CPUENA|G_OPENA, r1
			load	(r0), r2
			or		r2, r1
			store	r1, (r0)

			; Overwrite the first scaled bitmap object with a stop
			; object. The object processor interrupt overwritess
			; the first phrase of bitmap objects every time it runs,
			; so this will cause graphics to appear on the first
			; frame after the object processor is started.
			movei	#listbuf+BMLOGO_OFF, r14
			moveq	#$0, r1
			moveq	#$C, r2
			store	r1, (r14)
			store	r2, (r14+1)			; store to r14 + (1 long word)

			; Init the Object Processor List Pointer
			movei	#listbuf+LIST_START_OFF, r0
			movei	#OLP, r1
			rorq	#16, r0				; Swap address for OLP
			store	r0, (r1)

			; Set up a black/white CLUT
			movei	#CLUT, r2
			movei	#$0000FFFF, r1		; RGB black/white
			;movei	#$000077FF, r1		; CRY black/white
			store	r1, (r2)

			; Set the video mode
			movei	#PWIDTH4|BGEN|CSYNC|RGB16|VIDEN, r2
			movei	#VMODE, r0
			storew	r2, (r0)

			movei	#_gpusem, r0		; Indicate init complete
			moveq	#1, r1
			storew	r1, (r0)

			movei	#gpucmd, rgpucmd
infinite:	loadw	(rgpucmd), r1
			cmpq	#0, r1
			jr		EQ, infinite

			cmpq	#GCMD_STOP, r1
			jr		NE, .notstop
			nop

			movei	#stopgpu, r0
			jump	(r0)
.notstop:
			cmpq	#GCMD_CLRLIST, r1
			jr		NE, .notclrlist
			nop

			movei	#clrlist, r0
			jump	(r0)
.notclrlist:
			cmpq	#GCMD_DRAWSTRING, r1
			jr		NE, .notdrawstring
			nop

			movei	#drawstring, r0
			jump	(r0)

.notdrawstring:
			cmpq	#GCMD_INVERTRECT, r1
			jr		NE, .notinvertrect
			nop

			movei	#invertrect, r0
			jump	(r0)
			nop

.notinvertrect:
			; Unknown command. Clear it and continue.
			moveq	#0, r1
			storew	r1, (rgpucmd)
			movei	#infinite, r0
			jump	(r0)
			nop

stopgpu:	; Park the OLP on a stop object
			movei	#_ticks, r4			; &_ticks->r4
			movei	#OBF, r2			; &OBF->r2
			movei	#OLP, r3			; &OLP->r3
			movei	#$FF0, r0			; Re-use the skunk's stop object list

			load	(r4), r5			; ticks->r5
.waittick:	load	(r4), r6			; ticks->r6
			cmp		r5, r6
			jr		EQ, .waittick		; Wait until vblank
			nop

			rorq	#16, r0				; Swap address words for OLP
			moveq	#0, r1
			store	r0, (r3)			; Set OLP
			storew	r1, (r2)			; Clear OBF

			; Stop GPU
			moveq	#0, r0
			movei	#G_CTRL, r1
			store	r0, (r1)
			nop
			nop

clrlist:	; Clear the game list bitmap
			moveq	#0, r0				; 0 will be stored in various fields

			movei	#_gamelstbm, r1
			movei	#A1_BASE, r2

			movei	#A1_CLIP, r3

			store	r1, (r2)			; Store A1_BASE (destination addr)
			store	r0, (r3)			; Store 0 in A1_CLIP (No clipping)

			.assert GL_WIDTH = 192
			movei	#PITCH1|PIXEL1|WID192|XADDPIX|YADD0, r4
			movei	#A1_FLAGS, r5

			movei	#A1_PIXEL, r6
			movei	#A1_FPIXEL, r7

			; Add (1, -GL_WIDTH) to x, y pointers after each inner loop iter
			movei	#(1<<16)|((-GL_WIDTH)&$ffff), r8
			movei	#A1_STEP, r9
			movei	#A1_FSTEP, r10
			movei	#A1_FPIXEL, r11

			store	r4, (r5)			; Store A1_FLAGS
			store	r0, (r6)			; Store 0 in A1_PIXEL
			store	r0, (r7)			; Store 0 in A1_FPIXEL
			store	r8, (r9)			; Store (1, -GL_WIDTH)->A1_STEP
			store	r0, (r10)			; Store 0 in A1_FSTEP
			store	r0, (r11)			; Store 0 in A1_FPIXEL

			movei	#(GL_HEIGHT<<16)|GL_WIDTH, r1
			movei	#B_COUNT, r2

			movei	#DSTEN|UPDA1|LFU_CLEAR, r3	; 1bit pixels need DSTEN
			movei	#B_CMD, r4

			store	r1, (r2)			; Write loop dimensions to B_COUNT
			store	r3, (r4)			; Write op to B_CMD

.waitblit:
			load	(r4), r0			; Read back blit status
			btst	#0, r0				; See if bit 0 is set
			jr		EQ, .waitblit

			; Done. Clear command, interrupt 68k, and return to main loop
			moveq	#0, r1
			movei	#G_CTRL, r2
			movei	#infinite, r0
			load	(r2), r3		; Load G_CTRL into r3
			storew	r1, (rgpucmd)	; Clear gpucmd
			bset	#1, r3			; Set CPUINT bit in r3
			jump	(r0)			; Jump back to main loop
			store	r3, (r2)		; Write G_CTRL, causing 68k interrupt

drawstring:	; Write a NUL-terminated string to the game list
			;  surfaddr:   The  surface to draw to
			;  coords:     The packed coordinates (y<<16)|x
			;  stringaddr: Pointer to the NUL-terminated string
			;  fontdata:   The 1bpp font surface
			moveq	#0, r0				; 0 will be stored in various fields

			movei	#surfaddr, r6		; Surface address pointer -> r6
			movei	#A1_BASE, r2

			load	(r6), r1			; Load dst surface address into r1

			movei	#A1_CLIP, r3
			movei	#$ffffffff, r4
			movei	#B_PATD, r5

			store	r1, (r2)			; Store A1_BASE (destination addr)
			store	r4, (r5)			; Store white in B_PATD low dword
			addq	#4, r5				; Point r5 at 2nd dword of B_PATD
			store	r0, (r3)			; Store 0 in A1_CLIP (No clipping)
			store	r4, (r5)			; Store white in B_PATD high dword

			.assert GL_WIDTH = 192
			movei	#PITCH1|PIXEL1|WID192|XADDPIX|YADD0, r4
			movei	#A1_FLAGS, r5

			movei	#A1_FPIXEL, r7

			; Add (-CHR_WIDTH, 1) to dst x, y pointers after each inner loop iter
			movei	#(1<<16)|((-CHR_WIDTH)&$ffff), r8
			movei	#A1_STEP, r9
			movei	#A1_FSTEP, r10

			store	r4, (r5)			; Store A1_FLAGS
			store	r0, (r7)			; Store 0 in A1_FPIXEL
			store	r8, (r9)			; Store (-CHR_WIDTH, 1) in A1_STEP
			store	r0, (r10)			; Store 0 in A1_FSTEP

			movei	#fontdata, r4
			movei	#A2_BASE, r5
			movei	#coords, r11
			; Use PIXEL8 even though we're reading 1bit pixels. This, combined
			; with the below SRCENX + !SRCEN command, FNTWIDTH set to actual
			; divided by 8, fixed A2_STEP of -1, 1, and blit width of at most 8
			; is a trick to get the blitter to:
			;  1) Read 8 pixels of data at a time (PIXEL8 instead of PIXEL1)
			;  2) Read source data only once per inner loop iteration (SRCENX)
			;  3) Do bit->pixel expansion even with <8bpp dst pixels
			; which allows using the bit comparator even for 1/2/4bpp dst
			; pixels. Note for fonts with a character width >8, this trick would
			; require multiple blits per character, since the inner loop must be
			; clamped to 8 bits/1 byte given source data is only read once. This
			; case is not implemented here.
			movei	#PITCH1|PIXEL8|FNTWIDTH|XADDPIX|YADD0, r6
			movei	#A2_FLAGS, r7
			movei	#(1<<16)|((-1)&$ffff), r8
			movei	#A2_STEP, r9

			store	r4, (r5)			; Store fontdata in A2_BASE
			store	r6, (r7)			; Store A2_FLAGS
			store	r8, (r9)			; Store (-1, 1) in A2_STEP

			load	(r11), r1			; Load 1st dst pixel location in r1
			movei	#A1_PIXEL, r2
			movei	#(CHR_HEIGHT<<16)|CHR_WIDTH, r3
			movei	#B_COUNT, r4
			; r5 = B_CMD value
			; Notes:
			;  -1bit pixels need DSTEN,
			;  -SRCENX means read a pixel from src on first inner loop iteration
			;  -!SRCEN means no pixels read on other inner loop iterations
			;  -BCOMPEN uses bitmask to decide which pixels to actually write
			movei	#SRCENX|DSTEN|UPDA1|UPDA2|PATDSEL|BCOMPEN, r5;
			movei	#B_CMD, r6
			movei	#stringaddr, r11	; Load string pointer in r11
			movei	#FNTFIRSTCHR, r8	; Load first char idx of font in r8
			movei	#FNTLASTCHR, r9		; Load last char idx of font in r9
			movei	#A2_PIXEL, r10

			load	(r11), r12			; Load string address in r12
			jr		.nextchr			; Jump into loop
			xor		r11, r11			; clear r11

.blitloop:	store	r7, (r10)			; Store src pixel loc in A2_PIXEL
			store	r1, (r2)			; Store dst pixel loc in A1_PIXEL
			store	r3, (r4)			; Write loop dimensions to B_COUNT
			store	r5, (r6)			; Write op to B_CMD
			addq	#CHR_WIDTH, r1		; Add CHR_WIDTH to dst pixel location

.nextchr:	loadb	(r12), r7			; Load next character
			cmpq	#0, r7				; At NUL terminator?
			jr		EQ, .waitlast		; if yes, wait for the last blit
			cmp		r9, r7				; Compare to last char
			addqt	#1, r12				; Always advance string pointer
			jr		HI, .nextchr		; If chr out of range, leave blank space
			sub		r8, r7				; Subtract font first chr from character
			jr		MI, .nextchr		; If chr out of range, leave blank space
			nop
.waitblit:	btst	#0, r11				; See if bit 0 is set
			jr		NE, .blitloop		; If done, next iteration
			xor		r11, r11			; Clear r11 for next iteration
			jr		.waitblit			; Else, keep waiting

.waitlast:	; Done. Wait for the last blit...
			load	(r4), r11			; In both loops: Read back blit status
			btst	#0, r11				; See if bit 0 is set
			jr		EQ, .waitlast
			nop							; Don't signal completion while spinning

.done:		; ... then clear command, interrupt 68k, and return to main loop
			storew	r0, (rgpucmd)	; Clear gpucmd
			movei	#G_CTRL, r2
			load	(r2), r3		; Load G_CTRL into r3
			movei	#infinite, r0
			bset	#1, r3			; Set CPUINT bit in r3
			jump	(r0)			; Jump back to main loop
			store	r3, (r2)		; Write G_CTRL, causing 68k interrupt

invertrect:	; Invert a 1bpp rectangle of pixels
			;  surfaddr:   The  surface to draw to
			;  coords:     The packed start coordinates (y<<16)|x
			;  size:       The packed rect size (y<<16)|x
			moveq	#0, r0				; 0 will be stored in various fields

			movei	#surfaddr, r6		; Surface address pointer -> r6
			movei	#A1_BASE, r2

			load	(r6), r1			; Load dst surface address into r1

			movei	#A1_CLIP, r3
			.assert GL_WIDTH = 192
			movei	#PITCH1|PIXEL1|WID192|XADDPIX|YADD0, r4
			movei	#A1_FLAGS, r5
			movei	#coords, r6
			movei	#A1_FPIXEL, r7
			load	(r6), r8
			movei	#A1_PIXEL, r9

			; Add (-GL_WIDTH, 1) to x, y pointers after each inner loop iter
			movei	#(1<<16)|((-GL_WIDTH)&$ffff), r6
			movei	#A1_STEP, r10
			movei	#size, r11
			movei	#A1_FSTEP, r12
			load	(r11), r14			; Load size -> r14
			movei	#B_COUNT, r16
			movei	#DSTEN|UPDA1|LFU_NOTD, r17	; 1bit pixels need DSTEN
			movei	#B_CMD, r18

			store	r1, (r2)			; Store surfaddr in A1_BASE
			store	r0, (r3)			; Store 0,0 in A1_CLIP (No clipping)
			store	r4, (r5)			; Store A1_FLAGS data
			store	r0, (r7)			; Store 0,0 A1_FPIXEL
			store	r8, (r9)			; store coords in A1_PIXEL
			store	r6, (r10)			; store (-GL_WIDTH, 1) in A1_STEP
			store	r0, (r12)			; Store 0,0 in A1_FSTEP
			store	r14, (r16)			; Store size in B_COUNT
			store	r17, (r18)			; Write op to B_CMD

.waitblit:
			load	(r18), r0			; Read back blit status
			btst	#0, r0				; See if bit 0 is set
			jr		EQ, .waitblit

			; Done. Clear command, interrupt 68k, and return to main loop
			moveq	#0, r1
			movei	#G_CTRL, r2
			movei	#infinite, r0
			load	(r2), r3		; Load G_CTRL into r3
			storew	r1, (rgpucmd)	; Clear gpucmd
			bset	#1, r3			; Set CPUINT bit in r3
			jump	(r0)			; Jump back to main loop
			store	r3, (r2)		; Write G_CTRL, causing 68k interrupt

gpucpuint:
			; Test code: Just increment a 16-bit counter by two
			movei	#_gpusem, isr_reg0
			loadw	(isr_reg0), isr_reg1
			addq	#2, isr_reg1
			storew	isr_reg1, (isr_reg0)

			; Then exit the interrupt
			movei	#G_FLAGS, isr_reg0
			load	(isr_reg0), isr_reg1
			bclr	#3, isr_reg1
			bset	#9, isr_reg1
			load	(isr_sp), isr_reg2
			addq	#2, isr_reg2
			addq	#4, isr_sp
			jump	(isr_reg2)
			store	isr_reg1, (isr_reg0)

gpuopint:
			; Before anything, restart the object processor
			movei	#OBF, isr_reg4			; Writing any value to OBF restarts
			storew	isr_reg3, (isr_reg4)	; the object processor

			; First, strobe the background color from black->purple->black
			movei	#time, isr_reg0
			movei	#deltat, isr_reg1
			load	(isr_reg0), isr_reg2	; Load time in isr_reg2
			load	(isr_reg1), isr_reg3	; Load deltat in isr_reg3
			movei	#$100, isr_reg4			; Load constant 0x100 in isr_reg4
			add		isr_reg3, isr_reg2		; Add deltat to time
			jr		EQ, .reverse			; If result is zero, negate deltat
			cmp		isr_reg4, isr_reg2		; Always: Compare time with 0x100
			jr		NE, .goodtime			; If equal, negate deltat
.reverse:	store	isr_reg2, (isr_reg0)	; Always: Store new time back to mem
			neg		isr_reg3
			store	isr_reg3, (isr_reg1)

.goodtime:	moveq	#$0a, isr_reg0			; Load blue component in isr_reg0
			mult	isr_reg2, isr_reg0		; Multiply blue by time
			movei	#$7c0, isr_reg1			; Load blue bits mask in isr_reg1
			shrq	#2, isr_reg0			; Shift into blue bits spot
			moveq	#$05, isr_reg3			; load red component in isr_reg3
			and		isr_reg1, isr_reg0		; Mask off non-blue bits in isr_reg0
			movei	#$ff00, isr_reg4		; Load red bits mask in isr_reg4
			mult	isr_reg2, isr_reg3		; Multiply red by time
			and		isr_reg4, isr_reg3		; Mask off non-red bits in isr_reg3
			shlq	#3, isr_reg3			; Shift into red bits spot
			movei	#BORD1, isr_reg1		; Get address of BORD1 in isr_reg1
			movei	#BG, isr_reg2			; Get address of BG in isr_reg2
			or		isr_reg3, isr_reg0		; Combine red and blue bits
			store	isr_reg0, (isr_reg1)	; Store in BORD1
			storew	isr_reg0, (isr_reg2)	; Store in BG

			; Scale the bitmap
			movei	#bmpupdate, isr_regi
			movei	#_doscale, isr_reg3
			load	(isr_regi+3), isr_reg2	; Load old scalingvals in isr_reg2
			loadw	(isr_reg3), isr_reg1	; Load _doscale in isr_reg1
			movei	#scalespeed, isr_reg5
			move	isr_reg2, isr_reg0		; Save a copy of old scaling vals
			movei	#.noscale, isr_reg4
			loadw	(isr_reg5), isr_reg3	; Load scalespeed into isr_reg3
			cmpq	#0, isr_reg1			; Is _doscale == 0?
			jump	EQ, (isr_reg4)			; jump to .noscale
			nop
			subq	#1, isr_reg3			; Subtract 1 from scalespeed
			jump	NE, (isr_reg4)			; jump to .noscale
			nop

			moveq	#SCALESPEED, isr_reg3	; Reset scalespeed counter
			movei	#deltas, isr_reg6		; isr_reg6 = &deltas
			movei	#$ff, isr_reg4			; Set up a mask
			load	(isr_reg6), isr_reg1	; Load deltas to isr_reg1
			and		isr_reg4, isr_reg2		; Isolate the HSCALE value
			jr		EQ, .revscale
			moveq	#(1<<4), isr_reg4		; Load 1<<5 into isr_reg4
			shlq	#1, isr_reg4
			cmp		isr_reg4, isr_reg2		; if scale!=1<<5, leave deltas alone
			jr		NE, .adjscale
			nop

.revscale:	neg		isr_reg1				; Negate & store deltas if needed
			store	isr_reg1, (isr_reg6)

.adjscale:	add		isr_reg1, isr_reg2		; Add deltas to scale
			move	isr_reg2, isr_reg6		; Use it to update XPOS
			movei	#LGO_WIDTH, isr_reg1
			mult	isr_reg1, isr_reg6		; width * scale
			shrq	#5, isr_reg6			; Adjust for 3.5 fixed point
			load	(isr_regi+2), isr_reg4	; Original Phrase 2 low dword
			; The below is only safe if 0 <= (XPOS + width) <= 2047
			add		isr_reg1, isr_reg4		; XPOS + width
			sub		isr_reg6, isr_reg4		; XPOS + width - (width * scale)
			movei	#listbuf+BMLOGO_OFF+12, isr_reg1
			store	isr_reg4, (isr_reg1)	; Store in phrase 2's lower dword
			; Don't store isr_reg4 in bmpupdate+8/isr_regi+2. The logic here
			; relies on that field always containing the original XPOS value.

			move	isr_reg2, isr_reg0		; Save REMAINDER|VSCALE|HSCALE
			shlq	#8, isr_reg2
			or		isr_reg2, isr_reg0
			shlq	#8, isr_reg2
			or		isr_reg2, isr_reg0

.noscale:	storew	isr_reg3, (isr_reg5)	; Store the updated scalespeed
			store	isr_reg0, (isr_regi+3)	; Store the final scaling vals

			; Now update the rest of the logo scaled bitmap object
			load	(isr_regi), isr_reg1	; Load phrase 1 high dword
			load	(isr_regi+1), isr_reg2	; Load phrase 1 low dword

			movei	#listbuf+BMLOGO_OFF, isr_regi

			or		isr_reg1, isr_reg1		; Work-around indexed store bug
			or		isr_reg2, isr_reg2

			store	isr_reg1, (isr_regi)	; Store phrase 1 high dword
			store	isr_reg2, (isr_regi+1)	; Store phrase 1 low dword
			store	isr_reg0, (isr_regi+5)	; Store phrase 3 low dword

			; Now update the first phrase of the game list bitmap object
			movei	#glbmupdate, isr_reg0
			load	(isr_reg0), isr_reg1
			addq	#4, isr_reg0
			load	(isr_reg0), isr_reg2

			movei	#listbuf+BMGAMELST_OFF, isr_reg0

			store	isr_reg1, (isr_reg0)
			addq	#4, isr_reg0
			store	isr_reg2, (isr_reg0)

			; Increment _ticks
			movei	#_ticks, isr_reg4
			load	(isr_reg4), isr_reg0
			addq	#1, isr_reg0
			store	isr_reg0, (isr_reg4)

			; Exit the interrupt
			movei	#G_FLAGS, isr_reg0		
			load	(isr_reg0), isr_reg1
			bclr	#3, isr_reg1
			bset	#12, isr_reg1
			load	(isr_sp), isr_reg2
			addq	#2, isr_reg2
			addq	#4, isr_sp
			jump	(isr_reg2)
			store	isr_reg1, (isr_reg0)

			.68000
gpucodex:

.print "gpucode size: ",/u/w (gpucodex-gpucode), " bytes."

			.data
			.phrase

clr6x12fnt:	.incbin "clr6x12.jft"
fontdata	.equ	(clr6x12fnt+8)		; Font data is after 1 phrase header

			.bss
			.long

surfaddr:	.ds.l	1
coords:		.ds.l	1
size:		; Alias stringaddr
stringaddr:	.ds.l	1
_gpusem:	.ds.w	1
gpucmd:		.ds.w	1

			.end
