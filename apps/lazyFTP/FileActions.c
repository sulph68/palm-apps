/*
 * FileActions.c - per-file/folder actions reached from MainForm.c. All
 * three FTP commands used here (FtpMakeDir/FtpDelete/FtpRename) were
 * already fully implemented in FtpProtocol.c - only the UI to reach them
 * was missing.
 */
#include "FileActions.h"
#include "FtpProtocol.h"
#include "Listing.h"
#include "PromptForm.h"
#include "PreviewForm.h"

/* Same in-place field-content idiom as PromptForm.c's SetFieldContent - a
 * field straight from its resource has no text handle yet and FldInsert
 * silently no-ops without one. */
static void SetFieldContent(FieldType *fld, const char *text)
{
	UInt16 newLen = (UInt16) StrLen(text);
	MemHandle h = MemHandleNew(newLen + 1);
	if (!h) return;
	{
		char *p = (char *) MemHandleLock(h);
		StrCopy(p, text);
		MemHandleUnlock(h);
	}
	FldSetTextHandle(fld, h);
}

/* Replaces the old 4-button ALERT (whose single button row ran past the
 * screen's right edge - "Preview"/"Delete"/"Rename"/"Cancel" is too much
 * for one row) with a real FORM laying the same 4 actions out 2x2. Returns
 * 0=Preview/Download, 1=Delete, 2=Rename, 3=Cancel - same order the caller
 * already switches on. */
static UInt16 FileActionAlertRun(const char *name, Boolean isText)
{
	FormPtr frm;
	FormPtr prevFrm;
	FieldPtr fld;
	UInt16 fieldIndex;
	EventType event;
	Boolean done = false;
	UInt16 btn = btnFileAction3;

	prevFrm = FrmGetActiveForm();
	frm = FrmInitForm(frmFileAction);
	if (!frm) return 3;

	FrmSetActiveForm(frm);

	CtlSetLabel((ControlPtr) FrmGetObjectPtr(frm,
		FrmGetObjectIndex(frm, btnFileAction0)),
		isText ? "Preview" : "Download");

	fieldIndex = FrmGetObjectIndex(frm, fldFileActionMsg);
	fld = (FieldPtr) FrmGetObjectPtr(frm, fieldIndex);
	SetFieldContent(fld, name);

	FrmDrawForm(frm);

	while (!done) {
		EvtGetEvent(&event, evtWaitForever);
		if (event.eType == appStopEvent) {
			EvtAddEventToQueue(&event);
			btn = btnFileAction3;
			break;
		}
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, NULL)) continue;

		if (event.eType == ctlSelectEvent) {
			UInt16 cid = event.data.ctlSelect.controlID;
			if (cid == btnFileAction0 || cid == btnFileAction1 ||
				cid == btnFileAction2 || cid == btnFileAction3) {
				btn = cid;
				done = true;
			}
		}

		FrmHandleEvent(frm, &event);
	}

	FrmEraseForm(frm);
	FrmDeleteForm(frm);

	if (prevFrm) {
		FrmSetActiveForm(prevFrm);
		FrmDrawForm(prevFrm);
	}

	return (UInt16) (btn - btnFileAction0);
}

/* Shared by the file-action sheet's own Delete button (FileActionRun below)
 * AND Edit > Delete when the current selection is a file (DoDelete below) -
 * one confirm dialog/wording, one code path, reachable both ways. */
static void DoDeleteFile(const char *name)
{
	char err[ftpReplyLen];
	UInt16 btn = FrmAlert(altConfirmDelete);
	if (btn != 0) return;		/* button 0 = "Delete", 1 = "Cancel" */

	LzTPBusyBegin("Deleting...");
	if (!FtpDelete(name, false, err, sizeof(err))) {
		LzTPBusyEnd();
		FrmCustomAlert(altErr2, "DELETE", err, NULL);
	} else {
		LzTPBusyEnd();
		ListingCacheForceStale();	/* the directory just changed - don't serve a stale cache */
		MainRefreshList();
	}
}

static void DoRename(const char *oldName)
{
	char newName[ftpNameLen];
	char err[ftpReplyLen];

	StrCopy(newName, oldName);
	if (!PromptFormRun("Rename", "New name:", oldName, newName, sizeof(newName))) return;
	if (!newName[0] || StrCompare(newName, oldName) == 0) return;

	LzTPBusyBegin("Renaming...");
	if (!FtpRename(oldName, newName, err, sizeof(err))) {
		LzTPBusyEnd();
		FrmCustomAlert(altErr2, "RENAME", err, NULL);
	} else {
		LzTPBusyEnd();
		ListingCacheForceStale();
		MainRefreshList();
	}
}

static void DoInstall(const char *name)
{
	char err[ftpReplyLen];
	Boolean needsReset = false;

	FtpSetProgressHook(LzTPProgressBar);
	FtpSetCancelQuery(LzTPProgressCancelled);
	LzTPProgressBegin("Downloading", name);
	if (!FtpRetrieveDatabase(name, &needsReset, err, sizeof(err))) {
		Boolean cancelled = LzTPProgressCancelled();
		LzTPProgressEnd();
		FtpSetProgressHook(NULL);
		FtpSetCancelQuery(NULL);
		/* err[0]=='\0' means the user themselves declined the overwrite
		 * prompt (RetrieveDbDeleteProc in FtpProtocol.c) - not a real
		 * failure, nothing to report. A user-initiated Cancel tap is the
		 * same kind of "not a real failure" outcome, even though it does
		 * leave err[0] set (see FtpRetrieveDatabase's own comment). */
		if (err[0] && !cancelled) FrmCustomAlert(altErr2, "RETR", err, NULL);
		return;
	}
	LzTPProgressEnd();
	FtpSetProgressHook(NULL);
	FtpSetCancelQuery(NULL);
	FrmCustomAlert(needsReset ? altNeedsReset : altInstalled, name, NULL, NULL);
}

SEC001 void FileActionRun(const char *name)
{
	Boolean isText = ListingIsTextFilename(name);
	UInt16 btn = FileActionAlertRun(name, isText);

	switch (btn) {
	case 0:		/* Preview (text) / Download+install (everything else) */
		if (isText) {
			PreviewFormRun(name);
		} else {
			DoInstall(name);
		}
		break;
	case 1:		/* Delete */
		DoDeleteFile(name);
		break;
	case 2:		/* Rename */
		DoRename(name);
		break;
	default:	/* Cancel */
		break;
	}
}

SEC001 void NewFolderRun(void)
{
	char name[ftpNameLen];
	char err[ftpReplyLen];

	name[0] = '\0';
	if (!PromptFormRun("New Folder", "Folder name:", "", name, sizeof(name))) return;
	if (!name[0]) return;

	LzTPBusyBegin("Creating...");
	if (!FtpMakeDir(name, err, sizeof(err))) {
		LzTPBusyEnd();
		FrmCustomAlert(altErr2, "MKD", err, NULL);
	} else {
		LzTPBusyEnd();
		ListingCacheForceStale();
		MainRefreshList();
	}
}

/* Deletes a NAMED folder (not necessarily the current directory) and
 * everything inside it - the Edit > Delete counterpart to the old
 * Folder > Del Folder (which only ever deleted the CURRENT directory, since
 * folders had no way to be selected without navigating into them before the
 * two-tap select model). FtpDeleteCurrentFolderRecursive itself still only
 * knows how to delete "wherever the control connection's CWD currently is",
 * so this CWDs into `name` first, reusing that existing recursive-delete
 * machinery unchanged rather than duplicating it - the function always
 * climbs back to the parent when it's done (success or failure), which is
 * exactly where we started, so no separate "return to parent" step is
 * needed here on top of it. The old "cannot delete the root directory"
 * guard doesn't need porting: a selectable ROW is always a CHILD of
 * wherever you're browsing, never the true FTP root itself. */
static void DoDeleteFolder(const char *name)
{
	char err[ftpReplyLen];
	Boolean truncated = false;
	UInt16 btn = FrmAlert(altConfirmDelFolder);
	if (btn != 0) return;		/* 0 = "Delete", 1 = "Cancel" */

	LzTPBusyBegin("Deleting...");
	if (!FtpChangeDir(name, err, sizeof(err))) {
		LzTPBusyEnd();
		FrmCustomAlert(altErr2, "CWD", err, NULL);
		return;		/* never entered it - nothing changed, nothing to refresh */
	}
	if (!FtpDeleteCurrentFolderRecursive(&truncated, err, sizeof(err))) {
		LzTPBusyEnd();
		FrmCustomAlert(altErr2, "Delete Folder", err, NULL);
		/* Contents may be partially deleted and/or we may already have
		 * navigated up by the time this failed - always refresh so the
		 * listing reflects real server state, not whatever was on screen
		 * before this started. */
		ListingCacheForceStale();
		MainRefreshList();
		return;
	}
	LzTPBusyEnd();
	ListingCacheForceStale();
	MainRefreshList();
	if (truncated) {
		FrmCustomAlert(altErr2, "Delete Folder",
			"Some very large subfolders had more entries than could be "
			"fully processed - only what was found got deleted.", NULL);
	}
}

/* Edit > Delete - acts on the current two-tap selection, routing to
 * whichever of the two confirm dialogs/deletion paths above fits what's
 * actually selected. Replaces the old Folder > Del Folder entirely (that
 * only ever worked on the current directory; this works on anything
 * selected, file or folder, from wherever it's browsed). */
SEC001 void DoDelete(void)
{
	if (!gListSelActive) {
		FrmCustomAlert(altErr, "Select an item first.", NULL, NULL);
		return;
	}
	if (gListSelIsDir) {
		DoDeleteFolder(gListSelName);
	} else {
		DoDeleteFile(gListSelName);
	}
}

/* ---- Cut/Paste (Edit menu) ----------------------------------------------
 * Cut/Paste is a MOVE, implemented entirely with FtpRename (RNFR/RNTO) -
 * see FtpRename's own comment for why this already works unchanged for a
 * directory and everything inside it (the server's RNFR/RNTO is a single
 * generic filesystem rename(), not a recursive operation). Deliberately
 * separate from the row-selection state (Listing.h's gListSel*): selection
 * is short-lived per-directory UI state (cleared by MainRefreshList's
 * chokepoint the moment anything reloads the listing), while this buffer
 * has to persist across navigating to wherever the user wants to paste. */
static Boolean gCutActive;
static char    gCutSrcDir[ftpPathLen];
static char    gCutName[ftpNameLen];
static Boolean gCutIsDir;

SEC001 Boolean FileActionsHasCut(void)
{
	return gCutActive;
}

/* Clears the cut buffer unconditionally - shared by DoPaste's own success
 * path and DoConnect (MainForm.c), which calls this the moment a NEW
 * connection is started. A buffer left over from a previous session could
 * reference a path that doesn't exist on whatever server is being connected
 * to now (or, worse, exists there as something else entirely) - starting a
 * fresh connection unconditionally invalidates it rather than risk a Paste
 * silently acting on stale cross-session state. */
SEC001 void FileActionsClearCut(void)
{
	gCutActive = false;
	gCutName[0] = '\0';
	gCutSrcDir[0] = '\0';
	ListingDrawCutIndicator();
}

SEC001 void DoCut(void)
{
	if (!gListSelActive) {
		FrmCustomAlert(altErr, "Select an item first.", NULL, NULL);
		return;
	}
	StrNCopy(gCutSrcDir, FtpCurPath(), sizeof(gCutSrcDir) - 1);
	gCutSrcDir[sizeof(gCutSrcDir) - 1] = '\0';
	StrNCopy(gCutName, gListSelName, sizeof(gCutName) - 1);
	gCutName[sizeof(gCutName) - 1] = '\0';
	gCutIsDir = gListSelIsDir;
	gCutActive = true;
	ListingDrawCutIndicator();
}

SEC001 void DoPaste(void)
{
	char src[ftpPathLen], dst[ftpPathLen], err[ftpReplyLen];

	if (!gCutActive) {
		FrmCustomAlert(altErr, "Nothing to paste.", NULL, NULL);
		return;
	}
	FtpJoinPath(gCutSrcDir, gCutName, src, sizeof(src));
	FtpJoinPath(FtpCurPath(), gCutName, dst, sizeof(dst));
	if (StrCompare(src, dst) == 0) {
		FrmCustomAlert(altErr, "Already in this folder.", NULL, NULL);
		return;
	}

	LzTPBusyBegin("Moving...");
	if (!FtpRename(src, dst, err, sizeof(err))) {
		LzTPBusyEnd();
		FrmCustomAlert(altErr2, "MOVE", err, NULL);
		return;		/* keep the buffer - lets the user retry (e.g. after a network hiccup) */
	}
	LzTPBusyEnd();

	FileActionsClearCut();
	ListingCacheForceStale();
	MainRefreshList();		/* re-lists the CURRENT dir - the paste destination - showing the moved item */
}

/* ---- Rename Folder (Edit menu) -------------------------------------------
 * Same PromptFormRun flow as the file-action sheet's own DoRename above,
 * but acts on the current two-tap selection (must be a directory) instead
 * of a tapped filename - folders have no action sheet of their own to
 * reach a rename from otherwise. Same-directory rename, unlike Cut/Paste -
 * gListSelName is a bare name, not a path, so no FtpJoinPath involved. */
SEC001 void DoRenameFolder(void)
{
	char newName[ftpNameLen];
	char err[ftpReplyLen];

	if (!gListSelActive || !gListSelIsDir) {
		FrmCustomAlert(altErr, "Select a folder first.", NULL, NULL);
		return;
	}
	StrCopy(newName, gListSelName);
	if (!PromptFormRun("Rename Folder", "New name:", gListSelName, newName, sizeof(newName))) return;
	if (!newName[0] || StrCompare(newName, gListSelName) == 0) return;

	LzTPBusyBegin("Renaming...");
	if (!FtpRename(gListSelName, newName, err, sizeof(err))) {
		LzTPBusyEnd();
		FrmCustomAlert(altErr2, "RENAME", err, NULL);
	} else {
		LzTPBusyEnd();
		ListingCacheForceStale();
		MainRefreshList();		/* also clears the selection via the existing chokepoint */
	}
}
