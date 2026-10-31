/*
 * Modes.c - frmModes: pick one WiRSa mode (popup; guidance from
 * strModeNotes underneath) and Start it; FTP server Start/Stop push buttons.
 */
#include <PalmOS.h>
#include "WiRSaControl.h"
#include "relayproto.h"
#include "relaywire.h"

static char gModeLabel[24];
static char gModeNote[160];
static char gFtpText[32];

static void ModesShowNote(Int16 sel)
{
	FormPtr frm = FrmGetActiveForm();
	ListPtr lst = (ListPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, lstModesMode));
	ControlPtr pop = (ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, popModesMode));

	LstSetSelection(lst, sel);
	StrNCopy(gModeLabel, LstGetSelectionText(lst, sel), sizeof(gModeLabel) - 1);
	gModeLabel[sizeof(gModeLabel) - 1] = '\0';
	CtlSetLabel(pop, gModeLabel);
	SysStringByIndex(strModeNotes, (UInt16) sel, gModeNote, sizeof(gModeNote));
	SetFieldPtr(fldModesNote, gModeNote);
}

static void ModesShowFtp(void)
{
	FormPtr frm = FrmGetActiveForm();

	if (gStatus.valid) {
		FrmSetControlGroupSelection(frm, 1, gStatus.ftp ? pbtModesFtpStart : pbtModesFtpStop);
		StrCopy(gFtpText, gStatus.ftp ? "Started" : "Stopped");
	} else {
		CtlSetValue((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, pbtModesFtpStart)), 0);
		CtlSetValue((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, pbtModesFtpStop)), 0);
		StrCopy(gFtpText, "Unknown");
	}
	SetFieldPtr(fldModesFtp, gFtpText);
}

static void ModesFtp(Boolean start)
{
	UInt8 body[1], rep[8];
	UInt16 n;
	UInt8 r;

	body[0] = start ? 1 : 0;
	BusyShow(start ? "Starting FTP..." : "Stopping FTP...");
	r = WCtlBegin(SweepOK());
	if (r == wctlOk) {
		r = WCtlRequest(wctlTypeFtp, body, 1, rep, sizeof(rep), &n, SysTicksPerSecond() * 5);
		if (r == wctlOk)
			r = StatusFetch();
		WCtlEnd();
	}
	BusyHide();
	if (r != wctlOk)
		ShowResult(r);
	ModesShowFtp();
}

/* Leave WiRSa's current mode (back to its main menu). Port open. */
static UInt8 ModesExitCurrent(void)
{
	UInt8 rep[8];
	UInt16 n;
	UInt8 r = WCtlRequest(wctlTypeExit, (const UInt8 *) "EXIT", 4, rep, sizeof(rep), &n,
		SysTicksPerSecond() * 3);
	if (r == wctlOk) {
		/* WiRSa leaves the mode and restarts its UART at the menu rate;
		 * give it time before the next request. */
		SysTaskDelay(SysTicksPerSecond() / 2);
		if (gWCtlInfo.menuBaud)
			WCtlSetBaud(gWCtlInfo.menuBaud);
	}
	return r;
}

static void ModesStart(Int16 sel)
{
	UInt8 letter = (UInt8) kModeLetters[sel];
	char confirm[96];
	UInt8 rep[8];
	UInt16 n;
	UInt8 r;
	Boolean asked = false;		/* the ENTER/EXIT request went out */

	/* HotSync and SASD run off the selected disk (Setup > Disk): don't start them when the last status
	   said there is none (sd 2; the virtual card counts as a disk, sd 3). */
	if ((letter == 'H' || letter == 'D') && gStatus.valid && gStatus.sd == 2) {
		FrmCustomAlert(altError, "The last status showed no disk (SD card or virtual card). Insert a card, wait a few seconds, refresh the main screen, then try again.", NULL, NULL);
		return;
	}

	SysStringByIndex(strModeConfirm, (UInt16) sel, confirm, sizeof(confirm));
	if (confirm[0] && FrmCustomAlert(altConfirmMode, confirm, NULL, NULL) != 0)
		return;

	BusyShow("Talking to WiRSa...");
	r = WCtlBegin(SweepOK());
	if (r == wctlOk) {
		if (gWCtlInfo.mode == letter) {
			/* already there */
		} else if (letter == 'M') {
			asked = true;
			r = ModesExitCurrent();
		} else {
			if (gWCtlInfo.mode != 'M')
				r = ModesExitCurrent();
			if (r == wctlOk) {
				UInt8 body[1];
				body[0] = letter;
				asked = true;
				r = WCtlRequest(wctlTypeEnter, body, 1, rep, sizeof(rep), &n, SysTicksPerSecond() * 5);
			}
		}
		WCtlEnd();
	}
	BusyHide();

	/* A lost reply after the request went out most likely means WiRSa
	 * switched straight away: treat it as started. A refusal (no WiFi ...)
	 * or not reaching WiRSa at all is shown, then back to the main screen
	 * either way. */
	if (r == wctlNoReply && asked)
		r = wctlOk;
	if (r != wctlOk) {
		ShowResult(r);
		FrmReturnToForm(frmMain);
		FrmUpdateForm(frmMain, updRefresh);
		return;
	}
	/* Each mode listens at its own rate from now on. */
	if (letter == 'M' || letter == 'S')
		gWCtlBaud = gWCtlInfo.menuBaud ? gWCtlInfo.menuBaud : gWCtlBaud;
	else
		gWCtlBaud = rpBaudIdle;
	/* The mode is still starting up and won't answer yet: don't ask, show
	 * it as last known (the main screen's refresh asks later). */
	gPrefs.lastMode = letter;
	PrefsSave();
	FrmReturnToForm(frmMain);
	FrmUpdateForm(frmMain, updLastKnown);
}

Boolean ModesHandleEvent(EventPtr e)
{
	FormPtr frm;
	Int16 sel;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		sel = ModeIndex(gStatus.mode);
		if (sel < 0) sel = 0;
		ModesShowNote(sel);
		ModesShowFtp();
		FrmDrawForm(frm);
		return true;

	case popSelectEvent:
		if (e->data.popSelect.listID == lstModesMode && e->data.popSelect.selection >= 0) {
			ModesShowNote(e->data.popSelect.selection);
			return true;
		}
		break;

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case pbtModesFtpStart:	ModesFtp(true); return true;
		case pbtModesFtpStop:	ModesFtp(false); return true;
		case btnModesStart:
			frm = FrmGetActiveForm();
			sel = LstGetSelection((ListPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, lstModesMode)));
			if (sel >= 0 && sel < kNumModes)
				ModesStart(sel);
			return true;
		case btnModesCancel:
			FrmReturnToForm(frmMain);
			return true;
		}
		break;

	case frmCloseEvent:
		FldSetTextPtr(GetField(fldModesNote), NULL);
		FldSetTextPtr(GetField(fldModesFtp), NULL);
		break;

	default:
		break;
	}
	return false;
}
