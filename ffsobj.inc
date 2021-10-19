;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Begin SkunkUSB LOGO GEOMETRY CONFIGURATION
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

PPP1B			.equ	64			; Pixels per Phrase (1-bit)

LGO_WIDTH		.equ	320			; Width in Pixels
LGO_HEIGHT		.equ	160			; Height in Pixels
LGO_PHRASES		.equ	(LGO_WIDTH/PPP1B)
SCALESPEED		.equ	4			; Scale once every <n> VSyncs

GL_WIDTH		.equ	192			; Width in Pixels
GL_HEIGHT		.equ	180			; Height in Pixels
GL_PHRASES		.equ	(GL_WIDTH/PPP1B)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; End SkunkUSB LOGO GEOMETRY CONFIGURATION
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Globals Variables:
				.extern	_ticks
				.extern	_doscale
				.extern listbuf
				.extern bmpupdate
				.extern	glbmupdate
				.extern	time
				.extern deltat
				.extern	deltas
				.extern scalespeed
				.extern	_gamelstbm

; Globals Functions:
				.extern	InitGPUOP
				.extern	_showgl

; Object Processor Object Size Constants
SZ_BM			.equ	(2*8)
SZ_SBM			.equ	(3*8)
SZ_GPU			.equ	(1*8)
SZ_BRA			.equ	(1*8)
SZ_STP			.equ	(1*8)
PHRASE			.equ	(1*8)

LINKMASK_LO		.equ	$FF000000
LINKMASK_HI		.equ	$000007FF

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
; Logo Bitmap is 3 branch objects past the list start
BMLOGO_OFF 		.equ    (LIST_START_OFF+(3*SZ_BRA))
; Game List Bitmap is one phrase past the end of the Logo Bitmap (To align it)
BMGAMELST_OFF 		.equ    (BMLOGO_OFF+SZ_SBM+PHRASE)
; List length is the offset of the last bitmap + its size
LISTSIZE   		.equ    (BMGAMELST_OFF+(1*SZ_BM))
