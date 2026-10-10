/*
 * PromptForm.c - reusable single-field text prompt (New Folder, Rename).
 * Same hand-rolled modal event loop as ConnectForm.c/PrefsForm.c (FrmDoDialog
 * does not reliably dispatch frmOpenEvent through a custom handler on this
 * SDK/glue - see ConnectForm.c's own comment for the full story).
 */
#include "PromptForm.h"

static FormPtr gFrm;

/* Same in-place field-content idiom as ConnectForm.c's LzTPFldSetContent -
 * a field straight from its resource has no text handle yet, and FldInsert
 * silently no-ops without one (real bug found+fixed there; reused here
 * rather than rediscovering it). */
static void SetFieldContent(FieldType *fld, const char *text)
{
	UInt16 curLen;
	UInt16 newLen = (UInt16) StrLen(text);

	if (!FldGetTextHandle(fld)) {
		MemHandle h = MemHandleNew(1);
		if (h) {
			char *p = (char *) MemHandleLock(h);
			p[0] = '\0';
			MemHandleUnlock(h);
			FldSetTextHandle(fld, h);
		}
	}

	curLen = FldGetTextLength(fld);
	if (curLen > 0) {
		FldSetInsPtPosition(fld, curLen);
		FldDelete(fld, 0, curLen);
	}
	if (newLen > 0) {
		FldSetInsPtPosition(fld, 0);
		FldInsert(fld, text, newLen);
	}
}

SEC001 Boolean PromptFormRun(const char *title, const char *label,
	const char *initialValue, char *outValue, UInt16 outValueLen)
{
	FormPtr frm;
	FormPtr prevFrm;
	FieldPtr fld;
	UInt16 fieldIndex;
	EventType event;
	Boolean done = false;
	UInt16 btn = 0;

	prevFrm = FrmGetActiveForm();
	frm = FrmInitForm(frmPrompt);
	if (!frm) return false;
	gFrm = frm;

	FrmSetActiveForm(frm);
	FrmCopyTitle(frm, title);
	FrmCopyLabel(frm, lblPromptText, label);

	fieldIndex = FrmGetObjectIndex(frm, fldPromptValue);
	fld = (FieldPtr) FrmGetObjectPtr(frm, fieldIndex);
	SetFieldContent(fld, initialValue);

	FrmDrawForm(frm);
	FrmSetFocus(frm, fieldIndex);
	FldGrabFocus(fld);

	while (!done) {
		EvtGetEvent(&event, evtWaitForever);
		if (event.eType == appStopEvent) {
			EvtAddEventToQueue(&event);
			btn = btnPromptCancel;
			break;
		}
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, NULL)) continue;

		if (event.eType == ctlSelectEvent) {
			if (event.data.ctlSelect.controlID == btnPromptOk) {
				btn = btnPromptOk;
				done = true;
			} else if (event.data.ctlSelect.controlID == btnPromptCancel) {
				btn = btnPromptCancel;
				done = true;
			}
		}

		FrmHandleEvent(frm, &event);
	}

	if (btn == btnPromptOk) {
		const char *text = (const char *) FldGetTextPtr(fld);
		StrNCopy(outValue, text ? text : "", outValueLen - 1);
		outValue[outValueLen - 1] = '\0';
	}

	/* frmPrompt is not full-screen so FrmDrawForm above auto-saved the bits
	 * behind it - FrmEraseForm restores them before FrmDeleteForm, same as
	 * ConnectForm/PrefsForm's identical situation. */
	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	gFrm = NULL;

	if (prevFrm) {
		FrmSetActiveForm(prevFrm);
		FrmDrawForm(prevFrm);
	}

	return btn == btnPromptOk;
}
