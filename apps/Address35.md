# Address35 — Modern PalmOS Address Book (reference & rebuild guide)

A ground-up modern Address Book for the HackMaster Y2K35 suite. Unlike ToDo35 /
Datebook35 (era-rebased forks of the ROM PIMs), Address35 is a **new app with its own
database format**, sharing only the suite's `lib/` foundation (categories, hack gate,
era date math for birthdays). Creator `Ad35`, DB `Address35DB` (type `DATA`), backup
bit set; `.def` = `application { "Address35" Ad35 backup ok-to-install-newer }` +
`multiple code { "sec001" "sec002" "sec003" }`. See design memories [[address35-project]],
[[address35-vcard-beam]], [[address35-category-and-pose64]],
[[address35-keep-in-own-folder]].

## 1. What Address35 is

A contacts app with: a section-ordered inline-editable **Edit form**, a read-only
**Record form**, a searchable **picker**, per-field value dialogs (email / address /
social / date / relation), **tags** and **relations**, **vCard 3/4** beam + SD-card
import/export, and **era birthdays/anniversaries** that surface in Datebook35's Agenda.
Records store an arbitrary, ordered set of typed fields (not the ROM's fixed 19).

## 2. Era mechanism (birthdays only)

Address35 itself stores no appointment dates, but a contact's **birthday /
anniversary** is an era-wide date (stored as a portable `YYYYMMDD` string in the Date
value dialog, full-range — not clamped to 2031). Datebook35's Agenda reads these and
shows them as untimed rows with an era-correct age; tapping switches back to Address35.
See [[datebook35-agenda-birthdays]]. The build force-includes the same
`lib/edate_override.h` foundation as the other apps for any date math.

## 3. Build & segmentation

`Makefile` (not a build.sh). Build with **`-palmos4`** (far calls) — mandatory for the
multi-segment layout. `a35_seg.h` defines `SEC001` (foreground UI) `/SEC002` (SD-card
browse, the beam-inbox merge §9, Phone Lookup §10) `/SEC003` (**bundled crypto**: `enccore.c`+`aes.c`, and the decrypt/seal record
functions) (+ `SEC_RECV` = main, `DATELIB_SECTION` = main). `.def` declares
`multiple code { "sec001" "sec002" "sec003" }` and multigen generates the jump-table glue
(`Address35-sections.{s,ld}`). **Header-only `SEC00x` tag changes need `make clean`** (the
Makefile has no header deps, so a moved function silently stays put otherwise).

**No-globals sublaunch rule (critical — this is what makes beam receive + Find safe):** a
sublaunch with no `sysAppLaunchFlagNewGlobals` (global Find; beam receive when the app
isn't running) has **no valid A5**, so it must touch **no globals AND no A5-relative static
data** — and cannot load secondary segments. Being in `main` is necessary but *not
sufficient*: the vCard parser's static tables and the sort/AppInfo helpers are A5-relative
and corrupt the heap under a bad A5. So Find uses `A35GetRecordRaw` (main, clear fields
only, no decrypt) and beam receive stores raw bytes only (see §8). **GOTCHA**
([[address35-vcard-beam]]): a whole-file `.s` relabel breaks function pointers (RecordForm
callbacks) — use per-function section attributes, not a blanket relabel.

## 4. Data model (own TLV DB)

`AddressDB35.{c,h}` — each record is a self-describing **TLV** (type-length-value)
stream of typed fields, so a contact can carry any number/order of names, emails,
phones, addresses, dates, socials, tags and relations. `A35GetRecord` unpacks into a
working struct; writers repack. Categories use the standard AppInfo block (synced
suite-wide by CatSync). Middle name, prefix/suffix, gender/pronouns, and per-field type
pickers are supported.

## 5. UI & resources

`Address35En.rcp` (pilrc). Key forms (`AddressRsc.h`): `frmMain`(1000, list),
`frmEdit`(1100, inline-editable section list), `frmRecord`(1200, read-only),
`frmPick`(1500, searchable list + Find row — shared by the Country and phone
dial-code pickers) + contact picker, and value dialogs `frmEmailDlg`/`frmAddrDlg`
(full-screen)/`frmSocialDlg`/`frmDateDlg`/`frmRelDlg`/`frmNoteDlg`(full-screen)/
`frmPhoneDlg`(half-screen, bottom-aligned), and `frmLookup`(2100, the Phone Lookup
popup other apps open — §10). See §9 for the full-screen Address/Note
forms and the phone dialog (redesigned away from an inline chip after persistent
scroll-redraw artifacts — §9). **Date dialog** uses per-digit push-button
boxes + up/down arrows (X-Y2K35-plugin style) for month/day/year — not ROM dropdowns
([[address35-category-and-pose64]]). **Category** uses a custom `LstPopupList`
(`MainSelectCategory`) rather than ROM `CategorySelect`, because the ROM dialog didn't
update the app's category global; the SD "Card" source is a divider-separated last row
(CryptoNote idiom). "Edit Categories…" from the custom popup calls **`CategoryEdit`**
(straight to the editor), NOT `CategorySelect` (which re-pops the list) — same fix applied
to CryptoNote; ToDo35 was already correct. POSE64 works for non-VFS UI testing; use
CloudpilotEmu for SD.

**Menus & Preferences.** Options menu: **Font** (`FontSelect`; `gListFont` persists in
prefs; `MainLoadTable` derives row height + caps visible rows from it; the pinned Me row
uses the bold variant), **Preferences**, About. Record menu: Set-as-me, Edit, **Duplicate
Contact** (`A35DuplicateRecord` — duplicates from the *decrypted* view and re-seals under
the new uniqueID, since the nonce is uid-keyed; a raw byte copy would decrypt to garbage),
Delete, Beam. **Preferences** (`PrefsForm.c`, sec001): Name-field-label term
(Surname/Last/Family) + a two-level **Sort order** (Primary + Secondary, each Last/First/
Company) — stored in the AppInfo (`primarySort`/`secondarySort`); the comparator packs both
into its `Int16` arg (`A35SortArg = pri | sec<<8`).

## 6. Category cycling (#7)

Pressing the app's assigned hard button again while Address35 is foreground cycles to
the next category. Added `MainNextCategory()` (advances `gCurrentCategory` via
`CategoryGetNext(gAddrDB, …)` through ALL named categories incl. empty, wrapping through
"All"; updates `gShowAllCategories`/`gCategoryName`/the trigger label + `MainDraw`) and
a `case keyDownEvent:` in `MainFormHandleEvent` guarded by `TxtCharIsHardKey`. Same
mechanism as ToDo35/CryptoNote; see [[category-cycle-hardbutton]].

## 7. Beaming & SD card

- **vCard** (`vcard.c` parser → main; `vcardw.c` emitter → sec001; `Transfer.c` beam) —
  vCard 3/4, whole-contact including tags/relations. Host regression harness at
  `test/vcard/`. **Beam RECEIVE uses the transient-inbox pattern — see §8.4.**
- **SD card** (`AddrCard.c` + `CardIO.c`) — browse/import/export vCards on a card via
  the VFS; the "Card" category-popup row enters card-browse mode.

## 8. Encryption & secure beam receive (Sec35)

Address35 was the first app wired to **Sec35**, the suite's at-rest-encryption authority
(shared AES-128-CTR key in a Feature-Manager chunk, passcode+serial KDF, TTL auto-lock).
**All three PIM apps now share it** — ToDo35 (`ToDo35.md` §8) and Datebook35
(`Datebook35.md` §9) encrypt their sensitive text the same way with the same key, and
Sec35's Forget-Password **wipe** and password-change **rekey** cover all three DBs
(`'Ad35'`/`'Td35'`/`'Db35'`). See [[address35-encryption-wiring]],
[[sec35-encryption-design]], [[sec35-app-ui-and-icon]].

**8.1 Field-level, at rest.** Only the sensitive *payload* field values are encrypted
(`A35FieldEncrypted`: phone/email/IM/URL/address/city/state/zip/country/social/note/custom/
gender/pronouns/other). **Clear:** the name/sort keys (last/first/middle/nickname/company/
title/prefix — so the comparator + list draw work), tag (filter), relation (reciprocity
reads it), birthday/anniversary (Datebook35 Agenda reads them cross-app). Per-record misc
bit `a35MiscEncrypted` (0x01) marks an encrypted record; plaintext + encrypted records
coexist. **Nonce per field = (record uniqueID as hi, field index as lo)** — so a record's
ciphertext is bound to its uid; copying raw bytes to a new record (new uid) would decrypt to
garbage (that's why Duplicate re-seals from the decrypted view).

**8.2 Read/write.** `A35GetRecord` (sec003) copies the stored record to a buffer and, if the
misc bit is set, decrypts each payload field when unlocked / **masks it with `******`** when
locked (never shows ciphertext); release with **`A35ReleaseRecord`** (not MemHandleUnlock).
`A35EncryptRecordInPlace` (sec003) seals a record after it's attached (uid known); it is a
no-op while locked, so records created while locked stay plaintext until later.

**8.3 Launch gate, seal-on-unlock, "encryption active" signal.** `A35LaunchGate` (sec001):
the book is *protected* if `A35DbHasEncrypted() || (EncEncryptionActive() && records>0)`
(the `ENC_PWSET_FTR` feature, which Sec35 asserts, means "a passcode is set" even before any
record is encrypted). Protected + Sec35 missing → alert + exit; protected + locked → hand
off to Sec35 (`SwitchToSec35` via `kSecReturnFtr` feature, no cross-app param chunk) to
unlock, which switches back. Once past the gate (unlocked), `A35SealAllPlaintext(gAddrDB)`
sweeps and encrypts every still-plaintext record — so a record created while locked, or
beam-received, becomes encrypted on the next unlock and never lingers in the clear.

**8.4 Secure beam receive — the transient inbox (Address35DBPlain).** Beam receive arrives
as a no-globals sublaunch (§3): it must touch NO globals/A5-static/segments/crypto/goto. So
the sublaunch does the *minimum* — `A35OpenPlainDB` (create/open the inbox DB
**Address35DBPlain**, type `Ad3P` so Find never scans it) then `A35ReceiveData` drains the
socket and `A35InboxAppendRaw` stores the **RAW unparsed vCard bytes** as one record (or
several ≤60,000-byte records cut at vCard boundaries for a large beam — see §9). No
parse, no `A35NewRecord`, no sort, no goto, no display. On the next full launch (and on
`frmMain` reopen, for a running app), `A35MergePlainDB(gAddrDB)` PARSES each raw record
(`A35VCardSpan`/`A35VCardToRecord` → `A35NewRecord`) into the real DB, deletes the inbox, and
`A35SealAllPlaintext` seals. This is why the earlier "Fatal Exception on beam, on any app,
while navigating" is gone: nothing A5-relative runs under the bad A5. **Reusable pattern:**
any sublaunch-unsafe work → park raw in a transient DB, do it on the next full launch.

## 9. Post-encryption functional improvements (2026-08)

- **Data-loss bug fixed:** long addresses lost trailing fields (state/zip/country).
  Root cause was **not** a buffer-size cap but Palm's `StrNCat` strlcat semantics
  (`n` = TOTAL dst size, not chars-to-append); the codebase's
  `StrNCat(x,y,sizeof(x)-StrLen(x)-1)` idiom double-subtracted and silently
  truncated once a string passed ~half its buffer. Fixed everywhere in Address35 —
  see [[strncat-strlcat-semantics-gotcha]] (also flags ToDo35/Datebook35/CryptoNote
  as likely affected; not yet swept).
- **Full-screen Address dialog** (`frmAddrDlg`, was a small modal): Line 1/Line 2
  are now 2-line fields; City/State/Zip/Country get their own rows. On-disk format
  unchanged (still the 7-part TAB-delimited compound field) — no migration.
- **List-view name ellipsis:** `MainDrawRecord` composes the full name into one
  buffer and uses `WinDrawTruncChars` (adds "…") instead of silently dropping name
  runs that didn't fit.
- **Full-screen Note editor:** notes now live in their own `MemHandle`
  (`EditField.big`) instead of the 256-byte inline value buffer, so they can be
  arbitrarily long (`frmNoteDlg`, `EditNoteDialog`, `kNoteMax=2000`). Record view
  previews the first 20 lines + "(more...)"; edit-form row shows a 2-line preview;
  tapping either opens the full editor.
- **Phone dial-code — modal dialog, not an inline chip.** First attempt used an
  inline SELECTORTRIGGER "chip" beside the phone value field in the edit-form row;
  this caused persistent redraw artifacts on scroll (stale pixels between chip and
  field) that survived several fix attempts (dynamic chip width, full-viewport
  erase). **User redirected to sidestep the redraw problem entirely**: phone fields
  are now **dialog-edited** like Address/Note/Social (`frmPhoneDlg`,
  `EditPhoneDialog`), a half-screen modal with "Country:" (opens the dial-code
  picker) and "Number:" rows, bottom-aligned like the other value dialogs
  (`AT (2 76 156 82)`, bottom edge y=158). The edit-form row just shows read-only
  "+NN number" text (`EditPhoneDisplay`). An unset dial code reads "Pick a Country"
  (not a bare "+", which misleadingly implied a code was already set).
  Dial codes: `kDialCodes[]` in EditDialogs.c, 1:1 aligned with `kCountries[]`
  (compile-time size-check `typedef char kDialCount_check[...]`). Stored **in the
  phone value itself** as `"+66 8123456"` (vCard-friendly); `PhoneSplit` parses it
  back into `gFields[mi].sub` (a country index) + the bare number on load;
  `EditSave` re-prepends the code from `sub` before packing.
- **Searchable "Find" row on every country/dial-code list.** `A35PickList` (shared
  by the address Country picker and the phone dial-code picker) was type-ahead-only;
  rebuilt to mirror the Contact Picker's `fldPickFind`/`btnPickFind` pattern — a
  live substring filter (case-insensitive) as you type, via a new `PickFilterIdx`
  helper that tracks each shown item's ORIGINAL index so a selection maps back
  correctly. `frmPick` grew to full height (`AT (2 2 156 156)`) with the Find row.
- **Last-used defaults via UNSAVED app preferences** (`PrefSetAppPreferences(...,
  saved=false)` — not backed up on HotSync, survives soft reset): last-used address
  country and last-used phone dial code, seeded into new address/phone entries
  (`A35GetLastCountry`/`A35SetLastCountry`/`A35GetLastDial`/`A35SetLastDial` in
  Address35.c). "None" if never set.
- `gFields[a35EditMaxFields=48]` (was 40, now matches the DB's `a35MaxFields`) is
  ~12KB and moved off `.bss` into a heap allocation (`MemPtrNew` on frmOpen,
  `MemPtrFree` on frmClose) — a fixed-size global that big overflowed the app's
  data region.
- **Field-type popup (edit-form row) is now searchable.** Was a native ROM-bound
  popup list (`ctlEnterEvent` preselect + `popSelectEvent` apply); rewritten to
  open the same `A35PickList` search dialog as Country/dial-code, so a field type
  ("Mobile", "Relationship", ...) can be found by typing instead of only
  tap-and-scroll (`EditTypePick`, `kFieldTypeNames[]`). The `lstEditType` resource
  and its 8 `POPUPLIST` bindings were removed as dead.
- **SD-card category glyph:** the "Card" row in the category popup uses char
  **0x1A in the STANDARD font table** (`stdFont`/`boldFont`), not a bitmap and not
  `symbolFont`/`symbol11Font` — confirmed on real hardware after an initial wrong
  guess (symbol11Font 0x1A rendered a different, wrong glyph). `bmpCardIcon` and
  its bitmap resource were removed as dead (`CardDrawIcon` is still used for the
  browse-mode folder/up icons, which stay bitmaps). Final pixel-tuned position
  (real-device feedback, `CardCatListDraw` in `MainForm.c`): glyph at
  `(x+1, topY+2)`, "Card" text at `(x+9, topY+2)`.
- **vCard import silently failed for real-world exports with an embedded photo**
  (reported as "vCards with spaces in filenames fail" — the space was a red
  herring; real macOS/iOS/Android exports both embed a full-res `PHOTO` and name
  the file after the contact). Root cause: the whole vCard engine
  (`A35VCardSpan`/`A35VCardToRecord` and every caller: Import, browse-tap, Split,
  and beam receive in `Transfer.c`) used `UInt16` byte offsets, and
  `A35CardReadContact`/`A35CardImport`/`A35CardSplit`/`A35CardBrowseList` clamped
  the parse window to `0xfffe` (65,534) bytes — so any vCard whose `END:VCARD`
  fell past that (trivial with an embedded photo; confirmed against a real
  83,305-byte sample whose `END:VCARD` sat at byte 83,294) was never found.
  Fixed by (1) widening the engine's length/offset types to `UInt32` throughout,
  removing the artificial clamp (the file was already fully read into memory
  regardless — the clamp added no memory-safety benefit, only truncated the
  parse window), and (2) having the parser recognize `PHOTO`/`LOGO`/`SOUND`/`KEY`
  properties by name and skip their value (incl. all folded continuation lines)
  without buffering or storing it — Address35 has no photo field, and burning
  arena space + CPU cycles on tens of KB of discarded base64 on DragonBall-class
  hardware is pure waste. Verified against the real sample file via the host
  harness (`test/vcard/`): before, span was capped short of END; after, span
  matches the true file size, all fields parse correctly (incl. a phone number
  with an embedded space, "+1 555 0100 123" — proving spaces were never the
  problem), and the photo does not leak into a custom field. Existing round-trip
  regression tests still pass unchanged.
- **Follow-up: real-device test showed the UInt32 widening alone wasn't enough.**
  `A35CardImport`/`A35CardReadContact`/`A35CardBrowseList` still loaded the
  *whole* file into one `MemHandle` before parsing (`CardReadFile`) — fine on an
  unlimited-RAM host, but a single ~83KB allocation/`VFSFileRead` was observed to
  silently fail on real (memory-constrained) hardware, with no error surfaced
  (matched the user's "0 cards imported" / tap-to-preview "does nothing").
  **Fixed with a streaming reader**: `VCardStream` (vcard.c/h) reads a VFS file
  through a small 256-byte rolling buffer (1-byte pushback for line-folding
  lookahead) instead of one big buffer; `A35VCardStreamNext` reuses the exact
  same line-unfold/property-dispatch/PHOTO-skip logic as `A35VCardToRecord`, just
  pulling bytes from the stream instead of indexing an array. `A35CardImport`,
  `A35CardReadContact` (tap-to-preview), and the (currently dead-code, never
  called) `A35CardBrowseList` were rewired onto it — memory use is now bounded
  (256B stream buffer + 4KB arena + 2.4KB line buffer) regardless of file size.
  `A35CardSplit` deliberately KEPT the old whole-buffer `A35VCardSpan`/
  `A35VCardToRecord` approach — it writes verbatim raw-byte copies per contact
  (`CardWriteFile`), which streaming-parse-into-a-record can't provide. `FileRef`
  is stored as a bare `UInt32` in the public `VCardStream` struct (not typed
  `FileRef`) so `vcard.h` never needs `<VFSMgr.h>` — pulling that into some
  callers' include context triggers a parse error (same reason `CardIO.h` keeps
  VFSMgr.h `.c`-only, see its own comment).
  **Busy/progress dialogs**: a photo-heavy card can take a visible moment to
  stream (the file is still fully scanned to find `END:VCARD`, the photo bytes
  are just discarded rather than buffered). Two separate mechanisms now cover
  this, matching whether the operation has a real progress metric (superseded
  the original single `frmReading` MODAL form, since removed along with its
  resource):
  - **`A35BusyBegin(title)`/`A35BusyEnd()`** (AddrCard.c) — a lightweight
    text-only toast for operations with NO real progress metric: a directory
    scan or single-file preview read ("Reading..."), and SD-card **export**
    ("Exporting...", see the next bullet below). Screen-direct draw (`WinSaveBits`/
    `WinDrawRectangleFrame`/`WinDrawChars`/`WinRestoreBits`, same idiom as
    CryptoNote's `CardBusyShow`/`CardBusyHide`) — no form resource, auto-sized
    to the title text, auto-dismisses on `A35BusyEnd()`.
  - **`A35ProgressBegin(title)`/`A35ProgressBar(ctx, width)`/`A35ProgressEnd()`**
    (AddrCard.c) — a real MODAL form (`frmCardProgress`) with a hand-drawn bar,
    for operations that DO have a metric: **import** ("Importing...") and a
    confirmed group-vCard split ("Processing vCard..."). `VCardStream` carries
    an optional `VCardProgressFn` callback invoked from `vcStreamFill` with an
    ALREADY-COMPUTED bar width — the division runs in vcard.c/main, never in
    AddrCard.c/sec001, since 32-bit multiply/divide from a secondary segment
    needs unreachable libgcc (the family's [[multiseg-libgcc-gotcha]]).
  Verified via a new host test harness extension (stubbed `VFSFileOpen`/`Read`/
  `Close`/`Size` backed by real host file I/O) against the real 83KB sample and
  a hand-built 3-contact concatenated file — both parse identically to the
  whole-buffer version, and the progress callback fires the expected number of
  times, reaching the full bar width exactly at EOF.
- **SD-card export shows an "Exporting..." busy toast**, for UI parity with
  import's own "Importing..." progress dialog (both requested/verified in the
  same pass). `A35CardExport` (AddrCard.c) wraps its whole record-write loop in
  `A35BusyBegin("Exporting...")`/`A35BusyEnd()` rather than the form+bar
  mechanism import uses — export has no equivalent byte-stream to hook a
  proportional bar off (it iterates DB records directly via `DmNumRecords`,
  writing one small file per matching record, never touching `VCardStream`),
  and computing a record-count-based bar (`i * width / total`) would need a
  32-bit divide running from AddrCard.c's own SEC001 tag — exactly the
  [[multiseg-libgcc-gotcha]] crash class the byte-based progress bar above was
  already careful to avoid. The busy toast is the correct fit for "no real
  metric" per the split documented above. Confirmed in code (2026-09-29): both
  early returns in `A35CardExport` (`A35CardInit` failure, buffer alloc failure)
  happen before `A35BusyBegin`, and every path after it reaches `A35BusyEnd`,
  so the toast can't be left on screen. Working, no user-reported issues.
- **Beam receive over ~32KB — fixed (2026-09-29).** `A35ReceiveData`
  (`Transfer.c`) kept its growable buffer's `cap`/`total` as `UInt16`: `cap`
  doubled 4K→8K→16K→32K, the 64KB resize then succeeded but `(UInt16)65536`
  stored `cap = 0`, so the next `ExgReceive` got `(UInt32)(0 - 32768)` as its
  length and later rounds resized the handle to 0 bytes — heap corruption or a
  crash on any beam over ~32KB (a contact with an embedded photo, or a large
  Send Category). Fix:
  - `cap`/`total` and `A35InboxAppendRaw`'s `len` are `UInt32`; the buffer grows
    to at most `a35BeamRecMax` (60,000 bytes — one inbox record, under the ~64KB
    chunk limit).
  - When the buffer is full (at 60,000, or earlier if the heap is too tight to
    grow), `A35CardEndAt` finds the last line-start `END:` and everything up to
    it is stored as one inbox record; the partial card after it moves to the
    front and receiving continues. So a multi-contact beam of any size becomes
    several inbox records, each cut at a vCard boundary, and nothing is lost.
    `A35MergePlainDB` already walks every inbox record, so it needed no change.
  - A **single** card bigger than 60,000 bytes (a huge `PHOTO`) stores its first
    60,000 bytes, then discards input until its `END:` line, keeping only a
    16-byte tail between reads so each scan covers just the new bytes (a
    whole-buffer rescan per read would cost seconds on a DragonBall). The parser
    already tolerates a missing `END:VCARD` and discards `PHOTO` data, so only
    fields placed *after* the photo are lost.
  - `A35CardEndAt` compares characters one at a time rather than against a string
    literal — literals are globals, and this runs in the no-globals sublaunch.
    Disassembly confirmed no A5-relative access in the receive path.
  - Space: main `coderes` had ~51 bytes free and the fix needed ~326, so
    `A35MergePlainDB` (full launch only, called from `StartApplication` and
    `MainForm`, no 32-bit multiply/divide) moved to **sec002**. Main then had
    ~115 bytes free (84 after the Phone Lookup hook, §10). `AddressDB35.h` gained a host-harness `SEC002` fallback.
  - Verified with a host test that runs the real receive code under simulated
    beams in random read sizes (1 byte to 8KB, 40 seeds each) and parses the stored
    records with the real `vcard.c`: 1 contact; 300 contacts; 1,000 contacts
    (LF-only, lowercase `end:vcard`); 300 contacts with the heap capped at 8KB;
    20KB, 45KB, 100KB and 300KB photos; 100KB photo with the heap stuck at 4KB.
    All contacts recovered in order, every record ≤ 60,000 bytes. User-tested on
    device (2026-09-29): app runs normally after the `sec002` move; not yet
    exercised with a real IR beam.

## 10. Phone Lookup for the suite (2026-10)

Datebook35 and ToDo35's **Phone Lookup** (Day/List view Options menu, Note view) now look up
Address35 contacts. The ROM's `PhoneNumberLookup` can't be redirected: disassembling the
Palm OS 4.1 ROM shows it hard-codes `DmGetNextDatabaseByTypeCreator('appl', 'addr')` +
`SysAppLaunch(..., flags 0, sysAppLaunchCmdLookup, ...)`. So:

- **Caller side** (`apps/lib/a35lookup.c`, `A35LookupPhone(fld)`): the selection, else the word
  before the cursor, becomes `lookupString`; it sublaunches Address35 with the standard
  `AddrLookupParamsType` and `sysAppLaunchFlagNewGlobals` (Address35 is multi-segment), then
  pastes the returned `resultStringH` ("Name - number") over that word. No Address35 → falls
  back to the ROM lookup. Address35 locked → hands off to Sec35's unlock screen
  (`kSecLaunchUnlock`, return-to = the caller's own creator via `SysCurAppDatabase`), which
  switches back to the caller; the user then runs Phone Lookup again.
- **Address35** (`Lookup.c`, sec002; 5-line hook in `PilotMain`): a small modal form
  (`frmLookup`) over the caller's screen, **one row per contact** that has a phone, email or
  social handle; Find is pre-filled with the looked-up word and live-filters by name. Picking a
  contact pops up (`lstLookupItems`, `LstPopupList`) just that contact's phones (Mob/Hom/Wrk/
  Fax/Main/Pgr/Oth), emails and social handles; a contact with one item skips the popup.
  Pastes "Name - value" (social: "Name - Instagram @handle").
  Locked (same rule as `A35LaunchGate`) → `params->uniqueID = a35LookupLockedID`, no result.

**Speed** (POSE64 cycle profiler at the Palm III's 16.6 MHz, 300 encrypted contacts):
the first version (one row per phone, rows drawn by decrypting whole records) took ~3.1 s to
open, ~2.5 s per Find keystroke and ~1.9 s per page. Now:
- names come from the record header's sort-key offsets (clear, no unpack); the contact table
  (uniqueID + name, 28 bytes each) is cached in feature memory (`a35Creator`/3501, storage
  heap, ~8 KB for 300) and rebuilt only when Address35DB's modNum / record count / creation
  date / LocalID change; rows are resolved by uniqueID, so a re-sort can't misdirect a pick;
- only the picked contact's listed fields are decrypted, each with one prepared AES key
  schedule (`EncCryptFast`), never the whole record.
- Measured: open ~0.4 s from the cache (~1.0 s when it is rebuilt after a change; ~0.2 s of
  either is the ROM drawing the form), Find keystroke ~0.17 s, contact → popup ~0.26 s.

**Gotchas found building it (all POSE64-reproduced):**
1. A NewGlobals sublaunch's memory is freed by the OS when it returns. Anything the *system*
   allocates under it and keeps afterwards becomes a dangling pointer in the caller:
   - the caller field's blinking insertion point: its saved-bits window was re-created inside
     the sublaunch when Address35 called `FrmSetActiveForm(prev)` (and by any focused field),
     then freed again on the caller's next blink → `MemoryMgr.c, Invalid chunk ptr` from
     `InsPtCheckBlink`/`WinRestoreBits` in the caller's `EvtGetEvent`. Fix: the caller turns
     the cursor off before `SysAppLaunch` and re-activates its own form afterwards; Address35
     never re-activates the caller's form, and its Find is a NONEDITABLE field whose keys
     `Lookup.c` handles itself (no focus, no insertion point, no Field undo).
   - for the same reason the form has no `MENUID`.
2. `SysAppLaunch`'s 32-bit result came back with the high word zeroed - the locked signal
   travels in the caller's own parameter block instead.
3. A `static const` array inside a sec002 function is addressed A5-relative (as data) and
   reads garbage; use a string literal (PC-relative) - the label table is one.
4. The result handle is given owner 0 (`MemHandleSetOwner`) so it survives the sublaunch; the
   caller frees it.

**Limits:** the cached table holds up to 800 contacts (`lkMaxContacts`, ~22 KB); a contact's
popup lists up to 16 items (`lkMaxItems`).

**Crash testing (POSE64, 2026-10-04)** with the memory checks on (free/unlocked chunk access,
low stack, stack overflow, sizeless objects, MemMgr semaphore), plus an idle-blink/next-day/menu
check after each flow - all clean: pick via popup, single-item contact, popup dismissed then
Cancel, Paste button, Find overflow/backspace/filter/paging, empty and over-long lookup word,
a 20-phone contact, unnamed / "Other"-social / long-value / email-only contacts, lookup after
the Edit menu, no field focused ("Select Item"), 10 lookups in a row, Datebook35 Note view,
a hard app button with the list or popup open, silkscreen Find/Menu/Calc/Home, power off/on,
Sec35 auto-lock during a lookup, and ToDo35 List and Note views.

Segments after this (32,768-byte limit): Address35 main 0x7FAC (84 free, +32), sec002 0x2A50
(21,936 free); Datebook35 main 0x7A74 (1,420 free; helper + beam guard), sec001 0x7FA4 (92
free); ToDo35 main 0x7BD0 (1,072 free, +8), sec002 0x4878 (helper; 14,216 free).

## 11. File map

```
apps/address35/
  Makefile                -palmos4 multi-seg build (main + sec001/2/3); make clean on SEC moves
  Address35.def           application Ad35 + multiple code { "sec001" "sec002" "sec003" }
  a35_seg.h               SEC001(UI)/SEC002(SD)/SEC003(crypto), SEC_RECV(main), DATELIB_SECTION
  Address35.c             app core, PilotMain, launch gate, beam-inbox handler, merge, CatSync
  MainForm.c              list view, category popup (+Card), #7 cycle, Font, inbox merge on open
  EditForm.c EditDialogs.c  inline-editable edit form + per-field value dialogs
  RecordForm.c PrefsForm.c  read-only detail (+Duplicate) + prefs (surname term + sort order)
  AddressDB35.{c,h}       own TLV record DB; encrypt/decrypt/seal, sort, inbox DB helpers
  enccore.c aes.c         bundled Sec35 shared-key AES-128-CTR (sec003; copies of lib/)
  vcard.c vcardw.c Transfer.{c,h}  vCard 3/4 parse(main)/emit(sec001)/beam+inbox+merge
  AddrCard.{c,h} CardIO.{c,h}       SD-card vCard browse/import/export (VFS)
  Lookup.c                Phone Lookup for other apps (sec002; frmLookup) - §10
  AddressRsc.h Address35En.rcp      resource ids + forms
apps/lib/  (shared)       catsync, hackcheck, edate_override.h, era, enccore.{c,h}, aes.{c,h}, sects.h,
                          a35lookup.{c,h} (the caller side of Phone Lookup, built into Datebook35/ToDo35)
```

Two databases: **Address35DB** (`DATA`/`Ad35`, the real book) and the transient
**Address35DBPlain** (`Ad3P`/`Ad35`, beam-receive inbox — created + consumed automatically).
Address35 stays in `apps/address35/` only — never copy the PRC into other app folders
([[address35-keep-in-own-folder]]).
