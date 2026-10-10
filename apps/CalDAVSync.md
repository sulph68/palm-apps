# CalDAV Sync — PalmOS calendar sync client (reference & rebuild guide)

A Palm app that syncs read-only calendar events into Datebook35's Agenda/Day/Week/Month
views. Like BitWarden, it does none of its own network/TLS work: it talks over a raw serial
link to **WiRSa** (an ESP32-S3 relay, `caldav_relay.cpp` — see `WiRSa-Palm.md`, though that
doc currently has **no CalDAV Relay section** — a separate, pre-existing documentation gap
not addressed by this doc), which does the real HTTP(S)/CalDAV `REPORT`/OAuth work
server-side and streams already-parsed events to the Palm one at a time. Creator `CDDV`, own
prefs DB `CalDAVRelayDB`, and it writes into a *second* database, `DateBook35DAV.pdb`, which
Datebook35 reads directly (§8). Paths relative to `hackmaster_y2035/apps/caldav_sync/`.

Four calendar source types, one slot model, one wire protocol: **CalDAV-generic** (any
RFC 4791 server), **iCloud** (CalDAV with Apple's own shard-discovery/DSID quirks),
**Google** (Calendar API via OAuth 2.0 — the one type with an interactive sign-in flow
instead of a stored password), and **ICS Feed** (a plain `.ics` URL, no auth, WiRSa's own
recommended path per `README-Palm.md`). Up to `cddvMaxSlots` (8) independently-configured
calendars, each syncable alone (the **Sync** button, "Sync Now" in this doc) or all at once
("Sync All").

**Sec35 is mandatory**, same posture as BitWarden — this app refuses to launch at all if
Sec35 isn't installed, and bounces to Sec35's unlock UI if it's merely locked (§1, §5).

## 1. What CalDAV Sync is

A slot list (MainForm) is the whole top-level UI: pick a slot from a popup, see its type/
last-sync status, tap **Edit** to configure it (SlotEditForm, a focused modal with
per-server-type adaptive fields), **Sync** to fetch just that one, or use the Record
menu's **Sync All** to fetch every non-skipped slot in one pass. Global Preferences hold
default past/future sync-window and the serial baud rate; a per-slot override can widen or
narrow that window for just one calendar. A Help form (scrollable, full-page) documents the
whole flow for the end user.

`CalDAVSync.c`'s launch gate (mirrors `BitWarden.c` exactly):
```c
if (!CDDVSec35Installed()) { FrmCustomAlert(altErr, "Sec35 is not installed...", ...); return 0; }
if (!EncKeyPresent()) { CDDVSwitchToSec35(); return 0; }  /* bounces, re-launches on unlock */
```

## 2. Build & segmentation

Multi-segment via `cddv_seg.h` (`SEC001`–`SEC004`, force-included via `-include cddv_seg.h`
on every compile unit, same mechanism as every other multi-segment app in this suite). Build
with plain `make` (mandatory `-palmos4` — see `Bitwarden.md` §2 or `CryptoNote.md` §2.1 for
the underlying "far call" toolchain mechanics if unfamiliar).

**Segment map** (`cddv_seg.h`'s own header comment):
```
main   : PilotMain, launch dispatch, StartApplication/StopApplication, EventLoop only.
sec001 : app/UI - MainForm/SlotEditForm/PrefsForm/LongTextDialog/HelpForm/SyncProgress
         event handlers, dialogs, menu handling.
sec002 : prefs database - CDDVPrefs.c (CalDAVRelayDB) + CDDVSec35.c (Sec35 password/
         token encrypt/decrypt wiring, enccore).
sec003 : DateBook35DAV.pdb read/write - DAVDb.c (record CRUD, full-rebuild-then-atomic-
         rename sync, Recompute Times).
sec004 : communication - relayproto.c/relaywire.c/relaysec.c (shared wire framing + link
         encryption, via RP_SECTION) + Serial.c (CDDVSerialPoll) + SyncEngine.c (per-slot
         sync orchestration) + OAuthRelay.c (Google sign-in flow).
(enccore.c/aes.c, the Sec35 crypto, are in sec002 via ENCCORE_SECTION/AES_SECTION.)
```

**Real build, current state** (2026-10, after link encryption and the all-day fix;
confirmed via `make` + `m68k-palmos-objdump -h`, not estimated):
```
   text    data     bss     dec     hex filename
  30788    1280   31404   63472    f7f0 CalDAVSync
```
Per-segment breakdown:
- `sec001`: 3836 / 32768 bytes — **28932 free**
- `.text` (main): 11324 / 32768 bytes — **21444 free**
- `sec002`: 6676 / 32768 bytes — **26092 free**
- `sec003`: 1436 / 32768 bytes — **31332 free**
- `sec004`: 7516 / 32768 bytes — **25252 free**

Unlike BitWarden/Sec35/Datebook35 (all of which have hit single-digit-byte segment
margins at some point), **every CalDAV Sync segment currently has 20KB+ of headroom** —
this app has never needed a relocation-under-pressure fix the way those three have.

**The SEC00x rule this app follows** (same as the rest of the suite): a SEC00x function
calling back into an untagged (main-segment) function in the *same* `.c` file is the one
unsafe direction — inline it instead. `main → SEC00x` (any file) and `SEC00x → SEC00x`
(different file) are both safe. That is why this app's tiny wire-writer/reader helpers
(`cw8`/`cw16`/.../`crStr`) live in their own file rather than inlined into `SyncEngine.c` -
originally `CDDVWire.c`, now the shared `../lib/relaywire.c` (`CDDVWire.h` just aliases the
names to its `RpW*`/`RpR*` functions): real-device testing during the
sibling BitWarden project found that small, mutually-recursive helpers tagged into a
secondary segment alongside their only same-file caller corrupted outgoing frame content —
splitting them into their own file makes every call cross-file, the one shape proven safe.
**Declaration/definition segment-tag mismatches are the dominant real-device crash class**
in this whole family of apps — `CDDVSerialSendFrame` hit exactly this once (§7.1).

## 3. Protocol

One request/response frame at a time: `type(1B)` + `lenHi(1B)` + `lenLo(1B)` + `body` +
`checksum(1B XOR)` (shared `../lib/relayproto.h`), AES-encrypted after the link key exchange
(§4, "Serial-link encryption"). Bootstrap baud is a fixed `cddvBaudIdle` (9600) before
`CDDV_HELLO`/`CDDV_HELLO_ACK` negotiates up to the configured target — mirrors BitWarden's
own `HELLO`/`HELLO_ACK` idiom.

```c
typedef enum {
    CDDV_HELLO = 0x01, CDDV_HELLO_ACK = 0x02, CDDV_SYNC_REQUEST = 0x03,
    CDDV_EVENT_ITEM = 0x04, CDDV_ITEM_ACK = 0x05, CDDV_SYNC_DONE = 0x06,
    CDDV_SYNC_ERROR = 0x07, CDDV_OAUTH_START = 0x08, CDDV_OAUTH_INFO = 0x09,
    CDDV_OAUTH_TOKEN = 0x0A, CDDV_BYE = 0x0B
} CDDVFrameType;
```

**Flow control**: `SYNC_REQUEST` → a stream of `EVENT_ITEM` frames, each ACKed by the Palm
(`ITEM_ACK`) before WiRSa sends the next, up to `cddvItemAckRetries` (40) retries at 500ms
each — same overrun-prevention reasoning as BitWarden's identical `ITEM_ACK` scheme (§3 of
`Bitwarden.md`). Terminates in `SYNC_DONE` or `SYNC_ERROR`.

**OAuth handshake** (Google only, `OAuthRelay.c`) — a separate three-frame exchange, run at
idle baud (no `HELLO` negotiation — OAuth traffic is a handful of short strings, not a
calendar sync):
1. Palm sends `OAUTH_START` (empty body).
2. WiRSa replies `OAUTH_INFO` (its own IP, port, and a short-lived PIN) — or `SYNC_ERROR` if
   it can't start (e.g. no Client ID reachable yet).
3. Palm shows the IP/PIN and the exact `python3 google_auth.py --ip <ip> --pin <pin>`
   invocation for the user to run on their computer, then waits (bounded ~5 minutes,
   cancellable, reusing `SyncProgress.c`'s existing modal+Cancel machinery) for
   `OAUTH_TOKEN` (the refresh token, stored via `CDDVEncryptSlotSecret`) or a `SYNC_ERROR`.
   Always sends `CDDV_BYE` on exit, success or not.

**Per-sync clock**: `SYNC_REQUEST` includes the Palm's own current time (`cddvUnixToPalmEpoch`
conversion), so WiRSa computes the sync window from the Palm's clock instead of depending on
its own NTP succeeding — a lesson carried over from the BitWarden/CalDAV-relay real-device
history (§7).

**Event times**: `EVENT_ITEM` carries `utcStart`/`utcEnd` (Palm-epoch seconds) and an
`allDay` byte. For a timed event they are UTC instants, converted to the Palm's time zone on
receipt (`SyncEngine.c`) and again by Recompute Times (`DAVDb.c`). For an all-day event
(`allDay=1`) they are the event's **date** at 00:00 with no zone — what WiRSa produces from
an iCalendar `VALUE=DATE` (uICAL parses it zone-less) or Google's `"date"` field — and are
copied to `localStart`/`localEnd` unchanged, so an all-day event stays on its date in every
time zone, like a native untimed Datebook appointment. `utcStart` is a misnomer for these.

**Firmware side is out of scope for this doc** — see `caldav_relay.cpp` directly, and see
`WiRSa-Palm.md` §13 (CalDAV Relay mode, added 2026-09-28) for the firmware-side architecture,
protocol framing, uICAL extensions, and its own real-device bug-fix history.

## 4. File-by-file architecture

- **`CalDAVSync.c`** — deliberately thin: `PilotMain`/`StartApplication`/
  `StopApplication`/`EventLoop`, and the Sec35-mandatory launch gate (§1).
- **`CalDAVSync.h`** — shared types: `CDDVSlot`/`CDDVPrefsType` (§5), `CDDVServerType`,
  the past/future sync-window enums, all length-cap `#define`s. Resource IDs live
  separately in `CalDAVSyncRsc.h` (pilrc-generated, no `PalmOS.h` — same split
  `BitWardenRsc.h` uses, for the same pilrc-parser reason).
- **`MainForm.c`** — the slot list: popup slot selector, Edit/Sync/Clear DB buttons (Sync is
  `btnMainSyncNow`, "Sync Now" below), Record menu (Sync All, Preferences, Recompute Times, Delete Database, Help). Owns the sync
  start/end chime (§6).
- **`SlotEditForm.c`** (largest file, 844 lines) — one focused modal (`frmSlotEdit`) with
  four mutually-exclusive field groups (`gCalDAVFields`/`gICloudFields`/`gGoogleFields`/
  `gIcsFields`), shown/hidden by server type via `FrmShowObject`/`FrmHideObject` (hide the
  3 inactive groups *first*, then show the active one *last* — all four occupy the exact
  same on-screen rectangles). Long/URL-like fields (server URL, cal path, ICS feed URL,
  iCloud host/UUID, Google Calendar ID) are edited via `LongTextDialog.c`'s full-screen
  editor rather than a cramped single-line `FIELD` — the on-screen control still *looks*
  like an ordinary field (visual consistency with the rest of the rows), but intercepts
  `fldEnterEvent` to open the real editor instead of accepting direct keyboard input, the
  same idiom Address35's `EditForm.c` uses for its own "tap opens a sub-dialog" rows. Edits
  a local working copy (`gWorking`); OK writes it back to the caller's slot (encrypting any
  freshly-typed secret via `CDDVSec35.c`), Cancel discards it. Hand-rolled modal loop (not
  `FrmDoDialog` — same "doesn't reliably dispatch `frmOpenEvent`" finding as every other
  hand-rolled loop in this suite).
- **`PrefsForm.c`** — global past/future sync-window defaults and serial baud rate.
- **`SyncEngine.c`** — `SyncEngineRunOne(prefs, slotIdx, silent, keepOpen)`: negotiates
  baud, sends `SYNC_REQUEST` (with this slot's decrypted fields — server/path/uuid/user/
  alias/googleClientId/googleClientSecret/secret, plus the resolved past/future window and
  the Palm's own clock), receives+ACKs `EVENT_ITEM` frames into `DAVDb.c`, until
  `SYNC_DONE`/`SYNC_ERROR`/timeout. `keepOpen` lets a Sync All batch reuse one negotiated
  connection across slots instead of renegotiating per calendar (`gCddvConnNegotiated`
  latch). `silent` suppresses the modal failure alert and the Sec35-locked bounce, so one
  failed slot doesn't stall the rest of a batch.
- **Serial framing — shared `../lib/relayproto.c`/`relaywire.c`** (merged with BitWarden
  2026-09-30). Open/close/receive stay in main (tagging them `SEC0xx` crashed on a real
  device); `RpSendFrame` and the body writer/reader are in `sec004` via `RP_SECTION` in
  `cddv_seg.h`. `Serial.h`/`CDDVWire.h` alias the historical names (`CDDVSerial*`,
  `CDDVW`/`CDDVR`, `cw8`…`crStr`); app settings (4KB receive buffer, hooks) are in
  `relay_cfg.h`, and **`Serial.c`** holds only `CDDVSerialPoll` (auto-off reset + Cancel
  detection). `RP_SEND_BODY_MAX` in `relay_cfg.h` keeps CalDAV's original single-buffer send
  (whole frame copied into a 3,196-byte `gSendBuf`, one `SrmSend`); BitWarden, which has no
  globals room for that buffer, keeps its three-`SrmSend` send. The single send is kept on
  purpose: in POSE, a separately-sent trailing checksum byte of an empty `ITEM_ACK` was seen
  arriving seconds late. Verified by object comparison: CalDAV's send and every caller
  compile to the same instructions as before; the only difference is the idle-timeout
  override check in open/receive, which CalDAV never sets (so its wait stays 180s).
- **Serial-link encryption — shared `../lib/relaysec.c`** (2026-09-30, `sec004`): the key
  exchange runs in `SyncEngineRunOne` (once per connection, so Sync All secures once) and
  right after the port opens in `OAuthRelay.c`; every later frame is AES-128-CTR encrypted
  inside `relayproto.c`. No plaintext fallback: a failed exchange shows "Serial link cannot be
  secured!" (also the slot's failure status) before any calendar data is touched; no reply or
  Cancel keeps "Lost contact with WiRSa negotiating baud rate." Wire format:
  `bitwarden/PROTOCOL.md`, "Serial-link encryption". POSE-verified end to end against an
  independent Python WiRSa (secured sync of 3 events). Real-hardware sync confirmed by the user 2026-10-10.
- **`CDDVPrefs.c`/`.h`** — `CalDAVRelayDB` load/save, with byte-for-byte frozen snapshots
  of the v1/v2 slot layouts so `CDDVLoadPrefs` can migrate an existing blob forward instead
  of wiping saved slots on every struct-size bump (§5).
- **`CDDVSec35.c`/`.h`** — Sec35 integration for every identifying/config field in a slot,
  not just the secret (§5).
- **`DAVDb.c`/`.h`** — the `DateBook35DAV.pdb` writer Datebook35 reads (§8).
- **`OAuthRelay.c`/`.h`** — the Google sign-in flow (§3).
- **`SyncProgress.c`/`.h`** — the modal status+sawtooth-progress-bar shown for the
  duration of a sync, mirroring BitWarden's own `BwProgressShow`/`SetStatus`/`Bar`/`Hide`
  shape (one of this suite's few centered, not bottom-aligned, modals — the other being
  BitWarden's equivalent). Cancel is polled via the same transport-layer
  `EvtResetAutoOffTimer`-between-slices mechanism `Serial.c`'s `ReadExact` already uses, so
  this file doesn't need its own auto-off-timer awareness.
- **`LongTextDialog.c`/`.h`** — the full-screen text editor `SlotEditForm.c`'s long/URL
  fields open into.
- **`HelpForm.c`/`.h`** — scrollable, full-page Help text (bold titles/labels, no
  underline, real pixel word-wrap — same polish pass BitWarden/Sec35 have both had).

## 5. Data model / at-rest crypto

**`CDDVSlot`** (`CalDAVSync.h`) is the whole per-calendar configuration, one of
`cddvMaxSlots` (8) held in `CDDVPrefsType`:
```c
typedef struct {
    char server[cddvUrlLen];       /* CalDAV: server URL. iCloud: shard hostname. Google: unused. ICSFeed: feed URL. */
    char path[cddvPathLen];        /* CalDAV: calendar path. iCloud: calendar name. Google: calendar ID. */
    char uuid[40];                 /* iCloud principal UUID only */
    char user[cddvUserLen];        /* CalDAV/iCloud username */
    UInt8 secret[cddvSecretMaxLen];/* Sec35 ciphertext - app password or OAuth refresh token */
    UInt16 secretLen;
    char alias[cddvAliasLen];
    CDDVServerType serverType;
    Boolean ignoreOnSyncAll;
    UInt8 pastOverrideSel, futureOverrideSel;   /* 0 = inherit global default */
    UInt32 lastSyncTime; Boolean lastSyncOk;
    UInt16 lastSyncEventCount; char lastSyncErrMsg[cddvSyncErrMsgLen];
    char googleClientId[cddvGoogleClientIdLen];      /* v3 */
    char googleClientSecret[cddvGoogleClientSecretLen]; /* v4 */
    UInt16 serverLen, pathLen, uuidLen, userLen, aliasLen,
           googleClientIdLen, googleClientSecretLen;    /* v5 */
    Boolean fieldsEncrypted;
} CDDVSlot;
```

**Every identifying/config field is Sec35 ciphertext at rest, not just the secret** — this
was a deliberate widening (v5, `cddvPrefsVersion = 5`, 2026-09-24, user request): originally
only `secret` was encrypted (matching Address35/lazyFTP's "encrypt payload, not everything"
pattern), but since this whole app's reason to exist is storing calendar account details,
every field is real personal information (server hostnames, usernames/Apple IDs, calendar
IDs/UUIDs, OAuth credentials) — server/path/uuid/user/alias/googleClientId/
googleClientSecret all got the same AES-CTR-in-place treatment. Each field needs its own
plaintext-length companion (`serverLen`, `pathLen`, ...) since AES-CTR ciphertext isn't
NUL-safe. Nonce is `(slotIndex, CDDVField-constant)` — `secret`'s own original nonce
(`nonceHi=0`) is left untouched for backward compatibility with already-saved ciphertext;
every other field gets a distinct nonzero `nonceHi` (`cddvFieldServer`..
`cddvFieldGoogleClientSecret`) so CTR mode's keystream-reuse weakness doesn't let two
fields on the same slot leak each other's plaintext via XOR.

**One-time migration, not a version-bump wipe** (unlike BitWarden's `bwStoreFormatVersion`,
which *does* wipe on mismatch) — `CDDVLoadPrefs` keeps byte-for-byte frozen snapshots of the
v1 (`CDDVSlotV1`, pre-verbose-status) and v2 (`CDDVSlotV2`, pre-`googleClientId`) slot
layouts and forward-copies an old blob's fields into the current shape field-by-field,
preserving every already-configured slot rather than wiping them. `fieldsEncrypted` starts
false for any slot that arrived via this path (its fields are plaintext, forward-copied
byte-for-byte); `CalDAVSync.c`'s `StartApplication` runs a one-time
`CDDVEncryptSlotFieldsIfNeeded` pass per slot once Sec35's own unlock gate has already run,
encrypting them in place and flipping the flag — a fast no-op for every slot already at v5.

**Google Client ID/Secret placement moved TWICE during development** (a real, documented
design reversal, not a one-shot decision): first built as a WiRSa-resident EEPROM setting
(G0), then **undone and moved to a per-slot Palm field** instead (googleClientId in v3,
googleClientSecret in v4) — sent fresh in every `SYNC_REQUEST`, never persisted on WiRSa,
which "must never hold resident, account-identifying state of its own" per the field's own
comment. Client ID is plaintext even after v5 (Google's own "Desktop app" client type treats
it as public); Client Secret is likewise plaintext-classified in the struct comment despite
Google's docs claiming it's optional for that client type — in practice the token endpoint
still rejects both `authorization_code` and `refresh_token` grants without one, confirmed
against a real client, so it's required here despite the documentation.

**`DateBook35DAV.pdb`'s own `CDDVEvent` records are separately Sec35-encrypted** (added
2026-09-24, alongside the slot-field widening above) — see §8. A now-stale project memory
(`caldav-sync-sec35-protection-todo.md`) still describes this as a not-yet-started future
improvement; the current `DAVDb.h` shows it done (`uidLen`/`aliasLen`/`titleLen`/
`locationLen`/`notesLen` plaintext-length companions appended to `CDDVEvent`) — a concrete
example of why memory files carry a "point-in-time, verify against current code" caveat.

## 6. UI/UX flows

- **Slot list + per-slot actions**: popup slot selector (`MainForm.c`), Edit (opens
  `SlotEditForm`), Sync Now, Clear DB (wipes only this slot's synced events, not its
  configuration — confirmed via `altConfirmClearSlotDB`).
- **Sync Now vs. Sync All — one chime pair either way, not one per calendar.** Sync Now
  wraps its single `SyncEngineRunOne` call with a start chime before and an end chime
  after. Sync All plays the start chime exactly once before its loop (only if there's at
  least one non-empty, non-skipped slot to sync) and the end chime exactly once after the
  loop — regardless of per-slot success/failure, matching the existing summary alert which
  already reports partial success ("N of M calendar(s) synced successfully") rather than
  treating a partial failure as silence:
    ```c
    if (totalCount > 0) PlaySyncChime(true);
    for (i = 0; i < cddvMaxSlots; i++) { ... SyncEngineRunOne(...); ... }
    if (totalCount > 0) PlaySyncChime(false);
    ```
  Implementation (`MainForm.c`, `PlaySyncChime`/`PlayTone`, added 2026-09): raw `SndDoCmd`/
  `sndCmdFreqDurationAmp` (present since PalmOS v1), amplitude read live from
  `SndGetDefaultVolume`'s `sysAmpP` so it follows the device's actual System Sound level
  (Off/Low/Medium/High) rather than always playing at max — unlike `SndPlaySystemSound`,
  raw `SndDoCmd` has no built-in volume/preference awareness, so this is checked by hand
  (`if (sysAmp == 0) return;`). Exact tones (tuned interactively — the first pass was too
  loud/always-max-volume, then too slow, then slightly too fast, before landing here):
  - **Start**: 1400 Hz/75 ms, 1500 Hz/113 ms, 2400 Hz/450 ms
  - **End**: 2400 Hz/75 ms, 1800 Hz/113 ms, 1200 Hz/450 ms

  This is the *original* implementation of this chime in the suite — BitWarden's own sync
  start/end chime (`Bitwarden.md` §6) is a note-for-note mirror added afterward, reusing
  these exact same values.
- **Adaptive per-server-type fields** (`SlotEditForm.c`, §4): CalDAV/iCloud/Google/ICS Feed
  each show only their own relevant fields, all four occupying the same on-screen rectangles.
  Google's group includes a "Sign In" button that runs the OAuth flow (§3) instead of a
  plain password field.
- **Sync progress + Cancel**: a centered modal (one of two exceptions to this suite's usual
  bottom-aligned-dialog rule, the other being BitWarden's own), sawtooth progress bar (no
  true total until `SYNC_DONE`), alias + live event count shown during the sync, Cancel
  button.
- **Password reveal**: a per-group "Show" checkbox unmasks the secret field temporarily,
  same Sec35-style reveal-then-hide idiom used elsewhere in this suite.
- **Skip on Sync All**: `ignoreOnSyncAll` (renamed from an earlier "Ignore" label to "Skip",
  with a "Skipped" marker shown in the slot list) excludes a slot from the batch without
  deleting its configuration.
- **Help**: Record menu → Help, a scrollable full-page form.

## 7. Known gotchas / real-device (or POSE64-confirmed) bug list

1. **Missing `SEC004` tag on `CDDVSerialSendFrame`'s definition — zero bytes ever sent.**
   The header declaration (`Serial.h`) already carried `SEC004`; the `.c` definition
   didn't. Symptom: the ACK-retry loop silently burned its entire budget — `SrmSend`/
   `SrmSendWait` reported success, but nothing reached the wire, because the compiler
   decides near-call vs. far-call from what it can see *at the call site* (the header
   declaration), not the real definition's tag — a mismatch produces a wild jump, not
   always an immediate crash. Fixed by adding `SEC004` to the definition (before the return
   type, not after the parameter list — the latter is a parse error). This is the same bug
   class BitWarden independently hit three separate times (`Bitwarden.md` §2.1) and is
   described there as "very likely the dominant real-device crash" in this whole family of
   apps.
2. **`DateDAVGet`'s embedded time-of-day prefix duplicated Datebook35's own time column.**
   Agenda already draws start time in its own dedicated table column
   (`AgendaApptDrawTime`) for every row type uniformly — the DAV reader's own formatted
   prefix produced visible duplicates ("10:00 10:00 am Mock Meeting..."). Fixed by removing
   the embedded time string entirely; the text column now carries only title (+
   " @ location" if present), matching how a real appointment's own description already
   works.
3. **`HTTPClient::getString()` silent truncation (WiRSa side, `caldav_relay.cpp`) —
   "sync only shows past events."** Not a Palm-side bug, but the real root cause of a
   symptom that looked Palm-side at first: ESP32 Arduino's `HTTPClient::getString()`
   silently discards `writeToStream()`'s return value, so a large response that fails to
   fully buffer (heap fragmentation, not necessarily low free-heap) returns whatever
   partial data it managed to capture with zero error surfaced. A related library gotcha
   found in the same investigation: `HTTPClient::addHeader()` silently rejects
   `"Connection"`/`"User-Agent"`/`"Host"` (reserved) — must use the real setters
   (`setUserAgent()`, `setReuse(false)`). Fixed on the WiRSa side with a custom chunked
   `Stream` subclass that forwards each dechunked piece via callback instead of ever
   buffering a whole response. Documented here because any future Palm-side symptom that
   looks like "some events are just missing" should consider this class of bug on the
   relay side before assuming the Palm's own parsing/DB-write path is at fault.
4. **Slot-field Sec35 widening (v5) needed a one-time migration path, not a version wipe**
   — see §5's `fieldsEncrypted`/`CDDVEncryptSlotFieldsIfNeeded` mechanism. Getting the
   *order* right mattered: the app's own Sec35 unlock gate must run (in `CalDAVSync.c`'s
   `PilotMain`) before `StartApplication`'s migration pass, or the pass fails safe (leaves
   the slot untouched, still plaintext) rather than half-migrating.
5. **DSID field never saved back in `SlotEditForm`** (iCloud slot editing) — a real,
   since-fixed bug (task-tracked as "CalDAV Sync: fix DSID field never saved back in
   SlotEditForm"); not independently re-verified for this doc beyond confirming the fix is
   present in current `SlotEditForm.c`'s iCloud field group.
6. **Testing method note, not a bug**: live-serial POSE64 testing of the Datebook35↔CalDAV
   Sync integration was abandoned partway through in favor of directly building
   `DateBook35DAV.pdb` on the host (byte-exact `CDDVEvent` layout) and injecting it via
   `rc.py install` — the same host-injection pattern BitWarden's own bulk-fixture tools use.
   **Gotcha if reusing this technique**: `CDDVEvent`'s `localStart`/`localEnd` are consumed
   directly by `TimSecondsToDateTime` with **no timezone conversion at all** — a Python
   generator must use `calendar.timegm(dt.timetuple())` (naive UTC-equivalent seconds), not
   `dt.timestamp()` (applies the *host's own* local timezone and silently shifts every
   event's displayed time by the host/device UTC offset — this exact bug was caught once via
   a screenshot showing events 8 hours off on a UTC+8 host).
7. **All-day events moved to the wrong day outside UTC+1..+14 (fixed 2026-10-02).** The
   Palm shifted an all-day event's zone-less date by the device time zone like a timed event,
   on sync and on Recompute Times: west of UTC every all-day event landed on the previous day,
   and at UTC+0 it landed exactly on local midnight, which Datebook35 then skipped (its own
   midnight bug, `Datebook35.md` §18). Invisible at UTC+8, where the shift lands at 08:00 on the
   right date. Fix: all-day events keep their date (`SyncEngine.c`, `DAVDbRecomputeLocalTimes`);
   Recompute now re-sorts by `localStart` (the shift is no longer uniform across records), with
   ties broken by record uniqueID because `DmQuickSort` isn't stable — without it two all-day
   events on one day swapped places on every Recompute. Recompute Times also corrects all-day
   records written by older builds, no resync needed. POSE64-verified through Prefs time zone
   changes Singapore → UK → USA (Eastern) → Singapore with Recompute and sync at each step.
8. **Investigated, not reproducible (POSE64, 2026-10-02/03): "Edit Text" freeze.** One evening,
   several emulator runs froze after ~18 keystrokes in the long-text editor (then faulted when
   the test tool force-relaunched the app). It looked tied to X-Master/X-Y2K35, but: a POSE
   breakpoint on X-Y2K35's patched `TimSecondsToDateTime` is never hit while typing in or idling
   in the editor (3 minutes), X-Master changes no other trap, and the same sequences that froze
   (3/3 and 2/2 that evening) ran clean 13 times the next day - with and without X-Y2K35, under
   heavy host CPU load, and with 148-char URLs / UUID paths saved and reloaded. Most likely an
   emulator/test-harness condition that evening (orphaned scripted-WiRSa processes were found
   spinning at 100% CPU). No code change.
9. **Recompute Times left parts of its progress box on screen (real device, fixed
   2026-10-10).** `ToastDraw` (`DAVDb.c`) saves the screen under the box once, sized for the
   first text ("Recomputing..."), but sized the box to each text; the later, wider
   "Recomputing 17/250" drew outside the saved area, which `ToastEnd` then never restored.
   Only shows with more than 16 events (the first progress redraw), so the small POSE test
   sets missed it. Fix: the box has one fixed width, sized for the longest progress text, with
   the text centred; the font is also restored afterwards. Built clean; not yet re-tested.

## 8. Cross-app integration (→ Datebook35)

`DateBook35DAV.pdb` (creator-independent `DATA` type, name `"DateBook35DAV"`) is written
exclusively by `DAVDb.c` and read read-only by Datebook35's own `apps/datebook35/Src/
DateDAV.c` — Datebook35 never links this app's own DB layer, it's a completely separate,
self-contained reader over the same fixed-size record format (`DateDAV.h`'s own comment: "
Datebook35 can't link CalDAV Sync's DB layer"). The two independent copies of the `CDDVEvent`
struct shape (this app's `DAVDb.h`, Datebook35's `DateDAV.c`) must be kept byte-for-byte in
sync by hand — there is no shared header, by design (Datebook35 must not depend on CalDAV
Sync's own build).

**Write path is per-slot rebuild, not whole-database swap** (`DAVDb.h`'s own comment): a
whole-DB temp-then-atomic-rename (BitWarden's `Store.c` pattern) would be wrong here, since
"Sync Now" only re-fetches *one* slot — swapping in a temp DB containing just that slot's
events would wipe every other slot's already-synced ones. `DAVDbBeginSlotRebuild`/
`DAVDbAddEvent` stage into a separate temp database; only `DAVDbCommitSlotRebuild` (full
success only) deletes that slot's old records and copies in the staged new ones as one unit.
"Sync All" is just this same per-slot primitive called once per slot, not a separate
whole-DB code path.

**Datebook35's own DAV-row rendering** uses a sentinel `CurrentRecord` range
(`kDayDavBase`/`IsDayDav`, `Datebook35.md` §17) to mark a table row as "this is a read-only
CalDAV row, not a real `ApptDB` index" — full detail (including a real crash this sentinel
convention caused and its fix) lives in `Datebook35.md` §17, not duplicated here. Datebook35
Day/Week/Month/Agenda views all merge these DAV rows in alongside real appointments and
ToDo/birthday rows, following the same two-pass timed/untimed injection shape
`DayInjectTodos` already established (`datebook35-multiview-caldav-integration` memory —
11+ days old at time of writing, verify against current `DateDay.c` before relying on any
specific line/byte claim in it).

**Shared Sec35 unlock gate, not two independent ones**: both apps refuse to show real
content while Sec35 is locked — CalDAV Sync masks/bounces at its own launch gate (§1);
Datebook35 masks CalDAV rows and bounces to Sec35 the same way it already does for its own
encrypted appointments, per `Datebook35.md` §9.3/§17.

## 9. Build/verification status

Be honest about what's actually been exercised vs. assumed:

- **Palm-side build**: verified via a real `make` in this session — clean, zero warnings
  beyond pilrc's unrelated Graffiti-indicator notices, all four `SEC00x` segments comfortably
  under budget (§2).
- **CalDAV-generic + ICS Feed sync**: **working** — exercised against a local test CalDAV
  server and real ICS feed URLs, and in regular real-device use with no outstanding user
  complaints (confirmed 2026-09-29). The WiRSa side now streams ICS feeds through a two-phase
  incremental parse (`cddvFetchIcsFeed` → `cddvIcsOnComponent` per component →
  `cddvIcsFinishParse`), replacing the earlier whole-body buffer that hit Arduino `String`'s
  ~64KB ceiling on non-PSRAM boards — see `WiRSa-Palm.md` §13.3.
- **iCloud**: implemented (shard/DSID/UUID handling in `SlotEditForm.c`, WiRSa-side iCloud
  CalDAV Relay support) with a real fixed crash (uncaught exceptions in `caldav_relay.cpp`)
  along the way; real-hardware end-to-end confirmation status not independently re-verified
  for this doc.
- **Google OAuth**: **user-confirmed working (2026-09-28)** — the Calendar API fetch path
  (WiRSa's `cddvFetchGoogleCalendar`/`cddvGoogleRefreshAccessToken`) is real-hardware-tested
  and functions correctly; a successful fetch implies the OAuth handshake that produces its
  access token (Palm↔WiRSa↔`google_auth.py`↔Google) was exercised too, though that handshake
  itself wasn't separately called out. The Palm-side state machine (`OAuthRelay.c`) reads
  cleanly and builds clean.
- **Datebook35 integration**: POSE64-verified via direct `DateBook35DAV.pdb` injection
  (§7.6), not via a live serial sync — screenshots confirmed correct calendar-day placement,
  correct times, and correct truncated text for timed, all-day, and future-dated events.
- **Serial-link encryption** (2026-09-30, §4) and the **all-day/time-zone fix** (§3 "Event
  times", §7 item 7): POSE64-verified end to end against a scripted WiRSa (sync, OAuth,
  Recompute Times across Prefs time-zone changes, Datebook35 Day/Week/Month at UTC+8/0/-5);
  a real-hardware sync over the encrypted link succeeded (user-confirmed 2026-10-10; the
  all-day/time-zone behaviour itself wasn't separately called out). Needs the matching WiRSa
  firmware build.
- **Sec35 field-level encryption (v5)**: implemented and present in current code (§5); not
  independently re-tested end-to-end for this doc beyond confirming the struct shape and
  migration logic read correctly.
- **WiRSa firmware side**: extensively built out per the task history (CalDAV-generic,
  iCloud, ICS Feed, Google OAuth handshake+Calendar API fetch, chunked streaming, a long
  real-device HTTP/TLS/timeout bug-fix tail) but explicitly out of this doc's scope (§3) —
  see `caldav_relay.cpp` directly, or `WiRSa-Palm.md` §13 for the write-up.
