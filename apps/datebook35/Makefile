# Makefile - Datebook35 (era-aware Datebook). Compiles against the shared
# apps/lib (datelib + hackcheck) so the calendar/plugin code exists once.
APPNAME  = Datebook35
CREATOR  = Db35
PRC      = $(APPNAME).prc

SDK      = /home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/include
LIB      = ../lib
BUILD    = build

CC       = m68k-palmos-gcc
PILRC    = pilrc
OBJRES   = m68k-palmos-obj-res
BUILDPRC = build-prc
CFLAGS   = -palmos3.5 -O2 -Wall -I$(SDK) -I$(LIB)

CSRC     = datebook35.c $(LIB)/datelib.c $(LIB)/hackcheck.c $(LIB)/datepick.c
OBJS     = $(addprefix $(BUILD)/,$(notdir $(CSRC:.c=.o)))
VPATH    = .:$(LIB)

.PHONY: all clean
all: $(PRC)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(APPNAME): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $@

$(BUILD)/code.stamp: $(BUILD)/$(APPNAME)
	rm -f $(BUILD)/*.grc
	cd $(BUILD) && $(OBJRES) $(APPNAME)
	touch $@

$(BUILD)/rsc.stamp: datebook35.rcp $(LIB)/datepick.rcp
	rm -f $(BUILD)/*.bin
	$(PILRC) -I $(LIB) datebook35.rcp $(BUILD)
	touch $@

$(PRC): $(BUILD)/code.stamp $(BUILD)/rsc.stamp
	$(BUILDPRC) $(PRC) "$(APPNAME)" $(CREATOR) $(BUILD)/*.grc $(BUILD)/*.bin

clean:
	rm -rf $(BUILD) $(PRC)
