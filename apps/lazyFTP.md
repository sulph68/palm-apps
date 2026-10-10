# lazyFTP — FTP Client (reference & rebuild guide)

A standalone FTP client, UI look-and-feel inspired by `axxpac_ftp_src` (original code
throughout). Creator `LzTP`, DB name `LazyFTPDBCache` (type `DATA`, on-device large-listing
spillover only — no user-record database of its own; connection settings live in prefs).
`.def` = `application { "LazyFTP" LzTP backup }` with `multiple code { "sec001" "sec002"
"sec003" "sec004" }`. Reuses this suite's established idioms throughout (folder/up-arrow
SD-card icons, Sec35 field-level encryption, hand-rolled modal-dialog loops) but is its own
original design — not a derivative of any PIM app. See [[lazyftp-project]] for the full
round-by-round build history this doc distills.

## 1. What lazyFTP is

A directory-browsing FTP client: hand-drawn file/folder listing (`frmMain`) with an up-row,
folder icons, wrapping filenames, and file-size column; PASV-only transfers; **two-tap
select-then-act** on any row (first tap highlights it, a second tap on the already-selected
row performs the action - opens the file-action popup for a file, or navigates into a
directory) - the file-action popup itself offers Preview+Import-to-Memo for text,
Download+install for PRC/PDB, Delete, Rename; `Edit` menu (was `Folder`) for Cut / Paste /
Delete (all three act on the current two-tap selection, file or folder, from wherever it's
browsed - §5) / New Folder / Rename Folder / Refresh; a title-bar icon shows whenever
something is cut and waiting to be pasted (§5); `Upload` button opens a full-screen picker
(with a live find filter, §7) over every installed database and sends it as a file, showing
a real progress bar during the transfer (§7); `Network` menu for Connect / Disconnect Server
/ Disconnect Network (§11) - auto-offered on launch if not already connected, and starting
any new connection clears the cut buffer (§5); a 3-slot Connect dialog (§8) with
Sec35-encrypted saved passwords, an Anonymous-login checkbox, reveal-then-hide password
masking + a "Show" checkbox, a saved-password placeholder, and a client-side
connect-retry-with-countdown mechanism; a full command/reply connection log viewer (§9); a
Preferences dialog for the large-directory cache TTL and "Disconnect on exit."
**DONE and confirmed working on real hardware** (2026-08-22, connect-reliability work through
2026-08-26/27; the two-tap select/Cut-Paste/Rename-Folder/generic-Delete UI overhaul added and
live-verified 2026-08-27) - see [[lazyftp-project]] and [[lazyftp-connect-dialog-improvements]]
for the full round-by-round debugging trail this doc distills, including the two original root
causes (upload corruption in the WiRSa gateway, a real-device crash from a stack overflow in
the auto-reconnect call chain) and the later connect-reliability saga (a real "wrong password
sent when Sec35 is locked" bug, an under-timed control-connection read, a SLIP-link-drop-
between-retries issue, and two real screen-artifact bugs) - all fixed and user-confirmed
("ok. lazyftp is working now.").

## 2. Segmentation

```
main   : PilotMain, launch dispatch, StartApplication/StopApplication, EventLoop only -
         deliberately thin, reserved for a possible future cross-app custom launch code.
sec001 : app/UI - form event handlers, dialogs, menu handling (MainForm, ConnectForm,
         PrefsForm, FileActions, PromptForm, PreviewForm, UploadForm, BusyDialog).
sec002 : database - VFS-free ExgDBRead/ExgDBWrite, Sec35 password encrypt/decrypt wiring
         (LzTPSec35.c, over enccore.h - same shared crypto core Address35/Sec35 use).
sec003 : graphics - hand-drawn directory listing rows + scrollbar sync (Listing.c).
sec004 : communication - FTP transport + control/PASV/LIST protocol (FtpProtocol.c).
sec005 : reserved, unused - a future BearSSL/FTPS integration would live here, kept
         separate from sec004 given its likely size.
```

Same three multi-segment traps as the rest of this suite apply and were re-confirmed here:
build with `-palmos4` (plain `make`) or main→secNNN calls become wild near-jumps
([[palmos4-flag-multiseg-farcall]]); segment tags belong on forward declarations too
([[multiseg-forward-decl-gotcha]]); libgcc/32-bit-division and `static const` locals are
only safe from main ([[multiseg-libgcc-gotcha]], [[multiseg-local-static-const]]).

## 3. Transport & protocol (`FtpProtocol.c`/`.h`, sec004)

The **only** place that touches raw sockets (`FtpTransportSend/Receive/Open/Close`) — a
thin boundary deliberately kept separate from the FTP command/reply logic above it, so a
future FTPS/BearSSL integration only changes these functions' internals. **PASV-only**;
active/PORT mode was ruled out early as impractical on single-tasking PalmOS (the same
conclusion `axxpac_ftp_src` independently reached). All calls are **blocking** — callers
wrap them in `LzTPBusyBegin/End` or `LzTPProgressBegin/Bar/End` (§7) and reset
`EvtResetAutoOffTimer` themselves during any long-running one, rather than the transport
layer trying to be non-blocking itself (unlike EraNTP's own UDP client, a TCP control+PASV
exchange here is inherently a short, bounded sequence per user-initiated action — polling
state-machine complexity wasn't worth it for this app).

**Recursive folder delete** (`FtpDeleteCurrentFolderRecursive`): a self-contained walk
(`CollectListLevel`+`DeleteDirRecursive`) that never touches the UI listing cache — heap-
allocated per-level entry arrays, capped at 128 entries/level and depth 12, `outTruncated`
reported honestly (attempts deletion of whatever it *did* enumerate rather than silently
dropping entries past the cap). Always leaves the connection positioned in the parent
directory afterward, success or failure. Still only ever knows how to delete "wherever the
control connection's CWD currently is" — unchanged since it predates the two-tap select
model; `FileActions.c`'s `DoDeleteFolder` (§5) `CWD`s into a NAMED folder first to reuse
this exact machinery for an arbitrary selected folder rather than only the current
directory, and the old root-directory refusal doesn't need porting there (a selectable row
is always a child of wherever you're browsing, never the true FTP root itself).

**`FtpRename`** (`RNFR`+`RNTO`, pre-existing - used by the file-action popup's own Rename)
is a pure two-string pass-through with **zero file-vs-directory distinction** - confirmed by
reading WiRSa's own vendored FTP server source
(`~/WiRSa/WiRSa/Firmware/lib/MultiFTPServer/FtpServer.cpp`): its RNFR/RNTO handling is one
generic filesystem `rename()` call, and on a FAT-filesystem SD card, renaming a directory
(even into a different parent) is metadata-only, not a recursive copy - the *same* call
this app already used for a same-directory file rename is exactly what Cut/Paste (§5) needed
for a real cross-directory MOVE of a file *or* a folder-with-all-its-contents, no new
protocol work required. The one genuinely new piece is `FtpJoinPath(dir, name, out, outLen)`
- the first path-building helper this app ever needed, since ordinary `CWD` navigation only
ever sends a bare relative name and re-derives the new absolute path via a follow-up `PWD`
(`FtpChangeDir`), never building a path string itself. Bounded with `StrNCat` (not
`StrPrintF`, which is unbounded in this SDK - an early draft built the string with
`StrPrintF` then truncated the *string* after the unbounded write had already happened, a
real overflow-then-truncate bug caught before it ever built - see
[[strncat-strlcat-semantics-gotcha]]) and handles `dir == "/"` (root, no trailing slash to
add) as its one special case, matching `FtpCurPath()`'s own root representation.

**Upload/Download of installed databases** (`FtpStoreDatabase`/`FtpRetrieveDatabase`)
bridge directly to `ExgDBWrite`/`ExgDBRead` rather than reusing the app's own higher-level
`FtpStore`/`FtpRetrieve` chunk-callback shape — `FtpSendProc` is a **pull** callback
(`FtpStore` asks it for the next chunk) but ROM's `ExgDBWrite` is a **push** callback (it
hands you chunks on its own schedule); opposite calling directions with no real concurrency
in a single-threaded app to bridge them, so the database path runs its own PASV/STOR-or-RETR
sequence around a direct `ExgDBRead`/`ExgDBWrite` call instead of trying to adapt the generic
transfer functions.

Two real protocol-layer bugs found and fixed live against a real FTP server:

- **`dmErrAlreadyExists` (537) on reinstall-over-existing** — the `ExgDBDeleteProcPtr`
  callback (`RetrieveDbDeleteProc`) returned `true` after the user confirmed "Replace," but
  despite its name, ROM's `ExgDBRead` does **not** delete the pre-existing database on the
  caller's behalf — it expects the callback to have actually called `DmDeleteDatabase`
  itself. Fixed by adding the real `DmDeleteDatabase(cardNo, dbID)` call inside the callback.
  Verified with a real upload-then-reinstall-same-name round trip.
- **Cancelling a Download still reported "installed"** — returning `false` from
  `RetrieveDbDeleteProc` on decline didn't reliably propagate into `ExgDBRead`'s own return
  code. Fixed with an explicit `declined`/`ok` flag set directly inside the callback and
  checked by `FtpRetrieveDatabase` itself, rather than trusting `err` alone.
- **`RetrieveDbReadProc` (the `ExgDBReadProcPtr` callback) trusted a single
  `FtpDataReceive` call to fill the whole amount `ExgDBRead` asked for** — a real bug (one
  `NetLibReceive` can legitimately return fewer bytes than requested; normal TCP
  fragmentation, harmless for `FtpRetrieve`'s own raw-byte-stream callback but not
  guaranteed safe for a structured PDB/PRC parse). Fixed by looping until the full amount is
  accumulated or a genuine EOF/error ends the transfer — the same "give me exactly what I
  asked for, short only at real EOF" contract `ExgDBWrite`'s push side already gets for
  free. Investigated as the suspected cause of a real "Download says install failed" user
  report — turned out **not** to be that particular bug (see next item) but kept as a
  genuine correctness fix regardless.
- **"Install failed" root cause, once actually found**: not a code bug at all — the specific
  test file was itself malformed. Diagnosed by instrumenting `RetrieveDbReadProc` with a
  byte counter and an **order-sensitive rolling hash** (`hash = hash*31 + byte` — a plain
  sum/XOR checksum can't catch byte reordering) surfaced in the error alert, then comparing
  against a reference copy of the same file fetched directly via Python `ftplib` from the
  same server. Byte count, sum, and order-sensitive hash all matched exactly — the transfer
  was always byte-perfect. Confirmed independent of this app entirely by feeding the
  identical bytes to POSE64's own stock installer, which failed with the same ROM import
  error. A different, valid file (`CityTimeDB.pdb`) installed correctly with the same code,
  both as a fresh install and through the overwrite-confirm path — proving the feature
  itself works. See [[lazyftp-project]] for the full instrumentation trail.
- **A second, real "install failed" report on a different file (`Address35DB.pdb`) turned
  up an actual UPLOAD-side bug — the true mirror of the fix above**. `FtpTransportSend`
  called `NetLibSend` exactly once and returned whatever it got back, with every caller
  (`StoreDbWriteProc` included) only checking the result against `-1` — never against the
  requested length. `NetLibSend` can legitimately do a **partial** send (same root cause as
  `NetLibReceive`'s partial reads), silently dropping the unsent remainder with **no error
  code at all** — a corrupted upload that reports success. Fixed by looping
  `FtpTransportSend` internally until the full length is sent or a genuine error/zero-progress
  occurs; also hardened `StoreDbWriteProc`'s `(UInt16) *sizeP` cast into a proper
  `ftpChunkLen`-bounded loop rather than a single truncating cast. **Caught and corrected a
  real analysis mistake along the way**: a first read of `Address35DB.pdb`'s header bytes
  looked like corruption (a run of repeating `0xD2` — a classic debug-heap "poison" pattern)
  but turned out to be ordinary, legitimate `DatabaseHdrType` fields — confirmed by diffing a
  fresh re-upload of `Address35.prc` against the pre-existing original byte-for-byte (zero
  differences) and installing that identical byte pattern cleanly via POSE64's stock
  installer. Verified the real fix by uploading both a large multi-segment app and a small
  database to fresh filenames, fetching them back via `ftplib`, and independently installing
  both via the stock installer. See [[lazyftp-project]] for the full trail, including the
  "eyeballing hex is not evidence, diff it" lesson.
- **Real-hardware follow-up: the loop-until-fully-sent fix above wasn't enough on an actual
  slow SLIP link** — POSE64 never reproduced the truncation (fast/local networking masks
  the timing conditions). Three further changes, all user-directed:
  1. **Bound every individual `NetLibSend` call to `ftpSendChunkLen` = 536 bytes**, not just
     the outer loop's total. 536 is the classic minimum-MTU TCP segment size (576-byte
     minimum IPv4 MTU minus 40 bytes of IP+TCP headers) — the standard, conservative
     pre-path-MTU-discovery fallback, far safer over a slow real link than handing
     `NetLibSend` a chunk up to 8192 bytes in one call (much likelier to hit
     `ftpCtrlTimeoutTicks` mid-flight than many small sends are).
  2. **Real upload verification**: a compact table-free bitwise CRC-16/CCITT-FALSE
     (`Crc16Update`) accumulated over every byte actually sent in `StoreDbWriteProc`, then
     `FtpStoreDatabase` immediately re-downloads the same remote file (reusing the existing
     generic `FtpRetrieve` with a new `VerifyCrcRecvProc` callback that just checksums
     instead of keeping the bytes) and compares. A mismatch is now a real "Upload
     verification failed — please retry" error instead of a false "success" — a deliberate
     correctness-over-speed tradeoff (upload time roughly doubles: send + a full re-read).
  3. **Auto-reconnect on a dropped connection**: PalmOS/the network driver can silently
     idle-disconnect the link while the app is still running, leaving `FtpIsConnected()`
     reporting a stale "connected" state. `FtpCmd` — the one function nearly every FTP
     operation's wire exchange funnels through — now attempts exactly ONE silent
     reconnect + re-navigate-to-the-same-CWD + retry-the-original-command whenever a command
     fails at the TRANSPORT level (send failed, or no reply at all — a real dead connection,
     distinct from an ordinary FTP rejection code like a bad password, which `FtpCmd` still
     returns `true` for). Server/port/user/pass are saved in `gFtp` only once a connect
     FULLY succeeds (a non-empty `gFtp.user` is the "safe to auto-reconnect" signal);
     `FtpDisconnect()` clears them so a deliberate disconnect is never fought. The recursion
     guard (`gFtpReconnecting`) is deliberately kept OUTSIDE the `FtpState` struct, since
     `FtpConnect` does its own `MemSet(&gFtp,...)` mid-reconnect that would otherwise wipe a
     same-struct guard flag. **Not exercisable in POSE64** (no way to simulate an involuntary
     OS-level idle disconnect) — see the real-device crash this exposed, and its fix, below.
- **The actual root cause, found via a real gateway log + targeted diagnostics** (added
  `StoreDbCtx.offered`/`callCount` counters plus a `DmDatabaseSize` call, surfaced in the
  verification-failure message as `real=... offered=... calls=...`): a real-device error of
  `real=14759 offered=14539 calls=39` proved `ExgDBWrite` was NOT giving up early — it
  handed us essentially the whole database (only ~220 bytes short, normal accounting slack)
  — ruling out the memory-pressure theory above entirely. Hand-summing the user's own SLIP
  gateway log's `client->server` byte counts for the STOR connection showed only **10466
  bytes actually reached the gateway** before the Palm's own clean FIN closed it — a
  **4073-byte gap** between what the app believed it successfully sent (every `NetLibSend`
  call returned success) and what physically made it onto the wire. **Root cause**:
  `NetLibSend` only copies data into the OS/driver's own outgoing queue and returns success
  as soon as it's *accepted*, not once it's actually *transmitted* — over a slow real SLIP
  link, the app can hand data to `NetLibSend` far faster than the serial link can drain it.
  `FtpTransportClose` (`NetLibSocketClose`), called right after the send loop finished, was
  closing the connection while several KB could still be queued — discarding it. This is
  the literal mirror of the download-side short-read bug fixed earlier (same "OS/driver
  partial-I/O reality the app wasn't accounting for" root cause, just on the close-timing
  side instead of the read-count side). **Fixed** by enabling the standard SO_LINGER-
  equivalent socket option (`netSocketOptSockLinger` via `NetLibSocketOptionSet`,
  `FtpTransportEnableLinger`) on the upload data socket right after opening it — this makes
  a subsequent close BLOCK until the queue has actually drained (or a generous 30s timeout
  expires) instead of discarding whatever's still unsent. The earlier 536-byte chunking and
  CRC verification are both still worth keeping (safer chunking in general; CRC is what
  caught this whole bug chain and remains a good ongoing safety net) but neither actually
  addressed the root cause. **SO_LINGER did NOT fix it either** — a second real-device
  gateway log with IDENTICAL `offered`/`calls` numbers (confirming `ExgDBWrite`'s behavior
  is deterministic) showed the connection's FIN sent within ~35ms of the last data chunk —
  if the 30-second linger had actually been honored, the close would have blocked for
  several more seconds trying to drain the remaining ~6.7KB. It didn't wait at all: this
  specific Net.lib/SLIP driver combination silently doesn't support the option. **The actual
  fix**: `FtpTransportDrainWait` — an explicit, paced wall-clock delay (with
  `EvtResetAutoOffTimer()` between ~1-second increments) inserted between the send loop
  finishing and the close, sized to the bytes actually sent at a deliberately conservative
  assumed throughput (`ftpDrainBytesPerSec` = 1200, picked below the ~1495 B/s the second
  gateway log actually measured). This doesn't depend on any driver cooperation at all — it
  just gives the physical serial link literal wall-clock time to catch up before the
  connection is torn down. `FtpTransportEnableLinger`/SO_LINGER was left in (harmless,
  might help on some other Net.lib implementation) but is documented as non-functional on
  this hardware, not the real fix. POSE64-regression-tested; real-device confirmation still
  the deciding test, since POSE64's fast/local networking can't reproduce the
  close-before-drain race at all. See [[lazyftp-project]] for the full byte-by-byte trail
  across both attempts.
- **Resume mechanism tried, then REMOVED once the real root cause was found.** A first
  APPE-based resume (skip the already-landed prefix per `FtpSize`, re-send only the
  remainder) was built as a safety net independent of the drain-wait theory above. It
  didn't converge on real hardware: after several resume rounds the remote file's SIZE
  matched the expected total, but an independent md5/CRC check showed the CONTENT was
  still wrong. That's decisive - resume's entire premise ("whatever's already on the
  server up to SIZE bytes is a correct prefix, just short") assumes the link can only
  DROP bytes, not corrupt-in-place ones that still land. The real root cause turned out to
  be in the **WiRSa SLIP-NAT gateway** (`~/WiRSa/WiRSa/Firmware/src/modules/slip_nat.cpp`,
  a separate project - see [[wirsa-slip-nat-project]]), not lazyFTP or PalmOS: its
  client->server TCP relay had no sequence-number check, so a segment PalmOS correctly
  retransmitted (after the gateway dropped the original on an IP-checksum mismatch) got
  forwarded to the real server a second time, splicing it into the byte stream at the
  wrong position - corrupting content while the final size could still land right. Fixed
  at the gateway (only forward+advance on the expected sequence number; anything else gets
  a duplicate ACK instead of being blindly re-forwarded, letting PalmOS's own correct
  retransmit logic do the recovery).
- **`FtpStoreDatabase` is now a simple `ftpStoreMaxAttempts` (3) loop of fresh `STOR` + CRC
  verify**, full re-upload (not partial resume) on any mismatch. `STOR` already
  overwrites/truncates prior remote content, so no delete-between-attempts is needed.
  `FtpSize` and the skip/offered/callCount plumbing were deleted along with the resume
  logic - `StoreDbCtx` is back to just `ok`/`crc`/`sent` (`sent` only sizes
  `FtpTransportDrainWait`, unrelated to resume and kept).
- **CONFIRMED FIXED on real hardware.** Both the gateway-side fix
  ([[wirsa-slip-nat-project]]) and this app's own STOR+CRC-verify retry loop together
  resolved the upload content-corruption saga - user confirmed upload and download both
  work correctly, including through an auto-reconnect (see the stack-overflow fix below).
  If a genuinely slow/lossy real link ever needs multiple attempts even with the gateway
  fix in place, a resume mechanism could be reintroduced later - but it would need to
  verify the trusted prefix first (re-download + CRC-check just that prefix before
  APPEnding onto it), not just trust SIZE, given the corruption-not-just-truncation
  failure mode this saga proved is real on this hardware.

**Control-connection read timeout, a separate real bug from the upload saga above.** After
the upload/download work landed, real-hardware users still hit intermittent "Connection
lost" errors, especially right after opening a fresh connection. Root cause:
`FtpReadLine` (used for the greeting banner and every command reply, including USER/PASS)
called a plain blocking `NetLibReceive`-per-byte helper - the DATA connection had already
been fixed for the exact same class of problem (`NetLibReceive` "simply does not reliably
honor a short timeout on this SLIP-based connection type" - real-device instrumentation
once showed a 2-second-requested call actually blocking ~20 real seconds) via a
`NetLibSelect`-based non-blocking poll loop, but that fix had only ever been applied to the
data connection, never ported to the control connection's own read path. An early/short/
spurious read on the control connection reads as "Connection lost" even though the server
was still about to reply. **Fixed** by generalizing the existing data-connection poll
helper into `FtpPollReceive(sock, buf, len, budgetTicks, ...)` (added an explicit
`budgetTicks` parameter instead of a hardcoded data-connection-specific one) and switching
`FtpReadLine` to use it with `ftpCtrlTimeoutTicks` - the same already-proven fix, just
finally applied everywhere it needed to be, not a new mechanism.

**`FtpParseListLine` (the `ls -l`-style LIST line parser) — three real bugs found and
fixed against WiRSa's own vendored FTP server (MultiFTPServer), all in the same function.**
Symptoms first surfaced as "connects fine, but directory listing never displays" and then
"listing displays, but every file shows 0B" — both traced entirely to this one parser, not
the network layer (WiRSa's own gateway log confirmed the LIST data and the final `226`
reply were both delivered and ACKed correctly every time).
1. **Tab-delimiter bug.** The parser only ever recognized a literal space as a field
   separator. MultiFTPServer's LIST lines are tab-delimited
   (`"drwxrwsr-x\t2\t<user>\t4096\t<time>\t<name>"`), unlike every space-padded Unix server
   this parser had been tested against before — with no space anywhere until deep inside
   the time-of-day field, the "skip permissions token" loop ran straight through the whole
   line and it silently produced zero entries. Fixed via an `FTP_IS_WS(c)` macro (space OR
   tab) used throughout.
2. **Size always 0B.** The parser hardcoded a fixed skip of 3 non-whitespace tokens after
   the link count, assuming standard Unix `ls -l`'s owner+group (2 name fields) before the
   size column — but MultiFTPServer emits only ONE name field (no group), so the fixed skip
   consumed the size token itself as if it were "group," then failed to parse the time
   field's month text as a number, landing on `size = 0` every time.
3. **Robustness hardening**, on explicit request to make the parser bulletproof against
   common server output variance. Replaced the fixed-token-count approach with a
   **month-abbreviation anchor**: a new `FtpTokIsMonth()` helper recognizes `Jan`-`Dec`
   case-insensitively, and whichever token sits immediately before the recognized month is
   treated as the size, regardless of how many owner/group name fields preceded it. This
   also closes a related edge case a naive "first all-digit token" heuristic would have
   gotten wrong: some servers show a raw numeric UID/GID instead of a resolved username,
   which would be misidentified as the size by that simpler approach — the month anchor is
   immune to it. Deliberate remaining limitation, matching the pre-existing "Unix `ls -l`
   format only" v1 scope already noted in this file: a server using a non-English or
   purely-numeric/ISO date column (no month abbreviation) fails to parse that line rather
   than misparsing it.

Verified with a standalone host-side C harness reproducing the exact parser logic against
10 cases (WiRSa's own tab-delimited format, classic vsftpd/proftpd with both a recent-time
and an old-file-year date, numeric-UID owner with and without a group field, a filename
with embedded spaces, month case-insensitivity, and one deliberately out-of-scope ISO-date
line confirming graceful rejection) — all passed — plus a clean PalmOS SDK toolchain build.
**CONFIRMED WORKING on real hardware** against WiRSa's own FTP server, both directory
listing and correct file sizes, after WiRSa's own SLIP-NAT gateway self-connect/hairpin bug
(see [[wirsa-ftp-server-project]]) was fixed separately — that fix made the connection
itself reach WiRSa's FTP server at all; this one made the response actually render.

**Client-side connect retry with automatic reconnect** (`FtpConnect`, `ConnectForm.c`),
added after real-hardware testing kept showing intermittent connect failures even after the
fix above - traced to a likely WiRSa-gateway-side race around the very first
server-initiated packet on a brand new connection (FTP is unusual in that the *server*
speaks first), not a client- or server-side bug. `FtpConnect`'s socket-open+banner-wait
segment is a retry loop, scoped deliberately narrow: only transport-level failures (socket/
DNS open failure, banner timeout) retry, never a real auth rejection (wrong USER/PASS),
which retrying can never fix. Attempt count is user-configurable via a "Retry:" popup on
the Connect form (1/2/3, default 1 = 2 total attempts), a fixed 5-second interval between
attempts. Critically, **each retry attempt forces a full network close+reopen first**
(`FtpNetLibFullClose()` + fresh `NetLibOpen`) - the *existing* `FtpAutoReconnect` mechanism
built for "the SLIP link silently dropped" can't help here, since it has its own guard
(`if (!gFtp.user[0]) return false;`) that deliberately refuses to act during the very first
connect (there's no prior session to resume yet) - real-device testing showed the
underlying SLIP link actually had been torn down between retry attempts, and without this
forced reopen every retry after the first failed immediately with the same
`netErrNotOpen` (4610) error. The busy overlay is explicitly torn down *before* touching the
network (`LzTPBusyRetitle(NULL)`) and only redrawn after, for the same crash-avoidance
reason documented in §7 - forcing a network reopen is exactly the kind of operation that can
trigger PalmOS's own system-level redial UI. See §7 for the countdown-dialog hook and §5/§6
for two real screen-artifact bugs this feature's own UI hit and had fixed.

**Real-device crash on auto-reconnect, root cause and fix.** Precise real-device repro:
connect, browse, let the SLIP link's own idle-disconnect timeout (a Network Preferences
setting, independent of anything this app configures) AND the device's own auto-off both
fire, wake the device, trigger any command. `FtpAutoReconnect` fires correctly, but the
underlying redial (needed because the whole SLIP link is down, not just this app's TCP
session) crashed reliably. Two theories were tried:
1. **Screen-drawing conflict with PalmOS's own system-level redial UI** (this app's
   `LzTPBusyBegin`/`LzTPProgressBegin` both draw their own overlay via
   `WinSaveBits`/`FrmInitForm`, with no way to coordinate with the OS needing the same
   screen region for its own dial-up UI at the same moment) - **disproven by real-device
   retest**: dismissing the app's own overlay before the redial (rather than trying to
   keep "Reconnecting..." text up through it) did not fix the crash. The dismiss-on-
   reconnect behavior (`LzTPBusyStatusHook` now calls `LzTPBusyEnd()`+`LzTPProgressEnd()`
   instead of drawing new text) and the accompanying `LzTPProgressBar` guard (no-ops if
   its form was torn down mid-transfer, rather than corrupting whatever form IS active)
   were both kept regardless - genuine, low-risk correctness improvements even though they
   weren't the root cause.
2. **Stack overflow in the auto-reconnect call chain - the actual root cause, CONFIRMED
   by real-device retest.** A user-run differential test was decisive: a MANUAL "Connect"
   (shallow call chain, `DoConnect` straight into `FtpConnect`) never crashed on the exact
   same redial that reliably crashed via the AUTOMATIC path
   (`MainRefreshList -> FtpList -> FtpCmd -> FtpAutoReconnect -> FtpConnect` - several more
   stack frames deep). PalmOS gives an app only ~4KB of stack, and this exact bug class had
   already been fixed twice elsewhere in this file (`FtpTransportOpen`'s
   `NetHostInfoBufType`, the shared `gFtpChunkBuf`) but several large buffers in this
   SPECIFIC chain were still stack-local: `FtpCmd`'s `line[272]`, `FtpAutoReconnect`'s
   `FtpConnParams`+two 256B buffers+an inner 256B buffer (~962B - the single biggest
   contributor), `FtpConnect`'s `resp[256]`, plus `FtpList`'s two 256B buffers and
   `MainRefreshList`'s `err[256]` (baseline pressure even before the reconnect starts) -
   **~2.2KB total**. All converted to `static`, matching this file's own established
   remediation pattern - reentrancy checked per-function (`FtpCmd` genuinely IS
   re-entered via its own nested reconnect call, but rebuilds its buffer from `cmd`/`arg`
   fresh both before and after, so sharing storage is safe; the rest are either guarded by
   `gFtpReconnecting` or never re-entered at all).

## 4. Directory listing storage — cache DB for large directories (`Listing.c`/`.h`, sec003)

A real PalmOS FTP directory of 1660+ files broke the original fixed `gListEntries[96]`
array (silently capped at 96, no error reported). The fix went through two iterations:

1. A multi-chunk heap array up to 16384 entries — better, but still bounded by **dynamic
   heap**, which turned out to be the real binding constraint on memory-constrained
   devices/emulator configs well before 16384 (observed live in POSE64: out of heap after
   exactly one 512-entry chunk against the real 1660-file directory). Even where heap
   allowed it, "N of M" pagination computed from "how much fit in RAM" doesn't match the
   directory's *true* size — actively misleading, not just slow.
2. **Final design**: once a listing exceeds `lzListPageSize` (512) entries, it spills to an
   on-device database, `LazyFTPDBCache` (storage/flash heap — a completely different, far
   larger budget than dynamic heap). RAM (`gListPageBuf`, one `MemPtrNew`'d array, reused,
   never doubled with a second "canonical unfiltered" copy — an earlier version did that and
   ran out of heap even for a 3-entry directory on a real device) only ever holds the current
   page or filter-match batch. Record 0 of the cache DB is a `ListCacheInfo` metadata record
   (server/port/path/`cachedAtTick`) identifying which remote directory the cache represents;
   records 1..N are `FtpDirEntry` payloads in listing order.

**Staleness**: `ListingCacheValidFor(server, port, path)` — a cache is reusable only if it
matches the exact (server, port, path) tuple **and** is still within `gListCacheTtlSecs`
(runtime-configurable via Preferences: 30s/1min/2min/3min, default 30s) since it was built or
last revisited. `gListCacheLeftTick` starts a staleness clock the moment the CWD navigates
away from the directory the cache represents (0 while still "in" it) — revisiting within the
window keeps the existing cache alive without a re-fetch; past the window, or a Folder >
Refresh, forces a real re-`LIST`. This is genuinely a real network round-trip avoided on
ordinary back-and-forth browsing, not just a display optimization.

Small directories (≤512 entries) skip the cache DB entirely — `gListPageBuf` holds the whole
directory directly, filtering maps display row → buffer slot via a small `gFilterSlots[]`
indirection table (`LzListEntry()`) instead of physically copying matches into a second
buffer.

## 5. UI conventions & gotchas (`MainForm.c`, `Listing.c`, sec001/sec003)

- **Live type-to-filter** on the bottom filter field: `fldChangedEvent` does **not** fire
  per keystroke for a plain `SINGLELINE` field (this suite's own
  [[fldchangedevent-not-per-keystroke-gotcha]], first found in Address35, re-confirmed
  here) — `MainFormHandleEvent`'s `keyDownEvent` case dispatches to `FldHandleEvent`
  directly instead.
- **Page navigation** ("`< N of M >`") reuses Datebook35 Month View's exact title-bar
  `REPEATBUTTON`+glyph idiom (`FONT 4`, `\x02`/`\x03` render as left/right triangles); the
  "N of M" text itself is hand-drawn (`ListingDrawPageIndicator`), not a resource `LABEL`,
  since it changes dynamically.
- **Redraw bug — real root cause found only on the third attempt.** User report: "if I
  preview a text memo and dismiss that [...] I still see the same issue." Two earlier fixes
  were real bugs but not *the* bug:
  1. `ListingDraw()`'s own erase rectangle stopped at `lzListBottomY` (137px), but the
     bottom button bar in `MainEn.rcp` doesn't start until y=145 — an unerased 8px gap.
     Fixed by adding a separate erase-only bound, `lzListEraseBottomY` (144), kept distinct
     from `lzListBottomY` (still used for row-fitting/scroll-limit math).
  2. `ListingDraw()` alone never redrew the page indicator; added explicit
     `ListingDrawPageIndicator()` calls after `FileActionRun()`/`UploadFormRun()` return.
  3. **Actual root cause**, found via a live repro with real files on the test server and a
     before/after screenshot-hash comparison: returning from a full-screen sub-form
     (Preview, Upload) to `frmMain` left visible remnants of the sub-form's own **title bar**
     and **button bar** text — "LazyFTP _a.txt" bleeding into "LazyFTP", stray characters
     near Upload/Cancel from Preview's wider "Import to Memo"/"Close" labels. PalmOS's
     native title-bar/button drawing only clears **within each control's own bounds**, never
     whatever was on screen underneath before — this is not fixed by `FrmEraseForm` (which
     only restores auto-saved bits for **non-full-screen** forms; a full-screen form was
     never auto-saved in the first place). **Fix**: an explicit full-screen
     `WinEraseRectangle(0,0,160,160)` immediately before `FrmDrawForm(prevFrm)`, added to
     both `PreviewForm.c` and `UploadForm.c`. **Generalizable gotcha**: any full-screen form
     in this suite returning to a shorter/narrower parent needs this explicit erase — the
     bug class isn't specific to lazyFTP.
- **`FrmCopyTitle`/`FrmCopyLabel` buffer-overflow crash** — `frmPrompt` (`FORM ID 1400`,
  New Folder/Rename's shared text-entry dialog)'s `TITLE "Prompt"` (6 chars) and
  `LABEL ""` (0 chars) were both too short for the real runtime strings ("New Folder",
  "Folder name:", up to 12+ chars) written via `FrmCopyTitle`/`FrmCopyLabel` — these write
  into the **same fixed-size buffer the `.rcp` compiled at build time**, never reallocating.
  Silently corrupted the heap and wedged the CPU (confirmed via POSE64's `regs` showing an
  unmoving PC deep in ROM across repeated polls). Fixed by padding the `.rcp` placeholder
  text with enough spaces (30 for the label, 20 for the title) to cover the longest string
  any real caller writes. **This exact bug class was independently rediscovered here after
  already existing, documented, in EraNTP's own `.rcp`** (`EraNTP.md` §4) — see the
  standalone reusable writeup [[palmos-frmcopylabel-buffer-size-gotcha]]. Worth an explicit
  sweep of every `FrmCopyTitle`/`FrmCopyLabel` call site across the whole suite.
- **Retry-popup screen artifact on `frmConnect` — two attempts, the real fix on the third.**
  User report: a leftover artifact from the new Retry popup (§8) stuck on `frmMain` after
  the Connect dialog closed, but only ever on the *auto-launched first* Connect dialog
  (`MainFormHandleEvent`'s `frmOpenEvent` → `DoConnect()`), never on manually-reopened ones.
  1. First (wrong) theory: the popup's `LIST` resource sat only 3px from the form's own right
     edge (x=133, width=20, edge=153 vs form width 156) — narrowed to width 15 for more
     margin. Did **not** fix it — a real report followed pinpointing it was specifically the
     popup **trigger**, not the dropdown list, that was corrupted, and it appeared just from
     opening+dismissing the dialog, no popup interaction needed.
  2. **Real root cause**: `ConnectFormRun` called `CtlSetLabel`+`CtlDrawControl` on the Retry
     popup trigger **before** `FrmDrawForm(frm)`. `frmConnect` is a non-fullscreen MODAL
     frame form, and `FrmDrawForm` does an implicit "capture what's behind me for later
     restore" step for exactly this kind of form (the same mechanism §6's own `FrmEraseForm`
     note already documents). Drawing the trigger control *before* that capture ran meant the
     capture recorded the premature draw itself instead of `frmMain`'s real pixels
     underneath — so when the dialog later closed, `FrmEraseForm` restored the corrupted
     snapshot, stuck on screen until something else happened to redraw that exact region
     (nothing normally does). Only visible on the very first auto-launched dialog because
     that's the one case where `frmMain`'s true background at that screen position hadn't
     been freshly redrawn by anything else yet. **Fixed** by moving the whole
     `CtlSetLabel`/`CtlDrawControl`/`LstSetSelection` block to run *after* `FrmDrawForm(frm)`
     instead of before. **Generalizable gotcha, same family as the full-screen-form erase
     bug above**: any programmatic `CtlSetLabel`/`CtlDrawControl` call on a non-fullscreen
     MODAL form's own controls must happen *after* that form's own `FrmDrawForm`, never
     before — the Profile popup (`ConnectForm.c`) does the visually-identical
     draw-before-`FrmDrawForm` pattern and was never reported as buggy (likely because its
     default value always matches the resource-compiled default label, so the common case
     never actually changes any pixels) but was deliberately left as-is ("works, don't
     touch it") rather than "fixed" speculatively.
- **Two-tap select-then-act, Cut/Paste, Rename Folder, generic Delete** (2026-08-27) - a
  UI overhaul replacing the old "tap always acts immediately" model, arrived at over several
  investigation rounds before any code was written (Copy was explicitly considered and
  dropped: FTP has no server-side copy verb, and a client-mediated RETR+STOR round trip
  through Palm RAM was judged not worth the complexity/memory risk on this platform).
  - **Interaction model** (`MainForm.c`'s `penDownEvent`/`kListHitEntry`): a tap on a row
    now selects+highlights it (`WinInvertRectangle`, spanning the row's real - possibly
    2-line-wrapped - height via the existing `EntryRowHeight()`) unless it's *already* the
    selected row, in which case the tap performs today's unchanged action (open the
    file-action popup, or `FtpChangeDir`). Selection (`Listing.c`'s `gListSelActive`/
    `gListSelName`/`gListSelIsDir`) is identified **by filename, never by
    `ListHit.entryIndex`** - that index is explicitly only a slot in whatever page/
    filter-batch is *currently loaded* (§4's large-directory cache pages through
    `gListPageBuf`), so an index would silently point at the wrong entry after a page turn,
    filter change, or refresh. Cleared only at the `MainRefreshList()` chokepoint (already
    the single point every mutation/navigation funnels through) and `ListingReset()` -
    deliberately **not** cleared by `ListingUpdatePaging()`, which also reloads
    `gListPageBuf` but fires on every filter-field keystroke; a still-visible-after-
    filtering selection should stay selected, and the name-match design makes a
    filtered-out selection harmlessly stop highlighting on its own rather than needing an
    explicit clear. Cancelling out of the file-action popup deliberately retains the
    highlight (an explicit requirement) - it works for free, since Cancel/Preview never
    call `MainRefreshList`.
  - **Cut/Paste = a real move** (`FileActions.c`'s `DoCut`/`DoPaste`), reached only from the
    `Edit` menu, explicitly never added to the file-action popup. `DoCut` snapshots
    `FtpCurPath()` + the selected name into a persistent module-static buffer (deliberately
    separate from the row-selection state above - selection is short-lived per-directory UI
    state, this buffer has to survive navigating away on purpose); `DoPaste` builds the
    destination from wherever's currently browsed via `FtpJoinPath` and calls the existing
    `FtpRename` (§3) - the entire "move," reusing already-proven code, working identically
    for a file or a directory-with-contents. Alerts "Already in this folder." (pasting into
    the source dir, buffer preserved) / "Nothing to paste." (empty buffer); a transport
    failure also preserves the buffer so the user can retry. **`FileActionsClearCut()`**
    unconditionally resets the buffer (and redraws the indicator below) - called by
    `DoPaste`'s own success path, and by `MainForm.c`'s `DoConnect` the moment a *new*
    connection is actually started (after `ConnectFormRun` returns `true`, i.e. genuinely
    attempted, not Cancelled) - a buffer surviving into a different server/session could
    reference a path that's meaningless or dangerous there.
  - **Title-bar cut-buffer indicator** (`ListingDrawCutIndicator`, `Listing.c`) reuses the
    exact hand-drawn-into-the-native-title-bar technique the "N of M" page indicator
    already established (erase-then-conditionally-redraw a small bitmap at a fixed title
    strip slot) - and needed the *same* redraw-discipline lesson that indicator itself
    learned the hard way: co-called at every one of `ListingDrawPageIndicator`'s own call
    sites, or a full-screen sub-form's return leaves it stale. Doing this surfaced a
    **pre-existing latent gap**: `mniLogs` never called `ListingDrawPageIndicator()` at all
    despite `frmLog` being full-screen (same class as Preview/Upload) - never noticed
    before because a stale "N of M" text rarely *looks* wrong, but the new icon's binary
    present/absent state would have made it obvious, so fixed both while adding the new
    one rather than leaving it sitting right next to a freshly-added indicator with the
    identical bug. **Two real-hardware/live-testing rounds on the icon itself**: (1) the
    icon's first position (`lzCutIndicatorX` = 90, `Listing.h`) clipped into
    `btnMainPrevPage` (the "◀" `REPEATBUTTON` at `AT (91,0,11,13)` in `MainEn.rcp`, right
    next door) - the user spotted this live while watching a POSE64 session; moved to
    X=84. (2) the icon's first *design* (an X/scissors-cross) was replaced, on request, to
    match this app's own existing file-row glyph exactly - cropped and inspected a live
    screenshot of that glyph (a rectangular page outline with 3 short horizontal lines
    inside, `lzGlyphTextFile`) rather than guessing, then redrew `cut.bmp` to match. Both
    `folder.bmp`/`cardup.bmp`/`cut.bmp` are hand-crafted raw 2-color BMPs via a Python
    `struct`-based writer, not PIL's default writer - PIL's own BMP output always emits a
    full 256-color palette table that doesn't match this trio's established minimal-palette
    convention (194 bytes vs. ~1.2KB for the identical 11x11 pixel content).
  - **Rename Folder** (`DoRenameFolder`, `Edit` menu) - only possible *because* folders can
    now be selected without navigating into them. Same `PromptFormRun` flow as the
    file-action popup's own Rename, but reads the current selection (must be a directory)
    instead of a tapped filename, with its own "Rename Folder" title distinguishing it from
    the file version. Same-directory rename only (a bare name, unlike Cut/Paste which cross
    directories - no `FtpJoinPath` involved).
  - **Generic `Edit > Delete` replaces the old `Folder > Del Folder` entirely.** The old
    Del Folder only ever deleted the CURRENT directory (had to navigate *into* a folder
    first); the new one (`DoDelete`, same menu-item ID 1607, just renamed) routes on
    `gListSelIsDir`: a folder gets the identical `altConfirmDelFolder` confirm + recursive
    delete as before, via the `DoDeleteFolder` CWD-first wrapper described in §3; a file
    routes to `DoDeleteFile` (renamed from `DoDeleteConfirm`), which now shares its confirm
    dialog with the file-action popup's own Delete button - by explicit instruction, one
    dialog, reachable both ways. The old file-specific wording (`altConfirmDelete`,
    `MESSAGE "Delete \"^1\"?"`) was replaced with generic phrasing matching the folder
    dialog's own style: `TITLE "Delete File"`, `MESSAGE "Delete this file?\nThis cannot be
    undone."` - no more `^1` substitution. Edit pulldown order (also an explicit,
    deliberate choice, confirmed live): `Cut / Paste / Delete / -- / New Folder /
    Rename Folder / Refresh` - Delete groups with Cut/Paste above the separator since all
    three act on the current selection, while New Folder/Rename Folder/Refresh (folder-
    lifecycle/listing actions, not selection-dependent in the same way) sit below it.
  - All of the above **live-verified against a real server** (a private FTP host,
    `/Scratch`) via POSE64 ReControl, including ground-truth bidirectional navigation
    (not just a screenshot) proving a Cut+Paste genuinely moved a file's bytes across
    directories and a Rename Folder's contents survived intact. This specific POSE64
    session had unusually severe `menu "Pulldown" "Item"` ReControl command lag (sometimes
    several real seconds, with screenshots staying stale even longer) - `info`'s `form=`
    field and `ui`'s live object dump cut through it reliably where screenshot polling
    didn't; see [[lazyftp-cutpaste-tapselect-renamefolder]] for the full harness-quirk
    writeup if a future session hits the same thing.

## 6. Dialog layout — two standing rules for this app specifically

Unlike the PIM apps (where dialog sizing follows whatever the ROM original looked like),
lazyFTP's dialogs are **original UI**, and the user set two explicit, non-negotiable
conventions for every modal dialog `FORM` in this app:

1. **Full screen width, bottom-aligned** — `AT (2 y 156 H)` with `y+H=156` (one dialog,
   `frmPrompt`, was explicitly extended to `y+H=160` per live feedback). Content must start
   at `y>=16` (never overlapping the dialog's own title bar — a real clipping bug found and
   fixed on `frmConnect`, which originally started content at y=7).
2. **"Shorter" means height only, never width.** This was violated once, on the Rename
   dialog: "shrink the form" was misread as narrowing it to snugly fit its ~44px buttons
   (156→100px), breaking rule 1. Corrected immediately on live (sharp) user feedback by
   restoring full width and instead genuinely reducing **height** (86→68) — moving the
   `GRAFFITISTATEINDICATOR` onto the same row as the buttons rather than its own row below.

`MainEn.rcp` is also under a standing **process** rule (not a layout rule): the user edits
it directly and manual changes there should not be made without their explicit go-ahead in
that specific message — this doc's UI description reflects the file's state as of this
session's last read, not a claim that it won't keep evolving under the user's own hand.

All modal dialogs (Connect, Preferences, Prompt, FileAction, Preview's own inner
confirmations) use a **hand-rolled `EvtGetEvent`+`SysHandleEvent`+`MenuHandleEvent`+
`FrmHandleEvent` loop**, never `FrmDoDialog` — `FrmDoDialog` was found not to reliably
dispatch `frmOpenEvent` through a custom handler on this SDK/glue configuration (documented
inline in `ConnectForm.c`, the pattern all the later dialogs — `PrefsForm.c` explicitly
credits `ConnectForm.c` as precedent — copy verbatim). Non-full-screen dialogs restore the
screen behind them via `FrmEraseForm` (bits were auto-saved by `FrmDrawForm` since they
aren't full-screen); full-screen ones need the explicit erase from §5 instead.

## 7. Busy/progress feedback (`BusyDialog.c`, sec001)

Two mechanisms, both callable from anywhere `FtpProtocol.c` runs, wired via two opt-in
function-pointer hooks (`FtpSetStatusHook`/`FtpSetProgressHook` in `FtpProtocol.h` - NULL
by default, zero effect on any caller that doesn't register one):

- **`LzTPBusyBegin/End`**: the plain text-only toast ("Accessing...", "Connecting...",
  etc.), still screen-direct via `WinSaveBits`/`WinRestoreBits` (no FORM resource) —
  appropriate for something this small and transient.
- **`LzTPProgressBegin/Bar/End`**: a **real modal FORM** (`frmXferProgress`, ID 1900, `AT
  (2 54 156 52) MODAL` - vertically CENTERED rather than bottom-aligned, a deliberate
  exception to §6's own rule for this one small transient toast, matching Sec35's own
  `frmProgress` reasoning exactly) drives an actual percentage bar during
  upload/download, title set to "Uploading"/"Downloading", body line showing the remote
  filename. Byte-level progress comes from `FtpProgressProc` (`FtpStoreDatabase` sizes
  via `DmDatabaseSize`, `FtpRetrieveDatabase` via an FTP `SIZE` query, both best-effort -
  a failed query just means an empty/0% bar, not an error). `LzTPProgressBar`'s geometry
  and auto-off-safety (throttled `EvtResetAutoOffTimer` + unconditional event-drain every
  call, no dispatch) are copied pixel-for-pixel and behavior-for-behavior from Sec35's own
  `ProgressBar` (`sec35.c`) - this is a genuinely `FrmInitForm`/`FrmEraseForm`-based form
  now, drawn into by hand the same way Sec35's is, unlike this app's own earlier
  screen-direct attempt.

Both are **synchronous, tight-loop** draw calls (no event pump of their own) driven from
inside `FtpProtocol.c`'s own blocking calls. **`LzTPBusyStatusHook`** (registered as the
`FtpStatusProc`) fires the moment `FtpAutoReconnect` starts a silent reconnect - it
dismisses BOTH overlays entirely (`LzTPBusyEnd()`+`LzTPProgressEnd()`, each a safe no-op
if the other one is what's actually showing) rather than drawing new "Reconnecting..."
text, since PalmOS's own system-level network redial UI may need the same screen region a
moment later (see §3's crash writeup - this was tried as the crash fix and, while
disproven as the actual cause, was kept as a real correctness improvement regardless).
`LzTPProgressBar` correspondingly guards against its form having been torn down mid-
transfer by that same hook (no-ops the draw, but still runs its own auto-off-reset/event-
drain) rather than corrupting whatever form ends up active.

A **third**, deliberately separate hook backs the connect-retry countdown described in §3:
`FtpSetConnRetryHook`/`gFtpConnRetryHook` (`FtpConnRetryProc`, `FtpProtocol.h`) fires from
`FtpConnect`'s own retry loop right before each 5-second wait, passing the attempt number
and seconds-remaining so `ConnectForm.c` can redraw a live "Retrying in Ns... (attempt
N/M)" line. This is intentionally **not** folded into `FtpStatusProc` even though both fire
around reconnect-shaped events: `FtpStatusProc`/`LzTPBusyStatusHook` exists specifically to
get *out of the way* the instant PalmOS's own system-level network redial UI might need the
screen (§3, §7 above), while the connect-retry hook's whole job is to keep drawing a
countdown across that same window - merging them would mean either the countdown fights the
system UI for the screen, or the crash-avoidance dismiss-both behavior silently eats the
countdown. Two hooks, two screen-ownership models, kept apart on purpose.

`LzTPBusyRetitle` (the plain-toast half of this file) gained a `NULL`-title mode -
`LzTPBusyRetitle(NULL)` tears down the current toast without drawing a replacement, versus
its normal `LzTPBusyRetitle("new text")` behavior. Used immediately before any operation
that's about to reopen the network (`FtpNetLibFullClose()` + fresh `NetLibOpen` in the
retry loop) so there's never a stale toast on screen while ownership of the network layer
is mid-transition - same screen-ownership caution as the `FtpStatusProc` dismiss-both
pattern above, applied one layer earlier.

## 8. Saved connection slots + Sec35 password encryption (`LzTPSec35.c`/`.h`, sec002)

3 saved slots (`ConnectForm`'s popup), each `{server, port, user, encPass, passLen,
secMode}` in `LzTPConnPrefs` (versioned, `saved=true` app prefs — unlike the UI-only prefs
in §10, these are worth a HotSync backup). Only the **password** field is encrypted at rest
(`LzTPEncryptSlotPassword`/`LzTPDecryptSlotPassword`, AES-CTR via the shared `enccore.h`
core, nonce = the per-slot index — stable and unique per slot, same idea as Address35's own
`(uniqueID, fieldIndex)` nonce scheme); server/port/username stay in clear text, matching
Address35's established "encrypt payload, not everything" pattern. If Sec35 is locked,
`LzTPEncryptSlotPassword` is a no-op (returns `false`, slot left unchanged) — the caller can
still *use* the freshly typed plaintext for an immediate connect, it just can't be
persisted until unlocked. `secMode` (`FtpSecMode`: none/explicit-TLS/
implicit-TLS) is reserved, always `kFtpSecNone` in v1 — present now so a future FTPS
integration doesn't need a prefs-version migration to add it later.

The Connect dialog's password field is **not** simply always-masked. It ported Sec35's own
reveal-then-hide behavior from `sec35.c` (`RedrawMaskedField`/`PwFieldKeyDown`/
`MaskPendingReveal`): each typed character is shown in the clear for a short window
(`gRevealPending`/`gRevealUntilTicks`), then masked to `*`, driven from `ConnectFormRun`'s
own bounded-timeout `EvtGetEvent` loop rather than a real timer — same reasoning as Sec35's
original: on-device typing without any per-character feedback is error-prone with no way to
proofread, and full plaintext is one shoulder-surf away from full masking. A **"Show"**
checkbox (`ctlConnShow`/`gPassShowPlain`) sits next to the field for the times a full reveal
is actually wanted (e.g. checking pasted/synced text) — checking it displays the whole field
in clear for as long as it's checked, independent of the reveal-then-hide per-keystroke
animation.

An **Anonymous** checkbox (`ctlConnAnon`) is derived, not persisted: it reflects (and, when
toggled, drives) `user[0] == '\0' && passLen == 0` — checking it clears and disables the
user/password fields, unchecking re-enables them; no separate flag is stored in
`LzTPConnPrefs`, since "anonymous" is fully recoverable from an already-blank user/password
pair.

When a saved slot with a persisted (encrypted) password is selected, the password field
shows a fixed **`"*****"` placeholder** (`gPassPlaceholderShown`) rather than either the
real decrypted text or an empty field — distinguishing "a password is saved, unseen" from
"no password saved" at a glance, without decrypting anything just to populate a UI field.
This placeholder was also the site of a real root-cause bug: connect would intermittently
fail 2-3 times before succeeding, traced to the dialog silently sending a **blank** password
whenever Sec35 was locked at dialog-open time (the placeholder displayed as if a password
were present, but the plaintext needed to decrypt it wasn't available). Fixed with a
one-more-decrypt-attempt-then-recover flow: if the slot has a saved password but Sec35 is
locked, the dialog attempts one fresh decrypt first (Sec35 may have been unlocked since the
dialog opened) at both the point where this matters — Connect-tap, and toggling the **"Show"**
checkbox on while the placeholder is still showing — instead of silently sending/revealing an
empty password.

**2026-09: wired to the suite's standard Sec35 bounce, not an alert.** `LzTPSec35.c`'s
`LzTPSwitchToSec35()` (the same `FtrSet`+`SysUIAppSwitch(..., kSecLaunchUnlock, ...)` bounce
every other "35" app's `CryptoSwitchToSec35`-style function uses) existed from the start but
had zero call sites — the plumbing was built but never wired up. Both retry-then-still-fails
cases above now call it (gated on `LzTPSec35Installed()`, falling back to the original
`FrmCustomAlert` only if Sec35 isn't installed at all — nowhere to bounce to) instead of
blocking with an alert telling the user to go unlock Sec35 manually, matching
`CryptoLockedBounce()`'s pattern used throughout the rest of the suite. On the Connect-tap
path specifically, the bounce is handled exactly like tapping Cancel — the dialog tears down
the same way (no in-progress, not-yet-saved server/user/port edits survive the round trip;
same tradeoff every other Sec35 bounce in this suite already makes, since Sec35's flip-back
is a full app relaunch with no mechanism anywhere in the suite to resume mid-dialog state).
The passive slot-load path (`LoadSlotIntoUI`, on dialog-open or switching slots) deliberately
still does **not** bounce — same "list view shows `******`, only an actual open/use action
bounces" convention CryptoNote's own record list already established. **User-confirmed
working (2026-09-29).**

The **Retry** popup (`popConnRetry`/`lstConnRetry`, values 1/2/3, default 1 meaning 2 total
attempts) controls the §3 connect-retry-with-forced-reconnect mechanism. Its selection is
stored as `retryCount` in the §10 **UI-only** `LzTPUiPrefs` blob (`saved=false`) rather than
this section's `saved=true` `LzTPConnPrefs` blob — deliberately, to avoid bumping
`lzPrefsVersion` on the HotSync-backed-up connection-slot data just to add one small
device-local UI setting. The popup's active selection is tracked explicitly in a module
variable (`gRetryCount`, updated on `lstSelectEvent`) rather than read back via
`LstGetSelection()` when Connect is tapped — mirroring the pre-existing Profile popup's own
`gActiveSlot` pattern — since a deferred read proved unreliable across this dialog's custom
event loop.

## 9. Connection logging (`LzTPLog.c`/`.h` sec002, `LogForm.c`/`.h` sec001)

Built after the control-connection timeout fix (§3) didn't fully resolve the user's
"Connection lost" reports on real hardware — rather than keep guessing blind, `Options >
Logs` (`mniLogs`=1609, positioned right after Preferences, shortcut "L") opens a full-screen
Note-like viewer over exactly what FTP commands went out and what came back.

**Storage** writes to an actual on-device database, not RAM: `LzTPLogInit()` (called from
`StartApplication`) unconditionally deletes and recreates a `"LazyFTPLogs"` DB (type
`'DATA'`, exports as `LazyFTPLogs.pdb`, same convention as `Listing.c`'s own on-device
directory cache) fresh every launch — the log is explicitly meant to cover only the current
run. Each log line is its own record; `LzTPLogWrite` caps total size at 4096 bytes
(`lzLogMaxBytes`), trimming the oldest record via `DmRemoveRecord` before appending and
tracking a running `gLogTotalBytes` counter rather than re-walking records each call (cheap
since the DB always starts empty per launch). `LzTPLogReadAll` concatenates every record
newline-joined into one exactly-sized `MemHandle` for the viewer.

**Viewer** (`LogForm.c`, new `frmLogs`=1520 resource) is a dedicated new form rather than a
repurposed `PreviewForm.c` (§5's existing full-screen text viewer has an "Import to Memo"
button that makes no sense for a log — kept the two independent instead of adding
conditional button logic to the existing one), though it copies `PreviewForm.c`'s
scrollbar-sync pattern (`FldGetScrollValues`+`SclSetScrollBar`, `FldScrollField` on
`sclRepeatEvent`) and its full-screen `WinEraseRectangle`-before-`FrmDeleteForm` fix (§6) to
avoid the previous form's title/buttons showing through.

**Capture points** hook the single near-universal choke point, `FtpCmd` (used by nearly
every FTP operation — LIST/CWD/PWD/DELE/RMD/RNFR+RNTO/MKD/NOOP and USER/PASS/TYPE via
`FtpConnect`): logs the outgoing `"> CMD arg"` before send and the reply `"< code text"`
after, for both the first attempt and the silent auto-reconnect retry, plus explicit
`"! ..."` lines for every transport-level failure point. **Password is redacted** — `PASS`
always logs as `"> PASS ****"`, never the real value. `FtpConnect` additionally logs its own
pre-`FtpCmd` steps (connecting, socket-open failure, banner success/failure) and a clear
`"! login rejected: ..."` line specifically on a non-230 USER/PASS reply, so the classic
wrong-credentials case doesn't require scanning back through every line to spot.
`FtpAutoReconnect` logs its own trigger too.

Log-line buffers inside `FtpCmd`/`FtpConnect` are static, not stack-locals — both functions
already sit in a documented deep, real-hardware-crashed-once re-entrant call chain (`FtpCmd`
→ `FtpAutoReconnect` → `FtpConnect` → nested `FtpCmd`) on a tiny ~4KB app stack (§3's
auto-reconnect crash writeup); a new stack-local buffer in either would reintroduce that
exact overflow class.

## 10. UI-only preferences (`PrefsForm.c`/`.h`, sec001)

A **separate**, deliberately `saved=false` (not HotSync-backed-up) app-prefs blob
(`LzTPUiPrefs`: `cacheTtl`, `disconnectNetworkOnExit`, `retryCount`) — distinct from §8's `saved=true`
connection slots, since these are minor device-local UI settings not worth backing up.
Options > Preferences dialog: a popup for the large-directory cache TTL (§4) and a
**"Disconnect on exit"** checkbox. Semantics (renamed/clarified from an earlier
"Disconnect on close" wording, per explicit user correction): `StopApplication` **always**
closes the server session on exit, unconditionally - an open FTP control connection has
no reason to survive the app that owns it. This checkbox controls only the STRONGER
action: whether the underlying network/SLIP connection ALSO gets torn down (checked) or
is left up for whatever's next - another app, or relaunching this one without paying to
re-dial (unchecked, the default - establishing the SLIP link is the expensive part on
real hardware). The version constant (`lzUiPrefsVersion`) has been bumped twice: 1→2 when
the redundant "Preview Text Files" checkbox was removed; 2→3 when `disconnectOnClose` was
renamed to `disconnectNetworkOnExit` with narrower meaning (an old saved=true blob for
that bit would silently mean something different under the new semantics) - both bumps
make `LzTPLoadUiPrefs`'s own version check reset to defaults on an old real-device blob
rather than misapply/misread it.

## 11. Manual Network > Connect/Disconnect/Disconnect Network

Same refcounted-Net.lib-open idiom as EraNTP (`EraNTP.md` §6, [[palmos-netlib-udp-client-working-pattern]]):
a full network teardown drains the **entire** open count (`NetLibClose(refnum, true)` in a
loop, not just one close) plus `NetLibFinishCloseWait`, since a single close only
decrements the refcount and can leave the interface looking "still connected" for a while.

**Two distinct disconnect actions** (user-requested split, `FtpProtocol.c`): the `Network`
menu is `Connect` / separator / `Disconnect Server` / `Disconnect Network`.
`FtpDisconnectServer()` sends `QUIT`, closes the control socket, clears session state -
but deliberately leaves `gFtp.netOpened`/Net.lib alone, so switching servers doesn't pay
the SLIP re-dial cost. `FtpDisconnectNetwork()` is the superset action: calls
`FtpDisconnectServer()` first if a session is still open, then does the full Net.lib
teardown above. There is no third "network gone but still connected to a server" state.
`StopApplication` (§10) uses both: server close is unconditional, network teardown is
gated on the "Disconnect on exit" preference.

If the app launches with no active connection, `MainForm.c`'s `frmOpenEvent` (which only
ever fires once per launch - every sub-form is a modal-style dialog loop that returns
here, not a fresh `frmOpenEvent`) calls `DoConnect()` automatically, offering the Connect
dialog immediately instead of an empty list with no obvious next step.

## 12. File map

```
apps/lazyFTP/
  LazyFTP.c / .h        entry point, shared resource IDs (#define, no separate resource
                         header - this suite's convention for apps this size), lz_seg.h include
  lz_seg.h               segment map + the 3 standing multiseg-gotcha reminders (see §2)
  FtpProtocol.c / .h     sec004 - raw socket boundary, FTP control/PASV/LIST protocol,
                         recursive folder delete, Upload/Download-as-database bridging,
                         FtpRename (RNFR/RNTO, file-vs-directory-agnostic) + FtpJoinPath
                         (absolute-path builder for Cut/Paste, see §3/§5)
  Listing.c / .h         sec003 - hand-drawn directory listing, on-device cache DB (large
                         dirs), paging/filter state, scrollbar sync, two-tap row-selection
                         state (gListSelActive/gListSelName/gListSelIsDir, by name not
                         index) + highlight draw, title-bar cut-buffer indicator (see §5)
  MainForm.c             sec001 - frmMain event handler: two-tap select/act dispatch,
                         filter field, menu dispatch (incl. clearing the cut buffer on a
                         new connection), redraw-after-subform-return orchestration
  ConnectForm.c / .h     sec001 - 3-slot connect dialog (hand-rolled modal loop, the
                         pattern every later dialog copies), Anonymous/Show checkboxes,
                         reveal-then-hide password masking, Retry popup (see §8)
  PrefsForm.c / .h       sec001 - Options > Preferences dialog + LzTPUiPrefs (saved=false)
  FileActions.c / .h     sec001 - per-file action popup (Preview/Download/Delete/Rename);
                         Edit menu actions: Cut/Paste (DoCut/DoPaste + the cut buffer),
                         New Folder, Rename Folder, generic Delete (DoDelete, routes to a
                         file or folder confirm/deletion based on the current selection -
                         see §5)
  PromptForm.c / .h      sec001 - reusable single-field text-entry dialog (New Folder name,
                         Rename new name) - title/label/initial value set at runtime
  PreviewForm.c / .h     sec001 - full-screen text preview + Import to Memo (MemoDB)
  UploadForm.c / .h      sec001 - full-screen installed-database picker (with a live
                         find filter, fixed list outline regardless of match count) +
                         remote-name prompt + FtpStoreDatabase
  BusyDialog.c            sec001 - screen-direct busy toast (LzTPBusyBegin/End, no FORM) +
                         a real FORM-based transfer progress bar (LzTPProgressBegin/Bar/End,
                         frmXferProgress - Sec35 frmProgress design, see §7)
  LzTPSec35.c / .h        sec002 - saved connection-slot storage (saved=true) + Sec35
                         password field-level encryption (enccore.h)
  LzTPLog.c / .h          sec002 - FTP command/reply + connection-event logging to the
                         on-device LazyFTPLogs.pdb (see §9)
  LogForm.c / .h          sec001 - full-screen Note-like log viewer, frmLogs (see §9)
  MainEn.rcp              all resources - frmMain (1000), frmConnect (1200), frmPrefs
                         (1300), frmFileAction (1710), frmPrompt (1400), frmPreview (1500),
                         frmUpload (1800), frmXferProgress (1900), frmLogs (1520), MENU 1600
                         - PULLDOWN "Edit" (was "Folder"): Cut(1610)/Paste(1611)/
                         Delete(1607, was Del Folder, renamed in place)/--/New Folder(1603)/
                         Rename Folder(1612, new)/Refresh(1605) - ctlConnAnon/ctlConnShow,
                         popConnRetry/lstConnRetry, ALERTs 1700-1720 (1705 "Delete File"
                         reworded, no longer takes a ^1 filename substitution - shared by
                         the file-action popup's Delete button and Edit > Delete alike),
                         BITMAP 1011 "cut.bmp" (title-bar cut-buffer indicator, see §5),
                         ICON/SMALLICON (see below). Edited directly by the user; do not
                         modify without explicit per-message authorization.
  icons/                  lazyftp_large.bmp (22x22) + lazyftp_small.bmp (15x9), 1-bit ->
                         tAIB 1000/1001 via MainEn.rcp's ICON/SMALLICON directives.
                         mkicon_lazyftp.py generates both from a user-supplied, already
                         native-resolution reference (../large_icon.png): the large icon is
                         a direct pixel-for-pixel threshold-and-copy (no redrawing); the
                         small icon crops the exact "FTP" wordmark pixels straight out of
                         that same reference (cols 3-18, rows 13-18) rather than using a
                         separately hand-authored font, dropping one genuinely-redundant
                         column to fit the hard 15px width (see [[lazyftp-project]] for the
                         near-miss where the wrong column was dropped first and broke P's
                         legibility).
  Makefile               multi-segment build (-palmos4 required, see §2)
```

Depends on `apps/lib/enccore.h` (shared AES-CTR core with Address35/Sec35/CryptoNote) for
§8's password encryption; otherwise self-contained — no shared category-sync, no era-plugin
cooperation (an FTP client has no dates of its own to rebase).
