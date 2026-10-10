# Datebook35 — Era-Aware PalmOS Date Book (reference & rebuild guide)

Everything needed to rebuild **Datebook35** from the stock Palm OS **Date Book**
sample source: the era (Y2K35) mechanism, the multi-segment build model, the
UI/resource pipeline (made ROM-pixel-perfect), the agenda↔ToDo35 integration, and
every non-obvious gotcha. The era foundation and segmentation rules (§2–§4) are
**shared with ToDo35** — `ToDo35.md` documents them from the To Do angle; this doc
is self-contained but cross-references it.

Paths are relative to `hackmaster_y2035/`. Original source:
`Sample_Source/Examples/Datebook/` (byte-preserved where possible; changes marked
`Db35:`).

---

## 1. What Datebook35 is

A drop-in replacement for the ROM **Date Book** that is **fully era-independent**:
one calendar shows **any year 1904–9999**, pick any date, no era switching, dates
beyond the 2031 `DateType` wall work everywhere (views, repeats, details, search,
beam). Part of **HackMaster Y2K35**:

- **X-Y2K35** hack re-bases the RTC so "now" reads past 2031 + publishes an offset
  Feature (`'Y235'`,0).
- Datebook35 stores **full-range dates** and does its own date math; the hack is
  only needed to learn *NOW* (`Hc_CurrentRealDate`); all other logic is
  hack-independent.

Requires the hack installed + enabled (launch-time `HcOkOrAlert`, else alert +
exit). Creator `Db35`, database `DatebookDB`-equivalent owned by us. **Alarms are
removed** (see §7) — the era cannot support absolute-second alarms past the 2040
`UInt32` wall. Appointment **description + note are encrypted at rest** with the
suite-wide **Sec35** passcode key (see §9); locked appts show `******` and tapping
one bounces to Sec35. Events can **span multiple days** (e.g. an overnight
flight) — see §16. Events synced by **CalDAV Sync** show read-only alongside native
ones (§17-18), and **Phone Lookup** searches Address35's contacts (`Address35.md` §10).

---

## 2. The era mechanism (shared with ToDo35)

A **force-included override** widens `DateType` and redirects the ROM date traps
to full-range wrappers, so the ~300 date sites compile unchanged.

### 2.1 `lib/edate_override.h` (force-included FIRST)
Renames the SDK's 7-bit `DateType` to `_SysDateType`, then defines a **wide** one:
```c
typedef struct { Int16 year;  /* full year - 1904 (keeps the ROM offset) */
                 UInt8 month; UInt8 day; } DateType;   /* 4 bytes, was 2 */
```
and macro-redirects `DateToDays/DateSecondsToDate/DateAdjust/DaysInMonth/
DayOfWeek/DayOfMonth/DateToInt(→EDateInt)/TimSecondsToDateTime` to `EDate*`
wrappers, plus `#undef maxDays / #define maxDays 2957003UL` (days to 9999-12-31).

### 2.2 Full-range math — `lib/datelib.{c,h}` + `lib/edate.{c,h}`
- `datelib.c`: pure Hinnant civil-date math (32/64-bit), no ROM traps, no 2031 cap.
- `edate.c`: `EDate*` wrappers backed by datelib. `EDateInt(d)` = 32-bit key
  `year<<9|month<<5|day`, returns `0xffff` when `year==0xffff` (the "forever"
  sentinel). `EDateSetForever(d)` = `{0xffff,0,0}`.

### 2.3 Storage is REAL full-range years on disk
Datebook35 is the "aware tier": `ApptDateTimeType.date`, `RepeatInfoType.repeatEndDate`,
and the exceptions array all hold the **wide** `DateType` storing real years.
`ApptPack`/`ApptUnpack`/`ApptPackedSize` (DateDB.c) use **symbolic** `sizeof`/field
offsets, so the wider date is packed correctly — never hardcode offsets. Record
format is wider than stock (4-byte dates) → era-independent.

### 2.4 Era gotchas that WILL bite
- **`DateToInt` is a 32-bit key** — real years ≥2032 exceed 16 bits. Any
  `Int16`/`UInt16` var holding a `DateToInt` result truncates → far-future records
  vanish / "Record not on day" crash. Widen to `Int32` (fixed in `DateCompare`,
  `ApptGetAppointments` start/end keys).
- **Forever sentinel in `DateCompare`** (DateDB.c): a no-end-date repeat stores
  `repeatEndDate.year = 0xffff`. `DateCompare` must special-case `year==0xffff` as
  the **latest** date (else its small `DateToInt` key ranks it *earliest* and the
  repeat looks ended once "today" passes 2031 → agenda tap crashes "No displayable
  occurrences").
- **`maxDays` cap**: SDK `maxDays` derives from `numberOfYears=128` (Dec 31 2031)
  and caps `NextRepeat`'s occurrence search; override to 2957003 (§2.1).

### 2.5 Display — `lib/era.{c,h}`
ROM formatters cap at 2031, so titles/details/repeat/search strings are formatted
via `EraDate*` (substitute the real year; day view uses a 2-digit year to avoid
arrow clip, week/month 4-digit). `era.c` uses `% 100` → libgcc → **must be in
main** (§4).

### 2.6 Era-independence sweep (remove ALL 2031 clamps)
Every `lastYear`(2031)/`numberOfYears`(128) navigation/repeat bound was changed to
9999: `DateMonth.c` month forward-arrow; `DateWeek.c` week-number boundary +
end-of-range day-skip; `DateDB.c` monthly & yearly repeat occurrence caps (three
`year >= numberOfYears` → `year+firstYear > 9999`); `DateTransfer.c` beam-import
year clamp. Week/day forward-nav was never clamped (uses `DateAdjust`). Month
backward floors at `firstYear` (Jan 1904). **Still era-dependent by design**: only
"today" (hack clock) and alarms (removed).

---

## 3. Build & toolchain (shared)

prc-tools **gcc 2.95.3** (C89), `pilrc 3.2.90`, `m68k-palmos-multigen`, `build-prc`,
SDK at `palm-os-sdk/sdk-4/include`, flag `-palmos4`.

`datebook35/build.sh`:
1. **CFLAGS force-include order**: `edate_override.h` first (widen DateType),
   `db35_seg.h` (segment macros), `edate.h` last (wrappers). Plus
   `-DALLOW_ACCESS_TO_INTERNALS_OF_TABLES` for the agenda's 5-col ToDo table, and
   **`-DTODOEXT_SEG_NAME=sec002`** (relocates `todoext` sec001→sec002 so the
   encryption-swollen sec001 fits 32 KB — §9.1).
2. Compile `SOURCES` (all `Date*.c`, `ToDoDB.c`) + shared lib (`datepick datelib
   hackcheck era edate` **+ `enccore aes` for at-rest encryption, `todoext` for the
   ToDo extras DB**). **Segment placement is by `__attribute__((section))`
   tags in the sources/headers, not by file** — a source file's functions carry
   `SECnnn` (or the empty `SEC_RECV`/`DATELIB_SECTION` = main).
3. `m68k-palmos-multigen -b build/Datebook35-sections Datebook35.def` → glue+`.ld`;
   link with the `.ld` + `-lPalmOSGlue`.
4. Resources (see §6). `build-prc -o Datebook35.prc Datebook35.def <elf> build/res/*.bin`.

`Datebook35.def`:
```
application { "Datebook35" Db35 backup }
multiple code { "sec001" "sec002" "sec003" }
```

---

## 4. Segmentation — the critical rules (shared, see ToDo35.md §4)

Stock Date Book is ~88 KB of code — far over the 32 KB code-resource limit — so it
**must** be split. `Src/db35_seg.h` (force-included) defines the tags:
```c
#define SEC001 __attribute__((section("sec001")))
#define SEC002 __attribute__((section("sec002")))
#define SEC003 __attribute__((section("sec003")))
#define DATELIB_SECTION           /* empty = main (datelib/edate/era libgcc) */
#define SEC_RECV                  /* empty = main: beam-RECEIVE path         */
```
Three hard rules (full detail in ToDo35.md §4):

1. **libgcc → main**: non-power-of-2 division/modulo or 64-bit math pulls in
   `__divsi3`/`__modsi3`/`__divdi3` (reached by PC-relative BSR, can't span
   segments). Pins `datelib`, `edate`, `era`, and `NextRepeat`/`datepick`
   (they use `% freq` etc.) to main.
2. **forward-decl → tag must match**: a function's segment must be known at every
   call site **and** its prototype; a mismatched header tag → cross-segment jump to
   garbage (the original "New-Event crash").
3. **no-globals sublaunch → main**: secondary segments are only loaded when the app
   is launched WITH globals (crt0 runs `_GccRelocateData` only if
   `sysAppLaunchFlagNewGlobals`). Code reachable from a **no-globals sublaunch**
   (`sysAppLaunchCmdExgReceiveData`, `sysAppLaunchCmdExgAskUser`,
   `sysAppLaunchCmdFind`) **MUST be in main**. This was the device beam-receiver
   crash.

### Datebook35 segment layout (result)
- **main**: `Datebook.c` (PilotMain dispatch, `Search`/Find, `StartApplication`,
  `EventLoop`), `DateGlobals.c`, `DateDB.c` (the DB engine + the write path the
  receiver calls: `ApptNewRecord`/`ApptPack`/`DateCompare`/`ApptFindSortPosition`;
  also `NextRepeat` which has modulo), datelib/edate/era, **and the beam
  RECEIVE/import half of `DateTransfer.c`** (`SEC_RECV`: `DateReceiveData`,
  `DateImportVCal`, `DateImportVEvent`, `DateImportVToDo`, `DateImportRepeatingRule`,
  `MatchDateTimeToken`, `MatchWeekDayToken`, `MatchDayPositionToken`, `GetToken`,
  `TranslateAlarm`, `DateSetGoToParams`, `GetChar`).
- **sec001**: `DateDay.c` (day view + the NoteView editor).
- **sec002**: `DateWeek.c`, `ToDoDB.c` (shared To Do DB reader for the agenda),
  **plus** the foreground-only **Preferences** dialog (`DatePref.c`), **Display
  Options** dialog (`DateDisplay.c`), `ApptGetAppointments` (view-only DB reader),
  and `DatebookLoadPrefs`/`DatebookSavePrefs` — moved here to make room in main
  after the receive path moved in.
- **sec003**: `DateAgenda.c`, `DateMonth.c`, the beam SEND/export half of
  `DateTransfer.c` (`DateSendRecord`, `DateSendCategory`, `DateExportVCal`,
  `ApptGetAlarmTimeVCalForm`, `PutString`, `SetDescriptionAndFilename`, TryCatch
  wrappers), **and the encryption core** (`enccore.c` + `aes.c` via
  `AES_SECTION = ENCCORE_SECTION = SEC003`) — foreground-only. **Exception:** the AES
  const tables + the tiny main-side crypto (`ApptFlagsOf`, `ApptGetRecord`'s
  decrypt branch) are pinned to **main** (A5-relative, §9.1).

Adding the encryption copy-edit code (§9) consumed the old ~236 B headroom, so
`ApptAppInfoInit`+`ApptAddException` moved to sec002 and **`todoext` moved
sec001→sec002** (`-DTODOEXT_SEG_NAME=sec002`) — the swollen sec001 had overflowed
32 KB (`DISP16 TdxGet` truncation). If you add to main and overflow, move another
**division-free, foreground-only** function to sec001/2/3 and tag its prototype.
Foreground-only means: reached only via normal launch or `GoTo` (both have
globals), never Find/ExgReceive/ExgAskUser.

**Current state (2026-10, after CalDAV §17-18 and Phone Lookup) — the three
secondary segments are full; only main has room:**
| segment | used | cap | free |
|---|---|---|---|
| main (`.text`) | 31,348 | 32,768 | 1,420 |
| sec001 | 32,676 | 32,768 | **92** |
| sec002 | 32,736 | 32,768 | **32** |
| sec003 | 32,760 | 32,768 | **8** |

(After the multi-day span feature, §16, it was main 4,328 / sec001 4 / sec002 2,452 /
sec003 3,272 free.) `sec001` (`DateDay.c`) has effectively no headroom. Two functions
were relocated sec001→sec002 to make room for §16's work — the established
"move one more thing out" playbook, same tool each time:
`m68k-palmos-objdump -h build/Datebook35 | grep sec001` to check free bytes,
then find something in `DateDay.c` whose only caller(s) are already outside
sec001 and just flip its `SEC001`→`SEC002` tag (**must** update the
definition's tag — no separate forward-decl existed for either of these two,
since both are used only after their own definition in the file):
- `SubstituteStr` (only caller: `RepeatDrawDescription`, already `SEC002`).
- `SelectFont` (both callers were in sec001, but a sec001→sec002 call is
  fine — cross-segment calls always work, it's *same*-segment DISP16
  overflow or a missing/mismatched tag that breaks).

Any further `DateDay.c` addition needs a relocation **found first**, not
assumed — check the actual overflow amount, don't guess which function is
"big enough."

---

## 5. Views & data model

- **Day / Week / Month / Agenda** views (`DateDay/Week/Month/Agenda.c`). All titles,
  Details, repeat, and search sites format dates through `EraDate*`.
- `DateDB.c` is the appointment engine: `ApptGetRecord`/`ApptGetAppointments`
  (unpack), `ApptNewRecord`/`ApptChangeRecord`/`ApptPack` (pack), `NextRepeat`/
  `CountTotalRepeats` (repeat expansion), `DateCompare`/`ApptFindSortPosition`
  (sorted insert). Packed record = `ApptDateTimeType when; ApptDBRecordFlags flags;`
  then optional alarm/repeat/exceptions/description/note in flag order — all sized
  with `sizeof` so the wide date fits.
- **Date input** everywhere uses the shared era-aware picker (`lib/datepick`, form
  8000), unclamped 1904–9999. Month/Week/Agenda "Go to" all route through it
  (`DatePickSelectDay`), NOT the ROM `SelectDay` (which caps at 2031).

---

## 6. UI & resources (made ROM-pixel-perfect)

### 6.1 Pipeline
**`forms.rcp` is now static and hand-maintained** (per user request, once the
bulk conversion/layout work settled down) -- `pilrc` compiles it straight to
inline runtime `tFRM`, same as every other hand-authored `.rcp` in this
directory (`location35.rcp`/`todo35alert.rcp`/`aboutdb35.rcp`). Make layout
adjustments (pixel alignment, button repositioning, etc.) directly in
`forms.rcp`.

**Resource sources (2026-10-10).** The extracted `res/*.bin` files and the conversion scripts
(`tools/gen_forms.py`, `rsrc2rcp.py`) are no longer in the tree; mentions of them below are
history. The build now uses only text and bitmap sources: `forms.rcp` (forms and menus),
`resources.rcp` (strings, alerts, category names and the remaining fixed resources; entries that
`pilrc` cannot reproduce exactly, such as help strings containing carriage returns, are `HEX`
blocks) and `bitmaps.rcp` + `bitmaps/*.bmp`. The switch was verified by rebuilding: the PRC is
byte-identical to the previous one apart from its date stamps. netpbm is no longer needed.

History: `tools/gen_forms.py` + `tools/rsrc2rcp.py` originally converted the
ROM DateBook's Constructor form resources to `.rcp` at BUILD time, applying a
set of mechanical transforms (`SKIP`/`REMAP`/`FORMBOX`/`NONUSABLE`/`RELABEL`
dicts, plus insert-a-new-row text patches for the Location/Title fields --
see `gen_forms.py`'s own comments for the reasoning behind each one).
`build.sh` no longer invokes it; `forms.rcp` is that script's final output,
frozen as of the Title-field feature (§15) and now the base for all further
manual edits. `gen_forms.py` is left in `tools/` purely as a historical
reference for how each object's geometry was originally derived -- not
something to re-run (re-running it would DISCARD any hand-edits already made
to `forms.rcp`). Non-form resources are bundled verbatim; build.sh appends
the shared picker (`datepick.rcp`), hack alerts (`hackalerts.rcp`), the
ToDo35-not-installed alert (`todo35alert.rcp`, Talt 8902), and the custom
About (`aboutdb35.rcp`, 8800) after `forms.rcp`.

### 6.2 Dialog matching (`datebook35-dialog-matching`)
Display Options / Delete / Purge / Agenda dialogs were made ROM-pixel-perfect;
converter fixes: button frame, checkbox labels, popup sizing, popup-list hiding.
**Deliberate deviation** (`datebook35-nobold-checkbox-pref`): Display Options
checkbox labels are **non-bold** (not the ROM's bold).

### 6.3 Popup lists need NONUSABLE
Popup `LIST` resources must carry `NONUSABLE` in `forms.rcp` or they draw a
stray inline frame line (ListView 1011/1012, Details 1211/1215, Options 1405).

### 6.4 Launcher icon & name (`launcher-icon-tAIB-gotcha`)
Launcher reads standard **`tAIB`** (1000 large / 1001 small), NOT Constructor
`taif/taic`. build.sh drops `taif*/taic*`, emits real `tAIB` (large = ROM disc +
bold "35"; small = ROM Date Book icon verbatim). Name **`tAIN`** 0x03e8 =
"DateBk35"; version 1.0.

### 6.5 Date picker (`datebook35-date-picker`)
All pickers call the shared era picker directly for the exact original look; a
custom picker path exists but is dormant.

---

## 7. Alarms removed (`datebook35-alarm-removed`)

All alarm **code and UI** are gone: absolute `UInt32` alarm seconds hit the 2040
wall and can't be re-based safely. What was kept: the record still has the alarm
fields (for beam interop / packing), but no alarm setting UI, no alarm posting, no
alarm preset in Preferences. A form skip-set mechanism suppresses the alarm
controls. Beam **export** still writes an alarm token (harmless); **import**'s
`TranslateAlarm` parses it but the app never fires. Flag any live alarm control if
one appears.

---

## 8. Day View & Agenda ↔ ToDo35 integration

The Agenda view lists both appointments and **ToDo35** items:
- `DateAgenda.c`/`ToDoDB.c` repoint `sysFileCToDo`→`toDo35Creator`(`Td35`) and the
  DB name to `ToDo35DB`, so the agenda reads/launches **ToDo35** (not ROM To Do).
- `ToDo35Installed()` + a one-time `ToDo35NotInstalledAlert` (Talt 8902) if absent.
- The agenda's ToDo table is grown **3→5 columns** in C
  (`AgendaExpandToDoColumns`, SEC003, needs `ALLOW_ACCESS_TO_INTERNALS_OF_TABLES`)
  exactly like ToDo35's `ListExpandColumns` (pilrc caps at 3). Due-date/category
  columns drawn era-aware (read wide date from the record, `StripYearForColumn`,
  `WinGlueDrawTruncChars`, category right-aligned via `AgendaRightAlignCategory`).
- **Agenda tap → shown occurrence** (`datebook35-era-support`): tapping an
  appointment lands on the occurrence date the agenda is displaying (not "next from
  today"). The agenda stashes the current `Date` (packed `year<<16|month<<8|day`)
  in Feature `datebookCreator/agendaGotoDateFeature(4)` before `GoToAppointment`;
  the day view's `SetDateToNextOccurrence` `FtrGet`/`FtrUnregister`-consumes it. A
  **Feature** (not a global) is required because `GoToAppointment`'s
  `AppLaunchWithCommand(GoTo)` relaunch discards app globals.

### 8.1 Day-view ToDo rows (`DateDay.c`, `DayInjectTodos`)

The Day view also injects ToDo35 items as sentinel rows in the appointment list
(`recordNum = kDayTodoBase(0xF000)+slot`; `IsDayTodo()` guards every routine that
reads a recordNum so a sentinel never hits `ApptDB`). See
[[datebook35-dayview-todo-rows]] for the guard list; the whole feature grew from a
`MemHandleUnlock(ptr)`-vs-`MemPtrUnlock` heap-corruption bug ([[memhandleunlock-ptr-heap-corruption]]).

- **Match:** incomplete todos due exactly on the shown day, plus (today only) undated
  and overdue ones. Row uniqueID = `0xE0<<24 | realToDoUID` (distinct per item so the
  table re-inits when the day changes — a slot-index id collides).
- **Time + repeat** read from the shared **extras DB** (`lib/todoext.*`, added to the
  Datebook build; sec001 via CATSYNC_MULTISEG) — see [[todo-time-repeat-extras-db]].
- **Layout:** untimed/undated todos prepend at the top; **timed** todos are slotted at
  their start hour (reusing the empty hour slot there, else inserted before the next
  timed row). `DayViewDrawTimeBars` skips todos (no time bar).
- **Drawing:** a **5×5 checkbox square** marks a todo — in the time column
  (`DayViewDrawTime`) for untimed rows, or at the left of the description
  (`DayViewDrawIcons`, timed rows show the time in the time column instead). Note
  (`symbolNote`) / repeat (`symbolRepeat`) icons are right-aligned; the dotted
  text-field underline stops before them. Tapping a todo row → `LaunchToDoWithRecord`.

### 8.2 Day-view birthday/anniversary rows (`DateDay.c`, `DayInjectBdays`)

Same sentinel-row pattern as §8.1, one layer above it: `recordNum = kDayBdayBase
(0xF100)+slot`. `kDayBdayBase` is chosen with headroom above
`kDayTodoBase+kMaxDayTodo` (`0xF000+24=0xF018`) and `IsDayTodo()` is
**upper-bounded** at `kDayBdayBase` so it can't also match a birthday row (it
originally had no upper bound — a latent bug caught before it ever shipped,
fixed while adding this feature).

- **Source data**: reuses the Agenda's existing `AddrBday.h`/`AddrBday.c` module
  (`AddrBdayGet`/`AddrBdayGoTo`, SEC003) — the same Address35 birthday/anniversary
  lookup already used by `DateAgenda.c`, no new lookup code needed.
- **Match**: every Address35 birthday/anniversary falling on the shown day
  (`AddrBdayGet(Date.year+firstYear, Date.month, Date.day, ...)`).
- **Row uniqueID** = `0xD0000000 | (addressRecordIndex & 0x00FFFFFF)` — the `0xD`
  prefix is deliberately distinct from ToDo's `0xE` prefix (`gDayTodoUID`) so the
  two sentinel classes never collide in the table's row-tracking id.
- **Layout**: always untimed (`MemSet(...,0xFF)` on start/end time, matching the
  ROM's `apptNoTime` sentinel) — prepended to the top of `ApptsH` alongside the
  ToDo rows, so `DayViewDrawTimeBars`'s existing `TimeToInt(startTime)==apptNoTime`
  skip already excludes them with **no extra guard needed** there.
- **Initial scroll position** (`DayViewSetTopAppointment`): on **today's** date
  the ROM logic scrolls the day table down to the appointment nearest the
  current time, which would scroll the prepended sentinel rows off the top of
  the screen. Guarded the same way the pre-existing ToDo injection guards it:
  `if (gDayTodoCount > 0 || gDayBdayCount > 0) return;` keeps `TopVisibleAppt
  = 0` whenever either sentinel class is present, pinning them to the top.
- **Drawing**: plain "Name - Nth Birthday" text (`DayViewDrawIcons`), no
  checkbox/note/repeat icons — simpler than the ToDo case. **Time column**
  (`DayViewDrawTime`): falls through to the ordinary `DrawTime()` call, same as
  any non-sentinel row — no `IsDayBday` special case needed there at all.
  `DrawTime` (shared, `Datebook.c`) already special-cases `TimeToInt(t) ==
  apptNoTime` by drawing a centered symbol-font **`0x13`** (`symbolNoTime`)
  diamond; since birthday rows are always untimed (§ above), they get this
  marker automatically, and it's the exact same call+glyph the Agenda's
  `AgendaApptDrawTime` already makes unconditionally for its own untimed rows
  — so the two views match by construction, with no birthday-specific drawing
  code in either.
- **Tap → Address35**: `DayViewItemSelected` calls `AddrBdayGoTo(gDayBdayRec[slot])`,
  which does the `SysUIAppSwitch` to Address35 positioned on that contact —
  identical mechanism to the Agenda's birthday tap.
- Every `IsDayTodo`-guarded code path in `DateDay.c` got a parallel `IsDayBday`
  branch (record-renumbering skip, description/height/row-init guards, the two
  table row re-init uniqueID lookups). See [[datebook35-agenda-birthdays]] for the
  original Agenda-side version of this pattern.

### 8.3 Duplicate Event (`DateDay.c`, `DayViewDuplicateEvent`)

Record menu → **"Duplicate Event"** (first item, cmd 106, custom — no ROM
equivalent), same idiom as ToDo35's **Duplicate Item**
([[todo35-duplicate-item]]): read the source via `ApptGetRecord` (the existing
decrypt chokepoint — §9.2), and for a **repeating** event ask whether to
duplicate just this occurrence or the complete repeat setup, via a custom FORM
dialog (`DuplicateRepeatDlg`, id 2300) — reusing ToDo35's exact geometry (full
app width, bottom-aligned, buttons evenly spaced 5px from the borders and from
each other) and its synchronous `EvtGetEvent`/`FrmDispatchEvent` loop, since a
native `ALERT` can't control button width.

- **Non-repeating event** (`apptRec.repeat == NULL`): no prompt — duplicated as a
  single event on its own date.
- **"This Only"**: a one-off event dated on the **currently viewed day**
  (`Date`, the Day View's displayed date — this is a *later* occurrence's date
  for a repeating series, not necessarily the record's own anchor date), same
  start/end time, `repeat = NULL`.
- **"All"**: keeps the record's own anchor date (`apptRec.when->date`) and copies
  the `RepeatInfoType` by value — the duplicate repeats independently going
  forward, in parallel with the original. `ApptNewRecord` packs immediately
  (`ApptPack` copies into a fresh chunk), so passing the source's still-locked
  `apptRec.repeat`/`description`/`note` pointers straight through is safe — same
  technique the pre-existing `SplitRepeatingEvent` uses.
- **Alarm**: always `NULL` on the duplicate — alarms are removed suite-wide (§7).
- **Exceptions**: always `NULL` — a fresh duplicate starts with a clean
  occurrence list even when duplicating "All" of a series with existing
  exceptions.
- **Private flag**: copied from the source record onto the duplicate via
  `DmRecordInfo`/`DmSetRecordInfo` after creation — the exact pattern the
  pre-existing `CreateException` uses for the same purpose.
- **No category handling**: unlike ToDo35/Address35, Datebook appointments have
  no category concept at all (matches the ROM Date Book) — nothing to copy.
- **Segment fallout**: adding this (plus §8.2's birthday injection) pushed
  `sec001` (all of `DateDay.c`) over the 32 KB cap
  (`ld: region sec001res is full`). Fixed by compiling **only** `DateDay.c` at
  `-Os` in `build.sh` (`32464`→`32090` bytes, ~370 B headroom) — everything else
  stays `-O2`; no functional change, per [[segment-layout-policy]]'s "try -Os
  first" guidance.

---

## 9. At-rest encryption (Sec35 shared key)

Datebook35 encrypts every appointment's **description + note** at rest, using the
suite-wide **Sec35** passcode key (same model as ToDo35 / Address35 — see
[[datebook35-encryption-progress]], [[sec35-encryption-design]]). The model is
**copy-edit**: storage is **always ciphertext** when a passcode is set; edits happen
on an in-RAM decrypted copy and are re-sealed on write. Everything *except* the
description/note text stays plaintext (when/flags/alarm/repeat/exceptions drive
every date/time view, sort, and repeat expansion). **Birthdays/anniversaries in the
agenda stay clear** — Address35 stores those name/date fields unencrypted (they're
sort keys), so they show even when locked.

### 9.1 Crypto in the build & segments
- `build.sh` compiles `lib/enccore.c` + `lib/aes.c`; `db35_seg.h` sets
  `AES_SECTION = ENCCORE_SECTION = SEC003` → the crypto is **foreground-only**
  (a no-globals sublaunch must never call it — see §4/§9.6). **BUT the AES const
  tables (sbox/Rcon) MUST live in main** (A5-relative — [[multiseg-libgcc-gotcha]]);
  the small main-side crypto (`ApptFlagsOf`, the `ApptGetRecord` encrypted branch)
  lives in main too.
- Making room cost real relocations: `ApptAppInfoInit`+`ApptAddException` → sec002;
  **`todoext` moved sec001→sec002** via `-DTODOEXT_SEG_NAME=sec002` in build.sh
  (the bigger sec001 from the copy-edit code overflowed 32 KB → `relocation
  truncated to fit: DISP16 TdxGet`). Main now sits at **32,760 / 32,768 (8 B free)**
  — extremely tight.

### 9.2 Record format & the read chokepoint
- New **`flags.encrypted`** bit in `ApptDBRecordFlags` (byte 0 of the 2-byte
  big-endian flags word at offset 8 → mask **`0x02`**; when 0x80/alarm 0x40/repeat
  0x20/note 0x10/exceptions 0x08/description 0x04/**encrypted 0x02**).
- AES-CTR output can contain 0x00 (breaks inline NUL-terminated strings), so an
  encrypted record replaces the trailing string region with a **length-prefixed**
  form; the clear prefix is byte-identical:
  ```
  plaintext:  <prefix> [description\0 if flags.desc] [note\0 if flags.note]
  encrypted:  <prefix, flags.encrypted=1> [descLen:2 BE][noteLen:2 BE][encDesc][encNote]
  ```
  **Nonce = (uniqueID, 0) for desc, (uniqueID, 1) for note.** Prefix is VARIABLE
  (`ApptPrefixSize`, DateDB.c): `sizeof(ApptDateTimeType)=8 + flags 2 + alarm?
  sizeof(AlarmInfoType)=2 + repeat? sizeof(RepeatInfoType)=10 + exceptions? 2 +
  nExc*sizeof(DateType)=4`.
- **`ApptGetRecord` is the single decrypt/mask chokepoint** (~46 callers): for an
  encrypted record it decrypts (or masks `******` when Sec35 is **locked**) into a
  **reused** handle `gApptDecH` and returns it as `*handleP` — so every view masks/
  decrypts at once, no per-caller change. Lock state is detected **per record**
  from `(flags.encrypted && !EncKeyPresent())`, never from the passcode feature (a
  soft reset wipes the feature RAM while records stay encrypted).
- **Seal on write:** new appts are stored **plaintext** (so the no-globals beam
  receiver never calls sec003) and sealed by `ApptSealAllPlaintext` in
  `StartApplication` **and** `StopApplication`. `ApptChangeRecord` repacks to
  plaintext then `ApptEncryptOne` re-seals (else every edit would permanently
  decrypt). `ApptRecrypt(toEnc)` converts a record in place.

### 9.3 Locked UX — mask + bounce
- Locked → all appts show `******` in every view (the chokepoint) + the note view.
- **`DayLockedBounce`** (DateDay.c, SEC001): tapping any appt while locked
  (`!EncKeyPresent() && ApptDbHasEncrypted`) hands off to Sec35 to unlock
  (`FtrSet(ENC_CREATOR,kSecReturnFtr,'Db35')` + `SysUIAppSwitch(...,kSecLaunchUnlock)`)
  **before** the ROM enters edit mode. Called at the top of the `tblEnterEvent`
  handler; short-circuits on `EncKeyPresent()` so it only scans when locked.

### 9.4 Day-view inline-edit rendering — CRITICAL gotchas ([[datebook35-dayview-inline-edit-render]])
The copy-edit path fought the ROM's day-view rendering; the fixes are subtle:
- **NEVER write `attr.editable` in the table load-proc** (`DayViewGetDescription`).
  Setting editable=true/false via `FldSetAttributes` there makes the ROM draw the
  tapped item **selected/inverted** (whole-row black bar) instead of a clean cursor.
  Set **only** `autoShift` (as the pristine sample does); the ROM manages edit-mode
  editability itself. This was the #1 regression.
- **Demote the edited row to `textTableItem`** in `DayEnterEditStyle` (called from
  `tblEnterEvent` before grab-focus). Two reasons: (a) a row **with a note/repeat
  icon** is `narrowTextTableItem`, which the ROM inverts as a "custom item" on
  selection (see `DayViewSelectIconRect`'s comment) — demoting to a plain text item
  edits cleanly, and (b) **the `TblSetItemStyle` call itself** is what makes the ROM
  redraw the row with its proper **rounded** selection highlight; without it the
  first edit after launch keeps a raw square pen-invert. Call it for **every** row
  (icon and no-icon); only track icon rows (`gDayEditStyleRow`) to restore the icon
  on unfocus (`DayRestoreEditStyle` in `DayViewClearEditState`).
- **Refresh the time bars** (`DayViewDrawTimeBars()` in `DayViewItemSelected`) after
  the row redraw — the redraw erases that row's segment of the left time bars
  (they're drawn separately, not as a table column).
- Per-row decrypted **display** copies `gDayRowH[16]` + one **edit** copy
  `gDayEditH`; freed on day-form frmClose. `DayInjectTodos` decrypts injected
  ToDo35 rows (`priority & 0x40` → BE16 length prefix → decrypt nonce uid,0 / mask).

### 9.5 Agenda decryption (DateAgenda.c)
- `ListViewDrawDesc` decrypts the ToDo35 description into a local buffer (nonce
  uid,0) or masks `******` when locked (mirrors `DayInjectTodos`).
- **Priority "65" fix:** the datebook's copy of `ToDoDB.h` had a stale
  `priorityOnly = ~completeFlag (0x7F)` which doesn't mask the 0x40 encrypted flag,
  so an encrypted ToDo's `0x40|priority` displayed as 64+n. Matches ToDo35 now:
  `#define priorityOnly 0x3f` (+ `#define encryptedFlag 0x40`).

### 9.6 Sec35 wipe + rekey (in `apps/sec35/sec35.c`)
- **Forget Password / 10th-wrong-attempt wipe:**
  `WipeEncDb('DATA','Db35', 8, 0x02)` removes every appt with the encrypted flag
  (offset 8, mask 0x02 — a plaintext appt never sets 0x02).
- **Rekey on password change/set/delete:** `RecryptDatebook35` mirrors
  `RecryptToDo35`'s 3 modes (CHANGE re-key in place, SET plaintext→enc, CLEAR
  enc→plaintext) for the appt format, wired at all three password transitions.
  Unlike ToDo the prefix is variable → `ApptPrefix()` recomputes it from the flag
  bits. **All struct sizes were m68k-compiler-verified** (`or.b #2,8(%a0)` for the
  flag; `when 8, flags 2, AlarmInfoType 2, RepeatInfoType 10, DateType 4`) — never
  hardcode these from host reasoning; a wrong size corrupts records.

---

## 10. Beaming (send + receive)

vCal (`vEvent`, and forwards `vToDo` to ToDo35) over the Exchange Manager,
`DateTransfer.c`. `<IMCUtils.h>` capitalisation matters.

- Wire format is TEXT with a **4-digit year**; export writes real years, import's
  `MatchDateTimeToken` clamp widened to 9999 → far-future events beam intact.
- **SEND** foreground → sec003. **RECEIVE** is a no-globals sublaunch → **main**
  (`SEC_RECV`). This was the device crash: the importer (incl. the big
  `DateImportVEvent`) sat in sec003, unloaded during the receive sublaunch → Fatal
  Exception. Moved to main; compensation code pushed to sec002 (§4). VERIFIED on
  device: event received, no crash, date > 2031 preserved.
- **Encryption interaction (§9):** SEND reads via `ApptGetRecord` → **decrypts** →
  `DateExportVCal` emits plaintext vCal from `recordP->description/note` (record
  stays locked through the whole export). RECEIVE creates the appt via
  `ApptNewRecord` (**crypto-free**, stored plaintext) and it's **sealed on the next
  launch** by `ApptSealAllPlaintext` (the post-receive go-to launch triggers it).
  `DateTransfer.c` has **zero `Enc*` calls**, so the no-globals receive sublaunch
  never touches sec003 → no crash. (So the once-planned "beam-inbox refactor" is
  unnecessary for correctness — it was only a main-space optimization.)

---

## 11. Consolidated gotcha checklist (for a rebuild)

1. Force-include order: `edate_override.h`, then `db35_seg.h`, then `edate.h`.
2. `DateToInt`/`EDateInt` 32-bit — widen narrow date-key vars to `Int32`.
3. `DateCompare` special-cases the forever sentinel (`year==0xffff`) as LATEST.
4. `maxDays` override to 2957003; remove every `lastYear`/`numberOfYears` clamp
   (month/week nav, monthly/yearly repeats, beam import) → 9999.
5. `ApptPack`/`Unpack` symbolic offsets — the wide date must round-trip.
6. Alarms removed (§7); "today" via the hack; everything else hack-independent.
7. Segment rules (§4): libgcc→main; prototype tag matches definition; **any code a
   no-globals sublaunch (ExgReceiveData/ExgAskUser/Find) reaches → main**. Every
   segment is tight (see §4's table) — sec001 especially, 4 B free — keep
   compensation moves division-free + foreground-only.
11. Any new optional packed-record field (`ApptDBRecordType`) must pack as an
    **even byte count**, even if it's logically one `UInt8` — an odd-sized
    insert shifts every later optional section's alignment and faults
    ("address error") the instant that record also has an alarm/repeat/
    exceptions section (§16.1).
8. Launcher `tAIB`(1000/1001)+`tAIN`(0x3e8), not Constructor `taif/taic`.
9. pilrc: tables ≤3 cols (grow the agenda ToDo table in C); popups NONUSABLE;
   `MENUITEM SEPARATOR`.
10. Agenda targets ToDo35's DB + not-installed alert; goto-occurrence via Feature.
11. Dialog matching + non-bold Display-Options checkboxes (deliberate).
12. **Encryption (§9):** crypto in sec003 but AES tables + `ApptGetRecord` branch in
    main; `flags.encrypted = 0x02` at offset 8; `ApptGetRecord` is the sole decrypt/
    mask chokepoint; seal on Start/Stop; new appts + beam-received records stored
    plaintext then sealed on launch (so the no-globals receiver is crypto-free).
13. **Day-view inline edit (§9.4):** NEVER write `attr.editable` in the load-proc;
    demote the edited row to `textTableItem` (the `TblSetItemStyle` call drives the
    rounded selection); refresh `DayViewDrawTimeBars` after. Agenda `priorityOnly =
    0x3f` (mask the 0x40 enc flag, else priority shows "65").
14. **Sec35 wipe/rekey (§9.6):** `WipeEncDb('DATA','Db35',8,0x02)` +
    `RecryptDatebook35` (variable prefix). Verify every appt struct size against the
    **m68k compiler**, never host reasoning.
15. **Day-view sentinel rows (§8.1/§8.2):** `IsDayTodo` is upper-bounded at
    `kDayBdayBase` so it never also matches a birthday row; every code path
    touching a table `recordNum` needs both `IsDayTodo` and `IsDayBday` guards
    (or the combined `isSentinel` form) — grep both together when adding a
    third sentinel class. Also grep the OTHER class's **count** global
    (`gDayTodoCount`/`gDayBdayCount`) for guards keyed on "are there any
    sentinel rows" (e.g. `DayViewSetTopAppointment`'s initial-scroll pin) —
    those don't match an `IsDayXxx(recordNum)` grep at all.
16. **Duplicate Event (§8.3):** `DateDay.c` compiles at `-Os` (not `-O2`) to fit
    `sec001`'s 32 KB cap — don't silently revert this in a future `build.sh` edit.
17. **`FrmCopyTitle`/`FrmCopyLabel` buffer-size gotcha (checked, not a live bug
    here)**: these write into the `.rcp`-compiled string buffer, never
    reallocating — an unpadded short placeholder overflows and silently
    corrupts the heap when a longer runtime string is written (real crashes
    found+fixed this session in EraNTP and lazyFTP, plus a latent one found
    and fixed in CryptoNote — see `CryptoNote.md` §8.15,
    [[palmos-frmcopylabel-buffer-size-gotcha]]). Datebook35's own call sites
    (`DateWeek.c`, `DateDay.c`, `DateMonth.c`, `DateAgenda.c` — week/day/month
    titles, week-number label, agenda time title) are all ROM-derived
    (converter output) and already-adequately-sized for their real dynamic
    content, extensively real-hardware-tested — no change needed, but any
    *new* `FrmCopyTitle`/`FrmCopyLabel` call added to this app needs its
    placeholder padded to the true worst case.

---

## 12. File map

```
apps/datebook35/
  build.sh              multi-segment build (main + sec001..sec003)
  Datebook35.def        application + `multiple code { sec001 sec002 sec003 }`
  Src/db35_seg.h        SEC001..3 / DATELIB_SECTION(main) / SEC_RECV(main)
  Src/Datebook.c        PilotMain dispatch, Search, StartApplication, EventLoop (main)
  Src/DateGlobals.c     globals (main)
  Src/DateDB.{c,h}      appointment engine, ApptPack/Unpack, DateCompare, NextRepeat
                        (main); ApptGetAppointments -> sec002
  Src/DateDay.{c,h}     day view + NoteView editor (sec001, compiled -Os -- see §8.3)
  Src/AddrBday.{c,h}    Address35 birthday/anniversary lookup, shared with Agenda (sec003)
  Src/DateLocation.{c,h} event Location feature: edit dialog, Address35/Previous-Loc
                        pickers, Details-gadget glue, Day-View suffix (sec002 -- see §13)
  location35.rcp        hand-authored Location dialog/picker FORMs + Edit menu (§13)
  Src/DateSpan.{c,h}    multi-day event Start:/End: "Set Date & Time" sub-dialog (sec002 -- see §16)
  Src/DateWeek.{c,h}    week view (sec002)
  Src/DateMonth.{c,h}   month view (sec003)
  Src/DateAgenda.{c,h}  agenda view + ToDo35 5-col table (sec003)
  Src/DatePref.{c,h}    Preferences dialog (sec002)
  Src/DateDisplay.{c,h} Display Options dialog (sec002)
  Src/DateAlarm.{c,h}   alarm stubs (removed; ~empty)
  Src/DateTransfer.{c,h} beam SEND(sec003) + RECEIVE(main=SEC_RECV)
  Src/ToDoDB.{c,h}      shared To Do DB reader for the agenda (sec002)
  Src/DateDAV.{c,h}     read-only CalDAV Sync rows (CDDV mirror DB) for Day/Week/Month/
                        Agenda + "CalDAV Sync" menu bounce (helpers split main/sec001-3;
                        see §17-18)
  tools/                gen_bulk_datebook.py (test data generator)
  resources.rcp         strings, alerts, category names and other fixed resources (see §6.1)
  bitmaps.rcp bitmaps/  runtime bitmaps (see §6.1)
  forms.rcp             static, hand-maintained runtime forms + menu bars (see §6.1)
  aboutdb35.rcp todo35alert.rcp icons/   About(8800), not-installed alert(8902), icons
  (enccore.c + aes.c   Sec35 shared-key AES-CTR core, compiled from apps/lib into
                        sec003; the AES tables end up in main - §9.1)
apps/lib/  (shared)     edate_override.h, edate.{c,h}, datelib.{c,h}, era.{c,h},
                        datepick.{c,h,rcp}, hackcheck.{c,h}, hackalerts.rcp, y2k35.h,
                        enccore.{c,h}, aes.{c,h} (Sec35 crypto), todoext.{c,h}
                        (ToDo extras DB; -DTODOEXT_SEG_NAME=sec002 for datebook),
                        locdb.{c,h} (event Location storage; -DLOCDB_SEG_NAME=sec002 --
                        see §13), a35lookup.{c,h} (Phone Lookup via Address35, main --
                        Address35.md §10)
```

---

## 13. Event Location (`DateLocation.{c,h}` + `apps/lib/locdb.{c,h}`)

A "Location" field on the Details dialog (2-line preview gadget, below Date),
backed by a picker modeled closely on Address35's own address form, plus a
reusable "Previous Loc" history. Storage is a new pair of small side
databases keyed by the appointment's uniqueID -- `ApptDBRecordType`'s packed
format is untouched, mirroring ToDo35's `todoext.c` extras-DB precedent
(§8.1) rather than adding a new optional field to the appointment record.

### 13.1 Storage (`apps/lib/locdb.{c,h}`)

Two databases, both owned by Datebook35 (creator `'Db35'`), structurally
cloning `todoext.c`'s `TdxGetDatabase`/`TdxGet`/`TdxSet` pattern:

- **`AppointmentLocationDB`** (type `'Dlk1'`) -- one `DlxLink` record per
  appointment that HAS a location, keyed by the appointment's own uniqueID
  (foreign key, stored as data + found via linear scan, same as
  `TdxRecord.uid` -- `LinkGet`/`LinkSet`/`LinkDelete`).
- **`LocationsDB`** (type `'Dlc1'`) -- the flat, non-foreign-keyed "Previous
  Loc" history. Records are addressed by their OWN Data-Manager uniqueID
  (`LocDbAdd` returns it; lookups use `DmFindRecordByID`, not a scan).

`DlxLink.locDbRecUID` is the explicit link between an event's location and
the `LocationsDB` record it came from (0 if none). This resolves "auto adds
or edits to LocationDB, only if it wasn't picked from Address35" without any
string-match dedup:
- Picked from **Address35**: `fromAddress35=1`, `locDbRecUID=0` --
  `LocationsDB` is never touched, and Address35 is only ever read
  (`LocReadAddress35Record`/`ByUID`, cloning `AddrBday.c`'s cross-DB-read
  template), never written.
- Picked from **Previous Loc**, then possibly edited: `locDbRecUID` is that
  record's uid; committing calls `LocDbUpdate` (edits in place).
- Typed **fresh** (never picked): `locDbRecUID=0` going in; committing calls
  `LocDbAdd`, and the returned uid is stored back into the link.
- Location **cleared** (every field blank on Details OK): `LinkDelete`. The
  `LocationsDB` entry (if any) is untouched -- clearing an event's location
  must not delete it from history.

`LocSummary` (`"Name, City Zip"`, gracefully dropping blank parts) is the
one-line formatter shared by the Details gadget's preview and the Day View
suffix, so the two are consistent by construction.

**Known gap, matching `todoext.c`'s own accepted limitation**: deleting an
appointment does not clean up its `AppointmentLocationDB` link record --
`DetailsDeleteButton` never calls `DetailsApply` (where the commit path
lives), so an orphaned link record accumulates. Not fixed (would cost more
of `sec001`'s already-thin budget); same tradeoff `todoext.c` already made
for orphaned ToDo extras.

### 13.2 Details dialog field (`DateDay.c` sec001 + `DateLocation.c` sec002)

**First attempt used a GADGET** (non-editable, dynamically drawn,
`formGadgetHandleEventCmd`/`penDownEvent` to catch taps) -- built, compiled
clean, but never actually received the tap (user report: "cannot tap on the
text field to trigger any form"). **Fixed by switching to the mechanism
Address35 itself already uses for this exact UI pattern**
(`address35/EditForm.c`'s `fldEnterEvent` handling for its
ftAddress/ftEmail/etc. rows): the Location control is a real **`FIELD ...
EDITABLE UNDERLINED MULTIPLELINES`**, and `DetailsHandleEvent` intercepts
`fldEnterEvent` for its ID (`DetailsLocationGadget` -- name kept from the
gadget attempt, now misleading but not worth a broad rename) BEFORE the ROM
lets it become editable, running the Location dialog instead and always
marking the event handled. The field is never actually typed into --
`LocFieldTapped` (`DateLocation.c`) always intercepts. Its resource ID
constant name still says "Gadget" (harmless, not worth a churn-risk rename).
Displayed text (the one-line summary, or a **"Tap to set location"**
placeholder when blank) comes from the SAME `DlgSetField` helper every
other field in `DateLocation.c` uses -- no custom draw code needed at all;
the ROM's own field renderer handles wrapping/underlining natively via the
`MULTIPLELINES`/`UNDERLINED` resource attributes.

**Layout also had to be fixed alongside this**: the FIRST version stacked
the label above a full-width 2-line block, breaking the form's own
established "label in the left column (x=9/23), control starting at x=56
in the right column, same row" pattern that every other Details row
(Time/Date/Repeat) uses. Now: `LABEL "Location:" AT (9 50)` +
`FIELD ... AT (56 50 100 22)` -- same row, right column, matching
Time/Date's own `x=56 width=100` trigger geometry exactly.

**`sec001` is at the 32KB cap** (this session's Duplicate Event + birthday
rows already left it at ~134B free), so the field's edit/staging logic
lives entirely in `DateLocation.c` (`sec002`) behind a module-static
`gCurrentLoc` the caller never touches directly. `DateDay.c`'s side is
exactly 3 cross-segment call sites:
1. `LocGadgetLoad(frm, uid)` in `DetailsInit` -- seeds `gCurrentLoc` via
   `LinkGet` AND sets the field's displayed text in one call. Reuses the
   SAME `DmRecordInfo` call `DetailsInit` already makes for the
   secret-checkbox attr (just adds a non-NULL uid pointer) rather than a
   second lookup.
2. `LocFieldTapped(frm)` from the `fldEnterEvent` case above.
3. `LocGadgetCommit(uid)` in `DetailsApply` -- persists `gCurrentLoc`
   (`LinkSet`/`LinkDelete` + `LocDbAdd`/`LocDbUpdate` as appropriate). Only
   called on OK/Note (never Cancel/Delete -- confirmed by reading
   `DetailsHandleEvent`), so Cancel correctly discards any picks/edits made
   in this session; idempotent, so it's called unconditionally rather than
   diffing (a diff would cost sec001 budget for no real benefit here).

**Layout mechanics**: `gen_forms.py`'s `FORMBOX[1300]` grew from
`(2,55,156,103)` to `(2,27,156,131)` (+28px, all absorbed by moving the
box's TOP up -- bottom stays pinned at 158). `add_location_field()` inserts
the Location `LABEL`+`FIELD` lines (no Constructor original exists for
them) right after Date, before the `REMAP`-shifted Repeat row
(Repeat/Private/buttons all moved to a second, taller set of y's to make
room). Form object coordinates in this resource format are relative to the
FORM's own top-left, not the screen -- confirmed empirically (Time's rel
y=18 only makes sense against a box whose own y=55, not as a literal
screen y=18) before designing the geometry.

### 13.3 Location edit form + pickers (`DateLocation.c`)

Modeled closely on Address35's `EditAddressDialog`/`A35PickList`/
`RelPickContact` (`address35/EditDialogs.c`) -- see that file for the
originals. Per explicit user request, the Country field matches Address35's
UX exactly (full-screen searchable picker, not a plain text field): `apps/lib/locdb.h`'s `kLocCountries[]` is a synced copy of Address35's own
`kCountries[]` (~193 entries), same duplication idiom as `AddrBday.c`'s
`kAnnivType[]` copy. The last-picked country is remembered via an
**unsaved** app-pref (`PrefSetAppPreferences(..., saved=false)`,
`datebookUnsavedPrefID` -- a pre-existing, previously-unused ROM-template
constant), mirroring Address35's own `A35GetLastCountry`/`A35SetLastCountry`.

- **Location edit dialog**: clone of `frmAddrDlg` with Line1/Line2 shrunk
  from 2-line to single-line fields, a new Name field, and two picker
  buttons ("Address35"/"Previous Loc") above Name -- the single-line shrink
  (~20px saved) roughly offsets the additions, so it fits Address35's own
  156×156 full-screen box unchanged.
- **"Address35" picker**: clone of `RelPickContact` (full-screen list +
  Find, reads Address35DB read-only via `AddrBday.c`'s
  `DmOpenDatabaseByTypeCreator` template). Name defaults to "First Last",
  else Company (for a business contact with no personal name on file).
  **Edited-after-pick still saves to LocationsDB**: per user request, if
  the user picks an Address35 contact and then edits ANY field before
  tapping OK, the result is treated as a normal typed/history location from
  that point on (saved via `LocDbAdd`, reusable from "Previous Loc" going
  forward) instead of staying `fromAddress35=1` (which suppresses the
  LocationsDB save entirely -- the original, still-correct rule for an
  UNEDITED Address35 pick). Detected via a `snapshot` taken right after the
  pick (`LocationEditForm`, `DateLocation.c`), compared field-by-field
  (`MemCmp` over the whole `DlxLocation` -- safe since it's all-`char[]`
  fields with no compiler padding) against the form's values at OK time.
  Never writes back to Address35DB itself either way -- that stays
  untouched regardless of whether the picked location gets edited.
  **Encryption**: `a35fAddress` (the TAB-packed Line1/Line2/Apt/City/
  State/Zip/Country block) is one of Address35's encrypted PAYLOAD fields
  (see `AddressDB35.c`'s `A35FieldEncrypted`) -- unlike Name/Company/First/
  Last, which stay clear as sort keys (the reason `AddrBday.c`'s raw-read
  pattern is safe to clone for THOSE fields but not for the address block).
  `LocReadAddress35Record` checks the record's `misc` byte and, if
  encrypted, decrypts the copied `a35fAddress` bytes locally via
  `EncGetKey`/`EncCrypt` (nonce = record uid + field-loop index) before
  TAB-splitting -- mirrors `AddressDB35.c`'s own `A35GetRecord` decrypt step
  byte for byte (Address35 exposes no cross-app decrypt call to reuse
  directly), including masking with `******` when Sec35 is locked rather
  than showing ciphertext-as-text. `enccore.c`/`aes.c` were already linked
  into Datebook35 for its own appointment encryption (§9) and tagged
  `sec003`; this is a sec002→sec003 cross-segment call, same jump-table
  glue as any other.
- **Country picker**: reuses the SAME generic engine + form resource as the
  Address35 picker (`LocPickStringList`, form `LocAddrPickDialog`) --
  structurally identical (list+Find, no Delete), just fed a plain string
  array instead of a contact-name array built from DB records; title set at
  runtime (`FrmSetTitle`), not fixed in the resource.
- **"Previous Loc" picker**: a genuinely new dedicated loop (no existing
  precedent in this codebase has Delete inside a picker) with an inline
  "Del" button + confirm `ALERT` (`DeleteLocationAlert`, same
  `CONFIRMATION`/`BUTTONS "OK" "Cancel"` idiom as Address35's `altSetMe`).
- All 3 new forms carry a minimal Edit-only `MENUID` (`mnuLocEdit`:
  Undo/Cut/Copy/Paste/Select All, same items+IDs as the Day View's own Edit
  pulldown) -- the first Datebook35 dialogs with real text-entry fields
  outside the main views, and a `LocDlgSysMenu` helper
  (`SysHandleEvent`+`MenuHandleEvent` before `FrmDispatchEvent`) makes
  Cut/Copy/Paste and the command-stroke work in these hand-rolled
  synchronous event loops, cloning Address35's own `A35DlgSysMenu`.

### 13.3.1 "Suffix never shows up at all" -- two stacked bugs

On a real device the suffix never appeared AT ALL, focus or no focus, no
matter what. Two independent bugs, found in order:

1. **`DayViewLoadTable`'s row-reuse cache never noticed a location change.**
   It decides whether a row needs `TblMarkRowInvalid` (the only thing that
   makes `TblRedrawTable` actually re-run the load-proc for that row) purely
   by comparing the row's cached `uniqueID`/`startTime`/font/`iconsWidth`
   against the appointment's current values. A location lives in a wholly
   separate DB (`AppointmentLocationDB`) that changing doesn't touch any of
   those fields, so a location-only edit to an appointment already sitting
   in the same row left that row's `DaySetRowCopy`'d cached text
   permanently stale. Fixed the same way the pre-existing `masked`/
   `TblRowMasked` check right next to it already solved an identical class
   of problem (secret-record masking also isn't visible to those 4 fields):
   added `gDayRowHasLoc[kDayMaxRows]`, a remembered-per-row boolean compared
   each `DayViewLoadTable` pass against `gDayLocText[apptIndex][0] != 0`,
   `TblMarkRowInvalid` on mismatch. Reset alongside `gDayRowH[]` in
   `DayViewFreeRowCopies`. **This alone did not fix the report** -- it fixed
   a real bug (the row now reloads when it should), but the text it reloads
   still had no suffix, because of bug 2:
2. **The suffix-append code was only ever wired into the NON-encrypted
   fallthrough path of `DayViewGetDescription`.** That function has an
   entirely separate early-return branch for `ApptRecEncrypted() == true`
   (Db35's own at-rest appointment encryption, §9 -- a PER-RECORD flag, set
   on any appointment saved while a Sec35 passcode is active) that decrypts
   and returns the description directly, in 3 sub-cases (locked-masked,
   unlocked-editing, unlocked-display) -- none of which knew the location
   feature existed, since that branch predates it. Every appointment the
   user tested with had been saved under an active Sec35 passcode, so 100%
   of their test rows took this branch and 0% ever reached the suffix code
   400 lines further down -- "is this even being done?" was the right
   question, and the answer was no, not for these rows. Fixed by adding the
   identical suffix-append (`DayBuildLocSuffix`) to the unlocked+DISPLAY
   sub-case only, built from the just-decrypted plain text
   (`DayCopyDescPlain`'s output, not `apptRec.description` which is
   ciphertext here). Left the locked-masked sub-case alone (still shows
   plain `"******"`, no suffix) -- a locked/masked row revealing its
   location while its title stays hidden is a privacy trade-off, not a bug,
   flagged for the user rather than decided silently.

### 13.3.2 Row height never accounted for the suffix

Third stacked bug in the same feature: `DayViewGetDescriptionHeight` (the
function every row-height computation in the file funnels through --
`DayViewLoadTable`, `DayViewLayoutDay`'s compression pass, scroll math)
measured `FldCalcFieldHeight` against `apptRec.description` alone -- it had
no idea the drawn text could be "desc @ location" (longer, can wrap to more
lines). So a suffix long enough to need 2 lines got silently clipped to
whatever height the plain title alone needed. Fixed by measuring the SAME
suffixed text `DayViewGetDescription` actually draws (built via the same
`DayBuildLocSuffix`, freed right after measuring) whenever a row has a
location AND isn't Sec35-locked -- locked rows draw plain `"******"` with no
suffix (§13.3.1 item 9), so sizing off the suffixed text there would make
the row taller than what's actually shown. `DayViewLoadTable`'s pre-existing
`height != oldHeight` invalidation check picks the new taller height up for
free once `DayViewGetDescriptionHeight` reports it correctly -- no separate
invalidation logic needed here, unlike §13.3.1 item 8's fix.

### 13.4 Day View "@ location" suffix

`DayInjectLocationText` (sec001, called from `DayViewLayoutDay` right after
`DayInjectTodos`/`DayInjectBdays`) prefetches every row's location summary
ONCE per day-load into `gDayLocText[apptIndex][kLocSummaryLen]` (capped at
`kMaxDayLocRows=48` -- a day with more rows just skips the suffix for the
overflow, graceful not fatal) -- avoiding a `locdb.c` scan on every table
redraw. The actual per-row lookup (`DmRecordInfo` for the uid +
`LinkGet`+`LocSummary`) is ONE cross-segment call per row
(`LocSummaryForRecord`, `locdb.c`), so the sec001-side loop itself stays
small.

**KNOWN, ACCEPTED LIMITATION: entering a row for edit takes 2 taps before
the suffix clears on the first tap.** (Tap 1 shows the row focused but
still carrying the suffix; a second tap on the same row clears it.) Four
fix attempts were made and three were reverted; per explicit user direction
("no it is still not working. revert. and stop working on this issue."),
work on this specific behavior has STOPPED -- do not resume without being
asked again. History, in order:
1. `TblMarkRowInvalid` alone on focus-entry (`tblEnterEvent`): suffix never
   cleared at all (marking invalid only flags a row stale for the *next*
   redraw pass, it doesn't force one).
2. Adding `TblRedrawTable` right after (mirroring
   `DayViewClearEditState`'s own proven invalidate+redraw PAIR for the
   symmetric blur transition): cleared, but needed the 2 taps described
   above -- the app's own synchronous redraw (still `editable==false`,
   since it's a plain redraw not a focus grab) and the ROM's own
   subsequent default focus-grab (still to come in the same event, since
   `handled` was left `false`) were racing for the same tap.
3. Taking over the whole grab-focus sequence explicitly (`TblGrabFocus`+
   `FldGrabFocus`+`handled=true`, mirroring `DayViewRestoreEditState`'s own
   pattern for "returning from Details/Note view") to eliminate the race:
   this made it WORSE -- tapping an already-focused row again crashed with
   "table already has focus". Reverted immediately.
4. Moved the suffix out of the load-proc entirely, drawing it instead as a
   custom-draw overlay in `DayViewDrawIcons` (the same callback that
   already draws note/repeat icons, gated by `TblEditing`+`TblGetSelection`
   instead of the load-proc's `editable` param) -- reasoned to sidestep the
   load-proc's caching semantics by leaning on the ALREADY-PROVEN
   icon-hide-on-focus mechanism instead. User reported this ALSO didn't
   work. Reverted.

**Current (final, stopped-here) state**: back to attempt 2 --
`DayViewGetDescription` appends the suffix via `DayBuildLocSuffix`
(`DateLocation.c`) when `!editable`, and `tblEnterEvent`'s handler calls
`TblMarkRowInvalid`+`TblRedrawTable` on the tapped row before falling
through to the ROM's default focus-grab. Stable (no crash), suffix
correctly restores on blur (`DayViewClearEditState`'s pre-existing pair,
never part of the problem), but needs 2 taps to clear on the FIRST entry
into a row per session. See [[datebook35-location-feature]] memory for the
full blow-by-blow if this is revisited later.

### 13.5 Segment placement -- three false starts before the fix that worked

Every piece of new code was tried in **sec003** first (per the original
feasibility plan, since it had the most apparent headroom) -- overflowed by
~450B once `DateLocation.c`'s functions landed there. Fallback to **main**
also failed ("`ld: region coderes is full`") despite the raw per-object
`.text` sum staying comfortably under the nominal 32770-byte cap -- a
`-Wl,-Map` dump revealed why: `libPalmOSGlue.a`'s `TextMgr.o` (3664B) +
`IntlGlue.o` (380B) + `TxtGlue.o` (2094B) + related glue objects (~7KB
total) were ALREADY being linked into `main`, pulled in transitively by a
single **pre-existing** `TxtGlueUpperChar` call in `DateDay.c` (auto-
capitalizing the first keystroke of a new appointment) -- invisible to a
naive "sum `build/*.o`" measurement since it comes from the static library,
not the project's own object files. `main` had essentially zero real
headroom once that's counted, even before this feature. Fixed in two parts:
1. Retagged `DateLocation.c`'s functions to **`sec002`** instead (most
   headroom of the four segments, and already hosts `locdb.c`, so the two
   files' mutual calls are cheap intra-segment calls) -- but the country
   name STRING LITERALS still land in `main`'s `.text` regardless of
   tagging (this compiler pools literal bytes separately from the
   `static const char*[]` pointer array, which DOES follow the tagged
   segment).
2. Replaced the `TxtGlueUpperChar` call with a plain ASCII uppercase fold
   (`DateDay.c`, the keydown-on-new-appointment path) -- consistent with
   this project's existing English-only case-folding convention elsewhere
   (`LocLower`/Address35's `DlgLower`), and Graffiti/hardware keydown chars
   are plain ASCII in practice. This alone freed ~7KB from `main`, far more
   than the ~1.8KB of country-string data needed.
3. `sec001` still needed one more relocation once the Details-gadget +
   Day-View hooks were added (down to 44B free, then negative): moved
   `RepeatDrawDescription` (1274B, the self-contained Repeat sub-dialog's
   description-text builder, called only from within `DateDay.c` itself) to
   `sec002` -- same "relocate one more existing function" playbook already
   used twice this session (Duplicate Event + birthday rows needed
   `DateDay.c` at `-Os`; encryption moved `ApptAppInfoInit`/
   `ApptAddException`).

Final segment state: `main`=23268/32770, `sec001`=31700/32770 (1070 free),
`sec002`=27424/32770 (5346 free), `sec003`=28938/32770 (3832 free). Build
clean (`DONE: 150641 bytes`). Since tested: adding a Location via the picker and
its Day View / Details display are ticked in `apps/test/full_checklist.md` §2.
(Segment numbers here are from that time; current ones are in §4.)

## 14. Day View navigation: day-by-day, not week-by-week

Deliberate deviation from stock DateBook. The two triangle triggers beside
the day-of-week bar (`DayPrevWeekButton`/`DayNextWeekButton`) originally
jumped a full week (`DateAdjust(&Date, ±daysInWeek)`, same weekday, per the
ROM original). Per user request, they now step **one day** at a time
(`DateAdjust(&Date, ±1)`), rolling into the next/previous week the same way
`DateAdjust` already rolls a day-of-week-bar jump into the next/previous
month -- no new logic needed, just the increment (`ctlRepeatEvent` handler,
`DateDay.c`). A single tap steps one day; holding repeats day-by-day (same
repeat-trigger mechanism as before, just a smaller step).

## 15. Title field in Event Details + tap-opens-Details for existing events

Deliberate departure from stock DateBook's edit model. Stock (and this
app until now) edits an event's title *inline*, directly in the Day View
table row (tap -> row becomes an editable field -> type -> tap away
commits). That mechanism is also the exact one behind the still-parked
"2-tap to clear the `@ location` suffix" bug (§13.4 item 6 /
`[[datebook35-location-feature]]`) -- the suffix has to be stripped the
instant a row gets focus and restored the instant it loses focus, and that
hand-off proved fragile across 4 fix attempts. Per user request, this
sidesteps the bug at the root instead of continuing to chase it: title
editing moved into a new "Title:" field on the Event Details dialog, and
tapping an *existing* event's row now opens Details immediately instead of
entering inline edit. Since the row is never put into inline-edit mode for
an existing event, there is no focus/blur transition on that row anymore --
the location-suffix caching problem is now structurally unreachable for
these taps (not solved, sidestepped; §13.4 item 6 is still the honest
record of what went wrong when it *was* being edited inline).

**Scope** (what changed, what didn't):
- Tapping an EXISTING appointment's description column -> `FrmPopupForm
  (DetailsDialog)` immediately, no inline edit.
- Keystroke auto-create (typing with nothing selected -- classic PalmOS
  DateBook quick-entry, `DayViewHandleEvent`'s `keyDownEvent` branch calling
  `DayViewNewAppointment`) -- UNCHANGED per explicit instruction ("keep
  that"). That path calls `TblGrabFocus`/`FldGrabFocus` directly and never
  goes through `tblEnterEvent`, so it was never touched by this change
  regardless.
- Tapping a truly EMPTY time slot directly (not via typing) -- UNCHANGED,
  still creates a blank timed appointment at that slot and enters inline
  edit exactly as before. Once created (either way), the row is a normal
  existing-appointment row, so the *next* tap on it opens Details like any
  other event.

**Title field** (`DetailsDialog`, form 1300, `DatebookRsc.h`
`DetailsTitleLabel`/`DetailsTitleField` = 1322/1323): inserted as the FIRST
row, above Time -- `gen_forms.py`'s `add_title_field` (same
insert-a-brand-new-row idiom as `add_location_field`). Everything below
shifts down one row (~16px): Time 18->34, Date 34->50, Location 50->66,
Repeat 78->94, Private 94->110, buttons 114->130. `FORMBOX[1300]` grows
UPWARD to stay bottom-anchored (top 27->11, bottom pinned at 158) -- same
rule as every dialog in this app, and the same technique Location's own
insertion already used once (top 55->27 then). y=11 is close to the
title-bar area; visually fine so far, worth a fresh look if it ever needs
to grow again.

Storage: `DetailsType` gained `MemHandle descH` (NOT a fixed `char[]` like
Location's bounded fields -- an event description has no compiled-in size
cap elsewhere, and a fixed staging buffer would silently truncate any
existing long title on every Details open even without an edit). Staged via
`DayCopyDescPlain` (already existed, used for the inline encrypted-edit
copy) in `DetailsInit`, freed in `DetailsExit` (matches `gDayEditH`'s own
explicit-free convention -- `FldSetTextHandle` never takes ownership).

`DetailsApply` commit is the subtle part: a title change is tracked via a
SEPARATE `descDirty` flag, deliberately NOT folded into the existing
`dirty` flag. `dirty` also gates the "This/Future/All" range-dialog prompt
for repeating events, and a title is not a per-occurrence property in this
data model (the old inline-edit path never prompted for this either,
confirming titles apply to the whole series). `descDirty` is threaded
through both write sites: the final `else if (dirty || descDirty)` ->
`ApptChangeRecord` call (sets `changedFields.description`/`newRec.description`
only when `descDirty`, so a title-only edit doesn't touch `newRec`'s other
fields at all -- safe, since `ApptChangeRecord` only reads fields flagged in
`changedFields`), and the sibling `if (exception)` "This Only" split-off
branch, which used to unconditionally copy the OLD description into the new
record -- fixed to carry the edited text when `descDirty`, or editing the
title AND changing the time in the same Details session would silently
drop the title edit. `ApptChangeRecord` re-encrypts automatically if the
record is Sec35-encrypted (`ApptEncryptOne`, `DateDB.c`) -- no special
encryption-awareness needed here, unlike the inline-edit path's manual
`gDayEditH` re-encrypt dance (that dance exists only because the inline
path edits a live decrypted copy without going through `ApptChangeRecord`
at all).

Tap interception (`DayViewHandleEvent`'s `tblEnterEvent` handler,
description-column branch): splits on `recordNum != emptySlot`. An existing
record sets `CurrentRecord`/`ItemSelected` and calls `FrmPopupForm
(DetailsDialog)` -- the exact same call the "Details" button already used,
so the return-and-refresh path ("dismissing the form unfocuses the day view
row") needed zero new cleanup code: the row was never in edit mode to begin
with, so there's nothing to unfocus. An empty slot falls through to the
pre-existing `DayEnterEditStyle`+invalidate+redraw code, unchanged -- and
that code's old "KNOWN LIMITATION, 2-tap" comment was removed from this
branch since it no longer applies here (a blank not-yet-created event can't
have a location suffix to begin with; that comment's relevant code now only
runs for existing-event rows the OLD way it used to -- wait, it doesn't
run there anymore at all, since existing rows redirect to Details before
reaching it).

**Crash found on first device test: "MemoryMgr FreeHandle" on exiting Event
Details.** `DetailsExit` freed `descH` (`MemHandleFree`) without first
detaching it from the field (`FldSetTextHandle(fld, NULL)`) --
`FldSetTextHandle` never takes ownership, but the field still holds a
reference to whatever handle it was given, so freeing it out from under
the field crashes on form teardown. This exact detach-then-free two-step
already exists elsewhere in this same file (`DayViewSaveDescription`'s
`gDayEditH` cleanup) -- missed replicating it for `descH`. Fixed: `DetailsExit`
now calls `FldSetTextHandle(GetObjectPtr(DetailsTitleField), NULL)`
immediately before `MemHandleFree(descH)`.

**Second crash: "table already has focus" on the SECOND entry+exit of
Details** (first exit silently corrupts the Table Manager's focus state,
second exit's own grab-focus attempt collides with it). Root cause:
`DayViewRestoreEditState` -- "restores the edit state... when returning
from the details dialog", called unconditionally from `DayViewHandleEvent`'s
`frmUpdateEvent` handler on every Details close -- is guarded only by
`if (!ItemSelected) return;`. The new tap-to-Details path sets
`ItemSelected = true` (needed for other things) even though the row was
never put into inline edit, so this function's `TblGrabFocus`/`FldGrabFocus`
calls fire on a row that was never focused to begin with. Same "table
already has focus" signature as this session's earlier, reverted
location-suffix "attempt 3" -- same mistake class (grab focus without
confirming the table doesn't already believe it has it), different call
site. Fixed with a one-shot flag (`gDaySkipRestoreEditState`): set right
before `FrmPopupForm(DetailsDialog)` in the tap-to-Details branch, checked
+cleared at the top of `DayViewRestoreEditState`. Deliberately doesn't
touch that function's ~12 OTHER call sites (time-change, Note-view return
via the OLD Details-button flow) -- those are all genuine "row really was
mid-inline-edit" cases this function exists for.

Segment cost: ~284 bytes of `sec001` total across both crash fixes (740 ->
456 free) -- **margin is now very thin**; a relocation out of sec001 is
likely needed before much more lands in this file.

**User-confirmed working**, both crashes fixed, end to end: tap an existing
event opens Details (no inline edit); editing the title and tapping OK
updates Day View with no lingering focus/highlight and no crash on repeat
entry/exit; keystroke quick-entry and direct empty-slot taps behave exactly
as before.

## 15.1 Follow-up UI pass: full-screen Details, 2-line Title, full address summary

Three user-requested refinements on top of §15's initial rollout, all in
`forms.rcp`/`locdb.c`/`DateLocation.c` -- no `DateDay.c` (sec001) changes at
all this round, so the tight 456-bytes-free budget there is untouched.
**User-confirmed working** (all three, including item 3's open question).

1. **Details goes full-screen; Title grows to 2 lines.** `FORM ID 1300`
   grew from `(2 11 156 147)` to `(2 2 156 156)` -- same full-screen
   convention the Location pickers already use (`location35.rcp`'s
   `(2 2 156 156)` boxes). Bottom stays pinned at 158 either way (2+156 =
   11+147). Title's `FIELD` gained `MULTIPLELINES` and grew 11px->22px
   tall; every row below it re-flowed using this app's established
   spacing convention (16px pitch between single-line rows, +12px extra
   after any 2-line field -- same math Location's own insertion used for
   the row after it). No C code changes needed for this part --
   `FldSetTextHandle`/`FldGetTextPtr` don't care about a field's wrap mode.

2. **`LocSummary` now concatenates every filled field**, not just
   "Name, City Zip" -- name/line1/line2/apt/city/state/zip/country,
   comma-joined, blanks dropped (`apps/lib/locdb.c`). `kLocSummaryLen`
   grew 64->256 (theoretical max ~282 bytes for all 8 fields at their own
   max length + 7 separators; Day View's existing row-height cap clips
   visually well before this length matters). Day View shows this FULL
   text (multi-line, via the row-height fix from §13.3.2 -- unaffected,
   already handles arbitrary wrap height). The Details preview field
   can't show that much in 2 lines, so a new `LocEllipsize` helper
   (`DateLocation.c`, sec002) truncates a COPY with a trailing "..." if
   `FldCalcFieldHeight` (at the field's own font/width, queried via
   `FldGetFont`/`FldGetBounds` -- matches `DayViewGetDescriptionHeight`'s
   own font-aware measurement convention) reports more than 2 lines.
   `LocSummary` itself never truncates beyond its own `outLen` -- the
   ellipsis is purely a Details-display-time concern, applied in
   `LocFieldRefresh` only.

3. **Title field scrolling past 2 lines when editing**: NOT a new feature
   -- this is standard PalmOS Field Manager behavior for any bounded
   (non-auto-growing) `EDITABLE MULTIPLELINES` field: dragging a selection
   toward the bottom edge auto-scrolls to reveal more text, same mechanism
   Memo Pad's own editor relies on. Title/Location in this dialog are
   exactly that kind of field (unlike Day View's inline table field, which
   deliberately auto-GROWS via `fldHeightChangedEvent` instead of
   scrolling). No code was added for this, and the guess was right --
   **user confirmed it scrolls correctly as-is**, no follow-up needed.

To rebuild from stock Date Book: start from `Sample_Source/Examples/Datebook/`,
apply the era override (§2) and store **real years** in the wide record; sweep out
every 2031 clamp (§2.6) and fix the `DateCompare` sentinel + `maxDays`; keep the
existing multi-segment split but ensure **the beam-receive half of DateTransfer is
in main** (§4/§9); remove alarms (§7); wire the era picker, agenda↔ToDo35
integration (§8), dialog matching (§6.2), and the launcher `tAIB`. Cross-check
`ToDo35.md` for the shared era/segmentation foundation.

---

## 16. Multi-day (spanning) events

Events can now end on a **later calendar day** than they start (e.g. a
flight departing 11pm and landing 1am the next day). Design doc:
`.claude/plans/ancient-frolicking-ember.md`. Full feature, done and
POSE64-verified across Day/Week/Month/Agenda + vCard + a full regression
pass; user confirmed working live.

### 16.1 Data model — `DateDB.h`/`.c` (main)

The span is a **day offset from the start date**, not an absolute end date
— this is what makes it generalize to repeating events for free
(`occurrence end day = occurrence start day + offset`, same offset every
occurrence):
```c
// ApptDBRecordFlags gains an 8th bit (fills the byte exactly):
unsigned endDate : 1;
// ApptDBRecordType gains a plain value (not a pointer, unlike
// alarm/repeat/exceptions -- it's one fixed byte, always cheap to copy):
UInt8 endDateOffset;   // days after when->date the event ends; 0 = same day
```
`ApptUnpack` always populates it (0 when the flag bit is absent — old
on-disk records transparently mean "same day", no migration needed).

**The packing gotcha that WILL bite if you add another optional field the
same way:** `ApptUnpack` casts raw bytes directly onto
`AlarmInfoType*`/`RepeatInfoType*`/etc pointers **into the packed on-disk
buffer itself** (no copy) — `dest->repeat = (RepeatInfoType *) p;` — and
those structs contain 2-/4-byte fields, which m68k faults on
("address error") if misaligned. `endDateOffset` is logically one `UInt8`,
but packing it as a *lone* odd-sized byte shifts every later optional
section's offset by 1 relative to the endDate-absent case (the only case
ever exercised before this feature existed) — invisible with a
non-repeating spanning event (only byte-oriented description/note strings
follow), but **crashes the instant a spanning event also repeats or has an
alarm**. Fix: always pack/unpack it as **2 bytes** (1 padding byte),
preserving whatever alignment parity the following sections already had:
```c
#define kEndDateOffsetPackedSize 2   // NOT sizeof(UInt8) -- see above
```
Applied consistently in `ApptPack`, `ApptUnpack`, `ApptPackedSize`, and
`ApptPrefixSize` (the Sec35 encryption re-seal path's clear-prefix size
calculator — also needed the same fix, another crash source if missed).
**General lesson for this codebase: any new optional packed-record field
must consume an even byte count**, independent of whether it "needs" to.

`ApptChangeRecord` gets the usual `if (changedFields.endDate) src.endDateOffset = r->endDateOffset;`
one-liner. Every place that builds a *fresh* `ApptDBRecordType` for a "This
Only" repeat-exception branch (`CreateNote`, `DeleteNote`, `MoveEvent` in
`DateDay.c`) needed an explicit `newRec.endDateOffset = apptRec.endDateOffset;`
too — these rebuild the struct field-by-field rather than copying it
wholesale, an existing pattern in this file (see ToDo35.md's Duplicate
Item note on the same "rebuild, don't memcpy" convention), so a new field
is invisible to them unless added explicitly.

### 16.2 Event Details UI — `DateDay.c` (sec001) + new `DateSpan.c` (sec002)

The old "Time:" (combined ROM `SelectTime` start+end trigger) and "Date:"
(start-date-only) rows became **"Start:"** and **"End:"**, each a single
selector showing a combined moment ("11:00 pm Wed 8/12/26" — weekday kept,
year shrunk to 2 digits to fit the trigger width via a search-from-the-end
digit substitution in `DateSpanFormatLabel`, same idiom `era.c`'s own
`SubstYear` uses internally). Tapping either opens a new shared "Set Date &
Time" sub-dialog (`DateSpan.c`, new FORM 1360): a `Date:` row (era day
picker, `DatePickSelectDay`) + a `Time:` row (`SelectOneTime`, `SelTime.h`
— picks ONE moment, no start/end pairing, already proven elsewhere in this
suite) + a "No Time" checkbox (Start only — whole-event untimed toggle).
The ROM's own Set Time dialog is untouched — **zero risk to the two fast
same-day shortcuts** (digit quick-create, direct Day-View time-column tap),
which deliberately stay same-day-only.

`DetailsType` gained `UInt8 endDateOffset`. `DetailsApply`'s existing
3-way when/date change-detection gained a 4th "has End span changed?"
check, same `changedFields.endDate` + `dirty = true` pattern as everything
else there.

Max span: **30 days**, both here and in the query engine (§16.3) — bounds
the backward-lookback cost. End's date picker is clamped to
`[start date, start date + 30]` with an `FrmAlert` re-prompt
(`altSpanOutOfRange`) outside that range.

**Two real bugs found via live testing, both fixed:**
1. **Crash ("MemoryMgr.c Free handle") on the sub-dialog's OK** —
   `DateSpanEditMoment` never saved/restored the previously-active form
   around its own `FrmInitForm`+`FrmSetActiveForm`, unlike `DateLocation.c`'s
   `LocationEditForm` (the template this was modeled on). Fixed by adding
   the same `prev = FrmGetActiveForm()` / `if (prev) FrmSetActiveForm(prev)`
   pair every other hand-rolled dialog loop in this app already uses.
2. **End's picked time silently discarded on OK** ("/:i5 pm"-style garbage
   if opened right after Start turned an untimed event timed) — two
   compounding bugs: (a) the commit code only ever wrote `*timeP` back for
   Start, never End; (b) when Start's edit transitions untimed→timed,
   `endTime` was left holding the stale `apptNoTime` sentinel, formatted as
   garbage the moment End's dialog opened. Fixed at the source
   (`DetailsHandleEvent`'s Start case now defaults `endTime` to
   `startTime + 1hr` on that specific transition) plus a defensive clamp in
   `DateSpanEditMoment` itself.

**Continuation-day tap-to-Details routing — a third, subtler bug**,
found only once the query engine (§16.3) started actually placing a
spanning event's row on more than one day: `DetailsInit`'s
`details->when.date = Date;` unconditionally used the **currently viewed**
day as the staged start date — correct for viewing a specific occurrence
of a *repeating* event (where `Date` legitimately represents which
occurrence you tapped), wrong for a spanning event's continuation-day row
(where `Date` is just a display artifact of the same record bleeding onto
a later day, not a real "start"). Two sub-cases, both fixed in
`DetailsInit`:
- **Non-repeating**: trivial — if `endDateOffset && Date != apptRec.when->date`,
  use `apptRec.when->date` (the record's one and only real start) instead
  of `Date`.
- **Repeating + spanning — genuinely ambiguous without more context**: the
  same day can show *two* rows for the same record (yesterday's occurrence
  continuing past midnight, **and** today's own fresh occurrence) — `Date`
  alone can't tell which row was tapped, and `CurrentRecord` is the same
  for both. Fixed by threading **which row** was tapped: a new file-scope
  `gDetailsApptIndex` (sentinel `kDetailsApptUnknown`) is set alongside
  `CurrentRecord` at the two real tap-to-Details call sites
  (`tblEnterEvent`'s existing-event branch, `DayViewItemSelected`'s
  desc-column branch) — **not** at the other ~5 unrelated `CurrentRecord =`
  sites (Go-to, keyboard nav, etc.), because `DetailsInit` unconditionally
  clears it back to `kDetailsApptUnknown` right after reading it (one-shot
  consume) — any later Details-open via a non-tap path naturally finds it
  already unknown and falls back to the Date-based heuristic, so those
  other sites don't need touching. `DetailsInit` then compares
  `appts[gDetailsApptIndex].startTime` (the row's *clamped* display start)
  against `apptRec.when->startTime` (the record's real per-occurrence
  start-of-day time, same every occurrence): a mismatch means a
  continuation row, and it walks backward (same `min(endDateOffset,30)`
  bound as the query engine's own lookback) with `ApptRepeatsOnDate` to
  find which earlier date's occurrence it actually belongs to.

### 16.3 Query engine — `ApptGetAppointments` (`DateDB.c`, sec002)

This is the single shared function Day View (`days=1`), Week View
(`days=7`), and Agenda (`days=1` per day) all call — fixing it here is what
makes Week and Agenda "just work" with **zero changes of their own**
(verified live, not assumed).

New shared helper `ApptPlaceSpanning(occDate, span, startTime, endTime,
recordNum, anchorDate, days, apptLists, counts)`: places a (possibly
multi-day) appointment/occurrence into **every** day-slot of `[0,days-1]`
it overlaps, clamping per day — the day it really starts shows the real
start time; the day it really ends shows the real end time; any day
strictly between shows the whole day (`{0,0}`–`{23,59}`). A no-op (zero
iterations) if the span doesn't overlap the query window at all, so
callers never need to pre-filter.

Both scan loops needed a **backward lookback**, since a spanning
appointment/occurrence that started up to `min(endDateOffset,30)` days
before the query window can still span into it — the pre-existing
`ApptFindFirst(dbP, date, ...)` anchor and repeating `ApptNextRepeat` walk
both started exactly at `date`, missing this (not a regression — an
existing gap this feature closes):
- **Non-repeating**: anchor `ApptFindFirst` at `date - 30` instead of
  `date`. The sort-order early-`break` on "record's own start is past the
  whole window" stays valid (nothing later can matter either); the
  opposite "before the window → skip" case becomes "keep scanning, let
  `ApptPlaceSpanning`'s own overlap check decide" instead of an immediate
  break.
- **Repeating**: `ApptRepeatsOnDate(rec, date)` (already existed) is the
  cheap primitive that makes this tractable — no new repeat-math function
  needed: `for (back = 1; back <= min(endDateOffset,30); back++) { candidate
  = date - back; if (ApptRepeatsOnDate(&apptRec, candidate)) ApptPlaceSpanning(candidate, ...); }`
  runs *before* the existing forward `ApptNextRepeat` walk, and can never
  double-count against it (lookback candidates are always strictly before
  `date`, the forward walk starts at `date`).

`DayViewSelectTime`/`DayViewSaveDescription` (direct tap/edit on a Day View
row's time column) needed a continuation-day guard: on a continuation day
those `startTime`/`endTime` are the *clamped display* values, not the
record's real ones — a quick edit there would silently corrupt the real
start/end via `MoveEvent`. Same idea as this file's existing
`IsDayTodo`/`IsDayBday` sentinel-row guards: detect it and redirect to
Details instead.

### 16.4 Month View — `DateMonth.c` (`DrawTimedAppointments`, sec003)

Does **not** go through `ApptGetAppointments` — its own dedicated
`ApptFindFirst`+`DmQueryNextInCategory` scan. New shared
`DrawSpanningAppointment(monthP, occDate, span, startTime, endTime)`
(exact same per-day clamping idiom as `ApptPlaceSpanning`, §16.3) draws
the day-cell indicator on every day of the span that falls within the
visible month grid. Both scans' month-boundary logic loosened the same
way as §16.3: non-repeating anchors `ApptFindFirst` **30 days before** the
1st of the month (was exactly the 1st), so a span that *started* in the
previous month is still found; repeating adds the same backward
`ApptRepeatsOnDate` lookback. Verified live: a daily-repeating spanning
event marks every day of the month it's active (the day before it starts
is correctly blank), and an unrelated month with zero events near the
feature shows zero bogus markers (confirms the widened anchor doesn't leak
into unrelated months).

### 16.5 vCard export/import — `DateTransfer.c`

vCalendar's `DTSTART`/`DTEND` already natively support different dates —
not a format limitation, just needed wiring:
- **Export** (`DateExportVCal`, sec003): computes
  `endDate = when->date + endDateOffset` and emits `DTEND` on that date
  instead of always the start date — both the timed case (was already
  emitting `DTEND`, just on the wrong date) and the untimed case (a
  spanning untimed event now ALSO emits a date-only `DTEND`, previously
  omitted for any untimed event at all).
- **Import** (`DateImportVEvent`, main/`SEC_RECV`): now captures `DTEND`'s
  own date (was discarded, only the time was kept via `MatchDateTimeToken(tempP,
  NULL, &endTime)`) and derives `endDateOffset` from
  `DateToDays(dtendDate) - DateToDays(dtstartDate)`, clamped `[0,30]`,
  assuming `DTSTART` parses before `DTEND` (matches this function's
  existing field-ordering assumptions elsewhere).

**Implemented and code-reviewed only — not verified via an actual beam
round-trip** (needs two-device or loopback infrastructure not set up in
this environment). If a spanning event's beam-received `DTEND` ever looks
wrong, start here.

### 16.6 UI polish — colon alignment & sub-dialog geometry

Start:/End: label x-positions and the sub-dialog's Date/Time row
positions were **hand-tuned by the user directly in `forms.rcp`** after
this session's own tuning passes weren't quite right — **treat the
current `forms.rcp` as the source of truth for this geometry, don't
re-tune it unassisted** (same "static, hand-maintained" rule §6.1 already
establishes for the file as a whole).

**Pixel-alignment lesson** (a repeat offender in this session, worth
remembering for any future UI tuning pass in any of these apps): don't
*eyeball* a screenshot to judge proportional-font colon alignment — it's
easy to convince yourself two glyphs are "close enough" when a 7-9px error
is actually present. Measure it:
```python
from PIL import Image
im = Image.open(screenshot_path).convert("L")
px = im.load()
# scan each label's row for the x-column of its rightmost dark pixel
# (exclude known border/dotted-underline noise ranges), compare across
# labels that should align -- gives an exact pixel delta, not a guess.
```

### 16.7 Testing gotchas found this session (POSE64 + ReControl)

- **Week View's up/down time-scroll controls are REPEATBUTTONs**
  (`ctlRepeatEvent`, not `ctlSelectEvent`) — a plain `tap` (down+up
  immediately) does nothing; use `pen down`, sleep ~1-1.5s, `pen up -1 -1`
  to trigger the hold-driven scroll. All 7 day-columns share **one**
  synchronized vertical hour axis (0-23) — a spanning event's tail on its
  END day (e.g. 12am-1am) shows at the **top** of the grid, not the
  bottom, even though it visually started late at night on the *previous*
  day's column.
- **Reinstalling a rebuilt `.prc` over an app with existing REPEATING
  records, in this POSE64/AppImage setup, can silently drop the repeat
  flag on disk** (span/other fields stay intact, `flags.repeat` alone
  reads back false) — reproduced once, did not reproduce on a second,
  controlled same-session test (looks like an install/DB-reattach
  artifact, not an app logic bug — `ApptPack`/`ApptChangeRecord` were
  unchanged code at the time it happened). **Practical rule: after any
  reinstall-over-existing-DB, don't trust a repeating record's state from
  before the reinstall** — re-verify (or just recreate) it fresh in the
  new session before relying on it for further testing.
- Two crashes hit mid-session ("MemoryMgr.c Free handle", identical PC,
  following either a delete-then-immediate-tap or a
  details-save-then-immediate-repeatbutton-hold) were traced to the long
  test session's emulator state going stale, **not** a real app bug — the
  exact same reproduction sequence ran clean on a freshly restarted
  POSE64 instance. If this resurfaces under normal (non-scripted,
  non-rapid) use on a genuinely fresh install, treat it as real; don't
  assume it's guilty from a long-running emulator session alone.

### 16.8 Regression coverage

Single-day events (both plain and repeating) unaffected — verified
create/save/day-navigate/tap-to-Details all show correct unshifted dates.
The two fast shortcuts (digit quick-create, direct time-column tap) stay
same-day-only, produce plain non-spanning records. Sec35-encrypted
spanning event masking — user-confirmed working (the `ApptPrefixSize` fix
in §16.1 is what makes this correct). Beam round-trip not verified (§16.5).

## 17. CalDAV note-icon crash fix + "CalDAV Sync" Options-menu bounce (2026-09)

### 17.1 The crash — `ItemSelected` left stale after a note-icon tap on a DAV row

**Repro**: Day view, tap a CalDAV-synced row's note icon → Details → Done → tap any
*other* row. ROM panic: `DataMgr.c ... Index out of range`.

**Root cause**: DAV-synced (read-only) rows use a sentinel `CurrentRecord` range, not a
real `ApptDB` index — `#define IsDayDav(rn) ((rn) >= kDayDavBase && (rn) < kDayTodoBase)`.
`DetailsExit()` already guarded this (`if (IsDayDav(CurrentRecord)) ItemSelected = false;`,
added for an earlier fix in this same class of bug), but a direct tap on a DAV row's note
icon calls `DayOpenDavDetails(rn, toNote=true)`, which sets `CurrentRecord`/`ItemSelected`
and jumps **straight to `NewNoteView`**, bypassing `DetailsExit()` entirely. Every exit from
the note view (`Done`, hard-key save) only reached `NoteViewSave()` — never resetting
`ItemSelected` — so it stayed `true` with `CurrentRecord` still a DAV sentinel. The next tap
on any other row then skipped `DayViewClearEditState()`'s own `if (!ItemSelected) { ...
return; }` guard and fell through to `DmRecordInfo(ApptDB, CurrentRecord, ...)` with the
sentinel as the index.

**Fix** (`DateDay.c`, `NoteViewHandleEvent`'s `frmCloseEvent` handler): the same one-line
guard `DetailsExit()` already has —
```c
if (IsDayDav (CurrentRecord))
    ItemSelected = false;
```
Cost: 20 bytes (`sec001` went from 24 → 4 bytes free).

### 17.2 "CalDAV Sync" menu item — plain switch-to-app bounce

New Options-menu item in Day view, right after "Phone Lookup" (`forms.rcp`, `DatebookRsc.h`
`DayCalDavSyncCmd = 306`), dispatched in `DayViewDoCommand` (`sec001`) to a new
`DateDAVGoToSync()` in `DateDAV.c` (untagged/main — no data to hand off, unlike
`AddrBdayGoTo`'s `GoToParamsType`):
```c
DmGetNextDatabaseByTypeCreator(true, &st, sysFileTApplication, kCddvCreator,
        true, &cardNo, &dbID);
SysUIAppSwitch(cardNo, dbID, sysAppLaunchCmdNormalLaunch, NULL);
```
CalDAV Sync handles its own Sec35 unlock gate internally, same as any normal launch of it —
no special handling needed on this side.

**Segment fallout**: this pushed `sec001` (already at 4 bytes free after §17.1) over its
cap on the first build. Fix: relocated `RepeatDescRectHandler` (a `FormGadgetType`
callback, confirmed only ever invoked indirectly via `FrmSetGadgetHandler` — never called
directly by name in the same file, so no same-file-SEC00x-calls-untagged risk) from
`SEC001` to untagged/main. Final: `sec001` 96 bytes free, main 2216 bytes free.

**Phone Lookup → Address35**: done (2026-10) - see `Address35.md` §10. Both Datebook35 call
sites (Day view Options menu, Note view) call `A35LookupPhone` (`apps/lib/a35lookup.c`, main
segment) instead of the ROM `PhoneNumberLookup`.

## 18. CalDAV rows starting exactly at local midnight were never shown (2026-10)

`DateDAV.c` (`DateDAVGet`, `DateDAVGetOne`) finds a day's first synced event by binary search
(`DmFindSortPosition`) on `localStart`, then scans forward. `DmFindSortPosition` returns the
position *after* every record equal to the key, so searching with the day's own start
(`dayStartLocal`) skipped any record starting exactly at 00:00 — a timed event at midnight,
and, since CalDAV Sync's all-day fix (`CalDAVSync.md` §7 item 7), every all-day event. Fix:
`DavCompareByLocalStart` (only ever called by `DmFindSortPosition`, with the search key as
`rec1`) treats equal as "key first", so the search lands on the first record at or after
00:00. Done in the comparator rather than searching for `dayStartLocal - 1` because that
version added 8 bytes and filled `sec003` to exactly 0x8000; this one adds none. Native
appointments are unaffected (separate `ApptDB` code path, unchanged).
POSE64-verified in Day, Week and Month views at UTC+8, UTC+0 and UTC-5 alongside native
no-time, timed and "All Day" appointments, which stay put through every Prefs time zone change.

**Crash fixed in the same pass (pre-existing, found during POSE64 testing):** leaving Datebook35
(a hard button, Home, another app) while a CalDAV event's Details was still open was a fatal
"DataMgr.c, Index out of range". Details sets `CurrentRecord` to the row's sentinel and
`ItemSelected = true`; only `DetailsExit` clears `ItemSelected`, and an app switch never runs it,
so `StopApplication`'s `FrmSaveAllForms` reached `DayViewClearEditState`, which calls
`DmRecordInfo` on the sentinel. Fix: `StopApplication` drops the selection when `CurrentRecord`
is a sentinel (>= 0xE000) before saving, the beam-receive path (`sysAppLaunchCmdExgReceiveData`
while running, which also calls `FrmSaveAllForms`) does the same, and `ClearEditState`
(`Datebook.c`) treats a sentinel as "nothing to save" instead of reading or deleting it. All in
the main segment.

**Segment budget** (32,768-byte limit each, after the Phone Lookup work): sec001 0x7FA4 (92
free), sec002 0x7FE0 (32 free), sec003 0x7FF8 (8 free), main 0x7A74 (1,420 free). The
three secondary segments are effectively full, so new features beyond what main's ~1.4 KB can
hold need a fourth segment: add `"sec004"` to `Datebook35.def`'s `multiple code`
list and a `SEC004` macro in `Src/db35_seg.h` (CalDAV Sync uses four this way).
