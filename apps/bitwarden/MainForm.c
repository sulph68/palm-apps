/*
 * MainForm.c - folder/org popup + type popup + list + live search,
 * modeled on Address35's own MainForm.c (native TABLE + customTableItem
 * draw callback, TblSetRowID to carry the real store index per visible
 * row) but deliberately leaner: no SD-card browsing, no Me-record
 * pinning, no incremental find-narrowing cache - none of those apply to a
 * read-only synced vault. A personal vault is realistically small enough
 * that a full linear rescan per keystroke (decrypting only the currently-
 * filtered items' names, never the whole vault) stays fast - AES-128-CTR
 * is already proven fast at 100+ record scale elsewhere in this project.
 *
 * Top-right popup (popMainFolder): "All", then "Unfiled" + every personal
 * Folder, then (if the account has any organizations) a dotted-line
 * separator followed by every Organization - folder and org filters are
 * mutually exclusive (an item is one or the other, never both, per
 * PROTOCOL.md/Bitwarden's own model), so picking one clears the other.
 *
 * Bottom row (per explicit design instruction): [type filter popup] [live
 * search field] [Clear button].
 */
/* Needed for the mniArchive label toggle below (MenuItemType.itemStr) -
 * this ABI is frozen (old fixed-ROM hardware, never moving to a newer OS
 * release), so the header's own "will not be available in the next OS
 * release" warning doesn't apply here. No official MenuMgr call replaces an
 * existing item's text in place (MenuAddItem only ever INSERTS a new item,
 * which would leave a stale duplicate behind on every menu open). */
#define ALLOW_ACCESS_TO_INTERNALS_OF_MENUS
#include "BitWarden.h"
#include "enccore.h"

/* ---- screen-direct busy toast, matching lazyFTP's own LzTPBusyBegin/End
 * (apps/lazyFTP/BusyDialog.c) exactly - a centered box sized to the title
 * text, WinSaveBits/WinRestoreBits so whatever's underneath (the previous
 * form's own content, in this app's two uses) isn't disturbed. No FORM
 * resource, no buttons, no event pump - caller shows it right before a
 * blocking pass and tears it down right after. Used for "Loading..."
 * (MainOpen, real-device feedback: the list can take a moment to populate)
 * and "Searching..." (MainSearchTermChanged's debounced fire, see its own
 * comment). */
#define bwToastPadX	10
#define bwToastH	24
#define bwToastMarg	4
static WinHandle gToastSaved = NULL;
static RectangleType gToastSaveR;

static void BwToastBegin(const char *title)
{
	RectangleType r;
	WinHandle prevDraw;
	Err err;
	Int16 w, len = (Int16) StrLen(title);

	prevDraw = WinGetDrawWindow();
	WinSetDrawWindow(WinGetDisplayWindow());

	FntSetFont(boldFont);
	w = FntCharsWidth((Char *) title, len);
	r.extent.x = (Int16) (w + 2 * bwToastPadX);
	r.extent.y = bwToastH;
	r.topLeft.x = (Int16) ((160 - r.extent.x) / 2);
	r.topLeft.y = (Int16) ((160 - r.extent.y) / 2);

	gToastSaveR.topLeft.x = (Int16) (r.topLeft.x - bwToastMarg);
	gToastSaveR.topLeft.y = (Int16) (r.topLeft.y - bwToastMarg);
	gToastSaveR.extent.x = (Int16) (r.extent.x + 2 * bwToastMarg);
	gToastSaveR.extent.y = (Int16) (r.extent.y + 2 * bwToastMarg);

	gToastSaved = WinSaveBits(&gToastSaveR, &err);
	WinEraseRectangle(&r, 3);
	WinDrawRectangleFrame(dialogFrame, &r);
	WinDrawChars((Char *) title, len, r.topLeft.x + bwToastPadX, r.topLeft.y + 6);

	WinSetDrawWindow(prevDraw);
}

static void BwToastEnd(void)
{
	if (gToastSaved) {
		WinHandle prevDraw = WinGetDrawWindow();
		WinSetDrawWindow(WinGetDisplayWindow());
		WinRestoreBits(gToastSaved, gToastSaveR.topLeft.x, gToastSaveR.topLeft.y);
		WinSetDrawWindow(prevDraw);
		gToastSaved = NULL;
	}
}

#define kMainCol	0
#define bwFilterAll	0xFFFE	/* "don't restrict on this dimension" - distinct
							 * from bwFolderNone/bwOrgNone (0xFFFF, "item has
							 * none"), which is itself a real filterable
							 * state ("Unfiled"). */

/* See MainSearchExecute's own comment (below) for the full story - defined
 * up here so MainRebuildMatches' own findActive check can use the same
 * threshold. */
#define bwSearchMinChars	2
#define bwSearchDebounceMs	1000

/* ---- top-right folder/org popup ------------------------------------- */
#define bwMaxFilterEntries	96
/* Shrunk from 40 (2026-09) to free real datares budget for Serial.c's own
 * receive buffer (see gSerialRecvBuf's own comment) - low-risk: every write
 * into this already goes through an explicit-capacity copy
 * (BwStoreGetFolderName/BwStoreGetOrgName/StrCopy, sizeof(gFilterNameBuf[0])
 * each time), so a longer name just truncates gracefully in the popup, no
 * overflow. This popup's own on-screen width was already clipping well
 * short of 40 chars regardless. */
static char gFilterNameBuf[bwMaxFilterEntries][24];
static char *gFilterNamePtrs[bwMaxFilterEntries];
/* Parallel array: what each popup row actually means. kind: 0=All,
 * 1=folder, 2=org, 3=separator (its own row exists only for visual
 * grouping - re-selecting it is treated as a no-op, see popSelectEvent). */
static UInt8 gFilterKind[bwMaxFilterEntries];
static UInt16 gFilterIndex[bwMaxFilterEntries];
static UInt16 gFilterCount = 0;
static UInt16 gFilterSelRow = 0;	/* currently-selected popup row, for the no-op-on-separator revert */

static UInt16 gFolderFilter = bwFilterAll;
static UInt16 gOrgFilter = bwFilterAll;

/* Toggled via the Options menu's "Archived Items" entry (mniArchive,
 * MainFormHandleEvent below); when true, the list shows ONLY archived items
 * instead of only non-archived ones - a fourth filter dimension alongside
 * folder/org/type. Reset lifecycle matches gTypeFilter/gFolderFilter/
 * gOrgFilter (gMainFiltersNeedReset, below) - only a true fresh launch
 * clears it back to false. */
static Boolean gArchiveFilter = false;

/* Real bug (2026-08 user report): "last category and type not saved after
 * viewing item" - MainOpen (below) unconditionally reset gFolderFilter/
 * gOrgFilter/gTypeFilter/search on EVERY frmOpenEvent, and frmOpenEvent
 * fires again every time RecordForm.c's own Done button returns here via
 * FrmGotoForm(frmMain) - indistinguishable, at the event level, from a
 * true fresh app launch. One-shot flag (same idea as Address35's own
 * gMainDbFreshFromLaunch) - true only for the very first frmMain open
 * this app session; a static's initializer value is naturally restored on
 * every real cold launch (PalmOS tears down and reloads app globals
 * between separate launches), so this doesn't need any explicit reset in
 * StartApplication. */
static Boolean gMainFiltersNeedReset = true;

/* ---- bottom-left item-type popup -------------------------------------- */
static const char *kTypeNames[] = { "All", "Login", "Note", "Card", "Identity" };
static const UInt8 kTypeValues[] = { 0, bwTypeLogin, bwTypeNote, bwTypeCard, bwTypeIdentity };
#define bwTypeFilterCount	5
static UInt8 gTypeFilter = 0;	/* 0 = All */

static char gFindTerm[32];

/* Scrolling: index into the currently-filtered set (not a raw store index)
 * of the first visible row - reset to 0 whenever any filter/search term
 * changes, matching Address35's own gTopVisibleRecord/gFindTop idiom
 * (simpler here since there's no card mode / Me-pin to special-case). */
static UInt16 gListTop = 0;

UInt16 gRecordIndex = 0;

static void *GetObjectPtr(UInt16 objID)
{
	return FrmGetObjectPtr(FrmGetActiveForm(), FrmGetObjectIndex(FrmGetActiveForm(), objID));
}

static TableType *MainTable(void) { return (TableType *) GetObjectPtr(tblMain); }
static ScrollBarType *MainScrollBar(void) { return (ScrollBarType *) GetObjectPtr(sclMain); }

/* Static, not stack-local: this project has hit real stack-overflow
 * crashes from exactly this pattern before (a sizable buffer as a
 * loop-local inside a function called repeatedly, deep in a call chain -
 * see Sync.c's own gSyncItem/Store.c's gRecBuf) - MainDrawRow is called
 * once per visible row, every table redraw, from deep inside
 * EventLoop -> FrmDispatchEvent -> ... -> TblDrawTable. POSE64 testing
 * showed exactly this bug's usual signature: seemingly-random single-byte
 * corruption in the drawn text (real-device stack overflows are often more
 * silent/subtle than an outright crash). */
static char gMainDrawRowName[bwMaxField];

/* Per-item-type row icon (icon/mkicon_types.py, 11x7 1-bit Tbmp - the
 * user's own art) drawn left of the title, matching lazyFTP/Address35's own
 * proven "small Tbmp icon" idiom (DmGetResource/WinDrawBitmap/release).
 * Silent (draws nothing) for an unrecognized type or a missing resource -
 * matches this suite's other icon-draw helpers, never worth crashing over. */
#define bwRowIconW	11
#define bwRowIconGap	2
/* Wraps BwSyncRun with its start/end chime - SEC001 so this (and its own
 * calls into BwPlaySyncChime/BwSyncRun, both defined in a different file,
 * Sync.c - always a safe direction) don't grow main .text, which has
 * essentially no headroom left (see bw_seg.h). MainFormHandleEvent itself
 * stays untagged/main and calls this same-file SEC001 helper - the safe
 * direction (main calling into SEC001 in the same file), mirrored from
 * Sync.c's own BwSyncRun/ShowLostContactAlert pairing. */
static Boolean SyncNowWithChime(void) SEC001;

static Boolean SyncNowWithChime(void)
{
	Boolean ok;
	BwPlaySyncChime(true);
	ok = BwSyncRun();
	BwPlaySyncChime(false);
	gBwSyncOkPending = ok;
	return ok;
}

static void MainDrawTypeIcon(UInt8 itemType, Coord x, Coord y) SEC001;

static void MainDrawTypeIcon(UInt8 itemType, Coord x, Coord y)
{
	UInt16 bmpId;
	MemHandle h;
	switch (itemType) {
	case bwTypeLogin:    bmpId = bmpTypeLogin; break;
	case bwTypeNote:     bmpId = bmpTypeNote; break;
	case bwTypeCard:     bmpId = bmpTypeCard; break;
	case bwTypeIdentity: bmpId = bmpTypeIdentity; break;
	default: return;
	}
	h = DmGetResource(bitmapRsc, bmpId);
	if (h) {
		BitmapPtr b = (BitmapPtr) MemHandleLock(h);
		WinDrawBitmap(b, x, y);
		MemHandleUnlock(h);
		DmReleaseResource(h);
	}
}

static void MainDrawRow(void *table, Int16 row, Int16 column, RectangleType *bounds) SEC001;

static void MainDrawRow(void *table, Int16 row, Int16 column, RectangleType *bounds)
{
	UInt16 recordIndex;
	char *name = gMainDrawRowName;
	Coord textX = bounds->topLeft.x;
	Coord textW = bounds->extent.x;
	(void) column;
	FntSetFont(stdFont);
	/* This row can legitimately draw DIFFERENT content across separate
	 * redraw passes (masked "******" while Sec35 is locked, the real
	 * decrypted name once it's unlocked; a shorter name over a previously
	 * longer one as the list is filtered) - neither branch below erases
	 * first, so without this the old pass's pixels stayed visible
	 * underneath/around the new text (real-device/POSE64 feedback: showed
	 * up as garbled leading characters like "*est Note Item" or entire
	 * rows truncated to one character). */
	WinEraseRectangle(bounds, 0);
	recordIndex = TblGetRowID((TableType *) table, row);
	/* itemType is a plaintext header byte (Store.c's own on-disk layout -
	 * not one of the encrypted fields), so the icon can show even while
	 * locked - only the NAME needs the mask below. BwStoreGetCachedHeader
	 * (not BwStoreGetItemType) - O(1) via the header cache, same fix as
	 * the "scroll is very slow" one just below: BwStoreGetItemType's own
	 * NthItemDbIndex scan, paid once per VISIBLE ROW on every redraw, was
	 * exactly the per-row O(n) cost this whole cache exists to avoid. */
	{
		Coord iconY = (Coord) (bounds->topLeft.y + (FntLineHeight() - 7) / 2);
		UInt8 type; UInt16 folder, org; Boolean archived;
		if (BwStoreGetCachedHeader(recordIndex, &type, &folder, &org, &archived)) MainDrawTypeIcon(type, bounds->topLeft.x, iconY);
		textX = (Coord) (bounds->topLeft.x + bwRowIconW + bwRowIconGap);
		textW = (Coord) (bounds->extent.x - bwRowIconW - bwRowIconGap);
	}
	/* Every field is wrapped by BOTH keys (Sec35's outer layer, this app's
	 * own local-vault inner layer - see Store.c's own top-of-file note) -
	 * decrypt needs BOTH present, so the mask needs to trigger on EITHER
	 * being unavailable, not just Sec35's. Real-device/POSE64 feedback
	 * (2026-08): checking only EncKeyPresent() left this blind to the
	 * app's OWN key simply expiring (its 5-minute default TTL, same as
	 * every other per-app session key in this suite) or "Lock Now" being
	 * used - EncKeyPresent() stays true in both cases (Sec35 itself isn't
	 * touched), so this fell through into a decrypt that silently failed,
	 * showing entirely BLANK rows (not even the masked placeholder) - the
	 * exact "list doesn't redraw" symptom reported after Clear, once
	 * enough browsing time had passed for the local key to expire mid-
	 * session. */
	if (!EncKeyPresent() || !BwIsUnlocked()) {
		/* Locked (either layer) - don't even attempt the decrypt, just
		 * show the masked placeholder like every other Sec35-wrapped
		 * record list in this suite (see CryptoNote's own chokepoint
		 * masking). */
		WinDrawChars("******", 6, textX, bounds->topLeft.y);
		return;
	}
	/* Cache first (instant, no crypto) if a search has already built it
	 * this session - see BwStoreGetCachedNameIfReady's own comment on why
	 * it won't force a build here. Falls back to the real per-row decrypt
	 * otherwise (a plain list open before any search, or the cache
	 * allocation failed) - same as every redraw before this cache existed.
	 * Real-device feedback (2026-08): even after search matching itself
	 * got cached, every visible row was STILL being re-decrypted from
	 * scratch on every redraw (which happens every keystroke), since this
	 * draw call never consulted the cache at all - a real remaining chunk
	 * of "search still isn't instant" that had nothing to do with the
	 * match-finding logic itself. */
	if (!BwStoreGetCachedNameIfReady(recordIndex, name, bwMaxField)
			&& !BwStoreGetItemName(recordIndex, name, bwMaxField))
		return;
	WinDrawTruncChars(name, (Int16) StrLen(name), textX, bounds->topLeft.y, textW);
}

/* Case-insensitive substring test - Store.c's own name cache already
 * stores lowercase, so only the needle needs lowercasing here. Reads the
 * cache directly (BwStoreGetCachedLowerNamePtr) rather than copying it out
 * first - this is called once per real item on every non-extending search
 * keystroke, the hottest loop in the app. */
static Boolean NameMatches(UInt16 recordIndex, const char *needleLower)
{
	const char *cached = BwStoreGetCachedLowerNamePtr(recordIndex);
	return cached ? StrStr(cached, needleLower) != NULL : false;
}

/* No TblGetNumRows exists in the Table Manager API - the row CAPACITY is
 * only ever known from the .rcp's own ROWS count, so (as Address35's own
 * MainFitRows does) compute how many rows actually fit the table's real
 * pixel bounds instead of hardcoding it twice (once in the .rcp, once
 * here) - tblMain's ROWS in BitWardenEn.rcp is set generously enough to
 * cover this at any reasonable font. */
static UInt16 MainRows(void)
{
	TableType *tableP = MainTable();
	RectangleType r;
	Int16 lh = FntLineHeight();
	UInt16 fit;
	TblGetBounds(tableP, &r);
	if (lh < 1) lh = 11;
	fit = (UInt16) (r.extent.y / lh);
	return fit < 1 ? 1 : fit;
}

/* Must be >= BitWardenEn.rcp's own tblMain "ROWS N" - any RCP-declared row
 * beyond what MainRows() computes as actually fitting is otherwise never
 * touched by this function at all, left showing pilrc's compiled default
 * row style (an empty checkbox glyph - the exact "table draws before rows
 * load" symptom this project has hit and documented before). Clearing all
 * the way to this bound, not just to MainRows()'s fit count, is what
 * actually fixes it. */
#define bwMainRowsRcp	14

/* Header-only filter test - MainRebuildMatches' own raw single pass takes
 * the already-read type/folder/org directly rather than each doing its own
 * BwStoreGetItem{Type,Folder,Org} lookup (see BwStoreGetHeaderByDbIndex's
 * own comment on why looping those was O(n^2) at real vault scale). Was a
 * separate PassesAllFiltersRaw function called from MainRebuildMatches -
 * now inlined directly into that loop instead (see its own comment on why:
 * a same-file segment-crossing call crashed live in POSE64). */

/* Cached set of store indices passing the CURRENT folder/org/type filters
 * and search term - real-device feedback at real vault scale (779 items):
 * the old design re-scanned+re-decrypted the WHOLE store from
 * MainLoadRows AND AGAIN from MainCountMatching (a redundant second full
 * pass) on EVERY keystroke, EVERY filter change, and EVERY scroll -
 * quadratic-feeling and, at real hardware speed, an outright multi-second
 * hang per interaction. Mirrors Address35's own proven MainBuildFind/
 * MainNarrowFind pattern (see that file's own comment): one O(total
 * records) scan builds this cache when a filter genuinely changes;
 * MainLoadRows/MainUpdateScrollBar then just read from it (O(visible rows)
 * / O(1)), and scrolling alone never rebuilds at all. bwMaxCachedMatches
 * is a generous static cap (matches this project's static-over-dynamic
 * philosophy, e.g. bwMaxFilterEntries) - a vault larger than this still
 * works, just stops finding additional matches past the cap rather than
 * growing unbounded. 900 (1.8KB) comfortably covers the real vault this
 * was diagnosed against (779 items + a handful of folders/orgs) - had to
 * come down from an initial 2048 after hitting "region datares is full"
 * on real build, so this is deliberately not padded much further. */
#define bwMaxCachedMatches	900
static UInt16 gMatchIndices[bwMaxCachedMatches];
static UInt16 gMatchCount = 0;
static char gMatchBuiltTerm[32] = "";	/* gFindTerm the cache currently reflects - see MainSearchTermChanged */

/* Full O(total records) rescan - the only expensive pass in ordinary use
 * now. Called whenever folder/org/type actually changes, the search term
 * changes in a way that ISN'T a simple extension (see
 * MainSearchTermChanged), or the underlying store content itself could
 * have changed (a fresh sync, or first form open).
 *
 * Walks RAW db indices in one pass (like BwStoreCount) and tracks its own
 * running recIndex ("Nth real item") count, rather than looping recIndex
 * and asking Store.c to re-resolve it back to a db index every call - see
 * BwStoreGetHeaderByDbIndex's own comment: that redundant resolution (once
 * per BwStoreGetItemType/Folder/Org call, all three previously called from
 * inside this same loop) turned a single filter-rebuild into a real O(n^2)
 * hang at 780-item vault scale (~136s in POSE64), on top of - and
 * independent from - the original per-row decrypt cost this cache was
 * already built to fix. gMatchIndices[] still stores recIndex values
 * (every other caller - row open, delete, etc. - expects that), just
 * computed by counting forward instead of re-deriving per record. */
/* SysQSort comparator - alphabetical (case-insensitive) row order, the
 * user's own ask. gMatchIndices holds recIndex values; gNameCache is
 * already lowercase (built for search matching), so comparing those
 * directly is a case-insensitive sort "for free" - no extra lowercasing
 * needed here. BwStoreGetCachedLowerNamePtr force-builds the name cache on
 * its own first call if it isn't already built (a real, once-per-session
 * full-vault decrypt - unavoidable: sorting by name inherently requires
 * knowing every matching item's name), then every subsequent comparator
 * call during this same sort is a cheap cache hit. */
static Int16 MatchNameCompare(void *a, void *b, Int32 other) SEC001;

static Int16 MatchNameCompare(void *a, void *b, Int32 other)
{
	UInt16 ia = *(UInt16 *) a;
	UInt16 ib = *(UInt16 *) b;
	const char *na = BwStoreGetCachedLowerNamePtr(ia);
	const char *nb = BwStoreGetCachedLowerNamePtr(ib);
	(void) other;
	return (Int16) StrCompare(na ? na : "", nb ? nb : "");
}

static void MainRebuildMatches(void) SEC001;

/* PassesAllFiltersRaw/NameMatches's own logic is inlined directly below
 * rather than called - both are same-file (MainForm.c) static functions,
 * and a SEC001-tagged function calling back into a same-file main-segment
 * function crashed live in POSE64 testing (see BwDebugEstablishAndDumpKeys's
 * own comment in Store.c and RecordTotpTick's own comment in RecordForm.c -
 * narrowed to specifically this direction, same file, regardless of
 * static/extern linkage). Both are small enough that duplicating them here
 * is cheap; BwStoreGetCachedLowerNamePtr/StrStr/StrCompare etc are all
 * either different-file or ROM traps, safe regardless of segment. */
static void MainRebuildMatches(void)
{
	UInt16 total = BwStoreGetCachedCount();
	UInt16 recIndex;
	char needleLower[bwNameCacheEntryLen];
	Boolean findActive = StrLen(gFindTerm) >= bwSearchMinChars;

	if (findActive) StrToLower(needleLower, gFindTerm);
	gMatchCount = 0;
	for (recIndex = 0; recIndex < total && gMatchCount < bwMaxCachedMatches; recIndex++) {
		UInt8 type; UInt16 folder, org; Boolean archived;
		Boolean passes;
		if (!BwStoreGetCachedHeader(recIndex, &type, &folder, &org, &archived)) continue;
		if (archived != gArchiveFilter) continue;
		if (gFolderFilter != bwFilterAll) {
			passes = (org == bwOrgNone && folder == gFolderFilter);
		} else if (gOrgFilter != bwFilterAll) {
			passes = (org == gOrgFilter);
		} else {
			passes = true;
		}
		if (passes && gTypeFilter != 0 && type != gTypeFilter) passes = false;
		if (passes && findActive) {
			const char *cached = BwStoreGetCachedLowerNamePtr(recIndex);
			passes = cached ? (StrStr(cached, needleLower) != NULL) : false;
		}
		if (passes)
			gMatchIndices[gMatchCount++] = recIndex;
	}
	/* Removing entries from an already-sorted array (MainSearchTermChanged's
	 * own in-place narrowing, below) preserves order - only a real rebuild
	 * (here) ever needs to re-sort.
	 *
	 * Real-device report (2026-09): "Lock Now" froze the device instead of
	 * showing a masked list - root cause was THIS sort. MatchNameCompare
	 * calls BwStoreGetCachedLowerNamePtr, which force-builds the name cache
	 * on first use (a real per-item decrypt pass) if it isn't already
	 * built - locked means that build fails every time (no key), but the
	 * failure isn't remembered anywhere, so EVERY SINGLE comparison during
	 * the sort retried the full failed build from scratch. At real vault
	 * scale (hundreds of items x O(n log n) comparisons x 2 lookups each),
	 * that's thousands of repeated failed decrypt attempts - not a true
	 * infinite loop, but slow enough on real hardware to be indistinguishable
	 * from one, and made worse by the "locked = trivial redraw" assumption
	 * (MainRefresh's own comment) that skipped the one thing that would have
	 * at least shown a "Processing..." toast instead of looking hung. Sorting
	 * by name is meaningless once every name shows as "******" anyway -
	 * skip it entirely instead of trying to make the failed lookup cheaper. */
	if (gMatchCount > 1 && EncKeyPresent() && BwIsUnlocked())
		SysQSort(gMatchIndices, (Int16) gMatchCount, sizeof(UInt16), MatchNameCompare, 0);
	StrNCopy(gMatchBuiltTerm, gFindTerm, sizeof(gMatchBuiltTerm) - 1);
	gMatchBuiltTerm[sizeof(gMatchBuiltTerm) - 1] = 0;
}

/* Re-filters the EXISTING gMatchIndices[] against the (longer) gFindTerm,
 * in place - folder/org/type are unchanged from when the cache was built
 * (the only caller, MainSearchTermChanged, already verified this), so only
 * the name-match test needs re-checking, and only over the already-
 * narrowed set rather than the whole store. Mirrors Address35's own
 * MainNarrowFind exactly. */
static void MainNarrowMatches(void)
{
	UInt16 i, out = 0;
	char needleLower[bwNameCacheEntryLen];
	StrToLower(needleLower, gFindTerm);
	for (i = 0; i < gMatchCount; i++)
		if (NameMatches(gMatchIndices[i], needleLower))
			gMatchIndices[out++] = gMatchIndices[i];
	gMatchCount = out;
	StrNCopy(gMatchBuiltTerm, gFindTerm, sizeof(gMatchBuiltTerm) - 1);
	gMatchBuiltTerm[sizeof(gMatchBuiltTerm) - 1] = 0;
}

/* The one call site every search-field-changed handler should use:
 * decides narrow vs full-rebuild by whether gFindTerm simply extends
 * whatever the cache was last built/narrowed against (folder/org/type are
 * never touched by typing in this field, so they're always still valid -
 * unlike Address35, which also has to re-check "same category", this form
 * has nothing else that can change out from under a keystroke). */
static void MainSearchTermChanged(void)
{
	UInt16 prevLen = (UInt16) StrLen(gMatchBuiltTerm);
	Boolean extendsPrev = (Boolean) (prevLen > 0
		&& StrLen(gFindTerm) > prevLen
		&& StrNCompare(gFindTerm, gMatchBuiltTerm, prevLen) == 0);
	if (extendsPrev) MainNarrowMatches();
	else MainRebuildMatches();
}

/* ---- debounced search trigger ---------------------------------------
 * Real-device feedback (2026-08): even with the whole match/name/header
 * pipeline cached (see Store.c's own comments), search STILL felt
 * sluggish - because it was re-running on EVERY keystroke, so a fast
 * typist paid for (however small) a rebuild/narrow pass per character
 * instead of once per pause in typing. This defers the actual filter to
 * either (a) the search text dropping back below bwSearchMinChars
 * (treated as "no search", same as before - cheap, header-only, fires
 * immediately since there's nothing to debounce) or (b) bwSearchDebounceMs
 * of no further keystrokes once at/above that length - matching how a
 * real Bitwarden client's own search box behaves, and avoiding the
 * "stutter" of the previous every-keystroke firing entirely, not just
 * making each individual firing faster. bwSearchMinChars/bwSearchDebounceMs
 * live near the top of this file (MainRebuildMatches needs the former). */
static Boolean gSearchPending = false;
static UInt32 gSearchPendingTick = 0;

Boolean MainHasPendingSearch(void)
{
	return gSearchPending;
}

/* Forward decl - defined further down this file, next to MainUpdateScrollBar
 * (its own natural home alongside the rest of the row-drawing helpers).
 * Without this, the implicit-int fallback for the call below silently
 * disagreed with the real `static void` definition later in the file -
 * caught as a compiler warning, not a functional bug (nothing captures a
 * return value at the call site either way), but worth being explicit
 * about rather than relying on that fallback. */
static void MainLoadRows(void);

/* Actually applies the current gFindTerm - called either immediately
 * (below bwSearchMinChars, always cheap - header-only, no crypto) or once
 * the debounce timer fires (bwSearchMinChars+, potentially the first-ever
 * name-cache-build cost this session). The toast/keep-awake pair only
 * wraps the potentially-expensive case - showing "Searching..." for a
 * sub-bwSearchMinChars clear/backspace would just flicker uselessly. */
static void MainSearchExecute(void)
{
	Boolean maybeSlow = StrLen(gFindTerm) >= bwSearchMinChars;
	if (maybeSlow) {
		BwToastBegin("Searching...");
		/* Real-device feedback: the pipeline itself is now fast, but the
		 * debounce wait ABOVE this call already let the device sit
		 * "idle" (no user input) for bwSearchDebounceMs - on a short
		 * auto-off setting, that gap plus whatever this pass itself
		 * takes could be enough to let the device power off right as
		 * the result appears, catching the user mid-read. Reset both
		 * before AND after, matching every other long-ish operation in
		 * this app (Sync.c's own BwKeepDeviceAwake call sites). */
		BwKeepDeviceAwake();
		BwTouchActivity();
	}
	MainSearchTermChanged();
	MainLoadRows();
	if (maybeSlow) BwToastEnd();
	TblRedrawTable(MainTable());
	if (maybeSlow) { BwKeepDeviceAwake(); BwTouchActivity(); }
}

static void MainUpdateScrollBar(UInt16 shown)
{
	ScrollBarType *sb = MainScrollBar();
	Int16 value, max;

	max = (Int16) ((gMatchCount > shown) ? gMatchCount - shown : 0);
	value = (Int16) gListTop;
	if (value > max) value = max;
	gListTop = (UInt16) value;
	SclSetScrollBar(sb, value, 0, max, (Int16) (shown ? shown : 1));
}

static void MainLoadRows(void)
{
	TableType *tableP = MainTable();
	UInt16 rows = MainRows();
	UInt16 r = 0;
	Int16 lineHeight = FntLineHeight();

	/* A row marked NOT usable (below) is simply skipped by the Table
	 * Manager's own draw pass - never erased, never drawn - so whenever
	 * there are fewer matching items than visible rows, whatever was on
	 * screen in that leftover area before (real-device feedback: a record
	 * view's own masked password field, if it happened to land in that
	 * region) keeps showing through indefinitely, not just transiently.
	 * MainDrawRow's own per-row erase only covers rows that actually get
	 * drawn, so it can't fix this - erase the table's whole area up front
	 * instead, matching the same "erase before draw" discipline already
	 * proven there. */
	{
		RectangleType bounds;
		TblGetBounds(tableP, &bounds);
		WinEraseRectangle(&bounds, 0);
	}

	for (; r < rows && (UInt16) (gListTop + r) < gMatchCount; r++) {
		UInt16 idx = gMatchIndices[gListTop + r];
		TblSetRowHeight(tableP, r, lineHeight);
		TblSetItemStyle(tableP, r, kMainCol, customTableItem);
		TblSetRowID(tableP, r, idx);
		TblSetRowUsable(tableP, r, true);
		TblMarkRowInvalid(tableP, r);
	}
	for (; r < bwMainRowsRcp; r++) TblSetRowUsable(tableP, r, false);
	MainUpdateScrollBar(r);
}

/* Move the top-of-list by delta matching-entries (for scrolling) -
 * mirrors Address35's own MainScroll, simplified (no card mode / find-set
 * indirection to special-case here). */
static void MainScroll(Int16 deltaEntries)
{
	Int16 nt = (Int16) gListTop + deltaEntries;
	if (nt < 0) nt = 0;
	gListTop = (UInt16) nt;
	MainLoadRows();
	TblRedrawTable(MainTable());
}

/* ---- top-right folder/org popup content ------------------------------- */
/* The list itself can be as wide as it needs to be (MainLoadFolderPopup's own
 * width-to-content fix below), but the closed POPUPTRIGGER auto-sizes its
 * own label to whatever text it's given with no cap - a long folder/org name
 * grows the trigger leftward far enough to run under the "BitWarden" title
 * (real-device/POSE64 feedback). Cap what's ever handed to CtlSetLabel
 * instead of trying to constrain the trigger control itself. */
#define bwTriggerLabelMax	12
static char gTriggerLabelBuf[bwTriggerLabelMax + 1];

static const char *TruncateForTrigger(const char *name)
{
	UInt16 len = (UInt16) StrLen(name);
	if (len <= bwTriggerLabelMax) return name;
	MemMove(gTriggerLabelBuf, name, bwTriggerLabelMax - 3);
	StrCopy(gTriggerLabelBuf + (bwTriggerLabelMax - 3), "...");
	return gTriggerLabelBuf;
}

static void MainLoadFolderPopup(void)
{
	ListType *lst = (ListType *) GetObjectPtr(lstMainFolder);
	UInt16 folderN = BwStoreFolderCount();
	UInt16 orgN = BwStoreOrgCount();
	UInt16 i;

	gFilterCount = 0;

#define ADD_ENTRY(k, idx, nameSrc) \
	do { \
		if (gFilterCount < bwMaxFilterEntries) { \
			gFilterKind[gFilterCount] = (k); \
			gFilterIndex[gFilterCount] = (idx); \
			nameSrc; \
			gFilterNamePtrs[gFilterCount] = gFilterNameBuf[gFilterCount]; \
			gFilterCount++; \
		} \
	} while (0)

	ADD_ENTRY(0, 0, StrCopy(gFilterNameBuf[gFilterCount], "All"));
	ADD_ENTRY(1, bwFolderNone, StrCopy(gFilterNameBuf[gFilterCount], "Unfiled"));
	for (i = 0; i < folderN && gFilterCount < bwMaxFilterEntries; i++)
		ADD_ENTRY(1, i, BwStoreGetFolderName(i, gFilterNameBuf[gFilterCount], sizeof(gFilterNameBuf[0])));

	if (orgN > 0) {
		ADD_ENTRY(3, 0, StrCopy(gFilterNameBuf[gFilterCount], "- - - - - - - - -"));
		for (i = 0; i < orgN && gFilterCount < bwMaxFilterEntries; i++)
			ADD_ENTRY(2, i, BwStoreGetOrgName(i, gFilterNameBuf[gFilterCount], sizeof(gFilterNameBuf[0])));
	}
#undef ADD_ENTRY

	LstSetListChoices(lst, gFilterNamePtrs, gFilterCount);
	/* Real-device feedback: this list's own compiled height (BitWardenEn.rcp)
	 * is just a placeholder floor - without an explicit LstSetHeight call it
	 * popped up oversized with dead space below a short entry list instead
	 * of matching the actual count. Capped at 10 (comfortably fits under the
	 * 160px screen from this popup's own AT y=1 start) - a vault with more
	 * folders/orgs than that is a pre-existing, separate scroll-worthy
	 * problem this doesn't attempt to solve. */
	LstSetHeight(lst, (gFilterCount > 10) ? 10 : (gFilterCount ? gFilterCount : 1));
	{
		/* Width also isn't fixed by the count alone - unlike the bottom-left
		 * type popup (always the same 5 fixed strings), folder/org names are
		 * real vault data of any length, so the compiled AT width (a generic
		 * placeholder) is either too cramped for a long name or leaves dead
		 * space next to short ones. Measure the widest choice and size to
		 * that instead, keeping the right edge pinned at the screen edge to
		 * match the trigger's own RIGHTANCHOR (see BitWardenEn.rcp). */
		Coord maxW = 0;
		UInt16 i;
		FontID prevFont = FntSetFont(stdFont);
		for (i = 0; i < gFilterCount; i++) {
			Coord w = FntCharsWidth(gFilterNamePtrs[i], (Int16) StrLen(gFilterNamePtrs[i]));
			if (w > maxW) maxW = w;
		}
		FntSetFont(prevFont);
		maxW += 10;	/* list-item left/right padding + selection highlight */
		if (maxW < 30) maxW = 30;		/* floor - keeps very short lists (e.g. just "All") from looking cramped */
		if (maxW > 158) maxW = 158;	/* never past the 2px-margin left screen edge */
		lst->bounds.extent.x = maxW;
		lst->bounds.topLeft.x = 160 - maxW;
	}
	{
		/* Reflect the CURRENT gFolderFilter/gOrgFilter, not always "All" -
		 * same reasoning as MainLoadTypePopup's own fix (row 0 alone made
		 * the trigger's label go stale/wrong once gMainFiltersNeedReset
		 * stopped resetting the filter itself on every open). */
		UInt16 sel = 0, i;
		for (i = 0; i < gFilterCount; i++) {
			if (gFolderFilter != bwFilterAll && gFilterKind[i] == 1 && gFilterIndex[i] == gFolderFilter) { sel = i; break; }
			if (gOrgFilter != bwFilterAll && gFilterKind[i] == 2 && gFilterIndex[i] == gOrgFilter) { sel = i; break; }
		}
		LstSetSelection(lst, (Int16) sel);
		gFilterSelRow = sel;
		CtlSetLabel((ControlType *) GetObjectPtr(popMainFolder), TruncateForTrigger(gFilterNamePtrs[sel]));
	}
}

/* ---- bottom-left item-type popup content ------------------------------ */
static void MainLoadTypePopup(void)
{
	ListType *lst = (ListType *) GetObjectPtr(lstMainType);
	LstSetListChoices(lst, (char **) kTypeNames, bwTypeFilterCount);
	{
		/* Same reasoning as MainLoadFolderPopup's own width fix - the
		 * compiled AT width (BitWardenEn.rcp) is a placeholder, not sized to
		 * these five specific words, and left dead space on the right past
		 * "Identity". Content here is fixed (unlike the folder/org popup)
		 * but still measured rather than hardcoded, so a future wording
		 * change can't silently reintroduce the same dead space. Left edge
		 * stays pinned (this trigger sits at the left, no RIGHTANCHOR). */
		Coord maxW = 0;
		UInt16 i;
		FontID prevFont = FntSetFont(stdFont);
		for (i = 0; i < bwTypeFilterCount; i++) {
			Coord w = FntCharsWidth(kTypeNames[i], (Int16) StrLen(kTypeNames[i]));
			if (w > maxW) maxW = w;
		}
		FntSetFont(prevFont);
		maxW += 10;
		lst->bounds.extent.x = maxW;
	}
	{
		/* Reflect the CURRENT gTypeFilter, not always "All" - otherwise the
		 * trigger's own label goes stale/wrong the moment gMainFiltersNeedReset
		 * stops resetting gTypeFilter itself (returning from a record view
		 * with a non-All filter still active would show "All" in the
		 * trigger while the list stayed filtered). */
		UInt16 sel = 0, i;
		for (i = 0; i < bwTypeFilterCount; i++) {
			if (kTypeValues[i] == gTypeFilter) { sel = i; break; }
		}
		LstSetSelection(lst, (Int16) sel);
		CtlSetLabel((ControlType *) GetObjectPtr(popMainType), kTypeNames[sel]);
	}
}

static void MainRefresh(void)
{
	/* Unconditional rebuild, not the smart narrow-vs-rebuild check - this is
	 * called after a sync, where the underlying store CONTENT changed even
	 * if the filter/search state didn't, so a stale cache would show (or
	 * scroll into) records that no longer exist.
	 *
	 * MainLoadFolderPopup was previously only ever called from MainOpen
	 * (form entry) - real-device feedback (2026-08): "none of the org
	 * filters are showing records" - the normal in-app flow is Options >
	 * Sync Now while frmMain is ALREADY open, which calls this function,
	 * not MainOpen. Without rebuilding the popup here too, its choice list
	 * (gFilterKind/gFilterIndex/gFilterNamePtrs) kept reflecting whatever
	 * folders/orgs existed at the LAST full form open - stale org NAMES
	 * still displayed fine (list widget text doesn't change on its own),
	 * but gFilterIndex[sel] no longer matched the freshly-synced items'
	 * real orgIndex values, so selecting "Org 1" from a stale popup could
	 * silently filter against the WRONG (or a since-removed) org index.
	 * Deliberately does NOT reset gFolderFilter/gOrgFilter/gTypeFilter/
	 * search here (unlike MainOpen) - the user's current view should
	 * survive a re-sync; only the popup's own CHOICE LIST needs refreshing. */
	/* Real-device feedback (2026-09): after a real-size sync, the folder-
	 * popup rebuild + full MainRebuildMatches rescan (a real per-item
	 * decrypt pass at large vault scale, same cost MainOpen's own
	 * "Loading..." toast already covers) took long enough with nothing on
	 * screen to look hung. Only worth the toast when there's actually a
	 * vault to rescan/decrypt - locked (e.g. right after mniLockNow) means
	 * this is a trivial empty-list redraw, same guard MainOpen uses. */
	if (BwIsUnlocked()) {
		BwToastBegin("Processing...");
		MainLoadFolderPopup();
	}
	MainRebuildMatches();
	MainLoadRows();
	if (BwIsUnlocked()) BwToastEnd();
	FrmDrawForm(FrmGetActiveForm());
}

/* Shared by MainOpen (always, on form entry) and tblSelectEvent (opening a
 * record while the local key has EXPIRED mid-session - real-device/POSE64
 * feedback, 2026-08: BwSec35LockedBounce alone only catches Sec35 being
 * locked, not this app's OWN key's separate 5-minute TTL running out while
 * the user is still browsing an already-open list - that showed as a
 * generic "Could not read this item" error instead of a re-prompt, since
 * nothing here checked BwIsUnlocked() before trying to open the record). */
static void MainEnsureLocalUnlock(void) SEC001;

/* mniArchive's own label should describe what TAPPING it does, not the
 * dimension's current name - "Archived Items" while browsing the normal
 * (active) list offers to switch there; once switched, it becomes "Active
 * Items" to offer switching back. Done in menuOpenEvent (fires every time
 * the menu is about to be drawn) rather than at the point gArchiveFilter is
 * flipped, so it stays correct regardless of how that flag got set (also
 * reset to false by MainOpen's own filters-reset block). See this file's
 * own ALLOW_ACCESS_TO_INTERNALS_OF_MENUS comment up top for why this
 * reaches into MenuItemType directly instead of using MenuAddItem (which
 * only ever INSERTS a new item, leaving a stale duplicate behind).
 *
 * Real-device root cause (2026-09): "Fatal Exception" on ~every other Sync
 * Now tap - itemStr was pointed DIRECTLY at a string literal ("Active
 * Items"/"Archived Items"), which the compiler places inside THIS
 * function's own compiled code, i.e. the sec001 code RESOURCE (this
 * function is SEC001-tagged for main-segment budget). That resource isn't
 * guaranteed to stay resident/unmoved once this function returns - a later
 * segment load for some other call can leave it purged or relocated, and
 * the menu bar keeps that now-dangling pointer around until the OS next
 * actually draws the menu (which any later menu interaction - including
 * picking a completely different item like "Sync Now" - can trigger).
 * Fixed by copying into a static (data-segment, never split/relocated in
 * this multi-segment scheme, unlike code) buffer instead and pointing
 * itemStr at THAT - stable regardless of which code segment is currently
 * loaded. Only touches gArchiveFilter (a plain static) and calls a ROM trap
 * (MenuGetActiveMenu) - no same-file call, still safe to tag SEC001. */
static char gArchiveMenuLabel[16];
static void MainUpdateArchiveMenuLabel(void) SEC001;

static void MainUpdateArchiveMenuLabel(void)
{
	MenuBarType *mb = MenuGetActiveMenu();
	if (mb) {
		Int16 mi, ii;
		StrCopy(gArchiveMenuLabel, gArchiveFilter ? "Active Items" : "Archived Items");
		for (mi = 0; mi < mb->numMenus; mi++) {
			MenuPullDownType *pd = &mb->menus[mi];
			for (ii = 0; ii < pd->numItems; ii++) {
				if (pd->items[ii].id == mniArchive) {
					pd->items[ii].itemStr = gArchiveMenuLabel;
				}
			}
		}
	}
}

static void MainEnsureLocalUnlock(void)
{
	char password[128];
	if (BwIsUnlocked() || !BwHasVault()) return;
	{
		Boolean ok = false;
		/* Session-cached password first (set by either this same prompt
		 * or a prior sync's own) - skip prompting if it still decrypts
		 * and unlocks cleanly. BwUnlock's own real AES-stretch cost (2s,
		 * see its own comment in Store.c) applies here too, not just on a
		 * freshly-typed password - a "Working..." toast keeps this from
		 * feeling like a hang on a plain "reopen the list" tap. */
		if (BwGetCachedPassword(password, sizeof(password))) {
			BwProgressShow("Working...");
			ok = BwUnlock(password);
			BwProgressHide();
		} else if (BwHasCachedPassword()) {
			/* A cache exists and its own TTL ("Lock After") hasn't
			 * expired, but BwGetCachedPassword still failed - the only
			 * other reason is Sec35 itself being locked (the cache is
			 * encrypted under Sec35's key). Bounce there instead of
			 * falling straight to a full master-password re-prompt - same
			 * mechanism/pattern as BwSec35LockedBounce (tblSelectEvent,
			 * below): abort this attempt now, Sec35 bounces back here
			 * once unlocked, and frmOpenEvent re-runs this whole check
			 * fresh, this time succeeding via the cache. */
			BwSwitchToSec35();
			MemSet(password, sizeof(password), 0);
			return;
		}
		if (!ok) {
			if (BwUnlockPromptRun(password, sizeof(password))) {
				BwProgressShow("Working...");
				ok = BwUnlock(password);
				BwProgressHide();
				if (ok) BwSetCachedPassword(password);
			}
		}
		if (!ok)
			FrmCustomAlert(altErr, "Locked. Use Menu > Sync to set up the vault, or try again.", NULL, NULL);
		MemSet(password, sizeof(password), 0);
	}
}

static void MainOpen(void)
{
	FormPtr frm = FrmGetActiveForm();
	FieldType *fld = (FieldType *) GetObjectPtr(fldMainFilter);

	/* Consumed unconditionally, BEFORE the BwIsUnlocked() check below - the
	 * flag's job is only to distinguish "true first open this session" from
	 * "returning here again", which has nothing to do with whether the
	 * vault happens to be locked at this exact moment. Gating this on
	 * BwIsUnlocked() (as an earlier version of this fix did) meant it never
	 * consumed at all on a session that's never been unlocked yet (or -
	 * the same bug in miniature - whenever the key's TTL happens to have
	 * expired in the gap between viewing a record and hitting Done), so
	 * every later open kept re-resetting the filters regardless. */
	if (gMainFiltersNeedReset) {
		gFolderFilter = bwFilterAll;
		gOrgFilter = bwFilterAll;
		gTypeFilter = 0;
		gArchiveFilter = false;
		gMainFiltersNeedReset = false;
	}

	MainEnsureLocalUnlock();

	TblSetCustomDrawProcedure(MainTable(), kMainCol, MainDrawRow);
	TblSetColumnUsable(MainTable(), kMainCol, true);

	/* MainLoadTypePopup - unlike MainLoadFolderPopup - never needed the
	 * vault unlocked at all: its choices are fixed strings (Login/Note/
	 * Card/Identity) and its trigger label only reflects gTypeFilter's
	 * own plain (never-encrypted) value. Real bug found alongside the
	 * filter-persistence fix above: gating it behind BwIsUnlocked() meant
	 * the trigger's label could go stale (stuck on the form's compiled
	 * "All" default) on any open where the vault happened to be locked at
	 * that moment, even though gTypeFilter itself was correctly preserved
	 * underneath - confirmed via POSE64 (label showed "All" while a debug
	 * print showed typeFilter=2/Note still set). Called unconditionally
	 * now, same as the flag-consume block above. */
	MainLoadTypePopup();

	/* Real-device feedback (2026-08): "when the entire list is loading, it
	 * can take a while" - shown BEFORE FrmDrawForm ever runs for this form
	 * (still whatever was on screen previously - the toast's own
	 * WinSaveBits/WinRestoreBits cleanly hands that back), torn down right
	 * before the real FrmDrawForm call draws the actual list content over
	 * it - matches lazyFTP's own "toast around the blocking work, gone
	 * before anything else needs the screen" convention. */
	if (BwIsUnlocked()) {
		BwToastBegin("Loading...");
		MainLoadFolderPopup();
		gFindTerm[0] = 0;
		gListTop = 0;
		FldSetTextPtr(fld, "");
	}
	MainRebuildMatches();
	MainLoadRows();
	if (BwIsUnlocked()) BwToastEnd();
	FrmDrawForm(frm);
}

Boolean gBwSyncOkPending = false;

/* After a successful sync, once the list has been rebuilt, sorted and drawn
 * (MainRefresh for Options > Sync Now, MainOpen after Preferences' Sync Now
 * switches here). Skipped if the vault isn't unlocked at that point. */
static void MainShowSyncOk(void) SEC001;

static void MainShowSyncOk(void)
{
	if (!gBwSyncOkPending) return;
	gBwSyncOkPending = false;
	if (BwIsUnlocked()) FrmAlert(altSyncOk);
}

Boolean MainFormHandleEvent(EventPtr e)
{
	Boolean handled = false;
	FormPtr frm = FrmGetActiveForm();

	switch (e->eType) {
	case frmOpenEvent:
		MainOpen();
		MainShowSyncOk();
		handled = true;
		break;

	case menuOpenEvent:
		MainUpdateArchiveMenuLabel();
		break;

	case menuEvent:
		switch (e->data.menu.itemID) {
		case mniSync:
			/* Refresh after a failed sync too: its progress-dialog close
			 * redraws this list while the empty temp database is still
			 * open (BwStoreAbortSync swaps the real one back afterwards),
			 * leaving it blank. */
			SyncNowWithChime();
			MainRefresh();
			MainShowSyncOk();
			handled = true;
			break;
		case mniPreferences:
			FrmGotoForm(frmPrefs);
			handled = true;
			break;
		case mniLockNow:
			BwLock();
			MainRefresh();
			handled = true;
			break;
		case mniDeleteAccount:
			/* Lets the user switch to a different Bitwarden account on this
			 * device - wipes everything that's specific to the CURRENT
			 * account (trust/email/cached master password/every synced
			 * record) but deliberately leaves the server URL alone (not
			 * asked for, and still useful if switching accounts on the same
			 * self-hosted server). BwResetVaultForNewPassword forgets the
			 * local vault's own salt/rounds/check too, so the next BwUnlock
			 * (whatever password that ends up being, for whichever account)
			 * is treated as a fresh first-ever unlock rather than being
			 * checked against the OLD account's password. */
			if (FrmAlert(altConfirmDeleteAccount) == 0) {
				BwStoreBeginSync();			/* wipes every record: items + folders + orgs */
				BwResetVaultForNewPassword();
				BwClearPinnedFingerprint();
				BwSetEmail("");
				BwWipeCachedPassword();
				BwSetTwoFactorRemember("");	/* account-specific - see Store.c's own note */
				BwLock();
				FrmGotoForm(frmPrefs);
			}
			handled = true;
			break;
		case mniArchive:
			/* Full rebuild (not the narrow-in-place path) since flipping
			 * this dimension can both add and remove rows relative to the
			 * current match set - narrowing only ever removes. */
			gArchiveFilter = !gArchiveFilter;
			gListTop = 0;
			gSearchPending = false;
			MainRebuildMatches();
			MainLoadRows();
			TblRedrawTable(MainTable());
			/* Real-device report (2026-09): relying on menuOpenEvent alone
			 * (MainUpdateArchiveMenuLabel's own call site, below) left the
			 * label stuck on the just-tapped text until the form was
			 * re-entered (opening then returning from a record) - PalmOS
			 * doesn't refire menuOpenEvent for a menu bar already
			 * initialized this form-open session, only re-displays the
			 * existing one. Updating right here, at the moment the flag
			 * actually flips, means the very next menu open already shows
			 * the correct label regardless of whether menuOpenEvent fires
			 * again - menuOpenEvent's own call stays for the OTHER way
			 * gArchiveFilter can change (MainOpen's filters-reset block, on
			 * a fresh form entry). */
			MainUpdateArchiveMenuLabel();
			handled = true;
			break;
		case mniAbout:
			FrmAlert(altAbout);
			handled = true;
			break;
		case mniSecurityTwoFactor:
			BwTwoFactorPrefsPromptRun();
			handled = true;
			break;
		}
		break;

	case tblSelectEvent:
		/* Sec35 lock gate - mirrors CryptoNote's MainFormEnterTableItem's
		 * own CryptoLockedBounce call: bounce before opening anything that
		 * would otherwise just try (and fail) to decrypt. */
		if (BwSec35LockedBounce()) {
			handled = true;
			break;
		}
		/* This app's OWN local-vault key has a separate TTL from Sec35's -
		 * see MainEnsureLocalUnlock's own comment on why this second check
		 * is needed even after the Sec35 gate above passes. */
		if (!BwIsUnlocked()) {
			MainEnsureLocalUnlock();
			if (!BwIsUnlocked()) { handled = true; break; }
			MainRefresh();
		}
		gRecordIndex = TblGetRowID((TableType *) e->data.tblSelect.pTable, e->data.tblSelect.row);
		/* Each itemType gets its own purpose-built form (RecordForm.c) -
		 * not one shared Login-shaped layout with relabeled/hidden rows. */
		switch (BwStoreGetItemType(gRecordIndex)) {
		case bwTypeNote:     FrmGotoForm(frmRecordNote); break;
		case bwTypeCard:     FrmGotoForm(frmRecordCard); break;
		case bwTypeIdentity: FrmGotoForm(frmRecordIdentity); break;
		default:             FrmGotoForm(frmRecordLogin); break;
		}
		handled = true;
		break;

	case popSelectEvent:
		if (e->data.popSelect.controlID == popMainType) {
			UInt16 sel = e->data.popSelect.selection;
			gTypeFilter = (sel < bwTypeFilterCount) ? kTypeValues[sel] : 0;
			gListTop = 0;
			gSearchPending = false;	/* about to rebuild against gFindTerm right now - a later debounce fire would be redundant */
			MainRebuildMatches();
			MainLoadRows();
			TblRedrawTable(MainTable());
		}
		break;

	case ctlSelectEvent:
		if (e->data.ctlSelect.controlID == popMainFolder) {
			/* Driven manually (no POPUPLIST pairing in the .rcp - see its
			 * own comment) specifically so the trigger's label can be
			 * capped afterward without PalmOS's own auto-relabel silently
			 * putting the full-length name back. */
			ListType *lst = (ListType *) GetObjectPtr(lstMainFolder);
			Int16 sel = LstPopupList(lst);
			if (sel >= 0 && sel < (Int16) gFilterCount && gFilterKind[sel] == 3) {
				/* Separator row - not a real choice, revert to whatever was
				 * selected before (a plain LIST has no "disabled row"
				 * concept, so this is handled after the fact). */
				sel = (Int16) gFilterSelRow;
				LstSetSelection(lst, sel);
			}
			if (sel >= 0 && sel < (Int16) gFilterCount) {
				gFilterSelRow = (UInt16) sel;
				if (gFilterKind[sel] == 2) {
					gOrgFilter = gFilterIndex[sel];
					gFolderFilter = bwFilterAll;
				} else if (gFilterKind[sel] == 1) {
					gFolderFilter = gFilterIndex[sel];
					gOrgFilter = bwFilterAll;
				} else {
					gFolderFilter = bwFilterAll;
					gOrgFilter = bwFilterAll;
				}
				CtlSetLabel((ControlType *) GetObjectPtr(popMainFolder), TruncateForTrigger(gFilterNamePtrs[sel]));
				gListTop = 0;
				gSearchPending = false;
				MainRebuildMatches();
				MainLoadRows();
				TblRedrawTable(MainTable());
			}
			handled = true;
		} else if (e->data.ctlSelect.controlID == btnMainClear) {
			FieldType *fld = (FieldType *) GetObjectPtr(fldMainFilter);
			FldDelete(fld, 0, FldGetTextLength(fld));
			gFindTerm[0] = 0;
			gListTop = 0;
			gSearchPending = false;
			MainRebuildMatches();
			MainLoadRows();
			TblRedrawTable(MainTable());
			handled = true;
		}
		break;

	/* Hardware scroll buttons (page up/down) - checked unconditionally,
	 * same as Address35's own MainFormHandleEvent, since these should
	 * scroll the list regardless of which field currently has focus. */
	case keyDownEvent:
		if (e->data.keyDown.chr == vchrPageUp || e->data.keyDown.chr == vchrPageDown) {
			Int16 page = (Int16) MainRows();
			if (page < 1) page = 1;
			MainScroll((Int16) (e->data.keyDown.chr == vchrPageDown ? page : -page));
			handled = true;
		} else {
			FieldType *fld = (FieldType *) GetObjectPtr(fldMainFilter);
			if (FrmGetFocus(frm) == FrmGetObjectIndex(frm, fldMainFilter)) {
				FldHandleEvent(fld, e);
				StrNCopy(gFindTerm, FldGetTextPtr(fld) ? FldGetTextPtr(fld) : "", sizeof(gFindTerm) - 1);
				gFindTerm[sizeof(gFindTerm) - 1] = 0;
				gListTop = 0;
				/* See MainSearchExecute's own comment - below bwSearchMinChars
				 * is always cheap (no crypto), so fires right away same as
				 * before; at/above it, defer to the debounce timer
				 * (nilEvent case below) instead of re-filtering on every
				 * single keystroke. */
				if (StrLen(gFindTerm) < bwSearchMinChars) {
					gSearchPending = false;
					MainSearchExecute();
				} else {
					gSearchPending = true;
					gSearchPendingTick = TimGetTicks();
				}
				handled = true;
			}
		}
		break;

	/* Fires periodically while idle - see BitWarden.c's own EventLoop
	 * comment on why frmMain gets a bounded EvtGetEvent timeout instead of
	 * the app's normal evtWaitForever specifically so this has a chance to
	 * run at all. Polls the debounce timer set by keyDownEvent above. */
	case nilEvent:
		if (gSearchPending) {
			UInt32 debounceTicks = ((UInt32) SysTicksPerSecond() * bwSearchDebounceMs) / 1000;
			if (TimGetTicks() - gSearchPendingTick >= debounceTicks) {
				gSearchPending = false;
				MainSearchExecute();
			}
		}
		break;

	case sclRepeatEvent:
		MainScroll((Int16) (e->data.sclRepeat.newValue - e->data.sclRepeat.value));
		handled = true;
		break;

	default:
		break;
	}

	return handled;
}
