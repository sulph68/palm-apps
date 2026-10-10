#include "QueryGlobalPasswordForm.h"
#include "Database.h"
#include "Utils.h"
#include "AesCrypt.h"

static Int16 globalPasswordStatus;
static Int16 encryptedStatus;


static Boolean ValidateGlobalPassword(Char *);
static void QueryGlobalPassword(Char *);

/******************************************
 * ValidateGlobalPassword:
 * Checks that the encrypted global
 * password of the current record
 * matches the given string.
 ******************************************
 */

static Boolean ValidateGlobalPassword(Char * textP)
{
	Boolean bResult;
	Char * decryptedGlobalPasswordP;

	decryptedGlobalPasswordP = MemPtrNew(StrLen(prefsGlobalPasswordP) + 1);
	
	AESCryptEx(prefsGlobalPasswordP, decryptedGlobalPasswordP, textP, 0,
		prefsGlobalPasswordIV, prefsGlobalPasswordSalt, prefsGlobalPasswordRounds,
		StrLen(prefsGlobalPasswordP), AES_DECRYPT);
	
	bResult = (StrCompare(decryptedGlobalPasswordP, textP) == 0);

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

static void QueryGlobalPassword(Char * textP)
{
	Char * passwordP;
	EventType event;

	if (!ValidateGlobalPassword(textP)) {
		FrmAlert(altInvalidGlobalPasswordAlert);
		return;
	}

	/* Sets the globalPasswordP variable */
	 

	globalPasswordP = MemPtrNew(StrLen(textP) + 1);
	StrCopy(globalPasswordP, textP);

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
				EventType event;

				/* Apply changes to the password form */
				case btnQueryGlobalPasswordOK:
					textP = FldGetTextPtr(fieldP);

					if (textP != 0 && *textP != '\0')
						QueryGlobalPassword(textP);
					else FrmAlert(altInvalidGlobalPasswordAlert);
					
					handled = true;
					break;

				/* Simply return to the calling form */
				case btnQueryGlobalPasswordCancel:
					FrmReturnToForm(0);
					handled = true;
					break;
			}
			break;

		case frmOpenEvent:
			FrmDrawForm(FrmGetActiveForm());
			FrmSetFocus(formP, fieldIndex);
			FldGrabFocus(fieldP);
			handled = true;
			break;
	}

	return handled;
}

