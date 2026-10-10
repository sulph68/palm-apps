/*
 * LogForm.c - full-screen "Logs" viewer (Options > Logs). Same shape as
 * PreviewForm.c's full-screen text display (own file rather than a
 * repurposed PreviewForm - that one's "Import to Memo" button doesn't apply
 * here, see MainEn.rcp's own comment on frmLogs) - read PreviewForm.c as
 * the layout/scrollbar-sync reference, original code here.
 */
#include "LogForm.h"
#include "LzTPLog.h"

static FormPtr gFrm;
static MemHandle gLogH;

static void SyncLogScrollbar(FieldPtr fld)
{
	ScrollBarPtr bar = (ScrollBarPtr) FrmGetObjectPtr(gFrm, FrmGetObjectIndex(gFrm, sclLogs));
	UInt16 pos, textHeight, fieldHeight;
	Int16 maxVal;

	FldGetScrollValues(fld, &pos, &textHeight, &fieldHeight);
	maxVal = (Int16) ((textHeight > fieldHeight) ? (textHeight - fieldHeight) : 0);
	SclSetScrollBar(bar, (Int16) pos, 0, maxVal, (Int16) (fieldHeight > 0 ? fieldHeight - 1 : 1));
}

SEC001 void LogFormRun(void)
{
	FormPtr frm;
	FormPtr prevFrm;
	FieldPtr fld;
	UInt32 logLen;
	EventType event;
	Boolean done = false;

	if (!LzTPLogReadAll(&gLogH, &logLen)) {
		/* Nothing logged yet this run (e.g. Logs opened before any Connect
		 * attempt) - FldSetTextHandle needs a real MemHandle (there's no
		 * plain-pointer field-content API in this SDK - FldSetText also
		 * takes a MemHandle, not a Char*), so give it a tiny one rather
		 * than special-case a second display path. */
		static const char noneMsg[] = "(nothing logged yet)";
		UInt16 msgLen = (UInt16) (sizeof(noneMsg) - 1);
		gLogH = MemHandleNew((UInt32) msgLen + 1);
		if (gLogH) {
			char *p = (char *) MemHandleLock(gLogH);
			MemMove(p, noneMsg, msgLen + 1);
			MemHandleUnlock(gLogH);
		}
	}

	prevFrm = FrmGetActiveForm();
	frm = FrmInitForm(frmLogs);
	if (!frm) {
		if (gLogH) MemHandleFree(gLogH);
		gLogH = 0;
		return;
	}
	gFrm = frm;
	FrmSetActiveForm(frm);

	fld = (FieldPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, fldLogsText));
	if (gLogH) FldSetTextHandle(fld, gLogH);

	FrmDrawForm(frm);
	SyncLogScrollbar(fld);

	while (!done) {
		EvtGetEvent(&event, evtWaitForever);
		if (event.eType == appStopEvent) {
			EvtAddEventToQueue(&event);
			break;
		}
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, NULL)) continue;

		if (event.eType == ctlSelectEvent && event.data.ctlSelect.controlID == btnLogsClose) {
			done = true;
		} else if (event.eType == sclRepeatEvent
			&& event.data.sclRepeat.scrollBarID == sclLogs) {
			Int16 delta = (Int16) (event.data.sclRepeat.newValue - event.data.sclRepeat.value);
			if (delta != 0) {
				FldScrollField(fld, (UInt16) (delta > 0 ? delta : -delta),
					(WinDirectionType) (delta > 0 ? winDown : winUp));
			}
		}

		FrmHandleEvent(frm, &event);
	}

	FldSetTextHandle(fld, NULL);
	if (gLogH) { MemHandleFree(gLogH); gLogH = 0; }

	/* frmLogs IS full-screen (AT 0 0 160 160) - explicit erase first, same
	 * reason as PreviewForm.c's identical step (its own comment has the
	 * full real-bug writeup): native title-bar/button drawing only clears
	 * within its own bounds, so a shorter previous form's leftovers can
	 * otherwise show through past the end of this form's own content. */
	{
		RectangleType full;
		full.topLeft.x = 0; full.topLeft.y = 0;
		full.extent.x = 160; full.extent.y = 160;
		WinEraseRectangle(&full, 0);
	}
	FrmDeleteForm(frm);
	gFrm = NULL;

	if (prevFrm) {
		FrmSetActiveForm(prevFrm);
		FrmDrawForm(prevFrm);
	}
}
