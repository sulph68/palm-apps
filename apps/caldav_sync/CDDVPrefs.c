/*
 * CDDVPrefs.c - CalDAVRelayDB: one record holding the whole CDDVPrefsType
 * blob. No dmHdrAttrBackup - this is an easily-rebuilt config cache with
 * Sec35-encrypted secrets, nothing gained by exposing it to HotSync backup.
 */
#include "CDDVPrefs.h"

#define cddvDBName "CalDAVRelayDB"
#define cddvDBType 'DATA'

/* v1 layout snapshot (pre-verbose-status-fields), kept only so
 * CDDVLoadPrefs can migrate an existing blob forward instead of wiping it
 * on a struct-size change. Must stay byte-for-byte identical to the old shape. */
typedef struct {
	char server[cddvUrlLen];
	char path[cddvPathLen];
	char uuid[40];
	char user[cddvUserLen];
	UInt8 secret[cddvSecretMaxLen];
	UInt16 secretLen;
	char alias[cddvAliasLen];
	CDDVServerType serverType;
	Boolean ignoreOnSyncAll;
	UInt8 pastOverrideSel;
	UInt8 futureOverrideSel;
	UInt32 lastSyncTime;
	Boolean lastSyncOk;
} CDDVSlotV1;

typedef struct {
	UInt16 version;
	CDDVPastWindow pastDefault;
	CDDVFutureWindow futureDefault;
	UInt8 activeSlot;
	CDDVSlotV1 slots[cddvMaxSlots];
} CDDVPrefsTypeV1;

/* v2 layout snapshot (pre-googleClientId) - same reasoning as v1 above.
 * Must stay byte-for-byte identical to CDDVSlot's own pre-v3 shape. */
typedef struct {
	char server[cddvUrlLen];
	char path[cddvPathLen];
	char uuid[40];
	char user[cddvUserLen];
	UInt8 secret[cddvSecretMaxLen];
	UInt16 secretLen;
	char alias[cddvAliasLen];
	CDDVServerType serverType;
	Boolean ignoreOnSyncAll;
	UInt8 pastOverrideSel;
	UInt8 futureOverrideSel;
	UInt32 lastSyncTime;
	Boolean lastSyncOk;
	UInt16 lastSyncEventCount;
	char lastSyncErrMsg[cddvSyncErrMsgLen];
} CDDVSlotV2;

typedef struct {
	UInt16 version;
	CDDVPastWindow pastDefault;
	CDDVFutureWindow futureDefault;
	UInt8 activeSlot;
	CDDVSlotV2 slots[cddvMaxSlots];
} CDDVPrefsTypeV2;

/* v3 layout snapshot (pre-googleClientSecret) - same reasoning as v1/v2
 * above. Must stay byte-for-byte identical to CDDVSlot's own pre-v4 shape. */
typedef struct {
	char server[cddvUrlLen];
	char path[cddvPathLen];
	char uuid[40];
	char user[cddvUserLen];
	UInt8 secret[cddvSecretMaxLen];
	UInt16 secretLen;
	char alias[cddvAliasLen];
	CDDVServerType serverType;
	Boolean ignoreOnSyncAll;
	UInt8 pastOverrideSel;
	UInt8 futureOverrideSel;
	UInt32 lastSyncTime;
	Boolean lastSyncOk;
	UInt16 lastSyncEventCount;
	char lastSyncErrMsg[cddvSyncErrMsgLen];
	char googleClientId[cddvGoogleClientIdLen];
} CDDVSlotV3;

typedef struct {
	UInt16 version;
	CDDVPastWindow pastDefault;
	CDDVFutureWindow futureDefault;
	UInt8 activeSlot;
	CDDVSlotV3 slots[cddvMaxSlots];
} CDDVPrefsTypeV3;

/* v4 layout snapshot (pre-per-field-encryption) - same reasoning as v1/v2/v3
 * above. Must stay byte-for-byte identical to CDDVSlot's own pre-v5 shape -
 * server/path/uuid/user/alias/googleClientId/googleClientSecret are all
 * still PLAINTEXT at this version (only secret was ever encrypted). */
typedef struct {
	char server[cddvUrlLen];
	char path[cddvPathLen];
	char uuid[40];
	char user[cddvUserLen];
	UInt8 secret[cddvSecretMaxLen];
	UInt16 secretLen;
	char alias[cddvAliasLen];
	CDDVServerType serverType;
	Boolean ignoreOnSyncAll;
	UInt8 pastOverrideSel;
	UInt8 futureOverrideSel;
	UInt32 lastSyncTime;
	Boolean lastSyncOk;
	UInt16 lastSyncEventCount;
	char lastSyncErrMsg[cddvSyncErrMsgLen];
	char googleClientId[cddvGoogleClientIdLen];
	char googleClientSecret[cddvGoogleClientSecretLen];
} CDDVSlotV4;

typedef struct {
	UInt16 version;
	CDDVPastWindow pastDefault;
	CDDVFutureWindow futureDefault;
	UInt8 activeSlot;
	CDDVSlotV4 slots[cddvMaxSlots];
} CDDVPrefsTypeV4;

/* Static, not stack-local - this platform's default stack can't hold a
 * struct this size. A union, not separate statics per version - datares is
 * tight and these migration paths never run in the same load, so they can
 * safely share one region sized to the largest of the four. */
static union {
	CDDVPrefsTypeV1 v1;
	CDDVPrefsTypeV2 v2;
	CDDVPrefsTypeV3 v3;
	CDDVPrefsTypeV4 v4;
} gMigrateScratch;

static LocalID CDDVFindOrCreateDB(void)
{
	LocalID dbID = DmFindDatabase(0, cddvDBName);
	if (dbID != 0) return dbID;
	if (DmCreateDatabase(0, cddvDBName, appCreator, cddvDBType, false) != errNone)
		return 0;
	return DmFindDatabase(0, cddvDBName);
}

SEC002 void CDDVLoadPrefs(CDDVPrefsType *p)
{
	LocalID dbID;
	DmOpenRef db;
	MemHandle h;
	const void *raw;
	UInt32 size;

	MemSet(p, sizeof(*p), 0);
	p->version = cddvPrefsVersion;
	p->pastDefault = cddvPastWindowDefault;
	p->futureDefault = cddvFutureWindowDefault;

	dbID = DmFindDatabase(0, cddvDBName);
	if (dbID == 0) return;	/* never saved yet - defaults above stand */

	db = DmOpenDatabase(0, dbID, dmModeReadOnly);
	if (!db) return;

	if (DmNumRecords(db) < 1) {
		DmCloseDatabase(db);
		return;
	}

	h = DmQueryRecord(db, 0);
	if (!h) {
		DmCloseDatabase(db);
		return;
	}

	/* Peek the version field before copying the whole struct - avoids a
	 * stack-sized temporary. */
	size = MemHandleSize(h);
	raw = MemHandleLock(h);
	if (size == sizeof(CDDVPrefsType)) {
		UInt16 version;
		MemMove(&version, raw, sizeof(version));
		if (version == cddvPrefsVersion) {
			MemMove(p, raw, sizeof(CDDVPrefsType));
		}
		/* size matches but version doesn't - keep the defaults already set above. */
	} else if (size == sizeof(CDDVPrefsTypeV4)) {
		/* v4 blob - migrate field by field, same reasoning as v1/v2/v3
		 * below. Every field copied here (server/path/uuid/user/alias/
		 * googleClientId/googleClientSecret) is still PLAINTEXT at this
		 * version - n->fieldsEncrypted is left false by the MemSet above
		 * on purpose, so CalDAVSync.c's StartApplication runs its
		 * one-time CDDVEncryptSlotFieldsIfNeeded pass on these slots
		 * right after this load (Sec35 is guaranteed unlocked by then -
		 * the app's own launch gate). The new *Len companions are left
		 * 0 here too - that same pass computes them from these fields'
		 * real StrLen() before encrypting, so there's no need to
		 * duplicate that here. */
		UInt16 version;
		MemMove(&version, raw, sizeof(version));
		if (version == 4) {
			UInt8 i;
			MemMove(&gMigrateScratch.v4, raw, sizeof(CDDVPrefsTypeV4));
			p->pastDefault = gMigrateScratch.v4.pastDefault;
			p->futureDefault = gMigrateScratch.v4.futureDefault;
			p->activeSlot = gMigrateScratch.v4.activeSlot;
			for (i = 0; i < cddvMaxSlots; i++) {
				CDDVSlotV4 *o = &gMigrateScratch.v4.slots[i];
				CDDVSlot *n = &p->slots[i];
				MemMove(n->server, o->server, sizeof(o->server));
				MemMove(n->path, o->path, sizeof(o->path));
				MemMove(n->uuid, o->uuid, sizeof(o->uuid));
				MemMove(n->user, o->user, sizeof(o->user));
				MemMove(n->secret, o->secret, sizeof(o->secret));
				n->secretLen = o->secretLen;
				MemMove(n->alias, o->alias, sizeof(o->alias));
				n->serverType = o->serverType;
				n->ignoreOnSyncAll = o->ignoreOnSyncAll;
				n->pastOverrideSel = o->pastOverrideSel;
				n->futureOverrideSel = o->futureOverrideSel;
				n->lastSyncTime = o->lastSyncTime;
				n->lastSyncOk = o->lastSyncOk;
				n->lastSyncEventCount = o->lastSyncEventCount;
				MemMove(n->lastSyncErrMsg, o->lastSyncErrMsg, sizeof(o->lastSyncErrMsg));
				MemMove(n->googleClientId, o->googleClientId, sizeof(o->googleClientId));
				MemMove(n->googleClientSecret, o->googleClientSecret, sizeof(o->googleClientSecret));
				/* n->fieldsEncrypted, n->serverLen, etc. stay 0/false. */
			}
		}
		MemSet(&gMigrateScratch, sizeof(gMigrateScratch), 0);
	} else if (size == sizeof(CDDVPrefsTypeV3)) {
		/* v3 blob - migrate field by field, same reasoning as v1/v2 below.
		 * googleClientSecret is left zeroed by the MemSet above (empty -
		 * the user re-enters it once per Google slot after this migration). */
		UInt16 version;
		MemMove(&version, raw, sizeof(version));
		if (version == 3) {
			UInt8 i;
			MemMove(&gMigrateScratch.v3, raw, sizeof(CDDVPrefsTypeV3));
			p->pastDefault = gMigrateScratch.v3.pastDefault;
			p->futureDefault = gMigrateScratch.v3.futureDefault;
			p->activeSlot = gMigrateScratch.v3.activeSlot;
			for (i = 0; i < cddvMaxSlots; i++) {
				CDDVSlotV3 *o = &gMigrateScratch.v3.slots[i];
				CDDVSlot *n = &p->slots[i];
				MemMove(n->server, o->server, sizeof(o->server));
				MemMove(n->path, o->path, sizeof(o->path));
				MemMove(n->uuid, o->uuid, sizeof(o->uuid));
				MemMove(n->user, o->user, sizeof(o->user));
				MemMove(n->secret, o->secret, sizeof(o->secret));
				n->secretLen = o->secretLen;
				MemMove(n->alias, o->alias, sizeof(o->alias));
				n->serverType = o->serverType;
				n->ignoreOnSyncAll = o->ignoreOnSyncAll;
				n->pastOverrideSel = o->pastOverrideSel;
				n->futureOverrideSel = o->futureOverrideSel;
				n->lastSyncTime = o->lastSyncTime;
				n->lastSyncOk = o->lastSyncOk;
				n->lastSyncEventCount = o->lastSyncEventCount;
				MemMove(n->lastSyncErrMsg, o->lastSyncErrMsg, sizeof(o->lastSyncErrMsg));
				MemMove(n->googleClientId, o->googleClientId, sizeof(o->googleClientId));
			}
		}
		MemSet(&gMigrateScratch, sizeof(gMigrateScratch), 0);
	} else if (size == sizeof(CDDVPrefsTypeV2)) {
		/* v2 blob - migrate field by field, same reasoning as the v1 branch
		 * below. googleClientId is left zeroed by the MemSet above (empty -
		 * the user re-enters it once per Google slot after this migration). */
		UInt16 version;
		MemMove(&version, raw, sizeof(version));
		if (version == 2) {
			UInt8 i;
			MemMove(&gMigrateScratch.v2, raw, sizeof(CDDVPrefsTypeV2));
			p->pastDefault = gMigrateScratch.v2.pastDefault;
			p->futureDefault = gMigrateScratch.v2.futureDefault;
			p->activeSlot = gMigrateScratch.v2.activeSlot;
			for (i = 0; i < cddvMaxSlots; i++) {
				CDDVSlotV2 *o = &gMigrateScratch.v2.slots[i];
				CDDVSlot *n = &p->slots[i];
				MemMove(n->server, o->server, sizeof(o->server));
				MemMove(n->path, o->path, sizeof(o->path));
				MemMove(n->uuid, o->uuid, sizeof(o->uuid));
				MemMove(n->user, o->user, sizeof(o->user));
				MemMove(n->secret, o->secret, sizeof(o->secret));
				n->secretLen = o->secretLen;
				MemMove(n->alias, o->alias, sizeof(o->alias));
				n->serverType = o->serverType;
				n->ignoreOnSyncAll = o->ignoreOnSyncAll;
				n->pastOverrideSel = o->pastOverrideSel;
				n->futureOverrideSel = o->futureOverrideSel;
				n->lastSyncTime = o->lastSyncTime;
				n->lastSyncOk = o->lastSyncOk;
				n->lastSyncEventCount = o->lastSyncEventCount;
				MemMove(n->lastSyncErrMsg, o->lastSyncErrMsg, sizeof(o->lastSyncErrMsg));
			}
		}
		MemSet(&gMigrateScratch, sizeof(gMigrateScratch), 0);
	} else if (size == sizeof(CDDVPrefsTypeV1)) {
		/* v1 blob - migrate field by field instead of falling through to
		 * the generic reset-to-defaults case. */
		UInt16 version;
		MemMove(&version, raw, sizeof(version));
		if (version == 1) {
			UInt8 i;
			MemMove(&gMigrateScratch.v1, raw, sizeof(CDDVPrefsTypeV1));
			p->pastDefault = gMigrateScratch.v1.pastDefault;
			p->futureDefault = gMigrateScratch.v1.futureDefault;
			p->activeSlot = gMigrateScratch.v1.activeSlot;
			for (i = 0; i < cddvMaxSlots; i++) {
				CDDVSlotV1 *o = &gMigrateScratch.v1.slots[i];
				CDDVSlot *n = &p->slots[i];
				MemMove(n->server, o->server, sizeof(o->server));
				MemMove(n->path, o->path, sizeof(o->path));
				MemMove(n->uuid, o->uuid, sizeof(o->uuid));
				MemMove(n->user, o->user, sizeof(o->user));
				MemMove(n->secret, o->secret, sizeof(o->secret));
				n->secretLen = o->secretLen;
				MemMove(n->alias, o->alias, sizeof(o->alias));
				n->serverType = o->serverType;
				n->ignoreOnSyncAll = o->ignoreOnSyncAll;
				n->pastOverrideSel = o->pastOverrideSel;
				n->futureOverrideSel = o->futureOverrideSel;
				n->lastSyncTime = o->lastSyncTime;
				n->lastSyncOk = o->lastSyncOk;
				/* Newer fields left zeroed by the MemSet above. */
			}
		}
		MemSet(&gMigrateScratch, sizeof(gMigrateScratch), 0);
	}
	MemHandleUnlock(h);
	DmCloseDatabase(db);
}

SEC002 void CDDVSavePrefs(const CDDVPrefsType *p)
{
	LocalID dbID = CDDVFindOrCreateDB();
	DmOpenRef db;
	UInt16 index;
	MemHandle h;
	void *raw;
	Err err;

	if (dbID == 0) return;

	db = DmOpenDatabase(0, dbID, dmModeReadWrite);
	if (!db) return;

	if (DmNumRecords(db) < 1) {
		index = 0;
		h = DmNewRecord(db, &index, sizeof(CDDVPrefsType));
	} else {
		index = 0;
		h = DmGetRecord(db, index);
		if (h && MemHandleSize(h) != sizeof(CDDVPrefsType)) {
			DmReleaseRecord(db, index, false);
			DmRemoveRecord(db, index);
			h = DmNewRecord(db, &index, sizeof(CDDVPrefsType));
		}
	}
	if (!h) {
		DmCloseDatabase(db);
		return;
	}

	raw = MemHandleLock(h);
	err = DmWrite(raw, 0, p, sizeof(CDDVPrefsType));
	(void) err;
	MemHandleUnlock(h);
	DmReleaseRecord(db, index, true);

	DmCloseDatabase(db);
}

/* Baud rate is stored separately from CDDVPrefsType (PrefGetAppPreferences,
 * not the DB blob above) so picking a rate can never trip the size-mismatch
 * reset and wipe saved slots. kCddvBauds mirrors WiRSa's own bauds[]
 * (main.cpp) order exactly. */
#define cddvBaudPrefID		1
#define cddvDefaultBaudIndex	5	/* 57600 */
/* kCddvBauds is WiRSa's own bauds[]: 9600 14400 19200 28800 38400 57600 115200. Pref version 3 stores an index
 * into it. Version 1 stored one into 300 1200 2400 4800 9600 19200 38400 57600 115200, version 2 into the same
 * with 14400 and 28800 added; both are converted when read. */
static const UInt32 kCddvBauds[] = { 9600, 14400, 19200, 28800, 38400, 57600, 115200 };
#define cddvBaudCount	7

SEC002 UInt8 CDDVGetBaudIndex(void)
{
	UInt8 idx;
	UInt16 sz = sizeof(idx);
	Int16 ver = PrefGetAppPreferences(appCreator, cddvBaudPrefID, &idx, &sz, false);
	if (ver == noPreferenceFound || sz != sizeof(idx) || ver < 1 || ver > 3) {
		return cddvDefaultBaudIndex;
	}
	if (ver == 3) return idx < cddvBaudCount ? idx : cddvDefaultBaudIndex;
	if (ver == 2) idx = (UInt8) (idx < 4 ? 0 : idx - 4);			/* 300..4800 become 9600 */
	else if (idx < 5) idx = 0;										/* 300..9600 */
	else idx = (UInt8) (idx == 5 ? 2 : idx == 6 ? 4 : idx == 7 ? 5 : 6);
	if (idx >= cddvBaudCount) idx = cddvDefaultBaudIndex;
	CDDVSetBaudIndex(idx);
	return idx;
}

SEC002 void CDDVSetBaudIndex(UInt8 idx)
{
	if (idx >= cddvBaudCount) idx = cddvDefaultBaudIndex;
	PrefSetAppPreferences(appCreator, cddvBaudPrefID, 3, &idx, sizeof(idx), false);
}

SEC002 UInt32 CDDVGetBaud(void)
{
	return kCddvBauds[CDDVGetBaudIndex()];
}

/* Screenless Mode - its own pref ID for the same reason as the baud rate. */
#define cddvScreenlessPrefID	2

SEC002 Boolean CDDVGetScreenless(void)
{
	UInt8 v = 0;
	UInt16 sz = sizeof(v);
	if (PrefGetAppPreferences(appCreator, cddvScreenlessPrefID, &v, &sz, false) == noPreferenceFound
		|| sz != sizeof(v))
		return false;
	return (Boolean) (v != 0);
}

SEC002 void CDDVSetScreenless(Boolean on)
{
	UInt8 v = on ? 1 : 0;
	PrefSetAppPreferences(appCreator, cddvScreenlessPrefID, 1, &v, sizeof(v), false);
}
