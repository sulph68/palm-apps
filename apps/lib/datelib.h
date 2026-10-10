/*
 * datelib.h - shared 64-bit Gregorian calendar engine (Int32 year).
 *
 * Pure, global-free math used by the HackMaster Y2K35 plugin and (Phase 2) the
 * rewritten Datebook+/ToDo+ apps. Host-portable (this header uses <stdint.h>);
 * for the m68k build the dl_* integer typedefs are swapped for the Palm SDK
 * types. See hackmaster_y2035.md sections 3 and 8.
 */
#ifndef DATELIB_H
#define DATELIB_H

#include <stdint.h>

#ifndef DATELIB_SECTION
#define DATELIB_SECTION
#endif

typedef int64_t dl_i64;
typedef int32_t dl_i32;
typedef int16_t dl_i16;
typedef int8_t  dl_i8;
typedef int     dl_bool;

/* Legacy re-base window (native Palm DateType range, 1904..2031). */
#define DL_WINDOW_LOW    1904
#define DL_WINDOW_HIGH   2031
#define DL_EPOCH_YEAR    1904

typedef struct {
    dl_i32 year;      /* full Int32 year */
    dl_i16 month;     /* 1..12 */
    dl_i16 day;       /* 1..31 */
    dl_i16 hour;      /* 0..23 */
    dl_i16 minute;    /* 0..59 */
    dl_i16 second;    /* 0..59 */
    dl_i16 weekday;   /* 0=Sunday .. 6=Saturday */
} DlDateTime;

/* Core conversions (seconds are relative to 1904-01-01 00:00:00). */
/* NOTE: every dl_* decl must carry DATELIB_SECTION to match its definition in
 * datelib.c -- Datebook35 places datelib in sec002, and an un-attributed decl
 * makes callers emit a main-segment call (wild jump). */
dl_i64 dl_dateToSeconds(dl_i32 year, dl_i16 mon, dl_i16 day,
                        dl_i16 hour, dl_i16 min, dl_i16 sec) DATELIB_SECTION;
void   dl_secondsToDate(dl_i64 secs, DlDateTime *out) DATELIB_SECTION;

/* Full-Gregorian primitives. */
dl_bool dl_isLeap(dl_i32 year) DATELIB_SECTION;
dl_i16  dl_daysInMonth(dl_i32 year, dl_i16 mon) DATELIB_SECTION;
dl_i16  dl_dayOfWeek(dl_i32 year, dl_i16 mon, dl_i16 day) DATELIB_SECTION;  /* 0=Sun */

/* Re-basing (legacy tier). yearType = weekday(Jan 1)*2 + isLeap, range 0..13;
 * two years with the same type share an identical calendar. */
dl_i8  dl_yearType(dl_i32 year) DATELIB_SECTION;
dl_i32 dl_matchInWindow(dl_i32 year) DATELIB_SECTION;   /* same-type year in [LOW,HIGH] */
dl_i32 dl_reBaseOffset(dl_i32 realYear) DATELIB_SECTION; /* whole-year offset, 0 if <=2031 */

#endif /* DATELIB_H */
