/*
 * UploadForm.c - full-screen installed-database picker + remote-filename
 * prompt + FtpStoreDatabase orchestration for frmMain's "Upload" button.
 */
#include "UploadForm.h"
#include "FtpProtocol.h"
#include "Listing.h"
#include "PromptForm.h"

#define uploadMaxItems 128

typedef struct {
	char name[dmDBNameLength];
	LocalID dbID;
	UInt16 cardNo;
	Boolean isResourceDb;	/* dmHdrAttrResDB - .prc (app) vs .pdb (data) */
} UploadDbEntry;

static UploadDbEntry gUploadEntries[uploadMaxItems];
static char *gUploadChoices[uploadMaxItems];
static UInt16 gUploadCount;

/* Live type-to-filter, same bottom-bar idiom as frmMain's own fldMainFilter
 * (keyDownEvent dispatches to the field directly - fldChangedEvent does NOT
 * fire per keystroke for a plain SINGLELINE field, see
 * fldchangedevent-not-per-keystroke-gotcha in project memory). Unlike
 * frmMain's hand-drawn list, lstUpload is a real native List control, so
 * filtering means swapping in a SUBSET choices array via
 * LstSetListChoices - gUploadFilterMap translates a selection index in
 * that subset back to the real index into gUploadEntries/gUploadChoices. */
static Boolean gUploadFilterActive;
static char    gUploadFilterTerm[64];
static char   *gUploadFilterChoices[uploadMaxItems];
static UInt16  gUploadFilterMap[uploadMaxItems];
static UInt16  gUploadFilterCount;

/* Case-insensitive substring search - same idiom as Address35's
 * A35FieldHasTerm / this app's own Listing.c NameHasTerm, original code
 * here (each place in this suite keeps its own small copy rather than
 * sharing one across unrelated modules). */
static Boolean NameHasTerm(const char *name, const char *lower, UInt16 tl)
{
	UInt16 len = (UInt16) StrLen(name);
	UInt16 i, j;

	if (tl == 0) return true;
	if (tl > len) return false;
	for (i = 0; (UInt16) (i + tl) <= len; i++) {
		for (j = 0; j < tl; j++) {
			char c = name[i + j];
			if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
			if (c != lower[j]) break;
		}
		if (j == tl) return true;
	}
	return false;
}

static void RebuildFilteredList(ListPtr list)
{
	char lower[sizeof(gUploadFilterTerm)];
	UInt16 tl = (UInt16) StrLen(gUploadFilterTerm);
	UInt16 k;

	if (tl > sizeof(lower) - 1) tl = (UInt16) (sizeof(lower) - 1);
	for (k = 0; k < tl; k++) {
		char c = gUploadFilterTerm[k];
		if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
		lower[k] = c;
	}
	lower[tl] = '\0';

	/* Real bug found live: shrinking the list (e.g. typing a filter that
	 * narrows 100+ entries down to 5) via LstSetHeight does NOT erase the
	 * rows that used to occupy the now-vacated area below the new, shorter
	 * list - LstDrawList only repaints its OWN current bounds, leaving
	 * stale rows from the previous, taller render visibly stuck on screen.
	 * Erase the full MAXIMUM possible list area (matches lstUpload's own
	 * placeholder bounds in MainEn.rcp) before every redraw, regardless of
	 * whether this pass grows or shrinks it, so there's never a stale
	 * leftover row no matter which direction the count just changed. */
	{
		RectangleType maxR;
		maxR.topLeft.x = 1; maxR.topLeft.y = 18;
		maxR.extent.x = 158; maxR.extent.y = 125;
		WinEraseRectangle(&maxR, 0);
	}

	/* Deliberately NEVER calls LstSetHeight here - the list's own outline/
	 * bounds are fixed once at UploadFormRun's initial setup and stay that
	 * size for the dialog's whole lifetime (user-requested: the frame
	 * shouldn't visibly grow/shrink as the match count changes while
	 * typing, only the item rows drawn WITHIN it should). Fewer choices
	 * than the fixed height just means the extra rows render blank within
	 * the same, unchanging frame - completely normal native List behavior
	 * once the bounds themselves stop moving. */
	if (!gUploadFilterActive || tl == 0) {
		LstSetListChoices(list, gUploadChoices, (Int16) gUploadCount);
	} else {
		UInt16 i, fc = 0;
		for (i = 0; i < gUploadCount; i++) {
			if (NameHasTerm(gUploadEntries[i].name, lower, tl)) {
				gUploadFilterChoices[fc] = gUploadEntries[i].name;
				gUploadFilterMap[fc] = i;
				fc++;
			}
		}
		gUploadFilterCount = fc;
		LstSetListChoices(list, gUploadFilterChoices, (Int16) fc);
	}
	LstDrawList(list);
}

/* Every VISIBLE installed database, any type/creator (wildcard search) -
 * skips only our own on-device listing cache (LazyFTPDBCache - an internal
 * implementation detail, not something a user would ever mean to "upload")
 * and anything flagged dmHdrAttrHidden, same filter the Launcher itself
 * uses to decide what's user-facing. Sorted alphabetically (simple
 * insertion sort - typical device has well under a few hundred databases,
 * so O(n^2) here is not worth the complexity of anything fancier). */
static UInt16 EnumerateDatabases(void)
{
	DmSearchStateType state;
	UInt16 cardNo;
	LocalID dbID;
	Boolean newSearch = true;
	UInt16 count = 0;

	while (count < uploadMaxItems) {
		UInt16 attr;
		char name[dmDBNameLength];

		if (DmGetNextDatabaseByTypeCreator(newSearch, &state, 0, 0, false,
			&cardNo, &dbID) != errNone) break;
		newSearch = false;

		if (DmDatabaseInfo(cardNo, dbID, name, &attr, NULL, NULL, NULL, NULL,
			NULL, NULL, NULL, NULL, NULL) != errNone) continue;
		if (attr & dmHdrAttrHidden) continue;
		if (StrCompare(name, lzCacheDbName) == 0) continue;

		{
			UInt16 j = count;
			while (j > 0 && StrCompare(gUploadEntries[j - 1].name, name) > 0) {
				gUploadEntries[j] = gUploadEntries[j - 1];
				j--;
			}
			StrCopy(gUploadEntries[j].name, name);
			gUploadEntries[j].dbID = dbID;
			gUploadEntries[j].cardNo = cardNo;
			gUploadEntries[j].isResourceDb = (Boolean) ((attr & dmHdrAttrResDB) != 0);
			count++;
		}
	}

	return count;
}

static void DoUploadTo(const UploadDbEntry *entry)
{
	char remoteName[ftpNameLen];
	char err[ftpReplyLen];
	const char *ext = entry->isResourceDb ? ".prc" : ".pdb";

	/* Real bug found live: the default remote name was the bare PalmOS DB
	 * name with no extension at all ("AddressDB" instead of
	 * "AddressDB.pdb") - added here based on dmHdrAttrResDB (set at
	 * EnumerateDatabases time) so app/resource databases default to .prc
	 * and data/record databases default to .pdb, matching what a real FTP
	 * client naming convention (and this app's own Download/install side)
	 * expects. Still just the PROMPT's starting value - the user can edit
	 * it before confirming, same as before. */
	StrPrintF(remoteName, "%s%s", entry->name, ext);
	if (!PromptFormRun("Upload", "Remote name:", remoteName, remoteName, sizeof(remoteName))) return;
	if (!remoteName[0]) return;

	FtpSetProgressHook(LzTPProgressBar);
	FtpSetPhaseHook(LzTPProgressSetPhase);
	FtpSetCancelQuery(LzTPProgressCancelled);
	LzTPProgressBegin("Uploading", remoteName);
	if (!FtpStoreDatabase(remoteName, entry->name, entry->dbID, entry->cardNo, err, sizeof(err))) {
		Boolean cancelled = LzTPProgressCancelled();
		LzTPProgressEnd();
		FtpSetProgressHook(NULL);
		FtpSetPhaseHook(NULL);
		FtpSetCancelQuery(NULL);
		if (!cancelled) FrmCustomAlert(altErr2, "STOR", err, NULL);
	} else {
		LzTPProgressEnd();
		FtpSetProgressHook(NULL);
		FtpSetPhaseHook(NULL);
		FtpSetCancelQuery(NULL);
		FtpNoop();	/* keeps the control connection warm across a long CRC
			       verify so the refresh below doesn't pay for a reconnect */
		ListingCacheForceStale();
		MainRefreshList();
	}
}

SEC001 void UploadFormRun(void)
{
	FormPtr frm;
	FormPtr prevFrm;
	ListPtr list;
	UInt16 listIndex;
	EventType event;
	Boolean done = false;
	Int16 chosen = -1;
	UInt16 i;

	gUploadCount = EnumerateDatabases();
	if (gUploadCount == 0) {
		FrmCustomAlert(altErr, "No installed databases found to upload.", NULL, NULL);
		return;
	}
	for (i = 0; i < gUploadCount; i++) gUploadChoices[i] = gUploadEntries[i].name;

	gUploadFilterActive = false;
	gUploadFilterTerm[0] = '\0';

	prevFrm = FrmGetActiveForm();
	frm = FrmInitForm(frmUpload);
	if (!frm) return;
	FrmSetActiveForm(frm);

	listIndex = FrmGetObjectIndex(frm, lstUpload);
	list = (ListPtr) FrmGetObjectPtr(frm, listIndex);
	LstSetListChoices(list, gUploadChoices, (Int16) gUploadCount);
	/* Caps the list's visible row count at runtime - must match lstUpload's
	 * own VISIBLEITEMS in MainEn.rcp (currently 11), or LstSetHeight here
	 * silently overrides whatever the resource says. */
	LstSetHeight(list, (Int16) (gUploadCount < 11 ? gUploadCount : 11));

	FrmDrawForm(frm);

	while (!done) {
		EvtGetEvent(&event, evtWaitForever);
		if (event.eType == appStopEvent) {
			EvtAddEventToQueue(&event);
			break;
		}
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, NULL)) continue;

		if (event.eType == keyDownEvent) {
			FieldPtr fld = (FieldPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, fldUploadFilter));
			if (fld && FldHandleEvent(fld, &event)) {
				char *t = (char *) FldGetTextPtr(fld);
				if (t && t[0]) {
					StrNCopy(gUploadFilterTerm, t, sizeof(gUploadFilterTerm) - 1);
					gUploadFilterTerm[sizeof(gUploadFilterTerm) - 1] = '\0';
					gUploadFilterActive = true;
				} else {
					gUploadFilterActive = false;
					gUploadFilterTerm[0] = '\0';
				}
				RebuildFilteredList(list);
				continue;
			}
		}

		if (event.eType == lstSelectEvent && event.data.lstSelect.listID == lstUpload) {
			chosen = event.data.lstSelect.selection;
			/* Translate a filtered-subset selection index back to the real
			 * index into gUploadEntries/gUploadChoices - see
			 * RebuildFilteredList's own comment for why this map exists. */
			if (gUploadFilterActive && gUploadFilterTerm[0] &&
				chosen >= 0 && chosen < (Int16) gUploadFilterCount) {
				chosen = (Int16) gUploadFilterMap[chosen];
			}
			done = true;
		} else if (event.eType == ctlSelectEvent &&
			event.data.ctlSelect.controlID == btnUploadCancel) {
			done = true;
		}

		FrmHandleEvent(frm, &event);
	}

	/* frmUpload is full-screen (AT 0 0 160 160) - FrmEraseForm's "restore
	 * the auto-saved bits behind it" mechanism doesn't apply (nothing WAS
	 * auto-saved for a full-screen form) so it was a harmless no-op here,
	 * not the real fix. Real bug found live (same class as PreviewForm.c's
	 * own): PalmOS's native title-bar/button drawing only clears within
	 * its OWN control bounds, not whatever was on screen before - if
	 * frmUpload's own title/buttons happen to be WIDER than frmMain's at
	 * the same position, remnants of them would survive FrmDrawForm(prevFrm)
	 * below. An explicit full-screen erase first guarantees a clean slate
	 * regardless. */
	{
		RectangleType full;
		full.topLeft.x = 0; full.topLeft.y = 0;
		full.extent.x = 160; full.extent.y = 160;
		WinEraseRectangle(&full, 0);
	}
	FrmDeleteForm(frm);

	if (prevFrm) {
		FrmSetActiveForm(prevFrm);
		FrmDrawForm(prevFrm);
	}

	if (chosen >= 0 && chosen < (Int16) gUploadCount) {
		DoUploadTo(&gUploadEntries[chosen]);
	}
}
