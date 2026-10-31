/*
 * WiRSaControl.c - app entry, shared helpers, and frmMain (status).
 * Spec: apps/WiRSaControl.md. The serial port is only open while a request
 * runs (WCtlBegin/WCtlEnd), so other apps and the SASD driver can use it.
 */
#include <PalmOS.h>
#include "WiRSaControl.h"
#include "relayproto.h"
#include "relaywire.h"

WCPrefs gPrefs;
WCStatus gStatus;

/* Text shown in frmMain's non-editable fields (FldSetTextPtr). */
static char gTxtMode[40], gTxtLink[40], gTxtWifi[48], gTxtIp[24],
	gTxtSd[24], gTxtFtp[32], gTxtBatt[24], gTxtNote[120];

/* ---- prefs -------------------------------------------------------------- */

static void PrefsLoad(void)
{
	UInt16 size = sizeof(gPrefs);
	Int16 v = PrefGetAppPreferences(appCreator, appPrefID, &gPrefs, &size, false);
	if (v == noPreferenceFound || size != sizeof(gPrefs) || gPrefs.version != appPrefVersion) {
		MemSet(&gPrefs, sizeof(gPrefs), 0);
		gPrefs.version = appPrefVersion;
		gPrefs.baud = rpBaudIdle;
		gPrefs.preferredBaud = rpBaudIdle;	/* don't ask WiRSa for more until set */
	}
	gWCtlBaud = gPrefs.baud;
	gWCtlMenuBaud = gPrefs.menuBaud;
}

void PrefsSave(void)
{
	gPrefs.baud = gWCtlBaud;
	gPrefs.menuBaud = gWCtlMenuBaud;
	PrefSetAppPreferences(appCreator, appPrefID, appPrefVersion, &gPrefs, sizeof(gPrefs), false);
}

/* Trying every rate sends junk to a WiRSa that sits in a mode, so only do
 * it when WiRSa's menu rate isn't known yet, or WiRSa should be in its
 * menu (the burst guard drops the junk there). */
Boolean SweepOK(void)
{
	return gWCtlMenuBaud == 0 || gPrefs.lastMode == 0 || gPrefs.lastMode == 'M';
}

/* ---- field helpers ------------------------------------------------------ */

FieldPtr GetField(UInt16 id)
{
	FormPtr frm = FrmGetActiveForm();
	return (FieldPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, id));
}

void SetFieldPtr(UInt16 id, char *text)
{
	FieldPtr fld = GetField(id);
	FldSetTextPtr(fld, text);
	FldRecalculateField(fld, true);
	if (FrmVisible(FrmGetActiveForm()))
		FldDrawField(fld);
}

void SetFieldEdit(UInt16 id, const char *text)
{
	FieldPtr fld = GetField(id);
	MemHandle old = FldGetTextHandle(fld);
	UInt16 len = (UInt16) StrLen(text);
	MemHandle h = MemHandleNew(len + 1);

	if (! h) return;
	StrCopy((char *) MemHandleLock(h), text);
	MemHandleUnlock(h);
	FldSetTextHandle(fld, h);
	if (old) MemHandleFree(old);
	FldRecalculateField(fld, true);
	if (FrmVisible(FrmGetActiveForm()))
		FldDrawField(fld);
}

void GetFieldEdit(UInt16 id, char *out, UInt16 cap)
{
	char *p = FldGetTextPtr(GetField(id));
	out[0] = '\0';
	if (p) {
		StrNCopy(out, p, cap - 1);
		out[cap - 1] = '\0';
	}
}

/* ---- masked password fields --------------------------------------------- */

void MaskRedraw(WCMasked *m)
{
	char disp[65];
	UInt16 len = (UInt16) StrLen(m->real), i;
	FieldPtr fld = GetField(m->fieldID);

	for (i = 0; i < len; i++)
		disp[i] = m->show ? m->real[i] : '*';
	disp[len] = '\0';
	SetFieldEdit(m->fieldID, disp);
	FldSetInsPtPosition(fld, len);
}

/* Append/backspace only, like Bitwarden's unlock dialog. */
Boolean MaskKey(WCMasked *m, EventPtr e)
{
	WChar c = e->data.keyDown.chr;
	UInt16 len = (UInt16) StrLen(m->real);

	if (c == chrBackspace) {
		if (len > 0) m->real[len - 1] = '\0';
		MaskRedraw(m);
		return true;
	}
	if (TxtCharIsPrint(c)) {
		if (len < sizeof(m->real) - 1) {
			m->real[len] = (char) c;
			m->real[len + 1] = '\0';
			MaskRedraw(m);
		}
		return true;
	}
	return false;
}

/* Edit menu for any form; a masked field only takes Paste (appended). */
Boolean EditMenu(UInt16 itemID, WCMasked *masks, UInt16 nMasks)
{
	FormPtr frm = FrmGetActiveForm();
	UInt16 focus = FrmGetFocus(frm);
	FieldPtr fld;
	UInt16 i;

	if (itemID == mniEditKeyboard) { SysKeyboardDialog(kbdDefault); return true; }
	if (itemID == mniEditGraffiti) { SysGraffitiReferenceDialog(referenceDefault); return true; }
	if (focus == noFocus) return true;
	if (FrmGetObjectType(frm, focus) != frmFieldObj) return true;
	fld = (FieldPtr) FrmGetObjectPtr(frm, focus);

	for (i = 0; i < nMasks; i++) {
		if (FrmGetObjectId(frm, focus) != masks[i].fieldID) continue;
		if (itemID == mniEditPaste) {
			UInt16 clipLen;
			MemHandle h = ClipboardGetItem(clipboardText, &clipLen);
			UInt16 len = (UInt16) StrLen(masks[i].real);
			if (h && clipLen) {
				char *p = (char *) MemHandleLock(h);
				if (clipLen > sizeof(masks[i].real) - 1 - len)
					clipLen = (UInt16) (sizeof(masks[i].real) - 1 - len);
				MemMove(masks[i].real + len, p, clipLen);
				masks[i].real[len + clipLen] = '\0';
				MemHandleUnlock(h);
				MaskRedraw(&masks[i]);
			}
		}
		return true;
	}

	switch (itemID) {
	case mniEditUndo:		FldUndo(fld); break;
	case mniEditCut:		FldCut(fld); break;
	case mniEditCopy:		FldCopy(fld); break;
	case mniEditPaste:		FldPaste(fld); break;
	case mniEditSelectAll:	FldSetSelection(fld, 0, FldGetTextLength(fld)); break;
	}
	return true;
}

/* ---- busy box ----------------------------------------------------------- */

static WinHandle gBusyBits = 0;
static RectangleType gBusySaved;

static void BusyDraw(const char *msg)
{
	RectangleType r;
	UInt16 len = (UInt16) StrLen(msg);
	FontID old;

	RctSetRectangle(&r, 12, 66, 136, 26);
	WinEraseRectangle(&r, 0);
	WinDrawRectangleFrame(dialogFrame, &r);
	old = FntSetFont(boldFont);
	WinDrawChars(msg, len, (Coord) (r.topLeft.x + (r.extent.x - FntCharsWidth(msg, len)) / 2),
		(Coord) (r.topLeft.y + (r.extent.y - FntLineHeight()) / 2));
	FntSetFont(old);
}

void BusyShow(const char *msg)
{
	UInt16 err;

	BusyHide();
	RctSetRectangle(&gBusySaved, 8, 62, 144, 34);
	gBusyBits = WinSaveBits(&gBusySaved, &err);
	BusyDraw(msg);
}

void BusyHide(void)
{
	if (gBusyBits) {
		WinRestoreBits(gBusyBits, gBusySaved.topLeft.x, gBusySaved.topLeft.y);
		gBusyBits = 0;
	}
}

/* ---- WiRSa helpers ------------------------------------------------------ */

void ShowResult(UInt8 r)
{
	const char *msg;

	switch (r) {
	case wctlOk:		return;
	case wctlPortBusy:	msg = "The serial port is in use (SASD card or a network connection?)."; break;
	case wctlErrReply:
		if (gWCtlErrMsg[0]) msg = gWCtlErrMsg;
		else if (gWCtlErrCode == wctlErrNoWifi) msg = "WiRSa is not connected to WiFi.";
		else if (gWCtlErrCode == wctlErrNoSd) msg = "WiRSa has no SD card.";
		else if (gWCtlErrCode == wctlErrNotBuilt) msg = "That mode is not built into this WiRSa.";
		else if (gWCtlErrCode == wctlErrBusy) msg = "WiRSa is busy in another mode.";
		else msg = "WiRSa refused the request.";
		break;
	case wctlBadReply:	msg = "Unexpected reply from WiRSa."; break;
	case wctlSecFailed:	msg = "Could not secure the link to WiRSa."; break;
	default:			msg = "WiRSa did not answer."; break;
	}
	FrmCustomAlert(altError, msg, NULL, NULL);
}

Int16 ModeIndex(UInt8 letter)
{
	const char *p = kModeLetters;
	Int16 i;
	for (i = 0; p[i]; i++)
		if ((UInt8) p[i] == letter) return i;
	return -1;
}

UInt8 StatusFetch(void)
{
	UInt8 rep[96];
	UInt16 n;
	RpR r;
	UInt8 res = WCtlRequest(wctlTypeStatus, NULL, 0, rep, sizeof(rep), &n, SysTicksPerSecond() * 3);

	gStatus.valid = false;
	if (res != wctlOk) return res;
	r.buf = rep; r.len = n; r.pos = 0; r.ok = true;
	gStatus.mode = RpR8(&r);
	gStatus.wifi = RpR8(&r);
	RpRStr(&r, gStatus.ssid, sizeof(gStatus.ssid));
	gStatus.rssi = (Int8) RpR8(&r);
	RpRStr(&r, gStatus.ip, sizeof(gStatus.ip));
	gStatus.sd = RpR8(&r);
	gStatus.ftp = RpR8(&r);
	gStatus.ftpClients = RpR8(&r);
	gStatus.uptime = RpR32(&r);
	if (! r.ok) return wctlBadReply;
	/* Battery follows uptime on firmware that has it; older firmware ends
	 * here, which reads as "Not found". */
	gStatus.battery = 0;
	gStatus.sdText[0] = '\0';
	gStatus.sasdDisk = 0;
	gStatus.diskKnown = false;
	if (r.pos + 4 <= r.len) {
		gStatus.battery = RpR8(&r);
		gStatus.battMv = RpR16(&r);
		gStatus.battPct = RpR8(&r);
		if (! r.ok) gStatus.battery = 0;
		/* SD used/total follows the battery on firmware that has it. */
		if (r.ok && r.pos + 2 <= r.len) {
			RpRStr(&r, gStatus.sdText, sizeof(gStatus.sdText));
			if (! r.ok) gStatus.sdText[0] = '\0';
		}
		/* The SASD disk setting follows the SD text. */
		if (r.ok && r.pos + 1 <= r.len) {
			gStatus.sasdDisk = RpR8(&r);
			gStatus.diskKnown = true;
		}
	}
	gStatus.valid = true;
	return wctlOk;
}

/* ---- frmMain ------------------------------------------------------------ */

static void ModeName(UInt8 letter, char *out, UInt16 cap)
{
	Int16 i = ModeIndex(letter);
	if (i < 0) StrCopy(out, "?");
	else SysStringByIndex(strModeNames, (UInt16) i, out, cap);
}

/* The SD row follows the SASD disk setting: "SD:" with the card, or "V. SD:" with the virtual card;
 * Auto shows "SD:" and says which one it is ("Card ..." / "Virt ..."). */
static Boolean gSdVirtual = false;			/* the row is the virtual card's: label "V. SD:" */

static void MainFillSd(void)
{
	UInt8 sd = gStatus.sd;
	const char *t = gStatus.sdText;
	const char *pre = "";

	gSdVirtual = false;
	if (! gStatus.diskKnown || gStatus.sasdDisk == 1) {		/* older firmware, or the SD card chosen */
		if (sd == 1) pre = "Present";
		else { StrCopy(gTxtSd, sd == 2 ? "Not present" : "Unknown"); return; }
	} else if (gStatus.sasdDisk == 2) {						/* virtual card chosen */
		gSdVirtual = true;
		if (sd != 3) { StrCopy(gTxtSd, "Not present"); return; }
	} else if (sd == 1) pre = "Card";
	else if (sd == 3) pre = "Virt";
	else { StrCopy(gTxtSd, sd == 2 ? "Not found" : "Unknown"); return; }

	if (! StrCompare(t, "blank")) t = "not formatted";
	/* a size after the prefix; the prefix is only the card/virtual marker in Auto */
	if ((gStatus.diskKnown && gStatus.sasdDisk == 0) || ! t[0]) {
		StrCopy(gTxtSd, pre);
		if (t[0]) { StrCat(gTxtSd, " "); StrNCat(gTxtSd, t, sizeof(gTxtSd)); }
	} else
		StrNCopy(gTxtSd, t, sizeof(gTxtSd) - 1);
	gTxtSd[sizeof(gTxtSd) - 1] = '\0';
}

static void MainSdLabel(void)
{
	FormPtr frm = FrmGetActiveForm();
	UInt16 vi = FrmGetObjectIndex(frm, lblMainVsd), si = FrmGetObjectIndex(frm, lblMainSd);

	FrmHideObject(frm, gSdVirtual ? si : vi);
	FrmShowObject(frm, gSdVirtual ? vi : si);
}

static void MainShowAll(void)
{
	MainSdLabel();
	SetFieldPtr(fldMainMode, gTxtMode);
	SetFieldPtr(fldMainLink, gTxtLink);
	SetFieldPtr(fldMainWifi, gTxtWifi);
	SetFieldPtr(fldMainIp, gTxtIp);
	SetFieldPtr(fldMainSd, gTxtSd);
	SetFieldPtr(fldMainFtp, gTxtFtp);
	SetFieldPtr(fldMainBatt, gTxtBatt);
	SetFieldPtr(fldMainNote, gTxtNote);
}

static void MainClear(void)
{
	StrCopy(gTxtLink, "-");
	StrCopy(gTxtWifi, "-");
	StrCopy(gTxtIp, "-");
	StrCopy(gTxtSd, "-");
	gSdVirtual = false;
	StrCopy(gTxtFtp, "-");
	StrCopy(gTxtBatt, "-");
	gTxtNote[0] = '\0';
}

/* WiRSa answered: fill everything from gWCtlInfo/gStatus. */
static void MainFillFound(void)
{
	char num[12];

	ModeName(gStatus.mode, gTxtMode, sizeof(gTxtMode));
	/* short rate (115.2k) so "(no screen)" still fits the 114 px field */
	if (gWCtlBaud >= 14400)
		StrPrintF(gTxtLink, "%lu.%luk", gWCtlBaud / 1000, (gWCtlBaud % 1000) / 100);
	else
		StrPrintF(gTxtLink, "%lu", gWCtlBaud);
	if (gWCtlInfo.fw[0]) {
		StrCat(gTxtLink, ", ");
		StrNCat(gTxtLink, gWCtlInfo.fw, sizeof(gTxtLink));
	}
	if (gWCtlInfo.flags & wctlFlagScreenless)
		StrNCat(gTxtLink, " (no screen)", sizeof(gTxtLink));

	switch (gStatus.wifi) {
	case 2:
		StrCopy(gTxtWifi, gStatus.ssid);
		StrPrintF(num, "  %d dBm", (Int16) gStatus.rssi);
		StrNCat(gTxtWifi, num, sizeof(gTxtWifi));
		break;
	case 1:	StrCopy(gTxtWifi, "Connecting..."); break;
	case 3:	StrCopy(gTxtWifi, "Not connected"); break;
	default: StrCopy(gTxtWifi, "Not set up"); break;
	}
	StrCopy(gTxtIp, gStatus.ip[0] ? gStatus.ip : "-");
	MainFillSd();
	if (gStatus.ftp) {
		StrPrintF(gTxtFtp, "Started (%u client%s)", (UInt16) gStatus.ftpClients,
			gStatus.ftpClients == 1 ? "" : "s");
	} else
		StrCopy(gTxtFtp, "Stopped");
	if (gStatus.battery) {
		/* "3.92V 78%", the same as the firmware's Information screen */
		StrPrintF(gTxtBatt, "%u.%02uV %u%%", (UInt16) (gStatus.battMv / 1000),
			(UInt16) ((gStatus.battMv % 1000) / 10), (UInt16) gStatus.battPct);
	} else
		StrCopy(gTxtBatt, "Not found");
	gTxtNote[0] = '\0';
}

/* Main screen auto refresh: every kAutoRefreshTicks while frmMain is the
 * active form (EventLoop wakes for it; MainHandleEvent's nilEvent runs it). */
#define kAutoRefreshTicks	(SysTicksPerSecond() * 15)
static UInt32 gNextAutoRefresh = 0;
/* Something (the menu bar, an alert) is over frmMain: its window lost the
 * focus (winExitEvent) and hasn't got it back (winEnterEvent). The auto
 * refresh must not draw then - it would paint the status over the menu. */
static Boolean gMainCovered = false;

/* "<mode> (last known)" in the mode field, or "(last)" if that doesn't fit. */
static void MainSetLastKnownMode(void)
{
	FontID old;
	UInt16 i;

	ModeName(gPrefs.lastMode, gTxtMode, 24);
	i = (UInt16) StrLen(gTxtMode);
	StrCat(gTxtMode, " (last known)");
	old = FntSetFont(stdFont);
	if (FntCharsWidth(gTxtMode, (Int16) StrLen(gTxtMode)) > 114)
		StrCopy(gTxtMode + i, " (last)");	/* too long for the field */
	FntSetFont(old);
}

/* WiRSa did not answer (or the port is busy): the last mode seen, marked
 * "(last known)", or "Not found" if there never was one. The guidance
 * says what probably happened. */
static void MainFillNotFound(UInt8 r)
{
	UInt16 note;

	MainClear();
	if (gPrefs.lastMode) {
		MainSetLastKnownMode();
		note = gPrefs.lastMode == 'D' ? 0 : gPrefs.lastMode == 'S' ? 1 : r == wctlPortBusy ? 2 : 6;
	} else {
		StrCopy(gTxtMode, "Not found");
		note = r == wctlPortBusy ? 2 : 3;
	}
	SysStringByIndex(strMainNotes, note, gTxtNote, sizeof(gTxtNote));
}

/* Port open, WiRSa answered a PING. If WiRSa's menu runs at 9600 (just
 * booted, or another Palm brought it there) and this Palm prefers more, ask
 * WiRSa to move the menu up (SPEED). A rate that failed or gained nothing
 * isn't asked for again until Preferences or Find WiRSa. */
static void SpeedUp(void)
{
	UInt32 got;

	if (gPrefs.preferredBaud <= rpBaudIdle || gPrefs.speedSkip == gPrefs.preferredBaud)
		return;
	if (! (gWCtlInfo.flags & wctlFlagSpeed) || gWCtlInfo.mode != 'M' || gWCtlInfo.menuBaud != rpBaudIdle)
		return;
	if (WCtlSetSpeed(gPrefs.preferredBaud, &got) == wctlOk && got != rpBaudIdle)
		gWCtlMenuBaud = got;
	else
		gPrefs.speedSkip = gPrefs.preferredBaud;
}

/* Short rate text (115.2k) for the busy box. */
static void BaudShort(UInt32 b, char *out)
{
	if (b >= 14400)
		StrPrintF(out, "%lu.%luk", b / 1000, (b % 1000) / 100);
	else
		StrPrintF(out, "%lu", b);
}

static const char *gProgressFmt;	/* "Refreshing @ %s..." or "Checking %s..." */

static void BusyProgress(UInt32 baud)
{
	char rate[12], msg[40];

	BaudShort(baud, rate);
	StrPrintF(msg, gProgressFmt, rate);
	BusyDraw(msg);
}

/* quiet: the 15 s auto refresh - no busy box, and never the full sweep. */
static void MainRefresh(Boolean sweep, Boolean quiet)
{
	UInt8 r;
	Boolean partial = false;

	if (! quiet) {
		gProgressFmt = sweep ? "Checking %s..." : "Refreshing @ %s...";
		BusyShow("Finding WiRSa...");
		gWCtlProgress = BusyProgress;
	}
	r = sweep ? WCtlBeginFind() : WCtlBegin(quiet ? false : SweepOK());
	gWCtlProgress = 0;
	if (r == wctlOk) {
		SpeedUp();
		r = StatusFetch();
		WCtlEnd();
	}
	if (! quiet) BusyHide();
	gNextAutoRefresh = TimGetTicks() + kAutoRefreshTicks;

	if (r == wctlErrReply) {
		/* PING worked but STATUS was refused (older firmware in a mode):
		 * show what PING told us. */
		MemSet(&gStatus, sizeof(gStatus), 0);
		gStatus.mode = gWCtlInfo.mode;	/* valid stays false: FTP etc. unknown */
		partial = true;
		r = wctlOk;
	}
	if (r == wctlOk) {
		gPrefs.lastMode = gStatus.mode;
		PrefsSave();
		MainFillFound();
		if (partial) {
			StrCopy(gTxtWifi, "-");
			StrCopy(gTxtIp, "-");
			StrCopy(gTxtSd, "-");
			gSdVirtual = false;
			StrCopy(gTxtFtp, "-");
			StrCopy(gTxtBatt, "-");
		}
	} else {
		MemSet(&gStatus, sizeof(gStatus), 0);
		MainFillNotFound(r);
		gStatus.mode = gPrefs.lastMode;		/* frmModes preselects it */
	}
	MainShowAll();
}

/* Right after a mode was started from frmModes: it is still starting up,
 * so don't ask WiRSa - show the mode as last known (the auto refresh asks
 * once it is up). */
static void MainShowLastKnown(void)
{
	UInt16 n = gPrefs.lastMode == 'D' ? 0 : gPrefs.lastMode == 'S' ? 1 : 5;

	MainClear();
	MainSetLastKnownMode();
	SysStringByIndex(strMainNotes, n, gTxtNote, sizeof(gTxtNote));
	MemSet(&gStatus, sizeof(gStatus), 0);
	gStatus.mode = gPrefs.lastMode;		/* frmModes preselects it */
	MainShowAll();
	gNextAutoRefresh = TimGetTicks() + kAutoRefreshTicks;
}

static void MainSimpleCommand(UInt8 type, const UInt8 *body, UInt16 len, const char *busy, const char *done)
{
	UInt8 rep[8];
	UInt16 n;
	UInt8 r;

	BusyShow(busy);
	r = WCtlBegin(SweepOK());
	if (r == wctlOk) {
		r = WCtlRequest(type, body, len, rep, sizeof(rep), &n, SysTicksPerSecond() * 3);
		WCtlEnd();
	}
	BusyHide();
	if (r != wctlOk) { ShowResult(r); return; }
	MemSet(&gStatus, sizeof(gStatus), 0);
	MainClear();
	StrCopy(gTxtMode, "Restarting");
	SysStringByIndex(strMainNotes, 4, gTxtNote, sizeof(gTxtNote));
	MainShowAll();
	FrmCustomAlert(altInfo, done, NULL, NULL);
}

/* ---- frmPrefs: this app's own preferences ------------------------------ */

/* Preferred Baud popup: list index <-> rate */
static UInt32 PrefsRateAt(Int16 i)
{
	switch (i) {
	case 1:  return 14400;
	case 2:  return 19200;
	case 3:  return 28800;
	case 4:  return 38400;
	case 5:  return 57600;
	case 6:  return 115200;
	default: return 9600;
	}
}

static char gPrefsLabel[8];

static void PrefsSetSel(Int16 sel)
{
	FormPtr frm = FrmGetActiveForm();
	ListPtr lst = (ListPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, lstPrefsBaud));

	LstSetSelection(lst, sel);
	StrNCopy(gPrefsLabel, LstGetSelectionText(lst, sel), sizeof(gPrefsLabel) - 1);
	gPrefsLabel[sizeof(gPrefsLabel) - 1] = '\0';
	CtlSetLabel((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, popPrefsBaud)), gPrefsLabel);
}

Boolean PrefsHandleEvent(EventPtr e)
{
	FormPtr frm;
	Int16 i, sel;
	UInt32 rate;

	switch (e->eType) {
	case frmOpenEvent:
		sel = 0;
		for (i = 0; i < 7; i++)
			if (PrefsRateAt(i) == gPrefs.preferredBaud)
				sel = i;
		PrefsSetSel(sel);
		FrmDrawForm(FrmGetActiveForm());
		return true;

	case popSelectEvent:
		if (e->data.popSelect.listID == lstPrefsBaud && e->data.popSelect.selection >= 0) {
			PrefsSetSel(e->data.popSelect.selection);
			return true;
		}
		break;

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnPrefsOk:
			frm = FrmGetActiveForm();
			rate = PrefsRateAt(LstGetSelection((ListPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, lstPrefsBaud))));
			if (rate != gPrefs.preferredBaud) {
				gPrefs.preferredBaud = rate;
				gPrefs.speedSkip = 0;
				PrefsSave();
			}
			FrmReturnToForm(frmMain);
			FrmUpdateForm(frmMain, updRefresh);	/* ask WiRSa now */
			return true;
		case btnPrefsCancel:
			FrmReturnToForm(frmMain);
			return true;
		}
		break;

	default:
		break;
	}
	return false;
}

static Boolean MainHandleEvent(EventPtr e)
{
	FormPtr frm;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		MainClear();
		StrCopy(gTxtMode, "...");
		MainShowAll();
		FrmDrawForm(frm);
		MainRefresh(false, false);
		return true;

	case nilEvent:
		/* auto refresh (EventLoop wakes for it), not while a menu is up */
		if (TimGetTicks() >= gNextAutoRefresh && FrmGetActiveFormID() == frmMain && ! gMainCovered)
			MainRefresh(false, true);
		return true;

	case winExitEvent:
		if (e->data.winExit.exitWindow == (WinHandle) FrmGetFormPtr(frmMain))
			gMainCovered = true;
		break;

	case winEnterEvent:
		if (e->data.winEnter.enterWindow == (WinHandle) FrmGetFormPtr(frmMain))
			gMainCovered = false;
		break;

	case frmUpdateEvent:
		if (e->data.frmUpdate.updateCode == updRefresh) {
			FrmDrawForm(FrmGetActiveForm());
			MainRefresh(false, false);
			return true;
		}
		if (e->data.frmUpdate.updateCode == updLastKnown) {
			FrmDrawForm(FrmGetActiveForm());
			MainShowLastKnown();
			return true;
		}
		break;

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnMainRefresh:	MainRefresh(false, false); return true;
		case btnMainWifi:		FrmPopupForm(frmWifi); return true;
		case btnMainModes:		FrmPopupForm(frmModes); return true;
		case btnMainSetup:		SetupOpenFromMain(); return true;
		}
		break;

	case menuEvent:
		switch (e->data.menu.itemID) {
		case mniMainFind:
			gPrefs.lastMode = 0;
			gPrefs.speedSkip = 0;		/* ask for the Preferred Baud again */
			MainRefresh(true, false);	/* every rate */
			return true;
		case mniMainReboot:
			MainSimpleCommand(wctlTypeReboot, NULL, 0, "Rebooting WiRSa...", "WiRSa is restarting.");
			return true;
		case mniMainFactory:
			if (FrmAlert(altFactory) == 0)
				MainSimpleCommand(wctlTypeFactory, (const UInt8 *) "RESET", 5,
					"Resetting WiRSa...", "WiRSa settings were erased; it is restarting.");
			return true;
		case mniMainPrefs:
			FrmPopupForm(frmPrefs);
			return true;
		case mniMainSystem:
			SysOpenFromMain();
			break;
		case mniMainLogs:
			LogOpenFromMain();
			return true;
		case mniMainAbout:
			FrmAlert(altAbout);
			return true;
		}
		break;

	case frmCloseEvent:
		FldSetTextPtr(GetField(fldMainMode), NULL);
		FldSetTextPtr(GetField(fldMainLink), NULL);
		FldSetTextPtr(GetField(fldMainWifi), NULL);
		FldSetTextPtr(GetField(fldMainIp), NULL);
		FldSetTextPtr(GetField(fldMainSd), NULL);
		FldSetTextPtr(GetField(fldMainFtp), NULL);
		FldSetTextPtr(GetField(fldMainBatt), NULL);
		FldSetTextPtr(GetField(fldMainNote), NULL);
		break;

	default:
		break;
	}
	return false;
}

/* ---- app ---------------------------------------------------------------- */

static Boolean AppHandleEvent(EventPtr e)
{
	if (e->eType == frmLoadEvent) {
		UInt16 id = e->data.frmLoad.formID;
		FormPtr frm = FrmInitForm(id);
		FrmSetActiveForm(frm);
		switch (id) {
		case frmMain:	FrmSetEventHandler(frm, MainHandleEvent); break;
		case frmModes:	FrmSetEventHandler(frm, ModesHandleEvent); break;
		case frmWifi:	FrmSetEventHandler(frm, WifiHandleEvent); break;
		case frmSetup:	FrmSetEventHandler(frm, SetupHandleEvent); break;
		case frmFtp:	FrmSetEventHandler(frm, FtpHandleEvent); break;
		case frmAp:		FrmSetEventHandler(frm, ApHandleEvent); break;
		case frmSlip:	FrmSetEventHandler(frm, SlipHandleEvent); break;
		case frmTimers:	FrmSetEventHandler(frm, TimersHandleEvent); break;
		case frmPrefs:	FrmSetEventHandler(frm, PrefsHandleEvent); break;
		case frmLog:	FrmSetEventHandler(frm, LogHandleEvent); break;
		}
		return true;
	}
	return false;
}

static void EventLoop(void)
{
	EventType e;
	UInt16 err;

	do {
		Int32 wait = evtWaitForever;
		if (FrmGetActiveFormID() == frmMain && ! gMainCovered) {
			wait = (Int32) (gNextAutoRefresh - TimGetTicks());
			if (wait < 1) wait = 1;
		}
		EvtGetEvent(&e, wait);
		if (SysHandleEvent(&e)) continue;
		if (MenuHandleEvent(0, &e, &err)) continue;
		if (AppHandleEvent(&e)) continue;
		FrmDispatchEvent(&e);
	} while (e.eType != appStopEvent);
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
	if (cmd == sysAppLaunchCmdNormalLaunch) {
		PrefsLoad();
		FrmGotoForm(frmMain);
		EventLoop();
		FrmCloseAllForms();
		PrefsSave();
	}
	return 0;
}
