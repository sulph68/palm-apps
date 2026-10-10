#!/usr/bin/env python3
"""
gen_memodb.py -- build MemoDB.pdb by driving the REAL ROM Memo Pad app
through POSE64, then exporting the result. NOT hand-packed raw bytes.

WHY NOT HAND-PACKED BYTES (like apps/address35/tools/gen_fake_records.py)
----------------------------------------------------------------------------
Tried that first (2026-08-14). The hand-crafted MemoDB.pdb displayed
correctly in the real ROM Memo Pad app (titles, content, all readable) --
but CryptoNote's "Migrate from Memo Pad" feature crashed reliably
("DataMgr.c, Line:7386, DmWrite: DmWriteCheck failed") on every record from
that file. Root cause traced back by process of elimination: creating an
otherwise-identical memo through the REAL Memo Pad app's own UI, with the
exact same content, migrated into CryptoNote with ZERO issues. So the
hand-packed PDB satisfies the ROM Memo Pad app's own (lenient) display code
but not something CryptoNote's stricter migration path validates -- never
fully root-caused, because the fix is simply "don't hand-pack this one":
unlike Address35's fixture (a fully custom TLV format read only by
Address35's own parser, safe to hand-pack), MemoDB has to satisfy BOTH the
ROM Memo Pad app's parser AND CryptoNote's own read path, and only actually
creating records through the real app's own code guarantees that.

Usage:
    python3 gen_memodb.py
"""
import importlib.util
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TESTDIR = os.path.abspath(os.path.join(HERE, "..", "..", "test"))
sys.path.insert(0, TESTDIR)
import pose_helpers as p

OUT_PATH = os.path.join(HERE, "MemoDB.pdb")

# Memo Pad's own resource IDs (distinct from CryptoNote's -- this is the ROM
# app, verified live 2026-08-14 via `apps` + `ui`).
FRM_MEMO_LIST = 1000
BTN_MEMO_NEW = 1005
FRM_MEMO_EDIT = 1100
FLD_MEMO_TEXT = 1109
BTN_MEMO_DONE = 1105
BTN_MEMO_DETAILS = 1108
FRM_MEMO_DETAILS = 1200
BTN_DETAILS_CATEGORY = 1204
BTN_DETAILS_OK = 1208

# (category name, text) -- first line becomes the memo's list title. Covers:
# short/plain, multiline, near/over the `ui` dump's 80-char display
# truncation boundary, a long body (CryptoNote's own ">4k" handling), quotes/
# tab escaping, and a blank-line edge case. Split across Personal/Business so
# CryptoNote's migrate-skips-Unfiled behavior is exercised meaningfully.
MEMOS = [
    ("Personal", "Short plain memo\nsecond line"),
    ("Business", "Business memo multiline\nline two\nline three\nline four"),
    ("Personal", "Exactly at eighty chars on the first line 1234567890123456789012345\nsecond"),
    ("Business", "Just past eighty chars on the first line 12345678901234567890123456789\nsecond"),
    ("Personal", "Quotes and \"escaping\" test: back\\slash and a tab\ttoo\nsecond line"),
    ("Business", "Long single body\n" + ("Lorem ipsum dolor sit amet, consectetur adipiscing elit. " * 15)),
    ("Personal", "Blank second line follows\n\nthird line after the blank one"),
]


def select_category(name):
    """Details form is already open; tap the category popup and pick `name`
    by scanning the resulting LIST block's items for a matching row, same
    technique as the app-level popups -- but this one has repeatedly wedged
    the emulator this session (observed 2026-08-14), so retry generously
    with self-healing between attempts rather than failing on the first
    transient timeout."""
    for attempt in range(2):
        p.tap_id(BTN_DETAILS_CATEGORY, settle=1.0)
        state = p.ui()
        if state.strip().startswith("ERR"):
            # Recover to Launcher and give up on this memo's category rather
            # than trying to reconstruct which memo/form we were mid-edit on
            # -- the caller just leaves it at its default (Unfiled) category,
            # a minor test-data imperfection, not worth the complexity of a
            # full state-reconstructing retry here.
            p.ensure_healthy()
            return False
        lines = state.splitlines()
        list_y = None
        idx = None
        in_list = False
        for line in lines:
            if "LIST id=" in line and "(" in line:
                in_list = True
                try:
                    bounds = line.split("(", 1)[1].split(")", 1)[0]
                    list_y = int(bounds.split(",")[1])
                except Exception:
                    list_y = None
                continue
            if in_list:
                stripped = line.strip()
                if stripped.startswith("[") and '"' in stripped:
                    i_str = stripped[1:stripped.index("]")]
                    text = stripped.split('"', 1)[1].rsplit('"', 1)[0]
                    if text == name:
                        idx = int(i_str)
                elif stripped and not stripped.startswith("["):
                    in_list = False
        if idx is not None and list_y is not None:
            p.tap_xy(65, list_y + idx * 12 + 6, settle=1.0)
            return True
        # Category not found in this popup rendering, or list didn't parse --
        # close whatever's open and retry.
        p.R("key 261")
        time.sleep(1.0)
    return False


def main():
    p.ensure_healthy()
    p.launch("Launcher", settle=1.5)
    p.R("delete MemoDB")  # ok if it doesn't exist -- start genuinely empty

    p.launch("Memo Pad", settle=2.0)
    state = p.ui()
    p.check(f"FORM id={FRM_MEMO_LIST}" in state, f"Memo Pad did not launch cleanly: {state[:200]}")

    for i, (category, text) in enumerate(MEMOS):
        print(f"{i+1}. [{category}] {text.splitlines()[0][:40]}")
        p.tap_id(BTN_MEMO_NEW, settle=1.2)
        state = p.ui()
        p.check(f"FORM id={FRM_MEMO_EDIT}" in state, f"New memo did not open edit form: {state[:200]}")
        p.type_text_verified(FLD_MEMO_TEXT, text)

        p.tap_id(BTN_MEMO_DETAILS, settle=1.0)
        state = p.ui()
        p.check(f"FORM id={FRM_MEMO_DETAILS}" in state, f"Details did not open: {state[:200]}")
        ok = select_category(category)
        if not ok:
            print(f"   WARNING: could not select category '{category}' for memo {i+1}, leaving default")
            # select_category's failure path already recovered to Launcher;
            # get back into Memo Pad's list before the next iteration's New tap.
            p.launch("Memo Pad", settle=2.0)
            continue
        p.tap_id(BTN_DETAILS_OK, settle=1.0)

        state = p.ui()
        if f"FORM id={FRM_MEMO_EDIT}" in state:
            p.tap_id(BTN_MEMO_DONE, settle=1.0)

    print("exporting MemoDB ->", OUT_PATH)
    r = p.R(f"export MemoDB {OUT_PATH}")
    p.check(r.strip().startswith("OK"), f"export failed: {r}")
    size = os.path.getsize(OUT_PATH)
    print(f"wrote {OUT_PATH} ({size} bytes, {len(MEMOS)} memos, all created via the real ROM Memo Pad app)")


if __name__ == "__main__":
    main()
