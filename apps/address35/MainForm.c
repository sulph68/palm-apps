/*
 * MainForm.c - Address35 list (main) view.
 *
 * Renders contacts in a custom-drawn table: "First Last" with the LAST name
 * underlined (per design), a primary phone/email right-aligned. Category popup
 * (standard CategorySelect for now; the "Card" row + tag filter come later).
 *
 * PERFORMANCE: rows are drawn on demand (windowed); each row unpacks only the
 * few fields it shows, as a zero-copy view into the locked record.
 *
 * NOTE (step 2a): New / tapping a record show "not yet" until the Edit and
 * Record forms land in 2b/2c.
 */
#include <PalmOS.h>
#include "Main.h"
#include "CardIO.h"
#include "AddrCard.h"

#define kRecordColumn	0

static void *GetObjectPtr(UInt16 objID)
{
	FormPtr f = FrmGetActiveForm();
	return FrmGetObjectPtr(f, FrmGetObjectIndex(f, objID));
}

static TableType *MainTable(void)
{
	return (TableType *) GetObjectPtr(tblMainList);
}

static UInt16 CurrentCat(void)
{
	return gShowAllCategories ? dmAllCategories : gCurrentCategory;
}

/* number of category records with index < stopIndex */
static UInt16 CountCatBefore(UInt16 cat, UInt16 stopIndex)
{
	UInt16 idx = 0, count = 0;

	while (idx < stopIndex &&
		DmSeekRecordInCategory(gAddrDB, &idx, 0, dmSeekForward, cat) == errNone) {
		count++;
		idx++;
	}
	return count;
}

/* ------------------------------------------------------------------ *
 *  "Me" record pinning
 *
 * The record marked as Me (gMeUniqueID) is shown as the first list row, in
 * bold, in every category it belongs to (and in "All"). gMeIndex caches its
 * record index for the current load (a35NoRecord = not shown here); it is also
 * skipped in its natural in-line position so it never appears twice.
 * ------------------------------------------------------------------ */
static UInt16 gMeIndex = a35NoRecord;

/* These small helpers live in sec001 (foreground UI segment) to keep main under
 * the 32KB code cap; they're only called from the foreground list code. */
static UInt16 MeIndexForCat(UInt16 cat) SEC001;
static Boolean SeekNonMeFwd(UInt16 *idxP, UInt16 cat) SEC001;
static Boolean NextNonMe(UInt16 *idxP, UInt16 cat) SEC001;
static Boolean PrevNonMe(UInt16 *idxP, UInt16 cat) SEC001;
static UInt16 CountNormalBefore(UInt16 cat, UInt16 stopIndex) SEC001;
static UInt16 MainFitRows(void) SEC001;
static Boolean A35FieldHasTerm(const char *val, UInt16 len, const char *lower, UInt16 tl) SEC001;
static Boolean A35RecordMatches(UInt16 idx, const char *term) SEC001;
static void MainBuildFind(void) SEC001;
static void MainNarrowFind(void) SEC001;
static void MainFindFieldChanged(void) SEC001;
static void MainDoCancel(void) SEC001;
static void MainRestoreFindField(void) SEC001;
static void MainPageScroll(Boolean down) SEC001;
static UInt16 MainLoadFindRows(TableType *tableP, UInt16 rows, UInt16 lineHeight) SEC001;
static void MainScrollFind(Int16 delta) SEC001;

/* Record index of the Me record if it exists AND belongs to cat, else none. */
static UInt16 MeIndexForCat(UInt16 cat)
{
	UInt16 idx, attr = 0;

	if (gMeUniqueID == 0) return a35NoRecord;
	if (!A35FindByUniqueID(gAddrDB, gMeUniqueID, &idx)) return a35NoRecord;
	if (cat != dmAllCategories) {
		if (DmRecordInfo(gAddrDB, idx, &attr, NULL, NULL) != errNone)
			return a35NoRecord;
		if ((UInt16) (attr & dmRecAttrCategoryMask) != cat)
			return a35NoRecord;
	}
	return idx;
}

/* Seek forward to the first category record at/after *idxP that isn't Me. */
static Boolean SeekNonMeFwd(UInt16 *idxP, UInt16 cat)
{
	UInt16 idx = *idxP;
	while (DmSeekRecordInCategory(gAddrDB, &idx, 0, dmSeekForward, cat) == errNone) {
		if (idx != gMeIndex) { *idxP = idx; return true; }
		idx++;
	}
	return false;
}

/* Move *idxP to the next non-Me category record strictly after it. */
static Boolean NextNonMe(UInt16 *idxP, UInt16 cat)
{
	UInt16 idx = (UInt16) (*idxP + 1);
	if (!SeekNonMeFwd(&idx, cat)) return false;
	*idxP = idx;
	return true;
}

/* Move *idxP to the previous non-Me category record strictly before it. */
static Boolean PrevNonMe(UInt16 *idxP, UInt16 cat)
{
	UInt16 idx;
	if (*idxP == 0) return false;
	idx = (UInt16) (*idxP - 1);
	for (;;) {
		UInt16 s = idx;
		if (DmSeekRecordInCategory(gAddrDB, &s, 0, dmSeekBackward, cat) != errNone)
			return false;
		if (s != gMeIndex) { *idxP = s; return true; }
		if (s == 0) return false;
		idx = (UInt16) (s - 1);
	}
}

/* Count non-Me category records with index < stopIndex. */
static UInt16 CountNormalBefore(UInt16 cat, UInt16 stopIndex)
{
	UInt16 c = CountCatBefore(cat, stopIndex);
	if (gMeIndex != a35NoRecord && gMeIndex < stopIndex && c) c--;
	return c;
}

/* ---- list-view Find filter ------------------------------------------- *
 * On Find tap we scan the current category ONCE, collecting matching record indices into
 * gFindMatches (any decrypted TEXT field contains the term, case-insensitive); the list then
 * displays that array as a parallel "find mode" (mirrors card mode).  Cancel clears it. */
#define kFindMax	400
static Boolean gFindActive = false;
static UInt16  gFindTop = 0;
static UInt16  gFindCount = 0;
static UInt16  gFindMatches[kFindMax];
static char    gFindTerm[40];

/* Debounce: even narrowed (see MainNarrowFind), re-filtering still decrypts
 * every candidate, so re-running it on EVERY keystroke of a fast typist still
 * feels laggy. Instead of filtering immediately, arm a short deadline in the
 * keyDownEvent handler below; Address35.c's EventLoop calls
 * MainFindFirePending() on a matching nilEvent timeout (bounded EvtGetEvent
 * wait, not polling -- same idiom as Sec35/CryptoNote's password-reveal
 * timer), so the filter only actually runs once typing pauses. Each new
 * keystroke before the deadline just pushes it back (debounce, not a fixed
 * interval) -- MainFindPendingTimeout reports how long the event loop should
 * wait next. Clearing the field to empty bypasses this and exits find mode
 * immediately (cheap, no decrypt involved, no reason to make that feel slow). */
#define kFindDebounceTicksFrac 2	/* SysTicksPerSecond() / this = ~0.5s */
static UInt32 gFindPendingUntilTicks = 0;

/* Per-visible-row cache of the record index last drawn there, so re-typing a
 * character that leaves a row's assigned record unchanged (common early in a
 * narrowing search, or after a scroll-neutral edit) skips re-decrypting +
 * redrawing that row -- see MainLoadFindRows. 0xFFFF = never drawn. Sized to
 * comfortably cover the max rows MainFitRows can ever compute (128px body /
 * smallest realistic line height). Safe ONLY within find-mode row loading: no
 * record content can change out from under a row while the user is mid-keystroke
 * in the find field (that requires navigating away first, which exits find
 * typing and goes through MainBuildFind on return anyway). */
#define kFindRowCacheMax 16
static UInt16  gFindRowLastID[kFindRowCacheMax];

/* Visible body rows for the current list font (the 128px-tall body). */
static UInt16 MainFitRows(void)
{
	FontID o = FntSetFont(gListFont);
	UInt16 lh = FntLineHeight();
	UInt16 fit;
	FntSetFont(o);
	if (lh < 1) lh = 11;
	fit = (UInt16) (128 / lh);
	return fit < 1 ? 1 : fit;
}

/* Case-insensitive substring search bounded to len (values aren't guaranteed NUL-term). */
static Boolean A35FieldHasTerm(const char *val, UInt16 len, const char *lower, UInt16 tl)
{
	UInt16 i, j;

	if (tl == 0) return true;
	if (tl > len) return false;
	for (i = 0; (UInt16) (i + tl) <= len; i++) {
		for (j = 0; j < tl; j++) {
			char c = val[i + j];
			if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
			if (c != lower[j]) break;
		}
		if (j == tl) return true;
	}
	return false;
}

/* True if any decrypted TEXT field of record idx contains term (case-insensitive). */
static Boolean A35RecordMatches(UInt16 idx, const char *term)
{
	A35Record rec;
	MemHandle h;
	UInt16 fi, tl, k;
	char lower[40];
	Boolean match = false;

	tl = (UInt16) StrLen(term);
	if (tl > sizeof(lower) - 1) tl = sizeof(lower) - 1;
	for (k = 0; k < tl; k++) {
		char c = term[k];
		lower[k] = (c >= 'A' && c <= 'Z') ? (char) (c + 32) : c;
	}
	lower[tl] = '\0';
	if (tl == 0)
		return true;

	if (A35GetRecord(gAddrDB, idx, &rec, &h) != errNone)
		return false;
	for (fi = 0; fi < rec.fieldCount && !match; fi++) {
		UInt8 t = rec.fields[fi].type;
		if (t == a35fRelation || t == a35fTag || rec.fields[fi].len == 0)
			continue;					/* non-text (uid / tag-id) fields */
		if (A35FieldHasTerm((const char *) rec.fields[fi].value, rec.fields[fi].len, lower, tl))
			match = true;
	}
	A35ReleaseRecord(h);
	return match;
}

/* Scan the current category, filling gFindMatches with matching record indices. */
static void MainBuildFind(void)
{
	UInt16 cat = CurrentCat();
	UInt16 idx = 0;

	gFindCount = 0;
	gFindTop = 0;
	MemSet(gFindRowLastID, sizeof(gFindRowLastID), 0xFF);	/* fresh session -> redraw every row once */
	while (gFindCount < kFindMax &&
		DmSeekRecordInCategory(gAddrDB, &idx, 0, dmSeekForward, cat) == errNone) {
		if (A35RecordMatches(idx, gFindTerm))
			gFindMatches[gFindCount++] = idx;
		idx++;
	}
}

/* Re-filter the EXISTING gFindMatches[] against the (longer) gFindTerm, in place --
 * used when the user simply extends the previous search term (typed one more
 * character), since a longer term's matches are always a SUBSET of the shorter
 * term's matches. Each candidate still needs A35RecordMatches (which decrypts, if
 * the record is encrypted), but only over the already-narrowed match set rather
 * than the whole category -- for a typical "keep typing to narrow down" search
 * this turns each keystroke's cost from O(records in category) into O(current
 * match count), which only shrinks as you type. MainFindFieldChanged is the only
 * caller and already verified gFindTerm actually extends the previous term before
 * calling this -- an edit that ISN'T a simple extension (backspace, paste, edited
 * mid-string) falls back to MainBuildFind's full rescan there instead. */
static void MainNarrowFind(void)
{
	UInt16 i, out = 0;

	for (i = 0; i < gFindCount; i++) {
		if (A35RecordMatches(gFindMatches[i], gFindTerm))
			gFindMatches[out++] = gFindMatches[i];
	}
	gFindCount = out;
	gFindTop = 0;
}

/* ------------------------------------------------------------------ *
 *  SD "Card" browse mode (CryptoNote-style in-list file/folder browser)
 * ------------------------------------------------------------------ */
/* entries per directory - CardListDir silently STOPS enumerating (no error,
 * no truncation indicator) once it hits this many entries, so a folder with
 * more files than this loses the rest entirely from the on-device listing
 * (they're still really there - confirmed by a real-device report: split
 * output "missing" on the Palm but present when the card was read on a
 * computer). 64 was too low for a large group vCard split's own output
 * (e.g. 80 contacts) landing in a directory that already had other files
 * in it from repeated testing. gCardEnt[]/gCardMatch[] below are heap-
 * allocated (MemPtrNew, lazily on first card-mode use in MainLoadCardDir),
 * NOT static arrays - at 256 entries a plain `static CardEntry gCardEnt[256]`
 * (~70B/entry) overflowed this app's own separate ~32KB app-wide static-data
 * budget (`datares`, shared by every .c file's globals - distinct from the
 * four ~32KB CODE segments); 96 was the largest static array that still fit.
 * The heap has its own, much bigger budget on real hardware, so moving these
 * off static data sidesteps that ceiling instead of fighting it. */
#define kCardMax	256

static Boolean   gCardMode = false;
static UInt16    gCardVol;
static char      gCardPath[cardMaxPath];	/* current dir, floored at /Address */
static CardEntry *gCardEnt;				/* heap: MemPtrNew'd, kCardMax entries - see kCardMax comment */
static UInt16    gCardCount = 0;			/* files+folders (excludes [..] row) */
static UInt16    gCardTop = 0;
static Boolean   gCardHasParent = false;	/* show a [..] row at display index 0 */
/* True once gCardEnt[]/gCardCount hold a fresh read of gCardPath. MainDraw's
 * "returning to the browser" refresh skips the (potentially slow, real SD-card)
 * directory scan when this is still true - nothing on the card changes just by
 * previewing a contact and coming back. Real navigation (ascend/descend) and
 * anything that writes to the card (a confirmed split) still call
 * MainLoadCardDir() directly, which always rescans and re-sets this true. */
static Boolean   gCardDirValid = false;

/* Prefs > Display > Show Hidden was toggled - the current listing (if any) may
 * now be showing/hiding the wrong set of dotfiles, force a real rescan next
 * time the browser is shown rather than trusting the stale cache. */
void MainInvalidateCardDir(void)
{
	gCardDirValid = false;
}

/* Filename filter for the SAME Find field, while browsing the card (a
 * PARALLEL mechanism to gFindActive/gFindTerm's on-device record search, not
 * a reuse of it - filtering filenames is cheap, no per-entry decrypt, so no
 * need for the on-device path's narrowing-cache complexity). */
static Boolean   gCardFindActive = false;
static char      gCardFindTerm[32];
static UInt16    *gCardMatch;				/* heap: gCardEnt[] indices that match, kCardMax entries */
static UInt16    gCardMatchCount = 0;

/* The SD-card browse UI lives in sec002 (per the segment map) to keep main under
 * the 32KB code cap. Forward-declared with SEC002 so main-segment callers emit
 * far calls and the definitions land in sec002. */
static UInt16 MainCardDisplayCount(void) SEC002;
static Boolean MainIsVcf(const char *name) SEC002;
static void MainSortCardEntries(void) SEC002;
static void MainLoadCardDir(void) SEC002;
static Boolean MainEnterCard(void) SEC002;
static void CardDrawIcon(UInt16 bmpId, Int16 x, Int16 y) SEC002;
static void CardCatListDraw(Int16 itemNum, RectangleType *bounds, Char **itemsText) SEC002;
static void MainDrawCardRow(void *table, Int16 row, RectangleType *bounds) SEC002;
static void MainCardTap(UInt16 di) SEC002;
static CardEntry *MainCardEntryAt(UInt16 di, Boolean *isParent) SEC002;
static void MainBuildCardFind(void) SEC002;

static UInt16 MainCardDisplayCount(void)
{
	if (gCardFindActive) return gCardMatchCount;
	return (UInt16) (gCardCount + (gCardHasParent ? 1 : 0));
}

/* Resolve a display row index to its CardEntry, whichever list (filtered or
 * full) is active. *isParent is set true for the synthetic "[..]" row (never
 * shown while filtering - see gCardFindActive's own comment); NULL means di
 * is out of range. */
static CardEntry *MainCardEntryAt(UInt16 di, Boolean *isParent)
{
	*isParent = false;
	if (gCardFindActive) {
		if (di >= gCardMatchCount) return NULL;
		return &gCardEnt[gCardMatch[di]];
	}
	if (gCardHasParent && di == 0) { *isParent = true; return NULL; }
	{
		UInt16 ei = (UInt16) (di - (gCardHasParent ? 1 : 0));
		if (ei >= gCardCount) return NULL;
		return &gCardEnt[ei];
	}
}

static Boolean MainIsVcf(const char *name)
{
	UInt16 n = (UInt16) StrLen(name);
	if (n < 4) return false;
	name += n - 4;
	return (Boolean) (name[0] == '.' &&
		(name[1]|32) == 'v' && (name[2]|32) == 'c' && (name[3]|32) == 'f');
}

/* macOS AppleDouble resource forks ("._foo.vcf") and metadata files
 * (".DS_Store") are all just dot-prefixed names - a leading '.' covers both
 * without needing to special-case each pattern. */
static Boolean MainIsHidden(const char *name)
{
	return (Boolean) (name[0] == '.');
}

/* Folders first, then files, each group alphanumeric (case-insensitive).
 * CardListDir only promises "filesystem order" (real FAT directories are NOT
 * guaranteed alphabetical - entries land wherever the driver puts them,
 * often creation order or a reused freed slot) - this app never explicitly
 * sorted the card listing itself, so any "sorted" appearance was always
 * incidental to how a given card's directory table happened to be laid out,
 * not a real guarantee. A real device confirmed this: after enough file
 * churn this session (many splits/replaces/deletes), the listing visibly
 * stopped looking sorted. Plain insertion sort - gCardCount is at most
 * kCardMax (256), a user-triggered one-off scan, not a hot loop. */
static void MainSortCardEntries(void)
{
	UInt16 i, j;
	for (i = 1; i < gCardCount; i++) {
		CardEntry key = gCardEnt[i];
		j = i;
		while (j > 0) {
			CardEntry *prev = &gCardEnt[j - 1];
			Boolean swapNeeded;
			if (prev->isDir != key.isDir)
				swapNeeded = (Boolean) (!prev->isDir && key.isDir);	/* dirs before files */
			else
				swapNeeded = (Boolean) (StrCaselessCompare(prev->name, key.name) > 0);
			if (!swapNeeded) break;
			gCardEnt[j] = *prev;
			j--;
		}
		gCardEnt[j] = key;
	}
}

/* (Re)read the current card directory: folders + .vcf files only. Always a
 * real scan when called directly (navigation); MainDraw's own "returning to
 * the browser" refresh instead checks gCardDirValid first so a plain preview
 * round-trip doesn't re-hit the card (see gCardDirValid's own comment). */
static void MainLoadCardDir(void)
{
	UInt16 i, k;
	if (gCardEnt == NULL) {			/* first time in card mode this session */
		gCardEnt = (CardEntry *) MemPtrNew((UInt32) sizeof(CardEntry) * kCardMax);
		gCardMatch = (UInt16 *) MemPtrNew((UInt32) sizeof(UInt16) * kCardMax);
		if (gCardEnt == NULL || gCardMatch == NULL) {
			if (gCardEnt) { MemPtrFree(gCardEnt); gCardEnt = NULL; }
			if (gCardMatch) { MemPtrFree(gCardMatch); gCardMatch = NULL; }
			gCardCount = 0;
			return;					/* out of memory - degrade to an empty listing, not a crash */
		}
	}
	gCardHasParent = (Boolean) (StrCompare(gCardPath, a35CardRoot) != 0);
	A35BusyBegin("Reading...");	/* real SD-card directory scans can be slow */
	{
		Boolean truncated;
		if (CardListDir(gCardVol, gCardPath, gCardEnt, kCardMax, &gCardCount, &truncated) != errNone)
			gCardCount = 0;
		A35BusyEnd();
		if (truncated)			/* more files exist than kCardMax could hold - say so, don't just hide them */
			FrmAlert(altCardTooMany);
	}
	for (i = 0, k = 0; i < gCardCount; i++) {
		if (!gShowHiddenFiles && MainIsHidden(gCardEnt[i].name))
			continue;
		if (gCardEnt[i].isDir || MainIsVcf(gCardEnt[i].name)) {
			if (k != i) gCardEnt[k] = gCardEnt[i];
			k++;
		}
	}
	gCardCount = k;
	MainSortCardEntries();
	gCardTop = 0;
	gCardDirValid = true;
	gCardFindActive = false;		/* leaving/reloading a directory drops any filename filter */
}

/* Enter card mode at /Address. Group vCards are left as-is here (no more
 * auto-split-on-browse); MainCardTap prompts to split only when the user
 * actually taps one. */
static Boolean MainEnterCard(void)
{
	if (!CardHasVolume(&gCardVol)) { FrmAlert(altNoCard); return false; }
	StrCopy(gCardPath, a35CardRoot);
	A35CardInit(gCardVol);
	gCardMode = true;
	MainLoadCardDir();
	return true;
}

/* Draw a small Tbmp icon at (x,y); silent if the resource is absent. */
static void CardDrawIcon(UInt16 bmpId, Int16 x, Int16 y)
{
	MemHandle h = DmGetResource(bitmapRsc, bmpId);
	if (h) {
		BitmapPtr b = (BitmapPtr) MemHandleLock(h);
		WinDrawBitmap(b, x, y);
		MemHandleUnlock(h);
		DmReleaseResource(h);
	}
}

/* Category-popup custom draw: the "Card" row (index gCatCardRow) gets a divider
 * line above it + the SD-card glyph + "Card", the way Palm apps show a card
 * source. Draw the divider LAST (WinDrawChars paints its cell background). The
 * card glyph is char 0x1A in the STANDARD font table (stdFont/boldFont) — Palm's
 * standard fonts reserve 0x00-0x1F for UI icon glyphs, distinct from the
 * separate symbolFont/symbol11Font glyph set (confirmed on real hardware:
 * symbol11Font 0x1A rendered the wrong glyph). */
static Int16 gCatCardRow = -1;
static void CardCatListDraw(Int16 itemNum, RectangleType *bounds, Char **itemsText)
{
	Char *s = itemsText[itemNum];
	FntSetFont(stdFont);
	if (itemNum == gCatCardRow) {
		Int16 topY = bounds->topLeft.y;
		char glyph[2];

		glyph[0] = (char) 0x1A; glyph[1] = '\0';
		WinDrawChars(glyph, 1, bounds->topLeft.x + 1, topY + 2);
		WinDrawChars(s, StrLen(s), bounds->topLeft.x + 9, topY + 2);
		WinDrawLine(bounds->topLeft.x - 4, topY + 1,
			bounds->topLeft.x + bounds->extent.x + 2, topY + 1);
	} else {
		WinDrawChars(s, StrLen(s), bounds->topLeft.x, bounds->topLeft.y);
	}
}

/* Bold counterpart of the chosen list font (for the pinned "Me" row). */
static FontID A35BoldFont(FontID f)
{
	switch (f) {
	case stdFont:	return boldFont;
	case largeFont:	return largeBoldFont;
	default:		return f;			/* already bold, or no bold variant */
	}
}

/* ------------------------------------------------------------------ *
 *  custom row draw
 * ------------------------------------------------------------------ */
static void MainDrawRecord(void *table, Int16 row, Int16 column,
	RectangleType *bounds)
{
	if (gCardMode) { MainDrawCardRow(table, row, bounds); return; }
	{
	UInt16 recordIndex;
	A35Record rec;
	MemHandle h;
	const char *first, *last, *company, *side, *sal;
	const A35Field *f;
	Int16 x, y, maxW, pw, nameW, base;

	(void) column;
	recordIndex = TblGetRowID((TableType *) table, row);
	if (A35GetRecord(gAddrDB, recordIndex, &rec, &h) != errNone)
		return;

	first = A35GetStr(&rec, a35fFirst);
	last = A35GetStr(&rec, a35fLast);
	company = A35GetStr(&rec, a35fCompany);
	sal = A35GetStr(&rec, a35fPrefix);	/* salutation: shown, but not a sort key */
	f = A35FindField(&rec, a35fPhone, 0);
	if (f == NULL || f->len == 0)
		f = A35FindField(&rec, a35fEmail, 0);
	side = (f != NULL && f->len) ? (const char *) f->value : "";

	x = bounds->topLeft.x;
	y = bounds->topLeft.y;
	maxW = bounds->extent.x;
	FntSetFont(recordIndex == gMeIndex ? A35BoldFont(gListFont) : gListFont); /* Me = bold */
	base = FntBaseLine();

	pw = side[0] ? FntCharsWidth(side, StrLen(side)) : 0;
	nameW = maxW - (pw ? pw + 6 : 0);	/* room reserved for the right value */

	/* Compose the display name ("Sal First, Last") then draw it: if it fits, draw
	 * plain (and underline the last/sort name); if not, WinDrawTruncChars adds the
	 * "..." ellipsis so long names are clipped cleanly instead of being dropped. */
	if (first[0] || last[0]) {
		char nameBuf[200];
		Int16 tw;
		UInt16 nl;
#define NB_CAT(s) StrNCat(nameBuf, (s), (Int16) sizeof(nameBuf))
		nameBuf[0] = '\0';
		if (sal[0]) { NB_CAT(sal); NB_CAT(" "); }
		if (first[0]) { NB_CAT(first); if (last[0]) NB_CAT(", "); }
		if (last[0]) NB_CAT(last);
#undef NB_CAT
		nl = (UInt16) StrLen(nameBuf);
		tw = FntCharsWidth(nameBuf, (Int16) nl);
		if (tw <= nameW) {
			WinDrawChars(nameBuf, (Int16) nl, x, y);
			if (last[0]) {			/* underline the last (sort) name */
				Int16 lw = FntCharsWidth(last, StrLen(last));
				Int16 off = FntCharsWidth(nameBuf,
					(Int16) (nl - StrLen(last)));
				WinDrawLine(x + off, y + base + 1,
					x + off + lw - 1, y + base + 1);
			}
		} else {
			WinDrawTruncChars(nameBuf, (Int16) nl, x, y, nameW);
		}
	} else if (company[0]) {
		WinDrawTruncChars(company, (Int16) StrLen(company), x, y, nameW);
	} else {
		WinDrawChars("-Unnamed-", 9, x, y);
	}

	if (side[0])
		WinDrawChars(side, StrLen(side), x + maxW - pw, y);

	A35ReleaseRecord(h);
	}
}

/* Draw a card row: up-icon "[ .. ]" parent, folder-icon folder, or the .vcf
 * contact (icon column blank; name drawn in the same column as folders). */
static void MainDrawCardRow(void *table, Int16 row, RectangleType *bounds)
{
	UInt16 di = TblGetRowID((TableType *) table, row);
	Int16 x = bounds->topLeft.x + 1, y = bounds->topLeft.y;
	Int16 tx = x + 13, w = bounds->extent.x - 14, iconY;

	FntSetFont(stdFont);
	iconY = y + ((Int16) FntLineHeight() - 11) / 2;
	if (iconY < y) iconY = y;

	{
		Boolean isParent;
		CardEntry *e = MainCardEntryAt(di, &isParent);
		if (isParent) {							/* [ .. ] ascend row */
			CardDrawIcon(bmpCardUp, x + 1, iconY);
			WinDrawChars("..", 2, tx, y);
			return;
		}
		if (e == NULL) return;
		if (e->isDir) {							/* folder icon + name */
			CardDrawIcon(bmpCardFolder, x + 1, iconY);
			WinDrawTruncChars(e->name, (Int16) StrLen(e->name), tx, y, w);
		} else {								/* contact: name, .vcf hidden */
			char nm[cardMaxName];
			UInt16 nl = (UInt16) StrLen(e->name);
			StrNCopy(nm, e->name, sizeof(nm) - 1); nm[sizeof(nm) - 1] = '\0';
			if (nl >= 4) nm[nl - 4] = '\0';
			WinDrawTruncChars(nm, (Int16) StrLen(nm), tx, y, w);
		}
	}
}

/* ------------------------------------------------------------------ *
 *  table load / scrollbar
 * ------------------------------------------------------------------ */
static UInt16 MainLoadTable(void)
{
	TableType *tableP = MainTable();
	UInt16 rows = TblGetNumberOfRows(tableP), r, shown = 0;
	UInt16 cat = CurrentCat();
	UInt16 recIndex = gTopVisibleRecord;
	UInt16 lineHeight;
	FontID old;

	old = FntSetFont(gListFont);			/* row height follows the chosen font */
	lineHeight = FntLineHeight();
	FntSetFont(old);
	{	/* a taller font fits fewer of the 128px-tall body's physical rows */
		UInt16 fit = (UInt16) (128 / (lineHeight ? lineHeight : 11));
		if (fit < 1) fit = 1;
		if (fit < rows) {
			for (r = fit; r < rows; r++)
				TblSetRowUsable(tableP, r, false);
			rows = fit;
		}
	}

	gMeIndex = a35NoRecord;

	if (gCardMode) {						/* card display list, fixed-height rows */
		UInt16 disp = MainCardDisplayCount();
		for (r = 0; r < rows; r++) {
			UInt16 di = (UInt16) (gCardTop + r);
			if (di < disp) {
				TblSetItemStyle(tableP, r, kRecordColumn, customTableItem);
				TblSetRowHeight(tableP, r, lineHeight);
				TblSetRowID(tableP, r, di);
				TblSetRowUsable(tableP, r, true);
				TblMarkRowInvalid(tableP, r);
				shown++;
			} else {
				TblSetRowUsable(tableP, r, false);
			}
		}
		TblSetColumnUsable(tableP, kRecordColumn, true);
		return shown;
	}

	if (gFindActive)						/* filtered "find mode" (SEC001) */
		return MainLoadFindRows(tableP, rows, lineHeight);

	if (gTopVisibleRecord >= DmNumRecords(gAddrDB))
		gTopVisibleRecord = recIndex = 0;

	gMeIndex = MeIndexForCat(cat);

	r = 0;
	if (gMeIndex != a35NoRecord) {			/* pin the Me record as the first row */
		TblSetItemStyle(tableP, 0, kRecordColumn, customTableItem);
		TblSetRowHeight(tableP, 0, lineHeight);
		TblSetRowID(tableP, 0, gMeIndex);
		TblSetRowUsable(tableP, 0, true);
		TblMarkRowInvalid(tableP, 0);
		r = 1;
		shown++;
	}

	for (; r < rows; r++) {
		UInt16 idx = recIndex;

		if (SeekNonMeFwd(&idx, cat)) {		/* next record, skipping the pinned Me */
			TblSetItemStyle(tableP, r, kRecordColumn, customTableItem);
			TblSetRowHeight(tableP, r, lineHeight);
			TblSetRowID(tableP, r, idx);
			TblSetRowUsable(tableP, r, true);
			TblMarkRowInvalid(tableP, r);
			recIndex = (UInt16) (idx + 1);
			shown++;
		} else {
			TblSetRowUsable(tableP, r, false);
		}
	}
	TblSetColumnUsable(tableP, kRecordColumn, true);
	return shown;
}

static void MainUpdateScrollBar(UInt16 shown)
{
	ScrollBarType *sb = (ScrollBarType *) GetObjectPtr(sclMainScroll);
	UInt16 cat, total;
	Int16 value, max;

	if (gCardMode) {
		total = MainCardDisplayCount();
		max = (Int16) ((total > shown) ? total - shown : 0);
		value = (Int16) gCardTop;
		if (value > max) value = max;
		SclSetScrollBar(sb, value, 0, max, (Int16) (shown ? shown : 1));
		return;
	}
	if (gFindActive) {						/* filtered set */
		max = (Int16) ((gFindCount > shown) ? gFindCount - shown : 0);
		value = (Int16) gFindTop;
		if (value > max) value = max;
		SclSetScrollBar(sb, value, 0, max, (Int16) (shown ? shown : 1));
		return;
	}
	cat = CurrentCat();
	{
		/* the pinned Me row is not part of the scrollable (normal) records */
		UInt16 meShown = (gMeIndex != a35NoRecord) ? 1 : 0;
		UInt16 normalShown = (shown > meShown) ? (UInt16) (shown - meShown) : 0;

		total = DmNumRecordsInCategory(gAddrDB, cat);
		if (meShown && total) total--;
		value = (Int16) CountNormalBefore(cat, gTopVisibleRecord);
		max = (Int16) ((total > normalShown) ? total - normalShown : 0);
		if (value > max)
			value = max;
		SclSetScrollBar(sb, value, 0, max, (Int16) (normalShown ? normalShown : 1));
	}
}

static void MainDraw(void)
{
	UInt16 shown = MainLoadTable();

	MainUpdateScrollBar(shown);
	TblRedrawTable(MainTable());
}

/* Move the top-of-list by delta category records (for scrolling). */
static void MainScroll(Int16 deltaRecords)
{
	UInt16 cat, idx;

	if (gCardMode) {
		Int16 disp = (Int16) MainCardDisplayCount();
		Int16 nt = (Int16) gCardTop + deltaRecords;
		if (nt > disp - 1) nt = (Int16) (disp - 1);
		if (nt < 0) nt = 0;
		gCardTop = (UInt16) nt;
		MainDraw();
		return;
	}
	if (gFindActive) {						/* scroll the filtered set (SEC001) */
		MainScrollFind(deltaRecords);
		return;
	}
	cat = CurrentCat();
	idx = gTopVisibleRecord;

	{	/* step over non-Me records so the pinned Me row never counts in scroll */
		Int16 i;
		if (deltaRecords > 0) {
			for (i = 0; i < deltaRecords; i++)
				if (!NextNonMe(&idx, cat)) break;
		} else {
			for (i = 0; i < -deltaRecords; i++)
				if (!PrevNonMe(&idx, cat)) { idx = 0; break; }
		}
		gTopVisibleRecord = idx;
	}
	MainDraw();
}

/* ---- Find bar actions (SEC001 to keep the main segment under the 32KB cap) ---- */
static UInt16 MainLoadFindRows(TableType *tableP, UInt16 rows, UInt16 lineHeight)
{
	UInt16 r, shown = 0;

	gMeIndex = a35NoRecord;					/* no Me-pin in a filtered view */
	for (r = 0; r < rows; r++) {
		UInt16 di = (UInt16) (gFindTop + r);
		if (di < gFindCount) {
			UInt16 recId = gFindMatches[di];
			TblSetItemStyle(tableP, r, kRecordColumn, customTableItem);
			TblSetRowHeight(tableP, r, lineHeight);
			TblSetRowID(tableP, r, recId);			/* real record index */
			TblSetRowUsable(tableP, r, true);
			/* Skip the redraw (and MainDrawRecord's A35GetRecord decrypt)
			 * when this row slot already shows the same record it did last
			 * time -- common while narrowing a search one keystroke at a
			 * time. See gFindRowLastID's comment for why this is safe here. */
			if (r >= kFindRowCacheMax || gFindRowLastID[r] != recId) {
				TblMarkRowInvalid(tableP, r);
				if (r < kFindRowCacheMax) gFindRowLastID[r] = recId;
			}
			shown++;
		} else {
			TblSetRowUsable(tableP, r, false);
			if (r < kFindRowCacheMax) gFindRowLastID[r] = 0xFFFF;	/* now empty */
		}
	}
	TblSetColumnUsable(tableP, kRecordColumn, true);
	return shown;
}

static void MainScrollFind(Int16 delta)
{
	Int16 rows = (Int16) MainFitRows();
	Int16 max = (Int16) ((gFindCount > (UInt16) rows) ? gFindCount - rows : 0);
	Int16 nt = (Int16) gFindTop + delta;

	if (nt > max) nt = max;
	if (nt < 0) nt = 0;
	gFindTop = (UInt16) nt;
	MainDraw();
}

/* Db35: refilters after every field change -- called both from the keyDownEvent
 * case below (which dispatches the key to the field itself, then refilters off
 * the just-updated text -- fldChangedEvent does NOT fire per keystroke for a
 * plain SINGLELINE field, only for bulk/programmatic edits like Cancel's
 * FldDelete or a menu Paste) and from the fldChangedEvent case (covers those
 * bulk edits). Unlike the old tap-triggered version, an empty result list while
 * typing is normal, not an error -- no altNoResults alert here. Clearing the
 * field back to empty turns the filter off entirely (same effect as Cancel,
 * minus re-deleting the already-empty field). */
/* Filename filter for the card browser (gCardFindActive/gCardFindTerm) - cheap
 * enough (<=64 filenames, no decrypt) to just rebuild in full on every
 * keystroke, unlike the on-device Find's narrowing-cache path. */
static void MainBuildCardFind(void)
{
	char lower[sizeof(gCardFindTerm)];
	UInt16 tl, k, i;

	tl = (UInt16) StrLen(gCardFindTerm);
	if (tl > sizeof(lower) - 1) tl = sizeof(lower) - 1;
	for (k = 0; k < tl; k++) {
		char c = gCardFindTerm[k];
		if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
		lower[k] = c;
	}
	lower[tl] = '\0';

	gCardMatchCount = 0;
	for (i = 0; i < gCardCount && gCardMatchCount < kCardMax; i++) {
		const char *nm = gCardEnt[i].name;
		if (A35FieldHasTerm(nm, (UInt16) StrLen(nm), lower, tl))
			gCardMatch[gCardMatchCount++] = i;
	}
	gCardTop = 0;
}

static void MainFindFieldChanged(void)
{
	FieldPtr fld = (FieldPtr) GetObjectPtr(fldMainFind);
	char *t = fld ? (char *) FldGetTextPtr(fld) : NULL;
	const char *activeTerm = gCardMode ? gCardFindTerm : gFindTerm;
	Boolean wasActive = gCardMode ? gCardFindActive : gFindActive;

	/* Idempotency guard: a programmatic FldDelete() (MainDoCancel) on a field
	 * that has REAL prior edit history queues a delayed fldChangedEvent that
	 * only gets delivered on some LATER, unrelated event, causing this to run
	 * again redundantly - same class of bug fixed in CryptoNote's own Find bar
	 * (see cryptonote-list-filter-and-busy-dialog memory). Skip the whole
	 * refresh (including gTopVisibleRecord's reset) if the field's current
	 * text already matches what's already reflected in state - a genuine edit
	 * always changes the text, so this can only ever skip a redundant
	 * re-trigger, never a real one. */
	if ((t && t[0]) ? (wasActive && StrCompare(t, activeTerm) == 0) : !wasActive)
		return;

	if (gCardMode) {			/* filter card FILENAMES, not the on-device list */
		if (t && t[0]) {
			StrNCopy(gCardFindTerm, t, sizeof(gCardFindTerm) - 1);
			gCardFindTerm[sizeof(gCardFindTerm) - 1] = '\0';
			gCardFindActive = true;
			MainBuildCardFind();
		} else {
			gCardFindActive = false;
			gCardFindTerm[0] = '\0';
		}
		MainDraw();
		return;
	}

	if (t && t[0]) {
		char prevTerm[sizeof(gFindTerm)];
		UInt16 prevLen = (UInt16) StrLen(gFindTerm);
		Boolean extendsPrev;

		StrNCopy(prevTerm, gFindTerm, sizeof(prevTerm) - 1);
		prevTerm[sizeof(prevTerm) - 1] = '\0';

		StrNCopy(gFindTerm, t, sizeof(gFindTerm) - 1);
		gFindTerm[sizeof(gFindTerm) - 1] = '\0';

		/* Was already filtering, on THIS category, and the new text simply
		 * extends what was already typed -- matches can only shrink, so
		 * narrow the existing (already decrypted-and-matched) result set
		 * instead of re-scanning/re-decrypting the whole category again. */
		extendsPrev = (Boolean) (gFindActive && !gCardMode && prevLen > 0 &&
			StrLen(gFindTerm) > prevLen &&
			StrNCompare(gFindTerm, prevTerm, prevLen) == 0);

		gCardMode = false;
		gFindActive = true;
		if (extendsPrev)
			MainNarrowFind();
		else
			MainBuildFind();
	} else {
		gFindActive = false;
		gFindTerm[0] = '\0';
		gTopVisibleRecord = 0;
	}
	MainDraw();
}

/* How long Address35.c's EventLoop should let EvtGetEvent block before waking
 * up to check the debounced filter -- evtWaitForever (block indefinitely) when
 * nothing is pending, otherwise the exact remaining time (0 if already due).
 * Deliberately NOT sec001-tagged (like MainFormHandleEvent itself): called
 * every trip through the main-segment event loop, so it needs to be a cheap
 * main-segment call, not a far call into sec001. */
UInt32 MainFindPendingTimeout(void)
{
	UInt32 now;

	if (!gFindPendingUntilTicks)
		return evtWaitForever;
	now = TimGetTicks();
	return (gFindPendingUntilTicks > now) ? (gFindPendingUntilTicks - now) : 0;
}

/* Called from EventLoop on a nilEvent once MainFindPendingTimeout's deadline
 * has actually passed -- runs the (possibly expensive) filter now that typing
 * has paused. A no-op if nothing is pending (e.g. this nilEvent woke up for an
 * unrelated reason, or another handler already cleared the field). */
void MainFindFirePending(void)
{
	if (gFindPendingUntilTicks && TimGetTicks() >= gFindPendingUntilTicks) {
		gFindPendingUntilTicks = 0;
		MainFindFieldChanged();
	}
}

/* Empties fldMainFind's on-screen text only - does not touch filter state.
 * Used where the caller (category switch) resets gFindActive/gCardFindActive
 * itself and is about to do its own full MainDraw() anyway, so this just
 * needs to stop the box showing a now-inactive filter's stale text. */
static void MainClearFindFieldText(void)
{
	FieldPtr fld = (FieldPtr) GetObjectPtr(fldMainFind);

	if (fld) {
		FldDelete(fld, 0, FldGetTextLength(fld));
		FldDrawField(fld);
	}
}

static void MainDoCancel(void)
{
	FieldPtr fld = (FieldPtr) GetObjectPtr(fldMainFind);

	gFindActive = false;
	gFindTerm[0] = '\0';
	gFindPendingUntilTicks = 0;
	if (fld) {
		FldDelete(fld, 0, FldGetTextLength(fld));
		FldDrawField(fld);
	}
	gTopVisibleRecord = 0;
	MainDraw();
}

/* Restore the Find field text (after returning from a viewed match) so it stays in
 * sync with the still-active filtered list. */
static void MainRestoreFindField(void)
{
	FieldPtr fld;
	MemHandle h;
	const char *term;

	if (gCardMode) {
		if (!gCardFindActive || !gCardFindTerm[0]) return;
		term = gCardFindTerm;
	} else {
		if (!gFindActive || !gFindTerm[0]) return;
		term = gFindTerm;
	}
	fld = (FieldPtr) GetObjectPtr(fldMainFind);
	if (fld == NULL)
		return;
	h = MemHandleNew((UInt32) (StrLen(term) + 1));
	if (h) {
		char *p = (char *) MemHandleLock(h);
		StrCopy(p, term);
		MemHandleUnlock(h);
		FldSetTextHandle(fld, h);
	}
}

/* Page the list up/down by one screen (hardware scroll buttons). */
static void MainPageScroll(Boolean down)
{
	Int16 page = (Int16) MainFitRows();
	if (page < 1) page = 1;
	MainScroll(down ? page : (Int16) -page);
}

/* ------------------------------------------------------------------ *
 *  init / events
 * ------------------------------------------------------------------ */
static void MainInit(void)
{
	FormPtr formP = FrmGetActiveForm();
	TableType *tableP = MainTable();

	CategoryGetName(gAddrDB, gCurrentCategory, gCategoryName);
	CategorySetTriggerLabel((ControlType *) GetObjectPtr(popMainCategory),
		gCardMode ? "Card" : (gShowAllCategories ? "All" : gCategoryName));
	/* gCardDirValid re-enabled: the original list-mismatch bug this was
	 * disabled to troubleshoot was confirmed caused by the unchecked
	 * CardWriteFile return value + whole-file-read memory pressure (both
	 * fixed since), not by this cache - see [[address35-sdcard-ux-fixes]].
	 * Skipping the rescan when nothing changed also cuts real VFS traffic
	 * (a full CardListDir opens+closes the dir handle plus one open+close
	 * PER FILE just to read its size), worth having back given a leaked or
	 * flaky SD-card file handle is a live suspect for the exit crash. */
	if (gCardMode && !gCardDirValid)		/* returning to the browser: refresh listing */
		MainLoadCardDir();

	TblSetCustomDrawProcedure(tableP, kRecordColumn, MainDrawRecord);
	TblSetColumnUsable(tableP, kRecordColumn, true);

	/* If a Find filter is active (e.g. we're returning from viewing a match), restore the
	 * field text so it stays in sync with the filtered list. */
	MainRestoreFindField();

	/* Load the table rows BEFORE FrmDrawForm paints them: drawing the form
	 * first rendered the custom-draw cells with stale/default row state,
	 * flashing a column of missing-glyph boxes until MainDraw repainted. */
	MainUpdateScrollBar(MainLoadTable());
	FrmDrawForm(formP);
}

/* Build the category popup ourselves (All + named + Unfiled + "Edit Categories")
 * and show it with LstPopupList, which honours the list resource's fixed width -
 * ROM CategorySelect() re-sizes its popup to the content instead, which is why it
 * came out a different width from CryptoNote's. Same pattern as CryptoNote. */
/* Handle a tap on a card display row: ascend ([..]), descend (folder), open a
 * single-person .vcf as a read-only card contact (RecordForm card mode), or -
 * for a group vCard - prompt to split it into individual contacts (browsing
 * no longer auto-splits every file up front; see gCardDirValid's neighbor
 * comments and [[address35-vcard-macos-contacts-compat]] for why). */
static void MainCardTap(UInt16 di)
{
	Boolean isParent;
	CardEntry *e = MainCardEntryAt(di, &isParent);
	char full[cardMaxPath];

	if (isParent) {						/* [..] -> ascend, floored at root */
		CardParentPath(gCardPath);
		if (StrLen(gCardPath) < StrLen(a35CardRoot))
			StrCopy(gCardPath, a35CardRoot);
		MainLoadCardDir();
		MainDraw();
		return;
	}
	if (e == NULL) return;
	if (e->isDir) {						/* descend into the folder */
		CardJoinPath(full, gCardPath, e->name);
		StrCopy(gCardPath, full);
		MainLoadCardDir();
		MainDraw();
		return;
	}

	CardJoinPath(full, gCardPath, e->name);
	/* "Reading..." shows continuously from here through whichever outcome
	 * follows (count-check, then either the group-split prompt or the
	 * preview read) - a long file with nothing shown in between is exactly
	 * what made this feel hung before. */
	A35BusyBegin("Reading...");
	{
		UInt16 vcount = A35CardCountAt(gCardVol, gAddrDB, full);
		if (vcount > 1) {				/* group vCard: prompt before splitting */
			char num[12];
			A35BusyEnd();
			StrIToA(num, vcount);
			if (FrmCustomAlert(altCardGroupSplit, num, "", "") == 0) {	/* OK */
				UInt16 splitN;
				A35ProgressBegin("Processing vCard...");
				splitN = A35CardSplitFile(gCardVol, gAddrDB, gCardPath, full, vcount);
				A35ProgressEnd();
				MainLoadCardDir();		/* the split changed the file list */
				MainDraw();
				if (splitN != vcount)	/* a write failed partway - original preserved, not deleted */
					FrmAlert(altCardSplitFailed);
			}
			return;						/* Cancel, or done splitting: back to the list */
		}
	}
	if (A35CardReadContact(gCardVol, gAddrDB, full,
			&gCardRecord, &gCardArena) == errNone) {
		A35BusyEnd();
		gViewingCard = true;
		FrmGotoForm(frmRecord);
	} else {
		A35BusyEnd();
	}
}

/* Fold in any beam-received contacts parked (raw) in the Address35DBPlain inbox, then seal
 * them if unlocked.  Called on every category switch so a beam that arrived while the app is
 * running becomes visible by just switching category -- no quit+relaunch needed.  Cheap
 * no-op (one DmFindDatabase) when the inbox is empty. */
static void A35RefreshInbox(void)
{
	A35MergePlainDB(gAddrDB);
	A35SealAllPlaintext(gAddrDB);
}

static void MainSelectCategory(void)
{
	FormPtr formP = FrmGetActiveForm();
	ListPtr lst;
	static char  names[20][24];		/* >dmCategoryLength: fits "Edit Categories..." */
	static char *choices[20];
	Int16   meaning[20];			/* >=0 category index; -101 Edit Categories */
	Int16   n = 0, i, sel, m;

	A35RefreshInbox();			/* Address35: fold in freshly-beamed contacts before redisplay */

	StrCopy(names[n], "All"); choices[n] = names[n]; meaning[n] = dmAllCategories; n++;
	for (i = 1; i < dmRecNumCategories && n < 16; i++) {
		CategoryGetName(gAddrDB, i, names[n]);
		if (names[n][0]) { choices[n] = names[n]; meaning[n] = i; n++; }
	}
	CategoryGetName(gAddrDB, dmUnfiledCategory, names[n]);
	if (names[n][0]) { choices[n] = names[n]; meaning[n] = dmUnfiledCategory; n++; }
	StrCopy(names[n], "Edit Categories\205"); choices[n] = names[n]; meaning[n] = -101; n++;
	gCatCardRow = -1;
	{	/* SD "Card" source row LAST, with a divider above it (CryptoNote-style) */
		UInt16 volRef;
		if (CardHasVolume(&volRef)) {
			gCatCardRow = n;
			StrCopy(names[n], "Card"); choices[n] = names[n]; meaning[n] = -102; n++;
		}
	}

	lst = (ListPtr) GetObjectPtr(lstMainCategory);
	LstSetListChoices(lst, choices, n);
	LstSetSelection(lst, -1);
	LstSetHeight(lst, n < 11 ? n : 11);
	if (gCatCardRow >= 0) {			/* draw the card icon/divider + 1px breathing room */
		LstSetDrawFunction(lst, CardCatListDraw);
		lst->bounds.extent.y += 1;
	}
	sel = LstPopupList(lst);
	LstSetDrawFunction(lst, NULL);
	gCatCardRow = -1;
	if (sel < 0) return;
	m = meaning[sel];

	if (m == -102) {				/* "Card" -> browse SD-card vCards in the list */
		gFindActive = false;		/* switching to Card exits the (DB-list) Find filter */
		gFindPendingUntilTicks = 0;
		gFindTerm[0] = '\0';
		MainClearFindFieldText();
		if (MainEnterCard()) {
			CategorySetTriggerLabel(
				(ControlType *) GetObjectPtr(popMainCategory), "Card");
			MainDraw();
		}
		return;
	}
	gCardMode = false;				/* any real choice leaves card-browse mode */
	if (m == -101) {				/* Edit Categories -> straight to the editor
									 * (CategoryEdit, NOT CategorySelect, which would
									 * re-pop the list and force a 2nd "Edit Categories"
									 * tap - matches ToDo35's one-popup behaviour). */
		(void) formP;
		if (!CategoryEdit(gAddrDB, &gCurrentCategory, 0, 1))
			return;					/* nothing changed */
		CategoryGetName(gAddrDB, gCurrentCategory, gCategoryName);
	} else {						/* a normal category */
		gCurrentCategory = (UInt16) m;
		CategoryGetName(gAddrDB, gCurrentCategory, gCategoryName);
	}
	gFindActive = false;		/* switching category exits the Find filter */
	gFindPendingUntilTicks = 0;
	gFindTerm[0] = '\0';
	MainClearFindFieldText();
	gShowAllCategories = (Boolean) (gCurrentCategory == dmAllCategories);
	gTopVisibleRecord = 0;
	CategorySetTriggerLabel((ControlType *) GetObjectPtr(popMainCategory),
		gShowAllCategories ? "All" : gCategoryName);
	MainDraw();
}

/* #7: advance to the next category, cycling through ALL named categories
 * (including empty ones) and wrapping through "All".  Bound to the hard app
 * button so repeated presses walk every category. */
static void MainNextCategory(void)
{
	UInt16 next;

	A35RefreshInbox();			/* Address35: fold in freshly-beamed contacts before redisplay */

	next = CategoryGetNext(gAddrDB, gCurrentCategory);

	if (next == gCurrentCategory) return;

	gCurrentCategory = next;
	gFindActive = false;		/* switching category exits the Find filter */
	gFindPendingUntilTicks = 0;
	gFindTerm[0] = '\0';
	MainClearFindFieldText();
	gShowAllCategories = (Boolean) (gCurrentCategory == dmAllCategories);
	gTopVisibleRecord = 0;
	gCardMode = false;
	CategoryGetName(gAddrDB, gCurrentCategory, gCategoryName);
	CategorySetTriggerLabel((ControlType *) GetObjectPtr(popMainCategory),
		gShowAllCategories ? "All" : gCategoryName);
	MainDraw();
}


Boolean MainFormHandleEvent(EventPtr eventP)
{
	Boolean handled = false;

	switch (eventP->eType) {
	case frmOpenEvent:
		/* Fold in anything beamed since we last drew (covers a reload while the app
		 * was already running), then seal it if we're unlocked. No-op when empty.
		 * SKIPPED on the very first open of a fresh launch: StartApplication just
		 * ran this identical pair (both real, whole-database DmNumRecords scans),
		 * and nothing could plausibly have changed in the instant since -- doing
		 * it again here doubled the launch-time scan cost for no benefit. Still
		 * runs on every subsequent return to frmMain, where a beam or an unlock
		 * really can have happened while some other form was showing. */
		if (!gMainDbFreshFromLaunch) {
			A35MergePlainDB(gAddrDB);
			A35SealAllPlaintext(gAddrDB);
		}
		gMainDbFreshFromLaunch = false;
		MainInit();
		handled = true;
		break;

	case keyDownEvent:
		/* #7: hard app-button re-press cycles to the next category. */
		if (TxtCharIsHardKey(eventP->data.keyDown.modifiers,
									eventP->data.keyDown.chr)) {
			MainNextCategory();
			handled = true;
		}
		/* Hardware up/down scroll buttons page the list. */
		else if (eventP->data.keyDown.chr == vchrPageUp ||
				 eventP->data.keyDown.chr == vchrPageDown) {
			MainPageScroll((Boolean) (eventP->data.keyDown.chr == vchrPageDown));
			handled = true;
		}
		else {
			/* Live Find filter (SEC001): dispatch the key to fldMainFind ourselves so
			 * the field updates immediately -- fldChangedEvent does not fire per
			 * keystroke for this single-line field, same idiom the hand-rolled
			 * picker loops (A35PickList/RelPickContact) already use. The actual
			 * (decrypt-heavy) filter pass is debounced ~0.5s behind the typing
			 * itself (see gFindPendingUntilTicks/MainFindFirePending) so a fast
			 * typist doesn't pay for a filter pass on every single keystroke;
			 * clearing the field to empty is cheap and exits find mode right away
			 * instead of waiting out the debounce. */
			FieldPtr fld = (FieldPtr) GetObjectPtr(fldMainFind);
			if (fld && FldHandleEvent(fld, eventP)) {
				char *t = (char *) FldGetTextPtr(fld);
				if (t && t[0])
					gFindPendingUntilTicks = TimGetTicks() +
						(SysTicksPerSecond() / kFindDebounceTicksFrac);
				else {
					gFindPendingUntilTicks = 0;
					MainFindFieldChanged();
				}
				handled = true;
			}
		}
		break;

	case fldChangedEvent:				/* live Find filter (SEC001) -- no Find button */
		if (eventP->data.fldChanged.fieldID == fldMainFind)
			MainFindFieldChanged();
		break;

	case frmUpdateEvent:
		MainDraw();
		handled = true;
		break;

	case ctlSelectEvent:
		switch (eventP->data.ctlSelect.controlID) {
		case popMainCategory:
			MainSelectCategory();
			handled = true;
			break;
		case btnMainNew:
			if (!A35LaunchGate()) { handled = true; break; }	/* locked -> Sec35 */
			gEditIndex = a35NoRecord;
			FrmGotoForm(frmEdit);
			handled = true;
			break;
		case btnMainCancel:				/* clear the filter (SEC001) */
			MainDoCancel();
			handled = true;
			break;
		}
		break;

	case tblSelectEvent:
		if (gCardMode) {				/* navigate the card browser */
			MainCardTap(TblGetRowID((TableType *) MainTable(),
				eventP->data.tblSelect.row));
			handled = true;
			break;
		}
		/* Opening a contact needs the data decrypted -> if locked, hand off to
		 * Sec35 to unlock (it returns here), rather than showing masked fields. */
		if (!A35LaunchGate()) { handled = true; break; }
		gCurrentRecord = TblGetRowID((TableType *) MainTable(),
			eventP->data.tblSelect.row);
		gEditIndex = gCurrentRecord;
		gViewingCard = false;			/* a real DB record, not a card vCard */
		FrmGotoForm(frmRecord);			/* tap -> read-only view -> Edit */
		handled = true;
		break;

	case sclRepeatEvent:
		MainScroll((Int16) (eventP->data.sclRepeat.newValue -
			eventP->data.sclRepeat.value));
		/* not "handled" so the scrollbar keeps tracking */
		break;

	case menuEvent:
		switch (eventP->data.menu.itemID) {
		case mniMainNew:
			if (!A35LaunchGate()) { handled = true; break; }	/* locked -> Sec35 */
			gEditIndex = a35NoRecord;
			FrmGotoForm(frmEdit);
			handled = true;
			break;
		case mniMainFont:
			gListFont = FontSelect(gListFont);
			gTopVisibleRecord = 0;			/* row count changed; start at top */
			MainDraw();
			handled = true;
			break;
		case mniMainPrefs:
			FrmPopupForm(frmPrefs);
			handled = true;
			break;
		case mniMainAbout:
			FrmPopupForm(frmAbout);
			handled = true;
			break;
		case mniMainBeam:
			A35BeamCategory(gAddrDB, CurrentCat());
			handled = true;
			break;
		case mniMainBrowseCard:
			if (MainEnterCard()) {
				CategorySetTriggerLabel(
					(ControlType *) GetObjectPtr(popMainCategory), "Card");
				MainDraw();
			}
			handled = true;
			break;
		case mniMainImport: {
			UInt16 volRef, cnt = 0;
			char num[12];
			if (!CardHasVolume(&volRef)) { FrmAlert(altNoCard); handled = true; break; }
			A35CardImport(gAddrDB, volRef, &cnt);
			StrIToA(num, cnt);
			MainDraw();					/* show any newly imported contacts */
			FrmCustomAlert(altImportDone, num, "", "");
			handled = true;
			break;
		}
		case mniMainExport: {
			UInt16 volRef, cnt = 0;
			char num[12];
			UInt16 choice;
			if (!CardHasVolume(&volRef)) { FrmAlert(altNoCard); handled = true; break; }
			choice = FrmAlert(altExportScope);		/* 0=Category 1=All 2=Cancel */
			if (choice == 2) { handled = true; break; }
			if (choice == 0 && CurrentCat() != dmAllCategories)
				A35CardExport(gAddrDB, volRef, a35ExpCategory, CurrentCat(), 0, 0, &cnt);
			else
				A35CardExport(gAddrDB, volRef, a35ExpAll, 0, 0, 0, &cnt);
			StrIToA(num, cnt);
			FrmCustomAlert(altExportDone, num, "", "");
			handled = true;
			break;
		}
		case mniMainDelete:
			/* Delete acts on an OPEN contact; from the list nothing is selected. */
			FrmAlert(altNoSelection);
			handled = true;
			break;
		default:
			/* Leave unhandled (handled stays false) so the system Edit menu commands
			 * (Cut/Copy/Paste/Undo/Select All/Keyboard/Graffiti) fall through to
			 * FrmHandleEvent, which performs them on the focused field. */
			break;
		}
		break;

	default:
		break;
	}
	return handled;
}
