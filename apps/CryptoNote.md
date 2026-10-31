# CryptoNote — SD-card notes on CryptoPad (reference & rebuild guide)

This document captures everything needed to understand and rebuild **CryptoNote**,
an **additive** extension of the AES-modified **CryptoPad** that keeps all of
CryptoPad's Data-Manager encrypted notes **and** adds reading, editing and
per-file encryption of plain-text files on an **SD card** (VFS). Nothing about
the original DB-notes behaviour is removed — the card is a new *source* selected
from the category list, side by side with the built-in notes.

Paths are relative to `hackmaster_y2035/apps/cryptonote/`. Origin:
`CryptoPad_v365_src/src/` (the AES-128-CTR + device-bound-KDF fork of CryptoPad);
CryptoNote-specific changes are marked `CryptoNote:` in comments. Creator renamed
`CrPd` → `CrNt`; app name `CryptoNote`; own DB `CryptoNoteDB` (the MemoDB source
stays `MemoDB`).

Target: **Palm OS 4.1 minimum** (needs VFSMgr / ExpansionMgr, native on m5xx).

> **Note — crypto is independent of the Sec35 suite.** CryptoNote inherits CryptoPad's
> own AES-128-CTR + device-bound-KDF, per-record / per-file, with its own password
> dialog. It is **not** wired to **Sec35** (the shared-passcode authority the three PIM
> apps ToDo35 / Datebook35 / Address35 use). Don't conflate the two: no shared key, no
> `EncGetKey`/`lib/enccore`, no Forget-Password wipe / rekey coupling.

---

## 1. What CryptoNote is

A superset of CryptoPad. Two note *sources*, chosen from the category popup:

- **DB notes** (unchanged CryptoPad): `MemoDB` + `CryptoNoteDB`, categories,
  per-record AES encryption, beam, find, details, fonts — all as before.
- **Card notes** (new): the **"Card"** entry appears in the category popup
  **only when an SD card is mounted** (Mgr1-style), separated from the generic
  categories by a divider line. Selecting it browses `/Notes` on the card:
  - Folders are shown with a folder icon and navigated by tapping; a `[..]` row
    (up-arrow icon) ascends, floored at `/Notes`.
  - Files (`.txt` / `.md`, and their encrypted `.enc` form) are listed **numbered
    like memos** (`1. 2. 3.`); an encrypted file shows the CryptoPad **key icon**
    right-aligned and hides the `.enc` suffix.
  - Tapping a file opens it in the **same Edit form** as DB notes.
  - Files can be **encrypted per-file** (AES, identical model to the DB notes)
    to `name.ext.enc`, or transparently edited when already encrypted.

The app creates `/Notes` itself on first use; no pre-provisioning needed.

---

## 2. Build & segmentation — READ THIS FIRST

### 2.1 Always build with `-palmos4`
Build with plain **`make`** (the Makefile default is `PALMOS_FLAG ?= -palmos4`).
**Do NOT run `make PALMOS_FLAG=`** (empty). CryptoNote is multi-segment, and
*without* `-palmos4` gcc emits a **near `bsr.w` (DISP16)** for a `main`→`sec001`
call whose target is defined in a **newly-added object file** (CardCrypt.o, and
any card function moved to `sec001`). At runtime `sec001` loads at a different
address, so the 16-bit displacement lands in garbage → a spurious Fatal Alert
("DataMgr.c Line 9573 Index out of range", "Fatal Exception") the instant that
function is called. With `-palmos4` the call becomes the correct far sequence
(`move.l __text__sec001@END.w(%a5),%aN; add.l #func,%aN; jsr (%aN)`, reloc **32**).
Verify: `m68k-palmos-objdump -r EditForm.o | grep CardCryptEncrypt` must show a
**32**, never a **DISP16**, on a main→sec001 call.

The `PALMOS_FLAG=` empty override exists only for environments where
`palmdev-prep` can't write specs; it builds here, so use `-palmos4`.

### 2.2 Segment map (main must stay < 32 KB)
`.def`: `multiple code { "sec001" }`. The main segment is chronically near full,
so foreground-only code is pushed to `sec001` (tagged `AES_SECTION`, from
`sects.h`, on the forward declaration — gcc propagates it to the definition):

- **main**: everything reachable from a no-globals sublaunch (beam receive, find)
  and any code doing **32/64-bit division** (libgcc lives in main and is
  unreachable from `sec001` — the [libgcc gotcha]). Includes **all of CardIO.c**
  (card VFS wrappers — kept in main because it's called from the category-popup
  hot path via `CardPresent`, and it is VFS-heavy), the category-popup builder
  `MainFormSelectCategory` + `CardCatListDraw`, the DB note code, and any row/table
  math that divides by `FntLineHeight()`.
- **sec001** (all tagged `AES_SECTION` on the **forward declaration** — gcc
  propagates the section to the definition): `AesCrypt.c`, `aes.c`, `Beam.c`
  (send), CryptoNote's `CardCrypt.c`, **`CryptoSec35.c`** (Sec35 wrap/unwrap +
  the streaming card-file import engine — see §4.2), and the foreground-only
  card functions in `MainForm.c`: `CardIsTxt`, `MainFormLoadCardDir`,
  `MainFormSortCardEntries`, `MainFormLoadCardOrder`, `MainFormDrawCardRow`,
  `MainFormOpenCardFile`, `MainFormNewCardNote`, `MainFormCardEncryptAll`,
  `MainFormCardDecryptAll`, `MainFormCardWriteOrder`, `MainFormCardReorderFile`,
  `MainFormCardDrag`. These either do only power-of-2 division (`/2` → a shift,
  libgcc-safe) or none, and are never reached from a no-globals sublaunch.
  `EditForm.c` itself is **NOT** `AES_SECTION`-tagged (its own functions stay in
  main) — code that must live alongside `EditForm.c`'s logic but needs sec001
  headroom is instead exposed as small helper functions in `CryptoSec35.c` that
  `EditForm.c` calls across the segment boundary (e.g. `CryptoEditApplyGuard`/
  `CryptoEditClearGuard`/`CryptoEditGuardBlockKey`, §4.2) — main was down to
  **8 bytes free** the one time this logic was tried inline in `EditForm.c`.

Four separate multi-seg traps to remember (all in memory): the `-palmos4` flag
(§2.1), the [forward-decl must be tagged] rule, the [libgcc division] rule, and
a same-segment **DISP16 link failure**: two `AES_SECTION` functions in
*different* `.c` files can still fail to link (`relocation truncated to fit:
DISP16`) even with plenty of segment budget free, because gcc doesn't know
they'll end up co-located until link time and may still emit a near branch.
Fix is a tiny deliberately **un-tagged** (plain main-segment) relay/stub
function in between — a genuine cross-segment call always uses the safe
indirect-jump form regardless of distance, so relaying through main sidesteps
the question entirely. (`multiseg-same-segment-disp16-gotcha` memory.)

### 2.3 Files
- **CardIO.h/.c** — thin VFSMgr wrapper (the only place that talks to VFS). API:
  `CardHasVolume`, `CardFindVolume`, `CardEnsureDir`, `CardListDir` (fills a
  `CardEntry[]`), `CardReadFile`, `CardWriteFile`, `CardExists`, `CardDeleteFile`,
  `CardRename`, `CardJoinPath`, `CardParentPath`. Returns Palm `Err`s. `CardIO.h`
  includes only `<PalmOS.h>` (NOT `<VFSMgr.h>` — pulling VFSMgr.h into some
  callers' include context triggers a trap-macro parse error; it's included only
  by `CardIO.c`).
- **CardCrypt.h/.c** — file encryption container + the password dialog. `sec001`.
- **CryptoSec35.h/.c** — Sec35 suite-wide-passcode wrap/unwrap for DB records
  (`CryptoGetRecord`/`CryptoGetRecordFlag`/`CryptoSealRecord`/`CryptoLockedBounce`
  etc.) plus the streaming "Import from Card" engine (`CryptoImportFromCardFile`/
  `CryptoImportFromEncryptedCardFile`) and the too-large-to-edit guard
  (`CryptoRecordTooLargeToEdit`/`CryptoEditApplyGuard`/`CryptoEditClearGuard`/
  `CryptoEditGuardBlockKey`). All `sec001`. Must `#include <VFSMgr.h>` as its
  own first include after `<PalmOS.h>` (see the `CardIO.h` note above) for its
  direct VFS calls in the import path. See §4.2 and the
  `cryptonote-sd-card-phase5` memory for full history/gotchas.
- **EditForm.h/.c** — the shared editor; card-file bridge globals + load/save.
- **PasswordForm.h/.c** — the app password form (masking with a short reveal, like Sec35's)
  and the numeric keypad popup (`frmKeypadForm`), used when Sec35's Preferences > Password
  Type is numeric (`EncGetPasswordEntryType`). A near-verbatim port of Sec35's own keypad in
  `sec35.c`. Its digit/Del keys are GADGETs, which get no automatic Control Manager tap
  click, so the pen-down handler plays `SndPlaySystemSound(sndClick)` itself, as Sec35's
  does (honours the System Sound preference; added 2026-10-14, user-tested on hardware
  2026-10-15). `sec001` (`AES_SECTION`).
- **MainForm.c** — category popup with "Card", the card browser + navigation.
- Icons: `card.bmp` (8×11 — `bmpCardIcon` 2003; a pixel-for-pixel copy of the ROM
  launcher's "card" glyph), `folder.bmp` (`bmpCardFolder` 2000), `cardup.bmp`
  (`bmpCardUp` 2001, up-arrow); the encrypted-file key reuses CryptoPad's existing
  `bmpMainKeyBitmap`/`key.bmp`. (`fbmpEditKey` 2012 is defined but unused — the
  editor key is drawn manually, see §4.1.)

### 2.4 Build output & `.def`
Plain **`make`** produces **`CryptoNote_v1.prc`** — the name is
`$(EXE)_v$(VERSION).prc` with `EXE = CryptoNote`, `VERSION = 1` (bump the variable
for a new build). `CryptoNote.def`:
```
application { "CryptoNote" CrNt backup ok-to-install-newer }
multiple code { "sec001" }
```
`m68k-palmos-multigen CryptoNote.def` emits `CryptoNote-sections.{s,ld}`; the app
links against the `.ld`; `build-prc -o CryptoNote_v1.prc CryptoNote.def CryptoNote
*.bin` packages it (pilrc emits `Main<LOC>.rcp` → `.bin` resources first).

---

## 3. Card source in the category popup (`MainForm.c`)

`MainFormSelectCategory` builds the popup by hand: All + named categories +
Unfiled + **"Edit Categories"** (no ellipsis — the trailing "…" clipped to a stray
"." in the narrow popup), then — **only if `CardPresent(&vol)`** — the **"Card"**
row. A parallel `meaning[]` array tags each row (`>=0` category, `-100` Card,
`-101` Edit). Selecting "Card" calls `MainFormEnterCard` (mounts, ensures
`/Notes`, sets `gCardMode`) and sets the trigger label to "Card".

There is **no separate separator row**. `CardCatListDraw` (a custom
`LstSetDrawFunction`, installed only when a card is present, cleared immediately
after `LstPopupList`) draws, for the Card row: a divider line at the **top edge of
the row** (like the ROM launcher), then the card icon, then "Card". The rest of
the rows fall through to a plain `WinDrawChars`. Pixel-tuned in the emulator:

- **Divider** at `topY+1`, x overshot (`-4` .. `extent.x+2`) so the popup-window
  clip trims it flush to the frame (edge-to-edge). It is drawn **last** — after
  the icon and text — because `WinDrawChars` paints its cell background from the
  same row as the line, so drawing the line first left a gap under "Card". The
  glyphs sit two rows lower, so the line laid on top never touches them.
- **Gap** above the divider is 3px (`Edit Categories` bottom → line); the icon is
  at `topY+2`, "Card" text at `topY+2`.
- **Popup grown by 1px**: when the Card row is present the builder bumps
  `lst->bounds.extent.y += 1` after `LstSetHeight` (`LstPopupList` sizes the window
  from the list bounds, so the extra pixel becomes a blank strip below the last
  row). This lets "Card" sit one pixel lower and still keep a 1px gap above the
  bottom frame. The bump is recomputed away by the next `LstSetHeight`, so it never
  leaks into the plain category popup or the ROM `CategorySelect` (Edit Categories)
  dialog, which reuse the same `lstMainCategory` object.

`gCardMode` is a guarded branch throughout: `MainFormDrawTableItem`,
`MainFormLoadTable`, `MainFormNumberOfRows`, the tap handler (`MainFormDragMemo`),
and `MainFormInit` (on return from the editor it reloads the dir + keeps the
"Card" label) all early-return into card equivalents when set. The DB paths are
untouched, so switching back to "All" restores the memos with no regression.

### 3.1 Browser row drawing (`MainFormDrawCardRow`, sec001)
Mirrors the DB memo list where sensible:
- `[..]` → up icon + "..".  Folder → folder icon + name (navigation, not numbered).
- File → `N.` number (right-aligned single digit, like `DrawCryptoPadRecordText`)
  + the display name with `.enc` stripped; if encrypted, the key icon is drawn
  right-aligned at `extent.x - 7`. Icons are vertically centered for the font.

### 3.2 Category cycling (#7)
Pressing the app's assigned hard button again while CryptoNote is foreground cycles
to the next category (existing `keyDownEvent`+`TxtCharIsHardKey`→`MainFormNextCategory`
path). `MainFormGetNextCategory` now advances through the UNION of the named categories
in BOTH DBs (`dbCryptoPadRef`+`dbMemoPadRef`, kept in step by CatSync), wrapping through
"All" — the old record-count / `currentRotateOrder` (roAll/roCategoriesUnfiled/…)
skipping was removed so EVERY category (incl. empty) is reachable. Same change made in
ToDo35 and Address35; see [[category-cycle-hardbutton]].

---

## 4. Editing card files (`EditForm.c`)

The DB Edit form is reused via a **card-file bridge** (globals in `EditForm.h`):
`gCardFileOpen`, `gCardEditVol`, `gCardEditPath` (the **plaintext/base** path,
never ends in `.enc`), `gCardEditName` (display title), `gCardEncrypted` (write
encrypted on save?), `gCardWasEncrypted` (opened encrypted?), `gCardDeletePlain`
(Encrypt File: delete the plaintext original?), `gCardPassword`, and
`gCardPreloadH`/`Len` (the plaintext bytes MainForm hands the editor).

- **Open** (`MainFormOpenCardFile`, sec001): peeks the file header first
  (`CardReadFileHeader`, cheap, no full read) to learn size + encryption status.
  A file that fits the editor's own ceiling (~57–60 KB, see §4.2) is read in
  full and decrypted normally (prompts via `CardPromptPassword`, wrong password
  → `altCardBadPassword`), stashed in `gCardPreloadH`, `FrmGotoForm(frmEditForm)`.
  A **larger** file no longer refuses outright (the old `altCardTooLarge` "open
  refused" behaviour is gone) — it routes to a genuine VFS **streaming** path
  instead (`MainFormImportLargeCardFileStreaming`/
  `MainFormImportLargeEncryptedCardFileStreaming`, relaying to
  `CryptoSec35.c`'s `CryptoImportFromCardFile`/`CryptoImportFromEncryptedCardFile`)
  that reads, (decrypts,) and imports the file into one or more Sec35-wrapped
  device-DB records directly — see §4.2. One-shot `MemHandleNew` for a whole
  large file genuinely fails with `memErrNotEnoughSpace` (confirmed on real
  hardware for a 98 KB file) even when reading it in a size-capped read-loop
  (VFS drivers cap a single read call, independent of available RAM) — only
  true streaming (never holding more than one ~57 KB part's plaintext at once)
  fixed it for good.
- **In the editor**: `EditRetrieveCardFile` loads `gCardPreloadH` into the field,
  titles the form with the file name, and **hides** the DB-only controls (category
  popup, Details); `EditRetrieveData` re-shows them for DB records. The menu
  handler swallows the DB record/crypto/beam items for a card file.
  **`FrmCopyTitle(formP, gCardEditName)` buffer-size gotcha (found and fixed
  this session)**: `gCardEditName` can be as long as `cardMaxName` (64), but
  `frmEditForm`'s compiled `.rcp` `TITLE` was just `"Memo"` (4 chars) —
  `FrmCopyTitle` writes into that string's compile-time buffer, never
  reallocating, so any card filename longer than the placeholder silently
  corrupted the heap. Same bug class as EraNTP's original instance (`EraNTP.md`
  §4) and lazyFTP's independent rediscovery (`lazyFTP.md` §5) — see
  [[palmos-frmcopylabel-buffer-size-gotcha]]. Fixed by padding the `TITLE` to
  64 chars in `MainEn.rcp`; normal (non-card) memo editing never calls
  `FrmCopyTitle` at all, so nothing else changes. Verified in POSE64 (build +
  install + open a normal memo, title bar unaffected); the actual long-
  filename repro needs the CloudpilotEmu VFS harness ([[cloudpilot-vfs-harness]]),
  not yet run.
- **Save** (`EditSaveCardFile`, on `frmClose`): writes encrypted (via
  `CardCryptEncrypt`) or plaintext per `gCardEncrypted`, and deletes the sibling
  (plaintext original only if `gCardDeletePlain`; the `.enc` when saving plaintext).
  It is **dialog-free** — see §4.1.

### 4.1 Encrypt / decrypt workflow
Only plain-text **`.txt`** notes are supported (`.md` was dropped). The browser
lists folders + `.txt`/`.txt.enc` only (`CardIsTxt` filter in
`MainFormLoadCardDir`) and **hides the `.txt`/`.enc` extensions** in the display;
the New dialog just asks for a name and appends `.txt`.

The Record menu's **"Encrypt Memo"** item works on a card file too (there is no
separate "Encrypt File" item). Card Record items are handled in the
`gCardFileOpen` block at the top of `EditFormHandleMenuEvent`:
- **Encrypt Memo** (plaintext files only, `EditCardEncryptChoose`): prompts a
  password and marks the file encrypted; on save the **plaintext original is
  always deleted** (no Delete/Keep prompt).
- **New Memo**: `FrmGotoForm(frmMainForm)` then queues `cardNewFileEvent`; the
  main form handles it by running the New-card-note dialog (saves+closes the open
  file, then prompts for a new one).
- **Beam Memo / Beam Category**: `altCardNoBeam` ("Beaming card files is not
  supported").
- **Delete Memo / Remove Password**: swallowed.
- **Originally-encrypted file**: opens decrypted transparently and, on close,
  **re-encrypts automatically with the same password** — no prompt. (There is no
  per-file decrypt-to-plaintext; use *Decrypt Category* below.)
- **Editor chrome**: the form title shows the **bare note name** (`.txt`/`.enc`
  stripped); an encrypted file shows a small **key** at the bottom-right, left of
  the Graffiti-shift indicator (drawn by `EditDrawCardKey` after `FrmDrawForm`,
  since pilrc 3.2.90's `FORMBITMAP` can't take an ID to show/hide).
- Modal dialogs must never run during `frmCloseEvent`/`EditSaveCardFile` (it
  corrupts the return-to-list redraw); all password/choice dialogs happen before
  the form closes or from the list.

**Bulk (whole-folder) crypto** — the list's Record menu, `gCardMode` branch in
`MainFormHandleMenuEvent` (`MainFormCardEncryptAll` / `MainFormCardDecryptAll`):
- **Encrypt Category**: warns (`altCardEncAll` — everything lost if the shared
  password is forgotten), prompts one password, encrypts every plaintext note in
  the current folder to `name.txt.enc` (deleting the plaintext).
- **Decrypt Category**: prompts one password, tries to decrypt every `.enc` note;
  ones that match are stored decrypted (`.enc` removed), the rest are left
  untouched.

A ~1.5 s KDF runs on every encrypt/decrypt, so the callers wrap
`CardCryptEncrypt`/`CardCryptDecrypt` in `CardBusyShow("Encrypting...")` /
`CardBusyHide` — an auto-dismissing **rounded** status box (`dialogFrame`) drawn
with `WinSaveBits` (safe during frmClose; drawing only, no event loop).
**Gotcha:** `dialogFrame` draws its 2px bevel *outside* the content rect, so
`WinSaveBits` must save a **4px margin** around it (`cardBusyMarg`); saving only
the content rect left an uncleared border on-screen when a wrong password aborted
the operation.

**Gotcha (busy-indicator scope):** cover the WHOLE slow operation, not just its
in-RAM crypto step. `EditSaveCardFile`'s "Encrypting..." originally wrapped
only `CardCryptEncrypt` (fast, in-RAM) and hid before the following
`CardWriteFile` (writing the — possibly large — encrypted blob to the actual
SD card) and `CardDeleteFile` (removing the plaintext original) — both real VFS
operations, and for a large file the write is plausibly the slowest step in the
whole flow. Left uncovered, this is a genuine silent hang: no dialog, no error,
just a pause after "Encrypting..." closes and before control returns to the
list. Fix: a second `CardBusyShow("Closing...")` wraps the write+delete too
(matching the label already used for the plain, non-encrypted save path, whose
own busy window was extended the same way to also cover its trailing
`.enc`-sibling cleanup delete).

### 4.2 Sec35 integration + Import from Card

CryptoNote's `CryptoPad` DB records are Sec35-wrapped like the other three
suite apps (`CryptoSec35.c`, mirrors Datebook35/Address35's own wrap idiom): a
clear leading `sec35Flag` byte prefixes the existing `PackedRecordType` bytes;
`0` = what follows is the unchanged plain format, `1` = it's `EncCrypt`
ciphertext of that same blob (nonce = record uid). `CryptoGetRecord`/
`CryptoGetRecordFlag` are the read chokepoint (peek-only variants exist for
cheap header checks without a full body decrypt); `CryptoSealRecord` is the
write-time re-seal, called after every existing plain write path
(`EncryptRecord`'s own "Encrypt Memo" layer-2 password feature is completely
unmodified — it's just now running on top of the Sec35 unwrap, i.e. Sec35 is
an OUTER envelope, layer-2 password is the pre-existing INNER one).
`CryptoLockedBounce` gates opening a record while Sec35 is locked (bounces to
Sec35 to unlock, matching `DayLockedBounce`/Address35's `SwitchToSec35`
verbatim, including checking both "is Sec35 installed at all" and "is the key
currently available").

**Import from Card** (Record menu, card file open, `EditCardImportToCrypto`):
copies a card file's content into one or more new Sec35-wrapped `CryptoPad`
records, leaving the source card file untouched. A part-count confirm shows
first (`altCardImportConfirm`) before any DB write. Large files split at
`kCryptoRecordCeiling * 95 / 100` (~57000 bytes, a 5% margin — see below for
why the margin exists) per part, titled `"<name> 001"`, `"<name> 002"`, ...
for a multi-part split. Both the plain and Sec35-wrapped-encrypted card-file
cases stream (`CryptoImportFromCardFile`/`CryptoImportFromEncryptedCardFile`,
`CryptoSec35.c`) — never holding more than one part's plaintext in RAM at
once, writing+freeing before starting the next. The encrypted-file decrypt
path uses `AesCtrAdvanceIV` (`AesCrypt.c`, ripple-carry-add over the 16-byte
IV treated as a big-endian counter) to compute the correct IV for an
arbitrary 16-byte-aligned block offset, since a large encrypted file is read
+ decrypted across multiple VFS reads, not in one call. **Verified only via a
standalone host-side test harness against the real `aes.c`** (6 cases
including mixed/exact-boundary splits) — the surrounding PalmOS/VFS
integration around this specific path has not been exercised on real
hardware; treat as the highest remaining risk in this whole feature area if a
large *encrypted* card file behaves oddly on device.

**View-vs-edit size guard.** A `CryptoPad` record imported or grown right up
against the ~60000-byte format ceiling can `DmWriteCheck failed` (a genuine
PalmOS Data Manager fatal error, confirmed on real hardware) if edited
further — the Field Manager needs headroom beyond the raw content size while
actively editing (undo/gap-buffer state, in-place resize growth) that isn't
accounted for by `textLen` alone. The FIRST attempt at guarding this
(refusing to even *open* such a record) was wrong and had to be reverted —
**viewing must always work**; only an actual edit attempt should be blocked.
Correct design: `EditRetrieveData` calls `CryptoEditApplyGuard` right after
loading a record's text, which checks `CryptoRecordTooLargeToEdit` (a cheap
header-only peek at `textLen`, ~99% margin) and sets the field's `editable`
attribute accordingly — read-only when too large, which blocks
paste/cut/drag/typing uniformly at the Field Manager level, not just
keystrokes. `EditFormHandleEvent`'s `keyDownEvent` case additionally shows
`altRecordTooLargeToEdit` once per open (`CryptoEditGuardBlockKey`, tracked
via a per-open "already warned" flag — no repeat alert-spam) on the first
actual typing attempt, since a purely-silent read-only field gives no
feedback at all. The field object is **reused** across record-switch reloads
(up/down navigation calls `EditSaveData` then `EditRetrieveData` again on the
same field, no fresh `FrmInitForm`) — the guard state and the field's
`editable` attribute must both be explicitly reset/re-evaluated on every
load, or a too-large record's read-only lock (or its warned-once flag) would
leak onto the next, normal-sized record. This whole guard's logic lives in
`CryptoSec35.c` (sec001), not inlined in `EditForm.c` — `EditForm.c`'s own
functions are main-segment, and inlining it there (`FieldAttrType`
manipulation isn't free) once left main at **8 bytes free**.

**Hand-rolled modal dialog contract.** `CardPromptPassword` and
`MainFormNewCardNote` both drive their own minimal `EvtGetEvent`/
`SysHandleEvent`/`MenuHandleEvent`/`FrmHandleEvent` loop instead of
`FrmDoDialog`, because `FrmDoDialog` never reliably gave their password/name
field auto-focus (two other fixes — setting focus before the call, and a
custom `frmOpenEvent` handler — both failed; `FrmDoDialog` doesn't dispatch
`frmOpenEvent` through a custom handler the way `FrmPopupForm` does). This
trade means **the code driving the loop must manually reproduce everything
`FrmDoDialog` normally does for you** on exit, or subtle bugs appear one at a
time as different call sites happen to paper over them:
1. `FrmSetActiveForm(prevFrmP)` (captured before switching to the dialog) —
   without it, the underlying form is left unresponsive to further taps.
   Went unnoticed for a while because the open/decrypt call site always calls
   `FrmGotoForm` right after regardless of outcome, which happens to
   re-establish a valid active form; a call site that does NOT navigate
   anywhere afterward (`EditCardEncryptChoose` — "Encrypt Memo" on an already
   -open card file) showed it as a complete hang until the OS's own
   app-switch forced a redraw.
2. `FrmEraseForm(frmP)` **before** `FrmDeleteForm(frmP)` — both dialogs are
   `FRAME MODAL` and smaller than the full screen (`AT (2 84 156 74)`, only
   the screen's lower half), so `FrmDrawForm` auto-saves the bits behind them
   into the form's own `bitsBehindForm` handle the first time it draws.
   `FrmDoDialog` normally consumes that save via `FrmEraseForm` to restore
   those exact pixels; skipping it just leaks the handle and leaves visible
   corruption in that screen region afterward. This was invisible on a small
   memo (whose text doesn't reach the dialog's rect) and glaring on a large
   one (whose text fills it) — exactly the kind of bug that looks
   size-dependent but is actually a fixed-region redraw bug that a big memo
   simply makes visible.
3. A subsequent `FrmDrawForm(prevFrmP)` is kept anyway as a defense-in-depth
   full redraw, in case step 2's auto-save/restore doesn't fire for some
   reason — a harmless no-op layered on top of already-correct pixels if it
   does.

---

## 5. Encryption container (`CardCrypt.c`)

Same AES-128-CTR + hardened, device-bound KDF as the DB records (`AesCrypt.h`,
`EncryptRecord`) — the `PackedRecordType` fields serialized to a flat file:

```
[0]  magic     "CNbin1"   (6 bytes)
[6]  flag      1          (encrypted)
[7]  pwIV      16         (CTR IV for the password verifier)
[23] bodyIV    16         (CTR IV for the note body)
[39] kdfSalt   16         (per-file salt)
[55] kdfRounds UInt16     (KDF work factor, calibrated ~1.5 s)
[57] passwdLen UInt16
[59] verifier  passwdLen  (AES-CTR of the password under its own derived key)
[..] ciphertext rest      (AES-CTR of the note text)
```

The key folds in the per-file password, the global password (if set) and the
device secret (ROM serial), so a card file is exactly as strong and as
device-bound as a DB note. Verify a password by decrypting the verifier and
comparing to the entered password (as `ValidatePassword` does for records).
`CardCryptDecrypt` returns `cardCryptOK` / `cardCryptBadPassword` /
`cardCryptNotEncrypted` / `cardCryptCorrupt`.

---

## 6. Testing

Real VFS can't be emulated in POSE/pose64 (no card). Use the **CloudpilotEmu +
Claude-in-Chrome** harness (see the `cloudpilot-vfs-harness` memory): m505 4.1 ROM
+ an SD card image, PRC installed by JS byte-injection (the app must be at the
launcher, not running, to reinstall). Pixel-level UI is checked by reading the
canvas back (`drawImage` → `getImageData`); the canvas is 486×759 at page offset
(237,68), 1:1 with the screenshot, and 1 Palm pixel = 486/160 ≈ 3.0375 canvas px.
Verified end to end: Card row appears with the top-edge divider (3px gap, solid
edge-to-edge, "Card" 1px above the frame); numbered browser + folder nav; open /
edit / save a `.txt`; **Encrypt Memo** → plaintext auto-deleted → key icon; wrong
password leaves no busy-border residue; an originally-encrypted file re-encrypts on
Done with the same password (no prompt); bulk **Encrypt/Decrypt Category**; manual
**drag-to-reorder** with the move-indicator line vs tap-to-open; no DB regression.
Real-device card test is the user's (no device logs → surface Err codes on-screen).

**Sec35 / Phase 5 (streaming import, size guard, dialog fixes): real-device
confirmed** as of the round-17 pass — large plain-file streaming import/open,
the view-vs-edit size guard, the modal-dialog corruption fix, and the
"Closing..." busy-indicator gap fix were all explicitly retested and signed
off on real hardware. **Still unconfirmed on real hardware:** the streaming
DECRYPT path for a large *encrypted* card file specifically (`Import from
Card` on/decrypting-open of an `.enc` file over the single-shot size ceiling)
— its crypto core (`AesCtrAdvanceIV`) is verified only via a host-side test
harness against the real `aes.c`, never against real VFS/PalmOS behavior.
Treat that path as the highest remaining risk if a large encrypted card file
misbehaves. Full round-by-round history (R1–R17) is in the
`cryptonote-sd-card-phase5` memory file, not duplicated here.

---

## 7. Known limits / future

- A `CryptoPad` record has a hard ~60000-byte format ceiling (`textLen` field
  width + the AES length field being 16-bit) — a plain card-file open/edit
  under that ceiling behaves as before; a **larger** file no longer refuses
  outright, it streams into one or more split records instead (§4.2), and a
  record already near the ceiling becomes view-only for further edits (§4.2's
  size guard) rather than crashing.
- Card removed mid-session isn't specifically handled (the popup only offers Card
  when mounted).
- Beaming card files is intentionally unsupported (shows `altCardNoBeam`); a card
  Find is not implemented (beam send lives in `sec001`).
- File order follows the app sort preference like memos: **soAlphabetic** sorts
  files A–Z; **soManual** uses a **hand order that drag-to-reorder persists** in a
  hidden per-folder `.cnorder` file (one name per line). Folders are always sorted
  alphabetically to the top. `MainFormSortCardEntries` ranks files by
  `MainFormLoadCardOrder` (parallel `gCardOrder[]` key); the pen handler
  `MainFormCardDrag` distinguishes a tap (open) from a drag (reorder →
  `MainFormCardReorderFile` → `MainFormCardWriteOrder` → refresh). `.cnorder` has
  no `.txt`, so the `CardIsTxt` filter hides it from the browser.

---

## 8. Consolidated gotcha checklist (for a rebuild)

1. **Build with `-palmos4`** (plain `make`), never `make PALMOS_FLAG=`. Without it
   a `main`→`sec001` call in a newly-added `.o` becomes a near `bsr.w` (DISP16) →
   garbage jump / spurious Fatal Alert. Verify with `objdump -r` (reloc must be
   **32**, not DISP16). (§2.1)
2. **Segment rules** (three traps): libgcc 32/64-bit division → main; the
   `AES_SECTION` tag must be on the **forward declaration**, not just the
   definition; anything a no-globals sublaunch can reach → main. (§2.2)
3. **Keep `CardIO.c` in main** — it's the VFS hot path (`CardPresent` from the
   popup) and VFS-heavy; only `CardCrypt` + foreground card functions go to sec001.
4. **`CardIO.h` includes `<PalmOS.h>` only**, never `<VFSMgr.h>` — pulling VFSMgr
   into a caller's include context triggers a trap-macro parse error. Only
   `CardIO.c` includes `<VFSMgr.h>`.
5. **No modal dialog during `frmCloseEvent` / `EditSaveCardFile`** — it corrupts
   the return-to-list redraw. All password/choice dialogs run **before** the form
   closes or from the list. (§4.1)
6. **Popup divider drawn LAST** — `WinDrawChars` paints its cell background from the
   text's top row, erasing a line drawn first; draw icon+text, then the line. (§3)
7. **Busy box saves a 4px margin** — `dialogFrame` bevels outside the content rect;
   `WinSaveBits` the rect + `cardBusyMarg` or a border stays on screen. (§4.1)
8. **pilrc 3.2.90**: `FORMBITMAP` takes **no ID**, so the editor key is drawn
   manually by `EditDrawCardKey` after `FrmDrawForm`; bottom-aligned dialogs use
   explicit `AT` rects; the "Edit Categories" label carries no "…" (it clips).
9. **Own DB / creator** — creator `CrNt`, DB `CryptoNoteDB`; the MemoDB source
   stays `MemoDB`. Additive only: never remove or alter DB-note behaviour.
10. **Crypto constants byte-identical to CryptoPad** — same AES-128-CTR + device
    bound KDF model, so card files are exactly as strong/portable as DB records.
11. **`StrLen` returns `Int16`** (max 32767, ROM trap) — comparing/assigning its
    result into a `UInt32` for anything that might exceed 32767 bytes
    sign-extends to garbage. Use `SafeStrLen` (`Utils.c`, plain byte-scanning
    loop) for any card-file/large-memo length, not `StrLen`. Root-caused a real
    "Import Failed" on a 33 KB file and a silent "invalid format" refusal on
    Encrypt Memo for any >32767-byte record.
12. **One-shot `MemHandleNew` for a whole large file can fail even after
    read-loop-ing past the VFS per-call size cap** — `memErrNotEnoughSpace`
    (258) is a real, confirmed failure mode (98 KB file). Only genuine
    streaming (never holding more than one bounded part's plaintext in RAM at
    once) fixes it; a bigger read-loop alone does not. (§4.2)
13. **A hand-rolled modal-dialog replacement for `FrmDoDialog` must reproduce
    its FULL exit contract**, not just focus: `FrmSetActiveForm(prevFrmP)`
    (event routing) AND `FrmEraseForm(frmP)` BEFORE `FrmDeleteForm(frmP)`
    (restores the pixels a `FRAME MODAL` form smaller than the screen
    auto-saved on its first `FrmDrawForm`) — skipping either produces a bug
    that different call sites mask differently (one hangs outright, another
    shows fixed-region visual corruption that's only visible behind a large
    memo's text). (§4.2)
14. **A "too large" guard belongs at the point of actual risk, not at open
    time** — refusing to even VIEW a record is a UX regression users will
    correctly reject; gate the SPECIFIC risky action (here: an edit attempt,
    via a read-only field + a keyDownEvent-time alert) instead of the whole
    open path. (§4.2)
15. **`FrmCopyTitle`/`FrmCopyLabel` write into the `.rcp`-compiled buffer size,
    never reallocating** — any dynamic title/label sourced from something
    longer than the resource's placeholder text (e.g. a card filename, up to
    `cardMaxName`=64) silently corrupts the heap. Pad the placeholder to the
    real worst case; trailing spaces are clipped by the draw width, not shown.
    (§4, [[palmos-frmcopylabel-buffer-size-gotcha]])
15. **A busy/status indicator must cover the WHOLE slow operation it names**,
    not just its fastest/first sub-step — hiding it right after an in-RAM
    crypto pass but before the actual (possibly much slower) VFS write that
    follows produces a silent hang with no error and no explanation. (§4.1)

---

## 9. File map

```
apps/cryptonote/
  Makefile              CryptoNote_v1.prc = $(EXE)_v$(VERSION).prc; PALMOS_FLAG ?= -palmos4
  CryptoNote.def        application { "CryptoNote" CrNt ... } + multiple code { "sec001" }
  sects.h               AES_SECTION / BEAM_SECTION → section("sec001")
  CardIO.{c,h}          VFSMgr wrapper (MAIN); only .c includes <VFSMgr.h>
  CardCrypt.{c,h}       per-file AES container + password dialog + busy box (sec001)
  CryptoSec35.{c,h}     Sec35 wrap/unwrap chokepoint, Import-from-Card streaming
                        engine, too-large-to-edit guard (sec001; own <VFSMgr.h>
                        include for its direct VFS calls, see §2.3)
  MainForm.c            category popup w/ Card row + divider (main), card browser,
                        sort/order/drag + bulk crypto (sec001)
  EditForm.{c,h}        shared editor + card-file bridge globals, load/save, key draw
  PasswordForm.{c,h}    password form (masking) + numeric keypad popup with key click (sec001)
  Main.h MainRsc.h      IDs, cardNewFileEvent, alerts (altCard*), bmpCard* ids
  Main<LOC>.rcp         pilrc source (latin-1): forms, frmCardNewNote/Password, alerts
  card.bmp folder.bmp cardup.bmp   card-browser icons (key reuses key.bmp)
  (CryptoPad carryover) AesCrypt.{c,h}, aes.{c,h}, Beam.c, key.bmp, app/font icons
```

To rebuild from the AES CryptoPad fork (`CryptoPad_v365_src/src/`): rename creator
`CrPd`→`CrNt` and the DB to `CryptoNoteDB`; add `CardIO` (main) + `CardCrypt`
(sec001) + the card branches in `MainForm`/`EditForm` behind `gCardMode` /
`gCardFileOpen`; build multi-segment with `-palmos4`; keep the AES/KDF model
byte-identical; and test on the CloudpilotEmu VFS harness (§6). Cross-check the
segmentation/`-palmos4` foundation against `Datebook35.md` / `ToDo35.md`.
