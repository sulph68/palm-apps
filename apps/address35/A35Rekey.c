/*
 * A35Rekey.c - Address35's kSecLaunchRekey handler (see enccore.h, encrekey.h).
 *
 * Sec35 sublaunches Address35 (new globals) when the Sec35 password is set,
 * changed, removed or forgotten, and once for the key scheme 1 -> 2
 * conversion. Address35 re-encrypts only its own Address35DB; the plaintext
 * inbox (Address35DBPlain) holds nothing encrypted. Record format as
 * AddressDB35.c: misc bit a35MiscEncrypted, header a35HdrSize, encrypted field
 * types per A35FieldEncrypted, nonce (recordUniqueID, fieldIndex).
 */
#include <PalmOS.h>
#include "Main.h"
#include "enccore.h"
#include "encrekey.h"

#define a35HdrSize	10		/* packed-record header (AddressDB35.c, private there) */

static UInt16 rd16(const UInt8 *p) SEC002;
static UInt16 rd16(const UInt8 *p) { return (UInt16) (((UInt16) p[0] << 8) | p[1]); }

static Boolean A35RekeyIsEnc(const UInt8 *rec, UInt32 size) SEC002;
static Boolean A35RekeyIsEnc(const UInt8 *rec, UInt32 size)
{
	return (Boolean) (size >= 2 && (rec[1] & a35MiscEncrypted));
}

/* One record, in place (field lengths never change). A record whose field
 * table does not fit its size is left untouched, as Sec35 always did. */
static Err A35RekeyRecord(DmOpenRef db, UInt16 index, const UInt8 *fromK, const UInt8 *toK) SEC002;
static Err A35RekeyRecord(DmOpenRef db, UInt16 index, const UInt8 *fromK, const UInt8 *toK)
{
	MemHandle h;
	UInt8 *rec, *tmp;
	Int32 size, off, chk;
	UInt16 count, f;
	UInt32 uid = 0;

	h = DmGetRecord(db, index);
	if (h == NULL) return dmErrIndexOutOfRange;
	rec = (UInt8 *) MemHandleLock(h);
	size = MemHandleSize(h);
	tmp = (UInt8 *) MemPtrNew(size);
	if (tmp == NULL) {
		MemHandleUnlock(h);
		DmReleaseRecord(db, index, false);
		return memErrNotEnoughSpace;
	}
	MemMove(tmp, rec, size);
	DmRecordInfo(db, index, NULL, &uid, NULL);

	count = (size >= a35HdrSize) ? rd16(tmp + 2) : 0;
	chk = a35HdrSize;
	for (f = 0; f < count; f++) {
		if (chk + 4 > size || chk + 4 + (Int32) rd16(tmp + chk + 2) > size) break;
		chk += 4 + rd16(tmp + chk + 2);
	}
	if (size >= a35HdrSize && f == count) {
		off = a35HdrSize;
		for (f = 0; f < count; f++) {
			UInt8 t = tmp[off];
			UInt16 ln = rd16(tmp + off + 2);
			if (ln && A35FieldEncrypted(t)) {
				if (fromK) EncCrypt(fromK, tmp + off + 4, ln, uid, f);	/* -> plaintext */
				if (toK)   EncCrypt(toK,   tmp + off + 4, ln, uid, f);	/* -> cipher    */
			}
			off += 4 + ln;
		}
		if (toK) tmp[1] |= a35MiscEncrypted;
		else     tmp[1] &= (UInt8) ~a35MiscEncrypted;
		DmWrite(rec, 0, tmp, size);
	}
	MemSet(tmp, size, 0);				/* may hold plaintext */
	MemPtrFree(tmp);
	MemHandleUnlock(h);
	DmReleaseRecord(db, index, true);
	return errNone;
}

void A35Rekey(EncRekeyParam *p)
{
	DmOpenRef db = NULL;

	p->result = kRekeyResultDone;
	if (A35GetDatabase(&db, dmModeReadWrite) != errNone || db == NULL)
		return;									/* no address book: nothing to do */
	p->result = EncRekeyDb(db, ENC_STORE_A35, p, 0, a35Creator,
		A35RekeyIsEnc, A35RekeyRecord);
	DmCloseDatabase(db);
}
