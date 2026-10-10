#include "Beam.h"
#include "Main.h"
#include "Database.h"
#include "CryptoSec35.h"


static void PrvStreamInit(StreamType *streamP, ExgSocketPtr exgSocketP) BEAM_SECTION;
static void PrvStreamFlush(StreamType *streamP) BEAM_SECTION;
static UInt32 PrvStreamWrite(StreamType *streamP, const Char * stringP, Int32 length, Err *errP) BEAM_SECTION;
static UInt32 WriteFunction(void * stream, const Char * const bufferP, Int32 length) BEAM_SECTION;
static Err MemoSendRecordTryCatch (MemPtr, ExgSocketPtr) BEAM_SECTION;
static Err MemoSendCategoryTryCatch (DmOpenRef dbP, UInt16 categoryNum, ExgSocketPtr exgSocketP, UInt16 index) BEAM_SECTION;


/***********************************************************************
 * MemoSendRecord
 * Send a record.  
 ***********************************************************************/

extern void MemoSendRecord (DmOpenRef dbP, Int16 recordNum)
{
	Char * recordP;
	MemHandle recordH = NULL;
	MemHandle cryptoTextH = NULL;
	Err error;
	ExgSocketType exgSocket;

	Boolean descriptionFit;
	UInt16 descriptionSize;
	Int16 descriptionWidth;
	Int16 unused;

	Char * spaceP;
	Int16 filenameLength;

	MemHandle descriptionH;
	Char * descriptionP;
	MemHandle filenameH;
	Char * filenameP;

	// important to init structure to zeros...
	MemSet(&exgSocket, sizeof(exgSocket), 0);

	// Form a description of what's being sent.  This will be displayed
	// by the system send dialog on the sending and receiving devices.
	//
	// Db35-suite-style Sec35 wrap: a CryptoPad record is a packed
	// PackedRecordType (possibly Sec35-ciphertext), never a plain
	// C-string like a MemoPad record -- the caller (EditForm.c's
	// itemRecordBeamMemo) already blocks beaming a layer-2-encrypted memo,
	// but Sec35 wrapping is independent of that, so this is still needed.
	if (dbP == dbCryptoPadRef) {
		if (! CryptoCopyTextToHandle (dbP, recordNum, &cryptoTextH)) return;
		recordP = (Char *) MemHandleLock (cryptoTextH);
	} else {
		recordH = DmQueryRecord (dbP, recordNum);
		recordP = MemHandleLock(recordH);
	}

	// Set the description to be the beginning of the memo
	exgSocket.description ; NULL;
	
	// Set the exg description to the record's description.
	descriptionSize = StrLen(recordP);
	WinGetDisplayExtent(&descriptionWidth, &unused);
	FontCharsInWidth(recordP, &descriptionWidth, &descriptionSize);

	exgSocket.length = descriptionSize;		// rough guess
    if (newMemoDB) {
        exgSocket.target = memoPadCreatorNew;
    }
    else {
        exgSocket.target = memoPadCreator;
    }

	if (descriptionSize > 0) {
		descriptionP = MemPtrNew(descriptionSize + sizeof('\0'));
		StrNCopy (descriptionP, recordP, descriptionSize);
		descriptionP[descriptionSize] = nullChr;
	} else {
		descriptionH = DmGetResource('tSTR', strBeamDescription);
		descriptionP = MemHandleLock(descriptionH);
	}
	
	if (descriptionSize > 0) {
		// Now form a file name.  Use only the first word or two.
		spaceP = StrChr(descriptionP, spaceChr);
		if (spaceP)
			// Check for a second space
			spaceP = StrChr(spaceP + sizeof(spaceChr), spaceChr);
	
		// If at least two spaces were found then use only that much of the description.
		// If less than two spaces were found then use all of the description.
		if (spaceP)
			filenameLength = spaceP - descriptionP;
		else
			filenameLength = StrLen(descriptionP);
		
		
		// Allocate space and form the filename
		filenameP = MemPtrNew(filenameLength + StrLen(memoFilenameExtension) + sizeof('\0'));
		StrNCopy(filenameP, recordP, filenameLength);
		filenameP[filenameLength] = 0;
		StrCat(filenameP, memoFilenameExtension);
	} else {
		filenameH = DmGetResource('tSTR', strBeamFilename);
		filenameP = MemHandleLock(filenameH);
	}

	exgSocket.description = descriptionP;
	
	exgSocket.name = filenameP;

	error = ExgPut(&exgSocket);   // put data to destination
	if (!error)
		{
		error = MemoSendRecordTryCatch(recordP, &exgSocket);

		ExgDisconnect(&exgSocket, error);
		}

	// Clean up
	if (descriptionSize > 0) {
		MemPtrFree(descriptionP);
		MemPtrFree(filenameP);
	} else {
	   	MemHandleUnlock(descriptionH);
		DmReleaseResource(descriptionH);
	   	MemHandleUnlock(filenameH);   
		DmReleaseResource(filenameH);
	}

	if (recordH) MemHandleUnlock(recordH);
	if (cryptoTextH) { MemHandleUnlock(cryptoTextH); MemHandleFree(cryptoTextH); }

	return;
}

/***********************************************************************
 * MemoSendCategory
 * Send all visible records in a category.  
 ***********************************************************************/

extern void MemoSendCategory (DmOpenRef dbP, UInt16 categoryNum)
{
	Err error;
   Char description[dmCategoryLength];
	UInt16 index;
	Boolean foundAtLeastOneRecord;
	ExgSocketType exgSocket;
	UInt16 mode;
	LocalID dbID;
	UInt16 cardNum = cardNo;
	Boolean databaseReopened;
	
	
	// If the database was opened to show secret records, reopen it to not see 
	// secret records.  The idea is that secret records are not sent when a 
	// category is sent.  They must be explicitly sent one by one.
	DmOpenDatabaseInfo(dbP, &dbID, NULL, &mode, &cardNum, NULL);
	if (mode & dmModeShowSecret)
		{
		dbP = DmOpenDatabase(cardNum, dbID, dmModeReadOnly);
		databaseReopened = true;
		}
	else
		databaseReopened = false;
	
	
	// important to init structure to zeros...
	MemSet(&exgSocket, sizeof(exgSocket), 0);
	
	// Make sure there is at least one record in the category.
	index = 0;
	foundAtLeastOneRecord = false;
	while (true)
		{
		if (DmSeekRecordInCategory(dbP, &index, 0, dmSeekForward, categoryNum) != 0)
			break;
		
		foundAtLeastOneRecord = DmQueryRecord(dbP, index) != 0;
		if (foundAtLeastOneRecord)
			break;
		
		
		index++;
		}
	
	
	// We should send the category because there's at least one record to send.
	if (foundAtLeastOneRecord)
		{
		// Form a description of what's being sent.  This will be displayed
		// by the system send dialog on the sending and receiving devices.
		CategoryGetName (dbP, categoryNum, description);
		exgSocket.description = description;
		
		// Now form a file name
		exgSocket.name = MemPtrNew(StrLen(description) + StrLen(memoFilenameExtension) + sizeof('\0'));
		if (exgSocket.name)
			{
			StrCopy(exgSocket.name, description);
			StrCat(exgSocket.name, memoFilenameExtension);
			}

		exgSocket.length = 0;		// rough guess
        if (newMemoDB) {
            exgSocket.target = memoPadCreatorNew;
        }
        else {
		    exgSocket.target = memoPadCreator;
        }
		error = ExgPut(&exgSocket);   // put data to destination
		
		if (!error)
			{
			error = MemoSendCategoryTryCatch (dbP, categoryNum, &exgSocket, index);
			
			ExgDisconnect(&exgSocket, error);
			}
		
		// Clean up
		if (exgSocket.name)
			MemPtrFree(exgSocket.name);
		}
	else
		FrmAlert(NoDataToBeamAlert);

	if (databaseReopened)
		DmCloseDatabase(dbP);
	
	return;
}


/***********************************************************************
 * PrvStreamInit
 * Function to put Initialize a stream socket. 
 ***********************************************************************/

static void PrvStreamInit(StreamType *streamP, ExgSocketPtr exgSocketP)
{
	streamP->socket = exgSocketP;
	streamP->bufSize = maxStreamBuf;
	streamP->pos = 0;
	streamP->len = 0;
}

/***********************************************************************
 * PrvStreamFlush
 * Function to put a string to the exg transport. 
 ***********************************************************************/

static void PrvStreamFlush(StreamType *streamP)
{
	Err err = 0;
	while (streamP->len && !err)
		streamP->len -= ExgSend(streamP->socket,streamP->buf,streamP->len,&err);

}

/***********************************************************************
 * PrvStreamWrite
 * Function to put a string to the exg transport. 
 ***********************************************************************/

static UInt32 PrvStreamWrite(StreamType *streamP, const Char * stringP, Int32 length, Err *errP)
{
	UInt32 count = 0;
	*errP = 0;

	while (count < length && !*errP)
	{
		if (streamP->len < streamP->bufSize) 
			{
			streamP->buf[streamP->len++] = *stringP++;
			count++;
			}
		else
		    streamP->len -= ExgSend(streamP->socket, streamP->buf, streamP->len, errP);
	}
	return count;
}

/***********************************************************************
 * WriteFunction
 * Function to put a string to the exg transport. 
 ***********************************************************************/

static UInt32 WriteFunction(void * stream, const Char * const bufferP, Int32 length)
{
	UInt32 len;
	Err err;

	// passing -1 length will assume a null terminated string
	if (length == -1)  length = StrLen(bufferP);
	
	len = PrvStreamWrite( stream, bufferP, length, &err);
	
	// If the bytes were not sent throw an error.
	if (len == 0 && length > 0)
		ErrThrow(err);
		
	return len;
}

/***********************************************************************
 * MemoSendRecordTryCatch
 * Send a record.  
 ***********************************************************************/

static Err MemoSendRecordTryCatch (MemPtr recordP, ExgSocketPtr exgSocketP)
{
   volatile Err error = 0;
   StreamType stream;

		PrvStreamInit(&stream, exgSocketP);
		
		// An error can happen anywhere during the send process.  It's easier just to 
		// catch the error.  If an error happens, we must pass it into ExgDisconnect.
		// It will then cancel the send and display appropriate ui.
		ErrTry
			{
			MemoExportMime(recordP, &stream, WriteFunction, false);
			}
		
		ErrCatch(inErr)
			{
			error = inErr;
			} ErrEndCatch

		PrvStreamFlush( &stream);
	return error;
}

/***********************************************************************
 * MemoSendCategoryTryCatch
 * Send all visible records in a category.  
 ***********************************************************************/

 static Err MemoSendCategoryTryCatch (DmOpenRef dbP, UInt16 categoryNum, 
	ExgSocketPtr exgSocketP, UInt16 index)
{
	volatile Err error = 0;
	volatile MemHandle outRecordH = 0;
	MemPtr outRecordP;
	StreamType stream;
	
	// An error can happen anywhere during the send process.  It's easier just to 
	// catch the error.  If an error happens, we must pass it into ExgDisconnect.
	// It will then cancel the send and display appropriate ui.
	ErrTry
		{

		PrvStreamInit(&stream, exgSocketP);
		
		// Write out the beginning of a multipart mime message
		WriteFunction(&stream, 
			mimeVersionString
			"Content-type: multipart/mixed; boundary=\"" simpleBoundary "\"" crlf crlf, stringZLen);
      
		// Loop through all records in the category.
		while (DmSeekRecordInCategory(dbP, &index, 0, dmSeekForward, categoryNum) == 0)
			{

			// Emit the record.  If the record is private do not emit it.
			outRecordH = DmQueryRecord (dbP, index);
			
			if (outRecordH != 0)
				{
				outRecordP = MemHandleLock(outRecordH);
				
				// Emit a mime boundary
				WriteFunction(&stream, delimiter crlf, stringZLen);

				MemoExportMime(outRecordP, &stream, WriteFunction, true);

				MemHandleUnlock(outRecordH);
				}
			
			index++;
			}
		outRecordH = 0;
		dbP = 0;
		
		// All done.  Write out an epilogue.
		WriteFunction(&stream, delimiter "--" crlf crlf, stringZLen);
		}
	
	ErrCatch(inErr)
		{
		error = inErr;
		
		if (outRecordH)
			MemHandleUnlock(outRecordH);
		} ErrEndCatch
	
	PrvStreamFlush(&stream);
	
	return error;	
}

/************************************************************
 * MemoExportMime
 * Export a record as a Imc Mime record
 *************************************************************/

void MemoExportMime(MemPtr recordP, void * outputStream, 
	WriteFunctionF outputFunc, Boolean outputMimeInfo)
{
	Char * c;
	Char * eolP;
	UInt32 len;

	// Write out all of the memo.  All linefeeds must be replaced with CRLF combos.
	c = recordP;

	if (outputMimeInfo)
		{
		outputFunc(outputStream, "Content-Type: Text/plain; charset=ISO-8859-1" crlf, stringZLen);
		
		outputFunc(outputStream, crlf, stringZLen);
		}
	
	while (*c != '\0')
		{
		eolP = StrChr(c, linefeedChr);
		if (eolP)
			{
			len = outputFunc( outputStream, c, eolP - c);

			outputFunc(outputStream, crlf, stringZLen);
			c = eolP + 1;
			}
		else if (*c != '\0')
			{
			eolP = StrChr(c, '\0');
			len = outputFunc( outputStream, c, eolP - c);
			
			c = eolP;
			}
		}
	outputFunc(outputStream, crlf, stringZLen);	// always end with an extra crlf
}

