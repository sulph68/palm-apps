/*
 * AddressDB35.c - Address35 database layer (own DB, TLV record format).
 *
 * See AddressDB35.h and apps/address35/Address35.md. Everything here lives in the
 * MAIN segment: the vCard receive/import path (a no-globals sublaunch) reaches the
 * DB write functions, so they must be in main.
 *
 * On-disk packed record:
 *   [0]  UInt8  version   (= a35RecVersion)
 *   [1]  UInt8  misc
 *   [2]  UInt16 fieldCount            (big-endian)
 *   [4]  UInt16 offLast               offset to last-name value, 0 if none
 *   [6]  UInt16 offFirst
 *   [8]  UInt16 offCompany
 *   [10] FieldEntry[fieldCount]
 * FieldEntry:  UInt8 type; UInt8 label; UInt16 len (BE); UInt8 value[len].
 *
 * ALL multi-byte access to the blob is byte-wise big-endian: a field length or a
 * value can land on an odd address, and a UInt16* load there faults on a 68000.
 */
#include <PalmOS.h>
#include "AddressDB35.h"
#include "enccore.h"			/* Sec35 shared-key AES-CTR (EncGetKey/EncCrypt) */

#define a35HdrSize		10		/* bytes before the first FieldEntry */
#define a35EncTtl		1000000000UL	/* read-time TTL: Sec35's alarm enforces lock */

/* ------------------------------------------------------------------ *
 *  at-rest encryption (payload fields only; sort/display/cross-app clear)
 *
 * Which field VALUES are encrypted. CLEAR: name/sort keys (last/first/middle/
 * nickname/company/title/prefix/suffix) so the comparator + list keep working; tag
 * (filtering); relation (reciprocity lookups read it directly); birthday +
 * anniversary (Datebook35 agenda reads them cross-app). Everything else is the
 * sensitive contact payload, reached only via A35GetRecord.
 * Nonce per field = (record uniqueID as nonceHi, field index as nonceLo).
 * ------------------------------------------------------------------ */
Boolean A35FieldEncrypted(UInt8 type)
{
	switch (type) {
	case a35fPhone: case a35fEmail: case a35fIM: case a35fURL:
	case a35fAddress: case a35fCity: case a35fState: case a35fZip:
	case a35fCountry: case a35fSocial: case a35fNote: case a35fCustom:
	case a35fGender: case a35fPronouns: case a35fOther:
		return true;
	default:
		return false;
	}
}

static void A35Unpack(const UInt8 *rec, A35Record *out);	/* defined below */

/* True if ANY record is marked encrypted (raw scan of the misc byte; early-exits).
 * No decryption, so it is sublaunch-safe and needs no key. */
Boolean A35DbHasEncrypted(DmOpenRef dbP)
{
	UInt16 i, n = DmNumRecords(dbP);

	for (i = 0; i < n; i++) {
		MemHandle h = DmQueryRecord(dbP, i);
		if (h != NULL) {
			Boolean enc = (Boolean) (((const UInt8 *) MemHandleLock(h))[1]
				& a35MiscEncrypted);
			MemHandleUnlock(h);
			if (enc)
				return true;
		}
	}
	return false;
}

/* Raw zero-copy view - NO decryption. Used by clear-field-only readers that can
 * run in a no-globals sublaunch (global Find, name display), where the crypto
 * segment (sec003) is NOT loaded and must not be called. Only the CLEAR fields
 * (name/company/tag/relation/birthday) are valid from this view. Release with
 * MemHandleUnlock (it is the storage handle, not a decrypt buffer). */
Err A35GetRecordRaw(DmOpenRef dbP, UInt16 index, A35Record *out,
	MemHandle *recordH)
{
	MemHandle h = DmQueryRecord(dbP, index);
	if (h == NULL)
		return DmGetLastErr();
	A35Unpack((const UInt8 *) MemHandleLock(h), out);
	*recordH = h;
	return errNone;
}

/* ------------------------------------------------------------------ *
 *  byte-wise big-endian access (alignment-safe on 68000)
 * ------------------------------------------------------------------ */
static UInt16 rdU16(const UInt8 *p)
{
	return (UInt16) (((UInt16) p[0] << 8) | (UInt16) p[1]);
}

static void wrU16(UInt8 *p, UInt16 v)
{
	p[0] = (UInt8) (v >> 8);
	p[1] = (UInt8) v;
}

/* ------------------------------------------------------------------ *
 *  unpacked-record helpers
 * ------------------------------------------------------------------ */
void A35RecordInit(A35Record *r)
{
	r->version = a35RecVersion;
	r->misc = 0;
	r->fieldCount = 0;
}

Boolean A35AddField(A35Record *r, UInt8 type, UInt8 label,
	const void *value, UInt16 len)
{
	A35Field *f;

	if (r->fieldCount >= a35MaxFields)
		return false;
	f = &r->fields[r->fieldCount++];
	f->type = type;
	f->label = label;
	f->len = len;
	f->value = (const UInt8 *) value;
	return true;
}

Boolean A35AddStr(A35Record *r, UInt8 type, UInt8 label, const char *s)
{
	if (s == NULL || s[0] == '\0')			/* never store empty strings */
		return true;
	return A35AddField(r, type, label, s, (UInt16) (StrLen(s) + 1));
}

const A35Field *A35FindField(const A35Record *r, UInt8 type, UInt16 occurrence)
{
	UInt16 i, seen = 0;

	for (i = 0; i < r->fieldCount; i++) {
		if (r->fields[i].type == type) {
			if (seen == occurrence)
				return &r->fields[i];
			seen++;
		}
	}
	return NULL;
}

const char *A35GetStr(const A35Record *r, UInt8 type)
{
	const A35Field *f = A35FindField(r, type, 0);

	if (f != NULL && f->len > 0)
		return (const char *) f->value;
	return "";
}

/* ---- relations (link to another record by its stable unique id) -------- */
static UInt32 rdU32(const UInt8 *p)
{
	return ((UInt32) p[0] << 24) | ((UInt32) p[1] << 16) |
	       ((UInt32) p[2] << 8) | (UInt32) p[3];
}
static void wrU32(UInt8 *p, UInt32 v)
{
	p[0] = (UInt8) (v >> 24); p[1] = (UInt8) (v >> 16);
	p[2] = (UInt8) (v >> 8);  p[3] = (UInt8) v;
}

void A35PackRelation(const A35Relation *rel, UInt8 *out)
{
	wrU32(out, rel->targetUniqueID);
	out[4] = rel->relType;
}
Boolean A35UnpackRelation(const UInt8 *p, UInt16 len, A35Relation *out)
{
	if (p == NULL || len < a35RelationPackLen)
		return false;
	out->targetUniqueID = rdU32(p);
	out->relType = p[4];
	return true;
}
Boolean A35FindByUniqueID(DmOpenRef dbP, UInt32 uid, UInt16 *indexP)
{
	return (Boolean) (DmFindRecordByID(dbP, uid, indexP) == errNone);
}

/* One-line display name for a record: "First Last", else Company, else Nickname,
 * else "-Unnamed-". */
void A35RecordDisplayName(DmOpenRef dbP, UInt16 index, char *out, UInt16 outLen)
{
	A35Record rec;
	MemHandle h;
	const char *first, *last, *company, *nick;

	out[0] = '\0';
	if (outLen == 0 || A35GetRecordRaw(dbP, index, &rec, &h) != errNone)
		return;
	first = A35GetStr(&rec, a35fFirst);
	last = A35GetStr(&rec, a35fLast);
	company = A35GetStr(&rec, a35fCompany);
	nick = A35GetStr(&rec, a35fNickname);
	if (first[0] || last[0]) {
		StrNCopy(out, first, outLen - 1); out[outLen - 1] = '\0';
		if (first[0] && last[0])
			StrNCat(out, " ", (Int16) outLen);
		StrNCat(out, last, (Int16) outLen);
	} else {
		const char *s = company[0] ? company : (nick[0] ? nick : "-Unnamed-");
		StrNCopy(out, s, outLen - 1); out[outLen - 1] = '\0';
	}
	MemHandleUnlock(h);
}

/* Rich one-line picker label: "[Salut ]First Last[, Company][, Title]" (Company
 * stands in when there is no name) - disambiguates contacts in the relationship
 * picker. */
void A35RecordPickName(DmOpenRef dbP, UInt16 index, char *out, UInt16 outLen)
{
	A35Record rec;
	MemHandle h;
	const char *salut, *first, *last, *company, *title;

#define PK_CAT(s)	StrNCat(out, (s), (Int16) outLen)
	out[0] = '\0';
	if (outLen == 0 || A35GetRecordRaw(dbP, index, &rec, &h) != errNone)
		return;
	salut = A35GetStr(&rec, a35fPrefix);
	first = A35GetStr(&rec, a35fFirst);
	last = A35GetStr(&rec, a35fLast);
	company = A35GetStr(&rec, a35fCompany);
	title = A35GetStr(&rec, a35fTitle);

	if (first[0] || last[0]) {
		if (salut[0]) { PK_CAT(salut); PK_CAT(" "); }
		PK_CAT(first);
		if (first[0] && last[0]) PK_CAT(" ");
		PK_CAT(last);
		if (company[0]) { PK_CAT(", "); PK_CAT(company); }
	} else if (company[0]) {
		PK_CAT(company);
	}
	if (title[0]) { if (out[0]) PK_CAT(", "); PK_CAT(title); }
	if (!out[0]) PK_CAT("-Unnamed-");
#undef PK_CAT
	MemHandleUnlock(h);
}

static UInt16 bdField(const char *s, UInt16 n)		/* n digits -> value */
{
	UInt16 v = 0, i;
	for (i = 0; i < n; i++) {
		if (s[i] < '0' || s[i] > '9') return 0;
		v = (UInt16) (v * 10 + (s[i] - '0'));
	}
	return v;
}

void A35FormatBirthday(const A35Birthday *bd, char *out)
{
	UInt16 y = (bd->flags & a35bdHasYear) ? (UInt16) bd->year : 0;

	out[0] = (char) ('0' + (y / 1000) % 10);
	out[1] = (char) ('0' + (y / 100) % 10);
	out[2] = (char) ('0' + (y / 10) % 10);
	out[3] = (char) ('0' + y % 10);
	out[4] = (char) ('0' + (bd->month / 10) % 10);
	out[5] = (char) ('0' + bd->month % 10);
	out[6] = (char) ('0' + (bd->day / 10) % 10);
	out[7] = (char) ('0' + bd->day % 10);
	out[8] = '\0';
}

Boolean A35ParseBirthday(const char *ymd, A35Birthday *out)
{
	UInt16 y;

	if (ymd == NULL || StrLen(ymd) < a35BirthdayStrLen)
		return false;
	y = bdField(ymd, 4);
	out->year = (Int16) y;
	out->month = (UInt8) bdField(ymd + 4, 2);
	out->day = (UInt8) bdField(ymd + 6, 2);
	out->flags = (UInt8) (y ? a35bdHasYear : 0);
	return true;
}

Boolean A35GetBirthday(const A35Record *r, A35Birthday *out)
{
	const A35Field *f = A35FindField(r, a35fBirthday, 0);

	if (f == NULL || f->len < a35BirthdayStrLen)
		return false;
	return A35ParseBirthday((const char *) f->value, out);
}

/* month abbreviations for the no-ROM-cap display (DateToAscii caps at 2031) */
static const char *const kBdMon[12] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

void A35BirthdayDisplay(const char *ymd, char *out)
{
	A35Birthday bd;
	DateFormatType fmt;
	Boolean dmy;
	char num[8];

	out[0] = '\0';
	if (!A35ParseBirthday(ymd, &bd) || bd.month < 1 || bd.month > 12)
		return;

	/* honor the system month/day order; year (if any) always trails */
	fmt = (DateFormatType) PrefGetPreference(prefLongDateFormat);
	dmy = (Boolean) (fmt == dfDMYLong || fmt == dfDMYLongWithComma ||
		fmt == dfDMYWithSlashes || fmt == dfDMYWithDots ||
		fmt == dfDMYWithDashes);

	if (dmy) {
		StrIToA(num, bd.day);
		StrCopy(out, num);
		StrCat(out, " ");
		StrCat(out, kBdMon[bd.month - 1]);
	} else {
		StrCopy(out, kBdMon[bd.month - 1]);
		StrCat(out, " ");
		StrIToA(num, bd.day);
		StrCat(out, num);
	}
	if (bd.flags & a35bdHasYear) {
		StrCat(out, ", ");
		StrIToA(num, bd.year);
		StrCat(out, num);
	}
}

Boolean A35RecordHasTag(const A35Record *r, UInt16 tagId)
{
	UInt16 i;

	for (i = 0; i < r->fieldCount; i++) {
		if (r->fields[i].type == a35fTag && r->fields[i].len >= 2 &&
			rdU16(r->fields[i].value) == tagId)
			return true;
	}
	return false;
}

Boolean A35RecordHasData(const A35Record *r)
{
	return (Boolean) (r->fieldCount > 0);
}

/* ------------------------------------------------------------------ *
 *  pack / unpack
 * ------------------------------------------------------------------ */
static Int32 A35PackedSize(const A35Record *r)
{
	Int32 size = a35HdrSize;
	UInt16 i;

	for (i = 0; i < r->fieldCount; i++)
		size += 4 + r->fields[i].len;			/* type+label+len16+value */
	return size;
}

/* Build the packed record into a fresh dynamic-heap chunk (byte-wise, so no
 * alignment traps). Caller MemHandleFree()s it. Returns NULL on OOM. */
static MemHandle A35BuildPacked(const A35Record *r)
{
	Int32 total = A35PackedSize(r);
	MemHandle h = MemHandleNew(total);
	UInt8 *dst;
	Int32 off;
	UInt16 i;
	UInt16 offLast = 0, offFirst = 0, offCompany = 0;

	if (h == NULL)
		return NULL;
	dst = (UInt8 *) MemHandleLock(h);

	dst[0] = a35RecVersion;
	dst[1] = r->misc;
	wrU16(dst + 2, r->fieldCount);

	off = a35HdrSize;
	for (i = 0; i < r->fieldCount; i++) {
		const A35Field *f = &r->fields[i];
		Int32 vOff = off + 4;

		dst[off] = f->type;
		dst[off + 1] = f->label;
		wrU16(dst + off + 2, f->len);
		if (f->len)
			MemMove(dst + vOff, (void *) f->value, f->len);

		if (f->type == a35fLast && offLast == 0)
			offLast = (UInt16) vOff;
		else if (f->type == a35fFirst && offFirst == 0)
			offFirst = (UInt16) vOff;
		else if (f->type == a35fCompany && offCompany == 0)
			offCompany = (UInt16) vOff;

		off = vOff + f->len;
	}
	wrU16(dst + 4, offLast);
	wrU16(dst + 6, offFirst);
	wrU16(dst + 8, offCompany);

	MemHandleUnlock(h);
	return h;
}

/* Zero-copy view: out->fields[i].value point into the locked packed record. */
static void A35Unpack(const UInt8 *rec, A35Record *out)
{
	UInt16 count, i;
	Int32 off;

	out->version = rec[0];
	out->misc = rec[1];
	count = rdU16(rec + 2);
	if (count > a35MaxFields)
		count = a35MaxFields;
	out->fieldCount = count;

	off = a35HdrSize;
	for (i = 0; i < count; i++) {
		A35Field *f = &out->fields[i];

		f->type = rec[off];
		f->label = rec[off + 1];
		f->len = rdU16(rec + off + 2);
		f->value = rec + off + 4;
		off += 4 + f->len;
	}
}

/* ------------------------------------------------------------------ *
 *  sort comparator (operates on packed records, no unpack)
 * ------------------------------------------------------------------ */
/* kind: a35sortXxx. Returns the header-referenced key cstring or NULL if absent. */
static const char *A35KeyForKind(const UInt8 *rec, Int16 kind)
{
	UInt16 off;

	switch (kind) {
	case a35sortFirst:		off = rdU16(rec + 6); break;	/* first   */
	case a35sortCompany:	off = rdU16(rec + 8); break;	/* company */
	case a35sortLast:
	default:				off = rdU16(rec + 4); break;	/* last    */
	}
	if (off == 0)
		return NULL;
	return (const char *) (rec + off);
}

/* Build the full key priority: primary, secondary, then any remaining key so
 * the order is total/deterministic. Returns count (always 3). */
static Int16 A35SortPriority(Int16 primary, Int16 secondary, Int16 order[3])
{
	Boolean used[3];
	Int16 n = 0, k;

	used[0] = used[1] = used[2] = false;
	if (primary >= 0 && primary <= 2)   { order[n++] = primary;   used[primary] = true; }
	if (secondary >= 0 && secondary <= 2 && !used[secondary])
	                                    { order[n++] = secondary; used[secondary] = true; }
	for (k = 0; k < 3; k++)
		if (!used[k]) order[n++] = k;
	return n;
}

/* The comparator's 'other' arg packs both keys: primary in the low byte,
 * secondary in the next. */
#define A35SortArg(pri, sec)	((Int16) (((pri) & 0xFF) | (((sec) & 0xFF) << 8)))

static Int16 A35ComparePackedRecords(void *rec1, void *rec2, Int16 sortArg,
	SortRecordInfoPtr info1, SortRecordInfoPtr info2, MemHandle appInfoH)
{
	const UInt8 *r1 = (const UInt8 *) rec1;
	const UInt8 *r2 = (const UInt8 *) rec2;
	Int16 order[3], n, i, res;

	/* unused */
	(void) info1; (void) info2; (void) appInfoH;

	n = A35SortPriority((Int16) (sortArg & 0xFF), (Int16) ((sortArg >> 8) & 0xFF), order);
	for (i = 0; i < n; i++) {
		const char *k1 = A35KeyForKind(r1, order[i]);
		const char *k2 = A35KeyForKind(r2, order[i]);

		if (k1 == NULL && k2 == NULL)
			continue;						/* neither has this key */
		if (k1 == NULL)
			return -1;						/* missing key sorts first */
		if (k2 == NULL)
			return 1;
		res = StrCaselessCompare(k1, k2);
		if (res == 0)
			res = StrCompare(k1, k2);
		if (res != 0)
			return res;
	}
	return 0;
}

/* Read the packed sort arg from the AppInfo (defaults last/first). */
static Int16 A35SortArgFromApp(DmOpenRef dbP)
{
	A35AppInfoPtr appInfoP = A35AppInfoGetPtr(dbP);
	UInt8 pri = a35sortLast, sec = a35sortFirst;

	if (appInfoP != NULL) {
		pri = appInfoP->primarySort;
		sec = appInfoP->secondarySort;
		MemPtrUnlock(appInfoP);
		if (pri > a35sortCompany) pri = a35sortLast;
		if (sec > a35sortCompany) sec = a35sortFirst;
	}
	return A35SortArg(pri, sec);
}

/* ------------------------------------------------------------------ *
 *  AppInfo
 * ------------------------------------------------------------------ */
A35AppInfoPtr A35AppInfoGetPtr(DmOpenRef dbP)
{
	UInt16 cardNo = 0;
	LocalID dbID = 0, appInfoID = 0;

	if (DmOpenDatabaseInfo(dbP, &dbID, NULL, NULL, &cardNo, NULL) != errNone)
		return NULL;
	if (DmDatabaseInfo(cardNo, dbID, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
			&appInfoID, NULL, NULL, NULL) != errNone)
		return NULL;
	if (appInfoID == 0)
		return NULL;
	return (A35AppInfoPtr) MemLocalIDToLockedPtr(appInfoID, cardNo);
}

Err A35AppInfoInit(DmOpenRef dbP)
{
	UInt16 cardNo = 0;
	LocalID dbID = 0, appInfoID = 0;
	MemHandle h;
	A35AppInfoPtr appInfoP;
	UInt16 zero16 = 0;
	UInt8 zero8 = 0;

	if (DmOpenDatabaseInfo(dbP, &dbID, NULL, NULL, &cardNo, NULL) != errNone)
		return dmErrInvalidParam;

	h = DmNewHandle(dbP, (Int32) sizeof(A35AppInfoType));
	if (h == NULL)
		return dmErrMemError;
	appInfoID = MemHandleToLocalID(h);
	/* &appInfoID MUST be the 8th pointer arg (appInfoIDP). */
	DmSetDatabaseInfo(cardNo, dbID, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
		&appInfoID, NULL, NULL, NULL);

	appInfoP = (A35AppInfoPtr) MemLocalIDToLockedPtr(appInfoID, cardNo);
	/* zero the whole struct, then init the standard category block by hand
	 * (deterministic; avoids the ROM's localized-strings-resource dependency).
	 * Category 0 = Unfiled, plus two sensible defaults; uniq ids 0..15. */
	DmSet(appInfoP, 0, sizeof(A35AppInfoType), 0);
	{
		char *base = (char *) appInfoP;
		UInt16 i;
		UInt8 uid;

		DmStrCopy(appInfoP,
			(Int32) ((char *) appInfoP->categoryLabels[dmUnfiledCategory] - base),
			"Unfiled");
		DmStrCopy(appInfoP,
			(Int32) ((char *) appInfoP->categoryLabels[1] - base), "Business");
		DmStrCopy(appInfoP,
			(Int32) ((char *) appInfoP->categoryLabels[2] - base), "Personal");
		for (i = 0; i < dmRecNumCategories; i++) {
			uid = (UInt8) i;
			DmWrite(appInfoP,
				(Int32) ((char *) &appInfoP->categoryUniqIDs[i] - base),
				&uid, sizeof(uid));
		}
		uid = (UInt8) (dmRecNumCategories - 1);
		DmWrite(appInfoP, (Int32) ((char *) &appInfoP->lastUniqID - base),
			&uid, sizeof(uid));
	}

	/* Address35 extensions: surnameTerm=Surname, sort primary=Last secondary=First. */
	DmWrite(appInfoP, (Int32) ((UInt8 *) &appInfoP->surnameTerm - (UInt8 *) appInfoP),
		&zero8, sizeof(zero8));
	{
		UInt8 pri = a35sortLast, sec = a35sortFirst;
		DmWrite(appInfoP, (Int32) ((UInt8 *) &appInfoP->primarySort - (UInt8 *) appInfoP),
			&pri, sizeof(pri));
		DmWrite(appInfoP, (Int32) ((UInt8 *) &appInfoP->secondarySort - (UInt8 *) appInfoP),
			&sec, sizeof(sec));
	}
	DmWrite(appInfoP, (Int32) ((UInt8 *) &appInfoP->tagCount - (UInt8 *) appInfoP),
		&zero16, sizeof(zero16));

	MemPtrUnlock(appInfoP);
	return errNone;
}

UInt8 A35GetSurnameTerm(DmOpenRef dbP)
{
	A35AppInfoPtr appInfoP = A35AppInfoGetPtr(dbP);
	UInt8 term = a35termSurname;

	if (appInfoP != NULL) {
		term = appInfoP->surnameTerm;
		MemPtrUnlock(appInfoP);
	}
	return term;
}

const char *A35SurnameTermStr(UInt8 term)
{
	switch (term) {
	case a35termLastName:	return "Last Name";
	case a35termFamilyName:	return "Family Name";
	default:				return "Surname";
	}
}

void A35SetSurnameTerm(DmOpenRef dbP, UInt8 term)
{
	A35AppInfoPtr appInfoP = A35AppInfoGetPtr(dbP);

	if (appInfoP != NULL) {
		DmWrite(appInfoP,
			(Int32) ((UInt8 *) &appInfoP->surnameTerm - (UInt8 *) appInfoP),
			&term, sizeof(term));
		MemPtrUnlock(appInfoP);
	}
}

UInt16 A35TagFind(DmOpenRef dbP, const char *name)
{
	A35AppInfoPtr appInfoP = A35AppInfoGetPtr(dbP);
	UInt16 i, found = 0;

	if (appInfoP == NULL || name == NULL || name[0] == '\0')
		return 0;
	for (i = 0; i < appInfoP->tagCount; i++) {
		if (StrCaselessCompare(appInfoP->tagNames[i], name) == 0) {
			found = (UInt16) (i + 1);
			break;
		}
	}
	MemPtrUnlock(appInfoP);
	return found;
}

UInt16 A35TagAdd(DmOpenRef dbP, const char *name)
{
	A35AppInfoPtr appInfoP;
	UInt16 i, id = 0, newCount;
	char slot[a35TagNameLen];

	if (name == NULL || name[0] == '\0')
		return 0;

	appInfoP = A35AppInfoGetPtr(dbP);
	if (appInfoP == NULL)
		return 0;

	for (i = 0; i < appInfoP->tagCount; i++) {
		if (StrCaselessCompare(appInfoP->tagNames[i], name) == 0) {
			id = (UInt16) (i + 1);
			break;
		}
	}
	if (id == 0 && appInfoP->tagCount < a35MaxTags) {
		i = appInfoP->tagCount;
		MemSet(slot, sizeof(slot), 0);
		StrNCopy(slot, name, a35TagNameLen - 1);
		DmWrite(appInfoP,
			(Int32) ((UInt8 *) appInfoP->tagNames[i] - (UInt8 *) appInfoP),
			slot, a35TagNameLen);
		newCount = (UInt16) (i + 1);
		DmWrite(appInfoP,
			(Int32) ((UInt8 *) &appInfoP->tagCount - (UInt8 *) appInfoP),
			&newCount, sizeof(newCount));
		id = newCount;
	}
	MemPtrUnlock(appInfoP);
	return id;
}

Boolean A35TagName(DmOpenRef dbP, UInt16 tagId, char *out)
{
	A35AppInfoPtr appInfoP = A35AppInfoGetPtr(dbP);
	Boolean ok = false;

	out[0] = '\0';
	if (appInfoP != NULL) {
		if (tagId >= 1 && tagId <= appInfoP->tagCount) {
			StrNCopy(out, appInfoP->tagNames[tagId - 1], a35TagNameLen - 1);
			out[a35TagNameLen - 1] = '\0';
			ok = true;
		}
		MemPtrUnlock(appInfoP);
	}
	return ok;
}

/* ------------------------------------------------------------------ *
 *  database open / create
 * ------------------------------------------------------------------ */
Err A35GetDatabase(DmOpenRef *dbPP, UInt16 mode)
{
	DmOpenRef dbP;
	Err err = errNone;

	// custom
	LocalID database_id;
	database_id = DmFindDatabase(0, a35DBName);
	if (database_id != 0) {
		dbP = DmOpenDatabase(0, database_id, mode);
	} else {
		dbP = NULL;
	}

	// dbP = DmOpenDatabaseByTypeCreator(a35DBType, a35Creator, mode);
	if (dbP == NULL) {
		err = DmCreateDatabase(0, a35DBName, a35Creator, a35DBType, false);
		if (err != errNone)
			return err;

		// custom
		database_id = DmFindDatabase(0, a35DBName);
		dbP = DmOpenDatabase(0, database_id, mode);

		// dbP = DmOpenDatabaseByTypeCreator(a35DBType, a35Creator, mode);
		if (dbP == NULL)
			return DmGetLastErr();

		/* mark for HotSync backup + initialize the AppInfo block */
		{
			UInt16 cardNo = 0;
			LocalID dbID = 0;
			UInt16 attr = 0;

			if (DmOpenDatabaseInfo(dbP, &dbID, NULL, NULL, &cardNo, NULL) == errNone) {
				DmDatabaseInfo(cardNo, dbID, NULL, &attr, NULL, NULL, NULL, NULL,
					NULL, NULL, NULL, NULL, NULL);
				attr |= dmHdrAttrBackup;
				DmSetDatabaseInfo(cardNo, dbID, NULL, &attr, NULL, NULL, NULL,
					NULL, NULL, NULL, NULL, NULL, NULL);
			}
		}
		A35AppInfoInit(dbP);
	}

	*dbPP = dbP;
	return errNone;
}

/* ------------------------------------------------------------------ *
 *  transient plaintext inbox (beam receive drop box)
 *
 * The beam-receive sublaunch runs with NO app globals and cannot load the crypto
 * segment; touching gAddrDB / the session key / the goto-display path there faults.
 * So it just drops received records, as plaintext, into this side database. The next
 * full launch (or list reload) merges them into the real DB and deletes the inbox.
 * Everything here is globals-free + crypto-free + main.
 * ------------------------------------------------------------------ */
Err A35OpenPlainDB(DmOpenRef *dbPP, Boolean create)
{
	LocalID id = DmFindDatabase(0, a35PlainDBName);

	*dbPP = NULL;
	if (id == 0) {
		if (!create)
			return dmErrCantFind;
		if (DmCreateDatabase(0, a35PlainDBName, a35Creator, a35PlainDBType, false)
				!= errNone)
			return DmGetLastErr();
		id = DmFindDatabase(0, a35PlainDBName);
		if (id == 0)
			return dmErrCantFind;
	}
	*dbPP = DmOpenDatabase(0, id, dmModeReadWrite);
	return *dbPP ? errNone : DmGetLastErr();
}

/* Append the RAW received bytes (unparsed vCard text) as one inbox record. This is
 * the ONLY thing the beam sublaunch writes: pure DB I/O, no parser, no sort, no
 * AppInfo, no globals - so nothing A5-relative is touched in the no-globals context.
 * A35MergePlainDB (Transfer.c, next full launch) parses these. */
void A35InboxAppendRaw(DmOpenRef pdb, const void *bytes, UInt32 len, UInt16 category)
{
	UInt16 idx = dmMaxRecordIndex;
	MemHandle h;

	if (pdb == NULL || len == 0)
		return;
	h = DmNewRecord(pdb, &idx, len);
	if (h != NULL) {
		void *p = MemHandleLock(h);
		DmWrite(p, 0, bytes, len);
		MemHandleUnlock(h);
		DmReleaseRecord(pdb, idx, true);
		/* Carry the receive-time category pick (from the accept dialog) in the inbox
		 * record's own category attr, so the merge files every beamed contact there. */
		if (category) {
			UInt16 attr = 0;
			DmRecordInfo(pdb, idx, &attr, NULL, NULL);
			attr = (UInt16)((attr & ~dmRecAttrCategoryMask) | (category & dmRecAttrCategoryMask));
			DmSetRecordInfo(pdb, idx, &attr, NULL);
		}
	}
}

/* ------------------------------------------------------------------ *
 *  record CRUD
 * ------------------------------------------------------------------ */
/* Copy the stored record into a private dynamic buffer and, if it is marked
 * encrypted, decrypt each payload field value in place (nonce = uniqueID + field
 * index). The returned view points into that buffer; free it via A35ReleaseRecord.
 * A private buffer (not the zero-copy storage handle) is required because storage
 * is read-only and concurrent/nested A35GetRecord views must not share a buffer.
 * When the record is encrypted but Sec35 is locked, encrypted values are blanked
 * so the UI shows nothing rather than ciphertext garbage. */
Err A35GetRecord(DmOpenRef dbP, UInt16 index, A35Record *out, MemHandle *recordH)
{
	MemHandle dbH, bufH;
	const UInt8 *recP;
	UInt8 *bufP;
	Int32 size;

	dbH = DmQueryRecord(dbP, index);
	if (dbH == NULL)
		return DmGetLastErr();
	recP = (const UInt8 *) MemHandleLock(dbH);
	size = MemHandleSize(dbH);

	bufH = MemHandleNew(size);
	if (bufH == NULL) {
		MemHandleUnlock(dbH);
		return dmErrMemError;
	}
	bufP = (UInt8 *) MemHandleLock(bufH);
	MemMove(bufP, recP, size);
	MemHandleUnlock(dbH);				/* copied out; done with storage */

	if (bufP[1] & a35MiscEncrypted) {
		UInt8 key[ENC_KEY_BYTES];
		UInt32 uid = 0;
		Boolean haveKey = EncGetKey(key, a35EncTtl);
		UInt16 count = rdU16(bufP + 2), i;
		Int32 off = a35HdrSize;

		DmRecordInfo(dbP, index, NULL, &uid, NULL);
		for (i = 0; i < count; i++) {
			UInt8 t = bufP[off];
			UInt16 ln = rdU16(bufP + off + 2);
			if (ln && A35FieldEncrypted(t)) {
				if (haveKey)
					EncCrypt(key, bufP + off + 4, ln, uid, i);
				else {					/* locked: MASK with '******' (fit the field) */
					UInt16 stars = (ln > 7) ? 6 : (ln > 1 ? (UInt16)(ln - 1) : 0);
					MemSet(bufP + off + 4, stars, '*');	/* (ptr, numBytes, value) */
					bufP[off + 4 + stars] = 0;
				}
			}
			off += 4 + ln;
		}
		if (haveKey)
			MemSet(key, ENC_KEY_BYTES, 0);
	}

	A35Unpack(bufP, out);				/* view points into the (locked) buffer */
	*recordH = bufH;
	return errNone;
}

void A35ReleaseRecord(MemHandle recordH)
{
	if (recordH != NULL) {
		MemHandleUnlock(recordH);
		MemHandleFree(recordH);
	}
}

/* Encrypt a just-written record's payload in place, keyed by its now-assigned
 * uniqueID. No-op (leaves it plaintext) if Sec35 is locked. Called after the
 * record is attached, so DmRecordInfo returns the final uniqueID. */
void A35EncryptRecordInPlace(DmOpenRef dbP, UInt16 index)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt32 uid = 0;
	MemHandle h;
	UInt8 *recP, *tmp;
	Int32 size;
	UInt16 count, i;
	Int32 off;

	if (!EncGetKey(key, a35EncTtl))
		return;							/* locked -> store plaintext for now */

	DmRecordInfo(dbP, index, NULL, &uid, NULL);
	h = DmGetRecord(dbP, index);
	if (h == NULL) { MemSet(key, ENC_KEY_BYTES, 0); return; }
	recP = (UInt8 *) MemHandleLock(h);
	size = MemHandleSize(h);
	tmp = (UInt8 *) MemPtrNew(size);
	if (tmp == NULL) {
		MemHandleUnlock(h);
		DmReleaseRecord(dbP, index, false);
		MemSet(key, ENC_KEY_BYTES, 0);
		return;
	}
	MemMove(tmp, recP, size);
	tmp[1] |= a35MiscEncrypted;
	count = rdU16(tmp + 2);
	off = a35HdrSize;
	for (i = 0; i < count; i++) {
		UInt8 t = tmp[off];
		UInt16 ln = rdU16(tmp + off + 2);
		if (ln && A35FieldEncrypted(t))
			EncCrypt(key, tmp + off + 4, ln, uid, i);
		off += 4 + ln;
	}
	DmWrite(recP, 0, tmp, size);
	MemPtrFree(tmp);
	MemHandleUnlock(h);
	DmReleaseRecord(dbP, index, true);
	MemSet(key, ENC_KEY_BYTES, 0);
}

/* Seal EVERY still-plaintext record (records created while locked, or beam/vCard
 * received - which never encrypt at creation). No-op when Sec35 is locked (no key).
 * Call on launch once unlocked so an unencrypted record can never linger: it becomes
 * encrypted the next time the password is provided. Only touches records WITHOUT the
 * enc bit (A35EncryptRecordInPlace would double-encrypt an already-sealed one). */
void A35SealAllPlaintext(DmOpenRef dbP)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt16 i, n;

	if (!EncGetKey(key, a35EncTtl))			/* locked -> nothing we can do yet */
		return;
	MemSet(key, ENC_KEY_BYTES, 0);
	n = DmNumRecords(dbP);
	for (i = 0; i < n; i++) {
		MemHandle h = DmQueryRecord(dbP, i);
		if (h != NULL) {
			Boolean enc = (Boolean) (((const UInt8 *) MemHandleLock(h))[1]
				& a35MiscEncrypted);
			MemHandleUnlock(h);
			if (!enc)
				A35EncryptRecordInPlace(dbP, i);	/* plaintext -> encrypted */
		}
	}
}

UInt16 A35FindSortPosition(DmOpenRef dbP, void *packedRecordP)
{
	return DmFindSortPosition(dbP, packedRecordP, NULL,
		(DmComparF *) A35ComparePackedRecords, A35SortArgFromApp(dbP));
}

Err A35NewRecord(DmOpenRef dbP, A35Record *r, UInt16 *index)
{
	MemHandle tmpH, recordH;
	Int32 size;
	UInt8 *tmpP, *recP;
	UInt16 newIndex;
	Err err;

	tmpH = A35BuildPacked(r);
	if (tmpH == NULL)
		return dmErrMemError;
	size = MemHandleSize(tmpH);

	recordH = DmNewHandle(dbP, size);
	if (recordH == NULL) {
		MemHandleFree(tmpH);
		return dmErrMemError;
	}
	tmpP = (UInt8 *) MemHandleLock(tmpH);
	recP = (UInt8 *) MemHandleLock(recordH);
	DmWrite(recP, 0, tmpP, size);
	newIndex = A35FindSortPosition(dbP, recP);
	MemHandleUnlock(recordH);
	MemHandleUnlock(tmpH);
	MemHandleFree(tmpH);

	err = DmAttachRecord(dbP, &newIndex, recordH, 0);
	if (err != errNone)
		MemHandleFree(recordH);
	else
		*index = newIndex;
	/* NOTE: no encryption here - A35NewRecord is also reached by the no-globals
	 * beam-receive sublaunch (sec003 unloaded). FOREGROUND callers seal explicitly
	 * with A35EncryptRecordInPlace; received records stay plaintext until edited. */
	return err;
}

/* Mirror of the ROM AddrChangeRecord reposition: build the new record in its own
 * storage chunk, decide whether its sort key moved by comparing to the immediate
 * neighbours, move the slot if needed, then DmAttachRecord to replace the record
 * at its index - which PRESERVES the unique id (relationships depend on it) and
 * the category. */
Err A35ChangeRecord(DmOpenRef dbP, UInt16 *index, A35Record *r)
{
	MemHandle tmpH, recordH, oldH = 0;
	Int32 size;
	UInt8 *tmpP, *recP, *cmpP;
	Boolean dontMove;
	Int16 sortArg;
	UInt16 i, attributes;
	Err result;

	tmpH = A35BuildPacked(r);
	if (tmpH == NULL)
		return dmErrMemError;
	size = MemHandleSize(tmpH);

	recordH = DmNewHandle(dbP, size);
	if (recordH == NULL) {
		MemHandleFree(tmpH);
		return dmErrMemError;
	}
	recP = (UInt8 *) MemHandleLock(recordH);
	tmpP = (UInt8 *) MemHandleLock(tmpH);
	DmWrite(recP, 0, tmpP, size);
	MemHandleUnlock(tmpH);
	MemHandleFree(tmpH);

	sortArg = A35SortArgFromApp(dbP);

	/* In sorted order iff prev < this < next. */
	if (*index > 0) {
		cmpP = (UInt8 *) MemHandleLock(DmQueryRecord(dbP, (UInt16) (*index - 1)));
		dontMove = (Boolean) (A35ComparePackedRecords(cmpP, recP, sortArg,
			NULL, NULL, 0) < 0);
		MemPtrUnlock(cmpP);
	} else
		dontMove = true;

	if ((UInt16) (*index + 1) < DmNumRecords(dbP)) {
		DmRecordInfo(dbP, (UInt16) (*index + 1), &attributes, NULL, NULL);
		if (!(attributes & dmRecAttrDelete)) {
			cmpP = (UInt8 *) MemHandleLock(DmQueryRecord(dbP, (UInt16) (*index + 1)));
			dontMove = (Boolean) (dontMove &&
				A35ComparePackedRecords(recP, cmpP, sortArg, NULL, NULL, 0) < 0);
			MemPtrUnlock(cmpP);
		}
	}

	if (!dontMove) {
		i = A35FindSortPosition(dbP, recP);
		DmMoveRecord(dbP, *index, i);
		if (i > *index)
			i--;
		*index = i;
	}

	result = DmAttachRecord(dbP, index, recordH, &oldH);
	MemHandleUnlock(recordH);
	if (result != errNone) {
		MemHandleFree(recordH);
		return result;
	}
	if (oldH != NULL)
		MemHandleFree(oldH);
	A35EncryptRecordInPlace(dbP, *index);		/* re-seal payload after the edit */
	return errNone;
}

Err A35DuplicateRecord(DmOpenRef dbP, UInt16 srcIndex, UInt16 *newIndex)
{
	A35Record rec;
	MemHandle h;
	UInt16 idx = dmMaxRecordIndex, srcAttr = 0, attr = 0;
	Err result;

	/* Duplicate from the DECRYPTED view: the copy gets a new uniqueID, and the
	 * payload nonce is keyed to the uid, so copying raw bytes would decrypt wrong. */
	if (A35GetRecord(dbP, srcIndex, &rec, &h) != errNone)
		return dmErrIndexOutOfRange;
	DmRecordInfo(dbP, srcIndex, &srcAttr, NULL, NULL);		/* source category */
	result = A35NewRecord(dbP, &rec, &idx);					/* plaintext, sorted insert */
	A35ReleaseRecord(h);
	if (result != errNone)
		return result;

	DmRecordInfo(dbP, idx, &attr, NULL, NULL);				/* same category, dirty */
	attr = (UInt16) ((attr & ~dmRecAttrCategoryMask) |
		(srcAttr & dmRecAttrCategoryMask) | dmRecAttrDirty);
	DmSetRecordInfo(dbP, idx, &attr, NULL);
	A35EncryptRecordInPlace(dbP, idx);						/* seal under the new uid */
	if (newIndex)
		*newIndex = idx;
	return errNone;
}

Err A35ChangeSortOrder(DmOpenRef dbP, UInt8 primary, UInt8 secondary)
{
	A35AppInfoPtr appInfoP = A35AppInfoGetPtr(dbP);

	if (primary > a35sortCompany)   primary = a35sortLast;
	if (secondary > a35sortCompany) secondary = a35sortFirst;
	if (appInfoP != NULL) {
		DmWrite(appInfoP,
			(Int32) ((UInt8 *) &appInfoP->primarySort - (UInt8 *) appInfoP),
			&primary, sizeof(primary));
		DmWrite(appInfoP,
			(Int32) ((UInt8 *) &appInfoP->secondarySort - (UInt8 *) appInfoP),
			&secondary, sizeof(secondary));
		MemPtrUnlock(appInfoP);
	}
	DmQuickSort(dbP, (DmComparF *) A35ComparePackedRecords,
		A35SortArg(primary, secondary));
	return errNone;
}

void A35GetSortOrder(DmOpenRef dbP, UInt8 *primary, UInt8 *secondary)
{
	A35AppInfoPtr appInfoP = A35AppInfoGetPtr(dbP);
	UInt8 pri = a35sortLast, sec = a35sortFirst;

	if (appInfoP != NULL) {
		pri = appInfoP->primarySort;
		sec = appInfoP->secondarySort;
		MemPtrUnlock(appInfoP);
		if (pri > a35sortCompany) pri = a35sortLast;
		if (sec > a35sortCompany) sec = a35sortFirst;
	}
	if (primary)   *primary = pri;
	if (secondary) *secondary = sec;
}

/* ---- reciprocal relationships ------------------------------------------ */
/* the type the OTHER record should carry back: child<->parent,
 * assistant<->manager; the rest (spouse/partner/sibling/friend/other) are
 * symmetric. */
UInt8 A35ReciprocalRelType(UInt8 t)
{
	switch (t) {
	case a35relChild:		return a35relParent;
	case a35relParent:		return a35relChild;
	case a35relAssistant:	return a35relManager;
	case a35relManager:		return a35relAssistant;
	default:				return t;
	}
}

/* Ensure the record identified by targetUid carries a relation back to backUid
 * (added with recipType if absent; left as-is if a link to backUid already
 * exists). Works on a private copy so the source DB handle is never locked
 * across the A35ChangeRecord that replaces it. */
Err A35AddReciprocalRelation(DmOpenRef dbP, UInt32 targetUid, UInt32 backUid,
	UInt8 recipType)
{
	UInt16 idx, i;
	MemHandle h;
	A35Record rec;
	Boolean exists = false;
	UInt8 packed[a35RelationPackLen];
	Err err;

	if (backUid == 0 || targetUid == 0 || targetUid == backUid)
		return errNone;
	if (!A35FindByUniqueID(dbP, targetUid, &idx))
		return dmErrCantFind;
	/* read DECRYPTED: A35ChangeRecord below re-packs from this view and re-encrypts,
	 * so feeding it ciphertext (raw DmQueryRecord) would double-encrypt. */
	if (A35GetRecord(dbP, idx, &rec, &h) != errNone)
		return dmErrCantFind;

	for (i = 0; i < rec.fieldCount; i++) {
		A35Relation rel;
		if (rec.fields[i].type == a35fRelation &&
			A35UnpackRelation(rec.fields[i].value, rec.fields[i].len, &rel) &&
			rel.targetUniqueID == backUid) {
			exists = true;
			break;
		}
	}
	if (exists) {
		A35ReleaseRecord(h);
		return errNone;
	}
	{
		A35Relation rel;
		rel.targetUniqueID = backUid;
		rel.relType = recipType;
		A35PackRelation(&rel, packed);
	}
	A35AddField(&rec, a35fRelation, 0, packed, a35RelationPackLen);
	err = A35ChangeRecord(dbP, &idx, &rec);		/* re-packs plaintext + re-encrypts */
	A35ReleaseRecord(h);						/* rec's views stay valid until here */
	return err;
}
