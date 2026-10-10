/*
 * edate.h - full-range (1904..9999) date primitives, datelib-backed.
 *
 * The "aware tier" foundation (2026-07-08 decision): Datebook35/ToDo35 store and
 * compute dates across the whole Gregorian range in ONE session, with no era
 * switching, by replacing the ROM date routines (which only work in 1904..2031)
 * with these datelib wrappers. The hack is then needed ONLY to learn "now"
 * (Hc_CurrentRealDate), not for any date math.
 *
 * These primitives work in FULL years (e.g. 2500), 1=Jan..12=Dec, 1..31, and a
 * day count relative to 1904-01-01 (matching the ROM DateToDays epoch so day
 * arithmetic/deltas port unchanged).
 *
 * MIGRATION (how the app moves onto these — see datebook35-era-support memory):
 *   1. Force-include an override header that (a) renames the SDK's 7-bit DateType
 *      to _SysDateType, then defines the app's DateType as a WIDE struct
 *      { Int16 year; UInt8 month; UInt8 day; } keeping the year=(full-1904)
 *      OFFSET convention so all the app's `date.year + firstYear` sites are
 *      unchanged; (b) #define DateToDays/DaysToDate/DateToInt/DateCompare/
 *      DateAdjust/DateSecondsToDate/DaysInMonth/DayOfWeek/DayOfMonth to the
 *      EDate* wrappers below so the ~300 call sites redirect automatically.
 *   2. Pack/unpack: the wide DateType is 4 bytes (was 2) -> new record format.
 *   3. Display: keep custom formatting but drive weekday from EDayOfWeek.
 *   4. "Today": DateSecondsToDate(TimGetSeconds()) -> Hc_CurrentRealDate.
 *   5. Picker: unclamp to 1904..9999.
 */
#ifndef EDATE_H
#define EDATE_H

#include <PalmOS.h>

#ifndef DATELIB_SECTION
#define DATELIB_SECTION
#endif

/* days since 1904-01-01 (same epoch as ROM DateToDays), full-range. */
Int32   EDays(Int16 year, Int16 month, Int16 day) DATELIB_SECTION;
void    EFromDays(Int32 days, Int16 *year, Int16 *month, Int16 *day) DATELIB_SECTION;

Int16   EDaysInMonth(Int16 year, Int16 month) DATELIB_SECTION;   /* leap-aware */
Int16   EDayOfWeek(Int16 year, Int16 month, Int16 day) DATELIB_SECTION; /* 0=Sun */
Boolean EIsLeap(Int16 year) DATELIB_SECTION;

/* -------- ROM-date-routine replacements (see edate_override.h) --------
 * These take the WIDE DateType (year = full - 1904); the app's calls to the ROM
 * names are macro-redirected here. Full-range, datelib-backed. */
UInt32  EDateToDays(DateType d) DATELIB_SECTION;
void    EDateDaysToDate(UInt32 days, DateType *d) DATELIB_SECTION;
void    EDateSecondsToDate(UInt32 seconds, DateType *d) DATELIB_SECTION;  /* -> REAL today */
void    EDateAdjust(DateType *d, Int32 adjustment) DATELIB_SECTION;
Int16   EDateDaysInMonth(Int16 month, Int16 year) DATELIB_SECTION;   /* year = FULL */
Int16   EDateDayOfWeek(Int16 month, Int16 day, Int16 year) DATELIB_SECTION;
Int16   EDateDayOfMonth(Int16 month, Int16 day, Int16 year) DATELIB_SECTION;
Int32   EDateInt(DateType d) DATELIB_SECTION;      /* monotonic; 0xffff if "forever" */
void    EDateSetForever(DateType *d) DATELIB_SECTION;
void    ETimSecondsToDateTime(UInt32 seconds, DateTimeType *dt) DATELIB_SECTION;

#endif /* EDATE_H */
