#ifndef CDDV_PREFSFORM_H
#define CDDV_PREFSFORM_H

#include "CalDAVSync.h"

/* Runs the global Preferences modal dialog (past/future sync-window
 * defaults). Edits p->pastDefault/futureDefault in place; caller persists
 * via CDDVSavePrefs afterward. */
void PrefsFormEdit(CDDVPrefsType *p) SEC001;

#endif
