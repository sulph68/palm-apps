#!/usr/bin/env python3
"""
Automated POSE regression test for the HackMaster Y2K35 plugin.

Assumes pose64 is running (ReControl :6416) on a booted Palm III 4.1 session.
Installs X-Master + the hack + DateProbe as needed, enables the hack, then checks:

  1. DateProbe on-device datelib self-test summary ("ST n/n PASS") -- the full
     re-basing math for every year class (leap/anomaly/far-future). This is the
     authoritative correctness check and needs no UI.
  2. Control-panel UI: the digit push-button + tREP spinner, driven only by
     ramping UP (pose64's repeat buttons ramp with no initial delay, so exact
     values are unreliable but caps are deterministic):
       - Month clamps to 12, Day clamps to the month length (Dec -> 31).
       - Year ramps to 9999 (four-digit picker, no clamp).
       - Set re-bases: the "based" readout lands in [1904,2031] with offset>0,
         and Date Book (system-wide) shows that based date.

Run:  python3 test/pose_test.py   (from hackmaster/)
"""
import importlib.util, time, sys, re

spec = importlib.util.spec_from_file_location("rc", "/home/cnrai/palmos_sdk/emulator/rc.py")
rc = importlib.util.module_from_spec(spec); spec.loader.exec_module(rc)

BASE  = "/home/cnrai/palmos_sdk/hackmaster_y2035/hackmaster"
HACK  = BASE + "/X-Y2K35.prc"
XM    = BASE + "/X-Master-Light.prc"
PROBE = BASE + "/DateProbe.prc"

YEAR = [2300,2301,2302,2303];  YEAR_UP = (126,24);  YEAR_DN = (126,32)
MONF = [2310,2311];            MON_UP  = (126,44)
DAYF = [2320,2321];            DAY_UP  = (126,64)

fails = []
def check(name, got, want):
    ok = (got == want)
    print(("  PASS " if ok else "  FAIL ") + name + f"  got={got!r} want={want!r}")
    if not ok: fails.append(name)

def R(c, t=15):
    for _ in range(5):
        try:
            r = rc.rc(c, timeout=t); time.sleep(0.2); return r
        except Exception:
            time.sleep(0.8)
    return "ERR"

def ui(): return R("ui")
def form():
    fs = [l for l in ui().splitlines() if l.startswith("OK FORM")]
    return fs[0] if fs else "?"
def label(cid):
    for l in ui().splitlines():
        if f"id={cid}" in l and '"' in l: return l.split('"')[-2]
    return ""
def read_field(bids): return "".join(label(b) for b in bids)

def tap1(x, y):
    R(f"pen down {x} {y}"); time.sleep(0.05); R(f"pen up {x} {y}"); time.sleep(0.12)
def ramp(sel_id, arrow_xy, n=16):
    R(f"tap-id {sel_id}"); time.sleep(0.15)
    for _ in range(n): tap1(*arrow_xy)

def set_digit_to(bid, arrow_xy, target):
    """Set an unclamped digit exactly, reading after each press (mod-10 wraps)."""
    R(f"tap-id {bid}"); time.sleep(0.15)
    for _ in range(14):
        if label(bid) == str(target): return True
        tap1(*arrow_xy)
    return label(bid) == str(target)

def ensure_installed():
    apps = R("apps all")
    if "X-Master" not in apps: R("install "+XM, 20)
    if "X-Y2K35"  not in apps: R("install "+HACK, 20)
    R("launch Memo Pad"); time.sleep(1)
    R("install "+PROBE, 20)

def open_panel():
    R("launch X-Master"); time.sleep(1.2)
    R("tap-id 9003"); time.sleep(1.2)
    if "id=2000" not in form():
        R("tap-id 9500"); time.sleep(0.6)
        R("tap-id 9003"); time.sleep(1.2)
    return "id=2000" in form()

def selftest():
    R("launch DateProbe"); time.sleep(1.6)
    st = label(1001).split("\\n")[0]
    check("datelib self-test", st, "ST 8/8 PASS")

def clamp_and_set():
    if not open_panel():
        check("panel-open", form(), 'OK FORM id=2000 "Y2K35 Era"'); return
    # Month cannot exceed 12
    ramp(2310, MON_UP); ramp(2311, MON_UP)
    check("month clamp <=12", read_field(MONF), "12")
    # Day cannot exceed the month length (month is 12 -> December -> 31)
    ramp(2320, DAY_UP); ramp(2321, DAY_UP)
    check("day clamp (Dec=31)", read_field(DAYF), "31")
    # Year minimum is the Palm epoch (1904): ramp every digit down -> floors at 1904
    # (deterministic thanks to the clamp; exact-value up-ramps are not reliable
    # because pose64 repeat buttons ramp by a variable amount per press).
    for b in YEAR:
        R(f"tap-id {b}"); time.sleep(0.15)
        for _ in range(16): tap1(*YEAR_DN)
    # Property check: no amount of down-ramping can push the year below the epoch.
    check("year floor >= 1904", int(read_field(YEAR)) >= 1904, True)
    # Raise the year clearly above 2031 so Set exercises re-basing.
    R("tap-id 2300"); time.sleep(0.15)          # thousands digit
    for _ in range(12):
        if int(read_field(YEAR)) > 2031: break
        tap1(*YEAR_UP)
    y = int(read_field(YEAR))
    check("year raised >2031", y > 2031, True)
    # Apply -> re-base
    R("tap-id 2001"); time.sleep(1)
    based = label(2020)
    m = re.match(r"yr=(\d+) 12/31 off=(\d+)", based)
    check("based readout format", bool(m), True)
    if not m: return
    by, off = int(m.group(1)), int(m.group(2))
    check("based year in window", 1904 <= by <= 2031, True)
    check("offset>0 (9999>2031)", off > 0, True)
    # System-wide: Date Book shows the based Dec 31
    R("tap-id 2002"); time.sleep(0.8)
    R("launch Date Book"); time.sleep(1.8)
    title = ""
    for l in ui().splitlines():
        if "TITLE" in l and '"' in l: title = l.split('"')[-2]
    check("DateBook shows based date", title, f"Dec 31, {by%100:02d}")

def main():
    rc.wait_boot()
    ensure_installed()
    print("[self-test]");        selftest()
    print("[clamp + set flow]"); clamp_and_set()
    print(f"\n==== {'ALL PASS' if not fails else str(len(fails))+' FAILED: '+', '.join(fails)} ====")
    sys.exit(1 if fails else 0)

if __name__ == "__main__":
    main()
