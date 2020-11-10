;-----------------------------------------------------------------------------
; Warning!!! Warning!!! Warning!!! Warning!!! Warning!!! Warning!!! Warning!!!
; Warning!!! Warning!!! Warning!!! Warning!!! Warning!!! Warning!!! Warning!!!
;-----------------------------------------------------------------------------
; Do not change any of the code in this file except where explicitly noted.
; Making other changes can cause your program's startup code to be incorrect.
;-----------------------------------------------------------------------------


;----------------------------------------------------------------------------
; Jaguar Development System Source Code
; Copyright (c)1995 Atari Corp.
; ALL RIGHTS RESERVED
;
; Module: startup.s - Hardware initialization/License screen display
;
; Revision History:
;  1/12/95 - SDS: Modified from MOU.COF sources.
;  2/28/95 - SDS: Optimized some code from MOU.COF.
;  3/14/95 - SDS: Old code preserved old value from INT1 and OR'ed the
;                 video interrupt enable bit. Trouble is that could cause
;                 pending interrupts to persist. Now it just stuffs the value.
;  4/17/95 - MF:  Moved definitions relating to startup picture's size and
;                 filename to top of file, separate from everything else (so
;                 it's easier to swap in different pictures).
;----------------------------------------------------------------------------
; Program Description:
; Jaguar Startup Code
;
; Steps are as follows:
; 1. Set GPU/DSP to Big-Endian mode
; 2. Set VI to $FFFF to disable video-refresh.
; 3. Initialize a stack pointer to high ram.
; 4. Initialize video registers.
; 5. Create an object list as follows:
;            BRANCH Object (Branches to stop object if past display area)
;            BRANCH Object (Branches to stop object if prior to display area)
;            BITMAP Object (Jaguar License Acknowledgement - see below)
;            STOP Object
; 6. Install interrupt handler, configure VI, enable video interrupts,
;    lower 68k IPL to allow interrupts.
; 7. Use GPU routine to stuff OLP with pointer to object list.
; 8. Turn on video.
; 9. Jump to _start.
;
; Notes:
; All video variables are exposed for program use. 'ticks' is exposed to allow
; a flicker-free transition from license screen to next. gSetOLP and olp2set
; are exposed so they don't need to be included by exterior code again.
;-----------------------------------------------------------------------------

	.include    "jaguar.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Begin SCREEN GEOMETRY CONFIGURATION
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PPP		.equ	4			; Pixels per Phrase (16-bit)
SCRN_WIDTH	.equ	320			; Width in Pixels
SCRN_HEIGHT	.equ	240			; Height in Pixels

BMP_HEIGHT	.equ	SCRN_HEIGHT
BMP_PHRASES	.equ	(320>>2)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; End SCREEN GEOMETRY CONFIGURATION
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Globals
		.globl	gSetOLP
		.globl	olp2set
		.globl	_ticks
		.globl	_screen

		.globl  a_vdb
		.globl  a_vde
		.globl  a_hdb
		.globl  a_hde
		.globl  width
		.globl  height
; Externals
		.extern	_start

SCRN_PHRASES	.equ	(SCRN_WIDTH/PPP)	; Width in Phrases
BITMAP_OFF  	.equ    (2*8)       		; Two Phrases
LISTSIZE    	.equ    5       		; List length (in phrases)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;; Program Entry Point Follows...

		.text

		move.l  #$70007,G_END		; big-endian mode
		move.l  #$70007,D_END
		move.w  #$FFFF,VI       	; disable video interrupts

		move.l  #INITSTACK,a7   	; Setup a stack

		jsr	InitGreen		; Initialize screen content
		jsr 	InitVideo      		; Setup our video registers.
		jsr 	InitLister     		; Initialize Object Display List
		jsr 	InitVBint      		; Initialize our VBLANK routine

;;; Sneaky trick to cause display to popup at first VB

		move.l	#$0,listbuf+BITMAP_OFF
		move.l	#$C,listbuf+BITMAP_OFF+4

		move.l  d0,olp2set      	; D0 is swapped OLP from InitLister
		move.l  #gSetOLP,G_PC   	; Set GPU PC
		move.l  #RISCGO,G_CTRL  	; Go!
waitforset:
		move.l  G_CTRL,d0   		; Wait for write.
		andi.l  #$1,d0
		bne 	waitforset

		move.w	#PWIDTH4|BGEN|CSYNC|RGB16|VIDEN,VMODE

	     	jmp 	_start			; Jump to main code

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Procedure: gSetOLP
;            Use the GPU to set the OLP and quit.
;
;    Inputs: olp2set - Variable contains pre-swapped value to stuff OLP with.
;
; NOTE!!!: This code can run in DRAM only because it contains no JUMP's or
;          JR's. It will generate a warning with current versions of MADMAC
;          because it doesn't '.ORG'.
;
		.long
		.gpu
gSetOLP:
		movei   #olp2set,r0   		; Read value to write
		load    (r0),r1

		movei   #OLP,r0       		; Store it
		store   r1,(r0)

		moveq   #0,r0         		; Stop GPU
		movei   #G_CTRL,r1
		store   r0,(r1)
		nop             		; Two "feet" on the brake pedal
		nop

		.68000
		.bss
		.long

olp2set:    	.ds.l   1           		; GPU Code Parameter

		.text

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Procedure: InitVBint
; Install our vertical blank handler and enable interrupts
;

InitVBint:
		move.l  d0,-(sp)

		move.l  #UpdateList,LEVEL0	; Install 68K LEVEL0 handler

		move.w  a_vde,d0        	; Must be ODD
		ori.w   #1,d0
		move.w  d0,VI

		move.w  #C_VIDENA,INT1         	; Enable video interrupts

		move.w  sr,d0
		and.w   #$F8FF,d0       	; Lower 68k IPL to allow
		move.w  d0,sr           	; interrupts

		move.l  (sp)+,d0
		rts
		
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Procedure: InitVideo (same as in vidinit.s)
;            Build values for hdb, hde, vdb, and vde and store them.
;
						
InitVideo:
		movem.l d0-d6,-(sp)
			
		move.w  CONFIG,d0      		 ; Also is joystick register
		andi.w  #VIDTYPE,d0    		 ; 0 = PAL, 1 = NTSC
		beq 	palvals

		move.w  #NTSC_HMID,d2
		move.w  #NTSC_WIDTH,d0

		move.w  #NTSC_VMID,d6
		move.w  #NTSC_HEIGHT,d4

		bra 	calc_vals
palvals:
		move.w  #PAL_HMID,d2
		move.w  #PAL_WIDTH,d0

		move.w  #PAL_VMID,d6
		move.w  #PAL_HEIGHT,d4

calc_vals:
		move.w  d0,width
		move.w  d4,height

		move.w  d0,d1
		asr 	#1,d1         	 	; Width/2

		sub.w   d1,d2         	  	; Mid - Width/2
		add.w   #4,d2         	  	; (Mid - Width/2)+4

		sub.w   #1,d1         	  	; Width/2 - 1
		ori.w   #$400,d1      	  	; (Width/2 - 1)|$400
		
		move.w  d1,a_hde
		move.w  d1,HDE

		move.w  d2,a_hdb
		move.w  d2,HDB1
		move.w  d2,HDB2

		move.w  d6,d5
		sub.w   d4,d5
		move.w  d5,a_vdb

		add.w   d4,d6
		move.w  d6,a_vde

		move.w  a_vdb,VDB
		move.w  #$FFFF,VDE
			
		move.l  #0,BORD1        	; Black border
		move.w  #0,BG           	; Init line buffer to black
			
		movem.l (sp)+,d0-d6
		rts

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; InitLister: Initialize Object List Processor List
;
;    Returns: Pre-word-swapped address of current object list in d0.l
;
;  Registers: d0.l/d1.l - Phrase being built
;             d2.l/d3.l - Link address overlays
;             d4.l      - Work register
;             a0.l      - Roving object list pointer
		
InitLister:
		movem.l d1-d4/a0,-(sp)		; Save registers
			
		lea     listbuf,a0
		move.l  a0,d2           	; Copy

		add.l   #(LISTSIZE-1)*8,d2  	; Address of STOP object
		move.l	d2,d3			; Copy for low half

		lsr.l	#8,d2			; Shift high half into place
		lsr.l	#3,d2
		
		swap	d3			; Place low half correctly
		clr.w	d3
		lsl.l	#5,d3

; Write first BRANCH object (branch if YPOS > a_vde )

		clr.l   d0
		move.l  #(BRANCHOBJ|O_BRLT),d1  ; $4000 = VC < YPOS
		or.l	d2,d0			; Do LINK overlay
		or.l	d3,d1
								
		move.w  a_vde,d4                ; for YPOS
		lsl.w   #3,d4                   ; Make it bits 13-3
		or.w    d4,d1

		move.l	d0,(a0)+
		move.l	d1,(a0)+

; Write second branch object (branch if YPOS < a_vdb)
; Note: LINK address is the same so preserve it

		andi.l  #$FF000007,d1           ; Mask off CC and YPOS
		ori.l   #O_BRGT,d1      	; $8000 = VC > YPOS
		move.w  a_vdb,d4                ; for YPOS
		lsl.w   #3,d4                   ; Make it bits 13-3
		or.w    d4,d1

		move.l	d0,(a0)+
		move.l	d1,(a0)+

; Write a Standard BITMAP object
		move.l	d2,d0
		move.l	d3,d1

		ori.l  #BMP_HEIGHT<<14,d1       ; Height of image

		move.w  height,d4           	; Center bitmap vertically
		sub.w   #SCRN_HEIGHT,d4
		add.w   a_vdb,d4
		andi.w  #$FFFE,d4               ; Must be even
		lsl.w   #3,d4
		or.w    d4,d1                   ; Stuff YPOS in low phrase

		move.l	#_screen,d4
		lsl.l	#8,d4
		or.l	d4,d0

		move.l	d0,(a0)+
		move.l	d1,(a0)+
		movem.l	d0-d1,bmpupdate

; Second Phrase of Scaled Bitmap
		move.l	#SCRN_PHRASES>>4,d0	; Only part of top LONG is IWIDTH
		move.l  #O_DEPTH16|(1<<15),d1   ; BPP = 16, Contiguous phrases

		move.w  width,d4            	; Get width in clocks
		lsr.w   #2,d4               	; /4 Pixel Divisor
		sub.w   #SCRN_WIDTH,d4
		lsr.w   #1,d4
		or.w    d4,d1

		ori.l	#(BMP_PHRASES<<18)|(SCRN_PHRASES<<28),d1 ; DWIDTH|IWIDTH

		move.l	d0,(a0)+
		move.l	d1,(a0)+

; Write a STOP object at end of list
		clr.l   (a0)+
		move.l  #(STOPOBJ|O_STOPINTS),(a0)+

; Now return swapped list pointer in D0

		move.l  #listbuf,d0
		swap    d0

		movem.l (sp)+,d1-d4/a0
		rts

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Procedure: UpdateList
;        Handle Video Interrupt and update object list fields
;        destroyed by the object processor.

UpdateList:
		move.l  a0,-(sp)

		move.l  #listbuf+BITMAP_OFF,a0

		move.l  bmpupdate,(a0)      	; Phrase = d1.l/d0.l
		move.l  bmpupdate+4,4(a0)

		add.l	#1,_ticks		; Increment ticks semaphore

		move.w  #$101,INT1      	; Signal we're done
		move.w  #$0,INT2

		move.l  (sp)+,a0
		rte

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Procedure: InitGreen
;        Draw green pixels to the entire "background" bitmap

InitGreen:
		; Set address of destination surface
		move.l	#_screen,A1_BASE
		; Contiguous phrases,
		; 16-bit pixels,
		; Window Width = 4 pixels (Should be BMP_WIDTH)
		; Blit a phrase at a time
		move.l	#PITCH1|PIXEL16|WID320|XADDPHR,A1_FLAGS

		; Start at <0 (low 16 bits), 0 (high 16 bits)>
		move.l	#0,A1_PIXEL

		; After each line, back X up BMP_WIDTH pixels, add 1 to Y
		move.l	#(1<<16)|((-(BMP_PHRASES*4))&($0000ffff)),A1_STEP

		; Run inner (X) loop BMP_WIDTH times, outer (Y) loop BMP_HEIGHT times
		move.l	#(BMP_HEIGHT<<16)|(BMP_PHRASES*4),B_COUNT

		; Jaguar RGB16 bits: RRRR.RBBB.BBGG.GGGG.
		; Repeat it 4 times to fill the phrase-size pattern register
		move.l	#$003F003F,B_PATD
		move.l	#$003F003F,B_PATD+4

		; Add A1_STEP after each line/outer loop iteration, use pattern
		; data register for source.
		move.l	#UPDA1|PATDSEL,B_CMD
		rts

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

		.bss
		.qphrase

		; 2 Phrases of padding to put bitmap object on a quad-phrase
		; boundary after 2 Phrases of branch objects.  This allows it
		; to be either a regular bitmap or scaled bitmap entry.
		.ds.l	4
listbuf:    	.ds.l   LISTSIZE*2  		; Object List
bmpupdate:  	.ds.l   3       		; 3 Longs of Scaled Bitmap for Refresh
_ticks:		.ds.l	1			; Incrementing # of ticks
a_hdb:  	.ds.w   1
a_hde:      	.ds.w   1
a_vdb:      	.ds.w   1
a_vde:      	.ds.w   1
width:      	.ds.w   1
height:     	.ds.w   1

		.phrase
_screen:	.ds.l	BMP_PHRASES*2*BMP_HEIGHT

		.end
