			.include "blitcode.inc"

dstaddr		.equ	$4000		; More or less standard RAM start address
srcaddr		.equ	$A00100		; <ROM Base (8MiB)> + 2MiB + 256 bytes
progsize	.equ	$A000		; 40k

			.68000
			.text

; loadprog - Loads a program from one location (Usually ROM) to another location
;            (usually RAM) and runs it.
;
; Note this code is position independent. It should run from wherever you call
; it. However, the source and destination address as well as the size are hard-
; coded. The intent is that this code be patched, either before or after
; assembly, with the actual source+destination address and size as needed.
;
; See the caveats about rounding up in blitcode.s. Since this function uses that
; one, it applies here as well.
;
; One last note: This code assumes it is launched by the Skunk BIOS or some
; other boot code that has already taken care of the super-low-level init of the
; Jaguar, things like setting the endianness of the processors, and that the
; destination code will handle any further initialization itself.
loadprog:	move.l	#progsize, d0
			move.l	#srcaddr, a0
			move.l	#dstaddr, a1
			bsr		blitcode
			jmp		(a1)
