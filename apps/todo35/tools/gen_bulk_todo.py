#!/usr/bin/env python3
"""
gen_bulk_todo.py - build a ToDoDB.pdb with N ToDo35 records for record-count-scaled
testing (Sec35 SET/rekey timing at volume, list-scroll performance).

ToDo35 owns its DB format (not a ROM-shared format like MemoDB), so hand-packing is
safe here the same way Address35's own generator is -- see the project's
cryptonote-memodb-fixture-hand-packed-bytes-trap note: hand-packing is only risky
when MULTIPLE independent readers exist. Byte layout taken directly from
ToDoDB.h/ToDoDB.c (ToDoDBRecord, ToDoNewRecord) and cross-checked against Sec35's
own TD_PRI_OFF/TD_DESC_OFF/TD_ENC constants (apps/sec35/sec35.c):

    plaintext record: [dueDate:4][priority:1][description\0][note\0]
    dueDate: {year:Int16 BE (full year - 1904), month:UInt8, day:UInt8}
             no-date sentinel: year=0xFFFF, month=0, day=0
    priority byte: bit7=complete(0x80), bit6=Sec35-encrypted(0x40, must stay clear
                   here), bits0-5=priority value (1-5 normally)

AppInfo layout from ToDoAppInfoType (ToDoDB.h): standard 16-category block plus
ToDo-specific dirtyAppInfo/sortOrder tail fields.

    python3 gen_bulk_todo.py [out.pdb] [target_total]
    python3 gen_bulk_todo.py ToDoDB_bulk.pdb 100     # default
"""
import struct
import sys
import time

MAC_EPOCH_OFFSET = 2082844800
DM_REC_NUM_CATEGORIES = 16
DM_CATEGORY_LENGTH = 16
CATEGORIES = ["Unfiled", "Business", "Personal"]

NO_DATE = (0xFFFF, 0, 0)


def b(s):
    return s.encode("latin-1")


def pack_record(due, priority, description, note):
    year, month, day = due
    out = bytearray()
    out += struct.pack(">HBB", year & 0xFFFF, month, day)   # dueDate: Int16 BE (unsigned-packed) + 2 UInt8
    out += bytes([priority & 0x3F])                # priority 1-5, complete/enc bits clear
    out += b(description) + b"\x00"
    out += b(note) + b"\x00"
    return bytes(out)


def build_items(target_total=100):
    items = []
    base_uid = 30000
    for i in range(target_total):
        uid = base_uid + i
        if i % 7 == 0:
            due = NO_DATE                          # undated items mixed in
        else:
            month = 1 + (i % 12)
            day = 1 + (i % 28)
            due = (131, month, day)                 # year 2035 - 1904 = 131
        priority = 1 + (i % 5)
        desc = "Bulk task %03d - follow up item" % i
        note = "" if i % 3 == 0 else ("Note for task %03d: check details." % i)
        items.append({
            "uid": uid,
            "category": i % len(CATEGORIES),
            "data": pack_record(due, priority, desc, note),
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
    buf += struct.pack(">H", 0)                                     # dirtyAppInfo
    buf += bytes([0])                                               # sortOrder (soDueDatePriority)
    buf += bytes([0])                                               # reserved3
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
    out += b"ToDo35DB".ljust(32, b"\x00")     # must match toDoDBName (ToDo.h) exactly,
                                                # or install creates a SEPARATE duplicate
                                                # DB alongside the real one instead of
                                                # replacing it
    out += struct.pack(">H", 0x0008)          # attributes: backup
    out += struct.pack(">H", 0)               # version
    out += struct.pack(">I", now)
    out += struct.pack(">I", now)
    out += struct.pack(">I", 0)
    out += struct.pack(">I", 0)
    out += struct.pack(">I", appinfo_off)
    out += struct.pack(">I", 0)
    out += b"DATA"
    out += b"Td35"
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
    out_path = sys.argv[1] if len(sys.argv) > 1 else "ToDoDB_bulk.pdb"
    target = int(sys.argv[2]) if len(sys.argv) > 2 else 100

    items = build_items(target)
    n = write_pdb(out_path, items, build_appinfo())
    print("wrote %s (%d ToDo items)" % (out_path, n))


if __name__ == "__main__":
    main()
