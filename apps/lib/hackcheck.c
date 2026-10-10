/*
 * hackcheck.c - see hackcheck.h.
 */
#include <PalmOS.h>
#include "y2k35.h"
#include "hackcheck.h"

/* hackcheck is the boundary that talks to the ACTUAL hack/ROM: it must call the
 * real ROM TimSecondsToDateTime (to trigger the hack's patch for the liveness
 * probe and to read the raw re-based clock), NOT the aware-tier datelib wrapper
 * that edate_override.h redirects it to. */
#undef TimSecondsToDateTime

DATELIB_SECTION HcState HcCheck(void)
{
    DmSearchStateType st;
    UInt16       cardNo;
    LocalID      dbID = 0;
    UInt32       before, alive = 0;
    DateTimeType tmp;

    /* Installed? Look for the plugin database by type/creator. */
    if (DmGetNextDatabaseByTypeCreator(true, &st, Y2K35_HACK_TYPE, Y2K35_CREATOR,
                                       false, &cardNo, &dbID) != errNone || dbID == 0)
        return HC_NOT_INSTALLED;

    /* Enabled? Trigger the patched trap and see whether it stamped the liveness
     * Feature during our call. If the hack is off, the patch never runs, so the
     * stamp is stale (or absent) and stays < 'before'. Self-contained -- no
     * dependence on X-Master's own Feature conventions. */
    before = TimGetTicks();
    TimSecondsToDateTime(TimGetSeconds(), &tmp);      /* our patch, iff enabled */
    if (FtrGet(Y2K35_CREATOR, Y2K35_FTR_ALIVE, &alive) == errNone && alive >= before)
        return HC_OK;

    return HC_NOT_ENABLED;
}

DATELIB_SECTION Boolean HcOkOrAlert(UInt16 notInstalledAlertId, UInt16 notEnabledAlertId)
{
    HcState s = HcCheck();
    if (s == HC_OK) return true;
    FrmAlert(s == HC_NOT_INSTALLED ? notInstalledAlertId : notEnabledAlertId);
    return false;
}

DATELIB_SECTION Int32 Hc_Offset(void)
{
    UInt32    off = 0;
    Y2k35Pref p;
    UInt16    sz = sizeof(p);

    /* Live Feature first; fall back to the saved pref (survives soft reset before
     * the next Set re-publishes the Feature). */
    if (FtrGet(Y2K35_CREATOR, Y2K35_FTR_OFFSET, &off) == errNone)
        return (Int32) off;
    if (PrefGetAppPreferences(Y2K35_CREATOR, Y2K35_PREF_ID, &p, &sz, true)
            != noPreferenceFound && sz >= sizeof(p))
        return p.offset;
    return 0;
}

DATELIB_SECTION void Hc_CurrentRealDate(DlDateTime *out)
{
    /* ROM traps only (no datelib int64/int32 division -> no libgcc helpers,
     * which are unreachable from a non-main code segment; see the picker). */
    DateTimeType dt;
    TimSecondsToDateTime(TimGetSeconds(), &dt);   /* the (re-based) system clock */
    out->year    = dt.year + Hc_Offset();          /* -> true, full-range year   */
    out->month   = dt.month;
    out->day     = dt.day;
    out->hour    = dt.hour;
    out->minute  = dt.minute;
    out->second  = dt.second;
    out->weekday = DayOfWeek(out->month, out->day, (Int16) out->year);
}
