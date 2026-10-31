/*
 * DavView.c - see DavView.h. Reads Address35DAV_<n> in place (Palm databases
 * are read from storage, so a big address book costs nothing to open).
 *
 * Record (version 2, written by CalDAV Sync's ContactDb.c): version, flags
 * (bit 0 text encrypted, bit 1 list value encrypted), list value length n and
 * n bytes, three readable sort names (u8 length + bytes: last, first,
 * company), then the CDDV_CONTACT_ITEM body: uid, displayName, sortName
 * (strings: u16 BE length + bytes), nDates u8 and {year u16, month, day,
 * kind, name str} each, nCategories u8 and strings, text str. Text and list
 * value are Sec35 AES-CTR, nonce ('CARD' / 'LIST', record uniqueID).
 */
#include <PalmOS.h>
#include "Main.h"
#include "DavView.h"
#include "enccore.h"

#define davEncTtl		1000000000UL
#define davNonceCard	0x43415244UL
#define davNonceList	0x4C495354UL
#define davNonceAlias	0x414C4941UL
#define davFlagText		0x01
#define davFlagList		0x02
#define davHead "BEGIN:VCARD\nVERSION:3.0\n"
#define davTail "END:VCARD\n"

static void DavName(UInt8 slot, char *name) SEC003;
static UInt16 DavStrAt(const UInt8 *p, UInt16 len, UInt16 pos) SEC003;
static UInt16 DavTextAt(const UInt8 *r, UInt16 len, UInt16 *tl) SEC003;

static void DavName(UInt8 slot, char *name)
{
	StrPrintF(name, "Address35DAV_%d", (Int16) (slot + 1));
}

/* position after the string at pos, or 0xFFFF */
static UInt16 DavStrAt(const UInt8 *p, UInt16 len, UInt16 pos)
{
	UInt16 n;
	if ((UInt32) pos + 2 > len) return 0xFFFF;
	n = (UInt16) (((UInt16) p[pos] << 8) | p[pos + 1]);
	if ((UInt32) pos + 2 + n > len) return 0xFFFF;
	return (UInt16) (pos + 2 + n);
}

/* offset of the text bytes in record r; its length in *tl; 0xFFFF if malformed */
static UInt16 DavTextAt(const UInt8 *r, UInt16 len, UInt16 *tl)
{
	UInt16 pos, k, nd, nc;
	if (len < 3 || r[0] != 2) return 0xFFFF;
	pos = (UInt16) (3 + r[2]);
	for (k = 0; k < 3; k++) {
		if (pos >= len) return 0xFFFF;
		pos = (UInt16) (pos + 1 + r[pos]);
	}
	for (k = 0; k < 3 && pos != 0xFFFF; k++) pos = DavStrAt(r, len, pos);	/* uid, display, sort */
	if (pos == 0xFFFF || pos >= len) return 0xFFFF;
	nd = r[pos++];
	for (k = 0; k < nd && pos != 0xFFFF; k++) {
		if ((UInt32) pos + 5 > len) return 0xFFFF;
		pos = DavStrAt(r, len, (UInt16) (pos + 5));
	}
	if (pos == 0xFFFF || pos >= len) return 0xFFFF;
	nc = r[pos++];
	for (k = 0; k < nc && pos != 0xFFFF; k++) pos = DavStrAt(r, len, pos);
	if (pos == 0xFFFF || (UInt32) pos + 2 > len) return 0xFFFF;
	*tl = (UInt16) (((UInt16) r[pos] << 8) | r[pos + 1]);
	if ((UInt32) pos + 2 + *tl > len) return 0xFFFF;
	return (UInt16) (pos + 2);
}

DmOpenRef DavOpen(UInt8 slot)
{
	char name[dmDBNameLength];
	LocalID id;
	DavName(slot, name);
	id = DmFindDatabase(0, name);
	if (!id) return NULL;
	return DmOpenDatabase(0, id, dmModeReadOnly);
}

UInt16 DavCount(UInt8 slot)
{
	DmOpenRef db = DavOpen(slot);
	UInt16 n;
	if (!db) return 0;
	n = DmNumRecords(db);
	DmCloseDatabase(db);
	return n;
}

Int16 DavSlotLabel(UInt8 slot, char *out)
{
	char name[dmDBNameLength];
	LocalID id, ai;
	MemPtr p;
	const UInt8 *x;
	UInt16 al;
	Int16 res = davSlotOk;
	UInt8 key[ENC_KEY_BYTES];
	char alias[26];

	DavName(slot, name);
	id = DmFindDatabase(0, name);
	if (!id) return davSlotNone;
	if (DmDatabaseInfo(0, id, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ai, NULL, NULL, NULL) != errNone || !ai)
		return davSlotNone;
	p = MemLocalIDToLockedPtr(ai, 0);
	if (!p) return davSlotNone;
	x = (const UInt8 *) p + sizeof(AppInfoType);
	if (x[2]) {												/* stale */
		MemPtrUnlock(p);
		return davSlotNone;
	}
	StrCopy(out, x[1] == 4 ? "iCloud" : "CardDAV");
	al = x[3];
	if (al > 24) al = 24;
	alias[0] = '\0';
	if (al && EncGetKey(key, davEncTtl)) {
		MemMove(alias, x + 4, al);
		EncCrypt(key, (UInt8 *) alias, al, davNonceAlias, slot);
		alias[al] = '\0';
		MemSet(key, sizeof(key), 0);
	} else if (al) {
		res = davSlotLocked;
	}
	MemPtrUnlock(p);
	if (alias[0]) {
		StrCat(out, " - ");
		StrCat(out, alias);
	}
	MemSet(alias, sizeof(alias), 0);
	return res;
}

UInt16 DavBuild(UInt8 slot, UInt16 cat, const char *term, UInt16 *out, UInt16 cap)
{
	DmOpenRef db = DavOpen(slot);
	UInt16 i, n, k = 0, tl = (UInt16) StrLen(term);
	char lower[34];

	if (!db) return 0;
	if (tl > sizeof(lower) - 1) tl = sizeof(lower) - 1;
	for (i = 0; i < tl; i++) {
		char c = term[i];
		lower[i] = (char) ((c >= 'A' && c <= 'Z') ? c + 32 : c);
	}
	n = DmNumRecords(db);
	for (i = 0; i < n && k < cap; i++) {
		UInt16 attr;
		MemHandle h;
		const UInt8 *r;
		if (DmRecordInfo(db, i, &attr, NULL, NULL) != errNone) continue;
		if (attr & dmRecAttrDelete) continue;
		if (cat != dmAllCategories && (attr & dmRecAttrCategoryMask) != cat) continue;
		if (tl) {
			Boolean hit = false;
			UInt16 pos, f;
			h = DmQueryRecord(db, i);
			if (!h) continue;
			r = (const UInt8 *) MemHandleLock(h);
			if (MemHandleSize(h) >= 3 && r[0] == 2) {
				pos = (UInt16) (3 + r[2]);
				for (f = 0; f < 3 && !hit; f++) {
					UInt16 nl = r[pos], j, m;
					for (j = 0; j + tl <= nl && !hit; j++) {
						for (m = 0; m < tl; m++) {
							char c = (char) r[pos + 1 + j + m];
							if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
							if (c != lower[m]) break;
						}
						if (m == tl) hit = true;
					}
					pos = (UInt16) (pos + 1 + nl);
				}
			}
			MemHandleUnlock(h);
			if (!hit) continue;
		}
		out[k++] = i;
	}
	DmCloseDatabase(db);
	return k;
}

Boolean DavRow(UInt8 slot, UInt16 idx, char *last, char *first, char *company,
	char *side, Boolean *locked)
{
	DmOpenRef db = DavOpen(slot);
	MemHandle h;
	const UInt8 *r;
	UInt16 len, pos, f;
	char *dst[3];

	last[0] = first[0] = company[0] = side[0] = '\0';
	*locked = false;
	if (!db) return false;
	h = DmQueryRecord(db, idx);
	if (!h) { DmCloseDatabase(db); return false; }
	r = (const UInt8 *) MemHandleLock(h);
	len = (UInt16) MemHandleSize(h);
	if (len >= 3 && r[0] == 2) {
		UInt16 ln = r[2];
		dst[0] = last; dst[1] = first; dst[2] = company;
		if (ln && ln < davSideMax && (r[1] & davFlagList)) {
			UInt8 key[ENC_KEY_BYTES];
			UInt32 uid = 0;
			DmRecordInfo(db, idx, NULL, &uid, NULL);
			if (EncGetKey(key, davEncTtl)) {
				MemMove(side, r + 3, ln);
				EncCrypt(key, (UInt8 *) side, ln, davNonceList, uid);
				side[ln] = '\0';
				MemSet(key, sizeof(key), 0);
			} else {
				*locked = true;
			}
		} else if (ln && ln < davSideMax) {
			MemMove(side, r + 3, ln);
			side[ln] = '\0';
		}
		pos = (UInt16) (3 + ln);
		for (f = 0; f < 3 && pos < len; f++) {
			UInt16 nl = r[pos];
			if (nl >= davNameMax) nl = davNameMax - 1;
			MemMove(dst[f], r + pos + 1, nl);
			dst[f][nl] = '\0';
			pos = (UInt16) (pos + 1 + r[pos]);
		}
	}
	MemHandleUnlock(h);
	DmCloseDatabase(db);
	return true;
}

Int16 DavLoad(UInt8 slot, UInt16 idx, A35Record *rec, VCardArena *arena)
{
	DmOpenRef db = DavOpen(slot);
	MemHandle h;
	const UInt8 *r;
	UInt16 len, to, tl;
	Int16 res = 2;
	char *buf;

	if (!db) return 2;
	h = DmQueryRecord(db, idx);
	if (!h) { DmCloseDatabase(db); return 2; }
	r = (const UInt8 *) MemHandleLock(h);
	len = (UInt16) MemHandleSize(h);
	to = DavTextAt(r, len, &tl);
	if (to != 0xFFFF) {
		buf = (char *) MemPtrNew((UInt32) tl + sizeof(davHead) + sizeof(davTail) + 1);
		if (buf) {
			Boolean ok = true;
			StrCopy(buf, davHead);
			MemMove(buf + sizeof(davHead) - 1, r + to, tl);
			if (r[1] & davFlagText) {
				UInt8 key[ENC_KEY_BYTES];
				UInt32 uid = 0;
				DmRecordInfo(db, idx, NULL, &uid, NULL);
				if (EncGetKey(key, davEncTtl)) {
					EncCrypt(key, (UInt8 *) buf + sizeof(davHead) - 1, tl, davNonceCard, uid);
					MemSet(key, sizeof(key), 0);
				} else {
					ok = false;
					res = 1;
				}
			}
			if (ok) {
				StrCopy(buf + sizeof(davHead) - 1 + tl, davTail);
				arena->used = 0;
				MemSet(rec, sizeof(A35Record), 0);
				if (A35VCardToRecord(gAddrDB, buf, (UInt32) StrLen(buf), rec, arena))
					res = 0;
			}
			MemSet(buf, tl, 0);
			MemPtrFree(buf);
		}
	}
	MemHandleUnlock(h);
	DmCloseDatabase(db);
	return res;
}

/* ---- "Me" per book ---- */
static UInt32 gMe[davMaxSlots];			/* hash of the Me contact's UID, 0 = none */
static Boolean gMeLoaded = false;
static UInt8 gViewSlot = 0;
static UInt16 gViewIdx = 0xFFFF;

static UInt32 DavUidHash(const UInt8 *r, UInt16 len) SEC003;
static void DavMeLoad(void) SEC003;

/* sdbm hash of the UID string of record r (len bytes); never 0 */
static UInt32 DavUidHash(const UInt8 *r, UInt16 len)
{
	UInt16 pos, f, n, i;
	UInt32 h = 0;

	if (len < 3 || r[0] != 2) return 0;
	pos = (UInt16) (3 + r[2]);
	for (f = 0; f < 3 && pos < len; f++) pos = (UInt16) (pos + 1 + r[pos]);
	if ((UInt32) pos + 2 > len) return 0;
	n = (UInt16) (((UInt16) r[pos] << 8) | r[pos + 1]);
	if ((UInt32) pos + 2 + n > len) return 0;
	for (i = 0; i < n; i++) h = (UInt32) r[pos + 2 + i] + (h << 6) + (h << 16) - h;
	return h ? h : 1;
}

static void DavMeLoad(void)
{
	UInt16 size = sizeof(gMe);

	if (gMeLoaded) return;
	gMeLoaded = true;
	if (PrefGetAppPreferences(a35PrefCreator, a35DavMePrefID, gMe, &size, true) == noPreferenceFound ||
		size < sizeof(gMe))
		MemSet(gMe, sizeof(gMe), 0);
}

UInt16 DavMeFind(UInt8 slot)
{
	DmOpenRef db;
	UInt16 i, n, found = 0xFFFF;

	DavMeLoad();
	if (slot >= davMaxSlots || gMe[slot] == 0 || (db = DavOpen(slot)) == NULL) return 0xFFFF;
	n = DmNumRecords(db);
	for (i = 0; i < n && found == 0xFFFF; i++) {
		MemHandle h = DmQueryRecord(db, i);
		if (h) {
			if (DavUidHash((const UInt8 *) MemHandleLock(h), (UInt16) MemHandleSize(h)) == gMe[slot]) found = i;
			MemHandleUnlock(h);
		}
	}
	DmCloseDatabase(db);
	return found;
}

void DavSetViewed(UInt8 slot, UInt16 idx)
{
	gViewSlot = slot;
	gViewIdx = idx;
}

void DavMeSetViewed(void)
{
	DmOpenRef db;
	MemHandle h;

	DavMeLoad();
	if (gViewSlot >= davMaxSlots || (db = DavOpen(gViewSlot)) == NULL) return;
	h = DmQueryRecord(db, gViewIdx);
	if (h) {
		UInt32 v = DavUidHash((const UInt8 *) MemHandleLock(h), (UInt16) MemHandleSize(h));
		MemHandleUnlock(h);
		if (v) {
			gMe[gViewSlot] = v;
			PrefSetAppPreferences(a35PrefCreator, a35DavMePrefID, 1, gMe, sizeof(gMe), true);
		}
	}
	DmCloseDatabase(db);
}

void DavGoTo(const GoToParamsType *gp)
{
	UInt8 slot = (UInt8) (gp->matchCustom & 0xFF);
	Int16 r = 2;

	if (slot < davMaxSlots && DavSlotLabel(slot, gDavTitle) != davSlotNone)
		r = DavLoad(slot, gp->recordNum, &gCardRecord, &gCardArena);
	if (r == 0) {
		DavSetViewed(slot, gp->recordNum);
		gViewingCard = true;
		FrmGotoForm(frmRecord);
		return;
	}
	gDavTitle[0] = '\0';
	FrmGotoForm(frmMain);
	if (r == 1)
		FrmAlert(altDavLocked);
	else
		FrmCustomAlert(altDavInfo, "That contact is no longer there.", "", "");
}

/* "Mob" etc. from the TYPE parameters in a TEL line's head (upper case) */
static const char *DavPhoneLabel(const char *head) SEC003;
static const char *DavPhoneLabel(const char *head)
{
	if (StrStr(head, "CELL") || StrStr(head, "MOBILE")) return "Mob";
	if (StrStr(head, "FAX")) return "Fax";
	if (StrStr(head, "PAGER")) return "Pgr";
	if (StrStr(head, "MAIN")) return "Main";
	if (StrStr(head, "HOME")) return "Hom";
	if (StrStr(head, "WORK")) return "Wrk";
	return "Oth";
}

/* TYPE= value of a property head (hup = upper case copy, raw = original case), quotes off */
static void DavTypeParam(const char *hup, const char *raw, char *out, UInt16 cap) SEC003;
static void DavTypeParam(const char *hup, const char *raw, char *out, UInt16 cap)
{
	const char *p = StrStr(hup, "TYPE=");
	UInt16 n = 0;

	out[0] = '\0';
	if (!p) return;
	p = raw + (p - hup) + 5;
	if (*p == '"') p++;
	while (*p && *p != ';' && *p != '"' && n < cap - 1) out[n++] = *p++;
	out[n] = '\0';
}

/* network name Address35 knows ("Instagram"...), NULL for any other (same matching as vcard.c) */
static const char *DavSocialName(const char *tyUp) SEC003;
static const char *DavSocialName(const char *tyUp)
{
	if (StrStr(tyUp, "INSTAGRAM")) return "Instagram";
	if (StrStr(tyUp, "FACEBOOK")) return "Facebook";
	if (StrStr(tyUp, "WHATSAPP")) return "WhatsApp";
	if (StrStr(tyUp, "TWITTER") || (tyUp[0] == 'X' && tyUp[1] == '\0')) return "X";
	if (StrStr(tyUp, "TELEGRAM")) return "Telegram";
	if (StrStr(tyUp, "SIGNAL")) return "Signal";
	return NULL;
}

Int16 DavItems(UInt8 slot, UInt16 idx, DavItem *out, UInt8 cap, UInt8 *n)
{
	DmOpenRef db = DavOpen(slot);
	MemHandle h;
	const UInt8 *r;
	UInt16 len, to, tl, i, e;
	Int16 res = 2;
	char *t;

	*n = 0;
	if (!db) return 2;
	h = DmQueryRecord(db, idx);
	if (!h) { DmCloseDatabase(db); return 2; }
	r = (const UInt8 *) MemHandleLock(h);
	len = (UInt16) MemHandleSize(h);
	if (len >= 3 && r[0] == 2 && (to = DavTextAt(r, len, &tl)) != 0xFFFF && (t = (char *) MemPtrNew((UInt32) tl + 1)) != NULL) {
		MemMove(t, r + to, tl);
		res = 0;
		if (r[1] & davFlagText) {
			UInt8 key[ENC_KEY_BYTES];
			UInt32 uid = 0;
			DmRecordInfo(db, idx, NULL, &uid, NULL);
			if (EncGetKey(key, davEncTtl)) {
				EncCrypt(key, (UInt8 *) t, tl, davNonceCard, uid);
				MemSet(key, sizeof(key), 0);
			} else {
				res = 1;
			}
		}
		t[tl] = '\0';
		for (i = 0; res == 0 && i < tl && *n < cap; i = (UInt16) (e + 1)) {
			char head[48], raw[48];
			UInt16 colon = 0xFFFF, k, hl;
			Int16 kind = -1;			/* 0 TEL, 1 EMAIL, 2 social / IM */
			char ty[24];
			const char *fixed = NULL;		/* network forced by the property name */
			for (e = i; e < tl && t[e] != '\n'; e++)
				if (t[e] == ':' && colon == 0xFFFF) colon = e;
			if (colon == 0xFFFF || colon == i) continue;
			hl = (UInt16) (colon - i);
			if (hl > sizeof(head) - 1) hl = sizeof(head) - 1;
			for (k = 0; k < hl; k++) {
				char c = t[i + k];
				raw[k] = c;
				head[k] = (c >= 'a' && c <= 'z') ? (char) (c - 32) : c;
			}
			head[hl] = raw[hl] = '\0';
			#define DAVPROP(nm) (StrNCompare(head, nm, StrLen(nm)) == 0 && (head[StrLen(nm)] == ';' || head[StrLen(nm)] == '\0'))
			if (DAVPROP("TEL")) kind = 0;
			else if (DAVPROP("EMAIL")) kind = 1;
			else if (DAVPROP("IMPP") || DAVPROP("X-SOCIALPROFILE")) kind = 2;
			else if (DAVPROP("X-WHATSAPP")) { kind = 2; fixed = "WhatsApp"; }
			else if (DAVPROP("X-TELEGRAM")) { kind = 2; fixed = "Telegram"; }
			else if (DAVPROP("X-SIGNAL")) { kind = 2; fixed = "Signal"; }
			#undef DAVPROP
			if (kind >= 0) {
				UInt16 vl = (UInt16) (e - colon - 1);
				DavItem *it = &out[*n];
				if (vl > 0 && t[e - 1] == '\r') vl--;
				if (vl == 0) continue;
				if (kind == 2) {
					const char *net = fixed;
					char tyUp[24];
					DavTypeParam(head, raw, ty, sizeof(ty));
					for (k = 0; ty[k]; k++) tyUp[k] = (ty[k] >= 'a' && ty[k] <= 'z') ? (char) (ty[k] - 32) : ty[k];
					tyUp[k] = '\0';
					if (!net) net = DavSocialName(tyUp);
					it->kind = 1;
					if (net) {
						StrCopy(it->label, net);
						if (vl > sizeof(it->value) - 1) vl = sizeof(it->value) - 1;
						MemMove(it->value, t + colon + 1, vl);
						it->value[vl] = '\0';
					} else {
						/* a network Address35 doesn't know: "Network: handle", no label */
						UInt16 al;
						it->label[0] = '\0';
						StrNCopy(it->value, ty[0] ? ty : "Social", sizeof(it->value) - 3);
						it->value[sizeof(it->value) - 3] = '\0';
						StrCat(it->value, ": ");
						al = (UInt16) StrLen(it->value);
						if (vl > sizeof(it->value) - 1 - al) vl = (UInt16) (sizeof(it->value) - 1 - al);
						MemMove(it->value + al, t + colon + 1, vl);
						it->value[al + vl] = '\0';
					}
				} else {
					it->kind = 0;
					if (vl > sizeof(it->value) - 1) vl = sizeof(it->value) - 1;
					StrCopy(it->label, kind == 0 ? DavPhoneLabel(head) : "Email");
					MemMove(it->value, t + colon + 1, vl);
					it->value[vl] = '\0';
				}
				(*n)++;
			}
		}
		MemSet(t, tl, 0);
		MemPtrFree(t);
	}
	MemHandleUnlock(h);
	DmCloseDatabase(db);
	return res;
}
