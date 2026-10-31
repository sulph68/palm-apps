# Meshtastic client - single code segment. Objects and pilrc's .bin resources go in
# build/; the PRC lands here as meshtastic.prc.

       EXE = MeshChat
       DEF = MeshChat
       PRC = meshtastic.prc
         B = build

      OBJS = $(B)/MeshChat.o $(B)/Data.o $(B)/Node.o $(B)/Relay.o $(B)/Link.o $(B)/Views.o $(B)/Chat.o $(B)/datelib.o $(B)/hackcheck.o $(B)/era.o \
             $(B)/wirsactl.o $(B)/relayproto.o $(B)/relaywire.o $(B)/relaysec.o $(B)/aes.o $(B)/$(EXE)-sections.o

  INCLUDES = $(shell find /home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/include -type d | sed 's/^/-I/' | tr '\n' ' ') -I../lib -I.
    CFLAGS = -palmos4 -Os -Wall -Wno-unknown-pragmas -Wno-multichar $(INCLUDES) -include mc_seg.h -DRP_WITH_PENDING

     PILRC = pilrc
        CC = m68k-palmos-gcc
  BUILDPRC = build-prc
   MULTIGEN = m68k-palmos-multigen

all: $(PRC)

$(PRC): $(DEF).def $(B)/$(EXE) $(B)/bin.stamp
	$(BUILDPRC) -o $@ $(DEF).def $(B)/$(EXE) $(B)/*.bin

$(B)/bin.stamp: $(EXE).rcp MeshChatRsc.h $(wildcard icons/*.bmp) | $(B)
	rm -f $(B)/*.bin
	$(PILRC) -q $(EXE).rcp $(B)
	touch $@

$(B):
	mkdir -p $(B)

$(B)/%.o: %.c MeshChat.h MeshChatRsc.h mc_seg.h ../lib/wirsactl.h | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/%.o: ../lib/%.c relay_cfg.h | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/$(EXE)-sections.s $(B)/$(EXE)-sections.ld: $(DEF).def | $(B)
	cd $(B) && $(MULTIGEN) ../$(DEF).def

$(B)/$(EXE)-sections.o: $(B)/$(EXE)-sections.s
	$(CC) -c $< -o $@

$(B)/$(EXE): $(OBJS) $(B)/$(EXE)-sections.ld
	$(CC) $(CFLAGS) $(OBJS) $(B)/$(EXE)-sections.ld -o $@

clean:
	rm -rf $(B)

clobber: clean
	rm -f $(PRC)

.PHONY: all clean clobber
