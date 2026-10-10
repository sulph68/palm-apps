/*
 * DateDAV.c - self-contained reader over CalDAV Sync's DateBook35DAV.pdb,
 * used by the Agenda to surface synced calendar events (cross-DB read,
 * same idiom as AddrBday.c's own Address35 birthday reader - read as
 * reference, original code). Unlike Address35's packed variable-length
 * record format, DateBook35DAV.pdb is one fixed-size raw struct per
 * record (apps/caldav_sync/DAVDb.c's own comment: "lets Datebook35's own
 * future reader use ordinary DmQueryRecord/per-record iteration"), so
 * there's no field-parsing loop needed - just a direct MemMove into a
 * byte-for-byte mirror of that struct.
 *
 * Read-only: this file never opens DateBook35DAV.pdb for write, and never
 * touches CalDAV Sync's own CalDAVRelayDB prefs/secrets database at all.
 */
#include <PalmOS.h>
#include "DateDAV.h"
#include "enccore.h"	/* Db35 already links this for its own appointment encryption (DateDB.c) */

/* By NAME, not DmOpenDatabaseByTypeCreator(type, creator) - CalDAV Sync
 * has TWO databases sharing the exact same type='DATA'/creator='CDDV'
 * pair (this one, and its own CalDAVRelayDB prefs database - see
 * apps/caldav_sync/CDDVPrefs.c). A real POSE64 test caught this: every
 * query kept returning exactly 1 record regardless of how many events
 * were actually synced, because DmOpenDatabaseByTypeCreator was silently
 * finding CalDAVRelayDB (always exactly 1 record - the single prefs
 * blob) instead of DateBook35DAV. AddrBday.c's own use of
 * DmOpenDatabaseByTypeCreator is safe because Address35's type+creator
 * pair is unique to just that one database. */
#define kDavDbName "DateBook35DAV"

/* MUST match apps/caldav_sync/DAVDb.h's own CDDVEvent exactly (field
 * order and sizes) - both apps compile with the same m68k-palmos-gcc
 * toolchain/ABI, so a byte-for-byte mirror reads back correctly with no
 * explicit (de)serialization, the same assumption AddrBday.c already
 * makes about Address35's own struct layouts it doesn't share code with
 * either. */
typedef struct {
	char uid[48];
	char alias[24];
	char title[64];
	char location[64];
	UInt32 utcStart;
	UInt32 utcEnd;
	UInt32 localStart;
	UInt32 localEnd;
	Boolean allDay;
	UInt8 sourceSlot;
	UInt8 marker;
	char notes[kDateDavNotesLen];
	Boolean isRepeating;
	/* Appended (2026-09-24) - uid/alias/title/location/notes above are Sec35
	 * ciphertext at rest now (apps/caldav_sync/DAVDb.h's own comment) -
	 * explicit plaintext-length companions, must mirror that struct's field
	 * order exactly, same as everything else in this type. */
	UInt16 uidLen;
	UInt16 aliasLen;
	UInt16 titleLen;
	UInt16 locationLen;
	UInt16 notesLen;
} CDDVEventMirror;

/* Matches apps/caldav_sync/CDDVSec35.c's own cddvEncTtl exactly (read-time
 * TTL; Sec35's own alarm enforces the real lock, same convention every "35"
 * app's crypto uses - see DateDB.c's own apptEncTtl). */
#define kDavEncTtl 1000000000UL

/* Must match apps/caldav_sync/DAVDb.h's own cddvEvField* constants exactly -
 * this file can't #include that header (different app/binary), so these are
 * kept in sync by hand, same discipline as this file's own struct mirror. */
#define kDavFieldUid      0
#define kDavFieldAlias    1
#define kDavFieldTitle    2
#define kDavFieldLocation 3
#define kDavFieldNotes    4

/* Decrypts one field of ev IN PLACE using this record's own DmRecordInfo
 * uniqueID as the nonce - mirrors apps/caldav_sync/DAVDb.c's own
 * DAVDbEncryptField exactly (same nonce derivation: (uniqueID, fieldIdx)).
 * len==0 (field was empty at sync time) is already a safe no-op (EncCrypt
 * with len=0 does nothing, buf[0]='\0' is exactly the right result) - no
 * special case needed. No bufCap/overflow check here - the caller's own
 * MemHandleSize(h)==sizeof(CDDVEventMirror) guard (DateDAVGet/
 * DateDAVGetOne, right where each record is read) already establishes this
 * record came from the CURRENT struct shape, so its *Len fields are
 * trustworthy (DAVDb.c's own writer always clamps them to their field's
 * real capacity before writing - see DAVDbEncryptField). Plain EncCrypt
 * (redoes the AES key schedule every call), not the EncCryptFast/
 * EncKeySchedule fast path - deliberately: that path pulls in 3 enccore.c
 * functions DateDB.c's own appointment crypto never needed, which alone
 * was enough to push sec003res over its cap (real link failure found while
 * building this). DateDB.c's own ApptBuildInline already calls plain
 * EncCrypt once per field per record in exactly this shape, so the same
 * cost here is an already-proven-acceptable tier for this app. */
/* SEC001/SEC002/SEC003 placement for this and the other small helpers below
 * (DavDecryptCommon/DavHashUid/DavCompareByLocalStart/DavCopyStr) was found
 * by exhaustive trial-and-error against the real linker, not chosen for any
 * principled reason - sec001/sec002/sec003 were all sitting within single-
 * digit bytes of their own 32KB caps by the time the Sec35-locked masking
 * work (2026-09-24) landed, and this specific distribution is the one
 * combination of "which segment holds which helper" that happened to fit
 * all three simultaneously. If a future change to any of these functions'
 * bodies (or their call sites) tips the budget again, don't assume the
 * "obvious" segment for a given helper is free - retest with a linker map
 * (`-Wl,-Map=out.map`, then `grep '^sec00[123] '` for the real per-segment
 * totals - the naive "sum each .o's own section size" approach undercounts
 * by the inter-object alignment padding the linker adds, enough to hide a
 * real overflow). */
static void DavDecryptField(const UInt8 key[ENC_KEY_BYTES], UInt32 uid, UInt32 fieldIdx,
	char *buf, UInt16 len) SEC001;
static void DavDecryptField(const UInt8 key[ENC_KEY_BYTES], UInt32 uid, UInt32 fieldIdx,
	char *buf, UInt16 len)
{
	EncCrypt(key, (UInt8 *) buf, len, uid, fieldIdx);
	buf[len] = '\0';
}

/* Decrypts alias/title/location in place - shared by DateDAVGet's own L
 * block and DateDAVGetOne (both need exactly these three fields, same
 * order) - factored out to shrink code size (fewer duplicated call sites),
 * not just for reuse, same sec003res budget reasoning as DavDecryptField's
 * own comment. */
static void DavDecryptCommon(const UInt8 key[ENC_KEY_BYTES], UInt32 uid, CDDVEventMirror *ev) SEC002;
static void DavDecryptCommon(const UInt8 key[ENC_KEY_BYTES], UInt32 uid, CDDVEventMirror *ev)
{
	DavDecryptField(key, uid, kDavFieldAlias, ev->alias, ev->aliasLen);
	DavDecryptField(key, uid, kDavFieldTitle, ev->title, ev->titleLen);
	DavDecryptField(key, uid, kDavFieldLocation, ev->location, ev->locationLen);
}

static UInt32 DavHashUid(const char *uid) SEC001;

/* Simple djb2-style string hash - just needs to be stable and cheap, not
 * cryptographic. Seeded with a nonzero constant (not 0) so an empty uid
 * string doesn't hash to 0 (0 is used elsewhere in this app's row-id
 * schemes as an "uninitialized" sentinel - see DateDay.c's own gDayXxxUID
 * comments). */
static UInt32 DavHashUid(const char *uid)
{
	UInt32 h = 5381UL;
	while (*uid) h = (h * 33UL) ^ (UInt32) (UInt8) *uid++;
	return h;
}

/* Real-device report (2026-09-17): "day view should respond within
 * 250ms... takes 2-3 seconds per change in day, 5-10 seconds for week/
 * month view" - traced to 400-500 real synced events and a full
 * DmQueryRecord scan of EVERY record on every single call (Month view
 * alone calls this up to 31 times per redraw, so 12,000+ record touches).
 * apps/caldav_sync/DAVDb.c's own DAVDbCommitSlotRebuild now sorts this
 * database by localStart ascending after every sync (see its own
 * comment) - binary-searching to the first record on-or-after the target
 * day via DmFindSortPosition, then scanning forward WITH EARLY EXIT the
 * moment a record's localStart reaches the next day, turns this from an
 * O(total events) scan into an O(log n + matches-on-this-day) one,
 * regardless of how large the synced calendar grows. The day-boundary
 * comparison itself is now a plain integer range check (localStart in
 * [dayStart, dayEnd)) instead of a TimSecondsToDateTime ROM call per
 * record, since a sorted binary-search boundary already IS exactly that
 * boundary - the old DavMatchesDay helper this replaced is gone (no
 * remaining callers). */
static Int16 DavCompareByLocalStart(void *rec1, void *rec2, Int16 other,
	SortRecordInfoPtr r1SortInfo, SortRecordInfoPtr r2SortInfo, MemHandle appInfoH) SEC002;
static Int16 DavCompareByLocalStart(void *rec1, void *rec2, Int16 other,
	SortRecordInfoPtr r1SortInfo, SortRecordInfoPtr r2SortInfo, MemHandle appInfoH)
{
	CDDVEventMirror *e1 = (CDDVEventMirror *) rec1;
	CDDVEventMirror *e2 = (CDDVEventMirror *) rec2;
	/* Only ever called by DmFindSortPosition with rec1 = the search key
	 * (this app never sorts this database). Equal counts as "key first":
	 * DmFindSortPosition returns the position after every record that
	 * compares <= the key, so a plain 0 here skipped records starting
	 * exactly at the day's 00:00 - every all-day event, and any timed
	 * event at midnight. (Done here rather than searching for
	 * dayStartLocal - 1: same effect, no extra bytes - sec003 is full.) */
	if (e1->localStart <= e2->localStart) return -1;
	return 1;
}

SEC003 UInt16 DateDAVGet(UInt16 year, UInt8 month, UInt8 day,
	char lines[][kDateDavLineLen], TimeType startTimes[], TimeType endTimes[],
	UInt32 uidHashes[], Boolean isRepeating[], Boolean hasNotes[], UInt16 maxLines)
{
	DmOpenRef db;
	LocalID dbID;
	UInt16 total, i, out = 0;
	DateTimeType dtBoundary;
	UInt32 dayStartLocal, dayEndLocal;
	CDDVEventMirror searchKey;
	UInt8 key[ENC_KEY_BYTES];
	Boolean locked;

	dbID = DmFindDatabase(0, kDavDbName);
	db = (dbID != 0) ? DmOpenDatabase(0, dbID, dmModeReadOnly) : NULL;
	if (db == NULL)
		return 0;	/* CalDAV Sync never installed/synced - silent no-op */

	/* Every text field in this database is Sec35 ciphertext now. Locked
	 * used to mean "silent no-op, same as CalDAV Sync never having synced
	 * at all" - changed (user request 2026-09-24): a locked CalDAV row
	 * should behave exactly like a locked real appointment (DateDay.c's
	 * own DayMaskCopy/DayLockedBounce) - still SHOWN (masked "******",
	 * same text a real encrypted appointment draws when locked), not
	 * hidden outright, with tapping it bouncing to Sec35 (DateDay.c's own
	 * DayOpenDavDetails, not here - this function only controls what gets
	 * drawn). The scan below still runs either way since every field it
	 * needs for THAT (day-window matching, times, allDay, isRepeating,
	 * hasNotes) is plaintext, unaffected by lock state - only uid/alias/
	 * title/location need the key, guarded individually below. */
	locked = !EncGetKey(key, kDavEncTtl);

	MemSet(&dtBoundary, sizeof(dtBoundary), 0);
	dtBoundary.year = (Int16) year;
	dtBoundary.month = (Int16) month;
	dtBoundary.day = (Int16) day;
	dayStartLocal = TimDateTimeToSeconds(&dtBoundary);
	dayEndLocal = dayStartLocal + 86400UL;

	MemSet(&searchKey, sizeof(searchKey), 0);
	searchKey.localStart = dayStartLocal;
	total = DmNumRecords(db);
	i = DmFindSortPosition(db, &searchKey, NULL, DavCompareByLocalStart, 0);

	for (; i < total && out < maxLines; i++) {
		MemHandle h = DmQueryRecord(db, i);
		CDDVEventMirror ev;
		const void *raw;
		char *L;
		UInt16 used, titleMax, titleLen;
		UInt32 recUid;

		/* MemHandleSize check (not just h==NULL) guards against a record
		 * written by an OLDER build of caldav_sync, before this struct grew
		 * its *Len fields - reading it as the current (larger) shape would
		 * misinterpret trailing bytes as ciphertext lengths, which
		 * DavDecryptField would otherwise have to bounds-check itself on
		 * every field/every call. One check here, once per record, instead -
		 * self-heals the moment that slot's next sync rewrites the record
		 * (DAVDb.c's own full-rebuild-per-sync model, see its header). */
		if (h == NULL || MemHandleSize(h) != sizeof(CDDVEventMirror)) continue;
		raw = MemHandleLock(h);
		MemMove(&ev, raw, sizeof(ev));
		MemHandleUnlock(h);

		/* Sorted ascending by localStart - once we've passed the target
		 * day's own end boundary, every remaining record is even later,
		 * so nothing further can possibly match. Stop scanning instead of
		 * touching the rest of a potentially much larger database. */
		if (ev.localStart >= dayEndLocal) break;
		if (ev.localStart < dayStartLocal) continue;	/* defensive - shouldn't happen given the search above */

		L = lines ? lines[out] : NULL;
		/* One DmRecordInfo call, shared by both blocks below (uidHashes and
		 * L are almost always requested together in practice) - code-size
		 * matters here, not just cycles (see DavDecryptField's own comment
		 * on the sec003res budget this whole file lives in). */
		if (uidHashes || L) DmRecordInfo(db, i, NULL, &recUid, NULL);

		if (uidHashes) {
			/* Locked: can't decrypt uid to hash it - default to the
			 * record's own real database uniqueID instead. Still stable
			 * and still unique per record (that's all row-diffing needs),
			 * just a different value than the unlocked hash of the same
			 * row - switching between the two the moment the user unlocks
			 * causes one harmless extra full redraw, not a correctness
			 * issue. */
			uidHashes[out] = recUid;
			if (!locked) {
				DavDecryptField(key, recUid, kDavFieldUid, ev.uid, ev.uidLen);
				uidHashes[out] = DavHashUid(ev.uid);
			}
		}
		if (isRepeating) isRepeating[out] = ev.isRepeating;
		/* notesLen (plaintext metadata) is enough to know whether notes
		 * exist - no need to decrypt the actual text just to answer that. */
		if (hasNotes) hasNotes[out] = (Boolean) (ev.notesLen > 0);

		/* lines may be NULL - Month view only ever needs the times (to
		 * draw a spanning bar or set the untimed-day flag), never the
		 * text, so it shouldn't need a per-day text scratch buffer at all
		 * (that used to cost 1536 bytes of the app's already-tight 32KB
		 * BSS budget: "region datares is full" at link time) - skips the
		 * alias/title/location decrypt work too now, same reasoning. */
		if (L) {
			L[0] = (char) kDateDavGlyph;
			L[1] = ' ';
			L[2] = '\0';
			if (locked) {
				/* Same "******" text a locked real appointment draws
				 * (DateDay.c's own DayMaskCopy) - kept behind the same
				 * glyph prefix unlocked DAV rows already show, so it's
				 * still recognizable as a synced-calendar row, just
				 * masked. */
				StrCat(L, "******");
			} else {
				DavDecryptCommon(key, recUid, &ev);
				StrCat(L, ev.alias);
				StrCat(L, " - ");
			}
		}
		if (ev.allDay) {
			/* PalmOS's standard "untimed" sentinel - every ApptInfoType
			 * row (real appointments, and AddrBday.c's own birthday rows)
			 * uses hour==-1 the same way. */
			MemSet(&startTimes[out], sizeof(TimeType), 0xFF);
			MemSet(&endTimes[out], sizeof(TimeType), 0xFF);
		} else {
			DateTimeType dtStart, dtEnd;

			TimSecondsToDateTime(ev.localStart, &dtStart);
			TimSecondsToDateTime(ev.localEnd, &dtEnd);
			startTimes[out].hours = (UInt8) dtStart.hour;
			startTimes[out].minutes = (UInt8) dtStart.minute;
			endTimes[out].hours = (UInt8) dtEnd.hour;
			endTimes[out].minutes = (UInt8) dtEnd.minute;

			/* No embedded time-string prefix here - AgendaApptDrawTime
			 * already draws startTimes[out] in its own dedicated column
			 * (same as every real appointment/birthday row), so a prefix
			 * baked into the text here would just duplicate it - found via
			 * a real POSE64 screenshot showing "10:00 10:00 am Mock Timed
			 * Meeting..." after wiring this reader into the Agenda. */
		}

		/* Title, truncated only defensively (kDateDavLineLen is sized for
		 * the worst real case - see DateDAV.h) - no longer pre-clipped to
		 * fit a single visible line: Agenda clips at DRAW time
		 * (WinDrawTruncChars) and Day view wraps this to multiple lines,
		 * same as a real appointment's own description. Skipped when
		 * locked - L is already the final "glyph ******" text set above,
		 * and ev.title/ev.location are still ciphertext (never decrypted
		 * in that branch), so appending them here would be meaningless. */
		if (L && !locked) {
			used = (UInt16) StrLen(L);
			titleMax = (UInt16) (kDateDavLineLen - 1 - used);
			titleLen = (UInt16) StrLen(ev.title);
			if (titleLen > titleMax) titleLen = titleMax;
			MemMove(L + used, ev.title, titleLen);
			L[used + titleLen] = '\0';

			/* " @ Location" appended only if the WHOLE result still fits -
			 * dropped entirely rather than truncated mid-name, computed on
			 * a scratch copy so a too-long result never corrupts L. */
			if (ev.location[0]) {
				char withLoc[kDateDavLineLen];
				StrCopy(withLoc, L);
				StrCat(withLoc, " @ ");
				StrCat(withLoc, ev.location);
				if (StrLen(withLoc) < kDateDavLineLen)
					StrCopy(L, withLoc);
			}
		}

		out++;
	}

	MemSet(key, ENC_KEY_BYTES, 0);
	DmCloseDatabase(db);
	return out;
}

/* Factored out (2026-09-17) so callers elsewhere (DateWeek.c's own new
 * result-cache) don't each need their own copy of the "DateBook35DAV"
 * string literal + DmOpenDatabaseInfo/DmDatabaseInfo dance - see this
 * function's own header comment in DateDAV.h for the real sec002res
 * budget bug that motivated it. Same DmOpenDatabaseInfo/DmDatabaseInfo
 * call shape DateMonth.c's own MonthScanAndDrawCalDav already uses. */
SEC003 UInt32 DateDAVGetModNum(void)
{
	LocalID dbID = DmFindDatabase(0, kDavDbName);
	DmOpenRef db;
	UInt32 modNum = 0;
	UInt16 cardNo;

	if (dbID == 0) return 0;
	db = DmOpenDatabase(0, dbID, dmModeReadOnly);
	if (!db) return 0;
	if (DmOpenDatabaseInfo(db, &dbID, NULL, NULL, &cardNo, NULL) == errNone)
		DmDatabaseInfo(cardNo, dbID, NULL, NULL, NULL, NULL, NULL, NULL,
			&modNum, NULL, NULL, NULL, NULL);
	DmCloseDatabase(db);
	return modNum;
}

/* Bounded copy with guaranteed NUL termination - sidesteps StrNCopy's own
 * strlcat-style size-argument semantics entirely (see the project's own
 * StrNCat/StrNCopy gotcha writeup) rather than risk getting the cap math
 * wrong for a one-off helper. dst may be NULL (caller doesn't want this
 * field) or cap may be 0 - both silent no-ops.
 *
 * SEC001 (was sec003, this function's own natural segment alongside its
 * only caller DateDAVGetOne) - the Sec35 decrypt work this file gained
 * (DavDecryptField/DavDecryptCommon + a MemHandleSize stale-record guard
 * in both DateDAVGet and DateDAVGetOne) pushed sec002res/sec003res right
 * to their 32KB caps; sec001 (DateDay's own segment) turned out to be the
 * only one of the three with real spare room. A cross-segment call from
 * DateDAVGetOne (sec003) into here is no different from any other
 * cross-segment call already used throughout this reader. */
static void DavCopyStr(char *dst, UInt16 cap, const char *src) SEC001;
static void DavCopyStr(char *dst, UInt16 cap, const char *src)
{
	UInt16 n;
	if (!dst || cap == 0) return;
	n = (UInt16) StrLen(src);
	if (n > (UInt16) (cap - 1)) n = (UInt16) (cap - 1);
	MemMove(dst, src, n);
	dst[n] = '\0';
}

SEC003 Boolean DateDAVGetOne(UInt16 year, UInt8 month, UInt8 day, UInt32 uidHash,
	char *alias, UInt16 aliasCap, char *title, UInt16 titleCap,
	char *location, UInt16 locationCap, char *notes, UInt16 notesCap,
	TimeType *startOut, TimeType *endOut, Boolean *allDayOut,
	Boolean *isRepeatingOut)
{
	DmOpenRef db;
	LocalID dbID;
	UInt16 total, i;
	Boolean found = false;
	DateTimeType dtBoundary;
	UInt32 dayStartLocal, dayEndLocal;
	CDDVEventMirror searchKey;
	UInt8 key[ENC_KEY_BYTES];

	dbID = DmFindDatabase(0, kDavDbName);
	db = (dbID != 0) ? DmOpenDatabase(0, dbID, dmModeReadOnly) : NULL;
	if (db == NULL)
		return false;

	/* Same "nothing here can be shown while locked" reasoning as
	 * DateDAVGet's own identical check. */
	if (!EncGetKey(key, kDavEncTtl)) {
		DmCloseDatabase(db);
		return false;
	}

	/* Same binary-search-then-early-exit as DateDAVGet - see its own
	 * comment (this function's own header comment in DateDAV.h already
	 * called this "a live re-scan, cheap here" back when the database was
	 * small; at 400-500 real synced events that assumption no longer
	 * holds, so it gets the same treatment). */
	MemSet(&dtBoundary, sizeof(dtBoundary), 0);
	dtBoundary.year = (Int16) year;
	dtBoundary.month = (Int16) month;
	dtBoundary.day = (Int16) day;
	dayStartLocal = TimDateTimeToSeconds(&dtBoundary);
	dayEndLocal = dayStartLocal + 86400UL;

	MemSet(&searchKey, sizeof(searchKey), 0);
	searchKey.localStart = dayStartLocal;
	total = DmNumRecords(db);
	i = DmFindSortPosition(db, &searchKey, NULL, DavCompareByLocalStart, 0);

	for (; i < total && !found; i++) {
		MemHandle h = DmQueryRecord(db, i);
		CDDVEventMirror ev;
		const void *raw;

		UInt32 recUid;

		/* Same stale-format guard as DateDAVGet's own identical check -
		 * see its comment. */
		if (h == NULL || MemHandleSize(h) != sizeof(CDDVEventMirror)) continue;
		raw = MemHandleLock(h);
		MemMove(&ev, raw, sizeof(ev));
		MemHandleUnlock(h);

		if (ev.localStart >= dayEndLocal) break;
		if (ev.localStart < dayStartLocal) continue;

		DmRecordInfo(db, i, NULL, &recUid, NULL);
		DavDecryptField(key, recUid, kDavFieldUid, ev.uid, ev.uidLen);
		if (DavHashUid(ev.uid) != uidHash)
			continue;

		DavDecryptCommon(key, recUid, &ev);
		DavDecryptField(key, recUid, kDavFieldNotes, ev.notes, ev.notesLen);

		DavCopyStr(alias, aliasCap, ev.alias);
		DavCopyStr(title, titleCap, ev.title);
		DavCopyStr(location, locationCap, ev.location);
		DavCopyStr(notes, notesCap, ev.notes);

		if (ev.allDay) {
			if (startOut) MemSet(startOut, sizeof(TimeType), 0xFF);
			if (endOut) MemSet(endOut, sizeof(TimeType), 0xFF);
		} else {
			DateTimeType dtStart, dtEnd;
			TimSecondsToDateTime(ev.localStart, &dtStart);
			TimSecondsToDateTime(ev.localEnd, &dtEnd);
			if (startOut) {
				startOut->hours = (UInt8) dtStart.hour;
				startOut->minutes = (UInt8) dtStart.minute;
			}
			if (endOut) {
				endOut->hours = (UInt8) dtEnd.hour;
				endOut->minutes = (UInt8) dtEnd.minute;
			}
		}
		if (allDayOut) *allDayOut = ev.allDay;
		if (isRepeatingOut) *isRepeatingOut = ev.isRepeating;

		found = true;
	}

	MemSet(key, ENC_KEY_BYTES, 0);
	DmCloseDatabase(db);
	return found;
}

/* Db35: "CalDAV Sync" menu item (Options menu, right after Phone Lookup) -
 * plain switch-to-app bounce, no data to hand off (unlike AddrBdayGoTo's
 * own GoToParamsType - CalDAV Sync just opens to its own MainForm; it
 * handles its own Sec35 unlock gate internally, same as any other normal
 * launch of it). Untagged (main segment, not SEC00x) - DayViewDoCommand's
 * own dispatch (DateDay.c, sec001) has essentially no headroom left, so
 * the call itself must reach out to main rather than a same-segment
 * helper; a non-main-to-main call is the safe cross-segment direction
 * (unlike the reverse - see db35_seg.h's own critical reminders). */
#define kCddvCreator	'CDDV'

void DateDAVGoToSync(void)
{
	DmSearchStateType st;
	UInt16 cardNo;
	LocalID dbID;

	if (DmGetNextDatabaseByTypeCreator(true, &st, sysFileTApplication, kCddvCreator,
			true, &cardNo, &dbID) != errNone || dbID == 0)
		return;
	SysUIAppSwitch(cardNo, dbID, sysAppLaunchCmdNormalLaunch, NULL);
}
