#!/bin/bash
# build.sh - ToDo35 TWO-segment build (main + sec002 via multigen). The core app +
# era lib fit main, but real vCal beaming (ToDoTransfer.c) pushed past the 32KB
# code-resource limit, so the SEND/export path + some foreground UI (NoteView,
# Details) live in sec002. The beam RECEIVE/import path stays in MAIN (TD35_RECV):
# a beam arriving while the app isn't running is a no-globals sublaunch and
# secondary segments aren't loaded then -- see td35_seg.h + beam-receive doc.
# Era-aware: edate_override.h widens DateType so the ToDo dueDate is full-range
# (1904..9999); era/edate/datelib live in main (datelib's libgcc must be in main).
set -e
cd "$(dirname "$0")"

SDK=/home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/include
GLUE=/home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/lib/m68k-palmos-coff
LIB=../lib
INCS=$(find "$SDK" -type d | sed 's/^/-I/' | tr '\n' ' ')
# edate_override.h first (renames+widens DateType); edate.h last (needs DATELIB_SECTION).
# ALLOW_ACCESS_TO_INTERNALS_OF_TABLES: ListViewInit grows the list table to 5
# columns in C (pilrc caps tables at 3) by touching TableType internals.
CFLAGS="-palmos4 -Os -DALLOW_ACCESS_TO_INTERNALS_OF_TABLES= -DTODOEXT_SEG_NAME=sec002 -include $LIB/edate_override.h -include $SDK/PalmUtils.h -include Src/td35_seg.h -include $LIB/edate.h $INCS -ISrc -I$LIB"
CC=m68k-palmos-gcc
B=build
mkdir -p "$B"

echo ">> compiling"
rm -f "$B"/*.o          # drop stale objects (e.g. the old todo35_beam.o stub)
# ToDoTransfer.c = real vCal beaming (was stubbed by todo35_beam.c). SDK header is
# IMCUtils.h (case fixed in the source); era import clamp widened to 9999.
SOURCES="ToDo ToDoDB ToDoTransfer"
for base in $SOURCES; do
  $CC $CFLAGS -c "Src/$base.c" -o "$B/$base.o"
done
for f in "$LIB/datepick.c" "$LIB/datelib.c" "$LIB/hackcheck.c" "$LIB/era.c" "$LIB/edate.c" "$LIB/catsync.c" "$LIB/todoext.c" "$LIB/enccore.c" "$LIB/aes.c" "$LIB/a35lookup.c"; do
  $CC $CFLAGS -c "$f" -o "$B/$(basename "${f%.c}").o"
done

echo ">> multigen segment glue (ToDoTransfer.c -> sec002; see td35_seg.h)"
m68k-palmos-multigen -b "$B/ToDo35-sections" ToDo35.def
$CC $CFLAGS -c "$B/ToDo35-sections.s" -o "$B/ToDo35-sections.o"

echo ">> linking (main + sec002)"
$CC $CFLAGS "$B"/*.o "$B/ToDo35-sections.ld" -L"$GLUE" -lPalmOSGlue -o "$B/ToDo35" 2>&1 | \
  grep -vE 'sections.o|already' || true

echo ">> resources"
rm -rf "$B/res"; mkdir -p "$B/res"
cat forms.rcp > "$B/forms.rcp"                 # Td35: static, hand-maintained (see its own header)
cat details1200.rcp >> "$B/forms.rcp"          # Td35: Details dialog with Time:/Repeat:
cat details1250.rcp >> "$B/forms.rcp"          # Td35: Change Repeat dialog
cat "$LIB/datepick.rcp" >> "$B/forms.rcp"      # era-aware full-range picker (form 8000)
cat "$LIB/hackalerts.rcp" >> "$B/forms.rcp"    # "plugin required" alerts (8900/8901)
cat abouttd35.rcp >> "$B/forms.rcp"           # ToDo35: custom About (8800)
pilrc "$B/forms.rcp" "$B/res" >/dev/null 2>&1
# fixed resources taken once from the original application: strings, alerts,
# category names etc. (resources.rcp) and the runtime bitmaps (bitmaps.rcp).
# Compiled after the forms so that, as before, they win on any shared ID.
pilrc resources.rcp "$B/res" >/dev/null 2>&1
pilrc bitmaps.rcp "$B/res" >/dev/null 2>&1

# ToDo35: launcher icon = ROM disc + "35" overlay. The launcher reads the standard
# 'tAIB' resource (1000 large / 1001 small), NOT the Constructor 'taif'/'taic'
# editor families from the Mac .rsrc (ignored at runtime). Emit real tAIB and drop
# the stale taif/taic.
rm -f "$B/res/taif"*.bin "$B/res/taic"*.bin
# Large icon = ROM disc + "35" overlay; small (list-view) icon = the ROM To Do
# small icon verbatim (10x9 -- a "35" text badge is illegible at this size).
cp icons/icon35_large.bmp "$B/big.bmp"; cp icons/rom_small.bmp "$B/sml.bmp"
( cd "$B"; printf 'ICON "big.bmp"\nSMALLICON "sml.bmp"\n' > appicon.rcp; pilrc appicon.rcp res ) >/dev/null 2>&1

# Brand as ToDo35
printf 'ToDo35\0' > "$B/res/tAIN03e8.bin" 2>/dev/null || true
printf '1.0\0' > "$B/res/tver0001.bin"                 # ToDo35: version 1.0

echo ">> build-prc"
build-prc -o ToDo35.prc ToDo35.def "$B/ToDo35" "$B"/res/*.bin
echo "DONE: $(ls -la ToDo35.prc | awk '{print $5}') bytes"
