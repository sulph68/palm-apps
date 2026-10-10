/*
 * SlotEditForm.c - focused modal per-slot editor (frmSlotEdit), replacing
 * the old inline-on-frmMain adaptive fields (user feedback, 2026-09-15:
 * the single-form design crammed 4 server-type field groups + slot picker
 * + sync-window controls onto one non-scrolling 160px screen, which is
 * exactly what this suite's own convention avoids - Address35 uses
 * focused dialogs like frmPhoneDlg/frmEmailDlg for the same reason).
 *
 * Edits happen on a local working copy (gWorking); OK writes it back into
 * the caller's slot (encrypting any freshly-typed secret via CDDVSec35),
 * Cancel discards it. Hand-rolled modal loop, NOT FrmDoDialog - same
 * proven shape as PrefsForm.c (read as reference, original code in this
 * app): populate plain FIELDs before FrmDrawForm, but defer popup-
 * trigger/button CtlDrawControl until after (a non-fullscreen modal's
 * implicit background-capture happens during FrmDrawForm itself).
 *
 * Long/URL-like values (Server URL, Cal path, ICS feed URL, iCloud Host,
 * iCloud Cal UUID, Google Calendar ID) are edited via LongTextEditDialog
 * instead of typing
 * directly into a cramped single-line FIELD - but the on-screen control is
 * an ordinary-LOOKING FIELD (same row rhythm/style as every other row,
 * user feedback: a BUTTON broke the visual/vertical consistency), which
 * intercepts fldEnterEvent to open the full editor instead of accepting
 * direct keyboard input - same idiom Address35's own EditForm.c uses for
 * its "tap opens a sub-dialog" rows (ftEmail/ftAddress/ftSocial, read as
 * reference).
 */
#include "CalDAVSync.h"
#include "SlotEditForm.h"
#include "CDDVSec35.h"
#include "OAuthRelay.h"
#include "LongTextDialog.h"
#include "DAVDb.h"

/* Same reasoning as MainForm.c's own identical arrays used to have: each
 * group's static descriptive LABELs must be listed alongside their FIELD,
 * not just the editable widgets - all 4 groups share the same y-band by
 * design, so a label left out stays permanently visible and overlaps the
 * other groups' labels. Each group's own "Show" checkbox (user request,
 * 2026-09-18) listed alongside its Passwd field for the same reason. */
static const UInt16 gCalDAVFields[] = { lblSlotUrl, fldSlotUrl, lblSlotPath, fldSlotPath,
	lblSlotUser, fldSlotUser, lblSlotSecret, fldSlotSecret, chkSlotSecretShow, lblSlot2FAHint, 0 };
static const UInt16 gICloudFields[] = { lblSlotICloudUser, fldSlotICloudUser, lblSlotICloudSecret, fldSlotICloudSecret,
	chkSlotICloudSecretShow, lblSlotICloudShard, fldSlotICloudShard, lblSlotICloudUUID, fldSlotICloudUUID,
	lblSlotICloudCal, fldSlotICloudCal, 0 };
static const UInt16 gGoogleFields[] = { lblSlotGoogleCal, fldSlotGoogleCal,
	lblSlotGoogleClientId, fldSlotGoogleClientId,
	lblSlotGoogleClientSecret, fldSlotGoogleClientSecret, btnSlotGoogleSignIn, 0 };
static const UInt16 gIcsFields[] = { lblSlotIcsUrl, fldSlotIcsUrl, 0 };

static FormPtr gFrm;
static CDDVSlot gWorking;
static UInt8 gSlotIdx;

/* Plaintext shadow buffers for every field CDDVSec35.c now encrypts at rest
 * (gWorking's own server/path/uuid/user/alias/googleClientId/
 * googleClientSecret are ciphertext, same "never NUL-safe, needs an
 * explicit companion length" treatment secret already had). Populated once
 * by DecryptWorkingFields (SlotEditFormRun, right after gWorking = *s) and
 * from then on are what every FIELD widget and LongTextEditDialog call
 * actually reads/writes directly - gWorking's own ciphertext fields are
 * only touched again at Save time (SaveTypeFields/SaveFromForm), which
 * re-encrypts FROM these buffers back into gWorking. Same "static, not
 * stack-local" reasoning as gSecretPlain already documents. */
static char gServerPlain[cddvUrlLen];
static char gPathPlain[cddvPathLen];
static char gUuidPlain[40];
static char gUserPlain[cddvUserLen];
static char gAliasPlain[cddvAliasLen];
static char gGoogleClientIdPlain[cddvGoogleClientIdLen];
static char gGoogleClientSecretPlain[cddvGoogleClientSecretLen];

static void DecryptWorkingFields(void)
{
	CDDVSlot *s = &gWorking;
	CDDVDecryptSlotField(gSlotIdx, cddvFieldServer, (UInt8 *) s->server, s->serverLen, gServerPlain, sizeof(gServerPlain));
	CDDVDecryptSlotField(gSlotIdx, cddvFieldPath, (UInt8 *) s->path, s->pathLen, gPathPlain, sizeof(gPathPlain));
	CDDVDecryptSlotField(gSlotIdx, cddvFieldUuid, (UInt8 *) s->uuid, s->uuidLen, gUuidPlain, sizeof(gUuidPlain));
	CDDVDecryptSlotField(gSlotIdx, cddvFieldUser, (UInt8 *) s->user, s->userLen, gUserPlain, sizeof(gUserPlain));
	CDDVDecryptSlotField(gSlotIdx, cddvFieldAlias, (UInt8 *) s->alias, s->aliasLen, gAliasPlain, sizeof(gAliasPlain));
	CDDVDecryptSlotField(gSlotIdx, cddvFieldGoogleClientId, (UInt8 *) s->googleClientId, s->googleClientIdLen,
		gGoogleClientIdPlain, sizeof(gGoogleClientIdPlain));
	CDDVDecryptSlotField(gSlotIdx, cddvFieldGoogleClientSecret, (UInt8 *) s->googleClientSecret, s->googleClientSecretLen,
		gGoogleClientSecretPlain, sizeof(gGoogleClientSecretPlain));
}

/* Fixed placeholder shown (not a real masked length) when a slot already
 * HAS a saved secret - same idiom as lazyFTP's own ConnectForm.c
 * lzPassPlaceholder (read as reference): distinguishes "no secret saved"
 * (field left truly blank) from "one's saved but hidden" (field shows
 * this) without ever decrypting/displaying the real value. gSecretShown
 * tracks whether the CURRENTLY VISIBLE secret field is still showing this
 * placeholder untouched - the first keystroke into it clears the field
 * outright (there's no real plaintext behind the placeholder to edit
 * character-by-character) and flips this false, so SaveTypeFields can
 * tell "untouched placeholder" apart from "user typed a new secret". */
#define cddvSecretPlaceholder "******"
static Boolean gSecretPlaceholderShown = false;

/* Masked-by-default Passwd entry with a "Show" checkbox to reveal (user
 * request, 2026-09-18) - mirrors lazyFTP's own proven ConnectForm.c
 * pattern (read as reference: gPassPlain/gPassShowPlain/RedrawMaskedPass/
 * PassFieldKeyDown), simplified: this always shows either the full real
 * value or a same-length run of '*', with no per-keystroke reveal-then-
 * hide timer - that fancier behavior wasn't asked for here, and that
 * exact class of dangling-timer bug already cost this suite a real crash
 * once (see the sec35-text-entry-reveal-timer-crash-fix precedent), so
 * skipping it entirely removes that whole risk class rather than
 * reintroducing it carefully. gSecretPlain is the ONE shared shadow
 * buffer for whichever secret field (CalDAV or iCloud) is currently
 * visible - matches gSecretPlaceholderShown's own existing single-shared-
 * flag convention, correct because the two are mutually exclusive by
 * server type for any one slot. Cleared in SlotEditFormRun at both open
 * and close - never left sitting in memory longer than the dialog needs
 * it. */
static char gSecretPlain[cddvSecretPlainLen];
static Boolean gSecretShowPlain = false;

static void *ObjP(UInt16 id) { return FrmGetObjectPtr(gFrm, FrmGetObjectIndex(gFrm, id)); }

static void ShowHideGroup(const UInt16 *ids, Boolean show)
{
	UInt16 i;
	for (i = 0; ids[i] != 0; i++) {
		UInt16 idx = FrmGetObjectIndex(gFrm, ids[i]);
		if (idx == frmInvalidObjectId) continue;
		if (show) FrmShowObject(gFrm, idx); else FrmHideObject(gFrm, idx);
	}
}

/* Hide the 3 inactive groups FIRST, then show the active one LAST - all 4
 * groups occupy the exact same rectangles by design, and FrmHideObject
 * only erases exactly the object it's hiding; showing the active group
 * before hiding the others left ragged fragments of its labels behind
 * (found via a real POSE64 screenshot in this app's earlier single-form
 * layout - same hazard applies here). */
static void ApplyServerTypeVisibility(CDDVServerType t)
{
	if (t != cddvTypeCalDAV)  ShowHideGroup(gCalDAVFields, false);
	if (t != cddvTypeICloud)  ShowHideGroup(gICloudFields, false);
	if (t != cddvTypeGoogle)  ShowHideGroup(gGoogleFields, false);
	if (t != cddvTypeICSFeed) ShowHideGroup(gIcsFields, false);

	switch (t) {
	case cddvTypeCalDAV:  ShowHideGroup(gCalDAVFields, true); break;
	case cddvTypeICloud:  ShowHideGroup(gICloudFields, true); break;
	case cddvTypeGoogle:  ShowHideGroup(gGoogleFields, true); break;
	case cddvTypeICSFeed: ShowHideGroup(gIcsFields, true); break;
	}
}

static void SetCtlLabel(UInt16 id, const char *text)
{
	ControlType *c = (ControlType *) ObjP(id);
	CtlSetLabel(c, (char *) text);
	CtlDrawControl(c);
}

/* Static, not a direct string-literal handoff to SetCtlLabel/CtlSetLabel
 * (which keeps a POINTER, not a copy) - this file's SlotEditFormRun is
 * SEC001, and a literal referenced only from SEC001's own compiled code is
 * the "SEC001 string-literal itemStr dangling pointer" class of bug
 * already found and fixed elsewhere in this suite (real intermittent
 * Fatal Exception on device once that segment isn't resident; POSE64
 * never catches it) - copying into a static buffer here, while SEC001 is
 * still actively executing this call, is safe. */
static char gTypeTriggerBuf[12];
static void SetTypeTrigger(CDDVServerType t)
{
	StrCopy(gTypeTriggerBuf, t < 4 ?
		(t == cddvTypeCalDAV ? "CalDAV" : t == cddvTypeICloud ? "iCloud" :
		 t == cddvTypeGoogle ? "Google" : "ICS Feed") : "CalDAV");
	SetCtlLabel(popSlotType, gTypeTriggerBuf);
}

/* Same static-buffer discipline as gTypeTriggerBuf above - this button's
 * label needs to reflect whether a token is already saved (secretLen > 0),
 * not just show a fixed "Sign in with Google" regardless of state. */
static char gGoogleSignInBtnBuf[32];
static void UpdateGoogleSignInButtonLabel(Boolean signedIn)
{
	StrCopy(gGoogleSignInBtnBuf, signedIn ? "Re-sign in with Google" : "Sign in with Google");
	SetCtlLabel(btnSlotGoogleSignIn, gGoogleSignInBtnBuf);
}

static const char *gPastWindowNames[] = { "Default", "None", "1wk", "2wks", "1mth", "2mths", "3mths" };
static const char *gFutureWindowNames[] = { "Default", "2wks", "1mth", "3mths", "6mths", "1yr" };
static char gPastTriggerBuf[8];
static char gFutureTriggerBuf[8];

static void SetOverrideTriggers(UInt8 pastSel, UInt8 futureSel)
{
	StrCopy(gPastTriggerBuf, gPastWindowNames[pastSel]);
	StrCopy(gFutureTriggerBuf, gFutureWindowNames[futureSel]);
	SetCtlLabel(popSlotPastOverride, gPastTriggerBuf);
	SetCtlLabel(popSlotFutureOverride, gFutureTriggerBuf);
}

static void GetFieldText(UInt16 fieldID, char *out, UInt16 outLen)
{
	FieldType *fld = (FieldType *) ObjP(fieldID);
	char *p = FldGetTextPtr(fld);
	out[0] = '\0';
	if (p) StrNCopy(out, p, outLen - 1);
}

/* Real bug found 2026-09-18 (user report: "passwords keeps disappearing...
 * especially on a new slot record", and the SAME root cause turned out to
 * explain "new slots refused to sync" too - a field straight from its
 * resource, never previously populated, has NO text handle at all yet.
 * lazyFTP's own ConnectForm.c documents this exact gap (LzTPFldSetContent,
 * read as reference): FldInsert silently no-ops on such a field instead of
 * allocating one itself, so this function's own FldDelete+FldInsert calls
 * were doing nothing on a brand-new slot's never-touched fields - not just
 * a cosmetic display miss for the secret field, but for SecretFieldKeyDown
 * specifically, a handle-less field never properly took keyboard focus at
 * all, so gSecretPlain never accumulated anything the user typed, and a
 * genuinely empty secret got "saved" (silently skipped, since
 * SaveTypeFields only calls CDDVEncryptSlotSecret when gSecretPlain[0] is
 * non-empty) - explaining the sync failures too. Ordinary typing into an
 * UNMODIFIED field self-heals this (the Field Manager allocates a handle
 * on the first native keystroke, per lazyFTP's own comment) - it only bit
 * fields this file drives entirely programmatically (SetFieldText calls,
 * and the secret fields' own keyDownEvent interception in particular).
 * Applied here globally (every caller of SetFieldText benefits, not just
 * the secret fields) since it's a strict correctness fix with no downside
 * for a field that already has a handle. */
static void SetFieldText(UInt16 fieldID, const char *text)
{
	FieldType *fld = (FieldType *) ObjP(fieldID);
	UInt16 curLen;

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
	if (StrLen(text) > 0) {
		FldSetInsPtPosition(fld, 0);
		FldInsert(fld, text, StrLen(text));
	}
}

/* Rebuilds whichever secret FIELD is given from gSecretPlain - masked
 * (same-length run of '*') unless gSecretShowPlain is on. Simple
 * FldDelete+FldInsert (SetFieldText, above) is safe to call on every
 * keystroke here specifically because this is always a FULL replace of
 * the field's own entire content, not a partial edit - the insertion
 * point naturally lands at the end after each rebuild, which is exactly
 * where continued typing should resume (same reasoning lazyFTP's own
 * RedrawMaskedPass documents for the identical trick). */
static void RedrawMaskedSecret(UInt16 fieldID)
{
	if (gSecretShowPlain) {
		SetFieldText(fieldID, gSecretPlain);
	} else {
		char disp[cddvSecretPlainLen];
		UInt16 len = (UInt16) StrLen(gSecretPlain), i;
		for (i = 0; i < len; i++) disp[i] = '*';
		disp[len] = '\0';
		SetFieldText(fieldID, disp);
	}
}

/* Intercepts every keystroke on either secret field (SlotEditHandleEvent's
 * own keyDownEvent case, below) - maintains gSecretPlain and redraws via
 * RedrawMaskedSecret instead of letting the Field Manager insert the raw
 * character directly (which would defeat the whole point of masking).
 * Only handles append-at-end/backspace-from-end (no mid-string editing) -
 * same simplification lazyFTP's own PassFieldKeyDown already established
 * as sufficient for a password/token field. Returns false for anything
 * else (navigation keys, etc.) so the Field Manager's own default handling
 * still runs for those - harmless since the displayed text is already
 * either masked stars or the plain value either way. */
static Boolean SecretFieldKeyDown(UInt16 fieldID, EventType *e)
{
	UInt16 len;
	WChar c = e->data.keyDown.chr;

	if (gSecretPlaceholderShown) {
		gSecretPlaceholderShown = false;
		gSecretPlain[0] = '\0';
	}
	len = (UInt16) StrLen(gSecretPlain);

	if (c == chrBackspace) {
		if (len > 0) gSecretPlain[len - 1] = '\0';
		RedrawMaskedSecret(fieldID);
		return true;
	}
	if (TxtCharIsPrint(c) && len < (UInt16) (cddvSecretPlainLen - 1)) {
		gSecretPlain[len] = (char) c;
		gSecretPlain[len + 1] = '\0';
		RedrawMaskedSecret(fieldID);
		return true;
	}
	return false;
}

/* Real gap found 2026-09-18 (user report: "pasting a password into the
 * password field doesn't work") - the standard Paste menu command
 * (mnuDlgEdit's own ID 10003, MainEn.rcp) is auto-handled by
 * SysHandleEvent, which calls FldPaste() directly on whatever field has
 * focus - a menuEvent, never a keyDownEvent, so it never reaches
 * SecretFieldKeyDown at all. The field would briefly show the pasted text
 * in the clear (FldPaste knows nothing about this file's own masking),
 * but gSecretPlain never saw it, so the actually-saved secret stayed
 * whatever it was before the paste. Reads the clipboard directly
 * (ClipboardGetItem) and appends into gSecretPlain instead - same
 * append-at-the-end simplification SecretFieldKeyDown itself already
 * uses (no mid-string paste support, matching that this whole masking
 * scheme never supported mid-string editing to begin with). Called from
 * SlotEditFormRun's own event loop BEFORE SysHandleEvent gets a chance to
 * auto-handle the same menuEvent. */
static void PasteIntoSecretField(UInt16 fieldID)
{
	UInt16 clipLen;
	MemHandle h = ClipboardGetItem(clipboardText, &clipLen);
	char *clip;
	UInt16 curLen, i;

	if (!h || clipLen == 0) return;
	clip = (char *) MemHandleLock(h);

	if (gSecretPlaceholderShown) {
		gSecretPlaceholderShown = false;
		gSecretPlain[0] = '\0';
	}
	curLen = (UInt16) StrLen(gSecretPlain);
	for (i = 0; i < clipLen && curLen < (UInt16) (cddvSecretPlainLen - 1); i++) {
		char c = clip[i];
		if (c == '\r' || c == '\n') continue;	/* a real password/token shouldn't contain one - guard against a stray trailing newline from wherever the clipboard text came from */
		gSecretPlain[curLen++] = c;
	}
	gSecretPlain[curLen] = '\0';
	MemHandleUnlock(h);
	RedrawMaskedSecret(fieldID);
}

/* Same real gap as PasteIntoSecretField, for Cut (mnuDlgEdit's own ID
 * 10001) - user request, 2026-09-18. FldCut would remove the SELECTED
 * range from the field's own internal buffer and put it on the clipboard,
 * entirely bypassing gSecretPlain the same way FldPaste did - a save
 * right after would still write the OLD (not actually cut) value, even
 * though the field visibly looks shorter/empty. Unlike Paste, Cut/Copy
 * both have a real selection RANGE to respect (FldGetSelection) rather
 * than just "append at the end" - queries it and removes exactly that
 * slice of gSecretPlain, shifting the remainder left, same as a real
 * mid-string cut would. Puts the cut PLAINTEXT on the clipboard (matching
 * what a real Cut is for - pasting it somewhere else), not whatever the
 * field happened to be displaying (masked stars would make a "cut this
 * password elsewhere" workflow pointless). */
static void CutSecretField(UInt16 fieldID)
{
	FieldType *fld = (FieldType *) ObjP(fieldID);
	UInt16 selStart, selEnd, len, cutLen, i;

	FldGetSelection(fld, &selStart, &selEnd);
	if (selEnd <= selStart) return;	/* nothing selected - matches FldCut's own no-op */

	len = (UInt16) StrLen(gSecretPlain);
	if (selStart > len) selStart = len;
	if (selEnd > len) selEnd = len;
	if (selEnd <= selStart) return;
	cutLen = (UInt16) (selEnd - selStart);

	ClipboardAddItem(clipboardText, &gSecretPlain[selStart], cutLen);

	for (i = selStart; i + cutLen <= len; i++)
		gSecretPlain[i] = gSecretPlain[i + cutLen];
	gSecretPlain[len - cutLen] = '\0';

	RedrawMaskedSecret(fieldID);
}

/* Populates ONLY the given type's own group of widgets from gWorking -
 * called both at dialog-open and every time the Type popup switches TO
 * that group, so a group always shows the latest edited values instead of
 * whatever was in gWorking back at dialog-open time (found via direct user
 * observation: switching CalDAV -> iCloud showed iCloud's "Host:" field
 * still holding CalDAV's Server URL value, because switching types only
 * ever called FrmShowObject/HideObject and never re-populated the newly-
 * shown group's widgets from the CURRENT working state).
 *
 * Secret fields: gSecretPlaceholderShown is set ONCE, at dialog-open
 * (LoadIntoForm) - NOT recomputed here on every type-switch. Real bug this
 * avoids: this function used to reset it from s->secretLen every call,
 * which would silently revert the field back to showing
 * cddvSecretPlaceholder (discarding the user's own in-progress typing
 * display, still safely sitting in gSecretPlain either way) the moment
 * they switched Type after typing a new secret. While still showing the
 * placeholder, show it verbatim (the real ciphertext is still never
 * decrypted/shown back either way); once it's been cleared, show
 * gSecretPlain via RedrawMaskedSecret (masked unless the "Show" checkbox
 * for this group is on) - and keep that checkbox's own displayed value in
 * sync with the shared gSecretShowPlain state. */
static void LoadTypeFields(CDDVServerType t)
{
	CDDVSlot *s = &gWorking;

	switch (t) {
	case cddvTypeCalDAV:
		SetFieldText(fldSlotUrl, gServerPlain);
		SetFieldText(fldSlotPath, gPathPlain);
		SetFieldText(fldSlotUser, gUserPlain);
		if (gSecretPlaceholderShown) SetFieldText(fldSlotSecret, cddvSecretPlaceholder);
		else RedrawMaskedSecret(fldSlotSecret);
		CtlSetValue((ControlType *) ObjP(chkSlotSecretShow), gSecretShowPlain);
		break;
	case cddvTypeICloud:
		SetFieldText(fldSlotICloudUser, gUserPlain);
		if (gSecretPlaceholderShown) SetFieldText(fldSlotICloudSecret, cddvSecretPlaceholder);
		else RedrawMaskedSecret(fldSlotICloudSecret);
		CtlSetValue((ControlType *) ObjP(chkSlotICloudSecretShow), gSecretShowPlain);
		SetFieldText(fldSlotICloudShard, gServerPlain);
		SetFieldText(fldSlotICloudUUID, gUuidPlain);
		SetFieldText(fldSlotICloudCal, gPathPlain);
		break;
	case cddvTypeGoogle:
		SetFieldText(fldSlotGoogleCal, gPathPlain);
		SetFieldText(fldSlotGoogleClientId, gGoogleClientIdPlain);
		SetFieldText(fldSlotGoogleClientSecret, gGoogleClientSecretPlain);
		UpdateGoogleSignInButtonLabel(s->secretLen > 0);
		break;
	case cddvTypeICSFeed:
		SetFieldText(fldSlotIcsUrl, gServerPlain);
		break;
	}
}

/* Set by SaveTypeFields whenever the user typed a genuinely NEW secret
 * (gSecretPlaceholderShown was false) but CDDVEncryptSlotSecret couldn't
 * save it (Sec35 locked) - checked once by SaveFromForm right after the
 * final OK-time save, so the user gets a CLEAR alert instead of the
 * secret silently vanishing (matches this suite's usual discipline of
 * never failing an intended write silently). Left false, harmlessly, on
 * every intermediate Type-switch call to this same function - only the
 * final OK-time call's outcome matters, and SaveFromForm resets this
 * right before making that call. */
static Boolean gSecretSaveFailed = false;

/* Reads ONLY the given type's own group of widgets back into gWorking -
 * the inverse of LoadTypeFields, called both on OK and right before a
 * Type switch (so in-progress edits in the group being hidden aren't
 * silently dropped when the other group's widgets get reloaded on top of
 * the same underlying gWorking.server/path/user fields).
 *
 * Secret handling: if gSecretPlaceholderShown is still true, the field
 * was never touched (still showing cddvSecretPlaceholder verbatim, not a
 * real secret) - skip re-encrypting it entirely, leaving the slot's
 * already-saved secret exactly as-is. Only a field the user actually
 * edited (gSecretPlaceholderShown already flipped false by
 * SecretFieldKeyDown) gets encrypted - reading from gSecretPlain, the
 * shadow buffer, NOT the field itself (which may currently be showing
 * masked stars rather than the real value - see RedrawMaskedSecret). */
static void SaveTypeFields(CDDVServerType t)
{
	CDDVSlot *s = &gWorking;

	switch (t) {
	case cddvTypeCalDAV:
		GetFieldText(fldSlotUser, gUserPlain, sizeof(gUserPlain));
		CDDVEncryptSlotField(gSlotIdx, cddvFieldServer, (UInt8 *) s->server, sizeof(s->server), gServerPlain, &s->serverLen);
		CDDVEncryptSlotField(gSlotIdx, cddvFieldPath, (UInt8 *) s->path, sizeof(s->path), gPathPlain, &s->pathLen);
		CDDVEncryptSlotField(gSlotIdx, cddvFieldUser, (UInt8 *) s->user, sizeof(s->user), gUserPlain, &s->userLen);
		if (! gSecretPlaceholderShown) {
			if (gSecretPlain[0] && ! CDDVEncryptSlotSecret(s, gSlotIdx, gSecretPlain))
				gSecretSaveFailed = true;
		}
		break;
	case cddvTypeICloud:
		GetFieldText(fldSlotICloudUser, gUserPlain, sizeof(gUserPlain));
		GetFieldText(fldSlotICloudUUID, gUuidPlain, sizeof(gUuidPlain));
		CDDVEncryptSlotField(gSlotIdx, cddvFieldUser, (UInt8 *) s->user, sizeof(s->user), gUserPlain, &s->userLen);
		CDDVEncryptSlotField(gSlotIdx, cddvFieldServer, (UInt8 *) s->server, sizeof(s->server), gServerPlain, &s->serverLen);
		CDDVEncryptSlotField(gSlotIdx, cddvFieldUuid, (UInt8 *) s->uuid, sizeof(s->uuid), gUuidPlain, &s->uuidLen);
		CDDVEncryptSlotField(gSlotIdx, cddvFieldPath, (UInt8 *) s->path, sizeof(s->path), gPathPlain, &s->pathLen);
		if (! gSecretPlaceholderShown) {
			if (gSecretPlain[0] && ! CDDVEncryptSlotSecret(s, gSlotIdx, gSecretPlain))
				gSecretSaveFailed = true;
		}
		break;
	case cddvTypeGoogle:
		/* Calendar ID/Client ID/Client Secret live in the shadow plaintext
		 * buffers directly via the long-text buttons - nothing FIELD-based
		 * to read back, just encrypt what's already there. The OAuth
		 * refresh token itself is written straight into gWorking.secret by
		 * OAuthRelayRunGoogleSignIn (CDDVEncryptSlotSecret) - nothing to do
		 * for it here. */
		CDDVEncryptSlotField(gSlotIdx, cddvFieldPath, (UInt8 *) s->path, sizeof(s->path), gPathPlain, &s->pathLen);
		CDDVEncryptSlotField(gSlotIdx, cddvFieldGoogleClientId, (UInt8 *) s->googleClientId,
			sizeof(s->googleClientId), gGoogleClientIdPlain, &s->googleClientIdLen);
		CDDVEncryptSlotField(gSlotIdx, cddvFieldGoogleClientSecret, (UInt8 *) s->googleClientSecret,
			sizeof(s->googleClientSecret), gGoogleClientSecretPlain, &s->googleClientSecretLen);
		break;
	case cddvTypeICSFeed:
		/* Feed URL - same "already in the shadow buffer via the long-text
		 * button" reasoning as Google's Calendar ID above. */
		CDDVEncryptSlotField(gSlotIdx, cddvFieldServer, (UInt8 *) s->server, sizeof(s->server), gServerPlain, &s->serverLen);
		break;
	}
}

static void LoadIntoForm(void)
{
	CDDVSlot *s = &gWorking;

	/* Set ONCE here, not inside LoadTypeFields (see that function's own
	 * comment on the real bug this avoids) - reflects whether THIS dialog
	 * session has a real saved secret to placeholder-protect, for the
	 * whole lifetime of this one open/edit/close cycle. */
	gSecretPlaceholderShown = (Boolean) (s->secretLen > 0);
	gSecretPlain[0] = '\0';
	gSecretShowPlain = false;

	SetFieldText(fldSlotAlias, gAliasPlain);
	CtlSetValue((ControlType *) ObjP(chkSlotIgnore), s->ignoreOnSyncAll);

	FrmDrawForm(gFrm);

	LstSetSelection((ListType *) ObjP(lstSlotType), (Int16) s->serverType);
	SetTypeTrigger(s->serverType);
	LstSetSelection((ListType *) ObjP(lstSlotPastOverride), (Int16) s->pastOverrideSel);
	LstSetSelection((ListType *) ObjP(lstSlotFutureOverride), (Int16) s->futureOverrideSel);
	SetOverrideTriggers(s->pastOverrideSel, s->futureOverrideSel);
	LoadTypeFields(s->serverType);
	ApplyServerTypeVisibility(s->serverType);
}

static void SaveFromForm(void)
{
	CDDVSlot *s = &gWorking;

	gSecretSaveFailed = false;

	GetFieldText(fldSlotAlias, gAliasPlain, sizeof(gAliasPlain));
	CDDVEncryptSlotField(gSlotIdx, cddvFieldAlias, (UInt8 *) s->alias, sizeof(s->alias), gAliasPlain, &s->aliasLen);
	s->serverType = (CDDVServerType) LstGetSelection((ListType *) ObjP(lstSlotType));
	s->ignoreOnSyncAll = (Boolean) CtlGetValue((ControlType *) ObjP(chkSlotIgnore));
	s->pastOverrideSel = (UInt8) LstGetSelection((ListType *) ObjP(lstSlotPastOverride));
	s->futureOverrideSel = (UInt8) LstGetSelection((ListType *) ObjP(lstSlotFutureOverride));
	SaveTypeFields(s->serverType);

	switch (s->serverType) {
	case cddvTypeCalDAV: case cddvTypeICloud: break;
	case cddvTypeGoogle:
		MemSet(s->server, sizeof(s->server), 0); s->serverLen = 0;
		MemSet(s->user, sizeof(s->user), 0); s->userLen = 0;
		break;
	case cddvTypeICSFeed:
		MemSet(s->path, sizeof(s->path), 0); s->pathLen = 0;
		MemSet(s->user, sizeof(s->user), 0); s->userLen = 0;
		break;
	}

	// Db35: make a failed encrypt VISIBLE rather than silently discarding
	// the freshly-typed secret (CDDVEncryptSlotSecret's own contract: a
	// locked Sec35 leaves the slot's PREVIOUSLY-saved secret untouched,
	// so nothing is corrupted here - just the new one the user just typed
	// never made it in). Every other field on this slot (alias, server,
	// path, etc.) still saves normally either way.
	if (gSecretSaveFailed)
		FrmCustomAlert(altErr,
			"This slot's password/token could not be saved because Sec35 is locked. "
			"Unlock Sec35, then edit this slot again to save it.", NULL, NULL);
}

static Boolean SlotEditHandleEvent(EventType *e)
{
	Boolean handled = false;

	switch (e->eType) {
	case popSelectEvent:
		if (e->data.popSelect.listID == lstSlotType) {
			CDDVServerType newType = (CDDVServerType) e->data.popSelect.selection;
			/* Save the group being hidden's in-progress edits into
			 * gWorking BEFORE loading the new group - otherwise the new
			 * group's LoadTypeFields would overwrite gWorking.server/
			 * path/user with itself, silently discarding whatever was
			 * just typed under the old type. */
			SaveTypeFields(gWorking.serverType);
			gWorking.serverType = newType;
			SetTypeTrigger(newType);
			LoadTypeFields(newType);
			ApplyServerTypeVisibility(newType);
			handled = true;
		} else if (e->data.popSelect.listID == lstSlotPastOverride ||
				   e->data.popSelect.listID == lstSlotFutureOverride) {
			SetOverrideTriggers((UInt8) LstGetSelection((ListType *) ObjP(lstSlotPastOverride)),
				(UInt8) LstGetSelection((ListType *) ObjP(lstSlotFutureOverride)));
		}
		break;

	/* Server URL/Path/UUID/Calendar ID look like ordinary FIELDs (same row
	 * rhythm as every other row - user feedback) but intercept
	 * fldEnterEvent to open the full-screen editor instead of accepting
	 * direct keyboard input - same idiom Address35's own EditForm.c uses
	 * (fldEditRowBase rows for ftEmail/ftAddress/ftSocial, read as
	 * reference). Returning handled=true here consumes the event before
	 * the Field Manager's own default focus-and-edit behavior runs, so the
	 * user is never able to type into these fields directly. */
	case fldEnterEvent: {
		UInt16 fid = e->data.fldEnter.fieldID;
		if (fid == fldSlotUrl) {
			if (LongTextEditDialog(gServerPlain, sizeof(gServerPlain)))
				SetFieldText(fldSlotUrl, gServerPlain);
			handled = true;
		} else if (fid == fldSlotICloudShard) {
			/* iCloud's shard host (e.g. "p32-caldav.icloud.com") shares
			 * gServerPlain with the CalDAV group's own Server field - same
			 * underlying slot member, mutually exclusive by type (user
			 * feedback, 2026-09-17: shard names can run long enough to
			 * look truncated in a 98px-wide inline FIELD too). */
			if (LongTextEditDialog(gServerPlain, sizeof(gServerPlain)))
				SetFieldText(fldSlotICloudShard, gServerPlain);
			handled = true;
		} else if (fid == fldSlotPath) {
			if (LongTextEditDialog(gPathPlain, sizeof(gPathPlain)))
				SetFieldText(fldSlotPath, gPathPlain);
			handled = true;
		} else if (fid == fldSlotICloudCal) {
			/* The calendar UUID, not the DSID - genuinely long, and the
			 * one field here that's actually at risk of looking truncated
			 * in a 98px-wide inline FIELD (user feedback, 2026-09-17). DSID
			 * is a short numeric account ID and stays a normal editable
			 * FIELD below - it was the DSID field that used to get this
			 * full-screen treatment, backwards from what actually needed
			 * it. */
			if (LongTextEditDialog(gPathPlain, sizeof(gPathPlain)))
				SetFieldText(fldSlotICloudCal, gPathPlain);
			handled = true;
		} else if (fid == fldSlotGoogleCal) {
			if (LongTextEditDialog(gPathPlain, sizeof(gPathPlain)))
				SetFieldText(fldSlotGoogleCal, gPathPlain);
			handled = true;
		} else if (fid == fldSlotGoogleClientId) {
			if (LongTextEditDialog(gGoogleClientIdPlain, sizeof(gGoogleClientIdPlain)))
				SetFieldText(fldSlotGoogleClientId, gGoogleClientIdPlain);
			handled = true;
		} else if (fid == fldSlotGoogleClientSecret) {
			if (LongTextEditDialog(gGoogleClientSecretPlain, sizeof(gGoogleClientSecretPlain)))
				SetFieldText(fldSlotGoogleClientSecret, gGoogleClientSecretPlain);
			handled = true;
		} else if (fid == fldSlotIcsUrl) {
			if (LongTextEditDialog(gServerPlain, sizeof(gServerPlain)))
				SetFieldText(fldSlotIcsUrl, gServerPlain);
			handled = true;
		}
		break;
	}

	case ctlSelectEvent:
		if (e->data.ctlSelect.controlID == btnSlotGoogleSignIn) {
			if (FrmAlert(altGoogleProductionWarning) == 0) {
				/* OAuthRelayRunGoogleSignIn's own failure paths already show
				 * their own alert (WiRSa unreachable, malformed reply, sign-in
				 * timed out) - a silent cancel (user tapped Cancel) gets no
				 * alert either way, correctly. Only the SUCCESS path was
				 * silent - nothing told the user the token was actually
				 * saved, indistinguishable from it quietly doing nothing. */
				if (OAuthRelayRunGoogleSignIn(&gWorking, gSlotIdx)) {
					UpdateGoogleSignInButtonLabel(true);	/* reflect the new state now, not just next time this dialog opens */
					FrmCustomAlert(altSyncOk, "Signed in to Google successfully.", NULL, NULL);
				}
			}
			handled = true;
		} else if (e->data.ctlSelect.controlID == chkSlotSecretShow ||
				   e->data.ctlSelect.controlID == chkSlotICloudSecretShow) {
			/* Both groups' checkboxes drive the ONE shared gSecretShowPlain
			 * (see that variable's own comment) - kept in sync with each
			 * other even though only one is ever visible at a time, so
			 * switching Type later shows the checkbox in the right state
			 * without needing its own separate memory. */
			UInt16 tappedId = e->data.ctlSelect.controlID;
			UInt16 activeField = (tappedId == chkSlotSecretShow) ? fldSlotSecret : fldSlotICloudSecret;
			gSecretShowPlain = (Boolean) CtlGetValue((ControlType *) ObjP(tappedId));
			CtlSetValue((ControlType *) ObjP(tappedId == chkSlotSecretShow ? chkSlotICloudSecretShow : chkSlotSecretShow),
				gSecretShowPlain);
			/* Real gap found 2026-09-18 (user report: "Show" checkbox did
			 * nothing for an ALREADY-SAVED secret, only worked while
			 * actively typing a new one): still showing
			 * cddvSecretPlaceholder means there's nothing in gSecretPlain
			 * to reveal yet - decrypt the real saved secret ON DEMAND here
			 * (CDDVDecryptSlotSecret, the exact same call SyncEngine.c
			 * already uses to decrypt for sending to WiRSa - not a new
			 * decrypt path) rather than leaving Show permanently unable to
			 * do anything for a secret that was saved in an earlier
			 * session. Sec35-locked or nothing-saved-yet both correctly
			 * leave gSecretPlain empty and the checkbox reverted - nothing
			 * to show either way. */
			if (gSecretPlaceholderShown) {
				if (gSecretShowPlain) {
					if (CDDVDecryptSlotSecret(&gWorking, gSlotIdx, gSecretPlain, sizeof(gSecretPlain))) {
						gSecretPlaceholderShown = false;
						RedrawMaskedSecret(activeField);
					} else {
						gSecretShowPlain = false;
						CtlSetValue((ControlType *) ObjP(chkSlotSecretShow), false);
						CtlSetValue((ControlType *) ObjP(chkSlotICloudSecretShow), false);
						FrmCustomAlert(altErr, "Could not decrypt the saved password/token - Sec35 may be locked.", NULL, NULL);
					}
				}
				/* Unchecking while still on the placeholder: nothing to
				 * redraw, cddvSecretPlaceholder itself never changes. */
			} else {
				RedrawMaskedSecret(activeField);
			}
			handled = true;
		}
		break;

	/* Every keystroke while either secret field has focus goes through
	 * SecretFieldKeyDown (masked-by-default entry - see that function's
	 * own comment) instead of the Field Manager's default handling, which
	 * would insert the raw character in the clear. */
	case keyDownEvent: {
		UInt16 focusIdx = FrmGetFocus(gFrm);
		UInt16 focusId = (focusIdx != noFocus) ? FrmGetObjectId(gFrm, focusIdx) : 0;
		if (focusId == fldSlotSecret || focusId == fldSlotICloudSecret) {
			handled = SecretFieldKeyDown(focusId, e);
		}
		break;
	}

	default:
		break;
	}
	return handled;
}

SEC001 Boolean SlotEditFormRun(CDDVSlot *s, UInt8 slotIdx)
{
	FormPtr prevFrm = FrmGetActiveForm();
	EventType event;
	Boolean done = false;
	Boolean ok = false;
	UInt16 uerr;

	gWorking = *s;
	gSlotIdx = slotIdx;
	DecryptWorkingFields();

	gFrm = FrmInitForm(frmSlotEdit);
	if (!gFrm) return false;
	FrmSetActiveForm(gFrm);

	LoadIntoForm();
	FrmSetFocus(gFrm, FrmGetObjectIndex(gFrm, fldSlotAlias));

	while (!done) {
		EvtGetEvent(&event, evtWaitForever);
		if (event.eType == appStopEvent) {
			EvtAddEventToQueue(&event);
			break;
		}
		/* Paste and Cut (mnuDlgEdit's own IDs 10003/10001, MainEn.rcp) must
		 * be caught HERE, before SysHandleEvent gets a chance to
		 * auto-handle them via FldPaste()/FldCut() directly on the focused
		 * field - see PasteIntoSecretField/CutSecretField's own comments
		 * for the real bugs this fixes (user reports: "pasting a
		 * password... doesn't work", and Cut has the identical gap). Only
		 * intercepted while focus is actually on one of the two secret
		 * fields - Cut/Paste into any other field (Alias, User, etc.)
		 * falls through to SysHandleEvent's own normal auto-handling
		 * unchanged. */
		if (event.eType == menuEvent && (event.data.menu.itemID == 10003 || event.data.menu.itemID == 10001)) {
			UInt16 focusIdx = FrmGetFocus(gFrm);
			UInt16 focusId = (focusIdx != noFocus) ? FrmGetObjectId(gFrm, focusIdx) : 0;
			if (focusId == fldSlotSecret || focusId == fldSlotICloudSecret) {
				if (event.data.menu.itemID == 10003) PasteIntoSecretField(focusId);
				else CutSecretField(focusId);
				continue;
			}
		}
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, &uerr)) continue;
		if (SlotEditHandleEvent(&event)) continue;

		/* Relabeled OK->Save (user request, 2026-09-18) - same ID/behavior,
		 * label-only change (see MainEn.rcp's own comment). */
		if (event.eType == ctlSelectEvent && event.data.ctlSelect.controlID == btnSlotEditOK) {
			SaveFromForm();
			ok = true;
			done = true;
			continue;
		}
		/* Delete (user request, 2026-09-18) - confirms first (explicitly
		 * noting event details are also removed, per that same request),
		 * then resets this slot to unconfigured (gWorking zeroed wholesale
		 * - same "wipe to empty" gWorking gets on Cancel anyway via the
		 * cleanup below, just written back to *s this time since ok=true)
		 * AND wipes its own synced DateBook35DAV.pdb entries - same real
		 * mechanism MainForm.c's own "Clear DB" button uses
		 * (DAVDbBeginSlotRebuild+CommitSlotRebuild with nothing added back
		 * in between). */
		if (event.eType == ctlSelectEvent && event.data.ctlSelect.controlID == btnSlotEditDelete) {
			if (FrmCustomAlert(altConfirmDeleteSlot, gAliasPlain[0] ? gAliasPlain : "(unnamed)", NULL, NULL) == 0) {
				if (DAVDbBeginSlotRebuild(gSlotIdx) == errNone)
					DAVDbCommitSlotRebuild();
				MemSet(&gWorking, sizeof(gWorking), 0);
				ok = true;
				done = true;
			}
			continue;
		}
		if (event.eType == ctlSelectEvent && event.data.ctlSelect.controlID == btnSlotEditCancel) {
			done = true;
			continue;
		}
		FrmHandleEvent(gFrm, &event);
	}

	FrmEraseForm(gFrm);
	FrmDeleteForm(gFrm);
	gFrm = NULL;

	if (prevFrm) {
		FrmSetActiveForm(prevFrm);
		FrmDrawForm(prevFrm);
	}

	if (ok) *s = gWorking;
	MemSet(&gWorking, sizeof(gWorking), 0);	/* don't leave a plaintext-adjacent copy sitting in memory longer than needed */
	MemSet(gSecretPlain, sizeof(gSecretPlain), 0);	/* same reasoning - the typed-password shadow buffer */
	/* Same reasoning, for every other field's own plaintext shadow buffer. */
	MemSet(gServerPlain, sizeof(gServerPlain), 0);
	MemSet(gPathPlain, sizeof(gPathPlain), 0);
	MemSet(gUuidPlain, sizeof(gUuidPlain), 0);
	MemSet(gUserPlain, sizeof(gUserPlain), 0);
	MemSet(gAliasPlain, sizeof(gAliasPlain), 0);
	MemSet(gGoogleClientIdPlain, sizeof(gGoogleClientIdPlain), 0);
	MemSet(gGoogleClientSecretPlain, sizeof(gGoogleClientSecretPlain), 0);
	return ok;
}
