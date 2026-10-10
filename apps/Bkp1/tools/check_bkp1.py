#!/usr/bin/env python3
"""List and validate every file of a Bkp1 backup folder, in restore order (file index).
Flags anything a PDB/PRC reader would reject: short file, unterminated name, entry table or
data offsets outside the file, offsets not ascending. usage: check_bkp1.py <backup folder>"""
import struct, sys, os

def check(path):
    d = open(path, 'rb').read(); size = len(d); bad = []
    if size < 78: return '?', '', '', 0, size, ['file shorter than a database header (%d bytes)' % size]
    rawname = d[:32]
    if b'\0' not in rawname: bad.append('name not NUL-terminated')
    name = rawname.split(b'\0')[0].decode('latin1')
    if not name: bad.append('empty name')
    attrs, = struct.unpack('>H', d[32:34])
    appi, sorti = struct.unpack('>II', d[52:60])
    typ, crid = d[60:64].decode('latin1'), d[64:68].decode('latin1')
    nxt, n = struct.unpack('>IH', d[72:78])
    res = attrs & 1; esz = 10 if res else 8
    if nxt: bad.append('nextRecordListID not 0')
    if 78 + n * esz > size: bad.append('entry table (%d entries) runs past end of file' % n); return name, typ, crid, n, size, bad
    offs = []
    for i in range(n):
        o = struct.unpack('>I', d[78 + i * esz + (6 if res else 0):78 + i * esz + (10 if res else 4)])[0]
        if o: offs.append(o)
    chain = [x for x in (appi, sorti) if x] + offs
    for x in chain:
        if x >= size: bad.append('offset %d is past end of file (%d)' % (x, size)); break
    if any(b <= a for a, b in zip(chain, chain[1:])): bad.append('offsets not ascending')
    if chain and chain[0] < 78 + n * esz: bad.append('first data offset inside the entry table')
    return name, typ, crid, n, size, bad

folder = sys.argv[1]
files = sorted(f for f in os.listdir(folder))
nbad = 0
for f in files:
    name, typ, crid, n, size, bad = check(os.path.join(folder, f))
    print('%-14s %-32s %s/%s %5d entries %8d bytes  %s' % (f, name, typ, crid, n, size, 'BAD: ' + '; '.join(bad) if bad else 'ok'))
    nbad += bool(bad)
idx = sorted(int(f.split('.')[0], 16) for f in files if f.split('.')[0].isalnum() and len(f.split('.')[0]) == 8)
gaps = [i for i in range(len(idx)) if idx[i] != i]
print('%d files, %d bad%s' % (len(files), nbad, '; file numbering has a gap at index %d (restore stops counting there)' % gaps[0] if gaps else ''))
