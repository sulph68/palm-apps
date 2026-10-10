/*
 * datelib.c - implementation. See datelib.h.
 *
 * Day-number conversions use Howard Hinnant's civil<->days algorithms, which
 * are exact for any proleptic-Gregorian year and branch-free. All arithmetic is
 * done in dl_i64 so Int32 years (to ~2.1e9) never overflow.
 *
 * NOTE: written in strict C89 (declarations at block top) so it compiles with
 * the Palm toolchain's gcc 2.95.3 as well as a modern host compiler.
 */
#include "datelib.h"

/* Days from 1970-01-01 for a Gregorian y/m/d (m,d 1-based). */
DATELIB_SECTION static dl_i64 days_from_civil(dl_i64 y, dl_i64 m, dl_i64 d)
{
    dl_i64 era, yoe, doy, doe;
    y -= (m <= 2);
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = y - era * 400;                                     /* [0,399]     */
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;    /* [0,365]     */
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;             /* [0,146096]  */
    return era * 146097 + doe - 719468;
}

/* Inverse: y/m/d from days since 1970-01-01. */
DATELIB_SECTION static void civil_from_days(dl_i64 z, dl_i64 *yy, dl_i64 *mm, dl_i64 *dd)
{
    dl_i64 era, doe, yoe, y, doy, mp, d, m;
    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;                                  /* [0,146096]  */
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; /* [0,399] */
    y   = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);           /* [0,365]     */
    mp  = (5 * doy + 2) / 153;                               /* [0,11]      */
    d   = doy - (153 * mp + 2) / 5 + 1;                      /* [1,31]      */
    m   = mp < 10 ? mp + 3 : mp - 9;                         /* [1,12]      */
    *yy = y + (m <= 2);
    *mm = m;
    *dd = d;
}

DATELIB_SECTION static dl_i64 epoch_days(void)   /* days from 1970-01-01 to the 1904 epoch */
{
    return days_from_civil(DL_EPOCH_YEAR, 1, 1);
}

DATELIB_SECTION dl_i64 dl_dateToSeconds(dl_i32 year, dl_i16 mon, dl_i16 day,
                        dl_i16 hour, dl_i16 min, dl_i16 sec)
{
    dl_i64 days = days_from_civil(year, mon, day) - epoch_days();
    /* Accumulate the (small) time-of-day in its own 64-bit value, then add it
     * once. Chaining these small adds directly onto days*86400 miscompiles
     * under gcc 2.95.3 when days*86400 exceeds 2^32 (year >~ 2040): the sub-hour
     * terms get dropped. Keeping tod separate avoids that. */
    dl_i64 tod = (dl_i64) hour * 3600 + (dl_i64) min * 60 + (dl_i64) sec;
    return days * 86400 + tod;
}

DATELIB_SECTION void dl_secondsToDate(dl_i64 secs, DlDateTime *out)
{
    dl_i64 days, tod, y, m, d;
    days = secs / 86400;
    tod  = secs - days * 86400;
    if (tod < 0) { tod += 86400; days -= 1; }   /* floor toward -inf */

    civil_from_days(days + epoch_days(), &y, &m, &d);

    out->year    = (dl_i32) y;
    out->month   = (dl_i16) m;
    out->day     = (dl_i16) d;
    out->hour    = (dl_i16) (tod / 3600);
    out->minute  = (dl_i16) ((tod % 3600) / 60);
    out->second  = (dl_i16) (tod % 60);
    out->weekday = dl_dayOfWeek((dl_i32) y, (dl_i16) m, (dl_i16) d);
}

DATELIB_SECTION dl_bool dl_isLeap(dl_i32 year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

DATELIB_SECTION dl_i16 dl_daysInMonth(dl_i32 year, dl_i16 mon)
{
    static const dl_i16 dim[13] = { 0,31,28,31,30,31,30,31,31,30,31,30,31 };
    if (mon == 2 && dl_isLeap(year)) return 29;
    return dim[mon];
}

DATELIB_SECTION dl_i16 dl_dayOfWeek(dl_i32 year, dl_i16 mon, dl_i16 day)
{
    /* Sakamoto's algorithm: 32-bit only, and every divisor is a constant so
     * gcc lowers it to multiply/shift -- no 64-bit libgcc helpers (__divdi3
     * etc.), which are unreachable from a non-main code segment. 0 = Sunday. */
    static const dl_i8 t[12] = { 0,3,2,5,0,3,5,1,4,6,2,4 };
    dl_i32 y = year;
    if (mon < 3) y -= 1;
    return (dl_i16)(((y + y/4 - y/100 + y/400 + t[mon-1] + day) % 7 + 7) % 7);
}

DATELIB_SECTION dl_i8 dl_yearType(dl_i32 year)
{
    return (dl_i8) (dl_dayOfWeek(year, 1, 1) * 2 + (dl_isLeap(year) ? 1 : 0));
}

/* Nearest same-type year to the window centre (1967) -> balanced era headroom. */
DATELIB_SECTION dl_i32 dl_matchInWindow(dl_i32 year)
{
    dl_i8  t;
    dl_i32 best, bestDist, yy, c, dist;
    t = dl_yearType(year);
    best = -1; bestDist = 0;
    c = (DL_WINDOW_LOW + DL_WINDOW_HIGH) / 2;   /* 1967 */
    for (yy = DL_WINDOW_LOW; yy <= DL_WINDOW_HIGH; yy++) {
        if (dl_yearType(yy) == t) {
            dist = yy > c ? yy - c : c - yy;
            if (best < 0 || dist < bestDist) { best = yy; bestDist = dist; }
        }
    }
    return best;
}

DATELIB_SECTION dl_i32 dl_reBaseOffset(dl_i32 realYear)
{
    if (realYear <= DL_WINDOW_HIGH) return 0;
    return realYear - dl_matchInWindow(realYear);
}
