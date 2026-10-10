#!/bin/bash
# build.sh - Datebook35 multi-segment build.
# The original Datebook is ~88KB of code, over the 32KB PalmOS code-resource
# limit, so it must be split into multiple 'code' segments (as the original
# was). We keep every original source file byte-identical: a file is placed
# into a segment by relabeling its single assembler .text directive to a named
# section (sec001..sec003) at the .s stage. multigen builds the segment glue +
# linker script; build-prc packages code segments with the extracted original
# resources.
set -e
cd "$(dirname "$0")"

SDK=/home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/include
GLUE=/home/cnrai/palmos_sdk/palm-os-sdk/sdk-4/lib/m68k-palmos-coff
LIB=../lib
INCS=$(find "$SDK" -type d | sed 's/^/-I/' | tr '\n' ' ')
# db35_seg.h (force-included) defines SEC001..SEC003; the segment placement is
# driven by __attribute__((section)) tags in the sources/headers so the COMPILER
# knows each function's segment and emits correct cross-segment call sequences.
# edate_override.h MUST be first (renames the SDK's 7-bit DateType, widens it);
# edate.h last (wrapper prototypes, needs DATELIB_SECTION from db35_seg.h).
# ALLOW_ACCESS_TO_INTERNALS_OF_TABLES: the agenda's To Do table is grown from 3
# to 5 columns in C (pilrc caps tables at 3) -- see AgendaExpandToDoColumns.
CFLAGS="-palmos4 -Os -DCATSYNC_MULTISEG -DTODOEXT_SEG_NAME=sec002 -DLOCDB_SEG_NAME=sec002 -DALLOW_ACCESS_TO_INTERNALS_OF_TABLES= -include $LIB/edate_override.h -include $SDK/PalmUtils.h -include Src/db35_seg.h -include $LIB/edate.h $INCS -ISrc -I$LIB"
CC=m68k-palmos-gcc
B=build
mkdir -p "$B"

# Segment map (see db35_seg.h). NOTE: placement is by __attribute__((section)) in
# the SOURCES, not by this list; per-function overrides exist (SEC_RECV pulls the
# beam-receive half of DateTransfer.c into main; DatePref/DateDisplay/
# ApptGetAppointments/DatebookLoad+SavePrefs were pushed to sec002 to make room):
#   main   : Datebook DateGlobals DateDB + beam RECEIVE path (SEC_RECV)
#   sec001 : DateDay
#   sec002 : DateWeek ToDoDB + Preferences/DisplayOptions dialogs + ApptGetAppointments
#   sec003 : DateAgenda DateMonth + DateTransfer SEND/export path
SOURCES="Datebook DateGlobals DatePref DateDisplay DateDB \
         DateDay DateWeek ToDoDB DateAgenda DateMonth DateTransfer AddrBday DateDAV DateLocation DateSpan"

echo ">> compiling sources (section attributes place code into segments)"
for base in $SOURCES; do
  $CC $CFLAGS -c "Src/$base.c" -o "$B/$base.o"
done
# shared apps/lib picker (era-aware SelectDay replacement) -> sec002 via DATELIB_SECTION
# locdb.c (event Location storage, -DLOCDB_SEG_NAME=sec002 above) mirrors todoext.c's segment idiom.
for f in "$LIB/datepick.c" "$LIB/datelib.c" "$LIB/hackcheck.c" "$LIB/era.c" "$LIB/edate.c" "$LIB/catsync.c" "$LIB/todoext.c" "$LIB/enccore.c" "$LIB/aes.c" "$LIB/locdb.c" "$LIB/a35lookup.c"; do
  $CC $CFLAGS -c "$f" -o "$B/$(basename "${f%.c}").o"
done

echo ">> multigen segment glue"
m68k-palmos-multigen -b "$B/Datebook35-sections" Datebook35.def
$CC -c "$B/Datebook35-sections.s" -o "$B/Datebook35-sections.o"

echo ">> linking"
$CC $CFLAGS "$B"/*.o "$B/Datebook35-sections.ld" -L"$GLUE" -lPalmOSGlue -o "$B/Datebook35" 2>&1 | \
  grep -vE 'sections.o|already' || true

echo ">> resources: runtime forms (pilrc)"
rm -rf "$B/res"; mkdir -p "$B/res"
cat forms.rcp > "$B/forms.rcp"                 # Db35: static, hand-maintained (see its own header)
cat "$LIB/datepick.rcp" >> "$B/forms.rcp"      # era-aware full-range picker (form 8000)
cat "$LIB/hackalerts.rcp" >> "$B/forms.rcp"    # "plugin required" alerts (8900/8901)
cat todo35alert.rcp >> "$B/forms.rcp"       # Db35: ToDo35-not-installed alert (8902)
cat aboutdb35.rcp >> "$B/forms.rcp"          # Db35: custom About (8800)
cat location35.rcp >> "$B/forms.rcp"         # Db35: event Location dialog + pickers (2400-2440)
pilrc "$B/forms.rcp" "$B/res" >/dev/null 2>&1
# fixed resources taken once from the original application: strings, alerts,
# category names etc. (resources.rcp) and the runtime bitmaps (bitmaps.rcp).
# Compiled after the forms so that, as before, they win on any shared ID.
pilrc resources.rcp "$B/res" >/dev/null 2>&1
pilrc bitmaps.rcp "$B/res" >/dev/null 2>&1

# Db35: launcher icon = ROM disc + "35" overlay. The launcher reads the standard
# 'tAIB' app-icon resource (1000 large / 1001 small) -- NOT the Constructor
# 'taif'/'taic' editor families that came out of the Mac .rsrc (those are ignored
# at runtime, which is why the stock ROM icon showed). Emit real tAIB here and
# drop the stale taif/taic so nothing competes.
rm -f "$B/res/taif"*.bin "$B/res/taic"*.bin
# Large icon = ROM disc + "35" overlay; small (list-view) icon = the ROM Date Book
# small icon verbatim (13x9 -- a "35" text badge is illegible at this size).
cp icons/icon35_large.bmp "$B/big.bmp"; cp icons/rom_small.bmp "$B/sml.bmp"
( cd "$B"; printf 'ICON "big.bmp"\nSMALLICON "sml.bmp"\n' > appicon.rcp; pilrc appicon.rcp res ) >/dev/null 2>&1

# Brand as Datebook35 (launcher/app name) instead of the original "Date Book".
printf 'DateBk35\0' > "$B/res/tAIN03e8.bin"
printf '1.0\0' > "$B/res/tver0001.bin"                # Db35: version 1.0

echo ">> build-prc"
build-prc -o Datebook35.prc Datebook35.def "$B/Datebook35" "$B"/res/*.bin
echo "DONE: $(ls -la Datebook35.prc | awk '{print $5}') bytes"
