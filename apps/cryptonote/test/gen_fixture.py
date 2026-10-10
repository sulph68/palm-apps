#!/usr/bin/env python3
"""
gen_fixture.py -- build CryptoNoteDB_fixture.pdb, a byte-correct sample
database covering the main CryptoNote record permutations.

APPROACH: seed bulk plain-text content via a real MemoPad import, not typed
keystrokes
-----------------------------------------------------------------------------
Two lessons from building this fixture the first time (2026-08-14):

  1. Typing bulk content character-by-character through the emulator is slow
     (a ~1700-char memo took the whole run past its timeout) and the `ui`
     dump's own 80-char display truncation (PalmFormReader.cpp kMaxDisplayLen)
     makes verifying long typed content awkward.
  2. CryptoNote already has a real, working "Migrate from Memo Pad" import
     path (triggered automatically on launch when MemoPad has memos) that is
     exactly the kind of real-app-code-produced-the-bytes guarantee this
     fixture needs -- and exercising it here doubles as a functional check of
     the import feature itself.

So: gen_memodb.py builds a standard PalmOS MemoDB.pdb host-side (safe to
hand-pack raw bytes for -- MemoPad's format is trivial, no cryptography), this
script installs it and lets CryptoNote's own migration prompt pull it in, then
drives the smaller set of remaining actions that genuinely need live UI
interaction (typing a password, tapping the Private checkbox) on the imported
records. CryptoNote's own migration explicitly skips Unfiled memos on import
(see project memory: cryptonote-memopad-migration-fixes) -- gen_memodb.py's
memos are deliberately split across Unfiled/Personal/Business so both the
skip and the import paths get exercised.

Per-record encryption (Encrypt Memo) and Sec35 wrapping (Details > Private)
are still driven live -- those transformations only exist by calling the
app's real code, there is no host-side shortcut for them without
reimplementing CryptoNote's crypto, which is exactly the risk this whole
approach avoids.

COVERAGE
--------
 - 8 plain-text memos imported from MemoDB (see gen_memodb.py's MEMOS list):
   short/multiline/near-80-char-boundary/~1.7KB body/quotes+tab escaping/
   blank-line edge case, split across Unfiled/Personal/Business categories.
 - 1 of the imported memos re-opened and encrypted via Record > Encrypt Memo
   (password "fixture1").
 - 1 of the imported memos marked Private via Details (routes through Sec35
   mask/hide -- requires Sec35 password "fixturesec35", set here).

Requires: CryptoNote_v1.prc, Sec35.prc already built; MemoDB.pdb built by
gen_memodb.py (run that first, or this script does it automatically).

Usage:
    python3 gen_fixture.py
"""
import importlib.util
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cryptonote_test as t  # reuses tap_id/menu_select/type_text_verified/etc.

OUT_PATH = os.path.join(HERE, "CryptoNoteDB_fixture.pdb")
MEMODB_PATH = os.path.join(HERE, "MemoDB.pdb")


def main():
    if not os.path.exists(MEMODB_PATH):
        subprocess.run([sys.executable, os.path.join(HERE, "gen_memodb.py")], check=True)

    t.reset_environment()  # CryptoNoteDB deleted+reinstalled, verified empty
    t.setup_sec35_password("fixturesec35")

    print("installing MemoDB fixture + importing into CryptoNote")
    t.launch("Launcher", settle=1.0)
    r = t.R(f"delete MemoDB")  # ok if it doesn't exist
    r = t.R(f"install {MEMODB_PATH}")
    assert r.strip() == "OK", f"MemoDB install failed: {r}"

    t.launch("CryptoNote", settle=2.0)
    state = t.ui()
    if "Memo Pad" in state and "found in Memo Pad" in state:
        t.tap_id(12004, settle=2.0)  # OK -- confirm migration
    state = t.ui()
    c = t.crash_text()
    assert c is None, f"crash during MemoPad import: {c}"

    print("encrypting 1 imported memo")
    t.open_row(0)
    t.encrypt_current_memo("fixture1")
    t.launch("CryptoNote", settle=1.5)  # re-anchor: encryption can reorder the list

    print("marking 1 imported memo Private (Sec35 path)")
    t.open_row(1)
    t.tap_id(t.BTN_EDIT_DETAILS, settle=1.0)
    t.tap_id(t.CHK_DETAILS_PRIVATE, settle=0.3)
    t.tap_id(t.BTN_DETAILS_OK, settle=1.5)
    state = t.ui()
    if "Private Records" in state:
        t.tap_xy(20, 138, settle=1.0)  # dismiss the redirect-to-Security alert
    t.launch("CryptoNote", settle=1.0)  # ensure we're back at the list

    print("exporting CryptoNoteDB ->", OUT_PATH)
    r = t.R(f"export CryptoNoteDB {OUT_PATH}")
    if not r.strip().startswith("OK"):
        print(f"EXPORT FAILED: {r}")
        sys.exit(1)
    size = os.path.getsize(OUT_PATH)
    print(f"wrote {OUT_PATH} ({size} bytes)")
    print("NOTE: the encrypted memo's password is 'fixture1'; the "
          "Private-flagged record needs Sec35 password 'fixturesec35' "
          "(this fixture assumes Sec35's Address35/Datebook35/ToDo35/CryptoNote "
          "shared password was also set to 'fixturesec35' when this ran).")


if __name__ == "__main__":
    main()
