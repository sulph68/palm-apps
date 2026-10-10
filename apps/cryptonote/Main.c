/* CryptoPad : a encryption/decryption enabled memopad replacement
 *
 * Maxime Labelle & Jean-Baptiste Labelle - 2000
 */

#include "MainForm.h"
#include "EditForm.h"
#include "DetailsForm.h"
#include "catsync.h"		/* system-wide category name sync */
#include "PasswordForm.h"
#include "QueryGlobalPasswordForm.h"
#include "SetAndChangeGlobalPasswordForm.h"
#include "PrefsForm.h"
#include "Database.h"
#include "AesCrypt.h"
#include "CryptoSec35.h"
/******************************************
 * isEncrypted:
 * Tells the status of the given record
 ******************************************
 */

Boolean isEncrypted(UInt16 recordNum)
{
	/* Db35-suite-style Sec35 wrap: packedP->flag (the "Encrypt Memo" layer-2
	 * password flag) is no longer necessarily readable straight off disk --
	 * if the record is ALSO Sec35-wrapped, those bytes are ciphertext at
	 * this offset. Route through the chokepoint so this always reads the
	 * correctly-unwrapped inner flag. A Sec35-locked record can't be
	 * decrypted to check, so it reads as "not encrypted" here (no key icon
	 * shown while locked) -- matches "no key icon by default" once
	 * everything's Sec35-wrapped; the icon is specifically for layer-2. */
	RecordType	record;
	if (! CryptoGetRecord (dbCryptoPadRef, recordNum, &record))
		return false;
	return (Boolean) (record.flag == recEncrypted);
}

/* Blowfish() used to live here. It's been replaced by AESCrypt(), declared
 * in AesCrypt.h and called directly from the sites that used to call
 * Blowfish() (EditForm.c, MainForm.c, PasswordForm.c, QueryGlobalPasswordForm.c,
 * SetAndChangeGlobalPasswordForm.c). Unlike Blowfish(), AESCrypt() takes an
 * explicit per-call IV instead of a hardcoded constant - see AesCrypt.h for
 * why that's required for AES-CTR to be safe here. */

/****************************************
 * SelectFont:
 * This routine handles selection of a font
 * in the main or edit form. 
 ****************************************/
FontID SelectFont (FontID currFontID)
{
	UInt16 formID;
	FontID fontID;

	formID = (FrmGetFormId (FrmGetActiveForm ()));

	// Call the OS font selector to get the id of a font.
	fontID = FontSelect (currFontID);

	if (fontID != currFontID)
		FrmUpdateForm (formID, updateFontChanged);

	return (fontID);
} 

/***********************************
 * RomVersionCompatible
 * Check that the ROM version meets the
 * minimum requirement.                    
 ***********************************/
static Boolean RomVersionCompatible (UInt32 requiredVersion)
{
	UInt32 romVersion;

	/* See if we're on in minimum required version of the ROM or later */
	FtrGet(sysFtrCreator, sysFtrNumROMVersion, &romVersion);

	if (romVersion < requiredVersion) {
		FrmAlert (altRomIncompatibleAlert);
		return (false);
	}

	return (true);
}

Boolean ApplicationHandleEvent(EventPtr eventP)
{
	Boolean handled = false;
	Int16 formID;
	FormPtr formP;

	if (eventP->eType == frmLoadEvent) {

		// Load the form and assign its event handler
		formID = eventP->data.frmLoad.formID;
		formP = FrmInitForm(formID);
		FrmSetActiveForm(formP);

		switch (formID) {
			case frmMainForm:
				FrmSetEventHandler(formP, MainFormHandleEvent);
				break;

			case frmEditForm:
				FrmSetEventHandler(formP, EditFormHandleEvent);
				break;

			case frmDetailsForm:
				FrmSetEventHandler(formP, DetailsFormHandleEvent);
				break;

			case frmPasswordForm:
				FrmSetEventHandler(formP, PasswordFormHandleEvent);
				break;

			case frmQueryGlobalPasswordForm:
				FrmSetEventHandler(formP, QueryGlobalPasswordFormHandleEvent);
				break;

			case frmSetGlobalPasswordForm:
				FrmSetEventHandler(formP, SetAndChangeGlobalPasswordFormHandleEvent);
				break;

			case frmConfirmGlobalPasswordForm:
				FrmSetEventHandler(formP, SetAndChangeGlobalPasswordFormHandleEvent);
				break;

			case frmChangeGlobalPasswordForm:
				FrmSetEventHandler(formP, SetAndChangeGlobalPasswordFormHandleEvent);
				break;

			case frmPrefsForm:
				FrmSetEventHandler(formP, PrefsFormHandleEvent);
				break;

			case frmKeypadForm:
				FrmSetEventHandler(formP, KeypadFormHandleEvent);
				break;
		}

		handled = true;
	}

	return handled;
}

/************************************************
 * GetApplicationPrefs:
 * Load the preferences and do any fixups needed for 
 * backwards and forwards compatibility
 ************************************************/
void GetApplicationPrefs(UInt32* currentRecordID)

{
	ApplicationPreferenceType appPrefs;
	UInt16 prefSize;
	Int16 result;

	prefSize = sizeof(ApplicationPreferenceType);

	result = PrefGetAppPreferences(cryptoPadCreator, dbPrefID, &appPrefs,
												&prefSize, true);

	if (dbVersion <= result && result != noPreferenceFound) {
		currentForm      = appPrefs.currentForm;
		currentCategory  = appPrefs.currentCategory;
		currentFieldFont = appPrefs.currentFieldFont;

		currentRotateOrder	= appPrefs.currentRotateOrder;

		currentRecord    = appPrefs.currentRecord;
		topRecord        = appPrefs.topRecord;
		currentSortOrder = appPrefs.currentSortOrder;
		currentTableFont = appPrefs.currentTableFont;

		moveRecordOnTop	= appPrefs.moveRecordOnTop;

		editScrollPosition = appPrefs.editScrollPosition;
		currentDB		 = appPrefs.currentDB;
		topDB 			 = appPrefs.topDB;
		showLines		 = appPrefs.showLines;
		saveBackup		 = appPrefs.saveBackup;
		showAllCategories  = appPrefs.showAllCategories;
		useSingleFont	= appPrefs.useSingleFont;
		*currentRecordID  = appPrefs.currentRecordID;
	} 


	else {

		/* Version 3.5 introduced the currentRecordId
		 * to fix a bug when exiting of the application
		 * from the edit form, make a hotsync with a change
		 * on this record and returning into CryptoPad. 
		 * The application preferences couldn't be
		 * retrieved. Try version 3.2 preferences instead. */

	 	*currentRecordID  	= noRecordSelectedID;
	 	currentRotateOrder	= roAll;
	 	moveRecordOnTop		= recNotMove;

		prefSize = sizeof(ApplicationPreferenceTypeV32);
		result = PrefGetAppPreferences(cryptoPadCreator, dbPrefIDV32, &appPrefs,
													&prefSize, true);

		if (dbFontVersion <= result && result != noPreferenceFound) {
			currentForm      = frmMainForm;
			currentCategory  = appPrefs.currentCategory;
			currentFieldFont = appPrefs.currentFieldFont;
			currentRecord    = noRecordSelected;
			topRecord        = appPrefs.topRecord;
			currentSortOrder = appPrefs.currentSortOrder;
			currentTableFont = appPrefs.currentTableFont;
			editScrollPosition = appPrefs.editScrollPosition;
			currentDB		 = memoPadDB;
			topDB 			 = appPrefs.topDB;
			showLines		 = appPrefs.showLines;
			saveBackup		 = appPrefs.saveBackup;
			showAllCategories  = appPrefs.showAllCategories;
			useSingleFont	= appPrefs.useSingleFont;
		}
		
		else {

			/* Version 3.2 introduced a separate font 
			 * feature for each memo/category of the 3.2 version.
			 * The application preferences couldn't be
			 * retrieved. Try version 2.2 preferences instead. */

			moveRecordOnTop		= recNotMove;
			currentRotateOrder	= roAll;
			*currentRecordID  = noRecordSelectedID;
			useSingleFont	= singleFont;

			prefSize = sizeof(ApplicationPreferenceTypeV31);
			result = PrefGetAppPreferences(cryptoPadCreator, dbPrefIDV31, &appPrefs,
												&prefSize, true);

			if (dbGlobalPWVersion <= result && result != noPreferenceFound) {
				currentForm      = frmMainForm;
				currentCategory  = appPrefs.currentCategory;
				currentFieldFont = stdFont;
				currentRecord    = noRecordSelected;
				topRecord        = appPrefs.topRecord;
				currentSortOrder = appPrefs.currentSortOrder;
				currentTableFont = stdFont;
				editScrollPosition = appPrefs.editScrollPosition;
				currentDB		 = memoPadDB;
				topDB 			 = appPrefs.topDB;
				showLines		 = appPrefs.showLines;
				saveBackup		 = appPrefs.saveBackup;
				showAllCategories = appPrefs.showAllCategories;
			} 

			else {
				/* Start with default values */
				currentForm     	= frmMainForm;
				currentRecord   	= noRecordSelected;
				currentCategory 	= dmAllCategories;
				currentFieldFont 	= stdFont;
				currentTableFont 	= stdFont;
				topRecord  		    = 0;
				currentSortOrder 	= soManual;
				currentDB			= memoPadDB;
    			topDB				= memoPadDB;
				showLines			= linesShown;
				editScrollPosition 	= 0;
	    		saveBackup			= true;
				showAllCategories 	= true;
				useSingleFont 		= singleFont;

				*currentRecordID  = noRecordSelectedID;
				currentRotateOrder	= roAll;
				moveRecordOnTop		= recNotMove;
			}
		}
	}
}

/************************************************
 * SetApplicationPrefs:
 * Save the preferences and do any fixups needed for 
 * backwards and forwards compatibility
 ************************************************/
void SetApplicationPrefs(UInt16 scrollPosition)
{
	ApplicationPreferenceType appPrefs;
	UInt32 uniqueID;

	/* Save the current settings for later
	 * execution of the application.
	 */

	appPrefs.currentForm      = currentForm;
	appPrefs.currentCategory  = currentCategory;
	appPrefs.currentFieldFont = currentFieldFont;
	appPrefs.currentRecord    = currentRecord;
	appPrefs.topRecord        = topRecord;
	appPrefs.currentSortOrder = currentSortOrder;
	appPrefs.currentTableFont = currentTableFont;
	appPrefs.editScrollPosition = scrollPosition;
	appPrefs.currentDB 		  = currentDB;
	appPrefs.topDB       	  = topDB;
	appPrefs.showLines 		  = showLines;
	appPrefs.saveBackup		  = saveBackup;
	appPrefs.showAllCategories = showAllCategories;
	appPrefs.useSingleFont	  = useSingleFont;
	appPrefs.currentRotateOrder = currentRotateOrder;
	appPrefs.moveRecordOnTop	= moveRecordOnTop;

	// Get the current record's unique id and save it with the state 
	// information.
	if ((currentDB == memoPadDB) & (DmQueryRecord (dbMemoPadRef, currentRecord) != 0))
		{
		DmRecordInfo (dbMemoPadRef, currentRecord, NULL, &uniqueID, NULL);
		appPrefs.currentRecordID = uniqueID;
		}
	else {
		appPrefs.currentRecordID = noRecordSelectedID;
		appPrefs.currentRecord = noRecordSelected;
	}

	PrefSetAppPreferences(cryptoPadCreator, dbPrefID, dbVersion,
							&appPrefs, sizeof(ApplicationPreferenceType), true);
}

/************************************************
 * GetCryptoDefaults / SetCryptoDefaults:
 * "Show Hidden" lives outside the HotSync-backed prefs blob above -
 * unsaved (local-only) prefs, like Address35's A35Defaults.
 ************************************************/
static void GetCryptoDefaults(CryptoDefaults *d)
{
	UInt16 size = sizeof(*d);
	Int16 r = PrefGetAppPreferences(cryptoPadCreator, dbUnsavedPrefID, d, &size,
		false);
	if (r == noPreferenceFound || size < sizeof(*d) || d->defVer != dbDefaultsVer) {
		d->defVer = dbDefaultsVer;
		d->showHidden = false;		/* Show Hidden starts unchecked */
	}
}

static void SetCryptoDefaults(const CryptoDefaults *d)
{
	PrefSetAppPreferences(cryptoPadCreator, dbUnsavedPrefID, dbDefaultsVer,
		(void *) d, sizeof(*d), false);	/* saved=false -> unsaved prefs DB */
}

void SetShowHidden(Boolean show)
{
	CryptoDefaults d;
	GetCryptoDefaults(&d);
	d.showHidden = (Boolean) show;
	SetCryptoDefaults(&d);
	showHidden = show;
}


/* Standard routines */

static Err StartApplication(void)
{
	UInt16 recAttributes;
	UInt16 mode;
	UInt32 uniqueID;
	UInt32 currentRecordID = 0;
	Boolean recordFound;

	if (!RomVersionCompatible (version))
	return 1;

	FtrGet(sysFtrCreator, sysFtrNumROMVersion, &OSVersion);

	// Determime if secret record should be shown.
	if (OSVersion < OS35) {
		if (PrefGetPreference(prefHidePrivateRecordsV33))
			PrivateRecordVisualStatus = hidePrivateRecords;
		else PrivateRecordVisualStatus = showPrivateRecords;
	} else
		PrivateRecordVisualStatus = (privateRecordViewEnum)PrefGetPreference(prefShowPrivateRecords);
	
	if (PrivateRecordVisualStatus == hidePrivateRecords)
		mode = dmModeReadWrite;
	else mode = dmModeReadWrite | dmModeShowSecret;

	dbCryptoPadRef = OpenOrCreateCryptoDatabase(mode);
	dbMemoPadRef = OpenMemoDatabase(mode);
	dbCryptoFontRef = OpenOrCreateFontDatabase();

	SynchroniseCategories();
	CatSync();		/* union category names across the era apps + ROM apps */
	EncRegisterSleepWipe();	/* Sec35 "Lock on Power-Off" -- see enccore.h */

	/* Db35-suite-style Sec35 wrap: silently migrate/seal existing CryptoPad
	 * records to the new format, and offer to migrate any MemoPad records
	 * into CryptoPad (Sec35-wrapped) -- every launch, not just once. Kept
	 * to this single call (all the real logic lives in CryptoSec35.c/
	 * sec001, not here -- main is critically tight on code space). */
	CryptoLaunchMaintenance();

	GetPrefsGlobalPassword();

	/* Read the application preferences */
	GetApplicationPrefs(&currentRecordID);
	{ CryptoDefaults d; GetCryptoDefaults(&d); showHidden = d.showHidden; }

	/* Change the sort order of MemoPad if it is
	 * different from the current sort order
	 * and sort alphabetically the CryptoPad
	 * database if the current sort order is so.
	 */
	 SetMemoPadSortOrder(dbMemoPadRef, currentSortOrder);

	 if (currentSortOrder == soAlphabetic) 
	 	DmQuickSort(dbCryptoPadRef,
			(DmComparF*) &CompareCryptoPadRecords, soAlphabetic);


	if (useSingleFont == separateFont) SynchroniseFont();

	/* Check that the category still exists; it
	 * may have been deleted after a hotsync.
	 */

	CategoryGetName(dbMemoPadRef, currentCategory, categoryName);
	if (*categoryName == 0) {
		currentCategory = dmAllCategories;
		showAllCategories = true;
	}	


	/* Count the records in both databases */
	if (showAllCategories)
		memosInCategory = DmNumRecordsInCategory (dbMemoPadRef, dmAllCategories)
			+ DmNumRecordsInCategory (dbCryptoPadRef, dmAllCategories);
	else
		memosInCategory = DmNumRecordsInCategory (dbMemoPadRef, currentCategory)
			+ DmNumRecordsInCategory (dbCryptoPadRef, currentCategory);

	/* If we have to display the mainForm, don't worry about
	 * the previously selected record.
	 */
	if (currentForm == frmMainForm) return 0;


	/* Check that the current record still exists and hasn't
	 * been deleted as a result of a hotsync.
	 */

	if ( DmQueryRecord (dbMemoPadRef, currentRecord) != 0) {
		DmRecordInfo (dbMemoPadRef, currentRecord, &recAttributes, &uniqueID, NULL);
		recordFound = (uniqueID == currentRecordID) && 
			((PrivateRecordVisualStatus == showPrivateRecords) || 
			(!(recAttributes & dmRecAttrSecret)));
	}

	if (! recordFound) {
		topRecord = 0;
		currentRecord = noRecordSelected;
		currentForm = frmMainForm;
		editScrollPosition = 0;
		}

	return 0;
}

static void EventLoop(void)
{
	EventType event;
	UInt16 error;
	UInt32 timeout;

	do {
		/* Password masking reveal timer (PasswordForm.c) -- wake up exactly
		 * when a just-typed character's reveal window ends even with no
		 * other event pending, instead of only masking on the next keystroke. */
		timeout = evtWaitForever;
		if (gPwRevealUntilTicks) {
			UInt32 now = TimGetTicks();
			timeout = (gPwRevealUntilTicks > now) ? (gPwRevealUntilTicks - now) : 0;
		}
		EvtGetEvent(&event, timeout);
		if (event.eType == nilEvent) {
			if (gPwRevealUntilTicks && TimGetTicks() >= gPwRevealUntilTicks)
				PasswordMaskPendingReveal();
		}

		if (event.eType == keyDownEvent)
			if ((event.data.keyDown.chr == hardPowerChr) || 
				(event.data.keyDown.chr == autoOffChr)) {
				if ((currentDB == cryptoPadDB) && (currentPasswordP != 0)) {
					FrmCloseAllForms();
					FrmGotoForm(frmMainForm);
				}
			globalPasswordP = 0;
			}

		if (!SysHandleEvent(&event))
			if (!MenuHandleEvent(0, &event, &error))
					if (!ApplicationHandleEvent(&event))
						FrmDispatchEvent(&event);
		} while (event.eType != appStopEvent);
}

static void StopApplication(void)
{
	UInt16 scrollPosition = 0;
	FormPtr formP;
	FieldPtr fieldP;

	// If we are in the Edit form, get the current scroll position.	
	if ((currentForm == frmEditForm) && (currentRecord != noRecordSelected)) {
		formP = FrmGetFormPtr (frmEditForm);
		fieldP = FrmGetObjectPtr (formP, FrmGetObjectIndex (formP, fldEditText));
		scrollPosition = FldGetScrollPosition (fieldP);
	}

	// Close all open forms,  this will force any unsaved data to
	// be written to the database.
	FrmCloseAllForms();
	EncUnregisterSleepWipe();

	/* Get the current record's unique id and save it with the state
	 * information.
	 */
	SetPrefsGlobalPassword(dbCryptoPadRef);

	/* Save the current settings for lat
	 * execution of the application.
	 */
	SetApplicationPrefs(scrollPosition);

	DmCloseDatabase(dbMemoPadRef);
	DmCloseDatabase(dbCryptoPadRef);
	DmCloseDatabase(dbCryptoFontRef);
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
	Err err;
	Boolean appIsActive;

	if (cmd == sysAppLaunchCmdNormalLaunch) {
		err = StartApplication();
		if (err) return (1);

		FrmGotoForm(currentForm);

		EventLoop();
		StopApplication();
	}

	else if (cmd == sysAppLaunchCmdFind) {
		appIsActive = launchFlags & sysAppLaunchFlagSubCall;
		Search ((FindParamsPtr)cmdPBP, appIsActive);
		}
	
	
	/* This action code might be sent to the app when it's already running
	*  if the use hits the Find soft key next to the Graffiti area.
	*/
	else if (cmd == sysAppLaunchCmdGoTo) {
		if (launchFlags & sysAppLaunchFlagNewGlobals) 
			{
			err = StartApplication ();
			if (err) return (1);
			GoToItem ((GoToParamsPtr) cmdPBP, true);
			
			EventLoop ();
			StopApplication ();
		} else {
			/* The app was already running.  Open a form to display the record */
			GoToItem ((GoToParamsPtr) cmdPBP, false);
			}
		}
	/* Launch code sent to running app to save all data.
	* This is neccessary before a find for some applications because
	* some apps do not edit in place and the user might search for
	* data not saved.  Or saving a record might reorder the database.
	* Saving before a find avoids finding a record, saving, resorting,
	* and then displaying a record moved to the position of the found
	* record.
	*/
	else if (cmd == sysAppLaunchCmdSaveData) {
		FrmSaveAllForms ();
		}

	return 0;
}
