/*
 * ContactDb.c - per-slot contacts databases; layout in ContactDb.h.
 * All SEC003, like DAVDb.c (cddv_seg.h: a SEC00x function must not call an
 * untagged one in the same file).
 */
#include "ContactDb.h"
#include "CDDVSec35.h"
#include "DAVDb.h"		/* ContactDbCleanup: a slot's DateBook35DAV rows */

#define kGroupMarker	0xFE		/* temp database only: a CDDV_GROUP_ITEM body */
#define kMaxNames		48			/* distinct group/category names considered */
#define kNameLen		dmCategoryLength	/* 15 chars + NUL */
#define kMaxGroupRecs	64
#define kBdaNameMax		31

/* one birthday/anniversary while Address35DAVBDA is built */
typedef struct {
	UInt8	month, day, slot, dateIdx;
	UInt16	rec;
} BdaEnt;

static DmOpenRef gContactDB = NULL;
static UInt8 gContactSlot;
static UInt16 gContactStaged = 0;		/* slots with a temp database waiting for CommitStaged */

static void ContactDbName(UInt8 slotIdx, Boolean temp, char *name) SEC003;
static UInt16 ContactStrAt(const UInt8 *p, UInt16 len, UInt16 pos, const UInt8 **s, UInt16 *slen) SEC003;
static UInt16 ContactBodyAt(const UInt8 *r, UInt16 len) SEC003;
static UInt16 ContactKey(const UInt8 *r, UInt16 len, Int16 kind, char *out) SEC003;
static Boolean ContactFindProp(const UInt8 *t, UInt16 tl, const char *name, const UInt8 **v, UInt16 *vl) SEC003;
static UInt16 ContactComponent(const UInt8 *v, UInt16 vl, UInt16 idx, UInt8 *out, UInt16 cap) SEC003;
static Int16 ContactSortArg(void) SEC003;
static void ContactSortDb(DmOpenRef db, Int16 arg) SEC003;
static Int16 ContactCompare(void *rec1, void *rec2, Int16 other,
	SortRecordInfoPtr r1, SortRecordInfoPtr r2, MemHandle appInfoH) SEC003;
static UInt16 ContactDatesAt(const UInt8 *rec, UInt16 len) SEC003;
static Boolean ContactNameEq(const char *a, const UInt8 *b, UInt16 lb) SEC003;
static Int16 ContactNameIndex(char *names, UInt16 n, const UInt8 *s, UInt16 len) SEC003;
static void ContactAddName(char *names, UInt16 *n, const UInt8 *s, UInt16 len) SEC003;
static void ContactApplyCategories(DmOpenRef db) SEC003;
static UInt8 *ContactAppInfo(DmOpenRef db) SEC003;
static void ContactRebuildBda(void) SEC003;

static void ContactDbName(UInt8 slotIdx, Boolean temp, char *name)
{
	StrPrintF(name, temp ? "%st%d" : "%s%d", cddvContactDbPrefix, (Int16) (slotIdx + 1));
}

/* string at pos (u16 BE length + bytes): returns the position after it, or
 * 0xFFFF if it doesn't fit */
static UInt16 ContactStrAt(const UInt8 *p, UInt16 len, UInt16 pos, const UInt8 **s, UInt16 *slen)
{
	UInt16 n;
	if ((UInt32) pos + 2 > len) return 0xFFFF;
	n = (UInt16) (((UInt16) p[pos] << 8) | p[pos + 1]);
	if ((UInt32) pos + 2 + n > len) return 0xFFFF;
	if (s) *s = p + pos + 2;
	if (slen) *slen = n;
	return (UInt16) (pos + 2 + n);
}

/* where the CDDV_CONTACT_ITEM body starts in a stored v2 record: after
 * version, flags, the list value and the three sort names; or 0xFFFF */
static UInt16 ContactBodyAt(const UInt8 *r, UInt16 len)
{
	UInt16 pos, k;
	if (len < 3 || r[0] != cddvContactRecVersion) return 0xFFFF;
	pos = (UInt16) (3 + r[2]);							/* list value */
	for (k = 0; k < 3; k++) {							/* last, first, company */
		if (pos >= len) return 0xFFFF;
		pos = (UInt16) (pos + 1 + r[pos]);
	}
	return pos <= len ? pos : 0xFFFF;
}

/* sort name kind (a35sortLast 0, First 1, Company 2) as a C string in out
 * (64 bytes); returns its length, 0 = absent */
static UInt16 ContactKey(const UInt8 *r, UInt16 len, Int16 kind, char *out)
{
	UInt16 pos, k, n;
	out[0] = '\0';
	if (len < 3 || r[0] != cddvContactRecVersion) return 0;
	pos = (UInt16) (3 + r[2]);
	for (k = 0; k < (UInt16) kind; k++) {
		if (pos >= len) return 0;
		pos = (UInt16) (pos + 1 + r[pos]);
	}
	if (pos >= len) return 0;
	n = r[pos];
	if (n > cddvContactKeyMax || (UInt32) pos + 1 + n > len) return 0;
	MemMove(out, r + pos + 1, n);
	out[n] = '\0';
	return n;
}

/* Address35's own order (AddressDB35.c A35ComparePackedRecords): the keys in
 * preference order (primary, secondary, the rest), a missing key first,
 * caseless then exact; ties by uniqueID so re-sorts are stable. other =
 * primary | secondary << 8. */
static Int16 ContactCompare(void *rec1, void *rec2, Int16 other,
	SortRecordInfoPtr r1, SortRecordInfoPtr r2, MemHandle appInfoH)
{
	const UInt8 *a = (const UInt8 *) rec1, *b = (const UInt8 *) rec2;
	UInt16 la = (UInt16) MemPtrSize(rec1), lb = (UInt16) MemPtrSize(rec2);
	Int16 order[3], n = 0, i, res, pri = (Int16) (other & 0xFF), sec = (Int16) ((other >> 8) & 0xFF);
	Boolean used[3];
	char ka[cddvContactKeyMax + 1], kb[cddvContactKeyMax + 1];

	used[0] = used[1] = used[2] = false;
	if (pri >= 0 && pri <= 2) { order[n++] = pri; used[pri] = true; }
	if (sec >= 0 && sec <= 2 && !used[sec]) { order[n++] = sec; used[sec] = true; }
	for (i = 0; i < 3; i++) if (!used[i]) order[n++] = i;
	for (i = 0; i < n; i++) {
		UInt16 na = ContactKey(a, la, order[i], ka);
		UInt16 nb = ContactKey(b, lb, order[i], kb);
		if (na == 0 && nb == 0) continue;
		if (na == 0) return -1;
		if (nb == 0) return 1;
		res = StrCaselessCompare(ka, kb);
		if (res == 0) res = StrCompare(ka, kb);
		if (res != 0) return res;
	}
	return (Int16) MemCmp(r1->uniqueID, r2->uniqueID, 3);
}

/* Address35's sort preference: Address35DB's AppInfo, primarySort and
 * secondarySort right after the standard category block (AddressDB35.h
 * A35AppInfoType: offsets 276, 277; not encrypted). Last/First if absent. */
static Int16 ContactSortArg(void)
{
	LocalID id = DmFindDatabase(0, "Address35DB");
	DmOpenRef db;
	UInt8 pri = 0, sec = 1, *ai;

	if (id && (db = DmOpenDatabase(0, id, dmModeReadOnly)) != NULL) {
		ai = ContactAppInfo(db);
		if (ai) {
			if (MemPtrSize(ai) > 277) { pri = ai[276]; sec = ai[277]; }
			MemPtrUnlock(ai);
		}
		DmCloseDatabase(db);
	}
	if (pri > 2) pri = 0;
	if (sec > 2) sec = 1;
	return (Int16) (pri | (sec << 8));
}

static void ContactSortDb(DmOpenRef db, Int16 arg)
{
	DmQuickSort(db, ContactCompare, arg);
}

/* first property `name` in the plain trimmed text (lines "NAME;params:value"
 * or "NAME:value"): its value, still escaped */
static Boolean ContactFindProp(const UInt8 *t, UInt16 tl, const char *name, const UInt8 **v, UInt16 *vl)
{
	UInt16 i = 0, nl = (UInt16) StrLen(name);
	while (i < tl) {
		UInt16 e = i, k, c;
		Boolean q = false, match = true;
		while (e < tl && t[e] != '\n') e++;
		for (k = 0; k < nl; k++) {
			UInt8 x = (i + k < e) ? t[i + k] : 0;
			if (x >= 'a' && x <= 'z') x = (UInt8) (x - 32);
			if (x != (UInt8) name[k]) { match = false; break; }
		}
		if (match && i + nl < e && (t[i + nl] == ':' || t[i + nl] == ';')) {
			for (c = (UInt16) (i + nl); c < e; c++) {
				if (t[c] == '"') q = (Boolean) !q;
				else if (t[c] == ':' && !q) {
					*v = t + c + 1;
					*vl = (UInt16) (e - c - 1);
					return true;
				}
			}
		}
		i = (UInt16) (e + 1);
	}
	return false;
}

/* component idx (';'-separated) of a value, unescaped into out (cap incl.
 * room for nothing more - not NUL-terminated); returns its length */
static UInt16 ContactComponent(const UInt8 *v, UInt16 vl, UInt16 idx, UInt8 *out, UInt16 cap)
{
	UInt16 i = 0, field = 0, o = 0;
	while (i < vl && field < idx) {
		if (v[i] == '\\' && i + 1 < vl) i += 2;
		else if (v[i] == ';') { field++; i++; }
		else i++;
	}
	if (field < idx) return 0;
	while (i < vl && v[i] != ';' && o < cap) {
		UInt8 c = v[i++];
		if (c == '\\' && i < vl) {
			c = v[i++];
			if (c == 'n' || c == 'N') c = ' ';
		}
		out[o++] = c;
	}
	while (o && out[o - 1] == ' ') o--;
	return o;
}

SEC003 Err ContactDbBegin(UInt8 slotIdx)
{
	char name[dmDBNameLength];
	LocalID id;

	ContactDbAbort();
	gContactSlot = slotIdx;
	gContactStaged &= (UInt16) ~(1 << slotIdx);	/* a newer sync of this slot replaces its staged one */
	ContactDbName(slotIdx, true, name);
	id = DmFindDatabase(0, name);
	if (id) DmDeleteDatabase(0, id);			/* left over from a crash */
	if (DmCreateDatabase(0, name, appCreator, cddvContactDbType, false) != errNone)
		return dmErrMemError;
	id = DmFindDatabase(0, name);
	if (!id) return dmErrMemError;
	gContactDB = DmOpenDatabase(0, id, dmModeReadWrite);
	return gContactDB ? errNone : dmErrMemError;
}

SEC003 Boolean ContactDbAdd(const UInt8 *body, UInt16 len)
{
	UInt16 pos = 0, i, n, index, textLen = 0, hl, listLen = 0, vl;
	UInt32 uid;
	MemHandle h;
	UInt8 *rec, flags = 0;
	const UInt8 *text = NULL, *v;
	UInt8 hdr[3 + cddvContactListMax + 3 * (1 + cddvContactKeyMax)];

	if (!gContactDB) return false;

	/* find the text: uid, displayName, sortName, dates, categories, text */
	for (i = 0; i < 3 && pos != 0xFFFF; i++) pos = ContactStrAt(body, len, pos, NULL, NULL);
	if (pos == 0xFFFF || pos >= len) return false;
	n = body[pos++];
	for (i = 0; i < n && pos != 0xFFFF; i++) {
		if ((UInt32) pos + 5 > len) return false;
		pos = ContactStrAt(body, len, (UInt16) (pos + 5), NULL, NULL);
	}
	if (pos == 0xFFFF || pos >= len) return false;
	n = body[pos++];
	for (i = 0; i < n && pos != 0xFFFF; i++) pos = ContactStrAt(body, len, pos, NULL, NULL);
	if (pos == 0xFFFF || ContactStrAt(body, len, pos, &text, &textLen) == 0xFFFF) return false;

	/* v2 header, from the plain text: list value (first phone, else first
	 * email), then last, first (N) and company (ORG) for sorting */
	hdr[0] = cddvContactRecVersion;
	hdr[1] = 0;
	if (ContactFindProp(text, textLen, "TEL", &v, &vl) || ContactFindProp(text, textLen, "EMAIL", &v, &vl))
		listLen = ContactComponent(v, vl, 0, hdr + 3, cddvContactListMax);
	hdr[2] = (UInt8) listLen;
	if (!listLen && (ContactFindProp(text, textLen, "IMPP", &v, &vl) ||
			ContactFindProp(text, textLen, "X-SOCIALPROFILE", &v, &vl) ||
			ContactFindProp(text, textLen, "X-WHATSAPP", &v, &vl) ||
			ContactFindProp(text, textLen, "X-TELEGRAM", &v, &vl) ||
			ContactFindProp(text, textLen, "X-SIGNAL", &v, &vl)))
		flags |= cddvContactFlagSocial;			/* Phone Lookup lists it without a list value */
	hl = (UInt16) (3 + listLen);
	for (i = 0; i < 3; i++) {
		UInt16 kl = 0;
		if (i < 2 ? ContactFindProp(text, textLen, "N", &v, &vl) : ContactFindProp(text, textLen, "ORG", &v, &vl))
			kl = ContactComponent(v, vl, i == 1 ? 1 : 0, hdr + hl + 1, cddvContactKeyMax);
		hdr[hl] = (UInt8) kl;
		hl = (UInt16) (hl + 1 + kl);
	}

	index = dmMaxRecordIndex;
	h = DmNewRecord(gContactDB, &index, (UInt32) hl + len);
	if (!h) return false;
	rec = (UInt8 *) MemHandleLock(h);
	DmWrite(rec, hl, body, len);
	if (DmRecordInfo(gContactDB, index, NULL, &uid, NULL) == errNone) {
		if (listLen && CDDVCryptBytes(cddvContactListNonce, uid, hdr + 3, listLen))
			flags |= cddvContactFlagEncList;
		if (textLen) {
			/* encrypt the text in a copy, then write it over the plain one */
			UInt8 *buf = (UInt8 *) MemPtrNew(textLen);
			if (buf) {
				MemMove(buf, text, textLen);
				if (CDDVCryptBytes(cddvContactNonce, uid, buf, textLen)) {
					DmWrite(rec, (UInt32) hl + (UInt32) (text - body), buf, textLen);
					flags |= cddvContactFlagEncText;
				}
				MemSet(buf, textLen, 0);
				MemPtrFree(buf);
			}
		}
	}
	hdr[1] = flags;
	DmWrite(rec, 0, hdr, hl);
	MemSet(hdr, sizeof(hdr), 0);
	MemHandleUnlock(h);
	DmReleaseRecord(gContactDB, index, true);
	if ((textLen && !(flags & cddvContactFlagEncText)) || (listLen && !(flags & cddvContactFlagEncList))) {
		/* Sec35 locked mid-sync: don't keep anything in plain */
		DmRemoveRecord(gContactDB, index);
		return false;
	}
	return true;
}

SEC003 Boolean ContactDbAddGroup(const UInt8 *body, UInt16 len)
{
	UInt16 index = dmMaxRecordIndex;
	MemHandle h;
	UInt8 *rec, hdr[2];

	if (!gContactDB || len < 5) return false;
	h = DmNewRecord(gContactDB, &index, (UInt32) len + 2);
	if (!h) return false;
	rec = (UInt8 *) MemHandleLock(h);
	hdr[0] = kGroupMarker; hdr[1] = 0;
	DmWrite(rec, 0, hdr, 2);
	DmWrite(rec, 2, body, len);
	MemHandleUnlock(h);
	DmReleaseRecord(gContactDB, index, true);
	return true;
}

SEC003 Err ContactDbStage(UInt8 slotType, const char *aliasPlain)
{
	LocalID tmpID, appID;
	UInt16 cardNo = 0, aliasLen;
	MemHandle h;
	UInt8 *p, ext[4];
	AppInfoType *ai;
	UInt8 aliasBuf[cddvAliasLen];

	if (!gContactDB) return dmErrInvalidParam;

	/* info block: standard categories (5b fills them from the groups) + ours */
	h = DmNewHandle(gContactDB, sizeof(AppInfoType) + 4 + cddvAliasLen);
	if (h) {
		p = (UInt8 *) MemHandleLock(h);
		DmSet(p, 0, sizeof(AppInfoType) + 4 + cddvAliasLen, 0);
		ai = (AppInfoType *) p;
		{
			UInt16 i;
			UInt8 ids[dmRecNumCategories];
			for (i = 0; i < dmRecNumCategories; i++) ids[i] = (UInt8) i;
			DmStrCopy(p, (UInt32) ((UInt8 *) ai->categoryLabels[0] - p), "Unfiled");
			DmWrite(p, (UInt32) ((UInt8 *) ai->categoryUniqIDs - p), ids, sizeof(ids));
			ids[0] = dmRecNumCategories - 1;
			DmWrite(p, (UInt32) ((UInt8 *) &ai->lastUniqID - p), ids, 1);
		}
		aliasLen = (UInt16) StrLen(aliasPlain);
		if (aliasLen > cddvAliasLen) aliasLen = cddvAliasLen;
		MemSet(aliasBuf, sizeof(aliasBuf), 0);
		MemMove(aliasBuf, aliasPlain, aliasLen);
		if (!CDDVCryptBytes(cddvContactAliasNonce, gContactSlot, aliasBuf, aliasLen)) aliasLen = 0;
		ext[0] = 1; ext[1] = slotType; ext[2] = 0; ext[3] = (UInt8) aliasLen;
		DmWrite(p, sizeof(AppInfoType), ext, 4);
		DmWrite(p, sizeof(AppInfoType) + 4, aliasBuf, cddvAliasLen);
		MemSet(aliasBuf, sizeof(aliasBuf), 0);
		MemHandleUnlock(h);
		appID = MemHandleToLocalID(h);
		DmOpenDatabaseInfo(gContactDB, &tmpID, NULL, NULL, &cardNo, NULL);
		DmSetDatabaseInfo(cardNo, tmpID, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &appID,
			NULL, NULL, NULL);
	}
	DmCloseDatabase(gContactDB);
	gContactDB = NULL;
	gContactStaged |= (UInt16) (1 << gContactSlot);
	return errNone;
}

SEC003 void ContactDbCommitStaged(void)
{
	char tmpName[dmDBNameLength], name[dmDBNameLength];
	UInt8 slot;
	LocalID tmpID, oldID;
	DmOpenRef db;
	Boolean changed = false;
	Int16 sortArg = -1;			/* read once, only if something is staged */

	for (slot = 0; slot < cddvMaxSlots; slot++) {
		if (!(gContactStaged & (1 << slot))) continue;
		gContactStaged &= (UInt16) ~(1 << slot);
		ContactDbName(slot, true, tmpName);
		ContactDbName(slot, false, name);
		tmpID = DmFindDatabase(0, tmpName);
		if (!tmpID) continue;
		db = DmOpenDatabase(0, tmpID, dmModeReadWrite);
		if (db) {
			DAVDbToast("Saving contacts...");
			ContactApplyCategories(db);		/* also removes the group records */
			if (sortArg == -1) sortArg = ContactSortArg();
			DAVDbToast("Sorting contacts...");	/* the long part on a big book */
			ContactSortDb(db, sortArg);
			DmCloseDatabase(db);
		}
		oldID = DmFindDatabase(0, name);
		if (oldID) DmDeleteDatabase(0, oldID);
		DmSetDatabaseInfo(0, tmpID, name, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
			NULL, NULL, NULL);
		changed = true;
	}
	if (changed) {
		DAVDbToast("Updating index...");
		ContactRebuildBda();
		DAVDbToastEnd();
	}
}

SEC003 void ContactDbResort(void)
{
	char name[dmDBNameLength], msg[24];
	LocalID id;
	DmOpenRef db;
	UInt8 slot, n = 0, k = 0;
	Int16 arg = ContactSortArg();

	for (slot = 0; slot < cddvMaxSlots; slot++) {
		ContactDbName(slot, false, name);
		if (DmFindDatabase(0, name)) n++;
	}
	if (n == 0) return;
	for (slot = 0; slot < cddvMaxSlots; slot++) {
		ContactDbName(slot, false, name);
		id = DmFindDatabase(0, name);
		if (!id) continue;
		StrPrintF(msg, "Sorting %d/%d", k + 1, n);
		DAVDbToast(msg);
		k++;
		if ((db = DmOpenDatabase(0, id, dmModeReadWrite)) == NULL) continue;
		ContactSortDb(db, arg);
		DmCloseDatabase(db);
	}
	DAVDbToast("Updating index");
	ContactRebuildBda();		/* record indexes moved */
	DAVDbToastEnd();
}

SEC003 void ContactDbAbort(void)
{
	LocalID id;
	UInt16 cardNo = 0;

	if (!gContactDB) return;
	DmOpenDatabaseInfo(gContactDB, &id, NULL, NULL, &cardNo, NULL);
	DmCloseDatabase(gContactDB);
	gContactDB = NULL;
	DmDeleteDatabase(cardNo, id);
}

SEC003 void ContactDbDelete(UInt8 slotIdx)
{
	char name[dmDBNameLength];
	LocalID id;

	ContactDbName(slotIdx, false, name);
	id = DmFindDatabase(0, name);
	if (id) {
		DmDeleteDatabase(0, id);
		ContactRebuildBda();
	}
}

SEC003 void ContactDbMarkStale(UInt8 slotIdx)
{
	char name[dmDBNameLength];
	LocalID id;
	DmOpenRef db;
	UInt8 *ai, one = 1;

	ContactDbName(slotIdx, false, name);
	id = DmFindDatabase(0, name);
	if (!id) return;
	db = DmOpenDatabase(0, id, dmModeReadWrite);
	if (!db) return;
	ai = ContactAppInfo(db);
	if (ai) {
		DmWrite(ai, sizeof(AppInfoType) + 2, &one, 1);		/* stale */
		MemPtrUnlock(ai);
	}
	DmCloseDatabase(db);
	ContactRebuildBda();
}

SEC003 void ContactDbCleanup(const CDDVPrefsType *p)
{
	char name[dmDBNameLength];
	LocalID id;
	UInt8 i;
	Boolean dirty = false;

	for (i = 0; i < cddvMaxSlots; i++) {
		const CDDVSlot *s = &p->slots[i];
		Boolean used = (Boolean) (s->aliasLen != 0);
		Boolean contacts = (Boolean) (used && cddvIsContacts(s->serverType));

		if (!contacts) {					/* contacts left over from an earlier type */
			ContactDbName(i, false, name);
			id = DmFindDatabase(0, name);
			if (id) { DmDeleteDatabase(0, id); dirty = true; }
		}
		if ((!used || contacts) && DAVDbSlotHasRows(i)) {	/* calendar rows left over */
			if (DAVDbBeginSlotRebuild(i) == errNone)
				DAVDbCommitSlotRebuild();
		}
	}
	if (dirty) ContactRebuildBda();
}

/* ---- helpers --------------------------------------------------------------- */

/* position of nDates in a contact record (after version, flags, uid,
 * displayName, sortName), or 0xFFFF */
static UInt16 ContactDatesAt(const UInt8 *rec, UInt16 len)
{
	UInt16 pos = ContactBodyAt(rec, len), i;
	for (i = 0; i < 3 && pos != 0xFFFF; i++) pos = ContactStrAt(rec, len, pos, NULL, NULL);
	return (pos != 0xFFFF && pos < len) ? pos : 0xFFFF;
}

/* a (NUL-terminated, <= 15 chars) equals b cut to 15 chars, ignoring case */
static Boolean ContactNameEq(const char *a, const UInt8 *b, UInt16 lb)
{
	UInt16 i;
	if (lb > kNameLen - 1) lb = kNameLen - 1;
	for (i = 0; i < lb; i++) {
		UInt8 x = (UInt8) a[i], y = b[i];
		if (x == 0) return false;
		if (x >= 'A' && x <= 'Z') x = (UInt8) (x + 32);
		if (y >= 'A' && y <= 'Z') y = (UInt8) (y + 32);
		if (x != y) return false;
	}
	return (Boolean) (a[lb] == 0);
}

static Int16 ContactNameIndex(char *names, UInt16 n, const UInt8 *s, UInt16 len)
{
	UInt16 i;
	char *q = names;
	while (len && *s == ' ') { s++; len--; }
	for (i = 0; i < n; i++, q += kNameLen)
		if (ContactNameEq(q, s, len)) return (Int16) i;
	return -1;
}

static void ContactAddName(char *names, UInt16 *n, const UInt8 *s, UInt16 len)
{
	char *q;
	UInt16 i;
	while (len && *s == ' ') { s++; len--; }
	if (len == 0 || *n >= kMaxNames || ContactNameIndex(names, *n, s, len) >= 0) return;
	if (len > kNameLen - 1) len = kNameLen - 1;
	q = names;
	for (i = 0; i < *n; i++) q += kNameLen;
	MemMove(q, s, len);
	q[len] = '\0';
	(*n)++;
}

/* the database's AppInfo, locked (MemPtrUnlock when done), or NULL */
static UInt8 *ContactAppInfo(DmOpenRef db)
{
	LocalID dbID, appID = 0;
	UInt16 cardNo = 0;
	DmOpenDatabaseInfo(db, &dbID, NULL, NULL, &cardNo, NULL);
	DmDatabaseInfo(cardNo, dbID, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &appID, NULL, NULL, NULL);
	return appID ? (UInt8 *) MemLocalIDToLockedPtr(appID, cardNo) : NULL;
}

/* The categories of the contact record at pos (= its nCategories byte):
 * the best (lowest) index among the kept names, or best unchanged. */
static UInt16 ContactBestOwn(const UInt8 *r, UInt16 len, UInt16 pos, char *names, UInt16 kept, UInt16 best) SEC003;
static UInt16 ContactBestOwn(const UInt8 *r, UInt16 len, UInt16 pos, char *names, UInt16 kept, UInt16 best)
{
	UInt16 n, k, sl;
	const UInt8 *s;
	if (pos == 0xFFFF || pos >= len) return best;
	n = r[pos++];
	for (k = 0; k < n && pos != 0xFFFF; k++) {
		UInt16 next = ContactStrAt(r, len, pos, &s, &sl);
		if (next != 0xFFFF) {
			Int16 ix = ContactNameIndex(names, kept, s, sl);
			if (ix >= 0 && (UInt16) ix < best) best = (UInt16) ix;
		}
		pos = next;
	}
	return best;
}

/* position of nCategories in a contact record, or 0xFFFF */
static UInt16 ContactCatsAt(const UInt8 *r, UInt16 len) SEC003;
static UInt16 ContactCatsAt(const UInt8 *r, UInt16 len)
{
	UInt16 pos = ContactDatesAt(r, len), n, k;
	if (pos == 0xFFFF) return 0xFFFF;
	n = r[pos++];
	for (k = 0; k < n && pos != 0xFFFF; k++) {
		if ((UInt32) pos + 5 > len) return 0xFFFF;
		pos = ContactStrAt(r, len, (UInt16) (pos + 5), NULL, NULL);
	}
	return (pos != 0xFFFF && pos < len) ? pos : 0xFFFF;
}

/* Categories from the groups (kGroupMarker records) and each contact's own
 * CATEGORIES: up to 15 names, alphabetical; each contact under the first it
 * belongs to, else Unfiled. Then the group records go. */
static void ContactApplyCategories(DmOpenRef db)
{
	char *names = (char *) MemPtrNew(kMaxNames * kNameLen);
	UInt16 groups[kMaxGroupRecs];
	UInt16 nNames = 0, nGroups = 0, total, i, j, k, kept;

	if (!names) return;
	total = DmNumRecords(db);

	/* 1. every name, and where the group records are */
	for (i = 0; i < total; i++) {
		MemHandle h = DmQueryRecord(db, i);
		const UInt8 *r, *s;
		UInt16 len, pos, sl, n;
		if (!h) continue;
		r = (const UInt8 *) MemHandleLock(h);
		len = (UInt16) MemHandleSize(h);
		if (r[0] == kGroupMarker) {
			if (nGroups < kMaxGroupRecs) groups[nGroups++] = i;
			pos = ContactStrAt(r, len, 2, NULL, NULL);					/* uid */
			if (pos != 0xFFFF && ContactStrAt(r, len, pos, &s, &sl) != 0xFFFF)
				ContactAddName(names, &nNames, s, sl);
		} else if (r[0] == cddvContactRecVersion && (pos = ContactCatsAt(r, len)) != 0xFFFF) {
			n = r[pos++];
			for (k = 0; k < n && pos != 0xFFFF; k++) {
				UInt16 next = ContactStrAt(r, len, pos, &s, &sl);
				if (next != 0xFFFF) ContactAddName(names, &nNames, s, sl);
				pos = next;
			}
		}
		MemHandleUnlock(h);
	}

	/* 2. alphabetical (insertion sort, few names); keep 15 */
	for (i = 1; i < nNames; i++) {
		char tmp[kNameLen];
		char *a = names, *b;
		for (k = 0; k < i; k++) a += kNameLen;
		MemMove(tmp, a, kNameLen);
		for (j = i; j > 0; j--) {
			b = a - kNameLen;
			if (StrCaselessCompare(b, tmp) <= 0) break;
			MemMove(a, b, kNameLen);
			a = b;
		}
		MemMove(a, tmp, kNameLen);
	}
	kept = nNames < dmRecNumCategories - 1 ? nNames : dmRecNumCategories - 1;

	/* 3. each contact: the first (alphabetically) of its own categories and
	 *    of the groups that list its UID */
	for (i = 0; i < total; i++) {
		MemHandle h = DmQueryRecord(db, i);
		const UInt8 *r, *uid;
		UInt16 len, ul, best = 0xFFFF, attr;
		if (!h) continue;
		r = (const UInt8 *) MemHandleLock(h);
		len = (UInt16) MemHandleSize(h);
		if (ContactBodyAt(r, len) == 0xFFFF || ContactStrAt(r, len, ContactBodyAt(r, len), &uid, &ul) == 0xFFFF) {
			MemHandleUnlock(h);
			continue;
		}
		best = ContactBestOwn(r, len, ContactCatsAt(r, len), names, kept, best);
		for (j = 0; j < nGroups; j++) {
			MemHandle gh = DmQueryRecord(db, groups[j]);
			const UInt8 *g, *gs = NULL;
			UInt16 glen, gp, gl = 0, m;
			Int16 gi;
			if (!gh) continue;
			g = (const UInt8 *) MemHandleLock(gh);
			glen = (UInt16) MemHandleSize(gh);
			gp = ContactStrAt(g, glen, 2, NULL, NULL);					/* group uid */
			if (gp != 0xFFFF) gp = ContactStrAt(g, glen, gp, &gs, &gl);	/* group name */
			gi = (gp != 0xFFFF) ? ContactNameIndex(names, kept, gs, gl) : -1;
			if (gi >= 0 && (UInt16) gi < best && gp < glen) {
				m = g[gp++];
				for (k = 0; k < m && gp != 0xFFFF; k++) {
					const UInt8 *mu;
					UInt16 ml;
					UInt16 next = ContactStrAt(g, glen, gp, &mu, &ml);
					if (next != 0xFFFF && ml == ul && MemCmp(mu, uid, ul) == 0) { best = (UInt16) gi; break; }
					gp = next;
				}
			}
			MemHandleUnlock(gh);
		}
		MemHandleUnlock(h);
		if (DmRecordInfo(db, i, &attr, NULL, NULL) == errNone) {
			attr = (UInt16) ((attr & ~dmRecAttrCategoryMask) |
				(best != 0xFFFF ? (UInt16) (best + 1) : dmUnfiledCategory));
			DmSetRecordInfo(db, i, &attr, NULL);
		}
	}

	/* 4. the group records go (last first, so the indexes stay valid) */
	for (j = nGroups; j > 0; j--) DmRemoveRecord(db, groups[j - 1]);

	/* 5. the names into the info block (index 0 stays Unfiled) */
	{
		UInt8 *ai = ContactAppInfo(db);
		if (ai) {
			AppInfoType *a = (AppInfoType *) ai;
			char *q = names;
			for (i = 0; i < kept; i++, q += kNameLen)
				DmStrCopy(ai, (UInt32) ((UInt8 *) a->categoryLabels[i + 1] - ai), q);
			MemPtrUnlock(ai);
		}
	}
	MemPtrFree(names);
}

/* Address35DAVBDA from every non-stale Address35DAV_<n> (layout in ContactDb.h) */
static void ContactRebuildBda(void)
{
	DmOpenRef dbs[cddvMaxSlots], bda;
	BdaEnt *ents = NULL;
	UInt16 count = 0, filled = 0, i, k, rec, m, a, b;
	UInt8 slot;
	char name[dmDBNameLength];
	LocalID id;

	for (slot = 0; slot < cddvMaxSlots; slot++) {
		UInt8 *ai;
		dbs[slot] = NULL;
		ContactDbName(slot, false, name);
		id = DmFindDatabase(0, name);
		if (!id) continue;
		dbs[slot] = DmOpenDatabase(0, id, dmModeReadOnly);
		if (!dbs[slot]) continue;
		ai = ContactAppInfo(dbs[slot]);
		if (ai) {
			Boolean stale = (Boolean) (ai[sizeof(AppInfoType) + 2] != 0);
			MemPtrUnlock(ai);
			if (stale) { DmCloseDatabase(dbs[slot]); dbs[slot] = NULL; }
		}
	}

	/* two passes: count, then collect */
	for (k = 0; k < 2; k++) {
		for (slot = 0; slot < cddvMaxSlots; slot++) {
			UInt16 n;
			if (!dbs[slot]) continue;
			n = DmNumRecords(dbs[slot]);
			for (rec = 0; rec < n; rec++) {
				MemHandle h = DmQueryRecord(dbs[slot], rec);
				const UInt8 *r;
				UInt16 len, pos, nd, d;
				if (!h) continue;
				r = (const UInt8 *) MemHandleLock(h);
				len = (UInt16) MemHandleSize(h);
				if (r[0] == cddvContactRecVersion && (pos = ContactDatesAt(r, len)) != 0xFFFF) {
					nd = r[pos++];
					for (d = 0; d < nd && pos != 0xFFFF; d++) {
						if ((UInt32) pos + 5 > len) break;
						if (r[pos + 2] >= 1 && r[pos + 2] <= 12) {
							if (k == 0) {
								count++;
							} else if (ents && filled < count) {
								ents[filled].month = r[pos + 2];
								ents[filled].day = r[pos + 3];
								ents[filled].slot = slot;
								ents[filled].dateIdx = (UInt8) d;
								ents[filled].rec = rec;
								filled++;
							}
						}
						pos = ContactStrAt(r, len, (UInt16) (pos + 5), NULL, NULL);
					}
				}
				MemHandleUnlock(h);
			}
		}
		if (k == 0 && count)
			ents = (BdaEnt *) MemPtrNew(((UInt32) count << 2) + ((UInt32) count << 1));	/* count * 6, no libgcc */
	}

	/* month, then day (insertion sort; a few hundred at most) */
	for (i = 1; i < filled; i++) {
		BdaEnt t = ents[i];
		k = i;
		while (k > 0 && (ents[k - 1].month > t.month ||
				(ents[k - 1].month == t.month && ents[k - 1].day > t.day))) {
			ents[k] = ents[k - 1];
			k--;
		}
		ents[k] = t;
	}

	id = DmFindDatabase(0, cddvContactBdaName);
	if (id) DmDeleteDatabase(0, id);
	if (DmCreateDatabase(0, cddvContactBdaName, appCreator, cddvContactDbType, false) == errNone &&
		(id = DmFindDatabase(0, cddvContactBdaName)) != 0 &&
		(bda = DmOpenDatabase(0, id, dmModeReadWrite)) != NULL) {
		a = 0;
		for (m = 1; m <= 12; m++) {
			UInt32 size = 2, off = 2;
			UInt16 idx = dmMaxRecordIndex;
			MemHandle h;
			UInt8 *out, cnt[2];

			for (b = a; b < filled && ents[b].month == m; b++) ;
			for (i = a; i < b; i++) size += 7 + 1 + kBdaNameMax + 1 + kBdaNameMax;	/* room per entry */
			h = DmNewRecord(bda, &idx, size);
			if (!h) { a = b; continue; }
			out = (UInt8 *) MemHandleLock(h);
			cnt[0] = (UInt8) ((b - a) >> 8);
			cnt[1] = (UInt8) (b - a);
			DmWrite(out, 0, cnt, 2);
			for (i = a; i < b; i++) {
				UInt8 e[7 + 1 + kBdaNameMax + 1 + kBdaNameMax];
				UInt16 el = 0, len, pos, d, nl = 0, ol = 0;
				const UInt8 *nm = NULL, *ot = NULL;
				MemHandle sh = DmQueryRecord(dbs[ents[i].slot], ents[i].rec);
				const UInt8 *r;
				if (!sh) continue;
				r = (const UInt8 *) MemHandleLock(sh);
				len = (UInt16) MemHandleSize(sh);
				pos = ContactBodyAt(r, len);
				if (pos != 0xFFFF) pos = ContactStrAt(r, len, pos, NULL, NULL);	/* uid */
				if (pos != 0xFFFF) ContactStrAt(r, len, pos, &nm, &nl);		/* displayName */
				pos = ContactDatesAt(r, len);
				if (pos != 0xFFFF) {
					pos++;
					for (d = 0; d < ents[i].dateIdx && pos != 0xFFFF; d++)
						pos = ContactStrAt(r, len, (UInt16) (pos + 5), NULL, NULL);
				}
				if (pos != 0xFFFF && (UInt32) pos + 5 <= len) {
					if (nl > kBdaNameMax) nl = kBdaNameMax;
					ContactStrAt(r, len, (UInt16) (pos + 5), &ot, &ol);
					if (ol > kBdaNameMax) ol = kBdaNameMax;
					e[el++] = r[pos + 3];				/* day */
					e[el++] = r[pos + 4];				/* kind */
					e[el++] = r[pos];					/* year, big-endian */
					e[el++] = r[pos + 1];
					e[el++] = ents[i].slot;
					e[el++] = (UInt8) (ents[i].rec >> 8);
					e[el++] = (UInt8) ents[i].rec;
					e[el++] = (UInt8) nl;
					if (nl) { MemMove(e + el, nm, nl); el = (UInt16) (el + nl); }
					e[el++] = (UInt8) ol;
					if (ol) { MemMove(e + el, ot, ol); el = (UInt16) (el + ol); }
					DmWrite(out, off, e, el);
					off += el;
				}
				MemHandleUnlock(sh);
			}
			MemHandleUnlock(h);
			DmReleaseRecord(bda, idx, true);
			if (off < size) DmResizeRecord(bda, idx, off);		/* give back the unused room */
			a = b;
		}
		DmCloseDatabase(bda);
	}

	if (ents) MemPtrFree(ents);
	for (slot = 0; slot < cddvMaxSlots; slot++)
		if (dbs[slot]) DmCloseDatabase(dbs[slot]);
}
