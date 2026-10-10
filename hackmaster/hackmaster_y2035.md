# HackMaster Y2K35 — Extended Date/Time Plugin (Redesign)

## 0. Status

This document replaces the original (deleted) design, whose core scheme was
technically unbuildable. It reflects decisions made in design review:

- Representation: **64-bit seconds since the 1904 epoch + `Int32` year.**
- Re-basing: **switchable active era (1a)** with **anomaly special-casing for
  weekday exactness (2b)**.
- Two-tier model: a **legacy tier** (best-effort, for un-recompiled apps) and an
  **aware tier** (fully correct, for apps we recompile).

Companion file: `TEST_PLAN.md` (suites A–N).

---

## 1. The problem, stated correctly

Palm OS has **two independent** date representations, each with its own hard wall.
A hack can replace a trap's *implementation* but **cannot change its ABI or the
callers already compiled against it** — so neither wall can be widened for
existing code.

| Representation | Field | Wall | Used by |
|---|---|---|---|
| `UInt32` seconds since 1904 | 32-bit | **6 Feb 2040** → wraps to 1904 | system clock, `TimGetSeconds`, alarms |
| `DateType {year:7; month:4; day:5}` | 7-bit year | **2031** (1904+127) → wraps to 1904 | packed record storage (Datebook, ToDo) |

`DateTimeType` uses `Int16 year` (to 32767) but is only reachable through the
2040-walled seconds bridge, so it is never a usable wide path on its own.

Consequences that drive the design:

- You **cannot** make `TimGetSeconds()` return 64 bits — callers read one 32-bit
  register and store into `UInt32`. 64-bit is only available through a **new API**
  that recompiled apps call.
- Datebook packs/unpacks the 7-bit year **inline**, before any trap runs, so a
  hack cannot change what a legacy app stores or displays as the year.
- The best a hack can do for legacy code is **re-base**: keep every value inside
  the legal window so the catastrophic "everything → 1904" failure becomes a
  benign "correct month/day/weekday, re-based year."

---

## 2. Architecture: two tiers

```
┌─ datelib  (shared, unit-tested 64-bit calendar engine, Int32 year) ─────────┐
│      used by ▼            used by ▼              mirrored in ▼                │
│   Datebook+ (Phase 2)   ToDo+ (Phase 2)     HackMaster plugin (Phase 1)      │
│   full range, own       full range, own     legacy re-base + hardening +     │
│   storage + display     storage             64-bit master clock + era mgr    │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.1 Legacy tier (this plugin, system-wide, best-effort)
- Keeps the 32-bit RTC **re-based** inside 1904–2031 so ROM and third-party apps
  never see the wrap.
- **Guarantee:** MM/DD, Feb 29, and weekday are correct for any date in the
  **active era**; the **year is re-based** (i.e. "wrong on purpose") whenever the
  real year > 2031.
- Hardens the 2040 boundary so nothing teleports to 1904.

### 2.2 Aware tier (Phase 2 apps on `datelib`)
- Read the plugin's **64-bit master clock**; store their own dates with `Int32`
  year. No re-basing. Both overflows simply do not exist for them.
- **Multiple eras coexist** here for free (each record holds its true year).
- Residual limit: `AlmSetAlarm` is 32-bit, so only in-era alarms can be
  OS-scheduled; far-future reminders are self-managed by the app.

---

## 3. Re-basing model (legacy tier)

### 3.1 Definitions
- **Anomaly year**: divisible by 100 but not 400 (non-leap century): 2100, 2200,
  2300, 2500, … These break the 4-year leap regularity.
- **Regular run**: a maximal span with no interior anomaly. Between anomalies the
  calendar is Julian-regular and the 28-year type cycle holds. The window
  [1904, 2031] is entirely regular (2000 is ÷400 → normal leap).
- **Year-type**: (weekday of Jan 1, leap/non-leap). 14 types total; a 28-year
  regular cycle contains all 14 exactly once, so the 128-year window holds each
  type ~4×.

### 3.2 Offset by year-type match (delivers 2b)
For a set real year `Y > 2031`:
1. `datelib` computes `Y`'s full-Gregorian year-type.
2. Find `Y'` in [1904, 2031] with the **same type** (always exists).
3. `offset = Y − Y'`. A single constant offset keeps **MM/DD + Feb 29 + weekday
   exact** across the active era, because era and window are both regular and
   matched at one point.

### 3.3 Active era & switching (delivers 1a)
- The **active era** is the anomaly-bounded, window-fit range around the set year.
- Only **one era is live** in the legacy view at a time. Setting a date in another
  century recomputes the offset (switch).
- **Seams:** a real one-year arithmetic seam at each century-anomaly boundary
  (accepted); anomaly years are non-leap singletons with their own offset; the two
  199-year ÷400 runs exceed the 128-year window, so navigating >128 years from the
  set date triggers a **weekday-neutral** 28-year-shifted offset (MM/DD/weekday
  unaffected).

### 3.4 Why not a single global offset
The Gregorian period is 400 years; the window is 128. A fixed whole-year offset
can be weekday-exact for at most ~32% of years. Per-era offsets (3.2) restore
exactness within each era; simultaneous multi-era correctness is impossible in
32-bit legacy storage (a based date carries no era tag) and therefore lives only
in the aware tier.

---

## 4. State & persistence

No globals are allowed in a hack. State lives in a preferences database and is
published for aware apps via the Feature Manager.

```c
typedef struct {
    Int64  masterSeconds;   /* true seconds since 1904 epoch (64-bit)      */
    Int32  activeOffset;    /* real - based, in whole years                */
    Int32  eraRealLow;      /* active era bounds (real years)              */
    Int32  eraRealHigh;
    Int32  masterYear;      /* Int32 year cache for large-year picker      */
} HmEraState;
```

- **Persist**: `PrefSetAppPreferences(creator, …)` — survives soft reset.
- **Publish (live)**: `FtrSet(creator, ftrOffset, activeOffset)` and a locked
  chunk holding the 8-byte `masterSeconds`, whose pointer is in
  `FtrSet(creator, ftrMasterPtr, ptr)` (a Feature value is only 32 bits).
- Legacy RTC itself persists in hardware; the plugin re-seeds it when the based
  clock creeps toward 2031.

---

## 5. HackMaster resource layout (per `hackapi.htm`)

Database type **`HACK`**, unique creator id. Built with prc-tools `hack` project
kind; each patch is a standalone code resource (`-nostartfiles`,
`HACKMASTER_TRAP(vector)` from `Standalone.h`), yielding matched `code`/`TRAP`
pairs numbered from 1000.

| Resource | ID | Purpose |
|---|---|---|
| `code` + `TRAP` | 1000 | patch `TimSecondsToDateTime` (hardening) |
| `code` + `TRAP` | 1001 | patch `DateSecondsToDate` (hardening) |
| `code` + `TRAP` | 1002 | patch `TimGetSeconds` (guard) |
| `code` + `TRAP` | 1003 | patch `TimSetSeconds` (guard) |
| `code` | 1500 | (optional) custom install/reset routine |
| `tFRM` | 2000 | control panel (date/time era picker) |
| `code` | 2000 | control-panel event handler |
| `tAIN` | 3000 | name shown in the X-Master list |
| `tFRM` | 3000 | (optional) about box |

**Chaining to the original trap** (`hackapi.htm`): each patch, every call, does
`FtrGet(creator, resID, &addr)` to fetch the original address and jumps through
it. Never cached (may point at the next hack in the chain).

Correct trap names/signatures (from `hacktrap.txt` / `DateTime.h`):
`TimSecondsToDateTime(UInt32, DateTimeType*)`, `TimDateTimeToSeconds(DateTimeType*)`,
`DateSecondsToDate(UInt32, DatePtr)`, `DateDaysToDate(UInt32, DatePtr)`,
`DateToDays(DateType)`, `TimGetSeconds()`, `TimSetSeconds(UInt32)`.

---

## 6. Control panel UI

Reached via the `+` icon in the X-Master list (`FrmGotoForm(2000)`).

- **Date picker**: day / month / **year** with independent-digit entry so the user
  can key arbitrary years (5+ digits) past the native range.
- **Time picker**: HH / MM / SS.
- **Set**: validate → store truth to `masterSeconds` → compute type-match offset →
  pre-set legacy RTC to the based moment via original `TimSetSeconds` → persist +
  publish.
- **Echo**: reopening the panel shows the real (un-re-based) date it was given.
- Handler obeys HackMaster CP protocol (`frmOpen`/`frmClose`, `FrmGotoForm(9000)`
  to exit); no globals — reads state from prefs each event.

---

## 7. Trap behavior (hardening only)

Because the RTC is pre-set to a legal based value and legacy records are already
based, the patches do **not** re-base; they **harden**:

- `TimSecondsToDateTime` / `DateSecondsToDate`: call the original via `FtrGet`;
  if the input is at/over the 2040 boundary, clamp instead of allowing the wrap to
  1904, so MM/DD stay meaningful (INV-1, INV-5).
- `TimGetSeconds` / `TimSetSeconds`: guard against a caller pushing the RTC past
  the window; keep the counter inside the legal range.
- On any internal error, do nothing but chain to the original (hacks must not
  crash — `hackapi.htm` §Error handling).

---

## 8. `datelib` (shared engine)

Pure, global-free calendar math over 64-bit seconds and `Int32` year; compiled
into the plugin and (Phase 2) into the apps.

- `Int64 dl_dateToSeconds(Int32 y, Int16 mo, Int16 d, Int16 h, Int16 mi, Int16 s)`
- `void  dl_secondsToDate(Int64 secs, DlDateTime *out)`
- `Int16 dl_dayOfWeek(Int32 y, Int16 mo, Int16 d)`  (full Gregorian)
- `Bool  dl_isLeap(Int32 y)` / `Int16 dl_daysInMonth(Int32 y, Int16 mo)`
- `Int8  dl_yearType(Int32 y)` (0–13) and `Int32 dl_matchInWindow(Int32 y)`
- `Int32 dl_reBaseOffset(Int32 realYear)` → whole-year offset per §3.2

Unit-tested host-side against an external oracle (suite A / L).

---

## 9. Build

Toolchain: `m68k-palmos-gcc`, `pilrc`, `m68k-palmos-obj-res`, `build-prc`;
SDK `palm-os-sdk/sdk-4/include`; `-palmos3.5`. Each patch `.c` links standalone
(`-nostartfiles`); `pilrc` builds the `tFRM`/`tAIN`; `build-prc` with the `hack`
kind bundles code/TRAP pairs + UI resources into the `HACK` prc.

## 10. Test & verify

POSE (pose64) + Palm III 4.1 ROM. Install `X-Master-Light.prc` + the plugin,
enable it, exercise via the control panel and the **DateProbe** harness app.
Success criteria and cases: see `TEST_PLAN.md`.

## 11. Phase 2 (out of scope here)

Rewrite Datebook / ToDo against `datelib` with wide-year storage and direct true-
year display; they read the 64-bit master clock and reconcile via the published
offset. This is where full correctness (year included) and simultaneous multi-era
are delivered to the user.
