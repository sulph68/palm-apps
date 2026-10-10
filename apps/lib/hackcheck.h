/*
 * hackcheck.h - detect whether the HackMaster Y2K35 plugin is installed and
 * enabled, and read the era it publishes. Shared by every era-aware app; an app
 * is pointless unless the plugin is active, so call HcOkOrAlert() on launch.
 */
#ifndef HACKCHECK_H
#define HACKCHECK_H

#include <PalmOS.h>
#include "datelib.h"

#ifndef DATELIB_SECTION
#define DATELIB_SECTION
#endif

typedef enum {
    HC_OK,             /* plugin installed and enabled                */
    HC_NOT_INSTALLED,  /* plugin database not present                 */
    HC_NOT_ENABLED     /* installed but not ticked on in X-Master     */
} HcState;

HcState HcCheck(void) DATELIB_SECTION;

/* If not OK, show the matching alert; returns true iff OK. The app supplies its
 * own alert resource ids (one "not installed", one "not enabled"). */
Boolean HcOkOrAlert(UInt16 notInstalledAlertId, UInt16 notEnabledAlertId) DATELIB_SECTION;

/* The published whole-year re-base offset (0 if none / not set yet). */
Int32 Hc_Offset(void) DATELIB_SECTION;

/* "Now" as the true, full-range date the user selected as the current era:
 * the (re-based) system clock mapped back by the offset. */
void Hc_CurrentRealDate(DlDateTime *out) DATELIB_SECTION;

#endif /* HACKCHECK_H */
