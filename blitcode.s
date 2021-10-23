			.include "jaguar.inc"
			.include "blitcode.inc"

			.globl	blitcode

			.68000
			.text

; blitcode - blits code or other linear data from one location to another.
;
; Note this rounds the size of the blit up to a .long boundary and uses the
; blitter in phrase mode without DSTEN, so it likely (TODO: Verify!) corrupts up
; to ((a phrase)-1 byte) of data beyond the end of the blit in the destination
; location.
;
; Parameters:
;  d0: Size, in bytes, of the code or data to be transferred.
;  a0: Address of the source data
;  a1: Address of the destination
;
; Registered clobbered:
;  d0
blitcode:
			add.l	#3, d0
			lsr.l	#2, d0

			move.l	#0, A1_CLIP			; Don't clip blitter writes

			move.l	a1, A1_BASE	; destination
			move.l	a0, A2_BASE	; source

			move.l	#XADDPHR|PIXEL32|WID2048|PITCH1, A1_FLAGS
			move.l	#XADDPHR|PIXEL32|WID2048|PITCH1, A2_FLAGS
			move.l	#0, A1_PIXEL
			move.l	#0, A2_PIXEL

			or.l	#1<<16, d0			; Loop 1x(data size in dwords) times
			move.l	d0, B_COUNT

			move.l	#SRCEN|UPDA1|UPDA2|LFU_REPLACE, B_CMD

.waitblit:	move.l	B_CMD, d0			; Wait for the blit to complete
			andi.l	#1, d0
			beq		.waitblit

			rts

			.end
