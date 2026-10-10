       LOC = En

   VERSION = 1

       EXE = LazyFTP

# grown incrementally as each piece lands (see project memory
# lazyftp-project.md task list) - not all files exist yet.
      OBJS = LazyFTP.o MainForm.o BusyDialog.o FtpProtocol.o Listing.o \
             ConnectForm.o PrefsForm.o PromptForm.o PreviewForm.o FileActions.o \
             UploadForm.o LogForm.o \
             LzTPSec35.o LzTPLog.o enccore.o aes.o \
             $(EXE)-sections.o

   DEFINES = -DPALMOS4

  INCLUDES = $(shell find /home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/include -type d | sed 's/^/-I/' | tr '\n' ' ') -I../lib

PALMOS_FLAG ?= -palmos4

# -include lz_seg.h: force lazyFTP's own segment tags (see lz_seg.h) into
# every translation unit before any file's own #include chain runs - needed
# so ../lib/enccore.c's "#include sects.h" (which resolves relative to ITS
# OWN directory) sees ENCCORE_SECTION already defined and skips the
# single-segment fallback, matching Address35/CryptoNote/Datebook35's own
# idiom for the same shared crypto library.
    CFLAGS = $(PALMOS_FLAG) -Os $(DEFINES) $(INCLUDES) -I../lib -include lz_seg.h

     PILRC = pilrc
        CC = m68k-palmos-gcc
  MULTIGEN = m68k-palmos-multigen
  BUILDPRC = build-prc

  ICONTEXT = "LazyFTP"
     APPID = LzTP
       PRC = $(EXE)_v$(VERSION).prc

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

$(EXE): $(OBJS) $(EXE)-sections.ld
	$(CC) $(CFLAGS) $(OBJS) $(EXE)-sections.ld -o $@

clean:
	rm -rf *.[oas] $(EXE) $(EXE)-sections.ld *.bin *.stamp

clobber: clean
	rm -f $(PRC)
