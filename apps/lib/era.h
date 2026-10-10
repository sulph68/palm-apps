/*
 * era.h - era-aware date DISPLAY helpers shared by the Y2K35 apps.
 *
 * The X-Y2K35 plugin re-bases the RTC into the native DateType window
 * (1904..2031), so month/day/weekday/leap are correct for the WINDOW year, but
 * the year NUMBER is "wrong on purpose" (window, not real). Apps store window
 * DateTypes and do all math with the ROM date routines unchanged; these helpers
 * are the thin boundary that shows the REAL year (= window year + Hc_Offset()).
 *
 * The ROM date formatters bake the weekday AND the year from a single year
 * argument, and the ROM math is only valid in 1904..2031 -- so we always feed
 * them the WINDOW year (correct weekday/layout/range) and swap in the real year
 * for the digits the user sees.
 */
#ifndef ERA_H
#define ERA_H

#include <PalmOS.h>

#ifndef DATELIB_SECTION
#define DATELIB_SECTION
#endif

/* Like DateTemplateToAscii, but works for ANY real year: the ^4 (year) token is
 * emitted as the real year while weekday/month/day come from a same-calendar
 * window year fed to the ROM (which caps at 2031).
 * fullYear: true -> 4-digit (unambiguous; a '\'' before the year is dropped);
 * false -> 2-digit (real % 100), for tight spots like the day-view title. */
void EraDateTemplateToAscii(const Char *templateP, UInt8 month, UInt8 day,
                            UInt16 realFullYear, Boolean fullYear,
                            Char *outP, Int16 maxLen) DATELIB_SECTION;

/* Like DateToDOWDMFormat / DateToAscii, but shows the real year for any year. */
void EraDateToDOWDMFormat(UInt8 month, UInt8 day, UInt16 realFullYear,
                          DateFormatType fmt, Char *outP) DATELIB_SECTION;
void EraDateToAscii(UInt8 month, UInt8 day, UInt16 realFullYear,
                    DateFormatType fmt, Char *outP) DATELIB_SECTION;

#endif /* ERA_H */
