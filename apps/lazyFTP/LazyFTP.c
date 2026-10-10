/*
 * LazyFTP.c - app entry point. Kept deliberately thin (launch dispatch,
 * StartApplication/StopApplication, EventLoop only) - reserved for a future
 * cross-app custom launch code even though nothing uses it yet, see
 * lz_seg.h and project memory lazyftp-project.md. Everything else lives in
 * secondary segments and is reached via ordinary cross-segment calls.
 *
 * No non-blocking state machine here (unlike EraNTP) - lazyFTP's transfers
 * run as blocking calls under a busy/progress dialog (BusyDialog.c),
 * matching Address35/CryptoNote's own SD-card operations. EventLoop is a
 * plain evtWaitForever loop, no tick-driven polling needed.
 */
#include "LazyFTP.h"
#include "FtpProtocol.h"
#include "PrefsForm.h"
#include "LzTPLog.h"

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
	LzTPLogInit();	/* fresh, empty run-scoped log - see LzTPLog.c */
	LzTPLoadUiPrefs();
	FtpSetStatusHook(LzTPBusyStatusHook);
	FtpSetConnRetryHook(LzTPBusyRetitle);
	FrmGotoForm(frmMain);
	return errNone;
}

static void StopApplication(void)
{
	/* Server session: ALWAYS closed on exit, unconditionally - an open FTP
	 * control connection has no reason to survive the app that owns it.
	 * Network/SLIP connection: only torn down if the user opted into that
	 * stronger action via the "Disconnect on exit" preference (Options >
	 * Preferences, default off) - otherwise left up for whatever's next. */
	if (FtpIsConnected()) FtpDisconnectServer();
	if (gUiPrefs.disconnectNetworkOnExit) FtpDisconnectNetwork();
	FrmCloseAllForms();
}

UInt32 PilotMain(UInt16 cmd, void *cmdPBP, UInt16 launchFlags)
{
	Err err;

	if (cmd == sysAppLaunchCmdNormalLaunch) {
		err = StartApplication();
		if (err) return err;
		EventLoop();
		StopApplication();
	}

	return errNone;
}
