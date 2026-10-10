/*
 * locdb.c - Db35 event Location storage (two small owned DBs). See locdb.h.
 * Uses only ROM Data-Manager traps, so it is safe in any segment via
 * -DLOCDB_SEG_NAME (mirrors todoext.c's -DTODOEXT_SEG_NAME idiom).
 */
#include "locdb.h"

#define linkType		'Dlk1'
#define linkCreator		'Db35'
#define linkName		"AppointmentLocationDB"

#define locType			'Dlc1'
#define locCreator		'Db35'
#define locName			"LocationsDB"

/* On-disk record for DB 1 -- apptUid is a FOREIGN key (the appointment's
 * own uniqueID), stored as data since it isn't this record's own identity. */
typedef struct {
	UInt32		apptUid;
	UInt8		fromAddress35;
	UInt8		reserved;
	UInt32		locDbRecUID;
	DlxLocation	loc;
} LinkRecord;


/***********************************************************************
 * OpenOrCreate -- shared DB-open-or-create helper for both databases.
 * Only a writer creates a missing DB; a read-only caller gets dmErrCantFind.
 ***********************************************************************/
static Err OpenOrCreate (DmOpenRef* dbPP, UInt16 mode, UInt32 type,
	UInt32 creator, const char* name) LOC_SEC;
LOC_SEC static Err OpenOrCreate (DmOpenRef* dbPP, UInt16 mode, UInt32 type,
	UInt32 creator, const char* name)
{
	DmOpenRef	db;
	Err			err;

	*dbPP = NULL;

	db = DmOpenDatabaseByTypeCreator (type, creator, mode);
	if (! db)
		{
		if (! (mode & dmModeWrite))
			return dmErrCantFind;

		err = DmCreateDatabase (0, name, creator, type, false);
		if (err) return err;

		db = DmOpenDatabaseByTypeCreator (type, creator, mode);
		if (! db) return dmErrCantOpen;

		/* Set the backup bit so this survives a HotSync. */
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

LOC_SEC Err LinkGetDatabase (DmOpenRef* dbPP, UInt16 mode)
{
	return OpenOrCreate (dbPP, mode, linkType, linkCreator, linkName);
}

LOC_SEC Err LocDbGetDatabase (DmOpenRef* dbPP, UInt16 mode)
{
	return OpenOrCreate (dbPP, mode, locType, locCreator, locName);
}


/* ===================================================================
 * DB 1: AppointmentLocationDB -- foreign-keyed by the appointment's uid.
 * =================================================================== */

/* Return the record index holding 'apptUid', or -1 if none. */
static Int16 LinkFind (DmOpenRef db, UInt32 apptUid) LOC_SEC;
LOC_SEC static Int16 LinkFind (DmOpenRef db, UInt32 apptUid)
{
	UInt16		n, i;

	n = DmNumRecords (db);
	for (i = 0; i < n; i++)
		{
		MemHandle	h = DmQueryRecord (db, i);
		UInt32		u;
		if (! h) continue;
		u = ((LinkRecord*) MemHandleLock (h))->apptUid;
		MemHandleUnlock (h);
		if (u == apptUid) return (Int16) i;
		}
	return -1;
}

LOC_SEC Boolean LinkGet (DmOpenRef db, UInt32 apptUid, DlxLink* out)
{
	Int16		i;
	MemHandle	h;
	LinkRecord*	rp;

	MemSet (out, sizeof (DlxLink), 0);
	if (! db) return false;

	i = LinkFind (db, apptUid);
	if (i < 0) return false;

	h = DmQueryRecord (db, (UInt16) i);
	if (! h) return false;
	rp = (LinkRecord*) MemHandleLock (h);
	out->apptUid       = rp->apptUid;
	out->fromAddress35 = rp->fromAddress35;
	out->locDbRecUID   = rp->locDbRecUID;
	MemMove (&out->loc, &rp->loc, sizeof (DlxLocation));
	MemHandleUnlock (h);

	return (Boolean) (! LocIsBlank (&out->loc));
}

LOC_SEC Err LinkSet (DmOpenRef db, UInt32 apptUid, const DlxLink* link)
{
	Int16		i;
	UInt16		idx;
	MemHandle	h;
	LinkRecord	rec;

	if (! db) return dmErrInvalidParam;
	if (LocIsBlank (&link->loc))
		return LinkDelete (db, apptUid);

	rec.apptUid       = apptUid;
	rec.fromAddress35 = link->fromAddress35;
	rec.reserved      = 0;
	rec.locDbRecUID   = link->locDbRecUID;
	MemMove (&rec.loc, &link->loc, sizeof (DlxLocation));

	i = LinkFind (db, apptUid);
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
	h = DmNewRecord (db, &idx, sizeof (LinkRecord));
	if (! h) return dmErrMemError;
	DmWrite (MemHandleLock (h), 0, &rec, sizeof (rec));
	MemHandleUnlock (h);
	DmReleaseRecord (db, idx, true);
	return 0;
}

LOC_SEC Err LinkDelete (DmOpenRef db, UInt32 apptUid)
{
	Int16	i;

	if (! db) return 0;
	i = LinkFind (db, apptUid);
	if (i < 0) return 0;

	DmRemoveRecord (db, (UInt16) i);
	return 0;
}


/* ===================================================================
 * DB 2: LocationsDB -- flat "Previous Loc" history, own-identity keyed
 * (each record's Data-Manager uniqueID IS the key -- no foreign field
 * needed, so lookups use DmFindRecordByID, not a linear scan).
 * =================================================================== */

LOC_SEC UInt32 LocDbAdd (DmOpenRef db, const DlxLocation* loc)
{
	UInt16		idx;
	MemHandle	h;
	UInt32		uid;

	if (! db) return 0;

	idx = DmNumRecords (db);
	h = DmNewRecord (db, &idx, sizeof (DlxLocation));
	if (! h) return 0;
	DmWrite (MemHandleLock (h), 0, loc, sizeof (DlxLocation));
	MemHandleUnlock (h);
	DmReleaseRecord (db, idx, true);

	uid = 0;
	DmRecordInfo (db, idx, NULL, &uid, NULL);
	return uid;
}

LOC_SEC Boolean LocDbUpdate (DmOpenRef db, UInt32 recUID, const DlxLocation* loc)
{
	UInt16		idx;
	MemHandle	h;

	if (! db) return false;
	if (DmFindRecordByID (db, recUID, &idx) != 0) return false;

	h = DmGetRecord (db, idx);
	if (! h) return false;
	DmWrite (MemHandleLock (h), 0, loc, sizeof (DlxLocation));
	MemHandleUnlock (h);
	DmReleaseRecord (db, idx, true);
	return true;
}

LOC_SEC Boolean LocDbGet (DmOpenRef db, UInt32 recUID, DlxLocation* out)
{
	UInt16		idx;
	MemHandle	h;

	MemSet (out, sizeof (DlxLocation), 0);
	if (! db) return false;
	if (DmFindRecordByID (db, recUID, &idx) != 0) return false;

	h = DmQueryRecord (db, idx);
	if (! h) return false;
	MemMove (out, MemHandleLock (h), sizeof (DlxLocation));
	MemHandleUnlock (h);
	return true;
}

LOC_SEC Err LocDbDelete (DmOpenRef db, UInt32 recUID)
{
	UInt16	idx;

	if (! db) return 0;
	if (DmFindRecordByID (db, recUID, &idx) != 0) return 0;

	DmRemoveRecord (db, idx);
	return 0;
}

LOC_SEC UInt16 LocDbCount (DmOpenRef db)
{
	if (! db) return 0;
	return DmNumRecords (db);
}

LOC_SEC Boolean LocDbEnumerate (DmOpenRef db, UInt16 idx, UInt32* recUID, DlxLocation* out)
{
	MemHandle	h;

	if (! db || idx >= DmNumRecords (db)) return false;

	h = DmQueryRecord (db, idx);
	if (! h) return false;
	MemMove (out, MemHandleLock (h), sizeof (DlxLocation));
	MemHandleUnlock (h);
	*recUID = 0;
	DmRecordInfo (db, idx, NULL, recUID, NULL);
	return true;
}


/* ===================================================================
 * Shared helpers
 * =================================================================== */

LOC_SEC Boolean LocIsBlank (const DlxLocation* loc)
{
	return (Boolean) (loc->name[0] == 0 && loc->line1[0] == 0 &&
		loc->line2[0] == 0 && loc->apt[0] == 0 && loc->city[0] == 0 &&
		loc->state[0] == 0 && loc->zip[0] == 0 && loc->country[0] == 0);
}

/* Per user request: every filled field, comma-joined, in address order
 * (name, line1, line2, apt, city, state, zip, country) -- not just
 * "Name, City Zip" -- so the user can see every component that's set.
 * Blank fields are dropped silently. Day View shows this in full
 * (multi-line, wraps/grows the row); the Details preview field truncates
 * its own copy with an ellipsis if it doesn't fit 2 lines (LocFieldRefresh,
 * DateLocation.c) -- this function itself never truncates beyond outLen. */
LOC_SEC void LocSummary (const DlxLocation* loc, char* out, UInt16 outLen)
{
	out[0] = 0;
	if (! outLen) return;

#define LOC_APPEND(field) \
	if (loc->field[0]) \
		{ \
		if (out[0]) StrNCat (out, ", ", outLen); \
		StrNCat (out, loc->field, outLen); \
		}

	LOC_APPEND (name)
	LOC_APPEND (line1)
	LOC_APPEND (line2)
	LOC_APPEND (apt)
	LOC_APPEND (city)
	LOC_APPEND (state)
	LOC_APPEND (zip)
	LOC_APPEND (country)

#undef LOC_APPEND
}

LOC_SEC void LocSummaryForRecord (DmOpenRef apptDb, UInt16 recordNum, char* outText, UInt16 outLen)
{
	UInt32		uid = 0;
	DmOpenRef	linkDb;
	DlxLink		link;

	if (outLen) outText[0] = 0;
	if (! outLen) return;

	if (DmRecordInfo (apptDb, recordNum, NULL, &uid, NULL) != errNone) return;
	if (LinkGetDatabase (&linkDb, dmModeReadOnly) != 0 || ! linkDb) return;
	if (LinkGet (linkDb, uid, &link))
		LocSummary (&link.loc, outText, outLen);
	DmCloseDatabase (linkDb);
}
