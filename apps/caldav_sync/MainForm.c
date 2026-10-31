/*
 * MainForm.c - slot picker + read-only summary + Sync/Clear DB buttons.
 * Per-slot editing lives in SlotEditForm.c's own modal dialog.
 */
#include "CalDAVSync.h"
#include "MainForm.h"
#include "CDDVPrefs.h"
#include "CDDVSec35.h"
#include "SlotEditForm.h"
#include "PrefsForm.h"
#include "HelpForm.h"
#include "SyncEngine.h"
#include "DAVDb.h"
#include "DupSlot.h"
#include "ContactDb.h"

static CDDVPrefsType gPrefs;
static UInt8 gCurrentSlot = 0;

/* Static, not stack-local - CtlSetLabel/LstSetListChoices keep pointers,
 * not copies. */
static char gSlotListBuf[cddvMaxSlots][cddvAliasLen + 4];
static char *gSlotListPtrs[cddvMaxSlots];
static char gSlotNumBuf[48];
static char gStatusBuf[cddvSyncErrMsgLen + 40];

static void *ObjP(UInt16 id)
{
	return FrmGetObjectPtr(FrmGetActiveForm(), FrmGetObjectIndex(FrmGetActiveForm(), id));
}

static void SetPopupTrigger(UInt16 popupID, const char *label)
{
	ControlType *trigger = (ControlType *) ObjP(popupID);
	CtlSetLabel(trigger, (char *) label);
	CtlDrawControl(trigger);
}

static void SetFieldText(UInt16 fieldID, const char *text)
{
	/* FldInsert copies into the field's own text handle, so a literal is safe here. */
	FieldType *fld = (FieldType *) ObjP(fieldID);
	FldDelete(fld, 0, FldGetTextLength(fld));
	FldInsert(fld, text, StrLen(text));
}

/* Sync start/end chimes - 3-note sequences via raw SndDoCmd (sndCmdFreqDurationAmp
 * has existed since PalmOS v1, unlike the v3.0-only Snd commands, so no version
 * check needed). Amplitude follows the device's actual System Sound volume
 * (Off/Low/Medium/High) via SndGetDefaultVolume's sysAmpP - SndDoCmd is a raw
 * hardware call with no built-in volume/preference awareness (unlike
 * SndPlaySystemSound, which reads this internally), so both gating (skip
 * entirely at Off) and scaling (match Low/Medium/High, not always max) must be
 * done by hand here. SEC001 (matches this whole file/its caller
 * MainFormHandleEvent) - a same-file SEC00x-calls-untagged crash is real on
 * this toolchain, see cddv_seg.h's own reminder. */
static void PlayTone(UInt32 freq, UInt16 durMs, UInt16 amp) SEC001;

static void PlayTone(UInt32 freq, UInt16 durMs, UInt16 amp)
{
	SndCommandType cmd;
	cmd.cmd = sndCmdFreqDurationAmp;
	cmd.param1 = (Int32) freq;
	cmd.param2 = durMs;
	cmd.param3 = amp;
	SndDoCmd(NULL, &cmd, false);
}

static void PlaySyncChime(Boolean isStart) SEC001;

static void PlaySyncChime(Boolean isStart)
{
	UInt16 sysAmp;
	SndGetDefaultVolume(NULL, &sysAmp, NULL);
	if (sysAmp == 0) return;

	if (isStart) {
		PlayTone(1400, 75, sysAmp);
		PlayTone(1500, 113, sysAmp);
		PlayTone(2400, 450, sysAmp);
	} else {
		PlayTone(2400, 75, sysAmp);
		PlayTone(1800, 113, sysAmp);
		PlayTone(1200, 450, sysAmp);
	}
}

/* Refreshes all 8 popup-list item labels from gPrefs. Alias is Sec35
 * ciphertext at rest (CDDVSec35.h) - decrypted into a small transient local
 * for display, never held onto beyond this one StrPrintF. */
static void RefreshSlotListLabels(void)
{
	UInt8 i;
	ListType *lst = (ListType *) ObjP(lstSlotSelector);
	for (i = 0; i < cddvMaxSlots; i++) {
		if (gPrefs.slots[i].aliasLen) {
			char aliasPlain[cddvAliasLen];
			CDDVDecryptSlotField(i, cddvFieldAlias, (UInt8 *) gPrefs.slots[i].alias,
				gPrefs.slots[i].aliasLen, aliasPlain, sizeof(aliasPlain));
			StrPrintF(gSlotListBuf[i], "%d: %s", i + 1, aliasPlain);
		} else {
			StrPrintF(gSlotListBuf[i], "%d: (empty)", i + 1);
		}
		gSlotListPtrs[i] = gSlotListBuf[i];
	}
	LstSetListChoices(lst, gSlotListPtrs, cddvMaxSlots);
}

/* Verbose per-slot status: last sync outcome, event count, and error if any. */
static void UpdateSummaryDisplay(void)
{
	CDDVSlot *s = &gPrefs.slots[gCurrentSlot];

	if (s->aliasLen == 0) {
		SetFieldText(fldMainTypeDisplay, "(empty slot)");
		SetFieldText(fldMainStatusDisplay, "");
		return;
	}

	SetFieldText(fldMainTypeDisplay,
		s->serverType == cddvTypeCalDAV ? "CalDAV" : s->serverType == cddvTypeICloud ? "iCloud" :
		s->serverType == cddvTypeGoogle ? "Google" : s->serverType == cddvTypeICSFeed ? "ICS Feed" :
		s->serverType == cddvTypeICloudContacts ? "iCloud Contacts" : "CardDAV Contacts");

	if (s->lastSyncTime == 0) {
		SetFieldText(fldMainStatusDisplay, "Never synced");
	} else {
		DateTimeType dt;
		char dateBuf[24];
		TimSecondsToDateTime(s->lastSyncTime, &dt);
		StrPrintF(dateBuf, "%d/%d/%d %d:%02d", dt.month, dt.day, dt.year, dt.hour, dt.minute);
		if (s->lastSyncOk) {
			StrPrintF(gStatusBuf, cddvIsContacts(s->serverType) ? "OK - %u contact(s) synced\n%s" :
				"OK - %u event(s) synced\n%s", (unsigned int) s->lastSyncEventCount, dateBuf);
		} else {
			StrPrintF(gStatusBuf, "Failed: %s\n%s",
				s->lastSyncErrMsg[0] ? s->lastSyncErrMsg : "(unknown error)", dateBuf);
		}
		SetFieldText(fldMainStatusDisplay, gStatusBuf);
	}
}

static void LoadSlot(UInt8 idx)
{
	CDDVSlot *s = &gPrefs.slots[idx];
	gCurrentSlot = idx;
	if (s->aliasLen) {
		char aliasPlain[cddvAliasLen];
		CDDVDecryptSlotField(idx, cddvFieldAlias, (UInt8 *) s->alias, s->aliasLen, aliasPlain, sizeof(aliasPlain));
		StrPrintF(gSlotNumBuf, "%d: %s%s", idx + 1, aliasPlain, s->ignoreOnSyncAll ? " [Skipped]" : "");
	} else {
		StrPrintF(gSlotNumBuf, "%d: (empty)", idx + 1);
	}
	SetPopupTrigger(popSlotSelector, gSlotNumBuf);
	LstSetSelection((ListType *) ObjP(lstSlotSelector), (Int16) idx);
	UpdateSummaryDisplay();
}

static void MainOpen(void)
{
	UInt8 i;
	Boolean needSave = false;

	CDDVLoadPrefs(&gPrefs);

	/* One-time migration for a prefs blob loaded from a pre-v5 version
	 * (CDDVPrefs.c's own migration branches forward-copy old fields as
	 * plaintext, leaving fieldsEncrypted false) - encrypts every slot's
	 * identifying fields in place now that Sec35 is guaranteed unlocked
	 * (CalDAVSync.c's own launch gate ran before this form ever opened).
	 * A fast no-op for every slot already at v5+ (fieldsEncrypted true). */
	for (i = 0; i < cddvMaxSlots; i++) {
		if (!gPrefs.slots[i].fieldsEncrypted) {
			CDDVEncryptSlotFieldsIfNeeded(&gPrefs.slots[i], i);
			needSave = true;
		}
	}
	if (needSave) CDDVSavePrefs(&gPrefs);

	RefreshSlotListLabels();
	LoadSlot(gPrefs.activeSlot < cddvMaxSlots ? gPrefs.activeSlot : 0);
}

static void MainPersist(void)
{
	gPrefs.activeSlot = gCurrentSlot;
	CDDVSavePrefs(&gPrefs);
}

static void ShowSyncResultAlert(UInt8 slotIdx)
{
	char msg[64];
	CDDVSlot *s = &gPrefs.slots[slotIdx];
	if (s->lastSyncOk) {
		char aliasPlain[cddvAliasLen];
		CDDVDecryptSlotField(slotIdx, cddvFieldAlias, (UInt8 *) s->alias, s->aliasLen, aliasPlain, sizeof(aliasPlain));
		StrPrintF(msg, "'%s' synced successfully.", aliasPlain);
		FrmCustomAlert(altSyncOk, msg, NULL, NULL);
	}
	/* A failure already raised its own alert in SyncEngine.c. */
}

SEC001 Boolean MainFormHandleEvent(EventType *e)
{
	Boolean handled = false;
	FormPtr frm = FrmGetActiveForm();

	switch (e->eType) {
	case frmOpenEvent:
		FrmDrawForm(frm);
		MainOpen();
		handled = true;
		break;

	case popSelectEvent:
		if (e->data.popSelect.listID == lstSlotSelector) {
			UInt8 newSlot = (UInt8) e->data.popSelect.selection;
			if (newSlot != gCurrentSlot && newSlot < cddvMaxSlots) {
				MainPersist();
				LoadSlot(newSlot);
			}
			handled = true;
		}
		break;

	case ctlSelectEvent:
		if (e->data.ctlSelect.controlID == btnMainEditSlot) {
			if (SlotEditFormRun(&gPrefs.slots[gCurrentSlot], gCurrentSlot)) {
				RefreshSlotListLabels();
				LoadSlot(gCurrentSlot);		/* the popup's own label too, not just the list */
				MainPersist();
			}
			handled = true;
		} else if (e->data.ctlSelect.controlID == btnMainSyncNow) {
			Boolean ran = false;
			PlaySyncChime(true);
			ContactDbCleanup(&gPrefs);		/* data left over from changed/deleted slots */
			if (SyncScreenlessBegin()) {
				SyncEngineRunOne(&gPrefs, gCurrentSlot, false, false);
				ran = true;
			}
			SyncScreenlessEnd();
			PlaySyncChime(false);
			CDDVSavePrefs(&gPrefs);
			UpdateSummaryDisplay();
			/* Screenless Mode couldn't start the mode: no sync ran, so don't
			 * show the slot's previous result as if it were this one. */
			if (ran) ShowSyncResultAlert(gCurrentSlot);
			handled = true;
		} else if (e->data.ctlSelect.controlID == btnMainClearDB) {
			/* Clears only this slot's own synced entries, leaving its
			 * configuration and last-sync status untouched. */
			CDDVSlot *cs = &gPrefs.slots[gCurrentSlot];
			if (cs->aliasLen) {
				char aliasPlain[cddvAliasLen];
				CDDVDecryptSlotField(gCurrentSlot, cddvFieldAlias, (UInt8 *) cs->alias, cs->aliasLen, aliasPlain, sizeof(aliasPlain));
				if (FrmCustomAlert(altConfirmClearSlotDB, aliasPlain, NULL, NULL) == 0) {
					if (cddvIsContacts(cs->serverType)) {
						ContactDbDelete(gCurrentSlot);		/* its own Address35DAV_<n> */
					} else if (DAVDbBeginSlotRebuild(gCurrentSlot) == errNone) {
						DAVDbCommitSlotRebuild();
					}
				}
			}
			handled = true;
		}
		break;

	case menuEvent:
		switch (e->data.menu.itemID) {
		case mniMainSyncAll: {
			UInt8 i;
			UInt8 okCount = 0, totalCount = 0, doneCount = 0;
			Boolean go;
			char msg[64];
			/* Pre-count eligible slots so only the true last one passes
			 * keepOpen=false - every earlier slot reuses one negotiated
			 * connection instead of renegotiating per calendar. */
			for (i = 0; i < cddvMaxSlots; i++) {
				if (gPrefs.slots[i].aliasLen == 0) continue;
				if (gPrefs.slots[i].ignoreOnSyncAll) continue;
				totalCount++;
			}
			ContactDbCleanup(&gPrefs);		/* data left over from changed/deleted slots */
			/* One chime per batch, not per-calendar - play start only if
			 * there's actually something to sync. */
			if (totalCount > 0) PlaySyncChime(true);
			/* Screenless Mode: WiRSa into CalDAV Relay once for the whole batch */
			go = (Boolean) (totalCount > 0 && SyncScreenlessBegin());
			for (i = 0; i < cddvMaxSlots && go; i++) {
				Boolean keepOpen;
				if (gPrefs.slots[i].aliasLen == 0) continue;
				if (gPrefs.slots[i].ignoreOnSyncAll) continue;
				doneCount++;
				keepOpen = (Boolean) (doneCount < totalCount);
				/* silent=true: a failed slot must not block the rest of the
				 * batch behind a modal alert. */
				if (SyncEngineRunOne(&gPrefs, i, true, keepOpen)) okCount++;
			}
			/* The last slot's SyncEngineRunOne (keepOpen=false) normally
			 * commits every staged calendar; this covers it returning early
			 * (Sec35 locked) before reaching that point. No-op otherwise. */
			DAVDbCommitStaged();
			ContactDbCommitStaged();
			SyncScreenlessEnd();
			/* End chime plays regardless of per-slot outcome - marks "the
			 * batch is done", same as the summary alert below which already
			 * reports partial success rather than treating it as silence. */
			if (totalCount > 0) PlaySyncChime(false);
			CDDVSavePrefs(&gPrefs);
			UpdateSummaryDisplay();
			if (totalCount == 0) {
				FrmCustomAlert(altSyncOk, "No calendars to sync (all empty or ignored).", NULL, NULL);
			} else if (go) {
				StrPrintF(msg, "%d of %d calendar(s) synced successfully.", okCount, totalCount);
				FrmCustomAlert(altSyncOk, msg, NULL, NULL);
			}
			handled = true;
			break;
		}
		case mniMainPreferences:
			PrefsFormEdit(&gPrefs);
			MainPersist();
			handled = true;
			break;
		case mniMainDuplicate: {
			UInt8 t;
			if (DupSlotRun(&gPrefs, gCurrentSlot, &t)) {
				RefreshSlotListLabels();
				LoadSlot(t);			/* show the new copy */
				MainPersist();
			}
			handled = true;
			break;
		}
		case mniMainRecompute:
			if (FrmAlert(altConfirmRecompute) == 0) {
				DAVDbRecomputeLocalTimes();
				/* Force a full redraw - the toast's own bit-save/restore
				 * only correctly restores the screen if nothing else (the
				 * confirm alert above) drew over that region in between. */
				FrmDrawForm(frm);
			}
			handled = true;
			break;
		case mniMainResort:
			/* contacts in Address35's current sort order (it lives in
			 * Address35DB's AppInfo; a sync applies it too) */
			if (FrmAlert(altConfirmResort) == 0) {
				ContactDbResort();
				FrmDrawForm(frm);
				FrmCustomAlert(altInfo, "Contacts sorted in Address35's order.", NULL, NULL);
			}
			handled = true;
			break;
		case mniMainDeleteDB:
			if (FrmAlert(altConfirmDeleteDB) == 0) {
				DAVDbDeleteAll();
			}
			handled = true;
			break;
		case mniMainHelp:
			HelpFormShow();
			handled = true;
			break;
		}
		break;

	case frmCloseEvent:
		MainPersist();
		break;

	default:
		break;
	}

	return handled;
}
