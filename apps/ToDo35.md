# ToDo35 — Era-Aware PalmOS To Do (reference & rebuild guide)

This document captures everything needed to rebuild **ToDo35** from the stock
Palm OS **To Do** sample source, including the era (Y2K35) mechanism, the build /
segmentation model, the UI/resource pipeline, and every non-obvious gotcha found
while porting. A companion doc, `Datebook35.md`, covers the calendar app; the two
share the same foundation (§2–§4), so read those sections in either doc.

Paths are relative to `hackmaster_y2035/`. Original source:
`Sample_Source/Examples/ToDo/` (byte-preserved where possible; changes are marked
`Td35:` in comments).

---

## 1. What ToDo35 is

A drop-in replacement for the ROM **To Do List** that supports **due dates beyond
the 2031 wall** (the 7-bit PalmOS `DateType` year field maxes at 1904+127=2031).
It is part of the **HackMaster Y2K35** two-tier system:

- **X-Y2K35** — a system hack (HackMaster plugin) that re-bases the RTC so "now"
  can be read past 2031, and publishes an offset as a Feature.
- **ToDo35 / Datebook35** — era-aware apps that store **full-range dates** and do
  their own date math, so they are correct for any year 1904–9999.

ToDo35 requires the hack to be **installed and enabled**; on launch it calls
`HcOkOrAlert()` (lib/hackcheck) and shows a "plugin not enabled" alert + exits if
the hack is off. Creator `Td35`, type `appl`; database `ToDo35DB` (type `DATA`,
creator `Td35`) — its **own** DB, never the ROM To Do's, so records with the wide
date format never collide with stock To Do. Each item's **description + note are
encrypted at rest** with the suite-wide **Sec35** passcode key (see §8); locked
items show `******` and editing bounces to Sec35.

---

## 2. The era mechanism (shared with Datebook35)

Goal: let the app store and manipulate **any** year without touching the ~hundreds
of date call sites. Achieved with a **force-included override header** that widens
`DateType` and redirects the ROM date traps to full-range wrappers.

### 2.1 Wide DateType — `lib/edate_override.h` (force-included FIRST)
```c
#define DateType _SysDateType
#include <PalmOS.h>          /* parse the SDK's 7-bit DateType under an alias */
#undef DateType
typedef struct { Int16 year;  /* full year - 1904 (KEEPS the ROM offset!) */
                 UInt8 month; /* 1..12 */
                 UInt8 day;   /* 1..31 */ } DateType;   /* now 4 bytes, was 2 */
#define DatePtr DateType *
/* redirect the ROM date routines (all capped at 2031) to datelib wrappers: */
#define DateToDays        EDateToDays
#define DateSecondsToDate EDateSecondsToDate
#define DateAdjust        EDateAdjust
#define DaysInMonth       EDateDaysInMonth
#define DayOfWeek         EDateDayOfWeek
#define DayOfMonth        EDateDayOfMonth
#undef  DateToInt
#define DateToInt(d)      EDateInt(d)
#define TimSecondsToDateTime ETimSecondsToDateTime   /* "now" = re-based clock */
#undef  maxDays
#define maxDays ((UInt32)2957003UL)   /* days 1904-01-01..9999-12-31 (see §8) */
```
Key design choice: `year` still stores **full-year − 1904**, so every existing
`date.year + firstYear` / `year - firstYear` site compiles unchanged.

### 2.2 Full-range math — `lib/datelib.{c,h}` + `lib/edate.{c,h}`
- `datelib.c` = pure 32/64-bit Hinnant civil-date algorithms (no ROM date traps,
  no 2031 cap). Days-from-epoch, day-of-week, days-in-month, add/adjust.
- `edate.c` = thin `EDate*` wrappers matching the ROM signatures, backed by
  datelib, that the override macros point at. `EDateInt(d)` packs
  `year<<9|month<<5|day` as a **32-bit** monotonic key; returns `0xffff` for the
  "forever/no-date" sentinel (`year == 0xffff`). `EDateSetForever(d)` sets
  `{year=0xffff, month=0, day=0}`.

### 2.3 Two era gotchas that WILL bite
- **`DateToInt` is now a 32-bit key.** Real years ≥ 2032 exceed 16 bits; any
  `Int16`/`UInt16` holding a `DateToInt` result truncates → wrong sort/range →
  far-future records "vanish" or crash ("Record not on day"). Grep for narrow
  vars assigned from `DateToInt` and widen to `Int32`.
- **No-date sentinel is `year == 0xffff`.** Stock code that did
  `*((UInt16*)&dueDate) = toDoNoDueDate` only writes 2 of the 4 wide bytes now →
  garbage month/day. Set all three fields (`year=0xffff; month=0; day=0`). Fixed
  in ToDoTransfer.c (import) and ToDo.c (new-item).

### 2.4 Display — `lib/era.{c,h}`
The ROM string formatters (`DateToDOWDMFormat`, `DateToAscii`,
`DateTemplateToAscii`) still cap at 2031, so ToDo35 formats via `EraDate*` helpers
that substitute the real year into the ROM output. NOTE: `era.c` uses `% 100`
(modulo) → it pulls in libgcc `__modsi3` and therefore **must be in the main
segment** (see §4).

---

## 3. Build & toolchain (shared)

prc-tools **gcc 2.95.3**, strict C89 (no `//`? — actually `//` is tolerated; but no
declarations-after-statements in some spots). `pilrc 3.2.90` for resources.
`m68k-palmos-multigen` for segments. `build-prc` to package. SDK headers at
`palm-os-sdk/sdk-4/include`, flag **`-palmos4`**.

`todo35/build.sh` steps:
1. **CFLAGS** force-include order matters:
   `-include edate_override.h` (widen DateType) **first**,
   `-include td35_seg.h` (segment macros), `-include edate.h` (wrappers) **last**.
   `-DALLOW_ACCESS_TO_INTERNALS_OF_TABLES` exposes `TableType` for the 5-col hack.
2. Compile app sources (`ToDo ToDoDB ToDoTransfer`) + shared lib
   (`datepick datelib hackcheck era edate`).
3. `m68k-palmos-multigen -b build/ToDo35-sections ToDo35.def` → glue `.s` + linker
   `.ld`; compile the `.s`; link everything with the `.ld` + `-lPalmOSGlue`.
   `rm -f build/*.o` first to drop stale objects.
4. Resources: start from the static `forms.rcp`; append the shared picker
   (`datepick.rcp`), hack alerts (`hackalerts.rcp`), custom About (`abouttd35.rcp`);
   run `pilrc`; then `pilrc` `resources.rcp` and `bitmaps.rcp` (see §6.1).
5. Launcher icon + name (see §6.3). `build-prc -o ToDo35.prc ToDo35.def <elf>
   build/res/*.bin`.

`ToDo35.def`:
```
application { "ToDo35" Td35 backup }
multiple code { "sec002" "sec003" }
```

---

## 4. Segmentation — the single most important architectural rule

PalmOS 68K code resources are capped at **32 KB**. ToDo35's code (app + era libs)
exceeds that once real beaming is added, so it is split into **main + sec002** via
multigen (plus **sec003** for the bundled Sec35 crypto, added with encryption — §8).
`Src/td35_seg.h` (force-included) defines the tags:
```c
#define DATELIB_SECTION           /* empty = main: datelib/edate/era live in main */
#define TD35_XFER __attribute__((section("sec002")))  /* foreground-only code    */
#define TD35_RECV                 /* empty = main: beam-RECEIVE path              */
#define SEC003    __attribute__((section("sec003")))  /* enccore/aes + decrypt/seal */
```

Three hard rules decide what may live outside main:

1. **libgcc rule** (`multiseg-libgcc-gotcha`): any function that emits a libgcc
   helper call (`__divsi3`, `__modsi3`, `__divdi3`, …) — i.e. does **non-power-of-2
   division or modulo, or 64-bit math** — must be in **main**. libgcc is linked
   into main and reached by PC-relative `BSR`, which can't span segments →
   otherwise a garbage jump. This pins `datelib`, `edate`, `era` (`% 100`) to main.
   (Division by a power of two compiles to a shift, so it's fine anywhere.)

2. **forward-decl rule** (`multiseg-forward-decl-gotcha`): the compiler must know a
   function's segment at **every call site AND its prototype/forward declaration**.
   A prototype tag that doesn't match the definition → cross-segment call jumps to
   garbage. So each moved function's header/`.h` prototype gets the same tag.

3. **no-globals-sublaunch rule** (`beam-receive-sublaunch-segment` — the beam
   receiver crash): **secondary segments are only loaded when the app is launched
   WITH a globals world.** prc-tools crt0 runs `_GccRelocateData` (which loads +
   relocates sec001/2/…) **only if `launchFlags & sysAppLaunchFlagNewGlobals`**
   (crt0 disassembly: `andiw #4,%d3 / beq / bsr _GccRelocateData`). A beam arriving
   while the app is **not running** is a **no-globals sublaunch**
   (`sysAppLaunchCmdExgReceiveData`; SDK: "you won't have global variables unless
   you happen to be the open application"). So **any code reachable from a
   no-globals sublaunch MUST be in main**. The no-globals sublaunch entry points
   are: **`sysAppLaunchCmdExgReceiveData`, `sysAppLaunchCmdExgAskUser`,
   `sysAppLaunchCmdFind`**. (`GoTo` is fine — it gets globals via
   `StartApplication`.) `SysAppLaunch` cannot be used to re-launch yourself with
   globals — its default is "no globals world" and `_GccLoadCodeAndRelocateData`
   needs A5 — so there is no shortcut; the code simply has to be in main.

### ToDo35 segment layout (result)
- **main**: `ToDo.c` core + PilotMain dispatch + `Search` (Find) +
  `CustomAcceptBeamDialog` (ExgAskUser) + `ToDoReceiveData` itself (`TD35_RECV`,
  the no-globals sublaunch entry point dispatched from `sysAppLaunchCmdExgReceiveData`);
  `ToDoDB.c` (the DB write path the receiver calls: `ToDoNewRecord`,
  `ToDoGetDatabase`, sort); datelib/edate/era.
- **sec002** (`TD35_XFER`, foreground-only): beam **SEND**/export
  (`ToDoSendRecord`, `ToDoSendCategory`, `ToDoExportVCal`, `PutString`,
  `SetDescriptionAndFilename`, TryCatch wrappers) + the **NoteView** cluster + the
  **Details** dialog cluster (moved out of main purely to make room; both are
  foreground/GoTo-only, never in a no-globals sublaunch) + the **vCal/vToDo
  parser** (`ToDoImportVCal`, `ToDoImportVToDo`, `ToDoImportVEvent`,
  `MatchDateToken`, `ToDoSetGoToParams`, `GetChar`, `ToDoMergePlainDB`).

  This looks like a rule-3 violation at first glance (`ToDoReceiveData`, the
  no-globals sublaunch entry point, is main, but the parser it seems to feed is
  sec002) - it isn't, because of a later architecture change
  (`beam-receive-sublaunch-segment`'s own "transient-inbox" note): the vCal
  parser's static tables are A5-relative and corrupt the heap if touched from a
  no-globals context (same trap Address35 hit), so `ToDoReceiveData` does the
  **minimum** during the sublaunch - drains the whole beam into a buffer and
  stashes those RAW, unparsed bytes into a `ToDoDBPlain` "inbox" DB (see its own
  code comment: "Do NOT parse here (sublaunch)"). It never calls into the
  sec002 parser directly. `ToDoMergePlainDB` (sec002) is what actually parses
  the inbox - but it only ever runs on a **full** launch/refresh
  (`ToDo.c`: `StartApplication`, and again on every category switch so a beam
  received while the app is already open shows up without a restart) - both
  contexts have globals and sec002 loaded, so rule 3 is satisfied. Confirmed by
  reading `ToDoReceiveData`'s body directly, not just by tag-grepping.
- **sec003** (`SEC003`, foreground-only): the bundled Sec35 crypto (`lib/enccore.c` +
  `aes.c`, via `AES_SECTION`/`ENCCORE_SECTION`) and `ToDoDB.c`'s decrypt/seal record
  helpers (§8).

Main after the fix ≈ **31,804 B** (< 32,768). Current (2026-10): main 31,696 B (1,072
free), sec002 18,552 B, sec003 6,060 B. If you add code to main and it
overflows (`ld: region coderes is full` / `relocation truncated to fit DISP16`),
move another **division-free, foreground-only** function to sec002 and tag its
prototype.

---

## 5. Data model & DB

`ToDoDB.h`:
```c
typedef struct { DateType dueDate;   /* WIDE: 4 bytes now */
                 UInt8 priority;     /* high bit = complete flag */
                 char  description;  /* inline; note string follows desc's NUL */ } ToDoDBRecord;
#define sizeDBRecord (sizeof(ToDoDBRecord)+1)   /* +1 for the note's NUL */
```
All record packing uses **symbolic offsets** (`&nilP->dueDate`, `sizeof(DateType)`,
`&nilP->description`) so the wider date is handled automatically — do NOT hardcode
byte offsets. The note string immediately follows the description string's NUL;
`GetToDoNotePtr()` = `&rec->description + StrLen(&rec->description) + 1`.

Because the DB is **owned by us**, byte width/alignment can be changed freely — but
keep pack and unpack symbolic and consistent (a partial write is what caused the
no-date-sentinel bug in §2.3).

---

## 6. UI & resources

### 6.1 Resource pipeline

**Resource sources (2026-10-10).** The extracted `res/*.bin` files and the conversion scripts
(`tools/gen_forms.py`, `rsrc2rcp.py`, `extract_rsrc.py`) are no longer in the tree; mentions of them below are
history. The build now uses only text and bitmap sources: `forms.rcp` (forms and menus),
`resources.rcp` (strings, alerts, category names and the remaining fixed resources; entries that
`pilrc` cannot reproduce exactly, such as help strings containing carriage returns, are `HEX`
blocks) and `bitmaps.rcp` + `bitmaps/*.bmp`. The switch was verified by rebuilding: the PRC is
byte-identical to the previous one apart from its date stamps. netpbm is no longer needed.

Original Mac `.rsrc` Constructor resources are converted to runtime PalmOS
resources by `tools/extract_rsrc.py` (AppleDouble/classic fork → `res/<type><id>.bin`)
and `tools/gen_forms.py` + `tools/rsrc2rcp.py` (Constructor form objects →
`pilrc` `.rcp` → inline runtime `tFRM`). `pilrc` then emits real forms; the build
bundles the non-form resources verbatim.

### 6.2 The 5-column list table (pilrc caps tables at 3 cols)
`pilrc 3.2.90` **hard-crashes** (glibc heap abort) on a `TABLE` with >3 columns,
and stock To Do's list needs 5 (checkbox / priority / description / **due date** /
**category**). Solution — **runtime C, no binary patching**
(`ListExpandColumns`/`ListRestoreColumns` in ToDo.c, needs
`-DALLOW_ACCESS_TO_INTERNALS_OF_TABLES`): on form open, swap the table's
`columnAttrs`/`items` pointers to larger STATIC arrays and set `numColumns=5`; on
`frmClose`, restore. The agenda in Datebook35 does the identical trick
(`AgendaExpandToDoColumns`). `gen_forms.py` grows the table resource to 5 columns
in C-friendly shape and marks popups NONUSABLE.

- **Due-date column**: read the 4-byte wide `dueDate` from the record via
  `TblGetRowID`, not from a 2-byte table int (a 4-byte date crammed into an int
  showed garbage like "221/1"). Strip the year with `StripYearForColumn` (handles
  4-digit years). `ListViewDrawDueDate`.
- **Category column**: width fits `FntCharsWidth("Business"/"Personal")`; longer
  names get a `chrEllipsis` via `WinGlueDrawTruncChars`; the popup shows the full
  label. Right-anchor the trigger (`ListRightAlignCategory`, x≈159−extent) after
  every `CategorySetTriggerLabel`, and RIGHTANCHOR the trigger in `gen_forms.py`.

### 6.3 Launcher icon & name (`launcher-icon-tAIB-gotcha`)
The launcher reads the **standard `tAIB`** family (1000 large / 1001 small), NOT
the Constructor `taif`/`taic` families from the Mac `.rsrc` (those are
editor-only, ignored at runtime). build.sh deletes `taif*/taic*` and emits real
`tAIB` via pilrc `ICON`/`SMALLICON`: large = ROM disc + bold "35" overlay; small =
the ROM To Do icon verbatim (a "35" badge is illegible at 10×9). App **name** =
`tAIN` (id 0x03e8) = "ToDo35"; version `tver` 0001 = "1.0".

### 6.4 Note editor overlock fix (`note-overlock-column-hack`)
Typing the first char into a note used to crash `Field.c ... Field object
overlocked`. Cause: the list's inline field editor held the record **locked**, and
the era 5-col `ListRestoreColumns` in `frmClose` swapped the column arrays out
**before** the ROM's default teardown released that editor (and
`frmSaveEvent` isn't sent during `FrmGotoForm`). Fix: call `TblReleaseFocus(table)`
**before** `ListRestoreColumns` in the ListView `frmClose`.

### 6.5 Menus / About / picker
- Record menu has a `MENUITEM SEPARATOR` between "Purge…" and "Beam Item" (pilrc
  needs `SEPARATOR`, not `"-"`). Options menu: Font / Phone Lookup / Security /
  About.
- Custom About form (`abouttd35.rcp`, id 8800): "Extended ToDo v3.5 (2026) / …
  Support dates beyond 2031."
- All date pickers call the shared era-aware picker (`lib/datepick`, form 8000),
  unclamped to 1904–9999.

---

## 7. Beaming (send + receive)

vCal (`vToDo`) over the Exchange Manager. Real implementation in `ToDoTransfer.c`
(the old stub `todo35_beam.c` is deleted). Include is `<IMCUtils.h>` (SDK ships it
capitalised; case matters on Linux — the original `<ImcUtils.h>` silently excluded
beaming).

- **Wire format** is TEXT with a **4-digit year**, so era dates transmit fine
  regardless of the on-disk wide `DateType`. Export writes `firstYear + year`;
  import's `MatchDateToken` clamp was widened from `lastYear`(2031) to **9999**.
- **SEND** (`ToDoSendRecord`/`ToDoSendCategory`) runs foreground → lives in sec002.
- **RECEIVE** (`ToDoReceiveData` → `ToDoImportVCal` → `ToDoImportVToDo`) runs as a
  **no-globals sublaunch** → MUST live in **main** (§4 rule 3). This was the device
  crash: importer was in sec002, unloaded during the receive sublaunch → Fatal
  Exception. Fixed by `TD35_RECV`. VERIFIED on device: item received, no crash,
  due date > 2031 preserved.
- Exchange Manager fails gracefully if the receiver lacks a vToDo handler.
- **Encryption interaction (§8):** SEND reads a **decrypted** copy of the description/
  note (list draw path); RECEIVE stores the item raw/plaintext and it's sealed by
  `ToDoSealAllPlaintext` on the next launch — the no-globals receiver never calls
  sec003 crypto.

---

## 8. At-rest encryption (Sec35 shared key)

ToDo35 encrypts each item's **description + note** at rest with the suite-wide
**Sec35** passcode key (copy-edit model: storage is **always ciphertext** when a
passcode is set; edits happen on an in-RAM decrypted copy, re-sealed on write).
Due date / priority / completion / category stay plaintext (they drive sort, filter,
and the agenda). Same authority + key as Datebook35 §9 / Address35 §8 — see
[[todo35-encryption-plan]], [[sec35-encryption-design]].

**8.1 Record format.** AES-CTR can emit 0x00 (breaks inline NUL-terminated strings),
so an encrypted record is length-prefixed; the clear prefix is byte-identical:
```
plaintext:  [dueDate:4][priority:1] description\0 note\0
encrypted:  [dueDate:4][priority|0x40] [descLen:2 BE][noteLen:2 BE][encDesc][encNote]
```
Marker = **priority bit 0x40** (`encryptedFlag`, ToDoDB.h). **`priorityOnly = 0x3f`**
masks BOTH the complete (0x80) and encrypted (0x40) bits, so the *displayed* priority
is clean — miss the 0x40 mask and priorities read 64+n (this was Datebook's agenda
"65" bug). Nonce = (uniqueID, 0) for desc, (uniqueID, 1) for note. Any code that
rewrites the priority byte must **preserve `completeFlag|encryptedFlag`**.

**8.2 Copy-edit (no single chokepoint).** Unlike Datebook's `ApptGetRecord`, ToDo35
has ~27 direct `->description` reads, so decryption is at the draw/edit sites: the
list draws **per-row decrypted display copies** `gTdRowH[32]` + one active **edit
copy** `gTdEditH` (`ListViewGetDescription`), masking `******` when locked;
`ListViewSaveDescription` re-encrypts. `ToDoCopyDescPlain`/`TdCopyField` decrypt one
field into a fresh handle. Lock is detected **per record** from
`(priority & encryptedFlag) && !EncKeyPresent()` (never from the passcode feature — a
soft reset wipes it while records stay encrypted).

**8.3 Locked UX + seal.** `TdLockedBounce` (`!EncKeyPresent() && ToDoDbHasEncrypted`
→ `SwitchToSec35`) blocks every edit action and hands off to Sec35 to unlock;
`ToDoLaunchGate` on open. New/edited items sealed via `ToDoEncryptOne`;
`ToDoSealAllPlaintext` on close; beam-receive stores raw, sealed on next launch (so
the no-globals receiver stays crypto-free). Category-switch re-scans the DB so newly
beamed items appear.

**8.4 Sec35 wipe + rekey** (`apps/sec35/sec35.c`): `WipeEncDb('DATA','Td35',
TD_PRI_OFF=4, TD_ENC=0x40)` deletes encrypted items on Forget Password;
`RecryptToDo35` re-keys them on password change/set/delete (CHANGE/SET/CLEAR modes).
The extras DB (time/repeat, §10) is keyed by uniqueID and is **not** encrypted (not
sensitive text).

---

## 9. Consolidated gotcha checklist (for a rebuild)

1. Force-include order: `edate_override.h`, then `td35_seg.h`, then `edate.h`.
2. `DateToInt`/`EDateInt` is 32-bit — widen any narrow date-key var to `Int32`.
3. No-date sentinel writes all 3 wide fields (`year=0xffff,month=0,day=0`).
4. `maxDays` override to 2957003 (else no-end-date logic / repeats cap at 2031).
5. Never binary-patch: 5-col table is runtime C via `ALLOW_ACCESS_TO_INTERNALS...`.
6. Launcher uses `tAIB` (1000/1001) + `tAIN` (0x3e8), not Constructor `taif/taic`.
7. pilrc: `MENUITEM SEPARATOR` (not "-"); tables ≤3 cols (grow in C).
8. Segment rules: libgcc→main; prototype tag must match definition; **anything a
   no-globals sublaunch (ExgReceiveData/ExgAskUser/Find) can reach → main**.
9. Note-editor: release the table field editor before any `frmClose` column swap.
10. Own DB (`ToDo35DB`) — never share the ROM To Do DB (format differs).
11. **Encryption (§8):** `encryptedFlag = priority bit 0x40`; **`priorityOnly = 0x3f`**
    (mask complete+encrypted, else priority shows 64+n); copy-edit via per-row
    `gTdRowH[32]` + `gTdEditH`; `TdLockedBounce` → Sec35; seal on close + on launch;
    Sec35 `WipeEncDb('Td35',4,0x40)` + `RecryptToDo35`.

---

## 10. Time, repeat & category cycling (Td35 extensions)

These add per-ToDo **start/end time** and **recurrence**, plus **category cycling**.
Design decision: time/repeat live in a SEPARATE "extras" DB, keyed by each ToDo's
Data-Manager unique ID — the ROM `ToDoDBRecord` is byte-for-byte unchanged, so
in-place note editing, sort order and HotSync are unaffected (see
[[todo-time-repeat-extras-db]] for why in-record storage was rejected).

- **`lib/todoext.{c,h}`** (shared with Datebook35) — DB `ToDo35ExtDB`, creator `Td35`,
  type `Tdx3`, backup bit set. Record = `{UInt32 uid; TdxExt ext}` where
  `TdxExt = {flags(tdxTimed|tdxHasRepeat), startTime, endTime, TdxRepeat}`. API
  `TdxGetDatabase / TdxGet / TdxSet(flags==0 deletes) / TdxDelete`. App opens it once
  in StartApplication (`ExtDB`), closes in StopApplication. **GOTCHA:** the "readers
  don't create" guard must test `!(mode & dmModeWrite)` — `dmModeReadWrite` INCLUDES
  the `dmModeReadOnly` bit. Segment: ToDo35 forces sec002 via
  `-DTODOEXT_SEG_NAME=sec002` (main is full); Datebook35 also uses sec002 (it moved there
from sec001 when sec001 overflowed — `Datebook35.md` §4).

- **Details dialog** is hand-authored (`details1200.rcp`, form 1200 — the Constructor
  1200 is dropped from `gen_forms.py`) so it can carry a **"Time:"** selector (→ ROM
  `SelectTime`, title tSTR0066) and a **"Repeat:"** selector (→ FrmPopupForm the Repeat
  dialog). `DetailsInit` TdxGets, `DetailsApply` TdxSets, keyed by uid.
  **GOTCHA (crash):** popup-trigger labels are written IN PLACE
  (`CtlGetLabel(ctl)`→`CategoryGetName`/date), so the resource must reserve a big
  enough label buffer — give category/due-date triggers ~40-char placeholder text,
  not `""`, or a long category name overflows and corrupts adjacent controls.

- **Change Repeat dialog** (`details1250.rcp`, form 1250, all handlers TD35_XFER):
  geometry matched to Datebook35's form 1400 (`2 2 156 156`, needs the 2px inset for
  the modal border). Type group (None/Day/Week/Month/Year), editable "Every N" field
  (`FldSetTextHandle`; form frees it), weekly day-of-week (7 pushbuttons, onMask
  bit0=Sun), monthly by-day/by-date, End-on popup (No End Date / Choose Date →
  `DatePickSelectDay`), and a read-only description field that live-composes the same
  sentences Datebook uses ("Every day", "Every week on Mon and Wed", "The last Friday
  of every month", "July 24th every year", …) — composed in C (`RepeatDlgComposeDesc`
  + `RepeatOrdinal`), anchored to the due date, not via Datebook's template resources.

- **Roll-forward on complete** (`TodoRollForward`/`TodoAdvanceRepeat`, sec002): marking
  a repeating todo complete instead advances its due date to the next occurrence and
  keeps it open, until the next occurrence would pass the end date. Daily/weekly use
  `EDateAdjust`; weekly steps to the next SELECTED weekday (onMask), N weeks on when a
  week is exhausted; monthly-by-day lands on the Nth/"last" weekday via `NthWeekday`;
  monthly-by-date / yearly clamp the day. Undated repeats just complete.

- **List icons** (`ListViewDrawDescIcons`, narrowTextTableItem + custom draw, sec002):
  right-aligned in the description cell, order clock → note → repeat. 7×7 clock (9:00)
  for timed, `symbolNote`/`symbolRepeat` glyphs; only drawn when set.

- **Category cycling (#7)** — `ListViewNextCategory` now `CategoryGetNext`s through ALL
  named categories (removed the skip-to-next-non-empty loop); driven by the existing
  hard-app-button keyDown handler. See [[category-cycle-hardbutton]].

- **Duplicate Item** (Record menu, `ListViewDuplicateToDo`, `DuplicateCmd=106`) — new
  feature, no ROM equivalent. Menu item is inserted by `tools/gen_forms.py`'s
  `add_duplicate_item()` patch (same idiom as `add_record_separator`), placed FIRST in
  the Record menu ahead of "Delete Item...". Reads the selected item via the same
  decrypted `ToDoGetInlineCopy` chokepoint everything else uses (so a duplicate of an
  encrypted item is created from plaintext, then re-encrypted under its own new uid —
  same reasoning as Address35's `A35DuplicateRecord`), builds a `ToDoItemType` (same
  dueDate/desc/note, priority masked with `priorityOnly` so the duplicate is never
  pre-completed), and calls the existing `ToDoNewRecord` in the same category.

  **If the item has a repeat** (`ExtDB` + `TdxGet(uid).flags & tdxHasRepeat`), a custom
  confirm dialog (`DuplicateRepeatDlg`, id 1260, hand-authored in `abouttd35.rcp`
  alongside the Sec35 alert; `DuplicatePromptRepeat` in ToDo.c — NOT a native ALERT,
  which auto-sizes buttons from label text with no width override) asks "This Only" /
  "All" / "Cancel", bold message text word-wrapped via `FldWordWrap` (not a resource
  LABEL, which doesn't wrap). Full app width (`AT (2 66 156 92)`, matching
  Details/RepeatDialog/About), bottom-aligned (bottom edge y=158, same as every other
  dialog), buttons evenly filling the row (5px from each border, 5px between buttons:
  `(5 74 45 12)` / `(55 74 45 12)` / `(105 74 46 12)`). Runs its own synchronous
  `EvtGetEvent`/`FrmDispatchEvent` loop (Address35 EditDialogs.c / Sec35 countdown-
  dialog idiom) rather than this app's usual `FrmPopupForm`+`FrmSetEventHandler` async
  pattern — simpler since there's no cross-turn state to stash. Both
  `DuplicatePromptRepeat` and `ListViewDuplicateToDo` are `TD35_XFER` (sec002) — main
  was already full.

  "This Only" duplicates a one-off task due on the same date (no repeat, keeps any
  time); "All" makes the duplicate repeat independently of the original from then on.
  ToDo35 has no per-occurrence materialization (unlike Datebook — a repeating ToDo is
  one record whose `dueDate` rolls forward on completion), so "the currently selected
  date" is simply the record's current `dueDate` — no extra date-picker UI needed.

  **Extras are rebuilt field-by-field, never a verbatim struct copy** — each field
  (`startTime`/`endTime`, `repeat`) is copied into the new `TdxExt` ONLY when its own
  flag bit (`tdxTimed`/`tdxHasRepeat`) confirms the SOURCE considers it valid. This
  fixed a real bug found on real hardware: `DetailsInit` zero-fills a brand-new extras
  block (`MemSet(&gDetailsExt, ..., 0)`) rather than sentinel-filling it, so an item
  that only ever had a repeat set (never a time) can have `startTime={0,0}` in storage
  instead of the "no time" sentinel (`hours=tdxNoTime`) — every *display* path gates on
  the flag so the original never shows it, but a naive verbatim copy carried the stale
  bytes into the duplicate (a "12am to 12am" phantom time). See
  [[todo35-duplicate-item]] for the full investigation.

---

## 11. File map

```
apps/todo35/
  build.sh              multi-segment build (main + sec002 + sec003)
  ToDo35.def            application + `multiple code { "sec002" "sec003" }`
  Src/td35_seg.h        DATELIB_SECTION(main) / TD35_XFER(sec002) / TD35_RECV(main) /
                        SEC003(crypto) / A35LK_SECTION(sec002)
  Src/ToDo.c            app core, list view, Details+NoteView(sec002), PilotMain,
                        Search, CustomAcceptBeamDialog, 5-col hack, hack gate
  Src/ToDoDB.{c,h}      DB pack/unpack (symbolic offsets), wide ToDoDBRecord
  Src/ToDoTransfer.c    beam SEND(sec002=TD35_XFER) + RECEIVE(main=TD35_RECV)
  Src/ToDo.h ToDoRsc.h  prototypes (transfer protos carry TD35_XFER/TD35_RECV)
  details1200.rcp       hand-authored Details dialog (Time:/Repeat: rows)
  details1250.rcp       Change Repeat dialog (full parity, form 1250)
  forms.rcp             static, hand-maintained forms + menus (see §6.1)
  resources.rcp         strings (incl. the "Set Time" dialog title, ID 102), alerts, categories
  bitmaps.rcp bitmaps/  runtime bitmaps
  tools/                gen_bulk_todo.py (test data generator)
  abouttd35.rcp icons/  custom About (8800), launcher icons
apps/lib/  (shared)     edate_override.h, edate.{c,h}, datelib.{c,h}, era.{c,h},
                        datepick.{c,h,rcp}, hackcheck.{c,h}, hackalerts.rcp, y2k35.h,
                        todoext.{c,h}  (per-ToDo time+repeat extras DB),
                        enccore.{c,h}, aes.{c,h}  (Sec35 at-rest encryption, §8; sec003),
                        a35lookup.{c,h}  (Phone Lookup via Address35, sec002, §12)
```

To rebuild from stock To Do: start from `Sample_Source/Examples/ToDo/`, apply the
era override (§2), widen `ToDoDBRecord.dueDate`, keep packing symbolic, add the
5-col runtime table (§6.2) + era column drawing, wire the era picker + hack gate,
implement beaming with the **receive path in main** (§4/§7), and fix the sentinel
+ note-editor + launcher-icon issues above. Cross-check against `Datebook35.md`
for the shared era/segmentation foundation.

## 12. Phone Lookup → Address35 (2026-10)

List view and Note view **Phone Lookup** call `A35LookupPhone` (`apps/lib/a35lookup.c`) instead
of the ROM `PhoneNumberLookup` (hard-wired to the built-in Address Book). It shows Address35's
small "Phone Number Lookup" picker (contact, then that contact's phones / emails / social
handles) and pastes "Name - value"; full design and gotchas in
`Address35.md` §10. The helper lives in sec002 here (`A35LK_SECTION` in `Src/td35_seg.h`);
main grew 8 bytes.
