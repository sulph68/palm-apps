/*
 * locdb.c - Db35 event Location storage (two small owned DBs). See locdb.h.
 * Uses only ROM Data-Manager traps plus enccore (Sec35), so it is safe in any
 * segment via -DLOCDB_SEG_NAME (mirrors todoext.c's -DTODOEXT_SEG_NAME idiom).
 *
 * Encryption (Sec35): both databases hold addresses (an Address35 pick is a
 * full copy of the contact's address), so the DlxLocation part of every
 * record is AES-CTR encrypted with its own store key while a password is set.
 *   AppointmentLocationDB: LinkRecord.reserved bit 0 set = loc encrypted;
 *     nonce (apptUid, kLocNonceLink). Same size either way.
 *   LocationsDB: an encrypted record is the DlxLocation + one trailing byte
 *     (kLocEncMark); a plaintext one is exactly sizeof(DlxLocation).
 *     Nonce (record uniqueID, kLocNonceHist).
 * Written while Sec35 is locked = stored plaintext, encrypted by LocSealAll at
 * the next unlocked launch (same rule as the appointments). Read while locked:
 * a link reads back masked ("******"); encrypted history entries are hidden
 * from the Previous Loc picker. Sec35's password set/change/removal/wipe goes
 * through LocRekey (called from Datebook35's ApptRekey).
 */
#include "locdb.h"
#include "enccore.h"
#include "encrekey.h"

#define kLocEncLink		0x01		/* LinkRecord.reserved: loc is encrypted   */
#define kLocEncMark		0x01		/* trailing byte of an encrypted history rec */
#define kLocNonceLink	0x4C4BUL	/* 'LK' */
#define kLocNonceHist	0x4C48UL	/* 'LH' */

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
	if (rp->reserved & kLocEncLink)
		LocDecryptOrMask (db, ENC_STORE_DBLOC, &out->loc, apptUid, kLocNonceLink);
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
	rec.locDbRecUID   = link->locDbRecUID;
	MemMove (&rec.loc, &link->loc, sizeof (DlxLocation));
	rec.reserved      = LocEncrypt (db, ENC_STORE_DBLOC, &rec.loc, apptUid, kLocNonceLink)
		? kLocEncLink : 0;

	i = LinkFind (db, apptUid);
	if (i >= 0)
		{
		idx = (UInt16) i;
		h = DmGetRecord (db, idx);
		if (! h) return dmErrIndexOutOfRange;
		DmWrite (MemHandleLock (h), 0, &rec, sizeof (rec));
		MemHandleUnlock (h);
		DmReleaseRecord (db, idx, true);
		MemSet (&rec, sizeof (rec), 0);
		return 0;
		}

	idx = DmNumRecords (db);
	h = DmNewRecord (db, &idx, sizeof (LinkRecord));
	if (h)
		{
		DmWrite (MemHandleLock (h), 0, &rec, sizeof (rec));
		MemHandleUnlock (h);
		DmReleaseRecord (db, idx, true);
		}
	MemSet (&rec, sizeof (rec), 0);
	return h ? 0 : dmErrMemError;
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

/* Write 'loc' into history record idx (already sized or new), encrypting it
 * when a key is available (record grows by the marker byte). */
static Boolean LocHistWrite (DmOpenRef db, UInt16 idx, const DlxLocation* loc) LOC_SEC;
LOC_SEC static Boolean LocHistWrite (DmOpenRef db, UInt16 idx, const DlxLocation* loc)
{
	DlxLocation	tmp;
	UInt32		uid = 0;
	UInt8		mark = kLocEncMark;
	Boolean		enc;
	MemHandle	h;

	MemMove (&tmp, loc, sizeof (tmp));
	DmRecordInfo (db, idx, NULL, &uid, NULL);
	enc = LocEncrypt (db, ENC_STORE_DBLHIST, &tmp, uid, kLocNonceHist);
	h = DmResizeRecord (db, idx, sizeof (DlxLocation) + (enc ? 1 : 0));
	if (h)
		{
		UInt8* p = (UInt8*) MemHandleLock (h);
		DmWrite (p, 0, &tmp, sizeof (tmp));
		if (enc) DmWrite (p, sizeof (tmp), &mark, 1);
		MemHandleUnlock (h);
		}
	MemSet (&tmp, sizeof (tmp), 0);
	return (Boolean) (h != NULL);
}

/* Read history record idx into 'out'. false = it is encrypted and Sec35 is
 * locked (the caller hides it), or the record is unreadable. */
static Boolean LocHistRead (DmOpenRef db, UInt16 idx, DlxLocation* out) LOC_SEC;
LOC_SEC static Boolean LocHistRead (DmOpenRef db, UInt16 idx, DlxLocation* out)
{
	MemHandle	h = DmQueryRecord (db, idx);
	Boolean		enc;
	UInt32		uid = 0;
	UInt8		key[ENC_KEY_BYTES];

	MemSet (out, sizeof (DlxLocation), 0);
	if (! h) return false;
	enc = (Boolean) (MemHandleSize (h) > sizeof (DlxLocation));
	MemMove (out, MemHandleLock (h), sizeof (DlxLocation));
	MemHandleUnlock (h);
	if (! enc) return true;
	if (! EncGetStoreKeyDb (key, ENC_STORE_DBLHIST, db))
		{ MemSet (out, sizeof (DlxLocation), 0); return false; }
	DmRecordInfo (db, idx, NULL, &uid, NULL);
	EncCrypt (key, (UInt8*) out, sizeof (DlxLocation), uid, kLocNonceHist);
	MemSet (key, ENC_KEY_BYTES, 0);
	return true;
}

/* Index of the n-th history record the caller may see (all of them when
 * unlocked; plaintext ones only while Sec35 is locked). -1 past the end. */
static Int16 LocHistNth (DmOpenRef db, UInt16 n) LOC_SEC;
LOC_SEC static Int16 LocHistNth (DmOpenRef db, UInt16 n)
{
	UInt8	key[ENC_KEY_BYTES];
	Boolean	unlocked = EncGetStoreKeyDb (key, ENC_STORE_DBLHIST, db);
	UInt16	i, total = DmNumRecords (db);

	MemSet (key, ENC_KEY_BYTES, 0);
	for (i = 0; i < total; i++)
		{
		MemHandle h = DmQueryRecord (db, i);
		if (! h) continue;
		if (! unlocked && MemHandleSize (h) > sizeof (DlxLocation)) continue;
		if (n == 0) return (Int16) i;
		n--;
		}
	return -1;
}

LOC_SEC UInt32 LocDbAdd (DmOpenRef db, const DlxLocation* loc)
{
	UInt16		idx;
	UInt32		uid;

	if (! db) return 0;

	idx = DmNumRecords (db);
	if (! DmNewRecord (db, &idx, sizeof (DlxLocation))) return 0;
	LocHistWrite (db, idx, loc);
	DmReleaseRecord (db, idx, true);

	uid = 0;
	DmRecordInfo (db, idx, NULL, &uid, NULL);
	return uid;
}

LOC_SEC Boolean LocDbUpdate (DmOpenRef db, UInt32 recUID, const DlxLocation* loc)
{
	UInt16		idx;
	Boolean		ok;

	if (! db) return false;
	if (DmFindRecordByID (db, recUID, &idx) != 0) return false;

	if (! DmGetRecord (db, idx)) return false;
	ok = LocHistWrite (db, idx, loc);
	DmReleaseRecord (db, idx, true);
	return ok;
}

LOC_SEC Boolean LocDbGet (DmOpenRef db, UInt32 recUID, DlxLocation* out)
{
	UInt16		idx;

	MemSet (out, sizeof (DlxLocation), 0);
	if (! db) return false;
	if (DmFindRecordByID (db, recUID, &idx) != 0) return false;
	return LocHistRead (db, idx, out);
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
	UInt16	n = 0;

	if (! db) return 0;
	while (LocHistNth (db, n) >= 0) n++;
	return n;
}

LOC_SEC Boolean LocDbEnumerate (DmOpenRef db, UInt16 idx, UInt32* recUID, DlxLocation* out)
{
	Int16	i;

	if (! db) return false;
	i = LocHistNth (db, idx);
	if (i < 0) return false;
	if (! LocHistRead (db, (UInt16) i, out)) return false;
	*recUID = 0;
	DmRecordInfo (db, (UInt16) i, NULL, recUID, NULL);
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


/* ===================================================================
 * Sec35 encryption helpers (see the header comment)
 * =================================================================== */

/* Encrypt 'loc' in place if a key is available. true = encrypted. */
LOC_SEC Boolean LocEncrypt (DmOpenRef db, UInt32 storeId, DlxLocation* loc,
	UInt32 nonceHi, UInt32 nonceLo)
{
	UInt8	key[ENC_KEY_BYTES];

	if (! EncGetStoreKeyDb (key, storeId, db)) return false;
	EncCrypt (key, (UInt8*) loc, sizeof (DlxLocation), nonceHi, nonceLo);
	MemSet (key, ENC_KEY_BYTES, 0);
	return true;
}

/* Decrypt 'loc' in place, or mask it ("******" as the name) when locked. */
LOC_SEC void LocDecryptOrMask (DmOpenRef db, UInt32 storeId, DlxLocation* loc,
	UInt32 nonceHi, UInt32 nonceLo)
{
	if (! LocEncrypt (db, storeId, loc, nonceHi, nonceLo))	/* CTR: same op */
		{
		MemSet (loc, sizeof (DlxLocation), 0);
		StrCopy (loc->name, "******");
		}
}

/* Mark a database scheme 2 if it holds nothing encrypted (key scheme marker,
 * as the appointment DB does at launch), then encrypt its plaintext records. */
static void LocSealDb (DmOpenRef db, Boolean isLink) LOC_SEC;
LOC_SEC static void LocSealDb (DmOpenRef db, Boolean isLink)
{
	UInt32	store = isLink ? ENC_STORE_DBLOC : ENC_STORE_DBLHIST;
	UInt8	key[ENC_KEY_BYTES];
	UInt16	i, n = DmNumRecords (db), encCount = 0;

	for (i = 0; i < n; i++)
		{
		MemHandle	h = DmQueryRecord (db, i);
		UInt8*		p;
		if (! h) continue;
		p = (UInt8*) MemHandleLock (h);
		if (isLink ? (Boolean) ((((LinkRecord*) p)->reserved & kLocEncLink) != 0)
				   : (Boolean) (MemHandleSize (h) > sizeof (DlxLocation)))
			encCount++;
		MemHandleUnlock (h);
		}
	if (! EncDbHasStoreKeys (db) && encCount == 0)
		EncDbSetStoreKeys (db);
	if (! EncGetStoreKeyDb (key, store, db)) return;		/* locked / no password */
	MemSet (key, ENC_KEY_BYTES, 0);
	if (encCount == n) return;
	for (i = 0; i < n; i++)
		{
		MemHandle	h = DmQueryRecord (db, i);
		UInt16		attr = 0;
		if (! h) continue;
		DmRecordInfo (db, i, &attr, NULL, NULL);
		if (isLink)
			{
			LinkRecord	rec;
			MemMove (&rec, MemHandleLock (h), sizeof (rec));
			MemHandleUnlock (h);
			if (! (rec.reserved & kLocEncLink) &&
				LocEncrypt (db, store, &rec.loc, rec.apptUid, kLocNonceLink))
				{
				rec.reserved |= kLocEncLink;
				h = DmGetRecord (db, i);
				if (h) { DmWrite (MemHandleLock (h), 0, &rec, sizeof (rec)); MemHandleUnlock (h);
				         DmReleaseRecord (db, i, false); }
				}
			MemSet (&rec, sizeof (rec), 0);
			}
		else if (MemHandleSize (h) <= sizeof (DlxLocation))
			{
			DlxLocation	loc;
			MemMove (&loc, MemHandleLock (h), sizeof (loc));
			MemHandleUnlock (h);
			if (DmGetRecord (db, i))
				{
				LocHistWrite (db, i, &loc);
				DmReleaseRecord (db, i, false);
				}
			MemSet (&loc, sizeof (loc), 0);
			}
		DmSetRecordInfo (db, i, &attr, NULL);	/* sealing must not queue a HotSync */
		}
}

LOC_SEC void LocSealAll (void)
{
	DmOpenRef	db;

	if (LinkGetDatabase (&db, dmModeReadWrite) == 0 && db)
		{ LocSealDb (db, true); DmCloseDatabase (db); }
	if (LocDbGetDatabase (&db, dmModeReadWrite) == 0 && db)
		{ LocSealDb (db, false); DmCloseDatabase (db); }
}

/* ---- kSecLaunchRekey: one record, explicit keys (encrekey.h) ---- */
static Boolean LocLinkIsEnc (const UInt8* rec, UInt32 size) LOC_SEC;
LOC_SEC static Boolean LocLinkIsEnc (const UInt8* rec, UInt32 size)
{
	return (Boolean) (size >= sizeof (LinkRecord) &&
		(((const LinkRecord*) rec)->reserved & kLocEncLink));
}

static Boolean LocHistIsEnc (const UInt8* rec, UInt32 size) LOC_SEC;
LOC_SEC static Boolean LocHistIsEnc (const UInt8* rec, UInt32 size)
{
	return (Boolean) (size > sizeof (DlxLocation));
}

static Err LocLinkRekey (DmOpenRef db, UInt16 index, const UInt8* fromK, const UInt8* toK) LOC_SEC;
LOC_SEC static Err LocLinkRekey (DmOpenRef db, UInt16 index, const UInt8* fromK, const UInt8* toK)
{
	MemHandle	h = DmGetRecord (db, index);
	LinkRecord	rec;
	UInt16		attr = 0;

	if (! h) return dmErrIndexOutOfRange;
	if (MemHandleSize (h) < sizeof (rec)) { DmReleaseRecord (db, index, false); return errNone; }
	DmRecordInfo (db, index, &attr, NULL, NULL);
	MemMove (&rec, MemHandleLock (h), sizeof (rec));
	if (fromK) EncCrypt (fromK, (UInt8*) &rec.loc, sizeof (DlxLocation), rec.apptUid, kLocNonceLink);
	if (toK)   EncCrypt (toK,   (UInt8*) &rec.loc, sizeof (DlxLocation), rec.apptUid, kLocNonceLink);
	rec.reserved = (UInt8) (toK ? (rec.reserved | kLocEncLink) : (rec.reserved & ~kLocEncLink));
	DmWrite (MemHandleLock (h), 0, &rec, sizeof (rec));
	MemHandleUnlock (h); MemHandleUnlock (h);
	DmReleaseRecord (db, index, false);
	DmSetRecordInfo (db, index, &attr, NULL);
	MemSet (&rec, sizeof (rec), 0);
	return errNone;
}

static Err LocHistRekey (DmOpenRef db, UInt16 index, const UInt8* fromK, const UInt8* toK) LOC_SEC;
LOC_SEC static Err LocHistRekey (DmOpenRef db, UInt16 index, const UInt8* fromK, const UInt8* toK)
{
	MemHandle	h = DmGetRecord (db, index);
	DlxLocation	loc;
	UInt32		uid = 0;
	UInt16		attr = 0;
	UInt8		mark = kLocEncMark;

	if (! h) return dmErrIndexOutOfRange;
	DmRecordInfo (db, index, &attr, &uid, NULL);
	MemMove (&loc, MemHandleLock (h), sizeof (loc));
	MemHandleUnlock (h);
	if (fromK) EncCrypt (fromK, (UInt8*) &loc, sizeof (loc), uid, kLocNonceHist);
	if (toK)   EncCrypt (toK,   (UInt8*) &loc, sizeof (loc), uid, kLocNonceHist);
	h = DmResizeRecord (db, index, sizeof (loc) + (toK ? 1 : 0));
	if (! h)
		{
		MemSet (&loc, sizeof (loc), 0);
		DmReleaseRecord (db, index, false);
		return memErrNotEnoughSpace;
		}
	{
	UInt8* p = (UInt8*) MemHandleLock (h);
	DmWrite (p, 0, &loc, sizeof (loc));
	if (toK) DmWrite (p, sizeof (loc), &mark, 1);
	MemHandleUnlock (h);
	}
	DmReleaseRecord (db, index, false);
	DmSetRecordInfo (db, index, &attr, NULL);
	MemSet (&loc, sizeof (loc), 0);
	return errNone;
}

/* Called from Datebook35's ApptRekey after the appointments (ordinal 0).
 * Returns kRekeyResultDone / LowMem / Failed. */
LOC_SEC UInt16 LocRekey (const EncRekeyParam* p, UInt32 ckptCreator)
{
	DmOpenRef	db = DmOpenDatabaseByTypeCreator (linkType, linkCreator, dmModeReadWrite);
	UInt16		r = kRekeyResultDone;

	if (db)
		{
		r = EncRekeyDb (db, ENC_STORE_DBLOC, p, 1, ckptCreator, LocLinkIsEnc, LocLinkRekey);
		DmCloseDatabase (db);
		if (r != kRekeyResultDone) return r;
		}
	db = DmOpenDatabaseByTypeCreator (locType, locCreator, dmModeReadWrite);
	if (db)
		{
		r = EncRekeyDb (db, ENC_STORE_DBLHIST, p, 2, ckptCreator, LocHistIsEnc, LocHistRekey);
		DmCloseDatabase (db);
		}
	return r;
}
