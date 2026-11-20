/* ViewList.c -- see ViewList.h. All in sec004. No 32-bit multiply/divide
 * here (libgcc lives in the main segment). */

#include <PalmOS.h>
#include "ViewList.h"
#include "SecDb.h"
#include "Database.h"
#include "CryptoSec35.h"

#define orderDbName		"CryptoNoteOrder"
#define orderDbType		'Ordr'
#define kTitleKey		12

typedef struct {
	UInt32	key;
	UInt16	idx;
	UInt8	src;
	UInt8	pad;
} GEnt;

typedef struct {
	UInt32	crDate;
	UInt32	uid;
} OrdEnt;

static MemHandle	gGH = NULL;		/* every record of every database, in order */
static UInt16		gGCount = 0;
static MemHandle	gVH = NULL;		/* positions in G of the memos shown */
static UInt16		gVCount = 0;
static Boolean		gViewValid = false;

/* sort scratch */
static GEnt*		gSortG;
static Char*		gSortT;

void ViewInvalidate (void) { gViewValid = false; }

void ViewFree (void)
{
	if (gGH) { MemHandleFree (gGH); gGH = NULL; }
	if (gVH) { MemHandleFree (gVH); gVH = NULL; }
	gGCount = gVCount = 0;
	gViewValid = false;
}

/* ------------------------------------------------------------------ *
 *  order table
 * ------------------------------------------------------------------ */
static DmOpenRef OrderOpen (Boolean create) UI4_SECTION;
static DmOpenRef OrderOpen (Boolean create)
{
	LocalID id = DmFindDatabase (0, orderDbName);
	if (! id)
		{
		if (! create) return NULL;
		/* RAM only, not backed up: a manual order is a local view setting */
		if (DmCreateDatabase (0, orderDbName, cryptoPadCreator, orderDbType, false)) return NULL;
		id = DmFindDatabase (0, orderDbName);
		if (! id) return NULL;
		}
	return DmOpenDatabase (0, id, dmModeReadWrite);
}

static void OrderWrite (void) UI4_SECTION;
static void OrderWrite (void)
{
	DmOpenRef	db = OrderOpen (true);
	MemHandle	h;
	UInt16		i, at = 0;
	UInt32		size = (UInt32) gGCount * sizeof (OrdEnt);
	GEnt*		g;
	UInt8*		p;

	if (! db) return;
	if (size == 0) size = sizeof (OrdEnt);
	if (DmNumRecords (db) == 0)
		h = DmNewRecord (db, &at, size);
	else
		{
		h = DmResizeRecord (db, 0, size);
		if (h) h = DmGetRecord (db, 0);
		}
	if (! h) { DmCloseDatabase (db); return; }
	p = (UInt8*) MemHandleLock (h);
	g = (GEnt*) MemHandleLock (gGH);
	for (i = 0; i < gGCount; i++)
		{
		OrdEnt e;
		e.crDate = SrcCrDate (g[i].src);
		e.uid = 0;
		DmRecordInfo (SrcRef (g[i].src), g[i].idx, NULL, &e.uid, NULL);
		DmWrite (p, (UInt32) i * sizeof (OrdEnt), &e, sizeof (e));
		}
	if (gGCount == 0)
		{
		OrdEnt e; e.crDate = 0; e.uid = 0;
		DmWrite (p, 0, &e, sizeof (e));
		}
	MemHandleUnlock (gGH);
	MemHandleUnlock (h);
	DmReleaseRecord (db, 0, false);
	DmCloseDatabase (db);
}

/* ------------------------------------------------------------------ *
 *  sorting (shell sort on an index permutation)
 * ------------------------------------------------------------------ */
static Int16 CmpKey (UInt16 a, UInt16 b) UI4_SECTION;
static Int16 CmpKey (UInt16 a, UInt16 b)
{
	if (gSortG[a].key < gSortG[b].key) return -1;
	if (gSortG[a].key > gSortG[b].key) return 1;
	return (Int16) (a < b ? -1 : (a > b ? 1 : 0));
}

static Int16 CmpTitle (UInt16 a, UInt16 b) UI4_SECTION;
static Int16 CmpTitle (UInt16 a, UInt16 b)
{
	Int16 c = StrCaselessCompare (gSortT + a * kTitleKey, gSortT + b * kTitleKey);
	if (c) return c;
	return CmpKey (a, b);
}

static void ShellSort (UInt16* perm, UInt16 n, Int16 (*cmp) (UInt16, UInt16)) UI4_SECTION;
static void ShellSort (UInt16* perm, UInt16 n, Int16 (*cmp) (UInt16, UInt16))
{
	UInt16 gap, i, j, t;
	for (gap = n / 2; gap > 0; gap /= 2)
		for (i = gap; i < n; i++)
			{
			t = perm[i];
			for (j = i; j >= gap && cmp (perm[j - gap], t) > 0; j -= gap)
				perm[j] = perm[j - gap];
			perm[j] = t;
			}
}

/* Sorted copy of the order table for lookups: {crDate, uid, pos}. */
typedef struct { UInt32 crDate; UInt32 uid; UInt16 pos; } OrdLook;
static OrdLook* gLook;

static Int16 CmpLook (UInt16 a, UInt16 b) UI4_SECTION;
static Int16 CmpLook (UInt16 a, UInt16 b)
{
	if (gLook[a].crDate != gLook[b].crDate) return (Int16) (gLook[a].crDate < gLook[b].crDate ? -1 : 1);
	if (gLook[a].uid != gLook[b].uid) return (Int16) (gLook[a].uid < gLook[b].uid ? -1 : 1);
	return 0;
}

/* Gives every G entry its manual-order key; returns true if the table has
 * to be rewritten (new memos, or memos that are gone). */
static Boolean ManualKeys (GEnt* g, UInt16 n) UI4_SECTION;
static Boolean ManualKeys (GEnt* g, UInt16 n)
{
	DmOpenRef	db = OrderOpen (false);
	MemHandle	h = NULL, lookH = NULL, permH = NULL;
	UInt16		m = 0, i, found = 0;
	OrdLook*	look = NULL;
	UInt16*		perm = NULL;
	Boolean		rewrite = false;

	if (db && DmNumRecords (db) > 0) h = DmQueryRecord (db, 0);
	if (h) m = (UInt16) (MemHandleSize (h) / sizeof (OrdEnt));
	if (m)
		{
		lookH = MemHandleNew ((UInt32) m * sizeof (OrdLook));
		permH = MemHandleNew ((UInt32) m * sizeof (UInt16));
		}
	if (lookH && permH)
		{
		OrdEnt* t = (OrdEnt*) MemHandleLock (h);
		OrdLook* tmp;
		look = (OrdLook*) MemHandleLock (lookH);
		perm = (UInt16*) MemHandleLock (permH);
		for (i = 0; i < m; i++)
			{
			look[i].crDate = t[i].crDate; look[i].uid = t[i].uid; look[i].pos = i;
			perm[i] = i;
			}
		MemHandleUnlock (h);
		gLook = look;
		ShellSort (perm, m, CmpLook);
		/* apply the permutation through a second pass: copy sorted into perm order */
		tmp = (OrdLook*) MemPtrNew ((UInt32) m * sizeof (OrdLook));
		if (tmp)
			{
			for (i = 0; i < m; i++) tmp[i] = look[perm[i]];
			MemMove (look, tmp, (UInt32) m * sizeof (OrdLook));
			MemPtrFree (tmp);
			}
		else m = 0;	/* no memory: fall back to database order */
		}
	else m = 0;

	for (i = 0; i < n; i++)
		{
		UInt32 cr = SrcCrDate (g[i].src), uid = 0;
		Int16 lo = 0, hi = (Int16) m - 1;
		Boolean hit = false;
		DmRecordInfo (SrcRef (g[i].src), g[i].idx, NULL, &uid, NULL);
		while (lo <= hi)
			{
			Int16 mid = (Int16) ((lo + hi) / 2);
			if (look[mid].crDate == cr && look[mid].uid == uid) { hit = true; lo = mid; break; }
			if (look[mid].crDate < cr || (look[mid].crDate == cr && look[mid].uid < uid)) lo = (Int16) (mid + 1);
			else hi = (Int16) (mid - 1);
			}
		if (hit)
			{
			g[i].key = 0x10000UL + look[lo].pos;
			found++;
			}
		else
			{
			/* not in the table: a new memo (top or bottom, as the
			 * "move on top" preference says), or one the table lost (RAM
			 * wipe) -- database order */
			g[i].key = moveRecordOnTop ? (UInt32) i : (0x80000000UL + i);
			rewrite = true;
			}
		}
	if (found != m) rewrite = true;

	if (look) MemHandleUnlock (lookH);
	if (perm) MemHandleUnlock (permH);
	if (lookH) MemHandleFree (lookH);
	if (permH) MemHandleFree (permH);
	if (db) DmCloseDatabase (db);
	return rewrite;
}

static void TitleKey (UInt8 s, UInt16 idx, Char* out) UI4_SECTION;
static void TitleKey (UInt8 s, UInt16 idx, Char* out)
{
	out[0] = 0;
	if (s == memoPadDB)
		{
		MemHandle h = DmQueryRecord (dbMemoPadRef, idx);
		if (h)
			{
			const Char* t = (const Char*) MemHandleLock (h);
			UInt16 k;
			for (k = 0; k < kTitleKey - 1 && t[k] && t[k] != linefeedChr; k++) out[k] = t[k];
			out[k] = 0;
			MemHandleUnlock (h);
			}
		}
	else
		{
		Boolean enc;
		CryptoGetRecordTitle (SrcRef (s), idx, out, kTitleKey, &enc);
		}
	/* titles Sec35 is hiding (locked) sort last, in database order */
	if (StrCompare (out, "******") == 0) { out[0] = (Char) 0xFF; out[1] = 0; }
}

/* ------------------------------------------------------------------ *
 *  filter
 * ------------------------------------------------------------------ */
static Boolean HasTerm (const Char* text) UI4_SECTION;
static Boolean HasTerm (const Char* text)
{
	UInt16 i, j;
	if (gDbFindLen == 0) return true;
	for (i = 0; text[i] && text[i] != linefeedChr; i++)
		{
		for (j = 0; j < gDbFindLen; j++)
			{
			Char c = text[i + j];
			if (c >= 'A' && c <= 'Z') c = (Char) (c + 32);
			if (c != gDbFindLower[j] || c == 0) break;
			}
		if (j == gDbFindLen) return true;
		}
	return false;
}

static Boolean Matches (UInt8 s, UInt16 idx) UI4_SECTION;
static Boolean Matches (UInt8 s, UInt16 idx)
{
	DmOpenRef	db = SrcRef (s);
	UInt16		attr = 0;
	Boolean		m = false;

	DmRecordInfo (db, idx, &attr, NULL, NULL);
	if ((attr & dmRecAttrSecret) && PrivateRecordVisualStatus == maskPrivateRecords)
		return false;
	if (s == memoPadDB)
		{
		MemHandle h = DmQueryRecord (db, idx);
		if (h) { m = HasTerm ((const Char*) MemHandleLock (h)); MemHandleUnlock (h); }
		}
	else
		{
		Char title[81];
		Boolean enc;
		if (CryptoGetRecordTitle (db, idx, title, sizeof (title), &enc))
			m = HasTerm (title);
		}
	return m;
}

/* ------------------------------------------------------------------ *
 *  build
 * ------------------------------------------------------------------ */
void ViewBuild (void)
{
	UInt16		total = 0, n = 0, i;
	UInt8		s;
	GEnt*		g;
	MemHandle	permH, titleH = NULL;
	UInt16*		perm;
	UInt16*		v;
	Boolean		rewrite = false;
	UInt16		inCat = 0, cat;

	if (gViewValid && gGH) return;
	ViewFree ();

	for (s = 0; s < gSrcCount; s++)
		{
		DmOpenRef db = SrcRef (s);
		if (db) total = (UInt16) (total + DmNumRecords (db));
		}
	gGH = MemHandleNew ((UInt32) (total ? total : 1) * sizeof (GEnt));
	if (! gGH) return;
	g = (GEnt*) MemHandleLock (gGH);

	for (s = 0; s < gSrcCount; s++)
		{
		DmOpenRef	db = SrcRef (s);
		UInt16		r = 0;
		if (! db) continue;
		while (n < total && DmQueryNextInCategory (db, &r, dmAllCategories))
			{
			g[n].idx = r; g[n].src = s; g[n].pad = 0;
			g[n].key = (UInt32) n;
			n++;
			r++;
			}
		}
	gGCount = n;

	permH = MemHandleNew ((UInt32) (n ? n : 1) * sizeof (UInt16));
	if (! permH) { MemHandleUnlock (gGH); ViewFree (); return; }
	perm = (UInt16*) MemHandleLock (permH);
	for (i = 0; i < n; i++) perm[i] = i;
	gSortG = g;

	if (currentSortOrder == soAlphabetic)
		{
		titleH = MemHandleNew ((UInt32) (n ? n : 1) * kTitleKey);
		if (titleH)
			{
			gSortT = (Char*) MemHandleLock (titleH);
			for (i = 0; i < n; i++)
				TitleKey (g[i].src, g[i].idx, gSortT + i * kTitleKey);
			ShellSort (perm, n, CmpTitle);
			MemHandleUnlock (titleH);
			MemHandleFree (titleH);
			}
		}
	else
		{
		rewrite = ManualKeys (g, n);
		ShellSort (perm, n, CmpKey);
		}

	/* reorder G by perm */
	{
	GEnt* tmp = (GEnt*) MemPtrNew ((UInt32) (n ? n : 1) * sizeof (GEnt));
	if (tmp)
		{
		for (i = 0; i < n; i++) tmp[i] = g[perm[i]];
		MemMove (g, tmp, (UInt32) n * sizeof (GEnt));
		MemPtrFree (tmp);
		}
	}
	MemHandleUnlock (permH);
	MemHandleFree (permH);

	/* the view: category + Find filter */
	gVH = MemHandleNew ((UInt32) (n ? n : 1) * sizeof (UInt16));
	if (! gVH) { MemHandleUnlock (gGH); ViewFree (); return; }
	v = (UInt16*) MemHandleLock (gVH);
	gVCount = 0;
	/* "All" stays "All" while a memo is open (the edit form sets
	 * currentCategory to the memo's own category) */
	cat = showAllCategories ? dmAllCategories : currentCategory;
	for (i = 0; i < n; i++)
		{
		if (cat != dmAllCategories
				&& SrcRecCategory (g[i].src, g[i].idx) != cat)
			continue;
		inCat++;
		if (gDbFindActive && ! Matches (g[i].src, g[i].idx)) continue;
		v[gVCount++] = i;
		}
	MemHandleUnlock (gVH);
	MemHandleUnlock (gGH);

	memosInCategory = inCat;
	gDbFindMatchCount = gVCount;
	gViewValid = true;

	if (rewrite && currentSortOrder != soAlphabetic) OrderWrite ();
}

UInt16 ViewCount (void)
{
	ViewBuild ();
	return gVCount;
}

Boolean ViewGet (UInt16 pos, UInt8* src, UInt16* idx)
{
	UInt16* v;
	GEnt* g;
	ViewBuild ();
	if (! gViewValid || pos >= gVCount) return false;
	v = (UInt16*) MemHandleLock (gVH);
	g = (GEnt*) MemHandleLock (gGH);
	*src = g[v[pos]].src;
	*idx = g[v[pos]].idx;
	MemHandleUnlock (gGH);
	MemHandleUnlock (gVH);
	return true;
}

Int16 ViewFind (UInt8 src, UInt16 idx)
{
	UInt16* v;
	GEnt* g;
	UInt16 i;
	Int16 r = -1;
	ViewBuild ();
	if (! gViewValid) return -1;
	v = (UInt16*) MemHandleLock (gVH);
	g = (GEnt*) MemHandleLock (gGH);
	for (i = 0; i < gVCount; i++)
		if (g[v[i]].src == src && g[v[i]].idx == idx) { r = (Int16) i; break; }
	MemHandleUnlock (gGH);
	MemHandleUnlock (gVH);
	return r;
}

/* Moves G entry `from` so that it ends up just before G entry `before`
 * (before == gGCount: last). */
static void GMove (UInt16 from, UInt16 before) UI4_SECTION;
static void GMove (UInt16 from, UInt16 before)
{
	GEnt* g = (GEnt*) MemHandleLock (gGH);
	GEnt e = g[from];
	if (before > from)
		{
		MemMove (&g[from], &g[from + 1], (UInt32) (before - from - 1) * sizeof (GEnt));
		g[before - 1] = e;
		}
	else if (before < from)
		{
		MemMove (&g[before + 1], &g[before], (UInt32) (from - before) * sizeof (GEnt));
		g[before] = e;
		}
	MemHandleUnlock (gGH);
}

void ViewOrderMove (UInt16 from, UInt16 to)
{
	UInt16* v;
	UInt16 gf, gt;
	ViewBuild ();
	if (! gViewValid || from >= gVCount || from == to) return;
	v = (UInt16*) MemHandleLock (gVH);
	gf = v[from];
	gt = (to < gVCount) ? v[to] : (UInt16) (v[gVCount - 1] + 1);
	MemHandleUnlock (gVH);
	GMove (gf, gt);
	OrderWrite ();
	gViewValid = false;
}

void ViewOrderToTop (UInt8 src, UInt16 idx)
{
	GEnt* g;
	UInt16 i, at = 0xFFFF;
	if (currentSortOrder == soAlphabetic) return;
	gViewValid = false;		/* the record may be new */
	ViewBuild ();
	if (! gGH) return;
	g = (GEnt*) MemHandleLock (gGH);
	for (i = 0; i < gGCount; i++) if (g[i].src == src && g[i].idx == idx) { at = i; break; }
	MemHandleUnlock (gGH);
	if (at == 0xFFFF) return;
	GMove (at, 0);
	OrderWrite ();
	gViewValid = false;
}

void ViewOrderReplace (UInt32 oldCrDate, UInt32 oldUid, UInt8 newSrc, UInt16 newIdx)
{
	DmOpenRef	db = OrderOpen (false);
	MemHandle	h;
	UInt16		m, i;
	OrdEnt*		t;

	gViewValid = false;
	if (! db) return;
	if (DmNumRecords (db) == 0) { DmCloseDatabase (db); return; }
	h = DmGetRecord (db, 0);
	if (! h) { DmCloseDatabase (db); return; }
	m = (UInt16) (MemHandleSize (h) / sizeof (OrdEnt));
	t = (OrdEnt*) MemHandleLock (h);
	for (i = 0; i < m; i++)
		if (t[i].crDate == oldCrDate && t[i].uid == oldUid)
			{
			OrdEnt e;
			e.crDate = SrcCrDate (newSrc);
			e.uid = 0;
			DmRecordInfo (SrcRef (newSrc), newIdx, NULL, &e.uid, NULL);
			DmWrite (t, (UInt32) i * sizeof (OrdEnt), &e, sizeof (e));
			break;
			}
	MemHandleUnlock (h);
	DmReleaseRecord (db, 0, false);
	DmCloseDatabase (db);
}
