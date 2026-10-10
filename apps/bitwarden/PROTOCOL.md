# BitWarden Relay — Palm↔WiRSa serial wire protocol (v1)

Single source of truth for three implementations: the PalmOS app (framing in the shared
`apps/lib/relayproto.c`/`relaywire.c`/`relaysec.c`; BitWarden's `Serial.c` keeps only its poll
hook), the Python
test harness (`tools/mock_wirsa.py`), and the WiRSa firmware
(`bitwarden_relay.cpp`). All three must encode/decode identically. **Note:**
`mock_wirsa.py` predates serial-link encryption (2026-09-30) and does not implement the
0x70–0x74 key exchange, so current Palm builds stop at "Serial link cannot be secured!"
against it; it needs that added before it can be used again.

This is the first custom Palm↔WiRSa serial protocol in this project (HotSync/SASD speak
Palm's own standard protocols instead) — there is no prior convention to match, so it's
defined fresh here, kept as simple as correctness allows.

## Transport

Raw UART, same physical link as HotSync/SASD (cradle serial). Palm side: PalmOS Serial
Manager (`SrmOpen`/`SrmSend`/`SrmReceive`/`SrmClose`). WiRSa side: hardware UART already
used by every other mode. Baud: reuse whatever rate the active WiRSa "BitWarden Relay" mode
sets (same mechanism as the existing SetBaud mode) — not fixed by this document.

## Framing

Every message is one frame:

```
+--------+--------+--------+-----------------+----------+
| type   | bodyLen (uint16, big-endian)      |   body   | checksum |
| 1 byte |            2 bytes                | bodyLen  |  1 byte  |
+--------+------------------------------------+----------+----------+
```

- **Byte order**: big-endian (network order) for every multi-byte integer, both
  directions — matches the Palm's native 68k endianness; the ESP32 side just does the
  shifts manually (`(b[0]<<8)|b[1]`), no assumption about WiRSa's own endianness needed.
- **checksum**: 1-byte XOR of every byte in `type`, `bodyLen` (both bytes), and `body`.
  Cheap integrity check against serial line noise — not cryptographic. A frame that fails
  checksum is silently dropped by the receiver (the sender times out and may retry; retry
  policy is a client concern, not part of this spec).
- **Strings** inside a body are always `uint16 length` + that many raw UTF-8 bytes (no NUL
  terminator on the wire). Empty string = length `0`.
- **Max body**: 65535 bytes (uint16 length field). In practice frames stay well under 1KB;
  a single long `notes` field is the only realistic way to approach this, and the sender
  should reject/error rather than truncate silently if a real field exceeds it.

## Two independent crypto layers (do not confuse them)

1. **Wire confidentiality** (mandatory since 2026-09-30, shared with CalDAV Sync) — every
   frame between Palm and WiRSa is encrypted, so the master password and decrypted vault
   items never cross the cradle wire in cleartext. Implemented once for all relays in
   `apps/lib/relaysec.c` (Palm) and WiRSa's `relay_proto.cpp`; no user interaction, no
   pinning, **no plaintext fallback**.
2. **At-rest confidentiality** (Palm-only, `enccore.c`) — encrypts the local
   `BitWardenDB.pdb` on the Palm; entirely independent of this wire protocol.

### Serial-link encryption (all relay modes)

Right after the port opens (9600 baud, before `HELLO`), the Palm runs a key exchange made of
four plaintext frame types in a range reserved for it (0x70–0x74):

| Type | Name | Direction | Body |
|---|---|---|---|
| 0x70 | `KEX_REQ` | Palm → WiRSa | `uint32 reqId` (random) |
| 0x71 | `KEX_PUB` | WiRSa → Palm | `uint32 reqId, uint16 modLen (=128), modulus[modLen], r3[modLen], wRand[16]` (big-endian) — RSA-1024, e = 3; `r3` = R³ mod modulus, R = 2^(8·modLen) |
| 0x72 | `KEX_KEY` | Palm → WiRSa | `uint32 reqId, RSA-PKCS#1-v1.5(secret[16])` |
| 0x73 | `KEX_OK` | WiRSa → Palm | sent **encrypted**, plaintext body `"RPOK"` |
| 0x74 | `KEX_FAIL` | WiRSa → Palm | `uint32 reqId` |

- Session key = AES-128-ECB(key = `secret`, block = `wRand`), so it is fresh per connection
  even if the Palm's random source repeats.
- Palm speed: e = 3 plus the WiRSa-supplied R³ lets the Palm encrypt in three Montgomery
  multiplications (m·m, ·m, ·R³). e = 3 is safe here because PKCS#1 v1.5 adds 100+ random
  padding bytes to every encryption.
- WiRSa generates its key pair in RAM on the first relay-mode entry after each boot; the Palm
  pins nothing, so a new key is accepted silently. A `KEX_PUB` with the wrong `reqId`
  (left over from an abandoned attempt) is skipped. If anything arrives but the exchange
  can't complete (stray or corrupt bytes, `KEX_FAIL`, bad `KEX_OK`), the Palm flushes and
  restarts it with a new `reqId`, up to 3 times.
- Every other frame is encrypted: wire type = `type | 0x80`, wire length = body + 4, wire
  body = `uint32 seq` + AES-128-CTR(body). Counter block = `dir, 0, 0, 0, seq (BE), 0…0`
  (`dir` = `'P'` 0x50 Palm→WiRSa, `'W'` 0x57 WiRSa→Palm), 128-bit big-endian increment;
  each sender starts `seq` at 0 after the exchange and increments it per frame (retries
  included). The XOR checksum covers the bytes as sent. The receiver takes `seq` from the
  frame; the body-size limits apply to the decrypted body, so no existing cap changes.
- Strict on both sides: a plaintext frame is accepted only if it is a key-exchange type, and
  an encrypted one only once secured; neither side sends a non-exchange frame unsecured
  (so an old, unencrypted app or firmware gets no response).
- Palm results: WiRSa never answering keeps the existing "WiRSa did not respond…" message;
  any other failure shows **"Serial link cannot be secured!"** and ends the sync before any
  local data is touched; Cancel stays a silent cancel.

## Message types

| Byte | Name          | Direction     | Phase |
|------|---------------|---------------|-------|
| 0x01 | HELLO         | Palm → WiRSa  | 1     |
| 0x02 | HELLO_ACK     | WiRSa → Palm  | 1     |
| 0x03 | TRUST         | Palm → WiRSa  | 1     |
| 0x04 | TRUST_RESULT  | WiRSa → Palm  | 1     |
| 0x05 | SYNC          | Palm → WiRSa  | 1     |
| 0x0C | FOLDER        | WiRSa → Palm  | 1     |
| 0x0D | ORG           | WiRSa → Palm  | 1     |
| 0x06 | SYNC_ITEM     | WiRSa → Palm  | 1     |
| 0x07 | SYNC_DONE     | WiRSa → Palm  | 1     |
| 0x08 | SYNC_ERROR    | WiRSa → Palm  | 1     |
| 0x09 | KEX           | Palm → WiRSa  | 2     |
| 0x0A | KEX_ACK       | WiRSa → Palm  | 2     |
| 0x0B | BYE           | either        | 1     |
| 0x0E | ITEM_ACK      | Palm → WiRSa  | 1     |
| 0x0F | TWO_FACTOR_REQUIRED | WiRSa → Palm | 1 |
| 0x10 | TWO_FACTOR_SUBMIT   | Palm → WiRSa | 1 |
| 0x11 | SYNC_STATUS   | WiRSa → Palm  | 1     |

### `HELLO` (0x01) — body: `uint8 protocolVersion` (=1)
### `HELLO_ACK` (0x02) — body: `uint8 protocolVersion, uint8 capFlags, string fwVersion`
`capFlags` bit0 was reserved for the retired `KEX` plan and is always 0; wire encryption is
now mandatory and negotiated before `HELLO` (see "Serial-link encryption").

### `TRUST` (0x03) — body: `string serverURL`
WiRSa opens a TLS connection to `serverURL` with `WiFiClientSecure::setInsecure()` (no
validation — this call's entire purpose is to *discover* what the server presents, not to
authenticate anything) purely to read the certificate.

### `TRUST_RESULT` (0x04) — body:
`uint8 status` (0=ok, 1=dns_fail, 2=connect_fail, 3=tls_handshake_fail),
`bytes[32] sha256Fingerprint` (all-zero if status≠0),
`string subjectCN`, `string issuerCN` (empty if status≠0).
The Palm shows the fingerprint (hex) + CN fields in the "Trust this server?" prompt and, on
acceptance, stores `serverURL → fingerprint` locally (§ TOFU, `PrefsForm.c`/`Store.c`).

### `SYNC` (0x05) — body:
`string serverURL, bytes[32] pinnedFingerprint, string email, string masterPassword,
uint8 kdfType (0=unknown/ask-server, 1=PBKDF2-SHA256, 2=Argon2id), uint32 kdfIterations
(0=unknown), uint32 kdfMemoryKiB (Argon2 only), uint8 kdfParallelism (Argon2 only),
string rememberedTwoFactorToken (empty = none stored locally)`.
`rememberedTwoFactorToken` is whatever Bitwarden/Vaultwarden's own `TwoFactorToken` value
the Palm has cached from a previous successful 2FA-verified login (§ `TWO_FACTOR_REQUIRED`
below) — lets this sync skip the interactive 2FA challenge entirely, the same way every real
Bitwarden client behaves (a fresh code is only needed again once that token expires — the
server's own call, typically ~30 days — or the account's 2FA is reconfigured). Empty string
if the Palm has never completed a 2FA login for this account, or the last remembered token
was rejected and cleared. The Palm stores this encrypted at rest under the Sec35 shared key
alone (not this app's own local-vault key, which isn't necessarily unlocked yet at the point
this frame is built) — see `Store.c`'s `BwGetTwoFactorRemember`/`BwSetTwoFactorRemember`.
WiRSa: `WiFiClientSecure::verify(pinnedFingerprint, host)` before sending anything over the
connection (refuse to proceed if it doesn't match — this is the actual pin enforcement;
`TRUST`'s `setInsecure()` connection was discovery-only and is not reused for this). If
`kdfType==0`, WiRSa first hits Vaultwarden's own prelogin endpoint to learn the real KDF
params for this account, then proceeds. `masterPassword` exists in WiRSa RAM only for the
duration of this one request; never written to flash, never logged even at full
`usbDebug` verbosity.

### `FOLDER` (0x0C) — body: `uint16 folderIndex, string name`
Sent once per folder, before any `SYNC_ITEM` references it. `folderIndex` is a session-local
sequence number WiRSa assigns (0, 1, 2, ...) — **not** Bitwarden's real folder GUID (no
reason to burn wire bytes on a GUID the Palm has no use for in v1). Maps 1:1 onto a PalmOS
category the way Address35 already models categories.

### `ORG` (0x0D) — body: `uint16 orgIndex, string name`
Sent once per organization the account belongs to, before any `SYNC_ITEM` references it —
same session-local-index convention as `FOLDER` (not the real org GUID). Organization
*names* are not end-to-end encrypted in Bitwarden's own model (the server manages org
membership/metadata directly, unlike personal-vault fields) — WiRSa relays the name exactly
as `/api/sync` returns it, no decrypt step. The Palm still encrypts it at rest like
everything else in the local store (its own blanket "only the right master password"
requirement, independent of what Bitwarden itself chooses to encrypt upstream).

### `SYNC_ITEM` (0x06) — body:
`string itemGuid` (real Bitwarden cipher GUID — kept even though v1 is read-only, so a
future edit feature can `PUT /api/ciphers/{itemGuid}` without a protocol change),
`uint8 itemType` (1=login, 2=note, 3=card, 4=identity),
`uint16 folderIndex` (0xFFFF = none/unfiled, or N/A when the item belongs to an
organization — matches `FOLDER`'s indices),
`uint16 orgIndex` (0xFFFF = personal item, not organization-owned — matches `ORG`'s
indices; an item is either personal (`folderIndex` meaningful) or org-owned (`orgIndex`
meaningful), never both — Bitwarden's own model doesn't file org items into personal
folders), `uint8 archived` (1 if the cipher's own `archivedDate` is non-null — Bitwarden's
native per-item Archive feature, distinct from Trash: a trashed item, `deletedDate`
non-null, is skipped entirely by WiRSa and never reaches this frame at all, see
`bwProcessOneCipher`'s own `deletedDate` check),
`string name, string notes`, then a **type-specific tail** whose shape depends on
`itemType` (nothing else in the frame says how many strings follow — both sides derive it
from `itemType`, which is already known by this point in the frame). One frame per item;
WiRSa decrypts, sends, and discards — never buffers more than one item's plaintext at a
time (this is what makes the "never store" property hold even under the 128KB-ish RAM
budget of an ESP32-S3).

Earlier versions of this protocol reused four generic `username`/`password`/`uri`/`totp`
slots with type-appropriate meaning for every type, to avoid a wire format change. Real-
device feedback (comparing Card/Identity items against the live Bitwarden web vault) found
this lost most of what those two types actually carry, so the tail below now has one
dedicated string per real Bitwarden field instead — empty string for any field that's
absent, same as before. Field names/order below match Bitwarden's own Card/Identity object
shape (see the export-format field reference at
https://gist.github.com/ctrlcmdshft/fe6baead7be858ca08666f34da028163 for Login/Card/Identity
naming; that reference's own Identity field list is missing `company`/`ssn`/
`passportNumber`/`licenseNumber` even though the live product has them — confirmed directly
against the web vault UI, not just that document, so this protocol includes all four
anyway).

**`itemType` 1 (login) tail**: `string username, string password, string uri` (first URI
only, v1), `string totpSecret`.

**`itemType` 2 (secure note) tail**: none — `notes` above already covers it.

**`itemType` 3 (card) tail**: `string cardholderName, string brand, string number,
string expMonth, string expYear, string code`.

**`itemType` 4 (identity) tail**: `string title, string firstName, string middleName,
string lastName, string username, string company, string ssn, string passportNumber,
string licenseNumber, string email, string phone, string address1, string address2,
string address3, string city, string state, string postalCode, string country`.

**Flow control**: `FOLDER`, `ORG`, and `SYNC_ITEM` are each individually followed by the
Palm sending `ITEM_ACK` before WiRSa sends the next one — see below. Real-device testing
found WiRSa's original free-running burst (no wait between frames) overran the Palm's small,
fixed-size Serial Manager receive buffer once the Palm's own per-record `DmWrite`/`EncCrypt`
work fell behind the incoming byte rate, corrupting frames outright or, worse, silently
dropping an item onto the Palm's tolerant "unknown frame type" fallback with no error at all.

### `ITEM_ACK` (0x0E) — body: empty
Sent by the Palm immediately after the corresponding `FOLDER`/`ORG`/`SYNC_ITEM`'s record has
been durably written to the local store (i.e. after the matching `BwStoreAdd*` call returns,
never before) — so a lost `ITEM_ACK` on the wire never means "WiRSa believes this saved but
it didn't." WiRSa blocks on this ack after every `FOLDER`/`ORG`/`SYNC_ITEM` send before
sending the next one.
A missed/slow ack is retried **in place** on both sides, up to 5 attempts each, before either
side gives up on the whole sync — matching cadences so neither side's patience runs out well
before the other's (real-device report, 2026-09: a mismatch here left WiRSa still retrying an
entire `/api/sync` refetch long after the Palm had already given up and closed its port).
WiRSa: `bwWriteFrameAcked` resends the exact same frame and re-waits, no reconnect - see its
own comment. Palm: `SendItemAckWithRetry`/`RecvFrameWithRetry` (`Sync.c`), same shape. Once
either side's 5 attempts are exhausted, the sync is aborted outright — this is now always
terminal, not retried again via a whole-`/api/sync` refetch (that outer retry, still 3
attempts, is reserved for a genuine Vaultwarden-side stream stall/malformed-JSON condition, a
different failure domain where talking to Vaultwarden again can actually help).
Real-device report (2026-09): even with matching attempt counts, the Palm's own per-attempt
wait for the next frame defaulted to 180s (`bwIdleTimeoutTicks`, `Serial.c` - sized for
WiRSa's own slow PBKDF2+login before the very first reply of a sync) versus WiRSa's own ~10s
per attempt - a genuine mid-sync loss meant the Palm could sit showing its progress dialog for
up to 15 minutes after WiRSa had already given up and sent `SYNC_ERROR`. `gBwIdleOverrideTicks`
shortens every wait AFTER the first successful one in a sync to 12s, so the Palm's own total
patience (~60s across 5 attempts) closely tracks WiRSa's - only the very first wait of a sync
still gets the full 180s.

### `SYNC_DONE` (0x07) — body: `uint16 itemCount, uint16 folderCount, uint16 orgCount,
string updatedTwoFactorToken`
`updatedTwoFactorToken` is empty when there's nothing new to store (account has no 2FA, or
the server didn't renew the remembered token this time) — the Palm leaves whatever it
already has cached untouched in that case. When non-empty, the Palm replaces its stored
`rememberedTwoFactorToken` (see `SYNC`) with this value, encrypted at rest the same way.

### `SYNC_ERROR` (0x08) — body: `uint8 errorCode, string message, string updatedTwoFactorToken`
Codes: `1`=dns_fail, `2`=connect_fail, `3`=tls_fail, `4`=fingerprint_mismatch,
`5`=auth_failed (bad email/master password, or too many incorrect 2FA codes - see
`TWO_FACTOR_REQUIRED` below), `6`=two_factor_required (only used as the *error code value*
inside a `TWO_FACTOR_REQUIRED` frame's own bookkeeping on the WiRSa/mock side — it is never
actually sent to the Palm as a terminal `SYNC_ERROR` on its own; the real 2FA round trip is
the `TWO_FACTOR_REQUIRED`/`TWO_FACTOR_SUBMIT` exchange below, which either succeeds and the
sync continues, or exhausts its retry budget and THEN sends `SYNC_ERROR` with code `5`),
`7`=sync_fetch_failed, `8`=decrypt_failed (derived key didn't validate — near-certainly a
wrong master password, since `auth_failed` already covers the login-level check),
`9`=malformed_kdf_params. Sent instead of any `FOLDER`/`SYNC_ITEM`/`SYNC_DONE` sequence;
terminates the sync attempt.
`updatedTwoFactorToken` is the same field/"empty = no change" convention as `SYNC_DONE`'s own
— real-device finding (2026-09): Vaultwarden rotates (invalidates) the account's remember-
device token the moment a login succeeds, regardless of what happens afterward. Any
`SYNC_ERROR` reachable only *after* a successful login (codes `7`/`8` in practice — a
mid-progress fetch/stream failure, or a decrypt failure once already authenticated) still
carries whatever fresh token that login received, so the Palm doesn't keep retrying with an
already-dead old one on its next attempt. Codes reachable before login (`1`-`4`, `9`, and `5`/
`6` from a failed login itself) always send this empty, since there's nothing new yet.

### `TWO_FACTOR_REQUIRED` (0x0F) — body: `uint8 providerType, string providerLabel`
Sent instead of `SYNC_ERROR` when Vaultwarden's `/identity/connect/token` response indicates
the account has 2FA enabled (`TwoFactorProviders`/`TwoFactorProviders2` present in the 400
response body) — this can happen either on the very first login attempt, or again after an
incorrect code (Vaultwarden's own response shape for "wrong code" is the same as "2FA
required", so WiRSa/mock can't distinguish them without an extra heuristic and doesn't try
to; both look like "ask the Palm for a code" from this protocol's point of view).
`providerType` mirrors Bitwarden's own numeric provider IDs (`0`=Authenticator app/TOTP,
`1`=Email, others exist server-side — Yubikey, Duo, etc. — but aren't practically usable from
a headless relay and this implementation doesn't special-case them beyond passing the number
through). WiRSa/mock picks ONE provider automatically when the account has several available
(prefers `0`, then `1`, then whatever the server listed first) rather than asking the Palm to
choose — v1 doesn't have a "pick your 2FA method" UI. `providerLabel` is a short human string
for whatever the Palm should show next to the code-entry field (e.g. `"Authenticator app
code"`, or `"Email code sent to j...n@example.com"` when the server's own response includes
a masked email hint for provider `1`).

The sender (WiRSa/mock) keeps the TLS connection and derived master/stretched keys alive
across this whole exchange — it's a pause in the middle of one logical sync attempt, not a
new attempt. After sending this frame it blocks waiting for `TWO_FACTOR_SUBMIT` with a
longer idle timeout than the normal inter-frame one (WiRSa: 180s, matching the Palm's own
`bwIdleTimeoutTicks` — see `Serial.c` — since this specific wait is for a human to check
their phone/email and type a code, not for WiRSa's own processing). A timeout, or any frame
other than `TWO_FACTOR_SUBMIT`/`BYE`, is treated as a failed sync attempt
(`SYNC_ERROR` code `5`). Retried up to 3 times (wrong code → another `TWO_FACTOR_REQUIRED`)
before giving up.

**Remember-this-device**: if `SYNC`'s own `rememberedTwoFactorToken` was non-empty, the
*first* login attempt of the whole sync already includes it as `twoFactorToken` with
`twoFactorProvider=5` (Bitwarden's own numeric ID for the "Remember" pseudo-provider) —
`TWO_FACTOR_REQUIRED` is only sent at all if that attempt was rejected (token expired/
invalid) or none was available, i.e. a real client-visible interactive prompt is the
exception now, not the default, for an account that's already completed 2FA once from this
Palm. Every token-exchange request (whether using a remembered token or a freshly-typed
code) now sends `twoFactorRemember=1`, so a successful login — by either path — often
gets a (possibly renewed) `TwoFactorToken` back from the server; see `SYNC_DONE`'s own
`updatedTwoFactorToken` field for how that gets back to the Palm to store for next time.
(Real bug fixed 2026-09, found by comparing against Vaultwarden's own source: this field is
`Option<i32>` server-side, not a bool — sending the string `true` silently failed to parse
and was treated as absent, so `TwoFactorToken` was never actually being requested despite
looking like it was; that's the entire explanation for it never coming back.)

### `TWO_FACTOR_SUBMIT` (0x10) — body: `string code`
Palm's reply to `TWO_FACTOR_REQUIRED` — whatever the user typed (the 6-digit TOTP code, the
code emailed to them, etc.), verbatim, no local validation. Sent from `BwTwoFactorPromptRun`
(`Dialogs.c`), a plain (non-masked — a 2FA code isn't as sensitive as a password, and Show/
Hide masking has no purpose the user would care about here) modal text entry, same shape as
`BwUnlockPromptRun`/`BwTrustPromptRun`. If the user cancels this prompt, the Palm sends `BYE`
instead and aborts the sync locally (no `SYNC_ERROR` needed for a user-initiated cancel).

### `SYNC_STATUS` (0x11) — body: `string statusText`

Purely cosmetic live status-label update, WiRSa → Palm, sent zero or more times between
`SYNC` and the first `FOLDER`/`ORG`/`SYNC_ITEM`/`SYNC_ERROR`/`TWO_FACTOR_REQUIRED` frame —
covers the one stretch (prelogin → PBKDF2 → login → `/api/sync` fetch) that used to have no
intermediate feedback at all. The Palm just displays whatever text arrives (e.g.
`"Iterating..."` right before WiRSa starts the PBKDF2-SHA256 derivation, `"Fetching
vault..."` again once it's done) and otherwise ignores the frame — in particular it does
**not** shorten its receive-timeout override the way a real data frame does (see `Sync.c`'s
own `gBwIdleOverrideTicks` handling), since a status ping doesn't mean data is now flowing
steadily; the wait for whatever WiRSa sends next can still legitimately be as long as the
un-shortened default.

### `KEX` (0x09) / `KEX_ACK` (0x0A) — retired
The original Phase 2 X25519 plan; never implemented. Superseded by the shared serial-link
encryption above (types 0x70–0x74). WiRSa still logs and ignores 0x09.

### `BYE` (0x0B) — body: empty
Either side may send this to close cleanly; the other should stop expecting further frames
and release the port.

## Debug logging (WiRSa `usbDebug`)

Follows the existing convention used throughout WiRSa (e.g. lazyFTP's own
`"> PASS ****"` precedent). At full `usbDebug` verbosity, log frame type/length/timing and
high-level flow (`"BW: TRUST -> <host>"`, `"BW: SYNC_ITEM #14 sent"`), but
**never** log `masterPassword`, decrypted `password`/`notes`/`totpSecret` field contents, or
derived key material — only field *names* and lengths where useful for troubleshooting
(`"BW: item 'GitHub' username=8 chars password=16 chars"`).

## Open items carried into implementation

- **2FA on the Vaultwarden login itself** (distinct from per-item TOTP) is now handled via
  `TWO_FACTOR_REQUIRED`/`TWO_FACTOR_SUBMIT` (see above) - implemented against the documented
  shape of Vaultwarden's `/identity/connect/token` 400 response, but the exact response shape
  for an INCORRECT code (as opposed to no code at all) was not verified against a live
  server while implementing this - confirm on real hardware. Only Authenticator app (TOTP)
  and Email providers get a friendly label; other provider types (Yubikey, Duo, ...) still
  work mechanically (the provider number is just passed through) but show a generic label.
- **KDF discovery**: whether WiRSa should always prelogin-fetch KDF params server-side
  (simplest, one extra round trip) vs. the Palm caching a known account's params locally —
  defaulting to always-fetch (`kdfType=0`) for v1 since it's simpler and this app has no
  local per-account state to invalidate.
