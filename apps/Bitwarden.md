# BitWarden — PalmOS Bitwarden/Vaultwarden client (reference & rebuild guide)

A **read-only (v1)** Bitwarden/Vaultwarden client for the HackMaster Y2K35 suite. Unlike
every other app in this suite, BitWarden does none of its own network/TLS/API work: it
talks over a raw serial link to **WiRSa** (an ESP32-S3 relay, `bitwarden_relay.cpp` — see
`WiRSa-Palm.md`), which does the real Vaultwarden login/`/api/sync` fetch/decrypt work
server-side and streams already-decrypted items to the Palm one at a time. Creator
`BWrd`, DB `BitWardenDB` (type `BWit`), **not** backed up by HotSync (deliberate — see §5).
Paths relative to `hackmaster_y2035/apps/bitwarden/`. Protocol spec: `PROTOCOL.md` in that
same directory (this doc summarizes it; that file is the source of truth for wire format).

Two independent encryption layers protect the local on-device store: this app's own
master-password-derived key (`Store.c`), wrapped a second time by the suite-wide **Sec35**
shared key. **Sec35 is mandatory** — BitWarden refuses to launch at all if Sec35 isn't
installed (unlike CryptoNote/Datebook35's optional integration).

## 1. What BitWarden is

List/search/filter (by folder, organization, item type, live text search) over a locally
cached, encrypted-at-rest copy of a Bitwarden vault: Logins (with live TOTP), Secure Notes,
Cards, and Identities. Read-only in v1 — no create/edit/delete, no writing back to
Vaultwarden. A "Sync Now" replaces the entire local vault with a fresh full fetch every
time (no incremental merge). TOFU (trust-on-first-use) certificate pinning for the
Vaultwarden server; Vaultwarden's own 2FA (TOTP/email) is supported mid-sync, with a
remember-this-device mechanism so it isn't required on every sync.

**Two independent crypto layers** (`PROTOCOL.md`): the cradle-serial link between the Palm
and WiRSa is encrypted (since 2026-09-30, shared with CalDAV Sync: an RSA-1024 key exchange
→ AES-128-CTR on every frame, `../lib/relaysec.c`, no plaintext fallback — §4), so the
master password and decrypted vault fields no longer cross the wire in cleartext; and the
local vault is encrypted at rest (§5). Don't confuse the two. The originally planned
`KEX`/`KEX_ACK` (0x09/0x0A, X25519) design was retired in favour of the shared relay scheme.

## 2. Build & segmentation — READ THIS FIRST

Multi-segment via `bw_seg.h` (`SEC001` = `__attribute__((section("sec001")))`, force-included
via `-include bw_seg.h` on every compile unit, same mechanism as every other app in this
suite). Build with plain `make` (mandatory `-palmos4`, same "far call" reasoning as every
other multi-segment app in this suite — see `CryptoNote.md` §2.1 for the underlying
toolchain mechanics if unfamiliar).

**This app is now full on BOTH budgets it has** (confirmed by direct build, not estimated;
2026-10, after serial-link encryption):

```
$ make clean && make 2>&1 | tail -5 && m68k-palmos-size BitWarden
   text    data     bss     dec     hex filename
  42044     236   31524   73804   1204c BitWarden
```
Breaking `text` down by linker region (`m68k-palmos-objdump -h BitWarden`):
- **`sec001` code region: 9376 / 32768 bytes** — 23392 bytes free. Plenty of headroom.
- **`.text` (main `coderes` region): 32668 / 32768 bytes — only ~100 bytes free.** This is
  the tight one.
- **`.data`+`.bss` (`datares` region): 31760 / 32767 bytes — ~1007 bytes free.** Was
  razor-thin (tens of bytes) mid-session; recovered by shrinking two oversized buffers to
  their exact computed worst case rather than round numbers (see §7).

**Practical consequence**: any future change that adds code to the main segment (a new
string literal, a new function, even a few extra bytes of a format string) can fail to
link with `region coderes is full`. There is essentially no slack left. Before adding
*any* new main-segment code, either (a) tag the new function `SEC001` if it's a
leaf/low-frequency call (there's ~23KB free there), or (b) shrink something else first and
verify via the exact build+`m68k-palmos-size` commands above — don't guess.

### 2.1 The SEC001 rules this app converged on (four independently-found real crashes)

Same base rule as the rest of the suite: **SEC001 code calling back into a same-file,
untagged main-segment function is the only unsafe direction.** `main → SEC001` (any file),
and `SEC001 → SEC001` (same file), are both safe. `SEC001 → any different file` is safe
regardless of the callee's own tag. This app hit every plausible variant of the *unsafe*
direction, on real hardware or in POSE64, and settled on three different mitigation
strategies depending on the situation:

1. **A missing tag on a shared-header *declaration*, not just the definition, is unsafe —
   found three separate times.** The compiler decides near-call (same segment) vs. far-call
   based on what it can see *at the call site*, i.e. the header declaration a caller
   actually includes — not the real definition's own tag. `Wire.h`'s original
   `wU8`/`wU16`/... declarations lacked `SEC001` even though `Wire.c` tagged the real
   definitions; the very first cross-file call from `Sync.c` produced a confirmed POSE64
   wild jump (`"BitWarden just changed the emulated program counter to 0x67A80000... This
   address is invalid"`). Found and fixed again on `BitWarden.h`'s own `BwSerialSendFrame`
   declaration (arguably the dominant real-device crash all session, since it's called on
   every single frame send), and again on `BwProgressNotePenUp`/`BwProgressCancelled`.
   **Fix**: tag the declaration everywhere it's visible to a caller, not just where it's
   defined — verified afterward with a `grep` sweep of every public `SEC001` definition
   against every header declaration.
2. **Many small, mutually-calling SEC001 leaf functions in the SAME FILE as their only
   caller, despite matching the "documented safe" SEC001→SEC001-same-file shape, corrupted
   real outgoing data anyway.** `Sync.c`'s own inline `wU8`/`wU16`/`wU32`/`wBytes`/`wStr`/
   `rU8`/`rU16`/`rBytes`/`rStr` family, tagged SEC001 alongside their only caller
   (`BwSyncRun`, also in `Sync.c`), produced WiRSa-side "checksum mismatch"/"dropped
   oversized frame" garbage — real corrupted frame content, not a clean crash. The
   toolchain-level reason was never nailed down, but the empirical fix was unambiguous:
   moved every one of those functions to its own file (`Wire.c`/`Wire.h`), so every call
   from `Sync.c` became cross-file — the one shape proven safe throughout this whole
   project without a single exception. Lesson: don't fully trust "SEC001→SEC001 same file
   is safe" for a *cluster* of small, hot, mutually-recursive helpers even when it's
   individually documented safe — cross-file is the only pattern with zero known failures.
3. **When neither of the above applies, just inline the logic instead of calling it.**
   `MainForm.c`'s `MainRebuildMatches` (SEC001) needed two small same-file static helpers
   (`PassesAllFiltersRaw`/`NameMatches`); rather than risk the same-file-SEC001-call
   direction a third way, both were inlined directly into `MainRebuildMatches`'s own body
   instead of called, with a comment explicitly citing the two other confirmed instances
   of this bug class (`Store.c`'s and `RecordForm.c`'s own history comments, both
   referencing a debug function — since removed — that crashed live in POSE64 doing
   exactly this).

**Net effect**: within this codebase, prefer *cross-file* calls into SEC001 code over
same-file SEC001 clusters, even when the latter is nominally "documented safe" — the
empirical evidence favors the more conservative pattern.

**Current real-build budget (2026-09, after the sync-chime feature)**: main `.text` is at
`32764`/`32768` bytes — **4 bytes free**. `sec001` has ~26.7KB free. Any new BitWarden
feature's code must be `SEC001`-tagged from the start (or land in a new file compiled
already-tagged) — main has no room left for anything, confirmed via a real `m68k-palmos-gcc`
build + `objdump -h`, not an estimate.

### 2.2 Reclaiming budget — the concrete techniques actually used

- **Size a buffer to the literal computed worst case, not a round number.** `Sync.c`'s
  `gWireBody` was `4096` (a generic round cap); the true worst-case `SYNC_ITEM` (an
  Identity item — 18 `bwMaxExtField`(128) fields = 2340 bytes, plus a 1072-byte
  header/name/notes) is exactly `3412` bytes. Shrunk to that exact number — freed 684
  bytes with zero real capacity loss (the existing "drain and drop oversized frame"
  fallback in `BwSerialRecvFrame` still protects against anything unexpectedly larger).
- **Share one static buffer between two mutually-exclusive code paths instead of two.**
  `Sync.c`'s `gNewTokenBuf` (`bwTwoFactorRememberMax` bytes) is used by both the
  `SYNC_DONE` and `SYNC_ERROR` receive-loop branches — they can never be live at the same
  time (one frame type or the other), so one shared buffer is exactly as safe as two
  separate ones and saves a full `bwTwoFactorRememberMax`-sized allocation.
  `MainForm.c`'s per-entry folder/org-popup name width was also trimmed 40→24 chars — safe
  because every write into it already goes through an explicit-capacity copy that
  truncates gracefully, and the popup's own on-screen width was already clipping well
  short of 40 characters regardless.
- **Single-character diagnostic codes instead of descriptive strings, when even a handful
  of bytes matters.** A real-device diagnostic needed to distinguish which of 5 call sites
  produced an identical "Lost contact with WiRSa during sync" alert. The first attempt
  (full descriptive suffixes like `"waiting for next frame"`/`"folder ack"`, even funneled
  through one shared helper function) still pushed `coderes` over its cap. Reduced to
  single letters (`'R'`/`'F'`/`'O'`/`'I'`/`'T'`) formatted via one shared `StrPrintF` call —
  this is what actually fit.
- **A one-byte prefs-block format version, bumped on any layout change, triggers a
  one-time full wipe rather than a link-time budget fight.** See §5's `bwStoreFormatVersion`
  — a distinct technique for a distinct problem (on-disk format drift, not link-time size),
  but the same "confirm via a real number, not an assumption" discipline applies: a prefs
  struct size change silently wipes server URL/email/2FA-remember/local-vault-salt on next
  launch (see the real incident in §7's gotcha list), so know this before growing that
  struct.

## 3. Protocol (see `PROTOCOL.md` for the byte-exact spec)

One request/response frame in flight at a time (`type`, `uint16 bodyLen`, `body`, 1-byte
XOR checksum), big-endian, strings as `uint16 length` + raw UTF-8 bytes. `HELLO` →
(optional `TRUST` for first-time TOFU pinning) → `SYNC` (carries the master password and
`rememberedTwoFactorToken` in one frame) → a stream of `FOLDER`/`ORG`/`SYNC_ITEM` frames →
`SYNC_DONE` or `SYNC_ERROR`.

**Flow control**: WiRSa blocks on the Palm's `ITEM_ACK` after every single `FOLDER`/`ORG`/
`SYNC_ITEM` before sending the next one. This exists because WiRSa's original free-running
burst (no per-frame wait) overran the Palm's small, fixed Serial Manager receive buffer once
the Palm's own per-item `DmWrite`/`EncCrypt` work fell behind the incoming byte rate —
corrupting frames outright, or worse, silently dropping an item onto the Palm's tolerant
"unknown frame type, keep waiting" fallback with no error at all.

**Retry design — retry a single missed ack IN PLACE, not the whole sync.** Both sides retry
a missed/slow `ITEM_ACK` up to 5 attempts, resending the exact same frame and re-waiting, no
reconnect — `bwWriteFrameAcked` (WiRSa) / `SendItemAckWithRetry`+`RecvFrameWithRetry`
(`Sync.c`), matching cadences (~10s per attempt on WiRSa, ~12s on the Palm once a sync is
already flowing) so neither side's patience runs out well before the other's. **This was
NOT the original design** — the first version retried by having WiRSa reconnect and refetch
the *entire* `/api/sync` response from Vaultwarden, resuming via skip counters. That turned
out to be the wrong level for a single missed ack: slow (a full reconnect+refetch for what
might be a one-frame hiccup), and actively harmful once the Palm had *already* given up and
closed its serial port — WiRSa kept retrying into a connection nothing was listening to
anymore. That whole-resync retry loop still exists (still 3 attempts) but is now reserved
for a genuinely different failure domain: a Vaultwarden-side stream stall or malformed-JSON
response, where reconnecting and talking to Vaultwarden again can actually help. An ack
failure is now always terminal at that level.

**2FA-remember-token propagation — a real, once-confirmed gotcha.** `SYNC` carries
whatever `TwoFactorToken` the Palm has cached from a prior login (`rememberedTwoFactorToken`
— sent as `twoFactorProvider=5`, Bitwarden's own "Remember" pseudo-provider, on the very
first login attempt of a sync); `SYNC_DONE` carries back a possibly-rotated replacement.
Vaultwarden **rotates (invalidates) this token the moment login succeeds**, independent of
whatever happens to the rest of the sync afterward. The token was originally only ever
reported back via `SYNC_DONE` — meaning any sync that failed *after* a successful login but
before full completion (a mid-progress lost-connection, a Sec35 lock, anything) silently
discarded the one token that was actually still valid, guaranteeing a fresh interactive 2FA
prompt on the very next attempt even though 2FA had just been completed. Fixed two ways:
`SYNC_ERROR` now carries the same field (empty = no change, same convention), *and* every
one of the Palm's own abort paths (which used to send `BYE` and close the port immediately)
now makes one short (3s), best-effort attempt to catch a trailing `SYNC_ERROR` before
actually closing — see `Sync.c`'s `TryCatchTrailingToken`.

## 4. File-by-file architecture

- **`BitWarden.c`** — `PilotMain`/`StartApplication`/`StopApplication`/`EventLoop`. The
  Sec35-mandatory launch gate (hard refuse if Sec35 isn't installed at all; bounce to
  Sec35's own unlock UI via `SysUIAppSwitch` if it's merely locked). Wall-clock sleep
  detection (`gLastActiveTime`/`bwSleepGapSeconds`) as a hardware-independent backstop
  since the `sysNotifySleepNotifyEvent` callback alone wasn't reliably observed firing on
  real devices.
- **`BitwardenSec35.c`** — `BwSec35Installed`/`BwSwitchToSec35`/`BwSec35LockedBounce`,
  mirroring `apps/cryptonote/CryptoSec35.c`'s own shape exactly. Simpler than CryptoNote's
  version: every BitWarden record is Sec35-wrapped unconditionally (no legacy unwrapped
  data to distinguish, since a one-time format-version wipe already guarantees this).
- **`Store.c`** (largest file) — the local at-rest store: `BwPrefsBlock` (server URL, TOFU
  pin, salt/rounds/check, 2FA-remember token, format version), the on-disk record format,
  the sync write path (temp-DB-then-atomic-rename), the read path, and three separate,
  independently-lifetime-managed caches (header cache, name cache, this app's own local
  session key + cached master password). See §5.
- **`Sync.c`** — orchestrates one `HELLO`/`TRUST`/`SYNC` exchange end to end: the receive
  loop, all five retry/diagnostic helpers (§3, §7), and the master-password/2FA UI
  round-trips. Owns three large static scratch buffers (`gWireBody`, `gSyncWBuf`,
  `gNewTokenBuf`) — deliberately *not* stack-local, see §7's stack-overflow gotcha.
- **Serial framing — shared `../lib/relayproto.c`/`relaywire.c`** (merged with CalDAV Sync
  2026-09-30, so future serial encryption has one place to change). `relayproto.c` does
  open/baud change/close, the polled `ReadExact`, `RpSendFrame` (`sec001` via `RP_SECTION` in
  `bw_seg.h`) and `RpRecvFrame`, plus the idle-timeout override (`gBwIdleOverrideTicks`) and the
  explicit 1KB receive buffer added after a real-device driver overrun (§7). `relaywire.c` is the
  PROTOCOL.md body writer/reader, still in its own file so every call is cross-file (§2.1).
  `BitWarden.h`/`Wire.h` alias the historical names (`BwSerial*`, `BwW`/`BwR`, `wU8`…`rStr`), so
  no call site changed. App settings live in `relay_cfg.h`. **`Serial.c`** now holds only
  `BwSerialPoll`, the auto-off-reset + cancel-detection hook called during every blocking read.
  Verified by object comparison: every caller and every moved function compiles to the same
  instructions as before, with identical section sizes.
- **Serial-link encryption — shared `../lib/relaysec.c`** (2026-09-30, `sec001`): the key
  exchange runs first in `BwNegotiateBaud`; every later frame is AES-128-CTR encrypted inside
  `relayproto.c`. No plaintext fallback: a failed exchange shows "Serial link cannot be
  secured!" and ends the sync before the vault is touched (no reply keeps "WiRSa did not
  respond…"). Wire format in `PROTOCOL.md`. To make room in the full main segment,
  `BwTwoFactorPrefsPromptRun` (Options > Security > 2FA) moved to `sec001` (only OS traps and
  cross-file calls; checked in POSE). The Palm's RSA step (e = 3, three Montgomery
  multiplications) measured 0.25 s on an emulated 16 MHz Palm III; real devices are slower.
  POSE-verified end to end against an independent Python WiRSa (key exchange, encrypted
  HELLO/HELLO_ACK/TRUST, decrypted error reply).
- **`MainForm.c`** — the list/search/filter screen: folder/org popup, type popup, live
  text search with a lazily-built lowercase name cache, `MainDrawRow`'s masked-vs-
  decrypted row rendering, per-item-type icons, the Options/Record/Security menus
  (Lock Now, Delete Account, Archived Items toggle, 2FA entry-mode picker).
- **`RecordForm.c`** — four purpose-built read-only forms (Login/Note/Card/Identity,
  sharing one `RecordFormHandleEvent` that dispatches on `FrmGetActiveForm()`), a dynamic
  one-row-per-populated-field layout (not a fixed layout with hidden rows — Identity alone
  has up to 18 possible fields), Show/Hide masking for the one sensitive field per type
  (password/card number/SSN), and a live TOTP countdown ticked once a second.
- **`PrefsForm.c`** — server URL/email, Sync Now, Forget Trusted Server, Lock-After TTL,
  baud rate.
- **`Dialogs.c`** — every transient modal: master-password entry (masked, Sec35-style
  reveal-then-hide), TOFU trust prompt, 2FA code entry (plain text field *or* a numeric
  keypad popup), the sync progress dialog + sawtooth progress bar + Cancel button. All
  hand-rolled blocking event loops (not `FrmDoDialog`) — same proven `lazyFTP`
  `PromptForm.c` finding reused here: `FrmDoDialog` doesn't reliably reach a custom
  handler's `frmOpenEvent` on this SDK/glue.
- **`BitWardenRsc.h`** — resource IDs and pure-integer constants only (no `PalmOS.h`,
  since pilrc's `.rcp` parser can't handle a real `#include <PalmOS.h>`) — same split
  Address35 already uses. Also the `bwField*` index scheme: **never insert a new field
  index in the middle of the existing list** — the on-disk nonce is `(recordUID,
  fieldIndex)`, so shifting indices silently corrupts every already-synced record's
  decrypt. New fields must always be appended right before `bwFieldCount`.

## 5. Data model / at-rest crypto

**Two layers, always both, never one without the other.** Every field write goes through
`EncCrypt(bwKey, ...)` (this app's own master-password-derived key) *and then*
`EncCrypt(sec35Key, ...)` (the shared Sec35 key) — Sec35 wraps, it never replaces, this
app's own scheme. Every read unwraps in reverse: Sec35's layer first (if `EncGetKey` fails,
i.e. Sec35 is locked, the function returns immediately without even attempting the inner
layer — this is the actual, single enforcement point that makes every list row/record view
show masked `******` the instant Sec35 locks, not a per-caller check). The nonce for a real
item's field is `(recordUID, fieldIndex)`; folder/org marker records reuse a small set of
reserved sentinel `nonceHi` values (`bwFolderNonceHi`/`bwOrgNonceHi`/etc.) chosen because a
real PalmOS `DmUniqueID` is only 24-bit and can never collide with them.

**On-disk record header is a fixed-size, mostly-plaintext prefix** (item type, folder/org
index, GUID, per-field ciphertext lengths, archived flag) followed by field ciphertexts
back to back. `folderIndex`/`orgIndex`/`itemType`/`archived` are deliberately **plaintext**
even at rest — this is what lets list filtering (folder/org/type/archived) run without
decrypting every candidate record, which matters a great deal at real vault scale on
16-33MHz hardware (see the performance-history notes throughout `Store.c`).

**Format-version bump → one-time full wipe, not a migration.** `bwStoreFormatVersion`
(currently `4`) is bumped every time the on-disk record layout changes shape (Sec35-wrap
added, Card/Identity fields added, the archived byte added) — `BwStoreInit` compares the
stored version against the compiled one on every launch and does a full
`BwStoreBeginSync()`-style wipe if they don't match, exactly once, before bumping it. This
exists because an old-format record's header is a *different byte layout*, not just stale
data — reading it with the new layout misinterprets content bytes as length-prefix bytes
and vice versa, not merely showing wrong values.

**Sync write path is temp-DB-then-atomic-rename, not wipe-then-fill.** `BwStoreBeginSync`
builds into a completely separate `BitWardenDB_new` database; only `BwStoreEndSync` (called
once a sync has *fully* succeeded, on `SYNC_DONE`) deletes the real DB and renames the temp
one onto it. Any failure/cancellation anywhere before that calls `BwStoreAbortSync` instead,
which discards the temp DB and reopens the real one completely untouched. This replaced an
earlier design that wiped the real database immediately at the start of every sync — a
cancel or failure at any point after that left only whatever partial content that one
attempt had managed to stream in, often nothing (§7).

**Two separate session caches, with different TTLs and different wipe triggers — conflating
them was a real bug.** (1) This app's own local-vault key (`BwGetKeyInternal`/
`BwCacheKeyInternal`, Feature Manager, own TTL via `BwGetTtlSeconds`/"Lock After"). (2) The
*cached master password itself* (`BwGetCachedPassword`/`BwSetCachedPassword`, also Feature
Manager, RAM-only, Sec35-key-encrypted, same TTL setting but a genuinely separate storage
slot) — lets `MainOpen`'s local-unlock prompt and `BwSyncRun`'s own prompt skip re-asking as
long as either one has succeeded recently. **"Lock Now" (`BwLock`) originally only wiped
(1)** — tapping any masked record immediately re-derived the key from the *still-cached*
password with zero re-prompt at all, defeating the entire point of an explicit lock action.
Fixed: `BwLock` now wipes both, while the passive sleep/app-exit path (which deliberately
does *not* wipe the cached password — a 2026-08 UX decision, "Lock After" TTL is the only
intended expiry there) is untouched, since both of `BwLock`'s only two callers are already
explicit user actions (Lock Now, Delete Account).

**Two independent name/header caches, deliberately not one.** `gTypeCache`/`gFolderCache`/
`gOrgCache`/`gArchivedCache` (plaintext header fields, cheap, built on every list rebuild
regardless of whether a search is active) vs. `gNameCache`/`gDisplayNameCache` (decrypted
names, real AES cost, built lazily only on the *first* search). Keeping them separate is
what lets opening the plain list (no search) stay cheap even at real vault scale — building
them together would force a full-vault decrypt on every plain list open, even for a session
that never searches at all. **A locked vault must never attempt to build the name cache at
all** — see §7's `MatchNameCompare`/Lock-Now-hang gotcha for what happens if that
invariant is violated even indirectly.

## 6. UI/UX flows

- **List masking**: `MainDrawRow` checks `!EncKeyPresent() || !BwIsUnlocked()` (both
  layers) before attempting any decrypt at all, drawing a fixed `"******"` instead. This is
  the sole gate; nothing downstream of it ever gets a chance to touch ciphertext while
  locked.
- **Sec35-locked bounce**: mirrors CryptoNote's own pattern — `BwSec35LockedBounce`
  (`!EncKeyPresent()` → `SysUIAppSwitch` to Sec35's unlock UI, which switches back on
  success). Checked at app launch (hard) and, implicitly, everywhere a decrypt would
  otherwise silently fail.
- **Sync progress + Cancel**: a modal progress form with a sawtooth progress bar (there's
  no true total item count until `SYNC_DONE`, so it fills 0→full every 20 items and snaps
  back, rather than stalling visually looking "finished" partway through a 300+-item vault).
  Cancel is detected via a manual `penDownEvent`/`penUpEvent` bounds check (not dispatched
  through `FrmHandleEvent` — that crashed a lazyFTP equivalent once already) requiring
  **both** the pen-down and pen-up to land inside the button's own rectangle, matching real
  PalmOS Control Manager semantics and ruling out a stray tap or a drag merely passing
  through it (a real regression found and fixed mid-session).
- **2FA entry**: either a plain text field or a numeric keypad popup (Options > Security >
  2FA), mirroring Sec35's own `frmKeypad` layout without the phone-letter decorations.
  "Remember this device" is transparent to the user — see §3. The keypad's 10 digit/Del
  keys are GADGETs (manual `penDownEvent`/`penUpEvent` hit-testing, not real Controls), so
  unlike a normal Button they never reached the Control Manager's own automatic tap click —
  fixed by calling `SndPlaySystemSound(sndClick)` by hand at the same point the visual
  press-invert already happens (`Dialogs.c`, `BwTwoFactorKeypadRun`), 2026-09. Exact mirror
  of Sec35's own `frmKeypad` fix (`sec35.c`, `KeypadFormHandleEvent`) — see `DESIGN.md` §10.
- **Sync start/end chime**: a 3-note tone plays when a sync begins and another when it ends
  (`Sync.c`'s `BwPlaySyncChime`, called from a same-file `SEC001` `SyncNowWithChime` wrapper
  in both `MainForm.c` (Options > Sync) and `PrefsForm.c` (Preferences' own Sync Now
  button) — not from inside `BwSyncRun` itself, since that function has several early
  "prerequisite not met" returns (no server URL, cancelled unlock prompt) before any real
  sync activity begins, which shouldn't chime as if a sync had actually started). Raw
  `SndDoCmd`/`sndCmdFreqDurationAmp` (present since PalmOS v1), amplitude read live from
  `SndGetDefaultVolume`'s `sysAmpP` so it follows the device's actual System Sound level
  (Off/Low/Medium/High) rather than always playing at max — `SndDoCmd` has no built-in
  volume/preference awareness, unlike `SndPlaySystemSound`. Note-for-note match of CalDAV
  Sync's own `PlaySyncChime` (`caldav_sync/MainForm.c`), added first and used as the
  reference: start = 1400 Hz/75 ms, 1500 Hz/113 ms, 2400 Hz/450 ms; end = 2400 Hz/75 ms,
  1800 Hz/113 ms, 1200 Hz/450 ms. 2026-09.
- **TOFU trust prompt**: full-screen (a small modal literally couldn't fit the Yes/No
  buttons on some devices — unreachable, blocking every sync until fixed), shows the
  SHA-256 fingerprint + subject/issuer CN, stores the accepted fingerprint keyed by server
  URL.

## 7. Known gotchas / real-device bug list (chronological)

Each entry: symptom as reported, root cause, fix, how it was confirmed. Several first
attempts here were wrong or had to be reverted outright — kept in, not softened, since the
wrong turn is often as instructive as the fix.

1. **"Palm lost mid-sync" data corruption / vault-wide data loss on cancel.** WiRSa's
   original free-running burst overran the Palm's serial buffer → added `ITEM_ACK`
   flow-control (§3). Separately, cancelling or any mid-sync failure used to leave the
   vault **completely empty**, since the old `BwStoreBeginSync` wiped the real database
   immediately at the start of every sync. Fixed with the temp-DB/atomic-rename design
   (§5).
2. **Storage-heap fragmentation from record-by-record wipe.** The old per-sync wipe called
   `DmRemoveRecord` once per existing record (~780 on the real test account) — freeing
   variously-sized chunks one at a time left the heap a patchwork of gaps, and a
   subsequent `DmNewRecord` was measured stalling **100+ seconds** (confirmed to exceed
   even this device's own maximum 3-minute auto-off setting — no amount of resetting a
   countdown *before* a single uninterruptible ROM call can help once that call's own
   worst case exceeds the ceiling itself). Fixed at the root by the delete-whole-DB
   temp-swap design (§5) plus a periodic `MemHeapCompact` every 10 items
   (`BwStoreCompactIfDue`) to keep individual `DmNewRecord` calls fast going forward.
3. **Wild-jump crashes from missing `SEC001` tags — found three separate times.** See
   §2.1. Confirmed via an actual POSE64 crash dump showing the emulated PC jumping to an
   invalid address.
4. **Same-file SEC001 helper cluster corrupting outgoing frame content.** See §2.1 — Wire
   functions moved to their own file after real garbage frame types/lengths were observed
   on WiRSa's side.
5. **Sec35 launch-gate false "not installed" error.** The original gate also hard-refused
   whenever `EncEncryptionActive()` (a Feature Manager flag) was false — but that flag is
   only *reasserted* when Sec35 itself has launched this boot session, so a real,
   previously-configured Sec35 password could still read as "not set" before Sec35 had
   ever been opened this session. Fixed by relying solely on `BwSec35Installed()` (a
   session-independent existence check) for the hard refuse, with everything else (locked,
   or simply not yet active) handled uniformly by the `EncKeyPresent()` bounce. Validated
   against `apps/cryptonote/CryptoSec35.c`'s own equivalent mechanism before trusting the
   diagnosis.
6. **Silent data loss on Sec35/vault lock mid-sync.** `AddMarker`/`BwStoreAddFolder`/
   `BwStoreAddOrg`/`BwStoreAddItem` were `void` functions that silently no-op'd whenever
   either key was unavailable — a mid-sync lock meant every subsequent item was silently
   dropped while the Palm kept acking WiRSa as if all were saved, and the sync still
   reported success at the end. Fixed by converting all four to return `Boolean` and
   having `Sync.c` treat a `false` return as fatal (abort via `BwStoreAbortSync`, alert the
   user).
7. **2FA-remember token silently truncated — twice, at two different caps.**
   `bwTwoFactorRememberMax` started at 128, was bumped to 256 after real-device testing
   showed the same "2FA every sync" symptom persisting (Vaultwarden's actual token value
   being silently truncated on receipt and again at rest — a truncated token can never
   validate again). Later, once the *real* remember-token bug (#11 below) was fixed and
   Vaultwarden actually started issuing tokens, the real measured length was **596 bytes**
   (it's a JWT) — 256 still truncated it. Bumped to 608, the largest value that still fit
   the linker's own `datares` cap (see §2 — this constant scales into *three* separate
   static buffers, so every extra byte costs three there).
8. **A launch-time hang that looked like the 608-byte bump's fault, but wasn't.** Right
   after that bump, the app hung on launch. The datares-vs-real-device-heap theory this
   prompted (reverting the bump) turned out to be a **red herring**: the user deleted a
   stale/leftover local database from an earlier interrupted sync and relaunched — it
   worked fine with the *same* 608-byte value back in place. Lesson (already learned once
   this session and reconfirmed): don't trust a plausible-sounding theory over a
   controlled retest, even under time pressure.
9. **A fully-built-then-fully-reverted OAuth refresh-token mechanism.** After confirming
   (via a diagnostic key-name dump of Vaultwarden's actual `/identity/connect/token`
   response) that `TwoFactorToken` was genuinely absent from this server's replies even
   with `twoFactorRemember` supposedly set, a whole parallel mechanism was built: a second
   remembered credential (an OAuth `refresh_token`), a new WiRSa-side `bwRefreshLogin`
   function trying it before the normal password+2FA path, new `SYNC`/`SYNC_DONE` wire
   fields, new Palm-side storage. **This was fully reverted once the real root cause (#11)
   was found** — the refresh_token mechanism was solving a problem that didn't actually
   exist once the real bug was fixed, and it was also a genuine memory-budget risk (OAuth
   refresh tokens are JWTs of unbounded-ish length; an attempt to size a receive buffer for
   one directly caused a `datares` overflow). Nothing of this mechanism remains in the
   current code — if reconsidering it, budget for that memory risk up front. WiRSa's own
   reference doc (`WiRSa-Palm.md`) still describes it as a design decision considered and
   rejected, for exactly this reason.
10. **`serErrLineErr`/`serLineErrorSWOverrun` — a genuine driver-level receive-buffer
    overrun, confirmed via real PalmOS error codes, not guessed.** "Lost contact" alerts
    were firing within ~10 seconds instead of anywhere near the intended multi-minute
    retry budget. Added diagnostics (`gBwLastRecvErr`/`gBwLastLineErrs`, surfaced directly
    in the alert text) turned a guess into a confirmed fact: `err=774` = `serErrLineErr`,
    `le=32` = `serLineErrorSWOverrun` — the Serial Manager's own driver-level receive
    buffer overflowed because the app's poll loop wasn't draining it fast enough for a
    burst that size. First fix attempt (`SrmClearErr` after detecting the error) helped
    but wasn't sufficient by itself. Root fix: an explicit `SrmSetReceiveBuffer` (§4,
    `Serial.c`) — the OS default was apparently too small, and no PalmOS SDK header
    documents what it actually is.
11. **The real 2FA-remember root cause: `two_factor_remember: Option<i32>`, not a bool.**
    Found by reading Vaultwarden's actual open-source Rust server code directly (not
    guesswork) after the refresh-token detour (#9) failed to reproduce reliably either.
    WiRSa was sending the literal string `"true"` for this form field; Rocket's form
    parser can't parse that as an integer, so it silently fell back to `None` — the rest
    of the request still parsed fine (login kept succeeding), so nothing *looked* broken.
    Fixed by sending `"1"` instead. This is the actual, complete explanation for "2FA every
    sync" — not a Vaultwarden-version limitation, not a truncation bug, not a missing
    refresh-token mechanism. See `WiRSa-Palm.md` for the firmware-side details.
12. **"Lock Now" appeared to hang the whole device — actually thousands of repeated failed
    decrypt attempts, not an infinite loop.** Bisected live via a sequence of temporary
    checkpoint alerts (single-character codes, added and removed in the same session,
    given the `coderes` budget — see §2.2) down to one specific line: `MainRebuildMatches`'s
    post-filter sort, whose comparator (`MatchNameCompare`) calls a function that
    force-builds the decrypted name cache on first use. Locked means that build fails
    *every single time* (no key available) — but the failure wasn't remembered anywhere,
    so **every individual name comparison during the sort re-attempted the full failed
    build from scratch**. At real vault scale (hundreds of items × O(n log n) comparisons
    × 2 lookups each), that's thousands of repeated failed attempts in a tight loop — slow
    enough on real hardware to be indistinguishable from a true hang, with no progress
    toast to explain it since the surrounding code assumed a locked rebuild was always
    trivial. Fixed by skipping the sort entirely when locked (sorting masked `******` rows
    by name is meaningless anyway).
13. **A security-review-found 16-bit integer wraparound in the wire reader.** `Wire.c`'s
    `rStr`: this compiler targets 16-bit `int`, so a naive `pos + n` bounds check can wrap
    silently. A malicious/compromised WiRSa (or a MITM before the TOFU pin is set) sending
    a crafted oversized length prefix could make the check pass when it should fail,
    reading out of bounds from adjacent static memory. Fixed with explicit `(UInt32)`
    widening before the comparison. Found in a dedicated security review, not real-device
    testing — a reminder that not every bug in this codebase surfaced via a crash report.
14. **Certificate fingerprint shown with four hex digits per byte.** `FormatFingerprintText`
   (`Dialogs.c`, used by both the "New Server" trust prompt and Prefs > Show) used
   `StrPrintF("%02X", byte)`; Palm OS `StrPrintF` ignores the field width and printed each
   byte as e.g. `0001`, so a fingerprint read `00010002 00030004 …`. Fixed (2026-10-01) by
   writing the hex digits directly; it now reads `0102 0304 …` (two bytes per group).
   POSE-verified on both screens.
15. **Blank vault list after a failed Options > Sync Now.** Every failure after
   `BwStoreBeginSync` closes the progress dialog first, and that redraw of the main list
   happens while the empty temp database is still open; `BwStoreAbortSync` swaps the real
   vault back only afterwards and nothing redrew the list (data intact - tapping a "blank"
   row still opened the record). Fixed (2026-10-01): the menu handler now calls
   `MainRefresh()` after every sync, not only a successful one. Preferences' Sync Now was
   unaffected (returning to the list runs `MainOpen`'s full rebuild). POSE-verified.
16. **Serial port left open when the local vault database can't be reset.** That path
   (`BwStoreBeginSync` returning false) showed its alert and returned without `BYE` or
   `BwSerialClose()`; nothing closes the port on app exit, so the cradle port could stay
   open (e.g. blocking HotSync). Fixed (2026-10-01) to match the neighbouring lost-contact
   path. Review-only (needs a storage-full condition to trigger).
17. **WiRSa sync-error alert titled "^1".** `altErr2`'s title was `^1`, but Palm OS only
   substitutes `^n` in an alert's message, not its title. Its one caller (`Sync.c`,
   `SYNC_ERROR`) passes "SYNC" for it; the title is now the fixed text "Sync Error".
18. **Wrong vault list after a cancelled or failed sync that had already received items.**
   Closing the progress dialog redraws the main list while the temp database is still open,
   which builds the name/header caches from the temp database's few records;
   `BwStoreAbortSync` then reopened the real vault without clearing those caches (unlike
   `BwStoreEndSync`), so the next list rebuild showed only that many rows, with wrong type
   icons, until the app was relaunched. Data was never affected. Found in the 2026-10-01 POSE
   review (Cancel after 2 items left 2 rows). Fixed by calling `BwStoreInvalidateNameCache()`
   at the end of `BwStoreAbortSync`.

- **Sync success alert (2026-10-01):** after a successful sync - once the end chime has
  played and the refreshed list is drawn ("Processing..." after Options > Sync Now,
  "Loading..." after Preferences' Sync Now) - an information alert "Bitwarden sync
  successful!" (`altSyncOk`). Failures and cancels keep only their existing alerts. Both
  `SyncNowWithChime` wrappers set `gBwSyncOkPending`; `MainShowSyncOk` (`sec001`, called after
  `MainRefresh` and after `MainOpen`) shows it once and clears it, and only if the vault is
  unlocked. Main segment 0x7f7c -> 0x7f98 with fixes 15-16. POSE-verified (menu and
  Preferences paths, record open/Done shows nothing, failure shows only its own alert).

## 8. Build/verification status

**Confirmed working on real hardware this session** (not just reasoned about): the
temp-DB/atomic-swap sync-cancel fix; the Sec35 launch-gate fix; the ack-retry-in-place
redesign on both sides; the shortened per-attempt idle timeout after a sync's first frame;
the `SrmSetReceiveBuffer` fix (pending final confirmation the exact chosen size, 1024
bytes, is sufficient for every real burst — it was grown past an earlier, more
conservative 640-byte value on explicit request rather than incrementally re-tested at the
boundary); the `twoFactorRemember=1` fix (confirmed via a real Vaultwarden response now
containing a 596-byte `TwoFactorToken`); the trailing-`SYNC_ERROR`-token-catch fix; the
Lock Now hang fix; the Lock Now cached-password fix.

**Serial-link encryption (§4)**: a real-hardware sync over the encrypted link succeeded
(user-confirmed 2026-10-10), after POSE64 verification against a scripted WiRSa.

**Not yet real-device confirmed / open**: the exact Vaultwarden response shape for an *incorrect* 2FA code (as
opposed to no code at all) was never verified against a live server. Whether 1024 bytes is
comfortably enough headroom for the serial receive buffer under a worst-case burst, or
whether it needs to grow further (which would require finding more `datares` budget first —
see §2).

**Memory headroom for future work, as of this doc**: `coderes` (main code segment) has
only ~100 bytes free — treat any new main-segment code as needing either a `SEC001` tag
(23KB free there) or a corresponding size reduction elsewhere, confirmed by an actual
build, not assumed. `datares` (static data) has ~1007 bytes free — tight but workable for
something small; anything larger needs a deliberate trim elsewhere first (see §2.2 for the
concrete techniques already used).
