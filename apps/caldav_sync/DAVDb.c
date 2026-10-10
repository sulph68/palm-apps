/*
 * DAVDb.c - DateBook35DAV.pdb (creator 'CDDV', matching the app that
 * writes it). Per-SLOT rebuild, not a whole-DB swap - see DAVDb.h for why
 * a whole-DB swap is wrong for a per-slot sync model.
 *
 * New events are staged into a separate temp database (cddvDavDbTmpName)
 * for the whole sync: the real database is never opened for writing until
 * DAVDbCommitSlotRebuild, which only runs on full success - it deletes the
 * slot's old records and copies in the staged new ones as one unit, then
 * discards the temp DB. Abort just deletes the temp DB, so a failed or
 * interrupted sync leaves every existing record untouched.
 *
 * One record per event occurrence (not one big blob) - lets Datebook35's
 * own reader use ordinary DmQueryRecord/per-record iteration.
 */
#include "DAVDb.h"
#include "CDDVSec35.h"

/* Encrypts one field of ev in place using this record's own real-database
 * uniqueID as the nonce - see DAVDb.h's own comment on why this has to
 * happen at commit time (final uniqueID), not stage time. Sec35 is
 * guaranteed unlocked for the whole duration of a sync (SyncEngineRunOne's
 * own precondition check), so CDDVCryptBytes failing here would only be a
 * genuine TTL race - fails safe by leaving *lenOut at 0 (blank field) and
 * wiping any plaintext already in buf, rather than ever writing real
 * plaintext into a record everything else treats as encrypted. */
static void DAVDbEncryptField(UInt32 uid, UInt8 fieldIdx, char *buf, UInt16 bufCap, UInt16 *lenOut)
{
	UInt16 len = (UInt16) StrLen(buf);
	/* bufCap-1, not bufCap - the decrypting reader always writes a NUL
	 * terminator after the decrypted bytes, which needs 1 spare byte in the
	 * buffer even in the (should-never-happen, StrLen already guarantees
	 * this) worst case. */
	if (len > (UInt16) (bufCap - 1)) len = (UInt16) (bufCap - 1);
	if (len > 0 && CDDVCryptBytes(uid, (UInt32) fieldIdx, (UInt8 *) buf, len)) {
		*lenOut = len;
	} else {
		*lenOut = 0;
		if (len > 0) MemSet(buf, bufCap, 0);
	}
}

/* Staging area only - always deleted by the end of Commit or Abort; a
 * stale one left behind by a crash mid-sync self-heals via
 * DAVDbBeginSlotRebuild's own delete-if-exists below. */
#define cddvDavDbTmpName "DateBook35DAV_tmp"

static DmOpenRef gRebuildDB = NULL;

/* ---- lightweight busy "toast" for Recompute Times - text-only, no form
 * resource, updates in place as the count changes. Same screen-direct-draw
 * technique as Address35's A35BusyBegin/A35BusyEnd, this app's own copy
 * (not a cross-app call - different binary). ---- */
#define cddvToastPadX  10
#define cddvToastH     24

static WinHandle     gToastSaved;
static RectangleType gToastSaveR;

static void ToastDraw(const char *text)
{
	RectangleType r;
	WinHandle prevDraw;
	Err err;
	Int16 w, len = (Int16) StrLen(text);
	FontID prevFont;

	prevDraw = WinGetDrawWindow();
	WinSetDrawWindow(WinGetDisplayWindow());

	/* Fixed box width, sized for the longest progress text: the screen
	 * under the toast is saved once (below), so the box must not grow when
	 * "Recomputing..." becomes the wider "Recomputing 17/250" - the extra
	 * width was never restored and stayed on screen. */
	prevFont = FntSetFont(boldFont);
	w = FntCharsWidth((Char *) text, len);
	r.extent.x = (Int16) (FntCharsWidth("Recomputing 00000/00000", 23) + 2 * cddvToastPadX);
	r.extent.y = cddvToastH;
	r.topLeft.x = (Int16) ((160 - r.extent.x) / 2);
	r.topLeft.y = (Int16) ((160 - r.extent.y) / 2);

	if (!gToastSaved) {
		RectangleType saveR;
		saveR.topLeft.x = (Int16) (r.topLeft.x - 4);
		saveR.topLeft.y = (Int16) (r.topLeft.y - 4);
		saveR.extent.x = (Int16) (r.extent.x + 8);
		saveR.extent.y = (Int16) (r.extent.y + 8);
		gToastSaveR = saveR;
		gToastSaved = WinSaveBits(&gToastSaveR, &err);
	}
	WinEraseRectangle(&r, 3);
	WinDrawRectangleFrame(dialogFrame, &r);
	WinDrawChars((Char *) text, len, (Int16) (r.topLeft.x + (r.extent.x - w) / 2), r.topLeft.y + 6);
	FntSetFont(prevFont);

	WinSetDrawWindow(prevDraw);
}

static void ToastEnd(void)
{
	if (gToastSaved) {
		WinHandle prevDraw = WinGetDrawWindow();
		WinSetDrawWindow(WinGetDisplayWindow());
		WinRestoreBits(gToastSaved, gToastSaveR.topLeft.x, gToastSaveR.topLeft.y);
		WinSetDrawWindow(prevDraw);
		gToastSaved = NULL;
	}
}

/* ---- per-slot rebuild path (see DAVDb.h for why this is per-slot, not a
 * whole-database temp-then-rename swap) ---- */

static UInt8 gRebuildSlot;

SEC003 Err DAVDbBeginSlotRebuild(UInt8 slotIdx)
{
	LocalID tmpID;

	gRebuildSlot = slotIdx;

	/* Fresh empty staging area every time - delete any stale temp DB left
	 * behind by a prior crash/abort before recreating it. */
	tmpID = DmFindDatabase(0, cddvDavDbTmpName);
	if (tmpID != 0) DmDeleteDatabase(0, tmpID);

	if (DmCreateDatabase(0, cddvDavDbTmpName, appCreator, cddvDavDbType, false) != errNone)
		return dmErrMemError;
	tmpID = DmFindDatabase(0, cddvDavDbTmpName);
	if (tmpID == 0) return dmErrMemError;

	gRebuildDB = DmOpenDatabase(0, tmpID, dmModeReadWrite);
	if (!gRebuildDB) return dmErrMemError;

	/* The real database is not touched here - see file header. */
	return errNone;
}

SEC003 Err DAVDbAddEvent(const CDDVEvent *ev)
{
	UInt16 index;
	MemHandle h;
	void *raw;

	if (!gRebuildDB) return dmErrInvalidParam;

	index = dmMaxRecordIndex;
	h = DmNewRecord(gRebuildDB, &index, sizeof(CDDVEvent));
	if (!h) return dmErrMemError;

	raw = MemHandleLock(h);
	DmWrite(raw, 0, ev, sizeof(CDDVEvent));
	MemHandleUnlock(h);
	DmReleaseRecord(gRebuildDB, index, true);
	return errNone;
}

/* Keeps DateDAVGet's own per-query scan (Datebook35's DateDAV.c re-reads
 * this DB's raw records directly, can't link this app's DB layer) cheap
 * regardless of total event count: sorting by localStart once per commit
 * lets it binary-search to the right neighborhood and scan-with-early-exit
 * instead of touching every record every time. DmQuickSort handles all the
 * locking - the comparator below just gets raw record pointers. */
static Int16 DAVDbCompareByLocalStart(void *rec1, void *rec2, Int16 other,
	SortRecordInfoPtr r1SortInfo, SortRecordInfoPtr r2SortInfo, MemHandle appInfoH) SEC003;
static Int16 DAVDbCompareByLocalStart(void *rec1, void *rec2, Int16 other,
	SortRecordInfoPtr r1SortInfo, SortRecordInfoPtr r2SortInfo, MemHandle appInfoH)
{
	CDDVEvent *e1 = (CDDVEvent *) rec1;
	CDDVEvent *e2 = (CDDVEvent *) rec2;
	if (e1->localStart < e2->localStart) return -1;
	if (e1->localStart > e2->localStart) return 1;
	/* DmQuickSort isn't stable - break ties on the record's uniqueID so
	 * events starting together (e.g. two all-day events) keep one order
	 * across re-sorts instead of swapping on every Recompute Times. */
	return (Int16) MemCmp(r1SortInfo->uniqueID, r2SortInfo->uniqueID, 3);
}

SEC003 void DAVDbCommitSlotRebuild(void)
{
	DmOpenRef realDB;
	LocalID dbID, tmpID;
	UInt16 tmpTotal, i;

	if (!gRebuildDB) return;
	tmpID = DmFindDatabase(0, cddvDavDbTmpName);

	dbID = DmFindDatabase(0, cddvDavDbName);
	if (dbID == 0) {
		if (DmCreateDatabase(0, cddvDavDbName, appCreator, cddvDavDbType, false) != errNone) {
			DmCloseDatabase(gRebuildDB);
			gRebuildDB = NULL;
			if (tmpID != 0) DmDeleteDatabase(0, tmpID);
			return;
		}
		dbID = DmFindDatabase(0, cddvDavDbName);
	}
	realDB = DmOpenDatabase(0, dbID, dmModeReadWrite);
	if (!realDB) {
		DmCloseDatabase(gRebuildDB);
		gRebuildDB = NULL;
		if (tmpID != 0) DmDeleteDatabase(0, tmpID);
		return;
	}

	/* Only now - once the new data in the temp DB is confirmed to exist -
	 * delete this slot's old records from the real database. Iterate
	 * backwards, since DmRemoveRecord shifts every later index down by one. */
	{
		Int16 j;
		for (j = (Int16) DmNumRecords(realDB) - 1; j >= 0; j--) {
			MemHandle h = DmQueryRecord(realDB, (UInt16) j);
			CDDVEvent ev;
			void *raw;
			if (!h) continue;
			raw = MemHandleLock(h);
			MemMove(&ev, raw, sizeof(ev));
			MemHandleUnlock(h);
			if (ev.sourceSlot == gRebuildSlot) DmRemoveRecord(realDB, (UInt16) j);
		}
	}

	/* Copy every staged record from the temp DB into the real one. */
	tmpTotal = DmNumRecords(gRebuildDB);
	for (i = 0; i < tmpTotal; i++) {
		MemHandle srcH = DmQueryRecord(gRebuildDB, i);
		CDDVEvent ev;
		void *srcRaw;
		UInt16 index;
		MemHandle dstH;
		void *dstRaw;

		if (!srcH) continue;
		srcRaw = MemHandleLock(srcH);
		MemMove(&ev, srcRaw, sizeof(ev));
		MemHandleUnlock(srcH);

		index = dmMaxRecordIndex;
		dstH = DmNewRecord(realDB, &index, sizeof(CDDVEvent));
		if (!dstH) continue;

		{
			UInt16 attr;
			UInt32 recUid;
			DmRecordInfo(realDB, index, &attr, &recUid, NULL);
			DAVDbEncryptField(recUid, cddvEvFieldUid, ev.uid, sizeof(ev.uid), &ev.uidLen);
			DAVDbEncryptField(recUid, cddvEvFieldAlias, ev.alias, sizeof(ev.alias), &ev.aliasLen);
			DAVDbEncryptField(recUid, cddvEvFieldTitle, ev.title, sizeof(ev.title), &ev.titleLen);
			DAVDbEncryptField(recUid, cddvEvFieldLocation, ev.location, sizeof(ev.location), &ev.locationLen);
			DAVDbEncryptField(recUid, cddvEvFieldNotes, ev.notes, sizeof(ev.notes), &ev.notesLen);
		}

		dstRaw = MemHandleLock(dstH);
		DmWrite(dstRaw, 0, &ev, sizeof(CDDVEvent));
		MemHandleUnlock(dstH);
		DmReleaseRecord(realDB, index, true);
	}

	DmQuickSort(realDB, DAVDbCompareByLocalStart, 0);
	DmCloseDatabase(realDB);

	DmCloseDatabase(gRebuildDB);
	gRebuildDB = NULL;
	if (tmpID != 0) DmDeleteDatabase(0, tmpID);
}

SEC003 void DAVDbAbortSlotRebuild(void)
{
	/* The real database was never opened for writing during this sync -
	 * only the temp staging DB was touched, so discarding it here leaves
	 * every existing record in DateBook35DAV.pdb exactly as it was before
	 * the sync started. */
	LocalID tmpID;
	if (gRebuildDB) { DmCloseDatabase(gRebuildDB); gRebuildDB = NULL; }
	tmpID = DmFindDatabase(0, cddvDavDbTmpName);
	if (tmpID != 0) DmDeleteDatabase(0, tmpID);
}

SEC003 void DAVDbDeleteAll(void)
{
	LocalID dbID = DmFindDatabase(0, cddvDavDbName);
	if (dbID != 0) DmDeleteDatabase(0, dbID);
}

/* ---- Recompute Times: local-only, no network - re-derive localStart/
 * localEnd from the permanent utcStart/utcEnd anchor against the current
 * device timezone, via the same PrefGetPreference+TimUTCToTimeZone
 * sequence EraNTP.c uses. ---- */
SEC003 void DAVDbRecomputeLocalTimes(void)
{
	DmOpenRef db;
	LocalID dbID;
	UInt16 total, i;
	Int16 tz, dst;
	char msg[32];

	dbID = DmFindDatabase(0, cddvDavDbName);
	if (dbID == 0) return;
	db = DmOpenDatabase(0, dbID, dmModeReadWrite);
	if (!db) return;

	tz  = (Int16) PrefGetPreference(prefTimeZone);
	dst = (Int16) PrefGetPreference(prefDaylightSavingAdjustment);

	/* All-day events keep their date (see SyncEngine.c), so timed events
	 * shift relative to them - re-sorted by localStart after the loop. */
	total = DmNumRecords(db);
	ToastDraw("Recomputing...");

	for (i = 0; i < total; i++) {
		MemHandle h = DmQueryRecord(db, i);
		CDDVEvent ev;
		void *raw;

		if (!h) continue;
		raw = MemHandleLock(h);
		MemMove(&ev, raw, sizeof(ev));
		MemHandleUnlock(h);

		/* All-day: start/end are dates with no zone. Also corrects all-day
		 * records written by older builds, which shifted them by tz. */
		ev.localStart = ev.allDay ? ev.utcStart : TimUTCToTimeZone(ev.utcStart, tz, dst);
		ev.localEnd   = ev.allDay ? ev.utcEnd : TimUTCToTimeZone(ev.utcEnd, tz, dst);

		h = DmGetRecord(db, i);
		if (h) {
			raw = MemHandleLock(h);
			DmWrite(raw, 0, &ev, sizeof(ev));
			MemHandleUnlock(h);
			DmReleaseRecord(db, i, true);
		}

		if ((i & 0x0F) == 0) {	/* redraw every 16 records - avoid pointless flicker */
			StrPrintF(msg, "Recomputing %d/%d", i + 1, total);
			ToastDraw(msg);
			EvtResetAutoOffTimer();
		}
	}

	DmQuickSort(db, DAVDbCompareByLocalStart, 0);
	ToastEnd();
	DmCloseDatabase(db);
}
