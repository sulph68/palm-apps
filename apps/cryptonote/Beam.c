#include "Beam.h"
#include "Main.h"
#include "Database.h"
#include "CryptoSec35.h"


static void PrvStreamInit(StreamType *streamP, ExgSocketPtr exgSocketP) BEAM_SECTION;
static void PrvStreamFlush(StreamType *streamP) BEAM_SECTION;
static UInt32 PrvStreamWrite(StreamType *streamP, const Char * stringP, Int32 length, Err *errP) BEAM_SECTION;
static UInt32 WriteFunction(void * stream, const Char * const bufferP, Int32 length) BEAM_SECTION;
static Err MemoSendRecordTryCatch (MemPtr, ExgSocketPtr) BEAM_SECTION;
static Err MemoSendCategoryTryCatch (DmOpenRef memoDb, DmOpenRef cryptoDb, UInt16 categoryNum, ExgSocketPtr exgSocketP) BEAM_SECTION;
static Boolean BeamOpenNoSecret (DmOpenRef dbP, DmOpenRef* outP) BEAM_SECTION;


/***********************************************************************
 * MemoSendRecord
 * Send a record.  
 ***********************************************************************/

extern void MemoSendRecord (DmOpenRef dbP, Int16 recordNum)
{
	Char * recordP;
	MemHandle recordH = NULL;
	MemHandle cryptoTextH = NULL;

	// Db35-suite-style Sec35 wrap: a CryptoPad record is a packed
	// PackedRecordType (possibly Sec35-ciphertext), never a plain
	// C-string like a MemoPad record.
	if (dbP == dbCryptoPadRef) {
		if (! CryptoCopyTextToHandle (dbP, recordNum, &cryptoTextH)) return;
		recordP = (Char *) MemHandleLock (cryptoTextH);
	} else {
		recordH = DmQueryRecord (dbP, recordNum);
		recordP = MemHandleLock(recordH);
	}

	MemoSendText (recordP);

	if (recordH) MemHandleUnlock(recordH);
	if (cryptoTextH) {
		MemSet (recordP, (Int32) MemHandleSize (cryptoTextH), 0);	/* plaintext copy */
		MemHandleUnlock(cryptoTextH); MemHandleFree(cryptoTextH);
	}
}

/***********************************************************************
 * MemoSendText
 * Beam one memo's text (for a memo with its own password: the decrypted
 * text the edit form is showing, after the user agreed to send it as
 * plain text). The text is not changed or stored.
 ***********************************************************************/

extern void MemoSendText (Char * recordP)
{
	Err error;
	ExgSocketType exgSocket;

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

	// Set the description to be the beginning of the memo
	exgSocket.description = NULL;
	
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

	return;
}

/***********************************************************************
 * MemoSendCategory
 * Send all visible records in a category.  
 ***********************************************************************/

/* Secret records are not sent with a category (they must be beamed one by
 * one): if dbP shows them, open a second reference that doesn't. *outP is
 * the reference to use; true = it was opened here (close it afterwards). */
static Boolean BeamOpenNoSecret (DmOpenRef dbP, DmOpenRef* outP)
{
	UInt16	mode, cardNum = cardNo;
	LocalID	dbID;

	*outP = dbP;
	DmOpenDatabaseInfo(dbP, &dbID, NULL, &mode, &cardNum, NULL);
	if (! (mode & dmModeShowSecret)) return false;
	*outP = DmOpenDatabase(cardNum, dbID, dmModeReadOnly);
	if (! *outP) { *outP = dbP; return false; }
	return true;
}

/***********************************************************************
 * MemoSendCategory
 * Send every memo of the category: the ones still in Memo Pad and the
 * CryptoNote notes. A CryptoNote note with its own password is skipped
 * (beam it on its own from the memo, which asks first); the user is told
 * how many were skipped. Sec35 locked: hand off to Sec35 to unlock first.
 ***********************************************************************/
extern void MemoSendCategory (UInt16 categoryNum)
{
	Err error;
	Char description[dmCategoryLength];
	ExgSocketType exgSocket;
	DmOpenRef memoDb, cryptoDb;
	Boolean memoOpened, cryptoOpened;
	UInt16 index, sendable = 0, skipped = 0;

	memoOpened = BeamOpenNoSecret (dbMemoPadRef, &memoDb);
	cryptoOpened = BeamOpenNoSecret (dbCryptoPadRef, &cryptoDb);

	for (index = 0; DmSeekRecordInCategory(memoDb, &index, 0, dmSeekForward, categoryNum) == 0; index++)
		if (DmQueryRecord(memoDb, index)) sendable++;
	for (index = 0; DmSeekRecordInCategory(cryptoDb, &index, 0, dmSeekForward, categoryNum) == 0; index++) {
		if (! DmQueryRecord(cryptoDb, index)) continue;
		if (CryptoLockedBounce(cryptoDb, index)) goto done;	/* gone to Sec35 */
		if (CryptoGetRecordFlag(cryptoDb, index)) skipped++;	/* own password */
		else sendable++;
	}

	if (sendable == 0) {
		FrmAlert(skipped ? altBeamCategoryAllPassword : NoDataToBeamAlert);
		goto done;
	}

	// important to init structure to zeros...
	MemSet(&exgSocket, sizeof(exgSocket), 0);

	// Form a description of what's being sent.  This will be displayed
	// by the system send dialog on the sending and receiving devices.
	CategoryGetName (dbMemoPadRef, categoryNum, description);
	exgSocket.description = description;

	// Now form a file name
	exgSocket.name = MemPtrNew(StrLen(description) + StrLen(memoFilenameExtension) + sizeof('\0'));
	if (exgSocket.name) {
		StrCopy(exgSocket.name, description);
		StrCat(exgSocket.name, memoFilenameExtension);
	}

	exgSocket.length = 0;		// rough guess
	exgSocket.target = newMemoDB ? memoPadCreatorNew : memoPadCreator;
	error = ExgPut(&exgSocket);   // put data to destination
	if (!error) {
		error = MemoSendCategoryTryCatch (memoDb, cryptoDb, categoryNum, &exgSocket);
		error = ExgDisconnect(&exgSocket, error);	/* the beam's final outcome */
	}
	if (exgSocket.name)
		MemPtrFree(exgSocket.name);

	if (skipped && !error) {
		Char num[8];
		StrIToA(num, skipped);
		FrmCustomAlert(altBeamCategorySkipped, num, "", "");
	}

done:
	if (memoOpened) DmCloseDatabase(memoDb);
	if (cryptoOpened) DmCloseDatabase(cryptoDb);
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

/***********************************************************************
 * MemoSendCategoryTryCatch
 * Send the category's Memo Pad memos, then its CryptoNote notes without
 * their own password (read through Sec35; each plaintext copy is wiped).
 ***********************************************************************/
static Err MemoSendCategoryTryCatch (DmOpenRef memoDb, DmOpenRef cryptoDb, UInt16 categoryNum,
	ExgSocketPtr exgSocketP)
{
	volatile Err error = 0;
	volatile MemHandle outRecordH = 0;		/* Memo Pad record, locked */
	volatile MemHandle textH = 0;			/* CryptoNote plaintext copy */
	MemPtr outRecordP;
	StreamType stream;
	UInt16 index;

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

		// 1. Memos still in Memo Pad.
		for (index = 0; DmSeekRecordInCategory(memoDb, &index, 0, dmSeekForward, categoryNum) == 0; index++)
			{
			outRecordH = DmQueryRecord (memoDb, index);
			if (outRecordH != 0)
				{
				outRecordP = MemHandleLock(outRecordH);
				WriteFunction(&stream, delimiter crlf, stringZLen);
				MemoExportMime(outRecordP, &stream, WriteFunction, true);
				MemHandleUnlock(outRecordH);
				}
			outRecordH = 0;
			}

		// 2. CryptoNote notes, except the ones with their own password.
		for (index = 0; DmSeekRecordInCategory(cryptoDb, &index, 0, dmSeekForward, categoryNum) == 0; index++)
			{
			MemHandle h = 0;
			if (! DmQueryRecord (cryptoDb, index) || CryptoGetRecordFlag (cryptoDb, index))
				continue;
			if (! CryptoCopyTextToHandle (cryptoDb, index, &h))
				continue;
			textH = h;
			outRecordP = MemHandleLock(h);
			WriteFunction(&stream, delimiter crlf, stringZLen);
			MemoExportMime(outRecordP, &stream, WriteFunction, true);
			MemSet(outRecordP, (Int32) MemHandleSize(h), 0);	/* plaintext copy */
			MemHandleUnlock(h);
			MemHandleFree(h);
			textH = 0;
			}

		// All done.  Write out an epilogue.
		WriteFunction(&stream, delimiter "--" crlf crlf, stringZLen);
		}

	ErrCatch(inErr)
		{
		error = inErr;
		if (outRecordH)
			MemHandleUnlock(outRecordH);
		if (textH)
			{
			MemHandleUnlock(textH);
			MemHandleFree(textH);
			}
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

