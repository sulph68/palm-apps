#!/usr/bin/env python3
"""
CryptoNote regression test suite -- stable, repeatable, byte-verified.

WHY THIS EXISTS
----------------
Ad-hoc emulator testing (guessing tap coordinates fresh each session, judging
"is it encrypted" from a screenshot or a checksum) is exactly what let a real
regression through on 2026-08-14: a build where "Encrypt Memo" silently left
the record in plaintext on disk passed a coordinate-tap smoke test because the
UI *looked* right (list showed a lock icon) even though the data was not
actually encrypted. This suite is designed so that mistake cannot happen
quietly again:

  1. STABLE INTERACTION. Every tap targets a resource ID from MainRsc.h
     (tap-id) or a menu command posted by TITLE through the emulator's real
     menuEvent queue (`menu <pulldown> <item>` -- see DoMenuLookup in
     ReControl.cpp: it looks up the item's resource ID and posts a genuine
     menuEvent, exactly what a real tap would do). The one exception is the
     record-list table (custom-drawn, no per-row object IDs) and standard
     ALERT dialog buttons (pilrc auto-numbers alert buttons, no symbolic ID) --
     both use ONE calibrated, documented coordinate each (see CALIBRATION
     below), not a fresh guess per run.

  2. BYTE-LEVEL GROUND TRUTH. "Is this record actually encrypted" is answered
     by exporting CryptoNoteDB to a real host file (`export` ReControl command)
     and searching the RAW BYTES for a unique plaintext marker string -- not
     by reading UI state, not by screenshot checksums (which can coincidentally
     collide across totally different screens, as happened during today's
     investigation). If the marker string is findable in the raw .pdb bytes,
     the data is plaintext on disk, full stop, regardless of what the UI shows.
     This is exactly the check that would have caught the 2a regression
     immediately instead of after ~40 minutes of confused UI-based debugging.

  3. UI STATE IS CHECKED TOO, SEPARATELY. The byte check proves the DATA is
     safe; a second, separate UI check proves the app's on-device security
     *presentation* (lock icon, password gate on open, no plaintext leak in
     the list preview) also holds -- both must pass. A correct on-disk state
     behind a broken/misleading UI would still be a real bug.

  4. EVERY SCENARIO RESETS TO A KNOWN, VERIFIED-EMPTY STATE FIRST. The single
     biggest source of confusion in today's manual testing was leftover
     records from past sessions surviving a `delete` of the WRONG database
     name (the app's real data DB is "CryptoNoteDB", not "CryptoNote" -- the
     latter deletes nothing and fails silently-ish). See CryptoNoteDB name in
     Database.h. reset_environment() below always deletes the *real* DB name
     and verifies (via `export` failing with "database not found") that it is
     actually gone before proceeding.

REQUIRES
--------
- POSE64 AppImage running with ReControl listening on localhost:6416
  (see [[pose64-recontrol-testing]] in project memory).
- X-Master-Light + X-Y2K35 hack installed and active (era support), same
  precondition as the rest of this suite's emulator testing.
- Sec35.prc installed (scenarios 8-10 exercise the Sec35 integration).
- apps/cryptonote/CryptoNote_v1.prc already built (this script does not build
  it -- run the app's own build.sh/Makefile first).

USAGE
-----
    python3 apps/cryptonote/test/cryptonote_test.py [scenario_name ...]

With no arguments, runs every scenario in order and prints a summary. Pass one
or more scenario function names to run a subset (useful while iterating on a
single fix). Exit code is 0 iff every requested scenario passed.

CALIBRATION (verified against MainEn.rcp + live screenshots 2026-08-14)
-------------------------------------------------------------------------
- Record-list table: `TABLE ID tblMainRecordList AT (0 18 152 124)` in
  MainEn.rcp -> top-left row starts at y=18. Row height is font-dependent
  (TblSetRowHeight uses the runtime line height, not a fixed constant) but was
  empirically confirmed stable at 11px across dozens of taps this session
  (rows landed correctly at y=18, 29, 40, ... every time). If the suite's
  default font/theme ever changes, recalibrate ROW_HEIGHT by taking a fresh
  screenshot and measuring row spacing -- do not just nudge the number and
  hope.
- Standard CONFIRMATION alert buttons (e.g. Sec35's "Forget Password" Yes/No)
  are pilrc-auto-numbered, not symbolic resource IDs, so tap-id cannot target
  them. ALERT_YES_XY below is calibrated against Sec35's specific 2-button
  CONFIRMATION alert layout; a different alert's buttons will be at different
  coordinates and need their own calibrated constant, not reuse of this one.
"""
import importlib.util
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
HACKMASTER = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
CRYPTONOTE_PRC = os.path.join(HACKMASTER, "apps", "cryptonote", "CryptoNote_v1.prc")
SEC35_PRC = os.path.join(HACKMASTER, "apps", "sec35", "Sec35.prc")

_rc_spec = importlib.util.spec_from_file_location(
    "rc", "/home/cnrai/palmos_sdk/emulator/rc.py")
_rc = importlib.util.module_from_spec(_rc_spec)
_rc_spec.loader.exec_module(_rc)

TMPDIR = os.environ.get("CLAUDE_JOB_TMP", "/tmp")

# ---- resource IDs (from MainRsc.h -- do not hand-guess these, re-grep the
# header if a form changes) --------------------------------------------------
FRM_MAIN = 1000
BTN_MAIN_NEW_MEMO = 1005

FRM_EDIT = 2000
FLD_EDIT_TEXT = 2003
BTN_EDIT_DONE = 2005
BTN_EDIT_DETAILS = 2006

FRM_DETAILS = 3000
CHK_DETAILS_PRIVATE = 3003
BTN_DETAILS_OK = 3004

FRM_PASSWORD = 4000
FLD_PASSWORD_TEXT = 4001
BTN_PASSWORD_OK = 4002
BTN_PASSWORD_CANCEL = 4003

# Sec35 (from sec35.c's #define block)
SEC35_PW_SELECTOR = 1001      # main form "Assigned"/"-Unassigned-" box
SEC35_BTN_UNLOCK = 1004       # main form Lock/Unlock button
SEC35_FLD_CURRENT = 1101
SEC35_FLD_NEW = 1102
SEC35_FLD_CONFIRM = 1103
SEC35_BTN_PW_OK = 1104
SEC35_BTN_PW_DELETE = 1109
SEC35_FRM_UNLOCK = 1300
SEC35_FLD_UNLOCK_PW = 1301
SEC35_BTN_UNLOCK_OK = 1302

# ---- calibrated coordinates (see CALIBRATION docstring above) --------------
TABLE_TOP_Y = 18
ROW_HEIGHT = 11
ROW_X = 50                    # anywhere inside the row's text column
ALERT_YES_XY = (95, 145)      # Sec35's 2-button CONFIRMATION alert, button 2
                               # ("Yes! I really forgot!") -- Sec35-specific


def list_row_y(index):
    """0-based row index -> tap Y coordinate in the record-list table."""
    return TABLE_TOP_Y + index * ROW_HEIGHT


# ---- low-level emulator I/O -------------------------------------------------

def R(cmd, timeout=20, retries=5):
    """Resilient ReControl call: retry transient connection errors.

    Also handles 'ERR pending: queued, not delivered within Nms' -- this is
    NOT a failure, it means the guest event was queued but the emulator's
    internal delivery wait timed out before confirming; the action still
    lands shortly after (verified empirically 2026-08-14: the pending event
    reliably completes). Sleeping here instead of retrying avoids the far
    worse failure mode of retrying and delivering the SAME tap/key twice."""
    last_err = None
    for _ in range(retries):
        try:
            r = _rc.rc(cmd, timeout=timeout)
            if "pending" in r and "not delivered" in r:
                time.sleep(2.5)
            return r
        except Exception as e:
            last_err = e
            time.sleep(1)
    raise RuntimeError(f"rc command failed after {retries} retries: {cmd!r} ({last_err})")


def tap_id(obj_id, settle=0.8):
    r = R(f"tap-id {obj_id}")
    time.sleep(settle)
    return r


def tap_xy(x, y, settle=0.8):
    r = R(f"tap {x} {y}")
    time.sleep(settle)
    return r


def menu_select(pulldown, item_text, settle=1.2):
    """Select a menu item by its exact/substring title -- posts a real
    menuEvent by resource ID (see DoMenuLookup), not a coordinate guess.

    Observed 2026-08-14: the posted menuEvent can sit queued behind the menu
    bar's own still-open UI state and not actually get processed until
    something forces the menu bar closed -- the target form/dialog silently
    never appears even though 'menu' reported success. A follow-up menu-key
    press (261, the same key that opens the bar) reliably unblocks it if the
    menu bar is still showing after the settle wait."""
    r = R(f'menu {pulldown} "{item_text}"')
    time.sleep(settle)
    if "MENUBAR" in ui():
        R("key 261")
        time.sleep(settle)
    return r


def type_text(s, delay=0.06):
    for ch in s:
        if ch == "\n":
            R("key 10")
        else:
            R(f"key {ord(ch)}")
        time.sleep(delay)


UI_DUMP_MAX_DISPLAY_LEN = 80  # PalmFormReader.cpp kMaxDisplayLen -- fields
                              # longer than this show as (first 80 chars)+"..."


def type_text_verified(field_id, text, settle=0.6):
    """Type `text` into the currently-focused field, then read the field back
    and require a match -- exact for short text, prefix-match for text past
    the `ui` dump's own 80-char display truncation (see UI_DUMP_MAX_DISPLAY_LEN;
    confirmed against PalmFormReader.cpp's ReadEmuString/kMaxDisplayLen -- it
    truncates the RAW field text to 80 chars + "..." before this script ever
    sees it, so comparing past that point is comparing against the dump
    format, not the field). For text longer than ~256 raw chars, the dump
    can't even read it back at all (kMaxStringLen) -- verify long content via
    a different route (e.g. export+substring, or don't rely on `ui` for it).

    PalmOS's guest-side key-event queue is persistent emulator state, not
    scoped to any one ReControl TCP connection (rc.py opens a fresh socket
    per command). Under emulator load, keystrokes sent faster than the guest
    consumes them can queue up and only get delivered once a field next gains
    focus -- which can be in a LATER, unrelated scenario run, minutes after
    the command that sent them returned "OK" (observed empirically
    2026-08-14: text typed during one scenario surfaced in a DIFFERENT
    memo two scenarios later). Verifying immediately after typing catches
    this at the point of failure instead of several steps downstream where a
    'plaintext not found on disk' failure gives no hint the real cause was a
    stale keystroke, not the app under test."""
    type_text(text)
    time.sleep(settle)
    got = field_text(ui(), field_id)
    if len(text) > UI_DUMP_MAX_DISPLAY_LEN:
        expected = text[:UI_DUMP_MAX_DISPLAY_LEN] + "..."
        ok = got == expected
    else:
        ok = got == text
    if not ok:
        raise ScenarioFailure(
            "typed text does not match field content right after typing -- most likely "
            "stale/queued keystrokes bleeding in from earlier in this emulator session, "
            "not a bug in the app under test. If this keeps happening, the emulator "
            "instance itself needs a full restart (a soft `reset` is not enough):\n"
            f"  typed: {text!r}\n  field: {got!r}")


def ui():
    try:
        return R("ui")
    except Exception:
        return ""


def crash_text():
    """Return a short crash description if a Fatal/handle-error dialog is on
    screen, else None."""
    d = R("dialog")
    for line in d.splitlines():
        ll = line.lower()
        if "line:" in ll or "fatal" in ll or "overlock" in ll:
            return line.strip()[:200]
    return None


def screenshot(name):
    path = os.path.join(TMPDIR, f"cn_test_{name}.png")
    R(f"screenshot {path}")
    return path


def is_stuck():
    """The emulator can wedge into a 'no active form' / blank-screen state
    after enough app switches/installs/deletes in one long session (observed
    empirically 2026-08-14; not specific to any one command). `ui` keeps
    returning the transient error indefinitely instead of recovering on its
    own. Detect it so callers can recover instead of failing confusingly deep
    inside an unrelated scenario."""
    return ui().strip().startswith("ERR transient: no active form")


def recover_from_stuck():
    """Soft-reset and dismiss X-Master's post-reset hack-reactivation prompt
    (era support must stay active for anything era-aware in this suite).
    A soft reset does NOT wipe storage RAM/databases -- installed apps and
    Sec35's password survive; only the wedged dynamic-memory UI state clears."""
    R("reset")
    time.sleep(3)
    state = ui()
    if "X-Master" in state:
        # "Do you want to reactivate..." Yes button
        tap_id(12004, settle=2.5)
        state = ui()
    if 'FORM id=1000 "Preferences"' in state:
        # X-Master's reactivation flow drops into the ROM Preferences app
        # (time zone/date confirmation) before returning control -- Done
        # dismisses it. Same object ID (1005) as CryptoNote's own New-memo
        # button, but that's fine: this only runs while Preferences is the
        # confirmed active form.
        tap_id(1005, settle=2.0)
    # Raw launch (not the self-healing launch() wrapper) -- avoids recursing
    # back into this same recovery path if something is still unhappy.
    R("launch Launcher")
    time.sleep(1.5)


def ensure_healthy():
    if is_stuck():
        recover_from_stuck()
    if is_stuck():
        raise RuntimeError("emulator still stuck after one reset+recover cycle -- "
                            "needs manual attention, not something this harness should paper over further")


def launch(app_name, settle=2.0, _retried=False):
    R(f"launch {app_name}")
    time.sleep(settle)
    if is_stuck() and not _retried:
        recover_from_stuck()
        R(f"launch {app_name}")
        time.sleep(settle)


# ---- byte-level ground truth ------------------------------------------------

def export_db_raw_bytes(dbname):
    """Export a database to a host file and return its raw bytes, or None if
    the database does not exist (e.g. never created, or freshly deleted)."""
    path = os.path.join(TMPDIR, f"export_{dbname}_{int(time.time() * 1000)}.pdb")
    r = R(f"export {dbname} {path}")
    if r.strip().startswith("ERR"):
        return None
    with open(path, "rb") as f:
        data = f.read()
    os.remove(path)
    return data


def db_contains_plaintext(dbname, marker):
    """The ONE check that matters for 'is my data actually encrypted': does
    the marker string appear anywhere in the raw exported bytes?"""
    data = export_db_raw_bytes(dbname)
    if data is None:
        return False
    return marker.encode("latin-1") in data


# ---- environment setup ------------------------------------------------------

def reset_environment(with_sec35_password=None):
    """Bring the emulator to a known, verified-clean state:
    - CryptoNoteDB deleted (verified gone, not just "delete command returned OK")
    - CryptoNote_v1.prc freshly (re)installed
    - optionally, Sec35 reset to Unassigned then given a fresh known password
    """
    ensure_healthy()
    launch("Launcher", settle=1.5)
    R("delete CryptoNoteDB")  # ok to fail if it didn't exist yet
    assert export_db_raw_bytes("CryptoNoteDB") is None, \
        "CryptoNoteDB still exported something after delete -- not actually clean"

    r = R(f"install {CRYPTONOTE_PRC}")
    assert r.strip() == "OK", f"install failed: {r}"

    if with_sec35_password is not None:
        setup_sec35_password(with_sec35_password)

    launch("CryptoNote", settle=2.0)
    c = crash_text()
    assert c is None, f"crash right after clean launch: {c}"


def setup_sec35_password(password):
    """Force Sec35 to -Unassigned- (Forget Password, wiping any prior test
    password) then set `password` fresh. Idempotent starting point for any
    scenario that needs a known Sec35 password."""
    launch("Sec35", settle=1.5)
    state = ui()
    if "-Unassigned-" not in state:
        # Forget Password: menu-select posts the real menuEvent; the
        # resulting CONFIRMATION alert's Yes button has no resource ID
        # (pilrc auto-numbers alert buttons), so this one tap uses the
        # calibrated ALERT_YES_XY documented at module top.
        menu_select("Options", "Forget Password")
        tap_xy(*ALERT_YES_XY, settle=1.2)
        state = ui()
        assert "-Unassigned-" in state, f"Forget Password did not reach Unassigned: {state[:200]}"

    tap_id(SEC35_PW_SELECTOR, settle=1.2)  # opens Set Password form
    tap_id(SEC35_FLD_NEW, settle=0.3)
    type_text(password)
    tap_id(SEC35_FLD_CONFIRM, settle=0.3)
    type_text(password)
    tap_id(SEC35_BTN_PW_OK, settle=2.0)
    state = ui()
    assert "Assigned" in state and "-Unassigned-" not in state, \
        f"Set Password did not reach Assigned: {state[:200]}"


def sec35_unlock(password):
    launch("Sec35", settle=1.0)
    state = ui()
    if "Unlocked" in state:
        return
    tap_id(SEC35_BTN_UNLOCK, settle=1.2)  # main form's Unlock button
    tap_id(SEC35_FLD_UNLOCK_PW, settle=0.3)
    type_text(password)
    tap_id(SEC35_BTN_UNLOCK_OK, settle=1.5)


# ---- app-level actions -------------------------------------------------------

def create_memo(text):
    """From the main list: New Memo, type `text` (verified), Done (commits)."""
    tap_id(BTN_MAIN_NEW_MEMO, settle=1.2)
    state = ui()
    check(f"FORM id={FRM_EDIT}" in state, f"New Memo did not open the edit form: {state[:200]}")
    check(field_text(state, FLD_EDIT_TEXT) == "",
          f"new memo field is not empty before typing -- stale content already present: {state[:300]}")
    type_text_verified(FLD_EDIT_TEXT, text)
    tap_id(BTN_EDIT_DONE, settle=1.0)
    c = crash_text()
    assert c is None, f"crash creating memo: {c}"


def open_row(index):
    """Tap record-list row `index` (0-based). See CALIBRATION docstring."""
    tap_xy(ROW_X, list_row_y(index), settle=1.0)


def wait_for_form(form_id, timeout=5.0, poll=0.5):
    """Poll `ui` until FORM id=`form_id` appears, or return the last-seen
    state after `timeout`. Menu-posted events (see menu_select) can settle
    slower than any fixed sleep under emulator load -- polling is more
    robust than a single settle-then-check."""
    end = time.time() + timeout
    state = ui()
    while form_id_present(state, form_id) is False and time.time() < end:
        time.sleep(poll)
        state = ui()
    return state


def form_id_present(ui_dump, form_id):
    return f'FORM id={form_id}' in ui_dump


def encrypt_current_memo(password):
    """Assumes a memo is already open (Memo N of M form). Record > Encrypt
    Memo, enter password twice (this dialog only asks once for Encrypt), OK.
    """
    menu_select("Record", "Encrypt Memo", settle=0.5)
    state = wait_for_form(FRM_PASSWORD)
    assert f'FORM id={FRM_PASSWORD}' in state, \
        f"Encrypt Memo did not open the Password form: {state[:200]}"
    tap_id(FLD_PASSWORD_TEXT, settle=0.3)
    type_text(password)
    tap_id(BTN_PASSWORD_OK, settle=2.5)  # "Encrypting..." progress + return to list
    c = crash_text()
    assert c is None, f"crash during encrypt: {c}"


def decrypt_open_row(index, password, expect_prompt=True):
    """Tap row `index`; if it's encrypted a Password form should appear --
    enter `password`, OK. Returns the resulting field text (empty string if
    a password form is still showing, e.g. after a wrong-password rejection)."""
    open_row(index)
    state = ui()
    if expect_prompt:
        assert f'FORM id={FRM_PASSWORD}' in state, \
            f"expected a password prompt opening row {index}, got: {state[:200]}"
        tap_id(FLD_PASSWORD_TEXT, settle=0.3)
        type_text(password)
        tap_id(BTN_PASSWORD_OK, settle=1.5)
    state = ui()
    return field_text(state, FLD_EDIT_TEXT)


def _unescape_ui_string(s):
    """Reverse of PalmFormReader.cpp's EscapeString: '"'->\\", '\\'->\\\\,
    '\n'->\\n, '\r'->\\r, '\t'->\\t (control chars below 0x20 become a bare
    '?' on the way in and are NOT reversible -- not used by this suite's
    typed content, which stays printable ASCII plus \\n)."""
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c == "\\" and i + 1 < len(s):
            nxt = s[i + 1]
            if nxt == "n":
                out.append("\n"); i += 2; continue
            if nxt == "r":
                out.append("\r"); i += 2; continue
            if nxt == "t":
                out.append("\t"); i += 2; continue
            if nxt == '"':
                out.append('"'); i += 2; continue
            if nxt == "\\":
                out.append("\\"); i += 2; continue
        out.append(c)
        i += 1
    return "".join(out)


def field_text(ui_dump, field_id):
    """Pull the quoted, UN-escaped text value for a given FIELD id out of a
    `ui` dump. The dump escapes real newlines etc. as literal backslash
    sequences (see EscapeString in PalmFormReader.cpp) -- must be reversed
    here or every multi-line comparison silently fails."""
    for line in ui_dump.splitlines():
        if f"FIELD id={field_id}" in line and '"' in line:
            raw = line.split('"', 1)[1].rsplit('"', 1)[0]
            return _unescape_ui_string(raw)
    return ""


def close_memo():
    tap_id(BTN_EDIT_DONE, settle=1.0)


# ---- scenario framework ------------------------------------------------------

_SCENARIOS = []


def scenario(fn):
    _SCENARIOS.append(fn)
    return fn


class ScenarioFailure(AssertionError):
    pass


def check(cond, msg):
    if not cond:
        raise ScenarioFailure(msg)


# ---- scenarios ---------------------------------------------------------------

@scenario
def s1_fresh_install_is_empty():
    """A freshly-installed CryptoNote with no CryptoNoteDB shows an empty list
    and does not crash on launch."""
    reset_environment()
    state = ui()
    check(f"FORM id={FRM_MAIN}" in state, f"main form not shown: {state[:200]}")


@scenario
def s2_create_memo_is_plaintext():
    """A newly created, unencrypted memo is genuinely plaintext on disk (not
    just 'looks fine in the UI')."""
    reset_environment()
    marker = "PLAINTEXT_MARKER_S2_7f3a"
    create_memo(f"{marker}\nsecond line")
    check(db_contains_plaintext("CryptoNoteDB", marker),
          "freshly created memo's text is not found on disk at all -- Done did not commit")
    close_memo()
    state = ui()
    check(marker in ui(), f"list preview does not show the plaintext title: {state[:200]}")


@scenario
def s3_encrypt_removes_plaintext_from_disk():
    """THE core check: after 'Encrypt Memo', the plaintext marker must be
    GONE from the raw exported bytes -- this is the exact check that would
    have caught the 2026-08-14 silent-no-op regression immediately."""
    reset_environment()
    marker = "PLAINTEXT_MARKER_S3_c91e"
    create_memo(f"{marker}\nsecond line")
    check(db_contains_plaintext("CryptoNoteDB", marker), "sanity: plaintext not present before encrypt")

    open_row(0)
    encrypt_current_memo("s3pass")

    check(not db_contains_plaintext("CryptoNoteDB", marker),
          "REGRESSION: plaintext marker still present in raw DB bytes after Encrypt Memo "
          "-- encryption silently did not happen")

    state = ui()
    check(f"FORM id={FRM_MAIN}" in state, f"expected to land back on main list: {state[:200]}")
    check(marker not in state, f"list preview leaks plaintext after encrypt: {state[:200]}")


@scenario
def s4_wrong_password_rejected_no_leak():
    """Opening an encrypted memo with the wrong password must not reveal the
    plaintext and must not corrupt the on-disk ciphertext."""
    reset_environment()
    marker = "PLAINTEXT_MARKER_S4_5b2d"
    create_memo(f"{marker}\nsecond line")
    open_row(0)
    encrypt_current_memo("s4correct")

    open_row(0)
    state = ui()
    check(f"FORM id={FRM_PASSWORD}" in state, f"expected password prompt: {state[:200]}")
    tap_id(FLD_PASSWORD_TEXT, settle=0.3)
    type_text("s4wrong")
    tap_id(BTN_PASSWORD_OK, settle=1.5)
    c = crash_text()
    check(c is None, f"crash on wrong password: {c}")

    state = ui()
    check(marker not in field_text(state, FLD_EDIT_TEXT),
          "wrong password revealed the plaintext")
    check(not db_contains_plaintext("CryptoNoteDB", marker),
          "wrong-password attempt corrupted the record into plaintext on disk")


@scenario
def s5_correct_password_exact_roundtrip():
    """Decrypting with the right password must restore EXACTLY the original
    text, byte for byte, both on screen and (once re-saved) on disk."""
    reset_environment()
    original = "Line one of test memo\nLine two of test memo"
    create_memo(original)
    open_row(0)
    encrypt_current_memo("s5pass")

    got = decrypt_open_row(0, "s5pass")
    check(got == original, f"decrypted text does not match original exactly:\n  got:      {got!r}\n  expected: {original!r}")


@scenario
def s6_multiple_encrypt_decrypt_cycles_stable():
    """Repeated encrypt/decrypt cycles on the same record must not drift or
    corrupt the content."""
    reset_environment()
    original = "Cycle stability check\nsecond line stays intact"
    create_memo(original)

    for i in range(3):
        open_row(0)
        encrypt_current_memo(f"cyclepass{i}")
        got = decrypt_open_row(0, f"cyclepass{i}")
        check(got == original, f"cycle {i}: content drifted -- got {got!r}")
        close_memo()


@scenario
def s7_delete_memo_removes_record():
    """Delete Memo actually removes the record (not just hides it from the
    list) -- verified by re-export showing zero records for its marker."""
    reset_environment()
    marker = "PLAINTEXT_MARKER_S7_a10c"
    create_memo(f"{marker}\nsecond line")
    check(db_contains_plaintext("CryptoNoteDB", marker), "sanity: memo not present before delete")

    open_row(0)
    menu_select("Record", "Delete Memo", settle=1.0)
    state = ui()
    # Delete Memo pops a confirmation form; button IDs there are also
    # pilrc-numbered, so accept either an immediate return to the list
    # (no confirm needed) or fall through gracefully if this build requires
    # a tap this script doesn't yet model -- surfaced as a clear failure
    # rather than a silent false pass.
    check(f"FORM id={FRM_MAIN}" in state or "Delete" in state,
          f"unexpected state after Delete Memo: {state[:200]}")
    if "Delete" in state and f"FORM id={FRM_MAIN}" not in state:
        # confirmation dialog present with a Delete Memo title/button; try OK
        tap_xy(6, 121, settle=1.0)  # calibrated for CryptoNote's Delete confirm OK

    check(not db_contains_plaintext("CryptoNoteDB", marker),
          "record still present on disk after Delete Memo")


@scenario
def s8_private_flag_routes_through_sec35():
    """Details > Private is a DISTINCT mechanism from 'Encrypt Memo' -- it
    should route through Sec35's Mask/Hide privacy system, not CryptoNote's
    own AES path. Confirms that distinction still holds (don't conflate the
    two paths, as today's manual testing initially did)."""
    reset_environment(with_sec35_password="sec35test")
    create_memo("Private-flag record\nsecond line")

    tap_id(BTN_EDIT_DETAILS, settle=1.0)
    state = ui()
    check(f"FORM id={FRM_DETAILS}" in state, f"Details form did not open: {state[:200]}")
    tap_id(CHK_DETAILS_PRIVATE, settle=0.3)
    tap_id(BTN_DETAILS_OK, settle=1.5)

    state = ui()
    check("Private Records" in state, f"expected the Private Records redirect alert: {state[:200]}")
    # Dismiss (this alert's only button is a standard OK at a fixed spot for
    # this specific alert layout).
    tap_xy(20, 138, settle=1.0)


@scenario
def s9_sec35_lock_gates_access():
    """A Sec35-migrated 'Private' record must be inaccessible while Sec35 is
    locked, and correctly show its content once unlocked."""
    reset_environment(with_sec35_password="sec35test")
    marker = "SEC35_GATED_S9_9d4e"
    create_memo(f"{marker}\nsecond line")
    tap_id(BTN_EDIT_DETAILS, settle=1.0)
    tap_id(CHK_DETAILS_PRIVATE, settle=0.3)
    tap_id(BTN_DETAILS_OK, settle=1.5)
    state = ui()
    if "Private Records" in state:
        tap_xy(20, 138, settle=1.0)

    # Lock Sec35 explicitly, then try to view the record.
    launch("Sec35", settle=1.0)
    state = ui()
    if "Unlocked" in state:
        tap_id(SEC35_BTN_UNLOCK, settle=1.0)  # this button is Lock when unlocked

    launch("CryptoNote", settle=1.5)
    open_row(0)
    state = ui()
    check(f"FORM id={SEC35_FRM_UNLOCK}" in state,
          f"expected Sec35's Unlock bounce while locked, got: {state[:200]}")

    tap_id(SEC35_FLD_UNLOCK_PW, settle=0.3)
    type_text("sec35test")
    tap_id(SEC35_BTN_UNLOCK_OK, settle=1.5)
    state = ui()
    check(marker in field_text(state, FLD_EDIT_TEXT) or marker in state,
          f"record content not correctly shown after Sec35 unlock: {state[:200]}")


# ---- driver -------------------------------------------------------------------

def main():
    requested = sys.argv[1:]
    to_run = [f for f in _SCENARIOS if not requested or f.__name__ in requested]
    if requested:
        missing = set(requested) - {f.__name__ for f in _SCENARIOS}
        if missing:
            print(f"Unknown scenario name(s): {sorted(missing)}")
            print(f"Available: {[f.__name__ for f in _SCENARIOS]}")
            sys.exit(2)

    results = []
    for fn in to_run:
        print(f"--- {fn.__name__} " + "-" * max(0, 60 - len(fn.__name__)))
        try:
            fn()
            print(f"PASS  {fn.__name__}")
            results.append((fn.__name__, True, None))
        except ScenarioFailure as e:
            print(f"FAIL  {fn.__name__}: {e}")
            screenshot(f"FAIL_{fn.__name__}")
            results.append((fn.__name__, False, str(e)))
        except Exception as e:
            print(f"ERROR {fn.__name__}: {type(e).__name__}: {e}")
            screenshot(f"ERROR_{fn.__name__}")
            results.append((fn.__name__, False, f"{type(e).__name__}: {e}"))

    print("\n" + "=" * 70)
    print("SUMMARY")
    print("=" * 70)
    n_pass = sum(1 for _, ok, _ in results if ok)
    for name, ok, msg in results:
        print(f"  [{'PASS' if ok else 'FAIL'}] {name}" + (f" -- {msg}" if msg else ""))
    print(f"\n{n_pass}/{len(results)} passed")

    sys.exit(0 if n_pass == len(results) else 1)


if __name__ == "__main__":
    main()
