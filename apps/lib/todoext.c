/*
 * todoext.c - Td35 per-ToDo extras DB (time + repeat), keyed by ToDo uniqueID.
 * See todoext.h.  Uses only ROM Data-Manager traps, so it is safe in "sec001".
 */
#include "todoext.h"

#define tdxType			'Tdx3'
#define tdxCreator		'Td35'			/* same creator as ToDo35 (owns it)   */
#define tdxName			"ToDo35ExtDB"

typedef struct {
	UInt32		uid;					/* ToDo record's DmRecordInfo uniqueID */
	TdxExt		ext;
} TdxRecord;


/***********************************************************************
 * TdxGetDatabase -- open (creating if needed) the extras DB.
 ***********************************************************************/
TDX_SEC Err TdxGetDatabase (DmOpenRef* dbPP, UInt16 mode)
{
	DmOpenRef	db;
	Err			err;

	*dbPP = NULL;

	db = DmOpenDatabaseByTypeCreator (tdxType, tdxCreator, mode);
	if (! db)
		{
		/* Only a writer creates it; a read-only reader just reports "none".
		 * NB: dmModeReadWrite == dmModeReadOnly|dmModeWrite, so test the WRITE
		 * bit -- checking dmModeReadOnly would wrongly fire for read-write too. */
		if (! (mode & dmModeWrite))
			return dmErrCantFind;

		err = DmCreateDatabase (0, tdxName, tdxCreator, tdxType, false);
		if (err) return err;

		db = DmOpenDatabaseByTypeCreator (tdxType, tdxCreator, mode);
		if (! db) return dmErrCantOpen;

		/* Set the backup bit so the extras survive a HotSync. */
		{
		UInt16		card;
		LocalID		lid;
		UInt16		attr;
		if (DmOpenDatabaseInfo (db, &lid, NULL, NULL, &card, NULL) == 0 &&
			 DmDatabaseInfo (card, lid, NULL, &attr, NULL, NULL, NULL, NULL,
								  NULL, NULL, NULL, NULL, NULL) == 0)
			{
			attr |= dmHdrAttrBackup;
			DmSetDatabaseInfo (card, lid, NULL, &attr, NULL, NULL, NULL, NULL,
									 NULL, NULL, NULL, NULL, NULL);
			}
		}
		}

	*dbPP = db;
	return 0;
}


/* Return the record index holding 'uid', or -1 if none. */
static Int16 TdxFind (DmOpenRef db, UInt32 uid) TDX_SEC;
TDX_SEC static Int16 TdxFind (DmOpenRef db, UInt32 uid)
{
	UInt16		n, i;

	n = DmNumRecords (db);
	for (i = 0; i < n; i++)
		{
		MemHandle	h = DmQueryRecord (db, i);
		UInt32		u;
		if (! h) continue;
		u = ((TdxRecord*) MemHandleLock (h))->uid;
		MemHandleUnlock (h);
		if (u == uid) return (Int16) i;
		}
	return -1;
}


/***********************************************************************
 * TdxGet -- fetch the extras for 'uid'.  Returns true iff a record with a
 *           non-zero flags block exists; 'out' is always written (zeroed if none).
 ***********************************************************************/
TDX_SEC Boolean TdxGet (DmOpenRef db, UInt32 uid, TdxExt* out)
{
	Int16		i;
	MemHandle	h;

	MemSet (out, sizeof (TdxExt), 0);
	if (! db) return false;

	i = TdxFind (db, uid);
	if (i < 0) return false;

	h = DmQueryRecord (db, (UInt16) i);
	if (! h) return false;
	MemMove (out, &((TdxRecord*) MemHandleLock (h))->ext, sizeof (TdxExt));
	MemHandleUnlock (h);

	return (Boolean) (out->flags != 0);
}


/***********************************************************************
 * TdxSet -- upsert the extras for 'uid'.  A zero flags block deletes the record.
 ***********************************************************************/
TDX_SEC Err TdxSet (DmOpenRef db, UInt32 uid, const TdxExt* ext)
{
	Int16		i;
	UInt16		idx;
	MemHandle	h;
	TdxRecord	rec;

	if (! db) return dmErrInvalidParam;
	if (ext->flags == 0)
		return TdxDelete (db, uid);

	rec.uid = uid;
	MemMove (&rec.ext, ext, sizeof (TdxExt));

	i = TdxFind (db, uid);
	if (i >= 0)
		{
		idx = (UInt16) i;
		h = DmGetRecord (db, idx);
		if (! h) return dmErrIndexOutOfRange;
		DmWrite (MemHandleLock (h), 0, &rec, sizeof (rec));
		MemHandleUnlock (h);
		DmReleaseRecord (db, idx, true);
		return 0;
		}

	idx = DmNumRecords (db);
	h = DmNewRecord (db, &idx, sizeof (TdxRecord));
	if (! h) return dmErrMemError;
	DmWrite (MemHandleLock (h), 0, &rec, sizeof (rec));
	MemHandleUnlock (h);
	DmReleaseRecord (db, idx, true);
	return 0;
}


/***********************************************************************
 * TdxDelete -- remove the extras record for 'uid' (no-op if absent).
 ***********************************************************************/
TDX_SEC Err TdxDelete (DmOpenRef db, UInt32 uid)
{
	Int16	i;

	if (! db) return 0;
	i = TdxFind (db, uid);
	if (i < 0) return 0;

	DmRemoveRecord (db, (UInt16) i);
	return 0;
}
