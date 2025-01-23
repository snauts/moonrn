ARCH ?= -mz80

CFLAGS += --nostdinc --nostdlib --no-std-crt0
CFLAGS += --code-loc $(CODE) --data-loc $(DATA)

ENTRY = grep _start_up moonrn.map | cut -d " " -f 6

all:
	@echo "make zxs" - build .tap for ZX Spectrum
	@echo "make fuse" - build and run fuse

pcx:
	@gcc $(TYPE) -lm pcx-dump.c -o pcx-dump
	@./pcx-dump -c title.pcx > data.h
	@./pcx-dump -c horizon.pcx >> data.h
	@./pcx-dump -c reward.pcx >> data.h
	@./pcx-dump -c deed.pcx >> data.h
	@./pcx-dump -c credits.pcx >> data.h
	@./pcx-dump -c hazard.pcx >> data.h
	@./pcx-dump -c select.pcx >> data.h
	@./pcx-dump -p waver.pcx >> data.h
	@./pcx-dump -p runner.pcx >> data.h
	@./pcx-dump -p drowner.pcx >> data.h
	@./pcx-dump -p boat.pcx >> data.h
	@./pcx-dump -p bonus.pcx >> data.h
	@./pcx-dump -l level0.pcx >> data.h
	@./pcx-dump -l levelM.pcx >> data.h
	@./pcx-dump -l levelP.pcx >> data.h
	@./pcx-dump -l levelS.pcx >> data.h
	@./pcx-dump -l levelN.pcx >> data.h
	@./pcx-dump -l levelC.pcx >> data.h
	@./pcx-dump -l levelA.pcx >> data.h
	@./pcx-dump -l levelZ.pcx >> data.h
	@./pcx-dump -l levelG.pcx >> data.h
	@./pcx-dump -l levelL.pcx >> data.h
	@./pcx-dump -l levelO.pcx >> data.h
	@./pcx-dump -l levelB.pcx >> data.h
	@./pcx-dump -l level1.pcx >> data.h
	@./pcx-dump -l level2.pcx >> data.h
	@./pcx-dump -l level3.pcx >> data.h
	@./pcx-dump -l level4.pcx >> data.h
	@./pcx-dump -l level5.pcx >> data.h
	@./pcx-dump -l level6.pcx >> data.h
	@./pcx-dump -l level7.pcx >> data.h

prg: pcx
	@sdcc $(ARCH) $(CFLAGS) $(TYPE) main.c -o moonrn.ihx
	hex2bin moonrn.ihx > /dev/null

tap:
	bin2tap -b -r $(shell printf "%d" 0x$$($(ENTRY))) moonrn.bin

zxs:
	CODE=0x8000 DATA=0x7000	TYPE=-DZXS make prg
	@make tap

dsk:
	iDSK -n moonrn.dsk
	iDSK moonrn.dsk -f -t 1 -c 1000 -e $(shell $(ENTRY)) -i moonrn.bin

cpc:
	CODE=0x1000 DATA=0x8B00	TYPE=-DCPC make prg
	@make dsk

fuse: zxs
	fuse --machine 128 --no-confirm-actions moonrn.tap

clean:
	rm -f moonrn* pcx-dump data.h

mame: cpc
	mame cpc664 -uimodekey F1 -window -skip_gameinfo -flop1 moonrn.dsk \
		-autoboot_delay 1 -ab "RUN \"MOONRN.BIN\"\n"

cap: cpc
	cap32 moonrn.dsk -a "RUN \"MOONRN.BIN\""

crop:
	dd if=music.pt3 of=tempory.pt3 bs=1 skip=100
	mv tempory.pt3 music.pt3
