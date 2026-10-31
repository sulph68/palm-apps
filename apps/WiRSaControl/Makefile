# WiRSaControl ("WiRSaCtl") - single code segment. Objects and pilrc's
# .bin resources go in build/; the PRC lands here as wirsacontrol.prc.

       EXE = WiRSaControl
       PRC = wirsacontrol.prc
         B = build

      OBJS = $(B)/WiRSaControl.o $(B)/Modes.o $(B)/Wifi.o $(B)/Setup.o $(B)/Log.o \
             $(B)/wirsactl.o $(B)/relayproto.o $(B)/relaywire.o $(B)/relaysec.o $(B)/aes.o

  INCLUDES = $(shell find /home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/include -type d | sed 's/^/-I/' | tr '\n' ' ') -I../lib -I.
    CFLAGS = -palmos4 -Os -Wall -Wno-unknown-pragmas -Wno-multichar $(INCLUDES)

     PILRC = pilrc
        CC = m68k-palmos-gcc
  BUILDPRC = build-prc

all: $(PRC)

$(PRC): $(EXE).def $(B)/$(EXE) $(B)/bin.stamp
	$(BUILDPRC) -o $@ $(EXE).def $(B)/$(EXE) $(B)/*.bin

$(B)/bin.stamp: $(EXE).rcp WiRSaControlRsc.h $(wildcard icons/*.bmp) | $(B)
	rm -f $(B)/*.bin
	$(PILRC) -q $(EXE).rcp $(B)
	touch $@

$(B):
	mkdir -p $(B)

$(B)/%.o: %.c WiRSaControl.h WiRSaControlRsc.h ../lib/wirsactl.h | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/%.o: ../lib/%.c relay_cfg.h | $(B)
	$(CC) $(CFLAGS) -c $< -o $@

$(B)/$(EXE): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@

clean:
	rm -rf $(B)

clobber: clean
	rm -f $(PRC)

.PHONY: all clean clobber
