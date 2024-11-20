ARCH ?= -mz80

CFLAGS += --nostdinc --nostdlib --no-std-crt0
CFLAGS += --code-loc $(CODE) --data-loc $(DATA)

ENTRY = grep _reset crawlo.map | cut -d " " -f 6

all:
	@echo "make zxs" - build .tap for ZX Spectrum
	@echo "make fuse" - build and run fuse

prg:
	@gcc $(TYPE) -lm pcx-dump.c -o pcx-dump
	@sdcc $(ARCH) $(CFLAGS) $(TYPE) main.c -o crawlo.ihx
	hex2bin crawlo.ihx > /dev/null

tap:
	bin2tap -b -r $(shell printf "%d" 0x$$($(ENTRY))) crawlo.bin

zxs:
	CODE=0x8000 DATA=0x7000	TYPE=-DZXS make prg
	@make tap

fuse: zxs
	fuse --machine 128 --no-confirm-actions -g 2x crawlo.tap

clean:
	rm -f crawlo* pcx-dump
