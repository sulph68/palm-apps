       LOC = En
   VERSION = 1
       EXE = Address35

      OBJS = Address35.o MainForm.o EditForm.o RecordForm.o EditDialogs.o \
             PrefsForm.o AddressDB35.o vcard.o vcardw.o Transfer.o CardIO.o \
             AddrCard.o Lookup.o hackcheck.o catsync.o enccore.o aes.o $(EXE)-sections.o

   DEFINES = -DCATSYNC_MULTISEG
  INCLUDES = $(shell find /home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/include -type d | sed 's/^/-I/' | tr '\n' ' ') -I../lib

# Defaults to -palmos4 (the intended build). Without it, main->secNNN far calls in
# new object files become near bsr.w (DISP16) -> wild jump. See the family memory.
PALMOS_FLAG ?= -palmos4

    CFLAGS = $(PALMOS_FLAG) -Os -DPALMOS3 $(DEFINES) $(INCLUDES) -include a35_seg.h

     PILRC = pilrc
        CC = m68k-palmos-gcc
  MULTIGEN = m68k-palmos-multigen
  BUILDPRC = build-prc
       PRC = $(EXE)_v$(VERSION).prc

all: $(PRC)

$(PRC): $(EXE).def $(EXE) bin.stamp
	$(BUILDPRC) -o $@ $(EXE).def $(EXE) *.bin

bin.stamp: $(EXE)$(LOC).rcp $(wildcard *.bmp)
	$(PILRC) $(EXE)$(LOC).rcp
	touch bin.stamp

$(EXE): $(OBJS) $(EXE)-sections.ld
	$(CC) $(CFLAGS) $(OBJS) $(EXE)-sections.ld -o $@

$(EXE)-sections.o: $(EXE)-sections.s
	$(CC) -c $(EXE)-sections.s -o $@

$(EXE)-sections.s $(EXE)-sections.ld: $(EXE).def
	$(MULTIGEN) $(EXE).def

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# shared library objects (apps/lib); catsync.c opts into sec001 via
# -DCATSYNC_MULTISEG above (main is at its 32KB cap; CatSync() is only reached
# from StartApplication's normal-launch path, never a sublaunch)
%.o: ../lib/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf *.o *.s $(EXE) $(EXE)-sections.s $(EXE)-sections.ld *.bin *.stamp *.grc

clobber: clean
	rm -f $(PRC)
