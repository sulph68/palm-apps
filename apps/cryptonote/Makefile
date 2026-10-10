       LOC = En

   VERSION = 1

       EXE = CryptoNote

      OBJS = Beam.o Database.o DetailsForm.o MainForm.o Utils.o \
      		 EditForm.o Main.o PrefsForm.o PasswordForm.o \
      		 QueryGlobalPasswordForm.o SetAndChangeGlobalPasswordForm.o \
      		 aes.o AesCrypt.o CardIO.o CardCrypt.o catsync.o \
      		 enccore.o CryptoSec35.o $(EXE)-sections.o

#uncomment this if you want to build a gdb debuggable version
#   DEFINES = -DDEBUG
   DEFINES = -D_MULTISEGMENT -DCATSYNC_MULTISEG 

  INCLUDES = $(shell palm-includes 2>/dev/null || find /home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/include -type d | sed 's/^/-I/' | tr '\n' ' ')

# Target-OS flag. Defaults to -palmos4 (a native prc-tools install with
# palmdev-prep-written specs, the intended/correct build). Override to empty on
# environments where palmdev-prep can't run (e.g. the Docker container, which
# runs `make PALMOS_FLAG=` and supplies the SDK include tree via INCLUDES).
PALMOS_FLAG ?= -palmos4

#    CFLAGS = -O2 -g $(DEFINES) $(INCLUDES)
# -include sects.h: force CryptoNote's own sects.h (AES_SECTION/BEAM_SECTION/
# ENCCORE_SECTION -> sec001) into every translation unit BEFORE any file's own
# #include chain runs -- needed so ../lib/enccore.c's "#include sects.h"
# (which resolves relative to ITS OWN directory, ../lib/sects.h, the
# single-segment default/fallback) sees ENCCORE_SECTION already defined and
# skips that fallback, instead of silently landing enccore.c's functions in
# main (which has ~472 bytes free -- no room). Same idiom Datebook35 uses for
# db35_seg.h.
    CFLAGS = $(PALMOS_FLAG) -Os -DPALMOS3 $(DEFINES) $(INCLUDES) -I../lib -include sects.h
#    CFLAGS = -palmos3.5 -O2 -DPALMOS3 $(DEFINES) $(INCLUDES)
#    CFLAGS = -palmos5r4 -O2 -DPALMOS3 -g $(DEFINES) $(INCLUDES)
#    CFLAGS = -palmos3.5 -O2 -DPALMOS3 -g $(DEFINES) $(INCLUDES)
   CSFLAGS = -palmos2 -O2 -S $(DEFINES) $(INCLUDES)

     PILRC = pilrc
  TXT2BITM = txt2bitm
        CC = m68k-palmos-gcc
    OBJRES = m68k-palmos-obj-res
  MULTIGEN = m68k-palmos-multigen
  BUILDPRC = build-prc

  ICONTEXT = "CryptoNote"
     APPID = CrNt
       PRC = $(EXE)_v$(VERSION).prc

all: $(PRC)

.S.o:
	$(CC) $(TARGETFLAGS) -c $<

.c.s:
	$(CC) $(CSFLAGS) $<

# $(PRC): code.stamp bin.stamp
# 	$(BUILDPRC) $@ $(ICONTEXT) $(APPID) *.grc *.bin
$(PRC): $(EXE).def $(EXE) bin.stamp
	$(BUILDPRC) -o $@ $(EXE).def $(EXE) *.bin

# code.stamp: $(EXE)
# 	$(OBJRES) $(EXE)
# 	touch code.stamp

bin.stamp: Main$(LOC).rcp $(wildcard *.bmp)
	$(PILRC) Main$(LOC).rcp
	touch bin.stamp

$(EXE)-sections.o: $(EXE)-sections.s
	m68k-palmos-gcc -c $(EXE)-sections.s

$(EXE)-sections.s $(EXE)-sections.ld: $(EXE).def
	$(MULTIGEN) $(EXE).def

catsync.o: ../lib/catsync.c
	$(CC) $(CFLAGS) -c $< -o $@

enccore.o: ../lib/enccore.c
	$(CC) $(CFLAGS) -c $< -o $@

$(EXE): $(OBJS) $(EXE)-sections.ld
	$(CC) $(CFLAGS) $(OBJS) $(EXE)-sections.ld -o $@

# clean:
# 	rm -rf *.[oa] $(EXE) *.bin *.stamp *.grc
clean:
	rm -rf *.[oas] $(EXE) $(EXE)-sections.ld *.bin *.stamp

clobber: clean
	rm $(PRC)
