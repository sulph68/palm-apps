       LOC = En

   VERSION = 1

       EXE = CalDAVSync

# grown incrementally as each piece lands (see the implementation plan) -
# not all files exist yet.
      OBJS = CalDAVSync.o MainForm.o SlotEditForm.o DupSlot.o ContactDb.o LongTextDialog.o PrefsForm.o HelpForm.o \
             SyncProgress.o \
             CDDVPrefs.o CDDVSec35.o DAVDb.o \
             Serial.o relayproto.o relaywire.o relaysec.o wirsactl.o SyncEngine.o OAuthRelay.o \
             enccore.o aes.o \
             $(EXE)-sections.o

   DEFINES = -DPALMOS4

  INCLUDES = $(shell find /home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/include -type d | sed 's/^/-I/' | tr '\n' ' ') -I../lib

PALMOS_FLAG ?= -palmos4

# -include cddv_seg.h: force CalDAV Sync's own segment tags into every
# translation unit before any file's own #include chain runs - same reason
# as lazyFTP's identical -include lz_seg.h (see that Makefile's own comment):
# ../lib/enccore.c's own "#include sects.h" resolves relative to ITS OWN
# directory and needs ENCCORE_SECTION already defined to skip the
# single-segment fallback.
    CFLAGS = $(PALMOS_FLAG) -Os $(DEFINES) $(INCLUDES) -I../lib -I. -include cddv_seg.h

     PILRC = pilrc
        CC = m68k-palmos-gcc
  MULTIGEN = m68k-palmos-multigen
  BUILDPRC = build-prc

  ICONTEXT = "CalDAVSync"
     APPID = CDDV
# Explicit fixed name (not the $(EXE)_v$(VERSION).prc pattern some other
# apps in this suite use) - per explicit project request.
       PRC = caldav_sync.prc

all: $(PRC)

$(PRC): $(EXE).def $(EXE) bin.stamp
	$(BUILDPRC) -o $@ $(EXE).def $(EXE) *.bin

bin.stamp: Main$(LOC).rcp $(wildcard *.bmp)
	$(PILRC) Main$(LOC).rcp
	touch bin.stamp

$(EXE)-sections.o: $(EXE)-sections.s
	$(CC) -c $(EXE)-sections.s

$(EXE)-sections.s $(EXE)-sections.ld: $(EXE).def
	$(MULTIGEN) $(EXE).def

enccore.o: ../lib/enccore.c
	$(CC) $(CFLAGS) -c $< -o $@

aes.o: ../lib/aes.c
	$(CC) $(CFLAGS) -c $< -o $@

# Shared WiRSa relay framing; relayproto.c finds this app's relay_cfg.h via -I.
relayproto.o: ../lib/relayproto.c ../lib/relayproto.h ../lib/relaysec.h relay_cfg.h
	$(CC) $(CFLAGS) -c $< -o $@

relaywire.o: ../lib/relaywire.c ../lib/relaywire.h
	$(CC) $(CFLAGS) -c $< -o $@

relaysec.o: ../lib/relaysec.c ../lib/relaysec.h ../lib/relayproto.h relay_cfg.h
	$(CC) $(CFLAGS) -c $< -o $@

# WiRSa control link - Screenless Mode (main segment: blocking reads).
wirsactl.o: ../lib/wirsactl.c ../lib/wirsactl.h ../lib/relayproto.h ../lib/relaywire.h ../lib/relaysec.h
	$(CC) $(CFLAGS) -c $< -o $@

$(EXE): $(OBJS) $(EXE)-sections.ld
	$(CC) $(CFLAGS) $(OBJS) $(EXE)-sections.ld -o $@

clean:
	rm -rf *.[oas] $(EXE) $(EXE)-sections.ld *.bin *.stamp

clobber: clean
	rm -f $(PRC)
