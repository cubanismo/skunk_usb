.include "jaguar.inc"

		.globl	_flashrom
		.globl	_launchrom

BUFSIZE		.equ $1000

		.text

		.phrase

; typedef long (*proc)(void *priv, char *buf, unsigned int bytes);
; void flashrom(proc get_data, void *priv, unsigned short blocks);
;
; Based on the flasher code in the skunkboard BIOS.
;
; This function takes 3 parameters:
; -get_data: A callback to read the next chunk of data to flash. Returns bytes
;            read or -1 when EOF is reached. Bytes read may be less than bytes
;            requested on the last call before EOF is returned.
; -priv:     Opaque state structure to pass to get_data.
; -blocks:   Number of blocks to erase. Must be 62 or 30.
_flashrom:
		; gcc calling convention:
		; - args pushed onto stack, last argument first, then ret addr
		; - a0, a1, d0, d1 are trashed by function. Other regs preserved
		; - return value goes in d0
		lea		(4,sp), a0			; Save pointer to paramer array in a0
		movem.l	d2-d7/a2-a6, -(sp)	; preserve registers per convention

		move.l	#$C00000, a5		; a5 = HPI address write/data read
		move.l	#$800000, a3		; a3 = HPI data write

		move.l	(a0)+, a2			; get_data callback
		move.l	(a0)+, d5			; get_data callback data
		move.w	(2,a0), d4			; block count. Must be 62 or 30

		; XXX Make this a parameter
		move.l	#$802000, a6		; Destination address

		move.w	#$4BA0, (a5)		; Set bank 0

		; Erase all the 8k blocks except the first which contains the BIOS.
		move.l	#$802000, a0
		jsr		EraseBlockInA0
		move.l	#$880000, a0
		jsr		EraseBlockInA0
		move.l	#$882000, a0
		jsr		EraseBlockInA0
		move.l	#$900000, a0
		jsr		EraseBlockInA0
		move.l	#$902000, a0
		jsr		EraseBlockInA0
		move.l	#$980000, a0
		jsr		EraseBlockInA0
		move.l	#$982000, a0
		jsr		EraseBlockInA0

		; Now erase the rest of the blocks.
		move.l	#$984000, a0
.eraseloop:
		jsr		EraseBlockInA0
		;add.l	#$1, (aX)			; Increment number of blocks erased.
		add.l	#16384, a0
		dbra	d4, .eraseloop

		; Now write the ROM!

		; put the flash chip into single-pulse Word program mode
		; code: 555/aa, aaa/55, 555/80, 555/aa, aaa/55, 555/a0
		; hash: 80036a/9098,801c94/c501,80036a/8008,80036a/9098,801c94/c501,80036a/8088
		move.w  #$4000, (a5)    ; Enter Flash read/write mode

		move.w	#$9098, $80036a
		move.w	#$c501, $801c94
		move.w	#$8008, $80036a
		move.w	#$9098, $80036a
		move.w	#$c501, $801c94
		move.w	#$8088, $80036a

		; Start copying data to the flash
.readblock:
		move.l	#BUFSIZE, -(sp)	; Byte count
		move.l	#buf, -(sp)		; destination buffer
		move.l	d5, -(sp)		; Private data
		jsr		(a2)			; Bytes read in d0
		lea		(12,sp), sp		; restore stack
		move.w	#$4000, (a5)	; Enter Flash read/write mode
		cmpi	#0, d0			; EOF?
		ble		.done

		addq	#3, d0			; round up to dwords
		lsr.w	#2, d0			; divide by 4
		subq	#1, d0			; and subtract one for the dbra loop

		move.l	#buf, a0		; Load scratch buf addr in a0
		move.l	(a0), d7
		addq	#4, a0
		swap	d7

.blockcopy:
		move.w	d7, (a6)		; Payload

		; We have to wait for the write. Set up the next word before waiting.
		move.l	a6, a1			; save a6
		addq	#2, a6			; next address
		move.l	d7, d6			; save d7
		swap	d7				; get next value
		move.w	#80, d3			; Wait at most 100us after program

.blkcpylp2:
		cmp.w	(a1), d6		; Check for correctly written data or...
		dbeq	d3, .blkcpylp2	; ...time out when d3 expires

		; Write next word.
		move.w	d7, (a6)		; Payload

		; We have to wait for the write. Set up the next word before waiting.
		move.l	a6, a1			; save a6
		addq	#2, a6			; next address
		move	d7, d6			; save d7
		move.l	(a0), d7		; get next value
		addq	#4, a0
		swap	d7				; prepare for the write
		move.w	#80, d3			; Wait at most 100us after program

.blkcpylp:
		cmp.w	(a1), d6		; Check for correctly written data or...
		dbeq	d3, .blkcpylp	; ...time out when d3 expires

		dbra	d0, .blockcopy	; Keep going until entire block is written.

		; One final wait (XXX does this do anything?)
.waitpgm2:
		cmp.w	(a1), d7		
		dbeq	d3, .waitpgm2

		bra		.readblock

.done:
		move.w	#$4001, (a5)	; Enter Flash read-only mode (helps us reboot!)
		movem.l	(sp)+, d2-d7/a2-a6 ; preserve registers per convention
		move.l	#$0, d0
		rts

; Copied verbatim from the BIOS code.
; Returns the status in d0.
; Expects a2 = "get data" callback, d5 = "get data" callback private data
EraseBlockInA0:
		move.w	#$4000, (a5)		; Enter Flash read/write mode

		move.w	#$9098, $80036A		; special command
		move.w	#$C501, $801C94
		move.w	#$8008, $80036A		; 80
		move.w	#$9098, $80036A		; erase command
		move.w	#$C501, $801C94
		move.w	#$8480, (a0)		; 30

; XXX - Doesn't handle erase errors (but what can we do?)
.waiterase:     
		move.w	(a0), d0			; Zero means busy, 8 means ready
		and.w	#8, d0
		beq		.waiterase

		move.w	#$4001, (a5)		; Enter Flash read-only mode

		movem.l	d0-d1/a0-a1, -(sp)	; Preserve d0, d1,a0 and a1
		move.l	#0, -(sp)			; Byte count
		move.l	#0, -(sp)			; destination buffer
		move.l	d5, -(sp)			; Private data
		jsr		(a2)				; Bytes read in d0
		lea		(12,sp), sp			; restore stack
		movem.l	(sp)+, d0-d1/a0-a1	; Restore registers
		rts

resetezhost:
		move.w	#$7BAC, (a1)		; Force reset
		move.w	#$4006, (a1)		; ...wait 16 cycles... enter HPI boot mode
		move.w	#$4006, (a1)		; ...wait 16 cycles... enter HPI boot mode
		move.w	#$7BAD, (a1)		; Exit reset (boot time)

		move.l	#12000, d1			; Wait at least 4ms (full boot)
.waitreset:
		dbra	d1, .waitreset

		move.w	#$4004, (a1)		; Enter HPI write mode

		move.w	#140, (a1)			; Locate idle task
		move.w	#$ee18, (a0)		; Force sie2_init
		move.l	#3000, d1			; Wait at least 1ms (idle loop duration)
.waitidle:
		dbra	d1, .waitidle

		move.w	#140, (a1)
		move.w	#$f468, (a0)		; Restore usb_idle

.dolock:
		; hardlock sector 0, preventing any accidental overwriting of the BIOS
		move.w	#$4000, (a1)		; Enter flash read/write mode

		move.w	#$4BA0, (a1)		; Switch to bank 0

		; 36A=9098 / 1C94=C501 / 36A=8008 / 36A=9098 / 1C94=C501 / Addr=8180
		move.w	#$9098, $80036a		; 555=aa
		move.w	#$c501, $801c94		; aaa=55
		move.w	#$8008, $80036a		; 555=80
		move.w	#$9098, $80036a		; 555=aa
		move.w	#$c501, $801c94		; aaa=55
		move.w	#$8180, $800000		; 0 = 60 -> sector lockdown

		move.w	#$4001, (a1)		; return to HPI write mode

		; XXX Set up serial EEPROM here.

		rts

_launchrom:
		move.l	#$800000, a0		; a0 = HPI data write
		move.l	#$C00000, a1		; a1 = HPI address write/data read

		move.w	#$FFFF, VI			; Disable video line interrupt
		move.w	#$2700, sr			; Disable interrupts

		move.w	#4001, (a1)			; set flash read-only mode
		move.w	#$4BA0, (a1)		; Select bank 0

		jsr		resetezhost

		move.l	#INITSTACK, a7		; Set Atari's default stack
		move.l	#$802000, a0
		jsr		(a0)				; Go! Go! Go!
		rts							; Unreachable

		.bss

		.phrase
buf:
		.ds.b	BUFSIZE

		.end
