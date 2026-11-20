/*
 * encrekey.c - see encrekey.h.
 */
#include <PalmOS.h>
#include "encrekey.h"

#define kCkptPrefId		0x7E35		/* unsaved preference of the app */
#define kCkptVersion	1

typedef struct {
	UInt32	opSerial;
	UInt16	dbOrdinal;
	UInt16	done;			/* 1 = this database is finished for opSerial */
	UInt32	storeId;
	UInt32	uid;			/* record being converted ...                 */
	UInt32	hashBefore;		/* ... and its contents' hash before          */
} EncRekeyCkpt;

/* NO 32-bit multiply or divide anywhere in this file: it runs from a code
 * segment, and libgcc's __mulsi3/__udivsi3 live in main, unreachable from
 * there (the multiseg-libgcc rule, see a35_seg.h). 16-bit ops are native. */

/* A 32-bit fingerprint of a record: CRC-16 (OS) in the high half, a
 * Fletcher-16 in the low half. Converting a record re-encrypts its text, which
 * changes both. */
static UInt32 RecHash (const UInt8* p, UInt32 n) ENCREKEY_SECTION;
static UInt32 RecHash (const UInt8* p, UInt32 n)
{
	UInt16	s1 = 0, s2 = 0, crc;
	UInt32	i;

	for (i = 0; i < n; i++)
		{
		s1 += p[i]; if (s1 >= 255) s1 -= 255;
		s2 += s1;   if (s2 >= 255) s2 -= 255;
		}
	crc = Crc16CalcBlock ((void*) p, (UInt16) n, (UInt16) n);
	return ((UInt32) crc << 16) | (UInt32) ((s2 << 8) | s1);
}

static UInt32 RecHashAt (DmOpenRef db, UInt16 index) ENCREKEY_SECTION;
static UInt32 RecHashAt (DmOpenRef db, UInt16 index)
{
	MemHandle	h = DmQueryRecord (db, index);
	UInt32		r;

	if (! h) return 0;
	r = RecHash ((const UInt8*) MemHandleLock (h), MemHandleSize (h));
	MemHandleUnlock (h);
	return r;
}

static Boolean CkptGet (UInt32 creator, EncRekeyCkpt* c) ENCREKEY_SECTION;
static Boolean CkptGet (UInt32 creator, EncRekeyCkpt* c)
{
	UInt16 sz = sizeof (*c);
	if (PrefGetAppPreferences (creator, kCkptPrefId, c, &sz, false) == noPreferenceFound
			|| sz != sizeof (*c))
		return false;
	return true;
}

static void CkptPut (UInt32 creator, const EncRekeyCkpt* c) ENCREKEY_SECTION;
static void CkptPut (UInt32 creator, const EncRekeyCkpt* c)
{
	PrefSetAppPreferences (creator, kCkptPrefId, kCkptVersion, c, sizeof (*c), false);
}

Boolean EncRekeyBlobBegin (const EncRekeyParam* p, UInt16 dbOrdinal, UInt32 ckptCreator,
                           const void* blob, UInt32 size)
{
	EncRekeyCkpt	c;
	UInt32			h = RecHash ((const UInt8*) blob, size);

	if (! CkptGet (ckptCreator, &c)) MemSet (&c, sizeof (c), 0);
	if (c.opSerial == p->opSerial)
		{
		if (c.dbOrdinal > dbOrdinal) return false;			/* an earlier store: finished */
		if (c.dbOrdinal == dbOrdinal)
			{
			if (c.done) return false;
			if (c.uid == 1 && c.hashBefore != h)			/* written before the reset */
				{ c.done = 1; CkptPut (ckptCreator, &c); return false; }
			}
		}
	MemSet (&c, sizeof (c), 0);
	c.opSerial = p->opSerial; c.dbOrdinal = dbOrdinal;
	c.uid = 1;					/* "a block is in progress" */
	c.hashBefore = h;
	CkptPut (ckptCreator, &c);
	return true;
}

void EncRekeyBlobEnd (const EncRekeyParam* p, UInt16 dbOrdinal, UInt32 ckptCreator)
{
	EncRekeyCkpt c;
	MemSet (&c, sizeof (c), 0);
	c.opSerial = p->opSerial; c.dbOrdinal = dbOrdinal; c.uid = 1; c.done = 1;
	CkptPut (ckptCreator, &c);
}

void EncRekeyClearCheckpoint (UInt32 ckptCreator)
{
	EncRekeyCkpt c;
	MemSet (&c, sizeof (c), 0);
	CkptPut (ckptCreator, &c);
}

void EncRekeyKeys (const EncRekeyParam* p, UInt32 storeId, Boolean marked,
                   UInt8 fromKey[ENC_KEY_BYTES], Boolean* haveFrom,
                   UInt8 toKey[ENC_KEY_BYTES], Boolean* haveTo)
{
	*haveFrom = *haveTo = false;
	switch (p->op) {
	case kRekeyChange: case kRekeyRemove: case kRekeyConvert:
		/* scheme 2 data is under the old store key; scheme 1 data under the
		 * Sec35 key itself */
		if (marked) EncDeriveStoreKey (p->oldKey, storeId, fromKey);
		else        MemMove (fromKey, p->oldKey, ENC_KEY_BYTES);
		*haveFrom = true;
		break;
	}
	switch (p->op) {
	case kRekeySet: case kRekeyChange: case kRekeyConvert:
		EncDeriveStoreKey (p->newKey, storeId, toKey);
		*haveTo = true;
		break;
	}
}

/* Same rules as Sec35's ProgressBar (see its comment): a long synchronous pass
 * must reset the auto-off timer (a device that sleeps mid-pass crashed in
 * Address35) at most every 15 s, and drain -- never dispatch -- the event
 * queue. The rekey handler runs in a new-globals sublaunch, so a static is
 * fine here. */
static UInt32 gRekeyLastAutoOff;

void EncRekeyProgress (const EncRekeyParam* p, UInt32 done, UInt32 total)
{
	RectangleType	r = p->progress;
	Coord			w;
	UInt32			now = TimGetTicks ();
	EventType		ev;

	if (now - gRekeyLastAutoOff >= (UInt32) SysTicksPerSecond () * 15)
		{ EvtResetAutoOffTimer (); gRekeyLastAutoOff = now; }
	while (EvtGetEvent (&ev, 0), ev.eType != nilEvent) { /* drain, don't dispatch */ }

	if (r.extent.x < 4 || r.extent.y < 3 || r.extent.x > 160) return;
	if (total == 0) total = 1;
	if (done > total) done = total;
	while (total > 255) { total >>= 1; done >>= 1; }	/* 16-bit math only (see top) */
	if (total == 0) total = 1;
	WinDrawRectangleFrame (simpleFrame, &r);
	r.topLeft.x++; r.topLeft.y++; r.extent.x -= 2; r.extent.y -= 2;
	w = (Coord) ((UInt16) ((UInt16) r.extent.x * (UInt16) done) / (UInt16) total);
	if (w > 0) { r.extent.x = w; WinDrawRectangle (&r, 0); }
}

UInt16 EncRekeyDb (DmOpenRef db, UInt32 storeId, const EncRekeyParam* p,
                   UInt16 dbOrdinal, UInt32 ckptCreator,
                   EncRecIsEncFn isEnc, EncRecCryptFn crypt)
{
	Boolean			marked = EncDbHasStoreKeys (db);
	UInt8			kFrom[ENC_KEY_BYTES], kTo[ENC_KEY_BYTES];
	Boolean			haveFrom, haveTo;
	EncRekeyCkpt	c;
	UInt16			i, n, start = 0;
	UInt16			result = kRekeyResultDone;

	if (! db) return kRekeyResultDone;
	n = DmNumRecords (db);
	MemSet (&c, sizeof (c), 0);

	/* Already done for this very operation (resumed after a reset)? */
	if (! CkptGet (ckptCreator, &c)) MemSet (&c, sizeof (c), 0);
	if (c.opSerial == p->opSerial && c.dbOrdinal > dbOrdinal)
		return kRekeyResultDone;				/* an earlier database of this op: finished */
	if (c.opSerial == p->opSerial && c.dbOrdinal == dbOrdinal && c.storeId == storeId)
		{
		if (c.done) return kRekeyResultDone;
		/* The checkpointed record: converted already if its contents changed. */
		if (c.uid && DmFindRecordByID (db, c.uid, &i) == errNone)
			start = (RecHashAt (db, i) == c.hashBefore) ? i : (UInt16)(i + 1);
		}
	else
		{
		MemSet (&c, sizeof (c), 0);
		c.opSerial = p->opSerial; c.dbOrdinal = dbOrdinal; c.storeId = storeId;
		}

	if (p->op == kRekeyConvert && marked)		/* nothing to convert */
		goto finished;

	if (p->op == kRekeyWipe)					/* no key: remove what is encrypted */
		{
		for (i = n; i > 0; )
			{
			MemHandle	h;
			Boolean		enc = false;
			i--;
			EncRekeyProgress (p, (UInt32)(n - i), n);
			h = DmQueryRecord (db, i);
			if (h)
				{
				enc = isEnc ((const UInt8*) MemHandleLock (h), MemHandleSize (h));
				MemHandleUnlock (h);
				}
			if (enc) DmRemoveRecord (db, i);
			}
		goto finished;
		}

	EncRekeyKeys (p, storeId, marked, kFrom, &haveFrom, kTo, &haveTo);

	for (i = start; i < n; i++)
		{
		MemHandle	h;
		Boolean		enc, act;
		UInt32		uid = 0;
		Err			e;

		EncRekeyProgress (p, (UInt32)(i + 1), n);
		h = DmQueryRecord (db, i);
		if (! h) continue;
		enc = isEnc ((const UInt8*) MemHandleLock (h), MemHandleSize (h));
		MemHandleUnlock (h);
		/* SET acts on plaintext records; every other operation on encrypted ones */
		act = (p->op == kRekeySet) ? (Boolean) ! enc : enc;
		if (! act) continue;

		DmRecordInfo (db, i, NULL, &uid, NULL);
		c.uid = uid;
		c.hashBefore = RecHashAt (db, i);
		c.done = 0;
		CkptPut (ckptCreator, &c);

		e = crypt (db, i, haveFrom ? kFrom : NULL, haveTo ? kTo : NULL);
		if (e == memErrNotEnoughSpace) { result = kRekeyResultLowMem; break; }
		if (e != errNone)              { result = kRekeyResultFailed; break; }
		}

	MemSet (kFrom, sizeof (kFrom), 0);
	MemSet (kTo, sizeof (kTo), 0);
	if (result != kRekeyResultDone) return result;

finished:
	EncDbSetStoreKeys (db);
	c.done = 1;
	CkptPut (ckptCreator, &c);
	EncRekeyProgress (p, 1, 1);
	return kRekeyResultDone;
}
