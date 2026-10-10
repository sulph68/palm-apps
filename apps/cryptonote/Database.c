#define ALLOW_ACCESS_TO_INTERNALS_OF_FINDPARAMS

#include "Database.h"
#include "CryptoSec35.h"

#include <SysEvtMgr.h> 	//	Needed for search for EvtSysEventAvail

static MemPtr  ApplicationInfoGetPtr(DmOpenRef);
static void InitAppInfoBlock(DmOpenRef, LocalID, LocalID);
static void SynchroniseFontByRef(DmOpenRef);
static void SynchroniseTableFontForCurrentCategoryByRef(DmOpenRef);

DmOpenRef dbCryptoPadRef;
DmOpenRef dbMemoPadRef;
DmOpenRef dbCryptoFontRef;

// Added this flag to allow other places (notably Beam.c) to decide which
// MemoDB creator to target)

Boolean newMemoDB = true;

UInt16 currentForm;
UInt16 currentCategory;
FontID currentFieldFont;

FontID currentTableFont;
Boolean useSingleFont;
UInt16 currentRecord;
UInt16 topRecord;
UInt16 currentSortOrder;
Boolean currentDB;
Boolean topDB;
Boolean selectedDB;
Boolean saveBackup;
Boolean showLines;
Boolean showHidden = false;
UInt16 editScrollPosition;
Boolean showAllCategories;
UInt8 	currentRotateOrder;
Boolean moveRecordOnTop = 1;

UInt16 memosInCategory;

Char categoryName[dmCategoryLength];
Char * currentPasswordP;
Char * globalPasswordP;
Char * prefsGlobalPasswordP;
UInt8	prefsGlobalPasswordIV[16];
UInt8	prefsGlobalPasswordSalt[16];
UInt16	prefsGlobalPasswordRounds;
privateRecordViewEnum PrivateRecordVisualStatus;
UInt32 OSVersion;

/****************************************
 * OpenOrCreateMemoDatabase:
 * Open or creates the crypto database and
 * sets the default categories and
 * application preferences
 ****************************************
 */

DmOpenRef OpenMemoDatabase(UInt16 mode)
{
	DmOpenRef dbReference;

	/* Open the database */

	dbReference = DmOpenDatabaseByTypeCreator(memoPadType, memoPadCreatorNew, mode);
    if (! dbReference) {
        newMemoDB = false;
	    dbReference = DmOpenDatabaseByTypeCreator(memoPadType, memoPadCreator, mode);
    }
	ErrFatalDisplayIf(dbReference == 0, "Unable to open MemoPadDB !");

	return dbReference;
}

/****************************************
 * OpenOrCreateCryptoDatabase:
 * Open or creates the crypto database and
 * sets the default categories and
 * application preferences
 ****************************************
 */

DmOpenRef OpenOrCreateCryptoDatabase(UInt16 mode)
{
	Err err;
	DmOpenRef dbReference;

	LocalID dbID;
	LocalID appInfoID;

	UInt16 dbAttr;
	UInt16 dbAppVersion;

	/* Open the database */

	dbID = DmFindDatabase (cardNo, cryptoPadName);
	if (dbID != 0)
		dbReference = DmOpenDatabase (cardNo, dbID, mode);
	else {	
	/* The database doesn't exist. Create it. */

		err = DmCreateDatabase(cardNo, cryptoPadName, cryptoPadCreator, cryptoPadType, false);
		ErrFatalDisplayIf(err, "Unable to create database");

		/* Open the database and get its reference */

		dbReference = DmOpenDatabaseByTypeCreator(cryptoPadType, cryptoPadCreator, mode);
		ErrFatalDisplayIf(dbReference == 0, "Could not find database.");

		/* Check the presence of the application info block */

		err = DmOpenDatabaseInfo(dbReference, &dbID, 0, 0, 0, 0);
		ErrFatalDisplayIf(err, "Unable to open database information");

		DmDatabaseInfo(cardNo, dbID, 0, &dbAttr, 0, 0,
							0, 0, 0, &appInfoID,
							0, 0, 0);
		
		dbAttr |= dmHdrAttrBackup;

		DmSetDatabaseInfo(cardNo, dbID, 0, &dbAttr, 0, 0,
						0, 0, 0, &appInfoID, 0, 0, 0);
		
		if (appInfoID == 0) {
			/* The application info block doesn't exist. Create it. */
			InitAppInfoBlock(dbReference, dbID, appInfoID);
		}
	}

	/* Make sure the database back itself up during a sync.
	 * This is temporarily put here, as it really should be
	 * done at Database creation. However, this will work for
	 * people who have used previous versions of CryptoPad.
	 */

	DmOpenDatabaseInfo(dbReference, &dbID, 0, 0, 0, 0);
	
	DmDatabaseInfo(cardNo, dbID, 0, &dbAttr, &dbAppVersion, 0,
						0, 0, 0, &appInfoID, 0, 0, 0);
	
	if (dbAppVersion < dbGlobalPWVersion)
		InitAppInfoBlock(dbReference, dbID, appInfoID);

	/* Initialize variabled */
	currentPasswordP = 0;
	globalPasswordP = 0;
	currentSortOrder = soManual;

	return dbReference;
}

/****************************************
 * OpenOrCreateFontDatabase:
 * Open or creates the font database and
 * synchronise the records with the
 * CryptoPad and MemoPad database
 ****************************************
 */

DmOpenRef OpenOrCreateFontDatabase(void)
{
	Err err;
	UInt16 mode = dmModeReadWrite;
	DmOpenRef dbReference;

	LocalID dbID;
	LocalID appInfoID;
	
	UInt16 dbAttr;
	UInt16 dbAppVersion;

	/* Open the database */

	dbID = DmFindDatabase (cardNo, cryptoFontName);
	if (dbID != 0)
		dbReference = DmOpenDatabase (cardNo, dbID, mode);
	else {	
		/* The database doesn't exist. Create it. */

		err = DmCreateDatabase(cardNo, cryptoFontName, cryptoPadCreator, cryptoPadType, false);
		ErrFatalDisplayIf(err, "Unable to create database");

		/* Open the database and get its reference */
		
		dbID = DmFindDatabase (cardNo, cryptoFontName);
		dbReference = DmOpenDatabase (cardNo, dbID, mode);
		ErrFatalDisplayIf(dbReference == 0, "Could not find database.");

		/* Check the presence of the application info block */
		DmOpenDatabaseInfo(dbReference, &dbID, 0, 0, 0, 0);
		
		DmDatabaseInfo(cardNo, dbID, 0, &dbAttr, &dbAppVersion, 0,
						0, 0, 0, &appInfoID, 0, 0, 0);
		
		dbAttr |= dmHdrAttrBackup;

		DmSetDatabaseInfo(cardNo, dbID, 0, &dbAttr, 0, 0,
						0, 0, 0, &appInfoID, 0, 0, 0);
	}

	/* Initialize variabled */

	return dbReference;
}

/***********************************************************************
 * InitAppInfoBlock:
 * Init the ApplicationInfo block.
 ***********************************************************************/
static void InitAppInfoBlock(DmOpenRef dbP, 
		LocalID dbID, LocalID appInfoID)
{
	UInt16 dbAppVersion;

	MemHandle handle;
	ApplicationInfoPtr appInfoP;
	ApplicationInfoPtr initAppInfoP;

	
	/* Create the new AppInof block if it does not exit
	* or it is a previous version.
	*/

	handle = DmNewHandle(dbP, sizeof(ApplicationInfoType));
	ErrFatalDisplayIf(handle == 0, "Unable to create application info block");

	/* Map the application info block to the database. */

	appInfoID = MemHandleToLocalID(handle);
	dbAppVersion = dbVersion;
	DmSetDatabaseInfo(cardNo, dbID, 0, 0, &dbAppVersion, 0,
		0, 0, 0, &appInfoID, 0, 0, 0);
	
	/* Clears the application info block */

	appInfoP = MemLocalIDToLockedPtr(appInfoID, cardNo);
	DmSet (appInfoP, 0, sizeof(ApplicationInfoType), 0);
	MemPtrUnlock(appInfoP);
}

/***********************************************************************
 * GetPrefsGlobalPassword:
 * Get the global password from
 * the ApplicationInfo.
 ***********************************************************************/
void GetPrefsGlobalPassword (void)
{
	Int8      length;
	ApplicationInfoPtr appInfoP;

	appInfoP = (ApplicationInfoPtr) ApplicationInfoGetPtr(dbCryptoPadRef); 

	length = StrLen(&(appInfoP->prefsGlobalPassword[0]));

	prefsGlobalPasswordP = MemPtrNew(length + 1);
	
	if (length != 0) {
		StrCopy(prefsGlobalPasswordP, &(appInfoP->prefsGlobalPassword[0]));
		MemMove(prefsGlobalPasswordIV, appInfoP->globalPasswordIV,
			sizeof(prefsGlobalPasswordIV));
		MemMove(prefsGlobalPasswordSalt, appInfoP->globalKdfSalt,
			sizeof(prefsGlobalPasswordSalt));
		prefsGlobalPasswordRounds = appInfoP->globalKdfRounds;
	}
	else prefsGlobalPasswordP = 0;
      
	MemPtrUnlock(appInfoP);
}

/***********************************************************************
 * SetPrefsGlobalPassword:
 * Get the global password from
 * the ApplicationInfo.
 ***********************************************************************/
void SetPrefsGlobalPassword(DmOpenRef dbP)
{
	ApplicationInfoPtr    appInfoP;
	ApplicationInfoPtr    appMemoInfoP;
	ApplicationInfoPtr    nilP = 0;

	// Get a copy of the app info
	appInfoP = (ApplicationInfoPtr) ApplicationInfoGetPtr(dbP);
	ErrFatalDisplayIf(appInfoP == NULL, 
    	"Bad database (invalid or no app info block)");

	appMemoInfoP = (ApplicationInfoPtr) ApplicationInfoGetPtr(dbMemoPadRef);
	ErrFatalDisplayIf(appMemoInfoP == NULL, 
    	"Bad database (invalid or no app info block)");
      
	DmSet(appInfoP, 0, sizeof(ApplicationInfoType), 0);

	/* Copy the shared category-info prefix (everything before
	 * prefsGlobalPassword) from the real MemoPad's app info block. Uses
	 * an explicit field offset rather than the old
	 * sizeof(ApplicationInfoType)-sizeof(prefsGlobalPassword) trick,
	 * which silently breaks now that globalPasswordIV follows
	 * prefsGlobalPassword in the struct. */

	DmWrite(appInfoP, 0, appMemoInfoP, (UInt32)&nilP->prefsGlobalPassword);

	// Make the changes
	if (prefsGlobalPasswordP) {
		DmWrite(appInfoP, (UInt32)&nilP->prefsGlobalPassword,
			prefsGlobalPasswordP, StrLen(prefsGlobalPasswordP));

		DmWrite(appInfoP, (UInt32)&nilP->globalPasswordIV,
			prefsGlobalPasswordIV, sizeof(prefsGlobalPasswordIV));

		DmWrite(appInfoP, (UInt32)&nilP->globalKdfSalt,
			prefsGlobalPasswordSalt, sizeof(prefsGlobalPasswordSalt));

		DmWrite(appInfoP, (UInt32)&nilP->globalKdfRounds,
			&prefsGlobalPasswordRounds, sizeof(prefsGlobalPasswordRounds));
	}

	// Unlock app info
	MemPtrUnlock(appInfoP);
	MemPtrUnlock(appMemoInfoP);

}

/************************************************************
 * SetMemoPadSortOrder:
 * Set the sort order setting of MemoPad.
 *************************************************************/
Err SetMemoPadSortOrder(DmOpenRef dbP, Boolean sortOrder)
{
	MemoApplicationInfoPtr appInfoP;
	MemoApplicationInfoPtr	nilP = 0;


	appInfoP = (MemoApplicationInfoPtr) ApplicationInfoGetPtr (dbP);

	if (appInfoP->sortOrder != sortOrder)
		DmWrite (appInfoP, (UInt32)&nilP->sortOrder, &sortOrder, sizeof(appInfoP->sortOrder));
		
	MemPtrUnlock (appInfoP);	

	return 0;
}


/************************************************************
 * GetMemoPadSortOrder:
 * Get the sort order setting of MemoPad.
 *************************************************************/

UInt8 GetMemoPadSortOrder (DmOpenRef dbP)
{
	UInt8 sortOrder;
	MemoApplicationInfoPtr appInfoP;
		
	appInfoP = (MemoApplicationInfoPtr) ApplicationInfoGetPtr (dbP);

	sortOrder = appInfoP->sortOrder;

	MemPtrUnlock (appInfoP);	

	return (sortOrder);
}

/************************************************************
 * ApplicationInfoGetPtr:
 * Return a locked pointer to the ApplicationInfo or NULL
 *************************************************************/
static MemPtr   ApplicationInfoGetPtr(DmOpenRef dbP)
{
	UInt16   	  cardNum;
	LocalID    dbID;
	LocalID    appInfoID;
   
	if (DmOpenDatabaseInfo(dbP, &dbID, NULL, NULL, &cardNum, NULL))
    	return NULL;
	if (DmDatabaseInfo(cardNum, dbID, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &appInfoID, NULL, NULL, NULL))
	    return NULL;

	if (appInfoID == NULL)
		return NULL;
	else
    	return MemLocalIDToLockedPtr(appInfoID, cardNum);
   
}   

/**************************************
 * SynchroniseFont:
 * Synchronise the record
 * between both databases and
 * the CryptoFont database.
 **************************************
 */

void SynchroniseFont(void)
{
	UInt16 fontRecordNum;
	UInt16 recordNum;
	Boolean removed = false;

	MemHandle handle;
	FontRecordPtr fontRecordP;

	Err err;
	Err err2;

	/* Checks if records have been deleted in
	 * the CryptoPad/MemoPad database.
	 */
	fontRecordNum = 0;
	err = DmSeekRecordInCategory (dbCryptoFontRef, &fontRecordNum, 
			0, dmSeekForward, dmAllCategories);

	while (err != dmErrIndexOutOfRange) {
		handle = DmQueryRecord (dbCryptoFontRef, fontRecordNum);
		fontRecordP = MemHandleLock(handle);

		selectedDB = fontRecordP->db;

		err2 = DmFindRecordByID (selectedDBRef, fontRecordP->uniqueID, &recordNum);

		MemHandleUnlock(handle);
		
		fontRecordNum++;
		
	if 	(err2 == dmErrUniqueIDNotFound) {
			fontRecordNum--;
			removed = true;
			DmRemoveRecord(dbCryptoFontRef, fontRecordNum);
		}

		err = DmSeekRecordInCategory (dbCryptoFontRef, &fontRecordNum, 
			0, dmSeekForward, dmAllCategories);
	}

	/* Synchronise the records between
	* the font database and both CryptoPad
	* and MemoPad databases.
	*/
	if (((DmNumRecords (dbMemoPadRef) + DmNumRecords (dbCryptoPadRef))
			!= DmNumRecords (dbCryptoFontRef)) || removed) {

		MemHandle strSynchroniseFontH;
		Char * strSynchroniseFontP;
		RectangleType tempRect;

		strSynchroniseFontH = DmGetResource('tSTR', strSynchroniseFont);
		strSynchroniseFontP = MemHandleLock(strSynchroniseFontH);

		RctSetRectangle (&tempRect, 15, 80, 130, 20);
		WinEraseRectangle (&tempRect, 2);
		WinDrawRectangleFrame(popupFrame, &tempRect);

		FntSetFont(stdFont);
		DrawStringWithEllipsis (strSynchroniseFontP, 25, 85, 135, justCenter);
	   	MemHandleUnlock(strSynchroniseFontH);
		DmReleaseResource(strSynchroniseFontH); 

		SynchroniseFontByRef(dbCryptoPadRef);
		SynchroniseFontByRef(dbMemoPadRef);
		WinEraseWindow ();

	}

}

/**************************************
 * SynchroniseFontByRef:
 * Synchronise the record
 * between the given database
 * and the CryptoFont database.
 **************************************
 */

static void SynchroniseFontByRef(DmOpenRef dbRef)
{
	UInt16 recordNum = 0;
	UInt16 fontRecordNum;
	UInt16 recAttributes;

	MemHandle handle;
	FontRecordPtr fontRecordP;

	UInt32 recordID;
	Err err;
	Err err2;

	/* Checks if records have been added in
	 * the CryptoPad/MemoPad database.
	 */
	err = DmSeekRecordInCategory (dbRef, &recordNum, 
			0, dmSeekForward, dmAllCategories);

	while (err != dmErrIndexOutOfRange) {
		recordID = 0;
		DmRecordInfo (dbRef, recordNum, &recAttributes, &recordID, 0);

		fontRecordNum = 0;
		err2 = DmSeekRecordInCategory (dbCryptoFontRef, 
			&fontRecordNum, 0, dmSeekForward, dmAllCategories);

		while (err2 != dmErrIndexOutOfRange) {
			handle = DmQueryRecord (dbCryptoFontRef, fontRecordNum);
			fontRecordP = MemHandleLock(handle);
			
			selectedDB = fontRecordP->db;

			if ((fontRecordP->uniqueID == recordID) &&
				(selectedDBRef == dbRef)) {
				MemHandleUnlock(handle);
				break;
			}

			MemHandleUnlock(handle);

			fontRecordNum++;
			err2 = DmSeekRecordInCategory (dbCryptoFontRef, 
				&fontRecordNum, 0, dmSeekForward, dmAllCategories);
		}

		if (err2 == dmErrIndexOutOfRange) {
			GetTableFontOfCategory(recAttributes & dmRecAttrCategoryMask);
			CreateFontRecord(dbRef, recordID);
		}

		recordNum++;
		err = DmSeekRecordInCategory (dbRef, &recordNum, 
			0, dmSeekForward, dmAllCategories);
	}
}

/******************************************
 * SynchroniseTableFontForCurrentCategory:
 * Synchronise the table font 
 * of each record of the current
 * category with currentTableFont.
 ******************************************
 */

void SynchroniseTableFontForCurrentCategory(void)
{
	/* Synchronise the tableFont for records
	* of MemoPad database.
	*/
	SynchroniseTableFontForCurrentCategoryByRef(dbMemoPadRef);

	/* Synchronise the tableFont for records
	* of CryptoPad database.
	*/
	SynchroniseTableFontForCurrentCategoryByRef(dbCryptoPadRef);
}

/**********************************************
 * SynchroniseTableFontForCurrentCategoryByRef:
 * Synchronise the table font 
 * of each record of the current
 * category with currentTableFont
 * for both databases.
 *********************************************
 */

static void SynchroniseTableFontForCurrentCategoryByRef(DmOpenRef dbRef)
{
	UInt16 recordNum = 0;
	UInt16 fontRecordNum;

	MemHandle handle;
	FontRecordPtr fontRecordP;

	UInt32 recordID;
	Err err;
	Err err2;

	err = DmSeekRecordInCategory (dbRef, &recordNum, 
			0, dmSeekForward, currentCategory);

	while ((err != dmErrIndexOutOfRange) &&
		(err != dmErrSeekFailed)) {
		recordID = 0;
		DmRecordInfo (dbRef, recordNum, 0, &recordID, 0);
		SetOrGetRecordFont(dbRef, recordID, setFont, fontTable);

		recordNum++;
		err = DmSeekRecordInCategory (dbRef, &recordNum, 
			0, dmSeekForward, currentCategory);
	}
}

/**************************************
 * GetTableFontOfCategory:
 * Get the tableFont of
 * the current category.
 **************************************
 */

void GetTableFontOfCategory(UInt16 category)
{
	UInt32 recordID;
	UInt16 recordNum;
	MemHandle record = 0;
	Err err = 1;

	selectedDB = memoPadDB;
	recordNum = 0;

	while (err) {
		record = DmQueryNextInCategory(selectedDBRef, &recordNum, category);

		if (record) {
			/* Try to get the table font if this
			* record is present in the fontRecord
			* database.
			*/
			DmRecordInfo(selectedDBRef, recordNum, 0, &recordID, 0);
			err = SetOrGetRecordFont(selectedDBRef, recordID, getFont, fontTable);
		} else err = true;

		recordNum++;

		if (record == 0) {
			if (selectedDB == memoPadDB) {
			/* If record is null, set the currentDB to CryptoPadDB
			* to continue the search in the CryptoPad database from
			* the record 0.
			*/
				selectedDB = cryptoPadDB;
				recordNum = 0;
			} else {
			/* If record is null, there is no record
			* in the current category so set the
			* font to standard.
			*/
				currentTableFont = stdFont;
				break;
			}
		}
	}		
}

/**************************************
 * CreateFontRecord:
 * Create a record in the
 * font database.
 **************************************
 */

void CreateFontRecord(DmOpenRef dbRef, UInt32 recordID)
{
	UInt16 index, recordNum;

	FontRecordType fontRecord;
	FontRecordPtr fontRecordP;
	MemHandle handle;

	index = dmMaxRecordIndex;

	/* Create a new record in the MemoPad database */
	handle = DmNewRecord(dbCryptoFontRef, &index, sizeof(fontRecord));
	ErrFatalDisplayIf(handle == 0, "Device full.");

	/* Lock down the block containing the new record */
	fontRecordP = MemHandleLock(handle);

	/* Write the default datas in the new record */
	if (dbRef == dbMemoPadRef)
		fontRecord.db = memoPadDB;
	else fontRecord.db = cryptoPadDB;
	fontRecord.uniqueID = recordID;

	DmFindRecordByID (dbRef, recordID, &recordNum);

	fontRecord.tableFont = currentTableFont;
	fontRecord.fieldFont = stdFont;

	DmWrite(fontRecordP, 0, &fontRecord, sizeof(FontRecordType));

	/* Unlock the block of the new record */
	MemHandleUnlock(handle);

	DmReleaseRecord(dbCryptoFontRef, index, true);	
}


/**************************************
 * DeleteFontRecord:
 * Create a record in the
 * font database.
 **************************************
 */

void DeleteFontRecord(DmOpenRef dbRef, UInt32 recordID)
{
	UInt16 index;
	UInt16 fontRecordNum = 0;

	MemHandle handle;
	FontRecordPtr fontRecordP;
	Err err;

	err = DmSeekRecordInCategory (dbCryptoFontRef, &fontRecordNum, 
			0, dmSeekForward, dmAllCategories);

	while (err != dmErrIndexOutOfRange) {
		handle = DmQueryRecord (dbCryptoFontRef, fontRecordNum);
		fontRecordP = MemHandleLock(handle);
		
		fontRecordNum++;
		
		selectedDB = fontRecordP->db;
		if ((fontRecordP->uniqueID == recordID) &&
				(selectedDBRef == dbRef)) {
			fontRecordNum--;
			MemHandleUnlock(handle);
			DmRemoveRecord(dbCryptoFontRef, fontRecordNum);
			break;
		}

		MemHandleUnlock(handle);

		err = DmSeekRecordInCategory (dbCryptoFontRef, &fontRecordNum, 
			0, dmSeekForward, dmAllCategories);
	}

	ErrFatalDisplayIf(err == dmErrIndexOutOfRange, "Unable to find the fontRecord to delete !");
}

/**************************************
 * SetOrGetRecordFont:
 * Change the record font
 * according to user choice.
 **************************************
 */

Boolean SetOrGetRecordFont(DmOpenRef dbRef, UInt32 recordID, 
		Boolean SetOrGet, Boolean TableOrFieldFont)
{
	UInt16 fontRecordNum = 0;

	MemHandle handle;
	FontRecordPtr fontRecordP;
	FontRecordType fontRecord;
	
	Boolean db;

	Err		err;
	Err		err2;

	err2 = DmSeekRecordInCategory (dbCryptoFontRef, 
		&fontRecordNum, 0, dmSeekForward, dmAllCategories);

	while (err2 != dmErrIndexOutOfRange) {
		handle = DmQueryRecord (dbCryptoFontRef, fontRecordNum);
		fontRecordP = MemHandleLock(handle);

		if (dbRef == dbMemoPadRef)
			db = memoPadDB;
		else db = cryptoPadDB;
		
		if ((fontRecordP->uniqueID == recordID) &&
				(fontRecordP->db == db)) {
			if (SetOrGet == setFont) {
				fontRecord.db = fontRecordP->db;
				fontRecord.uniqueID = fontRecordP->uniqueID;
			
				if (TableOrFieldFont == fontTable) {
					fontRecord.tableFont = currentTableFont;
					fontRecord.fieldFont = fontRecordP->fieldFont;
				} else {
					fontRecord.fieldFont = currentFieldFont;
					fontRecord.tableFont = fontRecordP->tableFont;
				}

				err = DmWrite(fontRecordP, 0, &fontRecord, sizeof(FontRecordType));
				ErrFatalDisplayIf(err, "Could not write to new record.");
			} else {
				currentTableFont = fontRecordP->tableFont;
				currentFieldFont = fontRecordP->fieldFont;
			}
			MemHandleUnlock(handle);
			break;
		}

		MemHandleUnlock(handle);

		fontRecordNum++;
		err2 = DmSeekRecordInCategory (dbCryptoFontRef, 
			&fontRecordNum, 0, dmSeekForward, dmAllCategories);
	}

	if (err2 == dmErrIndexOutOfRange) {
		currentTableFont = stdFont;
		currentFieldFont = stdFont;
		return (true);
	}

	return (false);
}

/****************************************
 * FontCharsInWidth:
 * Find if the first line of the textP fits
 * within the the strWidth. If it does not,
 * return text length in bytes that fits
 * and the exact width of this number of chars. 
 ****************************************
 */

Boolean FontCharsInWidth(Char * textP,
		UInt16* strWidth, UInt16* textLength)
{
	Char * 	ptr = StrChr (textP, linefeedChr);
	Boolean fits;
	
	*textLength = (ptr == NULL ? StrLen (textP) : (UInt16) (ptr - textP));
	
	FntCharsInWidth(textP, strWidth, textLength, &fits);

	return fits;
}

/**************************************
 * SynchroniseCategories:
 * Synchronise the categories between
 * MemoPad and CryptoPad.
 **************************************
 */

void SynchroniseCategories(void)
{
	UInt16 category;
	Char cryptoPadCategoryName[dmCategoryLength];
	Char memoPadCategoryName[dmCategoryLength];

	for (category = 1; category < dmRecNumCategories; category++) {

		CategoryGetName(dbCryptoPadRef, category, cryptoPadCategoryName);
		CategoryGetName(dbMemoPadRef, category, memoPadCategoryName);

		if ((StrCompare(cryptoPadCategoryName, memoPadCategoryName) != 0) &&
			(memoPadCategoryName[0] == '\0'))
				DmMoveCategory(dbCryptoPadRef, dmUnfiledCategory, category, true);
	}
}


/***************************************
 * ChangeCategory
 * This routine updates the global varibles that keep track
 * of category information.  
 **************************************/

void ChangeCategory (UInt16 category)
{
	if (showAllCategories)
		memosInCategory = DmNumRecordsInCategory (dbMemoPadRef, dmAllCategories)
			+ DmNumRecordsInCategory (dbCryptoPadRef, dmAllCategories);
	else
		memosInCategory = DmNumRecordsInCategory (dbMemoPadRef, category)
			+ DmNumRecordsInCategory (dbCryptoPadRef, category);
		
	currentCategory = category;
	topRecord = 0;
	topDB = memoPadDB;
}

/**************************************
 * RemoveCurrentPassword:
 * Clear the currentPasswordP variable.
 **************************************
 */

void RemoveCurrentPassword(void)
{
	/* Was MemSet(currentPasswordP, 0, 0) -- MemSet's arg order is
	 * (dstP, count, value), so that cleared ZERO bytes (a no-op: the
	 * plaintext password was never actually wiped from the freed chunk)
	 * and never called MemPtrFree at all, leaking the MemPtrNew'd buffer
	 * from PasswordApplyChanges on every single "Encrypt Memo"/unlock
	 * use. Found via human testing: repeated "Encrypt Memo" attempts. */
	if (currentPasswordP) {
		MemSet(currentPasswordP, StrLen(currentPasswordP), 0);
		MemPtrFree(currentPasswordP);
	}
	currentPasswordP = 0;
}

/**************************************
 * CreateRecord:
 * Creates an new empty memo.
 **************************************
 */

Boolean CreateRecord(void)
{
	UInt16	recAttributes;
	UInt32 recordID;
	FontID currentFont = currentTableFont;

	currentDB = cryptoPadDB;

	/* Db35-suite-style Sec35 wrap: "New" used to create every fresh memo
	 * in the ROM's plain MemoPad DB first (pre-dating the Sec35 retrofit) --
	 * that put every new note in the WRONG datastore under "encrypted by
	 * default", and made CryptoLaunchMaintenance re-prompt "found N memo(s)
	 * in Memo Pad, migrate?" on the very next launch for a note the user
	 * had just typed IN CryptoNote itself. Create directly in CryptoPad's
	 * own DB instead (logic kept in sec001, see CryptoCreateRecord --
	 * main overflowed once this grew to include PackRecord+CryptoSealRecord). */
	if (! CryptoCreateRecord(dbCryptoPadRef, &currentRecord, currentCategory)) {
		FrmAlert(DeviceFullAlert);
		return (false);
	}

	/* the category has gained one record */
	memosInCategory++;

	if (useSingleFont == separateFont) {
		/* Create a record in the Font database
		 * for the new created record.
		 */

		DmRecordInfo(dbCryptoPadRef, currentRecord, &recAttributes, &recordID, 0);
		GetTableFontOfCategory(recAttributes & dmRecAttrCategoryMask);

		CreateFontRecord(dbCryptoPadRef, recordID);
		currentTableFont = currentFont;
	}

	return (true);
}

/************************************
 * CompareMemoPadRecords:
 * This function is used by the
 * sort algorith routine.
 ************************************
 */

Int16 CompareMemoPadRecords(MemPtr rec1, MemPtr rec2, Int16 sortOrder)
{
	if (sortOrder == soAlphabetic) {
		return StrCaselessCompare(rec1, rec2);
	} else return 0;
}


/************************************
 * CompareCryptoPadRecords:
 * This function is used by the
 * sort algorith routine.
 ************************************
 */

/* Db35-suite-style Sec35 wrap: this is a DM sort-comparator callback -- the
 * ROM's sort routine calls it through a raw function pointer
 * ((DmComparF*)&CompareCryptoPadRecords), so THIS entry point must stay in
 * main (the only segment guaranteed loaded when the ROM calls through that
 * pointer) -- but the real comparison logic is delegated to
 * CryptoCompareRecords (sec001, ordinary safe cross-segment call, same as
 * everywhere else in this feature) to keep main's footprint minimal. See
 * CryptoCompareRecords for the marker/wrapped-bit handling + the
 * known-limitation note about wrapped records' nonce-aware sorting. */
Int16 CompareCryptoPadRecords(PackedRecordPtr rec1,
		PackedRecordPtr rec2, Int16 sortOrder)
{
	return CryptoCompareRecords((const UInt8*) rec1, (const UInt8*) rec2, sortOrder);
}

/**************************************
 * UnpackRecord:
 * Unpacks the record to identify the
 * address of the different strings in
 * the record.
 **************************************
 */

void UnpackRecord(RecordPtr recordP, PackedRecordPtr packedRecordP)
{
	Char * string = packedRecordP->dataP;

	/* Db35-suite-style Sec35 wrap: packedRecordP is NOT guaranteed to be
	 * 2-byte aligned here -- CryptoSec35.c's outer-wrap unpacking calls
	 * this with a pointer offset by exactly 1 (skipping the leading
	 * marker byte, e.g. `(PackedRecordPtr)(raw + 1)`), so an
	 * otherwise-even MemHandleLock base becomes ODD. textLen/kdfRounds
	 * are UInt16 fields -- a direct struct-member read of a misaligned
	 * UInt16 is a native word load on m68k, which FAULTS with an "address
	 * error" at an odd address (real 68000/POSE behavior, not a
	 * tolerant-of-misalignment x86). This was a real crash hit during
	 * testing (right after a MemoPad import triggered a read of a
	 * migrated record). MemMove is byte-oriented and immune to alignment,
	 * same as the IV/salt arrays already use below -- use it for every
	 * multi-byte scalar field too, not just the arrays. */
	recordP->flag      = packedRecordP->flag;
	recordP->passwdLen = packedRecordP->passwdLen;
	MemMove(&recordP->textLen, &packedRecordP->textLen, sizeof(recordP->textLen));

	MemMove(recordP->pwIV, packedRecordP->pwIV, sizeof(recordP->pwIV));
	MemMove(recordP->bodyIV, packedRecordP->bodyIV, sizeof(recordP->bodyIV));
	MemMove(recordP->kdfSalt, packedRecordP->kdfSalt, sizeof(recordP->kdfSalt));
	MemMove(&recordP->kdfRounds, &packedRecordP->kdfRounds, sizeof(recordP->kdfRounds));

	recordP->passwordP = string;

	string += recordP->passwdLen + 1;

	recordP->textP = string;
	string += recordP->textLen + 1;
}

/**************************************
 * PackRecord:
 * Packs the record into one structure
 **************************************
 */

Err PackRecord(RecordPtr recordP, MemHandle record)
{
	UInt16 nLength;
	UInt16 offset;

	offset = 0;
	nLength = sizeof(recordP->flag) +
				sizeof(recordP->passwdLen) +
				sizeof(recordP->textLen) +
				sizeof(recordP->pwIV) +
				sizeof(recordP->bodyIV) +
				sizeof(recordP->kdfSalt) +
				sizeof(recordP->kdfRounds) +
				recordP->passwdLen + 1 +
				recordP->textLen + 1;

	/* Resize the handle */

	if (MemHandleResize(record, nLength) == 0) {
		Char * ptr;

		ptr = MemHandleLock(record);
		offset = 0;

		DmSet(ptr, offset, 1, recordP->flag);
		offset += sizeof(recordP->flag);

		DmWrite(ptr, offset, &(recordP->passwdLen),
							sizeof(recordP->passwdLen));
		offset += sizeof(recordP->passwdLen);

		DmWrite(ptr, offset, &(recordP->textLen),
							sizeof(recordP->textLen));
		offset += sizeof(recordP->textLen);

		DmWrite(ptr, offset, recordP->pwIV, sizeof(recordP->pwIV));
		offset += sizeof(recordP->pwIV);

		DmWrite(ptr, offset, recordP->bodyIV, sizeof(recordP->bodyIV));
		offset += sizeof(recordP->bodyIV);

		DmWrite(ptr, offset, recordP->kdfSalt, sizeof(recordP->kdfSalt));
		offset += sizeof(recordP->kdfSalt);

		DmWrite(ptr, offset, &(recordP->kdfRounds), sizeof(recordP->kdfRounds));
		offset += sizeof(recordP->kdfRounds);

		DmWrite(ptr, offset, recordP->passwordP, recordP->passwdLen + 1);
		offset += recordP->passwdLen + 1;

		DmWrite(ptr, offset, recordP->textP, recordP->textLen + 1);
		offset += recordP->textLen + 1;

		MemHandleUnlock(record);
		return 0;

	} else return 1;
}



/***************************************
 * Search
 * This routine searchs the memo database for records 
 * containing the string passed.
 ***************************************/
void Search (FindParamsPtr findParams, Boolean appIsActive)
{
	UInt16 pos;
	Char * header;
	MemHandle recordH, headerStringH;
	RectangleType r;
	Boolean done, match;
	
	DmOpenRef dbP;
	DmSearchStateType searchState;
	UInt32 findDB;
	Err err, err2;
	UInt16 cardNum;
	UInt16 recordNum;
	LocalID dbID, memoPadDbID, cryptoPadDbID;
	
	Char * memoRecP;
	PackedRecordPtr cryptoRecP;
	RecordType record;


    // First try to open the new "Memos" database, and fallback on the old
    // format if it can't be found.
	err = DmGetNextDatabaseByTypeCreator (true, &searchState, memoPadType,
         		memoPadCreatorNew, true, &cardNum, &memoPadDbID);
    if (err == dmErrCantFind) {
	    err = DmGetNextDatabaseByTypeCreator (true, &searchState, memoPadType,
         		memoPadCreator, true, &cardNum, &memoPadDbID);
    }
	err2 = DmGetNextDatabaseByTypeCreator (true, &searchState, cryptoPadType,
         		cryptoPadCreator, true, &cardNum, &cryptoPadDbID);

     if (err || err2)
		{
		findParams->more = false;
		return;
		}

   // Find the application's data file.
	if ((findParams->newSearch) || !appIsActive) {
		dbID = memoPadDbID;
		findDB = memoPadDB;
   	} else dbID = findParams->appDbID;

   // Open the Memo database.
   dbP = DmOpenDatabase(cardNum, dbID, findParams->dbAccesMode);
   
   if (!dbP)
   	{
      findParams->more = false;
      return;
   	}

	// Display the heading line.
	headerStringH = DmGetResource('tSTR', strCryptoHeader);
	header = MemHandleLock(headerStringH);
	done = FindDrawHeader(findParams, header);
   	MemHandleUnlock(headerStringH);   
   	DmReleaseResource(headerStringH);   
   	if (done) 
      goto Exit;

	// Search the memos for the "find" string.
	recordNum = findParams->recordNum;
	while (true)
		{
		// Because applications can take a long time to finish a find when
		// the result may be on the screen or for other reasons, users like
		// to be able to stop the find.  Stop the find if an event is pending.
		// This stops if the user does something with the device.  Because
		// this call slows winDown the search we perform it every so many 
		// records instead of every record.  The response time should still
		// be short without introducing much extra work to the search.
		
		// Note that in the implementation below, if the next 16th record is  
		// secret the check doesn't happen.  Generally this shouldn't be a 
		// problem since if most of the records are secret then the search 
		// won't take long anyways!
		if ((recordNum & 0x000f) == 0 &&			// every 16th record
			EvtSysEventAvail(true))
			{
			// Stop the search process.
			findParams->more = true;
			break;
			}

		recordH = DmQueryNextInCategory (dbP, &recordNum, dmAllCategories);

		// Have we run out of records?
		if ((! recordH) && (appIsActive)) {
				if ((dbID == memoPadDbID) && (globalPasswordP != 0)) {
					DmCloseDatabase (dbP);
					dbID = cryptoPadDbID;
					recordNum = 0;
					findDB = cryptoPadDB;
					dbP = DmOpenDatabase(cardNum, dbID, findParams->dbAccesMode);
					recordH = DmQueryNextInCategory (dbP, &recordNum, dmAllCategories);
				}
		}

	 	if (! recordH){
				findParams->more = false;			
				break;
		}

		if (dbID == memoPadDbID) {

			memoRecP = MemHandleLock (recordH);
			match = FindStrInStr (memoRecP, findParams->strToFind, &pos);
		} else {

			/* Encrypted bodies are no longer bulk-decrypted for search: each
			 * record now needs its own password (plus the device and global
			 * secrets), which the search has no access to. The plaintext title
			 * (stored before the first newline) is still searched below.
			 *
			 * Db35-suite-style Sec35 wrap: cryptoRecP itself is no longer
			 * read directly (kept locked only to balance the unconditional
			 * MemHandleUnlock(recordH) below) -- a Sec35-wrapped record's
			 * raw bytes are opaque ciphertext (title included, unlike the
			 * layer-2 case above where the title stays plain), so this
			 * routes through the chokepoint instead. A wrapped+locked
			 * record is skipped entirely (title unreadable without the
			 * session key -- same "don't bulk-decrypt, skip what can't be
			 * read" philosophy as the layer-2 case). */
			cryptoRecP = MemHandleLock(recordH);
			if (CryptoRecordWrapped (dbP, recordNum) && ! EncKeyPresent ())
				match = false;
			else if (CryptoGetRecord (dbP, recordNum, &record))
				match = FindStrInStr (record.textP, findParams->strToFind, &pos);
			else
				match = false;
		}

		if (match)
			{
			// Add the match to the find parameter block,  if there is no room to
			// display the match the following function will return true.
			done = FindSaveMatch (findParams, recordNum, pos, 0, findDB, cardNum, dbID);

			if (!done)
				{
				// Get the bounds of the region where we will draw the results.
				FindGetLineBounds (findParams, &r);
				
				// Display the title of the description.	
				if (dbID == memoPadDbID)
					DrawStringWithEllipsis ((memoRecP), r.topLeft.x+1, r.topLeft.y, 
							r.extent.x-2, justLeft);
				else DrawStringWithEllipsis  ((record.textP), r.topLeft.x+1, 
							r.topLeft.y, r.extent.x-2, justLeft);
				findParams->lineNumber++;
				}
			}

		MemHandleUnlock(recordH);

		if (dbID == cryptoPadDbID) {
			DmReleaseRecord(dbP, recordNum, false);
		}

		if (done) break;

		recordNum++;
		}

Exit:
	DmCloseDatabase (dbP);	
}


/***************************************
 * GoToItem
 * This routine is an entry point of the memo application.
 * It is generally called as the result of hitting a 
 * "Go to" button in the text search dialog.  The record
 * identifies by the parameter block passed will be display,
 * with the character range specified highlighted.
 ***************************************/
void GoToItem (GoToParamsPtr goToParams, Boolean launchingApp)
{
	UInt16 recordNum;
	UInt16 attr;
	UInt32 uniqueID;
	EventType event;

	recordNum = goToParams->recordNum;
	currentDB = goToParams->matchCustom;
	
	//   vvvvvvvvv----DmRecordInfo is side effect of that if statement

	if (DmRecordInfo (currentDBRef, recordNum, &attr, &uniqueID, NULL) != 0)
		{
	//	FrmAlert(secGotoInvalidRecordAlert);
		FrmGotoForm(frmMainForm);
		return;
		}
	// Make the item the first item displayed.
	topRecord = recordNum;

	// Change the current category if necessary.
	if (currentCategory != dmAllCategories)
		{
		ChangeCategory (attr & dmRecAttrCategoryMask);
		}

	// If the application is already running, close all open forms.  This 
	// may cause in the database record to be reordered, so we'll find the 
	// records index by its unique id.
	if (! launchingApp)
		{
		FrmCloseAllForms ();
		DmFindRecordByID (currentDBRef, uniqueID, &recordNum);
		}
		
	// Send an event to goto a form and select the matching text.
	MemSet (&event, sizeof(EventType), 0);

	event.eType = frmLoadEvent;
	event.data.frmLoad.formID = frmEditForm;
	EvtAddEventToQueue (&event);
 
	event.eType = frmGotoEvent;
	event.data.frmGoto.recordNum = recordNum;
	event.data.frmGoto.matchPos = goToParams->matchPos;
	event.data.frmGoto.matchLen = goToParams->searchStrLen;
	event.data.frmGoto.matchFieldNum = goToParams->matchFieldNum;
	event.data.frmGoto.formID = frmEditForm;
	EvtAddEventToQueue (&event);

	currentForm = frmEditForm;
}
