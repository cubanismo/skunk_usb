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
		.globl	glbmupdate
		.globl	_ticks
		.globl	deltas
		.globl	scalespeed
		.globl	gamelstbm

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
;  Returns: void
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
			move.l	a0,d0				; Copy

			addq	#STOP_OFF,d0  		; Address of STOP object
			jsr		mklink				; Format link overlay into d0/d1
			move.l	d0,d2				; Move into d0/d1 into d2/d3
			move.l	d1,d3

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

			move.l	d1,d4				; Save d1 in d4
			move.l	#listbuf,d0			; Load GPU object address
			jsr		mklink				; Generate link overlay in d0/d1
			or.l	d1,d4				; overlay link high word with saved d1
			move.l	d4,d1				; Restore d1

			or.l	d4, d1				; Do LINK overlay

			move.l	d0,(a0)+
			move.l	d1,(a0)+

; Write the Logo Scaled Bitmap object: First Phrase
			move.l	d2,d0				; Skip the Game List Bitmap to start and
			move.l	d3,d1				; link to the Stop object.
			ori.w	#SCBITOBJ, d1		; Set type = Scaled Bitmap

			; Use (height - 1) for scaled bitmap objects, full height for
			; regular bitmap objects.
			ori.l  #(LGO_HEIGHT-1)<<14,d1	; Height of image

			move.w  height,d4			; Center bitmap vertically
			sub.w   #(LGO_HEIGHT-1),d4
			add.w   a_vdb,d4
			andi.w  #$FFFE,d4			; Must be even, half-lines.
			lsl.w   #3,d4
			or.w    d4,d1				; Stuff YPOS in low phrase

			move.l	#logobits,d4
			lsl.l	#8,d4				; Assumes phrase-aligned buffer,
			or.l	d4,d0				; so no masking of lower bits

			move.l	d0,(a0)+
			move.l	d1,(a0)+
			movem.l	d0-d1,bmpupdate

; Second Phrase of Logo Scaled Bitmap
			move.l	#(LGO_PHRASES>>4)|O_TRANS,d0	; Only part of top LONG is IWIDTH
			move.l  #O_DEPTH1|O_NOGAP,d1	; BPP = 1, Contiguous phrases

			move.w  width,d4			; Get width in clocks
			lsr.w   #2,d4				; /4 Pixel Divisor
			sub.w   #LGO_WIDTH,d4
			lsr.w   #1,d4
			or.w    d4,d1

			ori.l	#(LGO_PHRASES<<18)|(LGO_PHRASES<<28),d1 ; DWIDTH|IWIDTH

			move.l	d0,(a0)+
			move.l	d1,(a0)+
			move.l	d1,bmpupdate+8

; Third Phrase of Logo Scaled Bitmap
			moveq	#0, d0				; Nothing in bits 32-63
			move.l	#(1<<21)|(1<<13)|(1<<5), d1 ; REMAINDER|VSCALE|HSCALE

			move.l	d0,(a0)+
			move.l	d1,(a0)+
			move.l	d1,bmpupdate+12

; Phrase of padding
			lea		PHRASE(a0), a0

; Write the Game List Bitmap object: First Phrase
			move.l	d2,d0				; Link to the Stop object.
			move.l	d3,d1
			ori.w	#BITOBJ, d1			; Set  type = Bitmap

			ori.l	#GL_HEIGHT<<14, d1	; Set the height

			move.w  height, d4			; Center bitmap vertically
			sub.w   #(GL_HEIGHT-1), d4
			add.w   a_vdb, d4
			andi.w  #$FFFE, d4			; Must be even, half-lines.
			lsl.w   #3, d4
			or.w    d4, d1				; Stuff YPOS in low phrase

			move.l	#gamelstbm, d4
			lsl.l	#8, d4				; Assumes phrase-aligned buffer, so no
			or.l	d4, d0				; masking of lower bits is needed here.

			move.l	d0, (a0)+
			move.l	d1, (a0)+
			movem.l	d0-d1, glbmupdate

; Second Phrase of Game List Bitmap
			move.l	#(GL_PHRASES>>4), d0	; Top dword portion of IWIDTH
			move.l	#O_DEPTH1|O_NOGAP, d1	; BPP = 1, Contiguous phrases

			; With a Pixel Divisor of 4, the line buffer is about 332 pixels
			; wide with overscan, and Atari recommends we assume no more than
			; 266 of these pixels are visible. Put the game list on the left-
			; most visible pixel.
			move.w	width, d4			; Screen width in clocks
			lsr.w	#2, d4				; /4 for Pixel Divisor
			sub.w	#266, d4			; Subtract visible px to get overscan px
			lsr.w	d4					; /2 to split between left/right side
			or.w	d4, d1					; Store as XPOS

			ori.l	#(GL_PHRASES<<18)|(GL_PHRASES<<28), d1	; DWIDTH|IWIDTH

			move.l	d0, (a0)+
			move.l	d1, (a0)+

			movem.l (sp)+, d1-d4/a0
			rts

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; mklink: Turn d0.l into a link address overlay pair in d0.l/d1.l
;
;  Returns: High 11 bits of link addr in d0.l, low 8 bits of link addr in d1.l
;
;  Registers: d0.l      - Input: Target link address
;             d0.l/d1.l - Work and output registers
mklink:
			move.l	d0, d1				; Copy for low half

			lsr.l	#8, d0				; Shift high half into place
			lsr.l	#3, d0

			swap	d1					; Place low half correctly
			clr.w	d1
			lsl.l	#5,d1

			rts

_showgl:
			move.l	#listbuf+STOP_OFF, d0	; Address of Game List BM object

			tst.l	4(sp)
			beq		.makeit

			move.l	#listbuf+BMGAMELST_OFF, d0	; Address of Game List BM object
.makeit:
			jsr		mklink

			andi.l	#~LINKMASK_HI, bmpupdate
			andi.l	#~LINKMASK_LO, bmpupdate+4
			or.l	d0, bmpupdate
			or.l	d1, bmpupdate+4

			rts

			.bss
			.qphrase
			; Fill 3 phrases with variables to align the scaled bitmap object to
			; a quad phrase.
			;
			; *** DO NOT ADD ANY MORE VARIABLES BETWEEN HERE ***
alignedaddr:
bmpupdate:	.ds.l   4       			; 4 longs of SCBITOBJ for Refresh
glbmupdate:	.ds.l	2					; 2 longs of BITOBJ for Refresh
			; *** AND HERE! ***
listbuf:	.ds.l   LISTSIZE/4  		; Object List
			; Validate alignment:
			.assert (((listbuf-alignedaddr)+BMLOGO_OFF) & $1f) = 0
			.assert (((listbuf-alignedaddr)+BMGAMELST_OFF) & $f) = 0

deltat:		.ds.l	1
time:		.ds.l	1
_ticks:		.ds.l	1			; Incrementing # of ticks
deltas:		.ds.l	1
_doscale:	.ds.w	1
scalespeed:	.ds.w	1

			.phrase
gamelstbm:	.ds.b	(GL_WIDTH/8)*GL_HEIGHT	; (width/(8 pixels per byte))*height

			.data
			.phrase
logobits:	.incbin "skunkusb.raw"
