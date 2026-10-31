       LOC = En

   VERSION = 0.1

       EXE = BitWarden

# Multi-segment as of 2026-08 (was single-segment through v1 - see bw_seg.h's
# own comment on why: main's own 32KB code resource finally hit its cap).
# enccore.c/aes.c's own sects.h defaults their SECTION macros to empty when
# nothing pre-defines them, so they still compile as ordinary main-segment
# code here (bw_seg.h deliberately does NOT override AES_SECTION/
# ENCCORE_SECTION - see its own comment).
      OBJS = BitWarden.o MainForm.o RecordForm.o PrefsForm.o Dialogs.o Sync.o \
             Serial.o Store.o BitwardenSec35.o relayproto.o relaywire.o relaysec.o wirsactl.o Screenless.o \
             enccore.o aes.o totp.o $(EXE)-sections.o

   DEFINES = -DPALMOS4

  INCLUDES = $(shell find /home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/include -type d | sed 's/^/-I/' | tr '\n' ' ') -I../lib

# Defaults to -palmos4 (the intended build). Without it, main->sec001 far
# calls in new object files become near bsr.w (DISP16) -> wild jump. See
# bw_seg.h's own comment / the family's own memory on this exact bug class.
PALMOS_FLAG ?= -palmos4

    CFLAGS = $(PALMOS_FLAG) -Os $(DEFINES) $(INCLUDES) -I../lib -I. -include bw_seg.h

     PILRC = pilrc
        CC = m68k-palmos-gcc
  MULTIGEN = m68k-palmos-multigen
  BUILDPRC = build-prc

  ICONTEXT = "BitWarden"
     APPID = BWrd
       PRC = Bitwarden.prc

all: $(PRC)

$(PRC): $(EXE).def $(EXE) bin.stamp
	$(BUILDPRC) -o $@ $(EXE).def $(EXE) *.bin

bin.stamp: $(EXE)$(LOC).rcp
	$(PILRC) $(EXE)$(LOC).rcp
	touch bin.stamp

enccore.o: ../lib/enccore.c
	$(CC) $(CFLAGS) -c $< -o $@

aes.o: ../lib/aes.c
	$(CC) $(CFLAGS) -c $< -o $@

totp.o: ../lib/totp.c
	$(CC) $(CFLAGS) -c $< -o $@

# Shared WiRSa relay framing; relayproto.c finds this app's relay_cfg.h via -I.
relayproto.o: ../lib/relayproto.c ../lib/relayproto.h ../lib/relaysec.h relay_cfg.h
	$(CC) $(CFLAGS) -c $< -o $@

relaywire.o: ../lib/relaywire.c ../lib/relaywire.h
	$(CC) $(CFLAGS) -c $< -o $@

relaysec.o: ../lib/relaysec.c ../lib/relaysec.h ../lib/relayproto.h relay_cfg.h
	$(CC) $(CFLAGS) -c $< -o $@

# WiRSa control link - Screenless Mode (WCTL_SECTION = SEC001, bw_seg.h).
wirsactl.o: ../lib/wirsactl.c ../lib/wirsactl.h ../lib/relayproto.h ../lib/relaywire.h ../lib/relaysec.h
	$(CC) $(CFLAGS) -c $< -o $@

$(EXE): $(OBJS) $(EXE)-sections.ld
	$(CC) $(CFLAGS) $(OBJS) $(EXE)-sections.ld -o $(EXE)

$(EXE)-sections.o: $(EXE)-sections.s
	$(CC) -c $(EXE)-sections.s -o $@

$(EXE)-sections.s $(EXE)-sections.ld: $(EXE).def
	$(MULTIGEN) $(EXE).def

clean:
	rm -rf *.[oa] $(EXE) *.bin *.stamp *.s *.ld

clobber: clean
	rm -f $(PRC)
