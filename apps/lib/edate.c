/* edate.c - see edate.h. Strict C89 (gcc 2.95.3).
 *
 * CRITICAL: Datebook35 places this in a non-main code segment (sec002), from
 * which the 64-bit libgcc helpers (__divdi3 etc.) are UNREACHABLE. So all day
 * arithmetic here is 32-bit with CONSTANT divisors only (gcc lowers those to
 * multiply/shift, no libgcc) -- we must NOT call datelib's 64-bit
 * dl_dateToSeconds / dl_secondsToDate. The 32-bit dl_ helpers (dl_dayOfWeek,
 * dl_daysInMonth, dl_isLeap) are fine. */
#include <PalmOS.h>
#include "datelib.h"
#include "hackcheck.h"   /* Hc_Offset for the real "today" */
#include "edate.h"

/* days from 1904-01-01 to 1970-01-01 (66 yrs, 17 leap days) */
#define EDATE_1904_TO_1970  24107L

/* Howard Hinnant's civil<->days algorithms, in 32-bit (era base 0000-03-01).
 * Every divisor is a compile-time constant -> no 64-bit libgcc. Valid for the
 * full 1904..9999 range (intermediates stay < 2^31). */

DATELIB_SECTION Int32 EDays(Int16 year, Int16 month, Int16 day)
{
    Int32 y = year;
    Int32 m = month;
    Int32 era, yoe, doy, doe, days1970;
    if (m <= 2) y -= 1;
    era = y / 400;                         /* y >= 1903 > 0 here */
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    days1970 = era * 146097L + doe - 719468L;
    return days1970 + EDATE_1904_TO_1970;
}

DATELIB_SECTION void EFromDays(Int32 days, Int16 *year, Int16 *month, Int16 *day)
{
    Int32 z = days - EDATE_1904_TO_1970 + 719468L;   /* -> days since 0000-03-01 */
    Int32 era = (z >= 0 ? z : z - 146096L) / 146097L;
    Int32 doe = z - era * 146097L;
    Int32 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    Int32 y   = yoe + era * 400;
    Int32 doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    Int32 mp  = (5 * doy + 2) / 153;
    Int32 d   = doy - (153 * mp + 2) / 5 + 1;
    Int32 m   = mp + (mp < 10 ? 3 : -9);
    if (m <= 2) y += 1;
    if (year)  *year  = (Int16)y;
    if (month) *month = (Int16)m;
    if (day)   *day   = (Int16)d;
}

DATELIB_SECTION Int16 EDaysInMonth(Int16 year, Int16 month)
{
    return dl_daysInMonth(year, month);   /* 32-bit, safe */
}

DATELIB_SECTION Int16 EDayOfWeek(Int16 year, Int16 month, Int16 day)
{
    return dl_dayOfWeek(year, month, day);   /* Sakamoto, 32-bit, safe */
}

DATELIB_SECTION Boolean EIsLeap(Int16 year)
{
    return (Boolean)dl_isLeap(year);
}

/* ---- ROM-date-routine replacements (wide DateType, year = full - 1904) ---- */

DATELIB_SECTION UInt32 EDateToDays(DateType d)
{
    return (UInt32)EDays((Int16)(d.year + 1904), d.month, d.day);
}

DATELIB_SECTION void EDateDaysToDate(UInt32 days, DateType *d)
{
    Int16 y, m, dd;
    EFromDays((Int32)days, &y, &m, &dd);
    d->year = (Int16)(y - 1904); d->month = (UInt8)m; d->day = (UInt8)dd;
}

DATELIB_SECTION void EDateSecondsToDate(UInt32 seconds, DateType *d)
{
    /* 'seconds' is the re-based (window) clock; + Hc_Offset -> the REAL date.
       UInt32 / constant -> no 64-bit libgcc. */
    Int16 y, m, dd;
    EFromDays((Int32)(seconds / 86400UL), &y, &m, &dd);
    d->year  = (Int16)(y + Hc_Offset() - 1904);
    d->month = (UInt8)m;
    d->day   = (UInt8)dd;
}

DATELIB_SECTION void EDateAdjust(DateType *d, Int32 adjustment)
{
    Int16 y, m, dd;
    EFromDays((Int32)EDateToDays(*d) + adjustment, &y, &m, &dd);
    d->year = (Int16)(y - 1904); d->month = (UInt8)m; d->day = (UInt8)dd;
}

DATELIB_SECTION Int16 EDateDaysInMonth(Int16 month, Int16 year)  /* year = FULL */
{
    return dl_daysInMonth(year, month);
}

DATELIB_SECTION Int16 EDateDayOfWeek(Int16 month, Int16 day, Int16 year)
{
    return dl_dayOfWeek(year, month, day);
}

DATELIB_SECTION Int16 EDateDayOfMonth(Int16 month, Int16 day, Int16 year)
{
    /* which occurrence of the weekday within the month (1st..5th). */
    return (Int16)(((day - 1) / 7) + 1);
}

DATELIB_SECTION Int32 EDateInt(DateType d)
{
    if ((UInt16)d.year == 0xffffU) return (Int32)0xffffL;   /* "forever" */
    return ((Int32)(UInt16)d.year << 9) | ((Int32)d.month << 5) | (Int32)d.day;
}

DATELIB_SECTION void EDateSetForever(DateType *d)
{
    d->year = (Int16)0xffff; d->month = 0; d->day = 0;
}

/* Aware-tier "now": the re-based clock -> a REAL DateTimeType. All divisors are
   constants (UInt32), so no 64-bit libgcc. */
DATELIB_SECTION void ETimSecondsToDateTime(UInt32 seconds, DateTimeType *dt)
{
    UInt32 sod = seconds % 86400UL;
    Int16  y, m, dd;
    Int32  ry;
    EFromDays((Int32)(seconds / 86400UL), &y, &m, &dd);
    ry = (Int32)y + Hc_Offset();
    dt->second  = (Int16)(sod % 60UL);
    dt->minute  = (Int16)((sod / 60UL) % 60UL);
    dt->hour    = (Int16)(sod / 3600UL);
    dt->day     = (Int16)dd;
    dt->month   = (Int16)m;
    dt->year    = (Int16)ry;
    dt->weekDay = EDayOfWeek((Int16)ry, (Int16)m, (Int16)dd);
}
