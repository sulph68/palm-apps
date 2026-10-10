/*
 * PrefsForm.c - global Preferences: past/future sync-window defaults
 * (used by any slot without its own override) and the serial baud rate.
 *
 * Hand-rolled modal event loop, not FrmDoDialog - FrmDoDialog doesn't
 * reliably dispatch frmOpenEvent through a custom handler (see lazyFTP's
 * ConnectForm.c). Populate fields before FrmDrawForm, but defer popup-
 * trigger CtlDrawControl until after it - drawing on a trigger before the
 * modal form's implicit background capture corrupts that capture.
 */
#include "PrefsForm.h"
#include "CDDVPrefs.h"

static const char *gPastLabels[] = { "None", "1 week", "2 weeks", "1 month", "2 months", "3 months" };
static const char *gFutureLabels[] = { "2 weeks", "1 month", "3 months", "6 months", "1 year" };
/* Same order as WiRSa's own bauds[] (main.cpp) and CDDVPrefs.c's kCddvBauds. */
static const char *gBaudLabels[] = { "300", "1200", "2400", "4800", "9600", "19200", "38400", "57600", "115200" };

static CDDVPrefsType *gEditPrefs;
static FormPtr gFrm;
/* Baud isn't part of CDDVPrefsType (see CDDVPrefs.c) - tracked here,
 * persisted via CDDVSetBaudIndex only once OK is tapped. */
static UInt8 gEditBaudIndex;

/* Static, not a string literal handed to CtlSetLabel (which keeps a
 * pointer, not a copy) - a literal owned only by this SEC001 file can
 * dangle once the segment isn't resident (real intermittent Fatal
 * Exception on device, not caught by POSE64). Three separate buffers
 * since all three triggers are set in the same call sequence below. */
static char gPastTriggerBuf[10];
static char gFutureTriggerBuf[10];
static char gBaudTriggerBuf[10];

static void SetTrigger(UInt16 popupID, const char *label)
{
	ControlType *trigger = (ControlType *) FrmGetObjectPtr(gFrm, FrmGetObjectIndex(gFrm, popupID));
	char *dst = (popupID == popPrefsPast) ? gPastTriggerBuf :
		(popupID == popPrefsFuture) ? gFutureTriggerBuf : gBaudTriggerBuf;
	StrCopy(dst, label);
	CtlSetLabel(trigger, dst);
	CtlDrawControl(trigger);
}

static Boolean PrefsFormHandleEvent(EventType *e)
{
	Boolean handled = false;

	switch (e->eType) {
	case popSelectEvent:
		if (e->data.popSelect.listID == lstPrefsPast) {
			gEditPrefs->pastDefault = (CDDVPastWindow) e->data.popSelect.selection;
		} else if (e->data.popSelect.listID == lstPrefsFuture) {
			gEditPrefs->futureDefault = (CDDVFutureWindow) e->data.popSelect.selection;
		} else if (e->data.popSelect.listID == lstPrefsBaud) {
			gEditBaudIndex = (UInt8) e->data.popSelect.selection;
		}
		break;
	default:
		break;
	}
	return handled;
}

SEC001 void PrefsFormEdit(CDDVPrefsType *p)
{
	FormPtr prevFrm;
	EventType event;
	Boolean done = false;
	UInt16 uerr;

	gEditPrefs = p;
	prevFrm = FrmGetActiveForm();
	gFrm = FrmInitForm(frmPrefs);
	if (!gFrm) return;
	FrmSetActiveForm(gFrm);

	LstSetSelection((ListType *) FrmGetObjectPtr(gFrm, FrmGetObjectIndex(gFrm, lstPrefsPast)),
		(Int16) gEditPrefs->pastDefault);
	LstSetSelection((ListType *) FrmGetObjectPtr(gFrm, FrmGetObjectIndex(gFrm, lstPrefsFuture)),
		(Int16) gEditPrefs->futureDefault);
	gEditBaudIndex = CDDVGetBaudIndex();
	LstSetSelection((ListType *) FrmGetObjectPtr(gFrm, FrmGetObjectIndex(gFrm, lstPrefsBaud)),
		(Int16) gEditBaudIndex);

	FrmDrawForm(gFrm);

	/* After FrmDrawForm - see file header comment. */
	SetTrigger(popPrefsPast, gPastLabels[gEditPrefs->pastDefault]);
	SetTrigger(popPrefsFuture, gFutureLabels[gEditPrefs->futureDefault]);
	SetTrigger(popPrefsBaud, gBaudLabels[gEditBaudIndex]);

	while (!done) {
		EvtGetEvent(&event, evtWaitForever);
		if (event.eType == appStopEvent) {
			EvtAddEventToQueue(&event);
			break;
		}
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, &uerr)) continue;
		if (PrefsFormHandleEvent(&event)) continue;

		if (event.eType == ctlSelectEvent && event.data.ctlSelect.controlID == btnPrefsOK) {
			CDDVSetBaudIndex(gEditBaudIndex);
			done = true;
			continue;
		}
		FrmHandleEvent(gFrm, &event);
	}

	FrmEraseForm(gFrm);
	FrmDeleteForm(gFrm);
	gFrm = NULL;

	if (prevFrm) {
		FrmSetActiveForm(prevFrm);
		FrmDrawForm(prevFrm);
	}
}
