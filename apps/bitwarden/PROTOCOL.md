# BitWarden Relay — the Palm side of the wire protocol

**The wire protocol is no longer defined here.** The framing, the serial-link encryption, the session rules and every BitWarden frame with its body are specified in one
place, in the WiRSa repository: `docs/protocol/Protocol-wire.md` (sections 2 to 7 for the common layer, section 11 for the BitWarden frames). Change a frame in the
firmware, in this app and in that document together; this file does not repeat them.

What follows is what this app does with the frames, and the notes that belong to the app rather than to the wire.

## Where the code is

| Part | File |
|---|---|
| Framing, secure link (shared with CalDAV Sync and the others) | `apps/lib/relayproto.c`, `relaysec.c`, `relaywire.c`; BitWarden's `Serial.c` keeps only its poll hook |
| Frame type numbers | `Sync.c` (`BW_*`), must match section 11 of the protocol document |
| The sync: sending `SYNC`, storing items, acknowledging | `Sync.c` |
| Trust prompt, two-factor prompt, unlock prompt | `Dialogs.c` (`BwTrustPromptRun`, `BwTwoFactorPromptRun`, `BwUnlockPromptRun`) |
| Local vault, trusted fingerprints, remembered two-factor token | `Store.c`, `PrefsForm.c` |
| At-rest encryption of the local vault | `enccore.c` (independent of the wire) |
| Test harness | `tools/mock_wirsa.py` |

## What the app does

- **Two independent crypto layers.** The wire is encrypted by the shared serial-link layer, mandatory since 2026-09-30 and shared with CalDAV Sync. The local `BitWardenDB.pdb` is encrypted at rest by
  `enccore.c`. They share nothing.
- **Trust on first use.** The app sends `TRUST`, shows the fingerprint and names from `TRUST_RESULT` in the "Trust this server?" prompt, and on acceptance stores `serverURL -> fingerprint`
  (`PrefsForm.c`, `Store.c`). That fingerprint goes into every `SYNC`.
- **Remembered two-factor token.** The token from `SYNC_DONE` (or a late `ERR`) is kept encrypted at rest under the Sec35 shared key alone, not the app's own local-vault key (which may not be
  unlocked yet when `SYNC` is built): `BwGetTwoFactorRemember`, `BwSetTwoFactorRemember` in `Store.c`. Empty in a reply means keep what is stored.
- **Two-factor prompt.** `TWO_FACTOR_REQUIRED` opens `BwTwoFactorPromptRun`, a plain (not masked) modal entry; what the user types goes into `TWO_FACTOR_SUBMIT` verbatim, with no local validation.
  Cancel sends `BYE` and ends the sync locally, with no error alert.
- **Acknowledging.** `ACK` is sent after the record is written to the local store, never before, so a lost ack never means WiRSa thinks something is saved that is not. It retries its ack send and its
  wait for the next frame up to 5 times each (`SendItemAckWithRetry`, `RecvFrameWithRetry` in `Sync.c`). Timings are in the protocol document, 5.2.
- **Status lines.** `SYNC_STATUS` text is shown as it arrives. During a status line the app keeps its long wait for the next frame (`gBwIdleOverrideTicks` is not shortened), because a status line does
  not mean data is flowing steadily.
- **Categories.** A folder frame becomes a Palm category, the way Address35 models categories.
- **Test harness.** `tools/mock_wirsa.py` predates the serial-link encryption (2026-09-30) and does not implement the key exchange, so current builds stop at "Serial link cannot be secured!"
  against it. It needs the exchange added before it can be used again.

## Open items

- The response shape of Vaultwarden's `/identity/connect/token` for an **incorrect** two-factor code (as opposed to no code at all) was not checked against a live server; confirm on real hardware.
  Only Authenticator app and Email get a friendly label; other provider types work mechanically with a generic label.
- Argon2id accounts are refused by the firmware (ERR code 9); the app always asks WiRSa to discover the key-derivation settings (`kdfType` 0).
