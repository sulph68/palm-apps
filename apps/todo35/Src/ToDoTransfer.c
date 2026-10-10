/******************************************************************************
 *
 * Copyright (c) 1997-1999 Palm Computing, Inc. or its subsidiaries.
 * All rights reserved.
 *
 * File: ToDoTransfer.c
 *
 * Description:
 *      To Do routines to transfer records.
 *
 * History:
 *      8/9/97  djk - Created
 *
 *****************************************************************************/

#include <PalmOS.h>

// DOLATER kwk - figure out once and for all if we can really assume
// that all text being parsed during transfer is single-byte.

#define	NON_INTERNATIONAL
#include <CharAttr.h>
#include <IMCUtils.h>	/* Td35: SDK ships this as IMCUtils.h (case matters on Linux) */

#include "ToDo.h"
#include "ToDoRsc.h"

#define identifierLengthMax		40
#define tempStringLengthMax		20
#define defaultPriority				1
#define incompleteFlag				0x7F
#define todoFilenameExtension		".vcs"

extern Char * GetToDoNotePtr (ToDoDBRecordPtr recordP);	// needed to see if ToDo empty


/***********************************************************************
 *
 * FUNCTION:    GetChar
 *
 * DESCRIPTION: Function to get a character from the exg transport.
 *
 * PARAMETERS:  exgSocketP - the exg connection
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *         Name   Date      Description
 *         ----   ----      -----------
 *         roger   8/15/97   Initial Revision
 *
 ***********************************************************************/
TD35_XFER static UInt16 GetChar(const void * exgSocketP)
{
	UInt32 len;
	UInt8 c;
	Err err;
	
	len = ExgReceive((ExgSocketPtr) exgSocketP, &c, sizeof(c), &err);
	
	if (err || len == 0)
		return EOF;
	else
		return c;
}


/***********************************************************************
 *
 * FUNCTION:    PutString
 *
 * DESCRIPTION: Function to put a string to the exg transport. 
 *
 * PARAMETERS:  exgSocketP - the exg connection
 *					 stringP - the string to put
 *
 * RETURNED:    nothing
 *					 If the all the string isn't sent an error is thrown using ErrThrow.
 *
 * REVISION HISTORY:
 *         Name   Date      Description
 *         ----   ----      -----------
 *         roger  8/15/97   Initial Revision
 *
 ***********************************************************************/
TD35_XFER static void PutString(void * exgSocketP, const Char * const stringP)
{
	UInt32 len;
	Err err;
	
	len = ExgSend((ExgSocketPtr) exgSocketP, stringP, StrLen(stringP), &err);
	
	// If the bytes were not sent throw an error.
	if (len == 0 && StrLen(stringP) > 0)
		ErrThrow(err);
}


/************************************************************
 *
 * FUNCTION: MatchDateToken
 *
 * DESCRIPTION: Extract date from the given string
 *
 * PARAMETERS: 
 *		tokenP	-	string ptr from which to extract
 *		dateP		-	ptr where to store date (optional)
 *
 * RETURNS: nothing
 *
 * REVISION HISTORY:
 *			Name		Date		Description
 *			----		----		-----------
 *			djk		2/2/98	Copied code from Date Book
 *
 *************************************************************/
TD35_XFER static void MatchDateToken (const char* tokenP,	DateType * dateP)
{
	char			identifier[identifierLengthMax];
	int			nv;

	// Use identifier[] as a temp buffer to copy parts of the vCal DateTime
	// so we can convert them to correct form.  This date portion 
	// is 4 chars (date) + 2 chars (month) + 2 chars (day) = 8 chars long

	// Read the Year
	StrNCopy(identifier, tokenP, 4);
	identifier[4] = nullChr;
	nv = StrAToI(identifier);
	// Validate the number and use it. Td35 era: accept any year 1904..9999 (the
	// stored DateType is wide), not the ROM's lastYear (2031).
	if (nv < firstYear || 9999 < nv)
		nv = firstYear;
	dateP->year = nv - firstYear;
	tokenP += StrLen(identifier) * sizeof(Char);
	
	// Read the Month
	StrNCopy(identifier, tokenP, 2);
	identifier[2] = nullChr;
	nv = StrAToI(identifier); 
	// Validate the number and use it.
	if (nv < 1 || monthsInYear < nv)
		nv = 1;
	dateP->month = nv;
	tokenP += StrLen(identifier) * sizeof(Char);
	
	// Read the Day
	StrNCopy(identifier, tokenP, 2);
	identifier[2] = nullChr;
	nv = StrAToI(identifier);
	// Validate the number and use it.
	if (nv < 1 || 31 < nv)
		nv = 1;
	dateP->day = nv;
	tokenP += StrLen(identifier) * sizeof(Char);
}


/***********************************************************************
 *
 * FUNCTION:    SetDescriptionAndFilename
 *
 * DESCRIPTION: Derive and allocate a decription and filename from some text. 
 *
 * PARAMETERS:  textP - the text string to derive the names from
 *					 descriptionPP - pointer to set to the allocated description 
 *					 descriptionHP - MemHandle to set to the allocated description 
 *					 filenamePP - pointer to set to the allocated filename 
 *					 filenameHP - MemHandle to set to the allocated description 
 *
 * RETURNED:    a description and filename are allocated and the pointers are set
 *
 * REVISION HISTORY:
 *         Name   Date      Description
 *         ----   ----      -----------
 *         roger   11/4/97   Initial Revision
 *
 ***********************************************************************/
TD35_XFER static void SetDescriptionAndFilename(Char * textP, Char **descriptionPP, 
	MemHandle *descriptionHP, Char **filenamePP, MemHandle *filenameHP)
{
	Char * descriptionP;
   Int16 descriptionSize;
	Int16 descriptionWidth;
	Boolean descriptionFit;
	Char * spaceP;
	Char * filenameP;
	UInt8 filenameLength;
	Coord unused;
	
	
	descriptionSize = StrLen(textP);
	WinGetDisplayExtent(&descriptionWidth, &unused);
	FntCharsInWidth (textP, &descriptionWidth, &descriptionSize, &descriptionFit);
	
	if (descriptionSize > 0)
		{
		*descriptionHP = MemHandleNew(descriptionSize+sizeOf7BitChar('\0'));
		if (*descriptionHP)
			{
			descriptionP = MemHandleLock(*descriptionHP);
			MemMove(descriptionP, textP, descriptionSize);
			descriptionP[descriptionSize] = nullChr;
			}
		}
	else
		{
		*descriptionHP = DmGetResource(strRsc, BeamDescriptionStr);
		descriptionP = MemHandleLock(*descriptionHP);
		DmReleaseResource(*descriptionHP);
		*descriptionHP = NULL;			// so the resource isn't freed
		}
	
		
	if (descriptionSize > 0)
		{
		// Now form a file name.  Use only the first word or two.
		spaceP = StrChr(descriptionP, spaceChr);
		if (spaceP)
			// Check for a second space
			spaceP = StrChr(spaceP + sizeOf7BitChar(spaceChr), spaceChr);
		
		// If at least two spaces were found then use only that much of the description.
		// If less than two spaces were found then use all of the description.
		if (spaceP)
			filenameLength = spaceP - descriptionP;
		else
			filenameLength = StrLen(descriptionP);
		
		
		// Allocate space and form the filename
		*filenameHP = MemHandleNew(filenameLength + StrLen(todoFilenameExtension) + sizeOf7BitChar('\0'));
		filenameP = MemHandleLock(*filenameHP);
		if (filenameP)
			{
			MemMove(filenameP, descriptionP, filenameLength);
			MemMove(&filenameP[filenameLength], todoFilenameExtension, 
				StrLen(todoFilenameExtension) + sizeOf7BitChar('\0'));
			}
		}
	else
		{
		*filenameHP = DmGetResource(strRsc, BeamFilenameStr);
		filenameP = MemHandleLock(*filenameHP);
		DmReleaseResource(*filenameHP);
		*filenameHP = NULL;					// so the resource isn't freed
		}
	
	
	*descriptionPP = descriptionP;
	*filenamePP = filenameP;
}


/***********************************************************************
 *
 * FUNCTION:    ToDoSendRecordTryCatch
 *
 * DESCRIPTION: Send a record.  
 *
 * PARAMETERS:	 dbP - pointer to the database to add the record to
 * 				 recordNum - the record number to send
 * 				 recordP - pointer to the record to send
 * 				 exgSocketP - the exchange socket used to send
 *
 * RETURNED:    0 if there's no error
 *
 * REVISION HISTORY:
 *         Name   Date      Description
 *         ----   ----      -----------
 *         roger  12/11/97  Initial Revision
 *
 ***********************************************************************/
TD35_XFER static Err ToDoSendRecordTryCatch (DmOpenRef dbP, Int16 recordNum, 
	ToDoDBRecordPtr recordP, ExgSocketPtr exgSocketP)
{
   volatile Err error = 0;
	
	
	// An error can happen anywhere during the send process.  It's easier just to 
	// catch the error.  If an error happens, we must pass it into ExgDisconnect.
	// It will then cancel the send and display appropriate ui.
	ErrTry
		{
		PutString(exgSocketP, "BEGIN:VCALENDAR" imcLineSeparatorString);
		PutString(exgSocketP, "VERSION:1.0" imcLineSeparatorString);
		ToDoExportVCal(dbP, recordNum, recordP, exgSocketP, PutString, true);
		PutString(exgSocketP, "END:VCALENDAR" imcLineSeparatorString);
		}
	
	ErrCatch(inErr)
		{
		error = inErr;
		} ErrEndCatch

	
	return error;
}


/***********************************************************************
 *
 * FUNCTION:    ToDoSendRecord
 *
 * DESCRIPTION: Send a record.  
 *
 * PARAMETERS:	 dbP - pointer to the database to add the record to
 * 				 recordNum - the record to send
 *
 * RETURNED:    true if the record is found and sent
 *
 * REVISION HISTORY:
 *         Name   Date      Description
 *         ----   ----      -----------
 *         roger  5/9/97    Initial Revision
 *
 ***********************************************************************/
TD35_XFER extern void ToDoSendRecord (DmOpenRef dbP, Int16 recordNum)
{
   ToDoDBRecordPtr recordP;
   MemHandle recordH;
   MemHandle descriptionH = NULL;
   Err error;
	ExgSocketType exgSocket;
	MemHandle nameH = NULL;
	Boolean empty;
	
	
	// important to init structure to zeros...
	MemSet(&exgSocket, sizeof(exgSocket), 0);
	
	// Form a description of what's being sent.  This will be displayed
	// by the system send dialog on the sending and receiving devices.
	// Td35: export from a DECRYPTED inline copy of the record (masked "******" if Sec35 is
	// locked); the storage record stays encrypted and is never locked during the send.
	recordP = ToDoGetInlineCopy(dbP, recordNum, &recordH);
	if (recordP == NULL)
		return;

	// If the description field is empty and the note field is empty,
	// consider the record empty.
	empty = ToDoRecordIsEmpty(dbP, recordNum);

   if (!empty)
   {
		// Set the exg description to the record's description.
		SetDescriptionAndFilename(&recordP->description, &exgSocket.description, 
			&descriptionH, &exgSocket.name, &nameH);
		
		
		exgSocket.length = MemHandleSize(recordH) + 100;		// rough guess
		// Note, a hack in exgmgr will remap this to datebook
		exgSocket.target = toDo35Creator;
		error = ExgPut(&exgSocket);   // put data to destination
		if (!error)
			{
			error = ToDoSendRecordTryCatch(dbP, recordNum, recordP, &exgSocket);

			// (Td35: nothing to release here -- recordP is a dynamic decrypted copy, and
			// the storage record was never locked, so loopback sorting is unobstructed.)

			ExgDisconnect(&exgSocket, error);
			}
	}
	else
		FrmAlert(NoDataToBeamAlert);
	
	// Clean winUp
	if (descriptionH)
		MemHandleFree(descriptionH);
	if (nameH)
		MemHandleFree(nameH);
	ToDoReleaseInlineCopy(recordH);		// free the decrypted copy

	return;
}


/***********************************************************************
 *
 * FUNCTION:    ToDoSendCategoryTryCatch
 *
 * DESCRIPTION: Send all visible records in a category.  
 *
 * PARAMETERS:		dbP - pointer to the database to add the record to
 * 					categoryNum - the category of records to send
 * 					exgSocketP - the exchange socket used to send
 * 					index - the record number of the first record in the category to send
 *
 * RETURNED:    0 if there's no error
 *
 * REVISION HISTORY:
 *         Name   Date      Description
 *         ----   ----      -----------
 *         roger  12/11/97  Initial Revision
 *
 ***********************************************************************/
TD35_XFER static Err ToDoSendCategoryTryCatch (DmOpenRef dbP, UInt16 categoryNum, 
	ExgSocketPtr exgSocketP, UInt16 recordNum)
{
	volatile Err error = 0;
	volatile MemHandle outRecordH = 0;
   ToDoDBRecordPtr outRecordP;
	
	// An error can happen anywhere during the send process.  It's easier just to 
	// catch the error.  If an error happens, we must pass it into ExgDisconnect.
	// It will then cancel the send and display appropriate ui.
	ErrTry
		{

		PutString(exgSocketP, "BEGIN:VCALENDAR" imcLineSeparatorString);
		PutString(exgSocketP, "VERSION:1.0" imcLineSeparatorString);
		
		// Loop through all records in the category.
		while (DmSeekRecordInCategory(dbP, &recordNum, 0, dmSeekForward, categoryNum) == 0)
			{
			// Emit the record.  Td35: from a DECRYPTED inline copy (storage stays sealed).
			{
			MemHandle cpH;
			outRecordP = ToDoGetInlineCopy(dbP, recordNum, &cpH);
			outRecordH = cpH;
			if (outRecordP != 0)
				{
				ToDoExportVCal(dbP, recordNum, outRecordP, exgSocketP, PutString, true);
				ToDoReleaseInlineCopy(outRecordH);
				outRecordH = 0;
				}
			}
			
			recordNum++;
			}
		
		PutString(exgSocketP, "END:VCALENDAR" imcLineSeparatorString);
		}
	
	ErrCatch(inErr)
		{
		error = inErr;
		
		if (outRecordH)
			MemHandleUnlock(outRecordH);
		} ErrEndCatch
	
	
	return error;	
}


/***********************************************************************
 *
 * FUNCTION:    ToDoSendCategory
 *
 * DESCRIPTION: Send all visible records in a category.  
 *
 * PARAMETERS:  categoryNum - the category of records to send
 *
 * RETURNED:    true if any records are found and sent
 *
 * REVISION HISTORY:
 *         Name   Date      Description
 *         ----   ----      -----------
 *         roger   5/9/97   Initial Revision
 *
 ***********************************************************************/
TD35_XFER extern void ToDoSendCategory (DmOpenRef dbP, UInt16 categoryNum)
{
	Err error;
   Char description[dmCategoryLength];
	UInt16 recordNum;
	Boolean foundAtLeastOneRecord;
	ExgSocketType exgSocket;
	UInt16 mode;
	LocalID dbID;
	UInt16 cardNo;
	Boolean databaseReopened;
	
	
	// If the database was opened to show secret records, reopen it to not see 
	// secret records.  The idea is that secret records are not sent when a 
	// category is sent.  They must be explicitly sent one by one.
	DmOpenDatabaseInfo(dbP, &dbID, NULL, &mode, &cardNo, NULL);
	if (mode & dmModeShowSecret)
		{
		dbP = DmOpenDatabase(cardNo, dbID, dmModeReadOnly);
		databaseReopened = true;
		}
	else
		databaseReopened = false;
	
	
	// important to init structure to zeros...
	MemSet(&exgSocket, sizeof(exgSocket), 0);
	
	
	// Make sure there is at least one record in the category.
	recordNum = 0;
	foundAtLeastOneRecord = false;
	while (true)
		{
		if (DmSeekRecordInCategory(dbP, &recordNum, 0, dmSeekForward, categoryNum) != 0)
			break;
		
		foundAtLeastOneRecord = DmQueryRecord(dbP, recordNum) != 0;
		if (foundAtLeastOneRecord)
			break;
		
		
		recordNum++;
		}
	// ï¿½ï¿½ï¿½ï¿½Weird edge case: if the only record in the category is an empty record,
	// ï¿½ï¿½ï¿½ there will still be nothing to beam.  Result seems to be getting an empty
	// ï¿½ï¿½ï¿½ record at the other end, which immediately goes away.  Good enough!  
	
	
	// We should send the category because there's at least one record to send.
	if (foundAtLeastOneRecord)
		{
		// Form a description of what's being sent.  This will be displayed
		// by the system send dialog on the sending and receiving devices.
		CategoryGetName (dbP, categoryNum, description);
		exgSocket.description = description;
		
		// Now form a file name
		exgSocket.name = MemPtrNew(StrLen(description) + StrLen(todoFilenameExtension) + sizeOf7BitChar('\0'));
		if (exgSocket.name)
			{
			StrCopy(exgSocket.name, description);
			StrCat(exgSocket.name, todoFilenameExtension);
			}
		

		exgSocket.length = 0;		// rough guess
		// Note, a hack in exgmgr will remap this to datebook
		exgSocket.target = toDo35Creator;
		error = ExgPut(&exgSocket);   // put data to destination
		if (!error)
			{
			error = ToDoSendCategoryTryCatch (dbP, categoryNum, &exgSocket, recordNum);

			ExgDisconnect(&exgSocket, error);
			}
		
		// Clean winUp
		if (exgSocket.name)
			MemPtrFree(exgSocket.name);
		}
	else
		FrmAlert(NoDataToBeamAlert);
	
	if (databaseReopened)
		DmCloseDatabase(dbP);
	
	return;
}


/************************************************************
 *
 * FUNCTION: ToDoImportVEvent
 *
 * DESCRIPTION: Import a VCal record of type vEvent.
 *
 * This function doesn't exist on the device because
 * the Datebook imports vEvent records.  On the emulator
 * this function exists for linking completeness.
 *
 * PARAMETERS: 
 *			dbP - pointer to the database to add the record to
 *			inputStream	- pointer to where to import the record from
 *			inputFunc - function to get input from the stream
 *			obeyUniqueIDs - true to obey any unique ids if possible
 *			beginAlreadyRead - whether the begin statement has been read
 *
 * RETURNS: true if the input was read
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			roger	9/12/97		Created
 *
 *************************************************************/

TD35_XFER static Err ToDoImportVEvent(DmOpenRef dbP, void * inputStream, GetCharF inputFunc,
	Boolean obeyUniqueIDs, Boolean beginAlreadyRead)
{
	return 0;
}


/***********************************************************************
 *
 * FUNCTION:		ToDoSetGoToParams
 *
 * DESCRIPTION:	Store the information necessary to navigate to the 
 *                record inserted into the launch code's parameter block.
 *
 * PARAMETERS:		 dbP        - pointer to the database to add the record to
 *						 exgSocketP - parameter block passed with the launch code
 *						 uniqueID   - unique id of the record inserted
 *
 * RETURNED:		nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	10/17/97	Created
 *
 ***********************************************************************/
TD35_XFER static void ToDoSetGoToParams (DmOpenRef dbP, ExgSocketPtr exgSocketP, UInt32 uniqueID)
{
	UInt16		recordNum;
	UInt16		cardNo;
	LocalID 	dbID;

	
	if (! uniqueID) return;

	DmOpenDatabaseInfo (dbP, &dbID, NULL, NULL, &cardNo, NULL);

	// The this the the first record inserted, save the information
	// necessary to navigate to the record.
	if (! exgSocketP->goToParams.uniqueID)
		{
		DmFindRecordByID (dbP, uniqueID, &recordNum);

		exgSocketP->goToCreator = toDo35Creator;
		exgSocketP->goToParams.uniqueID = uniqueID;
		exgSocketP->goToParams.dbID = dbID;
		exgSocketP->goToParams.dbCardNo = cardNo;
		exgSocketP->goToParams.recordNum = recordNum;
		}

	// If we already have a record then make sure the record index 
	// is still correct.  Don't update the index if the record is not
	// in your the app's database.
	else if (dbID == exgSocketP->goToParams.dbID &&
			   cardNo == exgSocketP->goToParams.dbCardNo)
		{
		DmFindRecordByID (dbP, exgSocketP->goToParams.uniqueID, &recordNum);

		exgSocketP->goToParams.recordNum = recordNum;
		}
}


/***********************************************************************
 *
 * FUNCTION:		ReceiveData
 *
 * DESCRIPTION:	Receives data into the output field using the Exg API
 *
 * PARAMETERS:		exgSocketP, socket from the app code
 *						 sysAppLaunchCmdExgReceiveData
 *
 * RETURNED:		error code or zero for no error.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			???	???		Created
 *			art	10/17/97	Added "go to" record logic
 *			bhall	09/27/99	Now always read until no more data
 *								(previously datebook called us once per event)
 *
 ***********************************************************************/
/* Beam receive. Runs as a NO-GLOBALS sublaunch (app not running): the vCal parser's
 * static tables are A5-relative and corrupt the heap there (same trap as Address35).
 * So we do the minimum: drain the whole beam into a buffer and store those RAW bytes,
 * unparsed, into the ToDoDBPlain inbox. ToDoMergePlainDB parses them on the next full
 * launch. `dbP` (the live DB) is intentionally NOT touched here. */
TD35_RECV extern Err ToDoReceiveData(DmOpenRef dbP, ExgSocketPtr exgSocketP)
{
	volatile Err err = 0;
	MemHandle bufH;
	UInt8 *buf;
	UInt16 cap = 2048, total = 0;
	DmOpenRef pdb;

	(void) dbP;

	err = ExgAccept(exgSocketP);
	if (err) return err;

	bufH = MemHandleNew(cap);
	if (bufH == NULL) { ExgDisconnect(exgSocketP, exgMemError); return exgMemError; }
	buf = (UInt8 *) MemHandleLock(bufH);

	for (;;)
		{
		UInt32 got;
		if (total >= cap)
			{
			MemHandleUnlock(bufH);
			if (MemHandleResize(bufH, (UInt32) cap * 2) != errNone) { buf = (UInt8 *) MemHandleLock(bufH); break; }
			cap = (UInt16)(cap * 2);
			buf = (UInt8 *) MemHandleLock(bufH);
			}
		got = ExgReceive(exgSocketP, buf + total, (UInt32)(cap - total), (Err *) &err);
		if (err || got == 0) break;
		total = (UInt16)(total + got);
		}

	if (total > 0 && ToDoOpenPlainDB(&pdb, true) == errNone)
		{
		// Stash the receive-time category choice (if the accept flow set appData) alongside
		// the RAW bytes; the merge hands it to the parser.  Do NOT parse here (sublaunch).
		ToDoInboxAppendRaw(pdb, buf, total,
			(UInt16)(exgSocketP->appData & dmRecAttrCategoryMask));
		DmCloseDatabase(pdb);
		}

	MemHandleUnlock(bufH);
	MemHandleFree(bufH);
	ExgDisconnect(exgSocketP, err);
	return err;
}

/* Memory-buffer input stream for parsing the raw inbox on a full launch. The vCal
 * parser casts inputStream to ExgSocketPtr (reads ->appData for the category, writes
 * ->goToParams), so we embed a zeroed ExgSocketType FIRST and keep our cursor after
 * it; the parser's socket accesses hit the harmless embedded socket. */
typedef struct { ExgSocketType sock; const UInt8 *buf; UInt16 len; UInt16 pos; } TdMergeStream;
TD35_XFER static UInt16 TdMemGetChar(const void *s)
{
	TdMergeStream *m = (TdMergeStream *) s;
	return (m->pos >= m->len) ? (UInt16) EOF : (UInt16) m->buf[m->pos++];
}

/* Parse every raw record in the ToDoDBPlain inbox into mainDB, then delete the inbox.
 * Uses the vCal parser -> FULL launch only (called from StartApplication). sec002. */
TD35_XFER void ToDoMergePlainDB(DmOpenRef mainDB)
{
	LocalID id = DmFindDatabase(0, "ToDoDBPlain");
	DmOpenRef pdb;
	UInt16 i, n;

	if (id == 0 || mainDB == NULL)
		return;
	pdb = DmOpenDatabase(0, id, dmModeReadOnly);
	if (pdb == NULL)
		return;
	n = DmNumRecords(pdb);
	for (i = 0; i < n; i++)
		{
		MemHandle h = DmQueryRecord(pdb, i);
		if (h != NULL)
			{
			TdMergeStream ms;
			UInt16 icat = 0;
			DmRecordInfo(pdb, i, &icat, NULL, NULL);	// receive-time category (if any)
			MemSet(&ms.sock, sizeof(ms.sock), 0);
			ms.sock.appData = (UInt32)(icat & dmRecAttrCategoryMask);	// parser files it here
			ms.buf = (const UInt8 *) MemHandleLock(h);
			ms.len = (UInt16) MemHandleSize(h);
			ms.pos = 0;
			ErrTry {
				while (ToDoImportVCal(mainDB, &ms, TdMemGetChar, false, false, ToDoImportVEvent))
					{};
			} ErrCatch(inErr) { (void) inErr; } ErrEndCatch
			MemHandleUnlock(h);
			}
		}
	DmCloseDatabase(pdb);
	DmDeleteDatabase(0, id);
}


/************************************************************
 *
 * FUNCTION: ToDoImportVToDo
 *
 * DESCRIPTION: Import a VCal record of type vToDo
 *
 * PARAMETERS: 
 *			dbP - pointer to the database to add the record to
 *			inputStream	- pointer to where to import the record from
 *			inputFunc - function to get input from the stream
 *			obeyUniqueIDs - true to obey any unique ids if possible
 *			beginAlreadyRead - whether the begin statement has been read
 *			uniqueIDP - (returned) id of record inserted.
 *
 * RETURNS: true if the input was read
 *
 *	REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			djk	8/9/97	Created
 *			art	10/17/97	Added parameter to return unique id
 *
 *************************************************************/
 
TD35_XFER extern Boolean
ToDoImportVToDo(DmOpenRef dbP, void * inputStream, GetCharF inputFunc,
	Boolean obeyUniqueIDs, Boolean beginAlreadyRead, UInt32 * uniqueIDP)
{
	WChar c;
	char identifier[identifierLengthMax];
	int identifierEnd;
	UInt16 indexNew;
	UInt16 indexOld;
	UInt32 uid;
	Boolean lastCharWasQuotedPrintableContinueToNextLineChr;
	Boolean quotedPrintable;
	volatile ToDoItemType newToDo;
	int   nv;
	char * fieldP;
	Boolean firstLoop = true;
	volatile Err error = 0;
	char categoryName[dmCategoryLength + 1];		/* +1: line below NUL-terminates at [len] */
	UInt16	categoryID = dmUnfiledCategory;

	*uniqueIDP = 0;
	categoryName[0] = 0;

	c = inputFunc(inputStream);
	ImcReadWhiteSpace(inputStream, inputFunc, NULL, &c);
	
	if (c == EOF) return false;
	
	identifierEnd = 0;
	if (!beginAlreadyRead)
		{
		// CAUTION: IsAlpha(EOF) returns true
		while (TxtCharIsAlpha(c) || c == ':' || 
			(c != linefeedChr && c != 0x0D && TxtCharIsSpace(c)))
			{
			if (!TxtCharIsSpace(c) && (identifierEnd < identifierLengthMax - sizeOf7BitChar(nullChr)))
		 		identifier[identifierEnd++] = (char) c;
		 	
			c = inputFunc(inputStream);
			if (c == EOF) return false;
			}
		}
	identifier[identifierEnd++] = nullChr;

		
	// Read in the vToDo entry
	if (!(StrCaselessCompare(identifier, "BEGIN:VTODO") == 0 || beginAlreadyRead))
		return false;
	
	// Initialize the record to default values
	/* Td35: the era DateType is WIDE (4 bytes); the old 2-byte cast left month/day
	 * uninitialised. Set the full "no due date" sentinel (year 0xffff). */
	newToDo.dueDate.year = (Int16) 0xffff;
	newToDo.dueDate.month = 0;
	newToDo.dueDate.day = 0;
	newToDo.priority = defaultPriority;
	newToDo.priority &= incompleteFlag;
	newToDo.description = NULL;
	newToDo.note = NULL;
	uid = 0;
	
	// An error happens usually due to no memory.  It's easier just to 
	// catch the error.  If an error happens, we remove the last record.  
	// Then we throw a second time so the caller receives it and displays a message.
	ErrTry
		{
			do
				{
				quotedPrintable = false;
				
				if(!beginAlreadyRead || !firstLoop)
					{
					// Advance to the next line.  If the property wasn't handled we need to make sure that
					// if the property value contains quoted printable text that newlines within that 
					// section are skipped.
					lastCharWasQuotedPrintableContinueToNextLineChr = (c == '=');
					while ((c != endOfLineChr || lastCharWasQuotedPrintableContinueToNextLineChr) && c != EOF)
						{
						lastCharWasQuotedPrintableContinueToNextLineChr = (c == '=');
						c = inputFunc(inputStream);
						} 
					
					c = inputFunc(inputStream);
					}
					
				
				// Skip the second part of a 0x0D 0x0A end of line sequence
				if (c == 0x0A)
					c = inputFunc(inputStream);
				else if (c == EOF)
					ErrThrow(exgErrBadData);
					
					
				firstLoop = false;
				
				// Read in an identifier.
				identifierEnd = 0;
				ImcReadWhiteSpace(inputStream, inputFunc, NULL, &c);
				while (c != valueDelimeterChr && c != parameterDelimeterChr && 
					c != groupDelimeterChr && c != endOfLineChr && c != EOF)
					{
					if (identifierEnd < identifierLengthMax - sizeOf7BitChar(nullChr))
						identifier[identifierEnd++] = (Char) c;
					
					c = inputFunc(inputStream);
					}
				identifier[identifierEnd++] = nullChr;
					
			
				
				// Handle Priority tag 
				if (StrCaselessCompare(identifier, "PRIORITY") == 0)
					{
					ImcSkipAllPropertyParameters(inputStream, inputFunc, &c, identifier, &quotedPrintable);
					fieldP = ImcReadFieldNoSemicolon(inputStream, inputFunc, &c, imcUnlimitedChars);
					if (fieldP != NULL)
						{
						nv = StrAToI(fieldP);
				   	nv = min(nv, toDoMaxPriority);
						newToDo.priority = nv | (newToDo.priority & completeFlag);
						MemPtrFree(fieldP);
						}
					}
					

				// Handle the due date.
				if (StrCaselessCompare(identifier, "DUE") == 0)
					{
					ImcSkipAllPropertyParameters(inputStream, inputFunc, &c, identifier, &quotedPrintable);
					fieldP = ImcReadFieldNoSemicolon(inputStream, inputFunc, &c, imcUnlimitedChars);
					if (fieldP != NULL)
						{
						// Extract due date
						MatchDateToken(fieldP, (DateType*)&newToDo.dueDate);
						MemPtrFree(fieldP);
						}
					}


				// Handle the two cases that indicate completed	
				//the Status:completed property
				if (StrCaselessCompare(identifier, "STATUS") == 0)
					{
					ImcSkipAllPropertyParameters(inputStream, inputFunc, &c, identifier, &quotedPrintable);
					fieldP = ImcReadFieldQuotablePrintable(inputStream, inputFunc, &c, parameterDelimeterChr, quotedPrintable, imcUnlimitedChars);
					if (fieldP != NULL)
						{
						if (StrCaselessCompare(fieldP, "COMPLETED") == 0)
							{
							newToDo.priority |=  completeFlag;
							}
						
						MemPtrFree(fieldP);
						}
					}
				// and the date/time completed property
				if  (StrCaselessCompare(identifier, "COMPLETED") == 0)
					{
					newToDo.priority |=  completeFlag;
					}
			
				// Handle the description	
				if  (StrCaselessCompare(identifier, "DESCRIPTION") == 0)
					{
					
					if( newToDo.description != NULL)
						{
						MemPtrFree(newToDo.description);
						}
					
					ImcSkipAllPropertyParameters(inputStream, inputFunc, &c, identifier, &quotedPrintable);
					newToDo.description =ImcReadFieldQuotablePrintable(inputStream, inputFunc, &c, endOfLineChr, quotedPrintable, tableMaxTextItemSize);
					}

				// Treat attachments as notes
				if (StrCaselessCompare(identifier, "ATTACH") == 0)
					{
					// Note: vCal permits attachments of types other than text, specifically
					// URLs and Content ID's.  At the moment, wee will just treat both of these
					// as text strings
					
					if( newToDo.note != NULL)
						{
						MemPtrFree(newToDo.note);
						}
					
					ImcSkipAllPropertyParameters(inputStream, inputFunc, &c, identifier, &quotedPrintable);
					newToDo.note = ImcReadFieldQuotablePrintable(inputStream, inputFunc, &c, endOfLineChr, quotedPrintable, noteViewMaxLength);
					}
				
				// read in the category
				if (StrCaselessCompare(identifier, "CATEGORIES") == 0)
					{
					Char * categoryStringP;

					// Skip property parameters
					ImcSkipAllPropertyParameters(inputStream, inputFunc, &c, identifier, &quotedPrintable);
					
					// Read in the category
					categoryStringP = ImcReadFieldQuotablePrintable(inputStream, inputFunc, &c, endOfLineChr, quotedPrintable, imcUnlimitedChars);
					if (categoryStringP != NULL)
						{
						char	*spot;
						
						// Start with the one found
						spot = categoryStringP;
						
						// If the category was not a predefined vCal catval,
						// we need to skip the leading special category name mark ("X-")
						if (spot[0] == 'X' && spot[1] == '-')
							spot += 2;

						// Make a copy
						StrNCopy(categoryName, spot, dmCategoryLength);
						
						// Make sure it is null terminated
						categoryName[dmCategoryLength] = 0;

						// Free the string (Imc routines allocate the space)
						MemPtrFree(categoryStringP);

						// Since this uses CATEGORIES, we need to skip any additional ones here.
						// Note that ToDo itself does not do that, but an arbitrary VCARD could.
						// Since they are comma seperated, just look for the first comma and
						// make it null instead.
						spot = categoryName;
						while (*spot && (*spot != ',')) spot++;
						*spot = 0;
						}
					}

				// read in the unique identifier
				if (StrCaselessCompare(identifier, "UID") == 0 && obeyUniqueIDs)
					{
					Char * uniqueIDStringP;
					ImcSkipAllPropertyParameters(inputStream, inputFunc, &c, identifier, &quotedPrintable);
					
					
					uniqueIDStringP = ImcReadFieldQuotablePrintable(inputStream, inputFunc, &c, endOfLineChr, quotedPrintable, imcUnlimitedChars);
					if (uniqueIDStringP != NULL)
						{
						uid = StrAToI(uniqueIDStringP);
						MemPtrFree(uniqueIDStringP);
						
						// Check the uid for reasonableness.
						if (uid < (dmRecordIDReservedRange << 12))
							uid = 0;
						}
					}
				// stop if at the end
				else if (StrCaselessCompare(identifier, "END") == 0)
					{
						// Advance to the next line.
					while (c != endOfLineChr && c != EOF)
						{
						c = inputFunc(inputStream);
						} 
					
					break;
					}

					
				} while (true);
		
		// Make sure that there are values for description and note	
		if (newToDo.description == NULL)
			{

			 newToDo.description = (Char *) MemPtrNew(sizeOf7BitChar('\0'));
			 *newToDo.description = '\0' ;
			}
		
		if (newToDo.note == NULL)
			{
			// Need to add bound checking here so we don't read in
			// more chars than the max note size
			 newToDo.note = (Char *) MemPtrNew(sizeOf7BitChar('\0'));
			 *newToDo.note = '\0' ;
			}
			

		// Td35: file the received item into a real category rather than always Unfiled.
		// Priority: (a) the category chosen at beam-receive time (carried via appData -- the
		// transient inbox stashes the receiver's pick there), else (b) the category named in
		// the beamed vCal (CATEGORIES:), creating it on this device if there is a free slot,
		// else Unfiled.  (The old code dropped the vCal category behind OLD_CATEGORY_TRANSPORT
		// and fell through to Unfiled -- that was the "always Unfiled" bug.)
		if (((ExgSocketPtr)inputStream)->appData) {
				categoryID = ((ExgSocketPtr)inputStream)->appData & dmRecAttrCategoryMask;
		}
		// If a category was included in the vCal, try to use it
		else if (categoryName[0]) {

			// Get the category ID
			categoryID = CategoryFind(dbP, categoryName);

			// If it doesn't exist, and we have room, create it
			if (categoryID == dmAllCategories) {
				// Find the first unused category
				categoryID = CategoryFind(dbP, "");

				// If there is a slot, fill it with the name we were given
				if (categoryID != dmAllCategories) {
					CategorySetName(dbP, categoryID, categoryName);
				}
				else
					categoryID = dmUnfiledCategory; // reset to default
			}
		}

		// Write the actual record
		if (ToDoNewRecord(dbP, (ToDoItemType*)&newToDo, categoryID, & indexNew))
			ErrThrow(exgMemError);
			
		// If uid was set then a unique id was passed to be used.
		if (uid != 0 && obeyUniqueIDs)
			{
			// We can't simply remove any old record using the unique id and
			// then add the new record because removing the old record could
			// move the new one.  So, we find any old record, change the new
			// record, and then remove the old one.
			indexOld = indexNew;
			
			// Find any record with this uid.  indexOld changes only if 
			// such a record is found.
			DmFindRecordByID (dbP, uid, &indexOld);
			
			// Change this record to this uid.  The dirty bit is set from 
			// newly making this record.
			DmSetRecordInfo(dbP, indexNew, NULL, &uid);
			
			// Now remove any old record.
			if (indexOld != indexNew)
				{
				DmRemoveRecord(dbP, indexOld);
				}
			}
		
		// Return the unique id of the record inserted.
		DmRecordInfo(dbP, indexNew, NULL, uniqueIDP, NULL);
		}
	
	ErrCatch(inErr)
		{
		// Throw the error after the memory is cleaned winUp.
		error = inErr;
		} ErrEndCatch

	
	// Free any temporary buffers used to store the incoming data.
	if (newToDo.note) MemPtrFree(newToDo.note);
	if (newToDo.description) MemPtrFree(newToDo.description);

	if (error)
		ErrThrow(error);
	
	return (c != EOF);
}
		

/************************************************************
 *
 * FUNCTION: ToDoImportVCal
 *
 * DESCRIPTION: Import a VCal record of type vEvent and vToDo
 *
 * The Datebook handles vCalendar records.  Any vToDo records 
 * are sent to the ToDo app for importing.
 *
 * This routine doesn't exist for the device since the Datebook 
 * is sent vCal data.  The Datebook will To Do any vToDo records
 * via an action code.  This routine is only for the simulator
 * because the Datebook can't run at the same time.
 *
 * PARAMETERS: 
 *			dbP - pointer to the database to add the record to
 *			inputStream	- pointer to where to import the record from
 *			inputFunc - function to get input from the stream
 *			obeyUniqueIDs - true to obey any unique ids if possible
 *			beginAlreadyRead - whether the begin statement has been read
 *			vToDoFunc - function to import vToDo records
 *						on the device this is a function to call ToDo to read
 *						for the shell command this is an empty function
 *
 * RETURNS: true if the input was read
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			djk	8/9/97		Created
 *			roger	9/12/97		Modified to work on the device
 *
 *************************************************************/
 
TD35_XFER extern Boolean 
ToDoImportVCal(DmOpenRef dbP, void * inputStream, GetCharF inputFunc,
	Boolean obeyUniqueIDs, Boolean beginAlreadyRead, ImportVEventF vEventFunc)
{
	WChar c = '\n';
	Char identifier[40];
	int identifierEnd;
	UInt32 uid;
	UInt32 uniqueID;
	Err error = 0;
	
	uid = 0;
	
	c = inputFunc(inputStream);
	if (c == EOF) return false;
		
	identifierEnd = 0;
	// CAUTION: IsAlpha(EOF) returns true
	while (TxtCharIsAlpha(c) || c == ':' || (c != linefeedChr && TxtCharIsSpace(c)))
		{
		if (!TxtCharIsSpace(c) && (identifierEnd < sizeof(identifier) - sizeOf7BitChar(nullChr)))
	 		identifier[identifierEnd++] = (Char) c;
	 	
		c = inputFunc(inputStream);
		if (c == EOF) return false;
		}
	identifier[identifierEnd++] = nullChr;
	
	// Read in the vcard entry
	if (StrCaselessCompare(identifier, "BEGIN:VCALENDAR") == 0)
		{
		do
			{
			identifierEnd = 0;
			c = inputFunc(inputStream);
			if (c == EOF) return false;
			// CAUTION: IsAlpha(EOF) returns true
			while (TxtCharIsAlpha(c) || c == ':' || (c != linefeedChr && TxtCharIsSpace(c) ||
			      TxtCharIsAlNum(c) || c == '.' ))
				{
				if (!TxtCharIsSpace(c) && (identifierEnd < sizeof(identifier) - sizeOf7BitChar(nullChr)))
	 				identifier[identifierEnd++] = (Char) c;
	 	
				c = inputFunc(inputStream);
				if (c == EOF) return false;
				}
			identifier[identifierEnd++] = nullChr;

			// Hand it off to the correct sub-routine
			// Note: here VCalEventRead is a dummy routine that just runs until it finds an end
			if (StrCaselessCompare(identifier, "BEGIN:VTODO") == 0)
				{
				ToDoImportVToDo(dbP, inputStream, inputFunc, obeyUniqueIDs, true, &uniqueID);
				ToDoSetGoToParams (dbP, inputStream, uniqueID);
				}
			else if (StrCaselessCompare(identifier, "BEGIN:VEVENT") == 0)
				{
				error = vEventFunc(dbP, inputStream, inputFunc, obeyUniqueIDs, true);
				if (error)
					ErrThrow(error);
				}
			// Handle the end of a calender.
			else if (StrCaselessCompare(identifier, "END:VCALENDAR") == 0)
				{
				// Advance to the next line.
				while ((c != '\n') && (c != EOF))
					{
					c = inputFunc(inputStream);
					} 
				
				break;
				}
			
			} while (true);
		
		}

	return (c != EOF);
}


/************************************************************
 *
 * FUNCTION: ToDoExportVCal
 *
 * DESCRIPTION: Export a VCALENDAR record.
 *
 * PARAMETERS: 
 *			dbP - pointer to the database to add the record to
 *			inputStream	- pointer to where to import the record from
 *			inputFunc - function to get input from the stream
 *			obeyUniqueIDs - true to obey any unique ids if possible
 *			beginAlreadyRead - whether the begin statement has been read
 *
 * RETURNS: true if the input was read
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			djk	8/9/97	Created
 *
 *************************************************************/
 
TD35_XFER extern void ToDoExportVCal(DmOpenRef dbP, Int16 index, ToDoDBRecordPtr recordP, 
 	void * outputStream, PutStringF outputFunc, Boolean writeUniqueIDs)
{

	Char * 		note;   			// b/c the note doesnt have its own pointer in the record
	UInt32		uid;
	Char 			tempString[tempStringLengthMax];
	UInt16 		attr;

	ErrNonFatalDisplayIf (dmCategoryLength > tempStringLengthMax, 
						"ToDoExportVCal: tempString too Int16");

	outputFunc(outputStream, "BEGIN:VTODO" imcLineSeparatorString);

	// Emit the Category
	outputFunc(outputStream, "CATEGORIES");
	DmRecordInfo (dbP, index, &attr, NULL, NULL);
	CategoryGetName(dbP, (attr & dmRecAttrCategoryMask), tempString);

	// Check if "CHARSET=ISO-8859-1" is needed
	if (!ImcStringIsAscii(&tempString[0]))
		{
		outputFunc(outputStream, ";CHARSET=ISO-8859-1");
		}
	
	outputFunc(outputStream, ":");
	
	// Check to see if the category is a predefined vCal catval
	if((StrCaselessCompare(tempString, "Personal") != 0) && 
		(StrCaselessCompare(tempString, "Business") != 0))
		{
		// if not, output the special category ame mark "X-"
		outputFunc(outputStream, "X-");
		}
	
	// Write the category name
	outputFunc(outputStream, tempString);
	outputFunc(outputStream, imcLineSeparatorString);

	// Emit the Due date
	if (DateToInt(recordP->dueDate) != toDoNoDueDate)
		{
		outputFunc(outputStream, "DUE:");
		
		// NOTE: since we don't keep a time for ToDo due dates,
		// we will truncate the ISO 8601 date/time to an ISO 8601 date
		// as allowed by the standard
		StrPrintF(tempString, "%d%02d%02d", firstYear + recordP->dueDate.year, 
			 	  recordP->dueDate.month, recordP->dueDate.day);
		
		outputFunc(outputStream, tempString);

		outputFunc(outputStream, imcLineSeparatorString);
		}
		
	// Emit the completed flag
	if (recordP->priority & completeFlag)
	 	 outputFunc(outputStream, "STATUS:COMPLETED" imcLineSeparatorString);
	else
		 outputFunc(outputStream, "STATUS:NEEDS ACTION" imcLineSeparatorString);

	// Emit the Priority Level
	if ((recordP->priority & priorityOnly) != NULL)
		{
	 	outputFunc(outputStream, "PRIORITY:");
		StrPrintF(tempString, "%d" imcLineSeparatorString, recordP->priority & priorityOnly);
		
		outputFunc(outputStream, tempString);
		}
		
	// Emit the Decsription Text
	if(recordP->description != NULL)
		{
		outputFunc(outputStream, "DESCRIPTION");
		
		ImcWriteQuotedPrintable(outputStream, outputFunc, &recordP->description, false);

		outputFunc(outputStream, imcLineSeparatorString);
		}

	// Get the pointer to the note
	note = (&recordP->description) + StrLen(&recordP->description) + 1;

	// Emit the note
	if(*note != '\0')
		{
		outputFunc(outputStream, "ATTACH");
		
		ImcWriteQuotedPrintable(outputStream, outputFunc, note, false);

		outputFunc(outputStream, imcLineSeparatorString);
		}

		
	// Emit an unique id
	if (writeUniqueIDs)
		{
		outputFunc(outputStream, "UID:");
		
		// Get the record's unique id and append to the string.
		DmRecordInfo(dbP, index, NULL, &uid, NULL);
		StrIToA(tempString, uid);
		outputFunc(outputStream, tempString);
		outputFunc(outputStream, imcLineSeparatorString);
		}
		
		outputFunc(outputStream, "END:VTODO" imcLineSeparatorString);
}

