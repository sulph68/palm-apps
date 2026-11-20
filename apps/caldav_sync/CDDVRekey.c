/*
 * CDDVRekey.c - CalDAV Sync's kSecLaunchRekey handler (enccore.h, encrekey.h)
 * and the key-scheme check of its launch gate.
 *
 * CalDAV Sync owns three kinds of Sec35-encrypted data, each its own store:
 *   CalDAVRelayDB      one record: per slot the secret (nonce (0, slot)) and
 *                      the settings fields (nonce (field, slot))  ENC_STORE_CDDV
 *   DateBook35DAV      synced events, fields (uid, fieldIdx)      ENC_STORE_DAVEV
 *   Address35DAV_<n>   synced contacts per slot: list value (LIST, uid), text
 *                      (CARD, uid), AppInfo alias (ALIA, slot)    ENC_STORE_DAVCON+slot
 * The two synced databases are caches of server data: a sync needs Sec35
 * unlocked and writes them encrypted. With no password they cannot be synced
 * or read, so a password removal / forget deletes them (and the plain
 * birthdays index Address35DAVBDA built from the contacts); a change or the
 * scheme conversion re-keys them so they stay readable offline. A removal
 * clears the saved secrets (nowhere safe to keep them) and leaves the
 * settings fields as plaintext; a forget clears both.
 */
#include <PalmOS.h>
#include "CalDAVSync.h"
#include "CalDAVSyncRsc.h"
#include "DAVDb.h"
#include "ContactDb.h"
#include "CDDVSec35.h"
#include "enccore.h"
#include "encrekey.h"

#define cddvRelayDBName	"CalDAVRelayDB"

/* ---- CalDAVRelayDB ------------------------------------------------------ */

typedef struct { UInt16 dataOff, lenOff, cap; UInt8 field; } CDDVFieldDesc;

/* Field nonce ids: the CDDVField values used by CDDVEncryptSlotField. */
#define F(f, l, id) { OffsetOf(CDDVSlot, f), OffsetOf(CDDVSlot, l), sizeof(((CDDVSlot *) 0)->f), id }
static const CDDVFieldDesc kSlotFields[7] = {
	F(server, serverLen, cddvFieldServer),
	F(path, pathLen, cddvFieldPath),
	F(uuid, uuidLen, cddvFieldUuid),
	F(user, userLen, cddvFieldUser),
	F(alias, aliasLen, cddvFieldAlias),
	F(googleClientId, googleClientIdLen, cddvFieldGoogleClientId),
	F(googleClientSecret, googleClientSecretLen, cddvFieldGoogleClientSecret)
};
#undef F

static UInt16 rdLen(const UInt8 *s, UInt16 off) SEC004;
static UInt16 rdLen(const UInt8 *s, UInt16 off) { return *(const UInt16 *) (s + off); }
static void wrLen(UInt8 *s, UInt16 off, UInt16 v) SEC004;
static void wrLen(UInt8 *s, UInt16 off, UInt16 v) { *(UInt16 *) (s + off) = v; }

static Boolean RelayIsEnc(const UInt8 *rec, UInt32 size) SEC004;
static Boolean RelayIsEnc(const UInt8 *rec, UInt32 size)
{
	const CDDVPrefsType *p = (const CDDVPrefsType *) rec;
	UInt16 i;
	if (size < sizeof(CDDVPrefsType)) return false;
	for (i = 0; i < cddvMaxSlots; i++)
		if (p->slots[i].secretLen || p->slots[i].fieldsEncrypted) return true;
	return false;
}

/* fromK NULL: fields are plaintext (SET) -- encrypt them. toK NULL: removal --
 * fields to plaintext, secrets cleared. wipe: no key -- clear everything
 * encrypted. The whole record is rebuilt in RAM and written in one go. */
static Err RelayRekeyRecord(DmOpenRef db, UInt16 index, const UInt8 *fromK, const UInt8 *toK, Boolean wipe) SEC004;
static Err RelayRekeyRecord(DmOpenRef db, UInt16 index, const UInt8 *fromK, const UInt8 *toK, Boolean wipe)
{
	MemHandle h = DmGetRecord(db, index);
	UInt8 *rec;
	CDDVPrefsType *p;
	UInt32 size;
	UInt16 i, f;

	if (h == NULL) return dmErrIndexOutOfRange;
	rec = (UInt8 *) MemHandleLock(h);
	size = MemHandleSize(h);
	if (size < sizeof(CDDVPrefsType)) { MemHandleUnlock(h); DmReleaseRecord(db, index, false); return errNone; }
	p = (CDDVPrefsType *) MemPtrNew(sizeof(CDDVPrefsType));
	if (p == NULL) { MemHandleUnlock(h); DmReleaseRecord(db, index, false); return memErrNotEnoughSpace; }
	MemMove(p, rec, sizeof(CDDVPrefsType));

	for (i = 0; i < cddvMaxSlots; i++) {
		CDDVSlot *s = &p->slots[i];
		UInt8 *sb = (UInt8 *) s;

		/* the secret */
		if (s->secretLen > sizeof(s->secret)) s->secretLen = sizeof(s->secret);
		if (s->secretLen && fromK && toK) {
			EncCrypt(fromK, s->secret, s->secretLen, 0, (UInt32) i);
			EncCrypt(toK, s->secret, s->secretLen, 0, (UInt32) i);
		} else if (s->secretLen && !toK) {		/* removal / forget: nowhere to keep it */
			MemSet(s->secret, sizeof(s->secret), 0);
			s->secretLen = 0;
		}

		/* the settings fields */
		for (f = 0; f < 7; f++) {
			const CDDVFieldDesc *d = &kSlotFields[f];
			UInt8 *buf = sb + d->dataOff;
			UInt16 len = rdLen(sb, d->lenOff);

			if (s->fieldsEncrypted) {
				if (len > d->cap - 1) len = (UInt16) (d->cap - 1);
				if (wipe) {
					MemSet(buf, d->cap, 0); len = 0;
				} else if (len) {
					if (fromK) EncCrypt(fromK, buf, len, (UInt32) d->field, (UInt32) i);
					if (toK)   EncCrypt(toK,   buf, len, (UInt32) d->field, (UInt32) i);
					else { MemSet(buf + len, (Int32) d->cap - len, 0); }	/* plaintext: NUL-terminate */
				}
				wrLen(sb, d->lenOff, len);
			} else if (toK && !fromK) {			/* SET: plaintext -> encrypted */
				len = (UInt16) StrLen((const char *) buf);
				if (len > d->cap - 1) len = (UInt16) (d->cap - 1);
				if (len) EncCrypt(toK, buf, len, (UInt32) d->field, (UInt32) i);
				wrLen(sb, d->lenOff, len);
			}
		}
		if (wipe || (fromK && !toK)) s->fieldsEncrypted = false;	/* now plaintext/empty */
		else if (toK && !fromK) s->fieldsEncrypted = true;		/* SET encrypted them */
		/* a change/conversion leaves the flag as it was */
	}

	DmWrite(rec, 0, p, sizeof(CDDVPrefsType));
	MemSet(p, sizeof(CDDVPrefsType), 0);			/* may hold plaintext */
	MemPtrFree(p);
	MemHandleUnlock(h);
	DmReleaseRecord(db, index, true);
	return errNone;
}

static Err RelayRekeyCb(DmOpenRef db, UInt16 index, const UInt8 *fromK, const UInt8 *toK) SEC004;
static Err RelayRekeyCb(DmOpenRef db, UInt16 index, const UInt8 *fromK, const UInt8 *toK)
{
	return RelayRekeyRecord(db, index, fromK, toK, false);
}

/* ---- DateBook35DAV (events) --------------------------------------------- */

static Boolean EvIsEnc(const UInt8 *rec, UInt32 size) SEC004;
static Boolean EvIsEnc(const UInt8 *rec, UInt32 size)
{
	(void) rec;
	return (Boolean) (size >= sizeof(CDDVEvent));	/* every text field is ciphertext */
}

static Err EvRekeyRecord(DmOpenRef db, UInt16 index, const UInt8 *fromK, const UInt8 *toK) SEC004;
static Err EvRekeyRecord(DmOpenRef db, UInt16 index, const UInt8 *fromK, const UInt8 *toK)
{
	MemHandle h;
	CDDVEvent *ev;
	UInt8 *rec;
	UInt32 uid = 0;

	if (!fromK || !toK) return errNone;			/* only re-keyed (see the file header) */
	h = DmGetRecord(db, index);
	if (h == NULL) return dmErrIndexOutOfRange;
	ev = (CDDVEvent *) MemPtrNew(sizeof(CDDVEvent));
	if (ev == NULL) { DmReleaseRecord(db, index, false); return memErrNotEnoughSpace; }
	rec = (UInt8 *) MemHandleLock(h);
	MemMove(ev, rec, sizeof(CDDVEvent));
	DmRecordInfo(db, index, NULL, &uid, NULL);
#define RK(fld, lenf, idx) \
	if (ev->lenf && ev->lenf < sizeof(ev->fld)) { \
		EncCrypt(fromK, (UInt8 *) ev->fld, ev->lenf, uid, idx); \
		EncCrypt(toK,   (UInt8 *) ev->fld, ev->lenf, uid, idx); }
	RK(uid, uidLen, cddvEvFieldUid)
	RK(alias, aliasLen, cddvEvFieldAlias)
	RK(title, titleLen, cddvEvFieldTitle)
	RK(location, locationLen, cddvEvFieldLocation)
	RK(notes, notesLen, cddvEvFieldNotes)
#undef RK
	DmWrite(rec, 0, ev, sizeof(CDDVEvent));
	MemSet(ev, sizeof(CDDVEvent), 0);
	MemPtrFree(ev);
	MemHandleUnlock(h);
	DmReleaseRecord(db, index, true);
	return errNone;
}

/* ---- Address35DAV_<n> (contacts) ---------------------------------------- */

static UInt16 ConStrAt(const UInt8 *p, UInt16 len, UInt16 pos) SEC004;
static UInt16 ConStrAt(const UInt8 *p, UInt16 len, UInt16 pos)
{
	UInt16 n;
	if ((UInt32) pos + 2 > len) return 0xFFFF;
	n = (UInt16) (((UInt16) p[pos] << 8) | p[pos + 1]);
	if ((UInt32) pos + 2 + n > len) return 0xFFFF;
	return (UInt16) (pos + 2 + n);
}

/* Offset of the text bytes in contact record r, length in *tl; 0xFFFF if
 * malformed. The same walk as Address35's DavTextAt (the readers' parser). */
static UInt16 ConTextAt(const UInt8 *r, UInt16 len, UInt16 *tl) SEC004;
static UInt16 ConTextAt(const UInt8 *r, UInt16 len, UInt16 *tl)
{
	UInt16 pos, k, nd, nc;
	if (len < 3 || r[0] != cddvContactRecVersion) return 0xFFFF;
	pos = (UInt16) (3 + r[2]);
	for (k = 0; k < 3; k++) {
		if (pos >= len) return 0xFFFF;
		pos = (UInt16) (pos + 1 + r[pos]);
	}
	for (k = 0; k < 3 && pos != 0xFFFF; k++) pos = ConStrAt(r, len, pos);	/* uid, display, sort */
	if (pos == 0xFFFF || pos >= len) return 0xFFFF;
	nd = r[pos++];
	for (k = 0; k < nd && pos != 0xFFFF; k++) {
		if ((UInt32) pos + 5 > len) return 0xFFFF;
		pos = ConStrAt(r, len, (UInt16) (pos + 5));
	}
	if (pos == 0xFFFF || pos >= len) return 0xFFFF;
	nc = r[pos++];
	for (k = 0; k < nc && pos != 0xFFFF; k++) pos = ConStrAt(r, len, pos);
	if (pos == 0xFFFF || (UInt32) pos + 2 > len) return 0xFFFF;
	*tl = (UInt16) (((UInt16) r[pos] << 8) | r[pos + 1]);
	if ((UInt32) pos + 2 + *tl > len) return 0xFFFF;
	return (UInt16) (pos + 2);
}

static Boolean ConIsEnc(const UInt8 *rec, UInt32 size) SEC004;
static Boolean ConIsEnc(const UInt8 *rec, UInt32 size)
{
	return (Boolean) (size >= 3 && rec[0] == cddvContactRecVersion &&
		(rec[1] & (cddvContactFlagEncText | cddvContactFlagEncList)));
}

static Err ConRekeyRecord(DmOpenRef db, UInt16 index, const UInt8 *fromK, const UInt8 *toK) SEC004;
static Err ConRekeyRecord(DmOpenRef db, UInt16 index, const UInt8 *fromK, const UInt8 *toK)
{
	MemHandle h;
	UInt8 *rec, *buf;
	UInt16 len, tl = 0, to, ll;
	UInt32 uid = 0;

	if (!fromK || !toK) return errNone;			/* only re-keyed (see the file header) */
	h = DmGetRecord(db, index);
	if (h == NULL) return dmErrIndexOutOfRange;
	rec = (UInt8 *) MemHandleLock(h);
	len = (UInt16) MemHandleSize(h);
	buf = (UInt8 *) MemPtrNew(len);
	if (buf == NULL) { MemHandleUnlock(h); DmReleaseRecord(db, index, false); return memErrNotEnoughSpace; }
	MemMove(buf, rec, len);
	DmRecordInfo(db, index, NULL, &uid, NULL);
	ll = buf[2];
	if ((buf[1] & cddvContactFlagEncList) && ll && (UInt32) 3 + ll <= len) {
		EncCrypt(fromK, buf + 3, ll, cddvContactListNonce, uid);
		EncCrypt(toK,   buf + 3, ll, cddvContactListNonce, uid);
	}
	if ((buf[1] & cddvContactFlagEncText) && (to = ConTextAt(buf, len, &tl)) != 0xFFFF && tl) {
		EncCrypt(fromK, buf + to, tl, cddvContactNonce, uid);
		EncCrypt(toK,   buf + to, tl, cddvContactNonce, uid);
	}
	DmWrite(rec, 0, buf, len);
	MemSet(buf, len, 0);
	MemPtrFree(buf);
	MemHandleUnlock(h);
	DmReleaseRecord(db, index, true);
	return errNone;
}

/* The alias in a contacts database's AppInfo (after the standard block):
 * u8 version, u8 slotType, u8 stale, u8 aliasLen, alias (ALIA, slot). */
static void ConRekeyAlias(LocalID id, UInt8 slot, const UInt8 *fromK, const UInt8 *toK) SEC004;
static void ConRekeyAlias(LocalID id, UInt8 slot, const UInt8 *fromK, const UInt8 *toK)
{
	LocalID ai = 0;
	UInt8 *p, a[cddvAliasLen];
	UInt16 al;

	if (DmDatabaseInfo(0, id, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ai, NULL, NULL, NULL) != errNone || !ai)
		return;
	p = (UInt8 *) MemLocalIDToLockedPtr(ai, 0);
	if (p == NULL) return;
	al = p[sizeof(AppInfoType) + 3];
	if (al && al <= cddvAliasLen) {
		MemMove(a, p + sizeof(AppInfoType) + 4, al);
		EncCrypt(fromK, a, al, cddvContactAliasNonce, slot);
		EncCrypt(toK,   a, al, cddvContactAliasNonce, slot);
		DmWrite(p, sizeof(AppInfoType) + 4, a, al);
		MemSet(a, sizeof(a), 0);
	}
	MemPtrUnlock(p);
}

static void ConDbName(UInt8 slot, char *name) SEC004;
static void ConDbName(UInt8 slot, char *name)
{
	StrPrintF(name, "%s%d", cddvContactDbPrefix, (Int16) (slot + 1));
}

/* ---- handler ------------------------------------------------------------ */

static UInt16 RekeyNamedDb(const char *name, UInt32 storeId, EncRekeyParam *p, UInt16 ordinal,
	EncRecIsEncFn isEnc, EncRecCryptFn crypt, Boolean isContacts, UInt8 slot) SEC004;
static UInt16 RekeyNamedDb(const char *name, UInt32 storeId, EncRekeyParam *p, UInt16 ordinal,
	EncRecIsEncFn isEnc, EncRecCryptFn crypt, Boolean isContacts, UInt8 slot)
{
	LocalID id = DmFindDatabase(0, name);
	DmOpenRef db;
	Boolean wasMarked;
	UInt16 r;

	if (!id) return kRekeyResultDone;
	if (p->op == kRekeyRemove || p->op == kRekeyWipe) {	/* a cache nothing can read now */
		DmDeleteDatabase(0, id);
		return kRekeyResultDone;
	}
	if (p->op == kRekeySet) {					/* written encrypted already; just mark */
		EncDbIdSetStoreKeys(id);
		return kRekeyResultDone;
	}
	db = DmOpenDatabase(0, id, dmModeReadWrite);
	if (!db) return kRekeyResultFailed;
	wasMarked = EncDbHasStoreKeys(db);
	r = EncRekeyDb(db, storeId, p, ordinal, appCreator, isEnc, crypt);
	DmCloseDatabase(db);
	if (r == kRekeyResultDone && isContacts && !(p->op == kRekeyConvert && wasMarked)) {
		UInt8 kf[ENC_KEY_BYTES], kt[ENC_KEY_BYTES];
		Boolean hf, ht;
		EncRekeyKeys(p, storeId, wasMarked, kf, &hf, kt, &ht);
		if (hf && ht) ConRekeyAlias(id, slot, kf, kt);
		MemSet(kf, sizeof(kf), 0); MemSet(kt, sizeof(kt), 0);
	}
	return r;
}

void CDDVRekey(EncRekeyParam *p)
{
	LocalID id;
	UInt16 r = kRekeyResultDone;
	UInt8 slot;
	char name[dmDBNameLength];

	/* 1. CalDAVRelayDB (settings + secrets) */
	id = DmFindDatabase(0, cddvRelayDBName);
	if (id) {
		DmOpenRef db = DmOpenDatabase(0, id, dmModeReadWrite);
		if (!db) { p->result = kRekeyResultFailed; return; }
		if (p->op == kRekeyWipe) {
			UInt16 i, n = DmNumRecords(db);
			for (i = 0; i < n; i++) RelayRekeyRecord(db, i, NULL, NULL, true);
			EncDbSetStoreKeys(db);
		} else
			r = EncRekeyDb(db, ENC_STORE_CDDV, p, 0, appCreator, RelayIsEnc, RelayRekeyCb);
		DmCloseDatabase(db);
		if (r != kRekeyResultDone) { p->result = r; return; }
	}

	/* 2. DateBook35DAV (events) */
	r = RekeyNamedDb(cddvDavDbName, ENC_STORE_DAVEV, p, 1, EvIsEnc, EvRekeyRecord, false, 0);
	if (r != kRekeyResultDone) { p->result = r; return; }

	/* 3. Address35DAV_<n> (contacts) */
	for (slot = 0; slot < cddvMaxSlots; slot++) {
		ConDbName(slot, name);
		r = RekeyNamedDb(name, ENC_STORE_DAVCON + slot, p, (UInt16) (2 + slot),
			ConIsEnc, ConRekeyRecord, true, slot);
		if (r != kRekeyResultDone) { p->result = r; return; }
	}
	if (p->op == kRekeyRemove || p->op == kRekeyWipe) {	/* its index of deleted contacts */
		id = DmFindDatabase(0, cddvContactBdaName);
		if (id) DmDeleteDatabase(0, id);
	}
	p->result = kRekeyResultDone;
}

/* Launch gate: every CalDAV database that exists must already be in key
 * scheme 2 before this app reads or syncs (a sync copies new records into
 * the events database). False = ask Sec35 to convert first. */
Boolean CDDVStoresConverted(void)
{
	UInt8 slot;
	char name[dmDBNameLength];
	LocalID id;

	if ((id = DmFindDatabase(0, cddvRelayDBName)) != 0 && !EncDbIdHasStoreKeys(id)) return false;
	if ((id = DmFindDatabase(0, cddvDavDbName)) != 0 && !EncDbIdHasStoreKeys(id)) return false;
	for (slot = 0; slot < cddvMaxSlots; slot++) {
		ConDbName(slot, name);
		if ((id = DmFindDatabase(0, name)) != 0 && !EncDbIdHasStoreKeys(id)) return false;
	}
	return true;
}
