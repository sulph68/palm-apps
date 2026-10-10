/*
 * CalDAVSync.c - app entry point. Kept deliberately thin (launch dispatch,
 * StartApplication/StopApplication, EventLoop only), matching lazyFTP's
 * own CalDAVSync.c-equivalent (LazyFTP.c) shape - a plain evtWaitForever
 * loop, no tick-driven polling needed since sync happens as a blocking
 * call under a progress dialog, not a background state machine.
 *
 * Sec35-mandatory launch gate mirrors BitWarden.c's own identical logic
 * exactly (this app stores sensitive material - app-specific passwords and
 * OAuth refresh tokens - and refuses to run at all without Sec35 available
 * to protect it, matching BitWarden's own explicit design, unlike
 * CryptoNote/Datebook35's optional integration).
 */
#include "CalDAVSync.h"
#include "MainForm.h"
#include "CDDVSec35.h"

static Boolean AppHandleEvent(EventType *e)
{
	if (e->eType == frmLoadEvent) {
		FormPtr frm = FrmInitForm(e->data.frmLoad.formID);
		FrmSetActiveForm(frm);
		switch (e->data.frmLoad.formID) {
		case frmMain:
			FrmSetEventHandler(frm, MainFormHandleEvent);
			break;
		}
		return true;
	}
	return false;
}

static void EventLoop(void)
{
	EventType e;
	UInt16 uerr;

	do {
		EvtGetEvent(&e, evtWaitForever);
		if (SysHandleEvent(&e)) continue;
		if (MenuHandleEvent(0, &e, &uerr)) continue;
		if (AppHandleEvent(&e)) continue;
		FrmDispatchEvent(&e);
	} while (e.eType != appStopEvent);
}

static Err StartApplication(void)
{
	FrmGotoForm(frmMain);
	return errNone;
}

static void StopApplication(void)
{
	FrmCloseAllForms();
}

UInt32 PilotMain(UInt16 cmd, void *cmdPBP, UInt16 launchFlags)
{
	Err err;
	(void) cmdPBP;
	(void) launchFlags;

	if (cmd == sysAppLaunchCmdNormalLaunch) {
		if (!CDDVSec35Installed()) {
			FrmCustomAlert(altErr,
				"Sec35 is not installed. Launch will not proceed as saved passwords/tokens would not be encrypted at rest.",
				NULL, NULL);
			return 0;
		}
		if (!EncKeyPresent()) {
			/* Bounce straight to Sec35's unlock UI before showing any of our
			 * own UI - Sec35 switches back here via a normal re-launch on
			 * success, which re-runs this same check and falls through. */
			CDDVSwitchToSec35();
			return 0;
		}

		err = StartApplication();
		if (err) return err;
		EventLoop();
		StopApplication();
	}

	return 0;
}
