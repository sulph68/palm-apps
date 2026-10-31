/*
 * PrefsForm.c - server URL + email, Sync Now, Forget Trusted Server, and
 * the local-unlock TTL (index<->seconds mapping following the same small-
 * fixed-list-popup pattern lazyFTP's own PrefsForm.c uses for its cache
 * TTL).
 */
#include "BitWarden.h"

static const UInt32 kTtlSeconds[] = { 60, 300, 900, 1800, 3600 };
#define bwTtlCount	5

static UInt16 TtlSecondsToIndex(UInt32 seconds)
{
	UInt16 i;
	for (i = 0; i < bwTtlCount; i++) if (kTtlSeconds[i] == seconds) return i;
	return 1;	/* default: 5 min */
}

static void *GetObjectPtr(UInt16 objID)
{
	return FrmGetObjectPtr(FrmGetActiveForm(), FrmGetObjectIndex(FrmGetActiveForm(), objID));
}

/* Same in-place field-content idiom as Dialogs.c/lazyFTP's PromptForm.c -
 * a field straight from its resource has no text handle yet, and FldInsert
 * silently no-ops without one. */
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
	if (curLen > 0) { FldSetInsPtPosition(fld, curLen); FldDelete(fld, 0, curLen); }
	if (newLen > 0) { FldSetInsPtPosition(fld, 0); FldInsert(fld, text, newLen); }
}

/* "Trusted:" now just shows Yes/No - the actual fingerprint moved to its
 * own full-screen viewer (BwFingerprintViewRun, reached via "Show"), per
 * explicit design instruction. Placeholder "Yes"/"No" in the .rcp are
 * already long enough for either value, no FrmCopyLabel buffer-size risk
 * here (unlike the fingerprint hex itself, which is why that one uses
 * FldSetTextPtr elsewhere, not FrmCopyLabel). */
static void UpdateFingerprintLabel(void)
{
	FrmCopyLabel(FrmGetActiveForm(), lblPrefsFingerprint, BwHasPinnedFingerprint() ? "Yes" : "No");
}

static void PrefsOpen(void)
{
	FormPtr frm = FrmGetActiveForm();
	char buf[160];

	BwGetServerURL(buf, sizeof(buf));
	SetFieldContent((FieldType *) GetObjectPtr(fldPrefsServer), buf);
	BwGetEmail(buf, sizeof(buf));
	SetFieldContent((FieldType *) GetObjectPtr(fldPrefsEmail), buf);
	UpdateFingerprintLabel();

	{
		UInt16 idx = TtlSecondsToIndex(BwGetTtlSeconds());
		ListType *lst = (ListType *) GetObjectPtr(lstPrefsTtl);
		LstSetSelection(lst, idx);
		CtlSetLabel((ControlType *) GetObjectPtr(popPrefsTtl), LstGetSelectionText(lst, idx));
	}
	{
		UInt16 idx = BwGetBaudIndex();
		ListType *lst = (ListType *) GetObjectPtr(lstPrefsBaud);
		LstSetSelection(lst, idx);
		CtlSetLabel((ControlType *) GetObjectPtr(popPrefsBaud), LstGetSelectionText(lst, idx));
	}
	BwScreenlessFormLoad();

	FrmDrawForm(frm);
}

static void PrefsSave(void)
{
	const char *text;
	text = (const char *) FldGetTextPtr((FieldType *) GetObjectPtr(fldPrefsServer));
	if (text) BwSetServerURL(text);
	text = (const char *) FldGetTextPtr((FieldType *) GetObjectPtr(fldPrefsEmail));
	if (text) {
		/* A cached master password is only meaningful for the account it was
		 * typed for - changing the email here means the next sync would
		 * otherwise silently send that account's server auth using the OLD
		 * account's cached password. Force a fresh prompt instead. */
		char oldEmail[80];
		BwGetEmail(oldEmail, sizeof(oldEmail));
		if (StrCompare(oldEmail, text) != 0) BwWipeCachedPassword();
		BwSetEmail(text);
	}
	{
		UInt16 idx = LstGetSelection((ListType *) GetObjectPtr(lstPrefsTtl));
		if (idx < bwTtlCount) BwSetTtlSeconds(kTtlSeconds[idx]);
	}
	{
		UInt16 idx = LstGetSelection((ListType *) GetObjectPtr(lstPrefsBaud));
		BwSetBaudIndex((UInt8) idx);
	}
	BwScreenlessFormSave();
}

/* Wraps BwSyncRun with its start/end chime - SEC001 so this doesn't grow
 * main .text, which has essentially no headroom left (see bw_seg.h). Calls
 * BwPlaySyncChime/BwSyncRun, both defined in a different file (Sync.c) -
 * always a safe cross-segment direction. PrefsFormHandleEvent itself stays
 * untagged/main and calls this same-file SEC001 helper - the safe direction
 * (main calling into SEC001 in the same file). Same shape as MainForm.c's
 * own identical SyncNowWithChime (kept as a separate static copy per file,
 * not shared, to avoid a cross-file SEC001-to-SEC001 export just for this). */
static Boolean SyncNowWithChime(void) SEC001;

static Boolean SyncNowWithChime(void)
{
	Boolean ok;
	BwPlaySyncChime(true);
	ok = BwSyncScreenless();
	BwPlaySyncChime(false);
	gBwSyncOkPending = ok;
	return ok;
}

Boolean PrefsFormHandleEvent(EventPtr e)
{
	Boolean handled = false;

	switch (e->eType) {
	case frmOpenEvent:
		PrefsOpen();
		handled = true;
		break;

	case popSelectEvent:
		if (e->data.popSelect.controlID == popPrefsTtl) {
			CtlSetLabel((ControlType *) GetObjectPtr(popPrefsTtl),
				LstGetSelectionText(e->data.popSelect.listP, e->data.popSelect.selection));
		} else if (e->data.popSelect.controlID == popPrefsBaud) {
			CtlSetLabel((ControlType *) GetObjectPtr(popPrefsBaud),
				LstGetSelectionText(e->data.popSelect.listP, e->data.popSelect.selection));
		}
		break;

	case ctlSelectEvent:
		if (e->data.ctlSelect.controlID == btnPrefsOk) {
			PrefsSave();
			FrmGotoForm(frmMain);
			handled = true;
		} else if (e->data.ctlSelect.controlID == btnPrefsCancel) {
			FrmGotoForm(frmMain);
			handled = true;
		} else if (e->data.ctlSelect.controlID == btnPrefsSyncNow) {
			/* BwSyncRun shows its own alert on failure - nothing extra to
			 * confirm on success. On success, go straight back to the main
			 * list (real-device feedback: staying on Preferences after a
			 * successful sync meant the synced items were never actually
			 * visible - the user has to notice and manually back out).
			 * MainForm's own frmOpenEvent reloads everything fresh. */
			PrefsSave();
			if (SyncNowWithChime()) {
				FrmGotoForm(frmMain);
			} else {
				UpdateFingerprintLabel();
				FrmDrawForm(FrmGetActiveForm());
			}
			handled = true;
		} else if (e->data.ctlSelect.controlID == btnPrefsForgetTrust) {
			if (FrmAlert(altConfirmForgetTrust) == 0) {
				BwClearPinnedFingerprint();
				UpdateFingerprintLabel();
				FrmDrawForm(FrmGetActiveForm());
			}
			handled = true;
		} else if (e->data.ctlSelect.controlID == btnPrefsShowFingerprint) {
			if (BwHasPinnedFingerprint()) {
				UInt8 fp[32];
				BwGetPinnedFingerprint(fp);
				BwFingerprintViewRun(fp);
			} else {
				FrmCustomAlert(altErr, "No server is trusted yet - sync once first.", NULL, NULL);
			}
			handled = true;
		}
		break;

	default:
		break;
	}

	return handled;
}
