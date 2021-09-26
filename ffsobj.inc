;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Begin SkunkUSB LOGO GEOMETRY CONFIGURATION
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PPP				.equ	64			; Pixels per Phrase (16-bit)
LGO_WIDTH		.equ	320			; Width in Pixels
LGO_HEIGHT		.equ	160			; Height in Pixels
LGO_PHRASES		.equ	(LGO_WIDTH/PPP)
SCALESPEED		.equ	4			; Scale once every <n> VSyncs

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; End SkunkUSB LOGO GEOMETRY CONFIGURATION
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Globals Variables:
				.extern	_ticks
				.extern	_doscale
				.extern listbuf
				.extern bmpupdate
				.extern	time
				.extern deltat
				.extern	deltas
				.extern scalespeed

; Globals Functions:
				.extern	InitGPUOBJ
				.extern	InitLister
				.extern	UpdateList

; Object Processor Object Size Constants
SZ_BM			.equ	(2*8)
SZ_SBM			.equ	(3*8)
SZ_GPU			.equ	(1*8)
SZ_BRA			.equ	(1*8)
SZ_STP			.equ	(1*8)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; OBJECT PROCESSOR LIST LAYOUT
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;
;  -GPUOBJ
;  -STOPOBJ
;  -BRANCHOBJ ( VC < a_vdb -> STOP )
;  -BRANCHOBJ ( VC > a_vde -> STOP )
;  -BRANCHOBJ ( VC == a_vde -> GPU )
;  -SCBITOBJ ( SkunkUSB Logo)
;
; The stop object is just beyond the GPU object
STOP_OFF		.equ	(SZ_GPU)
; OLP Should start at the branch objects, following the stop object
LIST_START_OFF	.equ	(STOP_OFF+SZ_STP)
; Bitmap is 3 branch objects past the list start
BITMAP_OFF 		.equ    (LIST_START_OFF+(3*SZ_BRA))
; List length is the offset of the first bitmap + the size of the bitmap objects
LISTSIZE   		.equ    (BITMAP_OFF+(1*SZ_SBM))
