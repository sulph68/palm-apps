# CryptoNote secondary database, read-only memos and Memo Info - plan

Status: **built and tested in POSE 2026-11-19, uncommitted** (branch `cryptonote-secondary-db`). Code: SecDb.c
(sec003), ViewList.c and DbForms.c (sec004). Differences from the plan below: the dates are an 8-byte plain
**trailer** at the end of the record (marker bit 0x04), not a header, and stay outside the Sec35 wrap; the keying is
two marker bits (0x08 user database, 0x10 database key), with no keyMode byte or device check, so a memo from
another Palm reads as "Incorrect password"; global Find now also searches Main (it used to need a global password)
and the user databases; Beam Category still sends Main and Memo Pad only. Not testable in POSE: the device-ID path
(both ROMs report no serial) and a real JackFlash ROM (tested with a test-build switch, `-DROMSIM`, that treats
`CryptoNoteDB_ROM*` as ROM). Revised 2026-11-19 with the user's answers.

## Goal

- **Move a memo** (plain or encrypted) between CryptoNote's main database and a **secondary database**. The user can
  move the secondary database into flash ROM with JackFlash, so its memos survive a RAM wipe.
- **One list:** CryptoNote shows the memos of all databases as one list, in the user's sort order, and opens and
  decrypts them as usual.
- **Read-only:** any memo can be marked read-only; memos in ROM always are.
- **Memo Info:** a per-memo form shows and changes the memo's category, database, private and read-only flags, how it
  is protected, and when it was created, modified and last opened.

## Decisions so far (user)

- Each memo is plain or has its own password, in either database. Plain and encrypted memos can both move.
- Moved into the secondary database, an encrypted memo is protected by **its password + the device ID only**: no
  Sec35 key, no global password.
- The first line (title) stays readable.
- **The list keeps the user's order**, alphabetical or manual, across both databases.
- Two markers:
  - a **database icon** (7x9, like the key) for a memo in the secondary database;
  - a **read-only icon** (no-entry sign, 7x9) for a read-only memo, and a **chip** for a memo in ROM.
- The device-key fallback must stay transparent, no extra steps for the user.
- **Security** shows "Device ID available" / "Device ID not available".

## Why main-database encryption cannot go to ROM as it is

| Layer | Key comes from | Problem in ROM |
|---|---|---|
| Sec35 wrap (every record) | Sec35 password + device serial + Sec35 salt | The salt is in Sec35's prefs (RAM), lost on a hard reset. A Sec35 password change re-encrypts every record (`CryptoRekey`), which a ROM database cannot take. |
| Memo password | memo password + global password (if set) + device secret + record salt | Without a serial, the device secret is a random key in `CryptoNoteDevKey` (RAM, not backed up). |

## Secondary-database protection

- **With a device ID** (ROM serial token `sysROMTokenSnum`): key = KDF(memo password + serial + record salt). The
  serial is in ROM, so a RAM wipe cannot lose it. Shown as "Password + this device".
- **Without a device ID:** the secondary database carries its own random 16-byte key in its AppInfo, created with the
  database.
  - It plays the part of the device ID: key = KDF(memo password + that key + record salt).
  - It travels with the database into ROM, so nothing is lost on a RAM wipe and the user does nothing.
  - Anyone who copies the database file has that key too, so the memo is really protected by its password alone. It
    is shown honestly as "Password only".
  - The main database's `CryptoNoteDevKey` fallback is unchanged.

## Record format

A record's leading marker byte (outside any encryption) is extended. Today it is `0x80` (marked) with bit `0x01`
(Sec35-wrapped).

| Bit | Meaning |
|---|---|
| `0x01` | Sec35-wrapped (main database only) |
| `0x02` | **read-only** (set by the user; memos in ROM are read-only regardless) |
| `0x04` | **info header follows:** created, modified (2 x UInt32 Palm seconds, 8 bytes) |
| `0x08` | **secondary format:** `keyMode` byte (0 plain, 1 password + device ID, 2 password + database key) and a 4-byte check of the device secret, so "encrypted on another Palm" is told apart from "wrong password" |

- After that come the existing `PackedRecordType` bytes (Sec35-wrapped or not, in the main database).
- All the `raw + 1` reads go through one helper that skips the header.
- Records without bits `0x04`/`0x08` read exactly as now. A record gains the info header the first time it is written.
- An older CryptoNote build cannot read records that have the new bits (no downgrade).
- The info header and read-only bit are outside the encryption, so the icons and dates show even while Sec35 is
  locked. The cost: the dates are visible to anyone with the database file (accepted by the user, 2026-11-19).

## Memo Details form (kept, enlarged)

The existing **Memo Details** form (Details button) stays and becomes a **full-screen modal with border**
(AT 2 2 156 156, like the picker style). It gains a Database pop-up, a Read-only checkbox, a Protection line and two
date lines (no Accessed: user, 2026-11-19). Mock-up shot in POSE on 2026-11-19 (flow storyboard):

```
     Category: [v Unfiled]                     y 19
     Database: [v Main]                        y 36   list: Main, CryptoNoteDB_* suffixes, (ROM) ones, Create New...
      Private: [ ]   Read-only: [ ]            y 53
   Protection: Sec35 + password                y 70
      Created: 11/19/26  9:40 pm               y 87
     Modified: 11/19/26 10:02 pm               y 104
                                               (one blank row, user 2026-11-19)
 (OK) (Cancel)               (Delete...)       y 139 (OK 5, Cancel 48, Delete 111)
```

- Six rows on an even 17 px pitch, then one blank row of the same pitch above the buttons (user, 2026-11-19). The buttons are 5 px above the bottom edge.
- 5 px side margins: bold labels right-aligned to x 64, values at x 67. The Read-only box and the longest value end
  5 px from the right frame. Delete... sits 5 px from the right edge.
- "Password + this device" did not fit inside the margins, so that state reads "Password + this Palm".

- **Protection line:**

  | Database | Plain memo | Memo with password |
  |---|---|---|
  | Main, Sec35 password set | "Sec35 (this device)" | "Sec35 + password" |
  | Main, no Sec35 password | "Not encrypted" | "Password + this Palm" |
  | Secondary, with device ID | "Not encrypted" | "Password + this Palm" |
  | Secondary, no device ID | "Not encrypted" | "Password only" |

- **Database popup:** changing it moves the memo on OK.
  - It is re-encrypted for the target. The memo's password is already known, because the memo is open.
  - Moving to Main re-applies the global password (if set) and the Sec35 wrap.
  - Plain memos move as they are.
- **Read-only:** ticking it blocks editing and Delete until it is unticked.
- **A memo in ROM:** the Database popup and Read-only are disabled (Read-only shows ticked, "(in ROM)"). Category,
  Private and Delete are refused with "This memo is in ROM and cannot be changed".
- **Dates:**
  - Created: set at New, import and move (a move keeps it).
  - Modified: set when the text is saved changed.
  - Memos from before this feature show "Unknown" until written.

## The list

- **Built as one view index:** (source, record) entries for MemoDB (if migration was declined), the main database and
  every secondary database, filtered by category and Find.
- **Alphabetical:**
  - Entries are sorted by title. Titles are read with the cheap title peek and cached briefly (first 16 characters per
    entry).
  - This also fixes today's known limitation: Sec35-wrapped records are currently "sorted" by ciphertext.
  - While Sec35 is locked, main titles are unknown, so the last stored order is used.
- **Manual:**
  - The order is kept in a small RAM database (`CryptoNoteOrder`): a list of (database id, unique ID). The database id
    is its creation date, which stays the same in ROM.
  - Dragging reorders that list, never the databases, so ROM memos can be reordered too.
  - New memos go to the top or bottom as the "move on top" pref says.
  - Missing entries (for example after a RAM wipe) follow in database order.
- **Icons** packed to the right with no gaps (user, 2026-11-19), in the order key (memo password), read-only,
  database (secondary): the first present icon at x 144, the next at 136, then 128. Read-only icon: a no-entry sign,
  filled 7x7 disc with the bar knocked out (user chose N1, 2026-11-19; pencil and padlock rejected, the padlock can be
  mistaken for encryption).
  ```
  .......
  ..###..
  .#####.
  #######
  #.....#
  #######
  .#####.
  ..###..
  .......
  ``` ROM
  implies read-only, but read-only does not imply ROM, so a memo in ROM shows the chip **instead of** the no-entry sign
  and the database icon (user, 2026-11-19). Each is 7x9 with a 1 px
  gap; the title is cut to make room.
- Scrolling and selection work on the view index instead of `topRecord` / `topDB`.

## Secondary databases

- **Named by the user** (user, 2026-11-19): `CryptoNoteDB_<suffix>`, made with **Create New...** in the Database
  pop-up. The prefix is 13 characters, so the suffix can be up to 18 (Palm's 31-character limit).
- **Found by type and creator** (`'CNsc'` / `'CrNt'`), so a database still shows if it is renamed outside CryptoNote.
  The pop-up shows the suffix ("Recipes"), or the full name if the prefix is missing.
- **Database pop-up** in Memo Details:
  ```
  Main                <- CryptoNoteDB
  Recipes             <- CryptoNoteDB_Recipes (RAM)
  Vault (ROM)         <- CryptoNoteDB_Vault, in ROM: shown, cannot be chosen as a move target
  Create New...
  ```
  Main first, then the others in name order, then Create New.... Choosing a database moves the memo there on OK.
- **Create New...** opens a small dialog: `Name: CryptoNoteDB_[________]`, OK / Cancel. It refuses an empty name, a
  name already used in RAM **or ROM** (checked by walking every database, because a RAM database with a ROM
  database's name hides the ROM one), and characters Palm names cannot take. OK creates the empty database (AppInfo
  with categories and its own key) and selects it in the pop-up.
- **In ROM:** detected from the heap flags (`memHeapFlagReadOnly`), opened read-only, every write refused. Copied back
  to RAM, it is opened read-write and behaves normally.
- **AppInfo:** category names (memos are matched to the main database's categories by name; unknown names show under
  Unfiled) and the database key used when there is no device ID.
- The backup bit is set while in RAM. A restore to another Palm decrypts only "Password only" memos.

## Options > Manage Databases

A new **Options > Manage Databases** menu item opens a full-screen form (AT 2 2 156 156) to create, rename and
delete databases. Mock-up shot in POSE on 2026-11-19:

```
 +------------------------------------+
 | Main                               |   list (5,17,146,66), 6 rows
 |#Recipes############################|   suffix only; "(ROM)" marks ROM ones
 | Travel                             |
 | Vault (ROM)                        |
 +------------------------------------+
         Memos: 12                         y 93
          Size: 4.2 KB                     y 105
      Location: RAM                        y 117
 (Done) (New) (Rename) (Delete...)         y 139: x 5/30, 39/26, 69/38, 111/40
```

- The info rows show the selected database. The block is centred between the list and the buttons (12 px clear on
  each side). Bold labels end at x 64, values start at x 67, as in Memo Details.
- The buttons are named **New** and **Rename** without "..." because four buttons with "..." do not fit in 146 px.
  Delete... keeps it and sits 5 px from the right edge, as in Memo Details.
- **New / Rename dialog** (also used by Create New... in the Database pop-up), mocked 2026-11-19: a child modal
  AT (2 101 156 57) drawn over its parent (Manage Databases, or Memo Details for Create New...), title
  "New Database" / "Rename Database", one row: bold "New Suffix:" (5,20) and the suffix field (65,20,86,12,
  underlined, max 18 characters); OK / Cancel at y 40; Graffiti indicator (143,41). Rename pre-fills the current suffix. The database keeps its
  creation date on rename, so the manual-order table still matches its memos.
- **Delete...** (user, 2026-11-19): deletes the database **with its memos** after a confirmation alert, ROM
  CONFIRMATION style: title "Delete Database", "Delete CryptoNoteDB_Recipes and its 12 memos? This cannot be
  undone.", buttons **Confirm** / **Cancel**. The name and memo count are filled in.
- Options menu order: Font..., Preferences..., Security..., **Manage Databases...** (no shortcut), About CryptoNote.
- **Main** cannot be renamed or deleted. A database **in ROM** cannot be renamed or deleted ("This database is in
  ROM"); JackFlash moves it out.

## Security...

**Security** opens a CryptoNote form that **contains the Palm's own privacy choice** instead of launching the ROM
dialog (user, 2026-11-19: no second dialog). Modal AT (2 84 156 74), mocked in POSE:

```
   Device ID: Available            y 20   ("Not available" without a device ID)
     Privacy: [v Show Records]     y 36   Show / Mask / Hide Records, as the ROM "Change Security" dialog
 (OK) (Cancel)                     y 57
```

- Bold labels end at x 57; the value and the pop-up start at x 62 / 59.
- OK applies the privacy choice like the ROM dialog does: it writes `prefShowPrivateRecords`, and changing to Show
  from Mask or Hide first asks for the Palm password (`PwdVerify`) if one is set. The list then redraws.
- `SecSelectViewStatus()` is no longer called.

## Other code paths

- **Global Find and GoTo:** all databases (GoTo identifies the database by its id).
- **Beam Memo:** any memo (decrypted first, as now). **Beam Category:** plain memos of all databases.
- **Encrypt/Decrypt Category:** acts on writable memos of all databases; skips read-only and ROM ones.
- **CryptoRekey:** main database only. Secondary memos have no Sec35 wrap.
- **Launch maintenance** (format migration, MemoDB import): main database only. Card mode is unchanged.

## Phases (each testable and committable)

1. **Record format and Memo Info in the main database:** marker bits, info header, payload helper; read-only flag
   (editor field read-only via the too-large guard's mechanism; Delete refused); dates; protection line; the Memo
   Info form; the read-only icon.
2. **The view-index list:** combined sources, alphabetical by title (fixes the wrapped-record sort), manual order
   table, scrolling and dragging on it.
3. **Secondary databases:** create/find/open, ROM detection, the Database popup move with re-encryption, the database
   icon, ROM refusals, AppInfo category matching and the database key.
4. **Manage Databases, Security form, and the other paths:** Find, GoTo, Beam, category operations, rekey skip.

## Code space and testing

- `.text` (29.2 KB) and `sec001` (32.3 KB of about 32 KB) are nearly full. New code goes in `sec002` (about 3.6 KB
  used) and a new `sec003`, with thin calls from the main segment.
- **POSE Palm III ROM:** the no-device-ID path. **POSE m505 ROM:** the device-ID path, if POSE exposes a serial token
  there (to check).
- **ROM behaviour:** a debug switch opens a secondary database read-only and treats it as ROM (POSE cannot run
  JackFlash).
- **Real device (user):** JackFlash move to ROM and back; hard reset with the secondary database in ROM, then read the
  memos.
