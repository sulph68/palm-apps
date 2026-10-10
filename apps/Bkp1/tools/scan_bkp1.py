#!/usr/bin/env python3
"""Scan a Bkp1 backup folder (or any .pdb/.prc files) for the record layouts that crash the
ORIGINAL Bkp1 1.2 restore ("MemoryMgr.c, Line:4365, NULL handle"; fixed in 1.2.1, see
apps/Bkp1_improved.md):
  - a deleted-record stub (no data) listed before a record that has data, or
  - an archived record (deleted flag + data) that is not the last record with data.
usage: scan_bkp1.py <folder or files...>"""
import struct, sys, os

def scan(path):
    d = open(path, 'rb').read()
    if len(d) < 78: return None
    name = d[:32].split(b'\0')[0].decode('latin1')
    attrs, = struct.unpack('>H', d[32:34])
    typ, crid = d[60:64].decode('latin1'), d[64:68].decode('latin1')
    nxt, n = struct.unpack('>IH', d[72:78])
    if attrs & 0x0001:                       # resource DB: no stubs possible
        return name, typ, crid, 'res', n, 0, None
    ents = [struct.unpack('>IB', d[78 + i * 8:83 + i * 8]) for i in range(n)]
    data_idx = [i for i, (o, a) in enumerate(ents) if o]
    stubs = [i for i, (o, a) in enumerate(ents) if not o]
    # Replay Bkp1's import: stubs are appended at once, each data record when the NEXT one is read.
    db, prev, first = [], None, False
    for i, (o, a) in enumerate(ents):
        if not o: db.append(None); continue
        if not first: first = True; prev = i; continue
        db.append(prev); prev = i
    crash = None
    for k in range(len(data_idx) - 1):
        if k >= len(db) or db[k] is None:
            crash = (k, 'a deleted stub sits where its record should be'); break
        if ents[db[k]][1] & 0x80:            # deleted flag on a record with data: DmGetRecord refuses it
            crash = (k, 'archived record'); break
    return name, typ, crid, 'rec', n, len(stubs), crash

files = []
for a in sys.argv[1:]:
    if os.path.isdir(a):
        files += sorted(os.path.join(a, f) for f in os.listdir(a))
    else: files.append(a)
bad = 0
for f in files:
    try: r = scan(f)
    except Exception as e: print('%-16s unreadable: %s' % (os.path.basename(f), e)); continue
    if not r: continue
    name, typ, crid, kind, n, stubs, crash = r
    if crash:
        bad += 1
        print('CRASH  %-14s %-32s %s/%s  %d records, %d deleted stubs - Bkp1 1.2 restore dies at data record #%d (%s)'
              % (os.path.basename(f), name, typ, crid, n, stubs, crash[0], crash[1]))
    elif stubs:
        print('stubs  %-14s %-32s %s/%s  %d records, %d deleted stubs (all at the end - restores, last record moves behind them)'
              % (os.path.basename(f), name, typ, crid, n, stubs))
print('%d file(s) scanned, %d would crash the original Bkp1 1.2 restore' % (len(files), bad))
