/*
 * test_datelib.c - host-side unit tests for datelib (TEST_PLAN suites A, L
 * plus invariants INV-1,2,3,6,7). Weekday is cross-checked against an
 * INDEPENDENT Zeller's-congruence implementation so we are not merely
 * re-running the code under test.
 *
 * Build/run:
 *   make test      (from hackmaster/; datelib itself lives in ../apps/lib)
 */
#include <stdio.h>
#include "datelib.h"

static int g_pass = 0, g_fail = 0;

static void ok(int cond, const char *what)
{
    if (cond) { g_pass++; }
    else { g_fail++; printf("  FAIL: %s\n", what); }
}

/* Independent oracle: Zeller's congruence, returned as 0=Sunday..6=Saturday. */
static int zeller_dow(dl_i64 y, dl_i64 m, dl_i64 d)
{
    if (m < 3) { m += 12; y -= 1; }
    dl_i64 K = ((y % 100) + 100) % 100;
    dl_i64 J = y / 100;              /* y >= 1904 here, so plain floor is fine */
    dl_i64 h = (d + (13 * (m + 1)) / 5 + K + K / 4 + J / 4 + 5 * J) % 7;
    /* Zeller: 0=Saturday..6=Friday -> convert to 0=Sunday */
    return (int)((h + 6) % 7);
}

/* ---- weekday validation against the oracle over a wide range ---- */
static void test_weekday_oracle(void)
{
    printf("[weekday vs Zeller oracle]\n");
    int bad = 0;
    dl_i32 y;
    for (y = 1904; y <= 3200; y++) {
        dl_i16 mo;
        for (mo = 1; mo <= 12; mo++) {
            dl_i16 dmax = dl_daysInMonth(y, mo);
            dl_i16 d;
            for (d = 1; d <= dmax; d++)
                if (dl_dayOfWeek(y, mo, d) != zeller_dow(y, mo, d)) bad++;
        }
    }
    /* spot-check a few huge years too */
    dl_i32 big[] = { 5000, 30827, 999999, 2000000000 };
    int i;
    for (i = 0; i < 4; i++) {
        dl_i32 by = big[i];
        if (dl_dayOfWeek(by, 6, 15) != zeller_dow(by, 6, 15)) bad++;
    }
    ok(bad == 0, "dl_dayOfWeek matches Zeller across 1904..3200 + huge years");
    printf("  (checked ~1.3M dates, mismatches=%d)\n", bad);
}

/* ---- round-trip seconds<->date (INV-7 fidelity) ---- */
static void test_roundtrip(void)
{
    printf("[seconds<->date round-trip]\n");
    struct { dl_i32 y; dl_i16 mo,d,h,mi,s; } cases[] = {
        {1904,1,1,0,0,0}, {1998,3,15,12,0,0}, {2031,12,31,23,59,59},
        {2050,7,1,9,30,0}, {2100,6,15,0,0,1}, {2400,2,29,14,37,12},
        {30827,11,9,6,28,15}, {2000000000,1,1,0,0,0},
    };
    int i, n = sizeof(cases)/sizeof(cases[0]);
    for (i = 0; i < n; i++) {
        dl_i64 secs = dl_dateToSeconds(cases[i].y, cases[i].mo, cases[i].d,
                                       cases[i].h, cases[i].mi, cases[i].s);
        DlDateTime dt;
        dl_secondsToDate(secs, &dt);
        char msg[96];
        snprintf(msg, sizeof msg, "round-trip %d-%02d-%02d %02d:%02d:%02d",
                 cases[i].y, cases[i].mo, cases[i].d, cases[i].h, cases[i].mi, cases[i].s);
        ok(dt.year==cases[i].y && dt.month==cases[i].mo && dt.day==cases[i].d &&
           dt.hour==cases[i].h && dt.minute==cases[i].mi && dt.second==cases[i].s, msg);
    }
}

/* ---- shared re-base invariant check (INV-1,3,6 + Feb29 safety) ---- */
static void check_rebase(dl_i32 realYear, dl_i16 mo, dl_i16 d, const char *tag)
{
    dl_i32 off  = dl_reBaseOffset(realYear);
    dl_i32 based = realYear - off;
    char msg[128];

    snprintf(msg, sizeof msg, "%s: based %d in window", tag, based);
    ok(based >= DL_WINDOW_LOW && based <= DL_WINDOW_HIGH, msg);

    snprintf(msg, sizeof msg, "%s: offset>0 (real %d>2031)", tag, realYear);
    ok(off > 0, msg);

    snprintf(msg, sizeof msg, "%s: same year-type", tag);
    ok(dl_yearType(based) == dl_yearType(realYear), msg);

    snprintf(msg, sizeof msg, "%s: weekday preserved (%d/%d)", tag, mo, d);
    ok(dl_dayOfWeek(based, mo, d) == dl_dayOfWeek(realYear, mo, d), msg);

    if (mo == 2 && d == 29) {
        snprintf(msg, sizeof msg, "%s: Feb29 -> leap based year", tag);
        ok(dl_isLeap(based), msg);
    }
    printf("  %s: real %d -> based %d (offset %d), dow %d==%d\n",
           tag, realYear, based, off,
           dl_dayOfWeek(realYear, mo, d), dl_dayOfWeek(based, mo, d));
}

/* ---- Suite A: offset / calendar math ---- */
static void suite_A(void)
{
    printf("[Suite A: offset/calendar math]\n");
    ok(dl_reBaseOffset(2031) == 0, "A1 2031 offset 0");
    ok(dl_reBaseOffset(1998) == 0, "A7 1998 passthrough");
    ok(dl_isLeap(2000) && dl_reBaseOffset(2000) == 0, "A8 2000 leap, passthrough");
    check_rebase(2032, 6, 15, "A2 2032");
    check_rebase(2050, 6, 15, "A3 2050");
    check_rebase(2048, 2, 29, "A4 2048 leap");
    check_rebase(2059, 6, 15, "A5 2059");
    check_rebase(2099, 6, 15, "A6 2099");
}

/* ---- Suite L: multi-era & >2099 (2b) ---- */
static void suite_L(void)
{
    printf("[Suite L: multi-era & >2099]\n");
    check_rebase(2150, 6, 15, "L1 2150");
    ok(!dl_isLeap(2100), "L2 2100 is non-leap (anomaly)");
    check_rebase(2100, 6, 15, "L2 2100");
    ok(dl_isLeap(2400), "L3 2400 is leap (/400)");
    check_rebase(2400, 2, 29, "L3 2400 Feb29");
    ok(dl_isLeap(2104), "L4 2104 is leap");
    check_rebase(2104, 2, 29, "L4 2104 Feb29");
    check_rebase(2250, 6, 15, "L5 2250");
    check_rebase(2450, 6, 15, "L5 2450");
    check_rebase(2650, 6, 15, "L5 2650");
    check_rebase(30827, 6, 15, "L6 30827");
}

/* ---- Suite DIM: daysInMonth (the source the control panel clamps Day to) ---- */
static void suite_DIM(void)
{
    printf("[Suite DIM: daysInMonth]\n");
    ok(dl_daysInMonth(2100, 2) == 28, "Feb 2100 non-leap century = 28");
    ok(dl_daysInMonth(2104, 2) == 29, "Feb 2104 leap = 29");
    ok(dl_daysInMonth(2020, 2) == 29, "Feb 2020 leap = 29");
    ok(dl_daysInMonth(2021, 2) == 28, "Feb 2021 non-leap = 28");
    ok(dl_daysInMonth(2000, 2) == 29, "Feb 2000 /400 leap = 29");
    ok(dl_daysInMonth(2400, 2) == 29, "Feb 2400 /400 leap = 29");
    ok(dl_daysInMonth(2025, 4) == 30, "Apr = 30");
    ok(dl_daysInMonth(2025, 6) == 30, "Jun = 30");
    ok(dl_daysInMonth(2025, 1) == 31, "Jan = 31");
    ok(dl_daysInMonth(2025, 12) == 31, "Dec = 31");
}

int main(void)
{
    test_weekday_oracle();
    test_roundtrip();
    suite_A();
    suite_L();
    suite_DIM();
    printf("\n==== datelib: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
