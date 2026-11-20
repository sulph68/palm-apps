#include "QueryGlobalPasswordForm.h"
#include "Database.h"
#include "Utils.h"
#include "AesCrypt.h"
#include "PwMask.h"

static Int16 globalPasswordStatus;
static Int16 encryptedStatus;


static Boolean ValidateGlobalPassword(Char *) UI2_SECTION;
static void QueryGlobalPassword(Char *) UI2_SECTION;

/******************************************
 * ValidateGlobalPassword:
 * Checks that the encrypted global
 * password of the current record
 * matches the given string.
 ******************************************
 */

UI2_SECTION static Boolean ValidateGlobalPassword(Char * textP)
{
	Boolean bResult;
	Char * decryptedGlobalPasswordP;

	decryptedGlobalPasswordP = MemPtrNew(StrLen(prefsGlobalPasswordP) + 1);
	
	AESCryptEx(prefsGlobalPasswordP, decryptedGlobalPasswordP, textP, 0,
		prefsGlobalPasswordIV, prefsGlobalPasswordSalt, prefsGlobalPasswordRounds,
		StrLen(prefsGlobalPasswordP), AES_DECRYPT);
	
	bResult = (StrCompare(decryptedGlobalPasswordP, textP) == 0);
	PwFreeWiped(&decryptedGlobalPasswordP);		/* held the plaintext */

	return bResult;
}


/******************************************
 * QueryGlobalPassword:
 * Validate the given string and
 * performs the appropriate encryption/
 * decryption handling according to the
 * encryptedStatus.
 ******************************************
 */

UI2_SECTION static void QueryGlobalPassword(Char * textP)
{
	EventType event;

	if (!ValidateGlobalPassword(textP)) {
		FrmAlert(altInvalidGlobalPasswordAlert);
		PwMaskBegin(fldQueryGlobalPasswordText, chkQueryGlobalPasswordShow);	/* retry: start empty */
		return;
	}

	/* Sets the globalPasswordP variable */
	 

	PwFreeWiped(&globalPasswordP);				/* any previous copy */
	globalPasswordP = MemPtrNew(StrLen(textP) + 1);
	StrCopy(globalPasswordP, textP);
	PwMaskEnd();

	if (globalPasswordStatus == queryGlobalPassword) {
	/* Return to the calling form. Notify the
	 * calling form to immediately process the
	 * record by sending a memoProcess event.
	 */

		event.eType = memoStatusEvent;
		event.data.generic.datum[1] = encryptedStatus;

		FrmGotoForm(frmPasswordForm);
		EvtAddEventToQueue(&event);
	}
	else {
	/* Go to a form to change
	 * the global password.
	 */
	 FrmGotoForm(frmChangeGlobalPasswordForm);
	 }
}


Boolean QueryGlobalPasswordFormHandleEvent(EventPtr eventP) UI2_SECTION;
Boolean QueryGlobalPasswordFormHandleEvent(EventPtr eventP)
{
	Boolean handled = false;

	FormPtr formP;
	FieldPtr fieldP;
	UInt16 fieldIndex;

	formP = FrmGetActiveForm();
	fieldIndex = FrmGetObjectIndex(formP, fldQueryGlobalPasswordText);
	fieldP = FrmGetObjectPtr(formP, fieldIndex);

	switch (eventP->eType) {
		case globalPasswordStatusEvent:
			globalPasswordStatus = eventP->data.generic.datum[1];
			encryptedStatus = eventP->data.generic.datum[2];
			handled = true;
			break;

		case ctlSelectEvent:
			switch (eventP->data.ctlSelect.controlID) {
				Char * textP;

				/* Apply changes to the password form */
				case btnQueryGlobalPasswordOK:
					textP = PwMaskText();

					if (textP != 0 && *textP != '\0')
						QueryGlobalPassword(textP);
					else FrmAlert(altInvalidGlobalPasswordAlert);
					
					handled = true;
					break;

				/* Simply return to the calling form */
				case chkQueryGlobalPasswordShow:
					PwMaskShow(eventP->data.ctlSelect.on);
					handled = true;
					break;

				case btnQueryGlobalPasswordCancel:
					PwMaskEnd();
					FrmReturnToForm(0);
					handled = true;
					break;
			}
			break;

		case keyDownEvent:
			handled = PwMaskKeyDown(eventP);
			break;

		case frmOpenEvent:
			FrmDrawForm(FrmGetActiveForm());
			PwMaskBegin(fldQueryGlobalPasswordText, chkQueryGlobalPasswordShow);
			FrmSetFocus(formP, fieldIndex);
			FldGrabFocus(fieldP);
			handled = true;
			break;

		case frmCloseEvent:
			PwMaskEnd();
			break;
	}

	return handled;
}

