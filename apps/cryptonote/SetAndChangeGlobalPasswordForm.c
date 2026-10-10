#include "SetAndChangeGlobalPasswordForm.h"
#include "Database.h"
#include "Utils.h"
#include "AesCrypt.h"

static void SetGlobalPassword(Char *);
static void RemoveGlobalPassword(void);
static void ConfirmGlobalPassword(void);

static Char * passwordP;

/******************************************
 * SetGlobalPassword:
 * Set or change the global password.
 ******************************************
 */

static void SetGlobalPassword(Char * textP)
{
	passwordP = MemPtrNew(StrLen(textP) + 1);
	StrCopy(passwordP, textP);

	FrmGotoForm(frmConfirmGlobalPasswordForm);
}

/******************************************
 * ConfirmGlobalPassword:
 * Confirms the global password.
 ******************************************
 */

static void ConfirmGlobalPassword(void)
{
	ControlPtr controlP;
	FormPtr formP;
	Char * strGlobalPassword;
	MemHandle strGlobalPasswordH;
	Char * decryptedGlobalPasswordP;

	formP = FrmGetFormPtr(frmPrefsForm);

	/* Sets the globalPasswordP and
	* prefsGlobalPasswordP variables
	* to the chosen values.
	 */

	prefsGlobalPasswordP = MemPtrNew(StrLen(passwordP) + 1);
	
	/* The global-password verifier is itself device-bound and stretched: its
	 * key is derived from the global password + this device's secret + a salt,
	 * with no separate global password folded in (it IS the global password). */

	GenerateRandomIV(prefsGlobalPasswordIV);
	GenerateRandomIV(prefsGlobalPasswordSalt);
	prefsGlobalPasswordRounds = CalibrateKdfRounds();
	AESCryptEx(passwordP, prefsGlobalPasswordP, passwordP, 0,
		prefsGlobalPasswordIV, prefsGlobalPasswordSalt, prefsGlobalPasswordRounds,
		StrLen(passwordP), AES_ENCRYPT);

	globalPasswordP = MemPtrNew(StrLen(passwordP) + 1);
	StrCopy(globalPasswordP, passwordP);

	FrmReturnToForm(frmPrefsForm);

	// Display the status of the global password.
	strGlobalPasswordH = DmGetResource('tSTR', strGlobalPasswordAssigned);
	strGlobalPassword = MemHandleLock(strGlobalPasswordH);
		
	controlP = GetActiveFormObjectPtr(selPrefsGlobalPassword);
	CtlSetLabel(controlP, strGlobalPassword);

	MemHandleUnlock(strGlobalPasswordH);   
   	DmReleaseResource(strGlobalPasswordH); 
}


/******************************************
 * RemoveGlobalPassword():
 * Remove the global password.
 ******************************************
 */

static void RemoveGlobalPassword(void)
{
	Char * passwordP;
	ControlPtr controlP;
	FormPtr formP;
	Char * strGlobalPassword;
	MemHandle strGlobalPasswordH;	

	formP = FrmGetFormPtr(frmPrefsForm);

	/* Sets the globalPasswordP and
	* prefsGlobalPasswordP variables
	* to 0.
	 */

	prefsGlobalPasswordP = 0;
	globalPasswordP = 0;
	MemSet(prefsGlobalPasswordIV, sizeof(prefsGlobalPasswordIV), 0);
	MemSet(prefsGlobalPasswordSalt, sizeof(prefsGlobalPasswordSalt), 0);
	prefsGlobalPasswordRounds = 0;

	FrmReturnToForm(frmPrefsForm);

	// Display the status of the global password.
	strGlobalPasswordH = DmGetResource('tSTR', strGlobalPasswordUnassigned);
	strGlobalPassword = MemHandleLock(strGlobalPasswordH);
		
	controlP = GetActiveFormObjectPtr(selPrefsGlobalPassword);
	CtlSetLabel(controlP, strGlobalPassword);

	MemHandleUnlock(strGlobalPasswordH);   
   	DmReleaseResource(strGlobalPasswordH); 

}


Boolean SetAndChangeGlobalPasswordFormHandleEvent(EventPtr eventP)
{
	Boolean handled = false;

	FormPtr formP;
	FieldPtr fieldP;
	UInt16 fieldIndex;

	formP = FrmGetActiveForm();
	fieldIndex = FrmGetObjectIndex(formP, fldSetAndChangeGlobalPasswordText);
	fieldP = FrmGetObjectPtr(formP, fieldIndex);

	switch (eventP->eType) {
		case ctlSelectEvent:
			switch (eventP->data.ctlSelect.controlID) {
				Char * textP;
				EventType event;

				/* Asks the global password */
				case btnSetAndChangeGlobalPasswordOK:
					textP = FldGetTextPtr(fieldP);

					if (textP != 0 && *textP != '\0')
						SetGlobalPassword(textP);
					else FrmAlert(altInvalidPasswordAlert);;
					
					handled = true;
					break;

				/* Apply changes to the global password */
				case btnConfirmGlobalPasswordOK:
					textP = FldGetTextPtr(fieldP);

					if (textP != 0 && *textP != '\0')
						if (StrCompare(textP, passwordP) == 0) {
							ConfirmGlobalPassword();
							handled = true;
							break;
						}
					FrmAlert(altGlobalPasswordNotMatchAlert);
					
					handled = true;
					break;

				/* Simply return to the calling form */
				case btnSetAndChangeGlobalPasswordCancel:
					FrmReturnToForm(0);
					handled = true;
					break;

				case btnChangeGlobalPasswordDelete:
					RemoveGlobalPassword();
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

