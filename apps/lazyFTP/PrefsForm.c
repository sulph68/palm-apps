/*
 * PrefsForm.c - Options > Preferences dialog. Same hand-rolled modal event
 * loop as ConnectForm.c's ConnectFormRun (original code here, following
 * that proven pattern) - FrmDoDialog does not reliably dispatch
 * frmOpenEvent through a custom handler on this SDK/glue (documented
 * gotcha, see ConnectForm.c's own comment), so this drives its own loop
 * and populates the fields directly instead.
 */
#include "PrefsForm.h"
#include "Listing.h"

LzTPUiPrefs gUiPrefs;

static FormPtr gFrm;

static UInt16 TtlToSeconds(UInt8 ttl)
{
	switch (ttl) {
	case lzTtl1Min: return 60;
	case lzTtl2Min: return 120;
	case lzTtl3Min: return 180;
	case lzTtl30Sec:
	default:        return 30;
	}
}

SEC001 void LzTPLoadUiPrefs(void)
{
	UInt16 size = sizeof(gUiPrefs);
	Int16 r = PrefGetAppPreferences(appCreator, 1, &gUiPrefs, &size, false);
	if (r == noPreferenceFound || size < sizeof(gUiPrefs) || gUiPrefs.version != lzUiPrefsVersion) {
		MemSet(&gUiPrefs, sizeof(gUiPrefs), 0);
		gUiPrefs.version = lzUiPrefsVersion;
		gUiPrefs.cacheTtl = lzTtl30Sec;
		gUiPrefs.disconnectNetworkOnExit = false;
		gUiPrefs.retryCount = 1;
	}
	if (gUiPrefs.retryCount < 1 || gUiPrefs.retryCount > 3) gUiPrefs.retryCount = 1;
	gListCacheTtlSecs = TtlToSeconds(gUiPrefs.cacheTtl);
}

SEC001 void LzTPSaveUiPrefs(void)
{
	PrefSetAppPreferences(appCreator, 1, lzUiPrefsVersion, &gUiPrefs, sizeof(gUiPrefs), false);
}

static void LoadUIFromPrefs(void)
{
	ListPtr lst = (ListPtr) FrmGetObjectPtr(gFrm, FrmGetObjectIndex(gFrm, lstPrefsTtl));
	ControlPtr trig = (ControlPtr) FrmGetObjectPtr(gFrm, FrmGetObjectIndex(gFrm, popPrefsTtl));
	ControlPtr chkDisconnect = (ControlPtr) FrmGetObjectPtr(gFrm, FrmGetObjectIndex(gFrm, chkPrefsDisconnect));

	LstSetSelection(lst, gUiPrefs.cacheTtl);
	CtlSetLabel(trig, LstGetSelectionText(lst, gUiPrefs.cacheTtl));
	CtlSetValue(chkDisconnect, gUiPrefs.disconnectNetworkOnExit);
}

static void SavePrefsFromUI(void)
{
	ListPtr lst = (ListPtr) FrmGetObjectPtr(gFrm, FrmGetObjectIndex(gFrm, lstPrefsTtl));
	ControlPtr chkDisconnect = (ControlPtr) FrmGetObjectPtr(gFrm, FrmGetObjectIndex(gFrm, chkPrefsDisconnect));

	gUiPrefs.cacheTtl = (UInt8) LstGetSelection(lst);
	gUiPrefs.disconnectNetworkOnExit = CtlGetValue(chkDisconnect);

	LzTPSaveUiPrefs();
	gListCacheTtlSecs = TtlToSeconds(gUiPrefs.cacheTtl);
}

static Boolean PrefsFormHandleEvent(EventPtr eventP)
{
	if (eventP->eType == popSelectEvent) {
		ControlPtr trig = (ControlPtr) FrmGetObjectPtr(gFrm, FrmGetObjectIndex(gFrm, popPrefsTtl));
		CtlSetLabel(trig, LstGetSelectionText(eventP->data.popSelect.listP, eventP->data.popSelect.selection));
		return true;
	}
	return false;
}

SEC001 void PrefsFormRun(void)
{
	FormPtr frm;
	FormPtr prevFrm;
	EventType event;
	Boolean done = false;
	UInt16 btn = 0;

	prevFrm = FrmGetActiveForm();
	frm = FrmInitForm(frmPrefs);
	if (!frm) return;
	gFrm = frm;

	FrmSetActiveForm(frm);
	LoadUIFromPrefs();
	FrmDrawForm(frm);

	while (!done) {
		EvtGetEvent(&event, evtWaitForever);
		if (event.eType == appStopEvent) {
			EvtAddEventToQueue(&event);
			btn = btnPrefsCancel;
			break;
		}
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, NULL)) continue;
		if (PrefsFormHandleEvent(&event)) continue;

		if (event.eType == ctlSelectEvent) {
			if (event.data.ctlSelect.controlID == btnPrefsOk) {
				btn = btnPrefsOk;
				done = true;
			} else if (event.data.ctlSelect.controlID == btnPrefsCancel) {
				btn = btnPrefsCancel;
				done = true;
			}
		}

		FrmHandleEvent(frm, &event);
	}

	if (btn == btnPrefsOk) SavePrefsFromUI();

	/* frmPrefs is not full-screen (AT 2 40 156 120) so FrmDrawForm above
	 * auto-saved the bits behind it - FrmEraseForm restores them before
	 * FrmDeleteForm, same as ConnectForm's identical situation. */
	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	gFrm = NULL;

	if (prevFrm) {
		FrmSetActiveForm(prevFrm);
		FrmDrawForm(prevFrm);
	}
}
