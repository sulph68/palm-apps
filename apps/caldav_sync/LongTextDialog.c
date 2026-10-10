/*
 * LongTextDialog.c - shared full-screen multi-line text editor, mirrors
 * Address35's EditNoteDialog/frmNoteDlg exactly (read as reference,
 * original code): same hand-rolled modal loop shape, same scrollbar-sync
 * helper, same SysHandleEvent+MenuHandleEvent-before-FrmDispatchEvent
 * pattern for Cut/Copy/Paste/Select All (mnuDlgEdit, system item IDs
 * 10000-10007) to work in the field. Address35's version stores note text
 * via a MemHandle (open-ended length); this one works directly against a
 * caller-owned fixed char buffer instead, since every field in this app
 * already has its own fixed MAXCHARS ceiling - one MemHandle<->buf copy
 * at each end instead of Address35's own handle-swap-on-commit.
 */
#include "CalDAVSync.h"
#include "LongTextDialog.h"

static void LtdSyncScroll(FormPtr frm, FieldPtr fld) SEC001;
static void LtdSyncScroll(FormPtr frm, FieldPtr fld)
{
	UInt16 scrollPos, textHeight, fieldHeight;
	Int16 maxValue;
	ScrollBarPtr scl = (ScrollBarPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, sclLongText));

	FldGetScrollValues(fld, &scrollPos, &textHeight, &fieldHeight);
	if (textHeight > fieldHeight) maxValue = (Int16) (textHeight - fieldHeight);
	else if (scrollPos) maxValue = (Int16) scrollPos;
	else maxValue = 0;
	SclSetScrollBar(scl, (Int16) scrollPos, 0, maxValue, (Int16) fieldHeight);
}

static Boolean LtdSysMenu(EventPtr ev) SEC001;
static Boolean LtdSysMenu(EventPtr ev)
{
	UInt16 error;
	if (SysHandleEvent(ev)) return true;
	if (MenuHandleEvent(0, ev, &error)) return true;
	return false;
}

SEC001 Boolean LongTextEditDialog(char *buf, UInt16 bufCap)
{
	FormPtr prev = FrmGetActiveForm();
	FormPtr frm = FrmInitForm(frmLongText);
	FieldPtr fld;
	Boolean ok = false, done = false;
	MemHandle th = MemHandleNew((UInt32) bufCap);

	if (!th) { FrmDeleteForm(frm); return false; }

	{
		char *p = (char *) MemHandleLock(th);
		StrNCopy(p, buf, bufCap - 1);
		p[bufCap - 1] = '\0';
		MemHandleUnlock(th);
	}

	FrmSetActiveForm(frm);
	fld = (FieldPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, fldLongText));
	FldSetTextHandle(fld, th);
	FrmDrawForm(frm);
	FrmSetFocus(frm, FrmGetObjectIndex(frm, fldLongText));
	LtdSyncScroll(frm, fld);

	while (!done) {
		EventType ev;
		EvtGetEvent(&ev, evtWaitForever);

		if (ev.eType == ctlSelectEvent) {
			UInt16 cid = ev.data.ctlSelect.controlID;
			if (cid == btnLongTextDone) { ok = true; done = true; continue; }
			if (cid == btnLongTextCancel) { done = true; continue; }
		}
		if (ev.eType == sclRepeatEvent && ev.data.sclRepeat.scrollBarID == sclLongText) {
			Int16 delta = (Int16) (ev.data.sclRepeat.newValue - ev.data.sclRepeat.value);
			FldScrollField(fld, (UInt16) (delta < 0 ? -delta : delta), delta < 0 ? winUp : winDown);
			continue;
		}
		if (ev.eType == appStopEvent) {
			EvtAddEventToQueue(&ev);
			done = true;
			continue;
		}
		if (!LtdSysMenu(&ev)) FrmDispatchEvent(&ev);
		if (ev.eType == keyDownEvent || ev.eType == fldChangedEvent || ev.eType == fldEnterEvent) {
			LtdSyncScroll(frm, fld);
		}
	}

	if (ok) {
		char *ft = FldGetTextPtr(fld);
		if (ft) { StrNCopy(buf, ft, bufCap - 1); buf[bufCap - 1] = '\0'; }
		else buf[0] = '\0';
	}

	/* FrmEraseForm frees the field's text handle (th) for us - matches
	 * Address35's own EditNoteDialog exactly; an explicit MemHandleFree
	 * here would double-free. */
	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prev) FrmSetActiveForm(prev);
	return ok;
}
