/*
 * Setup.c - frmSetup (WiRSa settings) and its sub-forms: frmFtp, frmAp (user/SSID and password),
 * frmSlip (SLIP Gateway addresses) and frmTimers (Exit Timers).
 * Settings are read once (CFG_GET) before the form opens and written back
 * in one CFG_SET on OK; non-blank passwords go separately over the
 * encrypted link (SECRETS_SET), before CFG_SET, since a new baud rate takes
 * effect when CFG_SET is answered.
 */
#include <PalmOS.h>
#include "WiRSaControl.h"
#include "relayproto.h"
#include "relaywire.h"

/* Max baud: the popup lists 9600 14400 19200 28800 38400 57600 115200 (index 0..6). On the wire WiRSa still
 * numbers the rates as it did when the table began at 300, so 9600 is 4, 19200 5, 38400 6, 57600 7, 115200 8,
 * and the later 14400 is 9 and 28800 is 10; an older WiRSa's 0..3 (300..4800) show as 9600. Switches, not
 * tables: a constant table read from a secondary segment comes back as garbage on this toolchain. */
#define kBaudCount		7

static UInt32 BaudOf(UInt16 idx)
{
	switch (idx) {
	case 1:  return 14400;
	case 2:  return 19200;
	case 3:  return 28800;
	case 4:  return 38400;
	case 5:  return 57600;
	case 6:  return 115200;
	default: return 9600;
	}
}

static UInt8 BaudFromWire(UInt8 wire)
{
	switch (wire) {
	case 9:  return 1;
	case 5:  return 2;
	case 10: return 3;
	case 6:  return 4;
	case 7:  return 5;
	case 8:  return 6;
	default: return 0;
	}
}

static UInt8 BaudToWire(UInt8 idx)
{
	switch (idx) {
	case 1:  return 9;
	case 2:  return 5;
	case 3:  return 10;
	case 4:  return 6;
	case 5:  return 7;
	case 6:  return 8;
	default: return 4;
	}
}

typedef struct {
	UInt8	baudIdx;
	UInt8	usbDebug;
	UInt8	orientation;
	char	ftpUser[33];
	char	apSsid[33];
	UInt8	ip[4][4];		/* gateway, client, subnet, DNS */
	UInt8	passSet;		/* bit 0 FTP password set, bit 1 AP password set */
	Boolean	timersKnown;	/* WiRSa sent Exit Timers (newer firmware) */
	UInt8	unusedExitSec;	/* Exit Timers, seconds, 0 = never */
	UInt8	afterExitSec;
	Boolean	slipKnown;		/* WiRSa sent the SLIP timeout */
	UInt8	slipMin;		/* SLIP timeout, minutes: 1, 2, 3 or 5 */
	Boolean	sasdKnown;		/* WiRSa sent the SASD disk choice */
	UInt8	sasdDisk;		/* 0 auto, 1 SD card, 2 virtual card */
	Boolean	sleepKnown;		/* WiRSa sent the deep sleep delay */
	UInt8	sleepMin;		/* minutes: 0 off, 1, 3, 5, 15 */
} WCConfig;

static WCConfig gCfg;
static WCMasked gMasks[2];			/* FTP pass, AP pass */
static char gBaudLabel[8], gOrientLabel[10], gLogLabel[10], gDiskLabel[10], gSleepLabel[8];

/* Log level: list order Off Warn Info Verbose; on the wire 0 off, 1 info (an older firmware's
 * USB debug on), 2 warn, 3 verbose. */
static UInt8 LogIdxToWire(Int16 idx)
{
	return (idx == 1) ? 2 : (idx == 2) ? 1 : (idx == 3) ? 3 : 0;
}

static Int16 LogWireToIdx(UInt8 w)
{
	return (w == 1) ? 2 : (w == 2) ? 1 : (w == 3) ? 3 : 0;
}

/* Deep sleep: list order Off 1 3 5 15 minutes; on the wire the minutes. */
static UInt8 SleepIdxToWire(Int16 idx)
{
	static const UInt8 mins[] = { 0, 1, 3, 5, 15 };
	return (idx >= 0 && idx < 5) ? mins[idx] : 0;
}

static Int16 SleepWireToIdx(UInt8 w)
{
	return (w == 1) ? 1 : (w == 3) ? 2 : (w == 5) ? 3 : (w == 15) ? 4 : 0;
}

/* ---- frmSetup ----------------------------------------------------------- */

Boolean SetupOpenFromMain(void)
{
	UInt8 rep[128];
	UInt16 n, i;
	RpR r;
	UInt8 res;

	BusyShow("Reading settings...");
	res = WCtlBegin(SweepOK());
	if (res == wctlOk) {
		res = WCtlRequest(wctlTypeCfgGet, NULL, 0, rep, sizeof(rep), &n, SysTicksPerSecond() * 3);
		WCtlEnd();
	}
	BusyHide();
	if (res != wctlOk) { ShowResult(res); return false; }

	r.buf = rep; r.len = n; r.pos = 0; r.ok = true;
	gCfg.baudIdx = RpR8(&r);
	gCfg.baudIdx = BaudFromWire(gCfg.baudIdx);
	gCfg.usbDebug = RpR8(&r);
	gCfg.orientation = RpR8(&r);
	RpRStr(&r, gCfg.ftpUser, sizeof(gCfg.ftpUser));
	RpRStr(&r, gCfg.apSsid, sizeof(gCfg.apSsid));
	for (i = 0; i < 4; i++)
		RpRBytes(&r, gCfg.ip[i], 4);
	gCfg.passSet = RpR8(&r);
	if (! r.ok) { ShowResult(wctlBadReply); return false; }
	/* Exit Timers: an optional tail - older firmware doesn't send it */
	gCfg.timersKnown = (Boolean) (r.pos + 2 <= r.len);
	if (gCfg.timersKnown) {
		gCfg.unusedExitSec = RpR8(&r);
		gCfg.afterExitSec = RpR8(&r);
	}
	gCfg.slipKnown = (Boolean) (gCfg.timersKnown && r.pos + 1 <= r.len);
	if (gCfg.slipKnown)
		gCfg.slipMin = RpR8(&r);
	gCfg.sasdKnown = (Boolean) (gCfg.slipKnown && r.pos + 1 <= r.len);
	if (gCfg.sasdKnown)
		gCfg.sasdDisk = RpR8(&r);
	gCfg.sleepKnown = (Boolean) (gCfg.sasdKnown && r.pos + 1 <= r.len);
	if (gCfg.sleepKnown)
		gCfg.sleepMin = RpR8(&r);

	FrmPopupForm(frmSetup);
	return true;
}

static void SetPopup(UInt16 popID, UInt16 lstID, Int16 sel, char *label, UInt16 cap)
{
	FormPtr frm = FrmGetActiveForm();
	ListPtr lst = (ListPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, lstID));

	LstSetSelection(lst, sel);
	StrNCopy(label, LstGetSelectionText(lst, sel), cap - 1);
	label[cap - 1] = '\0';
	CtlSetLabel((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, popID)), label);
}

static Int16 GetListSel(UInt16 lstID)
{
	FormPtr frm = FrmGetActiveForm();
	return LstGetSelection((ListPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, lstID)));
}

static void SetupSave(void)
{
	UInt8 body[128], rep[8];
	UInt16 n, i;
	RpW w;
	UInt8 r;
	UInt8 newBaudIdx = (UInt8) GetListSel(lstSetupBaud);
	Boolean secrets = (gMasks[0].real[0] || gMasks[1].real[0]);

	gCfg.usbDebug = LogIdxToWire(GetListSel(lstSetupLog));
	gCfg.orientation = (UInt8) GetListSel(lstSetupOrient);
	if (gCfg.sasdKnown)
		gCfg.sasdDisk = (UInt8) GetListSel(lstSetupDisk);
	if (gCfg.sleepKnown)
		gCfg.sleepMin = SleepIdxToWire(GetListSel(lstSetupSleep));

	BusyShow("Saving settings...");
	r = WCtlBegin(SweepOK());
	if (r == wctlOk && secrets) {
		w.buf = body; w.cap = sizeof(body); w.len = 0;
		RpWStr(&w, gMasks[0].real);
		RpWStr(&w, gMasks[1].real);
		r = WCtlSecure();
		if (r == wctlOk)
			r = WCtlRequest(wctlTypeSecrets, body, w.len, rep, sizeof(rep), &n, SysTicksPerSecond() * 5);
		WCtlUnsecure();
		MemSet(body, sizeof(body), 0);
	}
	if (r == wctlOk) {
		w.buf = body; w.cap = sizeof(body); w.len = 0;
		RpW8(&w, BaudToWire(newBaudIdx));
		RpW8(&w, gCfg.usbDebug);
		RpW8(&w, gCfg.orientation);
		RpWStr(&w, gCfg.ftpUser);
		RpWStr(&w, gCfg.apSsid);
		for (i = 0; i < 4; i++)
			RpWBytes(&w, gCfg.ip[i], 4);
		if (gCfg.timersKnown) {
			RpW8(&w, gCfg.unusedExitSec);
			RpW8(&w, gCfg.afterExitSec);
			if (gCfg.slipKnown) {
				RpW8(&w, gCfg.slipMin);
				if (gCfg.sasdKnown) {
					RpW8(&w, gCfg.sasdDisk);
					if (gCfg.sleepKnown)
						RpW8(&w, gCfg.sleepMin);
				}
			}
		}
		r = WCtlRequest(wctlTypeCfgSet, body, w.len, rep, sizeof(rep), &n, SysTicksPerSecond() * 5);
		/* Older firmware: WiRSa's menu now runs at the new rate (only if it is
		 * in its menu). Firmware with SPEED: the menu keeps its rate - the
		 * maximum only caps the modes - so nothing to follow; just allow the
		 * Preferred Baud to be asked for again. */
		if (r == wctlOk && newBaudIdx != gCfg.baudIdx && gWCtlInfo.mode == 'M') {
			if (gWCtlInfo.flags & wctlFlagSpeed)
				gPrefs.speedSkip = 0;
			else
				gWCtlBaud = BaudOf(newBaudIdx);
		}
	}
	WCtlEnd();
	BusyHide();
	if (r != wctlOk) { ShowResult(r); return; }

	PrefsSave();
	MemSet(gMasks, sizeof(gMasks), 0);
	FrmReturnToForm(frmMain);
	FrmUpdateForm(frmMain, updRefresh);
}

Boolean SetupHandleEvent(EventPtr e)
{
	FormPtr frm;
	UInt16 i;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		MemSet(gMasks, sizeof(gMasks), 0);
		SetPopup(popSetupBaud, lstSetupBaud, gCfg.baudIdx < kBaudCount ? gCfg.baudIdx : 0, gBaudLabel, sizeof(gBaudLabel));
		SetPopup(popSetupOrient, lstSetupOrient, gCfg.orientation ? 1 : 0, gOrientLabel, sizeof(gOrientLabel));
		SetPopup(popSetupLog, lstSetupLog, LogWireToIdx(gCfg.usbDebug), gLogLabel, sizeof(gLogLabel));
		if (gCfg.sasdKnown)
			SetPopup(popSetupDisk, lstSetupDisk, gCfg.sasdDisk < 3 ? gCfg.sasdDisk : 0, gDiskLabel, sizeof(gDiskLabel));
		else {
			FrmHideObject(frm, FrmGetObjectIndex(frm, lblSetupDisk));
			FrmHideObject(frm, FrmGetObjectIndex(frm, popSetupDisk));
		}
		if (gCfg.sleepKnown)
			SetPopup(popSetupSleep, lstSetupSleep, SleepWireToIdx(gCfg.sleepMin), gSleepLabel, sizeof(gSleepLabel));
		else {
			FrmHideObject(frm, FrmGetObjectIndex(frm, lblSetupSleep));
			FrmHideObject(frm, FrmGetObjectIndex(frm, popSetupSleep));
		}
		if (gWCtlInfo.flags & wctlFlagScreenless) {
			/* No Orientation row: the rows below it move up into its place. */
			static const UInt16 moved[] = { lblSetupDisk, popSetupDisk, lstSetupDisk,
				lblSetupSleep, popSetupSleep, lstSetupSleep, btnSetupFtp, btnSetupAp, btnSetupSlip, btnSetupTimers };
			Coord x, y;
			FrmHideObject(frm, FrmGetObjectIndex(frm, lblSetupOrient));
			FrmHideObject(frm, FrmGetObjectIndex(frm, popSetupOrient));
			for (i = 0; i < sizeof(moved) / sizeof(moved[0]); i++) {
				UInt16 ix = FrmGetObjectIndex(frm, moved[i]);
				FrmGetObjectPosition(frm, ix, &x, &y);
				FrmSetObjectPosition(frm, ix, x, (Coord) (y - 13));
			}
		}
		FrmDrawForm(frm);
		return true;

	case popSelectEvent:
		if (e->data.popSelect.listID == lstSetupBaud && e->data.popSelect.selection >= 0) {
			SetPopup(popSetupBaud, lstSetupBaud, e->data.popSelect.selection, gBaudLabel, sizeof(gBaudLabel));
			return true;
		}
		if (e->data.popSelect.listID == lstSetupLog && e->data.popSelect.selection >= 0) {
			SetPopup(popSetupLog, lstSetupLog, e->data.popSelect.selection, gLogLabel, sizeof(gLogLabel));
			return true;
		}
		if (e->data.popSelect.listID == lstSetupDisk && e->data.popSelect.selection >= 0) {
			SetPopup(popSetupDisk, lstSetupDisk, e->data.popSelect.selection, gDiskLabel, sizeof(gDiskLabel));
			return true;
		}
		if (e->data.popSelect.listID == lstSetupSleep && e->data.popSelect.selection >= 0) {
			SetPopup(popSetupSleep, lstSetupSleep, e->data.popSelect.selection, gSleepLabel, sizeof(gSleepLabel));
			return true;
		}
		if (e->data.popSelect.listID == lstSetupOrient && e->data.popSelect.selection >= 0) {
			SetPopup(popSetupOrient, lstSetupOrient, e->data.popSelect.selection, gOrientLabel, sizeof(gOrientLabel));
			return true;
		}
		break;

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnSetupFtp:		FrmPopupForm(frmFtp); return true;
		case btnSetupAp:		FrmPopupForm(frmAp); return true;
		case btnSetupSlip:		FrmPopupForm(frmSlip); return true;
		case btnSetupTimers:
			if (gCfg.timersKnown)
				FrmPopupForm(frmTimers);
			else
				FrmCustomAlert(altError, "This WiRSa's firmware has no Exit Timers yet - update it.", NULL, NULL);
			return true;
		case btnSetupOk:		SetupSave(); return true;
		case btnSetupCancel:
			MemSet(gMasks, sizeof(gMasks), 0);
			FrmReturnToForm(frmMain);
			return true;
		}
		break;

	default:
		break;
	}
	return false;
}

/* ---- frmFtp / frmAp: one handler, the index says which. gMasks[] keeps the typed password
 * (blank = keep the current one) until Setup's OK sends it; Cancel puts back what was there. */

static char gMaskBackup[65];

static Boolean CredHandleEvent(EventPtr e, UInt16 which)
{
	FormPtr frm;
	char *text = which ? gCfg.apSsid : gCfg.ftpUser;
	UInt16 textLen = which ? sizeof(gCfg.apSsid) : sizeof(gCfg.ftpUser);
	UInt16 userFld = which ? fldApSsid : fldFtpUser, passFld = which ? fldApPass : fldFtpPass;
	UInt16 okBtn = which ? btnApOk : btnFtpOk, cancelBtn = which ? btnApCancel : btnFtpCancel;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		gMasks[which].fieldID = passFld;
		StrCopy(gMaskBackup, gMasks[which].real);
		SetFieldEdit(userFld, text);
		if (gMasks[which].real[0])
			MaskRedraw(&gMasks[which]);
		FrmDrawForm(frm);
		FrmSetFocus(frm, FrmGetObjectIndex(frm, userFld));
		return true;

	case keyDownEvent:
		frm = FrmGetActiveForm();
		if (FrmGetFocus(frm) == FrmGetObjectIndex(frm, passFld) && MaskKey(&gMasks[which], e))
			return true;
		break;

	case menuEvent:
		return EditMenu(e->data.menu.itemID, &gMasks[which], 1);

	case ctlSelectEvent:
		if (e->data.ctlSelect.controlID == okBtn) {
			GetFieldEdit(userFld, text, textLen);
			FrmReturnToForm(frmSetup);
			return true;
		}
		if (e->data.ctlSelect.controlID == cancelBtn) {
			StrCopy(gMasks[which].real, gMaskBackup);
			FrmReturnToForm(frmSetup);
			return true;
		}
		break;

	default:
		break;
	}
	return false;
}

Boolean FtpHandleEvent(EventPtr e) { return CredHandleEvent(e, 0); }
Boolean ApHandleEvent(EventPtr e) { return CredHandleEvent(e, 1); }

/* ---- frmSlip ------------------------------------------------------------ */

static const UInt16 kSlipFields[4] = { fldSlipGw, fldSlipClient, fldSlipMask, fldSlipDns };

/* "a.b.c.d" -> 4 bytes; false if not a valid dotted quad. */
static Boolean ParseIp(const char *s, UInt8 *out)
{
	UInt16 part = 0, val = 0, digits = 0;

	for (;; s++) {
		if (*s >= '0' && *s <= '9') {
			val = (UInt16) (val * 10 + (*s - '0'));
			if (++digits > 3 || val > 255) return false;
		} else if (*s == '.' || *s == '\0') {
			if (digits == 0 || part > 3) return false;
			out[part++] = (UInt8) val;
			val = 0; digits = 0;
			if (*s == '\0') break;
		} else
			return false;
	}
	return part == 4;
}

Boolean SlipHandleEvent(EventPtr e)
{
	FormPtr frm;
	char buf[16];
	UInt8 ip[4][4];
	UInt16 i;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		for (i = 0; i < 4; i++) {
			StrPrintF(buf, "%u.%u.%u.%u", (UInt16) gCfg.ip[i][0], (UInt16) gCfg.ip[i][1],
				(UInt16) gCfg.ip[i][2], (UInt16) gCfg.ip[i][3]);
			SetFieldEdit(kSlipFields[i], buf);
		}
		FrmDrawForm(frm);
		FrmSetFocus(frm, FrmGetObjectIndex(frm, fldSlipGw));
		return true;

	case menuEvent:
		return EditMenu(e->data.menu.itemID, NULL, 0);

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnSlipOk:
			for (i = 0; i < 4; i++) {
				GetFieldEdit(kSlipFields[i], buf, sizeof(buf));
				if (! ParseIp(buf, ip[i])) {
					FrmCustomAlert(altError, "Enter each address as four numbers 0-255, e.g. 192.168.7.1.", NULL, NULL);
					return true;
				}
			}
			MemMove(gCfg.ip, ip, sizeof(ip));
			FrmReturnToForm(frmSetup);
			return true;
		case btnSlipCancel:
			FrmReturnToForm(frmSetup);
			return true;
		}
		break;

	default:
		break;
	}
	return false;
}

/* ---- frmTimers ---------------------------------------------------------- */

static void TimersSet(UInt16 fld, UInt8 v)
{
	char buf[6];
	StrIToA(buf, (Int32) v);
	SetFieldEdit(fld, buf);
}

/* SLIP timeout popup: list index <-> minutes (1, 2, 3, 5) */
static char gSlipLabel[8];

static void TimersSetSlip(Int16 sel)
{
	FormPtr frm = FrmGetActiveForm();
	ListPtr lst = (ListPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, lstTimersSlip));

	LstSetSelection(lst, sel);
	StrNCopy(gSlipLabel, LstGetSelectionText(lst, sel), sizeof(gSlipLabel) - 1);
	gSlipLabel[sizeof(gSlipLabel) - 1] = '\0';
	CtlSetLabel((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, popTimersSlip)), gSlipLabel);
}

/* 0-255, or -1 if not a number */
static Int16 TimersGet(UInt16 fld)
{
	char buf[6];
	UInt16 i;
	Int32 v;

	GetFieldEdit(fld, buf, sizeof(buf));
	if (! buf[0]) return -1;
	for (i = 0; buf[i]; i++)
		if (buf[i] < '0' || buf[i] > '9') return -1;
	v = StrAToI(buf);
	return (Int16) (v > 255 ? 255 : v);
}

Boolean TimersHandleEvent(EventPtr e)
{
	FormPtr frm;
	Int16 u, a;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		TimersSet(fldTimersUnused, gCfg.unusedExitSec);
		TimersSet(fldTimersAfter, gCfg.afterExitSec);
		if (gCfg.slipKnown) {
			TimersSetSlip(gCfg.slipMin == 1 ? 0 : gCfg.slipMin == 2 ? 1 : gCfg.slipMin == 3 ? 2 : 3);
		} else {
			FrmHideObject(frm, FrmGetObjectIndex(frm, lblTimersSlip));
			FrmHideObject(frm, FrmGetObjectIndex(frm, popTimersSlip));
		}
		FrmDrawForm(frm);
		FrmSetFocus(frm, FrmGetObjectIndex(frm, fldTimersUnused));
		return true;

	case menuEvent:
		return EditMenu(e->data.menu.itemID, NULL, 0);

	case popSelectEvent:
		if (e->data.popSelect.listID == lstTimersSlip && e->data.popSelect.selection >= 0) {
			TimersSetSlip(e->data.popSelect.selection);
			return true;
		}
		break;

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnTimersOk:
			u = TimersGet(fldTimersUnused);
			a = TimersGet(fldTimersAfter);
			if (u < 0 || a < 0) {
				FrmCustomAlert(altError, "Enter seconds as a number, 0-255 (0 = never).", NULL, NULL);
				return true;
			}
			gCfg.unusedExitSec = (UInt8) u;
			gCfg.afterExitSec = (UInt8) a;
			if (gCfg.slipKnown) {
				frm = FrmGetActiveForm();
				switch (LstGetSelection((ListPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, lstTimersSlip)))) {
				case 0:  gCfg.slipMin = 1; break;
				case 1:  gCfg.slipMin = 2; break;
				case 2:  gCfg.slipMin = 3; break;
				default: gCfg.slipMin = 5; break;
				}
			}
			FrmReturnToForm(frmSetup);
			return true;
		case btnTimersCancel:
			FrmReturnToForm(frmSetup);
			return true;
		}
		break;

	default:
		break;
	}
	return false;
}
