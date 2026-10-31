# CardDAV contacts sync (design, not built)

> **Not a separate app.** CardDAV contacts sync is folded into existing work:
> - **CalDAV Sync** (`apps/caldav_sync/`, see `CalDAVSync.md`) gains contacts slot types. It
>   does the syncing, holds the login, and writes the contacts databases.
> - **WiRSa's CalDAV Relay mode** (`caldav_relay.cpp`, see `WiRSa-Palm.md` §13) fetches the
>   contacts. There is no new WiRSa mode.
> - **Address35** (`apps/address35/`, see `Address35.md`) shows the synced contacts, read-only.
> - **Datebook35** (`apps/datebook35/`, see `Datebook35.md`) shows their birthdays and
>   anniversaries, like Address35's own.
>
> This file is the design for those changes. Each app's own doc gets the as-built details when
> the work is done.

Status: steps 1-7 built (2026-10-21); Datebook35 birthdays: `Datebook35.md` §21. Differences from §5: no new sentinel range, the rows reuse the birthday rows with a flagged record index.

## 1. Goals and decisions

- Sync contacts from **iCloud** (`contacts.icloud.com`) to the Palm. Generic CardDAV servers
  (Nextcloud, Radicale, Baïkal, Fastmail, Synology and others) use the same protocol (RFC 6352)
  and come almost for free. **Google is out of scope.**
- **One way only:** server → Palm. Synced contacts are read-only on the Palm.
- WiRSa does the network work and trims each contact card. The Palm stores and shows them.
- Synced contacts live in **their own databases**, one per contacts slot, never mixed into
  Address35's own database.
- **8 slots** stay; calendars and contacts share them.

## 2. WiRSa firmware (CalDAV Relay)

### 2.1 Server types

Two new values for the sync request's server type, next to CalDAV, iCloud, Google and ICS Feed:

| Type | URL |
|---|---|
| iCloud Contacts | Found by asking `contacts.icloud.com` for the address book's location (current-user-principal, then addressbook-home-set). Falls back to the slot's server name (`pNN-contacts.icloud.com`) with `/<DSID>/carddavhome/card/`. Path from memory: confirm with `curl` first (§7). |
| CardDAV Contacts | The full address-book URL from the slot. |

Login is the same as for calendars: Basic auth with the Apple ID and an app-specific password
for iCloud. The past/future window fields in the request are ignored for contacts. Older Palm
apps never send these types, so their behaviour is unchanged.

### 2.2 Fetching

Same path as iCloud/CalDAV calendars: `cddvHttpRequest` with a `REPORT` body, the reply
streamed through the existing chunk reader, and each `<address-data>` block cut out the way
`extractCalendarDataBlocksIncremental` cuts out `<calendar-data>` (make the tag name a
parameter). Which request to send depends on what servers accept (§7):

- `addressbook-query` with no filter, the simplest; not accepted by every server.
- `PROPFIND` Depth 1 for the list of contacts and their ETags, then `addressbook-multiget` in
  batches. Works almost everywhere.
- `sync-collection` (RFC 6578): also allows change-only syncs later.

**Tested against iCloud (2026-10-19, one real account, 853 cards):** `addressbook-query` with no
filter returns every card in one HTTP 207 (825 KB reply) - use it; the list-then-fetch route isn't
needed for iCloud. `sync-collection` also works (HTTP 207 with a token). The address book is
`https://pNN-contacts.icloud.com:443/<DSID>/carddavhome/card/` (this account: `p147`). Cards are
vCard 3.0, XML-escaped (no CDATA), so unescape as `xmlUnescape` already does for calendars.

### 2.3 Trimming each vCard

The firmware has the space (about 655 KB free); the Palm doesn't. For each card:

- **Drop:** `PHOTO`, `LOGO`, `SOUND`, `KEY`, anything with `ENCODING=b` or a `data:` URI (as
  Address35's own import does, `vcIsBinaryProp`, `vcard.c:610`), and Apple's internal fields that
  carry no contact data. Address35's import keeps any property it doesn't know as a custom field
  (`vcard.c:596-604`), so the firmware must drop these, not leave them to the Palm. Seen in the
  test: `X-IMAGEHASH`, `X-IMAGETYPE`, `X-ABADR`, `X-SHARED-PHOTO-DISPLAY-PREF`,
  `X-ADDRESSING-GRAMMAR`, `X-ADDRESSBOOKSERVER-PHONEME-DATA`, `VND-63-SENSITIVE-CONTENT-CONFIG`,
  plus `PRODID`, `REV`, `VERSION`, `X-ABUID`.
- **Drop `itemN.X-ABRELATEDNAMES` whose label is `RingTone`, with its label.** In the test 232 of
  245 `X-ABRELATEDNAMES` were these (ringtone settings stored as "related names" by some other
  app); kept, they would show as bogus relations.
- **Also drop, to save space:** `BEGIN`/`END` (one contact per record; Address35 adds them back
  before parsing), `UID` (kept in the record header instead), and `FN` when `N` or "show as
  company" is present (Address35 builds the name from `N` and skips `FN`, `vcard.c:381-384`).
- **Keep instant messaging and social fields:** `IMPP`, `X-AIM`, `X-JABBER`, `X-MSN`,
  `X-SOCIALPROFILE`.
- **Keep these Apple `X-` fields**, with their paired `itemN.X-ABLabel`:
  - `X-ABRELATEDNAMES` (relations, §4.1), and `RELATED`.
  - `X-ABDATE` (Apple's anniversaries and other dates; see the next point).
- **Anniversaries:** iCloud sends them as `itemN.X-ABDATE:YYYY-MM-DD` with
  `itemN.X-ABLabel:_$!<Anniversary>!$_` (confirmed: all 7 `X-ABDATE` in the test; no
  `ANNIVERSARY` at all). Address35's parser only knows `ANNIVERSARY` / `X-ANNIVERSARY`
  (`vcard.c:460`), so convert: Apple's Anniversary label → `ANNIVERSARY` (Wedding); a custom label
  → `ANNIVERSARY` of type Other carrying its name.
- **Birthdays:** `BDAY` comes as `BDAY:`, `BDAY;value=date:` and `BDAY;VALUE=date:` - match
  parameters case-insensitively. `BDAY;X-APPLE-OMIT-YEAR=nnnn:` means the year is unknown (Apple
  stores a placeholder year, usually 1604): send year `0000`.
- **Relation labels** (Apple's `_$!<...>!$_`; seen: Child, Spouse, Mother, Father, Sister) →
  Address35 types (`AddressDB35.h:110`): Spouse → Spouse, Partner → Partner, Child → Child,
  Mother/Father/Parent → Parent, Sister/Brother/Sibling → Sibling, Assistant → Assistant,
  Manager → Manager, Friend → Friend, anything else → Other.
- **"Show as company":** `X-ABShowAs:COMPANY` (25 cards in the test): build the display name and
  sort name from `ORG` for these cards.
- **Other labels:** `_$!<HomePage>!$_` → web address; `_$!<Other>!$_` → Other.
- **Join** folded (continuation) lines and undo `\,` `\;` `\n` escapes.
- **Keep** `UID` always.
- **Apple custom labels:** iCloud writes `item1.TEL:…` plus `item1.X-ABLabel:Office`. Merge the
  label into its field, or it is lost.
- Group cards (`X-ADDRESSBOOKSERVER-KIND:group`) are not sent as contacts: they become the
  slot's categories (§2.4, §3.4).
- **Measured** (test account, 850 contacts after leaving out 3 group cards): 261 KB of trimmed
  text before the extra savings above, 314 bytes a card on average, largest 1,498 bytes (428 cards
  ≤256 B, 331 ≤512 B, 82 ≤1 KB, 9 above). With the extra savings ~180-190 KB: roughly half a
  minute at 57600 baud for a full sync. Storing vCard text is fine; no compact format needed.

**Generic servers** (tested 2026-10-19 against `~/caldav-test-server`, Radicale 3 on port 5232,
address book `/testuser/testcontacts/` with 5 made-up cards): discovery
(`current-user-principal`, `addressbook-home-set`), `addressbook-query` and `sync-collection`
all work, like iCloud. Differences the trimming must handle:

- **Properties re-sorted** (alphabetically): `item1.TEL` and its `item1.X-ABLABEL` are far
  apart. Merge labels by group name across the whole card, never by neighbouring lines.
- **Property names uppercased** (`X-ABLABEL`, `X-ABSHOWAS`): match names case-insensitively.
- **vCard 4 groups:** `KIND:group` + `MEMBER:urn:uuid:<UID>` (RFC 6350), besides Apple's
  `X-ADDRESSBOOKSERVER-KIND:group` + `X-ADDRESSBOOKSERVER-MEMBER`. Handle both (§2.4).
- **Several categories:** `CATEGORIES:Family,Work` - split on commas.
- **`UID:urn:uuid:...`** in vCard 4 cards: strip `urn:uuid:` on both sides before matching.
- **`RELATED`** both as a `urn:uuid:` reference and as `VALUE=text`.
- **Date forms:** `YYYY-MM-DD`, `YYYYMMDD` (`ANNIVERSARY:19750821`), `--MMDD` (`BDAY:--0315`,
  no year), `--MM-DD`; Apple's `X-APPLE-OMIT-YEAR` passes through unchanged.
- Servers may rewrite cards (Radicale also cut test values at unescaped commas), so assume no
  fixed layout or order.

No vCard library: this is plain string handling, a few KB.

### 2.3.1 Text conversion: UTF-8 → Palm Latin (CardDAV first)

Palm OS Western ROMs use **Palm Latin** (essentially Windows-1252); the server sends UTF-8 (74
non-ASCII lines in the iCloud test account). No app converts today: CalDAV event text and
Bitwarden item names already reach the Palm as raw UTF-8 and show as e.g. "JosÃ©". The firmware
converts each contact's text before sending it:

1. ASCII: unchanged.
2. U+00A0-U+00FF (é ü å ö ñ ç ß Ø ...): the same byte value in Palm Latin.
3. The Windows-1252 extras (€ ‘ ’ “ ” – — … • ™ Œ œ Š š Ž ž Ÿ and the rest of 0x80-0x9F):
   their single-byte codes.
4. Other accented Latin letters (ā č ł ő ș ...): a small table maps them to the base letter.
5. Anything else (CJK, Cyrillic, Greek ...): `?` per character - visible that something was
   there (a Chinese name like 王小明 becomes `???`). Greek/Cyrillic transliteration later if
   needed.
6. Emoji, variation selectors and zero-width joiners: dropped.

About 1-2 KB of flash. Also covers the display name and sort name in the contact frame, so the
Palm sorts converted text.

**Rollout:** CardDAV contacts first. If it works out, extend the same function to CalDAV Relay
(event titles, locations, notes) and Bitwarden Relay (item names, folder names, usernames, URLs,
notes).

**Never convert secrets.** Passwords, TOTP keys, card numbers/codes, and anything else used as a
credential must be sent byte for byte: a password with non-ASCII characters would change and stop
working. The conversion applies only to fields that are displayed.

### 2.4 Frames to the Palm

*Built 2026-10-19* (WiRSa `caldav_relay.cpp`, `WIRSA_FEATURE_CARDDAV`, default 1): server types
4 = iCloud Contacts (URL `https://{server}/{uuid = DSID}/carddavhome/{path or "card"}/`), 5 =
CardDAV Contacts (`joinUrl(server, path)`); one `addressbook-query` REPORT, streamed through
the calendar multistatus reader with its tag switched to `address-data`; each card goes
through `CardDav::trimCard` (`carddav_card.cpp`). Frames, both acked with `CDDV_ITEM_ACK`:

- `CDDV_CONTACT_ITEM` (0x0C): uid str, displayName str, sortName str, nDates u8,
  {year u16 (0 = unknown), month u8, day u8, kind u8 (0 birthday, 1 engagement, 2 remembrance,
  3 wedding, 4 work, 5 other), name str}×nDates (max 8), nCategories u8, {str}×n (max 16),
  text str (trimmed vCard lines, `\n`-separated; cut at a line break if it would not fit the
  3,192-byte frame).
- `CDDV_GROUP_ITEM` (0x0D): uid str, name str, nMembers u8, {member uid str}×n - a big group
  comes as several frames with the same uid.

All text Palm Latin. Flash: +23 KB with it on (C++ containers), +0.1 KB with it off.

| Frame | Body | Notes |
|---|---|---|
| Contact | UID, display name, sort name, dates, trimmed vCard text | Large cards are split into pieces. Each frame is acknowledged, like events (`CDDV_EVENT_ITEM` / `CDDV_ITEM_ACK`). **Dates:** birthday and each anniversary as `YYYYMMDD` (`0000` = year unknown) + kind (Birthday, Engagement, Remembrance, Wedding, Work, Other + name), already extracted, so CalDAV Sync can build the birthday index (§3.6) without parsing the vCard. |
| Group | Group name, member UIDs | iCloud only. Groups can arrive before or after their members, so the Palm applies them at the end. Member lines are `X-ADDRESSBOOKSERVER-MEMBER:urn:uuid:<UID>` (iCloud) or `MEMBER:urn:uuid:<UID>` on a `KIND:group` card (vCard 4 servers): strip `urn:uuid:` so they match the contacts' `UID` exactly. Many generic servers use `CATEGORIES:` inside each vCard instead (or as well). |

Sync done, sync error and BYE are unchanged.

## 3. CalDAV Sync (Palm)

### 3.1 Sec35 gate

*Already in place* (`CalDAVSync.c:65-75`: no Sec35 → refuse to run; locked → bounce to Sec35).

The app requires Sec35 to be unlocked before use, like Bitwarden. Every slot field is
encrypted already, and Duplicate needs to decrypt and re-encrypt (§3.3).

### 3.2 Slot types

iCloud Calendar, CalDAV Calendar, Google Calendar, ICS Feed, **iCloud Contacts**, **CardDAV
Contacts**. Still 8 slots, so the saved-settings size (6,774 bytes) and format (version 5) are
unchanged; the new types are new values of the existing type field. The type decides where a
slot writes: calendar types to `DateBook35DAV`, contacts types to the slot's own contacts
database (§3.4).

### 3.3 Duplicate slot

*Built 2026-10-19* (`DupSlot.c`, `CalDAVSync.md` §11). The `-caldav` → `-contacts` swap happens
in the slot editor when the type is switched (`CalDAVSync.md` §12).

- The user picks the source slot and the target slot.
- An occupied target asks before it is overwritten.
- Each encrypted field is decrypted with the source slot's number and encrypted again with the
  target's: the encryption nonce is (slot number, field number) (`CalDAVSync.md` §5), so the
  bytes can't be copied as they are.
- Settings only: last-sync time, result, event count and error message are not copied.
- When the copy is changed to iCloud Contacts, offer to change `-caldav` to `-contacts` in the
  server name. The user can still edit it if their account's server number differs.

### 3.4 Contacts databases

*Built 2026-10-19* (`ContactDb.c/.h`, `CalDAVSync.md` §12, §12.1): slot types, receiving contacts,
per-slot database with staging (5a); categories from groups, `Address35DAVBDA`, stale flag and
cleanup (5b).

- One database per contacts slot: `Address35DAV_<n>` (n = slot number).
- Created with **CalDAV Sync's creator, never `'Ad35'`**: CatSync finds Address35's own database
  by type `DATA` + creator `'Ad35'` (`lib/catsync.c:13`), and must not pick one of these instead,
  or merge Address35's categories into the server's.
- Written as a temporary database and swapped in only on a successful sync (Bitwarden's
  `Store.c` pattern). A failed sync leaves the previous contacts in place.
- Records are Sec35-encrypted and sorted by sort name.
- **Record layout:** a small header (UID, sort name, display name) followed by the trimmed
  vCard text. Address35 builds its list from the headers and parses the vCard only when a
  contact is opened.
- **Info block:** slot type, alias and a stale flag. CalDAV Sync writes it at every sync and
  every slot Save, so Address35 never needs to read CalDAV Sync's settings.
- **Categories must carry across** (requirement). They come from the server's groups (iCloud
  group cards, or `CATEGORIES:`): the slot's contacts database gets one category per group, named
  after it, and each contact is filed under its group. At most 15 named (extra groups → Unfiled),
  names cut to 15 characters, one category per contact (the alphabetically first if it is in
  several groups); contacts in no group → Unfiled. Address35's category popup in the CardDAV view
  then filters by these.
- **Verified against iCloud (2026-10-19):** 3 groups; every member reference (118, 21 and 12)
  matched a contact's `UID` once `urn:uuid:` is stripped; group names well under 15 characters;
  1 contact in two groups; 700 in none.

### 3.5 Stale data and cleanup

No warning dialogs and no immediate deletion; one rule covers every case:

- **On Save** of a type change or a Duplicate overwrite: if the slot had a contacts database,
  set its stale flag (one small write, instant). Show a one-line note: "Old records are removed
  at the next sync."
- **At the start of every Sync Now and Sync All, for all 8 slots:** delete the contacts
  database of any slot that is now a calendar or empty, and remove `DateBook35DAV` rows of any
  slot that is now contacts or empty (one pass by slot number, with the existing progress bar).
  This runs even if the sync itself then fails: that data is wrong either way. Replacing a
  slot's current data stays success-only, as today.
- Calendar rows of a changed slot still show in Datebook35 until that cleanup, the same as after
  a calendar path change today. Hiding them sooner would need Datebook35 to read a flag; its
  fullest code segments have 68 and 268 bytes free, so this is left out.

### 3.6 Birthday index for Datebook35

Datebook35 shows Address35's birthdays and anniversaries by walking **every record and every
field** of Address35's database for each day it draws (`AddrBdayGet`, `datebook35/Src/
AddrBday.c:121-200`). That works because Address35 keeps names and dates unencrypted
(`Address35.md` §8.1). The contacts databases are Sec35-encrypted vCard text, so the same
approach would mean decrypting and text-searching every contact for every day drawn: too slow.

Instead CalDAV Sync writes a small index while it syncs:

- **One database for all contacts slots:** `Address35DAVBDA`, CalDAV Sync's creator.
- **12 records, one per month,** so drawing a day reads one small record.
- **Entry:** day; year (`0` = unknown); kind (Birthday, Engagement, Remembrance, Wedding, Work,
  Other); the Other name (Datebook35 shows Other anniversaries by name, `AddrBday.c:180-190`);
  slot number; record index in that slot's contacts database; short display name. About 40
  bytes, so 1,000 entries ≈ 40 KB across the 12 records.
- **Birthdays and anniversaries both,** mixed in date order, as Address35's own rows are.
- **Unencrypted**, the same level as Address35's own names and dates.
- **Rebuilt** after every contacts sync and every cleanup (§3.5), so it always matches the
  contacts databases: removed, changed or stale slots drop out with their contacts, and record
  indexes stay valid because the index and the databases change together.

### 3.7 Unchanged

Sync All (now mixing calendars and contacts), Screenless Mode, Exit Timers and the progress
screens work as they do for calendars.

## 4. Address35

- **Options → CardDAV:**
  - "Show:" Local | one entry per contacts slot (its alias). Stale databases are left out.
  - "Sync…" switches to CalDAV Sync (like Datebook35's "CalDAV Sync" menu item,
    `Datebook35.md` §17.2).
  - The choice is remembered.
- **CardDAV view:**
  - Title "iCloud - [Alias]" or "CardDAV - [Alias]", from the database's info block.
  - List and search read that database. The category popup works as usual over its categories;
    no Edit Categories.
  - The Record form parses the stored vCard with Address35's existing parser (`vcard.c`). No
    Edit, Delete or New; no beam receive into it.
- Locked Sec35: masked and bounced, as for local contacts.
- **Space:** Address35's main code segment has about 84 bytes free (`Address35.md` §9), so all
  new code goes in sec002/sec003.
- **GoTo by database:** Palm OS's GoTo parameters carry a database ID. When it names a contacts
  database, Address35 opens that slot's CardDAV view on the given record (used by Datebook35's
  birthday rows, §5).
- **Later, optional:** Phone Lookup and Find reading these databases.

### 4.1 Relations

Address35 stores a relation as a **link to another record's unique ID in the same database**
(`AddressDB35.h:135-139`), and adding one writes the reverse link into the other contact
(`A35AddReciprocalRelation`). A contacts database is rebuilt on every sync, so record unique IDs
change and any stored link would break. So:

- **Relations stay text** in the stored vCard (`RELATED`, `X-ABRELATEDNAMES` + label; §2.3).
- **Resolved when a contact is opened:** the CardDAV view passes **that slot's database** to the
  vCard parser, which already looks a `RELATED` name up in the database it is given and keeps
  the name as text when nothing matches (`vcard.c:552-568`). Links therefore always point within
  the same account and are never stale. vCard 4 servers may use `urn:uuid:` references; matching
  those against the stored UID is a small addition.
- **The CardDAV view never writes:** no reverse links, no relation edits, and a relation never
  leads into the local database.
- **No links across databases:** a local contact can't link to a synced one (that link would break
  each sync); store the name as text if needed.

## 5. Datebook35

- **Day view and Agenda** show CardDAV birthdays and anniversaries exactly like Address35's
  ("Name - Nth Birthday", "Name - Nth Wedding", untimed, `Datebook35.md` §8.2), read from
  `Address35DAVBDA` (§3.6): one database open and one month record per day drawn.
- **Row IDs:** a new sentinel range, separate from Address35 birthdays (`0xD...`) and ToDo rows
  (`0xE...`).
- **Tap:** switch to Address35 with GoTo naming the slot's contacts database and the record index
  (§4).
- **Locked Sec35:** the index holds only names and dates, like Address35's clear fields; follow
  whatever Datebook35 does for Address35 birthdays while locked.
- **Space - the main risk:** Datebook35's code segments are full (main 68, sec001 268, sec002 32,
  sec003 80 bytes free). This needs a **new segment, sec004**, with the known gotchas: calls
  from main into functions late in another segment (`Datebook35.md` §20), and constant tables
  read from secondary segments.

## 6. Shared formats kept in step by hand

1. Contact and group frames: firmware ↔ CalDAV Sync.
2. Contacts database info block and record layout: CalDAV Sync ↔ Address35. Same arrangement as
   `DateBook35DAV` between CalDAV Sync and Datebook35 (`CalDAVSync.md` §8).
3. `Address35DAVBDA` record layout: CalDAV Sync ↔ Datebook35.
4. GoTo with a contacts database ID: Datebook35 ↔ Address35.

## 7. Space

| Where | Now | Note |
|---|---|---|
| WiRSa flash | ~655 KB free (1,295,325 / 1,966,080, scratch build 2026-10-16) | Contacts adds a few KB |
| CalDAV Sync main `.text` | ~13 KB of 32 KB | Other segments also have room |
| CalDAV Sync globals | 31,950 of 32,767 bytes | ~800 bytes left: allocate new buffers when needed, no new globals |
| Address35 main | ~84 bytes free | New code in sec002/sec003 |
| Datebook35 | main 68, sec001 268, sec002 32, sec003 80 bytes free | Needs a new sec004 (§5) |

## 8. To find out before building

1. ~~**iCloud's replies, with `curl`**~~ - done 2026-10-19 (§2.2-§2.4, §3.4). ~~Generic CardDAV
   server~~ - done 2026-10-19 against Radicale (§2.3).
2. ~~**Accented characters**~~ - decided 2026-10-19: the firmware converts UTF-8 to Palm Latin
   (§2.3.1), CardDAV first.
3. **Large address books:** Palm memory and time for 1,000-2,000 contacts (Address35's parser
   was tested with 1,000). Measured size for 850: §2.3.

## 9. Build order

1. ~~`curl` tests (§8)~~ - done for iCloud.
2. CalDAV Sync groundwork: Sec35 gate, Duplicate, the new types, stale flag and cleanup.
   Testable with existing calendar slots.
3. Firmware: contacts server types, address-book lookup, vCard trimming (both group styles,
   all date forms), UTF-8 → Palm Latin conversion (§2.3.1), contact and group frames.
4. CalDAV Sync: write the contacts databases, categories and the birthday index.
5. Address35: Options → CardDAV, the read-only view, relations resolved in the slot's database,
   GoTo by database ID.
6. Datebook35 last (riskiest: new sec004): birthday and anniversary rows from the index.
7. If the conversion works out: extend it to CalDAV Relay and Bitwarden Relay display text
   (never secrets).
