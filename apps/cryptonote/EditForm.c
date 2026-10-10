#include "EditForm.h"
#include "Database.h"
#include "Utils.h"
#include "AesCrypt.h"
#include "CardCrypt.h"
#include "MainRsc.h"
#include "MainForm.h"		/* MainInvalidateCardDir */
#include "Beam.h"
#include "CryptoSec35.h"

#include <System/Graffiti.h>

static void EditFormInit(void);
static Boolean EditValidateFormat(void);

static void EditRetrieveData(FieldPtr);
static void EditRetrieveCryptoPadData(FieldPtr);
static void EditRetrieveMemoPadData(FieldPtr);
static void EditRetrieveCardFile(FieldPtr);
static void EditSaveCardFile(FieldPtr) AES_SECTION;
static void EditCardEncryptChoose(void);
static void EditCardDeleteFile(FieldPtr);
static void EditCardImportToCrypto(FieldPtr) AES_SECTION;

/* Draw the key indicator at the bottom-right (left of the Graffiti shift
 * indicator) when an encrypted card file is open. Called after FrmDrawForm and
 * when encryption is chosen; a bare form draw (no key) is enough to hide it. */
static void EditDrawCardKey(void)
{
	if (gCardFileOpen && (gCardEncrypted || gCardWasEncrypted)) {
		MemHandle h = DmGetResource(bitmapRsc, bmpMainKeyBitmap);
		if (h) {
			BitmapPtr b = (BitmapPtr) MemHandleLock(h);
			WinDrawBitmap(b, 125, 148);
			MemHandleUnlock(h);
			DmReleaseResource(h);
		}
	}
}

/* Card-file edit bridge state (declared in EditForm.h). */
Boolean   gCardFileOpen = false;
UInt16    gCardEditVol;
char      gCardEditPath[cardMaxPath];
char      gCardEditName[cardMaxName];
Boolean   gCardEncrypted = false;
Boolean   gCardWasEncrypted = false;
Boolean   gCardDeletePlain = false;
char      gCardPassword[cardMaxPassword];

/* Dirty-check snapshot of the file as loaded (EditRetrieveCardFile), so a
 * plain "opened it, looked at it, closed it" round-trip doesn't pay for a
 * real card write (and the resulting cache invalidation - see
 * MainInvalidateCardDir) when nothing actually changed. A 32-bit checksum
 * isn't collision-proof, but a false "unchanged" here only costs a missed
 * fast path, never data loss - a real edit almost always changes the length
 * too. */
static UInt32 gCardOrigLen = 0;
static UInt32 gCardOrigSum = 0;

static UInt32 EditCardChecksum(const char *p, UInt32 len) AES_SECTION;
static UInt32 EditCardChecksum(const char *p, UInt32 len)
{
	UInt32 h = 2166136261UL;
	UInt32 i;
	for (i = 0; i < len; i++) {
		h ^= (UInt8) p[i];
		h *= 16777619UL;
	}
	return h;
}
MemHandle gCardPreloadH = NULL;
UInt32    gCardPreloadLen = 0;

static void EditSaveData(FieldPtr);
static void EditSaveCryptoPadData(Char *);
static void EditSaveMemoPadDataInCryptoPad(Char *);

/* Diagnostic (temporary -- root-causing an intermittent "Encrypt Memo
 * silently does nothing" report, 2026-08-14): set true the moment "Encrypt
 * Memo" is chosen and the password prompt pops; if EditSaveCryptoPadData is
 * later reached with this still true but currentPasswordP already back to
 * 0, that proves the password got lost somewhere between PasswordForm's OK
 * and the save -- neither of the two encrypt-time diagnostics already added
 * (EncryptRecord's own format-check alert, and the CryptoRecordWrapped
 * still-sealed check) can catch that, since both live INSIDE the
 * `currentPasswordP != 0` block that would never be entered. */
static Boolean gEncryptMemoRequested = false;

static void EditFormDisplayMaxSizeLabel(void);
static Err EditSortMemo(DmOpenRef, UInt16*);

static void EditFormUpdateScrollBar(void);
static void EditFormScrollLines(Int16, Boolean);
static void EditFormScrollPage(WinDirectionType);

static void EditSelectCategory(void);

static Boolean EditFormUpdateDisplay(Int16);
static void EditFormSetTitle (void);

static Boolean EditFormHandleMenuEvent(UInt16);

/* Set by EditRetrieveData whenever the just-loaded CryptoPad record is
 * already at/beyond kCryptoRecordEditWarnLen -- viewing it is always
 * allowed (these are NOT open-time gates), but the field is made
 * non-editable and any actual keystroke attempt shows altRecordTooLargeToEdit
 * once (gEditTooLargeWarned) instead of silently doing nothing. Reset on
 * every record (re)load, including category/record-switch reloads that
 * also go through EditRetrieveData. */
static Boolean gEditRecordTooLargeToEdit = false;
static Boolean gEditTooLargeWarned = false;

/******************************************
 * EditFormInit:
 ******************************************
 */

static void EditFormInit(void)
{
	FormPtr formP;
	FieldPtr fieldP;
	FieldAttrType fieldAttr;
	UInt32 recordID;

	formP = FrmGetActiveForm();
	fieldP = FrmGetObjectPtr(formP,
	FrmGetObjectIndex(formP, fldEditText));
	
	/* Show or hide the lines of the edit field
	* according to the global variable set in
	* the Preferences form.
	*/
	FldGetAttributes(fieldP, &fieldAttr);
	
	if (showLines == linesHidden)
		fieldAttr.underlined = noUnderline;
	else fieldAttr.underlined = grayUnderline;

	FldSetAttributes (fieldP, &fieldAttr);

	/* Attaches the correct menu bar depending upon the currentPasswordP --
	 * or, for a card file (which has no currentPasswordP of its own), its
	 * own gCardEncrypted flag. Same "Remove Password" swap either way. */

	if (currentPasswordP != 0 || (gCardFileOpen && gCardEncrypted))
		FrmSetMenu(formP, mnuEditMenuNoPwd);

	/* Updates the currently selected font */

	if (useSingleFont == separateFont && !gCardFileOpen) {
		DmRecordInfo (currentDBRef, currentRecord, 0, &recordID, 0);
		SetOrGetRecordFont(currentDBRef, recordID, getFont, fontField);
	}

	FldSetFont(fieldP, currentFieldFont);

	EditRetrieveData(fieldP);

	currentForm = frmEditForm;
}

/******************************************
 * EditValidateFormat:
 * Checks that the memo contains at least
 * two lines to perform encryption.
 ******************************************
 */

static Boolean EditValidateFormat(void)
{
	FieldPtr fieldP;
	Char * textP;

	UInt16 nOffset;
	UInt32 textLen32;

	fieldP = GetActiveFormObjectPtr(fldEditText);
	textP = FldGetTextPtr(fieldP);

	/* SafeStrLen, not StrLen -- same bug as EncryptRecord: StrLen returns
	 * Int16 (max 32767), and comparing a small UInt16 nOffset against a
	 * NEGATIVE StrLen result for a >32767-byte memo made this ALWAYS
	 * return false, so "Encrypt Memo" was silently refused as "invalid
	 * format" for any such record. */
	textLen32 = SafeStrLen(textP);

	/* Finds the offset to start encryption */

	nOffset = 0;
	while (nOffset <= textLen32) {
		if (textP[nOffset++] == '\n') break;
	}

	return (nOffset < textLen32);
}

/******************************************
 * EditRetrieveCryptoPadData:
 * Get the data from the current record
 * and start to edit in place.
 ******************************************
 */

static void EditRetrieveCryptoPadData(FieldPtr fieldP)
{
	MemHandle handle;
	PackedRecordPtr packedP;
	RecordType record;

	UInt16 nOffset, nLength;

	if (DmNumRecords(dbCryptoPadRef) > 0) {

		/* Db35-suite-style Sec35 wrap: if this record is Sec35-wrapped, the
		 * raw DB bytes are ciphertext and can't be edited in place like the
		 * plain case below -- CryptoEditRetrieveWrapped decrypts into a
		 * scratch copy and takes over instead (kept in CryptoSec35.c/sec001,
		 * not here -- this file's own segment is critically tight; it also
		 * covers its own decrypt with a busy indicator when the record is
		 * actually wrapped, since that decrypt can take a noticeable moment
		 * for a large memo). */
		if (CryptoEditRetrieveWrapped (dbCryptoPadRef, currentRecord, fieldP))
			return;

		handle = DmGetRecord(dbCryptoPadRef, currentRecord);

		/* Get the text length */

		packedP = MemHandleLock(handle);
		UnpackRecord(&record, packedP);
		nOffset = sizeof(record.flag) +
					sizeof(record.passwdLen) +
					sizeof(record.textLen) +
					sizeof(record.pwIV) +
					sizeof(record.bodyIV) +
					sizeof(record.kdfSalt) +
					sizeof(record.kdfRounds) +
					record.passwdLen + 1;
		nLength = (UInt16) SafeStrLen(record.textP) + 1;
		MemHandleUnlock(handle);

		/* Start editing the record in place */

		FldSetText(fieldP, handle, nOffset, nLength);
	}
}


/******************************************
 * EditRetrieveMemoPadData:
 * Get the data from the current record
 * and start to edit in place.
 ******************************************
 */

static void EditRetrieveMemoPadData(FieldPtr fieldP)
{
	MemHandle handle;

	if (DmNumRecords(dbMemoPadRef) > 0) {
		handle = DmGetRecord(dbMemoPadRef, currentRecord);
		FldSetTextHandle(fieldP, handle);
	}
}

/******************************************
 * EditRetrieveData:
 * Get the data from the current record
 * and start to edit in place.
 ******************************************
 */

/******************************************
 * EditRetrieveCardFile:
 * Load the current card file's bytes into
 * the edit field (editable in place). Hides
 * the DB-only controls and titles the form
 * with the file name.
 ******************************************
 */
static void EditRetrieveCardFile(FieldPtr fieldP)
{
	FormPtr  formP = FrmGetActiveForm();
	MemHandle h = gCardPreloadH;     /* MainForm supplies the (decrypted) text */
	UInt32   size = gCardPreloadLen;
	Char    *p;

	gCardPreloadH = NULL;            /* ownership transfers to the field         */
	gCardPreloadLen = 0;

	/* A card file has no category/details -- hide those controls. */
	FrmHideObject(formP, FrmGetObjectIndex(formP, popEditCategory));
	FrmHideObject(formP, FrmGetObjectIndex(formP, btnEditDetails));

	if (!h) { h = MemHandleNew(1); size = 0; }   /* empty editable buffer */
	if (MemHandleResize(h, size + 1) == errNone) {
		p = MemHandleLock(h);        /* NUL-terminate for the field package */
		p[size] = 0;
		MemHandleUnlock(h);
	}

	gCardOrigLen = size;
	p = MemHandleLock(h);
	gCardOrigSum = EditCardChecksum(p, size);
	MemHandleUnlock(h);

	FldSetTextHandle(fieldP, h);
	FrmCopyTitle(formP, gCardEditName);
	FldSetScrollPosition(fieldP, 0);
	EditFormUpdateScrollBar();
	/* the key indicator is drawn in the frmOpen/frmGoto handlers after FrmDrawForm */
}

/******************************************
 * EditSaveCardFile:
 * Write the edited text back to the card
 * file, then detach and free the field's
 * handle. Clears the card-edit bridge.
 ******************************************
 */
static void EditSaveCardFile(FieldPtr fieldP)
{
	MemHandle h;
	Char *textP = FldGetTextPtr(fieldP);
	/* SafeStrLen, not StrLen -- StrLen returns Int16 (ROM trap, wraps past
	 * 32767 bytes) and `len` here is UInt32, so a >32KB card file would
	 * sign-extend to a huge garbage length and corrupt the write. Same bug
	 * class as CryptoImportText's "Import Failed". */
	UInt32 len  = textP ? SafeStrLen(textP) : 0;
	char  encPath[cardMaxPath];
	Boolean writeEnc;

	/* Nothing to write back if the text is byte-identical to what was loaded
	 * AND the encryption choice didn't change (encrypt/decrypt toggles still
	 * need a real write even with unchanged text - they rename the file).
	 * Skips the SD-card write itself, not just the cache invalidation below -
	 * a plain "opened it, looked at it, closed it" round-trip pays for
	 * neither. See gCardOrigLen/gCardOrigSum's own comment. */
	if (len == gCardOrigLen && gCardEncrypted == gCardWasEncrypted &&
		EditCardChecksum(textP ? textP : "", len) == gCardOrigSum)
		goto cardSaveDone;

	MainInvalidateCardDir();	/* about to write the card - the cached listing is stale */

	StrCopy(encPath, gCardEditPath);
	StrCat(encPath, ".enc");

	/* An encrypted file re-encrypts automatically with its existing password; a
	 * plaintext file encrypts only if "Encrypt Memo" was chosen. gCardEncrypted
	 * already reflects this, so no dialog runs here (and none may, during
	 * frmClose - it would corrupt the return-to-list redraw). */
	writeEnc = gCardEncrypted;

	if (writeEnc && gCardPassword[0]) {
		MemHandle blobH; UInt32 blobSize;
		WinHandle busy = CardBusyShow("Encrypting...");
		Err cerr = CardCryptEncrypt(textP ? textP : "", len, gCardPassword,
		                            &blobH, &blobSize);
		CardBusyHide(busy);
		if (cerr == errNone) {
			void *blob;

			/* CardCryptEncrypt (above) only does the in-RAM encryption --
			 * the actual VFS write of the (potentially large) encrypted
			 * blob, plus deleting the plaintext original, still follows
			 * here with no feedback of its own. For a large file, VFS
			 * writes to a real SD card are the slowest part of this whole
			 * operation -- confirmed as a real silent hang (no dialog at
			 * all) between "Encrypting..." closing and control finally
			 * returning to the list. Cover it with its own busy indicator,
			 * matching the plain-file "Closing..." convention below. */
			busy = CardBusyShow("Closing...");
			blob = MemHandleLock(blobH);
			CardWriteFile(gCardEditVol, encPath, blob, blobSize);
			MemHandleUnlock(blobH);
			MemHandleFree(blobH);
			/* remove the plaintext original only if the user asked (Encrypt Memo) */
			if (gCardDeletePlain && CardExists(gCardEditVol, gCardEditPath))
				CardDeleteFile(gCardEditVol, gCardEditPath);
			CardBusyHide(busy);
		}
	} else {
		WinHandle busy = CardBusyShow("Closing...");
		CardWriteFile(gCardEditVol, gCardEditPath, textP ? textP : "", len);
		/* drop the .enc sibling ONLY when deliberately decrypting a file that was
		 * opened encrypted (the "No" path) - never when just editing a plaintext
		 * file that happens to have a kept encrypted sibling. Kept under the same
		 * busy indicator as the write above, not just the write alone. */
		if (gCardWasEncrypted && CardExists(gCardEditVol, encPath))
			CardDeleteFile(gCardEditVol, encPath);
		CardBusyHide(busy);
	}

cardSaveDone:
	FldReleaseFocus(fieldP);
	FldCompactText(fieldP);
	h = FldGetTextHandle(fieldP);
	FldSetTextHandle(fieldP, 0);
	if (h) MemHandleFree(h);

	MemSet(gCardPassword, sizeof(gCardPassword), 0);   /* don't linger in RAM */
	gCardFileOpen = false;
	currentForm = frmMainForm;
}

/* "Encrypt Memo" on a plaintext card file: prompt a password, mark the file to
 * be saved encrypted, and delete the plaintext original on save. No-op if the
 * file is already encrypted. (An originally-encrypted file just re-encrypts with
 * its existing password on close - no prompt.) */
static void EditCardEncryptChoose(void)
{
	char pw[cardMaxPassword];

	if (gCardWasEncrypted || gCardEncrypted) return;
	/* Phase 5a: Sec35 gate -- EditSaveCardFile (the function that actually
	 * runs CardCryptEncrypt) can't gate itself, it fires during frmClose
	 * where no dialog may run. Gate here instead, before the encrypt
	 * CHOICE is even committed -- once gCardEncrypted is set, encryption
	 * happens silently on save with no further prompts. */
	if (CryptoCardLockedBounce()) return;
	if (CardPromptPassword(pw, sizeof(pw))) {
		StrCopy(gCardPassword, pw);
		gCardEncrypted = true;
		gCardDeletePlain = true;                 /* always replace the plaintext */
		EditDrawCardKey();                        /* title stays bare; show the key */
	}
	MemSet(pw, sizeof(pw), 0);
}

/* Delete the currently-open card file (both its plain and ".enc" forms --
 * gCardEditPath is always the BASE path without ".enc", see
 * MainFormOpenCardFile, so only ONE of the two ever actually exists;
 * CardDeleteFile on the other is a harmless no-op). Confirms first (reuses
 * altConfirmDeleteAlert, the same generic confirm used elsewhere). No
 * Sec35 gate needed here -- deleting isn't a crypto operation, and if the
 * file was encrypted the user already had to pass the Phase 5a gate (and
 * enter its password) to open it in the first place. */
static void EditCardDeleteFile(FieldPtr fieldP)
{
	char path[cardMaxPath];
	MemHandle h;

	if (FrmAlert(altConfirmDeleteAlert) == 1) return;   /* 1 = Cancel */

	MainInvalidateCardDir();	/* deleting the card file - the cached listing is stale */

	StrCopy(path, gCardEditPath);
	if (CardExists(gCardEditVol, path))
		CardDeleteFile(gCardEditVol, path);
	StrCat(path, ".enc");
	if (CardExists(gCardEditVol, path))
		CardDeleteFile(gCardEditVol, path);

	/* Detach + free the field's handle and clear gCardFileOpen BEFORE
	 * navigating away: frmCloseEvent unconditionally calls
	 * EditSaveCardFile when gCardFileOpen is still true, which would
	 * silently resurrect the file we just deleted by writing the field's
	 * (still-live) text back out. Clearing both here makes frmCloseEvent's
	 * save branches no-op, matching EditSaveCardFile's own field-cleanup
	 * sequence. */
	FldReleaseFocus(fieldP);
	h = FldGetTextHandle(fieldP);
	FldSetTextHandle(fieldP, 0);
	if (h) MemHandleFree(h);

	gCardFileOpen = false;
	MemSet(gCardPassword, sizeof(gCardPassword), 0);
	gCardEditName[0] = 0;
	FrmGotoForm(frmMainForm);
}

/* Copy the currently-open card file's content into one or more new
 * CryptoPad DB records (Phase 5b "Import from Card"). Non-destructive --
 * the card file itself is untouched either way. Confirms first, stating
 * the resulting part count (CryptoImportPartCount is a cheap pre-check,
 * no actual work done yet). All the real logic (splitting, sealing,
 * one-part-at-a-time writes) lives in CryptoSec35.c/sec001 -- this stays
 * a thin caller since main is critically tight on code space. */
static void EditCardImportToCrypto(FieldPtr fieldP)
{
	MemHandle textH = FldGetTextHandle(fieldP);
	Char *textP;
	UInt16 parts, created;
	Char countStr[6];

	if (!textH) return;

	/* Pin the field's own text handle for the WHOLE operation, not just a
	 * one-shot FldGetTextPtr() pointer -- CryptoImportText makes large
	 * (tens-of-KB) MemHandleNew allocations internally (per the round-4
	 * fix), and MemHandleNew can trigger heap COMPACTION, which is free to
	 * relocate any UNLOCKED movable block, including this field's own text
	 * handle if its lock count happens to be 0 at that moment. A stale
	 * `textP` from before that relocation would read garbage/freed memory
	 * -- exactly the kind of bug CryptoEditSaveWrapped's own comment
	 * already flags for FldGetTextPtr + FldCompactText. Explicit
	 * MemHandleLock here (nested on top of whatever the Field Manager
	 * itself holds) guarantees the handle cannot move for as long as we're
	 * reading from it, regardless of what CryptoImportText allocates. */
	textP = (Char *) MemHandleLock(textH);

	/* SafeStrLen -- see Utils.c. THE actual bug behind "Import Failed":
	 * StrLen returns Int16 (max 32767) and this is passed to a UInt32
	 * parameter, sign-extending to a huge garbage value for any file over
	 * ~32KB, regardless of how much memory is actually free. */
	parts = CryptoImportPartCount((UInt32) SafeStrLen(textP));
	StrIToA(countStr, parts);
	if (FrmCustomAlert(altCardImportConfirm, countStr, "", "") != 0) {
		MemHandleUnlock(textH);
		return;		/* Cancel */
	}

	{
	WinHandle busy = CardBusyShow("Importing...");
	created = CryptoImportText(textP, gCardEditName, currentCategory);
	CardBusyHide(busy);
	}

	MemHandleUnlock(textH);

	/* Explicit success/failure feedback -- a silent 0-record import
	 * (e.g. DmNewRecord running out of storage partway through) used to
	 * leave no visible trace at all, impossible to tell apart from "it
	 * worked but I can't find the memo". */
	if (created == parts) {
		StrIToA(countStr, created);
		FrmCustomAlert(altCardImportComplete, countStr, "", "");
	} else {
		StrIToA(countStr, created);
		FrmCustomAlert(altCardImportFailed, countStr, "", "");
	}
}

static void EditRetrieveData(FieldPtr fieldP)
{
	FormPtr formP;
	ControlPtr triggerP;
	UInt16 recAttributes;

	/* Reset here unconditionally -- covers the card-file path below (which
	 * returns early) and a brand-new blank record, neither of which is ever
	 * "too large to edit"; the CryptoPad branch further down overrides this
	 * when the just-loaded record actually is. */
	gEditRecordTooLargeToEdit = false;
	gEditTooLargeWarned = false;

	if (gCardFileOpen) { EditRetrieveCardFile(fieldP); return; }

	/* DB records own the category popup + Details button; make sure they are
	 * visible again after a card file (which hides them) was edited. */
	{
		FormPtr fp = FrmGetActiveForm();
		FrmShowObject(fp, FrmGetObjectIndex(fp, popEditCategory));
		FrmShowObject(fp, FrmGetObjectIndex(fp, btnEditDetails));
	}

	if (currentRecord != noRecordSelected) {
		if (currentDB == memoPadDB) {
			EditRetrieveMemoPadData (fieldP);
			EditFormDisplayMaxSizeLabel();

			/* Field object is reused across record-switch reloads -- undo
			 * any read-only lock left over from a too-large record
			 * previously loaded into this same field. */
			CryptoEditClearGuard (fieldP);
		} else {
			EditRetrieveCryptoPadData (fieldP);

			/* Viewing a large record is always fine -- only an attempted
			 * EDIT of one risks the "DmWriteCheck failed" fatal error (see
			 * kCryptoRecordCeiling's comment in CryptoSec35.c).
			 * CryptoEditApplyGuard makes the field read-only when the
			 * record is too large (also handles the same field-reuse
			 * concern for the normal-sized case); EditFormHandleEvent's
			 * keyDownEvent case shows the alert on the first actual
			 * attempt via gEditTooLargeWarned. */
			gEditRecordTooLargeToEdit =
				CryptoEditApplyGuard (dbCryptoPadRef, currentRecord, fieldP);
			gEditTooLargeWarned = false;
		}

		formP = FrmGetActiveForm();
	
		triggerP = FrmGetObjectPtr(formP,
				FrmGetObjectIndex(formP, popEditCategory));

		/* Updates the currently selected category */
		DmRecordInfo(currentDBRef, currentRecord, &recAttributes, 0, 0);
		currentCategory = (recAttributes & dmRecAttrCategoryMask);

		// If the record is secret and secret records are hidden, then the record isn't
		// accounted for by MemosInCategory.  Adjust it, and EditFormSaveData will 
		// adjust when done with the record.
		if ((PrivateRecordVisualStatus == hidePrivateRecords) && (recAttributes & dmRecAttrSecret))
			memosInCategory++;

		// Set the edit form title
		EditFormSetTitle ();

		CategoryGetName(dbMemoPadRef, currentCategory, categoryName);
		CategorySetTriggerLabel(triggerP, categoryName);

		FldSetScrollPosition (fieldP, editScrollPosition);
		EditFormUpdateScrollBar();	
	}
}

/******************************************
 * EditSaveCryptoPadData:
 * Saves the data of the current record
 * in the CryptoPad database, and remove 
 * the record from MemoPadDB.
 ******************************************
 */

static void EditSaveCryptoPadData(Char * textP)
{
	MemHandle handle;
	PackedRecordPtr packedP;
	UInt16 nOffset, nLength;

	/* The global password no longer replaces the per-record password; it is
	 * folded into the key derivation as an extra factor inside EncryptRecord.
	 * currentPasswordP holds the record's own password (entered via the
	 * Password form). */

	/* Db35-suite-style Sec35 wrap: if this edit session was against a
	 * decrypted scratch copy (Sec35-wrapped record), CryptoEditSaveWrapped
	 * rebuilds + re-seals storage and returns true. Otherwise (unwrapped,
	 * or no passcode active) fall through to the existing in-place
	 * length-fixup, unchanged. */
	if (! CryptoEditSaveWrapped (dbCryptoPadRef, currentRecord, textP)) {
		handle = DmGetRecord(dbCryptoPadRef, currentRecord);
		packedP = MemHandleLock(handle);

		nOffset = sizeof(packedP->flag) + sizeof(packedP->passwdLen);
		nLength = StrLen(textP);
		DmWrite(packedP, nOffset, &nLength, sizeof(nLength));

		MemHandleUnlock(handle);
		DmReleaseRecord(dbCryptoPadRef, currentRecord, true);
	}

	/* Encrypt the record if the password is not empty */
	if (currentPasswordP != 0) {
		/* Diagnostic + safety guard (temporary -- root-causing an
		 * intermittent "Encrypt Memo silently does nothing" report): if
		 * CryptoEditSaveWrapped (above) failed to unwrap+rebuild the
		 * record as plain bytes first (its own early-return cases: not
		 * migrated, or its gCryptoEditH scratch copy was never
		 * populated), EncryptRecord below would misparse the STILL
		 * Sec35-wrapped ciphertext as a plain PackedRecordType -- garbage
		 * offsets, garbage record.textP, and (worst case) it "encrypting"
		 * that garbage and marking the record recEncrypted would corrupt
		 * it irrecoverably rather than just no-op. Alert AND skip the
		 * call rather than let it run against an unsafe record. */
		if (CryptoRecordWrapped (dbCryptoPadRef, currentRecord)) {
			FrmAlert (altInvalidFormatAlert);
			RemoveCurrentPassword();
		} else {
			WinHandle busy = CardBusyShow("Encrypting...");
			EncryptRecord(currentRecord, currentPasswordP);
			CardBusyHide(busy);
			RemoveCurrentPassword();
		}
		gEncryptMemoRequested = false;
	}
	/* Diagnostic (see gEncryptMemoRequested's declaration): reaching here
	 * with the request flag still set means "Encrypt Memo" was chosen and
	 * the password prompt was answered, yet currentPasswordP was ALREADY 0
	 * by the time this save ran -- the password/request was lost somewhere
	 * between PasswordForm's OK and here, before either of the two checks
	 * above ever got a chance to run. */
	else if (gEncryptMemoRequested) {
		FrmAlert (altInvalidFormatAlert);
		gEncryptMemoRequested = false;
	}

	/* Db35-suite-style Sec35 wrap: re-seal after whichever plain write just
	 * happened above -- also covers a brand-new record being saved for the
	 * first time while a passcode is active. */
	CryptoSealRecord (dbCryptoPadRef, currentRecord);
}

/******************************************
 * EditSaveMemoPadDataInCryptoPad:
 * Saves the data of the current record
 * in the CryptoPad database, and remove 
 * the record from MemoPadDB.
 ******************************************
 */

static void EditSaveMemoPadDataInCryptoPad(Char * textPtr)
{
	MemHandle handle;
	RecordType newRecord;

	UInt16 recMemoAttributes;	
	UInt16 recCryptoAttributes;
	UInt16 index;
	UInt32 memoRecordID;

	PackedRecordPtr packedP;
	UInt16 nOffset, textLength, passwordLength;

	/* Creates a new record at the end of the database */
	index = currentRecord;

	currentRecord = dmMaxRecordIndex;
	currentDB = cryptoPadDB;

	handle = DmNewRecord(dbCryptoPadRef, &currentRecord, sizeof(PackedRecordType));
	ErrFatalDisplayIf(handle == 0, "Unable to add record to the database.");

	/* Current record now contains the record index.
	 * Sets an initial value for the record.
	 */

	passwordLength = StrLen(currentPasswordP);
	textLength = StrLen(textPtr);

	newRecord.flag      = recDecrypted;
	newRecord.passwdLen =  passwordLength;
	newRecord.textLen   = textLength;
	MemSet(newRecord.pwIV, sizeof(newRecord.pwIV), 0);
	MemSet(newRecord.bodyIV, sizeof(newRecord.bodyIV), 0);
	MemSet(newRecord.kdfSalt, sizeof(newRecord.kdfSalt), 0);
	newRecord.kdfRounds = 0;
	newRecord.passwordP = currentPasswordP;
	newRecord.textP = textPtr;

	PackRecord(&newRecord, handle);
	
	/* Set the category of the record */
	DmRecordInfo(dbMemoPadRef, index, &recMemoAttributes, &memoRecordID, 0);
	
	DmReleaseRecord(dbCryptoPadRef, currentRecord, true);

	DmSetRecordInfo(dbCryptoPadRef, currentRecord, &recMemoAttributes, 0);
	
	if (useSingleFont == separateFont) {
		UInt32 cryptoRecordID;

		DmRecordInfo (dbCryptoPadRef, currentRecord, 0, &cryptoRecordID, 0);
		CreateFontRecord(dbCryptoPadRef, cryptoRecordID);
		SetOrGetRecordFont(dbCryptoPadRef, cryptoRecordID, setFont, fontField);
	}

	/* Delete the record from MemoPadDB */
	DmDeleteRecord (dbMemoPadRef, index);
	DmMoveRecord (dbMemoPadRef, index, DmNumRecords (dbMemoPadRef));

	/* Encrypt the record if the password is not empty */
	{
		WinHandle busy;
		busy = CardBusyShow("Encrypting...");
		EncryptRecord(currentRecord, currentPasswordP);
		CardBusyHide(busy);
	}
	RemoveCurrentPassword();

	/* Db35-suite-style Sec35 wrap: this just created a brand-new CryptoPad
	 * record (converted from a MemoPad one) -- seal it too, same as any
	 * other freshly-written record. */
	CryptoSealRecord (dbCryptoPadRef, currentRecord);
}


/******************************************
 * EditSaveData:
 * Saves the data to the current record
 * and release the record.
 ******************************************
 */

static void EditSaveData(FieldPtr fieldP)
{
	Char * textP;

	UInt16 recAttributes;
	UInt32 recordID;

	Boolean dirty;
	Boolean empty;

	textP = FldGetTextPtr(fieldP);

	// Find out if the field has been modified.
	dirty = FldDirty (fieldP);
	empty = (*textP == '\0');

	FldReleaseFocus (fieldP);

	// Release any free space in the memo field.
	FldCompactText(fieldP);

	// Clear the handle value in the field, otherwise the handle
	// will be free when the form is disposed of.
	FldSetTextHandle(fieldP, 0);


	// If there's data in an existing record, mark it dirty if 
	// necessary and release it.
	DmRecordInfo (currentDBRef, currentRecord, &recAttributes, &recordID, 0);

	if (!empty) {
		if (PrivateRecordVisualStatus == hidePrivateRecords && (recAttributes & dmRecAttrSecret))
				memosInCategory--;
		DmReleaseRecord(currentDBRef, currentRecord, dirty);
		
		/* Db35-suite-style Sec35 wrap: this USED to demote a CryptoPad
		 * record back into the plain MemoPad DB whenever it was saved
		 * without ALSO setting a NEW layer-2 "Encrypt Memo" password
		 * (currentPasswordP == 0) -- made sense pre-Sec35, when CryptoPad
		 * was only for password-protected notes. Under "encrypted by
		 * default" that's now the OVERWHELMINGLY common case (Sec35
		 * wrapping doesn't use currentPasswordP at all), so this was
		 * silently writing the just-decrypted PLAINTEXT into the
		 * unencrypted MemoPad DB and deleting the CryptoPad record on
		 * every single edit -- a real confidentiality bug (any
		 * Sec35-protected note got un-wrapped just by opening and
		 * re-saving it) and the cause of "records disappear, then show
		 * up as a Memo Pad import prompt" hit during testing. A CryptoPad
		 * record now ALWAYS stays in CryptoPad; EditSaveCryptoPadData
		 * itself already handles both the with/without-layer-2-password
		 * cases internally. */
		if ((currentDB == memoPadDB) && (currentPasswordP != 0)) {
				EditSaveMemoPadDataInCryptoPad(textP);
				dirty = true;
		}
		if (currentDB == cryptoPadDB) EditSaveCryptoPadData(textP);

		// Move the current record to the correct sort position.
		if (dirty)
			EditSortMemo(currentDBRef, &currentRecord);

		if (moveRecordOnTop) {
			DmMoveRecord (currentDBRef, currentRecord, 0);
			currentRecord = 0;
		}
	}

	// If the record is empty, delete it.
	else
		{
		// Delete the record in the Font database.
		if ((useSingleFont == separateFont) 
				&& (currentDB == cryptoPadDB))
			DeleteFontRecord(currentDBRef, recordID);

		if (dirty)
			{
			DmDeleteRecord (currentDBRef, currentRecord);
			DmMoveRecord (currentDBRef, currentRecord, DmNumRecords (currentDBRef));
			}
		else DmRemoveRecord(currentDBRef, currentRecord);

		if (currentPasswordP != 0) RemoveCurrentPassword();

		memosInCategory--;
		currentForm = frmMainForm;
		currentRecord = noRecordSelected;
	}
}

/******************************************
 * EditSortMemo:
 * Sorts the memo in one or the other
 * database according to the current sort Order.
 * This function taken from the
 * 'Memo' 3COM sample application.
 ******************************************
 */

Err EditSortMemo(DmOpenRef dbP, UInt16* indexP)
{

	Err err;
	Int16 sortOrder;
	UInt16 index;
	UInt16 attributes;
	UInt32 uniqueID;
	MemHandle recordH;
	MemHandle h;
	Boolean dontMove;

	MemPtr cmp;
	MemPtr recordP;

	if (currentSortOrder != soAlphabetic)
		return (0);

	/* Check if the record is already in the correct position */
	recordP = MemHandleLock(DmQueryRecord (dbP, *indexP));		

	if (*indexP > 0) {

		/* This record wasn't deleted and deleted records are at the end of the
		 * database so the prior record may not be deleted!
		 */

		h = DmQueryRecord (dbP, *indexP-1);

		if (! h) dontMove = false;
		else {
			cmp = MemHandleLock(h);

			if (dbP == dbMemoPadRef) {
				dontMove = (CompareMemoPadRecords(cmp, recordP,
								(Int16) currentSortOrder) == -1);
			} else {
				dontMove = (CompareCryptoPadRecords((PackedRecordPtr) cmp, 
					(PackedRecordPtr) recordP, (Int16) currentSortOrder) == -1);
			}

			MemPtrUnlock(cmp);
		}

	} else dontMove = true;

	if (dontMove && (*indexP+1 < DmNumRecords (dbP))) {

		DmRecordInfo(dbP, *indexP+1, &attributes, NULL, NULL);
		if ( ! (attributes & dmRecAttrDelete) ) {
			cmp = MemHandleLock(DmQueryRecord (dbP, *indexP+1));
			if (dbP == dbMemoPadRef) {
				dontMove &= (CompareMemoPadRecords(recordP, cmp,
							(Int16) currentSortOrder) == -1);
			} else {
				dontMove &= (CompareCryptoPadRecords((PackedRecordPtr) recordP, 
					(PackedRecordPtr) cmp, (Int16) currentSortOrder) == -1);
			}

			MemPtrUnlock(cmp);
		}
	}

	MemPtrUnlock(recordP);

	if (dontMove) return (0);


	/* Since the routine that determines the records sort position uses a 
	 * binary search algorythm we need to remove the record from the database 
	 * before we can determine its new position.  We will also save and restore the 
	 * record's attributes and unique ID.
	 */

	DmRecordInfo(dbP, *indexP, &attributes, &uniqueID, NULL);

	err = DmDetachRecord(dbP, *indexP, &recordH);
	if (err) return err;
	
	recordP = MemHandleLock(recordH);
	
	if (dbP == dbMemoPadRef) {
		index = DmFindSortPosition(dbP, recordP, 0,
			(DmComparF *) CompareMemoPadRecords, (Int16) currentSortOrder);
	} else {
		index = DmFindSortPosition(dbP, (PackedRecordPtr) recordP, 0,
			(DmComparF *) CompareCryptoPadRecords, (Int16) currentSortOrder);
	}

	MemPtrUnlock(recordP);
	
	err = DmAttachRecord(dbP, &index, recordH, 0);
	if (err) return err;

	DmSetRecordInfo(dbP, index, &attributes, &uniqueID);	

	*indexP = index;
	
	return (err);
}

/******************************************
 * EncryptRecord:
 * Encrypt the current record
 ******************************************
 */

void EncryptRecord(UInt16 recordNum, Char * passwordP)
{
	MemHandle handle;
	PackedRecordPtr packedP;
	RecordType record;
	UInt16 nOffset;
	Boolean handleLocked;

	handle = DmGetRecord(dbCryptoPadRef, recordNum);
	packedP = MemHandleLock(handle);
	handleLocked = true;
	UnpackRecord(&record, packedP);

	/* Finds the offset to start encryption */

	/* SafeStrLen, not StrLen -- StrLen returns Int16 (ROM trap, max 32767)
	 * and a >32767-byte record.textP (now reachable via a large SD-card
	 * import) makes StrLen return NEGATIVE; comparing `nOffset <=
	 * StrLen(...)` against a small positive UInt16 nOffset was then ALWAYS
	 * false, so this loop silently never ran and "Encrypt Memo" became a
	 * silent no-op on any such record. */
	{
	UInt32 textLen32 = SafeStrLen(record.textP);
	nOffset = 0;
	while (nOffset <= textLen32) {
		if (record.textP[nOffset++] == '\n') break;
	}

	if (nOffset < textLen32) {
		RecordType newRecord;
		MemHandle newTextH;

		/* Set the flag to reflect the status of the record */

		newRecord.flag      = recEncrypted;
		newRecord.passwdLen = (UInt16) SafeStrLen(passwordP);
		newRecord.textLen   = (UInt16) textLen32;

		/* Each encryption needs its own fresh, random IV: the same
		 * password (and therefore the same derived AES key) gets
		 * reused across many records via the global password
		 * feature, and the password-verifier blob and the body text
		 * are two different plaintexts encrypted under that same
		 * key, so they each need their own IV too - reusing one
		 * would break AES-CTR's security guarantee. See AesCrypt.h. */

		GenerateRandomIV(newRecord.pwIV);
		GenerateRandomIV(newRecord.bodyIV);

		/* Fresh per-record salt + a work factor calibrated to ~1.5 s, both
		 * stored in the record so the key can be re-derived for decryption.
		 * The derived key also folds in this device's secret and the global
		 * password (if one is set) - see AesCrypt.h. The verifier and body
		 * share salt/rounds, so the ~1.5 s derivation runs only once (the key
		 * cache serves the second call). */

		GenerateRandomIV(newRecord.kdfSalt);
		newRecord.kdfRounds = CalibrateKdfRounds();

		/* Store the encrypted password into the record */

		newRecord.passwordP = MemPtrNew(newRecord.passwdLen + 1);
		AESCryptEx(passwordP, newRecord.passwordP, passwordP, globalPasswordP,
								newRecord.pwIV, newRecord.kdfSalt,
								newRecord.kdfRounds,
								newRecord.passwdLen, AES_ENCRYPT);

		/* Store the encrypted text in the record. MemHandleNew (movable),
		 * not MemPtrNew -- this can be up to ~60000 bytes for a large
		 * imported memo, and a fixed non-relocatable allocation that size
		 * is unreliable on a real device's heap (same class of bug fixed
		 * in CryptoSec35.c's CryptoSealRecord/CryptoImportText). */
		newTextH = MemHandleNew(textLen32 + 1);
		newRecord.textP = (Char*) MemHandleLock(newTextH);
		StrNCopy(newRecord.textP, record.textP, nOffset);
		AESCryptEx(record.textP + nOffset, newRecord.textP + nOffset,
								passwordP, globalPasswordP, newRecord.bodyIV,
								newRecord.kdfSalt, newRecord.kdfRounds,
								newRecord.textLen - nOffset, AES_ENCRYPT);

		/* Replaces the record by the new record.
		 *
		 * ROOT CAUSE (found 2026-08-14 via live POSE64 repro + a chain of
		 * three diagnostic readbacks): `handle` is STILL LOCKED from the
		 * MemHandleLock at the top of this function (that's how
		 * record.textP, just read above, stays valid). PackRecord's own
		 * MemHandleResize(handle, ...) call CANNOT resize a locked handle
		 * -- PalmOS's Memory Manager requires a handle be unlocked before
		 * it can be resized/moved. That resize was silently failing every
		 * time, so PackRecord returned an error and never wrote anything
		 * -- "Encrypt Memo" ran the full AES pass, showed the busy
		 * indicator, and then quietly discarded the result. This is
		 * exactly why the record never ended up marked recEncrypted, on
		 * any memo, every time: CryptoEditSaveWrapped's own PackRecord
		 * call (elsewhere) works precisely because IT never locks the
		 * handle first. record.textP (the only reason `handle` needed to
		 * stay locked) is read for the last time in the AESCryptEx call
		 * just above, so it's safe to unlock right here, before
		 * PackRecord. */
		MemHandleUnlock(handle);
		handleLocked = false;
		PackRecord(&newRecord, handle);

		/* Deallocates memory */

		MemPtrFree(newRecord.passwordP);
		MemHandleUnlock(newTextH);
		MemHandleFree(newTextH);
	}
	/* This mirrors EditFormHandleMenuEvent's EditValidateFormat() gate,
	 * which already required a 2nd line on the LIVE FIELD before ever
	 * popping the password prompt -- reaching here with nOffset >=
	 * textLen32 means the record.textP freshly re-read from the DB (after
	 * the save-before-encrypt sequence in EditSaveCryptoPadData) disagrees
	 * with what the field validated moments earlier. That disagreement is
	 * itself a real bug worth surfacing loudly (found via human testing:
	 * "Encrypt Memo" on the 2nd+ memo in a session silently did nothing,
	 * no error, no crash -- EditValidateFormat's caller shows
	 * altInvalidFormatAlert on its own failure, but this duplicate check
	 * failed SILENTLY, which is why nothing was visible). Alerting here
	 * both fixes the silent-failure UX and gives a concrete signal for
	 * which of the two checks is actually disagreeing, next repro. */
	else {
		FrmAlert(altInvalidFormatAlert);
	}
	}

	/* Release the record to the data manager. The success path above
	 * already unlocked `handle` itself (had to, for PackRecord's resize);
	 * only unlock here if that path wasn't taken. */
	if (handleLocked)
		MemHandleUnlock(handle);
	DmReleaseRecord(dbCryptoPadRef, recordNum, true);
}

/******************************************
 * EditFormUpdateScrollBar:
 * Updates the size and position of
 * the scrollbar.
 ******************************************
 */

static void EditFormUpdateScrollBar(void)
{
	FieldPtr fieldP;
	ScrollBarPtr scrollBarP;

	UInt16 nPosition, nMax;
	UInt16 textHeight, fieldHeight;

	fieldP = GetActiveFormObjectPtr(fldEditText);
	scrollBarP = GetActiveFormObjectPtr(sclEditScrollBar);

	FldGetScrollValues(fieldP, &nPosition, &textHeight, &fieldHeight);

		// On occasion, such as after deleting a multi-line selection of text,
		// the display might be the last few lines of a field followed by some
		// blank lines.  To keep the current position in place and allow the user
		// to "gracefully" scroll out of the blank area, the number of blank lines
		// visible needs to be added to max value.  Otherwise the scroll position
		// may be greater than maxValue, get pinned to maxvalue in SclSetScrollBar
		// resulting in the scroll bar and the display being out of sync.
	if (textHeight > fieldHeight) nMax = textHeight - fieldHeight + FldGetNumberOfBlankLines (fieldP);
	else if (nPosition) nMax = nPosition;
	else nMax = 0;

	SclSetScrollBar(scrollBarP, nPosition, 0, nMax, fieldHeight - 1);
}

/******************************************
 * EditFormScrollLines:
 * Scrolls the text from nLines upwards
 * (negative) or downwards (positive).
 ******************************************
 */

static void EditFormScrollLines(Int16 nLines, Boolean redraw)
{
	FieldPtr fieldP;
	Int16 blankLines;

	fieldP = GetActiveFormObjectPtr(fldEditText);
	blankLines = FldGetNumberOfBlankLines(fieldP);

	if (nLines < 0) FldScrollField(fieldP, -nLines, winUp);
	else FldScrollField(fieldP, nLines, winDown);

	/* If there are blank lines at the end, FldScrollField
	 * makes the blank lines disappear. Therefore, we have
	 * got to update the scrollbar.
	 */

	if (blankLines || redraw) {
		EditFormUpdateScrollBar();
		}
}

/******************************************
 * EditFormScrollPage:
 * Scrolls the text one full page in
 * the given direction.
 ******************************************
 */

static void EditFormScrollPage(WinDirectionType direction)
{
	FieldPtr fieldP;
	Int16 nLines;

	Int16 seekDirection;
	UInt16 category;
	UInt16 recordNum;
	UInt32 uniqueID;

	
	fieldP = GetActiveFormObjectPtr(fldEditText);
	if (FldScrollable(fieldP, direction)) {
		nLines = FldGetVisibleLines(fieldP) - 1;

		if (direction == winUp) nLines = -nLines;
		EditFormScrollLines(nLines, true);

		return;
	} 

	if (currentDB == cryptoPadDB) return;

	// Move to the next or previous memo.
	if (direction == winUp)	{
		seekDirection = dmSeekBackward;
		editScrollPosition = maxFieldTextLen;		
	} else {
		seekDirection = dmSeekForward;
		editScrollPosition = 0;
	}

	if (showAllCategories) category = dmAllCategories;
	else category = currentCategory;

	recordNum = currentRecord;
	
	DmSeekRecordInCategory (dbMemoPadRef, &recordNum, 1, seekDirection, category);
	  	
	if (recordNum == currentRecord) return;

	SndPlaySystemSound (sndInfo);

	// Saving the current record may cause it to move if the records are 
	// sorted alphabeticly.
	DmRecordInfo (dbMemoPadRef, recordNum, NULL, &uniqueID, NULL);
	EditSaveData(fieldP);
	DmFindRecordByID (dbMemoPadRef, uniqueID, &currentRecord);

	EditRetrieveData(fieldP);
	FldGrabFocus (fieldP);

	// Set the correct font.
	if (useSingleFont == separateFont) {
		SetOrGetRecordFont(currentDBRef, uniqueID, getFont, fontField);
		FldSetFont(fieldP, currentFieldFont);
	}
}

/******************************************
 * EditSelectCategory:
 * Invokes the standard category UI
 ******************************************
 */

static void EditSelectCategory(void)
{
	Boolean categoryUpdated;
	Boolean categoryChanged;
	UInt16 category;
	FormPtr formP;
	UInt16 recAttributes;
	UInt32 recordID;

	DmRecordInfo(currentDBRef, currentRecord, &recAttributes, &recordID, 0);
	category = (recAttributes & dmRecAttrCategoryMask);


	/* Process the category popup list */
	formP = FrmGetActiveForm();
	categoryUpdated = CategorySelect(dbMemoPadRef, formP,
			popEditCategory, lstEditCategory, false,
			&category, categoryName, 1, 0);

	categoryChanged = (category != (recAttributes & dmRecAttrCategoryMask));

	if (categoryChanged) {
		/* Updates the tableFont of the record */
		if (useSingleFont == separateFont) {
			GetTableFontOfCategory(category);
			SetOrGetRecordFont(currentDBRef, recordID, setFont, fontTable);
		}

		/* Updates the record category */
		DmRecordInfo(currentDBRef, currentRecord, &recAttributes, 0, 0);
		recAttributes &= ~dmRecAttrCategoryMask;
		recAttributes |= category | dmRecAttrDirty;
		DmSetRecordInfo(currentDBRef, currentRecord, &recAttributes, 0);
	}

	if (categoryUpdated || categoryChanged) {
		/* Updates the category trigger and
		 * changes the current category to
		 * make the record visible in the
		 * main form.
		 */
		ChangeCategory(category);

		// If the record is secret and secret records are hidden, then the record isn't
		// accounted for by MemosInCategory.  Adjust it, and EditFormSaveData will 
		// adjust when done with the record.
		if ((PrivateRecordVisualStatus == hidePrivateRecords) && (recAttributes & dmRecAttrSecret))
			memosInCategory++;
		EditFormSetTitle();	
	}
}

/******************************************
 * EditFormUpdateDisplay:
 * Updates the category that was changed
 * from the Details form.
 ******************************************
 */

static Boolean EditFormUpdateDisplay(Int16 updateCode)
{
	FormPtr formP;
	FieldPtr fieldP;
	UInt16 category;
	

	if (updateCode == updateCategoryChanged) {
		UInt16 recAttributes;

		DmRecordInfo(currentDBRef, currentRecord, &recAttributes, 0, 0);
		category = (recAttributes & dmRecAttrCategoryMask);

		ChangeCategory(category);

		CategoryGetName(dbMemoPadRef, currentCategory, categoryName);
		CategorySetTriggerLabel(GetActiveFormObjectPtr(popEditCategory),
						categoryName);

		// If the record is secret and secret records are hidden, then the record isn't
		// accounted for by MemosInCategory.  Adjust it, and EditViewSaveRecord will 
		// adjust when done with the record.
		if (PrivateRecordVisualStatus == hidePrivateRecords && (recAttributes & dmRecAttrSecret))
			memosInCategory++;
		EditFormSetTitle ();
		
		return (true);
		}

	if (updateCode == updateFontChanged) {
		UInt32 recordID;
		
		formP = FrmGetActiveForm ();
		fieldP = FrmGetObjectPtr(formP,
		FrmGetObjectIndex(formP, fldEditText));
	
		/* Updates the currently selected font */
		FldSetFont(fieldP, currentFieldFont);

		if (useSingleFont == separateFont) {
			DmRecordInfo (currentDBRef, currentRecord, 0, &recordID, 0);
			SetOrGetRecordFont(currentDBRef, recordID, setFont, fontField);
		}

		EditFormUpdateScrollBar();
		return (true);
		}
	
	return (false);
}


/******************************************
 * EditFormSetTitle :
 * Sets the title of the edit view
 * with the number of records.
 * If the edit view is visible, the new title is drawn.
 * The title parts are referenced as follows:
 * [text 1][arg 1][text 2][arg 2]
 ******************************************
 */
static void EditFormSetTitle (void)
{
	Char * titleTemplateP;
	Char * strP;
	Char * titleP;
	FormPtr frm;
	char posStr [digitsForRecordPosition];
	char totalStr [digitsForRecordPosition];
	UInt16 pos;
	UInt16 length;
	UInt16 category;

	/* Format as strings, the memo's postion within its category, and 
	* the total number of memos in the category.
	*/
	if (showAllCategories)
		category = dmAllCategories;
	else 
		category = currentCategory;

	if (currentDB == memoPadDB) 
		pos = DmPositionInCategory (dbMemoPadRef, currentRecord, category);
	else pos = DmNumRecordsInCategory (dbMemoPadRef, category) +
		DmPositionInCategory (dbCryptoPadRef, currentRecord, category);
	StrIToA (posStr, pos+1);

	StrIToA (totalStr, memosInCategory);


	/* Get the title template string.  It contains '#' chars which we replace
	* with the position of currentRecord within currentCategory and with
	* the total count of records in currentCategory.
	*/
	titleTemplateP = MemHandleLock (DmGetResource ('tSTR', strEditTitleMemo));

	/* Now get space for the new title */
	length = StrLen (posStr) + StrLen (totalStr) + StrLen (titleTemplateP);


	titleP = MemPtrNew (length);
	ErrFatalDisplayIf (!titleP, "Out of memory");


	strP = StrChr (titleTemplateP, '#');
	ErrNonFatalDisplayIf (!strP, "Missing '#' in MemoEdit'tStr' strEditTitleMemo");

	length = strP - titleTemplateP;
	StrNCopy (titleP, titleTemplateP, length);
	titleP[length] = 0;
	StrCat (titleP, posStr);

	strP++;
	StrCat (titleP, strP);
	StrCat (titleP, totalStr);

	frm = FrmGetFormPtr (frmEditForm);

	FrmSetTitle (frm, titleP);

	MemPtrUnlock(titleTemplateP);
}

/******************************************
 * EditFormDisplayMaxSizeLabel:
 * Show or hide the 4k max size label.
 ******************************************
 */

static void EditFormDisplayMaxSizeLabel(void)
{
	FormPtr formP;
	UInt16 fieldIndex;
	FieldPtr fieldP;

	formP = FrmGetActiveForm();
	fieldIndex = FrmGetObjectIndex(formP, fldEditText);
	fieldP = FrmGetObjectPtr(formP, fieldIndex);
	
	/* Display the label if the size of the memo
	 * is over 4k otherwise hide it.
	 */
	if (FldGetTextLength(fieldP) <= 4097)
		FrmHideObject (formP, FrmGetObjectIndex(formP, lblEditMaxSize));
	else FrmShowObject (formP, FrmGetObjectIndex(formP, lblEditMaxSize));
}

/******************************************
 * EditFormHandleMenuEvent:
 * Process menu commands.
 ******************************************
 */

static Boolean EditFormHandleMenuEvent(UInt16 itemID)
{
	Boolean handled = true;

	FieldPtr fieldP;
	EventType event;
	FormPtr formP;
	UInt32 uniqueID;
	Char * textP;

	/* A card file is not a DB record. Handle the Record-menu items for it here;
	 * editing/font/goto/About and the system edit items fall through below. */
	if (gCardFileOpen) {
		switch (itemID) {
			case itemRecordEncryptMemo:      /* "Encrypt Memo" encrypts a card file too */
				EditCardEncryptChoose();
				return true;
			case itemRecordNewMemo:          /* save+close this file, then New-file dialog */
				FrmGotoForm(frmMainForm);
				{
					EventType ev;
					MemSet(&ev, sizeof(ev), 0);
					ev.eType = cardNewFileEvent;
					EvtAddEventToQueue(&ev);
				}
				return true;
			case itemRecordBeamMemo:
				FrmAlert(altCardNoBeam);
				return true;
			case itemRecordDeleteMemo:
				{
				FormPtr cardFormP = FrmGetActiveForm();
				FieldPtr cardFieldP = FrmGetObjectPtr(cardFormP,
					FrmGetObjectIndex(cardFormP, fldEditText));
				EditCardDeleteFile(cardFieldP);
				}
				return true;
			case itemRecordRemovePasswd:
				/* Marks the file to be saved back out PLAIN -- EditSaveCardFile
				 * already handles this transition correctly (writes to the
				 * base .txt path and drops the .enc sibling, since
				 * gCardWasEncrypted stays true) once gCardEncrypted is false. */
				gCardEncrypted = false;
				MemSet(gCardPassword, sizeof(gCardPassword), 0);
				return true;
			case itemRecordImportToCrypto:
				{
				FormPtr cardFormP = FrmGetActiveForm();
				FieldPtr cardFieldP = FrmGetObjectPtr(cardFormP,
					FrmGetObjectIndex(cardFormP, fldEditText));
				EditCardImportToCrypto(cardFieldP);
				}
				return true;
			default:
				break;
		}
	}

	switch (itemID) {
		case itemRecordNewMemo:
			fieldP = GetFocusObjectPtr();
			if (fieldP) {
				EditSaveData(fieldP);
				CreateRecord();
				editScrollPosition = 0;
				if (currentRecord != noRecordSelected) {
					EditRetrieveData(fieldP);
					FldGrabFocus(fieldP);
				} else FrmGotoForm(frmMainForm);
			}
			break;

		case itemRecordDeleteMemo:
			if (DetailsFormDeleteCurrentRecord ())
				FrmGotoForm(frmMainForm);
			break;

		case itemRecordEncryptMemo:
			/* Prompts for the password if
			 * the format of the record is
			 * valid for encryption.
			 */
			ErrFatalDisplayIf(currentPasswordP != 0, "Password should be NULL");

			if (EditValidateFormat()) {
				if ((prefsGlobalPasswordP != 0) && (globalPasswordP == 0)) {
					event.eType = globalPasswordStatusEvent;
					event.data.generic.datum[1] = queryGlobalPassword;
					event.data.generic.datum[2] = recDecrypted;

					FrmPopupForm(frmQueryGlobalPasswordForm);
					EvtAddEventToQueue(&event);
					handled = true;
					break;
				}

				event.eType = memoStatusEvent;
				event.data.generic.datum[1] = recDecrypted;

				gEncryptMemoRequested = true;	/* diagnostic, see declaration comment */
				FrmPopupForm(frmPasswordForm);
				EvtAddEventToQueue(&event);

			} else FrmAlert(altInvalidFormatAlert);
			break;

		case itemRecordRemovePasswd:
			/* Remove the password protection
			 * so that the memo is not
			 * automatically encrypted on exit.
			 */

//			ErrFatalDisplayIf(currentPasswordP == 0, "Password should not be NULL");
			RemoveCurrentPassword();

			FrmSetMenu(FrmGetActiveForm(), mnuEditMenu);
			EditFormDisplayMaxSizeLabel();
			FrmAlert(altRemovePasswdAlert);
			break;

		case itemOptionsGoTopOfPage:
		case itemOptionsGoBottomOfPage:
			fieldP = GetFocusObjectPtr();
			if (!fieldP)  break;
			if (itemID == itemOptionsGoTopOfPage)	{
				FldSetInsPtPosition(fieldP, 0);
				EditFormUpdateScrollBar();
				}
			else if (itemID == itemOptionsGoBottomOfPage)	{
				FldSetInsPtPosition(fieldP, FldGetTextLength(fieldP));
				EditFormUpdateScrollBar();
				}
			break;

		case itemOptionsFont:
			currentFieldFont = SelectFont (currentFieldFont);
			break;

		case itemRecordBeamMemo:
			if ((currentDB == memoPadDB) ||
				((currentDB == cryptoPadDB) && (currentPasswordP == 0))) {
				formP = FrmGetActiveForm();
				fieldP = FrmGetObjectPtr(formP, FrmGetObjectIndex(formP, fldEditText));
				if (FldGetTextLength(fieldP) > 0) {
					EditSaveData(fieldP);
					MemoSendRecord(currentDBRef, currentRecord);
				
				// Redisplay the record.  If the IR loopback mechanism sends the 
				// record to this app the goto action code closes all forms and 
				// send a frmGotoEvent.  Load the record again only if the form 
				// still exits.
				if (formP) EditRetrieveData(fieldP);
				} else FrmAlert(NoDataToBeamAlert);
			} else FrmAlert(altBeamCryptoDataAlert);
			break;

		case itemOptionsAbout:
			formP = FrmInitForm(frmAboutForm);
			FrmDoDialog(formP);
			FrmDeleteForm(formP);
			break;

		default:
			handled = false;
	}

	return handled;
}

Boolean EditFormHandleEvent(EventPtr eventP)
{
	Boolean handled = false;

	FormPtr formP;
	UInt16 fieldIndex;
	FieldPtr fieldP;
	Char * textP;
	UInt16 startPos, endPos;

	switch (eventP->eType) {
		/* A notification to immediately encrypt
		 * the record has been made. Close the
		 * form and let the frmCloseEvent handling
		 * code do the work.
		 */
		case memoProcessEvent:
			ErrFatalDisplayIf(currentPasswordP == 0, "Password shouldn't be NULL");
			FrmGotoForm(frmMainForm);
			handled = true;
			break;

		/* Notification was received to update the display */
		case frmUpdateEvent:
			handled = EditFormUpdateDisplay(eventP->data.frmUpdate.updateCode);
			break;

		case ctlSelectEvent:
			switch (eventP->data.ctlSelect.controlID) {
				/* Saves the memo and return to the main form */
				case btnEditTerminate:
					FrmGotoForm(frmMainForm);
					handled = true;
					break;

				/* Show the details attributes */
				case btnEditDetails:
					FrmPopupForm(frmDetailsForm);
					handled = true;
					break;

				/* Changes the category of the current memo */
				case popEditCategory:
					EditSelectCategory();
					handled = true;
					break;

			}
			break;

		/* The text in the field has changed */
		case fldChangedEvent:
			EditFormUpdateScrollBar();
			handled = true;
			break;

		/* the scrollbars has been tapped on the screen */
		case sclRepeatEvent:
			EditFormScrollLines(eventP->data.sclRepeat.newValue -
					eventP->data.sclRepeat.value, false);
			/* Let the system repeat the event */
			break;

		case keyDownEvent:
			if (TxtCharIsHardKey(eventP->data.keyDown.modifiers, eventP->data.keyDown.chr)) {
				FrmGotoForm(frmMainForm);
				handled = true;
			}

			else if (EvtKeydownIsVirtual(eventP)) {
				if (eventP->data.keyDown.chr == vchrPageUp) {
					EditFormScrollPage(winUp);
					handled = true;
				}
	
				else if (eventP->data.keyDown.chr == vchrPageDown) {
					EditFormScrollPage(winDown);
					handled = true;
				}
			
				// Send data key presed?
				else if (eventP->data.keyDown.chr == vchrSendData) {
					EditFormHandleMenuEvent(itemRecordBeamMemo);
					handled = true;
				}
			}

			/* A regular (non-hardkey, non-virtual) keystroke against a
			 * record flagged too-large-to-edit -- the field is already
			 * read-only so this can't actually modify anything, but give
			 * explicit feedback once per open rather than silently eating
			 * every keystroke. */
			else if (CryptoEditGuardBlockKey(gEditRecordTooLargeToEdit, &gEditTooLargeWarned)) {
				handled = true;
			}

			else if (currentPasswordP == 0) EditFormDisplayMaxSizeLabel();
			break;

		case menuEvent:
			handled = EditFormHandleMenuEvent(eventP->data.menu.itemID);
			break;

		// Add the buttons that we want available on the command bar, based on the current context
		case menuCmdBarOpenEvent:
			formP = FrmGetActiveForm();
			FldGetSelection(FrmGetObjectPtr(formP, 
			FrmGetObjectIndex(formP, fldEditText)) , &startPos, &endPos);
		
			// if there's some highlighted text
			if (startPos != endPos) {
				// (Note that we're adding buttons from right to left)
				MenuCmdBarAddButton(menuCmdBarOnLeft, BarPasteBitmap, menuCmdBarResultMenuItem, sysEditMenuPasteCmd, 0);
				MenuCmdBarAddButton(menuCmdBarOnLeft, BarCopyBitmap, menuCmdBarResultMenuItem, sysEditMenuCopyCmd, 0);
				MenuCmdBarAddButton(menuCmdBarOnLeft, BarCutBitmap, menuCmdBarResultMenuItem, sysEditMenuCutCmd, 0);
				MenuCmdBarAddButton(menuCmdBarOnLeft, BarUndoBitmap, menuCmdBarResultMenuItem, sysEditMenuUndoCmd, 0);
			}
			// there's no highlighted text
			else {
				MenuCmdBarAddButton(menuCmdBarOnLeft, BarDeleteBitmap, menuCmdBarResultMenuItem, itemRecordDeleteMemo, 0);
				MenuCmdBarAddButton(menuCmdBarOnLeft, BarPasteBitmap, menuCmdBarResultMenuItem, sysEditMenuPasteCmd, 0);
				MenuCmdBarAddButton(menuCmdBarOnLeft, BarUndoBitmap, menuCmdBarResultMenuItem, sysEditMenuUndoCmd, 0);
				MenuCmdBarAddButton(menuCmdBarOnLeft, BarBeamBitmap, menuCmdBarResultMenuItem, itemRecordBeamMemo, 0);
				MenuCmdBarAddButton(menuCmdBarOnLeft, bmpBarFontBitmap, menuCmdBarResultMenuItem, itemOptionsFont, 0);
			}

			// tell the field package to not add buttons automatically; we've done it all ourselves.
			eventP->data.menuCmdBarOpen.preventFieldButtons = true;
		
			// don't set handled to true; this event must fall through to the system.
			break;

		case frmOpenEvent:
			formP = FrmGetActiveForm();
			fieldIndex = FrmGetObjectIndex(formP, fldEditText);
			fieldP = FrmGetObjectPtr(formP, fieldIndex);

			EditFormInit();
			FrmDrawForm(formP);
			EditDrawCardKey();
			FrmSetFocus(formP, fieldIndex);

			EvtGetEvent(eventP, evtWaitForever);
			if (eventP->eType == keyDownEvent) {
				Boolean capsLock, numLock, autoShifted;
				UInt16 tempShift;

				GrfGetState(&capsLock, &numLock, &tempShift, &autoShifted);

				GrfSetState(capsLock, numLock, false);
				EvtAddEventToQueue(eventP);
			}
			handled = true;
			break;
			
		case frmGotoEvent:
			formP = FrmGetActiveForm ();
			fieldIndex = FrmGetObjectIndex(formP, fldEditText);
			fieldP = FrmGetObjectPtr(formP, fieldIndex);
			
			currentRecord = eventP->data.frmGoto.recordNum;

			/* A too-large record is fine to open from a Find result too --
			 * EditRetrieveData (called via EditFormInit below) makes the
			 * field read-only for it, same as any other open path. */

			if (currentDB == cryptoPadDB && CryptoGetRecordFlag(dbCryptoPadRef, currentRecord)) {
				/* Encrypted records now require their own password (in addition
				 * to the device and global secrets), so they can't be decrypted
				 * inline from a find result. Hand back to the list, where
				 * opening the record prompts for its password normally. */
				FrmGotoForm(frmMainForm);
				handled = true;
				break;
			}
			EditFormInit();
			FldSetScrollPosition (fieldP, eventP->data.frmGoto.matchPos);
			FldSetSelection(fieldP, eventP->data.frmGoto.matchPos, 
			eventP->data.frmGoto.matchPos + eventP->data.frmGoto.matchLen);
			EditFormUpdateScrollBar();
			FrmDrawForm (formP);
			FrmSetFocus(formP, fieldIndex);
			handled = true;
			break;

		case frmCloseEvent:
		case frmSaveEvent:
			formP = FrmGetActiveForm();
			fieldIndex = FrmGetObjectIndex(formP, fldEditText);
			fieldP = FrmGetObjectPtr(formP, fieldIndex);

			if (gCardFileOpen) {
				EditSaveCardFile(fieldP);
			} else if (FldGetTextHandle(fieldP)) {
				EditSaveData(fieldP);
			}
			/* Let the system handle the event */
			break;
	}

	return handled;
}
