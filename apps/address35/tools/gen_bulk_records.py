#!/usr/bin/env python3
"""
gen_bulk_records.py - extends gen_fake_records.py's rich 29-contact set with
enough additional simple synthetic contacts to reach a target total (default
100), for record-count-scaled testing (Sec35 SET/rekey timing at volume,
list-scroll performance) that the original 29-contact set is too small for.

Reuses gen_fake_records.py's own field-packing/PDB-writing code verbatim (same
on-disk TLV format, same AppInfo/category setup) -- the bulk contacts are
simpler (a handful of fields each, not full permutation coverage; that's
already what the base 29 are for) but built through the exact same functions,
so there's no new format risk.

    python3 gen_bulk_records.py [out.pdb] [target_total]
    python3 gen_bulk_records.py Address35DB.pdb 100     # default
"""
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_fake_records as base


def build_bulk_contacts(target_total=100):
    contacts = base.build_contacts()
    base_uid = 5000
    i = 0
    while len(contacts) < target_total:
        uid = base_uid + i
        f = [
            (base.a35fFirst, 0, base.b("Bulk%03d" % i)),
            (base.a35fLast, 0, base.b("Contact%03d" % i)),
            (base.a35fPhone, base.plMobile | base.a35PrimaryFlag,
             base.b("+1 555 %04d" % (i % 10000))),
            (base.a35fEmail, base.a35PrimaryFlag,
             base.b("bulk%03d@example.com" % i)),
            (base.a35fCity, 0, base.b("Springfield")),
            (base.a35fState, 0, base.b("CA")),
        ]
        contacts.append({
            "uid": uid,
            "category": i % len(base.CATEGORIES),
            "misc": 0,
            "fields": f,
        })
        i += 1
    return contacts


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "Address35DB.pdb"
    target = int(sys.argv[2]) if len(sys.argv) > 2 else 100

    contacts = build_bulk_contacts(target)
    contacts.sort(key=lambda c: base.sort_key(c["fields"]))
    n = base.write_pdb(out_path, contacts, base.build_appinfo())
    print("wrote %s (%d contacts: 29 rich + %d bulk)" % (out_path, n, n - 29))


if __name__ == "__main__":
    main()
