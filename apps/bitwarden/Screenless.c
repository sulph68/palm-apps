/*
 * Screenless.c - Screenless Mode (Preferences): for a WiRSa without a
 * screen, the app itself puts WiRSa into BitWarden Relay before a sync
 * (../lib/wirsactl.c WCtlEnsureMode) and back to its main menu after
 * (WCtlLeaveMode). Off by default - then BwSyncScreenless is just
 * BwSyncRun. All SEC001: main .text is full (bw_seg.h).
 */
#include "BitWarden.h"
#include "wirsactl.h"
#include "relaysec.h"	/* gRpKexReplyTicks */

/* Its own pref ID, like the baud rate (Store.c), so it never touches the
 * main prefs block. */
#define bwScreenlessPrefID	3

Boolean BwGetScreenless(void)
{
	UInt8 v = 0;
	UInt16 sz = sizeof(v);
	if (PrefGetAppPreferences(appCreator, bwScreenlessPrefID, &v, &sz, false) == noPreferenceFound
		|| sz != sizeof(v))
		return false;
	return (Boolean) (v != 0);
}

void BwSetScreenless(Boolean on)
{
	UInt8 v = on ? 1 : 0;
	PrefSetAppPreferences(appCreator, bwScreenlessPrefID, 1, &v, sizeof(v), false);
}

void BwScreenlessFormLoad(void)
{
	FormPtr frm = FrmGetActiveForm();
	UInt16 idx = FrmGetObjectIndex(frm, chkPrefsScreenless);
	if (idx == frmInvalidObjectId) return;
	CtlSetValue((ControlType *) FrmGetObjectPtr(frm, idx), BwGetScreenless());
}

void BwScreenlessFormSave(void)
{
	FormPtr frm = FrmGetActiveForm();
	UInt16 idx = FrmGetObjectIndex(frm, chkPrefsScreenless);
	if (idx == frmInvalidObjectId) return;
	BwSetScreenless((Boolean) CtlGetValue((ControlType *) FrmGetObjectPtr(frm, idx)));
}

Boolean BwSyncScreenless(void)
{
	Boolean ok, cancelled;
	UInt8 r;

	if (!BwGetScreenless())
		return BwSyncRun();

	BwProgressShow("Starting WiRSa...");
	r = WCtlEnsureMode('B', BwGetBaud());
	cancelled = BwProgressCancelled();
	BwProgressHide();
	if (cancelled) {
		ok = false;				/* Cancel tapped: no alert, no sync */
	} else if (r == wctlErrReply) {
		FrmCustomAlert(altErr, gWCtlErrMsg[0] ? gWCtlErrMsg : "WiRSa refused to start BitWarden Relay.", NULL, NULL);
		ok = false;
	} else if (r == wctlModeNotUp) {
		/* WiRSa answered but the mode never did - syncing would just hang */
		FrmCustomAlert(altErr, "WiRSa didn't switch to BitWarden Relay. Try again, or reset WiRSa.", NULL, NULL);
		ok = false;
	} else if (r != wctlOk) {
		/* Not found at any rate: syncing anyway would only hang. */
		FrmCustomAlert(altErr, r == wctlPortBusy ? "The serial port is in use." :
			"WiRSa didn't answer. Check the cable and that WiRSa is on (Screenless Mode needs the WiRSa control link).", NULL, NULL);
		ok = false;
	} else {
		/* The mode is up and its key ready: don't wait 3 minutes on a lost
		 * first KEX_REQ (relaysec.h). */
		gRpKexReplyTicks = SysTicksPerSecond() * 5;
		ok = BwSyncRun();
		gRpKexReplyTicks = 0;
	}
	/* Back to the main menu - only if WiRSa was reached above (otherwise
	 * there is no control link to talk to). */
	if (r == wctlOk || r == wctlModeNotUp) {
		BwProgressShow("Returning WiRSa...");
		WCtlLeaveMode(BwGetBaud());
		BwProgressHide();
	}
	return ok;
}
