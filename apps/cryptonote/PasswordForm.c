#include "PasswordForm.h"
#include "SecDb.h"
#include "DbForms.h"
#include "Database.h"
#include "Utils.h"
#include "AesCrypt.h"
#include "CryptoSec35.h"
#include "CardCrypt.h"	/* CardBusyShow/CardBusyHide -- shared busy-dialog style */
#include "PwMask.h"		/* masked entry + "Show", shared by every password field */

static void PasswordApplyChanges(Char *) AES_SECTION;

static Boolean alreadyTriedWithGlobalPassword;
static Int16 encryptedStatus;

/* Password masking: PwMask.c (shared by every CryptoNote password field).
 * The keypad below still builds its digits in its own buffer. */
#define kPwBufLen 33

/* ---- Numeric keypad (mirrors Sec35's own frmKeypad -- see sec35.c's
 * KeypadDrawStars/KeypadDrawKey/KeypadHitTest/KeypadFormHandleEvent, ported
 * near-verbatim). Digits build up gKeypadBuf as a hand-drawn row of stars
 * (real-time feedback while typing); OK hands it to PwMaskSet (masked, same
 * as Text Entry) and returns to the Password form -- no auto-submit unlike
 * Sec35's Unlock-only special case, since this app has just the one target
 * field and its own OK button is always the actual submit action. */
#define kKeypadStarsY	15
#define kKeypadStarsH	12
#define kKeyNone		(-1)
#define kKeyDelSlot		10
#define kKeyFrame		((FrameType) ((3 << 8) | 1))

static Int16	gKeyPressed = kKeyNone;
static char		gKeypadBuf[kPwBufLen];
static UInt16	gKeypadLen;

static const UInt16 kKeyIds[10] =
	{ kpd0, kpd1, kpd1+1, kpd1+2, kpd1+3, kpd1+4, kpd1+5, kpd1+6, kpd1+7, kpd1+8 };
static const char* const kKeyLetters[10] =
	{ "", "", "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ" };

static void KeypadDrawStars(void) AES_SECTION;
static void KeypadDrawStars(void)
{
	RectangleType	r;
	Char			buf[kPwBufLen];
	UInt16			i;
	Int16			w, x;
	FontID			old;

	r.topLeft.x = 2; r.topLeft.y = kKeypadStarsY; r.extent.x = 152; r.extent.y = kKeypadStarsH;
	WinEraseRectangle(&r, 0);

	for (i = 0; i < gKeypadLen; i++) buf[i] = '*';
	buf[i] = 0;

	old = FntSetFont(boldFont);
	w = FntCharsWidth(buf, (Int16) StrLen(buf));
	x = (Int16) ((156 - w) / 2);
	if (x < 2) x = 2;
	WinDrawChars(buf, (Int16) StrLen(buf), x, kKeypadStarsY);
	FntSetFont(old);
}

/* Draw one key's border + digit + (if any) phone-style letters. */
static void KeypadDrawKey(FormType* frm, UInt16 id, const char* label, const char* letters) AES_SECTION;
static void KeypadDrawKey(FormType* frm, UInt16 id, const char* label, const char* letters)
{
	RectangleType	r;
	FontID			old;
	Int16			w, x, y, boldH;

	FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, id), &r);
	WinDrawRectangleFrame(kKeyFrame, &r);

	old = FntSetFont(boldFont);
	boldH = FntLineHeight();
	w = FntCharsWidth(label, (Int16) StrLen(label));
	x = (Int16) (r.topLeft.x + (r.extent.x - w) / 2);
	y = (Int16) (r.topLeft.y + 1);
	WinDrawChars(label, (Int16) StrLen(label), x, y);

	if (letters && letters[0]) {
		FntSetFont(stdFont);
		w = FntCharsWidth(letters, (Int16) StrLen(letters));
		x = (Int16) (r.topLeft.x + (r.extent.x - w) / 2);
		y = (Int16) (y + boldH - 2);
		WinDrawChars(letters, (Int16) StrLen(letters), x, y);
	}
	FntSetFont(old);
}

static void KeypadDrawKeys(FormType* frm) AES_SECTION;
static void KeypadDrawKeys(FormType* frm)
{
	UInt16 d;
	Char digitStr[2];

	digitStr[1] = 0;
	for (d = 0; d < 10; d++) {
		digitStr[0] = (Char) ('0' + d);
		KeypadDrawKey(frm, kKeyIds[d], digitStr, kKeyLetters[d]);
	}
	KeypadDrawKey(frm, kpdDel, "Del", NULL);
}

/* Which key (0-9 digit, kKeyDelSlot, or kKeyNone) is under (x,y). */
static Int16 KeypadHitTest(FormType* frm, Int16 x, Int16 y) AES_SECTION;
static Int16 KeypadHitTest(FormType* frm, Int16 x, Int16 y)
{
	UInt16			d;
	RectangleType	r;

	for (d = 0; d < 10; d++) {
		FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, kKeyIds[d]), &r);
		if (RctPtInRectangle(x, y, &r)) return (Int16) d;
	}
	FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, kpdDel), &r);
	if (RctPtInRectangle(x, y, &r)) return kKeyDelSlot;
	return kKeyNone;
}

static void KeypadInvertKey(FormType* frm, Int16 key) AES_SECTION;
static void KeypadInvertKey(FormType* frm, Int16 key)
{
	RectangleType	r;
	UInt16			id = (key == kKeyDelSlot) ? kpdDel : kKeyIds[key];

	FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, id), &r);
	WinInvertRectangle(&r, 3);		/* match the key's 3px rounded corners */
}

static void KeypadAppend(char c) AES_SECTION;
static void KeypadAppend(char c)
{
	if (gKeypadLen < sizeof(gKeypadBuf) - 1) {
		gKeypadBuf[gKeypadLen++] = c;
		gKeypadBuf[gKeypadLen] = 0;
		KeypadDrawStars();
	}
}

static void KeypadBackspace(void) AES_SECTION;
static void KeypadBackspace(void)
{
	if (gKeypadLen > 0) {
		gKeypadBuf[--gKeypadLen] = 0;
		KeypadDrawStars();
	}
}

/* Don't leave a plaintext PIN sitting in RAM once the keypad is done with it. */
static void KeypadClearBuf(void) AES_SECTION;
static void KeypadClearBuf(void)
{
	MemSet(gKeypadBuf, sizeof(gKeypadBuf), 0);
	gKeypadLen = 0;
}

Boolean KeypadFormHandleEvent(EventPtr eventP)
{
	Boolean handled = false;

	switch (eventP->eType) {
		case frmOpenEvent:
			{
			FormType* frm = FrmGetActiveForm();
			KeypadClearBuf();
			gKeyPressed = kKeyNone;
			FrmDrawForm(frm);
			KeypadDrawKeys(frm);
			KeypadDrawStars();
			handled = true;
			}
			break;

		/* The 10 digit/Del keys are GADGETs (need 2 lines drawn inside their
		 * own border), so they don't generate ctlSelectEvent; tap detection
		 * + press feedback are hand-tracked here, same as Sec35's keypad. */
		case penDownEvent:
			{
			FormType* frm = FrmGetActiveForm();
			Int16 key = KeypadHitTest(frm, eventP->screenX, eventP->screenY);
			if (key != kKeyNone) {
				gKeyPressed = key;
				KeypadInvertKey(frm, key);
				/* GADGETs get no automatic Control Manager tap click, so play
				 * it by hand like Sec35's keypad (SndPlaySystemSound honours
				 * the System Sound preference itself). */
				SndPlaySystemSound(sndClick);
				handled = true;
			}
			}
			break;

		case penUpEvent:
			if (gKeyPressed != kKeyNone) {
				FormType* frm = FrmGetActiveForm();
				Int16 key = KeypadHitTest(frm, eventP->screenX, eventP->screenY);
				KeypadInvertKey(frm, gKeyPressed);	/* un-invert (2nd invert restores) */
				if (key == gKeyPressed) {
					if (key == kKeyDelSlot) KeypadBackspace();
					else                    KeypadAppend((char) ('0' + key));
				}
				gKeyPressed = kKeyNone;
				handled = true;
			}
			break;

		case ctlSelectEvent:
			if (eventP->data.ctlSelect.controlID == kpdOk) {
				FrmReturnToForm(0);			/* back to the Password dialog */
				PwMaskSet(gKeypadBuf);		/* masked, same as Text Entry */
				KeypadClearBuf();
				handled = true;
			}
			else if (eventP->data.ctlSelect.controlID == kpdCancel) {
				FrmReturnToForm(0);
				KeypadClearBuf();
				handled = true;
			}
			break;

		default:
			break;
	}
	return handled;
}

/******************************************
 * ValidatePassword:
 * Checks that the encrypted password
 * of the current record matches the
 * given string.
 ******************************************
 */

Boolean ValidatePassword(Char * textP) AES_SECTION;
Boolean ValidatePassword(Char * textP)
{
	RecordType record;

	Boolean bResult;
	Char * passwordP;

	if (currentRecord == noRecordSelected) return;

	/* Db35-suite-style Sec35 wrap: this only ever runs against CryptoPad DB
	 * records (MemoPad records have no password), which may now be
	 * Sec35-wrapped -- route through the chokepoint instead of reading raw
	 * bytes. */
	/* user databases: the record's own keying (SecDb.c) */
	SrcKeyForRecord ((UInt8) currentDB, currentRecord);
	if (! CryptoGetRecord (currentDBRef, currentRecord, &record))
		return false;

	/* Decrypts the password using textP as the key,
	 * and checks that we find textP itself!
	 */

	passwordP = MemPtrNew(record.passwdLen + 1);
	AESCryptEx(record.passwordP, passwordP, textP, globalPasswordP, record.pwIV,
		record.kdfSalt, record.kdfRounds, record.passwdLen, AES_DECRYPT);

	bResult = (StrCompare(passwordP, textP) == 0);

	MemPtrFree(passwordP);

	return bResult;
}


/******************************************
 * PasswordApplyChanges:
 * Performs the appropriate encryption/
 * decryption handling according to the
 * encryptedStatus.
 ******************************************
 */

static void PasswordApplyChanges(Char * textP) AES_SECTION;
static void PasswordApplyChanges(Char * textP)
{
	EventType event;

	/* Db35-suite-style Sec35 wrap: THIS is where the real ~1.5s KDF stretch
	 * for the single-record "enter password to view/remove" flow actually
	 * happens (ValidatePassword re-derives the key from scratch here) --
	 * DecryptRecord's own busy-dialog-wrapped work (MainForm.c) runs AFTER
	 * this and is fast (AesCrypt's key cache serves it), so wrapping only
	 * DecryptRecord left this exact wait with zero visual feedback, the
	 * dialog flashing by too fast to see -- caught during testing
	 * ("doesn't show any progress"). Only covers the single-record path
	 * (recEncrypted here); the bulk EncryptCategory/DecryptCategory loops
	 * call ValidatePassword directly (MainForm.c), not through this
	 * function, so they're untouched -- same "don't nest inside the bulk
	 * gauge" scoping as the other single-record busy-dialog call sites.
	 * Uses CardBusyShow/CardBusyHide (CardCrypt.c) -- the SAME lightweight
	 * WinSaveBits-based busy indicator the SD-card encrypt/decrypt paths
	 * already used, standardized here after the DB-record path's own
	 * separate FORM-based version (with an animated bar) was judged more
	 * distracting; the card version is calmer, so it won out everywhere
	 * instead of the other way around. */
	if (encryptedStatus == recEncrypted) {
		Boolean ok;
		WinHandle busy;
		busy = CardBusyShow("Decrypting...");
		ok = ValidatePassword(textP);
		CardBusyHide(busy);
		if (! ok) {
			FrmAlert(altInvalidPasswordAlert);
			return;
		}
	}

	if (globalPasswordP != 0) {
		if (encryptedStatus == recDecrypted)
			FrmAlert(altEncryptRecordWithGlobalPasswordAlert);
	
		if (encryptedStatus == catDecrypted)
			FrmAlert(altEncryptCategoryWithGlobalPasswordAlert);
	}

	/* Sets the currentPasswordP variable and
	 * return to the calling form. Notify the
	 * calling form to immediately process the
	 * record by sending a memoProcess event.
	 */

	currentPasswordP = MemPtrNew(StrLen(textP) + 1);
	StrCopy(currentPasswordP, textP);

	if ((encryptedStatus == recEncrypted) || 
					(encryptedStatus == recDecrypted))
		event.eType = memoProcessEvent;
	else {
		event.eType = catProcessEvent;
		event.data.generic.datum[1] = encryptedStatus;
	}

	FrmReturnToForm(0);
	EvtAddEventToQueue(&event);
}


Boolean PasswordFormHandleEvent(EventPtr eventP) AES_SECTION;
Boolean PasswordFormHandleEvent(EventPtr eventP)
{
	Boolean handled = false;

	FormPtr formP;
	FieldPtr fieldP;
	UInt16 fieldIndex;

	Char * textP;

	formP = FrmGetActiveForm();
	fieldIndex = FrmGetObjectIndex(formP, fldPasswordText);
	fieldP = FrmGetObjectPtr(formP, fieldIndex);

	switch (eventP->eType) {
		case memoStatusEvent:
			encryptedStatus = eventP->data.generic.datum[1];
			alreadyTriedWithGlobalPassword = false;

			/* The global password no longer auto-unlocks the record: the user
			 * must enter the record's own password (the global password, if
			 * one is set, is folded into the key derivation). So always prompt.
			 * Hide the 'forget...' button if the memo is not already encrypted.
			 */
			if (encryptedStatus != recEncrypted) {
				FrmHideObject(formP,
					FrmGetObjectIndex(formP, btnPasswordForget));
			}

			handled = true;
			break;

		case ctlSelectEvent:
			switch (eventP->data.ctlSelect.controlID) {
				/* Apply changes to the password form */
				case btnPasswordOK:
					/* PwMaskText, not FldGetTextPtr(fieldP) -- the field only
					 * ever shows the masked display (PwMask.c). */
					textP = PwMaskText();

					if (textP != 0 && *textP != '\0') {
						PasswordApplyChanges(textP);
						/* A wrong password leaves this dialog up for a retry:
						 * start a fresh (empty, still masked) entry. Otherwise
						 * the dialog has gone: just wipe. */
						if (FrmGetActiveFormID() == frmPasswordForm)
							PwMaskBegin(fldPasswordText, chkPasswordShow);
						else
							PwMaskEnd();
					}
					else FrmAlert(altInvalidPasswordAlert);
					handled = true;
					break;

				/* Simply return to the calling form */
				case chkPasswordShow:
					PwMaskShow(eventP->data.ctlSelect.on);
					handled = true;
					break;

				case btnPasswordCancel:
					PwMaskEnd();
					FrmReturnToForm(0);
					handled = true;
					break;

				/* Without the password, we cannot restore
				 * a memo. Deletes the memo and ask the
				 * Main form to reload the table.
				 */

				case btnPasswordForget:
					/* forgetting deletes the memo: not for read-only ones */
					if (EditViewOnlyRefuse ()) { handled = true; break; }
					if (FrmAlert(altConfirmDeleteAlert) == 0 /* OK button */) {
						if (useSingleFont == separateFont) {
							UInt32 recordID;

							DmRecordInfo (currentDBRef, currentRecord, 0, &recordID, 0);
							DeleteFontRecord(currentDBRef, recordID);
						}
						DmRemoveRecord(currentDBRef, currentRecord);
						PwMaskEnd();
						FrmReturnToForm(frmMainForm);
						FrmUpdateForm(frmMainForm, updateTable);
					}
					handled = true;
					break;
			}
			break;

		/* Text Entry mode: intercept keystrokes on the focused password field
		 * so the real text never sits in the field itself (masked display
		 * instead -- see PwMask.c). Numeric Keypad mode
		 * never gives this field focus to type into (fldEnterEvent pops the
		 * keypad instead), so this never fires then. */
		case keyDownEvent:
			{
			handled = PwMaskKeyDown(eventP);
			}
			break;

		/* Numeric Keypad mode: a tap into the password field pops the keypad
		 * instead of letting it take normal graffiti/keyboard focus. Text
		 * Entry mode (the default) falls through unhandled -- the field
		 * behaves exactly as before. */
		case fldEnterEvent:
			if (EncGetPasswordEntryType() == kPwTypeNumeric
					&& eventP->data.fldEnter.fieldID == fldPasswordText) {
				FrmPopupForm(frmKeypadForm);
				handled = true;
			}
			break;

		case frmOpenEvent:
			FrmDrawForm(FrmGetActiveForm());
			PwMaskBegin(fldPasswordText, chkPasswordShow);
			if (EncGetPasswordEntryType() == kPwTypeNumeric) {
				/* Numeric Keypad mode: pop the keypad immediately instead of
				 * waiting for the user to tap the (masked, otherwise-idle)
				 * field first -- that extra tap was pure friction, since this
				 * dialog has no other reason to be open. The keypad's own OK
				 * (KeypadFormHandleEvent) only fills the entry and returns here;
				 * it deliberately does NOT auto-submit, so the user still gets
				 * a real choice between this dialog's OK and Forget after
				 * typing the password, same as Text Entry mode always has. */
				FrmPopupForm(frmKeypadForm);
			} else {
				FrmSetFocus(formP, fieldIndex);
				/* FrmSetFocus alone routes key events to the field but
				 * doesn't reliably show the insertion point / accept input
				 * without an extra tap first -- FldGrabFocus is the
				 * field-level API that actually claims input immediately. */
				FldGrabFocus(fieldP);
			}
			handled = true;
			break;
	}

	return handled;
}

