			.include "jaguar.inc"

			.globl	gpucode
			.globl	gpucodex
			.globl	gpuinit

			.globl	_startgpu
			.globl	_testgpu

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

			clr.w	_gpusem				; Initialize the GPU semaphore
			move.l	#gpuinit, G_PC		; Start the GPU init code
			move.l	#RISCGO, G_CTRL

.waitinit:	move.w	_gpusem, d0			; Wait for the GPU init code to finish
			beq		.waitinit

			rts

_testgpu:
			moveq	#0, d0			; Clear high word of d0
			move.w	_gpusem, d1		; Load initial value of _gpusem to d0
			move.l	#RISCGO|FORCEINT0, G_CTRL	; Force a CPU interrupt on GPU
.waitintr:	move.w	_gpusem, d0		; Wait for _gpusem to change
			cmp.w	d1, d0
			beq		.waitintr

			rts

			.phrase
gpucode:
			.gpu
			.org G_RAM


isr_sp		.equr	r31
isr_reg0	.equr	r30
isr_reg1	.equr	r29
isr_reg2	.equr	r28

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
			movei	#gpustack, r31

			movei	#G_FLAGS, r0		; Enable CPU & OP interrupts
			;movei	#G_CPUENA|G_OPENA, r1
			;movei	#G_CPUENA, r1
			load	(r0), r2
			or		r2, r1
			store	r1, (r0)

			movei	#_gpusem, r0		; Indicate init complete
			moveq	#1, r1
			storew	r1, (r0)

.infinite:	movei	#.infinite, r0
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
			; ... Actual ISR routine

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

			.end
