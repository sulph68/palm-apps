/*
 * Listing.c - hand-drawn directory listing rows + scrollbar sync, plus the
 * on-device large-directory cache (LazyFTPDBCache). Same design language
 * as Address35/CryptoNote's SD-card browsing (MainDrawCardRow, read as
 * reference only - original code here): folder bitmap icon, an up-arrow
 * "[ .. ]" row instead of a Back button. Unlike the SD-card precedent,
 * file rows get a symbol-font glyph and long names wrap across up to
 * lzListMaxWrapLines lines instead of being truncated - FTP filenames run
 * longer than typical card filenames.
 */
#include "Listing.h"
#include "FileActions.h"	/* FileActionsHasCut - ListingDrawCutIndicator's query */
#include <TimeMgr.h>	/* TimGetTicks - cache staleness clock */

FtpDirEntry *gListPageBuf;		/* the ONLY entry-data buffer - see Listing.h */
UInt16       gListWindowCount;
UInt16       gListVisibleCount;
UInt16       gListTop;

UInt16       gListCacheTtlSecs = 30;
Boolean      gListCacheActive;
UInt32       gListCacheLeftTick;
UInt32       gListTruncatedAt;		/* ingest count at the point of a rare
									   write failure - see ListingIngestEntry */
Boolean      gListTruncated;
Boolean      gListOutOfMemory;

char        gListFilterTerm[lzFilterMaxLen + 1];
Boolean     gListFilterActive;

UInt16      gListPage;
UInt16      gListPageCount;

/* Two-tap select state - identified by NAME, never by gListPageBuf index:
 * ListHit's own entryIndex is only a slot in whatever page/filter-batch is
 * CURRENTLY loaded (see its own comment in Listing.h), so an index would
 * silently point at the wrong entry the moment a page turn, filter change,
 * or refresh reloads gListPageBuf. Matching by name instead makes stale
 * selection self-correcting: if the selected name isn't in the currently
 * drawn set, it just doesn't highlight anything - see ListingIsSelected. */
Boolean     gListSelActive;
char        gListSelName[ftpNameLen];
Boolean     gListSelIsDir;

/* Small-dir + filter only: display row -> gListPageBuf slot (see
 * ListEntry/ReloadWindow). Tiny (1KB) fixed static array, not heap - no
 * need for lazy allocation at this size. */
static UInt16 gFilterSlots[lzListPageSize];

/* Ingest-time-only state (valid between ListingIngestBegin/End). */
static DmOpenRef     gIngestDb;
static UInt32        gIngestCount;
static ListCacheInfo gIngestInfo;
static Boolean        gCacheForceStale;

/* Case-insensitive substring search - same idiom as Address35's
 * A35FieldHasTerm (read as reference, original code here). */
static Boolean NameHasTerm(const char *name, const char *lower, UInt16 tl)
{
	UInt16 len = (UInt16) StrLen(name);
	UInt16 i, j;

	if (tl == 0) return true;
	if (tl > len) return false;
	for (i = 0; (UInt16) (i + tl) <= len; i++) {
		for (j = 0; j < tl; j++) {
			char c = name[i + j];
			if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
			if (c != lower[j]) break;
		}
		if (j == tl) return true;
	}
	return false;
}

static void LowerFilterTerm(char *out, UInt16 *outLen)
{
	UInt16 tl = (UInt16) StrLen(gListFilterTerm);
	UInt16 k;
	if (tl > lzFilterMaxLen) tl = lzFilterMaxLen;
	for (k = 0; k < tl; k++) {
		char c = gListFilterTerm[k];
		if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
		out[k] = c;
	}
	out[tl] = '\0';
	*outLen = tl;
}

/* ==== on-device cache DB ================================================ */

static DmOpenRef CacheDbOpen(Boolean createIfMissing)
{
	LocalID dbID = DmFindDatabase(0, lzCacheDbName);
	if (!dbID) {
		if (!createIfMissing) return 0;
		if (DmCreateDatabase(0, lzCacheDbName, appCreator, lzCacheDbType, false) != errNone)
			return 0;
		dbID = DmFindDatabase(0, lzCacheDbName);
		if (!dbID) return 0;
	}
	return DmOpenDatabase(0, dbID, dmModeReadWrite);
}

/* Record 0 is always the ListCacheInfo metadata record; inserts it if this
 * is a fresh DB, updates it in place otherwise (used both when building a
 * new cache and when "touching" an existing one on revisit). */
static void CacheWriteInfo(DmOpenRef db, const ListCacheInfo *info)
{
	if (DmNumRecords(db) > 0) {
		MemHandle h = DmGetRecord(db, 0);
		if (h) {
			void *p = MemHandleLock(h);
			DmWrite(p, 0, info, sizeof(ListCacheInfo));
			MemHandleUnlock(h);
			DmReleaseRecord(db, 0, true);
		}
	} else {
		UInt16 idx = 0;
		MemHandle h = DmNewRecord(db, &idx, sizeof(ListCacheInfo));
		if (h) {
			void *p = MemHandleLock(h);
			DmWrite(p, 0, info, sizeof(ListCacheInfo));
			MemHandleUnlock(h);
			DmReleaseRecord(db, idx, true);
		}
	}
}

static Boolean CacheReadInfo(DmOpenRef db, ListCacheInfo *out)
{
	MemHandle h;
	if (DmNumRecords(db) < 1) return false;
	h = DmQueryRecord(db, 0);
	if (!h) return false;
	MemMove(out, MemHandleLock(h), sizeof(ListCacheInfo));
	MemHandleUnlock(h);
	return true;
}

static Boolean CacheAppendEntry(DmOpenRef db, const FtpDirEntry *e)
{
	UInt16 idx = dmMaxRecordIndex;		/* append at end */
	MemHandle h = DmNewRecord(db, &idx, sizeof(FtpDirEntry));
	void *p;
	if (!h) return false;
	p = MemHandleLock(h);
	DmWrite(p, 0, e, sizeof(FtpDirEntry));
	MemHandleUnlock(h);
	DmReleaseRecord(db, idx, true);
	return true;
}

SEC003 Boolean ListingCacheValidFor(const char *server, UInt16 port, const char *path)
{
	DmOpenRef db;
	ListCacheInfo info;
	Boolean ok;
	UInt32 nowTick, staleTicks;

	if (gCacheForceStale) return false;	/* Folder > Refresh */

	db = CacheDbOpen(false);
	if (!db) return false;
	ok = CacheReadInfo(db, &info);
	DmCloseDatabase(db);
	if (!ok) return false;

	if (StrCompare(info.server, server) != 0 || info.port != port
		|| StrCompare(info.path, path) != 0)
		return false;

	/* gListCacheActive true + gListCacheLeftTick==0 means we're STILL
	 * resident in this exact directory this session (never navigated
	 * away) - always valid in that case, no time limit, matching the
	 * user's own spec ("the moment we navigate away... 30 seconds")
	 * literally: staying put never invalidates it.
	 *
	 * Real bug found live: gListCacheLeftTick also defaults to 0 on a
	 * FRESH APP LAUNCH (global zero-init) - which this same "==0 means
	 * still resident" read used to treat identically to "never left",
	 * incorrectly reusing an on-disk cache from a PREVIOUS session
	 * forever, no matter how much real time had actually passed (POSE64's
	 * virtual clock keeps advancing across app relaunches within the same
	 * emulator process, so this was directly observable: a cache built
	 * several rebuild/reinstall cycles earlier, long past 30s, was still
	 * being served fresh). Fix: only trust "==0" as "still resident" when
	 * gListCacheActive is ALSO true for THIS session; otherwise (a fresh
	 * launch encountering a leftover DB, or having genuinely left it) fall
	 * back to the durable, DB-stored cachedAtTick for the time check -
	 * that field is what actually survives a relaunch, unlike this
	 * in-memory tick. */
	if (gListCacheActive && gListCacheLeftTick == 0) return true;

	nowTick = TimGetTicks();
	staleTicks = (UInt32) gListCacheTtlSecs * SysTicksPerSecond();
	{
		UInt32 sinceTick = (gListCacheLeftTick != 0) ? gListCacheLeftTick : info.cachedAtTick;
		if ((nowTick - sinceTick) > staleTicks) return false;
	}
	return true;
}

SEC003 void ListingCacheActivateExisting(const char *server, UInt16 port, const char *path)
{
	DmOpenRef db;
	ListCacheInfo info;
	(void) server; (void) port; (void) path;	/* identity already checked by ListingCacheValidFor */

	ListingReset();
	gListCacheActive = true;
	gListCacheLeftTick = 0;
	gCacheForceStale = false;

	db = CacheDbOpen(false);
	if (db) {
		if (CacheReadInfo(db, &info)) {
			info.cachedAtTick = TimGetTicks();		/* revisiting keeps it alive */
			CacheWriteInfo(db, &info);
		}
		DmCloseDatabase(db);
	}
}

SEC003 void ListingCacheMarkLeft(void)
{
	if (gListCacheActive) gListCacheLeftTick = TimGetTicks();
}

SEC003 void ListingCacheForceStale(void)
{
	gCacheForceStale = true;
}

/* Single full pass over the cache DB applying the current filter (if any),
 * collecting the pageIndex'th batch of up to lzListPageSize matches into
 * gListPageBuf while continuing to count the TRUE total match count -
 * local flash I/O, not network, so this stays fast even for a very large
 * directory, and "N of M" is always accurate no matter which page is
 * requested. */
static UInt32 CacheScanAndLoadPage(UInt16 pageIndex)
{
	DmOpenRef db = CacheDbOpen(false);
	UInt16 total, i;
	UInt32 matchCount = 0;
	UInt16 wantStart, wantEnd;
	char lower[lzFilterMaxLen + 1];
	UInt16 tl = 0;
	Boolean filtering = (Boolean) (gListFilterActive && gListFilterTerm[0]);

	gListWindowCount = 0;
	if (!db) return 0;

	if (filtering) LowerFilterTerm(lower, &tl);

	wantStart = (UInt16) (pageIndex * lzListPageSize);
	wantEnd = (UInt16) (wantStart + lzListPageSize);

	total = DmNumRecords(db);
	for (i = 1; i < total; i++) {		/* record 0 is metadata */
		MemHandle h = DmQueryRecord(db, i);
		FtpDirEntry tmp;
		if (!h) continue;
		MemMove(&tmp, MemHandleLock(h), sizeof(FtpDirEntry));
		MemHandleUnlock(h);

		if (filtering && !NameHasTerm(tmp.name, lower, tl)) continue;

		if (matchCount >= wantStart && matchCount < wantEnd
			&& gListWindowCount < lzListPageSize && gListPageBuf) {
			gListPageBuf[gListWindowCount++] = tmp;
		}
		matchCount++;
	}

	DmCloseDatabase(db);
	return matchCount;
}

/* ==== ingest (writer side, driven by FtpList) =========================== */

SEC003 void ListingIngestBegin(const char *server, UInt16 port, const char *path)
{
	ListingReset();
	gIngestCount = 0;
	gIngestDb = 0;
	MemSet(&gIngestInfo, sizeof(gIngestInfo), 0);
	StrNCopy(gIngestInfo.server, server, sizeof(gIngestInfo.server) - 1);
	gIngestInfo.port = port;
	StrNCopy(gIngestInfo.path, path, sizeof(gIngestInfo.path) - 1);
}

/* Lazily allocates gListPageBuf if needed - shared by the ingest path
 * (small directories write directly into it) and ReloadWindow (cache-page
 * loads and small-dir filtering both read/write it too). Returns false
 * (and sets the truncation flags) on allocation failure. */
static Boolean EnsurePageBuf(void)
{
	if (gListPageBuf) return true;
	gListPageBuf = (FtpDirEntry *) MemPtrNew((UInt32) lzListPageSize * sizeof(FtpDirEntry));
	if (gListPageBuf) return true;
	gListTruncated = true;
	gListOutOfMemory = true;
	gListTruncatedAt = gIngestCount;
	return false;
}

SEC003 Boolean ListingIngestEntry(const FtpDirEntry *e)
{
	if (!gListCacheActive) {
		if (gIngestCount < lzListPageSize) {
			if (!EnsurePageBuf()) return false;
			gListPageBuf[gIngestCount] = *e;
			gIngestCount++;
			return true;
		}

		/* Crossing the page-size threshold - spill to the cache DB:
		 * delete/recreate fresh (an old cache for a DIFFERENT directory
		 * must not linger), write metadata, flush what's already in
		 * gListPageBuf (which IS the small-dir store - no separate copy),
		 * then fall through to append this entry too. */
		DmDeleteDatabase(0, DmFindDatabase(0, lzCacheDbName));
		gIngestDb = CacheDbOpen(true);
		if (!gIngestDb) {
			gListTruncated = true;
			gListTruncatedAt = gIngestCount;
			return false;
		}
		gIngestInfo.cachedAtTick = TimGetTicks();
		CacheWriteInfo(gIngestDb, &gIngestInfo);
		{
			UInt16 i;
			for (i = 0; i < gIngestCount; i++) {
				CacheAppendEntry(gIngestDb, &gListPageBuf[i]);
			}
		}
		gListCacheActive = true;
		/* fall through */
	}

	if (!gIngestDb) {
		gIngestDb = CacheDbOpen(true);
		if (!gIngestDb) {
			gListTruncated = true;
			gListTruncatedAt = gIngestCount;
			return false;
		}
	}
	if (!CacheAppendEntry(gIngestDb, e)) {
		gListTruncated = true;
		gListTruncatedAt = gIngestCount;
		return false;
	}
	gIngestCount++;
	return true;
}

SEC003 UInt32 ListingIngestEnd(void)
{
	if (gIngestDb) {
		DmCloseDatabase(gIngestDb);
		gIngestDb = 0;
	}
	if (!gListCacheActive) gListWindowCount = (UInt16) gIngestCount;
	/* Caller must call ListingUpdatePaging() next to populate the display
	 * window - kept separate so the same reload path is used whether this
	 * just finished a fresh ingest or a later filter/page change. */
	return gIngestCount;
}

SEC003 void ListingReset(void)
{
	gListWindowCount = 0;
	gListVisibleCount = 0;
	gListCacheActive = false;
	gListCacheLeftTick = 0;
	gListTruncated = false;
	gListOutOfMemory = false;
	gListTruncatedAt = 0;
	gListPage = 0;
	gListPageCount = 0;
	gListTop = 0;
	ListingClearSelection();
	/* Deliberately does NOT touch the on-disk cache DB - it persists
	 * across resets on purpose (see gListCacheLeftTick/gListCacheTtlSecs). */
}

/* ==== two-tap select (reader side, driven by the UI) ===================== */

SEC003 void ListingClearSelection(void)
{
	gListSelActive = false;
	gListSelName[0] = '\0';
}

SEC003 void ListingSetSelection(const char *name, Boolean isDir)
{
	StrNCopy(gListSelName, name, sizeof(gListSelName) - 1);
	gListSelName[sizeof(gListSelName) - 1] = '\0';
	gListSelIsDir = isDir;
	gListSelActive = true;
}

SEC003 Boolean ListingIsSelected(const char *name)
{
	return (Boolean) (gListSelActive && StrCompare(gListSelName, name) == 0);
}

/* ==== filter + paging (reader side, driven by the UI) =================== */

SEC003 FtpDirEntry *LzListEntry(UInt16 idx)
{
	if (!gListCacheActive && gListFilterActive && gListFilterTerm[0]) {
		return &gListPageBuf[gFilterSlots[idx]];
	}
	return &gListPageBuf[idx];
}

static void ReloadWindow(void)
{
	if (!EnsurePageBuf()) { gListVisibleCount = 0; gListPageCount = 0; return; }

	if (gListCacheActive) {
		UInt32 total = CacheScanAndLoadPage(gListPage);
		gListVisibleCount = gListWindowCount;
		gListPageCount = (total == 0) ? 0
			: (UInt16) ((total + lzListPageSize - 1) / lzListPageSize);
	} else {
		if (!gListFilterActive || !gListFilterTerm[0]) {
			gListVisibleCount = gListWindowCount;
		} else {
			char lower[lzFilterMaxLen + 1];
			UInt16 tl, i;
			LowerFilterTerm(lower, &tl);
			gListVisibleCount = 0;
			for (i = 0; i < gListWindowCount; i++) {
				if (NameHasTerm(gListPageBuf[i].name, lower, tl)) {
					gFilterSlots[gListVisibleCount++] = i;
				}
			}
		}
		gListPageCount = (gListVisibleCount == 0) ? 0 : 1;
		gListPage = 0;
	}
}

SEC003 void ListingUpdatePaging(void)
{
	gListPage = 0;
	gListTop = 0;
	ReloadWindow();
	ListingDrawPageIndicator();
	ListingDrawCutIndicator();		/* same call-site discipline as the page indicator - see its own comment */
}

SEC003 Boolean ListingPrevPage(void)
{
	if (gListPage == 0) return false;
	gListPage--;
	gListTop = 0;
	ReloadWindow();
	ListingDrawPageIndicator();
	ListingDrawCutIndicator();
	return true;
}

SEC003 Boolean ListingNextPage(void)
{
	if (gListPageCount == 0 || (UInt32) gListPage + 1 >= gListPageCount) return false;
	gListPage++;
	gListTop = 0;
	ReloadWindow();
	ListingDrawPageIndicator();
	ListingDrawCutIndicator();
	return true;
}

SEC003 void ListingDrawPageIndicator(void)
{
	FormPtr frm = FrmGetActiveForm();
	ControlPtr prevBtn, nextBtn;
	char buf[16];
	Int16 w;
	RectangleType r;

	if (!frm) return;
	prevBtn = (ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, btnMainPrevPage));
	nextBtn = (ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, btnMainNextPage));

	if (gListPageCount == 0) {
		StrCopy(buf, "0 of 0");
	} else {
		StrPrintF(buf, "%u of %u", (unsigned int) (gListPage + 1), (unsigned int) gListPageCount);
	}

	FntSetFont(stdFont);
	w = FntCharsWidth(buf, (Int16) StrLen(buf));

	r.topLeft.x = lzPageIndicatorX;
	r.topLeft.y = lzPageIndicatorY;
	r.extent.x = lzPageIndicatorW;
	r.extent.y = lzPageIndicatorH;
	WinEraseRectangle(&r, 0);
	WinDrawChars(buf, (Int16) StrLen(buf),
		(Int16) (lzPageIndicatorX + (lzPageIndicatorW - w) / 2), lzPageIndicatorY);

	if (prevBtn) CtlSetEnabled(prevBtn, (Boolean) (gListPageCount > 1 && gListPage > 0));
	if (nextBtn) CtlSetEnabled(nextBtn, (Boolean) (gListPageCount > 1 && (UInt32) gListPage + 1 < gListPageCount));
}

SEC003 Boolean ListingHasUpRow(void)
{
	const char *p = FtpCurPath();
	return FtpIsConnected() && !(p[0] == '/' && p[1] == '\0');
}

SEC003 Boolean ListingIsTextFilename(const char *name)
{
	UInt16 len = (UInt16) StrLen(name);
	const char *ext;
	UInt16 i;

	for (i = len; i > 0; i--) {
		if (name[i - 1] == '.') break;
	}
	if (i == 0) return false;		/* no extension */
	ext = &name[i];

	if (StrCaselessCompare(ext, "txt") == 0) return true;
	if (StrCaselessCompare(ext, "md") == 0) return true;
	if (StrCaselessCompare(ext, "markdown") == 0) return true;
	if (StrCaselessCompare(ext, "log") == 0) return true;
	if (StrCaselessCompare(ext, "csv") == 0) return true;
	if (StrCaselessCompare(ext, "ini") == 0) return true;
	if (StrCaselessCompare(ext, "cfg") == 0) return true;
	if (StrCaselessCompare(ext, "conf") == 0) return true;
	if (StrCaselessCompare(ext, "nfo") == 0) return true;
	if (StrCaselessCompare(ext, "me") == 0) return true;		/* readme, "read.me" style */
	return false;
}

/* Formats a size to AT MOST 5 characters (e.g. "999B", "9.9K", "99.9K",
 * "999K", "1.2G") so the size column can stay narrow and leave the rest
 * of the row for the filename - see lzListSizePos's own comment. Unit
 * thresholds are 1000*1024^n (not the "textbook" 1024^n), which keeps the
 * whole-number part under 1000 in every unit, guaranteeing the budget: 3
 * digits with no decimal, or up to 2 digits with one decimal, either way
 * plus a single-letter unit suffix. */
static void FormatSizeUnit(UInt32 wholeUnits, UInt32 remainder, UInt32 unitBytes, char unitChar, char *out)
{
	if (wholeUnits < 100UL) {
		UInt32 tenth = (remainder * 10UL) / unitBytes;
		StrPrintF(out, "%lu.%lu%c", (unsigned long) wholeUnits, (unsigned long) tenth, unitChar);
	} else {
		StrPrintF(out, "%lu%c", (unsigned long) wholeUnits, unitChar);
	}
}

SEC003 void ListingFormatSize(UInt32 bytes, char *out, UInt16 outLen)
{
	(void) outLen;
	if (bytes < 1000UL) {
		StrPrintF(out, "%luB", (unsigned long) bytes);
	} else if (bytes < 1000UL * 1024UL) {
		FormatSizeUnit(bytes / 1024UL, bytes % 1024UL, 1024UL, 'K', out);
	} else if (bytes < 1000UL * 1024UL * 1024UL) {
		FormatSizeUnit(bytes / (1024UL * 1024UL), bytes % (1024UL * 1024UL), 1024UL * 1024UL, 'M', out);
	} else {
		FormatSizeUnit(bytes / (1024UL * 1024UL * 1024UL), bytes % (1024UL * 1024UL * 1024UL), 1024UL * 1024UL * 1024UL, 'G', out);
	}
}

/* Wraps `name` across up to lzListMaxWrapLines lines within maxW pixels,
 * word-wrap-aware (FntWordWrap breaks at spaces where possible; filenames
 * with none still get a hard break at the fitting character count, same
 * fallback FntWordWrap itself uses). The last allowed line gets "..." if
 * the name still doesn't fully fit. */
typedef struct {
	UInt16  start[lzListMaxWrapLines];
	UInt16  len[lzListMaxWrapLines];
	UInt16  lineCount;
	Boolean truncated;
} NameWrap;

static void ComputeNameWrap(const char *name, Int16 maxW, NameWrap *w)
{
	UInt16 len = (UInt16) StrLen(name);
	Int16 fitW, fitLen;
	Boolean fits;

	MemSet(w, sizeof(*w), 0);

	fitW = maxW;
	fitLen = (Int16) len;
	FntCharsInWidth(name, &fitW, &fitLen, &fits);
	if ((UInt16) fitLen >= len) {
		w->start[0] = 0;
		w->len[0] = len;
		w->lineCount = 1;
		return;
	}

	{
		Int16 firstLen = FntWordWrap(name, maxW);
		UInt16 remain;
		UInt16 skip;
		if (firstLen <= 0) firstLen = fitLen;
		if (firstLen <= 0) firstLen = 1;			/* always advance */
		w->start[0] = 0;
		w->len[0] = (UInt16) firstLen;

		/* FntWordWrap returns how many characters fit on the FIRST line -
		 * it does NOT consume the space it broke at, so without this the
		 * second line started WITH a leading space (visible live: a name
		 * wrapped as "Foo Bar\n   Baz" instead of "Foo Bar\nBaz"). Skip
		 * any run of spaces sitting right at the break point before
		 * starting line 2 - the actual "wrap on whitespace" behavior. */
		skip = (UInt16) firstLen;
		while (skip < len && name[skip] == ' ') skip++;
		w->start[1] = skip;
		remain = (UInt16) (len - skip);
		{
			Int16 w2 = maxW, l2 = (Int16) remain;
			Boolean fits2;
			FntCharsInWidth(name + skip, &w2, &l2, &fits2);
			if ((UInt16) l2 >= remain) {
				w->len[1] = remain;
			} else {
				Int16 dotsW = FntCharsWidth("...", 3);
				w2 = (Int16) (maxW - dotsW);
				l2 = (Int16) remain;
				if (w2 > 0) FntCharsInWidth(name + firstLen, &w2, &l2, &fits2);
				else l2 = 0;
				w->len[1] = (UInt16) l2;
				w->truncated = true;
			}
		}
		w->lineCount = 2;
	}
}

/* Directories never show a size (only files do - see ListingDraw), so
 * they get the FULL name-column width instead of the size-reduced one -
 * was previously using the file-sized width unconditionally even for
 * folders, wasting up to lzListLastPos-lzListSizePos px of genuinely
 * empty row space on every folder name. */
static Int16 NameWrapWidth(const FtpDirEntry *e)
{
	return (Int16) ((e->is_dir ? lzListLastPos : lzListSizePos) - lzListNamePos);
}

/* Height (in rows-worth of pixels) a given entry index will take when
 * drawn - used by both the draw pass and the hit-test walk so the two stay
 * in lockstep. */
static UInt16 EntryRowHeight(UInt16 idx)
{
	NameWrap w;
	FtpDirEntry *e = LzListEntry(idx);
	ComputeNameWrap(e->name, NameWrapWidth(e), &w);
	return (UInt16) (w.lineCount * lzListRowH);
}

static void DrawIconAt(UInt16 bmpId, Int16 x, Int16 y)
{
	MemHandle h = DmGetResource(bitmapRsc, bmpId);
	if (h) {
		BitmapPtr bmp = (BitmapPtr) MemHandleLock(h);
		WinDrawBitmap(bmp, x, y);
		MemHandleUnlock(h);
		DmReleaseResource(h);
	}
}

SEC003 void ListingDraw(void)
{
	RectangleType r;
	Int16 y;
	UInt16 i;
	Boolean hasUp = ListingHasUpRow();

	/* Erases down to lzListEraseBottomY (144), NOT lzListBottomY (137) - the
	 * gap between them was never covered by any native resource object
	 * either, so stale pixels from a full-screen sub-form could persist
	 * there indefinitely (looked like a corrupted "last row") - see
	 * lzListEraseBottomY's own comment in Listing.h. */
	r.topLeft.x = 0; r.topLeft.y = lzListTopY;
	r.extent.x = lzListLastPos; r.extent.y = lzListEraseBottomY - lzListTopY;
	WinEraseRectangle(&r, 0);

	FntSetFont(stdFont);
	y = lzListTopY;

	if (hasUp && y < lzListBottomY) {
		Int16 iconY = (Int16) (y + (lzListRowH - 11) / 2);
		DrawIconAt(bmpCardUp, lzListIconX, iconY);
		WinDrawChars("..", 2, lzListNamePos, y);
		y = (Int16) (y + lzListRowH);
	}

	for (i = gListTop; i < gListVisibleCount; i++) {
		FtpDirEntry *e = LzListEntry(i);
		UInt16 rowH = EntryRowHeight(i);
		Int16 iconY;

		/* Never start a row that can't fully fit before lzListBottomY - a
		 * multi-line entry beginning just above the boundary would have
		 * its second line painted PAST it, outside the fixed-height erase
		 * rectangle above, leaving stale pixels on a later redraw where a
		 * shorter/absent entry occupies roughly the same spot. Skipping
		 * instead of partially drawing means the erase above is always
		 * sufficient, by construction. */
		if ((Int16) (y + rowH) > lzListBottomY) break;

		/* Aligned to the FIRST line only, not centered across the whole
		 * wrapped block - a multi-line name's icon should sit next to its
		 * first line, same as any normal wrapped-text-with-leading-icon
		 * layout. */
		iconY = (Int16) (y + (lzListRowH - 11) / 2);

		if (e->is_dir) {
			DrawIconAt(bmpFolder, lzListIconX, iconY);
		} else {
			char glyph[2];
			glyph[0] = (char) (ListingIsTextFilename(e->name) ? lzGlyphTextFile : lzGlyphFile);
			glyph[1] = '\0';
			FntSetFont(symbolFont);
			/* Real visual bug found live: the file glyph (a symbolFont
			 * CHARACTER, drawn via WinDrawChars) sat visually off-center
			 * compared to the folder icon (a fixed-size BITMAP at the same
			 * nominal X) - glyphs carry their own left-side bearing that a
			 * bitmap doesn't, so the same X coordinate doesn't produce the
			 * same visual alignment. +2px lines them up. */
			WinDrawChars(glyph, 1, (Int16) (lzListIconX + 2), iconY);
			FntSetFont(stdFont);
		}

		{
			NameWrap w;
			UInt16 line;
			ComputeNameWrap(e->name, NameWrapWidth(e), &w);
			for (line = 0; line < w.lineCount; line++) {
				Int16 ly = (Int16) (y + line * lzListRowH);
				WinDrawChars(e->name + w.start[line], (Int16) w.len[line], lzListNamePos, ly);
				if (line + 1 == w.lineCount && w.truncated)
					WinDrawChars("...", 3, (Int16) (lzListNamePos +
						FntCharsWidth(e->name + w.start[line], (Int16) w.len[line])), ly);
			}
		}

		if (!e->is_dir) {
			char sz[16];
			Int16 sw;
			ListingFormatSize(e->size, sz, sizeof(sz));
			sw = FntCharsWidth(sz, (Int16) StrLen(sz));
			WinDrawChars(sz, (Int16) StrLen(sz), (Int16) (lzListLastPos - sw), y);
		}

		/* Two-tap selection highlight - inverted AFTER the row's normal
		 * content, matching the standard PalmOS list-selection idiom (black
		 * text becomes white-on-black). Name-matched (see gListSelName's own
		 * comment), so a selection that's scrolled off-screen or filtered
		 * out this pass just never matches anything drawn - no separate
		 * "is it still visible" bookkeeping needed. Spans the row's real
		 * (possibly 2-line-wrapped) height, not a fixed one. */
		if (ListingIsSelected(e->name)) {
			RectangleType hr;
			hr.topLeft.x = 0; hr.topLeft.y = y;
			hr.extent.x = lzListLastPos; hr.extent.y = (Int16) rowH;
			WinInvertRectangle(&hr, 0);
		}

		y = (Int16) (y + rowH);
	}

	ListingSyncScrollbar();
}

SEC003 void ListingDrawCutIndicator(void)
{
	FormPtr frm = FrmGetActiveForm();
	RectangleType r;

	if (!frm) return;
	r.topLeft.x = lzCutIndicatorX; r.topLeft.y = lzCutIndicatorY;
	r.extent.x = lzCutIndicatorW; r.extent.y = lzCutIndicatorH;
	WinEraseRectangle(&r, 0);
	if (FileActionsHasCut()) DrawIconAt(bmpCut, lzCutIndicatorX, lzCutIndicatorY);
}

/* Largest valid gListTop - NOT gListVisibleCount-1 (that would let the very
 * last entry become the TOP row, leaving the rest of the screen blank and
 * making the scrollbar's usable range absurdly, unusably short for a long
 * list - real bug reported live). Instead walks backward from the end of
 * the list accumulating actual (possibly multi-line) row heights until the
 * visible area would overflow, so scrolling stops exactly when the last
 * entry's bottom edge reaches (not passes) the list's bottom edge - matches
 * what ListingDraw() itself renders, entry-height-for-entry-height, so the
 * two stay in lockstep by construction. */
static UInt16 ComputeMaxTop(void)
{
	Int16 avail = (Int16) (lzListBottomY - lzListTopY);
	UInt16 used = 0;
	UInt16 i;

	if (ListingHasUpRow()) avail = (Int16) (avail - lzListRowH);
	if (gListVisibleCount == 0) return 0;

	i = gListVisibleCount;
	while (i > 0) {
		UInt16 h = EntryRowHeight((UInt16) (i - 1));
		if ((Int16) (used + h) > avail) break;
		used = (UInt16) (used + h);
		i--;
	}
	return i;
}

SEC003 void ListingSyncScrollbar(void)
{
	ScrollBarPtr bar = (ScrollBarPtr) FrmGetObjectPtr(FrmGetActiveForm(),
		FrmGetObjectIndex(FrmGetActiveForm(), sclMain));
	/* Entry-granularity scrollbar (not line-granularity) - simple and good
	 * enough given typical FTP listing sizes; ranges over the CURRENT
	 * page's window only - coarser "< N of M >" navigation between pages
	 * lives in gListPage/gListPageCount instead (see ListingUpdatePaging). */
	Int16 maxVal = (Int16) ComputeMaxTop();
	SclSetScrollBar(bar, (Int16) gListTop, 0, maxVal, 1);
}

SEC003 void ListingScroll(Int16 delta)
{
	Int16 newTop = (Int16) (gListTop + delta);
	Int16 maxTop = (Int16) ComputeMaxTop();
	if (newTop < 0) newTop = 0;
	if (newTop > maxTop) newTop = maxTop;
	if (newTop == (Int16) gListTop) return;
	gListTop = (UInt16) newTop;
	ListingDraw();
}

SEC003 ListHit ListingHitTest(Int16 x, Int16 y)
{
	ListHit hit;
	Int16 rowY;
	UInt16 i;
	Boolean hasUp = ListingHasUpRow();

	hit.kind = kListHitNone;
	hit.entryIndex = 0;

	if (x < 0 || x >= lzListLastPos || y < lzListTopY || y >= lzListBottomY)
		return hit;

	rowY = lzListTopY;
	if (hasUp) {
		if (y < rowY + lzListRowH) { hit.kind = kListHitUp; return hit; }
		rowY = (Int16) (rowY + lzListRowH);
	}

	for (i = gListTop; i < gListVisibleCount; i++) {
		UInt16 rowH = EntryRowHeight(i);
		/* Same "never a row that can't fully fit" rule as ListingDraw, so
		 * hit-testing stays consistent with what's actually drawn. */
		if ((Int16) (rowY + rowH) > lzListBottomY) break;
		if (y < rowY + (Int16) rowH) {
			hit.kind = kListHitEntry;
			hit.entryIndex = i;
			return hit;
		}
		rowY = (Int16) (rowY + rowH);
	}
	return hit;
}
