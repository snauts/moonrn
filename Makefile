ARCH ?= -mz80

CFLAGS += --nostdinc --nostdlib --no-std-crt0
CFLAGS += --code-loc $(CODE) --data-loc $(DATA)

ENTRY = grep _reset moonrn.map | cut -d " " -f 6

all:
	@echo "make zxs" - build .tap for ZX Spectrum
	@echo "make fuse" - build and run fuse

pcx:
	@gcc $(TYPE) -lm pcx-dump.c -o pcx-dump
	@./pcx-dump -c title.pcx > data.h
	@./pcx-dump -c horizon.pcx >> data.h
	@./pcx-dump -c outro.pcx >> data.h
	@./pcx-dump -p waver.pcx >> data.h
	@./pcx-dump -p runner.pcx >> data.h
	@./pcx-dump -p drowner.pcx >> data.h
	@./pcx-dump -p credits.pcx >> data.h
	@./pcx-dump -l level1.pcx >> data.h
	@./pcx-dump -l level2.pcx >> data.h
	@./pcx-dump -l level3.pcx >> data.h

prg: pcx
	@sdcc $(ARCH) $(CFLAGS) $(TYPE) main.c -o moonrn.ihx
	hex2bin moonrn.ihx > /dev/null

tap:
	bin2tap -b -r $(shell printf "%d" 0x$$($(ENTRY))) moonrn.bin

zxs:
	CODE=0x8000 DATA=0x7000	TYPE=-DZXS make prg
	@make tap

fuse: zxs
	fuse --machine 128 --no-confirm-actions -g 2x moonrn.tap

clean:
	rm -f moonrn* pcx-dump data.h
