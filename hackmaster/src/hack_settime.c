/*
 * hack_settime.c - HackMaster Y2K35 guard patch on sysTrapTimSetSeconds.
 *
 * While an era offset is active (the real date is past 2031, so the clock runs
 * on a stand-in year and the era apps add the offset back), anything that sets
 * the clock directly would write a date the apps then shift by the offset.
 * Who sets it decides what happens:
 *
 *  - The hack's own panel and EraNTP raise Y2K35_FTR_BYPASS around their call:
 *    they already pass the stand-in (re-based) moment -> set as given.
 *  - HotSync (the HotSync app is current) passes the PC's REAL date -> re-base
 *    it exactly as the panel's Apply does (offset + stand-in + master clock), so
 *    the era stays right. Limit: HotSync itself turns the PC date into Palm
 *    seconds first, which end in Feb 2040.
 *  - Anything else (the Preferences Date & Time panel) -> ask first. OK sets
 *    the date literally and clears the era offset (a deliberate choice: every
 *    app then shows exactly that date); Cancel leaves the clock unchanged.
 *    With no form on screen (no UI is possible) it is re-based like HotSync.
 *  - No offset active: nothing to protect -> set as given, no alert.
 *
 * Stand-alone code resource: no globals; datelib is pure and data-free.
 */
#include <PalmOS.h>
#include <Standalone.h>
#include "datelib.h"
#include "y2k35.h"

typedef void (*TimSetSecondsFn)(UInt32);

/* HackMaster jumps to the FIRST function compiled into this resource (no
 * lookup by name -- see apps/McPhling4.md section 3), so the entry point
 * comes first and the helpers after it, declared here. */
static Int32   CurOffset(void);
static void    Publish(Int32 off, dl_i64 master);
static void    ReBase(TimSetSecondsFn orig, UInt32 realSeconds);
static Boolean AskLiteral(void);

void HackSetSeconds(UInt32 newSeconds)
{
    UInt32          v, creator = 0;
    TimSetSecondsFn orig;
    UInt16          card;
    LocalID         id;

    if (FtrGet(Y2K35_CREATOR, Y2K35_SETSEC_RESID, &v) != errNone || v == 0)
        return;                             /* no original: nothing safe to do */
    orig = (TimSetSecondsFn) v;

    /* the panel / EraNTP: already the stand-in moment */
    if (FtrGet(Y2K35_CREATOR, Y2K35_FTR_BYPASS, &v) == errNone && v) {
        orig(newSeconds);
        return;
    }

    if (SysCurAppDatabase(&card, &id) == errNone)
        DmDatabaseInfo(card, id, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                       NULL, NULL, NULL, &creator);

    if (creator == 'sync') {                /* HotSync: the PC's real date */
        ReBase(orig, newSeconds);
        return;
    }

    if (CurOffset() == 0) {                 /* no era offset: nothing to guard */
        orig(newSeconds);
        return;
    }

    if (FrmGetActiveForm() == NULL) {       /* no UI possible: keep the era */
        ReBase(orig, newSeconds);
        return;
    }

    if (AskLiteral()) {                     /* deliberate: exactly this date */
        orig(newSeconds);
        Publish(0, (dl_i64) newSeconds);
    }
    /* Cancel: the clock is left as it was */
}

static Int32 CurOffset(void)
{
    UInt32    off;
    Y2k35Pref p;
    UInt16    sz = sizeof(p);

    if (FtrGet(Y2K35_CREATOR, Y2K35_FTR_OFFSET, &off) == errNone)
        return (Int32) off;
    if (PrefGetAppPreferences(Y2K35_CREATOR, Y2K35_PREF_ID, &p, &sz, true)
            != noPreferenceFound && sz >= sizeof(p))
        return p.offset;
    return 0;
}

/* Publish an era state: offset + 64-bit master clock (Features + saved pref),
 * the same set the panel's Apply writes. */
static void Publish(Int32 off, dl_i64 master)
{
    Y2k35Pref p;
    FtrSet(Y2K35_CREATOR, Y2K35_FTR_OFFSET, (UInt32) off);
    FtrSet(Y2K35_CREATOR, Y2K35_FTR_MHI, (UInt32) (master >> 32));
    FtrSet(Y2K35_CREATOR, Y2K35_FTR_MLO, (UInt32) (master & 0xFFFFFFFFUL));
    p.master = master; p.offset = off;
    PrefSetAppPreferences(Y2K35_CREATOR, Y2K35_PREF_ID, 1, &p, sizeof(p), true);
}

/* realSeconds is a REAL date: re-base it and set the stand-in moment. */
static void ReBase(TimSetSecondsFn orig, UInt32 realSeconds)
{
    DlDateTime d;
    Int32      off;
    dl_i64     based;

    dl_secondsToDate((dl_i64) realSeconds, &d);
    off   = dl_reBaseOffset(d.year);
    based = dl_dateToSeconds(d.year - off, d.month, d.day, d.hour, d.minute, d.second);
    orig((UInt32) based);
    Publish(off, (dl_i64) realSeconds);
}

/* OK = 0 on the alert (OK / Cancel). The alert lives in the hack's own
 * resource database, which is not open in the calling app: open it read-only
 * for the alert. */
static Boolean AskLiteral(void)
{
    DmOpenRef db = DmOpenDatabaseByTypeCreator(Y2K35_HACK_TYPE, Y2K35_CREATOR, dmModeReadOnly);
    UInt16    b;

    b = FrmAlert(Y2K35_ALERT_SETDATE);
    if (db) DmCloseDatabase(db);
    return (Boolean) (b == 0);
}

HACKMASTER_TRAP(sysTrapTimSetSeconds);
STANDALONE_CODE_RESOURCE_ID(1001);
