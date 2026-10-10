# McPhling4 — build fix & "Sel." feature (reference & rebuild guide)

This document covers what it took to get **McPhling** (a third-party PalmOS
"App Hack" launcher by Mike McCollister, GPL, © 2000-2009) building again from
its original 2009 source under this toolchain, plus a new feature added on top
of the original. Paths are relative to
`hackmaster_y2035/apps/McPhlingSource/`. Target is **PalmOS 4.1 on Palm
IIIx/IIIxe only** — no HandEra/Sony/palmOne/OS5 device support is needed or
attempted.

---

## 1. What McPhling is

A **HackMaster "App Hack"** (`APP_TYPE 'HACK'`, requires HackMaster/X-Master
to load) that adds an MRU/favorites app-launcher popup list, triggerable by a
configurable screen swipe or hardware button. The source builds two totally
separate targets from the same tree:

- **`out/McPhling4.prc`** — the PalmOS 4.x Hack (`APP_ID 'McPg'`). **This is
  the only target this project builds/supports.**
- **`out/McPhling5.prc`** — a standalone OS5-only app. Not built, not
  supported, has its own pre-existing unrelated compile error
  (`McPhling.c:93`) — out of scope.

Build just the 4.x target:
```
make out/McPhling4.prc
```
Plain `make`/`make all` also attempts McPhling5 and the PalmGear-branded
copies — don't use it. `make` won't recompile a target it thinks is already
up to date; if source changed but the old `out/McPhling4.prc` sticks around,
`rm -f out/McPhling4.prc` first.

The code is split into several PalmOS **code resources**, each its own `.c`
file, glued together by `build-prc --hack`:
- `common.c` — shared helpers, compiled **13 times** (`-DLIBCOMMON_00`
  .. `-DLIBCOMMON_13`, one function-group per pass) into tiny separate `.o`s
  archived into `obj/libcommon.a`, so `code1000.o`/`code2000.o` only pull in
  the specific functions they actually call (keeps each hack code resource
  small).
- `code1000.c` — the **`SysHandleEvent` trap patch** (`HACKMASTER_TRAP`,
  resource ID 1000). Runs system-wide once the hack is enabled; owns swipe
  detection, hardware-key detection, and the popup list's own event loop
  (`MyLstPopupList`).
- `code2000.c` — the **Prefs/Activation code resource** (`STANDALONE_CODE_RESOURCE_ID`,
  resource ID 2000). HackMaster jumps here when the user taps "Configure" in
  X-Master.
- `McPhling.c` / `McPhlingVFS.c` — small helper "resource ID 0" launcher
  stubs, not touched.

---

## 2. Getting it to compile at all

The original 2009 source needs three now-unavailable vendor SDKs (HandEra
1.04, palmOne 5.1, Sony CLIE 5.0) plus a couple of PalmOS-5-only APIs. Given
the PalmOS-4.1-only target, none of that hardware/OS needs to actually work —
it just needs to compile and behave as "feature not present" at runtime,
which is what a real IIIx/IIIxe would report anyway.

### 2.1 CRLF line endings — the actual root cause of the first wall of errors
All source/resource files had **Windows CRLF** line endings. This 1995-vintage
gcc (2.95.3) doesn't treat `\<CR><LF>` as a valid macro line-continuation —
only `\<LF>` — so every multi-line `#define` (e.g. `SONY_ALL_SHIFT_KEY_MASKS`
in `McPhling.h`) silently corrupted into garbage tokens, cascading into
dozens of unrelated-looking parse errors. Fixed once, permanently:
```
sed -i 's/\r$//' src/*.c src/*.h src/*.rcp src/*.rch
```

### 2.2 Two genuinely dead vendor includes — just removed
`common.c`'s `#include <Sony/SonyCLIE.h>` and `common.h`'s
`#include <HandEra/Vga.h>` were never actually used by any code in either
file (checked: zero references to Sony/HandEra symbols outside the include
lines themselves). Deleted.

### 2.3 Vendor/OS5 stand-ins — `common.h` prototypes + `common.c` LIBCOMMON_13 bodies
Everything else missing (Vga screen-mode calls, Sony SilkLib, PalmSource Pen
Input Manager collapsible-Graffiti calls, two newer `PalmOSGlue` functions,
~40 vendor virtual-key/event constants) is **always called behind a runtime
feature-presence check** (`_TRGVGAFeaturePresent`, `GetCollapseGraffiti`,
`SysLibFind`) that correctly reports "absent" with no such hardware present
— so the call bodies are unreachable dead code on our target. Stand-ins:
- Real values were used where they're genuinely just newer-SDK constants
  (`willSendUpKeyMask`, the 5-way rocker keys, `winDisplayChangedEvent`,
  the PenInputMgr `pin*` feature-lookup constants) — copied from the OS5 SDK
  headers present in this toolchain (`sdk-5r4`), so if this ever *did* run on
  real matching hardware it would still behave correctly.
- Purely vendor-proprietary constants with no accessible reference (HandEra
  TRG jog keys, Sony jog keys, Treo `hsChr*` keys, HandEra Vga screen-mode
  enums) got safe **high sentinel values** (`0xF1xx` range), clearly commented,
  chosen to never collide with any real PalmOS virtual key/event code.
- One function, `LstGlueGetItemsText`, **is** load-bearing (populates the
  Activation form's "keyboard back" list) — given a real implementation using
  `ListType->itemsText`, directly accessible in this SDK
  (`ALLOW_ACCESS_TO_INTERNALS_OF_LISTS`, on by default per `BuildDefaults.h`).

**Critical placement rule, found the hard way (§3): these bodies MUST live in
`common.c` under `#ifdef LIBCOMMON_13`, never as `static` functions inlined
into `common.h`.** `common.h` is included by `code1000.c`/`code2000.c`, which
are raw HackMaster code resources (see §3) — a `static` function body in a
shared header gets compiled directly into *each* including file's own object,
which can silently reorder that file's compiled code and break HackMaster's
entry-point convention. `common.h` therefore only has plain (non-`static`)
prototypes + the `#define`d constants; `common.c`'s `LIBCOMMON_13` block has
the real bodies, linked in normally via `-lcommon`.

---

## 3. The entry-point bug — "Configure" loaded the form but drew nothing

**Symptom:** After the fixes in §2, the build succeeded and the hack
installed/enabled without crashing, but tapping "Configure" in X-Master
showed a **blank screen** — `ui`-style introspection confirmed FORM 2000 was
genuinely active with the right resource-loaded object tree, but nothing was
ever painted.

**Root cause:** `code1000.c` (`MySysHandleEvent`) and `code2000.c`
(`PrefsEventHandler`) are **HackMaster raw code resources** — the
`HACKMASTER_TRAP`/`STANDALONE_CODE_RESOURCE_ID` macros (`Standalone.h`, part
of prc-tools itself) only tag the resource's **ID** and trap **vector** via
special linker sections (`.disposn`/`.trap`); they say nothing about code
*order*. HackMaster's own convention (undocumented in this source, discovered
by symbol-table inspection) is to jump directly to **whatever function
compiles first** in that resource — there's no by-name lookup. Before this
session's fix, my first attempt at §2.3 put the vendor-SDK stub function
*bodies* directly in `common.h` as `static` functions. Since `common.h` is
`#include`d near the top of both `code1000.c` and `code2000.c`, this old
1995-era gcc (which doesn't reorder functions) compiled those stub bodies
*before* the real entry points (`MySysHandleEvent`, `PrefsEventHandler`) in
each file's own object — so HackMaster was jumping straight into a harmless
stub (e.g. `_TRGVGAFeaturePresent`, which just returns `false` and returns)
instead of the real handler. No crash, because the stub is valid code — just
nothing useful happened.

**Fix:** moved every stub body out of `common.h` into `common.c`'s new
`LIBCOMMON_13` block (see §2.3) as ordinary external functions. Verified via
symbol table that both real entry points are back at offset 0:
```
$ m68k-palmos-nm obj/code1000.o | grep " T " | sort -k1 | head -1
00000000 T MySysHandleEvent
$ m68k-palmos-nm obj/code2000.o | grep " T " | sort -k1 | head -1
00000000 T PrefsEventHandler
```
**Lesson for any future change to these two files:** never add a `static`
function to a header they include (directly or transitively) without first
checking it lands after, not before, the real entry point in the compiled
object — or just always put new shared code in `common.c` instead.

---

## 4. New feature: "Sel." — hardware-button confirm for the app-list popup

### 4.1 What it does
The Activation form's existing "Buttons/Keyboard" section already has "Apps:"
(bring up the popup list) and "Back:" (jump straight to the last app). Added
a third row, **"Sel.:"**, letting you assign one of the device's 4 hardware
app-launch buttons to **confirm whatever item is currently highlighted** in
an already-open popup list — the hardware-button equivalent of tapping the
highlighted row, so the whole launch flow (bring up list → move highlight →
launch) can be done without touching the screen at all.

The user had already sketched the resource-side UI (a `POPUPTRIGGER`/`LIST`
labelled "Sel.:" offering Disabled + Hard Key #1-4) but it was incomplete: no
`POPUPLIST` binding (so tapping it did nothing) and none of the C-side
plumbing existed. This session completed and wired it.

### 4.2 Data model — `McPhling.h`
```c
#define KEY_APP_SEL (SHRT_MAX - 3)   /* alongside the existing KEY_APP_LIST/LAST/FILTER */

typedef struct {
   ...
   KeyInfoType keyInfoApps;
   KeyInfoType keyInfoBack;
   KeyInfoType keyInfoSel;   /* new */
} McActivation;
```
`McActivation` is persisted as a raw `sizeof()`-tagged blob
(`PrefGetAppPreferences`/`PrefSetAppPreferences`); `GetActivation`
(`common.c`) already resets to defaults whenever the stored size doesn't
match the current struct size, so appending a field is a safe, automatic
one-time-reset upgrade — no migration code needed. Default: `chr = chrNull`
("Disabled"), matching the existing "0 = disabled" convention used by
`keyInfoApps`/`keyInfoBack`.

### 4.3 Settings UI — `McPhling.rcp` + `code2000.c`
- `POPUPLIST ID ActPopupTriggerKeyboardSel ActListKeyboardSel` added (the
  missing binding) + `NONUSABLE VISIBLEITEMS 5`.
- `ActivationUpdateForm`: populate/show-hide the Sel. row exactly like the
  existing Apps:/Back: blocks (same `ConvertXListToKey(..., true)` +
  `LstSetSelection`/`CtlSetLabel`/`FrmShowObject` pattern).
- `ActivationGetData`: reads `ActListKeyboardSel`'s selection back into
  `keyInfoSel` **unconditionally** (unlike Apps:/Back:, which conditionally
  skip re-reading to protect an in-flight "Custom..." pick — Sel. has no
  Custom option, so there's nothing to protect).
- New `ConvertSelListToKey()` (`code2000.c`, mirrors the existing
  `ConvertButtonListToKey`): a small local lookup table, Disabled + Hard Key
  #1-4, **using the "Legacy" hard-key shape** (see §4.5) since that's what
  matches this project's actual target hardware.

### 4.4 Runtime trigger — `code1000.c`
- `CheckSpecificKey()`: added `selChr`/`selKeyCode`/`selMod` alongside the
  existing `aChr`/`bChr` triple, with its own comparison branch returning
  `KEY_APP_SEL` on a match. No Hold-checkbox/repeat-wait logic (Sel. doesn't
  have one) — straight compare against `mod & STATIC_MODIFIERS` (the
  `poweredOnKeyMask`-filtered `mod`, **not** the raw `keyDown->modifiers` —
  a hard app-launch button commonly also wakes the device, and that bit must
  not be allowed to break the match).
- `MyLstPopupList()`'s own `keyDownEvent` switch (the loop that runs *while
  the popup list is open*) already has a `default:` case for unrecognized
  keys; added a check there for `CheckSpecificKey(...) == KEY_APP_SEL`,
  doing exactly what the existing hardcoded confirm keys do (jog push,
  Enter, Space): `sel = newSel; bLoop = false;` — launches whatever's
  currently highlighted.
- Auto-repeat filtering (the existing `if(CheckSpecificKey(...))` truthy
  check earlier in the same loop) needed no change — `KEY_APP_SEL` is
  non-zero, so it's already covered generically.

### 4.5 The "Legacy" hard-key discovery — read this before changing key tables
Live-tested via X-Master's own "Custom..." key-capture dialog (Activation
form → Apps: → Custom... → press the real hardware button): **this
project's target ROM (Palm III / 4.1) does not duplicate the vchr into
`keyCode` and does not set `willSendUpKeyMask` for a hard app-launch button
press.** McCollister's own `ConvertButtonListToKey` table already
anticipated this split with parallel "Hard Key #N" (`keyCode = vchrHardN`,
`willSendUpKeyMask | commandKeyMask`) vs "Legacy: Hard Key #N"
(`keyCode = chrNull`, just `commandKeyMask`) entries — Apps:/Back: expose
both as separate picker choices covering either ROM generation.

`ConvertSelListToKey`'s table deliberately encodes **only** the Legacy shape
(no separate "modern" entries) because this project only targets Palm
IIIx/IIIxe — confirmed live: "Hard Key #2" (modern shape) silently did
nothing when the real button was pressed; "Legacy: Hard Key #2" fired
correctly. If this project's target hardware ever changes, re-verify via the
same Custom... capture trick before assuming either shape.

### 4.6 Verification performed
- Settings UI: row shows/hides correctly with "Buttons/Keyboard", selection
  persists across Activation OK / Prefs OK / reinstall.
- Confirmed no layout overlap with the OK/Cancel/Default button row.
- End-to-end live test (POSE64 + real hardware-button emulation via
  ReControl's `button appN tap`, not just synthetic key events): bring up
  the popup via Apps: (Legacy Hard Key #4), highlight an item, press the
  configured Sel. key (Legacy Hard Key #2) — the highlighted app launches
  without any screen tap. User additionally confirmed working manually.

---

## 5. Testing gotchas specific to this app (POSE64 + ReControl)

- **A HACK's trap patch must be disabled (X-Master checkbox unchecked)
  before reinstalling its `.prc`** — installing over an active trap patch
  either fails outright or, worse, can corrupt the running instance (hit a
  real "illegal instruction" crash doing this once). Always: disable →
  install → re-enable.
- **ReControl's plain `key <charcode>` command cannot set modifier bits**
  (`commandKeyMask` etc.) — every entry in McCollister's key-binding tables
  requires at least `commandKeyMask`, so this command cannot test *any*
  Apps:/Back:/Sel. hardware-key binding. Use `button app1..app4|up|down tap`
  instead — those go through POSE64's actual emulated Port-D
  interrupt-driven button hardware (`EmRegsVZ`/`EmRegs328`/`EmRegsEZ`), the
  same code path a real hard-key press takes, producing a realistic event.
- **A `button appN tap` that isn't intercepted by a hack's trap patch still
  produces a visible, but misleading, result**: the ROM's own default
  hard-key launch action fires (e.g. Hard Key #4 → Memo Pad opens). This
  looks like "my code didn't match" but is *indistinguishable* from "the
  trap patch isn't installed at all" — both produce the exact same default
  behavior. Don't conclude a match failure from this alone; use the
  Custom... capture dialog (§4.5) to positively confirm what event shape is
  actually being generated.
