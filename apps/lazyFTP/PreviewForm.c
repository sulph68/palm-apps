/*
 * PreviewForm.c - text file preview + "Import to Memo". Downloads via
 * FtpRetrieve's callback interface straight into a MemHandle (capped at
 * lzPreviewMaxBytes - PalmOS's own single-chunk limit and a plain memo's
 * practical size both make an unbounded preview unrealistic), then hands
 * that SAME handle to a NONEDITABLE multi-line FIELD via FldSetTextHandle
 * (no separate display copy). Import re-locks the same handle briefly to
 * copy its bytes into a new MemoDB record.
 */
#include "PreviewForm.h"
#include "FtpProtocol.h"
#include "Listing.h"

/* Comfortably under the 65535-byte single-chunk limit, and a sane upper
 * bound for "preview text in a Palm field, then save as one memo" -
 * classic MemoPad itself caps a single memo's size in the same ballpark. */
#define lzPreviewMaxBytes 8000

static FormPtr gFrm;
static MemHandle gPreviewH;
static char     *gPreviewBuf;		/* valid only while locked, during download */
static UInt32     gPreviewLen;
static Boolean    gPreviewTruncated;

static Boolean PreviewRecvProc(const void *data, UInt32 len, void *userData)
{
	UInt32 room;
	(void) userData;
	if (!gPreviewBuf) return true;		/* shouldn't happen, but never abort the transfer over it */

	room = lzPreviewMaxBytes - gPreviewLen;
	if (room == 0) {
		gPreviewTruncated = true;
		return true;
	}
	if (len > room) {
		len = room;
		gPreviewTruncated = true;
	}
	MemMove(gPreviewBuf + gPreviewLen, data, len);
	gPreviewLen += len;
	return true;
}

/* Re-locks the already-downloaded handle briefly (safe - MemHandleLock is
 * refcounted, and the field also holds its own lock on the same handle
 * while displaying) to copy its bytes into a new MemoDB record. MemoDB
 * records are plain NUL-terminated text with no other structure; a fresh
 * DmNewRecord defaults to category 0 (Unfiled). */
static Boolean ImportTextToMemo(MemHandle srcH, UInt32 len)
{
	DmOpenRef db;
	UInt16 idx = dmMaxRecordIndex;
	MemHandle destH;
	void *destP;
	char *srcP;
	Boolean ok = false;

	db = DmOpenDatabaseByTypeCreator((UInt32) 'DATA', (UInt32) 'memo', dmModeReadWrite);
	if (!db) return false;

	destH = DmNewRecord(db, &idx, len + 1);
	if (destH) {
		destP = MemHandleLock(destH);
		srcP = (char *) MemHandleLock(srcH);
		DmWrite(destP, 0, srcP, len);
		MemHandleUnlock(srcH);
		DmSet(destP, len, 1, 0);		/* NUL terminator */
		MemHandleUnlock(destH);
		DmReleaseRecord(db, idx, true);
		ok = true;
	}
	DmCloseDatabase(db);
	return ok;
}

static void SyncPreviewScrollbar(FieldPtr fld)
{
	ScrollBarPtr bar = (ScrollBarPtr) FrmGetObjectPtr(gFrm, FrmGetObjectIndex(gFrm, sclPreview));
	UInt16 pos, textHeight, fieldHeight;
	Int16 maxVal;

	FldGetScrollValues(fld, &pos, &textHeight, &fieldHeight);
	maxVal = (Int16) ((textHeight > fieldHeight) ? (textHeight - fieldHeight) : 0);
	SclSetScrollBar(bar, (Int16) pos, 0, maxVal, (Int16) (fieldHeight > 0 ? fieldHeight - 1 : 1));
}

SEC001 void PreviewFormRun(const char *filename)
{
	FormPtr frm;
	FormPtr prevFrm;
	FieldPtr fld;
	char err[ftpReplyLen];
	Boolean ok;
	EventType event;
	Boolean done = false;

	gPreviewH = MemHandleNew((UInt32) lzPreviewMaxBytes + 1);
	if (!gPreviewH) {
		FrmCustomAlert(altErr, "Out of memory", NULL, NULL);
		return;
	}
	gPreviewBuf = (char *) MemHandleLock(gPreviewH);
	gPreviewLen = 0;
	gPreviewTruncated = false;

	LzTPBusyBegin("Downloading...");
	ok = FtpRetrieve(filename, PreviewRecvProc, NULL, err, sizeof(err));
	LzTPBusyEnd();

	if (!ok) {
		MemHandleUnlock(gPreviewH);
		MemHandleFree(gPreviewH);
		gPreviewH = 0;
		gPreviewBuf = NULL;
		FrmCustomAlert(altErr2, "RETR", err, NULL);
		return;
	}
	gPreviewBuf[gPreviewLen] = '\0';
	MemHandleUnlock(gPreviewH);
	gPreviewBuf = NULL;		/* field owns display access to the handle from here */

	prevFrm = FrmGetActiveForm();
	frm = FrmInitForm(frmPreview);
	if (!frm) {
		MemHandleFree(gPreviewH);
		gPreviewH = 0;
		return;
	}
	gFrm = frm;
	FrmSetActiveForm(frm);
	FrmCopyTitle(frm, filename);

	fld = (FieldPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, fldPreviewText));
	FldSetTextHandle(fld, gPreviewH);

	FrmDrawForm(frm);
	SyncPreviewScrollbar(fld);

	if (gPreviewTruncated) {
		FrmCustomAlert(altErr2, "Preview",
			"File is larger than the preview limit; showing the first part only.", NULL);
	}

	while (!done) {
		EvtGetEvent(&event, evtWaitForever);
		if (event.eType == appStopEvent) {
			EvtAddEventToQueue(&event);
			break;
		}
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, NULL)) continue;

		if (event.eType == ctlSelectEvent) {
			if (event.data.ctlSelect.controlID == btnPreviewClose) {
				done = true;
			} else if (event.data.ctlSelect.controlID == btnPreviewImport) {
				if (ImportTextToMemo(gPreviewH, gPreviewLen)) {
					FrmAlert(altImportDone);
				} else {
					FrmCustomAlert(altErr, "Could not save the Memo", NULL, NULL);
				}
				done = true;
			}
		} else if (event.eType == sclRepeatEvent
			&& event.data.sclRepeat.scrollBarID == sclPreview) {
			Int16 delta = (Int16) (event.data.sclRepeat.newValue - event.data.sclRepeat.value);
			if (delta != 0) {
				FldScrollField(fld, (UInt16) (delta > 0 ? delta : -delta),
					(WinDirectionType) (delta > 0 ? winDown : winUp));
			}
		}

		FrmHandleEvent(frm, &event);
	}

	FldSetTextHandle(fld, NULL);
	MemHandleFree(gPreviewH);
	gPreviewH = 0;

	/* frmPreview IS full-screen (AT 0 0 160 160) - nothing was auto-saved
	 * behind it, so no FrmEraseForm needed (unlike the smaller dialogs).
	 *
	 * REAL bug found live: that's true for the "auto-saved bits" mechanism,
	 * but PalmOS's own NATIVE title-bar/button drawing only clears/paints
	 * within ITS OWN control's bounds, not whatever was on screen before -
	 * frmPreview's title (the full filename, e.g. "somefile_a.txt") and its
	 * wide "Import to Memo"/"Close" buttons are BOTH longer/wider than
	 * frmMain's own "LazyFTP" title and "Upload"/"Cancel" buttons at the
	 * same screen position, so FrmDrawForm(prevFrm) below left visible
	 * remnants past the end of the new (shorter) text - looked exactly like
	 * "LazyFTP _a.txt" in the title and stray characters near the bottom
	 * buttons. An explicit full-screen erase first guarantees a clean slate
	 * regardless of any such length mismatch, for any current or future
	 * form pair. */
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
