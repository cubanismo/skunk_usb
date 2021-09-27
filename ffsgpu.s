			.include "jaguar.inc"
			.include "ffsobj.inc"

			.globl	gpucode
			.globl	gpucodex
			.globl	gpuinit

			.globl	_startgpu
			.globl	_testgpu
			.globl	_stopgpu
			.globl	_clrgamelst

gpustack	.equ	G_RAM+4096

			.68000
			.text

			; Blit the GPU code to GPU RAM and start the GPU
_startgpu:
			move.l	#gpucodex, d0		; Calculate size of GPU code in dwords
			sub.l	#gpucode, d0
			add.l	#3, d0
			lsr.l	#2, d0

			move.l	#0, A1_CLIP			; Don't clip blitter writes

			move.l	#G_RAM, A1_BASE		; destination
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

_clrgamelst: ; Tell the GPU to clear gamelstbm
			move.w	#GCMD_CLRLIST, gpucmd

.waitgpu:	cmpi.w	#0, gpucmd			; Wait for the GPU to finish the clear
			bne		.waitgpu

			rts

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

			movei	#gamelstbm, r1
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
			jr		NE, .waitblit

			; Done. Clear the command and return to the message loop
			moveq	#0, r1
			storew	r1, (rgpucmd)
			movei	#infinite, r0
			jump	(r0)
			nop

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

			.bss
			.long

_gpusem:	.ds.w	1
gpucmd:		.ds.w	1

			.end
