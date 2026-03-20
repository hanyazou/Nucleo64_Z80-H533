PJ_DIR ?= ../..
ASM_DIR ?= $(PJ_DIR)/tools/zasm
ASM ?= $(ASM_DIR)/zasm
ASM_OPTS ?= --opcodes --bin --target=ram --reqcolon

all: emubasic.inc ipl.inc

%.inc: %.asm
	$(ASM) $(ASM_OPTS) -l $*.lst -o $*.bin $*.asm && \
        cat $*.bin | xxd -i > $@

clean::
	rm -rf *.bin *.lst *.inc
