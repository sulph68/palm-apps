# Address35 — Modern era-aware Address Book (DESIGN PROPOSAL)

**Status: historical design proposal — implemented.** The current reference for how
Address35 actually works is `apps/Address35.md`; this doc is kept for the design
rationale and may differ in detail. It captures the design agreed with the user for a
modernized replacement of the ROM **Address** app (`Sample_Source/Examples/Address/`).
It extends the ROM app's feature set with multiple values per field, contact
relationships, an era-independent birthday that surfaces in **Datebook35**'s Agenda,
messaging handles, tags + tag filtering, and vCard 3.0/4.0 import/export over beam
**and** SD card. Address35 uses its **own database and its own record format**, so the
format is free to be redesigned.

Paths are relative to `hackmaster_y2035/`. New code lives entirely in
`apps/address35/`; shared era/date/picker/hack code is reused from `apps/lib/`.
Origin for the UI/behaviour baseline: `Sample_Source/Examples/Address/` (changes
marked `Ad35:` in comments).

Identity: creator **`Ad35`**, type `appl`; database **`Address35DB`** (type
`DATA`, creator `Ad35`) — its own DB, never the ROM `AddressDB`.

Target: **Palm OS 4.1 minimum** (VFSMgr for the SD-card vCard flow; era libs).

---

## 1. Goals (what's new vs the ROM Address)

1. **Multiple values per field** — several addresses, phones, emails, messaging
   handles per contact (ROM caps at fixed slots).
2. **Relationships** — link contacts as spouse/partner/child/parent/sibling/… by a
   stable record id; display the linked name; optional auto-reciprocal.
3. **Era-independent birthday** — stored full-range (1904–9999), month/day-only
   allowed, entered with the ROM-look era picker; **surfaces in Datebook35 Agenda**.
4. **Messaging fields** — WhatsApp / Telegram / Signal / … handles (extensible).
5. **Tags + filtering** — multi-label tags in addition to the single Palm category;
   filter the list by tag.
6. **Middle name** — first / **middle** / last, plus a user-chosen term for the
   surname (see §6).
7. **vCard 3.0 (RFC 2426) + 4.0 (RFC 6350)** import/export over **beam** and **SD
   card**, filling fields where appropriate.

Everything the ROM app does (categories, list/edit/details views, phone-label
picker, find, note, beam) is preserved; the new capabilities are additive.

**Non-functional requirement (first-class): speed.** Address35 must feel **instant
on a Palm Pilot / DragonBall-class device** (16–33 MHz, tight RAM) — scrolling the
list, opening a contact, switching category/tag, and drawing the Agenda birthdays
must not stutter. This constraint shapes the record format, the list drawing, and
the birthday lookup below; see **§13** for the concrete discipline. Where a feature
would cost per-row work, it is precomputed or made windowed instead.

---

## 2. Database & record format — self-describing TLV

The ROM record is a rigid packed struct (19 fixed fields, 4-bit phone-label
nibbles). It cannot grow to the above without breaking. Address35 uses an
**own, extensible TLV (type-length-value) record**: a small fixed header followed
by a list of typed field entries. New field types never break the format; multiple
values fall out for free; unknown vCard properties are preserved as passthrough.

```
Address35 record = FixedHeader + FieldEntry[]   (big-endian, 68K-native)

FixedHeader {
  UInt8   version;        /* record format version = 1                        */
  UInt8   fieldCount;
  UInt16  flags;          /* hasNote, sortByCompany-at-create, reserved       */
  /* fast sort keys: byte offsets into the TLV blob for last/first/company so
     list-view + name/company sort never unpack the whole record             */
  UInt16  offLast, offFirst, offCompany;
}

FieldEntry (TLV) {
  UInt8   type;           /* see field types below                            */
  UInt8   label;          /* per-type subtype in low bits + kPrimaryFlag(0x80)*/
  UInt16  length;         /* value byte count                                 */
  UInt8   value[length];  /* UTF-8/latin string, OR a fixed struct (below)    */
}
```

**Field types** (`type`):
```
kName kFirst kMiddle kLast kNickname kOrg kTitle
kPhone kEmail kIM kURL
kAddress            (structured, see below)
kBirthday           (structured)
kRelation           (structured)
kTag                (UInt16 tagId)
kNote kCustom       (custom carries its own label string first)
```

**Label enums** (`label` low bits), extensible:
- `kPhone`: mobile, home, work, fax, main, pager, other.
- `kIM`: whatsapp, telegram, signal, other.
- `kAddress`: home, work, other.
- `kRelation`: spouse, partner, child, parent, sibling, assistant, manager,
  friend, other.
- `kPrimaryFlag` (0x80): the value shown in the list view (one primary phone /
  email). Mirrors the ROM "displayed phone" idea, generalized.

**Structured values** (fixed little structs inside `value[]`):
```
Birthday  { UInt8 flags; Int16 year; UInt8 month; UInt8 day; }
          flags.hasYear (bit0) — clear for month/day-only birthdays.
          year uses the era convention (full year - 1904), matching
          edate_override's DateType, so datelib/edate consume it directly.

Relation  { UInt32 targetUniqueID; UInt8 relType; }  /* +optional label str */
          targetUniqueID = DmRecordInfo unique id of the linked record
          (stable across sort/edit). Name resolved id->name at draw time.

Address   a short sub-TLV: street / city / state / zip / country lines, so an
          address is one FieldEntry holding its own labelled lines.
```

**AppInfo** holds: the standard Palm category block (single category per record,
kept for beam/HotSync compatibility) **and** the **tag dictionary** (`tagId ->
name`, so tags rename once and filter fast), plus the field-label customizations
and the surname-term preference (§6).

Packing/unpacking is **symbolic** (offsets via `&nilP->field`, `sizeof`), like
ToDo35 — never hardcode byte offsets. **Performance:** the sort-key strings (last,
first, company) are stored **first, immediately after the fixed header**, so the DB
sort comparator reads them at a known offset with **no TLV scan and no full
unpack** — `DmQuickSort`/insertion comparisons stay cheap even with thousands of
records. List draw unpacks only the fields a visible row shows (name + primary
value) and only for **on-screen rows** (windowed, like the ROM). The last-name
underline is free: its offset is already in the header, so list draw underlines
that run directly without re-parsing the name.

---

## 3. Birthday — era-independent, ROM-look picker, Agenda integration

- **Storage**: the `Birthday` struct above; full-range via `datelib`/`edate`. A
  birthday with no known year sets `hasYear = 0` (month/day only) — common for
  contacts.
- **Entry**: the shared era-aware picker **`lib/datepick`** (`DatePickSelectDay`,
  form 8000) — a pixel drop-in for the ROM `SelectDay` "Go to Date" dialog but
  unclamped to 1904–9999 (Datebook35 already uses it everywhere). Add a **"year
  unknown"** affordance to the picker for month/day-only birthdays.
- **Display**: age / "turns N" computed with full-range `edate` math against
  **today** = `Hc_CurrentRealDate` (real date past 2031 via the hack).
- **Datebook35 Agenda integration** (implemented **LAST**, per user): mirror the
  existing ToDo35 hook — `DateAgenda.c` already opens `ToDo35DB` by type/creator
  and renders a tasks section. Add an **additive, guarded** birthday source that
  opens `Address35DB` read-only and renders synthetic all-day lines (e.g. "🎂 Jane
  Doe — turns 41"), age via era math. If `Address35DB` is absent, skip silently.
  Datebook35 won't launch without the hack anyway, so era math is always available
  there — low integration risk.
- **Performance — a birthday index, not a per-day full scan.** Scanning every
  contact for each agenda day would be slow on old hardware. Instead Address35
  maintains a compact, sorted **birthday index** (an array of `{ UInt16
  packedMonthDay; UInt32 uniqueId }`, sorted by month/day) in its own DB (an
  index record / AppInfo), rebuilt only when a birthday is added/changed/deleted.
  The Agenda does a **bounded lookup** (binary search to the day's bucket, read
  only the matching records) — O(log n) + the few hits, not O(all contacts). The
  index is month/day only, so it is naturally era-independent and recurs annually.

---

## 4. Relationships

- A `kRelation` field links to another record by its `DmRecordInfo` **unique id**
  (stable). `relType` = spouse/partner/child/parent/sibling/…; a free label string
  may follow for "other".
- **Picking**: an AddressLookup-style contact picker chooses the target.
- **Display**: resolve id→name at draw; a deleted target shows "(deleted)" and the
  dangling link is pruned lazily.
- **Auto-reciprocal** (optional, default on): creating spouse↔spouse, parent↔child,
  sibling↔sibling writes the mirror link on the other record.

---

## 5. Tags + categories (both kept)

- The **single Palm category** is kept (coarse grouping, beam/HotSync friendly).
- **Tags** are multi-label, free-form, stored as `kTag` id lists with the id→name
  dictionary in AppInfo. Add/rename tags from the edit view and a tag manager.
- **Filter**: the list view can filter by **category** (existing popup) **or by
  tag** (a tag filter, sitting beside the category trigger). Filtering by a tag
  shows records containing that tag id.

---

## 6. Names — middle name, surname terminology, underline

- Fields: **first / middle / last** (+ nickname, title, company as today).
- **Surname term preference**: Options → Preferences has a popup to choose the
  label used for the last-name field throughout the UI: **"Surname" / "Last Name"
  / "Family Name"** (stored in AppInfo). Affects field labels only, not storage.
- **List view**: the **last name is underlined** in the displayed name **whenever a
  name is shown** (regardless of sort-by-name vs sort-by-company), so the family
  name is always visually distinct.

---

## 7. UI — ROM layout first, dynamic fields grouped

- **Edit view starts identical to the ROM Address edit form** (same fields, order,
  phone-label popups, category trigger, Details/Note), so it's instantly familiar.
- On top of that it's a **dynamic field editor**: an "add field" affordance inserts
  new typed fields into **reasonable groups** (Phones, Emails & Messaging,
  Addresses, Personal [birthday, relations], Tags). New rows use the TLV model, so
  any number can be added.
- List / Record / Details / Find views reuse the ROM structure; list draw adds the
  underlined surname and (optionally) a birthday/tag affordance.

---

## 8. vCard 3.0/4.0 — beam + SD card

**Standards**: import **vCard 3.0 (RFC 2426)** and **4.0 (RFC 6350)** (the ROM
exports only 2.1). Mapping:

| vCard | Address35 |
|-------|-----------|
| `N` (family;given;additional;prefix;suffix) | last / first / **middle** / title |
| `FN`, `ORG`, `TITLE`, `NICKNAME` | display name, company, title, nickname |
| `TEL;TYPE=cell/home/work/fax/…` (multiple) | `kPhone` entries + labels |
| `EMAIL` (multiple) | `kEmail` entries |
| `ADR;TYPE=home/work` (multiple) | `kAddress` entries |
| `BDAY` (`YYYY-MM-DD`, or vCard-4 `--MM-DD`) | era `Birthday`, hasYear as given |
| `IMPP` / `X-SOCIALPROFILE` / `X-WHATSAPP` / `X-TELEGRAM` / `X-SIGNAL` | `kIM` |
| `URL` | `kURL` |
| `CATEGORIES` | tags (+ the single category best-effort) |
| `RELATED;TYPE=` (vCard-4) | `kRelation`, best-effort name match else label |
| `NOTE` | `kNote` |
| unknown props | preserved as `kCustom` passthrough |

Export writes **vCard 4.0** (3.0 fallback), including `BDAY`, `IMPP`, `RELATED`,
`CATEGORIES`.

**Transport 1 — beam**: over the Exchange Manager, like the ROM (send in a
foreground segment; the **receive/import path must live in main** — no-globals
sublaunch rule, see the family's segmentation memories).

**Transport 2 — SD card** (VFSMgr via a `CardIO`-style wrapper, reused/adapted from
CryptoNote). Card root layout:
```
/Address/            <- "Card" browse source (see below)
/Address/Import/     <- Import menu reads vCards from here
/Address/Export/     <- Export menu writes vCards here
```
- **Import menu item**: if no card → alert "No card". Else read every vCard under
  `/Address/Import/` and import into `Address35DB`.
- **Export menu item**: if no card → alert "No card". Else prompt an **export scope**
  (current record / current category / tagged / all — like beam record-vs-category)
  and write the chosen contacts as vCards into `/Address/Export/`.
- **"Card" in the category popup** (CryptoNote-style extra row, only when a card is
  mounted): selecting it **browses the vCards directly under `/Address/`**
  (**excluding** the `Import/` and `Export/` subfolders) and displays them as
  read-only contacts. Viewing a browsed vCard offers an **"Import"** button that
  imports that **single** record into `Address35DB`.
- **Splitting & naming**: when a vCard under `/Address/` contains **multiple
  people**, it is **split and rewritten on the card into one file per person**
  (the original multi-person file is replaced) — done once so future browsing is
  fast and low-RAM; a file that is already single-person is left untouched.
  Filenames use **`firstname_lastname`** (`.vcf`); on collision append **`_N`**
  (`firstname_lastname_2.vcf`, …).

---

## 9. Era & the hack — soft gate

- Birthdays **store** era-independently with no hack. "Today", age, and "upcoming
  birthdays" need the real date → the hack is a **soft** dependency: if the hack is
  off, warn once and stay usable (ages past 2031 just aren't computed). This
  differs from ToDo35/Datebook35's hard gate, by user choice.
- Reuse `apps/lib`: `edate_override.h` (wide `DateType` for the birthday),
  `edate`/`datelib` (math), `datepick` (picker), `hackcheck` (`Hc_CurrentRealDate`,
  soft check), `era` (display).

---

## 10. Build & segmentation

- **Multi-segment** (the ROM Address is ~16K LOC → well over the 32 KB code-resource
  cap). Same scaffolding as the family: `SEC00x`/section tags on **forward
  declarations**, build with **`-palmos4`** (the three multi-seg traps:
  `-palmos4` far-call, forward-decl tag match, libgcc-division→main). The vCard
  **receive/import path stays in main** (no-globals sublaunch).
- **Makefile** like CryptoNote → output **`Address35_v1.prc`** (`VERSION` variable).
  `.def`: `application { "Address35" Ad35 backup } / multiple code { "sec001" ... }`.
- Reuse the era libs from `apps/lib`; resource pipeline reuses the ROM Address
  Constructor resources where forms are kept (converter), hand-authored pilrc for
  the new dialogs (tags, relations, prefs term popup).

---

## 11. Implementation order (Datebook35 change LAST)

1. **DB layer** — `AddressDB35.{c,h}`: TLV record pack/unpack (symbolic), AppInfo
   (category + tag dict + label/term prefs), CRUD, sort (name/company), the wide
   birthday. **[DONE — compiles clean; see below.]**
2. **Core app + list/record/edit views** — ROM layout baseline + dynamic fields,
   middle name, surname-term pref, underlined last name, category + tag filter.
   **[2a list view + 2b edit view DONE & verified on CloudpilotEmu.** List draws
   underlined last names sorted; edit form opens from a tap with all fields +
   category "Unfiled"; save round-trips; persisted-DB reopen works. Fixed a
   `DmSetDatabaseInfo` arg-position bug (see dmsetdatabaseinfo-arg-position memory)
   that caused a nil-ptr edit crash + a silent no-launch on reopen. Still TODO:
   dynamic field groups (multiple phones/emails/etc.), surname-term pref, tag
   filter, a read-only Record view.]**
3. **Birthday picker** (datepick + year-unknown), relationships, tags UI.
4. **vCard 3.0/4.0** import/export — beam (receive in main) + SD-card `CardIO`
   (Import/Export folders, "Card" browse, multi-person split, naming).
5. **Datebook35 Agenda birthday integration** — additive, guarded, **done LAST**.
6. Testing — **CloudpilotEmu (m505 4.1 ROM + SD card) driven via Claude-in-Chrome**,
   the same harness used for CryptoNote (see the `cloudpilot-vfs-harness` memory).
   **Not POSE/pose64** — they can't emulate an SD card, which Address35's vCard
   import/export/browse needs. Core (non-card) behaviour is exercised on the same
   emulator, so there's one consistent test target.

### 11a. Build status & step-1 notes
- **Done:** `a35_seg.h`, `AddressDB35.h`, `AddressDB35.c` — the full DB layer
  (TLV pack/unpack, AppInfo with category block + tag dictionary + surname-term
  pref, record CRUD, name/company sort, birthday). Compiles clean under
  prc-tools gcc with `-palmos4 -Wall` (only the SDK's own pragma warnings).
- **Sound choices made while implementing:**
  - **Birthday stores a FULL `Int16` year** (era-independent, 1904–9999) directly,
    *not* the `edate_override` `year = full−1904` convention — Address35 is a fresh
    format with only one date, so full years are simpler and avoid the heavy
    force-included override. Birthday math will call the `edate` E* primitives
    (which take full years).
  - **All TLV multi-byte access is byte-wise big-endian** (`rdU16`/`wrU16`): a
    field length/value can land on an odd address and a `UInt16*` load there faults
    on a 68000.
  - **Fast sort:** the packed header carries `offLast/offFirst/offCompany` (value
    offsets), so `A35ComparePackedRecords` reads the sort keys with no TLV walk.
  - **`A35ChangeRecord` preserves the record's unique id** (relationships depend on
    it) by mirroring the ROM's `DmMoveRecord`+`DmAttachRecord(oldHP)` swap, not a
    detach/reattach.
  - DB layer lives entirely in **main** (the vCard receive path reaches it).

---

## 12. Resolved decisions (previously open)

1. **"Card" browse scope** — the "Card" popup entry browses vCards **directly under
   `/Address/`**, **excluding** the `Import/` and `Export/` subfolders.
2. **Splitting side effects** — a multi-person vCard is **split and rewritten on the
   card** into per-person files (original replaced), so future browsing is fast and
   uses less RAM. Browsing a vCard also offers a **per-record "Import"** button.
3. **Export selection** — the Export menu **prompts a scope** (current record /
   current category / tagged / all).
4. **Underline scope** — underline the last name **whenever a name is displayed**,
   regardless of sort order.
5. **Creator id** — **`Ad35`** confirmed.

---

## 13. Performance — fast on Palm Pilot-class hardware

Speed is a hard requirement (§1). Concrete discipline, all measured against a
16–33 MHz DragonBall with little RAM:

- **Record-at-a-time, never all-in-RAM.** Like the ROM Address, operate on one
  packed record at a time from the DB; do not build in-memory arrays of all
  contacts. Unpack a full record only for the **currently viewed/edited** contact.
- **Cheap sort.** Sort-key strings (last/first/company) live right after the fixed
  header so the DB comparator reads them at a known offset — no TLV walk, no alloc,
  per comparison. Rely on the DB's own sorted order + `DmFindSortPosition` on
  insert; avoid re-sorting the whole DB on every edit.
- **Windowed list drawing.** Draw only the on-screen rows on demand (the ROM/Table
  model already does this); per row, unpack just the name + the one primary value
  shown. The last-name underline uses the header offset — no name re-parsing.
- **Category/tag filter via seeking.** Use category record attributes for the Palm
  category (standard, fast). Tag filtering checks the record's small `kTag` list
  while seeking the next visible record — bounded (a contact has few tags); no
  global scan or secondary index needed for typical sizes.
- **Birthday index for the Agenda.** A compact sorted `{monthDay, uniqueId}` index
  (rebuilt only on birthday change) turns the Agenda's daily lookup into a binary
  search + a few record reads, not a scan of every contact (§3).
- **Lazy, one-shot vCard work.** Parse vCards only when Card-browse is opened or
  Import/Export is invoked; cache the parsed list for the browse session. Multi
  person files are split **once** and rewritten per-person, so subsequent browses
  read small single-record files (fast, low-RAM). Never re-scan the card per row.
- **No surprise libgcc / float / heavy math in hot paths.** Age/date math runs only
  when a birthday is actually drawn, not per row; keep 32/64-bit division in main
  per the segmentation rules. No floating point.
- **Minimal redraw.** Invalidate/redraw only changed rows (row-uniqueid checks like
  the ROM), not the whole list/form, on edits and category/tag changes.

## 14. Phone Lookup for the suite (2026-10)

Implemented - see the reference doc, `apps/Address35.md` §10.
