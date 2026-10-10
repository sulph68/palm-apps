# Bkp1 - prc-tools (m68k-palmos-gcc) build. The original project is CodeWarrior (bkp1.mcp);
# the original binary is kept as Bkp1_orig.prc.
SDK   = /home/cnrai/palmos_sdk/palm-os-sdk/sdk-4
INCS  = $(shell find $(SDK)/include -type d | sed 's/^/-I/') -Icompat -ISrc
CC    = m68k-palmos-gcc
CFLAGS = -palmos4 -Os $(INCS)
OBJS  = build/bkp1.o build/ops.o build/choose.o build/VfsExportImport.o build/ExgDB.o

all: Bkp1.prc

build/%.o: Src/%.c Src/ops.h Src/choose.h Src/ExgDB.h Src/VfsExportImport.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/Bkp1: $(OBJS)
	$(CC) -palmos4 $(OBJS) -L$(SDK)/lib/m68k-palmos-coff -lPalmOSGlue -o $@

Bkp1.prc: build/Bkp1 Rsc/res.rcp Rsc/version.rcp Bkp1.def
	rm -f build/*.bin
	cd Rsc && pilrc -q res.rcp ../build && pilrc -q version.rcp ../build
	build-prc -o $@ Bkp1.def build/Bkp1 build/*.bin
	m68k-palmos-size build/Bkp1

clean:
	rm -rf build
