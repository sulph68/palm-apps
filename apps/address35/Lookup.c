/*
 * Lookup.c - Address35's answer to sysAppLaunchCmdLookup (Phone Lookup from
 * Datebook35 / ToDo35 via ../lib/a35lookup.c).
 *
 * Runs as a sublaunch WITH its own globals (the caller passes
 * sysAppLaunchFlagNewGlobals), over the caller's screen. Two steps:
 *   1. a small modal form lists one row per CONTACT that has a phone, email
 *      or social handle, live-filtered by the Find text (pre-filled with the
 *      caller's lookupString);
 *   2. picking a contact pops up just that contact's phones / emails /
 *      social handles; picking one returns "Name - value" in resultStringH.
 *      A contact with a single item skips the popup.
 *
 * Speed on a 16 MHz Palm III (POSE64 cycle profiler, 300-contact encrypted
 * book) drove the layout - the first, one-row-per-phone version took ~3 s
 * to open and ~2.5 s per Find keystroke:
 *   - contact names (clear fields) are read straight from the record
 *     header's sort-key offsets (no unpack, no decryption) into a compact
 *     table; filtering and drawing only touch that table;
 *   - that table is cached in feature memory (names are clear at rest
 *     anyway) and rebuilt only when Address35DB changes (its modification
 *     number, record count, creation date or ID); entries carry the record's
 *     uniqueID, so a re-sort can never map a row to the wrong contact;
 *   - only the picked contact's items are decrypted, field by field, with
 *     one prepared AES key schedule (EncCryptFast) - never the whole record.
 *
 * Everything here is SEC002, including the static helpers (a secondary-
 * segment function calling an untagged same-file function is a wild jump).
 * No static const tables (A5-addressed from a code segment = garbage). See
 * Address35.md section 14 for the sublaunch rules this form follows.
 */
#include <PalmOS.h>
#include "Main.h"
#include "AddressRsc.h"
#include "AddressDB35.h"
#include "enccore.h"
#include "a35lookup.h"
#include "DavView.h"

#define lkMaxContacts	800			/* 800 x 28 bytes = ~22 KB at most */
#define lkNameLen		24			/* list row text (incl. NUL)       */
#define lkMaxItems		16			/* popup rows per contact          */
#define lkItemLen		44			/* popup row text (incl. NUL)      */
#define lkValueLen		64			/* one decrypted value (incl. NUL) */
#define a35HdrSize		10			/* packed-record header (AddressDB35.c, private there) */

typedef struct {
	UInt32	uid;				/* DmRecordInfo uniqueID - resolved at pick time */
	char	name[lkNameLen];
} LkContact;

/* Cached contact table (feature a35Creator/lkCacheFtr, storage heap): this
 * header, then `total` LkContacts. Valid while the stamp matches the DB. */
#define lkCacheFtr		3501
#define lkCacheFtrDav	3502		/* the last CardDAV book's table (any slot; the stamp tells which) */
typedef struct {
	LocalID	dbID;
	UInt32	crDate;
	UInt32	modNum;
	UInt16	nrec;
	UInt16	total;
} LkCacheHdr;

typedef struct {
	UInt8	fieldIdx;
	UInt8	type;
	UInt8	label;
} LkItem;

/* State for the list draw callback (globals are ours in this sublaunch). */
static LkContact *gLkContacts;
static UInt16 *gLkShown;
static char gLkFind[24];			/* Find text - the field is display-only */

static UInt16 LkRd16(const UInt8 *p) SEC002;
static void LkLower(char *s) SEC002;
static Boolean LkContains(const char *name, const char *lowTerm) SEC002;
static void LkNameFromRecord(const UInt8 *r, UInt32 size, char *out, UInt16 outLen) SEC002;
static Boolean LkWanted(UInt8 type) SEC002;
static UInt16 LkFilter(UInt16 total) SEC002;
static void LkDrawRow(Int16 itemNum, RectangleType *bounds, Char **itemsText) SEC002;
static void *LkObj(FormPtr f, UInt16 id) SEC002;
static Boolean LkLocked(DmOpenRef db) SEC002;
static void LkShowFind(FieldPtr find, Boolean draw) SEC002;
static const char *LkLabel(UInt8 type, UInt8 label) SEC002;
static UInt32 LkFieldOffset(const UInt8 *r, UInt8 fieldIdx) SEC002;
static void LkValue(const UInt8 *r, UInt8 fieldIdx, EncKeySchedule *sched, UInt32 uid, char *out) SEC002;
static MemHandle LkPick(DmOpenRef db, UInt32 uid, FormPtr frm, UInt32 *uidOut) SEC002;
static void LkStamp(DmOpenRef db, LkCacheHdr *h) SEC002;
static UInt16 LkBuild(DmOpenRef db, LkContact *out, UInt16 cap) SEC002;
static void LkNameFromDav(const UInt8 *r, UInt32 size, char *out, UInt16 outLen) SEC002;
static UInt16 LkBuildDav(DmOpenRef db, LkContact *out, UInt16 cap) SEC002;
static UInt16 LkLoad(UInt8 src, DmOpenRef localDb, MemHandle *hOut) SEC002;
static MemHandle LkPickDav(UInt8 src, UInt32 wantUid, FormPtr frm) SEC002;
static void LkToast(const char *text) SEC002;
static void LkToastEnd(void) SEC002;

SEC002 static UInt16 LkRd16(const UInt8 *p)
{
	return (UInt16) (((UInt16) p[0] << 8) | p[1]);
}

SEC002 static void LkLower(char *s)
{
	for (; *s; s++)
		if (*s >= 'A' && *s <= 'Z')
			*s = (char) (*s + 32);
}

/* Case-insensitive substring test (term already lower-case). */
SEC002 static Boolean LkContains(const char *name, const char *lowTerm)
{
	char low[lkNameLen];
	if (!lowTerm[0]) return true;
	StrCopy(low, name);
	LkLower(low);
	return (Boolean) (StrStr(low, lowTerm) != NULL);
}

SEC002 static void *LkObj(FormPtr f, UInt16 id)
{
	return FrmGetObjectPtr(f, FrmGetObjectIndex(f, id));
}

/* "First Last", else Company, else "-Unnamed-", from the packed record's
 * header sort-key offsets (clear values, NUL-terminated). */
SEC002 static void LkNameFromRecord(const UInt8 *r, UInt32 size, char *out, UInt16 outLen)
{
	UInt16 offLast = LkRd16(r + 4), offFirst = LkRd16(r + 6), offCompany = LkRd16(r + 8);
	const char *last = (offLast && offLast < size) ? (const char *) r + offLast : "";
	const char *first = (offFirst && offFirst < size) ? (const char *) r + offFirst : "";
	const char *company = (offCompany && offCompany < size) ? (const char *) r + offCompany : "";

	out[0] = '\0';
	if (first[0] || last[0]) {
		StrNCat(out, first, (Int16) outLen);
		if (first[0] && last[0]) StrNCat(out, " ", (Int16) outLen);
		StrNCat(out, last, (Int16) outLen);
	} else
		StrNCat(out, company[0] ? company : "-Unnamed-", (Int16) outLen);
}

SEC002 static Boolean LkWanted(UInt8 type)
{
	return (Boolean) (type == a35fPhone || type == a35fEmail || type == a35fSocial);
}

/* gLkShown = contacts whose name contains the Find text. Memory only. */
SEC002 static UInt16 LkFilter(UInt16 total)
{
	char lt[sizeof(gLkFind)];
	UInt16 i, n = 0;

	StrCopy(lt, gLkFind);
	LkLower(lt);
	for (i = 0; i < total; i++)
		if (LkContains(gLkContacts[i].name, lt))
			gLkShown[n++] = i;
	return n;
}

/* List draw callback: the contact's name, from the table - no DB access. */
SEC002 static void LkDrawRow(Int16 itemNum, RectangleType *b, Char **itemsText)
{
	const char *name = gLkContacts[gLkShown[itemNum]].name;
	Int16 w = b->extent.x, len = (Int16) StrLen(name);
	Boolean fits;

	(void) itemsText;
	FntCharsInWidth(name, &w, &len, &fits);
	WinDrawChars(name, len, b->topLeft.x, b->topLeft.y);
}

/* Same rule as Address35's launch gate (A35LaunchGate): the book needs the
 * Sec35 key if anything is encrypted, or a passcode is set and there are
 * contacts (plaintext ones get sealed on the next unlock). */
SEC002 static Boolean LkLocked(DmOpenRef db)
{
	UInt8 key[ENC_KEY_BYTES];

	if (!A35DbHasEncrypted(db) && !(EncEncryptionActive() && DmNumRecords(db) > 0))
		return false;
	if (!EncGetKey(key, 1000000000UL))
		return true;
	MemSet(key, ENC_KEY_BYTES, 0);
	return false;
}

/* Find is display-only (see the frmLookup comment in Address35En.rcp). */
SEC002 static void LkShowFind(FieldPtr find, Boolean draw)
{
	FldSetTextPtr(find, gLkFind);
	FldRecalculateField(find, false);
	if (draw) FldDrawField(find);
}

/* Short label for the item popup / pasted social handle. Literals only. */
SEC002 static const char *LkLabel(UInt8 type, UInt8 label)
{
	label = (UInt8) (label & a35LabelMask);
	if (type == a35fEmail)
		return "Email";
	if (type == a35fSocial) {
		switch (label) {
		case a35socInstagram:	return "Instagram";
		case a35socFacebook:	return "Facebook";
		case a35socWhatsApp:	return "WhatsApp";
		case a35socX:			return "X";
		case a35socTelegram:	return "Telegram";
		case a35socSignal:		return "Signal";
		default:				return "";		/* Other: the value is "Platform: handle" */
		}
	}
	switch (label) {
	case a35plMobile:	return "Mob";
	case a35plHome:		return "Hom";
	case a35plWork:		return "Wrk";
	case a35plFax:		return "Fax";
	case a35plMain:		return "Main";
	case a35plPager:	return "Pgr";
	default:			return "Oth";
	}
}

/* Byte offset of field fieldIdx's FieldEntry in a packed record. */
SEC002 static UInt32 LkFieldOffset(const UInt8 *r, UInt8 fieldIdx)
{
	UInt32 off = a35HdrSize;
	UInt8 f;
	for (f = 0; f < fieldIdx; f++)
		off += 4 + LkRd16(r + off + 2);
	return off;
}

/* One field's value, decrypted with the prepared schedule when sched != NULL
 * (same nonce as A35GetRecord: the record's uniqueID + the field index). */
SEC002 static void LkValue(const UInt8 *r, UInt8 fieldIdx, EncKeySchedule *sched, UInt32 uid, char *out)
{
	UInt32 off = LkFieldOffset(r, fieldIdx);
	UInt16 ln = LkRd16(r + off + 2);

	if (ln > lkValueLen - 1) ln = lkValueLen - 1;
	MemMove(out, r + off + 4, ln);
	if (sched) EncCryptFast(sched, (UInt8 *) out, ln, uid, fieldIdx);
	out[ln] = '\0';
}

/* Step 2: this contact's phones / emails / social handles. A single item is
 * used as-is; several pop up for a choice. Only listed fields are decrypted.
 * Returns "Name - value" (owner 0), or NULL if dismissed. */
SEC002 static MemHandle LkPick(DmOpenRef db, UInt32 wantUid, FormPtr frm, UInt32 *uidOut)
{
	MemHandle recH, textH = NULL, ptrsH = NULL, outH = NULL;
	const UInt8 *r;
	UInt32 size, uid = 0, off;
	UInt16 count, i, n = 0;
	LkItem items[lkMaxItems];
	char name[40], value[lkValueLen];
	char *rows = NULL, **ptrs = NULL;
	Int16 pick = 0;
	EncKeySchedule sched;
	EncKeySchedule *schedP = NULL;
	UInt16 recIndex;

	if (DmFindRecordByID(db, wantUid, &recIndex) != errNone)
		return NULL;					/* deleted since the table was built */
	recH = DmQueryRecord(db, recIndex);
	if (recH == NULL) return NULL;
	DmRecordInfo(db, recIndex, NULL, &uid, NULL);
	r = (const UInt8 *) MemHandleLock(recH);
	size = MemHandleSize(recH);
	LkNameFromRecord(r, size, name, sizeof(name));
	count = LkRd16(r + 2);

	/* Which fields: types and labels are clear even in an encrypted record. */
	off = a35HdrSize;
	for (i = 0; i < count && off + 4 <= size && n < lkMaxItems; i++) {
		UInt16 ln = LkRd16(r + off + 2);
		if (LkWanted(r[off]) && ln > 0) {
			items[n].fieldIdx = (UInt8) i;
			items[n].type = r[off];
			items[n].label = r[off + 1];
			n++;
		}
		off += 4 + ln;
	}
	if (n == 0) { MemHandleUnlock(recH); return NULL; }

	if (r[1] & a35MiscEncrypted) {
		UInt8 key[ENC_KEY_BYTES];
		if (!EncGetKey(key, 1000000000UL)) { MemHandleUnlock(recH); return NULL; }
		EncKeyScheduleInit(&sched, key);
		MemSet(key, ENC_KEY_BYTES, 0);
		schedP = &sched;
	}

	if (n > 1) {
		/* Popup rows "Mob  +1 555 0101" / "Email  a@b.c" / "Instagram  @x". */
		ListPtr lst = (ListPtr) LkObj(frm, lstLookupItems);
		textH = MemHandleNew((UInt32) n * lkItemLen);
		ptrsH = MemHandleNew((UInt32) n * sizeof(char *));
		if (textH == NULL || ptrsH == NULL) { pick = -1; goto done; }
		rows = (char *) MemHandleLock(textH);
		ptrs = (char **) MemHandleLock(ptrsH);
		for (i = 0; i < n; i++) {
			const char *lbl = LkLabel(items[i].type, items[i].label);
			LkValue(r, items[i].fieldIdx, schedP, uid, value);
			ptrs[i] = rows + (UInt32) i * lkItemLen;
			StrNCopy(ptrs[i], lbl, lkItemLen - 1);
			ptrs[i][lkItemLen - 1] = '\0';
			if (lbl[0]) StrNCat(ptrs[i], "  ", lkItemLen);
			StrNCat(ptrs[i], value, lkItemLen);
		}
		LstSetListChoices(lst, ptrs, n);
		LstSetHeight(lst, (Int16) (n < 8 ? n : 8));
		LstSetSelection(lst, 0);
		pick = LstPopupList(lst);		/* noListSelection (-1) if dismissed */
		LstSetListChoices(lst, NULL, 0);
	}

	if (pick >= 0) {
		/* Social handles keep their platform: "Name - Instagram @handle". */
		const char *lbl = (items[pick].type == a35fSocial) ? LkLabel(a35fSocial, items[pick].label) : "";
		LkValue(r, items[pick].fieldIdx, schedP, uid, value);
		outH = MemHandleNew((UInt32) (StrLen(name) + 3 + StrLen(lbl) + 1 + StrLen(value) + 1));
		if (outH) {
			char *s = (char *) MemHandleLock(outH);
			StrCopy(s, name);
			StrCat(s, " - ");
			if (lbl[0]) { StrCat(s, lbl); StrCat(s, " "); }
			StrCat(s, value);
			MemHandleUnlock(outH);
			/* Owned by the system, not this sublaunch - the OS frees a
			 * sublaunched app's own chunks when it returns. */
			MemHandleSetOwner(outH, 0);
			*uidOut = uid;
		}
	}

done:
	MemSet(value, sizeof(value), 0);
	if (schedP) EncKeyScheduleWipe(schedP);
	if (rows) { MemSet(rows, (Int32) n * lkItemLen, 0); MemHandleUnlock(textH); }
	if (textH) MemHandleFree(textH);
	if (ptrs) MemHandleUnlock(ptrsH);
	if (ptrsH) MemHandleFree(ptrsH);
	MemHandleUnlock(recH);
	return outH;
}

/* What identifies this exact state of Address35DB. */
SEC002 static void LkStamp(DmOpenRef db, LkCacheHdr *h)
{
	UInt16 card = 0;
	LocalID id = 0;

	MemSet(h, sizeof(*h), 0);
	DmOpenDatabaseInfo(db, &id, NULL, NULL, &card, NULL);
	h->dbID = id;
	DmDatabaseInfo(card, id, NULL, NULL, NULL, &h->crDate, NULL, NULL, &h->modNum,
		NULL, NULL, NULL, NULL);
	h->nrec = DmNumRecords(db);
}

/* One entry per contact with something to paste, in the book's sort order.
 * Clear header + field types only: no unpack, no decryption. */
SEC002 static UInt16 LkBuild(DmOpenRef db, LkContact *out, UInt16 cap)
{
	UInt16 i, nrec = DmNumRecords(db), total = 0;

	for (i = 0; i < nrec && total < cap; i++) {
		MemHandle h = DmQueryRecord(db, i);
		const UInt8 *r;
		UInt32 size, off;
		UInt16 count, f;
		Boolean any = false;
		if (h == NULL) continue;
		r = (const UInt8 *) MemHandleLock(h);
		size = MemHandleSize(h);
		if (size >= a35HdrSize) {
			count = LkRd16(r + 2);
			off = a35HdrSize;
			for (f = 0; f < count && off + 4 <= size && !any; f++) {
				if (LkWanted(r[off]) && LkRd16(r + off + 2) > 0) any = true;
				off += 4 + LkRd16(r + off + 2);
			}
			if (any) {
				DmRecordInfo(db, i, NULL, &out[total].uid, NULL);
				LkNameFromRecord(r, size, out[total].name, lkNameLen);
				total++;
			}
		}
		MemHandleUnlock(h);
	}
	return total;
}

/* "Loading..." box while a table is built: this file's own copy of the text-only toast (the
 * shared one, A35BusyBegin, is in another segment, which a lookup sublaunch must not call -
 * the jump goes wild). Drawn straight on the display, the pixels under it saved and
 * restored by LkToastEnd, so no form or window outlives this sublaunch. */
static WinHandle gLkToast;
static RectangleType gLkToastR;

SEC002 static void LkToast(const char *text)
{
	RectangleType r;
	WinHandle prevDraw = WinGetDrawWindow();
	Int16 w, len = (Int16) StrLen(text);
	Err err;
	FontID prev;

	WinSetDrawWindow(WinGetDisplayWindow());
	prev = FntSetFont(boldFont);
	w = FntCharsWidth((Char *) text, len);
	r.extent.x = (Int16) (w + 20);
	r.extent.y = 24;
	r.topLeft.x = (Int16) ((160 - r.extent.x) / 2);
	r.topLeft.y = 68;
	gLkToastR.topLeft.x = (Int16) (r.topLeft.x - 4);
	gLkToastR.topLeft.y = (Int16) (r.topLeft.y - 4);
	gLkToastR.extent.x = (Int16) (r.extent.x + 8);
	gLkToastR.extent.y = (Int16) (r.extent.y + 8);
	gLkToast = WinSaveBits(&gLkToastR, &err);
	WinEraseRectangle(&r, 3);
	WinDrawRectangleFrame(dialogFrame, &r);
	WinDrawChars((Char *) text, len, (Int16) (r.topLeft.x + 10), (Int16) (r.topLeft.y + 6));
	FntSetFont(prev);
	WinSetDrawWindow(prevDraw);
}

SEC002 static void LkToastEnd(void)
{
	if (gLkToast) {
		WinHandle prevDraw = WinGetDrawWindow();
		WinSetDrawWindow(WinGetDisplayWindow());
		WinRestoreBits(gLkToast, gLkToastR.topLeft.x, gLkToastR.topLeft.y);
		WinSetDrawWindow(prevDraw);
		gLkToast = NULL;
	}
}

/* CardDAV record (DavView.c layout): version, flags, listLen, list value, then three
 * readable sort names (u8 length + bytes): last, first, company. */
SEC002 static void LkNameFromDav(const UInt8 *r, UInt32 size, char *out, UInt16 outLen)
{
	char nm[3][40];
	UInt32 pos;
	UInt16 f, n;

	nm[0][0] = nm[1][0] = nm[2][0] = '\0';
	pos = (size >= 3) ? (UInt32) 3 + r[2] : size;
	for (f = 0; f < 3 && pos < size; f++) {
		n = r[pos];
		if (pos + 1 + n > size) break;
		if (n > 39) n = 39;
		MemMove(nm[f], r + pos + 1, n);
		nm[f][n] = '\0';
		pos += 1 + r[pos];
	}
	out[0] = '\0';
	if (nm[1][0] || nm[0][0]) {
		StrNCat(out, nm[1], (Int16) outLen);
		if (nm[1][0] && nm[0][0]) StrNCat(out, " ", (Int16) outLen);
		StrNCat(out, nm[0], (Int16) outLen);
	} else
		StrNCat(out, nm[2][0] ? nm[2] : "-Unnamed-", (Int16) outLen);
}

/* One entry per CardDAV contact with a phone or email (the record header's list value is
 * the first phone, else the first email, so a non-empty one says there is something to
 * paste), in the book's order. Names are clear: no decryption, no parsing. */
SEC002 static UInt16 LkBuildDav(DmOpenRef db, LkContact *out, UInt16 cap)
{
	UInt16 i, nrec = DmNumRecords(db), total = 0;

	for (i = 0; i < nrec && total < cap; i++) {
		MemHandle h = DmQueryRecord(db, i);
		const UInt8 *r;
		UInt32 size;
		UInt16 attr;
		if (h == NULL) continue;
		if (DmRecordInfo(db, i, &attr, &out[total].uid, NULL) != errNone || (attr & dmRecAttrDelete)) continue;
		r = (const UInt8 *) MemHandleLock(h);
		size = MemHandleSize(h);
		if (size >= 3 && r[0] == 2 && (r[2] > 0 || (r[1] & davFlagSocial))) {
			LkNameFromDav(r, size, out[total].name, lkNameLen);
			total++;
		}
		MemHandleUnlock(h);
	}
	return total;
}

/* gLkContacts for source src (0 = Local, else CardDAV slot 1..8): the cached table if that
 * book hasn't changed, else built now (behind a "Loading..." box) and cached. *hOut gets
 * the heap block backing a freshly built table (caller frees), NULL for a cached one. */
SEC002 static UInt16 LkLoad(UInt8 src, DmOpenRef localDb, MemHandle *hOut)
{
	DmOpenRef db = localDb;
	LkCacheHdr stamp, *cache = NULL;
	UInt32 ftrVal;
	void *ftrP;
	UInt16 ftr = src ? lkCacheFtrDav : lkCacheFtr, total = 0, cap;

	*hOut = NULL;
	gLkContacts = NULL;
	if (src && (db = DavOpen((UInt8) (src - 1))) == NULL)
		return 0;
	LkStamp(db, &stamp);
	if (FtrGet(a35Creator, ftr, &ftrVal) == errNone) {
		cache = (LkCacheHdr *) ftrVal;
		if (cache->dbID != stamp.dbID || cache->crDate != stamp.crDate ||
			cache->modNum != stamp.modNum || cache->nrec != stamp.nrec)
			cache = NULL;
	}
	if (cache) {
		gLkContacts = (LkContact *) (cache + 1);
		total = cache->total;
	} else {
		cap = (stamp.nrec < lkMaxContacts) ? stamp.nrec : lkMaxContacts;
		*hOut = MemHandleNew((UInt32) (cap ? cap : 1) * sizeof(LkContact));
		if (*hOut != NULL) {
			LkToast("Loading...");
			gLkContacts = (LkContact *) MemHandleLock(*hOut);
			total = src ? LkBuildDav(db, gLkContacts, cap) : LkBuild(db, gLkContacts, cap);
			stamp.total = total;
			if (FtrGet(a35Creator, ftr, &ftrVal) == errNone)
				FtrPtrFree(a35Creator, ftr);
			if (FtrPtrNew(a35Creator, ftr,
					(UInt32) sizeof(LkCacheHdr) + (UInt32) total * sizeof(LkContact), &ftrP) == errNone) {
				DmWrite(ftrP, 0, &stamp, sizeof(stamp));
				if (total)
					DmWrite(ftrP, sizeof(stamp), gLkContacts, (UInt32) total * sizeof(LkContact));
			}
			LkToastEnd();
		}
	}
	if (src) DmCloseDatabase(db);
	return total;
}

/* LkPick for a CardDAV contact: the phones and emails come from its (decrypted) vCard text
 * (DavItems), the name from the record header. */
SEC002 static MemHandle LkPickDav(UInt8 src, UInt32 wantUid, FormPtr frm)
{
	DmOpenRef db = DavOpen((UInt8) (src - 1));
	MemHandle h, outH = NULL, textH = NULL, ptrsH = NULL;
	DavItem *it;
	char name[40];
	UInt16 ri;
	UInt8 n = 0, i;
	Int16 pick = 0, res;
	char *rows = NULL, **ptrs = NULL;

	if (db == NULL) return NULL;
	if (DmFindRecordByID(db, wantUid, &ri) != errNone || (h = DmQueryRecord(db, ri)) == NULL) {
		DmCloseDatabase(db);
		return NULL;					/* gone since the table was built */
	}
	{
		const UInt8 *r = (const UInt8 *) MemHandleLock(h);
		LkNameFromDav(r, MemHandleSize(h), name, sizeof(name));
		MemHandleUnlock(h);
	}
	DmCloseDatabase(db);
	it = (DavItem *) MemPtrNew(lkMaxItems * sizeof(DavItem));
	if (it == NULL) return NULL;
	res = DavItems((UInt8) (src - 1), ri, it, lkMaxItems, &n);
	if (res == 1) {
		FrmAlert(altDavLocked);
	} else if (res != 0 || n == 0) {
		FrmCustomAlert(altDavInfo, "No phone number or email.", "", "");
	} else {
		if (n > 1) {
			ListPtr lst = (ListPtr) LkObj(frm, lstLookupItems);
			textH = MemHandleNew((UInt32) n * lkItemLen);
			ptrsH = MemHandleNew((UInt32) n * sizeof(char *));
			if (textH == NULL || ptrsH == NULL) {
				pick = -1;
			} else {
				rows = (char *) MemHandleLock(textH);
				ptrs = (char **) MemHandleLock(ptrsH);
				for (i = 0; i < n; i++) {
					ptrs[i] = rows + (UInt32) i * lkItemLen;
					StrNCopy(ptrs[i], it[i].label, lkItemLen - 1);
					ptrs[i][lkItemLen - 1] = '\0';
					if (it[i].label[0]) StrNCat(ptrs[i], "  ", lkItemLen);
					StrNCat(ptrs[i], it[i].value, lkItemLen);
				}
				LstSetListChoices(lst, ptrs, n);
				LstSetHeight(lst, (Int16) (n < 8 ? n : 8));
				LstSetSelection(lst, 0);
				pick = LstPopupList(lst);
				LstSetListChoices(lst, NULL, 0);
			}
		}
		if (pick >= 0) {
			/* a social handle keeps its network: "Name - Instagram @handle" (as for local contacts) */
			const char *lbl = (it[pick].kind == 1) ? it[pick].label : "";
			outH = MemHandleNew((UInt32) (StrLen(name) + 3 + StrLen(lbl) + 1 + StrLen(it[pick].value) + 1));
			if (outH) {
				char *s = (char *) MemHandleLock(outH);
				StrCopy(s, name);
				StrCat(s, " - ");
				if (lbl[0]) { StrCat(s, lbl); StrCat(s, " "); }
				StrCat(s, it[pick].value);
				MemHandleUnlock(outH);
				MemHandleSetOwner(outH, 0);		/* survives this sublaunch, as in LkPick */
			}
		}
	}
	if (rows) { MemSet(rows, (Int32) n * lkItemLen, 0); MemHandleUnlock(textH); }
	if (textH) MemHandleFree(textH);
	if (ptrs) MemHandleUnlock(ptrsH);
	if (ptrsH) MemHandleFree(ptrsH);
	MemSet(it, lkMaxItems * sizeof(DavItem), 0);
	MemPtrFree(it);
	return outH;
}

SEC002 UInt32 A35PhoneLookup(AddrLookupParamsPtr params)
{
	DmOpenRef db;
	MemHandle contactsH = NULL, shownH;
	UInt16 total = 0, shown;
	FormPtr frm;
	ListPtr lst;
	FieldPtr find;
	char *srcText;
	char *srcPtrs[9];
	UInt8 srcSlot[9], v = 0;
	Int16 nsrc = 1, cur = 0, i;
	UInt16 vsz = sizeof(v);
	Boolean done = false;

	if (params == NULL || A35GetDatabase(&db, dmModeReadOnly) != errNone)
		return 0;
	if (LkLocked(db)) {
		DmCloseDatabase(db);
		params->uniqueID = a35LookupLockedID;	/* the caller hands off to Sec35's unlock screen */
		return 0;
	}

	/* Sources: Local, then every CardDAV / iCloud book (the popup next to Cancel). The last
	 * pick is remembered. */
	srcText = (char *) MemPtrNew(9 * 36);
	shownH = MemHandleNew((UInt32) lkMaxContacts * sizeof(UInt16));
	if (srcText == NULL || shownH == NULL) {
		if (srcText) MemPtrFree(srcText);
		if (shownH) MemHandleFree(shownH);
		DmCloseDatabase(db);
		return 0;
	}
	gLkShown = (UInt16 *) MemHandleLock(shownH);
	StrCopy(srcText, "Local");
	srcPtrs[0] = srcText;
	srcSlot[0] = 0;
	if (PrefGetAppPreferences(a35PrefCreator, a35LookupPrefID, &v, &vsz, true) == noPreferenceFound)
		v = 0;
	for (i = 0; i < davMaxSlots; i++) {
		char *q = srcText + nsrc * 36;
		if (DavSlotLabel((UInt8) i, q) != davSlotOk) continue;
		srcPtrs[nsrc] = q;
		srcSlot[nsrc] = (UInt8) (i + 1);
		if (v == i + 1) cur = nsrc;
		nsrc++;
	}

	total = LkLoad(srcSlot[cur], db, &contactsH);
	if (total == 0 && nsrc == 1) {
		FrmAlert(altLookupNone);
	} else {
		frm = FrmInitForm(frmLookup);
		lst = (ListPtr) LkObj(frm, lstLookup);
		find = (FieldPtr) LkObj(frm, fldLookupFind);
		if (params->title)
			FrmSetTitle(frm, params->title);
		if (nsrc > 1) {
			ListPtr sl = (ListPtr) LkObj(frm, lstLookupSrc);
			LstSetListChoices(sl, srcPtrs, nsrc);
			LstSetHeight(sl, nsrc);
			LstSetSelection(sl, cur);
			CtlSetLabel((ControlPtr) LkObj(frm, popLookupSrc), srcPtrs[cur]);
		} else {
			FrmHideObject(frm, FrmGetObjectIndex(frm, popLookupSrc));
		}
		FrmSetActiveForm(frm);
		StrNCopy(gLkFind, params->lookupString, sizeof(gLkFind) - 1);
		gLkFind[sizeof(gLkFind) - 1] = '\0';
		shown = LkFilter(total);
		if (shown == 0 && gLkFind[0]) {			/* nothing matched: show everyone */
			gLkFind[0] = '\0';
			shown = LkFilter(total);
		}
		LkShowFind(find, false);
		LstSetDrawFunction(lst, LkDrawRow);
		LstSetListChoices(lst, NULL, shown);
		LstSetSelection(lst, shown ? 0 : -1);
		FrmDrawForm(frm);
		/* the full-screen form's border: a 2-pixel line around the screen edge */
		WinDrawLine(0, 0, 0, 159);
		WinDrawLine(1, 0, 1, 159);
		WinDrawLine(158, 0, 158, 159);
		WinDrawLine(159, 0, 159, 159);
		WinDrawLine(0, 158, 159, 158);
		WinDrawLine(0, 159, 159, 159);

		while (!done) {
			EventType ev;
			Int16 sel = -1;

			EvtGetEvent(&ev, evtWaitForever);
			if (ev.eType == appStopEvent) {
				EvtAddEventToQueue(&ev);
				break;
			}
			if (ev.eType == popSelectEvent && ev.data.popSelect.listID == lstLookupSrc) {
				Int16 ns = ev.data.popSelect.selection;
				if (ns >= 0 && ns < nsrc && ns != cur) {
					cur = ns;
					CtlSetLabel((ControlPtr) LkObj(frm, popLookupSrc), srcPtrs[cur]);
					if (contactsH) { MemHandleUnlock(contactsH); MemHandleFree(contactsH); contactsH = NULL; }
					total = LkLoad(srcSlot[cur], db, &contactsH);
					PrefSetAppPreferences(a35PrefCreator, a35LookupPrefID, 1, &srcSlot[cur], 1, true);
					shown = LkFilter(total);
					LstSetListChoices(lst, NULL, shown);
					LstSetSelection(lst, shown ? 0 : -1);
					LstDrawList(lst);
				}
				continue;
			}
			if (ev.eType == lstSelectEvent && ev.data.lstSelect.listID == lstLookup)
				sel = ev.data.lstSelect.selection;		/* tapping a contact picks it */
			else if (ev.eType == ctlSelectEvent) {
				if (ev.data.ctlSelect.controlID == btnLookupPaste)
					sel = LstGetSelection(lst);
				else if (ev.data.ctlSelect.controlID == btnLookupCancel) {
					done = true;
					continue;
				}
			}
			if (sel >= 0 && sel < (Int16) shown) {
				UInt32 wantUid = gLkContacts[gLkShown[sel]].uid;
				MemHandle h = srcSlot[cur] ? LkPickDav(srcSlot[cur], wantUid, frm) :
					LkPick(db, wantUid, frm, &params->uniqueID);
				if (h) { params->resultStringH = h; done = true; }
				continue;		/* popup dismissed: stay on the contact list */
			}
			if (ev.eType == keyDownEvent && EvtKeydownIsVirtual(&ev) &&
				(ev.data.keyDown.chr == vchrPageUp || ev.data.keyDown.chr == vchrPageDown)) {
				LstScrollList(lst, ev.data.keyDown.chr == vchrPageUp ? winUp : winDown,
					(Int16) (LstGetVisibleItems(lst) - 1));
				continue;
			}
			if (ev.eType == keyDownEvent && !EvtKeydownIsVirtual(&ev)) {
				/* Find: typed characters and backspace, live-filtering the list */
				WChar c = ev.data.keyDown.chr;
				UInt16 n = (UInt16) StrLen(gLkFind);
				Boolean changed = false;
				if (c == chrBackspace) {
					if (n) { gLkFind[n - 1] = '\0'; changed = true; }
				} else if (c >= ' ' && c < 0x100 && n < sizeof(gLkFind) - 1) {
					gLkFind[n] = (char) c; gLkFind[n + 1] = '\0'; changed = true;
				}
				if (changed) {
					LkShowFind(find, true);
					shown = LkFilter(total);
					LstSetListChoices(lst, NULL, shown);
					LstSetSelection(lst, shown ? 0 : -1);
					LstDrawList(lst);
				}
				continue;
			}
			if (SysHandleEvent(&ev)) continue;
			FrmDispatchEvent(&ev);
		}

		FldSetTextPtr(find, NULL);
		FrmEraseForm(frm);
		FrmDeleteForm(frm);
		/* The CALLER re-activates its own form after we return (a35lookup.c):
		 * done here, the Form Manager re-shows the caller field's insertion
		 * point inside this sublaunch, allocating its saved-bits window under
		 * our owner - freed on return, then freed again by the caller's next
		 * cursor blink ("MemoryMgr.c, Invalid chunk ptr"). */
	}

	if (contactsH) { MemHandleUnlock(contactsH); MemHandleFree(contactsH); }
	MemHandleUnlock(shownH);
	MemHandleFree(shownH);
	MemPtrFree(srcText);
	gLkContacts = NULL;
	gLkShown = NULL;
	DmCloseDatabase(db);
	return 0;
}
