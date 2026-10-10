/*
 * datepick.h - a full-range date picker that looks EXACTLY like the ROM
 * SelectDay "Go to Date" dialog (year + prev/next arrows, a 6x2 month-name
 * grid, a day calendar, Cancel/Today), but supports any year (SelectDay's
 * DateType caps at 2031). This is the era-aware replacement for SelectDay.
 *
 * Shared by every era-aware app: the drawing/logic lives here once; each app
 * includes the form via `#include "datepick.rcp"` and links datepick.c.
 */
#ifndef DATEPICK_H
#define DATEPICK_H

#include <PalmOS.h>
#include "datelib.h"

#ifndef DATELIB_SECTION
#define DATELIB_SECTION
#endif

/* Resource ids (also used in datepick.rcp). Kept in the 8000s to avoid app ids. */
#define DATEPICK_FORM     8000
#define DP_BTN_TODAY      8001
#define DP_BTN_CANCEL     8003
#define DP_LBL_YEAR       8010
#define DP_YEAR_PREV      8200
#define DP_YEAR_NEXT      8201
#define DP_MON_JAN        8101      /* Jan..Dec = 8101..8112 */
#define DP_MON_GROUP      1         /* pushbutton group for the month buttons */
#define DP_GADGET         8300

/* Open the picker seeded with *io. Returns true and updates *io if the user
 * picks a day; false (io unchanged) on Cancel. titleP may be NULL. */
Boolean DatePicker(DlDateTime *io, const char *titleP) DATELIB_SECTION;

/* Drop-in for the ROM SelectDay() with the identical signature but full range.
 * selectDayBy is accepted for compatibility and ignored (we always pick a day). */
Boolean DatePickSelectDay(const SelectDayType selectDayBy, Int16 *month, Int16 *day,
                          Int16 *year, const Char *titleP) DATELIB_SECTION;

#endif /* DATEPICK_H */
