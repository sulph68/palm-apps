#!/usr/bin/env python3
"""
gen_bulk_memodb.py - build a MemoDB.pdb with ~100 records for record-count-
scaled testing (Sec35 SET/rekey timing at volume, CryptoNote migration + list-
scroll performance).

Does NOT hand-pack new record bytes from scratch -- see project memory
(cryptonote-memodb-fixture-hand-packed-bytes-trap): a from-scratch hand-packed
MemoDB.pdb satisfied the ROM Memo Pad app's own lenient reader but crashed
CryptoNote's stricter migration path, and the actual root cause was never
pinned down at the byte level. Instead this CLONES the raw record bytes
already sitting in the checked-in, real-app-driven, twice-migration-tested
MemoDB.pdb (gen_memodb.py) bit-for-bit, varying only the record-list's
category ATTRIBUTE BYTE (outside the record's own data, already an
established-safe thing to patch -- gen_memodb.py's own category workaround
does the same). No new record CONTENT bytes are ever synthesized.

    python3 gen_bulk_memodb.py [out.pdb] [target_total]
    python3 gen_bulk_memodb.py MemoDB_bulk.pdb 100     # default
"""
import struct
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "MemoDB.pdb")

MAC_EPOCH_OFFSET = 2082844800


def read_source():
    data = open(SRC, "rb").read()
    numRecords = struct.unpack(">H", data[76:78])[0]
    appInfoID = struct.unpack(">I", data[52:56])[0]
    reclist_off = 78
    recs = []
    offsets = []
    for i in range(numRecords):
        off = reclist_off + i * 8
        localChunkID = struct.unpack(">I", data[off:off + 4])[0]
        attr = data[off + 4]
        offsets.append(localChunkID)
        recs.append({"attr": attr, "offset": localChunkID})
    offsets.append(len(data))  # sentinel for the last record's end
    for i, r in enumerate(recs):
        r["bytes"] = data[offsets[i]:offsets[i + 1]]
    appinfo = data[appInfoID:offsets[0]]
    return recs, appinfo


def write_pdb(path, recs_out, appinfo):
    now = int(__import__("time").time()) + MAC_EPOCH_OFFSET
    num = len(recs_out)

    header_size = 78
    reclist_size = 8 * num
    gap = 2
    appinfo_off = header_size + reclist_size + gap
    data_off = appinfo_off + len(appinfo)

    out = bytearray()
    out += b"MemoDB".ljust(32, b"\x00")
    out += struct.pack(">H", 0x0008)
    out += struct.pack(">H", 0)
    out += struct.pack(">I", now)
    out += struct.pack(">I", now)
    out += struct.pack(">I", 0)
    out += struct.pack(">I", 0)
    out += struct.pack(">I", appinfo_off)
    out += struct.pack(">I", 0)
    out += b"DATA"
    out += b"memo"
    max_uid = max(r["uid"] for r in recs_out)
    out += struct.pack(">I", (max_uid + 1) & 0xFFFFFF)
    out += struct.pack(">I", 0)
    out += struct.pack(">H", num)

    offset = data_off
    for r in recs_out:
        out += struct.pack(">I", offset)
        out += bytes([r["attr"] & 0xFF])
        out += struct.pack(">I", r["uid"] & 0xFFFFFF)[1:]
        offset += len(r["bytes"])
    out += b"\x00" * gap
    out += appinfo
    for r in recs_out:
        out += r["bytes"]

    with open(path, "wb") as fp:
        fp.write(out)
    return num


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "MemoDB_bulk.pdb"
    target = int(sys.argv[2]) if len(sys.argv) > 2 else 100

    templates, appinfo = read_source()
    base_uid = 20000
    recs_out = []
    i = 0
    while len(recs_out) < target:
        t = templates[i % len(templates)]
        recs_out.append({
            "bytes": t["bytes"],                       # bit-for-bit clone, never modified
            "attr": (t["attr"] & 0xF0) | ((i % 2) + 1),  # cycle category 1/2, keep the dirty bit
            "uid": base_uid + i,
        })
        i += 1

    n = write_pdb(out_path, recs_out, appinfo)
    print("wrote %s (%d records, cloned from %d validated templates)" % (out_path, n, len(templates)))


if __name__ == "__main__":
    main()
