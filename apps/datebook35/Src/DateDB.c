/******************************************************************************
 *
 * Copyright (c) 1904-1999 Palm Computing, Inc. or its subsidiaries.
 * All rights reserved.
 *
 * File: DateDB.c
 *
 * Description:
 *		Appointment Manager routines
 *
 * History:
 *			Name	Date		Description
 *			----	----		-----------
 *			rsf	1/25/95	Created
 *			rbb	4/22/99	Added support for snooze feature
 *								Moved code to link w/ 16-bit jumps
 *
 *****************************************************************************/

#include <PalmOS.h>

// Set this to get to private database defines
#define __APPTMGR_PRIVATE__
#include "Datebook.h"
#include "enccore.h"		/* Db35: Sec35 shared-key AES-CTR (EncGetKey/EncCrypt) */


/***********************************************************************
 *
 *	Internal Structutes
 *
 ***********************************************************************/

// The following structure doesn't really exist.  The first field
// varies depending on the data present.  However, it is convient
// (and less error prone) to use when accessing the other information.
typedef struct {
	ApptDateTimeType 	when;
	ApptDBRecordFlags	flags;	// A flag set for each  datum present
	char				firstField;
	UInt8				reserved;
} ApptPackedDBRecordType;

typedef ApptPackedDBRecordType * ApptPackedDBRecordPtr;

/* Db35: endDateOffset is logically a single UInt8, but every optional
 * section AFTER it (alarm/repeat/exceptions) is a struct containing 2- or
 * 4-byte fields and gets pointer-cast directly onto the raw packed bytes
 * (see ApptUnpack) -- m68k faults ("address error") reading a misaligned
 * multi-byte field. A lone odd-sized section would shift everything after
 * it off the alignment the rest of this file already assumes, so it's
 * always packed/unpacked as 2 bytes (1 padding byte) to preserve parity. */
#define kEndDateOffsetPackedSize 2

typedef Int16 comparF (const void *, const void *, UInt16 other);



/***********************************************************************
 *
 *	Internal Routines
 *
 ***********************************************************************/
void ECApptDBValidate (DmOpenRef dbP);

static Int16 TimeCompare (TimeType t1, TimeType t2);

static Int16 DateCompare (DateType d1, DateType d2);

/***********************************************************************
 *
 *	External Routines
 *
 ***********************************************************************/
extern PendingAlarmQueueType*
ReserveAlarmInternals (
	PendingAlarmQueueType*		outAlarmInternalsP );

extern void
ReleaseAlarmInternals (
	PendingAlarmQueueType*		inAlarmInternalsP,
	Boolean							inModified );

extern UInt16
GetDismissedAlarmCount (
	PendingAlarmQueueType*		inAlarmInternalsP );

extern UInt32 *
GetDismissedAlarmList (
	PendingAlarmQueueType*		inAlarmInternalsP );



/************************************************************
 *
 *  FUNCTION: ECApptDBValidate
 *
 *  DESCRIPTION: This routine validates the integrity of a
 *					  datebook datebase.
 *
 *  PARAMETERS: database pointer
 *
 *  RETURNS: nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	11/15/95	Initial Revision
 *
 *************************************************************/
#if	EMULATION_LEVEL != EMULATION_NONE

#define maxDescLen	tableMaxTextItemSize
#define maxNoteLen	noteViewMaxLength

void ECApptDBValidate (DmOpenRef dbP)
{
	UInt16 i;
	UInt16 size;
	UInt16 len;
	UInt16 blockSize;
	UInt16 numRecord;
	MemHandle recH;
	DateType date;
	ApptDBRecordType rec;
	
	numRecord = DmNumRecords (dbP);
	for (i = 0 ; i < numRecord; i++)
		{
		recH = DmQueryRecord (dbP, i);
		if (! recH) continue;

		ApptGetRecord (dbP, i, &rec, &recH);
		
		// Is the event an untimed event?
		if (TimeToInt(rec.when->startTime) == apptNoTime)
			{
			// There should not be and end time if there is no start time.
			if (TimeToInt(rec.when->endTime) != apptNoTime)
				ErrDisplay ("DB integrity error");				
			}

		// Validate the event date.
		if ((rec.when->date.month == 0) ||
			 (rec.when->date.month > 12) ||
			 (rec.when->date.day == 0) ||
			 (rec.when->date.day > DaysInMonth (rec.when->date.month, 
			 	rec.when->date.year + firstYear)))
			ErrDisplay ("DB integrity error");				
			 

		// The start time may not be greater than the end time -- UNLESS the
		// event has a Db35 end-date offset (spans to a later day), in which
		// case end < start is exactly what a midnight-crossing event looks
		// like (e.g. 11pm start, 1am end the next day).
		else if (rec.endDateOffset == 0 &&
					TimeCompare (rec.when->startTime, rec.when->endTime) > 0)
			{
			ErrDisplay ("DB integrity error");
			}

		// Validate the alarm info.
		if (rec.alarm)
			{
			if (rec.alarm->advance > 99)
				ErrDisplay ("DB integrity error");				

			if (rec.alarm->advanceUnit > aauDays)
				ErrDisplay ("DB integrity error");				
			}


		// Validate the repeat info.
		if (rec.repeat)
			{
			// Validate the repeat type.
			if (rec.repeat->repeatType > repeatYearly)
				ErrDisplay ("DB integrity error");				

			// Validate the repeat end date.
			date = rec.repeat->repeatEndDate;
			if (DateToInt (date) != apptNoEndDate)
				{
				if (DateCompare (date, rec.when->date) < 0)
					ErrDisplay ("DB integrity error");				
				
				if ((date.month == 0) ||
					 (date.month > 12) ||
					 (date.day == 0) ||
					 (date.day > DaysInMonth (date.month, date.year + firstYear)))
					ErrDisplay ("DB integrity error");
				}				
			
			// Validate the repeat frequency.
			if (rec.repeat->repeatFrequency > 99)
				ErrDisplay ("DB integrity error");				

			// Validate the "repeatOn" info
			if (rec.repeat->repeatType == repeatWeekly)
				{
				if (rec.repeat->repeatOn == 0)
					ErrDisplay ("DB integrity error");				
				}
			else if (rec.repeat->repeatType == repeatMonthlyByDay)
				{
				if (rec.repeat->repeatOn > domLastSat)
					ErrDisplay ("DB integrity error");				
				}
			else
				{
				if (rec.repeat->repeatOn != 0)
					ErrDisplay ("DB integrity error");				
				}

			// Validate the "repeatStartOfWeek" info,
			if (rec.repeat->repeatType == repeatWeekly)
				{
				if (rec.repeat->repeatStartOfWeek > monday)
					ErrDisplay ("DB integrity error");
				}
			else if (rec.repeat->repeatStartOfWeek)
				ErrDisplay ("DB integrity error");

			}

		// Validate the record size.
		size = sizeof (ApptDateTimeType) + sizeof (ApptDBRecordFlags);
		if (rec.endDateOffset)
			size += kEndDateOffsetPackedSize;
		if (rec.alarm)
			size += sizeof (AlarmInfoType);
		if (rec.repeat)
			size += sizeof (RepeatInfoType);
		if (rec.exceptions)
			size += sizeof (DateType) * rec.exceptions->numExceptions +
				sizeof (UInt16);
		if (rec.description)
			{
			len = StrLen (rec.description);
			ErrFatalDisplayIf (len > maxDescLen, "DB integrity error");
			size += len + 1;
			}
		if (rec.note)
			{
			len = StrLen (rec.note);
			ErrFatalDisplayIf (len > maxNoteLen, "DB integrity error");
			size += len + 1;
			}

		blockSize = MemHandleSize (recH);
//		ErrFatalDisplayIf ( (blockSize != size), "DB integrity error");

		MemHandleUnlock (recH);
		}
}
#endif

/***********************************************************************
 *
 * FUNCTION:    DateCompare
 *
 * DESCRIPTION: This routine compares two dates.
 *
 * PARAMETERS:  d1 - a date 
 *              d2 - a date 
 *
 * RETURNED:    if d1 > d2  returns a positive int
 *              if d1 < d2  returns a negative int
 *              if d1 = d2  returns zero
 *
 * NOTE: This routine treats the DateType structure like an unsigned int,
 *       it depends on the fact the the members of the structure are ordered
 *       year, month, day form high bit to low low bit.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/12/95		Initial Revision
 *
 ***********************************************************************/
static Int16 DateCompare (DateType d1, DateType d2)
{
	/* Db35 aware tier: DateToInt (EDateInt) is a full 32-bit monotonic key --
	   real years >=2032 exceed 16 bits, so these MUST be 32-bit or far-future
	   dates sort wrong ("Record not on day"). */
	Int32 int1, int2;
	Boolean f1, f2;

	/* The "forever" sentinel (year 0xffff, used for no-end-date repeats) must
	   sort as the LATEST date. Its DateToInt key is only 0xffff, which is SMALLER
	   than any real post-2031 date's key -- so without this a no-end repeat looked
	   ended once "today" passed 2031 (agenda->day-view crash). */
	f1 = ((UInt16) d1.year == 0xffffU);
	f2 = ((UInt16) d2.year == 0xffffU);
	if (f1 || f2)
		return (f1 == f2) ? 0 : (f1 ? 1 : -1);

	int1 = DateToInt(d1);
	int2 = DateToInt(d2);

	if (int1 > int2)
		return (1);
	else if (int1 < int2)
		return (-1);
	return 0;
}


/***********************************************************************
 *
 * FUNCTION:    TimeCompare
 *
 * DESCRIPTION: This routine compares two times.  "No time" is represented
 *              by minus one, and is considered less than all times.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    if t1 > t2  returns a positive int
 *              if t1 < t2  returns a negative int
 *              if t1 = t2  returns zero
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/12/95		Initial Revision
 *
 ***********************************************************************/
static Int16 TimeCompare (TimeType t1, TimeType t2)
{
	Int16 int1, int2;
	
	int1 = TimeToInt(t1);
	int2 = TimeToInt(t2);
	
	if (int1 > int2)
		return (1);
	else if (int1 < int2)
		return (-1);
	return 0;

}


/************************************************************
 *
 *  FUNCTION: ApptAppInfoInit
 *
 *  DESCRIPTION: Create and initialize the app info chunk if missing.
 *
 *  PARAMETERS: database pointer
 *
 *  RETURNS: 0 if successful, errorcode if not
 *
 *  CREATED: 1/25/95 
 *
 *  BY: Roger Flores
 *
 *************************************************************/
SEC002 Err ApptAppInfoInit(DmOpenRef dbP);		/* Db35: freed from main for crypto space */
Err	ApptAppInfoInit(DmOpenRef dbP)
{
	UInt16 				cardNo;
	MemHandle 		h;
	LocalID 			dbID;
	LocalID 			appInfoID;
	ApptAppInfoPtr	appInfoP;
	
	if (DmOpenDatabaseInfo(dbP, &dbID, NULL, NULL, &cardNo, NULL))
		return dmErrInvalidParam;
		
	if (DmDatabaseInfo(cardNo, dbID, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &appInfoID, NULL, NULL, NULL))
		return dmErrInvalidParam;
	
	if (appInfoID == NULL) 
		{
		h = DmNewHandle(dbP, sizeof(ApptAppInfoType));
		if (! h) return dmErrMemError;

		appInfoID = MemHandleToLocalID (h);
		DmSetDatabaseInfo(cardNo, dbID, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &appInfoID, NULL, NULL, NULL);
		}
		
	// Get pointer to app Info chunk
	appInfoP = MemLocalIDToLockedPtr(appInfoID, cardNo);
	
	// Init it
	DmSet(appInfoP, 0, sizeof(ApptAppInfoType), 0); 

	// Unlock it
	MemPtrUnlock(appInfoP);
	
	return 0;
}



/************************************************************
 *
 *  FUNCTION:    ApptComparePackedRecords
 *
 *  DESCRIPTION: Compare two packed records.
 *
 *  PARAMETERS:  r1    - database record 1
 *				     r2    - database record 2
 *               extra - extra data, not used in the function
 *
 *  RETURNS:    -1 if record one is less
 *		           1 if record two is less
 *
 *  CREATED: 1/14/95 
 *
 *  BY: Roger Flores
 *
 *	COMMENTS:	Compare the two records key by key until
 *	there is a difference.  Return -1 if r1 is less or 1 if r2
 *	is less.  A zero is never returned because if two records
 *	seem identical then their unique IDs are compared!
 *
 *************************************************************/ 
static Int16 ApptComparePackedRecords (ApptPackedDBRecordPtr r1, 
	ApptPackedDBRecordPtr r2, Int16 extra, SortRecordInfoPtr info1, 
	SortRecordInfoPtr info2, MemHandle appInfoH)
{
#pragma unused (extra, info1, info2, appInfoH)

	Int16 result;

	if ((r1->flags.repeat) || (r2->flags.repeat))
		{
		if ((r1->flags.repeat) && (r2->flags.repeat))
			result = 0;
		else if (r1->flags.repeat)
			result = -1;
		else
			result = 1;
		}

	else
		{
		result = DateCompare (r1->when.date, r2->when.date);
		if (result == 0)
			{
			result = TimeCompare (r1->when.startTime, r2->when.startTime);
			}
		}
	return result;
}


/************************************************************
 *
 *  FUNCTION: ApptPackedSize
 *
 *  DESCRIPTION: Return the packed size of an ApptDBRecordType 
 *
 *  PARAMETERS: database record
 *
 *  RETURNS: the size in bytes
 *
 *  CREATED: 1/25/95 
 *
 *  BY: Roger Flores
 *
 *************************************************************/
static UInt16 ApptPackedSize (ApptDBRecordPtr r)
{
	UInt16 size;

	
	size = sizeof (ApptDateTimeType) + sizeof (ApptDBRecordFlags);

	if (r->endDateOffset != 0)
		size += kEndDateOffsetPackedSize;

	if (r->alarm != NULL)
		size += sizeof (AlarmInfoType);
	
	if (r->repeat != NULL)
		size += sizeof (RepeatInfoType);
	
	if (r->exceptions != NULL)
		size += sizeof (UInt16) + 
			(r->exceptions->numExceptions * sizeof (DateType));
	
	if (r->description != NULL)
		size += StrLen(r->description) + 1;
	
	if (r->note != NULL)
		size += StrLen(r->note) + 1;
	

	return size;
}


/************************************************************
 *
 *  FUNCTION: ApptPack
 *
 *  DESCRIPTION: Pack an ApptDBRecordType
 *
 *  PARAMETERS: database record
 *
 *  RETURNS: the ApptPackedDBRecord is packed
 *
 *  CREATED: 1/25/95 
 *
 *  BY: Roger Flores
 *
 *************************************************************/
static void ApptPack(ApptDBRecordPtr s, ApptPackedDBRecordPtr d)
{
	ApptDBRecordFlags	flags;
	UInt16 	size;
	UInt32	offset = 0;
	
	
	*(UInt8 *)&flags = 0;			// clear the flags
	
	
	// copy the ApptDateTimeType
	//c = (char *) d;
	offset = 0;
	DmWrite(d, offset, s->when, sizeof(ApptDateTimeType));
	offset += sizeof (ApptDateTimeType) + sizeof (ApptDBRecordFlags);

	// Db35: end-date offset (days after when->date the event ends) -- first
	// optional section, grouped with the date. 0 = same day, unchanged.
	if (s->endDateOffset != 0)
		{
		UInt8 packed[kEndDateOffsetPackedSize];
		packed[0] = s->endDateOffset;
		packed[1] = 0;	/* pad -- keeps sections after this one aligned */
		DmWrite(d, offset, packed, kEndDateOffsetPackedSize);
		offset += kEndDateOffsetPackedSize;
		flags.endDate = 1;
		}

	if (s->alarm != NULL)
		{
		DmWrite(d, offset, s->alarm, sizeof(AlarmInfoType));
		offset += sizeof (AlarmInfoType);
		flags.alarm = 1;
		}
	

	if (s->repeat != NULL)
		{
		DmWrite(d, offset, s->repeat, sizeof(RepeatInfoType));
		offset += sizeof (RepeatInfoType);
		flags.repeat = 1;
		}
	

	if (s->exceptions != NULL)
		{
		size = sizeof (UInt16) + 
			(s->exceptions->numExceptions * sizeof (DateType));
		DmWrite(d, offset, s->exceptions, size);
		offset += size;
		flags.exceptions = 1;
		}
	
	
	if (s->description != NULL)
		{
		size = StrLen(s->description) + 1;
		DmWrite(d, offset, s->description, size);
		offset += size;
		flags.description = 1;
		}
	

	
	if (s->note != NULL)
		{
		size = StrLen(s->note) + 1;
		DmWrite(d, offset, s->note, size);
		offset += size;
		flags.note = 1;
		}
	
	DmWrite(d, sizeof(ApptDateTimeType), &flags, sizeof(flags));
}


/************************************************************
 *
 *  FUNCTION: ApptUnpack
 *
 *  DESCRIPTION: Fills in the ApptDBRecord structure
 *
 *  PARAMETERS: database record
 *
 *  RETURNS: the record unpacked
 *
 *  CREATED: 1/25/95 
 *
 *  BY: Roger Flores
 *
 *************************************************************/
static void ApptUnpack(ApptPackedDBRecordPtr src, ApptDBRecordPtr dest)
{
	ApptDBRecordFlags	flags;
	char *p;

	
	flags = src->flags;
	p = &src->firstField;

	dest->when = (ApptDateTimeType *) src;

	if (flags.endDate)
		{
		dest->endDateOffset = *(UInt8 *) p;
		p += kEndDateOffsetPackedSize;
		}
	else
		dest->endDateOffset = 0;

	if (flags.alarm)
		{
		dest->alarm = (AlarmInfoType *) p;
		p += sizeof (AlarmInfoType);
		}
	else
		dest->alarm = NULL;

	
	if (flags.repeat)
		{
		dest->repeat = (RepeatInfoType *) p;
		p += sizeof (RepeatInfoType);
		}
	else
		dest->repeat = NULL;
	
	
	if (flags.exceptions)
		{
		dest->exceptions = (ExceptionsListType *) p;
		p += sizeof (UInt16) + 
			(((ExceptionsListType *) p)->numExceptions * sizeof (DateType));
		}
	else
		dest->exceptions = NULL;
		
	
	if (flags.description)
		{
		dest->description = p;
		p += StrLen(p) + 1;
		}
	else
		dest->description = NULL;
		
	
	if (flags.note)
		{
		dest->note = p;
		}
	else
		dest->note = NULL;
	
}

/* ==== Db35 at-rest encryption (Sec35 shared key) ======================= *
 * Sensitive text = the appointment description + note.  The clear prefix
 * ([when][flags][alarm?][repeat?][exceptions?]) drives every date/time view and
 * stays plaintext; only the trailing [description\0][note\0] region is encrypted.
 * AES-CTR output can contain 0x00 (which would truncate the inline NUL-terminated
 * strings), so an encrypted record replaces that region with a length-prefixed form:
 *   plaintext:  <prefix> [description\0] [note\0]
 *   encrypted:  <prefix, flags.encrypted=1> [descLen:2 BE][noteLen:2 BE][encDesc][encNote]
 * flags.description / flags.note still say which are present (descLen/noteLen are the
 * plaintext lengths, 0 when absent).  Nonce = (uniqueID, 0)=desc, (uniqueID, 1)=note.
 * ApptGetRecord decrypts a record into a reused buffer (masks "******" when locked) so
 * every view/read works unchanged; Find skips encrypted records; beam-receive stores raw.
 * The crypto touches sec003 -> FOREGROUND only (see db35_seg.h). */

#define apptEncTtl	1000000000UL		/* read TTL; Sec35's alarm enforces the lock */

static UInt16 apptRdU16(const UInt8 *p) ENCCORE_SECTION;
static UInt16 apptRdU16(const UInt8 *p) { return (UInt16)(((UInt16)p[0] << 8) | p[1]); }
static void   apptWrU16(UInt8 *p, UInt16 v) ENCCORE_SECTION;
static void   apptWrU16(UInt8 *p, UInt16 v) { p[0] = (UInt8)(v >> 8); p[1] = (UInt8)v; }

/* Copy the record's 2-byte flags word out of the packed bytes (offset 8). */
static ApptDBRecordFlags ApptFlagsOf(const UInt8 *rec)
{
	ApptDBRecordFlags f;
	MemMove(&f, rec + sizeof(ApptDateTimeType), sizeof(f));
	return f;
}

/* True if ANY record is marked encrypted (raw flag scan; no key, no crypto -> MAIN,
 * sublaunch-safe). */
Boolean ApptDbHasEncrypted(DmOpenRef dbP) ENCCORE_SECTION;
Boolean ApptDbHasEncrypted(DmOpenRef dbP)
{
	UInt16 i, n = DmNumRecords(dbP);
	for (i = 0; i < n; i++) {
		MemHandle h = DmQueryRecord(dbP, i);
		if (h != NULL) {
			Boolean enc = (Boolean) ApptFlagsOf((const UInt8 *) MemHandleLock(h)).encrypted;
			MemHandleUnlock(h);
			if (enc) return true;
		}
	}
	return false;
}

/* Is record `index` encrypted?  Raw flag check (no key). ENCCORE_SECTION (foreground use). */
Boolean ApptRecEncrypted(DmOpenRef dbP, UInt16 index) ENCCORE_SECTION;
Boolean ApptRecEncrypted(DmOpenRef dbP, UInt16 index)
{
	MemHandle h = DmQueryRecord(dbP, index);
	Boolean enc;
	if (h == NULL) return false;
	enc = (Boolean) ApptFlagsOf((const UInt8 *) MemHandleLock(h)).encrypted;
	MemHandleUnlock(h);
	return enc;
}

/* Size of the clear prefix ([when][flags][alarm?][repeat?][exceptions?]) -- identical in
 * the plaintext and encrypted forms.  ENCCORE_SECTION (crypto-only caller). */
static UInt32 ApptPrefixSize(const UInt8 *rec) ENCCORE_SECTION;
static UInt32 ApptPrefixSize(const UInt8 *rec)
{
	ApptDBRecordFlags flags = ApptFlagsOf(rec);
	UInt32 o = sizeof(ApptDateTimeType) + sizeof(ApptDBRecordFlags);
	if (flags.endDate) o += kEndDateOffsetPackedSize;
	if (flags.alarm) o += sizeof(AlarmInfoType);
	if (flags.repeat) o += sizeof(RepeatInfoType);
	if (flags.exceptions) {
		UInt16 nExc;
		MemMove(&nExc, rec + o, sizeof(UInt16));
		o += sizeof(UInt16) + (UInt32) nExc * sizeof(DateType);
	}
	return o;
}

/* Build a DECRYPTED (or masked, when locked) INLINE copy of encrypted packed record `src`
 * into `out`; returns the total size.  `out` must hold prefix + descLen+1 + noteLen+1.
 * ENCCORE_SECTION. */
static UInt32 ApptBuildInline(const UInt8 *src, UInt32 uid, UInt8 *out) ENCCORE_SECTION;
static UInt32 ApptBuildInline(const UInt8 *src, UInt32 uid, UInt8 *out)
{
	ApptDBRecordFlags flags = ApptFlagsOf(src);
	UInt32 prefix = ApptPrefixSize(src);
	UInt16 descLen = apptRdU16(src + prefix);
	UInt16 noteLen = apptRdU16(src + prefix + 2);
	const UInt8 *enc = src + prefix + 4;
	UInt8 key[ENC_KEY_BYTES];
	Boolean haveKey = EncGetKey(key, apptEncTtl);
	UInt32 o = prefix;

	MemMove(out, src, prefix);						/* clear prefix (when..exceptions) */
	((ApptDBRecordFlags *)(out + sizeof(ApptDateTimeType)))->encrypted = 0;

	if (!haveKey) {									/* LOCKED: mask desc "******", drop note */
		((ApptDBRecordFlags *)(out + sizeof(ApptDateTimeType)))->note = 0;
		if (flags.description) {
			MemMove(out + o, "******", 6); out[o + 6] = 0; o += 7;
		}
		return o;
	}

	if (flags.description) {							/* description first (matches ApptUnpack) */
		MemMove(out + o, enc, descLen);
		if (descLen) EncCrypt(key, out + o, descLen, uid, 0);
		out[o + descLen] = 0; o += descLen + 1;
	}
	if (flags.note) {
		MemMove(out + o, enc + descLen, noteLen);
		if (noteLen) EncCrypt(key, out + o, noteLen, uid, 1);
		out[o + noteLen] = 0; o += noteLen + 1;
	}
	MemSet(key, ENC_KEY_BYTES, 0);
	return o;
}

/* Convert one record between plaintext and the length-prefixed encrypted layout, IN PLACE.
 * No-op if already in the target state or if Sec35 is locked.  Preserves the pre-existing
 * dirty bit (the crypto toggle must not queue a HotSync).  ENCCORE_SECTION. */
static void ApptRecrypt(DmOpenRef dbP, UInt16 index, Boolean toEnc) ENCCORE_SECTION;
static void ApptRecrypt(DmOpenRef dbP, UInt16 index, Boolean toEnc)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt32 uid = 0, prefix, outSize = 0;
	MemHandle h;
	UInt8 *rec, *out = NULL;
	ApptDBRecordFlags flags;
	UInt16 attr = 0;
	Boolean wasDirty;

	if (!EncGetKey(key, apptEncTtl))
		return;
	MemSet(key, ENC_KEY_BYTES, 0);					/* only needed to gate; re-fetched below */
	h = DmGetRecord(dbP, index);
	if (h == NULL) return;
	rec = (UInt8 *) MemHandleLock(h);
	flags = ApptFlagsOf(rec);
	if ((Boolean) flags.encrypted == toEnc) {		/* already in target state */
		MemHandleUnlock(h); DmReleaseRecord(dbP, index, false); return;
	}
	DmRecordInfo(dbP, index, &attr, &uid, NULL);
	wasDirty = (Boolean) ((attr & dmRecAttrDirty) != 0);
	prefix = ApptPrefixSize(rec);
	EncGetKey(key, apptEncTtl);

	if (toEnc) {									/* plaintext -> encrypted */
		const char *desc = flags.description ? (const char *)(rec + prefix) : "";
		UInt16 descLen = (UInt16) StrLen(desc);
		const char *note = flags.note ? (const char *)(rec + prefix + (flags.description ? descLen + 1 : 0)) : "";
		UInt16 noteLen = (UInt16) StrLen(note);
		outSize = prefix + 4 + descLen + noteLen;
		out = (UInt8 *) MemPtrNew(outSize);
		if (out != NULL) {
			MemMove(out, rec, prefix);
			((ApptDBRecordFlags *)(out + sizeof(ApptDateTimeType)))->encrypted = 1;
			apptWrU16(out + prefix, descLen);
			apptWrU16(out + prefix + 2, noteLen);
			MemMove(out + prefix + 4, desc, descLen);
			MemMove(out + prefix + 4 + descLen, note, noteLen);
			if (descLen) EncCrypt(key, out + prefix + 4, descLen, uid, 0);
			if (noteLen) EncCrypt(key, out + prefix + 4 + descLen, noteLen, uid, 1);
		}
	} else {										/* encrypted -> plaintext (inline) */
		UInt16 descLen = apptRdU16(rec + prefix);
		UInt16 noteLen = apptRdU16(rec + prefix + 2);
		outSize = prefix + (flags.description ? descLen + 1 : 0) + (flags.note ? noteLen + 1 : 0);
		out = (UInt8 *) MemPtrNew(outSize);
		if (out != NULL)
			(void) ApptBuildInline(rec, uid, out);	/* rebuilds inline (key present) */
	}
	MemSet(key, ENC_KEY_BYTES, 0);
	MemHandleUnlock(h);
	if (out != NULL) {
		h = DmResizeRecord(dbP, index, outSize);
		if (h != NULL) {
			rec = (UInt8 *) MemHandleLock(h);
			DmWrite(rec, 0, out, outSize);
			MemHandleUnlock(h);
		}
		MemPtrFree(out);
	}
	DmReleaseRecord(dbP, index, true);
	if (!wasDirty) {								/* crypto toggle must not queue a HotSync */
		DmRecordInfo(dbP, index, &attr, NULL, NULL);
		attr = (UInt16) (attr & ~dmRecAttrDirty);
		DmSetRecordInfo(dbP, index, &attr, NULL);
	}
}

/* Seal every still-plaintext record (call on close / first unlock). No-op locked. */
void ApptSealAllPlaintext(DmOpenRef dbP) ENCCORE_SECTION;
void ApptSealAllPlaintext(DmOpenRef dbP)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt16 i, n;
	if (!EncGetKey(key, apptEncTtl)) return;
	MemSet(key, ENC_KEY_BYTES, 0);
	n = DmNumRecords(dbP);
	for (i = 0; i < n; i++) {
		MemHandle hh = DmQueryRecord(dbP, i);
		if (hh != NULL) {
			Boolean enc = (Boolean) ApptFlagsOf((const UInt8 *) MemHandleLock(hh)).encrypted;
			MemHandleUnlock(hh);
			if (!enc) ApptRecrypt(dbP, i, true);
		}
	}
}

/* Seal ONE plaintext record (after creation). No-op if already encrypted / locked. */
void ApptEncryptOne(DmOpenRef dbP, UInt16 index) ENCCORE_SECTION;
void ApptEncryptOne(DmOpenRef dbP, UInt16 index) { ApptRecrypt(dbP, index, true); }

/* Read chokepoint decrypt buffer: ApptGetRecord decrypts an encrypted record into this
 * single reused handle (masked when locked) and hands it back as *handleP.  Foreground,
 * one-record-at-a-time (no nested live ApptGetRecord on encrypted records). */
static MemHandle gApptDecH = NULL;

/* Decrypt/mask the (already-locked) encrypted record bytes `src` into gApptDecH, unpack `r`
 * from it, and return gApptDecH (locked; caller unlocks).  Does NOT touch the storage handle
 * -- ApptGetRecord owns that lock (locking it here too caused a chunk over-lock leak per
 * read).  ENCCORE_SECTION. */
MemHandle ApptGetDecrypted(DmOpenRef dbP, UInt16 index, const UInt8 *src,
	ApptDBRecordPtr r) ENCCORE_SECTION;
MemHandle ApptGetDecrypted(DmOpenRef dbP, UInt16 index, const UInt8 *src, ApptDBRecordPtr r)
{
	UInt32 prefix = ApptPrefixSize(src);
	UInt16 descLen = apptRdU16(src + prefix);
	UInt16 noteLen = apptRdU16(src + prefix + 2);
	UInt32 need = prefix + descLen + 1 + noteLen + 1;	/* worst case (both present) */
	UInt32 uid = 0;
	UInt8 *out;

	DmRecordInfo(dbP, index, NULL, &uid, NULL);
	if (gApptDecH == NULL)
		gApptDecH = MemHandleNew(need);
	else if (MemHandleSize(gApptDecH) < need) {
		/* MemHandleResize resizes in place and returns an Err (not a new
		 * handle) -- gApptDecH's identity doesn't change on success. */
		Err err = MemHandleResize(gApptDecH, need);	/* handle is unlocked between reads */
		if (err != errNone) return NULL;
	}
	if (gApptDecH == NULL) return NULL;
	out = (UInt8 *) MemHandleLock(gApptDecH);
	ApptBuildInline(src, uid, out);
	ApptUnpack((ApptPackedDBRecordPtr) out, r);
	return gApptDecH;								/* stays locked; caller unlocks */
}

/************************************************************
 *
 *  FUNCTION: ApptFindSortPosition
 *
 *  DESCRIPTION: Return where a record is or should be
 *		Useful to find or find where to insert a record.
 *
 *  PARAMETERS: database record
 *
 *  RETURNS: position where a record should be
 *
 *  CREATED: 1/25/95 
 *
 *  BY: Roger Flores
 *
 *************************************************************/
static UInt16 ApptFindSortPosition(DmOpenRef dbP, ApptPackedDBRecordPtr newRecord)
{
	return (DmFindSortPosition (dbP, newRecord, NULL, (DmComparF *)ApptComparePackedRecords, 0));
}


/************************************************************
 *
 *  FUNCTION: ApptSort
 *
 *  DESCRIPTION: Sort the appointment database.
 *
 *  PARAMETERS: database record
 *
 *  RETURNS: nothing
 *
 *  CREATED: 9/5/95 
 *
 *  BY: Art Lamb
 *
 *************************************************************/
void ApptSort (DmOpenRef dbP)
{
	DmQuickSort(dbP, (DmComparF *)ApptComparePackedRecords, 0);
}


/***********************************************************************
 *
 * FUNCTION:    ApptFindFirst
 *
 * DESCRIPTION: This routine finds the first appointment on the specified
 *              day.
 *
 * PARAMETERS:  dbP    - pointer to the database
 *              date   - date to search for
 *              indexP - pointer to the index of the first record on the 
 *                       specified day (returned value)
 *
 * RETURNED:    true if a record has found
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/15/95		Initial Revision
 *
 ***********************************************************************/
Boolean ApptFindFirst (DmOpenRef dbP, DateType date, UInt16* indexP)
{
	Err err;
	Int16 numOfRecords;
	Int16 kmin, probe, i;		// all positions in the database.
	Int16 result = 0;			// result of comparing two records
	UInt16 index;
	MemHandle recordH;
	Boolean found = false;
	ApptPackedDBRecordPtr r;


	kmin = probe = 0;
	numOfRecords = DmNumRecords(dbP);
	
	
	while (numOfRecords > 0)
		{
		i = numOfRecords >> 1;
		probe = kmin + i;
		
		index = probe;
		recordH = DmQueryNextInCategory (dbP, &index, dmAllCategories);
		if (recordH)
			{
			r = (ApptPackedDBRecordPtr) MemHandleLock (recordH);
			if (r->flags.repeat)
				result = 1;
			else
				result = DateCompare (date, r->when.date);
			MemHandleUnlock (recordH);
			}

		// If no handle, assume the record is deleted, deleted records
		// are greater.
		else
			result = -1;
			

		// If the date passed is less than the probe's date, keep searching.
		if (result < 0)
			numOfRecords = i;

		// If the date passed is greater than the probe's date, keep searching.
		else if (result > 0)
			{
			kmin = probe + 1;
			numOfRecords = numOfRecords - i - 1;
			}

		// If the records are equal find the first record on the day.
		else
			{
			found = true;
			*indexP = index;
			while (true)
				{
				err = DmSeekRecordInCategory (dbP, &index, 1, dmSeekBackward, 
					dmAllCategories);
				if (err == dmErrSeekFailed) break;
				
				recordH = DmQueryRecord(dbP, index);
				r = (ApptPackedDBRecordPtr) MemHandleLock (recordH);
				if (r->flags.repeat)
					result = 1;
				else
					result = DateCompare (date, r->when.date);
				MemHandleUnlock (recordH);
				if (result != 0) break;
				*indexP = index;
				}

			break;
			}
		}

	
	// If that were no appointments on the specified day, return the 
	// index of the next appointment (on a future day).
	if (! found)
		{
		if (result < 0) 
			*indexP = probe;
		else if (DmNumRecords(dbP) == 0)
			*indexP = 0;
		else
			*indexP = probe + 1;
		}

	return (found);
}



/***********************************************************************
 *
 * FUNCTION:    ApptAddException
 *
 * DESCRIPTION: This routine adds an entry to the exceptions list of the 
 *              specified record.
 *
 *  PARAMETERS: database pointer
 *					 database index
 *					 exception date
 *
 * RETURNED:	 error code 
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/26/95	Initial Revision
 *
 ***********************************************************************/
SEC002 Err ApptAddException (DmOpenRef dbP, UInt16 *index, DateType date);	/* Db35: freed from main */
Err ApptAddException (DmOpenRef dbP, UInt16 *index, DateType date)
{
	Err error;
	UInt16 size;
	MemHandle recordH;
	ApptDBRecordType r;
	ExceptionsListPtr exceptions;
	ApptDBRecordFlags changedFields;
	
	error = ApptGetRecord(dbP, *index, &r, &recordH);
	if (error) return error;
	
	// If the record already has an expections list, add an entry to 
	// the list.
	if (r.exceptions)
		{
		size = sizeof (ExceptionsListType) + 
			(sizeof (DateType) * r.exceptions->numExceptions);
		exceptions = MemPtrNew (size);
		ErrFatalDisplayIf ((!exceptions), "Out of memory");
		
		MemMove (exceptions, r.exceptions, size - sizeof (DateType));
		exceptions->numExceptions++;
		*(&exceptions->exception + r.exceptions->numExceptions) = date;
		}

	// Create an expections list.
	else
		{
		size = sizeof (ExceptionsListType);
		exceptions = MemPtrNew (size);
		ErrFatalDisplayIf ((!exceptions), "Out of memory");

		exceptions->numExceptions = 1;
		exceptions->exception = date;
		}
	
	MemHandleUnlock (recordH);
	
	// Update the record
	r.exceptions = exceptions;
	MemSet (&changedFields, sizeof (changedFields), 0);
	changedFields.exceptions = true;
	error = ApptChangeRecord (dbP, index, &r, changedFields);
	
	MemPtrFree (exceptions);

	return (error);
}


/***********************************************************************
 *
 * FUNCTION:    ApptRepeatsOnDate
 *
 * DESCRIPTION: This routine returns true if a repeating appointment
 *              occurrs on the specified date.
 *
 * PARAMETERS:  apptRec - a pointer to an appointment record
 *              date    - date to check              
 *
 * RETURNED:    true if the appointment occurs on the date specified
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/14/95		Initial Revision
 *
 ***********************************************************************/
Boolean ApptRepeatsOnDate (ApptDBRecordPtr apptRec, DateType date)
{
	Int16  i;
	UInt16 freq;
	UInt16 weeksDiff;
	UInt16 dayInMonth;
	UInt16 dayOfWeek;
	UInt16 dayOfMonth;
	UInt16 firstDayOfWeek;
	Int32 dateInDays;
	Int32 startInDays;
	Boolean onDate;
	DatePtr exceptions;
	DateType startDate;

	// Is the date passed before the start date of the appointment?
	if (DateCompare (date, apptRec->when->date) < 0)
		return (false);

	// Is the date passed after the end date of the appointment?
	if (DateCompare (date, apptRec->repeat->repeatEndDate) > 0)
		return (false);
	

	// Get the frequency of occurrecne (ex: every 2nd day, every 3rd month, etc.).  
	freq = apptRec->repeat->repeatFrequency;
	
	// Get the date of the first occurrecne of the appointment.
	startDate = apptRec->when->date;

	switch (apptRec->repeat->repeatType)
		{
		// Daily repeating appointment.
		case repeatDaily:
			dateInDays = DateToDays (date);
			startInDays = DateToDays (startDate);
			onDate = ((dateInDays - startInDays) % freq) == 0;
			break;
			

		// Weekly repeating appointment (ex: every Monday and Friday). 
		// Yes, weekly repeating appointment can occur more then once a
		// week.
		case repeatWeekly:
			// Are we on a day of the week that the appointment repeats on.
			dayOfWeek = DayOfWeek (date.month, date.day, date.year+firstYear);
			onDate = ((1 << dayOfWeek) & apptRec->repeat->repeatOn);
			if (! onDate) break;

			// Are we in a week in which the appointment occurrs, if not 
			// move to that start of the next week in which the appointment
			// does occur.
			dateInDays = DateToDays (date);
			startInDays = DateToDays (startDate);

			firstDayOfWeek = (DayOfWeek (1, 1, firstYear) - 
				apptRec->repeat->repeatStartOfWeek + daysInWeek) % daysInWeek;

			weeksDiff = (((dateInDays + firstDayOfWeek) / daysInWeek) - 
							 ((startInDays + firstDayOfWeek) / daysInWeek)) %freq;
			onDate = (weeksDiff == 0);
			break;


//			// Compute the first occurrence of the appointment that occurs
//			// on the same day of the week as the date passed.
//			startDayOfWeek = DayOfWeek (startDate.month, startDate.day, 
//				startDate.year+firstYear);
//			startInDays = DateToDays (startDate);
//			if (startDayOfWeek < dayOfWeek)
//				startInDays += dayOfWeek - startDayOfWeek;
//			else if (startDayOfWeek > dayOfWeek)
//				startInDays += dayOfWeek+ (daysInWeek *freq) - startDayOfWeek;
//			
//			// Are we in a week in which the appointment repeats.
//			dateInDays = DateToDays (date);
//			onDate = (((dateInDays - startInDays) / daysInWeek) % freq) == 0;
//			break;


		// Monthly-by-day repeating appointment (ex: the 3rd Friday of every
		// month).
		case repeatMonthlyByDay:
			// Are we in a month in which the appointment repeats.
			onDate = ((((date.year - startDate.year) * monthsInYear) + 
						   (date.month - startDate.month)) % freq) == 0;
			if (! onDate) break;

			// Do the days of the month match (ex: 3rd Friday)
			dayOfMonth = DayOfMonth (date.month, date.day, date.year+firstYear);
			onDate = (dayOfMonth == apptRec->repeat->repeatOn);
			if (onDate) break;
			
			// If the appointment repeats on one of the last days of the month,
			// check if the date passed is also one of the last days of the 
			// month.  By last days of the month we mean: last sunday, 
			// last monday, etc.
			if ((apptRec->repeat->repeatOn >= domLastSun) &&
				 (dayOfMonth >= dom4thSun))
				{
				dayOfWeek = DayOfWeek (date.month, date.day, date.year+firstYear);
				dayInMonth = DaysInMonth (date.month, date.year+firstYear);
				onDate = (((date.day + daysInWeek) > dayInMonth) &&
							 (dayOfWeek == (apptRec->repeat->repeatOn % daysInWeek)));
				}
			break;						


		// Monthly-by-date repeating appointment (ex: the 15th of every
		// month).
		case repeatMonthlyByDate:
			// Are we in a month in which the appointment repeats.
			onDate = ((((date.year - startDate.year) * monthsInYear) + 
						   (date.month - startDate.month)) % freq) == 0;
			if (! onDate) break;
			
			// Are we on the same day of the month as the start date.
			onDate = (date.day == startDate.day);
			if (onDate) break;

			// If the staring day of the appointment is greater then the 
			// number of day in the month passed, and the day passed is the 
			// last day of the month, then the appointment repeats on the day.
			dayInMonth = DaysInMonth (date.month, date.year+firstYear);
			onDate = ((startDate.day > dayInMonth) && (date.day == dayInMonth));
			break;


		// Yearly repeating appointment.
		case repeatYearly:
			// Are we in a year in which the appointment repeats.
			onDate = ((date.year - startDate.year) % freq) == 0;
			if (! onDate) break;
			
			// Are we on the month and day that the appointment repeats.
			onDate = (date.month == startDate.month) &&
				      (date.day == startDate.day);
			if (onDate) break;
			
			// Specal leap day processing.
			if ( (startDate.month == february) && 
				  (startDate.day == 29) &&
				  (date.month == february) && 
				  (date.day == DaysInMonth (date.month, date.year+firstYear)))
				{
				onDate = true;
				}				      
			break;
		}

	// Check for an exception.
	if ((onDate) && (apptRec->exceptions))
		{
		exceptions = &apptRec->exceptions->exception;
		for (i = 0; i < apptRec->exceptions->numExceptions; i++)
			{
			if (DateCompare (date, exceptions[i]) == 0)
				{
				onDate = false;
				break;
				}
			}
		}


	return (onDate);
}



/***********************************************************************
 *
 * FUNCTION:    ApptNextRepeat
 *
 * DESCRIPTION: This routine computes the date of the next 
 *              occurrence of a repeating appointment.
 *
 * PARAMETERS:  apptRec - a pointer to an appointment record
 *              date    - passed:   date to start from
 *                        returned: date of next occurrence             
 *
 * RETURNED:    true if the appointment occurs again
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/14/95	Initial Revision
 *
 ***********************************************************************/
static Boolean NextRepeat (ApptDBRecordPtr apptRec, DatePtr dateP)
{
	Int16  i;
	UInt16 day;
	UInt16 freq;
	UInt16 year;
	UInt16 adjust;
	UInt16 weeksDiff;
	UInt16 monthsDiff;
	UInt16 daysInMonth;
	UInt16 dayOfWeek;
	UInt16 apptWeekDay;
	UInt16 firstDayOfWeek;
	UInt16 daysTilNext;
	UInt16 monthsTilNext;
	UInt32 dateInDays;
	UInt32 startInDays;
	DateType start;
	DateType date;
	DateType next;

	date = *dateP;

	// Is the date passed after the end date of the appointment?
	if (DateCompare (date, apptRec->repeat->repeatEndDate) > 0)
		return (false);
	
	// Is the date passed before the start date of the appointment?
	if (DateCompare (date, apptRec->when->date) < 0)
		date = apptRec->when->date;

	// Get the frequency on occurrecne (ex: every 2nd day, every 3rd month, etc).  
	freq = apptRec->repeat->repeatFrequency;
	
	// Get the date of the first occurrecne of the appointment.
	start = apptRec->when->date;
	

	switch (apptRec->repeat->repeatType)
		{
		// Daily repeating appointment.
		case repeatDaily:
			dateInDays = DateToDays (date);
			startInDays = DateToDays (start);
			daysTilNext = (dateInDays - startInDays + freq - 1) / freq * freq;
			if (startInDays + daysTilNext > (UInt32) maxDays)
				return (false);
			DateDaysToDate (startInDays + daysTilNext, &next);
			break;
			


		// Weekly repeating appointment (ex: every Monday and Friday). 
		// Yes, weekly repeating appointment can occur more then once a
		// week.
		case repeatWeekly:
			dateInDays = DateToDays (date);
			startInDays = DateToDays (start);

			firstDayOfWeek = (DayOfWeek (1, 1, firstYear) - 
				apptRec->repeat->repeatStartOfWeek + daysInWeek) % daysInWeek;

			dayOfWeek = DayOfWeek (date.month, date.day, date.year+firstYear);
			apptWeekDay = (dayOfWeek - apptRec->repeat->repeatStartOfWeek +
				daysInWeek) % daysInWeek;

			// Are we in a week in which the appointment occurrs, if not 
			// move to that start of the next week in which the appointment
			// does occur.
			weeksDiff = (((dateInDays + firstDayOfWeek) / daysInWeek) - 
							 ((startInDays + firstDayOfWeek) / daysInWeek)) %freq;
			if (weeksDiff)
				{
				adjust = ((freq - weeksDiff) * daysInWeek)- apptWeekDay;
				apptWeekDay = 0;
				dayOfWeek = (dayOfWeek + adjust) % daysInWeek;
				}
			else
				adjust = 0;
			
			// Find the next day on which the appointment repeats.
			for (i = 0; i < daysInWeek; i++)
				{
				if (apptRec->repeat->repeatOn & (1 << dayOfWeek)) break;
				adjust++;
				if (++dayOfWeek == daysInWeek)
					dayOfWeek = 0;
				if (++apptWeekDay == daysInWeek)
					adjust += (freq - 1) * daysInWeek;
				}

			if (dateInDays + adjust > (UInt32) maxDays)
				return (false);
			DateDaysToDate (dateInDays + adjust, &next);
//			next = date;
//			DateAdjust (&next, adjust);
			break;



		// Monthly-by-day repeating appointment (ex: the 3rd Friday of every
		// month).
		case repeatMonthlyByDay:
			// Compute the number of month until the appointment repeats again.
			monthsTilNext = (date.month - start.month);

			monthsTilNext = ((((date.year - start.year) * monthsInYear) + 
						          (date.month - start.month)) + freq - 1) /
						       freq * freq;

			while (true)
				{
				year = start.year + 
								 (start.month - 1 + monthsTilNext) / monthsInYear;
				if (year + firstYear > 9999)		/* Db35 era: was >= numberOfYears (2031) */
					return (false);

				next.year = year;
				next.month = (start.month - 1 + monthsTilNext) % monthsInYear + 1;
	
				dayOfWeek = DayOfWeek (next.month, 1, next.year+firstYear);
				if ((apptRec->repeat->repeatOn % daysInWeek) >= dayOfWeek)
					day = apptRec->repeat->repeatOn - dayOfWeek + 1;
				else
					day = apptRec->repeat->repeatOn + daysInWeek - dayOfWeek + 1;
	
				// If repeat-on day is between the last sunday and the last
				// saturday, make sure we're not passed the end of the month.
				if ( (apptRec->repeat->repeatOn >= domLastSun) &&
					  (day > DaysInMonth (next.month, next.year+firstYear)))
					{
					day -= daysInWeek;
					}
				next.day = day;

				// Its posible that "next date" calculated above is 
				// before the date passed.  If so, move forward
				// by the length of the repeat freguency and preform
				// the calculation again.
				if ( DateToInt(date) > DateToInt (next))
					monthsTilNext += freq;
				else
					break;
				}
			break;						



		// Monthly-by-date repeating appointment (ex: the 15th of every
		// month).
		case repeatMonthlyByDate:
			// Compute the number of month until the appointment repeats again.
			monthsDiff = ((date.year - start.year) * monthsInYear) + 
				(date.month - start.month);
			monthsTilNext = (monthsDiff + freq - 1) / freq * freq;

			if ((date.day > start.day) && (!(monthsDiff % freq)))
				monthsTilNext += freq;
				
			year = start.year + 
							 (start.month - 1 + monthsTilNext) / monthsInYear;
			if (year + firstYear > 9999)		/* Db35 era: was >= numberOfYears (2031) */
				return (false);

			next.year = year;
			next.month = (start.month - 1 + monthsTilNext) % monthsInYear + 1;
			next.day = start.day;

			// Make sure we're not passed the last day of the month.
			daysInMonth = DaysInMonth (next.month, next.year+firstYear);
			if (next.day > daysInMonth)
				next.day = daysInMonth;
			break;



		// Yearly repeating appointment.
		case repeatYearly:
			next.day = start.day;
			next.month = start.month;

			year = start.year + 
				((date.year - start.year + freq - 1) / freq * freq);
			
			if ((date.month > start.month) ||
				((date.month == start.month) && (date.day > start.day)))
				 year += freq;

			// Specal leap day processing.
			if ( (next.month == february) && (next.day == 29) &&
				  (next.day > DaysInMonth (next.month, year+firstYear)))
				{
				next.day = DaysInMonth (next.month, year+firstYear);
				}				      
			if (year + firstYear > 9999)		/* Db35 era: was >= numberOfYears (2031) */
				return (false);

			next.year = year;	
			break;
		}

	// Is the next occurrence after the end date of the appointment?
	if (DateCompare (next, apptRec->repeat->repeatEndDate) > 0)
		return (false);

	ErrFatalDisplayIf ((DateToInt (next) < DateToInt (*dateP)),
		"Calculation error");

	*dateP = next;
	return (true);
}


/***********************************************************************
 *
 * FUNCTION:    IsException
 *
 * DESCRIPTION: This routine returns true the date passed is in a 
 *              repeating appointment's exception list.
 *
 * PARAMETERS:  apptRec - a pointer to an appointment record
 *              date    - date to check              
 *
 * RETURNED:    true if the date is an exception date.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/14/95		Initial Revision
 *
 ***********************************************************************/
static Boolean IsException (ApptDBRecordPtr apptRec, DateType date)
{
	int i;
	DatePtr exceptions;

	if (apptRec->exceptions)
		{
		exceptions = &apptRec->exceptions->exception;
		for (i = 0; i < apptRec->exceptions->numExceptions; i++)
			{
			if (DateCompare (date, exceptions[i]) == 0)
			return (true);
			}
		}
	return (false);
}


/***********************************************************************
 *
 * FUNCTION:    ApptNextRepeat
 *
 * DESCRIPTION: This routine computes the next occurrence of a 
 *              repeating appointment.
 *
 * PARAMETERS:  apptRec - a pointer to an appointment record
 *              dateP   - passed:   date to start from
 *                        returned: date of next occurrence            
 *
 * RETURNED:    true if there is an occurrence of the appointment
 *              between the date passed and the appointment's end date
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/20/95	Initial Revision
 *
 ***********************************************************************/
Boolean ApptNextRepeat (ApptDBRecordPtr apptRec, DatePtr dateP)
{
	DateType date;
	
	date = *dateP;
	
	while (true)
		{
		// Compute the next time the appointment repeats.
		if (! NextRepeat (apptRec, &date))
			return (false);

		// Check if the date computed is in the exceptions list.
		if (! IsException (apptRec, date))
			{
			*dateP = date;
			return (true);
			}
			
		DateAdjust (&date, 1);
		}		
}


/***********************************************************************
 *
 * FUNCTION:    UnDayOfMonth
 *
 * DESCRIPTION: Inverse of DayOfMonth routine.  Takes a month and year
 *		and a dayOfMonth value (e.g., dom1stSun, domLastFri, etc.) and computes
 *		what date that day is for that month.
 *
 * PARAMETERS:	month
 *					year - a year (1904, etc.)
 *					dayOfMonth - a dayOfMonth, like those returned from DayOfMonth
 *
 * RETURNED:    date in month
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			grant	3/1/99	Initial Revision
 *
 ***********************************************************************/
static UInt16 UnDayOfMonth(UInt16 month, UInt16 year, UInt16 dayOfMonth)
{
	Int16 dayOfWeek;
	Int16 firstDayOfWeek;
	Int16 week;
	Int16 day;

	dayOfWeek = dayOfMonth % daysInWeek;
	week = dayOfMonth / daysInWeek;
	
	firstDayOfWeek = DayOfWeek(month, 1, year);
	day = (dayOfWeek - firstDayOfWeek + daysInWeek) % daysInWeek + 1 + week * daysInWeek;
	
	// adjust for last-fooday in months with only 4 foodays
	while (day > DaysInMonth(month, year))
		day -= daysInWeek;
	
	return day;
}


/***********************************************************************
 *
 * FUNCTION:    CountTotalRepeats
 *
 * DESCRIPTION: Counts the total number of times a repeating event occurs.
 * 	Returns apptNoEndDate (-1) if the event has no end date (repeats forever).
 *		The default value returned is 1, since an appointment that repeats on 0
 * 	days is not allowed.
 *
 * PARAMETERS:	apptRecP - pointer to an appointment record
 *
 * RETURNED:	number of times the appointment repeats
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			grant	3/1/99	Initial Revision
 *
 ***********************************************************************/
static Int32 CountTotalRepeats(ApptDBRecordPtr apptRecP)
{
	DateType start;
	DateType end;
	UInt16 daysTotal;
	UInt16 freq;
	UInt16 weeks;
	UInt16 months;
	UInt16 years;
	UInt16 dayOfWeek;
	UInt16 startDOW;
	UInt16 endDOW;
	UInt16 daycount;
	UInt16 repeatOnDay;
	UInt32 startInDays;
	UInt32 endInDays;
	UInt32 firstSunday;
	UInt32 lastSaturday;

	ErrFatalDisplayIf(apptRecP == NULL, "no appointment");
	ErrFatalDisplayIf(apptRecP->repeat == NULL, "appointment does not repeat");
	ErrFatalDisplayIf(apptRecP->when == NULL, "appointment has no date");
	ErrFatalDisplayIf(apptRecP->repeat->repeatFrequency == 0, "zero repeat frequency");

	// Get the frequency of occurrence (ex: every 2nd day, every 3rd month, etc).  
	freq = apptRecP->repeat->repeatFrequency;
	
	// Get the date of the first occurrence of the appointment.
	start = apptRecP->when->date;
	
	// Get the date of the last occurrence of the appointment.
	end = apptRecP->repeat->repeatEndDate;

	// Does the appointment repeat forever?
	if (DateToInt(end) == apptNoEndDate)
		return (apptNoEndDate);
	
	// if the end date is somehow before the start date, just return 1
	if (DateCompare(end, start) < 0)
		return 1;
	
	daysTotal = 0;
	switch (apptRecP->repeat->repeatType)
		{
		// Daily repeating appointment.
		case repeatDaily:
			startInDays = DateToDays(start);
			endInDays = DateToDays(end);
			daysTotal = ((endInDays - startInDays) / freq) + 1;
			break;
			
		// Weekly repeating appointment (ex: every Monday and Friday).
		// The strategy is to break the time period into 3 fragments that
		// are easily dealt with - days before the first sunday, whole weeks
		// from the first sunday to the last saturday, and days after the
		// last saturday.
		// Yes, weekly repeating appointment can occur more then once a
		// week.
		case repeatWeekly:
			startInDays = DateToDays(start);
			endInDays = DateToDays(end);
			startDOW = DayOfWeek(start.month, start.day, start.year + firstYear);
			endDOW = DayOfWeek(end.month, end.day, end.year + firstYear);
			
			// find firstSunday and lastSaturday
			if (startDOW != sunday)
				{
				firstSunday = startInDays - startDOW + daysInWeek;
				}
			else
				{
				firstSunday = startInDays;
				}
			if (endDOW != saturday)
				{
				lastSaturday = endInDays - endDOW - 1;
				}
			else
				{
				lastSaturday = endInDays;
				}
			
			// compute number of full sunday-to-saturday weeks
			weeks = (lastSaturday - firstSunday + 1) / daysInWeek;
			
			// count number of times appt repeats in a full week
			daycount = 0;
			for (dayOfWeek = sunday; dayOfWeek < daysInWeek; dayOfWeek++)
				{
				//if repeat on dayOfWeek, daycount++
				if (RepeatOnDOW(apptRecP->repeat, dayOfWeek))
					daycount++;
				}
			
			// Now we are ready to total the repetitions.
			daysTotal = 0;
			// fragment 1 - before firstSunday
			if (startDOW != sunday)
				{
				for (dayOfWeek = startDOW; dayOfWeek < daysInWeek; dayOfWeek++)
					{
					// if repeat on dayOfWeek, daysTotal++
					if (RepeatOnDOW(apptRecP->repeat, dayOfWeek))
						daysTotal++;
					}
				}
			
			// fragment 2 - full weeks from firstSunday to lastSaturday
			daysTotal += (daycount * (weeks / freq));
			
			// fragment 3 - after lastSaturday
			if (endDOW != saturday)
				{
				for (dayOfWeek = sunday; dayOfWeek <= endDOW; dayOfWeek++)
					{
					// if repeat of dayOfWeek, daysTotal++
					if (RepeatOnDOW(apptRecP->repeat, dayOfWeek))
						daysTotal++;
					}
				}
			break;


		// Monthly-by-day repeating appointment
		case repeatMonthlyByDay:
			// Compute the number of months
			months = ((end.year - start.year) * monthsInYear) + (end.month - start.month);
			
			// if the end day is too early in the last month, don't include that month
			repeatOnDay = UnDayOfMonth(end.month, end.year + firstYear,
												apptRecP->repeat->repeatOn);
			if (end.day < repeatOnDay)
				months--;
			
			daysTotal = months / freq + 1;	// repeats once every freq months
			break;
		
		
		// Monthly-by-date repeating appointment
		case repeatMonthlyByDate:
			// Compute the number of months
			months = ((end.year - start.year) * monthsInYear) + (end.month - start.month);
			
			// if the end day is too early in the last month, don't include that month
			if (end.day < start.day)
				months--;
			
			daysTotal = months / freq + 1;	// repeats once every freq months
			break;
		
		
		// Yearly repeating appointment.
		case repeatYearly:
			years = end.year - start.year;
			
			// if the end day is too early in the last year, don't include that year
			if (end.month < start.month
					|| (end.month == start.month && end.day < start.day))
				years--;
			
			daysTotal = years / freq + 1;		// repeats once every freq years
			break;
		
		default:
			daysTotal = 1;
			break;
		}
	
	ErrNonFatalDisplayIf(daysTotal == 0, "event repeats on 0 days");
	ErrNonFatalDisplayIf(daysTotal < 0, "event repeats on negative days");
	if (daysTotal <= 0)  daysTotal = 1;
	
	return (daysTotal);
}


/***********************************************************************
 *
 * FUNCTION:    ApptHasMultipleOccurences
 *
 * DESCRIPTION: Does the given appointment occur more than once?
 *
 *		This function compares the repeat info and the exception list for
 *		an appointment to determine if it has more than one visible (non-excepted)
 *		occurence.
 *		The decision is based solely on the number of times the appointment
 *		repeats versus the number of exceptions.
 *
 * PARAMETERS:  ApptDBRecordPtr apptRecP - the appointment to examine
 *
 * RETURNED:    true if the appointment occurs more than once
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			grant	3/2/99	Initial Revision
 *
 ***********************************************************************/
Boolean ApptHasMultipleOccurences(ApptDBRecordPtr apptRecP)
{
	Int32 totalRepeats;
	Int32 numExceptions;
	
	ErrFatalDisplayIf(apptRecP == NULL, "no appointment");

	// if the appointment does not repeat, then it can't occur more than once
	if (!apptRecP->repeat)  return false;
	
	totalRepeats = CountTotalRepeats(apptRecP);
	if (apptRecP->exceptions)
		numExceptions = apptRecP->exceptions->numExceptions;
	else
		numExceptions = 0;
	
	if (totalRepeats == apptNoEndDate)  return true;
	if ((totalRepeats - numExceptions) > 1)  return true;
	
	return false;
}


/***********************************************************************
 *
 * FUNCTION:    ApptListCompare
 *
 * DESCRIPTION: This routine compares two entries in the appointment list, 
 *              it's called by ApptGetAppointments via the quick sort 
 *              routine.
 *
 * PARAMETERS:  a     - a pointer to an entry in the appointment list
 *					 b     - a pointer to an entry in the appointment list
 *              extra - extra data passed to quick sort - not used
 *
 * RETURNED:    if a1 > a2  returns a positive int
 *              if a1 < a2  returns a negative int
 *              if a1 = a2  returns zero
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/15/95		Initial Revision
 *
 ***********************************************************************/
static Int16 ApptListCompare (ApptInfoPtr a1, ApptInfoPtr  a2, Int32 extra)
{
#pragma unused (extra)

	Int16 result;
	
	result = TimeCompare (a1->startTime, a2->startTime);
	if (result == 0)
		{
		result = TimeCompare (a1->endTime, a2->endTime);
		}
	return result;
}


/***********************************************************************
 *
 * FUNCTION:    ApptGetAppointments
 *
 * DESCRIPTION: This routine returns a list of appointments that are on 
 *              the date specified
 *
 * PARAMETERS:  dbP    - pointer to the database
 *              date   - date to search for
 *              countP - number of appointments on the specified 
 *                       day (returned value)
 *
 * RETURNED:    handle of the appointment list (ApptInfoType)
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/15/95		Initial Revision
 *
 ***********************************************************************/
#if 0
VoidHand ApptGetAppointments (DmOpenRef dbP, DateType date, UInt16* countP)
{
	Err	error;
	Int16	result;
	Int16	count = 0;
	UInt16	recordNum;
	Boolean repeats;
	MemHandle recordH;
	MemHandle apptListH;
	ApptInfoPtr apptList;
	ApptDBRecordType apptRec;
	ApptPackedDBRecordPtr r;

	// Allocated a block to hold the appointment list.
	apptListH = MemHandleNew (sizeof (ApptInfoType) * apptMaxPerDay);
	ErrFatalDisplayIf(!apptListH, "Out of memory");
	if (! apptListH) return (0);

	apptList = MemHandleLock (apptListH);
	

	// Find the first non-repeating appointment of the day.
	if (ApptFindFirst (dbP, date, &recordNum))
		{
		while (count < apptMaxPerDay)
			{
			// Check if the appointment is on the date passed, if it is 
			// add it to the appointment list.		
			recordH = DmQueryRecord (dbP, recordNum);
			r = MemHandleLock (recordH);
			result = DateCompare (r->when.date, date);

			if (result == 0)
				{
				// Add the record to the appoitment list.
				apptList[count].startTime = r->when.startTime;				
				apptList[count].endTime = r->when.endTime;				
				apptList[count].recordNum = recordNum;	
				count++;
				}
			MemHandleUnlock (recordH);
			if (result != 0) break;

			// Get the next record.
			error = DmSeekRecordInCategory (dbP, &recordNum, 1, dmSeekForward, dmAllCategories);
			if (error == dmErrSeekFailed) break;
			}
		}


	// Add the repeating appointments to the list.  Repeating appointments
	// are stored at the beginning of the database.
	recordNum = 0;
	while (count < apptMaxPerDay)
		{
		recordH = DmQueryNextInCategory (dbP, &recordNum, dmAllCategories);
		if (! recordH) break;
		
		r = (ApptPackedDBRecordPtr) MemHandleLock (recordH);
		
		repeats = (r->flags.repeat != 0);
		if (repeats)
			{
			ApptUnpack (r, &apptRec);
			if (ApptRepeatsOnDate (&apptRec, date))
				{
				// Add the record to the appoitment list.
				apptList[count].startTime = r->when.startTime;				
				apptList[count].endTime = r->when.endTime;				
				apptList[count].recordNum = recordNum;	
				count++;
				}
			}
		MemHandleUnlock (recordH);

		// If the record has no repeating info we've reached the end of the 
		// repeating appointments.
		if (! repeats) break;
		
		 recordNum++;
		}

	
	// Sort the list by start time.
	SysInsertionSort (apptList, count, sizeof (ApptInfoType), ApptListCompare, 0L);
	

	// If there are no appointments on the specified day, free the appointment
	// list.
	if (count == 0)
		{
		MemPtrFree (apptList);
		apptListH = 0;
		}

	// Resize the appointment list block to release any unused space.
	else
		{
		MemHandleUnlock (apptListH);
		MemHandleResize (apptListH, count * sizeof (ApptInfoType));
		}

	*countP = count;
	return (apptListH);
}
#endif



/***********************************************************************
 *
 * FUNCTION:    AddAppointmentToList
 *
 * DESCRIPTION: This routine adds an appointment to a list of appointments. 
 *
 * PARAMETERS:  apptListH - pointer to list to add appointment to
 *					 count     - number of appointments on the specified day
 *					 startTime - start time of appointment
 *					 endTime   - end time of appointment
 *					 recordNum - index of associated record
 *
 * RETURNED:    true if successful, false if error
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	4/17/96	Initial Revision
 *			rbb	4/27/99	Patched error detection when allocating apptListH
 *
 ***********************************************************************/
static Boolean AddAppointmentToList (MemHandle * apptListH, UInt16 count,
	TimeType startTime, TimeType endTime, UInt16 recordNum)
{
	Err err;
	UInt16 newSize;
	ApptInfoPtr apptList;

	if (count == 0)
		{
		// Allocated a block to hold the appointment list.
		*apptListH = MemHandleNew (sizeof (ApptInfoType) * (apptMaxPerDay / 10));
		ErrFatalDisplayIf(!*apptListH, "Out of memory");
		if (! *apptListH) return (false);
		apptList = MemHandleLock (*apptListH);
		}
		
	// Resize the list to hold more more appointments.
	else if ((count % (apptMaxPerDay / 10)) == 0)
		{
		if (count + (apptMaxPerDay / 10) > apptMaxPerDay)
			return (false);
		
		newSize = sizeof (ApptInfoType) * (count + (apptMaxPerDay / 10));
		err = MemHandleResize (*apptListH, newSize);
		apptList = MemHandleLock (*apptListH);

		ErrFatalDisplayIf(err, "Out of memory");
		if (err) return (false);
		}

	else
		apptList = MemHandleLock(*apptListH);

	apptList[count].startTime = startTime;				
	apptList[count].endTime = endTime;						
	apptList[count].recordNum = recordNum;	

	MemHandleUnlock(*apptListH);

	return (true);
}


/***********************************************************************
 *
 * FUNCTION:    ApptGetAppointments
 *
 * DESCRIPTION: This routine returns a list of appointments that are in 
 *              the range of dates specified
 *
 * PARAMETERS:  dbP       - pointer to the database
 *              date   	  - start date to search from
 *              days   	  - number a days in search range
 *              apptLists - returned: array of handle of the 
 *									 appointment list (ApptInfoType)
 *              counts    - returned: returned: array of counts of the 
 *                          number of appointments in each list.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	4/7/96	Initial Revision
 *
 ***********************************************************************/
#define kSpanMaxDays	30		/* matches DateSpan.c's own cap on End's date picker */

/* Db35: place a (possibly multi-day) appointment/occurrence into every
 * day-slot of [0,days-1] it overlaps, given its own start day (occDate)
 * and day-span (span, 0=same day). Per-day times are clamped: the day it
 * really starts on shows the real start time; the day it really ends on
 * shows the real end time; any day strictly between shows the whole day
 * (00:00-23:59). anchorDate is day-slot 0's calendar date (the caller's
 * own `date` parameter) -- naturally a no-op (zero iterations) if the
 * span doesn't overlap [0,days-1] at all, so callers don't need to
 * pre-filter. If AddAppointmentToList fails for one day (that day's list
 * is full), only THIS record stops being placed into further days -- the
 * outer scan continues to the next record (each day's list is
 * independently growable, so one full day shouldn't abort everything). */
static void ApptPlaceSpanning (DateType occDate, UInt8 span, TimeType startTime,
	TimeType endTime, UInt16 recordNum, DateType anchorDate, UInt16 days,
	MemHandle apptLists [], UInt16 counts []) SEC002;
static void ApptPlaceSpanning (DateType occDate, UInt8 span, TimeType startTime,
	TimeType endTime, UInt16 recordNum, DateType anchorDate, UInt16 days,
	MemHandle apptLists [], UInt16 counts [])
{
	Int32 trueFirst = (Int32) DateToDays (occDate) - (Int32) DateToDays (anchorDate);
	Int32 trueLast = trueFirst + (Int32) span;
	Int32 firstIdx = (trueFirst < 0) ? 0 : trueFirst;
	Int32 lastIdx = (trueLast > (Int32) (days - 1)) ? (Int32) (days - 1) : trueLast;
	Int32 di;

	for (di = firstIdx; di <= lastIdx; di++)
		{
		TimeType dayStart, dayEnd;

		if (di == trueFirst) dayStart = startTime;
		else { dayStart.hours = 0; dayStart.minutes = 0; }

		if (di == trueLast) dayEnd = endTime;
		else { dayEnd.hours = 23; dayEnd.minutes = 59; }

		if (! AddAppointmentToList (&apptLists[di], counts[di], dayStart, dayEnd, recordNum))
			break;
		counts[di]++;
		}
}

SEC002 void ApptGetAppointments (DmOpenRef dbP, DateType date, UInt16 days,
	MemHandle apptLists [], UInt16 counts [])
{
	Int32	endDate;     /* Db35: DateToInt is now a full 32-bit key (real years
								* >=2032 exceed 16 bits) -- must not truncate here. */
	UInt16	index;
	UInt16	recordNum;
	DateType	apptDate;
	TimeType startTime;
	TimeType endTime;
	DateType tempDate;
	DateType repeatDate;
	Boolean repeats;
	MemHandle recordH;
	ApptInfoPtr apptList;
	ApptDBRecordType apptRec;
	ApptPackedDBRecordPtr r;

	
	MemSet (apptLists, days * sizeof (MemHandle), 0);
	MemSet (counts, days * sizeof (UInt16), 0);


	tempDate = date;
	DateAdjust (&tempDate, days-1);
	endDate = DateToInt(tempDate);

	// Db35: a non-repeating appointment that started up to kSpanMaxDays
	// days before the query window can still span INTO it -- anchor the
	// scan that far back instead of exactly at `date` (records are sorted
	// by start date, so this only costs a few extra records, not a full
	// scan). ApptPlaceSpanning is a no-op for any record whose span
	// doesn't actually reach [0,days-1], so no separate skip check is
	// needed for the ones the wider anchor surfaces but don't concern
	// this query.
	tempDate = date;
	DateAdjust (&tempDate, -kSpanMaxDays);
	ApptFindFirst (dbP, tempDate, &recordNum);
	while (true)
		{
		UInt8 apptSpan;

		recordH = DmQueryNextInCategory (dbP, &recordNum, dmAllCategories);
		if (! recordH) break;

		// Check if the appointment is on the date passed, if it is
		// add it to the appointment list.
		r = MemHandleLock (recordH);
		startTime = r->when.startTime;
		endTime = r->when.endTime;
		apptDate = r->when.date;
		apptSpan = r->flags.endDate ? (UInt8) r->firstField : 0;
		MemHandleUnlock (recordH);

		// Sorted by start date -- once a record's OWN start is past the
		// whole window, nothing later can matter either.
		if (DateToInt (apptDate) > endDate)
			break;

		ApptPlaceSpanning (apptDate, apptSpan, startTime, endTime, recordNum,
			date, days, apptLists, counts);

		recordNum++;
		}


	// Add the repeating appointments to the list.  Repeating appointments
	// are stored at the beginning of the database.
	recordNum = 0;
	while (true)
		{
		recordH = DmQueryNextInCategory (dbP, &recordNum, dmAllCategories);
		if (! recordH) break;

		r = (ApptPackedDBRecordPtr) MemHandleLock (recordH);
		repeats = (r->flags.repeat != 0);

		if (repeats)
			{
			UInt8 apptSpan;
			UInt8 back;

			ApptUnpack (r, &apptRec);
			apptSpan = apptRec.endDateOffset;

			// Db35: backward lookback -- an occurrence that started up to
			// apptSpan days before `date` can still span into the query
			// window. ApptRepeatsOnDate (already exists) is the cheap
			// primitive that makes this tractable, no new repeat-math
			// function needed. Candidate dates here are always STRICTLY
			// before `date`, so this can never double-count against the
			// forward walk below (which starts AT `date`).
			for (back = 1; back <= apptSpan && back <= kSpanMaxDays; back++)
				{
				DateType candidate = date;
				DateAdjust (&candidate, -(Int16) back);
				if (ApptRepeatsOnDate (&apptRec, candidate))
					ApptPlaceSpanning (candidate, apptSpan, r->when.startTime,
						r->when.endTime, recordNum, date, days, apptLists, counts);
				}

			if (days == 1)
				{
				if (ApptRepeatsOnDate (&apptRec, date))
					ApptPlaceSpanning (date, apptSpan, r->when.startTime,
						r->when.endTime, recordNum, date, days, apptLists, counts);
				}

			else
				{
				repeatDate = date;
			 	while (ApptNextRepeat (&apptRec, &repeatDate))
					{
					if (DateToInt (repeatDate) > endDate)
						break;

					ApptPlaceSpanning (repeatDate, apptSpan, r->when.startTime,
						r->when.endTime, recordNum, date, days, apptLists, counts);

					if (DateToInt (repeatDate) == endDate)
						break;

					DateAdjust (&repeatDate, 1);
					}
				}
			}
		MemHandleUnlock (recordH);

		// If the record has no repeating info we've reached the end of the 
		// repeating appointments.
		if (! repeats) break;
		
		 recordNum++;
		}

	
	// Sort the list by start time.
	for (index = 0; index < days; index ++)
		{
		if (apptLists[index])
			{
			apptList = MemHandleLock(apptLists[index]);
			SysInsertionSort (apptList, counts[index], sizeof (ApptInfoType), 
				(_comparF *)ApptListCompare, 0L);
	
			MemHandleUnlock (apptLists[index]);
			MemHandleResize (apptLists[index], counts[index] *
				sizeof (ApptInfoType));
			}
		}
}


/************************************************************
 *
 *  FUNCTION: ApptGetRecord
 *
 *  DESCRIPTION: Get a read-only record from the Appointment Database
 *
 *  PARAMETERS: database pointer
 *				    database index
 *				    database record
 *
 *  RETURNS: ##0 if successful, errorcode if not
 *
 *  CREATED: 1/25/95 
 *
 *  BY: Roger Flores
 *
 *************************************************************/
Err ApptGetRecord (DmOpenRef dbP, UInt16 index, ApptDBRecordPtr r, 
	MemHandle * handleP)
{
	MemHandle handle;
	ApptPackedDBRecordPtr src;
	

	handle = DmQueryRecord(dbP, index);
	ErrFatalDisplayIf(DmGetLastErr(), "Error Querying record");
	
	src = (ApptPackedDBRecordPtr) MemHandleLock (handle);

	if (DmGetLastErr())
		{
		*handleP = 0;
		return DmGetLastErr();
		}

	// Db35: an encrypted record is decrypted (or masked "******" when Sec35 is locked) into
	// a reused buffer; *handleP then points at that buffer (caller MemHandleUnlocks it as
	// usual).  We keep our single lock on `handle` only long enough to copy the bytes out.
	if (ApptFlagsOf((const UInt8 *) src).encrypted)
		{
		MemHandle dec = ApptGetDecrypted(dbP, index, (const UInt8 *) src, r);
		MemHandleUnlock (handle);				/* release OUR storage lock (copied out) */
		if (dec != NULL)
			{
			*handleP = dec;						/* the decrypted buffer, locked; caller unlocks */
			return 0;
			}
		src = (ApptPackedDBRecordPtr) MemHandleLock (handle);	/* OOM: best-effort raw */
		}

	ApptUnpack(src, r);

	*handleP = handle;
	return 0;
}


/* Db35: like ApptGetRecord, but never decrypts. Fills only the clear prefix --
 * when/endDateOffset/alarm/repeat/exceptions -- leaving description/note NULL
 * and never touching (or even looking at) the possibly-encrypted trailing
 * region. Safe for any caller that only needs date/repeat/span math (which is
 * every Month View scan/marker/tick-draw routine -- none of them read
 * description or note text), and considerably cheaper: ApptGetRecord pays for
 * a full AES-CTR decrypt via ApptGetDecrypted on every encrypted record
 * regardless of which fields the caller actually wants. */
Err ApptGetWhenRepeat (DmOpenRef dbP, UInt16 index, ApptDBRecordPtr r,
	MemHandle * handleP)
{
	MemHandle handle;
	ApptPackedDBRecordPtr src;
	ApptDBRecordFlags flags;
	char *p;

	handle = DmQueryRecord(dbP, index);
	ErrFatalDisplayIf(DmGetLastErr(), "Error Querying record");

	src = (ApptPackedDBRecordPtr) MemHandleLock (handle);

	if (DmGetLastErr())
		{
		*handleP = 0;
		return DmGetLastErr();
		}

	flags = src->flags;
	p = &src->firstField;

	r->when = (ApptDateTimeType *) src;

	if (flags.endDate)
		{
		r->endDateOffset = *(UInt8 *) p;
		p += kEndDateOffsetPackedSize;
		}
	else
		r->endDateOffset = 0;

	if (flags.alarm)
		{
		r->alarm = (AlarmInfoType *) p;
		p += sizeof (AlarmInfoType);
		}
	else
		r->alarm = NULL;

	if (flags.repeat)
		{
		r->repeat = (RepeatInfoType *) p;
		p += sizeof (RepeatInfoType);
		}
	else
		r->repeat = NULL;

	if (flags.exceptions)
		r->exceptions = (ExceptionsListType *) p;
	else
		r->exceptions = NULL;

	r->description = NULL;
	r->note = NULL;

	*handleP = handle;
	return 0;
}


/************************************************************
 *
 *  FUNCTION: ApptChangeRecord
 *
 *  DESCRIPTION: Change a record in the Appointment Database
 *
 *  PARAMETERS: database pointer
 *					 database index
 *					 database record
 *					 changed fields
 *
 *  RETURNS: ##0 if successful, errorcode if not
 *
 *  CREATED: 1/25/95 
 *
 *  BY: Roger Flores
 *
 *	COMMENTS:	Records are not stored with extra padding - they
 *	are always resized to their exact storage space.  This avoids
 *	a database compression issue.  The code works as follows:
 *	
 *	1)	get the size of the new record
 *	2)	make the new record
 *	3)	pack the packed record plus the changes into the new record
 *	4)	if the sort position is changes move to the new position
 *	5)	attach in position
 *
 *************************************************************/
Err ApptChangeRecord(DmOpenRef dbP, UInt16 *index, ApptDBRecordPtr r, 
	ApptDBRecordFlags changedFields)
{
	Err result;
	Int16 newIndex;
	UInt16 attributes;
	Boolean dontMove;
	MemHandle oldH;
	MemHandle srcH;
	MemHandle dstH;
	ApptDBRecordType src;
	ApptPackedDBRecordPtr dst;
	ApptPackedDBRecordPtr cmp;
	
	// We do not assume that r is completely valid so we get a valid
	// ApptDBRecordPtr...
	if ((result = ApptGetRecord(dbP, *index, &src, &srcH)) != 0)
		return result;
	
	// and we apply the changes to it.
	if (changedFields.when)
		src.when = r->when;

	if (changedFields.endDate)
		src.endDateOffset = r->endDateOffset;

	if (changedFields.alarm)
		src.alarm = r->alarm;
	
	if (changedFields.repeat)
		src.repeat = r->repeat;
	
	if (changedFields.exceptions)
		src.exceptions = r->exceptions;
		
	if (changedFields.description)
		src.description = r->description;
		
	if (changedFields.note)
		src.note = r->note;


	// Allocate a new chunk with the correct size and pack the data from 
	// the unpacked record into it.
	dstH = DmNewHandle(dbP, (UInt32) ApptPackedSize(&src));
	if (dstH)
		{
		dst = MemHandleLock (dstH);
		ApptPack (&src, dst);
		}

	MemHandleUnlock (srcH);
	if (dstH == NULL)
		return dmErrMemError;


	// If the sort position is changed move to the new position.
	// Check if any of the key fields have changed. 
	if ((!changedFields.when) && (! changedFields.repeat))
		goto attachRecord;				// repeating events aren't in sorted order
	
		
	// Make sure *index-1 < *index < *index+1, if so it's in sorted 
	// order.  Leave it there.	
	if (*index > 0)
		{
		// This record wasn't deleted and deleted records are at the end of the
		// database so the prior record may not be deleted!
		cmp = MemHandleLock (DmQueryRecord(dbP, *index-1));		
		dontMove = (ApptComparePackedRecords (cmp, dst, 0, NULL, NULL, 0) <= 0);
		MemPtrUnlock (cmp);
		}
	else 
		dontMove = true;


	if (dontMove && (*index+1 < DmNumRecords (dbP)))
		{
		DmRecordInfo(dbP, *index+1, &attributes, NULL, NULL);
		if ( ! (attributes & dmRecAttrDelete) )
			{
			cmp = MemHandleLock (DmQueryRecord(dbP, *index+1));
			dontMove = dontMove && ApptComparePackedRecords (dst, cmp, 0, NULL, NULL, 0) <= 0;
			MemPtrUnlock (cmp);
			}
		}
		
	if (dontMove)
		goto attachRecord;
	
	
	// The record isn't in the right position.  Move it.
	newIndex = ApptFindSortPosition (dbP, dst);
	DmMoveRecord (dbP, *index, newIndex);
	if (newIndex > *index) newIndex--;
	*index = newIndex;						// return new position


attachRecord:
	// Attach the new record to the old index,  the preserves the 
	// category and record id.
	result = DmAttachRecord (dbP, index, dstH, &oldH);
	
	MemPtrUnlock(dst);

	if (result) return result;

	MemHandleFree(oldH);

	// Db35: ApptChangeRecord repacks from the DECRYPTED src, so the new chunk is plaintext.
	// Re-seal it (no-op if no passcode / locked) so an edit never leaves it decrypted.
	ApptEncryptOne (dbP, *index);

	#if	EMULATION_LEVEL != EMULATION_NONE
		ECApptDBValidate (dbP);
	#endif

	return 0;
}


/************************************************************
 *
 *  FUNCTION: ApptNewRecord
 *
 *  DESCRIPTION: Create a new packed record in sorted position
 *
 *  PARAMETERS: database pointer
 *				database record
 *
 *  RETURNS: ##0 if successful, error code if not
 *
 *  CREATED: 1/25/95 
 *
 *  BY: Roger Flores
 *
 *************************************************************/
Err ApptNewRecord(DmOpenRef dbP, ApptDBRecordPtr r, UInt16 *index)
{
	MemHandle recordH;
	ApptPackedDBRecordPtr recordP;
	UInt16 					newIndex;
	Err err;

	
	// Make a new chunk with the correct size.
	recordH = DmNewHandle (dbP, (UInt32) ApptPackedSize(r));
	if (recordH == NULL)
		return dmErrMemError;

	recordP = MemHandleLock (recordH);
	
	// Copy the data from the unpacked record to the packed one.
	ApptPack (r, recordP);

	newIndex = ApptFindSortPosition(dbP, recordP);

	MemPtrUnlock (recordP);

	// 4) attach in place
	err = DmAttachRecord(dbP, &newIndex, recordH, 0);
	if (err) 
		MemHandleFree(recordH);
	else
		*index = newIndex;
	
	return err;
}

