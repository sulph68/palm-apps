/*
 * DAVDb.c - DateBook35DAV.pdb (creator 'CDDV', matching the app that
 * writes it). Per-SLOT rebuild, not a whole-DB swap - see DAVDb.h for why
 * a whole-DB swap is wrong for a per-slot sync model.
 *
 * New events are staged into a per-slot temp database (DAVDbTmpName) for
 * the whole sync: the real database is never opened for writing until
 * DAVDbCommitStaged, which only runs for slots that synced in full - it
 * deletes each staged slot's old records and copies in its new ones, then
 * discards the temp DBs. Abort just deletes the temp DB, so a failed or
 * interrupted sync leaves every existing record untouched.
 *
 * Staging and committing are separate steps so Sync All can download every
 * calendar first and only then do the slow part (encrypting each record,
 * sorting the database) after it has said BYE to WiRSa. Done between
 * calendars, that work could outlast WiRSa's wait for the next request.
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

/* Staging areas only, one per slot ("DateBook35DAV_t0".."_t7") - always
 * deleted by DAVDbCommitStaged or DAVDbAbortSlotRebuild. Stale ones left
 * behind by a crash mid-sync are deleted by the next DAVDbCommitStaged.
 * cddvDavDbOldTmpName is the single staging DB older builds used. */
#define cddvDavDbOldTmpName "DateBook35DAV_tmp"

static DmOpenRef gRebuildDB = NULL;
static UInt16 gStagedMask = 0;	/* bit n: slot n has a staged rebuild to commit */

static void DAVDbTmpName(UInt8 slotIdx, char *name)
{
	StrPrintF(name, "%s_t%d", cddvDavDbName, (Int16) slotIdx);
}

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

SEC003 void DAVDbToast(const char *text)
{
	ToastDraw(text);
}

SEC003 void DAVDbToastEnd(void)
{
	ToastEnd();
}

/* ---- per-slot rebuild path (see DAVDb.h for why this is per-slot, not a
 * whole-database temp-then-rename swap) ---- */

static UInt8 gRebuildSlot;

SEC003 Err DAVDbBeginSlotRebuild(UInt8 slotIdx)
{
	LocalID tmpID;
	char tmpName[dmDBNameLength];

	gRebuildSlot = slotIdx;
	DAVDbTmpName(slotIdx, tmpName);

	/* Fresh empty staging area every time - delete any stale temp DB left
	 * behind by a prior crash/abort, or an earlier sync of this slot in the
	 * same batch, before recreating it. */
	gStagedMask &= (UInt16) ~(1 << slotIdx);
	tmpID = DmFindDatabase(0, tmpName);
	if (tmpID != 0) DmDeleteDatabase(0, tmpID);

	if (DmCreateDatabase(0, tmpName, appCreator, cddvDavDbType, false) != errNone)
		return dmErrMemError;
	tmpID = DmFindDatabase(0, tmpName);
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

/* Closes the current slot's staging DB and keeps it for DAVDbCommitStaged. */
SEC003 void DAVDbStageSlotRebuild(void)
{
	if (!gRebuildDB) return;
	DmCloseDatabase(gRebuildDB);
	gRebuildDB = NULL;
	gStagedMask |= (UInt16) (1 << gRebuildSlot);
}

/* Stage + commit in one step, for callers outside a sync (Clear DB). */
SEC003 void DAVDbCommitSlotRebuild(void)
{
	DAVDbStageSlotRebuild();
	DAVDbCommitStaged();
}

/* Copies one slot's staged records into realDB, encrypting each field with
 * the record's final uniqueID as the nonce. Returns the records it read. */
static UInt16 DAVDbCopyStaged(DmOpenRef realDB, DmOpenRef tmpDB, UInt16 doneSoFar, UInt16 grandTotal) SEC003;
static UInt16 DAVDbCopyStaged(DmOpenRef realDB, DmOpenRef tmpDB, UInt16 doneSoFar, UInt16 grandTotal)
{
	UInt16 tmpTotal = DmNumRecords(tmpDB), i;
	char msg[32];

	for (i = 0; i < tmpTotal; i++) {
		MemHandle srcH = DmQueryRecord(tmpDB, i);
		CDDVEvent ev;
		void *srcRaw;
		UInt16 index;
		MemHandle dstH;
		void *dstRaw;

		if ((i & 0x0F) == 0) {	/* redraw every 16 records - avoid pointless flicker */
			StrPrintF(msg, "Saving %u/%u", doneSoFar + i + 1, grandTotal);
			ToastDraw(msg);
			EvtResetAutoOffTimer();
		}

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
	return tmpTotal;
}

/* Commits every slot staged since the last commit as one unit: removes
 * those slots' old records, copies in the staged ones, sorts once, and
 * deletes all staging DBs (including stale ones nothing staged this run).
 * If Sec35 has locked since the sync, nothing is committed - the fields
 * could not be encrypted - and the old records are left as they were. */
SEC003 void DAVDbCommitStaged(void)
{
	DmOpenRef realDB = NULL;
	LocalID dbID, tmpID;
	UInt16 mask, grandTotal = 0, done = 0;
	UInt8 slot;
	char tmpName[dmDBNameLength];

	if (gRebuildDB) DAVDbAbortSlotRebuild();	/* never commit a half-written stage */
	mask = gStagedMask;
	gStagedMask = 0;

	if (mask && CDDVSec35Installed() && !CDDVSec35Unlocked()) mask = 0;

	if (mask) {
		dbID = DmFindDatabase(0, cddvDavDbName);
		if (dbID == 0 &&
			DmCreateDatabase(0, cddvDavDbName, appCreator, cddvDavDbType, false) == errNone)
			dbID = DmFindDatabase(0, cddvDavDbName);
		if (dbID != 0) realDB = DmOpenDatabase(0, dbID, dmModeReadWrite);
	}

	if (realDB) {
		Int16 j;

		/* Count staged records first, for the progress toast; drop any slot
		 * whose staging DB has gone missing so its old records survive. */
		for (slot = 0; slot < cddvMaxSlots; slot++) {
			DmOpenRef t;
			if (!(mask & (1 << slot))) continue;
			DAVDbTmpName(slot, tmpName);
			tmpID = DmFindDatabase(0, tmpName);
			t = tmpID ? DmOpenDatabase(0, tmpID, dmModeReadOnly) : NULL;
			if (!t) { mask &= (UInt16) ~(1 << slot); continue; }
			grandTotal += DmNumRecords(t);
			DmCloseDatabase(t);
		}
		ToastDraw("Saving...");

		/* Only now - once the new data in the temp DBs is confirmed to exist -
		 * delete the staged slots' old records from the real database.
		 * Iterate backwards, since DmRemoveRecord shifts every later index
		 * down by one. */
		for (j = (Int16) DmNumRecords(realDB) - 1; j >= 0; j--) {
			MemHandle h = DmQueryRecord(realDB, (UInt16) j);
			UInt8 src;
			if (!h) continue;
			src = ((CDDVEvent *) MemHandleLock(h))->sourceSlot;
			MemHandleUnlock(h);
			if (src < cddvMaxSlots && (mask & (1 << src))) DmRemoveRecord(realDB, (UInt16) j);
		}

		for (slot = 0; slot < cddvMaxSlots; slot++) {
			DmOpenRef t;
			if (!(mask & (1 << slot))) continue;
			DAVDbTmpName(slot, tmpName);
			tmpID = DmFindDatabase(0, tmpName);
			t = tmpID ? DmOpenDatabase(0, tmpID, dmModeReadOnly) : NULL;
			if (!t) continue;
			done += DAVDbCopyStaged(realDB, t, done, grandTotal);
			DmCloseDatabase(t);
		}

		ToastDraw("Sorting...");
		DmQuickSort(realDB, DAVDbCompareByLocalStart, 0);
		DmCloseDatabase(realDB);
		ToastEnd();
	}

	/* Delete every staging DB, committed or not. */
	for (slot = 0; slot < cddvMaxSlots; slot++) {
		DAVDbTmpName(slot, tmpName);
		tmpID = DmFindDatabase(0, tmpName);
		if (tmpID != 0) DmDeleteDatabase(0, tmpID);
	}
	tmpID = DmFindDatabase(0, cddvDavDbOldTmpName);
	if (tmpID != 0) DmDeleteDatabase(0, tmpID);
}

SEC003 void DAVDbAbortSlotRebuild(void)
{
	/* The real database was never opened for writing during this sync -
	 * only the temp staging DB was touched, so discarding it here leaves
	 * every existing record in DateBook35DAV.pdb exactly as it was before
	 * the sync started. */
	LocalID tmpID;
	char tmpName[dmDBNameLength];
	if (gRebuildDB) { DmCloseDatabase(gRebuildDB); gRebuildDB = NULL; }
	gStagedMask &= (UInt16) ~(1 << gRebuildSlot);
	DAVDbTmpName(gRebuildSlot, tmpName);
	tmpID = DmFindDatabase(0, tmpName);
	if (tmpID != 0) DmDeleteDatabase(0, tmpID);
}

SEC003 Boolean DAVDbSlotHasRows(UInt8 slotIdx)
{
	LocalID id = DmFindDatabase(0, cddvDavDbName);
	DmOpenRef db;
	UInt16 i, n;
	Boolean found = false;

	if (id == 0) return false;
	db = DmOpenDatabase(0, id, dmModeReadOnly);
	if (!db) return false;
	n = DmNumRecords(db);
	for (i = 0; i < n && !found; i++) {
		MemHandle h = DmQueryRecord(db, i);
		if (h) {
			const CDDVEvent *ev = (const CDDVEvent *) MemHandleLock(h);
			found = (Boolean) (ev->sourceSlot == slotIdx);
			MemHandleUnlock(h);
		}
	}
	DmCloseDatabase(db);
	return found;
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
