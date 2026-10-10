#!/usr/bin/env python3
"""
gen_bulk_datebook.py - build a DatebookDB.pdb with N Datebook35 records for
record-count-scaled testing (Sec35 SET/rekey timing at volume, day/agenda scroll
performance).

Datebook35 owns its DB format, so hand-packing is safe here (single reader) --
see the project's cryptonote-memodb-fixture-hand-packed-bytes-trap note. Byte
layout taken directly from DateDB.h/DateDB.c (ApptPack) and cross-checked against
Sec35's own DB_FLAGS_OFF/DB_F_*/ApptPrefix constants (apps/sec35/sec35.c), which
independently re-derive the same packed format for rekeying:

    plaintext record: [when:8][flags:2][description\0][note\0]
    when = ApptDateTimeType: {startTime:TimeType(2) endTime:TimeType(2) date:DateType(4)}
        TimeType = {hours:UInt8 minutes:UInt8}; untimed sentinel = 0xFF,0xFF each
        DateType = {year:Int16 BE (full year - 1904), month:UInt8, day:UInt8}
    flags byte (offset 8; offset 9 is compiler padding, always 0):
        when=0x80 alarm=0x40 repeat=0x20 note=0x10 exceptions=0x08 description=0x04
        encrypted=0x02 endDate=0x01
    Deliberately omits alarm/repeat/exceptions/endDate -- keeps every record a
    plain single-day appointment, description+note only, avoiding needing to get
    those optional sections' byte order exactly right for a test fixture.

AppInfo layout from ApptAppInfoType (DateDB.h): standard 16-category block plus a
startOfWeek tail field.

    python3 gen_bulk_datebook.py [out.pdb] [target_total]
    python3 gen_bulk_datebook.py DatebookDB_bulk.pdb 100     # default
"""
import struct
import sys
import time

MAC_EPOCH_OFFSET = 2082844800
DM_REC_NUM_CATEGORIES = 16
DM_CATEGORY_LENGTH = 16
CATEGORIES = ["Unfiled", "Business", "Personal"]

DB_F_DESC = 0x04
DB_F_NOTE = 0x10
UNTIMED = (0xFF, 0xFF)


def b(s):
    return s.encode("latin-1")


def pack_record(start, end, date, description, note):
    year, month, day = date
    out = bytearray()
    out += bytes([start[0], start[1]])              # startTime
    out += bytes([end[0], end[1]])                   # endTime
    out += struct.pack(">HBB", year & 0xFFFF, month, day)      # date
    flags = 0
    if description:
        flags |= DB_F_DESC
    if note:
        flags |= DB_F_NOTE
    out += bytes([flags, 0])                          # flags byte + pad byte
    if description:
        out += b(description) + b"\x00"
    if note:
        out += b(note) + b"\x00"
    return bytes(out)


def build_items(target_total=100):
    import datetime
    items = []
    base_uid = 40000
    today = datetime.date.today()
    for i in range(target_total):
        uid = base_uid + i
        # spread across today +/- ~target_total/2 days, so a fresh install lands
        # visibly in the default Day/Week/Agenda view without date navigation.
        d = today + datetime.timedelta(days=i - target_total // 2)
        date = (d.year - 1904, d.month, d.day)
        if i % 4 == 0:
            start, end = UNTIMED, UNTIMED
        else:
            hour = 8 + (i % 10)
            start = (hour, 0)
            end = (hour + 1, 0)
        desc = "Bulk event %03d" % i
        note = "" if i % 3 == 0 else ("Details for event %03d." % i)
        items.append({
            "uid": uid,
            "category": i % len(CATEGORIES),
            "data": pack_record(start, end, date, desc, note),
        })
    return items


def build_appinfo():
    buf = bytearray()
    buf += struct.pack(">H", 0)                                    # renamedCategories
    for i in range(DM_REC_NUM_CATEGORIES):
        name = CATEGORIES[i] if i < len(CATEGORIES) else ""
        buf += name.encode("latin-1")[:DM_CATEGORY_LENGTH - 1].ljust(DM_CATEGORY_LENGTH, b"\x00")
    buf += bytes(range(DM_REC_NUM_CATEGORIES))                      # categoryUniqIDs 0..15
    buf += bytes([DM_REC_NUM_CATEGORIES - 1])                       # lastUniqID
    buf += bytes([0])                                               # reserved1
    buf += struct.pack(">H", 0)                                     # reserved2
    buf += bytes([0])                                               # startOfWeek (Sunday)
    buf += bytes([0])                                               # reserved
    return bytes(buf)


def write_pdb(path, items, appinfo):
    now = int(time.time()) + MAC_EPOCH_OFFSET
    num = len(items)

    header_size = 78
    reclist_size = 8 * num
    gap = 2
    appinfo_off = header_size + reclist_size + gap
    data_off = appinfo_off + len(appinfo)

    out = bytearray()
    out += b"Datebook35DB".ljust(32, b"\x00")  # must match datebookDBName (Datebook.h)
                                                 # exactly, or install creates a SEPARATE
                                                 # duplicate DB alongside the real one
                                                 # instead of replacing it
    out += struct.pack(">H", 0x0008)          # attributes: backup
    out += struct.pack(">H", 0)               # version
    out += struct.pack(">I", now)
    out += struct.pack(">I", now)
    out += struct.pack(">I", 0)
    out += struct.pack(">I", 0)
    out += struct.pack(">I", appinfo_off)
    out += struct.pack(">I", 0)
    out += b"DATA"
    out += b"Db35"
    out += struct.pack(">I", (max(it["uid"] for it in items) + 1) & 0xFFFFFF)
    out += struct.pack(">I", 0)
    out += struct.pack(">H", num)

    offset = data_off
    for it in items:
        out += struct.pack(">I", offset)
        out += bytes([it["category"] & 0x0F])
        out += struct.pack(">I", it["uid"] & 0xFFFFFF)[1:]
        offset += len(it["data"])
    out += b"\x00" * gap
    out += appinfo
    for it in items:
        out += it["data"]

    with open(path, "wb") as fp:
        fp.write(out)
    return num


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "DatebookDB_bulk.pdb"
    target = int(sys.argv[2]) if len(sys.argv) > 2 else 100

    items = build_items(target)
    n = write_pdb(out_path, items, build_appinfo())
    print("wrote %s (%d appointments)" % (out_path, n))


if __name__ == "__main__":
    main()
