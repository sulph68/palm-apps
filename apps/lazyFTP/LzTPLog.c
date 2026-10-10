/*
 * LzTPLog.c - lightweight run-scoped FTP command/connection logger, written
 * to an on-device PDB ("LazyFTPLogs", type 'DATA', creator appCreator -
 * same convention as Listing.c's own on-device cache DB, read as reference
 * for the DmCreateDatabase/DmNewRecord idioms here) rather than kept in
 * RAM, per explicit user request - this is meant to survive whatever the
 * app itself is doing (including low-memory conditions) long enough to
 * actually be read afterward.
 *
 * Wiped and recreated fresh on every launch (LzTPLogInit, called from
 * StartApplication) - this is a diagnostic aid for the CURRENT run only,
 * not a persistent history across launches.
 *
 * Each log line is its own record (simplest possible append - DmNewRecord
 * at dmMaxRecordIndex, matching Listing.c's CacheAppendEntry idiom) rather
 * than one giant growing record, so trimming the oldest entries when the
 * total exceeds lzLogMaxBytes is just DmRemoveRecord(db, 0) in a loop - no
 * need to shift/rewrite anything.
 */
#include "LzTPLog.h"

#define lzLogDbName   "LazyFTPLogs"
#define lzLogDbType   ((UInt32) 'DATA')
#define lzLogMaxBytes 4096

/* Running total of all record payload bytes currently in the DB (each
 * record's on-disk size, which includes its own NUL terminator) - kept in
 * sync with every append/trim rather than re-walked each time, since
 * LzTPLogWrite can be called very frequently (once per FTP command/reply).
 * Always starts at 0 - LzTPLogInit always creates a genuinely fresh DB, so
 * there's never a stale total to reconcile with. */
static UInt32 gLogTotalBytes;

static DmOpenRef LogDbOpen(Boolean createIfMissing)
{
	LocalID dbID = DmFindDatabase(0, lzLogDbName);
	if (!dbID) {
		if (!createIfMissing) return 0;
		if (DmCreateDatabase(0, lzLogDbName, appCreator, lzLogDbType, false) != errNone)
			return 0;
		dbID = DmFindDatabase(0, lzLogDbName);
		if (!dbID) return 0;
	}
	return DmOpenDatabase(0, dbID, dmModeReadWrite);
}

SEC002 void LzTPLogInit(void)
{
	LocalID dbID = DmFindDatabase(0, lzLogDbName);
	DmOpenRef db;

	if (dbID) DmDeleteDatabase(0, dbID);
	gLogTotalBytes = 0;

	/* Create it now (rather than lazily on first LzTPLogWrite) so an empty
	 * "Logs" view right after launch shows "nothing logged yet" instead of
	 * silently failing to open a DB that was never created. */
	db = LogDbOpen(true);
	if (db) DmCloseDatabase(db);
}

SEC002 void LzTPLogWrite(const char *msg)
{
	DmOpenRef db;
	UInt16 len = (UInt16) StrLen(msg);
	UInt16 idx;
	MemHandle h;

	if (len == 0) return;
	db = LogDbOpen(true);
	if (!db) return;

	/* Trim oldest-first until this new entry will fit, rather than
	 * appending then trimming after - keeps gLogTotalBytes an accurate
	 * running total at every point, not just between calls. Record 0 is
	 * always the oldest surviving entry (append-only at the far end). */
	while (gLogTotalBytes + len + 1 > lzLogMaxBytes && DmNumRecords(db) > 0) {
		MemHandle oldH = DmQueryRecord(db, 0);
		UInt32 oldSize = oldH ? MemHandleSize(oldH) : 0;
		DmRemoveRecord(db, 0);
		if (oldSize) gLogTotalBytes -= oldSize;
	}

	idx = dmMaxRecordIndex;
	h = DmNewRecord(db, &idx, (UInt32) len + 1);
	if (h) {
		void *p = MemHandleLock(h);
		DmWrite(p, 0, msg, len);
		DmSet(p, len, 1, 0);	/* NUL terminator */
		MemHandleUnlock(h);
		DmReleaseRecord(db, idx, true);
		gLogTotalBytes += (UInt32) len + 1;
	}
	DmCloseDatabase(db);
}

SEC002 Boolean LzTPLogReadAll(MemHandle *outH, UInt32 *outLen)
{
	DmOpenRef db;
	UInt16 n, i;
	UInt32 total = 0, offset = 0;
	MemHandle h;
	char *p;

	*outH = NULL;
	*outLen = 0;

	db = LogDbOpen(false);
	if (!db) return false;

	n = DmNumRecords(db);
	for (i = 0; i < n; i++) {
		MemHandle rh = DmQueryRecord(db, i);
		if (rh) total += MemHandleSize(rh);	/* each record's size already includes its own NUL */
	}
	if (total == 0) { DmCloseDatabase(db); return false; }

	h = MemHandleNew(total);
	if (!h) { DmCloseDatabase(db); return false; }
	p = (char *) MemHandleLock(h);

	for (i = 0; i < n; i++) {
		MemHandle rh = DmQueryRecord(db, i);
		if (rh) {
			UInt32 sz = MemHandleSize(rh);		/* text + NUL */
			const char *rp = (const char *) MemHandleLock(rh);
			MemMove(p + offset, rp, sz - 1);	/* text only, replace NUL with '\n' below */
			offset += sz - 1;
			p[offset++] = '\n';
			MemHandleUnlock(rh);
		}
	}
	if (offset > 0) offset--;	/* drop the trailing newline after the last line */
	p[offset] = '\0';

	MemHandleUnlock(h);
	DmCloseDatabase(db);

	*outH = h;
	*outLen = offset;
	return true;
}
