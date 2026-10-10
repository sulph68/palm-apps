# Makefile - HackMaster Y2K35 plugin (Phase 1)
#
#   make            build X-Y2K35.prc (the HACK)
#   make probe      build DateProbe.prc (verification harness app)
#   make test       build & run host-side datelib unit tests
#   make clean

APPNAME  = X-Y2K35
CREATOR  = Y235
PRC      = $(APPNAME).prc

PROBE          = DateProbe.prc
PROBE_CREATOR  = DPrb

SDK     ?= ../../palm-os-sdk/sdk-4/include
BUILD    = build
PROBEB   = $(BUILD)/probe

CC       = m68k-palmos-gcc
PILRC    = pilrc
OBJRES   = m68k-palmos-obj-res
BUILDPRC = build-prc
CFLAGS   = -palmos3.5 -O2 -Wall -I$(SDK) -Isrc -I../apps/lib

# Stand-alone code resources bundled into the HACK (patches + control panel).
PATCHES  = $(BUILD)/hack_sec2dt $(BUILD)/ctlpanel

.PHONY: all probe test clean
all: $(PRC)

$(BUILD):
	mkdir -p $(BUILD)
$(PROBEB):
	mkdir -p $(PROBEB)

# ===== HACK (stand-alone patches + tAIN/tFRM) =====

# stand-alone hardening patch (TimSecondsToDateTime); links datelib
$(BUILD)/hack_sec2dt: src/hack_sec2dt.c ../apps/lib/datelib.c | $(BUILD)
	$(CC) $(CFLAGS) -nostartfiles -Wl,-e,HackSecToDateTime src/hack_sec2dt.c ../apps/lib/datelib.c -o $@

# control-panel handler (code #2000); links datelib (pure, no data section)
$(BUILD)/ctlpanel: src/ctlpanel.c ../apps/lib/datelib.c | $(BUILD)
	$(CC) $(CFLAGS) -nostartfiles -Wl,-e,CtlPanelHandler src/ctlpanel.c ../apps/lib/datelib.c -o $@

$(BUILD)/rsc.stamp: rsc/hack.rcp | $(BUILD)
	rm -f $(BUILD)/*.bin
	$(PILRC) rsc/hack.rcp $(BUILD)
	touch $@

$(PRC): $(PATCHES) $(BUILD)/rsc.stamp
	$(BUILDPRC) -H -o $(PRC) -n "$(APPNAME)" -c $(CREATOR) $(PATCHES) $(BUILD)/*.bin

# ===== DateProbe (normal appl) =====
probe: $(PROBE)

$(PROBEB)/dateprobe.o: probe/dateprobe.c | $(PROBEB)
	$(CC) $(CFLAGS) -c $< -o $@

$(PROBEB)/datelib.o: ../apps/lib/datelib.c | $(PROBEB)
	$(CC) $(CFLAGS) -c $< -o $@

$(PROBEB)/DateProbe: $(PROBEB)/dateprobe.o $(PROBEB)/datelib.o
	$(CC) $(CFLAGS) $^ -o $@

$(PROBEB)/code.stamp: $(PROBEB)/DateProbe
	rm -f $(PROBEB)/*.grc
	cd $(PROBEB) && $(OBJRES) DateProbe
	touch $@

$(PROBEB)/rsc.stamp: probe/dateprobe.rcp | $(PROBEB)
	rm -f $(PROBEB)/*.bin
	$(PILRC) probe/dateprobe.rcp $(PROBEB)
	touch $@

$(PROBE): $(PROBEB)/code.stamp $(PROBEB)/rsc.stamp
	$(BUILDPRC) $(PROBE) "DateProbe" $(PROBE_CREATOR) $(PROBEB)/*.grc $(PROBEB)/*.bin

# ===== host-side unit tests =====
test:
	cc -O2 -Wall -I../apps/lib ../apps/lib/datelib.c test/test_datelib.c -o $(BUILD)/test_datelib && $(BUILD)/test_datelib

clean:
	rm -rf $(BUILD) $(PRC) $(PROBE)
