/*
 * hack_sec2dt.c - HackMaster Y2K35 hardening patch on sysTrapTimSecondsToDateTime.
 *
 * Makes the project's ROM-validated calendar engine (datelib) the authoritative
 * seconds->DateTimeType converter for the whole system. datelib is byte-identical
 * to the Palm 4.1 ROM for every value in 0..2^32-1 (verified), so this is a
 * safe drop-in that additionally guarantees correct month/day/weekday right up to
 * the UInt32 ceiling with no signed-overflow or boundary surprises in the ROM
 * path. Regression-covered by test/pose_test.py.
 *
 * Stand-alone code resource: no globals; datelib is pure and data-free.
 */
#include <PalmOS.h>
#include <Standalone.h>
#include "datelib.h"
#include "y2k35.h"

void HackSecToDateTime(UInt32 seconds, DateTimeType *dtP)
{
    DlDateTime d;

    /* Liveness stamp: era-aware apps trigger this trap and check the stamp is
     * current to confirm the hack is enabled (a stale/absent stamp = disabled). */
    FtrSet(Y2K35_CREATOR, Y2K35_FTR_ALIVE, TimGetTicks());

    if (dtP == NULL)                    /* never crash (hackapi error handling) */
        return;
    dl_secondsToDate((dl_i64) seconds, &d);
    dtP->second  = d.second;
    dtP->minute  = d.minute;
    dtP->hour    = d.hour;
    dtP->day     = d.day;
    dtP->month   = d.month;
    dtP->year    = (Int16) d.year;      /* within window here; Int16 is ample */
    dtP->weekDay = d.weekday;
}

HACKMASTER_TRAP(sysTrapTimSecondsToDateTime);
STANDALONE_CODE_RESOURCE_ID(1000);
