#!/usr/bin/env python3
"""
gen_fake_records.py - build an Address35DB.pdb full of fake contacts for testing.

Reusable: run whenever you need sample data. It emits a native Address35 database
(.pdb) in the exact on-disk TLV format (see AddressDB35.c), then you install it:

    python3 gen_fake_records.py            # -> ./Address35DB.pdb
    python3 rc.py "install .../Address35DB.pdb"   # from emulator/, then launch

Coverage: 26 contacts (first+last A..Z, already sorted) plus a few edge cases,
spanning EVERY modern field type and, across the set, every label/subtype
permutation (all phone labels, all social networks, all relation types, all
anniversary types, both address styles, birthday with/without year, primary
flags, tags, notes, custom fields, gender, pronouns, prefixes, etc.).

The record/format constants below mirror AddressDB35.h - keep them in sync.
"""
import struct, sys, time

# ---- field types (AddressDB35.h) ---------------------------------------
(a35fNone, a35fLast, a35fFirst, a35fMiddle, a35fNickname, a35fCompany, a35fTitle,
 a35fPhone, a35fEmail, a35fIM, a35fURL, a35fAddress, a35fBirthday, a35fRelation,
 a35fTag, a35fNote, a35fCustom, a35fCity, a35fState, a35fZip, a35fCountry,
 a35fSocial, a35fGender, a35fPronouns, a35fAnniversary, a35fOther, a35fPrefix
 ) = range(27)

a35PrimaryFlag = 0x80
# phone labels
plMobile, plHome, plWork, plFax, plMain, plPager, plOther = range(7)
# social networks
socInstagram, socFacebook, socWhatsApp, socX, socTelegram, socSignal, socOther = range(7)
# address labels
alHome, alWork, alOther = range(3)
# relation types
(relSpouse, relPartner, relChild, relParent, relSibling, relAssistant,
 relManager, relFriend, relOther) = range(9)
# anniversary types (index 4 == "Other": custom name trails the date)
annivTypes = 5
a35AnnivOther = 4

A35_REC_VERSION = 1
A35_HDR_SIZE = 10

# On disk, string field values are stored NUL-terminated (A35AddStr uses
# StrLen(s)+1); only these carry raw binary and are stored as-is.
BINARY_TYPES = {a35fRelation, a35fTag}

# ---- categories + tags stored in AppInfo -------------------------------
CATEGORIES = ["Unfiled", "Business", "Personal", "Family"]   # indices 0..3
TAGS = ["VIP", "Lead", "Colleague", "Alumni"]                # ids 1..4

DM_REC_NUM_CATEGORIES = 16
DM_CATEGORY_LENGTH = 16
A35_MAX_TAGS = 32
A35_TAG_NAME_LEN = 16

FIRST = ["Aaron", "Bella", "Carlos", "Diana", "Ethan", "Fiona", "Gabriel", "Hana",
         "Ivan", "Julia", "Kevin", "Luna", "Marco", "Nadia", "Oscar", "Priya",
         "Quinn", "Rosa", "Sam", "Tara", "Umar", "Vera", "Wade", "Xena", "Yuki", "Zara"]
LAST = ["Anderson", "Brown", "Chen", "Delgado", "Evans", "Ferrari", "Gomez", "Hansen",
        "Ito", "Jones", "Khan", "Lopez", "Miller", "Novak", "Owens", "Patel",
        "Quezada", "Rossi", "Silva", "Tanaka", "Ueda", "Vargas", "Wong", "Xu",
        "Yamada", "Zimmer"]

PREFIXES = ["Mr.", "Ms.", "Dr.", "Mx.", "Prof."]
GENDERS = ["Female", "Male", "Non-binary", "Prefer not to say"]
PRONOUNS = ["she/her", "he/him", "they/them", "ze/zir"]
SOC_HANDLE = ["@insta", "fb.me/x", "+15550100", "@xhandle", "@tg", "signal.me/x", None]
SOC_OTHER_PLATFORM = "Mastodon"


def b(s):
    """Bytes for a field value (Latin-1; test data is ASCII)."""
    return s.encode("latin-1")


def pack_record(misc, fields):
    """fields = list of (type, label, value_bytes) -> packed on-disk record."""
    body = b""
    off = A35_HDR_SIZE
    offLast = offFirst = offCompany = 0
    for (t, l, v) in fields:
        if t not in BINARY_TYPES:
            v = v + b"\x00"                 # NUL-terminate strings, like A35AddStr
        v_off = off + 4
        body += bytes([t & 0xFF, l & 0xFF]) + struct.pack(">H", len(v)) + v
        if t == a35fLast and offLast == 0:
            offLast = v_off
        elif t == a35fFirst and offFirst == 0:
            offFirst = v_off
        elif t == a35fCompany and offCompany == 0:
            offCompany = v_off
        off = v_off + len(v)
    hdr = bytes([A35_REC_VERSION, misc & 0xFF]) + struct.pack(">H", len(fields)) \
        + struct.pack(">HHH", offLast, offFirst, offCompany)
    return hdr + body


def sort_key(fields):
    """Match A35ComparePackedRecords: last, first, company; missing sorts first;
    caseless then case-sensitive."""
    have = {}
    for (t, l, v) in fields:
        if t in (a35fLast, a35fFirst, a35fCompany) and t not in have:
            have[t] = v.decode("latin-1")

    def key(t):
        if t not in have:
            return (0, "", "")
        return (1, have[t].lower(), have[t])
    return (key(a35fLast), key(a35fFirst), key(a35fCompany))


def relation_value(target_uid, rel_type):
    return struct.pack(">I", target_uid) + bytes([rel_type & 0xFF])


def build_contacts():
    """Return list of dicts: {uid, category, misc, fields}. uid is stable and used
    by relations; sorting happens later (by name) without breaking relations."""
    n = len(FIRST)
    base_uid = 1000
    out = []
    for i in range(n):
        uid = base_uid + i
        target_uid = base_uid + ((i + 1) % n)     # relate to the next contact
        f = []

        if i % 4 == 0:
            f.append((a35fPrefix, 0, b(PREFIXES[i % len(PREFIXES)])))
        f.append((a35fFirst, 0, b(FIRST[i])))
        if i % 3 == 0:
            f.append((a35fMiddle, 0, b(chr(ord('A') + (i % 26)) + ".")))
        f.append((a35fLast, 0, b(LAST[i])))
        if i % 5 == 0:
            f.append((a35fNickname, 0, b(FIRST[i][:3])))
        if i % 2 == 0:
            f.append((a35fCompany, 0, b(LAST[i] + " & Co")))
            f.append((a35fTitle, 0, b("Director of Ops")))

        # phone: primary + a second with a different label (spans all 7 labels)
        f.append((a35fPhone, (plMobile + i) % 7 | a35PrimaryFlag,
                  b("+1 555 01%02d" % i)))
        f.append((a35fPhone, (plWork + i) % 7, b("+1 555 09%02d" % i)))

        f.append((a35fEmail, a35PrimaryFlag if i % 2 == 0 else 0,
                  b("%s.%s@example.com" % (FIRST[i].lower(), LAST[i].lower()))))

        # social: cycle all networks; "Other" carries "Platform: handle"
        net = i % 7
        if net == socOther:
            f.append((a35fSocial, socOther, b("%s: @%s" % (SOC_OTHER_PLATFORM, FIRST[i].lower()))))
        else:
            f.append((a35fSocial, net, b(SOC_HANDLE[net] or "@x")))
        if i % 4 == 1:
            f.append((a35fIM, socWhatsApp, b("+1555%04d" % i)))
        if i % 3 == 1:
            f.append((a35fURL, 0, b("https://%s.example.com" % LAST[i].lower())))

        # address: even = combined TAB-delimited a35fAddress (all 3 labels);
        #          odd  = separate City/State/Zip/Country fields
        if i % 2 == 0:
            addr = "\t".join(["%d Main St" % (100 + i), "Suite %d" % (i + 1), "Apt %d" % i,
                              "Springfield", "CA", "9%04d" % i, "USA"])
            f.append((a35fAddress, (alHome + i) % 3, b(addr)))
        else:
            f.append((a35fCity, 0, b("Rivertown")))
            f.append((a35fState, 0, b("NY")))
            f.append((a35fZip, 0, b("1%04d" % i)))
            f.append((a35fCountry, 0, b("USA")))

        # birthday: even with year, odd year-unknown ("0000MMDD")
        mm = (i % 12) + 1
        dd = (i % 28) + 1
        yyyy = "19%02d" % (60 + (i % 40)) if i % 2 == 0 else "0000"
        f.append((a35fBirthday, 0, b("%s%02d%02d" % (yyyy, mm, dd))))

        # anniversary: cycle all types; Other appends a custom name after the date
        atype = i % annivTypes
        aval = "20%02d%02d%02d" % (i % 24, mm, dd)
        if atype == a35AnnivOther:
            aval += "First Met"
        f.append((a35fAnniversary, atype, b(aval)))

        # relation to the next contact, cycling all relation types
        f.append((a35fRelation, 0, relation_value(target_uid, i % 9)))

        f.append((a35fTag, 0, struct.pack(">H", (i % len(TAGS)) + 1)))  # tagId 1..4

        if i % 3 == 2:
            f.append((a35fNote, 0, b("Met at the %d conference. Follow up Q%d." % (2030 + i % 9, (i % 4) + 1))))
        if i % 4 == 2:
            f.append((a35fCustom, 0, b("Blood Type: O+")))
        if i % 3 == 0:
            f.append((a35fGender, 0, b(GENDERS[i % len(GENDERS)])))
        if i % 3 == 1:
            f.append((a35fPronouns, 0, b(PRONOUNS[i % len(PRONOUNS)])))
        if i % 5 == 2:
            f.append((a35fOther, 0, b("Favorite tea: genmaicha")))

        out.append({"uid": uid, "category": i % len(CATEGORIES), "misc": 0, "fields": f})

    # ---- edge cases (exercise the "sparse"/degenerate permutations) ----
    out.append({"uid": base_uid + 100, "category": 0, "misc": 0,
                "fields": [(a35fFirst, 0, b("Solo"))]})                       # first-name only
    out.append({"uid": base_uid + 101, "category": 1, "misc": 0,
                "fields": [(a35fCompany, 0, b("Acme Holdings")),
                           (a35fPhone, plMain | a35PrimaryFlag, b("+1 555 0000"))]})  # company only
    out.append({"uid": base_uid + 102, "category": 2, "misc": 0,
                "fields": [(a35fNickname, 0, b("Ghost")),
                           (a35fNote, 0, b("No name, just a nickname + note."))]})  # nickname only
    return out


# ---- PDB writer --------------------------------------------------------
MAC_EPOCH_OFFSET = 2082844800   # seconds between 1904-01-01 and 1970-01-01


def build_appinfo():
    buf = bytearray()
    buf += struct.pack(">H", 0)                                   # renamedCategories
    for i in range(DM_REC_NUM_CATEGORIES):                        # categoryLabels[16][16]
        name = CATEGORIES[i] if i < len(CATEGORIES) else ""
        buf += name.encode("latin-1")[:DM_CATEGORY_LENGTH - 1].ljust(DM_CATEGORY_LENGTH, b"\x00")
    buf += bytes(range(DM_REC_NUM_CATEGORIES))                    # categoryUniqIDs 0..15
    buf += bytes([DM_REC_NUM_CATEGORIES - 1])                     # lastUniqID
    buf += bytes([0])                                             # surnameTerm (Surname)
    buf += bytes([0])                                             # sortByCompany
    buf += bytes([0])                                             # reserved
    buf += struct.pack(">H", len(TAGS))                           # tagCount
    for i in range(A35_MAX_TAGS):                                 # tagNames[32][16]
        name = TAGS[i] if i < len(TAGS) else ""
        buf += name.encode("latin-1")[:A35_TAG_NAME_LEN - 1].ljust(A35_TAG_NAME_LEN, b"\x00")
    return bytes(buf)


def write_pdb(path, contacts, appinfo):
    now = int(time.time()) + MAC_EPOCH_OFFSET
    recs = [(c["uid"], c["category"], pack_record(c["misc"], c["fields"])) for c in contacts]
    num = len(recs)

    header_size = 78
    reclist_size = 8 * num
    gap = 2                                   # traditional placeholder gap
    appinfo_off = header_size + reclist_size + gap
    data_off = appinfo_off + len(appinfo)

    # header
    out = bytearray()
    out += b"Address35DB".ljust(32, b"\x00")
    out += struct.pack(">H", 0x0008)          # attributes: backup
    out += struct.pack(">H", 0)               # version
    out += struct.pack(">I", now)             # creation
    out += struct.pack(">I", now)             # modification
    out += struct.pack(">I", 0)               # lastBackup
    out += struct.pack(">I", 0)               # modificationNumber
    out += struct.pack(">I", appinfo_off)     # appInfoID
    out += struct.pack(">I", 0)               # sortInfoID
    out += b"DATA"
    out += b"Ad35"
    out += struct.pack(">I", (max(u for u, _, _ in recs) + 1) & 0xFFFFFF)  # uniqueIDSeed
    out += struct.pack(">I", 0)               # nextRecordListID
    out += struct.pack(">H", num)             # numRecords

    # record list (offsets filled after we know record sizes)
    offset = data_off
    for (uid, cat, data) in recs:
        out += struct.pack(">I", offset)                       # localChunkID
        out += bytes([cat & 0x0F])                             # attributes: category in low nibble
        out += struct.pack(">I", uid & 0xFFFFFF)[1:]           # uniqueID (3 bytes BE)
        offset += len(data)
    out += b"\x00" * gap
    out += appinfo
    for (_, _, data) in recs:
        out += data

    with open(path, "wb") as fp:
        fp.write(out)
    return num


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "Address35DB.pdb"
    contacts = build_contacts()
    contacts.sort(key=lambda c: sort_key(c["fields"]))          # physical name sort
    n = write_pdb(out_path, contacts, build_appinfo())
    print("wrote %s (%d contacts, all field types + label permutations)" % (out_path, n))


if __name__ == "__main__":
    main()
