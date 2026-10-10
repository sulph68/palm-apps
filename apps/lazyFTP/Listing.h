/*
 * Listing.h - hand-drawn directory listing, same design language as
 * Address35/CryptoNote's SD-card browsing (see MainDrawCardRow in either
 * app's MainForm.c - read as reference, this is original code): a folder
 * bitmap icon for directories, an up-arrow bitmap "[ .. ]" row at the top
 * instead of a separate Back button, and (new here, no card-browsing
 * equivalent) file rows get a small glyph from the built-in symbol font
 * and long names wrap across up to two lines instead of truncating.
 */
#ifndef LISTING_H
#define LISTING_H

#include "LazyFTP.h"
#include "FtpProtocol.h"

/* Storage model: gListPageBuf holds AT MOST lzListPageSize (512) entries
 * at once - either the WHOLE directory (small directories, <=512 entries,
 * held entirely in RAM, exactly like the original design) or just the
 * CURRENT page/filter-batch's worth (large directories - see below).
 *
 * A real PalmOS directory of 1660+ files broke the original fixed
 * gListEntries[96] (silently capped at 96, no error). A first fix grew
 * that into a multi-chunk heap array up to 16384 entries - better, but
 * still bounded by DYNAMIC HEAP, which turned out to be the real binding
 * constraint on memory-constrained devices/emulator configs well before
 * 16384 (observed live in POSE64: out of heap after exactly one 512-entry
 * chunk against a real 1660-file directory) - and even when heap allows
 * it, "N of M" pagination computed from "how much fit in RAM" doesn't
 * match the directory's TRUE size, which is actively misleading.
 *
 * This design instead spills to an ON-DEVICE DATABASE (LazyFTPDBCache,
 * storage/flash heap, not the tight dynamic heap - a completely different,
 * far larger budget) once a listing exceeds lzListPageSize entries. RAM
 * only ever holds the current page's worth (or a batch of filter matches),
 * refreshed from the cache DB on page/filter changes - no re-fetch from
 * the network needed for ordinary browsing, only when the cache goes
 * stale (see gListCacheLeftTick) or the user explicitly asks (Folder >
 * Refresh). See ListingIngestBegin/ListingIngestEntry/ListingIngestEnd
 * (writer side, driven by FtpList) and ListingUpdatePaging/PrevPage/
 * NextPage (reader side, driven by the UI). */
#define lzListPageSize     512
#define lzListIconX          3
#define lzListNamePos       15
/* Size column budget: ListingFormatSize now caps its output at 5 chars
 * (e.g. "99.9K", "999K", "1.2G" - single-letter units, at most 1 decimal
 * digit, switching units before the whole part reaches 3 digits) - was
 * "%lu.%lu KB" style (up to 8 chars, "348.5 KB") reserving far more
 * column width than the text ever needed, starving the name column.
 * 30px comfortably fits 5 stdFont chars with margin. Only FILES reserve
 * this width at all - see NameWrapWidth(), directories (which never show
 * a size) get the full lzListLastPos-lzListNamePos width instead. */
#define lzListSizePos      122
#define lzListLastPos      152
#define lzListRowH          11			/* one text line's worth of row height */
#define lzListMaxWrapLines   2
#define lzListTopY          16
#define lzListBottomY      (lzListTopY + 11 * lzListRowH)	/* ~11 single-line rows' worth of space */
/* Real redraw bug found live: ListingDraw()'s own erase rectangle only
 * covered down to lzListBottomY (137), but the bottom button bar in
 * MainEn.rcp (frmMain) doesn't start until y=145 - the 8px gap between them
 * was never erased by ListingDraw NOR touched by any native resource
 * object, so a full-screen sub-form's leftover pixels there (looking like
 * stale content "at the last row") survived every return to frmMain
 * regardless of the earlier ListingDrawPageIndicator fix. Erase-only bound,
 * separate from lzListBottomY (still used for row-fitting/scroll-limit
 * math) - 1px clearance above the button row. */
#define lzListEraseBottomY  144

/* Title-bar "N of M" page-indicator text slot - sits between the two
 * REPEATBUTTON arrows at MainEn.rcp IDs btnMainPrevPage (91,0,11,13) and
 * btnMainNextPage (147,0,12,13). Worst-case text is short (thousands of
 * pages would still only be a handful of digits) so this comfortably
 * fits. */
#define lzPageIndicatorX   106
#define lzPageIndicatorY    1
#define lzPageIndicatorW   45
#define lzPageIndicatorH   11

/* Title-bar Cut-buffer indicator slot - same y=1 strip as the page
 * indicator above, in the unclaimed gap between the static "LazyFTP" title
 * text and the page-indicator block (which starts at x=106). Sized to match
 * the existing small bitmaps (folder.bmp/cardup.bmp, ~10-11px square).
 * X=84, not 90 - real-device/live testing found 90 clipped into
 * btnMainPrevPage (the "<" REPEATBUTTON, AT (91,0,11,13) in MainEn.rcp,
 * right next door) - shifted 6px left to clear it. */
#define lzCutIndicatorX    84
#define lzCutIndicatorY     1
#define lzCutIndicatorW    11
#define lzCutIndicatorH    11

/* Built-in symbol font glyphs (see project memory sdcard-glyph-0x1a-
 * stdfont-not-symbolfont.md for why these need verifying live rather than
 * assumed) - generic file vs. a recognized plain-text file. */
#define lzGlyphFile        0x0B
#define lzGlyphTextFile    0x09

/* ---- current-page window (RAM) --------------------------------------
 * gListPageBuf is the ONLY entry-data buffer, lzListPageSize entries
 * (MemPtrNew'd once, reused) - deliberately not doubled with a second
 * "canonical unfiltered" copy (an earlier version of this design did,
 * ~73KB combined, and ran out of heap on a real memory-constrained device
 * even for a 3-entry directory). Meaning depends on mode:
 *   - cached (gListCacheActive): holds exactly the current page/filter-
 *     batch, already in display order - gListWindowCount == gListVisibleCount.
 *   - small directory: holds ALL ingested entries (ingest writes directly
 *     into it, no separate store) - gListWindowCount is the TOTAL, which
 *     may exceed gListVisibleCount once a filter narrows what's actually
 *     shown. Filtering here maps display row -> gListPageBuf slot via the
 *     small internal gFilterSlots[] table instead of physically copying
 *     matches into a second buffer - see LzListEntry(). */
extern FtpDirEntry *gListPageBuf;
extern UInt16       gListWindowCount;	/* entries physically valid in gListPageBuf */
extern UInt16       gListVisibleCount;	/* how many are actually shown right now (<= gListWindowCount); Draw/HitTest/Scroll iterate this */
extern UInt16       gListTop;			/* first visible entry within the window (scroll offset) */

/* Resolves display index idx (0..gListVisibleCount-1) to its entry -
 * transparently handles the small-dir+filter indirection above. A real
 * function (not a macro) so it can branch on that; called across a
 * segment boundary like anything else in Listing.h. */
FtpDirEntry *LzListEntry(UInt16 idx) SEC003;

/* ---- on-device cache DB (large directories only) ---------------------
 * "LazyFTPDBCache" - record 0 is a ListCacheInfo metadata record
 * (identifies which remote directory this cache represents + when it was
 * built); records 1..N are FtpDirEntry data, one per remote entry, in
 * listing order. Only created when a directory turns out to have more
 * than lzListPageSize entries - see ListingIngestEntry. */
typedef struct {
	char   server[ftpNameLen];
	UInt16 port;
	char   path[ftpPathLen];
	UInt32 cachedAtTick;
} ListCacheInfo;

#define lzCacheDbName    "LazyFTPDBCache"
#define lzCacheDbType    ((UInt32) 'DATA')

/* Runtime-configurable staleness window - Options > Preferences' "Large
 * Dir Cache TTL" popup (PrefsForm.h) sets this; defaults to 30s until
 * prefs are loaded. */
extern UInt16 gListCacheTtlSecs;

extern Boolean gListCacheActive;	/* true once a listing spilled to the cache DB */
extern UInt32  gListCacheLeftTick;	/* TimGetTicks() when we last navigated AWAY
									   from the directory the cache represents;
									   0 while we're still "in" it. Starts the
									   staleness clock - see ListingCacheMarkLeft. */

/* True if an existing cache DB already represents (server,port,path) and
 * is still fresh (built, or last revisited, within gListCacheTtlSecs) - if
 * so, FtpList can skip the network fetch entirely. */
Boolean ListingCacheValidFor(const char *server, UInt16 port, const char *path) SEC003;

/* Activates the existing (already validated via ListingCacheValidFor) DB
 * as the current listing's backing store - sets gListCacheActive, resets
 * gListPage, and loads page 0 into gListPageBuf. Refreshes the cache's own
 * cachedAtTick (revisiting within the window keeps it alive) and clears
 * gListCacheLeftTick (we're "in" it again). */
void ListingCacheActivateExisting(const char *server, UInt16 port, const char *path) SEC003;

/* Call when navigating AWAY from whatever directory the active cache
 * represents (a different CWD, disconnect, etc.) - starts the 30s
 * staleness clock rather than deleting the cache immediately, so tapping
 * into a folder and right back out still hits the fast path. Does nothing
 * if no cache is active. */
void ListingCacheMarkLeft(void) SEC003;

/* Folder > Refresh - forces the NEXT load of the current directory to
 * re-fetch from the server even if a fresh cache would otherwise apply. */
void ListingCacheForceStale(void) SEC003;

/* Set by ListingIngestEntry on the (rare) failure paths: gListAllBuf/
 * gListPageBuf failing to allocate at all, or a cache DB write failing
 * (real storage exhaustion). gListOutOfMemory distinguishes "ran out of
 * heap/storage" from a hypothetical future hard cap, matching the same
 * "report the real cause honestly" principle as everywhere else in this
 * app - gListTruncatedAt records how many entries were successfully
 * ingested before the failure, for the error message. */
extern Boolean gListTruncated;
extern Boolean gListOutOfMemory;
extern UInt32  gListTruncatedAt;

/* ---- ingest (writer side, driven by FtpList) --------------------------
 * FtpList calls ListingIngestBegin() once before its parse loop, then
 * ListingIngestEntry() per parsed line, then ListingIngestEnd() once after
 * - these decide transparently whether the listing stays in RAM (<=512
 * entries: exactly the original small-directory behavior, gListPageBuf
 * holds everything, no DB touched) or spills to the cache DB (>512:
 * gListPageBuf's first 512 already-ingested entries are flushed to DB
 * records 1..512, gListCacheActive is set, and every further entry is
 * written directly to a new DB record instead of RAM - page 0 stays
 * exactly what's already sitting in gListPageBuf, no re-read needed). */
void ListingIngestBegin(const char *server, UInt16 port, const char *path) SEC003;
Boolean ListingIngestEntry(const FtpDirEntry *e) SEC003;
/* Returns the TRUE total entries ingested (RAM count, or cache DB record
 * count once spilled) - use this, not gListWindowCount, for the initial
 * ListingUpdatePaging() call after a fresh fetch. */
UInt32 ListingIngestEnd(void) SEC003;

/* Frees/resets in-RAM listing state (gListWindowCount, gListCacheActive)
 * for a fresh listing - does NOT touch the on-disk cache DB itself (that
 * persists deliberately, see gListCacheLeftTick/gListCacheTtlSecs). Call
 * before ListingIngestBegin() (FtpList does this itself) and anywhere the
 * on-screen listing is being invalidated outside of a fresh FtpList call
 * (disconnect, form reopen). */
void ListingReset(void) SEC003;

/* ---- filter + paging (reader side, driven by the UI) ------------------
 * Live filename filter (same idiom as Address35's card-mode filename
 * filter). For a small (RAM-resident) directory, filtering happens
 * in-place over gListPageBuf/gListWindowCount and is always exactly 1
 * page (matches, if any, all fit in the one window already held).  For a
 * large (cache-backed) directory, ListingUpdatePaging()/PrevPage()/
 * NextPage() do a full DB scan applying the filter as they go, so a
 * filter genuinely searches the ENTIRE cached directory (not just
 * whatever page happens to be loaded), and paginates the actual match
 * count - "1 of 1" for a single match anywhere in a 1660-entry directory,
 * same as a small directory. */
#define lzFilterMaxLen 60
extern char     gListFilterTerm[lzFilterMaxLen + 1];
extern Boolean  gListFilterActive;

extern UInt16 gListPage;			/* 0-based current page */
extern UInt16 gListPageCount;		/* ceil(true relevant count / lzListPageSize); 0 if no matches */

/* ---- two-tap select (reader side, driven by the UI) --------------------
 * First tap on a row selects it (highlight only); a second tap on the
 * ALREADY-selected row performs today's action (open the file-action sheet,
 * or CWD into a directory). Identified by name, not gListPageBuf index - see
 * ListHit's own comment below for why an index can't survive a page/filter/
 * refresh change. */
extern Boolean gListSelActive;
extern char    gListSelName[ftpNameLen];
extern Boolean gListSelIsDir;

/* Clears the selection (no row highlighted) - called from ListingReset and
 * MainRefreshList's chokepoint (see MainForm.c) after anything that reloads
 * gListPageBuf's CONTENTS (navigation, delete, rename, new/del folder,
 * refresh). Deliberately NOT called from ListingUpdatePaging (fires on every
 * filter keystroke) - a still-visible-after-filtering selection should stay
 * selected; ListingIsSelected's name-match already makes a filtered-out
 * selection harmlessly stop highlighting on its own. */
void ListingClearSelection(void) SEC003;
void ListingSetSelection(const char *name, Boolean isDir) SEC003;
Boolean ListingIsSelected(const char *name) SEC003;

/* Recomputes gListPageCount and loads gListPageBuf with page 0's data
 * (applying the current filter, RAM-in-place or DB-scan as appropriate) -
 * call once after a fresh listing (ListingIngestEnd) or after
 * gListFilterTerm/gListFilterActive changes. Resets gListTop/gListPage to
 * 0 and draws the "N of M" indicator (see ListingDrawPageIndicator).
 * Does NOT redraw the list itself - still call ListingDraw() after. */
void ListingUpdatePaging(void) SEC003;

/* Move to the previous/next page if one exists (no-op + returns false at
 * either end); reloads gListPageBuf for the new page (RAM-in-place or a
 * fresh DB scan+load), resets gListTop to 0, and redraws the indicator on
 * a real move. Does NOT redraw the list itself - the caller (the arrow
 * button handlers) still calls ListingDraw() afterward. */
Boolean ListingPrevPage(void) SEC003;
Boolean ListingNextPage(void) SEC003;

/* Draws the "N of M" text into the title bar's page-indicator slot
 * (between the two arrow buttons) and enables/disables the arrow buttons
 * to match whether a previous/next page actually exists. Only touches
 * that slot, never the rest of the title bar ("lazyFTP" stays put).
 * Called by ListingUpdatePaging()/ListingPrevPage()/ListingNextPage() -
 * only call directly if drawing the indicator without changing paging
 * state (e.g. right after FrmDrawForm on frmOpenEvent). */
void ListingDrawPageIndicator(void) SEC003;

/* Draws (or, if nothing is cut, erases) the small Cut-buffer indicator icon
 * in the title bar - erase-then-conditionally-redraw, same idiom as
 * ListingDrawPageIndicator, and needs the SAME call-site discipline: call
 * it everywhere ListingDrawPageIndicator() is called, or a full-screen
 * sub-form's return will leave this slot stale/wrong (the exact bug class
 * the page indicator itself hit before this one existed). Reads the cut
 * buffer via FileActionsHasCut() (FileActions.h) - kept as a query rather
 * than duplicating the buffer's own active flag here. */
void ListingDrawCutIndicator(void) SEC003;

/* True if a synthetic "up to parent" row should be shown as row 0 (i.e.
 * connected and not already at the FTP root). */
Boolean ListingHasUpRow(void) SEC003;

void ListingDraw(void) SEC003;
void ListingSyncScrollbar(void) SEC003;
void ListingScroll(Int16 delta) SEC003;

/* Result of a tap: which row it landed on. */
typedef enum {
	kListHitNone = 0,
	kListHitUp,
	kListHitEntry
} ListHitKind;

typedef struct {
	ListHitKind kind;
	UInt16 entryIndex;		/* valid only when kind == kListHitEntry - a
							   gListPageBuf slot (0..gListWindowCount-1),
							   NOT a directory-wide index */
} ListHit;

ListHit ListingHitTest(Int16 x, Int16 y) SEC003;

/* Recognized-as-plain-text filename check (extension-based) - shared with
 * the download/preview flow, not just icon selection. */
Boolean ListingIsTextFilename(const char *name) SEC003;

/* "1.2 MB" / "348 KB" / "97 B" style formatting. */
void ListingFormatSize(UInt32 bytes, char *out, UInt16 outLen) SEC003;

#endif
