#!/bin/bash
# Builds Bkp1Test.prc: the real Bkp1 (screens, chooser, restore) reading a backup embedded in the
# PRC instead of a card, for POSE. usage: ./build_fake.sh <backup folder> [out.prc] [skip,names]
set -e; cd "$(dirname "$0")"
FOLDER=$(readlink -f "$1"); OUT=${2:-Bkp1Test.prc}; SKIP=${3:-}
SDK=/home/cnrai/palmos_sdk/palm-os-sdk/sdk-4
INCS="$(find $SDK/include -type d | sed 's/^/-I/' | tr '\n' ' ') -I../compat -I../Src -I."
rm -rf fake; mkdir -p fake/parts fake/res
python3 - "$FOLDER" "$SKIP" <<'PY'
import os, sys, struct
folder, skip = sys.argv[1], set(sys.argv[2].split(',')) if sys.argv[2] else set()
PART = 60000; rcp = []; idx = b''; rid = 1; n = 0
for f in sorted(os.listdir(folder)):
    d = open(os.path.join(folder, f), 'rb').read()
    if d[:32].split(b'\0')[0].decode('latin1') in skip: continue
    first = rid
    for o in range(0, len(d), PART):
        open('fake/parts/p%d.bin' % rid, 'wb').write(d[o:o + PART]); rcp.append('DATA "bkpf" ID %d "parts/p%d.bin"\n' % (rid, rid)); rid += 1
    idx += struct.pack('>HHI', first, rid - first, len(d)); n += 1
open('fake/parts/idx.bin', 'wb').write(struct.pack('>H', n) + idx)
rcp.append('DATA "bkix" ID 1 "parts/idx.bin"\n'); open('fake/fake.rcp', 'w').write(''.join(rcp))
print(n, 'files embedded')
PY
for f in bkp1 ops choose VfsExportImport ExgDB; do m68k-palmos-gcc -palmos4 -Os -DBKP1_FAKE_VFS $INCS -c ../Src/$f.c -o fake/$f.o 2>&1 | grep -E "\.c:[0-9]+: [^w]" || true; done
m68k-palmos-gcc -palmos4 -Os -DBKP1_FAKE_VFS $INCS -c fakevfs.c -o fake/fakevfs.o 2>&1 | grep -E "\.c:[0-9]+: [^w]" || true
m68k-palmos-gcc -palmos4 fake/*.o -L$SDK/lib/m68k-palmos-coff -lPalmOSGlue -o fake/Bkp1
(cd ../Rsc && pilrc -q res.rcp ../test/fake/res && pilrc -q version.rcp ../test/fake/res)
(cd fake && pilrc -q fake.rcp res)
build-prc -o $OUT ../Bkp1.def fake/Bkp1 fake/res/*.bin
ls -la $OUT
