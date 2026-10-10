#!/usr/bin/env python3
"""
gen_bulk_fixture_plain.py - build a BitWardenDB.pdb full of synthetic
records directly on the host, for POSE64 list/search/filter/scroll/sort
SPEED-ONLY testing, bypassing BwStoreAddItem's real write path entirely -
that path is extremely slow in POSE64 even for 100 records (2026-08), and
real AES-CTR encrypt/decrypt cost is identical regardless of whether the
source bytes are really encrypted or not, so this no-op variant is good
enough for pure timing/benchmark work.

PLAINTEXT (no encryption at all): every field is written as-is - the real
app's own read path still runs a REAL AES-CTR decrypt pass over it (so
decrypt-dependent operations like sort/search still pay their real crypto
cost), but the "decrypted" result is garbled, not readable. NOT suitable
for visually verifying search/filter/sort correctness - use the real
double-encrypting gen_bulk_fixture.py + a live POSE64 session's own cached
keys for that instead.

Sequencing gotcha (see BwStoreInit in Store.c): the on-disk record format
carries a version stamp in PREFS (gPrefs.formatVersion), not in the PDB
itself. A fresh app install's prefs default to version 0, and BwStoreInit
does a ONE-TIME FULL WIPE of the DB whenever the stored version doesn't
match bwStoreFormatVersion (currently 4 - bumped when the archived flag got
its own header byte) - if you install this fixture BEFORE BitWarden has
ever launched once in this session, its very first launch will immediately
wipe every record this script wrote. Launch BitWarden at least once first
(so the version-stamp wipe already happened and gets recorded in prefs),
THEN install this fixture to replace the (now version-matched, empty)
on-device DB - matches apps/test/3_generate_bulk.py's own "install matches
DB by NAME" convention.

Usage:
    python3 gen_bulk_fixture_plain.py [item_count] [out_path] [archived_count]
    # then, after BitWarden has launched at least once in this POSE session:
    python3 rc.py "install .../BitWardenDB.pdb"
"""
import struct, sys, time

# no cryptography package needed - plaintext variant


def double_encrypt(data, nonce_hi, nonce_lo):
    """PLAINTEXT variant (gen_bulk_fixture_plain.py) - no encryption at all.
    Names/fields decrypt to garbage in the real app (AES-CTR of un-encrypted
    bytes just produces different wrong bytes, not a crash), which is fine
    for SCROLL-SPEED-ONLY testing at real vault scale (779 records) without
    needing a live POSE64 session's own keys extracted first. NOT for
    sort-order/search visual verification - use the real (encrypted)
    gen_bulk_fixture.py + in-app injection for that instead."""
    return data

# ---- record/field layout constants (Store.c / BitWardenRsc.h - keep in sync) --
BW_GUID_LEN = 37          # 36-char GUID + NUL
BW_FIELD_COUNT = 30
BW_MAX_FIELD = 512        # fields 0..5
BW_MAX_EXT_FIELD = 128    # fields 6..29
BW_FIELD_512_COUNT = 6
# +1 = archived byte (bwArchivedByteOff, Store.c), appended after fieldLen[]
# - bwStoreFormatVersion 4.
BW_HDR_FIXED_SIZE = 1 + 2 + 2 + BW_GUID_LEN + BW_FIELD_COUNT * 2 + 1   # = 103

BW_TYPE_LOGIN, BW_TYPE_NOTE, BW_TYPE_CARD, BW_TYPE_IDENTITY = 1, 2, 3, 4
BW_ORG_MARKER_TYPE = 0xFE
BW_FOLDER_NONE = 0xFFFF
BW_ORG_NONE = 0xFFFF

(F_NAME, F_USER, F_PASS, F_URI, F_NOTES, F_TOTP,
 F_CARD_HOLDER, F_CARD_BRAND, F_CARD_NUMBER, F_CARD_EXPMONTH, F_CARD_EXPYEAR, F_CARD_CODE,
 F_ID_TITLE, F_ID_FIRST, F_ID_MIDDLE, F_ID_LAST, F_ID_USERNAME, F_ID_COMPANY, F_ID_SSN,
 F_ID_PASSPORT, F_ID_LICENSE, F_ID_EMAIL, F_ID_PHONE, F_ID_ADDR1, F_ID_ADDR2, F_ID_ADDR3,
 F_ID_CITY, F_ID_STATE, F_ID_POSTAL, F_ID_COUNTRY) = range(30)

LOGIN_NAMES = ["Amazon", "Google", "GitHub", "Netflix", "Dropbox",
               "Twitter", "Facebook", "LinkedIn", "Reddit", "Spotify"]
NOTE_NAMES = ["Wifi Password", "Server Notes", "Recovery Codes", "Meeting Notes", "Todo"]
CARD_NAMES = ["Visa Card", "Mastercard", "Amex", "Debit Card"]
IDENTITY_NAMES = ["Home Address", "Work Identity", "Passport Info"]


def field_max_len(idx):
    return BW_MAX_FIELD if idx < BW_FIELD_512_COUNT else BW_MAX_EXT_FIELD


def b(s):
    return s.encode("latin-1")


BW_FOLDER_NONCE_HI = 0xFFFFFFFE
BW_ORG_NONCE_HI = 0xFFFFFFFD


def pack_item(item_type, folder_idx, org_idx, guid, fields, nonce_hi, archived=False):
    """fields: dict {field_index: bytes}. Returns the on-disk record body,
    matching BwStoreAddItem's own p[0]=type / folderIndex / orgIndex / guid /
    fieldLens[30] / concatenated-field-bytes layout exactly. Each field is
    double-encrypted (nonce_hi, field_index) exactly like BwStoreAddItem's
    own write loop, so BEFORE encryption is where the max-length truncation
    happens (matches the real app - EncCrypt is length-preserving).
    archived: plaintext byte, own dedicated header slot (bwArchivedByteOff,
    Store.c) - not encrypted, same as folder/org index."""
    lens = [0] * BW_FIELD_COUNT
    ordered = []
    for i in range(BW_FIELD_COUNT):
        v = fields.get(i, b"")
        maxlen = field_max_len(i)
        if len(v) > maxlen - 1:
            v = v[:maxlen - 1]
        lens[i] = len(v)
        ordered.append(double_encrypt(v, nonce_hi, i) if v else v)

    guid_bytes = guid.encode("latin-1")[:BW_GUID_LEN].ljust(BW_GUID_LEN, b"\x00")

    hdr = bytearray()
    hdr.append(item_type & 0xFF)
    hdr += struct.pack(">H", folder_idx)
    hdr += struct.pack(">H", org_idx)
    hdr += guid_bytes
    for ln in lens:
        hdr += struct.pack(">H", ln)
    hdr.append(1 if archived else 0)
    assert len(hdr) == BW_HDR_FIXED_SIZE, (len(hdr), BW_HDR_FIXED_SIZE)

    return bytes(hdr) + b"".join(ordered)


def pack_marker(marker_type, index, name):
    """Folder (type=0) or org (BW_ORG_MARKER_TYPE) marker record - same
    physical shape as a real item, but only field[0] (name) populated, guid
    all-zero, and the folderIndex slot reused to carry the marker's own
    index (see AddMarker in Store.c). AddMarker's own nonce is
    (bwFolderNonceHi/bwOrgNonceHi, index) - NOT (index, F_NAME) like a real
    item's fields would be."""
    nonce_hi = BW_FOLDER_NONCE_HI if marker_type == 0 else BW_ORG_NONCE_HI
    name_bytes = b(name)
    enc = double_encrypt(name_bytes, nonce_hi, index)
    hdr = bytearray()
    hdr.append(marker_type & 0xFF)
    hdr += struct.pack(">H", index)
    hdr += struct.pack(">H", BW_ORG_NONE)
    hdr += b"\x00" * BW_GUID_LEN
    lens = [0] * BW_FIELD_COUNT
    lens[F_NAME] = len(name_bytes)
    for ln in lens:
        hdr += struct.pack(">H", ln)
    hdr.append(0)	# archived - meaningless on marker records, always 0
    assert len(hdr) == BW_HDR_FIXED_SIZE, (len(hdr), BW_HDR_FIXED_SIZE)
    return bytes(hdr) + enc


def build_records(item_count, folder_count=1, org_count=3, archived_count=0):
    recs = []  # list of (item_type_byte, body_bytes) in insertion order

    for i in range(folder_count):
        recs.append(pack_marker(0, i, "Folder %d" % (i + 1)))
    for i in range(org_count):
        recs.append(pack_marker(BW_ORG_MARKER_TYPE, i, "Org %d" % (i + 1)))

    # Evenly spread archived items across the whole range (every Nth item)
    # rather than clustering them at the end - a more representative mix for
    # filter/sort benchmarking than "all archived items sort last". Offset by
    # (stride - 1) rather than landing on i % stride == 0 - folder_idx below
    # uses that exact same "i % 5 == 0" test, and a real benchmark run found
    # the two conditions colliding (every folder-assigned item ending up
    # archived too) makes the folder filter's own test data degenerate (0
    # non-archived matches) purely as a fixture artifact, not a real bug.
    archived_stride = (item_count // archived_count) if archived_count else 0

    for i in range(item_count):
        item_type = (i % 4) + 1  # cycle Login/Note/Card/Identity
        archived = archived_count > 0 and archived_stride > 0 and (i % archived_stride == archived_stride - 1)
        guid = "11111111-1111-1111-1111-%012d" % i
        folder_idx = 0 if (folder_count and i % 5 == 0) else BW_FOLDER_NONE
        org_idx = (i % org_count) if (org_count and i % 7 == 0) else BW_ORG_NONE
        if org_idx != BW_ORG_NONE:
            folder_idx = BW_FOLDER_NONE  # never both, matches PROTOCOL.md's own model

        fields = {}
        if item_type == BW_TYPE_LOGIN:
            fields[F_NAME] = b("%s %d" % (LOGIN_NAMES[i % len(LOGIN_NAMES)], i))
            fields[F_USER] = b("user%d@example.com" % i)
            fields[F_PASS] = b("password%d!" % i)
            fields[F_URI] = b("https://example%d.com" % (i % 50))
        elif item_type == BW_TYPE_CARD:
            fields[F_NAME] = b("%s %d" % (CARD_NAMES[i % len(CARD_NAMES)], i))
            fields[F_CARD_HOLDER] = b("Test Holder")
            fields[F_CARD_BRAND] = b("Visa")
            fields[F_CARD_NUMBER] = b("4111111111111111")
        elif item_type == BW_TYPE_IDENTITY:
            fields[F_NAME] = b("%s %d" % (IDENTITY_NAMES[i % len(IDENTITY_NAMES)], i))
            fields[F_ID_FIRST] = b("Test")
            fields[F_ID_LAST] = b("User")
        else:  # note
            fields[F_NAME] = b("%s %d" % (NOTE_NAMES[i % len(NOTE_NAMES)], i))
        if i % 13 == 0:
            fields[F_NOTES] = b("Test notes for item %d." % i)

        # Must match write_pdb's own uid assignment (enumerate(records),
        # i.e. this record's final position in the recs list) - EncCrypt's
        # nonceHi is the record's REAL DmRecordInfo uniqueID, which we
        # control directly via the PDB record list's own uniqueID field
        # (see write_pdb), not anything derived independently here.
        uid = folder_count + org_count + i
        recs.append(pack_item(item_type, folder_idx, org_idx, guid, fields, uid, archived=archived))

    return recs


# ---- PDB writer (adapted from address35/tools/gen_fake_records.py) -----
MAC_EPOCH_OFFSET = 2082844800   # seconds between 1904-01-01 and 1970-01-01


def write_pdb(path, records):
    now = int(time.time()) + MAC_EPOCH_OFFSET
    num = len(records)

    header_size = 78
    reclist_size = 8 * num
    data_off = header_size + reclist_size  # no AppInfo/SortInfo/gap

    out = bytearray()
    out += b"BitWardenDB".ljust(32, b"\x00")
    out += struct.pack(">H", 0x0008)          # attributes: backup (matches BwStoreInit)
    out += struct.pack(">H", 0)               # version
    out += struct.pack(">I", now)             # creation
    out += struct.pack(">I", now)             # modification
    out += struct.pack(">I", 0)               # lastBackup
    out += struct.pack(">I", 0)               # modificationNumber
    out += struct.pack(">I", 0)               # appInfoID - none
    out += struct.pack(">I", 0)               # sortInfoID
    out += b"BWit"                             # bwDBType
    out += b"BWrd"                             # appCreator
    out += struct.pack(">I", (num + 1) & 0xFFFFFF)  # uniqueIDSeed
    out += struct.pack(">I", 0)               # nextRecordListID
    out += struct.pack(">H", num)             # numRecords

    offset = data_off
    for uid, data in enumerate(records):
        out += struct.pack(">I", offset)                   # localChunkID
        out += bytes([0x00])                                # attributes
        out += struct.pack(">I", uid & 0xFFFFFF)[1:]        # uniqueID (3 bytes BE)
        offset += len(data)
    for data in records:
        out += data

    with open(path, "wb") as fp:
        fp.write(out)
    return num


def main():
    item_count = int(sys.argv[1]) if len(sys.argv) > 1 else 780
    out_path = sys.argv[2] if len(sys.argv) > 2 else "BitWardenDB.pdb"
    archived_count = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    recs = build_records(item_count, archived_count=archived_count)
    n = write_pdb(out_path, recs)
    print("wrote %s (%d records: 1 folder + 3 org markers + %d items, %d archived, plaintext/no-op-encrypted)"
          % (out_path, n, item_count, archived_count))


if __name__ == "__main__":
    main()
