/* SecDb.c -- CryptoNote's user databases (CryptoNoteDB_<suffix>), record
 * metadata (read-only flag, created/modified dates) and moving a memo between
 * databases. See SecDb.h and apps/cryptonote_secondary_db.md. All in sec003. */

#include <PalmOS.h>
#include "SecDb.h"
#include "Database.h"
#include "CryptoSec35.h"
#include "AesCrypt.h"
#include "EditForm.h"
#include "MainRsc.h"
#include "Utils.h"

/* MemoryPrv.h (not in the public SDK): the heap is in ROM/flash */
#ifndef memHeapFlagReadOnly
#define memHeapFlagReadOnly	0x0001
#endif

DmOpenRef	dbMainRef = NULL;
UInt8		gSrcCount = 2;

typedef struct {
	UInt16		card;
	LocalID		id;
	DmOpenRef	ref;
	Boolean		rom;
	UInt32		crDate;
	Char		name[dmDBNameLength];
	UInt8		catMap[dmRecNumCategories];	/* record category -> Memo Pad category */
} SrcInfo;

static SrcInfo	gSrc[kSrcMax];

/* AppInfo of a user database: the standard category block first (so the ROM
 * category calls work on it), then the database key used when the Palm has no
 * device ID. */
typedef struct {
	AppInfoType	cat;
	UInt16		secVer;
	UInt8		dbKey[secDbKeyLen];
} SecAppInfoType;

#define kSecAppInfoVer	1

/* ------------------------------------------------------------------ *
 *  helpers
 * ------------------------------------------------------------------ */
static LocalID DbAppInfoID (UInt16 card, LocalID id) DB3_SECTION;
static LocalID DbAppInfoID (UInt16 card, LocalID id)
{
	LocalID appInfoID = 0;
	DmDatabaseInfo (card, id, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &appInfoID, NULL, NULL, NULL);
	return appInfoID;
}

static Boolean IdInRom (UInt16 card, LocalID id, const Char* name) DB3_SECTION;
static Boolean IdInRom (UInt16 card, LocalID id, const Char* name)
{
	UInt16	attr = 0;
	UInt16	heap;
	MemPtr	p;

	DmDatabaseInfo (card, id, NULL, &attr, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	if (attr & dmHdrAttrReadOnly) return true;
#ifdef ROMSIM
	/* Test builds only: a database whose suffix starts with "ROM" behaves as
	 * if it were in flash ROM (POSE cannot run JackFlash). */
	if (StrNCompare (name, secDbPrefix "ROM", secDbPrefixLen + 3) == 0) return true;
#else
	(void) name;
#endif
	p = MemLocalIDToGlobal (id, card);
	if (! p) return false;
	if (MemLocalIDKind (id) == memIDHandle)
		heap = MemHandleHeapID ((MemHandle) p);
	else
		heap = MemPtrHeapID (p);
	return (Boolean) ((MemHeapFlags (heap) & memHeapFlagReadOnly) != 0);
}

/* Memo Pad's category block: the names every category index refers to. */
static AppInfoType* MemoCatLock (void) DB3_SECTION;
static AppInfoType* MemoCatLock (void)
{
	LocalID id, appInfoID;
	UInt16 card;
	if (DmOpenDatabaseInfo (dbMemoPadRef, &id, NULL, NULL, &card, NULL)) return NULL;
	appInfoID = DbAppInfoID (card, id);
	if (! appInfoID) return NULL;
	return (AppInfoType*) MemLocalIDToLockedPtr (appInfoID, card);
}

static SecAppInfoType* SecAppInfoLock (UInt8 s) DB3_SECTION;
static SecAppInfoType* SecAppInfoLock (UInt8 s)
{
	LocalID appInfoID = DbAppInfoID (gSrc[s].card, gSrc[s].id);
	if (! appInfoID) return NULL;
	return (SecAppInfoType*) MemLocalIDToLockedPtr (appInfoID, gSrc[s].card);
}

/* Builds the record-category -> Memo Pad-category map by name. In RAM the
 * database's records are renumbered to Memo Pad's categories and its names
 * replaced by Memo Pad's, so the map becomes 1:1. */
static void SrcSyncCategories (UInt8 s) DB3_SECTION;
static void SrcSyncCategories (UInt8 s)
{
	AppInfoType*	memo = MemoCatLock ();
	SecAppInfoType*	sec = SecAppInfoLock (s);
	UInt16			i, j;
	Boolean			same;

	for (i = 0; i < dmRecNumCategories; i++) gSrc[s].catMap[i] = (UInt8) i;
	if (! memo || ! sec) goto done;

	same = (Boolean) (MemCmp (memo->categoryLabels, sec->cat.categoryLabels,
		sizeof (memo->categoryLabels)) == 0);
	if (same) goto done;

	/* by the category's unique ID first (survives a rename), then by name;
	 * a category Memo Pad no longer has -> Unfiled */
	for (j = 1; j < dmRecNumCategories; j++)
		{
		UInt8 m = dmUnfiledCategory;
		if (sec->cat.categoryLabels[j][0])
			{
			for (i = 1; i < dmRecNumCategories; i++)
				if (memo->categoryLabels[i][0]
						&& memo->categoryUniqIDs[i] == sec->cat.categoryUniqIDs[j])
					{ m = (UInt8) i; break; }
			if (m == dmUnfiledCategory)
				for (i = 1; i < dmRecNumCategories; i++)
					if (StrCompare (sec->cat.categoryLabels[j], memo->categoryLabels[i]) == 0)
						{ m = (UInt8) i; break; }
			}
		gSrc[s].catMap[j] = m;
		}

	if (! gSrc[s].rom)
		{
		UInt16 n = DmNumRecords (gSrc[s].ref), r;
		for (r = 0; r < n; r++)
			{
			UInt16 attr;
			if (DmRecordInfo (gSrc[s].ref, r, &attr, NULL, NULL)) continue;
			if (gSrc[s].catMap[attr & dmRecAttrCategoryMask] != (attr & dmRecAttrCategoryMask))
				{
				attr = (UInt16) ((attr & ~dmRecAttrCategoryMask) | gSrc[s].catMap[attr & dmRecAttrCategoryMask]);
				DmSetRecordInfo (gSrc[s].ref, r, &attr, NULL);
				}
			}
		DmWrite (sec, 0, memo, sizeof (AppInfoType));
		for (i = 0; i < dmRecNumCategories; i++) gSrc[s].catMap[i] = (UInt8) i;
		}

done:
	if (memo) MemPtrUnlock (memo);
	if (sec) MemPtrUnlock (sec);
}

static void SrcFill (UInt8 s, UInt16 card, LocalID id) DB3_SECTION;
static void SrcFill (UInt8 s, UInt16 card, LocalID id)
{
	gSrc[s].card = card;
	gSrc[s].id = id;
	gSrc[s].crDate = 0;
	gSrc[s].name[0] = 0;
	DmDatabaseInfo (card, id, gSrc[s].name, NULL, NULL, &gSrc[s].crDate, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	gSrc[s].rom = IdInRom (card, id, gSrc[s].name);
}

static void SrcOpenOne (UInt8 s, UInt16 mode) DB3_SECTION;
static void SrcOpenOne (UInt8 s, UInt16 mode)
{
	UInt16 m = gSrc[s].rom ? (UInt16) (dmModeReadOnly | (mode & dmModeShowSecret)) : mode;
	gSrc[s].ref = DmOpenDatabase (gSrc[s].card, gSrc[s].id, m);
	if (gSrc[s].ref) SrcSyncCategories (s);
}

/* ------------------------------------------------------------------ *
 *  sources
 * ------------------------------------------------------------------ */
void SrcOpenAll (UInt16 mode)
{
	DmSearchStateType	st;
	UInt16				card;
	LocalID				id;
	Boolean				first = true;
	UInt8				n = 2, i, j;

	dbMainRef = dbCryptoPadRef;
	if (DmOpenDatabaseInfo (dbMainRef, &id, NULL, NULL, &card, NULL) == errNone)
		SrcFill (cryptoPadDB, card, id);
	gSrc[cryptoPadDB].ref = dbMainRef;
	if (DmOpenDatabaseInfo (dbMemoPadRef, &id, NULL, NULL, &card, NULL) == errNone)
		SrcFill (memoPadDB, card, id);
	gSrc[memoPadDB].ref = dbMemoPadRef;

	while (n < kSrcMax && DmGetNextDatabaseByTypeCreator (first, &st, secDbType,
			cryptoPadCreator, false, &card, &id) == errNone)
		{
		first = false;
		SrcFill (n, card, id);
		n++;
		}

	/* name order, a RAM copy before a ROM one of the same name */
	for (i = 3; i < n; i++)
		for (j = i; j > 2; j--)
			{
			Int16 c = StrCaselessCompare (gSrc[j - 1].name, gSrc[j].name);
			if (c > 0 || (c == 0 && gSrc[j - 1].rom && ! gSrc[j].rom))
				{
				SrcInfo t = gSrc[j]; gSrc[j] = gSrc[j - 1]; gSrc[j - 1] = t;
				}
			else break;
			}

	gSrcCount = n;
	for (i = 2; i < n; i++) SrcOpenOne (i, mode);
}

void SrcCloseAll (void)
{
	UInt8 i;
	for (i = 2; i < gSrcCount; i++)
		if (gSrc[i].ref) { DmCloseDatabase (gSrc[i].ref); gSrc[i].ref = NULL; }
	dbCryptoPadRef = dbMainRef;
	AesSetSecret (NULL, 0);
}

/* Show/Mask/Hide changed: the main and Memo Pad databases are reopened by the
 * caller; reopen ours with the new mode too. */
void SrcReopenAll (UInt16 mode)
{
	UInt8 i;
	for (i = 2; i < gSrcCount; i++)
		{
		if (gSrc[i].ref) DmCloseDatabase (gSrc[i].ref);
		SrcOpenOne (i, mode);
		}
	gSrc[cryptoPadDB].ref = dbMainRef;
	gSrc[memoPadDB].ref = dbMemoPadRef;
}

DmOpenRef SrcRef (UInt8 s)
{
	if (s == memoPadDB) return dbMemoPadRef;
	if (s == cryptoPadDB || s >= gSrcCount) return dbMainRef;
	return gSrc[s].ref;
}

void SrcSelect (UInt8 s)
{
	if (s >= 2 && s < gSrcCount && gSrc[s].ref)
		{
		dbCryptoPadRef = gSrc[s].ref;
		SrcKeyForWrite (s);
		}
	else
		{
		dbCryptoPadRef = dbMainRef;
		AesSetSecret (NULL, 0);
		}
}

Boolean SrcIsRom (UInt8 s)
{
	return (Boolean) (s >= 2 && s < gSrcCount && gSrc[s].rom);
}

UInt32 SrcCrDate (UInt8 s) { return (s < gSrcCount) ? gSrc[s].crDate : 0; }
LocalID SrcDbID (UInt8 s) { return (s < gSrcCount) ? gSrc[s].id : 0; }

UInt8 SrcOfDbID (LocalID id)
{
	UInt8 i;
	for (i = 0; i < gSrcCount; i++) if (gSrc[i].id == id) return i;
	return 0xFF;
}

UInt8 SrcOfRef (DmOpenRef ref)
{
	UInt8 i;
	if (ref == dbMemoPadRef) return memoPadDB;
	for (i = 2; i < gSrcCount; i++) if (gSrc[i].ref == ref) return i;
	return cryptoPadDB;
}

UInt8 SrcOfCrDate (UInt32 crDate)
{
	UInt8 i;
	for (i = 0; i < gSrcCount; i++) if (gSrc[i].crDate == crDate) return i;
	return 0xFF;
}

void SrcLabel (UInt8 s, Char* out)
{
	if (s == memoPadDB) { StrCopy (out, "Memo Pad"); return; }
	if (s == cryptoPadDB || s >= gSrcCount) { StrCopy (out, "Main"); return; }
	if (StrNCompare (gSrc[s].name, secDbPrefix, secDbPrefixLen) == 0 && gSrc[s].name[secDbPrefixLen])
		StrCopy (out, gSrc[s].name + secDbPrefixLen);
	else
		StrCopy (out, gSrc[s].name);
	if (gSrc[s].rom) StrCat (out, " (ROM)");
}

UInt16 SrcRecCategory (UInt8 s, UInt16 idx)
{
	UInt16 attr = 0;
	DmRecordInfo (SrcRef (s), idx, &attr, NULL, NULL);
	attr &= dmRecAttrCategoryMask;
	if (s >= 2 && s < gSrcCount) attr = gSrc[s].catMap[attr];
	return attr;
}

void SrcSetRecCategory (UInt8 s, UInt16 idx, UInt16 cat)
{
	UInt16 attr = 0;
	DmOpenRef db = SrcRef (s);
	if (SrcIsRom (s)) return;
	DmRecordInfo (db, idx, &attr, NULL, NULL);
	attr = (UInt16) ((attr & ~dmRecAttrCategoryMask) | (cat & dmRecAttrCategoryMask) | dmRecAttrDirty);
	DmSetRecordInfo (db, idx, &attr, NULL);
}

UInt16 SrcCountRecords (UInt8 s)
{
	DmOpenRef db = SrcRef (s);
	return db ? DmNumRecordsInCategory (db, dmAllCategories) : 0;
}

UInt32 SrcSizeBytes (UInt8 s)
{
	UInt32 total = 0;
	if (s < gSrcCount)
		DmDatabaseSize (gSrc[s].card, gSrc[s].id, NULL, &total, NULL);
	return total;
}

Boolean SrcRecReadOnly (UInt8 s, UInt16 idx)
{
	MemHandle h;
	UInt8 m;
	if (s == memoPadDB) return false;
	if (SrcIsRom (s)) return true;
	h = DmQueryRecord (SrcRef (s), idx);
	if (! h) return false;
	m = ((const UInt8*) MemHandleLock (h))[0];
	MemHandleUnlock (h);
	return (Boolean) ((m & kCryptoFmtMarker) && (m & kRecReadOnlyBit));
}

/* ------------------------------------------------------------------ *
 *  create / rename / delete
 * ------------------------------------------------------------------ */
static UInt16 SuffixCheck (const Char* suffix, Char* name) DB3_SECTION;
static UInt16 SuffixCheck (const Char* suffix, Char* name)
{
	DmSearchStateType	st;
	UInt16				card, len, i;
	LocalID				id;
	Boolean				first = true;
	Char				other[dmDBNameLength];

	len = (UInt16) StrLen (suffix);
	if (len == 0) return altDbNameEmpty;
	if (len > secDbSuffixMax) return altDbNameBad;
	for (i = 0; i < len; i++)
		if ((UInt8) suffix[i] < 0x20) return altDbNameBad;

	StrCopy (name, secDbPrefix);
	StrCat (name, suffix);

	if (DmFindDatabase (0, name)) return altDbNameUsed;
	/* A RAM database with a ROM database's name would hide the ROM one, so
	 * check every database of ours, ROM included, not just DmFindDatabase. */
	while (DmGetNextDatabaseByTypeCreator (first, &st, secDbType, cryptoPadCreator,
			false, &card, &id) == errNone)
		{
		first = false;
		other[0] = 0;
		DmDatabaseInfo (card, id, other, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
		if (StrCaselessCompare (other, name) == 0) return altDbNameUsed;
		}
	return 0;
}

UInt16 SrcCreate (const Char* suffix, UInt8* outSrc)
{
	Char		name[dmDBNameLength];
	UInt16		err, attr, ver = 1;
	LocalID		id, appInfoID;
	DmOpenRef	ref;
	MemHandle	h;
	UInt8		s;

	if (gSrcCount >= kSrcMax) return altDbTooMany;
	err = SuffixCheck (suffix, name);
	if (err) return err;

	if (DmCreateDatabase (0, name, cryptoPadCreator, secDbType, false)) return altDbCreateFailed;
	id = DmFindDatabase (0, name);
	if (! id) return altDbCreateFailed;
	ref = DmOpenDatabase (0, id, dmModeReadWrite);
	if (! ref) return altDbCreateFailed;

	h = DmNewHandle (ref, sizeof (SecAppInfoType));
	if (! h) { DmCloseDatabase (ref); DmDeleteDatabase (0, id); return altDbCreateFailed; }
	{
	SecAppInfoType*	p = (SecAppInfoType*) MemHandleLock (h);
	AppInfoType*	memo = MemoCatLock ();
	UInt8			key[secDbKeyLen];
	UInt16			v = kSecAppInfoVer;

	DmSet (p, 0, sizeof (SecAppInfoType), 0);
	if (memo) { DmWrite (p, 0, memo, sizeof (AppInfoType)); MemPtrUnlock (memo); }
	DmWrite (p, OffsetOf (SecAppInfoType, secVer), &v, sizeof (v));
	GenerateRandomIV (key);
	DmWrite (p, OffsetOf (SecAppInfoType, dbKey), key, secDbKeyLen);
	MemSet (key, sizeof (key), 0);
	MemHandleUnlock (h);
	}
	appInfoID = MemHandleToLocalID (h);
	attr = dmHdrAttrBackup;
	DmSetDatabaseInfo (0, id, NULL, &attr, &ver, NULL, NULL, NULL, NULL, &appInfoID, NULL, NULL, NULL);
	DmCloseDatabase (ref);

	/* appended (not re-sorted), so the source numbers in use stay valid */
	s = gSrcCount;
	SrcFill (s, 0, id);
	SrcOpenOne (s, (PrivateRecordVisualStatus == hidePrivateRecords) ?
		dmModeReadWrite : (dmModeReadWrite | dmModeShowSecret));
	if (! gSrc[s].ref) return altDbCreateFailed;
	gSrcCount++;
	if (outSrc) *outSrc = s;
	return 0;
}

UInt16 SrcRename (UInt8 s, const Char* suffix)
{
	Char	name[dmDBNameLength];
	UInt16	err;

	if (s < 2 || s >= gSrcCount) return altDbMainFixed;
	if (gSrc[s].rom) return altDbInRom;
	if (StrNCompare (gSrc[s].name, secDbPrefix, secDbPrefixLen) == 0
			&& StrCaselessCompare (gSrc[s].name + secDbPrefixLen, suffix) == 0
			&& StrCompare (gSrc[s].name + secDbPrefixLen, suffix) != 0)
		{
		/* only the letter case changes: skip the "already used" check */
		StrCopy (name, secDbPrefix); StrCat (name, suffix);
		err = 0;
		}
	else
		err = SuffixCheck (suffix, name);
	if (err) return err;
	if (DmSetDatabaseInfo (gSrc[s].card, gSrc[s].id, name, NULL, NULL, NULL, NULL, NULL,
			NULL, NULL, NULL, NULL, NULL))
		return altDbCreateFailed;
	StrCopy (gSrc[s].name, name);
	return 0;
}

UInt16 SrcDelete (UInt8 s)
{
	UInt8 i;
	if (s < 2 || s >= gSrcCount) return altDbMainFixed;
	if (gSrc[s].rom) return altDbInRom;
	if (dbCryptoPadRef == gSrc[s].ref) SrcSelect (cryptoPadDB);
	if (gSrc[s].ref) DmCloseDatabase (gSrc[s].ref);
	gSrc[s].ref = NULL;
	if (DmDeleteDatabase (gSrc[s].card, gSrc[s].id)) return altDbCreateFailed;
	for (i = s; i + 1 < gSrcCount; i++) gSrc[i] = gSrc[i + 1];
	gSrcCount--;
	return 0;
}

/* ------------------------------------------------------------------ *
 *  device ID and keying
 * ------------------------------------------------------------------ */
Boolean DevHasDeviceID (void)
{
	return AesDeviceID (NULL, NULL);
}

static void SrcUseDbKey (UInt8 s) DB3_SECTION;
static void SrcUseDbKey (UInt8 s)
{
	SecAppInfoType* p = SecAppInfoLock (s);
	if (p)
		{
		UInt8 key[secDbKeyLen];
		MemMove (key, p->dbKey, secDbKeyLen);
		MemPtrUnlock (p);
		AesSetSecret (key, secDbKeyLen);
		MemSet (key, sizeof (key), 0);
		}
	else
		{
		/* no AppInfo (damaged database): still never fall back to the
		 * main-database derivation */
		static const UInt8 zero[secDbKeyLen] = { 0 };
		AesSetSecret (zero, secDbKeyLen);
		}
}

void SrcKeyForWrite (UInt8 s)
{
	const UInt8*	sec;
	UInt16			len;
	if (s < 2 || s >= gSrcCount) { AesSetSecret (NULL, 0); return; }
	if (AesDeviceID (&sec, &len)) AesSetSecret (sec, len);
	else SrcUseDbKey (s);
}

void SrcKeyForRecord (UInt8 s, UInt16 idx)
{
	MemHandle	h;
	UInt8		m = 0;
	const UInt8* sec;
	UInt16		len;

	if (s < 2 || s >= gSrcCount) { AesSetSecret (NULL, 0); return; }
	h = DmQueryRecord (gSrc[s].ref, idx);
	if (h) { m = ((const UInt8*) MemHandleLock (h))[0]; MemHandleUnlock (h); }
	if ((m & kCryptoFmtMarker) && (m & kRecDbKeyBit)) SrcUseDbKey (s);
	else if (AesDeviceID (&sec, &len)) AesSetSecret (sec, len);
	else SrcUseDbKey (s);
}

/* ------------------------------------------------------------------ *
 *  record metadata
 * ------------------------------------------------------------------ */
static struct {
	Boolean		valid;
	DmOpenRef	db;
	UInt32		uid;
	UInt8		marker;
	Boolean		hasDates;
	UInt32		created, modified;
} gMeta;
static Boolean gMetaTouch = false;
static Boolean gMetaNoDates = false;

UInt32 MetaTrailerLen (const UInt8* raw, UInt32 size)
{
	if (size >= 1 + kRecTrailerLen && (raw[0] & kCryptoFmtMarker) && (raw[0] & kRecTrailerBit))
		return kRecTrailerLen;
	return 0;
}

Boolean MetaGet (DmOpenRef db, UInt16 idx, UInt32* created, UInt32* modified, UInt8* marker)
{
	MemHandle		h = DmQueryRecord (db, idx);
	const UInt8*	raw;
	UInt32			size;
	Boolean			has = false;

	*created = *modified = 0;
	if (marker) *marker = 0;
	if (! h) return false;
	raw = (const UInt8*) MemHandleLock (h);
	size = MemHandleSize (h);
	if (marker && size) *marker = (raw[0] & kCryptoFmtMarker) ? raw[0] : 0;
	if (MetaTrailerLen (raw, size))
		{
		MemMove (created, raw + size - kRecTrailerLen, 4);
		MemMove (modified, raw + size - 4, 4);
		has = true;
		}
	MemHandleUnlock (h);
	return has;
}

/* Remember a record's flag and dates before a save path rewrites it (the
 * writers drop the marker byte; CryptoSealRecord puts it back). */
void MetaCapture (DmOpenRef db, UInt16 idx)
{
	/* (gMetaTouch is left alone: the save path may set it before capturing;
	 * the seal clears it) */
	gMeta.valid = false;
	if (DmRecordInfo (db, idx, NULL, &gMeta.uid, NULL)) return;
	gMeta.db = db;
	gMeta.hasDates = MetaGet (db, idx, &gMeta.created, &gMeta.modified, &gMeta.marker);
	gMeta.valid = true;
}

/* The captured metadata goes to another record (a move). */
void MetaRebind (DmOpenRef db, UInt16 idx)
{
	if (! gMeta.valid) return;
	gMeta.db = db;
	DmRecordInfo (db, idx, NULL, &gMeta.uid, NULL);
}

void MetaTouch (void) { gMetaTouch = true; }
void MetaNoDates (Boolean on) { gMetaNoDates = on; }

UInt8 MetaSealBits (DmOpenRef db, UInt16 idx, Boolean wrapped, UInt32* created, UInt32* modified, Boolean* trailer)
{
	UInt8	bits = (UInt8) (kCryptoFmtMarker | (wrapped ? kCryptoFmtWrappedBit : 0));
	UInt32	uid = 0, now = TimGetSeconds ();

	DmRecordInfo (db, idx, NULL, &uid, NULL);
	*created = *modified = 0;
	*trailer = false;

	if (gMeta.valid && gMeta.db == db && gMeta.uid == uid)
		{
		bits |= (UInt8) (gMeta.marker & kRecReadOnlyBit);
		if (gMeta.hasDates)
			{
			*created = gMeta.created;
			*modified = gMetaTouch ? now : gMeta.modified;
			*trailer = true;
			}
		else if (gMetaTouch)
			{
			*modified = now;		/* created stays unknown (0) */
			*trailer = true;
			}
		}
	else if (! gMetaNoDates)
		{
		*created = *modified = now;	/* a new record */
		*trailer = true;
		}

	if (db != dbMainRef && db != dbMemoPadRef)
		{
		bits |= kRecSecKeyBit;
		if (! DevHasDeviceID ()) bits |= kRecDbKeyBit;
		}
	if (*trailer) bits |= kRecTrailerBit;

	gMeta.valid = false;
	gMetaTouch = false;
	return bits;
}

void MetaSetReadOnly (DmOpenRef db, UInt16 idx, Boolean on)
{
	MemHandle	h = DmGetRecord (db, idx);
	UInt8*		p;
	UInt8		m;
	if (! h) return;
	p = (UInt8*) MemHandleLock (h);
	m = p[0];
	if (m & kCryptoFmtMarker)
		{
		m = on ? (UInt8) (m | kRecReadOnlyBit) : (UInt8) (m & ~kRecReadOnlyBit);
		DmWrite (p, 0, &m, 1);
		}
	MemHandleUnlock (h);
	DmReleaseRecord (db, idx, true);
}

/* ------------------------------------------------------------------ *
 *  protection line
 * ------------------------------------------------------------------ */
void SrcProtectionText (UInt8 s, UInt16 idx, Boolean pw, Char* out)
{
	if (s == memoPadDB) { StrCopy (out, "Not encrypted"); return; }
	if (s == cryptoPadDB || s >= gSrcCount)
		{
		if (EncEncryptionActive ())
			StrCopy (out, pw ? "Sec35 + password" : "Sec35 (this Palm)");
		else
			StrCopy (out, pw ? "Password + this Palm" : "Not encrypted");
		return;
		}
	if (! pw) { StrCopy (out, "Not encrypted"); return; }
	{
	MemHandle h = DmQueryRecord (gSrc[s].ref, idx);
	UInt8 m = 0;
	if (h) { m = ((const UInt8*) MemHandleLock (h))[0]; MemHandleUnlock (h); }
	if ((m & kCryptoFmtMarker) && (m & kRecSecKeyBit))
		StrCopy (out, (m & kRecDbKeyBit) ? "Password only" : "Password + this Palm");
	else
		StrCopy (out, DevHasDeviceID () ? "Password + this Palm" : "Password only");
	}
}

/* ------------------------------------------------------------------ *
 *  moving a memo
 * ------------------------------------------------------------------ */

/* The memo's text in a new handle: decrypted (with the record's own keying)
 * when it has a password -- NULL if the password is wrong or missing. */
MemHandle SrcRecordText (UInt8 s, UInt16 idx, Char* password, Boolean* encrypted)
{
	DmOpenRef	db = SrcRef (s);
	RecordType	rec;
	MemHandle	textH = NULL;
	Char*		text;
	UInt32		len;

	*encrypted = false;
	if (s != memoPadDB) SrcKeyForRecord (s, idx);
	if (s == memoPadDB)
		{
		MemHandle h = DmQueryRecord (db, idx);
		if (! h) return NULL;
		len = SafeStrLen ((const Char*) MemHandleLock (h));
		textH = MemHandleNew (len + 1);
		if (textH) MemMove (MemHandleLock (textH), MemHandleLock (h), len + 1);
		if (textH) { MemHandleUnlock (textH); MemHandleUnlock (h); }
		MemHandleUnlock (h);
		return textH;
		}
	if (! CryptoGetRecord (db, idx, &rec)) return NULL;
	*encrypted = (Boolean) (rec.flag == recEncrypted);
	len = *encrypted ? rec.textLen : SafeStrLen (rec.textP);
	textH = MemHandleNew (len + 1);
	if (! textH) { CryptoReleaseLast (); return NULL; }
	text = (Char*) MemHandleLock (textH);
	MemMove (text, rec.textP, len);
	text[len] = 0;
	if (*encrypted)
		{
		Char*	chk;
		UInt32	nOffset = 0;
		Boolean	ok = false;
		if (password && (chk = (Char*) MemPtrNew (rec.passwdLen + 1)) != NULL)
			{
			MemSet (chk, rec.passwdLen + 1, 0);
			AESCryptEx (rec.passwordP, chk, password, globalPasswordP, rec.pwIV,
				rec.kdfSalt, rec.kdfRounds, rec.passwdLen, AES_DECRYPT);
			ok = (Boolean) (StrCompare (chk, password) == 0);
			MemSet (chk, rec.passwdLen + 1, 0);
			MemPtrFree (chk);
			}
		if (ok)
			{
			while (nOffset <= len) { if (rec.textP[nOffset++] == '\n') break; }
			if (nOffset < len)
				AESCryptEx (rec.textP + nOffset, text + nOffset, password, globalPasswordP,
					rec.bodyIV, rec.kdfSalt, rec.kdfRounds, (UInt16) (len - nOffset), AES_DECRYPT);
			text[len] = 0;
			}
		else
			{
			MemSet (text, len, 0);
			MemHandleUnlock (textH);
			MemHandleFree (textH);
			CryptoReleaseLast ();
			return NULL;
			}
		}
	MemHandleUnlock (textH);
	CryptoReleaseLast ();
	return textH;
}

Boolean SrcMoveRecord (UInt8 fromS, UInt16 fromIdx, UInt8 toS, Char* password, UInt16* outIdx)
{
	DmOpenRef	src = SrcRef (fromS), dst = SrcRef (toS);
	RecordType	plain;
	MemHandle	textH, newH;
	Char*		text;
	UInt16		attr, newIdx = dmMaxRecordIndex;
	Boolean		encrypted;
	UInt32		len;

	if (fromS == toS || fromS == memoPadDB || toS == memoPadDB) return false;
	if (SrcIsRom (fromS) || SrcIsRom (toS) || ! src || ! dst) return false;

	textH = SrcRecordText (fromS, fromIdx, password, &encrypted);
	if (! textH) { AesSetSecret (NULL, 0); return false; }
	text = (Char*) MemHandleLock (textH);
	len = SafeStrLen (text);

	DmRecordInfo (src, fromIdx, &attr, NULL, NULL);
	attr = (UInt16) ((attr & ~dmRecAttrCategoryMask) | SrcRecCategory (fromS, fromIdx));
	MetaCapture (src, fromIdx);

	newH = DmNewRecord (dst, &newIdx, sizeof (PackedRecordType));
	if (! newH) { MemSet (text, len, 0); MemHandleUnlock (textH); MemHandleFree (textH); return false; }
	plain.flag = recDecrypted;
	plain.passwdLen = 0;
	plain.textLen = (UInt16) len;
	plain.passwordP = "";
	plain.textP = text;
	MemSet (plain.pwIV, sizeof (plain.pwIV), 0);
	MemSet (plain.bodyIV, sizeof (plain.bodyIV), 0);
	MemSet (plain.kdfSalt, sizeof (plain.kdfSalt), 0);
	plain.kdfRounds = 0;
	PackRecord (&plain, newH);
	attr &= (UInt16) (dmRecAttrCategoryMask | dmRecAttrSecret);
	attr |= dmRecAttrDirty;
	DmSetRecordInfo (dst, newIdx, &attr, NULL);
	DmReleaseRecord (dst, newIdx, true);
	MemSet (text, len, 0);
	MemHandleUnlock (textH);
	MemHandleFree (textH);

	MetaRebind (dst, newIdx);
	SrcSelect (toS);
	if (encrypted) EncryptRecord (newIdx, password);
	/* EncryptRecord rewrote the record: the seal keeps the moved memo's flag
	 * and dates (MetaRebind above) */
	CryptoSealRecord (dst, newIdx);

	DmDeleteRecord (src, fromIdx);
	DmMoveRecord (src, fromIdx, DmNumRecords (src));

	*outIdx = newIdx;
	return true;
}
