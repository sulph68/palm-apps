# WiRSa Palm-Sync Additions — reference & rebuild guide

Everything needed to understand, rebuild, or debug the Palm-specific modes added on top of
[nullvalue0/WiRSa](https://github.com/nullvalue0/WiRSa): **SASD SDCard mode** (VFS-over-serial),
**HotSync mode** (real HotSync install target), **FTP Server mode** (§10 — general-purpose, not
Palm-specific, but added primarily to support a Palm upload-then-HotSync/SASD workflow via the
lazyFTP client app, see `apps/lazyFTP.md`), and the two relay modes for Palm apps that need
HTTPS, **BitWarden Relay** (§11) and **CalDAV Relay** (§13), and the **Mesh Relay** mode for the Meshtastic client (§17). Internal reference — mirrors the style/purpose of the
`apps/*.md` docs kept for the PalmOS-SDK side of this project (Datebook35.md etc.): dense, topical,
every non-obvious gotcha called out explicitly. For the user-facing version, see
`~/WiRSa/WiRSa/docs/README-Palm.md`.

Paths are relative to `~/WiRSa/WiRSa/Firmware/` unless stated otherwise. The user builds and
flashes WiRSa on their own machine; since 2026-10 a compile-only check is also possible in this
environment (see §9).

---

## 1. What this covers, and why two new modes

WiRSa is a WiFi↔RS232 adapter for vintage computers. This fork adds Palm-specific functionality on top
of the upstream feature set (telnet/BBS modem, PPP/SLIP gateways, file transfer, text playback): a
Palm can now mount the SD card as a native VFS volume (**SASD**), and can HotSync directly against
WiRSa with no desktop computer involved (**HotSync**). Both are from-scratch, wire-compatible
reimplementations of real embedded reference projects — not ports of PalmOS-side application code, and
not ports of the reference C sources verbatim (see §8 for exactly which reference sources were used and
how).

Both repurpose an *existing* main-menu slot rather than adding a new one, following the same pattern
each time: `MODE_PLAYBACK` (originally "Text Playback") became SASD; `MODE_FILE` (originally
"File Transfer" — XModem/YModem/ZModem) became HotSync. The numeric mode constants are unchanged
(`globals.h`), only their meaning and menu label. See §7 for what this displaced.

---

## 2. SASD SDCard mode

> On `improved` the disk SASD serves is the SD card or the 12 MB virtual card (WiRSaCtl Setup "Disk"): §16.3.

### 2.1 What it is, protocol family

Wire-compatible with dmitry.gr's real **SASD** hardware ([reference project](https://dmitry.gr/?r=05.Projects&proj=32.%20SASD)) — WiRSa plays the "device" role the original SASD hardware plays (the
SD-card-holding end); the Palm's *unmodified* `SASD.prc` slot driver mounts it as a native VFS card,
completely transparent to every app that already uses VFS (Address35's SD browsing, CryptoNote's
card-file features, etc.) — no app-side changes needed. **Fully passive**: WiRSa never initiates
anything, it only replies to whatever the Palm's slot driver sends. Reference source used:
`~/Downloads/sasdSource/SASD_release/software/serialSD/firmware/` — `Src/proto.h` (commands/structs),
`slk.c` (SLP framing + CRC-16/XMODEM), `frag.c` (the reliability wrapper — SASD payloads never
actually fragment, single-fragment-always is a valid simplification here, see §2.2), `lpc81x.c` (the
"device" role this file plays).

### 2.2 Wire format (`sasd.cpp`)

```
[magic 0xBE 0xEF 0xED][dstSlk][srcSck][pktTyp][dataLen:2 BE][tid][csum][payload][crc16:2 BE]
```
`dstSlk`/`srcSck`/`pktTyp` are fixed `0x08`/`0x08`/`0x10` (`PKT_ADDR_SASD`/`PKT_TYP_SASD`) for every
SASD packet. `csum` = 8-bit sum of the 9 preceding header bytes. `crc16` = CRC-16/XMODEM (poly
`0x1021`, init 0) over header+payload, big-endian.

Every SASD message also carries a 4-byte `FragHdr` (`typ`, `flags`, `ofst` BE) inside the slk payload:
`typ=1` (DATA) for commands from the Palm, `typ=2` (ACK) for WiRSa's replies. WiRSa never fragments
(every real payload — even a 512-byte sector + 1-byte tag — is ~517 bytes, comfortably under
`FRAG_MAX_CHUNK_SZ=524`) and never sends unsolicited DATA; every reply IS the ack for the Palm's own
request. This is a deliberate, documented simplification vs. HotSync's own frag layer (§4), which
genuinely needs multi-fragment support since resource/record payloads routinely exceed one chunk.

CRC-16/XMODEM table reused from `file_transfer.cpp`'s existing `zmCRC16Update()` (built originally for
ZMODEM) — confirmed byte-for-byte identical to the reference's own table, so no new table was needed.
This same table is also reused by `hotsync.cpp` (§4).

### 2.3 Command set (`sasd.cpp`)

`SASD_CMD_PING`(0x00) `GET_INFO`(0x01) `READ`(0x02) `WRITE`(0x03) `RESET_STATE`(0x04) `SET_BAUD`(0x05).
All Palm-initiated. `CMD_FW_UPDATE_START`/`CMD_GET_BATT` intentionally unimplemented (only reachable
via `SlotCustomControl`, never the VFS read/write path this mode exists for). **There is no
device-to-Palm "goodbye"/session-end message anywhere in this protocol** — relevant to §2.6.

### 2.4 Baud handling

Fixed **9600** listen baud (`SASD_BAUD_INITIAL`) on mode entry, matching the reference's own
`SD_BAUDRATE_INITIAL` convention. The Palm negotiates up via `CMD_SET_BAUD` once ready, capped at
`bauds[serialSpeed]` (WiRSa's own configured operating baud) — this cap is the fix for a real
file-copy-corruption bug found in round 1 of real-hardware testing (an uncapped negotiated rate
exceeded what the physical link could sustain reliably).

### 2.5 Capacity tracking (Used/Total)

`SD.usedBytes()`/`SD.totalBytes()` don't update live in practice on this target. Fixed with a **direct
FAT32 walk** (`sasdParseFat32Layout()`/`sasdScanFreeClusters()`) — reads the BPB (checking the
`BS_FilSysType` signature at offset 82 specifically, to avoid ever misparsing an MBR's own partition
table as if it were BPB fields — a real, easy-to-hit bug an FSInfo-only version didn't guard against)
then walks the actual FAT sectors counting free (zero) entries. Run on a 10s idle-debounce timer
(`sasdIsIdleEnoughToScan()`, not a plain 1s tick) so it never fires mid-transfer and risks a Palm
timeout from `sasdLoop()` blocking too long. **Gotcha hit during development**: the idle gate initially
checked `sasdOp.op == SASD_OP_NONE`, but that field is only ever reset at mode entry/exit or an
explicit `CMD_RESET_STATE` — never when a READ/WRITE cleanly finishes (matches the reference's own
state machine, which relies on `doneSec==numSec` instead) — so after the Palm's first file op,
capacity tracking silently stopped updating for the rest of the session. Fixed with a dedicated
`sasdMarkFileActivity()`/`sasdLastFileOpMillis` debounce, independent of `sasdOp.op`.

### 2.6 OLED status header

`sasdDrawStatusLines()` hand-draws its own boxed header (title + **live** baud, top-right, via the
shared `liveBaudStr()` helper — see §5.3) — this is a deliberate deviation from every other menu's
`showHeader()`/`baudDisp[serialSpeed]` convention, because SASD actually changes its own UART rate
mid-session (9600 listen → negotiated/capped rate) and the *configured* setting can read stale for
several seconds after a real change. Body: `SD: <used>/<total>` then `Speed: <n> KB/s` (auto-scaling
human-readable units, matching how every OS file browser formats sizes).

### 2.7 Exit behavior — still only "tolerable", not solved

`exitSasdMode()` drains RX both before and after the baud switch back
(`while(available()) read();` around a `.flush()`+`.end()`+`delay(200)`+`.begin()` UART restart),
because the Palm doesn't know WiRSa has exited and may keep sending for a bit — `PhysicalSerial.flush()`
only drains TX, never RX, and un-drained stray protocol bytes were leaking into whatever mode came
next, getting misread as keystroke commands by the menu system's own char-based serial parser.

**This mitigation was widened once** (round 9) to a **time-bounded** drain (discards anything arriving
for a full 1s past reopening the interactive baud, not just a single before/after snapshot) after
comparing against HotSync's own clean exit (§4.6) — SASD's own retry budget in this protocol family is
`FRAG_MAX_RESENDS=4` at ~150ms/attempt, so a stranded command can retry for up to ~600ms after WiRSa's
already moved on; the widened window covers that. **Root cause is still open**: unlike HotSync, SASD's
wire protocol has no session-end signal to send (§2.3), so there is no way to *tell* the Palm the
conversation is over — only to out-wait however long it keeps trying. Per direct user feedback this
round: **"tolerable, not fixed."** Don't describe this as resolved without new evidence.

### 2.8 Border-corruption-on-exit bug (shared bug class, see §5.1)

`exitSasdMode()`'s own custom drawing never touches the shared `lastMenu` global, so `showHeader()`'s
own "only fully redraw when `lastMenu != menuMode`" optimization can skip the border redraw on the way
back to the main menu, leaving a corrupted/missing border. Found via the identical, user-confirmed
HotSync bug (§4.7) and fixed *proactively* here by inspection — not yet independently reported or
verified for SASD specifically, but the mechanism is provably identical. Fix: `msgFlag = true;` before
both of `sasdLoop()`'s `mainMenu(false)` call sites (BTNBK and the `+++`-escape path).

---

## 3. Round-by-round bug list (SASD) — for future debugging reference

Roughly chronological; see `wirsa-sasd-implementation` memory for full blow-by-blow if deeper context
is ever needed. Real, user-confirmed hardware bugs only (not every UI tweak):
1. Uncapped negotiated baud caused file-copy corruption → capped at `bauds[serialSpeed]` (§2.4).
2. Stray protocol bytes leaking into the next mode after exit → RX drain (§2.7, later widened §2.7).
3. FSInfo-hint-only capacity tracking silently never updated → direct FAT32 walk (§2.5).
4. Idle-gate for the FAT walk kept it from ever firing again after the first file op → activity-based
   debounce independent of `sasdOp.op` (§2.5).
5. OLED baud display used the raw integer (`"9600"`) instead of the suite's compact `"X.Xk"` style →
   shared `liveBaudStr()` helper (§5.3).
6. (Proactive, unconfirmed) border corruption on exit → `msgFlag=true` (§2.8).

---

## 4. HotSync Install mode

> On `improved` HotSync installs from the SD card or the virtual card (same Disk setting) and creates its
> folders on the disk it mounts: §16.4.

### 4.1 What it is, protocol family

WiRSa plays the **desktop-cradle** role: waits for a Palm's HotSync WAKE packet, negotiates a session
over the real DLP/PADP/SLP wire protocol, then pushes every `.PRC`/`.PDB` file it finds in two SD-card
folders onto the device, then ends the sync — no desktop computer or Palm Desktop software involved.
Reference source: same SASD hardware project's own embedded HotSync host client (a *different*, fuller
part of the same reference than §2 uses) —
`~/Downloads/sasdSource/SASD_release/software/serialSD/firmware/hotsync.c`/`.h` (~1540/160 lines, a
complete embedded DLP-host client), `prc.c`/`.h` (PRC-only file parser — extended here to also read
PDB, which the reference deliberately doesn't: *"this code does not support PDB files. It could, but
does not"*), `slk.c`/`frag.c` (same framing family as §2, but needing the REAL multi-fragment version
here — resource/record payloads routinely exceed one 524-byte chunk, unlike SASD's).

Cross-validated throughout against **pilot-link** ([jichu4n/pilot-link](https://github.com/jichu4n/pilot-link),
`~/WiRSa/pilot-link/`) — the real, desktop-proven implementation — since the embedded reference is a
narrower target (one specific piece of hardware) and turned out to have gaps a real device would
actually exercise (§4.4). Key pilot-link files referenced: `libpisock/slp.c`, `libpisock/padp.c`,
`libpisock/cmp.c`, `libpisock/pi-file.c`, `include/pi-slp.h`, `include/pi-padp.h`, `include/pi-cmp.h`,
`include/pi-dlp.h`.

### 4.2 Architecture (`hotsync.cpp`/`.h`, ~1400 lines)

Layered, each layer's functions prefixed `hs*`:
1. **SLP framing** (`hsSlkSend`/`hsSlkRecvOne`) — same wire format as §2.2's byte layout (identical
   protocol family), reused CRC table.
2. **PADP/frag** (`hsFragSendChunk`/`hsFragSendStart`/`hsFragSendContinue`/`hsFragRecv`) — the REAL
   multi-fragment version. `hsFragSendContinue` can be called repeatedly with arbitrary small
   increments (e.g. a 256-byte SD-read buffer at a time); chunk boundaries and FIRST/LAST flags are
   computed purely from internal `curPos`/`totalLen` state, independent of how much the caller hands
   over per call — this is what lets a large PRC resource stream straight from SD without ever being
   buffered whole.
3. **DLP high-level envelope** (`hsDlpTxRx`, `hsRspParseOneArg`) — request header + tiny/small arg
   wrapper + payload; response header + err + optional single arg. Simplified from the reference's own
   fully-general N-arg parser since every opcode actually used here returns 0 or 1 args.
4. **DLP opcode subset**: `hsHandshake` (WAKE/INIT/CMP), `hsOpenConduit`, `hsGetSysInfo`, `hsDbCreate`/
   `hsDbDelete`/`hsDbClose`, `hsResWriteBegin/Continue/Finish`, `hsRecWriteBegin/Continue/Finish`,
   `hsInfoWriteBegin/Continue/Finish` (AppInfo/SortInfo blocks), `hsEndSync`. RPC and all DB/record/
   resource *read* APIs deliberately out of scope — the install driver never reads anything back from
   the Palm, only writes.
5. **PRC/PDB file reader** (`hsParseDbHeader`, `hsEntrySize`) — shared header parse to offset `0x4E`
   (confirmed byte-identical between PRC and PDB via `pi-file.c`), then branches on the
   `PALM_DB_ATTR_RESOURCE` bit into a 10-byte resource-entry reader or 8-byte record-entry reader.
6. **Install driver** (`hsInstallExt`, `hsFolderExists`, `hsInstallWalk`, `hsInstallFile`) — folder
   walk + per-file install, see §4.3.
7. **Mode/session/UX** (`enterHotsyncMode`, `hotsyncLoop`, `hsRunSession`, `hsExit`, `hsDrawHeader`,
   `hsDrawStatus`/`hsSetState`/`hsLog`) — see §4.5–§4.7.

Every multi-byte field is serialized/parsed **by hand** (`hsPut8`/`hsPut16`/`hsPut32`/`hsGet16`/
`hsGet32`) rather than casting wire bytes onto `__attribute__((packed))` structs like the C reference
does — avoids all alignment/struct-packing risk on this target, matches the byte-array idiom `sasd.cpp`
already established in this codebase.

### 4.3 Folder policy — two folders, different rules, one conditional rule

- **`PALM/PROGRAMS/SASD/`** — "drivers": `.PRC` files use **skip-if-exists** (a real `CreateDB` with no
  `DeleteDB` first; `dlpErrExists` logged as "already installed, skipping", not a failure).
  `PALM_DB_ATTR_RESET` (0x0020, "reset after install") is **forced** on every SASD-folder create,
  matching the original SASD hardware's own intent for this folder — install-once-and-activate.
- **`PALM/PROGRAMS/SASD/` `.PDB` files are conditional**: only installed *at all* — and with a
  **forced override** (delete-then-reinstall) rather than skip — if at least one `.PRC` in the same
  folder was **actually installed** this run (a real `CreateDB`, not a skip). A routine sync where
  nothing in SASD changed leaves every SASD-folder PDB completely untouched. When triggered, logs
  `"Initialization install triggered. PDB files will be installed."` and the session's final OLED
  state reads `"Init Complete"` instead of plain `"Completed"` (`hsSasdInitTriggered` module flag).
- **`PALM/PROGRAMS/INSTALL/`** — general app installs: **always** delete-then-reinstall, **never**
  forces a reset. Confirmed against pilot-link's own real `pi_file_install()`
  (`libpisock/pi-file.c:853`) — it passes the DB's on-disk `flags` straight through unmodified and
  only ORs in the reset bit for one narrow, deliberate Graffiti-specific special case, never
  unconditionally. An earlier pass here had copied the SASD reference's own choice
  (`attr | PALM_DB_ATTR_RESET_AFTER_INSTALL`, unconditional) for *every* install, which caused a real,
  unwanted Palm reset after installing an ordinary app — corrected after explicit user pushback.
- **`PALM/PROGRAMS/INSTALL/` → `PALM/PROGRAMS/INSTALLED/` move-on-success** (added later, explicit user
  request): once a file from `INSTALL` is successfully installed (a real `CreateDB`, not counting the
  file if the write itself later fails), `hsMoveToInstalled()` relocates it into a sibling
  `INSTALLED` folder (`hsEnsureFolder()` creates it on first use if missing) via `SD.rename()`, using
  `File::path()` (the full path — `File::name()` is only the basename) captured before the file handle
  is closed. A same-named stale archive copy already in `INSTALLED` is removed first, since
  `SD.rename()` fails if the destination exists. This means `INSTALL` files are no longer
  unconditionally reinstalled on *every* sync — once installed, they leave `INSTALL` and won't be
  found there (or reinstalled) again, unlike the SASD folder above whose files are deliberately meant
  to always stay in place. A move failure is logged but does not fail the install itself (the install
  already succeeded — worst case the file just stays in `INSTALL` and gets reinstalled+retried next
  sync). `hsInstallExt()` gained a `moveToDir` parameter for this (`NULL` for both SASD-folder call
  sites, `"/PALM/PROGRAMS/INSTALLED"` for both INSTALL-folder call sites). User-confirmed working on
  real hardware.
- Within each folder: **`.PRC` before `.PDB`** (apps before their data).
- `.` files (macOS AppleDouble resource forks like `._Converter.prc`, or any hidden dotfile) are
  explicitly skipped (`hsIsDotfile()`, checked by basename) — without this, `._Converter.prc` would
  ALSO pass the plain `.PRC` suffix match and get installed as if real.
- Skip-vs-delete decision uses the real DLP status codes: `dlpErrExists=0x0009`, `dlpErrNotFound=0x0005`
  (`~/WiRSa/pilot-link/include/pi-dlp.h`) — not inferred from a generic failure.

### 4.4 Real protocol bugs found during real-hardware bring-up (in order found)

These were each concrete, user-confirmed real-hardware failures, not theoretical — kept here in detail
since they're exactly the kind of thing worth checking first if HotSync ever breaks again:

1. **Two stack-buffer overflows** (self-review, before first hardware test): `hsDbCreate`'s request
   buffer was sized 44 bytes for an actual 46-byte payload (`crid+type+cardNo+rfu+flags+version+
   name[32]`); `hsResWriteBegin`'s header buffer was sized 10 bytes for an actual 16-byte payload
   (`HsResWriteReq` is 10 bytes once `resSize` is counted, not 8). Both recomputed by hand and fixed.
2. **Wrong declared arg-length** in `hsRecWriteBegin` (self-review): declared `10+size`, should be
   `8+size` (`HsRecWriteReq`'s fixed part is 8 bytes) — would have told a real device the arg was 2
   bytes longer than it truly is, corrupting its own parse.
3. **Missing protocol-level padding rule** (self-review, cross-checked against every reference request
   struct): every real DLP request body must pad to a minimum of 4 bytes AND to an even total length,
   *outside* the arg's own declared length (e.g. the argless `OpenConduit` request still sends 2 zero
   bytes after its header; `DbClose`'s 1-byte arg gets one extra pad byte). Fixed generically in
   `hsDlpTxRx` (`while (pos < 4 || (pos & 1)) pad;`).
4. **Misrouted AppInfo/SortInfo streaming** (self-review): the original boolean parameter to the
   streaming helper only distinguished resource-vs-record, so AppInfo/SortInfo bytes would have gone
   through the wrong `*WriteContinue` function. Fixed with a 3-way `HsStreamTarget` enum.
5. **First real-hardware failure — "handshake failed", no detail**: WAKE received and validated, but
   `hsHandshake()`'s own WAKE-version check (copied verbatim from the SASD reference's own stricter
   validation: reject minor version >3, reject nonzero reserved bytes) was too strict — pilot-link's
   real `cmp_rx_handshake()` (`libpisock/cmp.c`) only checks the *major* version byte is `1`, nothing
   else. Loosened to match. (Didn't fully fix it — see next.)
6. **Second real-hardware failure — still "handshake failed", now with detailed logging**: added
   step-by-step `usbDebug` logging (WAKE-byte-burst-seen, packet-synced, WAKE-ok/rejected, INIT-sent/
   acked/never-acked, per-resend-attempt). Log showed the INIT reply was sent 8× over 8s with **zero**
   response — not even corruption. Root-caused via pilot-link's real `padp_tx()` (`libpisock/padp.c`):
   (a) its own ack-matching is `(type==ACK, txid==ours)` **only** — it never checks the echoed
   size/offset field, but this code additionally required an exact match; (b) timeouts (~150ms×4
   resends, carried straight over from the SASD reference's own bare-metal tuning) were ~25× tighter
   than pilot-link's real numbers (`PI_PADP_TX_TIMEOUT=2000ms × PI_PADP_TX_RETRIES=10`). Dropped the
   extra ack check, widened `FRAG_ACK_WAIT_MS` 200→1000 and `FRAG_MAX_RESENDS` 4→8.
7. **Third real-hardware failure — same symptom, deeper diagnostics added**: baud-at-TX-time and a full
   hex dump of the outgoing INIT bytes both confirmed correct (`02 80 01 00 00 00 00 00 E1 00` decodes
   exactly right). New clue: the *first* ack-wait attempt saw exactly **18 bytes** that never formed a
   valid header; every later attempt saw nothing. Root cause: the SASD reference's own `frag.c` has a
   standalone comment right after the `FragHdr` struct — **"acks have 2 bytes of garbage data in them.
   Ask PalmOS why..."** — a real, unexplained PalmOS-side quirk the reference's own author flagged.
   `10 (SLP hdr) + 4 (FragHdr) + 2 (garbage) + 2 (CRC) = 18`, an exact match. The ack-wait receive
   buffer had been hardcoded to exactly 4 bytes, and oversized packets were rejected outright before
   ever being inspected — on every attempt, silently. **Fixed**: widened the buffer to 16 bytes and
   the parse now only inspects the first 4 bytes (typ/flags/ofst); any trailing bytes, garbage or
   otherwise, are simply ignored. This is the actual root cause — handshake, and the whole rest of the
   session (every subsequent DLP request/response also goes through this same ack-wait function), has
   worked cleanly on real hardware ever since.

### 4.5 OLED status area

Plain text lines directly on black, under the 16px title bar — **not** the SASD-style white
busy-dialog box (`showMessage()`), which wastes most of the screen for what's really just a few lines
of text. `hsDrawHeader()`: `"HotSync"` + live baud top-right (same `liveBaudStr()` convention as SASD,
§5.3 — HotSync's own UART rate also moves mid-session, 9600 listen → negotiated/capped rate).
`hsDrawStatus()`/`hsSetState()`/`hsLog()`:
- **Line 1** (`y=19`): `"State: "` + `Waiting`/`Connected`/`Syncing`/`Completed`/`Init Complete`.
- **Line 2** (`y=29`): static `"Details:"` label.
- **Lines 3–4** (`y=39`/`y=49`): a 2-line scrolling detail log — `hsLog()` shifts line 2 into line 1
  and puts the newest entry into line 2, so only the latest 2 entries are ever visible, oldest
  scrolling off. Populated at every major transition: `"Checking <SASD|INSTALL> Dir"` /
  `"Install from <SASD|INSTALL>"` per folder, each installed file's own name as it's processed (also
  the "show the current file" requirement from the original spec), the conditional PDB-override
  message (§4.3), and `"Hotsync completed"` at the end.

### 4.6 Exit behavior — clean, and *why* it's clean

`hsRunSession()` calls `hsEndSync()` — a real DLP request/response round trip that formally tells the
Palm the session is over and gets its acknowledgment — **before** ever touching the baud rate. By the
time the baud switches back to 9600, the Palm's own state machine already knows the conversation ended,
so nothing is left in flight to leak into the next mode. Confirmed clean on real hardware. This is the
mechanistic reason HotSync's exit succeeded where SASD's own exit (§2.7) only got to "tolerable" — SASD
has no equivalent goodbye message to send (§2.3). The same RX-drain-around-the-baud-switch mitigation
(`.flush()`+`.end()`+`delay(200)`+`.begin()`, drain before and after) is *also* applied here — both in
`hsRunSession()`'s own post-`EndSync` transition and in `hsExit()` (covers `BTNBK` and the 15s
auto-timeout) — as defense-in-depth for whatever post-session chatter a real Palm-initiated reset might
still generate (see §4.3's `PALM_DB_ATTR_RESET` discussion — a real Palm auto-reboots after installing
a DB with that flag set, which is expected standard behavior, not something WiRSa requests, but still
generates real reset/boot chatter on the wire).

### 4.7 Border-corruption-on-exit bug (found here first, see §5.1)

`hotsync.cpp` draws its own title/status directly, entirely bypassing `showHeader()`'s normal
border+clear logic and never touching the shared `lastMenu` global. If the user entered HotSync
straight from the main menu, `showHeader()`'s own "only fully redraw when `lastMenu != menuMode`"
optimization is false on the way back out, leaving a corrupted/missing border. **User-confirmed real
bug.** Fixed with `msgFlag = true;` right before `hsExit()`'s `mainMenu(false)` call — the same
`msgFlag=true; //force full menu redraw` idiom already established elsewhere in this codebase
(`modem.cpp`). The identical bug was then found (by inspection, not yet independently confirmed) and
proactively fixed in SASD mode too (§2.8).

### 4.8 Mode wiring

- `globals.h`: `MODE_FILE` repurposed (numeric value unchanged, meaning/comment updated).
- `main.cpp`: `mainMenuDisp[]` `"File Transfer"` → `"HotSync"`; `loop()`'s `MODE_FILE` branch calls
  `hotsyncLoop()` instead of `fileLoop()`.
- `modem.cpp`: the main-menu selection handler's serial shortcut moved `'F'`→`'H'` (matching the
  renamed label's auto-bracket — `showMenu()`'s own logic brackets the first letter of any menu item
  without an explicit `[X]` override, so `"HotSync"` renders as `"[H]otsync"` on the serial menu for
  free) and now calls `enterHotsyncMode()` — **keeping the pre-existing SD-card-presence gate in front
  of it unchanged** (same `SD.begin()`+"PLEASE INSERT SD CARD" pattern the SASD branch right below it
  already used).
- `fileLoop()`/`protocolLoop()`/`listFilesLoop()`/`fileMenu()`/`protocolMenu()`/`listFilesMenu()` and
  `playback.cpp`'s `listFiles()`/`getFileSize()` were **deliberately left in place**, unreachable but
  harmless (confirmed via grep that nothing else in the codebase depends on them), rather than swept
  out in a wider, riskier deletion pass. See §7.

---

## 5. Shared cross-mode lessons

### 5.1 The "custom-drawn screen bypasses the menu system's own redraw tracking" bug class

Both SASD and HotSync draw their own title/status directly to the OLED rather than going through
`showHeader()`. `showHeader()`'s own optimization — full `display.clearDisplay()` + border redraw only
when `lastMenu != menuMode`, otherwise just a small selection-highlight clear — assumes it's the *only*
thing ever drawing to the screen. Any mode that draws custom content without updating `lastMenu` can
leave stale/corrupted pixels (including the border) behind when the menu system later assumes a
partial redraw is enough. **Fix pattern**: set `msgFlag = true;` right before the `mainMenu(false)` (or
equivalent) call that returns control to a `showHeader()`-driven screen — this is an *existing*,
already-established idiom in this codebase (`modem.cpp`), not something invented for this session.
**If a third custom-drawing mode is ever added, check for this class of bug proactively** — it will
not show up until the user happens to enter that mode directly from the main menu without visiting
another mode first (which is why it can go unnoticed for a while).

### 5.2 Dotfile/resource-fork filtering

Both this project's PalmOS-SDK side (Address35/CryptoNote SD-card browsing, "Show Hidden" checkbox)
and now HotSync's own folder scan have independently hit the same gotcha: a `.`-prefixed filename
(macOS AppleDouble resource forks, ordinary hidden files) can still pass a plain suffix/extension
check. Check the *basename*, not the raw path, when filtering — robust regardless of what a given
filesystem API returns for `name()`.

### 5.3 `liveBaudStr()` — shared compact baud formatter (`display_menu.cpp`/`.h`)

```cpp
String liveBaudStr(uint32_t baud) { return String(baud / 1000.0, 1) + "k"; }
```
`9600` → `"9.6k"`, `57600` → `"57.6k"`. Used by both SASD's and HotSync's own header draw for the
**live** `PhysicalSerial.baudRate()` value — deliberately uniform (always divide-by-1000, one decimal,
always `"k"`) rather than replicating `baudDisp[]`'s own inconsistent mixed style (plain `"9600"` but
`"57.6k"` with a suffix, elsewhere in this codebase). Both modes need the *live* rate specifically
(not the configured `baudDisp[serialSpeed]` every ordinary menu's `showHeader()` shows) because both
actively change their own UART rate mid-session.

### 5.4 Retry/timeout tuning for this protocol family

The SASD reference firmware's own bare-metal tuning (`FRAG_MAX_RESENDS=4` at ~150ms/attempt) is
appropriate for SASD's own always-reactive role (WiRSa never initiates, so its own retry budget for
replies never really gets exercised under real load) but is **~25× tighter** than what a real desktop
HotSync client actually uses (pilot-link's `PI_PADP_TX_TIMEOUT=2000ms × PI_PADP_TX_RETRIES=10`) — this
was a genuine root cause of a real HotSync handshake failure (§4.4 item 6). If any future protocol work
in this family needs to originate requests toward a real device (not just replies), lean toward
pilot-link's real numbers, not the embedded reference's.

---

## 6. SLIP Gateway — high-level notes on improvements

(Predates this round of Palm-specific work; summarized here for completeness since it's part of the
same overall real-hardware reliability effort. Full detail in `slip_nat.cpp`'s own inline comments.)

The SLIP↔WiFi NAT engine (`slip_nat.cpp`/`.h`, `slip.cpp`, `slip_mode.cpp`) gained, across several
real-hardware rounds:
- **IP/TCP/UDP checksum validation** on incoming packets from the vintage computer — corrupted packets
  are now dropped rather than trusted and forwarded, closing a real class of NAT-layer corruption.
- **TCP ACK-regression handling** — a real bug where a duplicate/out-of-order ACK could regress the
  proxy's own sequence tracking, corrupting subsequent data.
- **Client→server sequence-number validation**.
- **Diagnostic logging** (`usbDebug`-gated) throughout the NAT path.
- **Self-connect/hairpin loopback fix** (added alongside §10's FTP server, but a general NAT-relay fix,
  not FTP-specific): `natProcessTcp`'s SYN-connect path used to open a real outbound
  `socket()`+`connect()` to whatever destination IP a packet specified, with no special case for a
  destination that's WiRSa itself. When a Palm (via SLIP) tries to reach a TCP service running ON
  WiRSa — e.g. §10's own FTP server — that connect has to leave over the WiFi radio and be reflected
  back by the AP to the same station that sent it, which most APs/WiFi drivers don't support (client
  isolation, or the station's own driver dropping frames sourced from itself). Symptom: the TCP
  handshake could partially/sporadically complete (the server-side application would log that it
  responded) while the actual response payload never made it back through `natSendTcpToClient()`. Fix:
  when the destination IP matches an address identifying WiRSa itself (`ctx->slipIP`, `WiFi.localIP()`
  when STA-connected, or `WiFi.softAPIP()` otherwise), the relay's outbound `connect()` is redirected
  to `127.0.0.1` instead (same port) — `WiFiServer`/`FtpServer` bind `INADDR_ANY`, so a loopback
  connection reaches the identical listening socket without ever touching the WiFi driver. No
  client-visible protocol change. **User-confirmed working on real hardware** — a full lazyFTP session
  (login, PASV, LIST, directory listing displayed correctly) through the gateway to WiRSa's own FTP
  server, after this fix.

These four rounds together resolved a real, previously-mysterious **upload-corruption bug** that had
been showing up in lazyFTP transfers over SLIP — traced all the way down to the gateway layer, not the
FTP app itself. If a future symptom looks like "data arrives corrupted but only sometimes, and only
over SLIP" — check here first, the checksum/seq-validation layer is exactly what catches this class of
issue now.

**2026-09 update**: SLIP Gateway now starts **immediately** from the main menu (`enterSlipMode()` called
directly from `mainLoop()`'s dispatch) instead of opening an intermediate submenu first; BACK from the
active gateway screen still returns to that submenu as before (`slipActiveLoop()`'s own handler, unchanged).
See §12.3 for the WiFi-connect behavior at entry, which changed at the same time.

---

## 7. What's removed / where this fork differs from upstream WiRSa

Comparing against the upstream feature list ([nullvalue0/WiRSa](https://github.com/nullvalue0/WiRSa)'s
own README, which this fork's own root `README.md` still substantially matches for the *unmodified*
parts):

| Upstream feature | Status in this fork |
|---|---|
| **MODEM Mode** (AT-command dial-up/telnet emulation) | **Removed entirely** (`modem.cpp` deleted, ~2,304 lines). Menu slot repurposed as `MODE_FTPSERVER` — see §10. The handful of general-purpose menu-loop functions `modem.cpp` also happened to own (`getLine`/`prompt`/`led_on`/`printSDCardInfo`, and `mainLoop`/`baudLoop`/`orientationLoop`/`settingsLoop`/`fileLoop`/`protocolLoop`/`listFilesLoop` — none modem-specific) were relocated into `display_menu.cpp` before deletion, since `file_transfer.cpp`/`slip_mode.cpp` still depend on some of them via `extern`. |
| **Web config server** (built-in web UI for status/config/file management) | **Removed entirely** (`web_ui.cpp`/`web_ui_html.cpp`/`web_ui.h` deleted). Configuration now happens via the OLED+button menu or typed serial only (unchanged from before for the settings that already worked that way; FTP/AP credentials use the same mechanism — see §10). |
| **Serial Config** (data bits/parity/stop bits menu entry) | **Removed from the menu**, locked to 8-N-1 via a named constant (`WIRSA_SERIAL_FORMAT`, `main.cpp`) — source-editable if a different format is ever needed, just no longer a runtime setting. Baud Rate is untouched, still user-selectable. |
| Telnet Server | Unchanged |
| **File Transfer** (XModem/YModem/ZModem via SD) | **Menu entry replaced by HotSync.** Underlying code (`file_transfer.cpp`, `fileLoop()`/`protocolLoop()`/`listFilesLoop()`/`fileMenu()`/`protocolMenu()`/`listFilesMenu()`, all now in `display_menu.cpp` since `modem.cpp` no longer exists) is still present in the tree, compiles, but is **unreachable from the main menu** — deliberately left in place rather than deleted, see §4.8. |
| **Text Playback** (display text files from SD via serial) | **Menu entry replaced by SASD SDCard.** `playback.cpp` was trimmed to just the shared remainder (`displayChar`/`listFiles`/`getFileSize`) still used by the now-orphaned File Transfer code. |
| **PPP Gateway** | **Removed entirely** (`ppp*.cpp`/`.h`, ~4,662 lines) to make room. Two things the removal had to also catch: a *second*, keystroke-driven copy of the main menu handler (in what was then `modem.cpp`'s own `mainLoop()`, separate from the button-driven one in `display_menu.cpp`) also had its own PPP menu slot + `ATDT PPP`/`777` dial-string shortcut, both needed removing with careful menu-index renumbering. |
| SLIP Gateway | Unchanged menu position; substantially hardened, see §6; **2026-09: starts immediately, no submenu-first** |
| — | **New: FTP Server mode** (§10) — replaces MODEM Mode's slot |
| Utilities | **2026-09: reduced to BACK/Retry WiFi/Information** — Auto-Detect Baud, Hex Dump Mode (toggle), and Loopback Test menu items removed (their functions still work via `AT$BAUD?`/`AT$HEX`/`AT$LOOP`, just not as menu items); Signal Monitor removed entirely (was fully dead — its toggle had no consumer since the "modem mode" overlay it once controlled was removed earlier, and the continuous-monitor function it was meant to launch was never actually called from anywhere). "Retry WiFi" and the redesigned scrollable "Information" screen are new — see §12.6/§12.7. |
| Config | Gained "FTP Setup"/"AP Setup" entries, lost "Serial Config" (see above) and "Default Menu" (no longer meaningful with MODEM Mode gone — its only purpose was choosing Main Menu vs. MODEM Mode as the boot default). **2026-09**: "About" now shows a static "WiRSa BUILD / for Palm" instead of the label + dynamic build-version string. |
| Web Interface | **Removed entirely** — see the Web config server row above |
| — | **New: SASD SDCard mode** (§2) |
| — | **New: HotSync mode** (§4) |
| **RTS/CTS hardware flow control, DTR signaling** | **Removed entirely, 2026-09** — confirmed not wired on this hardware. Pins, `pinMode`/`digitalWrite` calls, and the diagnostics-screen display of their states are all gone; DCD/DSR/RI are untouched. GPIO15's boot-safety drive (an ESP32 strapping-pin requirement, unrelated to RTS) was kept, just re-documented as such. See §12.5. |
| **10 speed-dial entries** | Only ever usable for dialing in MODEM Mode — gone along with it (not separately called out in the original table above, noted here for completeness). |
| — | **New: background WiFi auto-connect/reconnect manager**, a WiFi status icon on every title bar, and consistent live-baud display on every title bar — see §12 in full. |

**Net menu slot count is unchanged** — every addition repurposed an existing slot (§1) rather than
growing the main menu, and PPP's removal is what created room for the mode-slot count to stay flat
despite two net-new features once File Transfer/Text Playback were also repurposed.

---

## 8. References

- **This fork**: (working tree used for all the above) `~/WiRSa/WiRSa/`
- **Upstream WiRSa**: [github.com/nullvalue0/WiRSa](https://github.com/nullvalue0/WiRSa)
- **SASD hardware/firmware, by dmitry.gr** (both the SASD protocol §2 and the HotSync install client
  §4 are built from two different parts of this same reference project):
  [dmitry.gr/?r=05.Projects&proj=32.%20SASD](https://dmitry.gr/?r=05.Projects&proj=32.%20SASD) — local
  reference source used throughout: `~/Downloads/sasdSource/SASD_release/software/serialSD/firmware/`
- **pilot-link** (real desktop-side DLP/PADP/SLP/CMP implementation, used to cross-validate and fix
  real gaps in the embedded reference — see §4.4):
  [github.com/jichu4n/pilot-link](https://github.com/jichu4n/pilot-link) — local copy:
  `~/WiRSa/pilot-link/`
- **HotSync protocol overview** (background reading, confirmed the general architecture and PADP
  timeout numbers, no byte-level detail): [jichu4n/palm-sync — "How Palm OS HotSync
  Works"](https://github.com/jichu4n/palm-sync/blob/master/docs/how-palm-os-hotsync-works.md)

---

## 9. Build/verification notes

The user builds and flashes WiRSa on their own machine — `Firmware/platformio.ini` is
deliberately gitignored/machine-specific — as a standing workflow rule. **Compile-only check
(2026-10):** this environment has the ESP32-S3 toolchain and Arduino core under
`~/.platformio`, so a scratch copy of `Firmware/` builds with PlatformIO Core 6.1.19 in a
virtualenv and a stand-in ini (`board = esp32-s3-devkitc-1`, `board_build.partitions =
min_spiffs.csv` (since the virtual card, `partitions_vdisk.csv`, §16.1), `lib_deps` Adafruit SSD1306 / ArduinoJson / arduino-timer, plus
`-Wl,-Map,...` for a map file). It catches compile/link errors and gives size numbers; it
doesn't replace the user's own build, and nothing is flashed. §14 used it. Everything up to
§13 was verified by careful manual code review (brace balance,
exact wire-format byte-for-byte comparison against the reference sources) plus real-hardware testing
rounds directed by the user, iterating on `usbDebug`-gated serial log output — **not** by a local
compiler. No POSE64 (or any Palm emulator) equivalent exists for testing either mode, since WiRSa is
the physical HotSync/SASD *target* here, not the Palm side — verification is real-hardware only.

---

## 10. FTP Server mode

General-purpose (not Palm-specific like §2/§4), but added mainly to close the loop on a Palm
upload-then-HotSync/SASD workflow: upload a `.PRC`/`.PDB` to WiRSa's SD card from a desktop over WiFi
(or, once §6's hairpin fix landed, from the Palm itself over SLIP via the lazyFTP client app —
`apps/lazyFTP.md`), then HotSync/SASD it onto the device without a cable swap. Replaces MODEM Mode's
menu slot (§7) — unlike SASD/HotSync's mode-slot reuse, this repurposes the slot's *number*
(`MODE_FTPSERVER`, still 16 originally used by MODEM) but the label/behavior are entirely new.

### 10.1 Architecture

New module, `include/ftp_server.h`/`src/modules/ftp_server.cpp`, wrapping the vendored
[MultiFTPServer](https://github.com/renzo-mischianti/MultiFTPServer) library
(`Firmware/lib/MultiFTPServer/`, matching the existing `lib/CRC`/`lib/LinkedList` vendoring pattern):
- `ftpServerStart(bool apMode)` — refuses to start if SASD mode is active (§10.3); mounts the SD card
  (`SD.begin`); in AP mode, configures `WiFi.softAP()` (warns and falls back to an open network if the
  configured password is under 8 chars — ESP32's own WPA2-minimum behavior) and re-announces mDNS; in
  STA mode, calls `connectWiFi()` if not already connected; then `FtpServer::begin(user, pass)` (or
  `begin()` with no args for anonymous, if `ftpUser` is blank).
- `ftpServerStop()` — `FtpServer::end()`, tears the AP back down if one was started.
- `ftpServerPump()` — `FtpServer::handleFTP()`. Called **unconditionally every `loop()` iteration**,
  outside the `menuMode` dispatch chain (same non-blocking-pump idiom `slip_nat.cpp` already
  established — this codebase has no FreeRTOS tasking anywhere) — this is *why* HotSync and the FTP
  server can run concurrently (§10.3).
- New top-level menu, **`FTP Server`** (`ftpServerMenu()`/`ftpServerLoop()`, `display_menu.cpp`):
  Start / Start (AP Mode) / Stop / Status (live "Started"/"Stopped" line).
- New Config-menu entries **`FTP Setup`**/**`AP Setup`** (`settingsMenuDisp[]`, `main.cpp`) — set
  username/password or SSID/password via the same generalized character-entry primitive used by
  WiFi Setup's own password step (`wifi_setup.cpp`'s loop, parameterized to take a target
  buffer/max-length/prompt instead of being hardcoded to the WiFi password), which already supports
  both OLED+button entry and typed serial. Blank FTP username+password means anonymous login is
  allowed (`FtpServer::begin()` with no args). Credentials persist in EEPROM
  (`ftpUser`/`ftpPass`/`apSsid`/`apPass`, `settings.cpp`, same `setEEPROM`/`getEEPROM` pattern as
  `ssid`/`password`) — "running" state itself is not persisted, matching how WiFi connect state
  already worked (only credentials survive reboot, not "was it on").

### 10.2 Vendored-library configuration — a real gotcha, fixed once

`MultiFTPServer`'s ESP32 default storage target is `STORAGE_FFAT` (internal flash), not the SD card —
and its `STORAGE_TYPE`/`FTP_MAX_SESSIONS`/`FTP_SERVER_DEBUG` config is read by **two separate
translation units** that each do their own preprocessing pass: this project's own `ftp_server.cpp`,
*and* the library's own `lib/MultiFTPServer/FtpServer.cpp` (which has its own independent
`#include <FtpServer.h>`). A `#define` written in only one of those `.cpp` files is invisible to the
other — the real root cause of an early "FTP client sees no SD card contents" bug: the library's
actual compiled `openDir()`/`doList()` were silently using the un-overridden internal-flash default the
whole time, regardless of what `ftp_server.cpp`'s own view of the class claimed. **Fix, and the
project's standing convention going forward**: edit the vendored library's own designated config
header, `lib/MultiFTPServer/FtpServerKey.h`, directly — `DEFAULT_STORAGE_TYPE_ESP32` set to
`STORAGE_SD`, `FTP_MAX_SESSIONS` pinned to `1` (single-user device). Since both translation units
`#include` that identical physical file, editing it there is the only fix that reaches both correctly.
Deliberately **not** done via `platformio.ini` `build_flags` even though that would also technically
work — explicit user preference: config for a vendored library belongs in a source/header file the
project already owns, not in a gitignored, machine-local build setting.

### 10.3 Interaction with other modes

- **Mutually exclusive with SASD mode**, in both directions, via a `showMessage()` popup rather than
  fine-grained write-blocking (SASD does raw-sector I/O bypassing the FAT filesystem entirely, so
  running the FTP server's normal FAT writes at the same time risks real card corruption): SASD's own
  `enterSasdMode()` (`sasd.cpp`) checks `ftpServerRunning` and refuses with "Cannot start.\nStop FTP
  first"; `ftpServerStart()` checks `menuMode == MODE_PLAYBACK` (SASD's mode slot) and refuses with
  "Cannot start.\nStop SASD first". **User-confirmed working on real hardware**, both directions.
- **Runs concurrently with HotSync mode**, no interlock needed. HotSync reads the disk (directory
  scans, install-source files pushed to the Palm over DLP) and moves each installed file from INSTALL
  to INSTALLED (`rename`; since 2026-10 it also creates its folders), and never remounts the SD card.
  **User-confirmed working on real hardware** ("hotsyncd works with FTP server running"). On
  `improved` both may hold the virtual card: `vdfsMount()`/`vdfsUnmount()` are counted (§16.4).
- **Self-connect/hairpin loopback through the SLIP Gateway** — see §6's own entry; this was found and
  fixed specifically while testing the FTP server reached via lazyFTP over SLIP, but the fix itself
  lives in the general-purpose NAT relay, not this module.

### 10.4 Verification status

Build-verified by the user (at the time this environment had no ESP32 build set up — see §9). Real-hardware confirmed: SASD↔FTP mutual exclusion (both
directions), HotSync running concurrently with the FTP server, and a full lazyFTP session (login, PASV,
LIST with correct file sizes) through the SLIP Gateway to WiRSa's own FTP server, after §6's hairpin
fix. The FTP server's share of flash was later measured at ~20 KB linked (§14).

### 10.5 Two-folder root: SDCard and ESP32 (2026-10-23, `improved`)

The server's `STORAGE_MANAGER` is no longer `SD` but `gFtpRootFs` (`ftp_rootfs.h`/`.cpp`), so the FTP
root lists the SD card and the virtual card as folders. See §16.6.

---

## 11. BitWarden Relay mode

New module, `include/bitwarden_relay.h`/`src/modules/bitwarden_relay.cpp`. Unlike every other mode in
this document, this one has a genuine **counterpart application** on the Palm side (`hackmaster_y2035/
apps/bitwarden/`, reference doc `Bitwarden.md` in that same repo) — the two were built together against
one shared spec, `apps/bitwarden/PROTOCOL.md`, which is the authoritative wire-format source of truth
for both; this section covers the WiRSa/firmware half only, plus the real bugs found getting the two
sides to actually agree on real hardware.

### 11.1 What it is, protocol family

WiRSa does **all** the real work — TOFU certificate discovery, Vaultwarden OAuth login (including 2FA),
`/api/sync` fetch, and full client-side decryption (AES-CBC cipher strings, RSA-OAEP org-key unwrap) —
and streams already-decrypted items to the Palm **one at a time, never buffering more than one item's
plaintext in RAM** (this is what keeps the whole pipeline within an ESP32-S3's ~128KB-class RAM budget
regardless of vault size). The Palm never talks to Vaultwarden directly and never sees a raw cipher
string. Own raw-UART framing (`type`/`uint16 bodyLen`/`body`/1-byte XOR checksum) — not HotSync/DLP, not
SASD's sector protocol; see `PROTOCOL.md` for the byte-exact spec.

**Phase 1 (current, as built) runs with no wire encryption at all** — the master password and every
decrypted vault field cross the physical cradle wire in cleartext. `BW_KEX`/`BW_KEX_ACK` (X25519 session
key exchange) are defined in the wire-constant enum and explicitly stubbed — `HELLO_ACK` advertises
`capFlags` bit0=0 so a well-behaved Palm client won't even attempt it against this build. This was a
deliberate phase-1 scope decision (prioritize proving the network/decrypt pipeline on real hardware
first), not an oversight — but it means **this is not yet a secure link**, only the at-rest local
storage on the Palm side is encrypted today. Implementing `BW_KEX` is the natural next milestone if this
mode is revisited.

### 11.2 Architecture

- **TOFU trust** (`BW_TRUST`/`BW_TRUST_RESULT`): a `WiFiClientSecure::setInsecure()` connection purely to
  read the server's certificate SHA-256 fingerprint + subject/issuer CN, handed back for the Palm to
  show a trust prompt and pin. Not reused for anything else — the real pin enforcement is a *separate*
  `verify(pinnedFingerprint, host)` call on the connection actually used for login/sync.
- **Login** (`bwLogin`): `POST /identity/connect/token`, `grant_type=password`. Every
  `WiFiClientSecure`/`HTTPClient`/mbedTLS call in this file was checked directly against the actual
  vendored Arduino-ESP32 framework source (`~/.platformio/packages/framework-arduinoespressif32`) rather
  than assumed, since this sandbox had no ESP32 build set up at the time (§9) — notably,
  `verify()` must be called *after* `connect()` (it checks an already-open connection's peer cert, it
  doesn't configure a policy for future connects), and `HTTPClient::connect()` reuses an
  already-connected client rather than reconnecting, so one connect+setInsecure+verify per sync is
  reused across the prelogin/token/sync requests that follow.
  - **A parallel OAuth `refresh_token` login path was designed, fully implemented, and then fully
    reverted in the same session** — worth recording as a considered-and-rejected design, not just
    omitting it silently. After a raw response-key dump seemed to show this account's Vaultwarden server
    never issuing a `TwoFactorToken` (the "remember this device" value — see 11.4), a second mechanism
    was built: `bwRefreshLogin()`, tried before the normal password+2FA path whenever the Palm had a
    stored `refresh_token`, plus new `SYNC`/`SYNC_DONE` wire fields and matching Palm-side storage. This
    was **entirely removed** once the real root cause (11.4) was found — the mechanism was solving a
    problem that turned out not to exist. It was also a genuine risk in its own right: OAuth refresh
    tokens are JWTs of essentially unbounded length, and sizing a receive buffer for one directly caused
    a real Palm-side `datares` link overflow during that same experiment (see `Bitwarden.md` §2 for the
    Palm-side memory-budget details). If reconsidering this path, budget for that up front rather than
    re-discovering it.
- **`/api/sync` fetch + stream** (`bwSyncAndStream`/`bwStreamSyncBody`/`bwProcessOneCipher`): a
  filtered, depth-tracked JSON scanner (`BwJsonScanner`) walks the raw HTTPS response stream directly
  (never materializes the whole ~2MB+-class response in RAM), decrypting and immediately forwarding each
  folder/org/cipher as its own `FOLDER`/`ORG`/`SYNC_ITEM` frame, discarding the plaintext right after.
  `itemType` is derived from *which* sub-object (`login`/`card`/`identity`) is actually present in a
  cipher's JSON, not from its own `type` field read in isolation — trusting `type` alone risked sending
  a tail shape (see `PROTOCOL.md`'s own per-type field list) that the byte itself didn't match, silently
  desyncing every field read after it on the Palm side (11.5, bug 3).

### 11.3 Ack-retry-in-place redesign — the *level* the fix belongs at, not just the count

Every `FOLDER`/`ORG`/`SYNC_ITEM` blocks on the Palm's own `ITEM_ACK` before the next one is sent
(`bwWriteFrameAcked`) — added after real-hardware testing showed WiRSa's original free-running burst
overrunning the Palm's small, fixed serial receive buffer. A missed/slow ack is now retried **in place**:
`bwWriteFrameAcked` resends the exact same frame and re-waits, up to **5 attempts**, no reconnect, `500ms`
between attempts, `BW_ITEM_ACK_TIMEOUT_MS = 10000` per attempt — explicitly matching the Palm's own
`SendItemAckWithRetry`/`RecvFrameWithRetry` (`Sync.c`) cadence, on explicit user request, so neither side's
patience runs out well before the other's.

This was **not** the original design, and the earlier version was actively harmful, not just slower: a
missed ack used to be retried only via the *outer* `bwHandleSync` loop — a full TLS reconnect + fresh
`/api/sync` refetch + resume from `skipFolders`/`skipOrgs`/`skipCiphers`. That's the wrong *level* for a
single missed ack specifically because, if the Palm's own per-item retry has already exhausted its own
budget and given up (sending `BW_BYE` and closing its port), the outer retry just resends into a
connection nothing is listening to anymore — the exact real-device report this fixes ("firmware then
just continues to try again but the palm have terminated the sync"). `palmSaidBye` (an out-parameter on
`bwWriteFrameAcked`) distinguishes this case explicitly: set true *only* when the "different frame"
received back is `BW_BYE`, in which case the retry loop bails immediately (no point burning the rest of
the attempt budget resending into dead air) and the failure is classified `BW_ERR_CONNECT_FAIL`
(non-retryable), same as an ack exhausting all 5 attempts without ever getting a `BW_BYE` either. **The
outer whole-`/api/sync`-refetch loop (`kMaxSyncFetchAttempts = 3`) still exists**, but is now reserved
for a genuinely different failure domain — a Vaultwarden-side stream stall or malformed-JSON response —
where reconnecting and talking to Vaultwarden again can actually help; a `FOLDER`/`ORG` array's own
malformed-JSON-element failure is deliberately still classified `BW_ERR_SYNC_FETCH_FAILED` (retryable)
and kept distinct from an ack failure (`folderAckFailed`/`orgAckFailed` locals) at the exact same call
site, so the two failure classes never get conflated into the same retry decision.

### 11.4 2FA-remember flow, and the real bug behind "2FA every sync"

`SYNC`'s own `rememberedTwoFactorToken` field, if non-empty, is used as `twoFactorToken` with
`twoFactorProvider=5` (Bitwarden's own numeric "Remember" pseudo-provider) on the very first login
attempt of a sync — skipping the interactive `TWO_FACTOR_REQUIRED`/`TWO_FACTOR_SUBMIT` exchange entirely
when it's still valid. Every token-exchange request (remembered-token or freshly-typed code) sends
`twoFactorRemember=1`, so a successful login by either path can receive a renewed `TwoFactorToken` back,
reported to the Palm via `SYNC_DONE`'s own `updatedTwoFactorToken` field (and, since a later fix, also
via `SYNC_ERROR` — see 11.5, bug 6).

**The real, root-cause bug, found by reading Vaultwarden's own open-source Rust server code directly**
(not by guessing, and not resolved by the refresh-token detour above): the request field is
`two_factor_remember: Option<i32>` server-side, not a boolean. This code was sending the literal string
`"true"` for it — Rocket's form parser can't parse that as an integer, so it silently fell back to
`None` for that one field, while the rest of the request still parsed and authenticated fine (which is
exactly why nothing *looked* broken — login kept succeeding, every single sync). Server logic is
literally `if remember == 1 { issue a TwoFactorToken }`; since it never actually saw `1`, the token was
never generated, regardless of anything else about the request. Fixed by sending `"1"` instead
(`bwLogin`'s own `twoFactorRemember=1` line). Confirmed on real hardware: the very next sync's response
contained a genuine `TwoFactorToken` — measured **596 bytes** (it's a JWT), which then required its own
follow-up fix on the Palm side (a storage-cap bump — see `Bitwarden.md` §7, bug 7).

**Diagnostic technique that found this, worth reusing**: a raw dump of the response's own JSON key
*names* (never values — those are live session secrets) was added to `bwLogin` right after a successful
login, printed only when a 2FA code had actually been submitted. That one line (`BW: token response
keys: access_token expires_in ... Key`) is what showed, unambiguously, that `TwoFactorToken` was
genuinely absent from the response at the time — ruling out a parsing bug on WiRSa's own side, and
directly motivating the (initially wrong) hypothesis that this Vaultwarden deployment simply didn't
support the feature, which in turn led to reading the actual server source to check.

### 11.5 Round-by-round bug list — for future debugging reference

Chronological, each with symptom → root cause → fix:

1. **Free-running item burst overran the Palm's serial buffer.** Fixed by the `ITEM_ACK` flow-control
   design (§11.3) — WiRSa now blocks after every `FOLDER`/`ORG`/`SYNC_ITEM` until the Palm acks it.
2. **Ack-timeout-vs-retry redesign** (§11.3, in full) — the whole-`/api/sync`-refetch retry for a single
   missed ack was replaced with retry-in-place, and `BW_ITEM_ACK_TIMEOUT_MS` was shortened from an
   original, much longer value to `10000`ms on explicit user request ("in general an ack is received
   within a second... can we seek to auto retry after 10s of no response?"), paired with a stale-byte
   drain (`while (PhysicalSerial.available()) PhysicalSerial.read();`) right before every resend so a
   late ack from an already-abandoned attempt can never be misread as belonging to the new one.
3. **`itemType`/tail-shape desync risk.** `itemType` used to come from `c["type"]` read in isolation;
   fixed to be derived from the same sub-object-presence check that decides the type-specific tail's own
   shape (§11.2), so the two can never disagree. A pre-send diagnostic log (`"about to send item
   id=... itemType=... has(login=...,card=...,identity=...)"`) was added specifically because the
   *existing* post-ack log never fired for an item whose ack failed — the one case that actually needed
   diagnosing.
4. **Skip-counter duplicate-record bug.** `skipFolders`/`skipOrgs`/`skipCiphers` used to advance only
   *once*, after an entire array finished sending successfully — so a retry triggered partway through
   (say, folder #40 of 60 failing to ack) would leave the counter at its stale prior value, and the next
   attempt re-sent folders #0–39 that had already been sent+acked in the failed attempt, creating
   duplicate records on the Palm. Fixed: each counter now advances immediately, per element, the moment
   it's confirmed (successfully acked, or already-skipped from a prior attempt) — a retry correctly
   resumes from wherever the *last* attempt actually got to, not from the start of the array.
5. **`BW_BYE`/`palmSaidBye` non-retryable classification.** See §11.3 — a missed ack was originally
   always treated as possibly-transient and retryable via the outer loop, including when the "different
   frame" received back was an explicit `BW_BYE`. Fixed by classifying that specific case as terminal
   (`BW_ERR_CONNECT_FAIL`) instead.
6. **The 2FA-remember bug and fix** — §11.4 in full (the `Option<i32>`/`"true"` mismatch).
7. **`SYNC_ERROR` now also carries a rotated token.** Vaultwarden rotates (invalidates) the remember
   token the *moment* login succeeds, independent of whatever happens to the rest of the sync
   afterward. A sync that failed mid-progress *after* a successful login (a lost Palm connection, a
   Sec35/vault lock reported back as a failure) used to silently discard the one token that was actually
   still valid, since it was only ever reported via `SYNC_DONE` — guaranteeing a fresh interactive 2FA
   prompt on the very next attempt even though 2FA had just been completed. Fixed by adding the same
   `updatedTwoFactorToken` field to `SYNC_ERROR` (via `sendErr`'s own new optional `token` parameter),
   populated whenever the failure is reachable only after a successful login (in practice, codes 7/8 —
   see `PROTOCOL.md`'s own `SYNC_ERROR` section for the full code list). The Palm side's own matching fix
   (a short best-effort attempt to catch a trailing `SYNC_ERROR` even after its own abort paths would
   otherwise close the port immediately) is `Bitwarden.md` §3/§7.

### 11.6 Verification status

**Confirmed on real hardware this session**: the ack-retry-in-place redesign end to end (both the
5-attempt cadence and the `BW_BYE`/non-retryable classification); the skip-counter per-element-advance
fix; the `twoFactorRemember=1` fix (a real 596-byte `TwoFactorToken` observed in a live response,
confirmed via the raw key-name diagnostic in §11.4); the `SYNC_ERROR`-carries-a-token fix, in
conjunction with the Palm-side trailing-frame catch (both sides needed to land together for the "still
prompted for 2FA after a failure" report to actually resolve).

**Not yet confirmed / open**: `BW_KEX` (wire encryption) is not implemented at all — see §11.1. The
exact Vaultwarden response shape for an *incorrect* 2FA code (vs. no code submitted at all) was
implemented against the documented shape of the 400 response but never verified against a live server
during an actual wrong-code attempt. Build-verified by the user (and, since 2026-10, compiled clean in
the §9 scratch build).

### 11.7 WiFi-at-entry behavior change, 2026-09

`enterBitwardenRelayMode()` used to tolerate starting with no WiFi at all — it fired the old one-shot
blocking `connectWiFi()`, then proceeded regardless of the outcome, setting `bwWifiReady=false` and
showing a "WiFi connecting..." status; `bitwardenRelayLoop()` polled `WiFi.status()` every tick and only
started actually servicing the Palm once (if ever) WiFi came up in the background. This was unified with
FTP Server/SLIP Gateway's blocking entry pattern (§12.3): it now calls `waitForWiFiConnection()` and, on
failure (user pressed BACK during the wait), aborts back to the main menu instead of entering the mode in
a not-yet-ready state. See §12.3 for why this was necessary rather than optional polish: `maintainWiFi()`
pauses its own reconnect attempts while `menuMode==MODE_BITWARDEN` (§12.2), which means the old
"just wait for the background manager to bring WiFi up" approach would never actually succeed if WiFi
wasn't already connected at entry — a real latent bug this same session's own `transferActive` change
would otherwise have introduced.

---

## 12. Background WiFi manager, title bars, and Utilities redesign (2026-09)

Cross-cutting work touching every mode's OLED header and the WiFi bootstrap for every WiFi-requiring
feature. `network.cpp`/`.h`, `display_menu.cpp`/`.h`, `diagnostics.cpp`/`.h`, `main.cpp`,
`sasd.cpp`/`slip_mode.cpp`/`bitwarden_relay.cpp`/`ftp_server.cpp`, `globals.h`.

### 12.1 Per-feature compile-time toggles (`globals.h`)

`WIRSA_FEATURE_FTPSERVER`/`HOTSYNC`/`SASD`/`SLIP`/`BITWARDEN`/`CALDAV`/`UTILITIES`, each `#ifndef`-guarded and
defaulting to `1`. `main.cpp`'s `buildMainMenu()`/`buildSettingsMenu()` populate the menu arrays and a
matching set of runtime index variables (`mmIdxFtp`, `mmIdxHotsync`, ... `smIdxFtpSetup`, ...) from these
flags at boot, instead of the old fixed-index static initializer lists; every dispatch site in
`mainLoop()`/`settingsLoop()` (`display_menu.cpp`) compares against the runtime index rather than a
literal number, and is itself wrapped in the same `#if WIRSA_FEATURE_X` guard as its menu entry — so a
disabled feature's keyboard shortcut disappears too, not just its visible menu row. Disabling
`WIRSA_FEATURE_FTPSERVER` also removes "FTP Setup"/"AP Setup" from the Config submenu (AP Setup has no
other consumer). Config itself is never gated — always built regardless of any flag.

### 12.2 Background WiFi manager (`maintainWiFi()`, `network.cpp`)

Called unconditionally every `loop()` iteration (same idiom as `ftpServerPump()`), non-blocking — only
ever calls `WiFi.begin()` (async, returns immediately) and polls `WiFi.status()`.

**Retry/give-up design** (revised mid-session per user spec — see the note below): once an SSID+password
are saved, it re-issues `WiFi.begin()` every `WIFIMGR_RETRY_MS` (10s) for up to one `WIFIMGR_GRACE_MS`
(60s) window. If still not connected when that window expires, it sets an internal `wifiMgrGaveUp` flag
and **stops calling `WiFi.begin()` entirely** until a fresh window is started — either by Utilities' new
"Retry WiFi" item (`retryWiFi()`, §12.6) or automatically by a WiFi-requiring service's own entry
(`waitForWiFiConnection()`, §12.3).

**This was NOT the original design** — it originally retried forever, on the stated theory that a
network reappearing (router reboot, AP back in range) should be picked up automatically with no user
action, and the 60s window was purely a UI concept (only controlling whether the title-bar icon animates
vs. shows a static "not found" X). **Real hardware contradicted this**: WiFi reconnection was observed to
actually stop working after about a minute regardless of what the retry code was doing — most likely an
ESP32 WiFi-driver quirk where repeated `WiFi.begin()` calls against an unreachable/not-yet-found SSID
eventually get the radio into a state where it stops really attempting association, even though the
calling code keeps issuing them. The explicit give-up-and-require-a-manual-or-service-triggered-retry
model was chosen specifically to match what the hardware actually does instead of a design that looks
correct in code but silently stalls in practice. **If revisiting this**: the deeper fix would likely be
cycling `WiFi.mode(WIFI_OFF)` → `WiFi.mode(WIFI_STA)` (a real radio reset) periodically instead of just
re-calling `WiFi.begin()`, but that wasn't attempted this session — flagged as the most promising next
step if the give-up/manual-retry workaround proves too coarse in practice.

**Pause during active transfers** (`transferActive`): reconnect attempts (`WiFi.begin()` calls) are
skipped entirely while `menuMode` is `MODE_FILE` (HotSync), `MODE_PLAYBACK` (SASD), or `MODE_BITWARDEN` —
`WiFi.begin()` was found to briefly stall the whole sketch while the radio driver resets, which was
disrupting these three timing-sensitive Palm↔WiRSa serial protocols. **SLIP Gateway is deliberately
excluded** from this pause — it depends on WiFi to function at all, so reconnecting while it's active is
wanted, not harmful.

On a fresh/recovered connection, `wifiOnConnected()` (factored out, shared with `waitForWiFiConnection()`
below) runs the one-time mDNS/telnet-server/firmware-check setup exactly once per association.

### 12.3 `waitForWiFiConnection()` — blocking entry helper for WiFi-requiring services

Used by FTP Server, SLIP Gateway, and BitWarden Relay at mode entry, replacing the old one-shot blocking
`connectWiFi()` (**removed entirely this session, zero callers left**). Deliberately does **not** go
through `maintainWiFi()`/its `transferActive` pause — it drives its own independent `WiFi.begin()` retry
loop directly, because several of its own callers (HotSync/SASD/BitWarden — well, BitWarden specifically,
see §11.7) are exactly the modes `maintainWiFi()` pauses reconnects for; routing through the background
manager here would deadlock.

Behavior: shows "Waiting for WiFi..." while retrying every 10s for up to one 60s window (same constants
as §12.2). If it connects, runs `wifiOnConnected()` and syncs `maintainWiFi()`'s own static state
(`wifiMgrWasConnected=true`, `wifiMgrTrying=false`, `wifiMgrGaveUp=false`) so the background manager's
next tick doesn't think it's still trying and re-run the one-time setup again. If the window expires, it
shows "WiFi unavailable. Retry?" — ENTER starts another window, BACK returns `false` to the caller.

**Each caller's cancel-path unwind**:
- **FTP Server** (`ftp_server.cpp`): just `return`s — leaves the user on whatever screen (the FTP Server
  submenu) they were already on, same as its pre-existing failure handling.
- **SLIP Gateway** (`slip_mode.cpp`, `enterSlipMode()`): restores `menuMode` from
  `slipModeCtx.previousMenuMode`, turns binary mode back off, and calls `mainMenu(false)`. **Fixes a
  latent bug found while making this change**: `previousMenuMode` was already being saved on every entry
  but was never actually read/restored anywhere before this session — a cancelled/failed entry used to
  leave `menuMode` stuck at `MODE_SLIP` with nothing actually running.
- **BitWarden Relay** (`bitwarden_relay.cpp`, `enterBitwardenRelayMode()`): turns binary mode back off and
  calls `mainMenu(false)`. See §11.7 for the full before/after — this mode used to tolerate starting
  without WiFi at all; that's no longer the case.

### 12.4 WiFi status icon (`drawWifiStatusIcon()`, `network.cpp`)

3 vertical bars, mobile-signal-strength style, in the same header slot every `showHeader()` call paints
(`iconX=72, iconY=4`, inside the white header bar). Three states:
- **Connected**: 0–3 bars lit, `getSignalBars()`'s existing 0–4 RSSI scale collapsed to 0–3.
- **Trying, within the 60s grace window**: bars animate (climbing 0→1→2→3 then resetting, driven by
  wall-clock time via `millis()/400`, not by how often `maintainWiFi()` happens to be called).
- **Not connected / gave up / WiFi never configured**: static X, same glyph the older `showWifiIcon()`
  full-header indicator already used for "no link".

**Shown on every title bar**, not just Main's — `showHeader()` (`display_menu.cpp`, shared by every
`showMenu()`-based screen) calls it directly as the last step of every header redraw; HotSync/SASD/SLIP
Gateway (active)/BitWarden Relay's own custom-drawn headers (`hsDrawHeader()`, `sasdDrawStatusLines()`
via `sasdShowStatus()`/`sasdUpdateActiveDisplay()`, `slipShowStatus()`, `bwDrawHeader()`) each call it
directly too, since they bypass `showHeader()` entirely. `maintainWiFi()`'s own periodic redraw
(animation frame advance) is no longer gated to `menuMode==MODE_MAIN` — it fires regardless of which
screen is showing.

### 12.5 Title bar text: shortened, and consistent live baud rate everywhere

Every title bar now shows the **live** baud rate (`PhysicalSerial.baudRate()` via the shared
`liveBaudStr()` helper, §5.3) — previously only HotSync/SASD showed live baud (via their own custom
headers) and every `showHeader()`-based menu showed the *configured* `baudDisp[serialSpeed]` value
instead; BitWarden Relay and SLIP Gateway's active screens showed no baud at all. This is now uniform
across all five header styles.

To make room for the WiFi icon (§12.4) without colliding with the baud text, several titles were
shortened: "WiRSa MAIN"→"Main Menu", "WiRSa CONFIG"→"CONFIG" (dropped the "WiRSa " prefix only, no re-casing),
"WiRSa DIAG"→"Utilities" (dropped the prefix *and* renamed to match the main-menu item that leads here),
"BitWarden Relay"→"Bitwarden", "SLIP Gateway"→"SLIP", "SASD SDCard"→"SASDCard" (single word, this last
one on explicit user request after the WiFi-icon work landed).

A new optional `displayTitle` parameter was added to `showHeader()`/`showMenu()` (`display_menu.cpp`/`.h`)
specifically for the "Utilities" rename — the internal menu name passed as the first argument (`"DIAG"`)
still drives the serial `"-=-=- DIAG MENU -=-=-"` banner unchanged; `displayTitle`, when non-empty,
overrides only the OLED header's own printed text. **The main-menu item labels themselves were not
touched** ("FTP Server", "HotSync", "SAS[D] SDCard", "SLIP Gateway", "BitWarden Relay", "Utilities",
"Config" in `mainMenuDisp[]` all read exactly as before) — only the header text shown once you're inside
each mode changed.

Also removed as part of this same pass: RTS/CTS/DTR signaling entirely (pins, `pinMode`/`digitalWrite`
calls, and their display on the Utilities diagnostics screen) — user-confirmed not wired on this
hardware. GPIO15's boot-safety `OUTPUT`+`HIGH` drive in `setup()` was kept (it's an ESP32
strapping-pin requirement, unrelated to RTS, previously mis-documented as doubling as the RTS init) along
with DCD/DSR/RI. The dead `flowControl`/`dtrMode`/`txPaused` settings and their EEPROM slots went too
(confirmed via a full-codebase grep: written but never read/branched on anywhere). Separately, ~65
duplicate pin/EEPROM `#define` constants that used to be copy-pasted across `main.cpp`/`network.cpp`/
`settings.cpp` were consolidated into `globals.h` as the single source of truth — this surfaced one real,
previously-silent bug: `network.cpp`'s own local `#define CTS_PIN 27` had drifted from
`main.cpp`/`globals.h`'s `#define CTS_PIN 5` (two different physical pins for the "same" signal, never
caught because `network.cpp` didn't include `globals.h` at the time, specifically to avoid this exact
collision) — moot now since CTS_PIN itself no longer exists, but `network.cpp` now includes `globals.h`
directly like every other module, closing the door on a repeat.

### 12.6 Utilities menu simplified

Removed **Auto-Detect Baud**, **Hex Dump Mode** (toggle), and **Loopback Test** as menu items — their
underlying functions (`baudAutoDetect()`, `setHexDumpMode()`/`hexDumpByte()`, `loopbackTest()`) are
unchanged and still reachable via `AT$BAUD?`/`AT$HEX`/`AT$LOOP` over serial; `hexDumpByte()` in particular
is still actively called from `serial_io.cpp` for real traffic logging whenever hex dump mode is on via
the AT command. Removed **Signal Monitor** entirely, including its EEPROM-persisted `signalMonitorEnabled`
toggle and `SIGNAL_MONITOR_ADDRESS` slot — confirmed fully dead: the toggle had no consumer anywhere (its
only intended reader was an OLED overlay in "modem mode", itself removed in an earlier pass — see §7), and
the continuous-monitor function (`signalMonitor()`/`signalMonitorDisplay()`) it was meant to launch was
never actually called from anywhere in the codebase even before this cleanup. `showSignalStates()`
(the one-shot `AT$SIG` variant) was kept.

New: **"Retry WiFi"** (`diagnostics.cpp`, between BACK and Information) calls `retryWiFi()` (§12.2) —
disconnects any stale association and resets `maintainWiFi()`'s statics so its very next tick immediately
re-issues `WiFi.begin()`, for use after the background manager has given up.

Utilities is now exactly: BACK, Retry WiFi, Information (`diagnosticsMenuDisp[]`, `main.cpp`).

### 12.7 Information screen redesign

Replaced a fixed `showMessage()` popup (which visibly overflowed the screen — system info easily exceeds
the ~4-line box) with a scrollable, HotSync/BitWarden-style view: boxed header (title + live baud +
WiFi icon, §12.4/§12.5) + up to 4 visible body lines at a time (firmware version, free/total heap, CPU
freq + chip model, SDK version, uptime, WiFi SSID/RSSI/IP or "Not connected", baud rate), with
DOWN/UP scrolling (up/down scroll-indicator glyphs reused from `showMenu()`'s own long-list convention)
and BACK returning to Utilities. `showSystemInfo()`/`showConnectionStats()` (the `showMessage()`-based,
serial-`AT$SYS`/`AT$STAT`-triggered versions) are unchanged and still used for those AT commands — this
is a separate, new function (`showInformationScreen()`) for the menu path specifically, not a
replacement of the AT-command display.

**A redraw bug was found and fixed while building this**: this screen draws directly via its own
`infoDrawHeader()`/`infoDrawStatus()`, bypassing `showHeader()` entirely, and never changes `menuMode`
away from `MODE_DIAGNOSTICS` while it's up — so on return to Utilities, `showHeader()`'s own
`lastMenu != menuMode` full-clearDisplay optimization didn't trigger (menuMode never actually changed),
leaving this screen's scrollable text/scroll-arrows as garbage behind the redrawn Utilities menu. Same bug
class as §5.1, fixed the same way: `lastMenu = -1;` forced right before the `diagnosticsMenu(false)` call
on exit.

## 13. CalDAV Relay mode

### 13.1 What it is, protocol family

Same shape as BitWarden Relay (§11): the Palm can't do TLS, so `caldav_relay.cpp` terminates
HTTPS, does the actual CalDAV `REPORT` (or a plain `GET` for an ICS feed, or a Google Calendar
API call), expands recurring events, and streams already-parsed occurrences back over the
cradle serial line one at a time, each individually acked — same per-item flow control as
BitWarden Relay, for the same real-hardware reason (a free-running burst overruns the Palm's
small fixed Serial Manager buffer). The Palm-side counterpart is `apps/caldav_sync/`
(creator `CDDV`) — see `apps/CalDAVSync.md` for the full Palm-side doc; this section covers
the WiRSa/firmware half only, and cross-references that doc rather than repeating it.

Four server types (`CDDVServerType` on the Palm side, mirrored here as
`CDDV_TYPE_CALDAV`/`CDDV_TYPE_ICLOUD`/`CDDV_TYPE_GOOGLE`/`CDDV_TYPE_ICSFEED`): CalDAV-generic
and iCloud share one `REPORT`/multistatus code path; ICS Feed is a plain authenticated-by-
nothing `GET`; Google Calendar goes through its own OAuth handshake + REST API path with no
uICAL/RRULE involvement at all (Google expands recurrences server-side).

Frame types (`include/caldav_relay.h`'s own header comment, verified against
`apps/CalDAVSync.md` §3 — the two sides agree):
```c
CDDV_HELLO = 0x01, CDDV_HELLO_ACK = 0x02,
CDDV_SYNC_REQUEST = 0x03, CDDV_EVENT_ITEM = 0x04, CDDV_ITEM_ACK = 0x05,
CDDV_SYNC_DONE = 0x06, CDDV_SYNC_ERROR = 0x07,
CDDV_OAUTH_START = 0x08, CDDV_OAUTH_INFO = 0x09, CDDV_OAUTH_TOKEN = 0x0A,
CDDV_BYE = 0x0B
```
Bootstrap baud is a fixed idle rate (`CDDV_BAUD_IDLE = 9600`); `CDDV_HELLO`/`CDDV_HELLO_ACK`
negotiate up to the Palm's configured target (capped at WiRSa's own configured max), mirroring
BitWarden Relay's identical idiom. The negotiated rate is **not** reverted after every
`SYNC_REQUEST` — "Sync All" on the Palm runs multiple requests back-to-back over one held
connection (`SyncEngineRunOne`'s own `keepOpen` parameter), so a per-request revert would
desync the Palm's next request. Revert only happens on a real `CDDV_BYE` (the Palm's own true
last sync, single or batch) or a `CDDV_NEGOTIATE_TIMEOUT_MS` (15s) safety-net if the Palm goes
silent after negotiating but never follows up. The same 15 s applies between the requests of a
"Sync All" (the window restarts after each request), so the Palm must not do slow local work
there: since 2026-10-14 CalDAV Sync only stages each calendar between requests and saves them
all after its BYE (`CalDAVSync.md` §7 item 10). Saving in between used to outlast the window
on a large calendar and hang the batch.

### 13.2 Shared framing (`relay_proto.h`/`.cpp`)

Extracted from `bitwarden_relay.cpp`'s original private frame-I/O layer so every relay mode
shares one implementation of the framing/checksum/ack-retry logic. **Both relays now use it**
(migrated 2026-09-30): CalDAV calls `RelayProto::` directly; `bitwarden_relay.cpp` keeps its
`bwReadFrame`/`bwWriteFrame`/`bwWriteFrameAcked` names as thin static wrappers that pin its own
port, timeouts and frame types (`BwWriter`/`BwReader` are aliases of `RelayProto::Writer`/`Reader`),
so its ~30 call sites are unchanged. Side effect: Bitwarden's ack read now uses a separate scratch
buffer, so an ack retry always resends the original frame body. Verified by review,
compiled clean in the §9 scratch build, and both relays synced on real hardware 2026-10-10.
Any new relay mode should build on this module rather than adding its own framing.

**Palm side, same idea (2026-09-30):** BitWarden and CalDAV Sync now share
`apps/lib/relayproto.c` (open/baud/close, polled read, `RpSendFrame`/`RpRecvFrame`) and
`apps/lib/relaywire.c` (body writer/reader), each app configuring it through its own
`relay_cfg.h` (receive buffer size, keep-alive/cancel hooks) and `RP_SECTION` in its seg header.

**Serial-link encryption (2026-09-30, mandatory, no plaintext fallback):** every relay frame is
encrypted. The Palm's `RpSecure()` (`apps/lib/relaysec.c`) runs an RSA-1024 key exchange right
after the port opens, before `HELLO`/`OAUTH_START`; afterwards `RpSendFrame`/`RpRecvFrame`
(Palm) and `RelayProto::writeFrame`/`readFrame` (WiRSa) apply AES-128-CTR per frame. WiRSa
generates its key pair in RAM on the first relay-mode entry after each boot (status
"Preparing..." briefly) and answers the exchange via `RelayProto::handleKex()`, called before
each relay's own dispatch. Plaintext non-exchange frames are dropped on both sides. Full wire
format: `apps/bitwarden/PROTOCOL.md`, "Serial-link encryption". Verified on the host (real Palm C
code against real WiRSa C++ code, and against an independent Python implementation in POSE for
both CalDAV Sync and BitWarden), then on real hardware: BitWarden and CalDAV syncs both
succeeded over the encrypted link (user-confirmed 2026-10-10). RSA uses e = 3 with WiRSa sending
R³ mod n, so the Palm's step is three Montgomery multiplications (0.25 s on an emulated Palm III).

Frame format (identical across every relay mode that uses this): `type(1B)` + `lenHi(1B)` +
`lenLo(1B)` + `body(len bytes)` + `checksum(1B)`, checksum = XOR of type/lenHi/lenLo/every body
byte — big-endian integers, uint16-length-prefixed strings. `writeFrame()` flushes internally
before returning: skipping that flush was a real, confirmed BitWarden Relay bug (the Palm saw
zero bytes of the very first acked frame — a clean timeout, not corruption) that this shared
module's contract now rules out by construction.

### 13.3 Server-type fetch paths

- **CalDAV-generic / iCloud** (share one code path): `cddvHttpRequest` issues the `REPORT`,
  `extractCalendarDataBlocksIncremental()` scans a small accumulator as response chunks arrive
  and emits every *complete* `<calendar-data>` block to `parseICalAndEmit` as soon as it's
  available — memory stays proportional to roughly one in-flight block, not the whole
  multistatus response (§13.5 #1 explains why this replaced a much simpler whole-buffer read).
  `parseICalAndEmit` is a hand-written parallel to uICAL's own `Calendar::load()` (§13.4)
  that groups VEVENT blocks by UID into master/override maps, expands the master's RRULE via
  `VEventIter`, and substitutes an override's start/end/summary/location when its
  RECURRENCE-ID matches a generated occurrence's original start time (a CANCELLED override
  suppresses that occurrence entirely). **Known, documented limitation** (in the code's own
  comment, not glossed over here): an override whose RECURRENCE-ID points at an occurrence
  that itself falls *outside* the query window isn't independently emitted.
- **ICS Feed**: `cddvFetchIcsFeed` streams the feed through a **two-phase incremental parse**
  and never buffers the whole body. Phase 1: `cddvOnIcsChunk` scans a small bounded accumulator
  (`gCddvIcsPartial`, grown in `kCddvIcsGrowStep` 8KB steps up to `kCddvIcsPartialCap` 48KB)
  for `BEGIN:`/`END:` `VTIMEZONE`/`VEVENT` boundaries, and `cddvIcsOnComponent` parses each
  completed component on its own into shared tzmap/masters/overrides maps, dropping a
  non-repeating master immediately if it falls outside the sync window. Phase 2:
  `cddvIcsFinishParse` expands occurrences from those maps once the feed ends. It's
  deliberately a separate copy rather than shared with `parseICalAndEmit`, so the working
  CalDAV/iCloud path isn't put at risk. Motivation: the old whole-body `String` hit a hard
  ~64KB `CAPACITY_MAX` on non-PSRAM boards (real Google Calendar export, real hardware).
  Working in regular real-device use (confirmed 2026-09-29).
- **Google Calendar**: no uICAL involvement at all. `cddvGoogleRefreshAccessToken` exchanges
  the Palm-supplied refresh token (+ `clientId`/`clientSecret`, both sent fresh in every
  `SYNC_REQUEST` — WiRSa holds no resident Google-account-identifying state) for a short-lived
  access token, then `cddvFetchGoogleCalendar` pages through the Calendar API's `events.list`
  (capped at 20 pages, `kCddvGoogleMaxPages`) with `singleEvents=true` — recurrence expansion,
  cancellation filtering, and RECURRENCE-ID substitution are all handled server-side by Google,
  so this path is structurally simpler than the uICAL-based one. **Note**: an earlier design
  intent (recorded in an older project memory) assumed the Client Secret would be optional for
  a "Desktop app"-type OAuth client — the shipped code requires it unconditionally
  (`cddvFetchGoogleCalendar`'s own signature takes `clientSecret` as a required parameter, not
  optional), matching the Palm-side task history's own late pivot ("Add googleClientSecret
  field... v3→v4"). Trust the code over the older memory here.

### 13.4 uICAL library extensions

Two small, additive extensions to the vendored `lib/uICAL/` library (zero changes to existing
behavior):
1. `VObject::listProperties(name)` (mirrors the existing `listObjects`) — `getPropertyByName`
   only returns the *first* matching line, but `EXDATE` can legitimately repeat per RFC 5545.
2. `DateTime::toEpochSeconds()` — no existing public accessor exposed the raw absolute UTC
   epoch seconds `DateTime` already held internally (`epochtime.epochSeconds`, protected),
   needed to build the wire protocol's `utcStart`/`utcEnd` fields. For a date-only
   (`VALUE=DATE`, all-day) value uICAL uses no zone, so this is the date at 00:00 rather than
   a UTC instant; the Palm keeps it as a date (see `CalDAVSync.md` §3 "Event times"). Google
   all-day `"date"` values reach the wire the same way via `cddvParseRfc3339`.

A real research finding that changed the implementation approach mid-build: the original plan
assumed uICAL's own `Calendar::load()` + `getPropertyByName()` could reach UID/LOCATION/STATUS
directly. In fact `Calendar::load()` hard-codes a `useLine` whitelist at the `VObjectStream`
level that discards UID/LOCATION/STATUS/EXDATE/RECURRENCE-ID during parsing itself, before
`VObject` ever sees them — `Calendar::load()` cannot be used for this at all. Fixed by writing
`parseICalAndEmit` (§13.3) as a parallel parse function mirroring `Calendar::load()`'s own
structure but with a widened `useLine` filter and its own VEVENT-processing loop instead of
building a `Calendar`/`VEvent` collection.

`tz.cpp`'s `TZ::parseOffset` also gained a named-zone offset fallback table plus acceptance of
`HHMMSS`-format (not just `HHMM`) numeric offsets — a real report showed a `VTIMEZONE`
registering successfully but the *wrong* offset still being used for actual event times,
traced to a 7-char offset string `parseOffset` didn't previously handle; Apple's own
`VTIMEZONE` blocks were confirmed to have the identical limitation (not an iCloud-specific
quirk).

### 13.5 Round-by-round bug list — for future debugging reference

Each entry: symptom as reported (or the risk anticipated), root cause, fix, verification
status. Several of these were found investigating a *different* symptom than what they turned
out to actually be — kept in for that reason, not softened.

1. **"Sync only shows past events, future ones missing" → `HTTPClient::getString()` silent
   truncation.** Confirmed via a direct `curl REPORT` against the exact same account/query
   (58 blocks returned correctly, future events present) that the server, time-range filter,
   and headers were all innocent. Real root cause: ESP32 Arduino's `HTTPClient::getString()`
   silently discards `writeToStream()`'s return value — if the internal buffer fails to grow
   far enough into a large response (heap fragmentation, not necessarily low free-heap), it
   returns whatever partial data it managed to capture with **zero error surfaced**. A related
   library gotcha found in the same investigation: `HTTPClient::addHeader()` silently *rejects*
   `"Connection"`/`"User-Agent"`/`"Host"` (reserved, "handled by code" internally) — must use
   the real setters (`setUserAgent()`, `setReuse(false)` for `Connection: close`). Fixed with
   `CddvChunkStream` (a custom write-only `Stream` subclass passed to `writeToStream()`) that
   forwards each already-dechunked piece to a callback instead of ever buffering a whole
   response — confirmed present in current code (`caldav_relay.cpp` line ~863). A
   retry-vs-duplicate-emit guard was added alongside it: since blocks now stream to the Palm
   as they arrive rather than after full-response verification, `cddvHttpRequest`'s retry loop
   refuses to retry once even one byte has already been forwarded downstream (converting a
   would-be duplicate-emitting retry into a hard failure instead).
2. **DNS failure indistinguishable from a generic connection refusal.**
   `HTTPClient::errorToString(-1)` collapses DNS failure, network-path blocks, and local
   resource exhaustion into one generic string — indistinguishable from a real HTTP 401
   credential problem. Fixed by resolving the host via `WiFi.hostByName()` first, turning "can't
   even connect" into "DNS lookup failed for `<host>`" vs. an actual TCP/TLS-level refusal from
   a host that did resolve.
3. **`WiFiClientSecure` `Stream::_timeout` shadowing — same bug class as BitWarden Relay
   (§11.2/§11.5).** `WiFiClientSecure` declares its own non-virtual `int _timeout`, shadowing
   the inherited `Stream::_timeout` that `readStringUntil()`/`readBytes()` actually consult;
   calling `.setTimeout()` on a `WiFiClientSecure`-typed variable never reaches it.
   `HTTPClient`'s own internal `_client->setTimeout(...)` call has the identical problem for the
   same reason (dispatches through the shadowed override). Fixed the same way as BitWarden's
   fix: an explicit `((Stream &) tls).setTimeout(60000)` upcast, plus a separate
   `setHandshakeTimeout(60)` call (a genuinely different field, not fixed by the timeout upcast
   at all) — both present at two call sites in current code (`cddvHttpRequestOnce` and the
   Google-fetch path).
4. **A wrong TLS pre-flight heuristic, found and removed.** An earlier version of the connect
   path rejected a connection outright below a "40KB largest contiguous free heap block"
   threshold, on the theory that `mbedtls_ssl_setup()` needs one large contiguous allocation.
   Real testing disproved this: a sync failed the check at `largest_free_block=36852` — already
   comfortably above either of the two *separate* 16KB-each mbedTLS SSL record buffers (in/out,
   not one combined block) — with no evidence the real `connect()` would have failed at that
   level. The heuristic was actively rejecting connections that would have succeeded. Removed
   entirely; the real `WiFiClientSecure::connect()` call is now the sole arbiter, with the heap
   reading kept only as a diagnostic log line, not a gate. A useful example of a defensive check
   that was itself the bug.
5. **Hidden 5-second `HTTPClient` default connect timeout.** Not configured explicitly anywhere
   in the original code — `HTTPClient`'s own undocumented default silently capped every connect
   attempt at 5s, too short for a slow mobile-network CalDAV server. Widened (connect/read both
   moved from an initial 20s to 30s after further real-world testing).
6. **ICS Feed heap-exhaustion `abort()`.** The whole-body accumulation path (§13.3) originally
   grew its buffer in one large reservation matched to a `Content-Length` size hint; a feed
   whose declared size didn't leave enough contiguous heap caused a hard ESP-IDF `abort()`
   rather than a graceful failure. Fixed with coarse-step growth (`cddvOnIcsChunk` rewritten to
   grow in bounded steps rather than one large up-front reservation) plus heap diagnostics
   logging, and the `Content-Length` size-hint plumbing (`cddvOnIcsSizeHint`) kept only as a
   reservation hint, not a hard pre-allocation. (The whole-body path itself was later replaced
   by the streaming two-phase parse described in §13.3.)
7. **Dead recurring series sent needlessly.** A repeating VEVENT whose `RRULE` has an `UNTIL`
   entirely before the requested sync window is a series that's simply over — previously still
   fully expanded and checked occurrence-by-occurrence against the window before being
   discarded. Optimized to drop such a master immediately after parsing its `RRULE`, before any
   occurrence expansion — an explicit `COUNT` (mutually exclusive with `UNTIL` per RFC 5545)
   can't be resolved this cheaply and is left to the existing expand-then-filter path; a
   genuinely unbounded series (neither `UNTIL` nor `COUNT`) is always kept regardless.
8. **Full-body verbose logging, WiFi robustness pass (2026-09).** A later pass added: verbose
   per-stage `usbDebug` logging throughout the parse/fetch pipeline; `yield()` calls inside the
   parsing/expansion loops (a large multistatus response's parse loop could otherwise starve
   the WiFi/lwIP task long enough to trigger a watchdog); a third previously-unguarded
   `VTIMEZONE`-parsing throw site wrapped in try/catch (joining the existing per-VEVENT outer
   try/catch); surfacing uICAL's own real exception messages instead of a generic "parse error"
   string; masking account-identifying fields (URLs, IDs) in debug logs via the existing
   `maskUrlForLog`/`maskSensitive` helpers; disabling WiFi modem sleep globally (kept — an
   earlier same-pass attempt to also force max TX power was tried and **reverted**, per the
   task history, as not helping and only tried once further afield); capturing the real
   ESP-IDF WiFi disconnect reason code instead of a generic drop message; and moving
   `CDDV_SYNC_REQUEST` handling onto a dedicated FreeRTOS task with a large stack
   (`cddvHandleSyncRequestTask`, currently **65536 bytes** — note this is double an
   earlier-recorded 32KB target; trust the current code's `xTaskCreate` call, not the older
   number, since the exact reason for the increase wasn't independently confirmed by this
   research pass).
9. **Time-range window experiments.** The future-window bound was briefly dropped entirely for
   iCloud (informed by how EDS/gnome-calendar query Apple's servers) to investigate a
   "future events still missing" report, then reverted after real testing showed it made things
   *worse*, not better — the actual root cause turned out to be #1 above, unrelated to the query
   window shape at all. Per-occurrence date logging was added during this investigation to
   narrow down the gap and is still present as a diagnostic aid.

### 13.6 Verification status

**Originally static-only** (see §9's build notes and `feedback-wirsa-parallel-git-workflow`):
every change described above was checked via brace/paren and `#if`/`#endif` balance scripts,
and the user builds and flashes on their own machine. The current source, including the
serial-link encryption, has since compiled and linked clean in the §9 scratch build
(2026-10). Real-hardware confirmation status, as best determined from
memory files and code comments (not independently re-tested by this research pass):
- CalDAV-generic and ICS Feed: real-device testing referenced across multiple bug entries
  above (the truncation/DNS/timeout fixes were each traced from real symptom reports).
- iCloud: real-device sync testing referenced (§13.5 #4's heap-block theory was disproven by
  "real iCloud sync testing").
- Google Calendar: **user-confirmed working (2026-09-28)** — the Calendar API fetch path
  (`cddvFetchGoogleCalendar`/`cddvGoogleRefreshAccessToken`, §13.3) is real-hardware-tested
  and functions correctly. OAuth end-to-end (the PIN-pairing handshake + `google_auth.py`
  desktop script, §13.1/13.2) was not separately called out in that confirmation — treat the
  fetch path itself as confirmed, and the OAuth handshake as very likely exercised to get
  there (a fetch can't succeed without a valid access token from it) but not explicitly named.
- One stale-memory discrepancy found and corrected here rather than repeated: an older project
  memory recorded Google's Client Secret as expected-optional for a "Desktop app" OAuth client
  type; the current code requires it unconditionally (§13.3).

---

## 14. Flash size (2026-10)

Measured with the §9 scratch build of the current source (absolute numbers may differ a little
from the user's own build): **Flash 1,330,697 / 1,966,080 bytes before the trim below,
1,299,445 after** (66.1%, ~650 KB free in the `min_spiffs.csv` app slot); RAM 102,476 /
327,680, unchanged. Roughly where it goes (map-file totals): the ESP32 platform (WiFi, lwIP,
newlib, FreeRTOS, drivers) ~800 KB; TLS (mbedTLS + HTTPClient + WiFiClientSecure) ~165 KB; this
project's modules ~206 KB (`bitwarden_relay` 55 KB, `caldav_relay` 53 KB, SLIP/NAT 34 KB,
menu 12 KB, HotSync 11 KB, SASD 8 KB…); uICAL ~69 KB; mDNS ~24 KB; WebServer ~21 KB (only
the Google OAuth hand-off listener); MultiFTPServer ~20 KB; OLED (GFX + SSD1306 + Wire)
~13 KB.

**2026-10-23 (virtual card, §16):** the scratch build (same stand-in ini, `partitions_vdisk.csv`) is
1,417,249 / 1,966,080 bytes (72.1%), about 530 KB free in the app slot. The virtual card (vdisk, vdfs,
ftp_rootfs, the HotSync/SASD changes and the FatFs glue) cost about 45 KB over the 1,372,800 bytes
the user's own build reported just before it. The partition table is now `partitions_vdisk.csv` (§16.1); the
app slots are unchanged (0x1E0000 each).

**Done (−31,252 bytes):**
- `cddvParseRfc3339` (`caldav_relay.cpp`) uses `siscanf`, newlib's integer-only scanner,
  instead of `sscanf`. Those were the firmware's only `sscanf` calls and they only read `%d`,
  but they linked the floating-point scanner and `strtod` (−16.2 KB).
- `src/modules/tls_strerror.c` defines `mbedtls_strerror`, so the linker never pulls
  libmbedcrypto's `error.c` and its ~15 KB string table. The only callers are the Arduino
  core's `WiFiClientSecure` (`ssl_client.cpp`'s `_handle_error`, `lastError()`), whose
  `log_e` lines now read "mbedtls -0x7780" instead of a sentence (−15.2 KB). No ini change
  needed: the object file defines the symbol before the archive is searched.

**Measured but not done (user's choice):** remove mDNS / `wirsa.local` (−22.9 KB);
`-DWIRSA_DEBUG_LOGGING=0` (CalDAV logging only, −8.9 KB; Bitwarden/HotSync/relay_proto/SASD
have ~130 more `if (usbDebug)` log lines with no such switch).

**Estimated, bigger, riskier:** replace `WebServer` with a minimal raw listener (~18 KB);
replace uICAL (~70–85 KB with its share of the C++ runtime, but it parses all CalDAV/ICS data
and expands RRULEs); rebuild the ESP-IDF libraries with a custom sdkconfig (IPv6, unused TLS
features, newlib nano printf, core dump, WPA-Enterprise: maybe 80–150 KB, a big build-system
change). **Not possible:** `-flto` (fails to link against the precompiled Arduino core); `-Os`
is already the default; `-fexceptions` must stay (uICAL throws).


---

## 15. Palm control link and screenless build (2026-10, `improved` only)

Full design and as-built notes: `apps/WiRSaControl.md`. In short:

- **`WIRSA_FEATURE_PALMCTL`** (default 1): the Palm app WiRSaCtl (`apps/WiRSaControl/`) drives
  WiRSa over the Palm line with framed commands (0x16 + a RelayProto frame): status, WiFi scan/set,
  settings, FTP start/stop, enter/exit a mode, reboot, factory reset. `palm_ctl.cpp`.
- **Burst guard:** every single-letter menu loop reads keys through `menuKey()`; a key from the
  Palm line counts only if it arrives alone (100 ms quiet either side, CR/LF allowed after it), so
  stray bytes (e.g. a SASD wake ping) can't start a mode. Text entry is not guarded.
- **Factory Reset** now always asks first (ENTER/BACK, or `Y` when typed).
- **Exit hooks:** CalDAV and Bitwarden (between sessions) and HotSync (while waiting) answer
  PING/STATUS and leave on EXIT. They also leave on their own (Exit Timers, §15.2); SLIP Gateway
  leaves after a quiet spell (§15.3); SASD ends only with a reset.
- **`WIRSA_SCREENLESS`** (default 0): no OLED, no buttons - `NullDisplay` stands in for the
  display, `readSwitches()` reports nothing pressed, button-only prompts return instead of waiting.
- `relay_proto` gained `setControlLink()`/`readFrameRest()`; the relay modes never switch the
  control link on, so their wire behaviour is unchanged.

### 15.1 BOOT button = HotSync (2026-10-16)

For a fresh Palm (hard reset, so no WiRSaCtl) on a screenless WiRSa: press WiRSa's **BOOT** button
(GPIO0, `SWITCH_PIN`), then tap HotSync on the Palm - HotSync Install puts WiRSaCtl and the rest
back from the SD card. `boot_button.cpp`, `WIRSA_FEATURE_BOOT_HOTSYNC` (default on, both builds).
- `bootButtonPoll()` runs once per `loop()` pass: one pin read, no blocking. A short press
  (released after 50 ms - 3 s) in any menu screen starts HotSync through `startHotsyncFromMenu()`
  - the same function the H key now uses (SD check, then HotSync mode). Another press while
  HotSync is still waiting leaves it. Every other mode ignores the button.
- Status LED (`LED_PIN`, GPIO2 by default, active-low, otherwise the WiFi indicator): 3 slow
  blinks (1 s on, 1 s off) = HotSync started or left, one 2 s blink = not started (no disk for the Disk setting, §16);
  then back to the WiFi state. On a LilyGO T7-S3 GPIO2 is the battery-sense pin and the on-board
  LED is GPIO17 (the Palm line's TX), so an external LED needs `LED_PIN` moved to a free pin
  (3.3 V → resistor → LED → GPIO; e.g. 4-11, 13, 14, 18).
- Press it only after WiRSa has started: held during reset, GPIO0 selects download mode.
- The Exit Timers (§15.2) still bring WiRSa back from HotSync.

### 15.2 Exit Timers (2026-10-16)

HotSync, CalDAV Relay and Bitwarden Relay return to the main menu by themselves: **unused** - no
session within N s of entering (default 45), **after** - N s after a session ended (default 15; BYE,
the relays' 15 s silence revert, or a finished HotSync). Seconds, 0 = never. Config > **Exit Timers**
(key E) or WiRSaCtl Setup > Exit Timers.... Shared code: `mode_timer.h/.cpp`; settings in
`settings.cpp` at EEPROM 440 (marker) / 441 / 442 - the marker is needed because growing the EEPROM
blob fills the new bytes with 0, which would read as "never". HotSync's old fixed 45 s/15 s are
these now; `hsRunSession()` returns whether a real session ran (a stray non-WAKE packet no longer
counts).

### 15.3 SLIP timeout (2026-10-16)

SLIP Gateway returns to the main menu (gateway stopped, same as BACK) after 1, 2, 3 or 5 minutes
(default 5) with **no traffic either way**: bytes from the Palm, or frames WiRSa sends to it
(WiFi-side replies - so a Palm waiting quietly on a slow server isn't cut off). Counted from mode
entry, so a Palm that never connects also times out. `slipLastActivityMs` (slip.h) is set in
`slipSendFrame()` and on bytes received in `slipActiveLoop()`. Setting: Config > Exit Timers (third
prompt) or WiRSaCtl Setup > Exit Timers... > "SLIP Timeout:" popup; EEPROM 443 (anything but
1/2/3/5 reads as 5); a third optional byte on CFG_GET/CFG_SET.

### 15.4 Menu at 9600, Palm-chosen speed (2026-10-18)

For Palms of different speeds on one WiRSa (e.g. 115200 and 57600). `WIRSA_FEATURE_DUAL_BAUD`
(default 1; 0 = as before). Full notes: `apps/WiRSaControl.md` §2.10.
- The main menu runs at 9600 after every boot. WiRSaCtl's new SPEED command (0x4C) moves it to the
  Palm's "Pref Baud" (WiRSaCtl Options → Preferences) for this power-on; nothing is saved. SLIP
  Gateway starts at the menu's current rate.
- The configured baud is only the maximum for the modes' negotiation (Config "Baud Rate (Max)").
- While the menu runs faster than 9600, a second, receive-only UART (UART1) reads the same Palm RX
  pin at 9600; a valid frame or key there brings the menu back to 9600, so another Palm can get in
  and raise it to its own speed. Only SPEED moves the menu up.
- The header's top-right rate shows the menu's current rate.

### 15.5 Leveled log, session log and the System page (2026-10)

`log.h`/`log.cpp`: levels off < warn < info < verbose (`LOGW`/`LOGI`/`LOGV`, `gLogLevel`, default info,
stored as bit 4 | level in the old USB-debug EEPROM byte 101 so an older settings blob still reads
right). A line goes to USB serial if its level <= `gLogLevel`, and into a session log in RTC no-init
memory if its level <= max(`gLogLevel`, warn); nothing goes to flash or the SD card. It survives a
software reset, crash, watchdog and normally a brownout, not a power loss (discarded by a check
value). The Palm reads it with LOG (0x4D) (Help → Show Logs; Clear), sets the level in CFG_SET's
second byte (0 off, 1 info, 2 warn, 3 verbose on the wire; 1 stays "on" for an older app).
SYSINFO (0x4E, Help → System) returns `buildSysText()` as paged text: chip, CPU/temperature, flash,
firmware size (`getSketchSize` of `getSketchSize + getFreeSketchSpace`, i.e. used of used+free OTA
slot), heap and PSRAM, uptime, reset reason, IDF, the virtual card, the partition table.

Delivery logging (2026-10): the BitWarden and CalDAV relays used to log "synced N" when the
firmware had handed the data to its queue, before the Palm had it. `bwReportDelivery()` /
the CalDAV equivalent now log after the whole sync has gone to the Palm: `synced N ... to the Palm`
(info) or `sync incomplete - X of N ... reached the Palm` (warn). BitWarden items whose notes
exceed 8192 raw (still encrypted) bytes, or are malformed, are sent with empty notes and counted in
`gBwSkippedItems` (the Palm keeps 512 bytes of notes anyway). The SD mount (`sd_mount.h`) retries at
4 MHz if 20 MHz fails and logs only when the state changes.


## 16. Virtual card (2026-10-23, `improved` only)

A 12 MB flash partition used as a card: SASD serves it to the Palm, HotSync installs from it, the FTP
server shows it. Why: the T7-S3 has 16 MB flash and the firmware used 4 MB. One disk at a time over SASD
(§16.7). Palm-side docs: `apps/WiRSaControl.md` (Setup "Disk", the SD row).

### 16.1 Partition and layout

`Firmware/partitions_vdisk.csv` = `min_spiffs.csv` (nvs 0x9000, otadata 0xe000, app0 0x10000 and app1
0x1F0000 at 0x1E0000 each, spiffs 0x3D0000 128K, coredump 0x3F0000 64K) + `vdisk, data, 0x40,
0x400000, 0xC00000`. The user's `platformio.ini` (machine-specific, not in the repo) sets
`board_build.partitions = partitions_vdisk.csv`. A normal upload rewrites the table; NVS and the app
slots keep their offsets.

`vdisk.cpp`: a 512-byte-sector block device over `esp_partition_*`: MBR (one partition, type 0x06,
starting at sector 8 so it is 4 KB aligned), FAT16 boot sector, 2 FATs, 512 root entries, 4 sectors per
cluster: 24,576 sectors, 24,568 in the volume, 6,120 clusters, fatSecs 24, data area from sector 96.
`vdiskFormat()` writes only the metadata (the data area is not erased); `vdiskIsFormatted()` checks
the MBR type and "FAT16"; `vdiskEnsureLabel()` writes the volume name `ESP32` to the boot sector and
the root directory's label entry without disturbing files; `vdiskPrepare()` = open, format if blank,
else ensure the label; `vdiskUsage()` counts free FAT entries for used/total.

### 16.2 Cache and flash behaviour

Four 4 KB blocks in PSRAM (`heap_caps_malloc` SPIRAM, falling back to heap; allocated by
`vdiskOpen()`, freed by `vdiskClose()`; nothing is kept between sessions). Write-back: a block is
written when evicted or on `vdiskFlush()`; an unchanged block is read back and not erased. A whole-
block write skips the read. Flash is NOR: `esp_partition_erase_range` then write; a host test models
1->0-only writes. Flushing is the durability rule: SASD flushes before it acknowledges the last
sector of a WRITE (SASD ends only with a reset, so nothing may stay in the cache), FatFs flushes on
`CTRL_SYNC` (file close, `f_sync`).

### 16.3 SASD

`sasd.cpp` reads and writes through `sasdRd`/`sasdWr`/`sasdSecCount` (SD card or `vdisk`).
`enterSasdMode()`: `sdMount()` unless the Disk setting is Virtual; if no card (or Virtual) and the
`vdisk` partition exists and `vdiskPrepare()` works, `sasdVirtual = true`, and capacity comes from
`vdiskUsage()`. If nothing can be served (SD card setting and no card, or neither) it does not start:
"PLEASE INSERT SD CARD", back to the menu. The OLED status line reads `VD:` for the virtual card.
The main-menu SASD entry (`display_menu.cpp`) and the ENTER check (`palm_ctl.cpp`) follow the same
rule. The driver tells the Palm VFS "no MBR" in its metrics but mounts MBR cards fine; the virtual card
has an MBR and mounts (user-confirmed).

### 16.4 FatFs mount (`vdfs.h`/`.cpp`) and HotSync

`vdfsMount()`: `ff_diskio_get_drive`, `vdiskPrepare()`, `ff_diskio_register` with a driver whose
`read`/`write` call vdisk and whose `ioctl` answers CTRL_SYNC (flush), GET_SECTOR_COUNT,
GET_SECTOR_SIZE (512; the core's FatFs has `FF_MIN_SS` 512 and `FF_MAX_SS` 4096), GET_BLOCK_SIZE (8);
`esp_vfs_fat_register("/ESP32")`; `f_mount`. `vdfs()` is an `fs::FS` whose impl is a `VFSImpl` with
mount point "/ESP32", so code written for `File`/`fs::FS` works unchanged. The mount is
**counted**: HotSync and the FTP server each take one, and the volume (and the cache) is released
when the last is given back. Core facts used: FatFs in this core has LFN on (stack, 255 chars),
`FF_VOLUMES` 2 (SD takes drive 0, the virtual card 1), `FF_MULTI_PARTITION` with auto-detect (the MBR
is found), `FF_FS_TINY` off (4 KB per open file).

`hotsync.cpp`: `hsFs` points to `SD` or `vdfs()`; `hotsyncPrepareDisk()` (called from
`startHotsyncFromMenu()` in `display_menu.cpp`, which BOOT and the Palm's ENTER also use) picks the disk
by the Disk setting (SD card: `sdMount()`; Virtual: `vdfsMount()`; Auto: SD if it mounts, else virtual),
logs "HotSync: installing from the ...", and creates `/PALM`, `/PALM/PROGRAMS`, `INSTALL`, `SASD`,
`INSTALLED` with `hsEnsureFolder()` on whichever disk it mounted (SD too). `hsExit()` releases the
virtual mount. **HotSync is an installer, not a backup**: it reads `/PALM/PROGRAMS/SASD` and `INSTALL`
and moves done files to `INSTALLED`; Bkp1 backs up to `/PALM/PROGRAMS/BKP1/` and HotSync never
restores those.

### 16.5 Setting and status

- EEPROM 444 `SASD_DISK_ADDRESS` (`sasdDiskMode`: 0 auto, 1 SD card, 2 virtual; a freshly grown 0 is
  Auto, so no marker); the last optional byte of CFG_GET/CFG_SET (after `slipMin`).
- STATUS: `sd` 3 means the virtual card is the disk (Virtual chosen with the partition present, or Auto
  with no SD card and the partition present; Virtual without the partition reports 2), `sdText` is then
  the virtual card's used/total or "blank", and a final `sasdDisk` byte follows `sdText`. The Palm
  refuses HotSync/SASD only for `sd` 2. SYSINFO and STATUS leave the virtual card open if `vdfsMounted()`
  (otherwise the System page would close the cache from under HotSync or FTP).

### 16.6 FTP root (`ftp_rootfs.h`/`.cpp`)

The MultiFTPServer library's `STORAGE_MANAGER` macro (STORAGE_SD branch in `FtpServer.h`) is
`gFtpRootFs` when `FtpServerKey.h` defines `FTP_STORAGE_MANAGER_HEADER` (it does; both translation
units include that one file, see §10.2). `ftp_rootfs.h` is in the library folder because the library's
own translation unit does not see the project's `include/`. `RootImpl` derives from `VFSImpl` with an
empty mount point and maps `/SDCard/...` and `/ESP32/...` (case-insensitive names) straight to the real
VFS mounts; `/` is a synthetic directory listing the volumes that are present (`SD.cardType()` and
`vdfsMounted()`); the volume folders themselves cannot be created, removed or renamed, and a rename
between volumes fails. The SD card is now mounted with the name "/SDCard" (`sd_mount.h`), the virtual
card at "/ESP32". `ftpServerStart()` mounts both (the virtual card whenever the partition exists),
starts if either is there, and `ftpServerStop()` releases the virtual mount. The Disk setting does not
apply. Verified: host test of the path mapping; user-confirmed on hardware.

### 16.7 What was ruled out, and why

- **Both disks at once over SASD.** The driver (`serialSD/Src/Slot.c`) registers one slot and keeps the
  serial port open after connecting. In POSE64 (Palm III ROM, the patched ExpansionMgr, VFS libs, a test
  app counting slots) two copies of the driver with different creator and database names did register
  two slots (1, 2), but they cannot both hold the port, each tracks its own baud, and the source does
  not build with this SDK's gcc 2.95 (header conflict with `stdbool.h`), so the driver cannot be
  changed here. One slot with two partitions was tried by the user: does not mount as two volumes.
  Mgr1 itself lists every mounted volume (up to 10) with its label ("Card 0" when unlabelled).
- A second SASD unit/protocol extension, a composite disk, a file-level SD to virtual copy: not built.
  The FatFs layer (§16.4) is what a later copy tool would use.

### 16.8 Tests

`Firmware/test_host/vdisk_test.cpp` (g++, no ESP32): format geometry and label, cache and eviction,
straddling writes, identical rewrite skips the erase, bounds, persistence, `vdiskPrepare`, `vdiskUsage`;
`VDISK_IMG=file` dumps the image, which `fsck.fat` accepts, and an independent FAT library (pyfatfs)
read/wrote it with long names (250 files in a folder; the root holds only about 127 long names).
Compile check of the FatFs glue and the FTP root with the §9 scratch build. Hardware: SASD, HotSync
(installing from the virtual card and the SD card) and FTP user-confirmed.


---

## 17. Mesh Relay mode (Meshtastic client)

Added 2026-10 for `apps/meshtastic_client.md` (the Palm side, with usage notes). Protocol and frame list:
`~/WiRSa/WiRSa/docs/Mesh-Palm.md`. Main-menu entry `Mesh[T]astic`, WiRSaCtl ENTER letter `T`, switch
`WIRSA_FEATURE_MESH`.

### 17.1 Architecture

- `mesh_relay.cpp`: the mode and the Palm frames (same framing and encryption as the BitWarden and CalDAV relays).
  A worker task owns the Bluetooth link (`mesh_ble.cpp`, NimBLE client) and posts results to the loop task (`gBusyOp`
  says a node operation is running: 1 scan, 2 pair, 3 connect, 4 settings write or action, 5 settings read).
- `mesh_core.cpp` keeps the node list, channels, settings blobs and messages (`seq` rises per stored message);
  `mesh_pb.cpp` is the protobuf code written by hand.
- **Push, not poll:** `pushNews()` sends NEWS (link byte and last seq) when a message, a delivery or the node link
  changes, repeated every 3 s until the Palm syncs. The Palm reads the link state from the NEWS frame itself.
- **Settings read (`CFG_GET`)** asks the node for its configuration afresh: `cfgRead()` first forgets the stored
  settings, then waits up to 40 s for the node's end marker (11 to 20 s is normal on a node that has just connected)
  and ends "settings missing" when it does not come. Every request therefore costs the worker that long. The
  Palm app only does it when the user opens Settings. Channel names and slots reach the Palm through SYNC's CHAN
  frames, which answer from WiRSa's store immediately.
- **Exit:** with `unusedExit` / `afterExit` seconds (Exit Timers) and a Palm session, a Palm that is quiet for the
  limit is probed with PING; if no frame arrives within another limit the mode goes back to the main menu
  (`Mesh: exit timer - back to the main menu`). A running node operation counts as activity.
- **Background mode** (set from the Palm's Pair Device form): with no Palm session WiRSa reaches the node every
  1, 5, 15, 30 or 60 minutes, takes what it holds and hangs up after about 12 s.

### 17.2 Notes from bring-up

- A Palm at 38400 showed an irregular bit time (the sixth edge of an 'S' arrives about half a bit late), and now and
  then a damaged frame (`checksum mismatch`), after which the Palm went silent and the exit timer ended the session.
  57600 and 19200 were clean.
- In the Palm OS emulator a sync may stall mid-stream (the serial pty); the real line does not.
