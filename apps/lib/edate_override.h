/*
 * edate_override.h - aware-tier date widening (force-included FIRST).
 *
 * Parse the SDK's 7-bit DateType under the name _SysDateType, then give the app
 * a WIDE DateType that can hold any year 1904..9999, and macro-redirect the ROM
 * date routines (all capped at 2031) to the datelib-backed EDate* wrappers
 * (declared in edate.h, defined in edate.c). The ~300 app call sites redirect
 * with no edits. See edate.h and the datebook35-era-support memory.
 *
 * The wide DateType keeps the ROM "year = full - 1904" OFFSET convention, so
 * every `date.year + firstYear` / `year - firstYear` site is unchanged.
 */
#ifndef EDATE_OVERRIDE_H
#define EDATE_OVERRIDE_H

#define DateType _SysDateType
#include <PalmOS.h>
#undef DateType

typedef struct {
    Int16 year;    /* full year - 1904 (offset); (Int16)0xffff = "forever" */
    UInt8 month;   /* 1..12 */
    UInt8 day;     /* 1..31 */
} DateType;

/* app code that used DatePtr now points at the wide DateType */
#define DatePtr DateType *

/* Redirect the ROM date traps to full-range wrappers (prototypes in edate.h,
 * force-included after db35_seg.h so DATELIB_SECTION is set). */
#define DateToDays        EDateToDays
#define DateDaysToDate    EDateDaysToDate
#define DateSecondsToDate EDateSecondsToDate
#define DateAdjust        EDateAdjust
#define DaysInMonth       EDateDaysInMonth
#define DayOfWeek         EDateDayOfWeek
#define DayOfMonth        EDateDayOfMonth
#undef  DateToInt
#define DateToInt(d)      EDateInt(d)
/* "now" as the REAL date/time (re-based clock + era offset) */
#define TimSecondsToDateTime ETimSecondsToDateTime

/* Era: repeating appointments can occur up to year 9999. The SDK's maxDays is
 * derived from numberOfYears=128 (== Dec 31 2031) and caps NextRepeat's
 * occurrence search there, so post-2031 "no end date" repeats look ended and the
 * agenda->day-view navigation crashed ("No displayable occurrences"). Widen it to
 * the datelib range (days from 1904-01-01 to 9999-12-31). maxDays is only used by
 * NextRepeat; maxSeconds (which would overflow) is unused by the app. */
#undef  maxDays
#define maxDays ((UInt32) 2957003UL)

#endif /* EDATE_OVERRIDE_H */
