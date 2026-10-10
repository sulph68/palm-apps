#include "PrefsForm.h"
#include "Database.h"
#include "Utils.h"

static Boolean newSortOrder;
static Int8 newRotateOrder;

static UInt16 updateCode = 0;
static Boolean newUseSingleFont;


static void PrefsSortMemos(void);
static void PrefsFormInit(void);
static void PrefsSetGlobalPasswordStatus(void);
static void PrefsFormSortOrderSelect(void);
static void PrefsShowLines(void);
static void PrefsShowHidden(void);
static void PrefsUseSingleFont(void);

static void PrefsGlobalPassword(void);
static void PrefsForgottenPassword(void);

/************************************
 * PrefsFormInit:
 * Initialize the Preferences form
 ************************************
 */

static void PrefsFormInit(void)
{
	FormPtr formP;
	ListPtr listSortP;
	ControlPtr popSortP;
	ListPtr listRotateP;
	ControlPtr popRotateP;
	ControlPtr controlShowLinesP;
	ControlPtr controlShowHiddenP;
	ControlPtr controlUseSingleFontP;
	ControlPtr controlMoveRecordOnTopP;


	formP = FrmGetActiveForm();

	// Display the correct sort order.
	popSortP = FrmGetObjectPtr(formP, FrmGetObjectIndex(formP, popPrefsSortOrder));
	listSortP = FrmGetObjectPtr(formP, FrmGetObjectIndex(formP, lstPrefsSortOrder));

	CtlSetLabel(popSortP, LstGetSelectionText(listSortP, currentSortOrder));
	LstSetSelection(listSortP, currentSortOrder);

	// Display the correct rotate order.
	popRotateP = FrmGetObjectPtr(formP, FrmGetObjectIndex(formP, popPrefsRotateOrder));
	listRotateP = FrmGetObjectPtr(formP, FrmGetObjectIndex(formP, lstPrefsRotateOrder));

	CtlSetLabel(popRotateP, LstGetSelectionText(listRotateP, currentRotateOrder));
	LstSetSelection(listRotateP, currentRotateOrder);

	// Display the correct show line setting.
	controlShowLinesP = GetActiveFormObjectPtr(chkPrefsShowLines);
	CtlSetValue(controlShowLinesP, showLines);

	// Display the correct show hidden setting.
	controlShowHiddenP = GetActiveFormObjectPtr(chkPrefsShowHidden);
	CtlSetValue(controlShowHiddenP, showHidden);

	// Display the correct use single font setting.
	controlUseSingleFontP = GetActiveFormObjectPtr(chkPrefsUseSingleFont);
	CtlSetValue(controlUseSingleFontP, useSingleFont);

	// Display the move record on top setting.
	controlMoveRecordOnTopP = GetActiveFormObjectPtr(chkPrefsMoveRecordOnTop);
	CtlSetValue(controlMoveRecordOnTopP, moveRecordOnTop);

	PrefsSetGlobalPasswordStatus();

	newSortOrder = currentSortOrder;
	newRotateOrder = currentRotateOrder;
}

/************************************
 * PrefsSortMemos:
 * Sorts the memo alphabetically. The
 * PrefsSortMemos function uses the
 * CompareRecords comparison.
 ************************************
 */

static void PrefsSetGlobalPasswordStatus(void)
{
	ControlPtr controlP;
	Char * strGlobalPassword;
	MemHandle strGlobalPasswordH;

	if (prefsGlobalPasswordP == 0) {
		// Display the status of the global password as Unassigned.
		strGlobalPasswordH = DmGetResource('tSTR', strGlobalPasswordUnassigned);
		strGlobalPassword = MemHandleLock(strGlobalPasswordH);
		
		controlP = GetActiveFormObjectPtr(selPrefsGlobalPassword);
		CtlSetLabel(controlP, strGlobalPassword);

	   	MemHandleUnlock(strGlobalPasswordH);   
		DmReleaseResource(strGlobalPasswordH); 
	} else {
		// Display the status of the global password as Assigned
		strGlobalPasswordH = DmGetResource('tSTR', strGlobalPasswordAssigned);
		strGlobalPassword = MemHandleLock(strGlobalPasswordH);
		
		controlP = GetActiveFormObjectPtr(selPrefsGlobalPassword);
		CtlSetLabel(controlP, strGlobalPassword);

	   	MemHandleUnlock(strGlobalPasswordH);   
		DmReleaseResource(strGlobalPasswordH); 
	}
}

/*******************************************
 * PrefsSortMemos:
 * Sorts the memo alphabetically.
 * The PrefsSortMemos function uses the
 * CompareRecords comparison.
 *******************************************
 */

static void PrefsSortMemos(void)
{
	if (newSortOrder != currentSortOrder) {
		if (newSortOrder == soAlphabetic) {

			// The sort order has been changed to alphabetic
			if (FrmAlert (altAlphabeticSortAlert) == 1)
					return;

		currentSortOrder = soAlphabetic;
		DmQuickSort(dbCryptoPadRef, 
			(DmComparF*) &CompareCryptoPadRecords, (Int16) currentSortOrder);
		DmQuickSort(dbMemoPadRef, 
			(DmComparF*) &CompareMemoPadRecords, (Int16) currentSortOrder);
	
		updateCode = updateTable;
		} else currentSortOrder = soManual;

	SetMemoPadSortOrder(dbMemoPadRef, currentSortOrder);

	}
}

/************************************
 * PrefsShowLines:
 * Sets the show lines attribute.
 ************************************
 */

static void PrefsShowLines(void)
{
	ControlPtr controlP;

	controlP = GetActiveFormObjectPtr(chkPrefsShowLines);
	showLines = CtlGetValue(controlP);
}

/************************************
 * PrefsShowHidden:
 * Sets the show hidden attribute (dotfiles/resource forks on SD-card
 * listings) and persists it via SetShowHidden's unsaved prefs.
 ************************************
 */

static void PrefsShowHidden(void)
{
	ControlPtr controlP;
	Boolean show;

	controlP = GetActiveFormObjectPtr(chkPrefsShowHidden);
	show = (Boolean) CtlGetValue(controlP);
	if (show != showHidden)
		SetShowHidden(show);
}

/***********************************
 * PrefsMoveRecordOnTop:
 * Sets the move record on top attribut
 ***********************************
 */

static void PrefsMoveRecordOnTop(void)
{
	ControlPtr controlP;
	Boolean moveRecord;

	controlP = GetActiveFormObjectPtr(chkPrefsMoveRecordOnTop);
	moveRecord = CtlGetValue(controlP);

	if (moveRecord && (currentSortOrder == soAlphabetic)) {
		FrmAlert (altMoveRecordOnTopImpossibleAlert);
		moveRecord = recNotMove;
	}
	
	moveRecordOnTop = moveRecord;
}

/************************************
 * PrefsUseSingleFont:
 * Sets the use single font attribute.
 ************************************
 */

static void PrefsUseSingleFont(void)
{
	ControlPtr controlP;
	Err err = 0;
	UInt16 recordNum;

	controlP = GetActiveFormObjectPtr(chkPrefsUseSingleFont);
	newUseSingleFont = CtlGetValue(controlP);

	if (newUseSingleFont != useSingleFont) {
		if (newUseSingleFont == singleFont) {
			if (FrmAlert (altUseSingleFontAlert) == 1)
				return;

			useSingleFont = singleFont;
			currentFieldFont = stdFont;
			currentTableFont = stdFont;
			while (!err) {
				recordNum = 0;
				err = DmSeekRecordInCategory (dbCryptoFontRef, &recordNum, 
					0, dmSeekForward, dmAllCategories);
				if (!err) DmRemoveRecord(dbCryptoFontRef, recordNum);
			}

		} else {
			useSingleFont = separateFont;
			SynchroniseFont();
		}

		updateCode = updateTable;
	}
}

/************************************
 * PrefsForgottenPassword:
 * Delete the global password
 * and all memos crypted with it.
 ************************************
 */

static void PrefsForgottenPassword(void)
{
	UInt16 recordNum = 0;
	Err err;

	err = DmSeekRecordInCategory(dbCryptoPadRef, &recordNum, 
			0, dmSeekForward, dmAllCategories);

	while ((err != dmErrIndexOutOfRange) 
				&& (err != dmErrSeekFailed)) {

		if (isEncrypted(recordNum)) {
			RecordType record;

			/* Db35-suite-style Sec35 wrap: route through the chokepoint --
			 * this record may now be Sec35-wrapped on top of its layer-2
			 * password. isEncrypted() just succeeded for this same record,
			 * so a read failure here is not expected in practice; if it
			 * ever happens, just move on rather than get stuck. */
			if (! CryptoGetRecord (dbCryptoPadRef, recordNum, &record))
				recordNum++;
			else {
				Char * passwordP = MemPtrNew(record.passwdLen + 1);
				StrCopy(passwordP, record.passwordP);

				if (StrCompare(passwordP, prefsGlobalPasswordP) == 0) {
					DmRemoveRecord(dbCryptoPadRef, recordNum);
					updateCode = updateTable;
				}
				else recordNum++;

				MemPtrFree(passwordP);
			}
		}

	err = DmSeekRecordInCategory(dbCryptoPadRef, &recordNum, 
			0, dmSeekForward, dmAllCategories);
	}

	prefsGlobalPasswordP = 0;
	globalPasswordP = 0;
}


/************************************
 * PrefsGlobalPassword():
 * Sets the global password
 * attribute.
 ************************************
 */

static void PrefsGlobalPassword(void)
{
	EventType event;

		if (prefsGlobalPasswordP == 0) {
			event.eType = globalPasswordStatusEvent;
			event.data.generic.datum[1] = globalPasswordNotSet;
			event.data.generic.datum[2] = frmPrefsForm;

			FrmPopupForm(frmSetGlobalPasswordForm);
			EvtAddEventToQueue(&event);
		} else {
			event.eType = globalPasswordStatusEvent;
			event.data.generic.datum[1] = globalPasswordChange;
			event.data.generic.datum[2] = frmPrefsForm;

			FrmPopupForm(frmQueryGlobalPasswordForm);
			EvtAddEventToQueue(&event);
		}
}


/************************************
 * PrefsRotateOrderSelect:
 * Pops the selection list and select
 * the current rotate order.
 ************************************
 */

static void PrefsRotateOrderSelect(void)
{
	FormPtr formP;
	ControlPtr popP;
	ListPtr listP;

	Int8 rotateOrder;

	formP = FrmGetActiveForm();
	popP = FrmGetObjectPtr(formP, FrmGetObjectIndex(formP, popPrefsRotateOrder));
	listP = FrmGetObjectPtr(formP, FrmGetObjectIndex(formP, lstPrefsRotateOrder));

	rotateOrder = LstPopupList(listP);

	if (rotateOrder >= roAll) newRotateOrder = rotateOrder;
	CtlSetLabel(popP, LstGetSelectionText(listP, newRotateOrder));
}

/************************************
 * PrefsSortOrderSelect:
 * Pops the selection list and select
 * the current sort order.
 ************************************
 */

static void PrefsSortOrderSelect(void)
{
	FormPtr formP;
	ControlPtr popP;
	ListPtr listP;

	Int16 sortOrder;

	formP = FrmGetActiveForm();
	popP = FrmGetObjectPtr(formP, FrmGetObjectIndex(formP, popPrefsSortOrder));
	listP = FrmGetObjectPtr(formP, FrmGetObjectIndex(formP, lstPrefsSortOrder));

	sortOrder = LstPopupList(listP);

	if (sortOrder >= soManual) newSortOrder = sortOrder;
	CtlSetLabel(popP, LstGetSelectionText(listP, newSortOrder));
}


Boolean PrefsFormHandleEvent(EventPtr eventP)
{
	Boolean handled = false;

	switch (eventP->eType) {
		case ctlSelectEvent:
			switch (eventP->data.ctlSelect.controlID) {
				case btnPrefsOK:
					PrefsSortMemos();
					PrefsShowLines();
					PrefsShowHidden();
					PrefsUseSingleFont();
					PrefsMoveRecordOnTop();
					currentRotateOrder = newRotateOrder;
					FrmReturnToForm(frmMainForm);
					if (updateCode)
						FrmUpdateForm(frmMainForm, updateTable);
					handled = true;
					break;

				case btnPrefsForgotten:
					if (prefsGlobalPasswordP != 0) {
						if (FrmAlert (altConfirmDeleteForgottenPasswordAlert) == 0) {
							PrefsForgottenPassword();
							PrefsSetGlobalPasswordStatus();
						} 
					} else FrmAlert (altNoGlobalPasswordToDeleteAlert);
					handled = true;
					break;

				case btnPrefsCancel:
					FrmReturnToForm(frmMainForm);
					handled = true;
					break;

				case popPrefsSortOrder:
					PrefsSortOrderSelect();
					handled = true;
					break;
	
				case popPrefsRotateOrder:
					PrefsRotateOrderSelect();
					handled = true;
					break;

				case selPrefsGlobalPassword:
					PrefsGlobalPassword();
					handled = true;
					break;
			}

		case frmCloseEvent:
			/* Let the system handle the event */
			break;

		case frmOpenEvent:
			PrefsFormInit();
			FrmDrawForm(FrmGetActiveForm());
			handled = true;
			break;
	}

	return handled;
}

