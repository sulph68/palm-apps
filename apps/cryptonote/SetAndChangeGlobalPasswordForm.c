#include "SetAndChangeGlobalPasswordForm.h"
#include "Database.h"
#include "Utils.h"
#include "AesCrypt.h"
#include "PwMask.h"

static void SetGlobalPassword(Char *) UI2_SECTION;
static void RemoveGlobalPassword(void) UI2_SECTION;
static void ConfirmGlobalPassword(void) UI2_SECTION;

static Char * passwordP;		/* the new password between Set and Confirm (wiped) */

/******************************************
 * SetGlobalPassword:
 * Set or change the global password.
 ******************************************
 */

UI2_SECTION static void SetGlobalPassword(Char * textP)
{
	PwFreeWiped(&passwordP);
	passwordP = MemPtrNew(StrLen(textP) + 1);
	StrCopy(passwordP, textP);
	PwMaskEnd();

	FrmGotoForm(frmConfirmGlobalPasswordForm);
}

/******************************************
 * ConfirmGlobalPassword:
 * Confirms the global password.
 ******************************************
 */

UI2_SECTION static void ConfirmGlobalPassword(void)
{
	ControlPtr controlP;
	FormPtr formP;
	Char * strGlobalPassword;
	MemHandle strGlobalPasswordH;

	formP = FrmGetFormPtr(frmPrefsForm);

	/* Sets the globalPasswordP and
	* prefsGlobalPasswordP variables
	* to the chosen values.
	 */

	if (prefsGlobalPasswordP) MemPtrFree(prefsGlobalPasswordP);	/* old verifier */
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

	PwFreeWiped(&globalPasswordP);				/* the previous one, if changing */
	globalPasswordP = MemPtrNew(StrLen(passwordP) + 1);
	StrCopy(globalPasswordP, passwordP);
	PwFreeWiped(&passwordP);
	PwMaskEnd();

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

UI2_SECTION static void RemoveGlobalPassword(void)
{
	ControlPtr controlP;
	FormPtr formP;
	Char * strGlobalPassword;
	MemHandle strGlobalPasswordH;	

	formP = FrmGetFormPtr(frmPrefsForm);

	/* Sets the globalPasswordP and
	* prefsGlobalPasswordP variables
	* to 0.
	 */

	if (prefsGlobalPasswordP) MemPtrFree(prefsGlobalPasswordP);	/* the verifier */
	prefsGlobalPasswordP = 0;
	PwFreeWiped(&globalPasswordP);				/* zeroed, not just forgotten */
	PwFreeWiped(&passwordP);
	PwMaskEnd();
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


Boolean SetAndChangeGlobalPasswordFormHandleEvent(EventPtr eventP) UI2_SECTION;
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

				/* Asks the global password */
				case btnSetAndChangeGlobalPasswordOK:
					textP = PwMaskText();

					if (textP != 0 && *textP != '\0')
						SetGlobalPassword(textP);
					else FrmAlert(altInvalidPasswordAlert);;
					
					handled = true;
					break;

				/* Apply changes to the global password */
				case btnConfirmGlobalPasswordOK:
					textP = PwMaskText();

					if (textP != 0 && *textP != '\0')
						if (StrCompare(textP, passwordP) == 0) {
							ConfirmGlobalPassword();
							handled = true;
							break;
						}
					FrmAlert(altGlobalPasswordNotMatchAlert);
					PwMaskBegin(fldSetAndChangeGlobalPasswordText, chkSetAndChangeGlobalPasswordShow);	/* retry: start empty */
					
					handled = true;
					break;

				/* Simply return to the calling form */
				case chkSetAndChangeGlobalPasswordShow:
					PwMaskShow(eventP->data.ctlSelect.on);
					handled = true;
					break;

				case btnSetAndChangeGlobalPasswordCancel:
					PwFreeWiped(&passwordP);
					PwMaskEnd();
					FrmReturnToForm(0);
					handled = true;
					break;

				case btnChangeGlobalPasswordDelete:
					RemoveGlobalPassword();
					handled = true;
					break;
			}
			break;

		case keyDownEvent:
			handled = PwMaskKeyDown(eventP);
			break;

		case frmOpenEvent:
			FrmDrawForm(FrmGetActiveForm());
			PwMaskBegin(fldSetAndChangeGlobalPasswordText, chkSetAndChangeGlobalPasswordShow);
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

