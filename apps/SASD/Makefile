# SASD slot driver (Palm OS), the gcc build of dmitry.gr's serialSD with the changes in ../SASD_driver.md.
#
#   make              SASD.prc      the driver; keeps a log in RAM only when asked to (never: no upload, nothing printed)
#   make LOG=1        SASD_log.prc  the same driver with the packet log: per-exchange events and counters, uploaded to a reader that asks for them (SD_FLAG_DIAG)
#   make EXTRA=-DTXTEST=150   (testing) pretend the transmitter stalled after every 150th send: the port is reopened, see slotPrvTxDrain
#   make PAIR=0       (testing) send a request and its data back to back whatever the link: the emulator has no slow link
#   make clean
#
# Toolchain: prc-tools-remix (m68k-palmos-gcc 2.95.3), pilrc. The compiler is old: see gccisms/ and firmware/divhelp.c.
TOOLCHAIN		?=	/usr/bin/
CC				=	$(TOOLCHAIN)/m68k-palmos-gcc
LD				=	$(TOOLCHAIN)/m68k-palmos-gcc
OBJCOPY			=	$(TOOLCHAIN)/m68k-palmos-objcopy
LOG				?=	0
PAIR			?=	12
ifeq ($(LOG),1)
TARGET			=	SASD_log
else
TARGET			=	SASD
endif
EXTRA			?=
COMMON			=	$(EXTRA) -DDG_LOG=$(LOG) -DPAIR_MIN_LAT=$(PAIR) -DSASD_TOL -DsysFtrNumProcessorSuperVZ=0x00040000UL -Os -m68000 -mno-align-int -mpcrel -fpic -fshort-enums -mshort -nostdlib
LKR				=	linker.lkr
CFLAGS			= 	-palmos4
CCFLAGS			=	$(COMMON) -I.
LDFLAGS			=	$(COMMON) -Wl,-T $(LKR)
SRCS			=	Src/Slot.c Src/maxSpeed.c firmware/frag.c firmware/slk.c firmware/divhelp.c
OBJS			=	$(patsubst %.c,%.68k.o,$(SRCS))
INCS			=	-Igccisms -Ifirmware

all:	$(TARGET).prc

# the resource file names the .bin; the variant's own copy of it is made here
%.prc: %.bin res.rcp
	sed 's/SASD_gcc.bin/$*.bin/' res.rcp > $*.rcp
	if [ $(LOG) = 1 ]; then head -c 264 /dev/zero > crumb.bin; echo 'DATA "crmb" ID 0 "crumb.bin"' >> $*.rcp; fi
	pilrc -ro -type libs -creator PpSS -name SASD $*.rcp $@
	rm -f $*.rcp crumb.bin

%.68k.o: %.c Makefile Src/tol.inc firmware/tol.h
	$(CC) $(CFLAGS) $(CCFLAGS) $(INCS) -c $< -o $@

$(TARGET).elf: $(OBJS)
	$(LD) -o $@ $(LDFLAGS) $^

%.bin: %.elf
	$(OBJCOPY) -O binary $< $@ -j.text -j.rodata

# the variants share object files, so a build of one is cleaned before the other
clean:
	rm -f $(OBJS) SASD.elf SASD.bin SASD_log.elf SASD_log.bin

.PHONY: clean all
