/*
 * MainForm.c - the directory-listing form's event handler.
 */
#include "LazyFTP.h"
#include "Listing.h"
#include "ConnectForm.h"
#include "PrefsForm.h"
#include "FileActions.h"
#include "UploadForm.h"
#include "LogForm.h"

/* Clears the filter field's on-screen text + internal state - called
 * whenever the underlying listing changes (new directory, disconnect) so a
 * filter typed for one directory doesn't silently carry over and hide
 * entries in the next one (same "leaving/reloading drops the filter"
 * behavior as Address35's card-mode filename filter). */
static void ClearFilter(void)
{
	FormPtr frm = FrmGetActiveForm();
	FieldPtr fld = (FieldPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, fldMainFilter));

	gListFilterActive = false;
	gListFilterTerm[0] = '\0';
	if (fld) {
		FldDelete(fld, 0, FldGetTextLength(fld));
		FldDrawField(fld);
	}
}

SEC001 void MainRefreshList(void)
{
	/* static, not stack-local - this function sits directly above FtpList
	 * (which itself sits above FtpCmd) in the exact call chain a real
	 * stack-overflow crash was found in this session on real hardware
	 * (see FtpProtocol.c's FtpCmd for the full writeup) - not re-entered
	 * (nothing it calls transitively calls back into MainRefreshList), so
	 * safe, and every byte trimmed off this frame is extra headroom for
	 * the reconnect chain several calls further down. */
	static char err[ftpReplyLen];
	Boolean ok;
	Boolean truncated = false;

	LzTPBusyBegin("Accessing...");
	ok = FtpList(&truncated, err, sizeof(err));
	LzTPBusyEnd();

	/* Clears the two-tap row selection - this chokepoint runs after every
	 * action that reloads what's actually in gListPageBuf (CWD-success,
	 * Delete, Rename, New/Del/Rename Folder, Refresh) so a selected name
	 * can't linger pointing at something that moved/vanished/no longer
	 * exists at that position. Deliberately NOT done in ListingUpdatePaging
	 * (fires on every filter keystroke) - see ListingClearSelection's own
	 * comment. */
	ListingClearSelection();

	/* Real bug found this session (reported as "some directories don't
	 * load... resulting in me getting stuck"): a failed LIST here always
	 * runs AFTER a successful CWD - the server has already moved us into
	 * the new directory, and gFtp.curPath (which the up-row's visibility
	 * is computed from) already reflects that, but this used to `return`
	 * on failure BEFORE redrawing, leaving the OLD directory's listing
	 * frozen on screen - visually inconsistent with where the app (and
	 * server) actually are, and confusing to navigate from. Always redraw
	 * now (showing whatever partial entries LIST did manage to parse
	 * before failing, even zero) so the up-row stays usable and the
	 * screen matches reality, regardless of whether LIST itself
	 * succeeded - the alert still reports the failure either way. */
	ClearFilter();
	ListingUpdatePaging();
	ListingDraw();

	if (!ok) {
		FrmCustomAlert(altErr2, "LIST", err, NULL);
	} else if (truncated) {
		/* The large-directory design (Listing.c's on-device cache) has no
		 * fixed entry cap anymore - this now only fires on genuine
		 * heap/storage exhaustion partway through, reported honestly
		 * rather than silently dropped. */
		char msg[80];
		StrPrintF(msg, "Ran out of %s after %lu files; showing what was read.",
			gListOutOfMemory ? "memory" : "storage space",
			(unsigned long) gListTruncatedAt);
		FrmCustomAlert(altErr2, "LIST", msg, NULL);
	}
}

static void NavigateUp(void)
{
	char err[ftpReplyLen];

	ListingCacheMarkLeft();	/* starts the 30s staleness clock for the cache we're leaving, if any */
	LzTPBusyBegin("Accessing...");
	if (!FtpParentDir(err, sizeof(err))) {
		LzTPBusyEnd();
		FrmCustomAlert(altErr2, "CWD", err, NULL);
	} else {
		LzTPBusyEnd();
		MainRefreshList();
	}
}

static void DoConnect(void)
{
	FtpConnParams p;
	char err[ftpReplyLen];
	Boolean connectTapped;

	/* ConnectFormRun's own hand-rolled dialog loop only repaints frmMain's
	 * STATIC resource controls (Upload/filter field/Cancel/scrollbar) via
	 * FrmDrawForm when it restores the caller's active form - the listing
	 * area is hand-drawn separately, so it must be redrawn here regardless
	 * of Connect vs Cancel, or the just-closed dialog's own pixels (e.g.
	 * its popup trigger) are left showing through underneath. Real bug
	 * found this session in two rounds: the first fix only covered the
	 * Connect-tapped path (redrawing before attempting the connection,
	 * since a failed FtpConnect is the common case and also needs this);
	 * Cancel had the exact same artifact and was still missing it. */
	connectTapped = ConnectFormRun(&p);
	ListingDraw();
	if (!connectTapped) return;	/* user cancelled */

	/* A new connection is starting (regardless of whether it goes on to
	 * succeed or fail below) - any cut buffer from a previous session/server
	 * is unconditionally stale from this point on, see FileActionsClearCut's
	 * own comment. frmMain is already the active form again by now
	 * (ConnectFormRun has returned), so the title-bar redraw this does lands
	 * in the right place. */
	FileActionsClearCut();

	LzTPBusyBegin("Connecting...");
	if (!FtpConnect(&p, err, sizeof(err))) {
		LzTPBusyEnd();
		FrmCustomAlert(altErr, err, NULL, NULL);
		return;
	}
	LzTPBusyEnd();
	MainRefreshList();
}

SEC001 Boolean MainFormHandleEvent(EventPtr eventP)
{
	Boolean handled = false;

	switch (eventP->eType) {
	case frmOpenEvent:
		FrmDrawForm(FrmGetActiveForm());
		ListingReset();
		gListFilterActive = false;
		gListFilterTerm[0] = '\0';
		ListingUpdatePaging();
		ListingDraw();
		/* frmMain only ever opens once per launch (sub-forms like Connect/
		 * Upload/FileActions are all modal-style dialog loops that return
		 * here rather than re-triggering frmOpenEvent) - so this is exactly
		 * "app just launched" and a good moment to offer Connect right away
		 * if there's no live session yet, instead of showing an empty list
		 * with no obvious next step. */
		if (!FtpIsConnected()) DoConnect();
		handled = true;
		break;

	case keyDownEvent:
		{
			/* Live filename filter (same idiom as Address35's frmMain Find
			 * bar): dispatch the key to fldMainFilter ourselves - fldChangedEvent
			 * does not fire per keystroke for a plain SINGLELINE field, see
			 * fldchangedevent-not-per-keystroke-gotcha in project memory. Cheap
			 * (no decrypt, just a substring scan) to rebuild the filter in full
			 * on every keystroke, same as Address35's card-mode filter. */
			FormPtr frm = FrmGetActiveForm();
			FieldPtr fld = (FieldPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, fldMainFilter));
			if (fld && FldHandleEvent(fld, eventP)) {
				char *t = (char *) FldGetTextPtr(fld);
				if (t && t[0]) {
					StrNCopy(gListFilterTerm, t, sizeof(gListFilterTerm) - 1);
					gListFilterTerm[sizeof(gListFilterTerm) - 1] = '\0';
					gListFilterActive = true;
				} else {
					gListFilterActive = false;
					gListFilterTerm[0] = '\0';
				}
				ListingUpdatePaging();
				ListingDraw();
				handled = true;
			}
		}
		break;

	case penDownEvent:
		{
			/* Only claim pen-downs that actually land on something in the
			 * list (the up-row or an entry) - a tap outside it (the Upload
			 * button, the scrollbar) must fall through unhandled so
			 * PalmOS's own control dispatch still gets to turn it into a
			 * ctlSelectEvent/sclRepeatEvent. Claiming every penDownEvent
			 * unconditionally silently ate all button taps - found
			 * live-testing against a real FTP server in POSE64. */
			ListHit hit = ListingHitTest(eventP->screenX, eventP->screenY);
			if (hit.kind == kListHitUp) {
				NavigateUp();
				handled = true;
			} else if (hit.kind == kListHitEntry) {
				FtpDirEntry *e = LzListEntry(hit.entryIndex);
				if (!ListingIsSelected(e->name)) {
					/* First tap on this row: select + highlight only, same
					 * as every other file manager's "tap to select, tap the
					 * selected item again to act" convention - no CWD, no
					 * action sheet, until the SECOND tap below confirms it. */
					ListingSetSelection(e->name, e->is_dir);
					ListingDraw();
					handled = true;
					break;
				}
				/* Second tap on the ALREADY-selected row: today's unchanged
				 * behavior. Cancelling out of the action sheet (or Preview)
				 * deliberately does NOT clear the selection - only a real
				 * mutation or navigation does, via MainRefreshList's own
				 * chokepoint (ListingClearSelection) - so the highlight and
				 * whatever's in the Edit menu's Cut buffer both survive a
				 * Cancel with no extra tap needed to re-select. */
				if (e->is_dir) {
					char err[ftpReplyLen];
					ListingCacheMarkLeft();	/* starts the 30s staleness clock for the cache we're leaving, if any */
					LzTPBusyBegin("Accessing...");
					if (!FtpChangeDir(e->name, err, sizeof(err))) {
						LzTPBusyEnd();
						FrmCustomAlert(altErr2, "CWD", err, NULL);
					} else {
						LzTPBusyEnd();
						MainRefreshList();
					}
				} else {
					/* Copy the name out BEFORE calling FileActionRun - a
					 * Delete/Rename inside it triggers a fresh MainRefreshList,
					 * which can reallocate/overwrite gListPageBuf out from
					 * under a pointer straight into it. */
					char name[ftpNameLen];
					StrCopy(name, e->name);
					FileActionRun(name);
					/* Real bug found live: after returning from a
					 * full-screen sub-form (Preview in particular),
					 * the "N of M" page indicator vanished and stale
					 * pixels from that sub-form lingered - ListingDraw()
					 * only repaints the list ROWS rectangle, not the
					 * hand-drawn page-indicator text in the title bar,
					 * which nothing else was re-drawing either once
					 * control returned here. Same applies to the newer
					 * Cut-buffer indicator, drawn in the same title strip. */
					ListingDraw();
					ListingDrawPageIndicator();
					ListingDrawCutIndicator();
				}
				handled = true;
			}
		}
		break;

	case sclRepeatEvent:
		if (eventP->data.sclRepeat.scrollBarID == sclMain) {
			ListingScroll((Int16) (eventP->data.sclRepeat.newValue
				- eventP->data.sclRepeat.value));
		}
		handled = true;
		break;

	case menuEvent:
		switch (eventP->data.menu.itemID) {
		case mniConnect:
			DoConnect();
			handled = true;
			break;
		case mniDisconnectServer:
			if (FtpIsConnected()) {
				ListingCacheMarkLeft();
				FtpDisconnectServer();
				ListingReset();
				ClearFilter();
				ListingUpdatePaging();
				ListingDraw();
			}
			handled = true;
			break;
		case mniDisconnectNetwork:
			/* Unconditional (not gated on FtpIsConnected()) - the whole
			 * point is tearing down the network connection even if it's
			 * just sitting open with no active server session; safe either
			 * way since FtpDisconnectNetwork() itself no-ops on the parts
			 * that aren't actually open. Always clears the screen too,
			 * matching Disconnect Server - this is a superset of that
			 * action (closes the server session first if one exists). */
			ListingCacheMarkLeft();
			FtpDisconnectNetwork();
			ListingReset();
			ClearFilter();
			ListingUpdatePaging();
			ListingDraw();
			handled = true;
			break;
		case mniRefresh:
			if (FtpIsConnected()) {
				ListingCacheForceStale();
				MainRefreshList();
			}
			handled = true;
			break;
		case mniNewFolder:
			if (FtpIsConnected()) {
				NewFolderRun();
				ListingDraw();
			}
			handled = true;
			break;
		case mniDelete:
			if (FtpIsConnected()) {
				DoDelete();
				ListingDraw();
				ListingDrawPageIndicator();
				ListingDrawCutIndicator();
			}
			handled = true;
			break;
		case mniRenameFolder:
			if (FtpIsConnected()) {
				DoRenameFolder();
				ListingDraw();
			}
			handled = true;
			break;
		case mniPreferences:
			PrefsFormRun();
			ListingDraw();
			handled = true;
			break;
		case mniLogs:
			LogFormRun();
			/* frmLog is full-screen (AT (0 0 160 160), same as frmPreview/
			 * UploadForm) - same redraw gap as those two, just never fixed
			 * here before now since a stale "N of M" text rarely LOOKS wrong
			 * (a title-bar bug this real, just visually quiet); the new Cut
			 * indicator has only two states (icon/no icon) and would make
			 * any staleness here immediately obvious, so fixing both while
			 * adding it rather than leaving a latent gap right next to it. */
			ListingDraw();
			ListingDrawPageIndicator();
			ListingDrawCutIndicator();
			handled = true;
			break;
		case mniCut:
			if (FtpIsConnected()) DoCut();
			handled = true;
			break;
		case mniPaste:
			if (FtpIsConnected()) DoPaste();
			handled = true;
			break;
		case mniAbout:
			FrmAlert(altAbout);
			handled = true;
			break;
		}
		break;

	/* "< N of M >" page navigation - REPEATBUTTON (auto-repeats while
	 * held, same as Address35's date-picker up/down arrows) generates
	 * ctlRepeatEvent, but plain BUTTON controls (btnMainCancel) only ever
	 * generate ctlSelectEvent - handle both event types in one place,
	 * matching EditDialogs.c's own precedent of dispatching ctlRepeatEvent
	 * and ctlSelectEvent identically for its REPEATBUTTON pairs. */
	case ctlRepeatEvent:
	case ctlSelectEvent:
		{
			UInt16 cid = (eventP->eType == ctlRepeatEvent)
				? eventP->data.ctlRepeat.controlID
				: eventP->data.ctlSelect.controlID;

			/* Real bug found live: the page-nav REPEATBUTTONs stayed
			 * visually "pressed"/highlighted after being tapped. Root
			 * cause: MainFormHandleEvent IS the form's own registered
			 * event handler (FrmSetEventHandler in LazyFTP.c), unlike
			 * ConnectForm/PrefsForm's hand-rolled dialog loops which
			 * already call FrmHandleEvent unconditionally after their own
			 * check - returning handled=true here without ever calling it
			 * skips FrmDispatchEvent's own fallback to the control
			 * manager's un-highlight/redraw for this event entirely.
			 * Calling it ourselves (matching the dialogs' own proven
			 * pattern) fixes that.
			 *
			 * MUST run BEFORE the action below, not after: left-arrow
			 * specifically stayed stuck (right-arrow didn't) because
			 * ListingPrevPage() disables btnMainPrevPage itself (via
			 * ListingDrawPageIndicator's CtlSetEnabled) the moment it lands
			 * back on page 1 - the exact page most manual testing ends up
			 * on. Disabling the control BEFORE this default release/
			 * un-highlight pass runs means the control manager finds it
			 * already disabled and skips redrawing it back to a normal "up"
			 * state, leaving the last-drawn (pressed) bitmap on screen.
			 * Running the default handling first, while the control is
			 * still enabled, lets it release cleanly; the subsequent
			 * disable (if any) then draws correctly from a normal state. */
			FrmHandleEvent(FrmGetActiveForm(), eventP);

			if (cid == btnMainCancel) {
				ClearFilter();
				gListFilterActive = false;
				ListingUpdatePaging();
				ListingDraw();
			} else if (cid == btnMainPrevPage) {
				if (ListingPrevPage()) ListingDraw();
			} else if (cid == btnMainNextPage) {
				if (ListingNextPage()) ListingDraw();
			} else if (cid == btnUpload) {
				if (FtpIsConnected()) {
					UploadFormRun();
					/* Same full-screen-sub-form redraw gap as the file-tap
					 * Preview path above - UploadForm is also AT (0 0 160
					 * 160). */
					ListingDraw();
					ListingDrawPageIndicator();
					ListingDrawCutIndicator();
				}
			}
			handled = true;
		}
		break;

	default:
		break;
	}
	return handled;
}
