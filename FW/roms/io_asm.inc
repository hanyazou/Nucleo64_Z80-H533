;	Common definitions for Z80-Simulator
;
;	Copyright (C) 1988-2007 by Udo Munk
;
;
;	SuperMEZ80 I/O ports
;
CONSTA		EQU	0		;console status port
CONDAT		EQU	1		;console data port
PRTSTA		EQU	2		;printer status port
PRTDAT		EQU	3		;printer data port
AUXDAT		EQU	5		;auxiliary data port
FDCDAT		EQU	8		;fdc-port: # of drive
FDCD		EQU	10		;fdc-port: # of drive
FDCT		EQU	11		;fdc-port: # of track
FDCS		EQU	12		;fdc-port: # of sector
FDCOP		EQU	13		;fdc-port: command
		D_DMA_READ	EQU	0
		D_DMA_WRITE	EQU	1
		D_READ		EQU	2
		D_WRITE		EQU	3
FDCST		EQU	14		;fdc-port: status
		D_SUCCESS	EQU	0
		D_ERROR		EQU	1
DMAL		EQU	15		;dma-port: dma address low
DMAH		EQU	16		;dma-port: dma address high
MMU_INIT	EQU	20		;14h MMU initialisation
MMU_BANK_SEL	EQU	21		;15h MMU bank select
MMU_SEG_SIZE	EQU	22		;16h MMU select segment size (in pages a 256 bytes)
MMU_WR_PROT	EQU	23		;17h MMU write protect/unprotect common memory segment
