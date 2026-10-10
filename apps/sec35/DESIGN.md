# Sec35 — suite-wide at-rest encryption authority (design)

Status: **design only, not built.** Centralises passcode/TTL/key management for the
three era PIM apps (ToDo35, Datebook35, Address35). CryptoNote is out of scope — it
already has its own AES container.

---

## 1. Goal & threat model

Encrypt the **sensitive payload** of the "35" DBs at rest, keyed by `passcode + device
serial`, unlocked once per **TTL window**.

- **Covered:** a stolen HotSync backup / SD card / raw `.pdb` can't be read on another
  machine; a lost device **past the TTL window** can't be decrypted even knowing the
  serial, because the passcode is required and is never stored persistently.
- **NOT covered:** an *unlocked* device (key is live in RAM), chip-off flash extraction
  *during* the window, or a hostile app on the device reading the shared key (PalmOS has
  no secure enclave). These are accepted limits.

---

## 2. Architecture decisions (what we agreed)

1. **Passcode + serial**, device-bound; key = `KDF(passcode, SysGetROMToken(snum), salt,
   rounds)`. Reuse CryptoNote's KDF/AES; crank `rounds` hard (the passcode is the only
   real secret once the serial is assumed known → offline brute-force is the threat).
2. **Key held in memory, never in a file.** Cached in a **Feature-Manager chunk**
   (`FtrPtrNew`) → survives app relaunch + sleep/power-off, shared across apps, and
   **never lands in a HotSync backup**. Wiped on reset (→ re-prompt).
3. **TTL-cached unlock.** Unlock once; the key lives for X (pref, minutes…days). The
   **launch-time TTL check is authoritative** (compare `now` vs stored unlock timestamp;
   also wipe if `now < unlockTimestamp`, i.e. clock rolled back). `AlmSetAlarm`
   proactively wipes a dormant lost device even if no app is launched.
4. **Field-level encryption** (payload encrypted, sort/index keys clear) so native
   Data-Manager sort, category filtering, and Find keep working — see §7.
5. **Split of duties:** **Sec35** = policy + UI + prompt + key-cache + alarm; **`lib/
   enccore`** = the shared cipher + Feature-key API, linked *in-process* into every app.
   Per-record crypto is NEVER an inter-app call (too slow); only the *key* is shared.
6. **Hard block:** if a DB is marked encrypted and **Sec35 is not installed** → the app
   alerts "cannot decrypt" and exits.
7. **Change / remove passcode** re-crypts all data (slow, rare) behind a **progress
   bar**. Removing the passcode decrypts everything → Sec35 becomes removable and the
   apps launch bare (as today).

---

## 3. Components

### Sec35 (small app, own creator e.g. `Sec3`)
- Set / change / remove passcode UI; TTL configuration; "encryption enabled" flag.
- Owns the **KDF salt/rounds + a verifier** (to validate an entered code) in its AppInfo/
  prefs, and owns a tiny **alarm DB** (something for `AlmSetAlarm` to hang on).
- The **unlock prompt**: when an app is locked it hands off to Sec35 (see §6); Sec35
  prompts, derives the key, caches it in the Feature, returns.
- **Launch-time TTL enforcement + wipe**, and the `sysAppLaunchCmdAlarmTriggered`,
  `sysNotifyForgotPasswordEvent` ('bozo') handlers.
- Orchestrates enable/disable/rekey by signalling each app (each app migrates its own
  fields with its own progress bar). *As built, Sec35 re-keys every app's database itself
  (`sec35.c`, resumable if interrupted) — see §6.*

### `lib/enccore.{c,h}` (shared — linked into Sec35 + all three apps)
- `KDF`, `AES-CTR` buffer encrypt/decrypt (from CryptoNote's `aes.c`).
- The **shared Feature key** get / TTL-check / wipe, and the shared creator/featureNum
  constants (so every binary agrees).
- This is the whole "encrypt/decrypt method" the apps see.

### Per-app hooks
Each app links `enccore` and adds crypto at its **existing pack/unpack chokepoint**
(§7) plus, if it encrypts a list-displayed field, at that view's draw.

---

## 4. Key lifecycle

```
           set passcode (Sec35)                     no passcode
  [BARE] ───────────────────────────► [ENABLED, LOCKED] ◄──────────── [BARE]
                                          │   ▲                          ▲
              app launch / locked         │   │ TTL expired,             │ remove
              → SysUIAppSwitch(Sec35)      │   │ clock rollback,          │ passcode
              → prompt → KDF → FtrPtrNew   ▼   │ AlmAlarm, sleep(opt),    │ (decrypt all)
                                     [UNLOCKED]─┘ 'bozo' → FtrPtrFree ────┘
```

- **Unlocked** = key `{key, unlockTimestamp}` present in the Feature chunk.
- Every app, every launch: `EncTryGetKey()` → TTL-checked `FtrGet`; if expired/rolled
  back → wipe + treat as locked; if locked and DB encrypted → hand off to Sec35.
- **`'bozo'` (forgot-password)** → the OS clears the password and deletes *private*
  records; our encrypted data isn't flagged private, so Sec35 must **drop/reset** the
  encrypted store (key is unrecoverable) or the apps are left holding undecryptable data.

---

## 5. `lib/enccore.h` sketch (interfaces only)

```c
/* shared constants -- MUST match in every binary */
#define ENC_CREATOR      'Sec3'
#define ENC_KEY_FTR      1000        /* FtrPtrNew: {UInt8 key[16]; UInt32 unlockTime} */
#define ENC_KEY_BYTES    16

Boolean EncEnabled(void);                       /* a DB/pref flag says "encrypted"     */
Boolean EncTryGetKey(UInt8 keyOut[ENC_KEY_BYTES]);  /* false if locked/expired (wipes) */
void    EncCrypt(const UInt8 key[16], UInt8* buf, UInt16 len, UInt32 nonceLo, UInt32 nonceHi);
                                                 /* AES-128-CTR in place, size-preserving */
/* key derivation + cache are Sec35-side: */
Err     EncDeriveAndCache(const char* passcode); /* KDF(pc,serial,salt,rounds) -> Ftr    */
void    EncWipe(void);                           /* FtrPtrFree + zero                     */
```

Nonce per encrypted field = `(recordUniqueID, fieldTag)` → seekable, unique, nothing
extra stored. CTR keeps the field the same length ⇒ **no on-disk format change**.

---

## 6. Inter-app contract (prompt & rekey)

Use a **`SysUIAppSwitch` round-trip** (the GoTo pattern already used for Agenda→ToDo and
birthday→Address). Unlocking is at most once per window, so the switch cost is
irrelevant, and Sec35 prompts as a normal app with its own UI/resources (far cleaner
than a dialog from a sublaunch).

```c
#define kSecLaunchUnlock   0x9000   /* no param block - see below */
#define kSecReturnFtr      1001     /* Ftr(ENC_CREATOR 'Sec3', 1001) = return-to creator */
```

As built (`sec35/enccore.h`): no cross-app parameter chunk (that would need a cross-app
`MemPtrFree`); the return-to creator travels in a feature instead.

- Locked app: save state → `FtrSet('Sec3', kSecReturnFtr, myCreator)` →
  `SysUIAppSwitch(Sec35, kSecLaunchUnlock, NULL)`.
- Sec35: reads and unregisters the feature, prompts → `EncDeriveAndCache` →
  `SysUIAppSwitch` back to that creator.
- Callers: every encrypted app's launch gate, plus Phone Lookup (`apps/lib/a35lookup.c`,
  `Address35.md` §10), which returns to whichever app (Datebook35/ToDo35) asked.
- App relaunches, `EncTryGetKey` now succeeds, proceeds.
- The re-key launch code (`kSecLaunchRekey`) from the original design was not built; Sec35
  re-encrypts every app's database itself on a password change.
- **Sec35 missing while a DB is encrypted** → app shows the "cannot decrypt" alert and
  exits (no fallback prompt — password logic must not leak back into the apps).

---

## 7. Field map — encrypted vs clear (the deliverable)

Rule: **sort/index keys, structural fields, and category bits stay CLEAR** (so native
sort / occurrence math / category filter keep working); **payload is ENCRYPTED**.
"Tier 1" = minimal (payload only). "Tier 2" = also seal the primary list text (adds a
decrypt at that view's draw; the text shows only when unlocked, masked when locked).

### ToDo35 — hook at `ToDoDB.c` pack/unpack (`ToDoNewRecord`/`ToDoChangeRecord`/`GetToDoNotePtr`); comparator `ToDoCompareRecords`
| Field | Disposition | Why |
|---|---|---|
| `dueDate` | **CLEAR** | sort key (`ToDoCompareRecords`) + list column |
| `priority` | **CLEAR** | sort key + list column |
| category (DM attr) | **CLEAR** | metadata; drives category cycle/filter |
| `description` | CLEAR (T1) / **ENC** (T2) | shown in list via the load proc — T2 ⇒ list decrypts on unlock, masked when locked |
| `note` | **ENCRYPT** | payload, only reached via NoteView/unpack |

### Datebook35 — hook at `ApptGetRecord` / `ApptNewRecord` / `ApptChangeRecord`; sort by date/time
| Field | Disposition | Why |
|---|---|---|
| `when` (date/time) | **CLEAR** | sort key + occurrence math + display |
| `repeat`, `exceptions` | **CLEAR** | structural, read raw when generating occurrences |
| `description` (title) | CLEAR (T1) / **ENC** (T2) | drawn in day/week/month/agenda views |
| `note` | **ENCRYPT** | payload |
| category (DM attr) | **CLEAR** | metadata |

### Address35 — hook at `A35GetRecord` (decrypt each field value after unpack) + the packer; sort by name (`a35fFirst`/`a35fCompany` per surname pref)
| Field(s) | Disposition | Why |
|---|---|---|
| `a35fFirst`, `a35fCompany` (surname per pref) | **CLEAR** | sort key + list display |
| `a35fMiddle`, `a35fNickname`, `a35fTitle` | CLEAR (T1) / ENC (T2) | shown on the record line — choice |
| `a35fTag` (tagId) | **CLEAR** | used for tag filtering |
| `a35fPhone`,`Email`,`IM`,`URL`,`Address`,`Relation`,`Note`,`Custom` | **ENCRYPT** | payload, all via `A35GetRecord` |
| `a35fBirthday` | **ENC**, but see cross-app note | payload — feeds Datebook agenda |
| category (DM attr) | **CLEAR** | metadata |

Because Address is a clean single-chokepoint TLV store, you can encrypt **per field
value by field type** generically inside `A35GetRecord`/the packer — the cleanest of the
three.

### Cross-app reads (important)
The era apps read *each other's* data, so an encrypted field that another app reads means
that reader must also decrypt (it links `enccore`, holds the shared key — fine, but it's
another hook site):
- **Datebook35 day-view/agenda reads ToDo35** (`DayInjectTodos`, `gDayTodoDesc`) → if ToDo
  `description` is encrypted (T2), this reader must `EncDecrypt` it.
- **Datebook35 agenda reads Address35 birthdays** → if `a35fBirthday` is encrypted, the
  agenda birthday reader must decrypt it (or keep birthday clear).

---

## 8. Migration / rekey

- A per-DB **key "generation" marker** in AppInfo (which key a DB is under) so a crash
  mid-rekey is recoverable and you never double-encrypt.
- Enable / change / remove is driven by Sec35 (`kSecLaunchRekey`); each app walks its own
  records through its chokepoint with a **progress bar**. CTR is size-preserving, so
  records don't move; only bytes change (+ the generation marker flips).

---

## 9. Open questions / risks

- **Passcode strength** is everything post-window; enforce a minimum + heavy KDF rounds
  (offline brute-force of ciphertext+serial is the real attack).
- **Tier 1 vs Tier 2 per app** — decide whether the primary list text (todo/appt title,
  contact secondary lines) is sealed (masked-when-locked) or left visible in a backup.
- **Birthday** clear-vs-encrypted trades agenda simplicity against leaking DOBs.
- **Sec35 as a hard dependency** when enabled — block deleting Sec35 while encryption is
  on, or accept the "install Sec35 to read your data" recovery story.
- Feature key is readable by any app that knows `ENC_CREATOR/ENC_KEY_FTR` — obfuscate at
  most; it's a speed-bump, not a barrier.

---

## 10. Implementation notes (post-launch, not in the original design)

- **Numeric keypad tap-click sound** (`sec35.c`, 2026-09) — `frmKeypad`'s 10 digit/Del keys
  are GADGETs (`KeypadHitTest` hand-tests raw pen coordinates in
  `penDownEvent`/`penUpEvent`; see `KeypadFormHandleEvent`), so unlike a real Control they
  never reached the Control Manager's own automatic tap click. Fixed by calling
  `SndPlaySystemSound(sndClick)` right where the visual press-invert already happens (both
  digits and Del, via the same `if (key != kKeyNone)` branch). `SndPlaySystemSound` already
  self-gates on the System Sound preference internally (unlike the raw `SndDoCmd`), so no
  extra preference check was needed here. BitWarden's own 2FA numeric keypad
  (`bitwarden/Dialogs.c`, `BwTwoFactorKeypadRun`) got the identical fix in the same pass —
  see `Bitwarden.md` §6.

---

*Related suite docs: `apps/ToDo35.md`, `apps/Datebook35.md`, `apps/Address35.md`,
`apps/CryptoNote.md` (reusable AES/KDF).*
