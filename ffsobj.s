		.include "jaguar.inc"
		.include "ffsobj.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;; External Variables
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

		; From startffs.s
		.extern  a_vdb
		.extern  a_vde
		.extern  a_hdb
		.extern  a_hde
		.extern  width
		.extern  height

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;; External Routines
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

		; From ffsgpu.s
		.extern	_startgpu

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;; Global Variables
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
		.globl	time
		.globl	deltat
		.globl	bmpupdate
		.globl	_ticks
		.globl	deltas
		.globl	scalespeed

		.68000
		.text

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; InitGPUOP: Start the GPU and use it to start the object processor
;
InitGPUOP:
			jsr		InitLister		; Initialize Object Display List
			jsr		_startgpu		; Start the GPU, which will start the OP
			rts

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; InitLister: Initialize Object List Processor List
;
;    Returns: Pre-word-swapped address of current object list in d0.l
;
;  Registers: d0.l/d1.l - Phrase being built
;             d2.l/d3.l - Stop Object Link address overlays
;             d4.l      - Work register
;             a0.l      - Roving object list pointer
		
InitLister:
			movem.l d1-d4/a0,-(sp)		; Save registers

			move.l	#0, time			; Time starts at 0 ticks
			move.l	#1, deltat			; Init time delta value
			move.l	#1, deltas			; Init scale delta value
			move.w	#0, _doscale		; Don't scale at startup
			move.w	#SCALESPEED, scalespeed	; Higher is slower

			lea		listbuf,a0
			move.l	a0,d2				; Copy

			addq	#STOP_OFF,d2  		; Address of STOP object
			move.l	d2,d3				; Copy for low half

			lsr.l	#8,d2				; Shift high half into place
			lsr.l	#3,d2

			swap	d3					; Place low half correctly
			clr.w	d3
			lsl.l	#5,d3

; Write the GPU object
			clr.l	(a0)+
			move.l	#GPUOBJ,(a0)+

; Write the STOP object
			clr.l	(a0)+
			move.l	#STOPOBJ,(a0)+

; Write the first branch object (branch if YPOS < a_vdb)

			clr.l	d0
			move.l	#(BRANCHOBJ|O_BRGT),d1	; $8000 = VC > YPOS
			or.l	d2,d0				; Do LINK overlay
			or.l	d3,d1

			move.w	a_vdb,d4			; for YPOS
			lsl.w	#3,d4				; Make it bits 13-3
			or.w	d4,d1

			move.l	d0,(a0)+
			move.l	d1,(a0)+

; Write the second BRANCH object (branch if YPOS > a_vde )
; Note: LINK address is the same so preserve it

			andi.l	#$FF000007,d1		; Mask off CC and YPOS
			ori.l	#O_BRLT,d1			; $4000 = VC < YPOS

			move.w	a_vde,d4			; for YPOS
			lsl.w	#3,d4				; Make it bits 13-3
			or.w	d4,d1

			move.l	d0,(a0)+
			move.l	d1,(a0)+

; Write the third BRANCH object (branch if YPOS == a_vde )
; Note: YPOS is the same, so preserve it, but ensure it is even

			andi.l	#$00003ff7,d1		; Mask off LINK address and CC
			;ori.l	#O_BREQ,d1			; $0000 = VC == YPOS

			move.l	#listbuf,d0			; Load GPU object address
			move.l	d0,d4				; and make a copy

			lsr.l	#8,d0				; Shift high half into place
			lsr.l	#3,d0

			swap	d4					; Place low half correctly
			clr.w	d4
			lsl.l	#5,d4

			or.l	d4, d1				; Do LINK overlay

			move.l	d0,(a0)+
			move.l	d1,(a0)+

; Write a Scaled Bitmap object: First Phrase
			move.l	d2,d0
			move.l	d3,d1
			ori.w	#SCBITOBJ, d1		; Set type = Scaled Bitmap

			; Use (height - 1) for scaled bitmap objects, full height for
			; regular bitmap objects.
			ori.l  #(LGO_HEIGHT-1)<<14,d1	; Height of image

			move.w  height,d4				; Center bitmap vertically
			sub.w   #(LGO_HEIGHT-1),d4
			add.w   a_vdb,d4
			andi.w  #$FFFE,d4               ; Must be even, half-lines.
			lsl.w   #3,d4
			or.w    d4,d1                   ; Stuff YPOS in low phrase

			move.l	#logobits,d4
			lsl.l	#8,d4			; Assumes phrase-aligned buffer,
			or.l	d4,d0			; so no masking of lower bits

			move.l	d0,(a0)+
			move.l	d1,(a0)+
			movem.l	d0-d1,bmpupdate

; Second Phrase of Scaled Bitmap
			move.l	#(LGO_PHRASES>>4)|O_TRANS,d0	; Only part of top LONG is IWIDTH
			move.l  #O_DEPTH1|O_NOGAP,d1	; BPP = 1, Contiguous phrases

			move.w  width,d4            	; Get width in clocks
			lsr.w   #2,d4               	; /4 Pixel Divisor
			sub.w   #LGO_WIDTH,d4
			lsr.w   #1,d4
			or.w    d4,d1

			ori.l	#(LGO_PHRASES<<18)|(LGO_PHRASES<<28),d1 ; DWIDTH|IWIDTH

			move.l	d0,(a0)+
			move.l	d1,(a0)+
			move.l	d1,bmpupdate+8

; Third Phrase of Scaled Bitmap
			moveq	#0, d0			; Nothing in bits 32-63
			move.l	#(1<<21)|(1<<13)|(1<<5), d1 ; REMAINDER|VSCALE|HSCALE

			move.l	d0,(a0)+
			move.l	d1,(a0)+
			move.l	d1,bmpupdate+12

; Now return swapped list pointer in D0

			move.l  #listbuf+LIST_START_OFF,d0
			swap    d0

			movem.l (sp)+,d1-d4/a0
			rts

			.bss
			.qphrase
			; Fill 3 phrases with variables to align the scaled bitmap object to
			; a quad phrase.
			;
			; *** DO NOT ADD ANY MORE VARIABLES BETWEEN HERE ***
alignedaddr:
bmpupdate:	.ds.l   4       		; 4 longs of SCBITOBJ for Refresh
deltat:		.ds.l	1
time:		.ds.l	1
			; *** AND HERE! ***
listbuf:	.ds.l   LISTSIZE/4  		; Object List
			; Validate alignment:
			.assert (((listbuf-alignedaddr)+BITMAP_OFF) & $1f) = 0

_ticks:		.ds.l	1			; Incrementing # of ticks
deltas:		.ds.l	1
_doscale:	.ds.w	1
scalespeed:	.ds.w	1

			.data
			.phrase
logobits:	.incbin "skunkusb.raw"
