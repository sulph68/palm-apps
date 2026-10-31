/*
 * Log.c - frmLog: WiRSa's session log (Help > Show Logs). The log is kept in WiRSa's
 * RTC memory (about 6 KB, nothing on flash); it is fetched a page at a time, newest at
 * the bottom, into one block that the field shows.
 */
#include <PalmOS.h>
#include "WiRSaControl.h"
#include "relayproto.h"
#include "relaywire.h"

#define kLogMax		7000

static MemHandle gLogH;			/* text shown in fldLogText */
static UInt8 gLogRep[520];
static Boolean gShowSys;		/* frmLog is showing the System page, not the log */
static char gTitleSys[] = "WiRSa System";
static char gTitleLog[] = "WiRSa Log";

static void LogFree(void)
{
	if (gLogH) {
		MemHandleFree(gLogH);
		gLogH = 0;
	}
}

/* Fetch the log into a new block (op 1 clears it first). */
static Boolean LogFetch(UInt8 op)
{
	UInt8 req[5];
	UInt16 n, got, len = 0;
	UInt32 off = 0, total = 0, repOff;
	RpW w;
	RpR r;
	UInt8 res;
	MemHandle h = MemHandleNew(kLogMax + 1);
	char *buf;

	if (! h) {
		FrmCustomAlert(altError, "Not enough memory to read the log.", NULL, NULL);
		return false;
	}
	buf = (char *) MemHandleLock(h);
	BusyShow(gShowSys ? "Reading system..." : "Reading log...");
	res = WCtlBegin(SweepOK());
	while (res == wctlOk) {
		w.buf = req; w.cap = sizeof(req); w.len = 0;
		RpW8(&w, off == 0 ? op : 0);
		RpW32(&w, off);
		res = WCtlRequest(gShowSys ? wctlTypeSys : wctlTypeLog, req, w.len, gLogRep, sizeof(gLogRep), &n, SysTicksPerSecond() * 3);
		if (res != wctlOk)
			break;
		r.buf = gLogRep; r.len = n; r.pos = 0; r.ok = true;
		total = RpR32(&r);
		repOff = RpR32(&r);
		got = RpR16(&r);
		if (! r.ok || repOff != off || got > r.len - r.pos) { res = wctlBadReply; break; }
		if (got > kLogMax - len)
			got = kLogMax - len;
		RpRBytes(&r, (UInt8 *) buf + len, got);
		len += got;
		off += got;
		if (got == 0 || off >= total || len >= kLogMax)
			break;
	}
	WCtlEnd();
	BusyHide();
	buf[len] = '\0';
	MemHandleUnlock(h);
	if (res != wctlOk) {
		MemHandleFree(h);
		ShowResult(res);
		return false;
	}
	if (len == 0) {
		buf = (char *) MemHandleLock(h);
		StrCopy(buf, gShowSys ? "(no system data)" : "(the log is empty)");
		MemHandleUnlock(h);
	}
	LogFree();
	gLogH = h;
	return true;
}

Boolean SysOpenFromMain(void)
{
	gShowSys = true;
	if (! LogFetch(0))
		return false;
	FrmPopupForm(frmLog);
	return true;
}

Boolean LogOpenFromMain(void)
{
	gShowSys = false;
	if (! LogFetch(0))
		return false;
	FrmPopupForm(frmLog);
	return true;
}

/* Scrollbar in step with the field. */
static void LogScrollUpdate(void)
{
	FormPtr frm = FrmGetActiveForm();
	FieldPtr fld = (FieldPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, fldLogText));
	ScrollBarPtr sb = (ScrollBarPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, sclLog));
	UInt16 pos, textH, fieldH, maxV;

	FldGetScrollValues(fld, &pos, &textH, &fieldH);
	maxV = (textH > fieldH) ? (UInt16) (textH - fieldH) : 0;
	SclSetScrollBar(sb, pos, 0, maxV, (UInt16) (fieldH > 1 ? fieldH - 1 : 1));
}

static void LogShow(void)
{
	FormPtr frm = FrmGetActiveForm();
	FieldPtr fld = (FieldPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, fldLogText));

	FldSetTextHandle(fld, gLogH);
	FldDrawField(fld);
	if (! gShowSys)
		FldScrollField(fld, 1000, winDown);		/* newest at the bottom */
	LogScrollUpdate();
}

static void LogReload(UInt8 op)
{
	FormPtr frm = FrmGetActiveForm();
	FieldPtr fld = (FieldPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, fldLogText));
	MemHandle old = gLogH;

	FldSetTextHandle(fld, NULL);		/* the field lets go of the old block before it is freed */
	gLogH = 0;
	if (! LogFetch(op))
		gLogH = old;					/* keep showing what we had */
	else if (old)
		MemHandleFree(old);
	LogShow();
}

Boolean LogHandleEvent(EventPtr e)
{
	FormPtr frm;
	FieldPtr fld;
	Int16 lines;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		FrmSetTitle(frm, gShowSys ? gTitleSys : gTitleLog);
		if (gShowSys)
			FrmHideObject(frm, FrmGetObjectIndex(frm, btnLogClear));
		FrmDrawForm(frm);
		LogShow();
		return true;

	case sclRepeatEvent:
		frm = FrmGetActiveForm();
		fld = (FieldPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, fldLogText));
		lines = (Int16) (e->data.sclRepeat.newValue - e->data.sclRepeat.value);
		if (lines > 0)
			FldScrollField(fld, (UInt16) lines, winDown);
		else if (lines < 0)
			FldScrollField(fld, (UInt16) (-lines), winUp);
		LogScrollUpdate();
		return false;

	case keyDownEvent:
		frm = FrmGetActiveForm();
		fld = (FieldPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, fldLogText));
		if (e->data.keyDown.chr == pageUpChr) {
			FldScrollField(fld, 8, winUp);
			LogScrollUpdate();
			return true;
		}
		if (e->data.keyDown.chr == pageDownChr) {
			FldScrollField(fld, 8, winDown);
			LogScrollUpdate();
			return true;
		}
		break;

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnLogDone:
			FrmReturnToForm(frmMain);
			return true;
		case btnLogRefresh:
			LogReload(0);
			return true;
		case btnLogClear:
			LogReload(1);
			return true;
		}
		break;

	case frmCloseEvent:
		frm = FrmGetActiveForm();
		fld = (FieldPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, fldLogText));
		FldSetTextHandle(fld, NULL);
		LogFree();
		break;

	default:
		break;
	}
	return false;
}
