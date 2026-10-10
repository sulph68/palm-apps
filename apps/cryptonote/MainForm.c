#include "MainForm.h"
#include "Database.h"
#include "Utils.h"
#include "AesCrypt.h"
#include "CryptoSec35.h"
#include "Beam.h"
#include <PalmUtils.h>

#include <SysEvtMgr.h>
#include "CardIO.h"   /* includes PalmOS.h then VFSMgr.h in the right order */
#include "EditForm.h" /* card-file edit bridge: gCardFileOpen, gCardEditPath, ... */
#include "CardCrypt.h" /* .enc detection / decrypt + password prompt */

/* Deliberately NOT AES_SECTION (lands in main, same as the rest of this
 * file's untagged code) -- a same-segment call from this file's sec001-
 * tagged functions straight to CryptoSec35.c's sec001 CryptoCardLockedBounce
 * started failing to link ("relocation truncated to fit: DISP16") once
 * PasswordForm.c's masking+keypad code grew sec001 enough to push the two
 * files' code too far apart for a same-segment 16-bit branch, even with
 * plenty of total segment budget free -- a distance problem, not a size one.
 * Routing through this trivial main-segment hop turns both legs into
 * ordinary cross-segment far calls, which have no such distance limit.
 * Same fix as documented for this exact trap elsewhere in this codebase. */
static Boolean LockedBounceRelay(void)
{
	return CryptoCardLockedBounce();
}

/* Same same-segment-DISP16 trap as LockedBounceRelay above, hit by
 * DbRecordMatches (sec001, this file) calling CryptoSec35.c's sec001
 * CryptoGetRecordTitle directly once the DB-list filter code was added. */
static Boolean CryptoGetRecordTitleRelay(DmOpenRef dbRef, UInt16 index,
	Char *outTitle, UInt16 outTitleSize, Boolean *outEncrypted)
{
	return CryptoGetRecordTitle(dbRef, index, outTitle, outTitleSize, outEncrypted);
}

/* ============================================================================
 * Card ("SD") source. When the "Card" item is chosen from the category popup
 * the main list browses /Notes on the card (files + [Folder]/[ .. ]) instead of
 * DB records. Everything here is gated by gCardMode; the DB-note code paths run
 * unchanged whenever gCardMode is false. Low-level VFS is in CardIO.{c,h}.
 * ==========================================================================*/
#define kCardSentinel   0xFFFFU          /* kRecordColumn marker for a card row */
#define kMaxCardEntries 64

Boolean         gCardMode = false;       /* extern'd for EditForm later          */
static UInt16   gCardVol;                /* current volume ref                   */
static char     gCardPath[cardMaxPath];  /* current dir, floored at /Notes       */
static CardEntry gCardEnt[kMaxCardEntries];
static Int16    gCardOrder[kMaxCardEntries]; /* manual-order key per entry (parallel)*/
static UInt16   gCardCount = 0;          /* real entries (excludes the [..] row) */
static UInt16   gCardTop = 0;            /* scroll offset into the display list   */
static Boolean  gCardHasParent = false;  /* show a [ .. ] row at display index 0  */

/* True once gCardEnt[]/gCardCount hold a fresh read of gCardPath. MainFormInit's
 * "returning to the list" path skips the (potentially slow, real SD-card)
 * directory scan when this is still true - nothing on the card changes just by
 * viewing a note and coming back. Anything that actually writes/deletes/renames
 * a card file (EditForm.c's save/delete paths) calls MainInvalidateCardDir() to
 * force a real rescan next time; real navigation (folder change, new note,
 * bulk-encrypt, etc.) already goes through MainFormLoadCardDir()/
 * MainFormCardRefresh() directly, which always rescans and re-sets this true. */
static Boolean  gCardDirValid = false;

void MainInvalidateCardDir(void)
{
	gCardDirValid = false;
}

/* Filename filter for the card browser (fldMainFind, same row as "New"), same
 * idiom as Address35's list-view Find bar. Filtering filenames is cheap (no
 * decrypt), so unlike Address35's DB-record filter this runs live on every
 * keystroke with no debounce. While active the [ .. ] row is hidden (matches
 * Address35: filtering by name doesn't make ascending meaningful) and manual
 * drag-reorder is disabled (a filtered view's on-screen order doesn't map
 * cleanly onto the real underlying positions ".cnorder" reorders). */
static Boolean  gCardFindActive = false;
static char     gCardFindTerm[40];
static UInt16   gCardMatch[kMaxCardEntries];
static UInt16   gCardMatchCount = 0;

/* per-folder file holding the manual file order, one name per line. Hidden from
 * the browser (no ".txt"). See option B in the manual-sort design. */
#define cardOrderFile ".cnorder"

static void MainFormUpdateScrollBar(void);   /* used by the card table loader below */
static void MainFormLoadTable(void);         /* used by the card navigation below   */

static UInt16 CardDisplayCount(void)
{
	if (gCardFindActive) return gCardMatchCount;
	return gCardCount + (gCardHasParent ? 1 : 0);
}

/* Resolve a display index to its underlying gCardEnt entry, honoring the
 * active filter (if any); *isParent is set true for the [ .. ] row (only
 * possible when no filter is active). NULL means di is out of range. */
static CardEntry *CardEntryAt(UInt16 di, Boolean *isParent) AES_SECTION;
static CardEntry *CardEntryAt(UInt16 di, Boolean *isParent)
{
	*isParent = false;
	if (gCardFindActive) {
		if (di >= gCardMatchCount) return NULL;
		return &gCardEnt[gCardMatch[di]];
	}
	if (gCardHasParent && di == 0) { *isParent = true; return NULL; }
	{
		UInt16 ei = di - (gCardHasParent ? 1 : 0);
		if (ei >= gCardCount) return NULL;
		return &gCardEnt[ei];
	}
}

/* Case-insensitive substring search (name isn't guaranteed lowercase). */
static Boolean CardNameHasTerm(const char *name, const char *lower, UInt16 tl) AES_SECTION;
static Boolean CardNameHasTerm(const char *name, const char *lower, UInt16 tl)
{
	UInt16 len = StrLen(name);
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

/* Rebuild gCardMatch[] from gCardFindTerm against the current gCardEnt[]. */
static void MainBuildCardFind(void) AES_SECTION;
static void MainBuildCardFind(void)
{
	char   lower[40];
	UInt16 tl = (UInt16) StrLen(gCardFindTerm);
	UInt16 i, k;

	if (tl >= sizeof(lower)) tl = sizeof(lower) - 1;
	for (k = 0; k < tl; k++) {
		char c = gCardFindTerm[k];
		if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
		lower[k] = c;
	}
	lower[tl] = '\0';

	gCardMatchCount = 0;
	for (i = 0; i < gCardCount && gCardMatchCount < kMaxCardEntries; i++)
		if (CardNameHasTerm(gCardEnt[i].name, lower, tl))
			gCardMatch[gCardMatchCount++] = i;
	gCardTop = 0;
}

/* ---- Normal (non-card) list filter --------------------------------------
 * Rather than building a parallel filtered display list, the filter is
 * applied AT THE SOURCE: MainFormGetNextRecord/MainFormGetPreviousRecord
 * (the sole record-traversal primitives; every row-load/scroll/count/drag
 * path already funnels through them) transparently skip non-matching
 * records when gDbFindActive. Every existing caller keeps working unchanged
 * - it just never sees a non-matching record. */
static MemHandle MainFormGetNextRecord(Int16*);	/* real decl (+ tag) later in the file */
static void MainFormLoadRecords(void);			/* real decl (+ tag) later in the file */
static Boolean  gDbFindActive = false;
static char     gDbFindTerm[40];
static char     gDbFindLower[40];	/* lowercased once per keystroke, not per record */
static UInt16   gDbFindLen = 0;
static UInt16   gDbFindMatchCount = 0;	/* for the scrollbar only, see MainCountDbMatches */

/* True if record recordNum (in dbCryptoPad when cryptoPad, else dbMemoPad)
 * contains gDbFindLower. Masked private records never match - a filter
 * shouldn't be a side-channel for finding what masking is hiding. */
static Boolean DbRecordMatches(Boolean cryptoPad, UInt16 recordNum) AES_SECTION;
static Boolean DbRecordMatches(Boolean cryptoPad, UInt16 recordNum)
{
	DmOpenRef dbRef = cryptoPad ? dbCryptoPadRef : dbMemoPadRef;
	UInt16 recAttributes;

	DmRecordInfo(dbRef, recordNum, &recAttributes, NULL, NULL);
	if ((recAttributes & dmRecAttrSecret) && PrivateRecordVisualStatus == maskPrivateRecords)
		return false;

	if (cryptoPad) {
		Char title[81];
		Boolean encrypted;
		if (!CryptoGetRecordTitleRelay(dbRef, recordNum, title, sizeof(title), &encrypted))
			return false;
		return CardNameHasTerm(title, gDbFindLower, gDbFindLen);
	} else {
		MemHandle h = DmQueryRecord(dbRef, recordNum);
		char *textP;
		Boolean m;
		if (!h) return false;
		textP = MemHandleLock(h);
		m = textP ? CardNameHasTerm(textP, gDbFindLower, gDbFindLen) : false;
		MemHandleUnlock(h);
		return m;
	}
}

/* Count matches for the scrollbar (MainFormUpdateScrollBar) - the traversal
 * skip above makes scrolling/loading correct with no further changes, but
 * the scrollbar's range still needs an accurate total. Walks once per
 * keystroke via the very same wrapped traversal, so it can't disagree with
 * what scrolling/loading actually see. */
static void MainCountDbMatches(void) AES_SECTION;
static void MainCountDbMatches(void)
{
	Boolean savedDB = currentDB;
	Int16 recordNum = -1;

	currentDB = memoPadDB;
	gDbFindMatchCount = 0;
	while (MainFormGetNextRecord(&recordNum) != 0)
		gDbFindMatchCount++;
	currentDB = savedDB;
}

/* Live filter callback (fldChangedEvent + the keyDownEvent relay below).
 * Card mode filters filenames; otherwise filters memo/note titles. */
static void MainFindFieldChanged(void) AES_SECTION;
static void MainFindFieldChanged(void)
{
	FieldPtr fld = (FieldPtr) GetActiveFormObjectPtr(fldMainFind);
	char *t = fld ? (char *) FldGetTextPtr(fld) : NULL;
	const char *activeTerm = gCardMode ? gCardFindTerm : gDbFindTerm;
	Boolean wasActive = gCardMode ? gCardFindActive : gDbFindActive;

	/* Idempotency guard: a programmatic FldDelete() on a field that has
	 * REAL prior edit history (i.e. the user has actually typed into it at
	 * least once) can queue a delayed fldChangedEvent that only gets
	 * delivered on some LATER, unrelated event - e.g. the next category
	 * switch, making the list appear to redraw twice and keep doing so on
	 * every switch after that (each MainClearFindFieldText's own FldDelete
	 * queues another one). Skip the whole refresh if the field's current
	 * text already matches what's already reflected in state - a genuine
	 * edit always changes the text, so this can only ever skip a redundant
	 * re-trigger, never a real one. */
	if ((t && t[0]) ? (wasActive && StrCompare(t, activeTerm) == 0) : !wasActive)
		return;

	if (gCardMode) {
		if (t && t[0]) {
			StrNCopy(gCardFindTerm, t, sizeof(gCardFindTerm) - 1);
			gCardFindTerm[sizeof(gCardFindTerm) - 1] = '\0';
			gCardFindActive = true;
			MainBuildCardFind();
		} else {
			gCardFindActive = false;
			gCardFindTerm[0] = '\0';
			gCardTop = 0;
		}
		{
			TablePtr tableP = GetActiveFormObjectPtr(tblMainRecordList);
			MainFormLoadTable();
			TblEraseTable(tableP);
			TblDrawTable(tableP);
		}
		return;
	}

	if (t && t[0]) {
		UInt16 k;

		StrNCopy(gDbFindTerm, t, sizeof(gDbFindTerm) - 1);
		gDbFindTerm[sizeof(gDbFindTerm) - 1] = '\0';
		gDbFindLen = (UInt16) StrLen(gDbFindTerm);
		if (gDbFindLen >= sizeof(gDbFindLower)) gDbFindLen = sizeof(gDbFindLower) - 1;
		for (k = 0; k < gDbFindLen; k++) {
			char c = gDbFindTerm[k];
			if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
			gDbFindLower[k] = c;
		}
		gDbFindLower[gDbFindLen] = '\0';
		gDbFindActive = true;
		MainCountDbMatches();
	} else {
		gDbFindActive = false;
		gDbFindTerm[0] = '\0';
	}
	topRecord = 0;
	topDB = memoPadDB;
	currentRecord = noRecordSelected;
	MainFormLoadRecords();
	TblRedrawTable((TablePtr) GetActiveFormObjectPtr(tblMainRecordList));
}

/* Empties fldMainFind's on-screen text without touching any filter state -
 * used when a category switch resets the filter flags directly (a fuller
 * redraw is coming right behind it anyway, so MainFindFieldChanged's own
 * redraw would just be wasted work). */
static void MainClearFindFieldText(void) AES_SECTION;
static void MainClearFindFieldText(void)
{
	FieldPtr fld = (FieldPtr) GetActiveFormObjectPtr(fldMainFind);

	if (fld) {
		FldDelete(fld, 0, FldGetTextLength(fld));
		FldDrawField(fld);
	}
}

/* Clear button: exits whichever filter is active and redraws. */
static void MainFindClear(void) AES_SECTION;
static void MainFindClear(void)
{
	MainClearFindFieldText();
	MainFindFieldChanged();	/* field is now empty -- exits the active filter */
}

/* Mgr1-style: VFS feature present + a volume mounted (impl in CardIO.c). */
static Boolean CardPresent(UInt16 *volP) { return CardHasVolume(volP); }

/* CryptoNote supports plain-text notes only: a name is a note if it ends in
 * ".txt" (optionally with a trailing ".enc" for the encrypted form). */
static Boolean CardIsTxt(const char *name) AES_SECTION;
static Boolean CardIsTxt(const char *name)
{
	UInt16 n = StrLen(name);
	if (n >= 4 && StrNCompare(name + n - 4, ".enc", 4) == 0) n -= 4;
	return (n >= 4 && StrNCompare(name + n - 4, ".txt", 4) == 0);
}

/* macOS AppleDouble resource forks ("._foo.txt") and metadata files
 * (".DS_Store") are all just dot-prefixed names - a leading '.' covers both
 * without needing to special-case each pattern. */
static Boolean CardIsHidden(const char *name) AES_SECTION;
static Boolean CardIsHidden(const char *name)
{
	return (Boolean) (name[0] == '.');
}

/* Fill gCardOrder[] (parallel to the just-loaded gCardEnt, filesystem order) with
 * each file's manual rank: its line number in the current folder's ".cnorder", or
 * a large value (keeping filesystem order) if it isn't listed. Only used in manual
 * (soManual) sort. */
static void MainFormLoadCardOrder(void) AES_SECTION;
static void MainFormLoadCardOrder(void)
{
	MemHandle h;
	UInt32    sz;
	char      path[cardMaxPath];
	UInt16    i;

	for (i = 0; i < gCardCount; i++)
		gCardOrder[i] = 10000 + i;               /* default: keep filesystem order */

	CardJoinPath(path, gCardPath, cardOrderFile);
	if (CardReadFile(gCardVol, path, &h, &sz) != errNone || !h) return;
	{
		char *data = MemHandleLock(h);
		UInt32 p = 0;
		Int16  ln = 0;
		while (p < sz) {
			char name[cardMaxName];
			UInt16 k = 0;
			while (p < sz && data[p] != '\n' && data[p] != '\r' && k < cardMaxName - 1)
				name[k++] = data[p++];
			name[k] = 0;
			while (p < sz && (data[p] == '\n' || data[p] == '\r')) p++;
			if (k > 0) {
				for (i = 0; i < gCardCount; i++)
					if (!gCardEnt[i].isDir && StrCompare(gCardEnt[i].name, name) == 0) {
						gCardOrder[i] = ln;
						break;
					}
				ln++;
			}
		}
		MemHandleUnlock(h);
	}
	MemHandleFree(h);
}

/* Folders always sort to the top, case-insensitive alphanumeric. Files follow the
 * sort preference: alphabetic (A-Z) when soAlphabetic, otherwise the manual order
 * from ".cnorder" (files not listed keep filesystem order at the end). Stable
 * insertion sort carrying the parallel gCardOrder[] key. */
static void MainFormSortCardEntries(void) AES_SECTION;
static void MainFormSortCardEntries(void)
{
	Int16 i, j;
	Boolean alphaFiles = (currentSortOrder == soAlphabetic);

	if (!alphaFiles) MainFormLoadCardOrder();    /* manual: rank files by .cnorder */

	for (i = 1; i < (Int16) gCardCount; i++) {
		CardEntry key = gCardEnt[i];
		Int16     keyO = gCardOrder[i];
		j = i - 1;
		while (j >= 0) {
			CardEntry *a = &gCardEnt[j];
			Boolean after;
			if (a->isDir != key.isDir)
				after = (!a->isDir && key.isDir);        /* folder before file */
			else if (a->isDir)
				after = (StrCaselessCompare(a->name, key.name) > 0);  /* folders: always alpha */
			else if (alphaFiles)
				after = (StrCaselessCompare(a->name, key.name) > 0);
			else
				after = (gCardOrder[j] > keyO);          /* manual: by .cnorder rank */
			if (!after) break;
			gCardEnt[j + 1] = gCardEnt[j];
			gCardOrder[j + 1] = gCardOrder[j];
			j--;
		}
		gCardEnt[j + 1] = key;
		gCardOrder[j + 1] = keyO;
	}
}

static void MainFormLoadCardDir(void) AES_SECTION;
static void MainFormLoadCardDir(void)
{
	UInt16 i, k;

	gCardHasParent = (StrCompare(gCardPath, cardNotesDir) != 0);
	{
		WinHandle busy = CardBusyShow("Reading...");	/* real SD-card directory scans can be slow */
		Err err = CardListDir(gCardVol, gCardPath, gCardEnt, kMaxCardEntries, &gCardCount);
		CardBusyHide(busy);
		if (err != errNone)
			gCardCount = 0;
	}

	/* keep folders and .txt / .txt.enc notes only (hide .md and everything else) */
	for (i = 0, k = 0; i < gCardCount; i++) {
		if (!showHidden && CardIsHidden(gCardEnt[i].name))
			continue;
		if (gCardEnt[i].isDir || CardIsTxt(gCardEnt[i].name)) {
			if (k != i) gCardEnt[k] = gCardEnt[i];
			k++;
		}
	}
	gCardCount = k;

	MainFormSortCardEntries();
	gCardTop = 0;
	gCardDirValid = true;
	gCardFindActive = false;		/* leaving/reloading a directory drops any filename filter */
}

/* Enter card mode at /Notes (creating it if needed). Returns false if no card. */
static Boolean MainFormEnterCard(void)
{
	if (!CardPresent(&gCardVol)) return false;
	StrCopy(gCardPath, cardNotesDir);
	CardEnsureDir(gCardVol, cardNotesDir);
	gCardMode = true;
	MainFormLoadCardDir();
	return true;
}

/* Fill the table rows with card display-list indices (parallel to the DB
 * MainFormLoadTable, but fixed height and using the card sentinel). */
static void MainFormLoadCardTable(TablePtr tableP)
{
	Int16 numRows = TblGetNumberOfRows(tableP), nRow;
	RectangleType r;
	UInt16 tableHeight, lineHeight, dataHeight = 0, dispCount = CardDisplayCount();

	TblGetBounds(tableP, &r); tableHeight = r.extent.y;
	FntSetFont(currentTableFont); lineHeight = FntLineHeight();

	for (nRow = 0; nRow < numRows; nRow++) {
		UInt16 di = gCardTop + nRow;
		if (di < dispCount && tableHeight >= dataHeight + lineHeight) {
			TblSetItemStyle(tableP, nRow, kRecordColumn, customTableItem);
			TblSetItemFont (tableP, nRow, kRecordColumn, currentTableFont);
			TblSetRowHeight(tableP, nRow, lineHeight);
			TblSetRowID    (tableP, nRow, di);
			TblSetItemInt  (tableP, nRow, kRecordColumn, kCardSentinel);
			TblSetRowUsable(tableP, nRow, true);
			TblMarkRowInvalid(tableP, nRow);
		} else {
			TblSetRowHeight(tableP, nRow, lineHeight);
			TblSetRowUsable(tableP, nRow, false);
		}
		dataHeight += lineHeight;
	}
	MainFormUpdateScrollBar();
}

/* Card-mode counterparts of MainFormScrollLines/MainFormUpdateScrollBar --
 * split out to AES_SECTION (sec001) since main is at its 32KB cap; the two
 * main-segment functions just branch to these on gCardMode. See the callers'
 * comments for why this was needed (gCardTop was never actually scrolled). */
static Int16 MainFormNumberOfRows (TablePtr);	/* real decl (+ tag) later in the file */
static void MainFormCardScrollLines(Int16 nLines) AES_SECTION;
static void MainFormCardScrollLines(Int16 nLines)
{
	Int16 dispCount = (Int16) CardDisplayCount();
	Int16 rows = MainFormNumberOfRows(GetActiveFormObjectPtr(tblMainRecordList));
	Int16 maxTop = (dispCount > rows) ? (Int16) (dispCount - rows) : 0;
	Int16 nt = (Int16) gCardTop + nLines;
	TablePtr tableP;

	if (nt > maxTop) nt = maxTop;
	if (nt < 0) nt = 0;
	gCardTop = (UInt16) nt;

	tableP = GetActiveFormObjectPtr(tblMainRecordList);
	MainFormLoadCardTable(tableP);
	TblRedrawTable(tableP);
}

static void MainFormCardUpdateScrollBar(ScrollBarPtr scrollBarP, Int16 nRows) AES_SECTION;
static void MainFormCardUpdateScrollBar(ScrollBarPtr scrollBarP, Int16 nRows)
{
	UInt16 dispCount = CardDisplayCount();
	UInt16 nPosition, maxValue;

	if (dispCount < (UInt16) nRows) {
		maxValue = 0;
		nPosition = 0;
	} else {
		nPosition = gCardTop;
		maxValue = dispCount - (UInt16) nRows;
	}
	SclSetScrollBar(scrollBarP, nPosition, 0, maxValue, nRows);
}

/* Draw a small Tbmp icon at (x,y), returning silently if the resource is absent. */
static void CardDrawIcon(UInt16 bmpId, UInt16 x, UInt16 y)
{
	MemHandle h = DmGetResource(bitmapRsc, bmpId);
	if (h) {
		BitmapPtr b = (BitmapPtr) MemHandleLock(h);
		WinDrawBitmap(b, x, y);
		MemHandleUnlock(h);
		DmReleaseResource(h);
	}
}

/* Category popup: display index of the "Card" row (or -1). The divider line is
 * drawn at the top edge of this row (not a separate blank row), the way the ROM
 * launcher does it - ~2px below "Edit Categories" and ~2px above the card
 * icon/text - so there's no oversized gap on either side. */
static Int16 gCatCardRow = -1;

/* Db35/A35-style SD-card glyph: char 0x1A is the STANDARD font table's card
 * icon (stdFont/boldFont) -- NOT symbolFont/symbol11Font (a different,
 * unrelated glyph set; confirmed wrong on real hardware once already in
 * Address35). Address35's own CardCatListDraw (MainForm.c) is the proven
 * reference this is cloned from -- same font, same WinDrawChars idiom. */
static void CardCatListDraw(Int16 itemNum, RectangleType *bounds, Char **itemsText)
{
	Char *s = itemsText[itemNum];
	FntSetFont(stdFont);
	if (itemNum == gCatCardRow) {                /* divider + card glyph + "Card" */
		Int16 topY = bounds->topLeft.y;
		char glyph[2];
		glyph[0] = (char) 0x1A; glyph[1] = '\0';
		WinDrawChars(glyph, 1, bounds->topLeft.x + 1, topY + 2);
		WinDrawChars(s, StrLen(s), bounds->topLeft.x + 9, topY + 2);
		/* Draw the divider LAST: WinDrawChars paints its cell background from
		 * the same row as the line, so drawing the line first leaves a gap
		 * under the text.  The glyphs sit a couple rows lower, so the
		 * line laid on top doesn't touch them.  Overshoot x so it clips flush
		 * to the popup frame (edge-to-edge). */
		WinDrawLine(bounds->topLeft.x - 4, topY + 1,
		            bounds->topLeft.x + bounds->extent.x + 2, topY + 1);
	} else {
		WinDrawChars(s, StrLen(s), bounds->topLeft.x, bounds->topLeft.y);
	}
}

/* Draw one card row, styled like the DB memo list where it makes sense:
 *  - [ .. ] : up icon + ".."         (navigation)
 *  - folder : folder icon + name     (navigation, not numbered)
 *  - file   : "N. name" numbered like a memo; if encrypted (.enc) the extension
 *             is hidden and the CryptoPad key icon is drawn right-aligned.
 * Icons are 10-11px to fit the ~11px line and are vertically centered.
 * In sec001 (foreground-only, no libgcc division) to keep main under 32 KB. */
static void MainFormDrawCardRow(void *tableP, Int16 row, RectanglePtr bounds) AES_SECTION;
static void MainFormDrawCardRow(void *tableP, Int16 row, RectanglePtr bounds)
{
	UInt16 di = TblGetRowID(tableP, row);
	UInt16 x = bounds->topLeft.x + 1, y = bounds->topLeft.y;
	Int16  iconY;

	FntSetFont(currentTableFont);
	iconY = y + ((Int16)FntLineHeight() - 11) / 2;
	if (iconY < y) iconY = y;

	{
		Boolean isParent;
		CardEntry *e = CardEntryAt(di, &isParent);
		UInt16 ei;

		if (isParent) {                               /* [ .. ] row */
			CardDrawIcon(bmpCardUp, x + 1, iconY);
			WinDrawChars("..", 2, x + 13, y);
			return;
		}
		if (e == NULL) return;
		ei = (UInt16) (e - gCardEnt);                 /* true index -- file numbering
		                                                * below stays stable across filtering */

		if (e->isDir) {                              /* folder */
			CardDrawIcon(bmpCardFolder, x + 1, iconY);
			WinDrawChars(e->name, StrLen(e->name), x + 13, y);
			return;
		}
		{                                            /* numbered file */
			UInt16 nl = StrLen(e->name);
			Boolean enc = (nl >= 4 && StrNCompare(e->name + nl - 4, ".enc", 4) == 0);
			UInt16 dispLen = enc ? nl - 4 : nl;      /* hide the ".enc" suffix   */
			UInt16 right = enc ? 7 : 0;              /* room for the key icon    */
			char   posStr[6], disp[cardMaxName];
			UInt16 k, fnum = 0, length;
			Int16  cellWidth, tx = x;

			if (dispLen >= 4 && StrNCompare(e->name + dispLen - 4, ".txt", 4) == 0)
				dispLen -= 4;                        /* hide the ".txt" too      */

			for (k = 0; k <= ei; k++)                /* 1-based file number */
				if (!gCardEnt[k].isDir) fnum++;

			StrIToA(posStr, fnum);
			length = StrLen(posStr);
			posStr[length++] = '.';
			posStr[length] = 0;
			if (length < 3) tx += FntCharWidth('1');  /* right-align single digit */
			WinDrawChars(posStr, length, tx, y);
			tx += FntCharsWidth(posStr, length) + 4;

			StrNCopy(disp, e->name, dispLen);
			disp[dispLen] = 0;
			cellWidth = bounds->extent.x - tx - right;
			DrawStringWithEllipsis(disp, tx, y, cellWidth, justLeft);

			if (enc)                                 /* CryptoPad key, right-aligned */
				CardDrawIcon(bmpMainKeyBitmap, bounds->extent.x - right, y);
		}
	}
}

/* Reload the card list and repaint the table (after a directory change). */
static void MainFormCardRefresh(void)
{
	TablePtr tableP = GetActiveFormObjectPtr(tblMainRecordList);
	MainFormLoadCardDir();
	MainFormLoadTable();
	TblEraseTable(tableP);
	TblDrawTable(tableP);
}

/* Deliberately NOT AES_SECTION (plain main segment) -- a one-line relay to
 * CryptoImportFromCardBuffer (sec001, CryptoSec35.c). MainFormOpenCardFile
 * (below) is itself sec001, and calling CryptoImportFromCardBuffer
 * directly from there is a same-segment cross-OBJECT-FILE call that hit a
 * link-time "relocation truncated to fit: DISP16" once these two files'
 * sec001 contributions ended up too far apart -- a genuine cross-SEGMENT
 * call (main<->sec001, in either direction) uses a different, distance-
 * unlimited mechanism, proven reliable everywhere else in this app, so
 * relaying through this tiny main-segment hop sidesteps the whole
 * same-segment displacement question. */
static void MainFormImportLargeCardFile(MemHandle plainH, UInt32 plainLen, const char *baseName, UInt16 category)
{
	CryptoImportFromCardBuffer(plainH, plainLen, baseName, category);
}

/* Same relay reasoning as MainFormImportLargeCardFile above, for the
 * genuine-streaming path (CryptoImportFromCardFile) used by a large
 * PLAIN card file instead of the whole-buffer one. */
static void MainFormImportLargeCardFileStreaming(UInt16 volRef, const char *path, const char *baseName, UInt16 category)
{
	CryptoImportFromCardFile(volRef, path, baseName, category);
}

/* Same relay reasoning, for the streaming ENCRYPTED case. */
static void MainFormImportLargeEncryptedCardFileStreaming(UInt16 volRef, const char *path, const char *password, const char *baseName, UInt16 category)
{
	CryptoImportFromEncryptedCardFile(volRef, path, password, baseName, category);
}

/* Open a card note (leaf name in the current dir) in the shared Edit form via
 * the card-file bridge. The editor reads/writes it through CardIO.
 * In sec001 (foreground-only) to keep the main code segment under 32 KB. */
static void MainFormOpenCardFile(const char *leaf) AES_SECTION;
static void MainFormOpenCardFile(const char *leaf)
{
	char      full[cardMaxPath];
	MemHandle fileH = NULL;
	UInt32    fileSize = 0;
	UInt8     hdr[cardCryptHdrLen];
	UInt32    hdrTotalSize = 0;
	Boolean   enc;
	char      pw[cardMaxPassword];
	MemHandle plainH;
	UInt32    plainLen;

	pw[0] = 0;
	CardJoinPath(full, gCardPath, leaf);

	/* Peek just the header (small, bounded) to learn the file's total size
	 * and whether it's encrypted, WITHOUT reading a possibly-large whole
	 * file into one buffer first -- CardReadFile's single MemHandleNew for
	 * the WHOLE file was failing outright (confirmed: error 258 =
	 * memErrNotEnoughSpace) for a ~98KB file, even though a 33KB read
	 * succeeds fine. */
	{
	Err err = CardReadFileHeader(gCardVol, full, hdr, sizeof(hdr), &hdrTotalSize);
	if (err != errNone) {
		Char errStr[8];
		StrIToA(errStr, (Int32) err);
		FrmCustomAlert(altCardOpenFailed, errStr, "", "");
		return;
	}
	}
	enc = CardCryptIsEncrypted(hdr, (hdrTotalSize < sizeof(hdr)) ? hdrTotalSize : sizeof(hdr));

	/* Bare note name (".enc" then ".txt" stripped) -- needed either way: as
	 * the editor title below, or as the Import title prefix if this turns
	 * out to be a too-large-to-edit PLAIN file (the streaming-import
	 * branch right below returns without touching the rest of this
	 * function at all). */
	StrNCopy(gCardEditName, leaf, cardMaxName - 1);
	gCardEditName[cardMaxName - 1] = 0;
	{
		UInt16 n = StrLen(gCardEditName);
		if (n >= 4 && StrNCompare(gCardEditName + n - 4, ".enc", 4) == 0)
			{ gCardEditName[n - 4] = 0; n -= 4; }
		if (n >= 4 && StrNCompare(gCardEditName + n - 4, ".txt", 4) == 0)
			gCardEditName[n - 4] = 0;
	}

	/* A file too large for the edit field (~65000-char ceiling) is
	 * imported via genuine VFS streaming instead -- reads and writes ONE
	 * PART (<=60000 bytes) at a time, so it works regardless of the
	 * file's total size, whether plain or encrypted. */
	if (!enc && hdrTotalSize > 60000UL) {
		MainFormImportLargeCardFileStreaming(gCardVol, full, gCardEditName, currentCategory);
		return;
	}
	if (enc && hdrTotalSize > 60000UL) {
		/* Phase 5a: Sec35 gate -- don't even prompt for the file's OWN
		 * password if Sec35 itself is locked. */
		if (LockedBounceRelay()) return;
		if (!CardPromptPassword(pw, sizeof(pw))) return;
		MainFormImportLargeEncryptedCardFileStreaming(gCardVol, full, pw, gCardEditName, currentCategory);
		MemSet(pw, sizeof(pw), 0);
		return;
	}

	{
	WinHandle busy = CardBusyShow("Opening...");
	Err err = CardReadFile(gCardVol, full, &fileH, &fileSize);
	CardBusyHide(busy);
	if (err != errNone || !fileH) {
		Char errStr[8];
		StrIToA(errStr, (Int32) err);
		FrmCustomAlert(altCardOpenFailed, errStr, "", "");
		return;
	}
	}

	if (enc) {
		Int16 rc;
		WinHandle busy;
		UInt8 *data;
		/* Phase 5a: Sec35 gate -- don't even prompt for the file's OWN
		 * password if Sec35 itself is locked. */
		if (LockedBounceRelay()) { MemHandleFree(fileH); return; }
		if (!CardPromptPassword(pw, sizeof(pw))) { MemHandleFree(fileH); return; }
		data = MemHandleLock(fileH);
		busy = CardBusyShow("Decrypting...");
		rc = CardCryptDecrypt(data, fileSize, pw, &plainH, &plainLen);
		CardBusyHide(busy);
		MemHandleUnlock(fileH);
		MemHandleFree(fileH);
		if (rc != cardCryptOK) {
			MemSet(pw, sizeof(pw), 0);
			FrmAlert(altCardBadPassword);
			return;
		}
	} else {
		plainH = fileH;         /* takes ownership of the handle either way */
		plainLen = fileSize;
	}

	/* Still needed for the ENCRYPTED case -- the header peek only knew the
	 * CIPHERTEXT size, not the decrypted plaintext size. A large plain
	 * file never reaches this point at all (handled by the streaming
	 * branch above). */
	if (plainLen > 60000UL) {
		MainFormImportLargeCardFile(plainH, plainLen, gCardEditName, currentCategory);
		MemSet(pw, sizeof(pw), 0);
		return;
	}

	gCardPreloadH = plainH;         /* editor takes ownership of the handle */
	gCardPreloadLen = plainLen;
	StrCopy(gCardEditPath, full);
	if (enc) {
		/* base path is the file minus its ".enc" suffix */
		UInt16 n = StrLen(gCardEditPath);
		if (n > 4 && StrCompare(gCardEditPath + n - 4, ".enc") == 0)
			gCardEditPath[n - 4] = 0;
	}

	gCardEditVol = gCardVol;
	gCardEncrypted = enc;
	gCardWasEncrypted = enc;      /* originally-encrypted -> ask on the way out */
	gCardDeletePlain = false;
	StrCopy(gCardPassword, pw);
	MemSet(pw, sizeof(pw), 0);
	gCardFileOpen = true;
	FrmGotoForm(frmEditForm);
}

/* "New" while browsing the card: prompt for a name, reject an empty name, create
 * the (empty) ".txt" file, then open it in the editor.
 *
 * Drives its own minimal modal event loop instead of FrmDoDialog -- see
 * CardPromptPassword (CardCrypt.c) for why: neither setting focus before
 * FrmDoDialog, nor a FrmSetEventHandler-attached frmOpenEvent handler,
 * reliably worked, so this avoids depending on FrmDoDialog's internal
 * event dispatch at all. */
static void MainFormNewCardNote(void) AES_SECTION;
static void MainFormNewCardNote(void)
{
	FormPtr   frmP;
	FormPtr   prevFrmP;
	FieldPtr  fldP;
	UInt16    fieldIndex;
	UInt16    btn = 0;
	EventType event;
	Boolean   done = false;
	char      name[cardMaxName];
	char      leaf[cardMaxName];
	char      full[cardMaxPath];

	name[0] = 0;
	prevFrmP = FrmGetActiveForm();
	frmP = FrmInitForm(frmCardNewNote);
	fieldIndex = FrmGetObjectIndex(frmP, fldCardNewName);
	fldP = FrmGetObjectPtr(frmP, fieldIndex);

	FrmSetActiveForm(frmP);
	FrmDrawForm(frmP);
	FrmSetFocus(frmP, fieldIndex);
	FldGrabFocus(fldP);

	while (!done) {
		EvtGetEvent(&event, evtWaitForever);
		if (event.eType == appStopEvent) {
			EvtAddEventToQueue(&event);
			btn = btnCardNewCancel;
			break;
		}
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, NULL)) continue;

		if (event.eType == ctlSelectEvent) {
			if (event.data.ctlSelect.controlID == btnCardNewOK) {
				btn = btnCardNewOK;
				done = true;
			} else if (event.data.ctlSelect.controlID == btnCardNewCancel) {
				btn = btnCardNewCancel;
				done = true;
			}
		}

		FrmHandleEvent(frmP, &event);
	}

	if (btn == btnCardNewOK) {
		Char *txt = FldGetTextPtr(fldP);
		if (txt) { StrNCopy(name, txt, cardMaxName - 1); name[cardMaxName - 1] = 0; }
	}

	/* See CardPromptPassword (CardCrypt.c) for why FrmEraseForm is needed
	 * here before FrmDeleteForm -- frmCardNewNote is also FRAME MODAL and
	 * smaller than the screen, so skipping it leaks the auto-saved bits
	 * behind the dialog and leaves visible corruption in that rect. */
	FrmEraseForm(frmP);
	FrmDeleteForm(frmP);

	/* See CardPromptPassword (CardCrypt.c) for why this is needed --
	 * several exit paths below (bad/no name, file already exists) stay on
	 * the SAME form (frmMainForm's card browser) without navigating
	 * anywhere, which would otherwise leave it unresponsive. */
	if (prevFrmP) {
		FrmSetActiveForm(prevFrmP);
		FrmDrawForm(prevFrmP);
	}

	if (btn != btnCardNewOK) return;

	if (name[0] == 0) { FrmAlert(altCardNewNoName); return; }

	StrCopy(leaf, name);
	StrCat(leaf, ".txt");
	CardJoinPath(full, gCardPath, leaf);

	if (CardExists(gCardVol, full)) { FrmAlert(altCardNewExists); return; }

	if (CardWriteFile(gCardVol, full, "", 0) != errNone) return;
	MainFormOpenCardFile(leaf);
}

/* Encrypt every plaintext note in the current directory with one password. Warns
 * first (all lost if the password is forgotten). Per-file: reads, encrypts to
 * name.txt.enc, deletes the plaintext. */
static void MainFormCardEncryptAll(void) AES_SECTION;
static void MainFormCardEncryptAll(void)
{
	char pw[cardMaxPassword];
	UInt16 i;
	WinHandle busy;

	if (FrmAlert(altCardEncAll) != 0) return;       /* 0 = "Encrypt All" */
	if (LockedBounceRelay()) return;           /* Phase 5a: Sec35 gate */
	if (!CardPromptPassword(pw, sizeof(pw))) return;

	busy = CardBusyShow("Encrypting...");
	for (i = 0; i < gCardCount; i++) {
		CardEntry *e = &gCardEnt[i];
		UInt16 nl = StrLen(e->name);
		Boolean isEnc = (nl >= 4 && StrNCompare(e->name + nl - 4, ".enc", 4) == 0);
		char src[cardMaxPath], dst[cardMaxPath];
		MemHandle fh; UInt32 fsz;

		if (e->isDir || isEnc) continue;            /* plaintext notes only */
		CardJoinPath(src, gCardPath, e->name);
		if (CardReadFile(gCardVol, src, &fh, &fsz) != errNone || !fh) continue;
		{
			MemHandle blobH; UInt32 blobSize;
			void *data = MemHandleLock(fh);
			Err ce = CardCryptEncrypt(data, fsz, pw, &blobH, &blobSize);
			MemHandleUnlock(fh);
			MemHandleFree(fh);
			if (ce == errNone) {
				void *blob = MemHandleLock(blobH);
				StrCopy(dst, src); StrCat(dst, ".enc");
				CardWriteFile(gCardVol, dst, blob, blobSize);
				MemHandleUnlock(blobH);
				MemHandleFree(blobH);
				CardDeleteFile(gCardVol, src);
			}
		}
	}
	CardBusyHide(busy);
	MemSet(pw, sizeof(pw), 0);
	MainFormCardRefresh();
}

/* Try to decrypt every .enc note in the current directory with one password.
 * Files that decrypt are stored decrypted (name.txt, .enc removed); files with a
 * different password are left untouched. */
static void MainFormCardDecryptAll(void) AES_SECTION;
static void MainFormCardDecryptAll(void)
{
	char pw[cardMaxPassword];
	UInt16 i;
	WinHandle busy;

	if (LockedBounceRelay()) return;           /* Phase 5a: Sec35 gate */
	if (!CardPromptPassword(pw, sizeof(pw))) return;

	busy = CardBusyShow("Decrypting...");
	for (i = 0; i < gCardCount; i++) {
		CardEntry *e = &gCardEnt[i];
		UInt16 nl = StrLen(e->name);
		Boolean isEnc = (nl >= 4 && StrNCompare(e->name + nl - 4, ".enc", 4) == 0);
		char src[cardMaxPath], dst[cardMaxPath];
		MemHandle fh; UInt32 fsz;

		if (e->isDir || !isEnc) continue;           /* encrypted notes only */
		CardJoinPath(src, gCardPath, e->name);
		if (CardReadFile(gCardVol, src, &fh, &fsz) != errNone || !fh) continue;
		{
			MemHandle plainH; UInt32 plainLen; Int16 rc;
			void *data = MemHandleLock(fh);
			rc = CardCryptDecrypt(data, fsz, pw, &plainH, &plainLen);
			MemHandleUnlock(fh);
			MemHandleFree(fh);
			if (rc == cardCryptOK) {
				void *plain = MemHandleLock(plainH);
				UInt16 dl;
				StrCopy(dst, src);
				dl = StrLen(dst);
				if (dl >= 4) dst[dl - 4] = 0;       /* strip ".enc" */
				CardWriteFile(gCardVol, dst, plain, plainLen);
				MemHandleUnlock(plainH);
				MemHandleFree(plainH);
				CardDeleteFile(gCardVol, src);
			}
			/* rc != OK -> wrong password / not ours: leave the file untouched */
		}
	}
	CardBusyHide(busy);
	MemSet(pw, sizeof(pw), 0);
	MainFormCardRefresh();
}

/* A card row was tapped (di = display index): [ .. ] ascends, a folder descends,
 * a file opens in the editor. */
static void MainFormEnterCardItem(UInt16 di)
{
	Boolean isParent;
	CardEntry *e = CardEntryAt(di, &isParent);

	if (isParent) {                                  /* [ .. ] -> ascend */
		CardParentPath(gCardPath);
		if (StrLen(gCardPath) < StrLen(cardNotesDir)) /* never above /Notes */
			StrCopy(gCardPath, cardNotesDir);
		MainFormCardRefresh();
		return;
	}
	if (e == NULL) return;
	if (e->isDir) {                                  /* [Folder] -> descend */
		char np[cardMaxPath];
		CardJoinPath(np, gCardPath, e->name);
		StrCopy(gCardPath, np);
		MainFormCardRefresh();
	} else {                                         /* file -> open in editor */
		MainFormOpenCardFile(e->name);
	}
}


static void MainFormSelectTableItem (Boolean, TablePtr, Int16, Int16, RectangleType *r);
static WinHandle MainFormInvertMoveIndicator(RectanglePtr, WinHandle, Boolean);

/* Write the current file order (gCardEnt files, in order) to ".cnorder". */
static void MainFormCardWriteOrder(void) AES_SECTION;
static void MainFormCardWriteOrder(void)
{
	UInt32 len = 0;
	UInt16 i;
	MemHandle h;
	char path[cardMaxPath];

	for (i = 0; i < gCardCount; i++)
		if (!gCardEnt[i].isDir) len += StrLen(gCardEnt[i].name) + 1;
	if (len == 0) return;

	h = MemHandleNew(len);
	if (!h) return;
	{
		char *p = MemHandleLock(h);
		UInt32 off = 0;
		for (i = 0; i < gCardCount; i++) {
			if (gCardEnt[i].isDir) continue;
			{
				UInt16 nl = StrLen(gCardEnt[i].name);
				MemMove(p + off, gCardEnt[i].name, nl); off += nl;
				p[off++] = '\n';
			}
		}
		MemHandleUnlock(h);
	}
	CardJoinPath(path, gCardPath, cardOrderFile);
	{
		void *d = MemHandleLock(h);
		CardWriteFile(gCardVol, path, d, len);
		MemHandleUnlock(h);
	}
	MemHandleFree(h);
}

/* Move file gCardEnt[grabbedEi] to the file position implied by drop di, persist
 * to ".cnorder", refresh. */
static void MainFormCardReorderFile(UInt16 grabbedEi, UInt16 dropDi) AES_SECTION;
static void MainFormCardReorderFile(UInt16 grabbedEi, UInt16 dropDi)
{
	UInt16 numFolders = 0, i;
	Int16  tei;
	CardEntry tmp;

	for (i = 0; i < gCardCount; i++)
		if (gCardEnt[i].isDir) numFolders++;
	if (gCardCount - numFolders < 2) return;

	tei = (Int16) dropDi - (gCardHasParent ? 1 : 0);
	if (tei < (Int16) numFolders) tei = numFolders;
	if (tei >= (Int16) gCardCount) tei = gCardCount - 1;
	if (tei == (Int16) grabbedEi) return;

	tmp = gCardEnt[grabbedEi];
	if (tei < (Int16) grabbedEi)
		for (i = grabbedEi; i > (UInt16) tei; i--) gCardEnt[i] = gCardEnt[i - 1];
	else
		for (i = grabbedEi; i < (UInt16) tei; i++) gCardEnt[i] = gCardEnt[i + 1];
	gCardEnt[tei] = tmp;

	MainFormCardWriteOrder();
	MainFormCardRefresh();
}

/* Card list pen handler: tap opens; in manual mode a drag on a file reorders it
 * (persisted via ".cnorder"). Folders / [ .. ] / alphabetic mode just open. */
static void MainFormCardDrag(EventPtr eventP) AES_SECTION;
static void MainFormCardDrag(EventPtr eventP)
{
	TablePtr tableP = eventP->data.tblSelect.pTable;
	Int16    startRow = eventP->data.tblSelect.row;
	Int16    column   = eventP->data.tblSelect.column;
	UInt16   di = TblGetRowID(tableP, startRow);
	UInt16   ei;
	Int16    x, y, r, numRows, dropRow = startRow;
	Boolean  penDown = true;
	RectangleType selRec, rb, indRec;
	WinHandle indicator = 0;

	/* Drag-reorder needs stable positions in the REAL (unfiltered) list -
	 * a filtered view's on-screen order doesn't map onto ".cnorder" cleanly,
	 * so treat every row as a plain tap while a filter is active. */
	if (currentSortOrder == soAlphabetic || gCardFindActive || (gCardHasParent && di == 0)) {
		MainFormEnterCardItem(di); return;
	}
	ei = di - (gCardHasParent ? 1 : 0);
	if (gCardEnt[ei].isDir) { MainFormEnterCardItem(di); return; }

	MainFormSelectTableItem(true, tableP, startRow, column, &selRec);
	numRows = TblGetNumberOfRows(tableP);

	/* Trace the pen; show a move-indicator line at the row under it (like the DB
	 * drag), and remember that row as the drop target. */
	while (true) {
		Int16 row = dropRow;
		PenGetPoint(&x, &y, &penDown);
		if (!penDown) break;
		for (r = 0; r < numRows; r++) {          /* last usable row whose top <= y */
			if (!TblRowUsable(tableP, r)) continue;
			TblGetItemBounds(tableP, r, column, &rb);
			if (y >= rb.topLeft.y) row = r;
		}
		if (row != dropRow) {
			if (indicator) indicator = MainFormInvertMoveIndicator(&indRec, indicator, false);
			dropRow = row;
			if (dropRow != startRow) {
				TblGetItemBounds(tableP, dropRow, column, &indRec);
				indicator = MainFormInvertMoveIndicator(&indRec, 0, true);
			}
		}
	}
	if (indicator) MainFormInvertMoveIndicator(&indRec, indicator, false);

	if (dropRow == startRow) {
		MainFormSelectTableItem(false, tableP, startRow, column, &selRec);
		MainFormEnterCardItem(di);
	} else {
		MainFormCardReorderFile(ei, gCardTop + dropRow);
	}
}

static Boolean MainFormSeekRecord (UInt16* , Int16, Int16);
static WinHandle MainFormInvertMoveIndicator(RectanglePtr, WinHandle, Boolean);
static void MainFormDragMemo(EventPtr);
static void MainFormSelectTableItem (Boolean, TablePtr, Int16, Int16, RectangleType *r);
static void MainFormEnterTableItem(void);
static Boolean TableFindRowOfSelectedRecord(TablePtr, UInt16, UInt16*);
static void DrawInversionEffect (const RectangleType *rP, UInt16);

static void MainFormInit(void);

static void MainFormDrawTableItem(void *, Int16, Int16, RectanglePtr);
static void DrawCryptoPadRecordText(UInt16, UInt16, RectanglePtr);
static void DrawMemoPadRecordText(UInt16, UInt16, RectanglePtr);
static void MainFormDisplayMask (RectanglePtr);
static Int16 MainFormNumberOfRows (TablePtr);

static void MainFormLoadTable(void);
static void MainFormLoadRecords(void);
static void MainFormShowFullDisplay(void);
static MemHandle MainFormGetNextRecord(Int16*);
static MemHandle MainFormGetPreviousRecord(Int16*);

static void EncryptCategory();
static void DecryptCategory();
static ProgressInfoPtr ProgressFormInitAndDraw (FormPtr frmP);
static Boolean ProgressCallback (void* progressInfoP, ProgrType* progressP, Char* messageP);
static void ProgressUpdateGauge (RectangleType*	gaugeRP, UInt32 gaugeLevel);
static UInt32 ProgressHandleEvent (Int32 timeout);

static void MainFormSelectCategory(void);
static UInt16 MainFormGetNextCategory(UInt16);
static void MainFormNextCategory(void);

static void MainFormUpdateScrollBar(void);
static Boolean MainFormUpdateDisplay(UInt16);
static void MainFormScrollLines(Int16);
static void MainFormScrollPage(WinDirectionType);

static Boolean MainFormSeekRecord (UInt16* , Int16, Int16);
static UInt16 MainFormGotFirstCryptoPadRow(TablePtr);
static void MainFormDragMemo(EventPtr);

static void MainFormHandleMenuEvent(UInt16);

static UInt16 recordDB;

/***************************************
 * MainFormSeekRecord:
 * utility routine for MainFormDragMemo
 ***************************************
 */

static Boolean MainFormSeekRecord (UInt16* indexP, Int16 offset, Int16 direction)
{
	DmSeekRecordInCategory (selectedDBRef, indexP, offset, 
		direction, currentCategory);

	if (DmGetLastErr()) return (false);
	
	return (true);
}


/***************************************
 * MainFormGotFirstCryptoPadRow:
 * Search the number of the first
 * row with CryptoPad memos.
 ***************************************
 */

static UInt16 MainFormGotFirstCryptoPadRow(TablePtr tableP)
{
UInt16 nRow;
UInt16 numRows;

	numRows = TblGetNumberOfRows(tableP);
	
	for (nRow = 0; nRow < numRows; nRow++) {
		if (TblGetItemInt(tableP, nRow, kRecordColumn) == cryptoPadDB)
			return (nRow);
	}

	return (- 1);
}

/***************************************
 * MainFormInvertMoveIndicator:
 * Saves the screen behind the move
 * indicator while dragging a record.
 ***************************************
 */

static WinHandle MainFormInvertMoveIndicator(RectanglePtr itemR,
			WinHandle savedBits, Boolean draw)
{
	WinHandle winH = 0;
	RectangleType indictatorR;

	indictatorR.topLeft.x = itemR->topLeft.x;
	indictatorR.topLeft.y = itemR->topLeft.y + itemR->extent.y - 2;
	indictatorR.extent.x = itemR->extent.x;
	indictatorR.extent.y = 2;
				
	if (draw) {
		UInt16 i;
		UInt16 err;
		CustomPatternType pattern;
		CustomPatternType savedPattern;

		WinGetPattern ((CustomPatternType *) &savedPattern);

		for (i = 0; i < 8; i++) {
			pattern[i]= 0xAA;
			i++;
			pattern[i]= 0x55;
		}
	
		WinSetPattern ((const CustomPatternType *) &pattern);
	
		winH = WinSaveBits (&indictatorR, &err);
	
		WinFillRectangle (&indictatorR, 0);
	
		WinSetPattern ((const CustomPatternType *) &savedPattern);
	
	} else WinRestoreBits (savedBits, indictatorR.topLeft.x, indictatorR.topLeft.y);
	
	return (winH);
}

/**********************************************************
 * MainFormSelectTableItem
 * This routine either selects or unselects the specified
 * table item.				
 * While a table item is "on the move," having it be selected
 * can cause the Table code grief.  So, instead of using
 * the TblSelectItem()/TblUnhighlightSelect() calls, we now
 * manually select/unselect the table's row.  Before color,
 * only WinInvertRectangle() was called, so this is now in line
 * again with the way things used to work.  Sigh.
 **********************************************************/

static void MainFormSelectTableItem (Boolean selected, TablePtr table, Int16 row, Int16 column, RectangleType *r)
{
	// Get the item's rectangle.
	TblGetItemBounds (table, row, column, r);
	
	if (OSVersion < OS35)
		WinInvertRectangle (r, 0);
	else {

		// Set up the drawing state the way we want it.
		WinPushDrawState();
		WinSetBackColor(UIColorGetTableEntryIndex(UIFieldBackground));
		WinSetForeColor(UIColorGetTableEntryIndex(UIObjectForeground));
		WinSetTextColor(UIColorGetTableEntryIndex(UIObjectForeground));

		// Erase and (re)draw the item.
		WinEraseRectangle(r, 0);
		MainFormDrawTableItem(table, row, column, r);
	
		// If selected, make it look that way.
		if (selected)
			DrawInversionEffect(r, 0);
		
		// Restore the previous drawing state.
		WinPopDrawState();
	}
}

/**********************************************************
 * DrawInversionEffect
 * This routine does an inversion effect by swapping colors
 * this is NOT undoable by calling it a second time, rather
 * it just applies a selected look on top of already
 * rendered data.  (It's kind of a hack.)
 **********************************************************/
static void DrawInversionEffect (const RectangleType *rP, UInt16 cornerDiam) 
{
	WinPushDrawState();
	WinSetDrawMode(winSwap);
	WinSetPatternType (blackPattern);
	WinSetBackColor(UIColorGetTableEntryIndex(UIFieldBackground));
	WinSetForeColor(UIColorGetTableEntryIndex(UIObjectSelectedFill));
	WinPaintRectangle(rP, cornerDiam);
	
	if (UIColorGetTableEntryIndex(UIObjectSelectedFill) != UIColorGetTableEntryIndex(UIObjectForeground)) {
		WinSetBackColor(UIColorGetTableEntryIndex(UIObjectForeground));
		WinSetForeColor(UIColorGetTableEntryIndex(UIObjectSelectedForeground));
		WinPaintRectangle(rP, cornerDiam);
	}
	WinPopDrawState();
}

/*********************************
 * TableFindRowOfSelectedRecord:
 * Find the row which corresponds
 * to the selected record.
 *********************************/
static Boolean TableFindRowOfSelectedRecord(TablePtr tableP, 
			UInt16 selectedRecord, UInt16* selectedRow)

{
	UInt16 row;

	for (row = 0; row < TblGetNumberOfRows (tableP); row++) {
		if ((TblGetItemInt(tableP, row, kRecordColumn) == selectedDB) 
				&& TblRowUsable (tableP, row)) {
			if (TblGetRowID(tableP, row) == selectedRecord) {
				*selectedRow = row;
				return (true);
			}
		}
	}
	return (false);
}

/***************************************
 * MainFormDragMemo:
 * Performs the drag and drop operation
 * implementing Manual sorting.
 ***************************************
 */

static void MainFormDragMemo(EventPtr eventP)
{
	Int16 sortOrder;
	UInt16 selectedRow;
	UInt16 recordNum;
	UInt16 selectedRecord;
	UInt16 recAttributes;

	Int16 x, y;
	Boolean penDown = true;
	Boolean moving = false;
	Boolean selected = true;

	TablePtr tableP;
	Int16 row;
	UInt16 column;

	WinHandle savedBits;
	RectangleType rec;

	sortOrder = currentSortOrder;

	tableP = eventP->data.tblSelect.pTable;
	row = eventP->data.tblSelect.row;
	column = eventP->data.tblSelect.column;

	if (gCardMode) { MainFormCardDrag(eventP); return; }

	/* Drag-reorder needs stable neighbor positions in the REAL (unfiltered)
	 * list - treat every row as a plain tap (the soAlphabetic branch below)
	 * while a filter is active, same precedent as the card browser. */
	if (gDbFindActive) sortOrder = soAlphabetic;

	/* Highlight the selected record */

	selectedRecord = TblGetRowID(tableP, row);
	selectedDB = TblGetItemInt(tableP, row, kRecordColumn);

	MainFormSelectTableItem(selected, tableP, row, column, &rec);

	/* Trace the pen until it moves enough to constitute
	 * a move operation or until the pen it released.
	 */

	while (true) {

		PenGetPoint (&x, &y, &penDown);
		if (!penDown) break;
		
		if (!moving) {

			if (sortOrder != soAlphabetic) {

				/* Is the pen still within the bounds of the selected item?
				 * if not, draw the move indicator.
				 */

				if (!RctPtInRectangle (x, y, &rec)) {
					moving = true;
					TblGetItemBounds (tableP, row, column, &rec);
					savedBits = MainFormInvertMoveIndicator (&rec, 0, true);
				}

			} else selected = RctPtInRectangle(x, y, &rec);

		} else if (!RctPtInRectangle (x, y, &rec)) {

			/* Above the first item? */
			if (row < 0) {
				if (y >= rec.topLeft.y) {
					row++;
					MainFormInvertMoveIndicator(&rec, savedBits, false);
					rec.topLeft.y -= rec.extent.y;
					savedBits = MainFormInvertMoveIndicator (&rec, 0, true);
				}					
			}

			/* Move up */
			else if (y < rec.topLeft.y) {
				recordNum = TblGetRowID(tableP, row);
				recordDB = TblGetItemInt(tableP, row, kRecordColumn);

				if (MainFormSeekRecord (&recordNum, 1, dmSeekBackward) 
				&& (recordDB == selectedDB)) 
					{
					MainFormInvertMoveIndicator(&rec, savedBits, false);
					if (row) row--;
					else {				
						MainFormScrollLines(-1);
						if (TableFindRowOfSelectedRecord(tableP, selectedRecord, &selectedRow))
							MainFormSelectTableItem(selected, tableP, selectedRow, column, &rec);
					} 
					TblGetItemBounds (tableP, row, column, &rec);
					savedBits = MainFormInvertMoveIndicator (&rec, 0, true);

				} else if (((row == 0) && (recordDB == selectedDB)) 
					|| (MainFormGotFirstCryptoPadRow(tableP) == row)) {
					row--;
					MainFormInvertMoveIndicator (&rec, savedBits, false);
					rec.topLeft.y -= rec.extent.y;
					savedBits = MainFormInvertMoveIndicator (&rec, 0, true);
				}
			}

			/* Move down */
			else {
				recordNum = TblGetRowID(tableP, row);
				recordDB = TblGetItemInt(tableP, row, kRecordColumn);

				if (MainFormSeekRecord(&recordNum, 1, dmSeekForward)
				|| ((recordDB == memoPadDB) && (selectedDB == cryptoPadDB))) 
					{
					MainFormInvertMoveIndicator (&rec, savedBits, false);				
					if (row < TblGetLastUsableRow (tableP)) row++;
					else {
						MainFormScrollLines(1);
						if (TableFindRowOfSelectedRecord(tableP, selectedRecord, &selectedRow))
							MainFormSelectTableItem(selected, tableP, selectedRow, column, &rec);
					}

					TblGetItemBounds (tableP, row, column, &rec);
					savedBits = MainFormInvertMoveIndicator (&rec, 0, true);
				}
			}
		}
	}

	/* Turn off the move indicator, if it is on */

	if (moving) savedBits = MainFormInvertMoveIndicator (&rec, savedBits, false);

	/* If the highlighted item is visible, unhighlight it */
		if (TableFindRowOfSelectedRecord(tableP, selectedRecord, &selectedRow))
			MainFormSelectTableItem(false, tableP, selectedRow, column, &rec);

	if (moving) {
		if (row >= 0) {
			recordNum = TblGetRowID (tableP, row);
			recordDB = TblGetItemInt(tableP, row, kRecordColumn);

			if ((selectedRecord == recordNum) && (selectedDB == recordDB))
				return;
			
			recordNum++;

		} else recordNum = TblGetRowID(tableP, 0);

		if (recordDB != selectedDB) {
			if (selectedDB == cryptoPadDB) recordNum = 0;
			else recordNum = DmNumRecordsInCategory (dbMemoPadRef, currentCategory);
		}

		DmMoveRecord (selectedDBRef, selectedRecord, recordNum);

		MainFormLoadRecords();
		TblRedrawTable (tableP);
	}
		
	/* If we didn't move the item then it's been selected for editing,
	 * go to the Edit field.
	 */

	else if (sortOrder != soAlphabetic || selected) {
		currentRecord = TblGetRowID(tableP, row);
		currentDB = TblGetItemInt(tableP, row, kRecordColumn);

		editScrollPosition = 0;

		// Get the category and secret attribute of the current record.
		DmRecordInfo (currentDBRef, currentRecord, &recAttributes, NULL, NULL);

		// If this is a "private" record, then determine what is to be shown.
		if (recAttributes & dmRecAttrSecret) {
			switch (PrivateRecordVisualStatus) {
				case showPrivateRecords:
					MainFormEnterTableItem();
					break;
					
				case maskPrivateRecords:
					if (SecVerifyPW (showPrivateRecords) == true) {
						PrivateRecordVisualStatus = showPrivateRecords;
						MainFormEnterTableItem();
					}
					break;
			}
		} else MainFormEnterTableItem();
	}
}

/******************************************
 * MainFormEnterTableItem:
 * A item in the table of the main form
 * has been selected.
 ******************************************
 */

static void MainFormEnterTableItem(void)

{
	if (currentDB == cryptoPadDB)  {
		/* Db35-suite-style Sec35 lock gate: a Sec35-wrapped+locked record
		 * shows "******" in the list (via the chokepoint's masking) and
		 * isEncrypted() reads as false for it (can't tell if it ALSO has a
		 * layer-2 password without decrypting first) -- without this check
		 * up front, tapping it would just silently open the editor showing
		 * "******" instead of prompting to unlock. Bounce before any of
		 * the existing layer-2 password-prompt logic runs. */
		if (CryptoLockedBounce (dbCryptoPadRef, currentRecord))
			return;

		/* A record already too close to the size where a real
		 * "DmWriteCheck failed" fatal error was confirmed on a real device
		 * is still always safe to OPEN and VIEW -- EditRetrieveData (in
		 * EditForm.c) makes the field read-only for such a record and
		 * warns only if the user actually attempts to type into it. */

		/* CryptoGetRecordFlag instead of isEncrypted(): the latter does a
		 * FULL record decrypt just for this one flag, which for a large
		 * wrapped memo meant a real invisible hang here (no busy dialog)
		 * before the editor's own "Opening..." even appeared -- decrypting
		 * the same record's body twice in a row for one tap. This peeks at
		 * most a 54-byte header. */
		if (CryptoGetRecordFlag(dbCryptoPadRef, currentRecord)) {
			EventType event;

			if ((prefsGlobalPasswordP != 0) && (globalPasswordP == 0)) {
				event.eType = globalPasswordStatusEvent;
				event.data.generic.datum[1] = queryGlobalPassword;
				event.data.generic.datum[2] = recEncrypted;

				FrmPopupForm(frmQueryGlobalPasswordForm);
				EvtAddEventToQueue(&event);
				return;
			}

		/* The record is encrypted.
		 * Pops the password form up.
		 */

			event.eType = memoStatusEvent;
			event.data.generic.datum[1] = recEncrypted;

			FrmPopupForm(frmPasswordForm);
			EvtAddEventToQueue(&event);
		} else FrmGotoForm(frmEditForm);

	} else FrmGotoForm(frmEditForm);
}

/******************************************
 * MainFormInit:
 * Initialize the main form and sets up the
 * table drawing callback functions.
 ******************************************
 */

/* fldMainFind is a fresh object on every frmMainForm (re)load - restore its
 * text so an active filter stays visible after returning from a viewed
 * file/memo, instead of silently reading as cleared. */
static void MainRestoreFindField(const char *term) AES_SECTION;
static void MainRestoreFindField(const char *term)
{
	FieldPtr fld;
	MemHandle h;
	char *p;

	if (!term || !term[0]) return;
	fld = (FieldPtr) GetActiveFormObjectPtr(fldMainFind);
	h = MemHandleNew((UInt32) (StrLen(term) + 1));
	if (!fld || !h) return;
	p = (char *) MemHandleLock(h);
	StrCopy(p, term);
	MemHandleUnlock(h);
	FldSetTextHandle(fld, h);
}

static void MainFormInit(void)
{
	ControlPtr triggerP;
	UInt16 category;

	triggerP = GetActiveFormObjectPtr(popMainCategory);

	/* Returning to the list while browsing the card (e.g. after editing a file):
	 * re-read the directory so new/changed files show, and keep the Card label.
	 * Skipped when gCardDirValid is still true (see its own comment) - a plain
	 * view-and-return round-trip doesn't need to pay for a real SD-card scan. */
	if (gCardMode) {
		TablePtr tableP = GetActiveFormObjectPtr(tblMainRecordList);
		if (!gCardDirValid) MainFormLoadCardDir();
		MainFormLoadTable();
		TblSetCustomDrawProcedure(tableP, kRecordColumn, MainFormDrawTableItem);
		TblSetColumnUsable(tableP, kRecordColumn, true);
		CategorySetTriggerLabel(triggerP, "Card");
		if (gCardFindActive) MainRestoreFindField(gCardFindTerm);
		currentForm = frmMainForm;
		return;
	}

	if (showAllCategories)
		category = dmAllCategories;
	else
		category = currentCategory;

	/* Loads the record in the table */
	MainFormLoadRecords();

	/* Updates the currently selected category */
	CategoryGetName(dbMemoPadRef, category, categoryName);
	CategorySetTriggerLabel(triggerP, categoryName);

	if (gDbFindActive) MainRestoreFindField(gDbFindTerm);

	currentForm = frmMainForm;
}

/****************************************
 * DrawStringWithEllipsis:
 * This routine draws the title of the specified memo. 
 ****************************************
 */

void DrawStringWithEllipsis (Char * textP, Int16 x, 
			Int16 y, Int16 width, JustificationEnum just)
{
// ***************** Code for PalmOS 3.0 to 3.2 *************//
/*	Boolean fits;
	Char ellipsisCharP[2];
	UInt16		strWidth = width;
	UInt16		maxStrWidth = width;
	UInt16		ellipsWidth = 0;
	UInt16 textLength;

	fits = FontCharsInWidth(textP, &strWidth, &textLength);

	// If the string fits, draw it entirely
	if (!fits) {
		MemSet(ellipsisCharP , 2, 0);
		ChrHorizEllipsis (&(ellipsisCharP[0]));

		// Take out the width of an ellipsis char
		ellipsWidth = FntCharWidth ((ellipsisCharP[0]));
		maxStrWidth -= ellipsWidth;

		// Keep removing last char until name fits
		while ((textLength > 0) && (strWidth > maxStrWidth)) {
			textLength--;
			strWidth -= FntCharWidth (textP[textLength]);
		}
	}

	if (just == justCenter)
		x += ((width - strWidth - ellipsWidth) / 2);

	// Draw the string
	WinDrawChars(textP, textLength, x, y);

	// Draw the ellipsis, if necessary
	if (ellipsWidth != 0) {
		x += strWidth;
		WinDrawChars (ellipsisCharP, 1, x, y);
	}
*/
// ***************** End of code for PalmOS 3.0 to 3.2 *************//

	Char * ptr = StrChr (textP, linefeedChr);
	Int16 strWidth;
	
	UInt16 titleLen = (ptr == NULL ? StrLen (textP) : (UInt16) (ptr - textP));
	if (FntWidthToOffset (textP, titleLen, width, NULL, &strWidth) == titleLen) {
		if (just == justCenter)
			x += ((width - strWidth) / 2);
		WinDrawChars (textP, titleLen, x, y);
	} else {
		Int16 ellipsisWidth;

		ellipsisWidth = FntCharWidth (chrEllipsis);
		titleLen = FntWidthToOffset (textP, titleLen, width - ellipsisWidth, NULL, &strWidth);
		if (just == justCenter)
			x += ((width - strWidth - ellipsisWidth) / 2);

		WinDrawChars (textP, titleLen, x, y);
		WinDrawChar (chrEllipsis, x + strWidth, y);
	}
}


/*******************************************
 * DrawMemoPadRecordText:
 * Draw each usable table cell
 * that contains a MemoPad record.
 *******************************************
 */

static void DrawMemoPadRecordText(UInt16 recordNum, UInt16 x, RectanglePtr bounds)
{
	MemHandle handle;
	Char * textP;

	Int16 cellWidth;

	handle = DmGetRecord(dbMemoPadRef, recordNum);
	textP = MemHandleLock(handle);

	// Determine the length of text that will fit within the bounds.
	cellWidth = bounds->extent.x - x;

	DrawStringWithEllipsis (textP, x, bounds->topLeft.y, cellWidth, justLeft);
	
	// Unlock the handle to the record.
	MemHandleUnlock(handle);
	
	// Release the record, not dirty.
	DmReleaseRecord(dbMemoPadRef, recordNum, false);	
}


 /*******************************************
 * DrawCryptoPadRecordText:
 * Draw each usable table cell
 * that contains a CryptoPad record.
 *******************************************
 */

 static void DrawCryptoPadRecordText(UInt16 recordNum, UInt16 x, RectanglePtr bounds)
{
	Char title[81];
	Boolean encrypted;

	UInt16 right;
	Int16 cellWidth;

	/* Adjust spacing with magic values! */
	right = 7;

	/* List-row peek: title text + the layer-2 flag, WITHOUT decrypting a
	 * wrapped record's whole body -- CryptoGetRecord (full decrypt) here
	 * plus isEncrypted()'s own separate full-decrypt call used to cost two
	 * whole-record AES passes PER VISIBLE ROW, which made the list appear
	 * to hang after importing/viewing a large (tens-of-KB) memo. See
	 * CryptoGetRecordTitle's own comment. */
	if (! CryptoGetRecordTitle (dbCryptoPadRef, recordNum, title, sizeof (title), &encrypted))
		return;

	// Determine the length of text that will fit within the bounds.
	cellWidth = bounds->extent.x - x - right;

	DrawStringWithEllipsis (title, x, bounds->topLeft.y, cellWidth, justLeft);


	/* Draw the little icon */

	if (encrypted) {
		BitmapPtr bitmapP;
		MemHandle handle;

		handle = DmGetResource('Tbmp', bmpMainKeyBitmap);
		bitmapP = MemHandleLock(handle);
		WinDrawBitmap(bitmapP, bounds->extent.x - right, bounds->topLeft.y);
		MemHandleUnlock(handle);
		DmReleaseResource(handle);
	}
}
 
 /*******************************************
 * MainFormDrawTableItem:
 * Callback to draw each usable table cell
 *******************************************
 */

static void MainFormDrawTableItem(void * tableP,
				Int16 row, Int16 column, RectanglePtr bounds)
{
	UInt16 recordNum;
	UInt16 recordDB;
	UInt16 posInCategory;

	char posStr [6];
	UInt16 length;
	UInt16 x, y;
	UInt16 category;
	UInt16 recAttributes;

	if (gCardMode) { MainFormDrawCardRow(tableP, row, bounds); return; }

	/* Draw the record number */
	recordNum = TblGetRowID(tableP, row);
	recordDB  = TblGetItemInt(tableP, row, kRecordColumn);

	if (showAllCategories) category = dmAllCategories;
	else category = currentCategory;

	if (recordDB == memoPadDB) {
		posInCategory = DmPositionInCategory(dbMemoPadRef, recordNum, category);
	} else {
		posInCategory = DmNumRecordsInCategory(dbMemoPadRef, category);
		posInCategory += DmPositionInCategory(dbCryptoPadRef, recordNum, category);
	}

	x = bounds->topLeft.x + 1;
	y = bounds->topLeft.y;

	if (useSingleFont == separateFont)
		FntSetFont(TblGetItemFont (tableP, row, kRecordColumn));
	else FntSetFont(currentTableFont);

	/* Format the memo's postion, within its category, 
	* an draw it.
	*/
	StrIToA (posStr, posInCategory+1);
	length = StrLen(posStr);
	ErrNonFatalDisplayIf(length > sizeof(posStr) - 2, "Too many records");
	posStr[length++] = '.';
	posStr[length] = 0;
	
	if (length < 3) x += FntCharWidth ('1');
	WinDrawChars (posStr, length, x, y);
	x += FntCharsWidth (posStr, length) + 4;	

	// If we are here then we either we either mask the memo out or display the
	// memo title.
	DmRecordInfo (recordDBRef, recordNum, &recAttributes, NULL, NULL);
   	if (((recAttributes & dmRecAttrSecret) 
   	&& PrivateRecordVisualStatus == maskPrivateRecords)) {
		RectangleType maskRectangle;

		MemMove (&maskRectangle, bounds, sizeof (RectangleType));
		maskRectangle.topLeft.x = x;
		maskRectangle.extent.x = bounds->extent.x - x;
		MainFormDisplayMask (&maskRectangle);

	} else {
		/* Draw the record text */
		if (recordDB == cryptoPadDB) DrawCryptoPadRecordText(recordNum, x, bounds);
		else  DrawMemoPadRecordText(recordNum, x, bounds);
	}
}

/*******************************************
 * MainFormDisplayMask
 * Draws the masked display for the record.
  ******************************************/
static void MainFormDisplayMask (RectanglePtr bounds)
{
	RectangleType tempRect;
	CustomPatternType origPattern;
	MemHandle	bitmapH;
	BitmapType * bitmapP;
	
	CustomPatternType pattern = {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55};
	
	MemMove (&tempRect, bounds, sizeof (RectangleType));
	// Make sure it fits nicely into the display.
	tempRect.topLeft.y++;
	tempRect.extent.y --;
	tempRect.extent.x -= SecLockWidth + 1;

	WinGetPattern((CustomPatternType *) &origPattern);
	WinSetPattern ((const CustomPatternType *) &pattern);
	WinFillRectangle (&tempRect, 0);
	WinSetPattern((const CustomPatternType *) &origPattern);
	
	//draw lock icon
	bitmapH = DmGetResource (bitmapRsc, SecLockBitmap);
	if (bitmapH) {
		bitmapP = MemHandleLock (bitmapH);
		WinDrawBitmap (bitmapP, tempRect.topLeft.x + tempRect.extent.x + 1, 
			tempRect.topLeft.y + ((tempRect.extent.y - SecLockHeight) / 2));
		MemPtrUnlock (bitmapP);
	}
}

/********************************************************
 * MainFormNumberOfRows
 * This routine return the maximun number of visible rows,
 * with the current list view font setting.
 ********************************************************/
static Int16 MainFormNumberOfRows (TablePtr tableP)
{
	Int16	rowsInTable;
	Int16 nRows = 0;
	UInt32 recordID;

	UInt16 lineHeight;
	UInt16 tableHeight;
	RectangleType	r;

	rowsInTable = TblGetNumberOfRows (tableP);

	TblGetBounds (tableP, &r);
	tableHeight = r.extent.y;

	if (gCardMode) {
		FntSetFont(currentTableFont);
		nRows = tableHeight / FntLineHeight();
		return (nRows <= rowsInTable) ? nRows : rowsInTable;
	}

	if ((useSingleFont == separateFont)
	&& (currentCategory == dmAllCategories)) {
		UInt16	recordNum;
		MemHandle nextRecord;
		UInt16 dataHeight = 0;

		recordNum = topRecord;
		currentDB = topDB;

		nextRecord = DmQueryRecord(currentDBRef, recordNum);

		if (nextRecord) {
			DmRecordInfo (currentDBRef, recordNum, 0, &recordID, 0);
			SetOrGetRecordFont(currentDBRef, recordID, getFont, fontTable);
			FntSetFont (currentTableFont);
			lineHeight = FntLineHeight ();
			dataHeight += lineHeight;
			nRows++;
		}

		while (dataHeight <= tableHeight) {
			nextRecord = MainFormGetNextRecord(&recordNum);
			if (nextRecord == 0) break;
				DmRecordInfo (currentDBRef, recordNum, 0, &recordID, 0);
				SetOrGetRecordFont(currentDBRef, recordID, getFont, fontTable);
				FntSetFont (currentTableFont);
				lineHeight = FntLineHeight ();
				dataHeight += lineHeight;
				nRows++;
		}

		if (nextRecord)
			nRows--;
	} else {
		if (useSingleFont == separateFont)
			GetTableFontOfCategory(currentCategory);
		FntSetFont (currentTableFont);
		nRows = tableHeight / FntLineHeight ();
	}

	if (nRows <= rowsInTable) 
		return (nRows);
	else
		return (rowsInTable);
}


/******************************************
 * MainFormGetNextRecord:
 * Get the record index of
 * the next record in the
 * current category.
 ******************************************
 */

static MemHandle MainFormGetNextRecord(Int16* recordNum)
{
	MemHandle record;

	/* Loop (not recursion) so a long run of non-matching records while
	 * filtering can't grow the stack - see gDbFindActive's own comment. */
	for (;;) {
		(*recordNum)++;
		record = 0;

		/* Seek the item after the recordNum index in MemoPad database.*/
		if (currentDB == memoPadDB) {
			record = DmQueryNextInCategory(dbMemoPadRef, recordNum, currentCategory);

			/* If record is null, set the currentDB to CryptoPadDB
			* to continue the search in the CryptoPad database from
			* the record 0.
			*/
			if (record == 0) {
				currentDB = cryptoPadDB;
				*recordNum = 0;
			}
		}

		/* Seek the item after the recordNum index in CryptoPad database.*/
		if (currentDB == cryptoPadDB) {
			record = DmQueryNextInCategory(dbCryptoPadRef, recordNum, currentCategory);
		}

		if (!record || !gDbFindActive ||
			DbRecordMatches((Boolean) (currentDB == cryptoPadDB), *recordNum))
			return record;
	}
}


/******************************************
 * MainFormGetPreviousRecord:
 * Get the record index of
 * the previous record in the
 * current category.
 ******************************************
 */

static MemHandle MainFormGetPreviousRecord(Int16* recordNum)
{
	Int16 offset = 1;
	Err err;
	MemHandle record;

	/* Loop (not recursion) so a long run of non-matching records while
	 * filtering can't grow the stack - see gDbFindActive's own comment. */
	for (;;) {
		/* Seek the item befor the recordNum index in CryptoPad database.*/
		if (currentDB == cryptoPadDB) {
			if (*recordNum == dmMaxRecordIndex)
				offset = 0;
			err = DmSeekRecordInCategory (dbCryptoPadRef, recordNum,
					offset, dmSeekBackward, currentCategory);

			/* If record is null, set the currentDB to CryptoPadDB
			* to continue the search in the CryptoPad database from
			* the record 0.
			*/
			if ((err == dmErrSeekFailed) &&
			(DmNumRecordsInCategory (dbMemoPadRef, currentCategory) != 0)) {
				currentDB = memoPadDB;
				*recordNum = dmMaxRecordIndex;
				offset = 0;
			}
		}

		/* Seek the item before the recordNum index in MemoPad database.*/
		if (currentDB == memoPadDB) {
			err = DmSeekRecordInCategory (dbMemoPadRef, recordNum,
					offset, dmSeekBackward, currentCategory);
		}

		if (err) return 0;
		record = DmQueryRecord (currentDBRef, *recordNum);
		if (!record || !gDbFindActive ||
			DbRecordMatches((Boolean) (currentDB == cryptoPadDB), *recordNum))
			return record;
		offset = 1;	/* every further step in this loop is a plain 1-back seek */
	}
}

/******************************************
 * MainFormLoadTable:
 * Load the table with the records.
 ******************************************
 */

static void MainFormLoadTable(void)
{	
	TablePtr tableP;
	Int16 recordNum;
	Int16 numRows;

	MemHandle record;
	Int16 nRow;
	UInt16 lineHeight;
	UInt16 dataHeight;
	UInt16 tableHeight;
	RectangleType	r;

	tableP = GetActiveFormObjectPtr(tblMainRecordList);
	numRows = TblGetNumberOfRows(tableP);

	if (gCardMode) { MainFormLoadCardTable(tableP); return; }

	/* Set the correct topRecord and topDB
	 * according to the category.
	 */
	currentDB = topDB;

	topRecord--;
	MainFormGetNextRecord(&topRecord);
	
	topDB = currentDB;
	recordNum = topRecord;


	TblGetBounds (tableP, &r);
	tableHeight = r.extent.y;
	dataHeight = 0;

	/* For each row in the table, store the 
	* record number and database in the 
	* table item that will display the record.
	*/
	for (nRow = 0; nRow < numRows; nRow++) {

		if  (!nRow) recordNum--;
		record = MainFormGetNextRecord(&recordNum);

		if (record && (useSingleFont == separateFont) 
		&& (currentCategory == dmAllCategories)) {
			UInt32 recordID;

			DmRecordInfo (currentDBRef, recordNum, 0, &recordID, 0);
			SetOrGetRecordFont(currentDBRef, recordID, getFont, fontTable);
			FntSetFont (currentTableFont);
		}

		lineHeight = FntLineHeight ();

		if (record && (tableHeight >= dataHeight + lineHeight)) {
			TblSetItemStyle(tableP, nRow, kRecordColumn, customTableItem);
			TblSetItemFont (tableP, nRow, kRecordColumn, currentTableFont);
			TblSetRowHeight (tableP, nRow, lineHeight);
			TblSetRowID(tableP, nRow, recordNum);
			TblSetItemInt(tableP, nRow, kRecordColumn, currentDB);
			TblSetRowUsable(tableP, nRow, true);

		/* Marks the row invalid so that is is redrawn */
		TblMarkRowInvalid(tableP, nRow);
				
		} else {

			// Set the row height - when scrolling down, the heights of the last rows of
			// the table are used to determine how far to scroll.  As rows are deleted
			// from the top of the table, formerly unused rows scroll into view, and the
			// height is used before the next call to ListViewLoadTable (which would set
			// the height correctly).
			TblSetRowHeight (tableP, nRow, lineHeight);
			TblSetRowUsable(tableP, nRow, false);
		}

	dataHeight += lineHeight;
	}

	MainFormUpdateScrollBar();
}

/******************************************
 * MainFormLoadRecords:
 * Load the records that will be displayed
 * in the table.
 ******************************************
 */

static void MainFormLoadRecords(void)
{
	TablePtr tableP;

	MemHandle record;

	if (showAllCategories) currentCategory = dmAllCategories;

	tableP = GetActiveFormObjectPtr(tblMainRecordList);

	/* Try to show a full display of records 
	 *  and perform positioning on the table.
	 */
	MainFormShowFullDisplay();

	/* Load each row of the table with a record.
	 * If no record could be found, mark the row
	 * unusable.
	 */
	MainFormLoadTable();

	/* Set the table's custom draw procedure */
	TblSetCustomDrawProcedure(tableP, kRecordColumn, MainFormDrawTableItem);

	/* Mark the columns as usable otherwise
	 * the table won't be displayed by default.
	 */

	TblSetColumnUsable(tableP, kRecordColumn, true);

	/* Updates the scrollbars */
	MainFormUpdateScrollBar();
}

/******************************************
 * MainFormShowFullDisplay:
 * Show a full diplay in the main form.
 ******************************************
 */

static void MainFormShowFullDisplay(void)
{
	UInt16 recordNum;
	MemHandle previousRecord;
	TablePtr tableP;
	Int16 nRow, numRows = 0;
	UInt32 recordID;

	UInt16 lineHeight;
	UInt16 dataHeight = 0;
	UInt16 tableHeight;
	RectangleType	r;

	tableP = GetActiveFormObjectPtr(tblMainRecordList);

	TblGetBounds (tableP, &r);
	tableHeight = r.extent.y;

	if (currentRecord != noRecordSelected) {
	/* There is a current record. Make it visible.
	 * If the current record index is smaller than the top index
	 * of table, update the top index.
	 * Otherwise, find the record displayed at the bottom of the table
	 * If the record is before the current record then the 
	 * current record isn't displayed.  In that case make
	 * the current record the first record displayed in the table.
	 */

		if (currentDB == memoPadDB) {
			if (topRecord > currentRecord)
				topRecord = currentRecord;
			else {
				numRows = MainFormNumberOfRows (tableP);
				recordNum = topRecord;

				for (nRow = 1; nRow < numRows; nRow++)
					MainFormGetNextRecord(&recordNum);

				if ((currentDB == memoPadDB)
						&& (recordNum < currentRecord))
					topRecord = currentRecord;
			}
		}
	}

	/* Try to show a full display of records.  Starting at the last
	 * record and working backwards, find the record displayed at the 
	 * top of the table.  If the record is before the TopVisibleRecord
	 * then TopVisibleRecord is set too far down the list of records.
	 * Set TopVisibleRecord to the record one screen full from the end.
	 */

	if ((useSingleFont == separateFont)
		&& (currentCategory == dmAllCategories)) {

				recordNum = dmMaxRecordIndex;
				currentDB = cryptoPadDB;

				while (dataHeight <= tableHeight) {
					previousRecord = MainFormGetPreviousRecord(&recordNum);

					if (previousRecord == 0) break;

					if (useSingleFont == separateFont) {
						DmRecordInfo (currentDBRef, recordNum, 0, &recordID, 0);
						SetOrGetRecordFont(currentDBRef, recordID, getFont, fontTable);
					}
					FntSetFont (currentTableFont);
					lineHeight = FntLineHeight ();
					dataHeight += lineHeight;
				}

			if (previousRecord)
				MainFormGetNextRecord(&recordNum);
		
	} else {
		recordNum = dmMaxRecordIndex;
		currentDB = cryptoPadDB;

		if (!numRows)
			numRows = MainFormNumberOfRows (tableP);
		
		previousRecord = MainFormGetPreviousRecord(&recordNum);

		for (nRow = 1; nRow < numRows; nRow++) {
			previousRecord = MainFormGetPreviousRecord(&recordNum);

			if (previousRecord == 0) break;
		}
	}

	if (topDB == currentDB)
		topRecord = min(topRecord, recordNum);
	else if (topDB == cryptoPadDB) {
		topRecord = recordNum;
		topDB = currentDB;
	}
}

/******************************************
 * DecryptRecord:
 * Decrypt the current record.
 ******************************************
 */

void DecryptRecord(UInt16 recordNum, Char * passwordP)
{
	MemHandle handle;
	RecordType record;

	UInt16 nOffset;

	/* Db35-suite-style Sec35 wrap: get the correctly-unwrapped record via
	 * the chokepoint first (a separate, self-contained lock/unlock -- safe
	 * to pair with the DmGetRecord below, PalmOS reference-counts handle
	 * locks). DmGetRecord is still needed for a WRITABLE handle for
	 * PackRecord further down; CryptoSealRecord re-wraps its plain write
	 * afterward, mirroring Datebook35's ApptChangeRecord->ApptEncryptOne. */
	if (! CryptoGetRecord (dbCryptoPadRef, recordNum, &record))
		return;

	handle = DmGetRecord(dbCryptoPadRef, recordNum);

	/* Finds the offset to start decryption */

	nOffset = 0;
	while (nOffset <= record.textLen) {
		if (record.textP[nOffset++] == '\n') break;
	}

	if (nOffset < record.textLen) {
		RecordType newRecord;

		/* Set the flag to reflect the status of the record */

		newRecord.flag      = recDecrypted;
		newRecord.passwdLen = 0;
		newRecord.textLen   = record.textLen;

		/* Get rid of the stored password */

		newRecord.passwordP = MemPtrNew(1);
		newRecord.passwordP[0] = '\0';

		/* The record is now stored as plaintext (recDecrypted), so the
		 * stored IVs are no longer meaningful - zero them defensively
		 * rather than leaving stale values on disk. */

		MemSet(newRecord.pwIV, sizeof(newRecord.pwIV), 0);
		MemSet(newRecord.bodyIV, sizeof(newRecord.bodyIV), 0);
		MemSet(newRecord.kdfSalt, sizeof(newRecord.kdfSalt), 0);
		newRecord.kdfRounds = 0;

		/* Store the decrypted text in the record */

		newRecord.textP = MemPtrNew(record.textLen + 1);
		StrNCopy(newRecord.textP, record.textP, nOffset);
		AESCryptEx(record.textP + nOffset, newRecord.textP + nOffset,
					passwordP, globalPasswordP, record.bodyIV,
					record.kdfSalt, record.kdfRounds,
					newRecord.textLen - nOffset, AES_DECRYPT);

		/* Replaces the record with the new one */

		PackRecord(&newRecord, handle);

		/* Deallocates memory */

		MemPtrFree(newRecord.passwordP);
		MemPtrFree(newRecord.textP);

		/* Release the record to the data manager, then re-seal: PackRecord
		 * just wrote fresh PLAIN bytes (no leading byte at all), so if
		 * Sec35 has an active passcode the record must be re-wrapped or it
		 * would be left decrypted on disk. Db35-suite-style Sec35 wrap:
		 * NOT a MemHandleUnlock(handle) here -- `handle` (from
		 * DmGetRecord above) is never locked in this function; the
		 * chokepoint's own CryptoGetRecord call at the top manages its
		 * own separate lock/unlock, and PackRecord locks+unlocks `handle`
		 * itself internally. The old MemHandleUnlock(handle) here (kept
		 * from before the chokepoint retrofit removed this function's own
		 * MemHandleLock) was unlocking a chunk that was never locked --
		 * a real "chunk under-locked" MemoryMgr fatal alert, hit during
		 * testing on any layer-2-password-protected memo. */
		DmReleaseRecord(dbCryptoPadRef, recordNum, true);
		CryptoSealRecord (dbCryptoPadRef, recordNum);
		return;
	}

	/* Release the record to the data manager */

	DmReleaseRecord(dbCryptoPadRef, recordNum, true);
}

/******************************************
 * EncryptCategory:
 * Encrypt the current category.
 ******************************************
 */

static void EncryptCategory()
{
	MemHandle memoPadHandle;

	UInt16 recMemoAttributes, recCryptoAttributes;
	UInt16 i, numRecInCategory;

	Char * textP;
	UInt16 nOffset;

	FormPtr formP;
    ProgressInfoPtr progrInfoP;
    ProgrType progr;
	UInt16 progrinc;

	Char * strProgressP;
	MemHandle strProgressH;


	numRecInCategory = DmNumRecordsInCategory(dbMemoPadRef, currentCategory);
	if (numRecInCategory == 0) {
		FrmAlert(altCategoryAlreadyEncryptedAlert);
		RemoveCurrentPassword();
		return;
	}

	formP = FrmInitForm(frmProgressForm);

	// Display the title of the progress form.
	strProgressH = DmGetResource('tSTR', strProgressEncryption);
	strProgressP = MemHandleLock(strProgressH);
		
	FrmSetTitle (formP, strProgressP);

	MemHandleUnlock(strProgressH);   
	DmReleaseResource(strProgressH); 

	FrmDrawForm(formP);

	currentRecord = 0;


	progrInfoP = ProgressFormInitAndDraw (formP);

	for (i = 0; i < numRecInCategory; i++) {
		progr.major.soFar = 0;
		progr.major.total = numRecInCategory;
		progr.minor.soFar = 0;
		progr.minor.total = 0;
		progrinc = i ;
		progr.major.soFar = progrinc;

		/* Seek the item after the currentRecord index in MemoPad database.*/
		memoPadHandle = DmQueryNextInCategory(dbMemoPadRef, &currentRecord, currentCategory);
		textP = MemHandleLock(memoPadHandle);

		/* Validate the format of the record
		 * in checking if there is 2 lines.
		 */
		nOffset = 0;
		while (nOffset <= StrLen(textP)) {
			if (textP[nOffset++] == '\n') break;
		}

		progrInfoP->statusFunc (progrInfoP, &progr, textP);

		if (nOffset < StrLen(textP)) {
			MemHandle cryptoPadHandle;
			RecordType newRecord;
			UInt16 index;
			UInt32 memoRecordID;
			UInt16 textLength, passwordLength;

			/* Creates a new record at the end of the database */
			index = dmMaxRecordIndex;

			cryptoPadHandle = DmNewRecord(dbCryptoPadRef, &index, sizeof(PackedRecordType));
			ErrFatalDisplayIf(cryptoPadHandle == 0, "Unable to add record to the database.");

			/* Current record now contains the record index.
			 * Sets an initial value for the record.
			 */
	
			passwordLength = StrLen(currentPasswordP);
			textLength = StrLen(textP);

			newRecord.flag      = recDecrypted;
			newRecord.passwdLen =  passwordLength;
			newRecord.textLen   = textLength;
			MemSet(newRecord.pwIV, sizeof(newRecord.pwIV), 0);
			MemSet(newRecord.bodyIV, sizeof(newRecord.bodyIV), 0);
			MemSet(newRecord.kdfSalt, sizeof(newRecord.kdfSalt), 0);
			newRecord.kdfRounds = 0;
			newRecord.passwordP = currentPasswordP;
			newRecord.textP = textP;

			PackRecord(&newRecord, cryptoPadHandle);

			DmReleaseRecord(dbCryptoPadRef, index, true);

			/* Set the category of the record */
			DmRecordInfo(dbMemoPadRef, currentRecord, &recMemoAttributes, &memoRecordID, 0);

			DmSetRecordInfo(dbCryptoPadRef, index, &recMemoAttributes, 0);

	if (useSingleFont == separateFont) {
		UInt32 cryptoRecordID;

		DmRecordInfo (dbCryptoPadRef, index, 0, &cryptoRecordID, 0);
		GetTableFontOfCategory(recMemoAttributes & dmRecAttrCategoryMask);
		CreateFontRecord(dbCryptoPadRef, cryptoRecordID);
		SetOrGetRecordFont(dbMemoPadRef, memoRecordID, getFont, fontField);
		SetOrGetRecordFont(dbCryptoPadRef, cryptoRecordID, setFont, fontField);
	}

			/* Delete the record from MemoPadDB */
			DmDeleteRecord (dbMemoPadRef, currentRecord);
			DmMoveRecord (dbMemoPadRef, currentRecord, DmNumRecords (dbMemoPadRef));

			/* Encrypt the record */
			EncryptRecord(index, currentPasswordP);

			/* Db35-suite-style Sec35 wrap: this just created a brand-new
			 * CryptoPad record (bulk-converted from MemoPad) -- seal it
			 * too, same as any other freshly-written record. */
			CryptoSealRecord (dbCryptoPadRef, index);
		} else {
			currentRecord++;
			MemHandleUnlock(memoPadHandle);
		}
	}

	progr.major.soFar = progr.major.total;
	progrInfoP->statusFunc (progrInfoP,&progr,NULL);

	RemoveCurrentPassword();

	if (currentSortOrder == soAlphabetic) {
		DmQuickSort(dbCryptoPadRef, 
			(DmComparF*) &CompareCryptoPadRecords, (Int16) currentSortOrder);
		DmQuickSort(dbMemoPadRef, 
			(DmComparF*) &CompareMemoPadRecords, (Int16) currentSortOrder);
	}

	currentRecord = 0;
	currentDB = cryptoPadDB;

	FrmDeleteForm(formP);
	FrmGotoForm(frmMainForm);
}

/******************************************
 * DecryptCategory:
 * Decrypt the current category.
 ******************************************
 */

static void DecryptCategory()
{
	UInt16 i, numRecInCategory;

	FormPtr formP;
    ProgressInfoPtr progrInfoP;
    ProgrType progr;
	UInt16 progrinc;

	Char * strProgressP;
	MemHandle strProgressH;


	numRecInCategory = DmNumRecordsInCategory(dbCryptoPadRef, currentCategory);
	if (numRecInCategory == 0) {
		FrmAlert(altCategoryAlreadyDecryptedAlert);
		RemoveCurrentPassword();
		return;
	}

	formP = FrmInitForm(frmProgressForm);

	// Display the title of the progress form.
	strProgressH = DmGetResource('tSTR', strProgressDecryption);
	strProgressP = MemHandleLock(strProgressH);
		
	FrmSetTitle (formP, strProgressP);

	MemHandleUnlock(strProgressH);   
	DmReleaseResource(strProgressH); 


	FrmDrawForm(formP);


	currentRecord = 0;

	progrInfoP = ProgressFormInitAndDraw (formP);

	for (i = 0; i < numRecInCategory; i++) {

		progr.major.soFar = 0;
		progr.major.total = numRecInCategory;
		progr.minor.soFar = 0;
		progr.minor.total = 0;
		progrinc = i ;
		progr.major.soFar = progrinc;

		progrInfoP->statusFunc (progrInfoP,&progr,NULL);

		/* Seek the item after the currentRecord index in MemoPad database.*/
		DmSeekRecordInCategory (dbCryptoPadRef, &currentRecord, 0, dmSeekForward, currentCategory);

		if (ValidatePassword(currentPasswordP)) {
			UInt16 recMemoAttributes, recCryptoAttributes;
			UInt32  cryptoRecordID;
			UInt16 index;
			UInt16 nLength;

			MemHandle memoPadHandle;
			RecordType record;
			MemPtr		p;

			if (isEncrypted(currentRecord))
				DecryptRecord(currentRecord, currentPasswordP);

			/* Db35-suite-style Sec35 wrap: DecryptRecord above only strips
			 * the layer-2 "Encrypt Memo" password (and, via CryptoSealRecord,
			 * re-wraps under Sec35 if a passcode is active) -- so this
			 * record can still be Sec35-wrapped at this point. Route
			 * through the chokepoint rather than reading raw bytes. */
			if (! CryptoGetRecord (dbCryptoPadRef, currentRecord, &record))
				continue;

			progrInfoP->statusFunc (progrInfoP,&progr,record.textP);

			/* Create a new record in the MemoPad database */
			index = dmMaxRecordIndex;
			/* SafeStrLen, not StrLen -- StrLen's Int16 return sign-extends
			 * to a huge garbage value once passed to DmWrite's UInt32
			 * `bytes` parameter below, for any record over ~32KB (now
			 * reachable via a large SD-card import). */
			nLength = (UInt16) SafeStrLen(record.textP);

			memoPadHandle = DmNewRecord(dbMemoPadRef, &index, nLength + 1);
			ErrFatalDisplayIf(memoPadHandle == 0, "Unable to add record to the database.");

			/* Lock down the block containing the new record */
			p = MemHandleLock(memoPadHandle);

			/* Write the field's text to the database record */
			DmWrite(p, 0, record.textP, SafeStrLen(record.textP) + 1);

			/* Unlock the block of the new record */
			MemPtrUnlock(p);
			
			DmReleaseRecord(dbMemoPadRef, index, true);
			
			/* Set the category of the record */
			DmRecordInfo(dbCryptoPadRef, currentRecord, &recCryptoAttributes, &cryptoRecordID, 0);

			DmSetRecordInfo(dbMemoPadRef, index, &recCryptoAttributes, 0);

	if (useSingleFont == separateFont) {
		UInt32 memoRecordID;

		DmRecordInfo (dbMemoPadRef, index, 0, &memoRecordID, 0);
		GetTableFontOfCategory(recCryptoAttributes & dmRecAttrCategoryMask);
		CreateFontRecord(dbMemoPadRef, memoRecordID);
		SetOrGetRecordFont(dbCryptoPadRef, cryptoRecordID, getFont, fontField);
		SetOrGetRecordFont(dbMemoPadRef, memoRecordID, setFont, fontField);

		// Delete the record in the Font database.
		DeleteFontRecord(dbCryptoPadRef, cryptoRecordID);
	}

			/* Delete the record from CryptoPadDB */
			DmRemoveRecord(dbCryptoPadRef, currentRecord);
		} else {
			currentRecord++;
		}
	}

	progr.major.soFar = progr.major.total;
	progrInfoP->statusFunc (progrInfoP,&progr,NULL);

	RemoveCurrentPassword();

	if (currentSortOrder == soAlphabetic) {
		DmQuickSort(dbCryptoPadRef, 
			(DmComparF*) &CompareCryptoPadRecords, (Int16) currentSortOrder);
		DmQuickSort(dbMemoPadRef, 
			(DmComparF*) &CompareMemoPadRecords, (Int16) currentSortOrder);
	}

	currentRecord = noRecordSelected;
	currentDB = memoPadDB;

	FrmDeleteForm(formP);
	FrmGotoForm(frmMainForm);
}

/*****************************************
 * ProgressFormInitAndDraw:
 * Init and draw the progress bar
 * for encryption/decryption of
 * the category. 
 *****************************************
 */
static ProgressInfoPtr ProgressFormInitAndDraw (FormPtr frmP)
{
	ProgressInfoPtr infoP = NULL;

	ErrNonFatalDisplayIf (!frmP, "nil form");


	// Allocate and initialize the Progress Info structure
	infoP = MemPtrNew (sizeof (ProgressInfoType));
	ErrNonFatalDisplayIf (!infoP, "alloc failed");
	if (!infoP)
		return (NULL);

	MemSet (infoP, sizeof (*infoP), 0);

	// Get bounds of the progress message and gauge objects once
	FrmGetObjectBounds (frmP, FrmGetObjectIndex (frmP, gadProgressMessage),
			&infoP->messageR);

	WinDrawGrayRectangleFrame(simpleFrame, &infoP->messageR);

	FrmGetObjectBounds (frmP, FrmGetObjectIndex (frmP, gadProgressBar),
					  &infoP->gaugeR);


	// Init the rest of the progress info structure
	infoP->progressForm = frmP;
	infoP->gaugeWidth = infoP->gaugeR.extent.x;
	infoP->gaugeLevel = 0;
	infoP->statusFunc = ProgressCallback;

	// Draw the form
	FrmDrawForm (frmP);

	// Initialize the gauge
	ProgressUpdateGauge (&infoP->gaugeR, 0);

	return (infoP);
}

/***************************************
 * ProgressCallback:
 * callback function for
 * the progress bar.
 ***************************************
 */
static Boolean ProgressCallback (void* progressInfoP, 
		ProgrType* progressP, Char* messageP)
{
	UInt16		gaugeLevel = 0;
	ProgressInfoPtr infoP = (ProgressInfoPtr) progressInfoP;
	UInt16		itemWidth = 0;


	// Process any pending events, such as those for our Stop button
 
	// Reset the auto-off timer to make sure we don't fall asleep in the
	//  middle of the update
	EvtResetAutoOffTimer ();

	while (ProgressHandleEvent (0 /*timeout*/) != nilEvent)
		{/*intentionally left blank*/}


	// Update the progress gauge
	if (progressP) {
		// Update the progress gauge counts
		ErrNonFatalDisplayIf (!progressP->major.total, "major denominator is zero");

		if (progressP->major.total)
			gaugeLevel = (UInt16) ((progressP->major.soFar * infoP->gaugeWidth)
								  / progressP->major.total);

		if (progressP->minor.soFar) {
			ErrNonFatalDisplayIf (!progressP->minor.total, "minor denominator is zero");

			if (progressP->major.total)
				itemWidth = (UInt16)(infoP->gaugeWidth / progressP->major.total);
			if (itemWidth && progressP->minor.total)
				gaugeLevel += (UInt16) ((progressP->minor.soFar * itemWidth)
									  / progressP->minor.total);
		}

		if (gaugeLevel != infoP->gaugeLevel) {
			infoP->gaugeLevel = gaugeLevel;
		  
		ProgressUpdateGauge (&infoP->gaugeR, gaugeLevel);
	}

} 


	// Update the progress message
	if (messageP) {
		FontID	currFont;

		// Switch to bold, saving the current font
		currFont = FntSetFont (stdFont);

		WinEraseRectangle (&infoP->messageR, 0 /*cornerDiam*/);

		DrawStringWithEllipsis (messageP, infoP->messageR.topLeft.x, 
			infoP->messageR.topLeft.y, infoP->messageR.extent.x, justCenter);

		// Restore font
		FntSetFont (currFont);
	}

	return (false);

} 

/**************************************
 * ProgressUpdateGauge:
 * Update the progress indicator.
 **************************************
 */
static void ProgressUpdateGauge (RectangleType*	gaugeRP, 
										UInt32 gaugeLevel)
{
	RectangleType 	eraseR;
	RectangleType 	completeR;

  
	ErrNonFatalDisplayIf (!gaugeRP, "null arg");


	// Draw the frame of gadget
	WinDrawRectangleFrame (simpleFrame, gaugeRP);
  

	// Fill in the gauge.
	if (gaugeLevel > (UInt32)gaugeRP->extent.x) {
		ErrDisplay ("gaugeLevel out of bounds");
		gaugeLevel = gaugeRP->extent.x;
	}

	completeR.topLeft.x = gaugeRP->topLeft.x;
	completeR.topLeft.y = gaugeRP->topLeft.y;
	completeR.extent.x = (UInt16)gaugeLevel;
	completeR.extent.y = gaugeRP->extent.y;
	WinDrawRectangle (&completeR, 0);
  

	// Erase the empty portion of the gauge (not really necessary).
	eraseR.topLeft.x = (UInt16)(gaugeRP->topLeft.x + gaugeLevel);
	eraseR.topLeft.y = gaugeRP->topLeft.y;
	eraseR.extent.x = (UInt16)(gaugeRP->extent.x - gaugeLevel);
	eraseR.extent.y = gaugeRP->extent.y;
	WinEraseRectangle (&eraseR, 0);

	return;
}

/******************************************
 * MainFormGetNextCategory:
 * find the next category where there is
 * either a record from MemoPadDB or
 * from CryptoPad
 ******************************************
 */

static UInt16 MainFormGetNextCategory(UInt16 category)
{
	/* #7: cycle through ALL categories (including empty ones), advancing over the
	 * UNION of the named categories in both databases -- which CatSync keeps in
	 * step -- and wrapping through "All".  This ignores the old record-count /
	 * rotate-order skipping so every category is reachable by repeated presses. */
	UInt16 nextCryptoPadDBCategory = CategoryGetNext(dbCryptoPadRef, category);
	UInt16 nextMemoPadDBCategory   = CategoryGetNext(dbMemoPadRef, category);
	Char nextCryptoPadCategoryName[dmCategoryLength];
	Char nextMemoPadCategoryName[dmCategoryLength];

	/* If one DB wrapped to "All", take the other's next (or All if both wrapped). */
	if (nextCryptoPadDBCategory == dmAllCategories)
		return nextMemoPadDBCategory;
	if (nextMemoPadDBCategory == dmAllCategories)
		return nextCryptoPadDBCategory;

	/* Both have a real next category: take whichever sorts first so we visit
	 * each distinct name once as we go around. */
	CategoryGetName(dbCryptoPadRef, nextCryptoPadDBCategory, nextCryptoPadCategoryName);
	CategoryGetName(dbMemoPadRef, nextMemoPadDBCategory, nextMemoPadCategoryName);

	if (StrCompare(nextCryptoPadCategoryName, nextMemoPadCategoryName) <= 0)
		return nextCryptoPadDBCategory;
	return nextMemoPadDBCategory;
}


/******************************************
 * MainFormNextCategory:
 * switches to the next category view
 ******************************************
 */

static void MainFormNextCategory(void)
{
	UInt16 category;

	category = MainFormGetNextCategory(currentCategory);

	if (category != currentCategory) {
		TablePtr tableP;
		ControlPtr controlP;
		FormPtr formP;

		if (category == dmAllCategories) showAllCategories = true;
		else showAllCategories = false;

		ChangeCategory(category);		

		/* Set the label of the category trigger */
		controlP = GetActiveFormObjectPtr(popMainCategory);
		CategoryGetName(dbMemoPadRef, currentCategory, categoryName);
		CategorySetTriggerLabel(controlP, categoryName);

		/* Display the new category */
		formP = FrmGetActiveForm ();
		if (useSingleFont == separateFont) {
			GetTableFontOfCategory(currentCategory);
			FntSetFont (currentTableFont);
		}

		MainFormLoadTable ();
		tableP = GetActiveFormObjectPtr(tblMainRecordList);
		TblEraseTable (tableP);
		TblDrawTable (tableP);
	}
}

/******************************************
 * MainFormSelectCategory:
 * Invokes the standard category UI
 ******************************************
 */

static void MainFormSelectCategory(void)
{
	FormPtr  formP = FrmGetActiveForm();
	ListPtr  lst;
	UInt16   vol;
	Boolean  haveCard = CardPresent(&vol);
	static char  names[20][24];		/* >dmCategoryLength: fits "Edit Categories..." */
	static char* choices[20];
	Int16    meaning[20];   /* >=0 = category index; -100 Card; -101 Edit */
	Int16    n = 0, i, sel, m;

	/* Build the popup: All + named categories + Unfiled + "Edit Categories",
	 * then (only if a card is present) "Card" - whose row also draws the divider
	 * line at its top. Mirrors the standard category popup + the Card source. */
	StrCopy(names[n], "All"); choices[n] = names[n]; meaning[n] = dmAllCategories; n++;
	for (i = 1; i < dmRecNumCategories && n < 16; i++) {
		CategoryGetName(dbMemoPadRef, i, names[n]);
		if (names[n][0]) { choices[n] = names[n]; meaning[n] = i; n++; }
	}
	CategoryGetName(dbMemoPadRef, dmUnfiledCategory, names[n]);
	if (names[n][0]) { choices[n] = names[n]; meaning[n] = dmUnfiledCategory; n++; }
	StrCopy(names[n], "Edit Categories\205"); choices[n] = names[n]; meaning[n] = -101; n++;	/* \205 = ellipsis, matches the ROM "Edit Categories..." */
	gCatCardRow = -1;
	if (haveCard) {
		gCatCardRow = n;
		StrCopy(names[n], "Card"); choices[n] = names[n]; meaning[n] = -100; n++;
	}

	lst = GetActiveFormObjectPtr(lstMainCategory);
	LstSetListChoices(lst, choices, n);
	LstSetSelection(lst, -1);
	LstSetHeight(lst, n < 11 ? n : 11);
	/* Custom draw only when the Card row is present; reset afterwards so the ROM
	 * CategorySelect() (Edit Categories) reuses this list object cleanly. */
	if (haveCard) {
		LstSetDrawFunction(lst, CardCatListDraw);
		/* Grow the popup by 1px so the last ("Card") row can sit 1px lower and
		 * still keep a 1px gap above the bottom frame.  LstPopupList sizes the
		 * window from the list bounds, so the extra pixel becomes a blank strip
		 * below the last row. */
		lst->bounds.extent.y += 1;
	}
	sel = LstPopupList(lst);
	LstSetDrawFunction(lst, NULL);
	gCatCardRow = -1;
	if (sel < 0) return;
	m = meaning[sel];

	if (m == -100) {                             /* Card -> browse /Notes */
		gDbFindActive = false;		/* switching to Card exits the (DB-list) filter */
		gDbFindTerm[0] = '\0';
		MainClearFindFieldText();
		if (MainFormEnterCard())
			CategorySetTriggerLabel(GetActiveFormObjectPtr(popMainCategory), "Card");
		FrmUpdateForm(frmMainForm, updateRedrawAll);
		return;
	}

	if (m == -101) {                             /* Edit Categories -> straight to the
	                                              * editor (CategoryEdit, NOT
	                                              * CategorySelect, which would re-pop the
	                                              * list and force a 2nd "Edit Categories"
	                                              * tap - matches ToDo35's behaviour). */
		UInt16 cat = gCardMode ? dmAllCategories : currentCategory;
		(void) formP;
		CategoryEdit(dbMemoPadRef, &cat, 0, 1);
		gCardMode = false;
		gCardFindActive = false;	/* switching category exits either filter */
		gDbFindActive = false;
		gDbFindTerm[0] = '\0';
		MainClearFindFieldText();
		showAllCategories = (cat == dmAllCategories);
		CategoryGetName(dbMemoPadRef, cat, categoryName);
		ChangeCategory(cat);
		FrmUpdateForm(frmMainForm, updateRedrawAll);
		return;
	}

	/* a normal category */
	gCardMode = false;
	gCardFindActive = false;		/* switching category exits either filter */
	gDbFindActive = false;
	gDbFindTerm[0] = '\0';
	MainClearFindFieldText();
	showAllCategories = (m == dmAllCategories);
	CategoryGetName(dbMemoPadRef, m, categoryName);
	CategorySetTriggerLabel(GetActiveFormObjectPtr(popMainCategory), categoryName);
	ChangeCategory(m);
	FrmUpdateForm(frmMainForm, updateRedrawAll);
}

/******************************************
 * MainFormUpdateDisplay:
 * Updates the display of the main form.
 ******************************************
 */

static Boolean MainFormUpdateDisplay(UInt16 updateCode)
{	
	TablePtr tableP;

	if (updateCode & (updateTable | 
			updateFontChanged | updateRedrawAll)) {
		if (updateCode & updateTable) {
			topRecord = 0;
			topDB = memoPadDB;
		} 

		if (updateCode & updateFontChanged) {
			if (useSingleFont == separateFont)
				SynchroniseTableFontForCurrentCategory();
		}
		
		MainFormLoadRecords();
		tableP = GetActiveFormObjectPtr(tblMainRecordList);
		TblEraseTable(tableP);
		TblDrawTable(tableP);		

		return (true);
	}
		
	return (false);
}

/******************************************
 * MainFormUpdateScrollBar:
 * Updates the size and position of the
 * scroll bar object.
 ******************************************
 */

static void MainFormUpdateScrollBar(void)
{
	FormPtr formP;
	TablePtr tableP;
	ScrollBarPtr scrollBarP;
	Int16 nRows;
	UInt16 nPosition, maxValue;

	tableP = GetActiveFormObjectPtr(tblMainRecordList);
	scrollBarP = GetActiveFormObjectPtr(sclMainScrollBar);

	nRows = MainFormNumberOfRows(tableP);

	/* SD card browse mode has its own display list (gCardEnt/gCardTop), not the
	 * DB-note one (memosInCategory/topRecord/topDB) the rest of this function
	 * assumes -- without this branch the scrollbar always reflected the (small
	 * or empty) note count instead of the card file count, capping maxValue at
	 * 0 and making a large card directory unscrollable past the first page.
	 * Bulk logic lives in sec001 (MainFormCardUpdateScrollBar, AES_SECTION),
	 * main is at its 32KB cap. */
	if (gCardMode) {
		MainFormCardUpdateScrollBar(scrollBarP, nRows);
		return;
	}

	{
		/* While filtering, the scrollbar's range must reflect the MATCH
		 * count, not the true category count, or it reads as unscrollable
		 * (too few matches) or overshoots into non-matches (too many).
		 * gDbFindMatchCount is kept current by MainCountDbMatches, called
		 * whenever the filter term changes - see its own comment. */
		UInt16 total = gDbFindActive ? gDbFindMatchCount : memosInCategory;

		if (total < nRows) {
			maxValue = 0;
			nPosition = 0;
		}
		else {
			if (topDB == cryptoPadDB) {
				nPosition = (DmNumRecordsInCategory(dbMemoPadRef, currentCategory)
					+ DmPositionInCategory(dbCryptoPadRef, topRecord, currentCategory));
			} else {
				nPosition = DmPositionInCategory(dbMemoPadRef, topRecord, currentCategory);
			}

			maxValue = total - nRows;
			/* Position above is the record's TRUE (unfiltered) rank, which
			 * can exceed a filtered maxValue - clamp rather than track the
			 * exact filtered rank (would need an O(n) rescan on every
			 * scroll, too costly on real hardware for a cosmetic-only
			 * thumb position). */
			if (nPosition > maxValue) nPosition = maxValue;
		}
	}

	SclSetScrollBar(scrollBarP, nPosition, 0, maxValue, nRows);
}

/******************************************
 * MainFormScrollLines:
 * Scrolls the table nLines up (negative)
 * or down (positive).
 ******************************************
 */

static void MainFormScrollLines(Int16 nLines)
{
	TablePtr tableP;
	Int16 direction;
	Int16 iLines;

	UInt16 nRow;
	Int16 recordNum;
	MemHandle record;

	/* SD card browse mode: gCardTop drives MainFormLoadCardTable, not the
	 * DB-note topRecord/topDB state the rest of this function scrolls --
	 * bulk logic lives in sec001 (MainFormCardScrollLines, AES_SECTION),
	 * main is at its 32KB cap. Was previously entirely unhandled here
	 * (gCardTop was set once on entering a directory and never touched
	 * again), so hardware page up/down and scrollbar dragging silently
	 * did nothing in card mode. */
	if (gCardMode) {
		MainFormCardScrollLines(nLines);
		return;
	}

	if (nLines < 0) {
		nLines = -nLines;
		direction = dmSeekBackward;
	} else direction = dmSeekForward;
	
	recordNum = topRecord;
	currentDB = topDB;

	for (nRow = 0; nRow < nLines; nRow++) {
		if (direction == dmSeekForward) 
			record = MainFormGetNextRecord(&recordNum);
		else record = MainFormGetPreviousRecord(&recordNum);

		if (record == 0) break;
	}

	topRecord = recordNum;
	topDB = currentDB;

	currentRecord = noRecordSelected;

	tableP = GetActiveFormObjectPtr(tblMainRecordList);
	MainFormLoadRecords();

	TblRedrawTable(tableP);
}


/******************************************
 * MainFormScrollPage:
 * Scrolls the table one full page in
 * the given direction.
 ******************************************
 */

static void MainFormScrollPage(WinDirectionType direction)
{
	TablePtr tableP;
	Int16 nRows;

	tableP = GetActiveFormObjectPtr(tblMainRecordList);
	nRows = MainFormNumberOfRows(tableP);

	if (direction == winUp)
		MainFormScrollLines(-nRows + 1);
	else MainFormScrollLines(nRows - 1);
}


/******************************************
 * MainFormHandleMenuEvent:
 * Process menu commands.
 ******************************************
 */

static void MainFormHandleMenuEvent(UInt16 itemID)
{
	UInt32 currentRecordID;
	Boolean wasHiding;
	UInt16 mode;
	EventType event;
	FormPtr formP;

Int16 index = 0, i, recAttributes;
USES_ALERT;
Err err;

	switch (itemID) {
		case itemRecordEncryptCategory:
		case itemRecordDecryptCategory:
			if (gCardMode) {         /* bulk-crypt every note in the current folder */
				if (itemID == itemRecordEncryptCategory)
					MainFormCardEncryptAll();
				else
					MainFormCardDecryptAll();
				break;
			}
			if ((prefsGlobalPasswordP != 0) && (globalPasswordP == 0)) {
				event.eType = globalPasswordStatusEvent;
				event.data.generic.datum[1] = queryGlobalPassword;
				if (itemID == itemRecordEncryptCategory)
					event.data.generic.datum[2] = catDecrypted;
				else event.data.generic.datum[2] = catEncrypted;

				FrmPopupForm(frmQueryGlobalPasswordForm);
				EvtAddEventToQueue(&event);
			} else {
				event.eType = memoStatusEvent;
				if (itemID == itemRecordEncryptCategory)
					event.data.generic.datum[1] = catDecrypted;
				else event.data.generic.datum[1] = catEncrypted;

				FrmPopupForm(frmPasswordForm);
				EvtAddEventToQueue(&event);
			}
			break;

		case itemOptionsFont:
			if (useSingleFont == separateFont) {
				if (currentCategory == dmAllCategories) {
					FrmAlert(altChangeTableFontOfCategoryAllAlert);
					break;
				} else GetTableFontOfCategory(currentCategory);
			}

			currentTableFont = SelectFont (currentTableFont);
			break;

		case itemRecordBeamCategory:
			if (gCardMode) { FrmAlert(altCardNoBeam); break; }
			MemoSendCategory(dbMemoPadRef, currentCategory);
			break;

		case itemOptionsPrefs:
			FrmPopupForm(frmPrefsForm);
			break;

		case itemOptionsSecurity:
			wasHiding = (PrivateRecordVisualStatus == hidePrivateRecords);
			 
			PrivateRecordVisualStatus = SecSelectViewStatus();
			
			if (wasHiding ^ (PrivateRecordVisualStatus == hidePrivateRecords)) 
			//xor on two logical values - mode to open DB has changed
				{
				// Close the application's data file.
				SetApplicationPrefs(0);
				DmCloseDatabase(dbMemoPadRef);
				DmCloseDatabase(dbCryptoPadRef);
			
				mode = (PrivateRecordVisualStatus == hidePrivateRecords) ?
					dmModeReadWrite : (dmModeReadWrite | dmModeShowSecret);
				
				dbCryptoPadRef = OpenOrCreateCryptoDatabase(mode);
				dbMemoPadRef = OpenMemoDatabase(mode);

				// Read the preferences.
				GetApplicationPrefs(&currentRecordID);
			}
			
			//For safety, simply reset the currentRecord
			currentRecord = noRecordSelected;
			MainFormUpdateDisplay(updateTable);
			break;

		case itemOptionsAbout:
			formP = FrmInitForm(frmAboutForm);
			FrmDoDialog(formP);
			FrmDeleteForm(formP);
			break;

	}
}

/*************************************************
* ProgressHandleEvent:
* Get event from the progress
* bar form.
**************************************************
*/
static UInt32 ProgressHandleEvent (Int32 timeout)
{
	EventType event;
	UInt16 error;

	EvtGetEvent (&event, timeout);

	return (event.eType);
}

Boolean MainFormHandleEvent(EventPtr eventP)
{
	Boolean handled = false;

	switch (eventP->eType) {
		/* "New" was chosen while editing a card file: the editor saved+closed and
		 * queued this; now (back on the list) run the New-card-note dialog. */
		case cardNewFileEvent:
			if (gCardMode)
				MainFormNewCardNote();
			handled = true;
			break;

		/* A request to decrypt the record has
		 * been made. Swith to the Edit form
		 * to allow modification of the memo.
		 */
		case memoProcessEvent:
			{
			WinHandle busy;
			ErrFatalDisplayIf(currentPasswordP == 0, "Password shouldn't be NULL");
			busy = CardBusyShow("Decrypting...");
			DecryptRecord(currentRecord, currentPasswordP);
			CardBusyHide(busy);
			FrmGotoForm(frmEditForm);
			handled = true;
			}
			break;

		case catProcessEvent:
			ErrFatalDisplayIf(currentPasswordP == 0, "Password shouldn't be NULL");
			if (eventP->data.generic.datum[1] == catDecrypted)
				EncryptCategory();
			else DecryptCategory();
			handled = true;
			break;

		/* We need to reload the records because a
		 * record has been deleted as a result of
		 * a forgotten password.
		 */
		case frmUpdateEvent:
			handled = MainFormUpdateDisplay (eventP->data.frmUpdate.updateCode);
			break;

		case ctlSelectEvent:
			switch (eventP->data.ctlSelect.controlID) {
				/* Creates a new memo and switch to the edit form */
				case btnMainNewMemo:
					if (gCardMode)
						MainFormNewCardNote();
					else if (CreateRecord())
						FrmGotoForm(frmEditForm);
					handled = true;
					break;

				/* Changes the currently selected category */
				case popMainCategory:
					MainFormSelectCategory();
					handled = true;
					break;

				/* Find bar Clear: exits the filename filter */
				case btnMainClear:
					MainFindClear();
					handled = true;
					break;
			}
			break;

		/* Live Find filter -- no Find button, filters as you type (fldChangedEvent
		 * does not reliably fire per keystroke for a SINGLELINE field, so the real
		 * work happens via the keyDownEvent relay below; this covers programmatic
		 * changes, e.g. FldDelete in MainFindClear). */
		case fldChangedEvent:
			if (eventP->data.fldChanged.fieldID == fldMainFind)
				MainFindFieldChanged();
			break;

		/* A record has been tapped in the table 
		* or drag the memo with the stylus 
		*/
		case tblEnterEvent:
			MainFormDragMemo(eventP);
			handled = true;
			break;

		/* Scrolls the records */
		case sclRepeatEvent:
			MainFormScrollLines(eventP->data.sclRepeat.newValue -
						eventP->data.sclRepeat.value);
			/* Let the system repeat the event */
			break;


		case keyDownEvent:
			// Memo button pressed?
			if (TxtCharIsHardKey(eventP->data.keyDown.modifiers, eventP->data.keyDown.chr)) {
				if (!(eventP->data.keyDown.modifiers & poweredOnKeyMask))
					MainFormNextCategory ();
				handled = true;
			}

			else if (EvtKeydownIsVirtual(eventP)) {
				// Scroll up key presed?
				if (eventP->data.keyDown.chr == vchrPageUp) {
					MainFormScrollPage(winUp);
					handled = true;
				}
	
				// Scroll down key presed?
				else if (eventP->data.keyDown.chr == vchrPageDown) {
					MainFormScrollPage(winDown);
					handled = true;
				}
	
				// Send Data key presed?
				else if (eventP->data.keyDown.chr == vchrSendData) {
					MainFormHandleMenuEvent(itemRecordBeamCategory);
					handled = true;
				}
			}
		
			/* Printable character: dispatch it to fldMainFind ourselves so the
			 * filter (filenames in card mode, titles otherwise) updates live
			 * -- same idiom as Address35's list-view Find bar (fldChangedEvent
			 * doesn't fire per keystroke for a SINGLELINE field). A new
			 * note/memo now only ever comes from an explicit tap on "New" -
			 * typing used to silently create one here, which fought with
			 * trying to filter/search the list by typing. */
			else if (TxtCharIsPrint (eventP->data.keyDown.chr)) {
				FieldPtr fld = (FieldPtr) GetActiveFormObjectPtr(fldMainFind);
				if (fld && FldHandleEvent(fld, eventP)) {
					MainFindFieldChanged();
					handled = true;
				}
			}
			break;

		case menuEvent:
			MainFormHandleMenuEvent(eventP->data.menu.itemID);
			return (true);
			break;

		case menuOpenEvent:
			if (OSVersion >= OS35) {
				MemHandle menuItemH;
				Char * menuItemP;

				menuItemH = DmGetResource('tSTR', strMenuSecurityItem);
				menuItemP = MemHandleLock(menuItemH);

				MenuAddItem (itemOptionsPrefs, itemOptionsSecurity, 'S', menuItemP);

				MemHandleUnlock(menuItemH);
				DmReleaseResource(menuItemH);
			}
			break;

		case menuCmdBarOpenEvent:
			MenuCmdBarAddButton(menuCmdBarOnLeft, BarSecureBitmap, menuCmdBarResultMenuItem, itemOptionsSecurity, 0);
			MenuCmdBarAddButton(menuCmdBarOnLeft, bmpBarFontBitmap, menuCmdBarResultMenuItem, itemOptionsFont, 0);

			// tell the field package to not add buttons automatically; we've done it all ourselves.
			eventP->data.menuCmdBarOpen.preventFieldButtons = true;
		
			// don't set handled to true; this event must fall through to the system.
			break;

		case frmOpenEvent:
			MainFormInit();
			FrmDrawForm(FrmGetActiveForm());
			handled = true;
			break;
	}

	return handled;
}
