/*
 * vcard.c - vCard 3.0/4.0 <-> Address35 record conversion (see vcard.h).
 *
 * Kept in the MAIN segment (no SEC tag): the beam receive/import path is a
 * no-globals sublaunch and must reach it. Because it is main, ordinary 32-bit
 * '/' and '%' are safe here (the libgcc-division-from-a-secondary-segment trap
 * does not apply). Import is one-card-at-a-time and NOT re-entrant (module-static
 * line buffer); the produced record references the caller's VCardArena.
 */
#include "vcard.h"
#include <VFSMgr.h>

/* ------------------------------------------------------------ IMPORT (parse) */

static char gLine[2400];			/* current unfolded logical line (main seg) */

/* Apple item1.PROP / item1.X-ABLabel pairing state: the group prefix + field
 * index touched by the MOST RECENTLY parsed line, so a following X-ABLabel line
 * in the SAME group can patch that field's label (custom Apple labels have no
 * TYPE= param of their own - the label text lives on this separate paired line). */
static char   gLastGroup[16] = "";
static UInt16 gLastFieldIdx = 0xffff;

/* case-insensitive: does name-part `p` start with property `name` (then ';' or ':')? */
static Boolean vcIsProp(const char *p, const char *name)
{
	while (*name) {
		char a = *p, b = *name;
		if (a >= 'a' && a <= 'z') a = (char) (a - 32);
		if (b >= 'a' && b <= 'z') b = (char) (b - 32);
		if (a != b) return false;
		p++; name++;
	}
	return (Boolean) (*p == '\0' || *p == ';' || *p == ':');
}

/* first TYPE= token of a name-part, lowercased into out */
static void vcTypeParam(const char *np, char *out, UInt16 outLen)
{
	const char *p = np;
	out[0] = '\0';
	while (*p) {
		if ((p == np || p[-1] == ';') &&
			(p[0]=='T'||p[0]=='t') && (p[1]=='Y'||p[1]=='y') &&
			(p[2]=='P'||p[2]=='p') && (p[3]=='E'||p[3]=='e') && p[4]=='=') {
			UInt16 o = 0;
			p += 5;
			if (*p == '"') p++;
			while (*p && *p != ';' && *p != ',' && *p != '"' &&
				o < (UInt16) (outLen - 1)) {
				char c = *p++;
				if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
				out[o++] = c;
			}
			out[o] = '\0';
			return;
		}
		p++;
	}
}

/* first TYPE= token, ORIGINAL case, into out */
static void vcTypeParamRaw(const char *np, char *out, UInt16 outLen)
{
	const char *p = np;
	out[0] = '\0';
	while (*p) {
		if ((p == np || p[-1] == ';') &&
			(p[0]=='T'||p[0]=='t') && (p[1]=='Y'||p[1]=='y') &&
			(p[2]=='P'||p[2]=='p') && (p[3]=='E'||p[3]=='e') && p[4]=='=') {
			UInt16 o = 0;
			p += 5;
			if (*p == '"') p++;
			while (*p && *p != ';' && *p != ',' && *p != '"' &&
				o < (UInt16) (outLen - 1)) out[o++] = *p++;
			out[o] = '\0';
			return;
		}
		p++;
	}
}

/* lowercase copy of the WHOLE name-part (property name + all params, up to the
 * value-starting ':'). Unlike vcTypeParam (first TYPE= token only), this lets a
 * keyword search find a token anywhere among vCard 3.0's repeated/comma-listed
 * TYPE= params - macOS Contacts exports "EMAIL;type=INTERNET;type=HOME:" (the
 * meaningful label is the SECOND token) and "TEL;type=CELL,VOICE:". */
static void vcLowerAll(const char *np, char *out, UInt16 outLen)
{
	UInt16 o = 0;
	while (*np && o < (UInt16) (outLen - 1)) {
		char c = *np++;
		if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
		out[o++] = c;
	}
	out[o] = '\0';
}

/* remember (group, just-added field index) for a following item1.X-ABLabel line;
 * called right after adding the field an Apple custom label could apply to */
static void vcTrackGroup(const A35Record *r, const char *grp)
{
	if (!grp[0]) { gLastGroup[0] = '\0'; return; }
	StrNCopy(gLastGroup, grp, sizeof(gLastGroup) - 1); gLastGroup[sizeof(gLastGroup) - 1] = '\0';
	gLastFieldIdx = (UInt16) (r->fieldCount ? r->fieldCount - 1 : 0xffff);
}

static const UInt8 *vcArena(VCardArena *a, const void *src, UInt16 len);

/* add an a35fCustom passthrough field storing "name\0text" (data preserved even
 * when there's no dedicated field/label for it); shared by the TEL raw-label
 * fallback, the RELATED no-match fallback, and the unknown-property fallback */
static void vcAddCustom(A35Record *r, VCardArena *arena, const char *name,
	const char *text)
{
	char store[320];
	UInt16 nl = (UInt16) StrLen(name), tl = (UInt16) StrLen(text);

	if (!nl || !tl || nl + 1 + tl > sizeof(store)) return;
	MemMove(store, name, nl + 1);
	MemMove(store + nl + 1, text, tl);
	{ const UInt8 *v = vcArena(arena, store, (UInt16) (nl + 1 + tl));
	  if (v) A35AddField(r, a35fCustom, 0, v, (UInt16) (nl + 1 + tl)); }
}

/* best-effort: find the record whose display name equals `name` (case-insens.) */
static Boolean vcFindByName(DmOpenRef dbP, const char *name, UInt16 *indexP)
{
	UInt16 total = DmNumRecords(dbP), i;
	for (i = 0; i < total; i++) {
		char dn[64];
		A35RecordDisplayName(dbP, i, dn, sizeof(dn));
		if (dn[0] && StrCaselessCompare(dn, name) == 0) { *indexP = i; return true; }
	}
	return false;
}

/* unescape a TEXT value segment src[0..n) into out (NUL-terminated, capped) */
static void vcUnescape(const char *src, UInt16 n, char *out, UInt16 outLen)
{
	UInt16 i = 0, o = 0;
	while (i < n && o < (UInt16) (outLen - 1)) {
		char c = src[i++];
		if (c == '\\' && i < n) {
			char e = src[i++];
			if (e == 'n' || e == 'N') out[o++] = '\n';
			else out[o++] = e;
		} else out[o++] = c;
	}
	out[o] = '\0';
}

/* idx-th ';'-separated component of a structured value, unescaped into out */
static void vcComponent(const char *val, UInt16 idx, char *out, UInt16 outLen)
{
	const char *p = val, *start;
	UInt16 field = 0;

	out[0] = '\0';
	while (field < idx && *p) {
		if (*p == '\\' && p[1]) p += 2;
		else if (*p == ';') { field++; p++; }
		else p++;
	}
	if (field < idx) return;
	start = p;
	while (*p) {
		if (*p == '\\' && p[1]) p += 2;
		else if (*p == ';') break;
		else p++;
	}
	vcUnescape(start, (UInt16) (p - start), out, outLen);
}

/* parse a vCard date ("YYYY-MM-DD", "YYYYMMDD", "--MM-DD", "--MMDD") into
 * ymd[0..8] = "YYYYMMDD" (year "0000" when absent). Returns true if m+d found. */
static Boolean vcParseDate(const char *s, char *ymd)
{
	UInt16 y = 0, m = 0, d = 0, dig = 0;
	const char *p = s;

	while (*p == ' ') p++;
	if (p[0] == '-' && p[1] == '-') {			/* no-year form */
		p += 2;
		while (*p == '-') p++;
		if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9')
			m = (UInt16) ((p[0]-'0')*10 + (p[1]-'0'));
		p += 2;
		while (*p == '-') p++;
		if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9')
			d = (UInt16) ((p[0]-'0')*10 + (p[1]-'0'));
	} else {
		while (*p >= '0' && *p <= '9' && dig < 4) { y = (UInt16) (y*10 + (*p++-'0')); dig++; }
		while (*p == '-') p++;
		if (p[0] >= '0' && p[0] <= '9') { m = (UInt16) ((p[0]-'0')*10 + (p[1]>='0'&&p[1]<='9'?p[1]-'0':0)); p += 2; }
		while (*p == '-') p++;
		if (p[0] >= '0' && p[0] <= '9') d = (UInt16) ((p[0]-'0')*10 + (p[1]>='0'&&p[1]<='9'?p[1]-'0':0));
	}
	if (m == 0 || d == 0) return false;
	ymd[0] = (char) ('0' + (y/1000)%10); ymd[1] = (char) ('0' + (y/100)%10);
	ymd[2] = (char) ('0' + (y/10)%10);   ymd[3] = (char) ('0' + y%10);
	ymd[4] = (char) ('0' + m/10);        ymd[5] = (char) ('0' + m%10);
	ymd[6] = (char) ('0' + d/10);        ymd[7] = (char) ('0' + d%10);
	ymd[8] = '\0';
	return true;
}

/* reserve len bytes in the arena, copy src, return a stable pointer (or NULL) */
static const UInt8 *vcArena(VCardArena *a, const void *src, UInt16 len)
{
	UInt8 *p;
	if ((UInt32) a->used + len + 1 > vcArenaLen) return NULL;
	p = (UInt8 *) (a->buf + a->used);
	if (len) MemMove(p, src, len);
	p[len] = '\0';
	a->used = (UInt16) (a->used + len + 1);
	return p;
}

static void vcAddStr(A35Record *r, VCardArena *a, UInt8 type, UInt8 label,
	const char *s)
{
	UInt16 len = (UInt16) StrLen(s);
	const UInt8 *v = vcArena(a, s, len);		/* vcArena appends a NUL at [len] */
	/* Store the trailing NUL in the field length, matching A35AddStr (StrLen+1):
	 * once packed, string fields have no inter-field terminator, so A35GetStr on a
	 * saved record would read past a non-NUL-terminated value (garbage on display). */
	if (v) A35AddField(r, type, label, v, (UInt16) (len + 1));
}

/* unescape src[0..n) DIRECTLY into the arena (no large stack buffer) and add it
 * as a field. Used for potentially long values (NOTE) on the small beam stack. */
static void vcAddUnescaped(A35Record *r, VCardArena *a, UInt8 type, UInt8 label,
	const char *src, UInt16 n)
{
	char *dst = a->buf + a->used;
	UInt16 room = (a->used < vcArenaLen) ? (UInt16) (vcArenaLen - a->used) : 0;
	UInt16 o = 0, i = 0;
	if (room < 2) return;
	while (i < n && o < (UInt16) (room - 1)) {
		char c = src[i++];
		if (c == '\\' && i < n) {
			char e = src[i++];
			dst[o++] = (e == 'n' || e == 'N') ? '\n' : e;
		} else dst[o++] = c;
	}
	dst[o] = '\0';
	if (o == 0) return;
	a->used = (UInt16) (a->used + o + 1);
	A35AddField(r, type, label, dst, (UInt16) (o + 1));	/* include NUL (see vcAddStr) */
}

static UInt8 vcPhoneLabel(const char *ty)
{
	if (StrStr(ty, "cell") || StrStr(ty, "mobile") || StrStr(ty, "iphone")) return a35plMobile;
	if (StrStr(ty, "home")) return a35plHome;
	if (StrStr(ty, "work")) return a35plWork;
	if (StrStr(ty, "fax")) return a35plFax;
	if (StrStr(ty, "pager")) return a35plPager;
	if (StrStr(ty, "voice") || StrStr(ty, "main")) return a35plMain;
	return a35plOther;
}

/* email label (a35fEmail); "icloud" checked before "other" since iCloud addresses
 * are commonly typed "TYPE=INTERNET,iCloud" - substring match on the whole blob */
static UInt8 vcEmailLabel(const char *ty)
{
	if (StrStr(ty, "icloud")) return a35elICloud;
	if (StrStr(ty, "work")) return a35elWork;
	if (StrStr(ty, "home")) return a35elHome;
	return a35elOther;
}

/* address label (a35fAddress); no-match/untyped defaults to Home, matching the
 * field's prior hardcoded behavior for sources that never send TYPE= at all */
static UInt8 vcAddrLabel(const char *ty)
{
	if (StrStr(ty, "work")) return a35alWork;
	if (StrStr(ty, "other")) return a35alOther;
	return a35alHome;
}

static UInt8 vcRelFromToken(const char *ty)
{
	/* ty is lower case: a TYPE= token (firmware-normalised) or an Apple label
	 * (Mother, Father, Sister, Brother, Son, Husband...) */
	if (StrStr(ty, "spouse") || StrStr(ty, "husband") || StrStr(ty, "wife")) return a35relSpouse;
	if (StrStr(ty, "partner") || StrStr(ty, "sweetheart")) return a35relPartner;
	if (StrStr(ty, "child") || StrStr(ty, "son") || StrStr(ty, "daughter")) return a35relChild;
	if (StrStr(ty, "parent") || StrStr(ty, "mother") || StrStr(ty, "father")) return a35relParent;
	if (StrStr(ty, "sibling") || StrStr(ty, "sister") || StrStr(ty, "brother")) return a35relSibling;
	if (StrStr(ty, "assistant") || StrStr(ty, "agent")) return a35relAssistant;
	if (StrStr(ty, "manager")) return a35relManager;
	if (StrStr(ty, "friend")) return a35relFriend;
	return a35relOther;
}

/* social network TYPE -> a35soc*; returns a35socOther and fills customName if
 * the network is not one of the known set. */
static UInt8 vcSocialLabel(const char *ty, const char *rawType, char *custom,
	UInt16 customLen)
{
	custom[0] = '\0';
	if (StrStr(ty, "instagram")) return a35socInstagram;
	if (StrStr(ty, "facebook")) return a35socFacebook;
	if (StrStr(ty, "whatsapp")) return a35socWhatsApp;
	if (StrStr(ty, "twitter") || (ty[0] == 'x' && ty[1] == '\0')) return a35socX;
	if (StrStr(ty, "telegram")) return a35socTelegram;
	if (StrStr(ty, "signal")) return a35socSignal;
	StrNCopy(custom, rawType[0] ? rawType : "Social", customLen - 1);
	custom[customLen - 1] = '\0';
	return a35socOther;
}

/* process one logical line into the record being built */
static void vcParseLine(DmOpenRef dbP, A35Record *r, VCardArena *arena,
	const char *line)
{
	const char *colon;
	char np[128];			/* name-part (before ':') */
	const char *value;
	char ty[128], raw[48];		/* ty sized to hold vcLowerAll(np, ...) in full */
	char grp[16];
	UInt16 npl;

	/* split at the first ':' that is not inside a quoted param */
	{
		const char *p = line;
		Boolean q = false;
		while (*p && (q || *p != ':')) { if (*p == '"') q = !q; p++; }
		if (*p != ':') return;
		colon = p;
	}
	npl = (UInt16) (colon - line);
	if (npl > sizeof(np) - 1) npl = sizeof(np) - 1;
	MemMove(np, line, npl); np[npl] = '\0';
	value = colon + 1;

	/* strip a group prefix "item1.TEL" -> "TEL", keeping a copy in grp[] for the
	 * X-ABLabel pairing check below (empty if this line has no group) */
	grp[0] = '\0';
	{
		char *dot = StrChr(np, '.');
		char *semi = StrChr(np, ';');
		if (dot && (!semi || dot < semi)) {
			UInt16 keep = (UInt16) (StrLen(np) - (UInt16) (dot + 1 - np));
			UInt16 gl = (UInt16) (dot - np);
			if (gl > sizeof(grp) - 1) gl = sizeof(grp) - 1;
			MemMove(grp, np, gl); grp[gl] = '\0';
			MemMove(np, dot + 1, keep + 1);
		}
	}

	/* Apple item1.X-ABLabel: patch the label of the field the paired item1.PROP
	 * line (immediately preceding, same group) just added. Only TEL/EMAIL/ADR/
	 * RELATED carry a matchable label enum; anything else is a no-op patch. */
	if (grp[0] && vcIsProp(np, "X-ABLabel") && StrCompare(grp, gLastGroup) == 0 &&
		gLastFieldIdx < r->fieldCount) {
		char lbl[48];
		const char *s = value;
		UInt16 n = (UInt16) StrLen(s);
		/* strip the "_$!<...>!$_" wrapper Apple puts around the label text */
		if (n >= 7 && StrNCompare(s, "_$!<", 4) == 0 && StrNCompare(s + n - 3, "!$_", 3) == 0)
			vcUnescape(s + 4, (UInt16) (n - 7), lbl, sizeof(lbl));
		else
			vcUnescape(s, n, lbl, sizeof(lbl));
		vcLowerAll(lbl, lbl, sizeof(lbl));		/* in-place: safe, 1:1 char copy */
		switch (r->fields[gLastFieldIdx].type) {
		case a35fPhone:   r->fields[gLastFieldIdx].label = vcPhoneLabel(lbl); break;
		case a35fEmail:   r->fields[gLastFieldIdx].label = vcEmailLabel(lbl); break;
		case a35fAddress: r->fields[gLastFieldIdx].label = vcAddrLabel(lbl); break;
		case a35fRelation:
			/* relType is the packed value's 5th byte (see A35PackRelation) -
			 * field.label itself is unused for a35fRelation, always 0. */
			if (r->fields[gLastFieldIdx].len >= a35RelationPackLen)
				((UInt8 *) r->fields[gLastFieldIdx].value)[4] = vcRelFromToken(lbl);
			break;
		default: break;
		}
		return;
	}

	if (vcIsProp(np, "BEGIN") || vcIsProp(np, "END") ||
		vcIsProp(np, "VERSION") || vcIsProp(np, "PRODID") ||
		vcIsProp(np, "REV") || vcIsProp(np, "UID") || vcIsProp(np, "FN"))
		return;					/* control / derived-from-N: skip (FN via N) */

	if (vcIsProp(np, "N")) {
		char c[64];
		vcComponent(value, 0, c, sizeof(c)); if (c[0]) vcAddStr(r, arena, a35fLast, 0, c);
		vcComponent(value, 1, c, sizeof(c)); if (c[0]) vcAddStr(r, arena, a35fFirst, 0, c);
		vcComponent(value, 2, c, sizeof(c)); if (c[0]) vcAddStr(r, arena, a35fMiddle, 0, c);
		vcComponent(value, 3, c, sizeof(c)); if (c[0]) vcAddStr(r, arena, a35fPrefix, 0, c);
		vcComponent(value, 4, c, sizeof(c)); if (c[0]) vcAddStr(r, arena, a35fSuffix, 0, c);
		return;
	}
	if (vcIsProp(np, "ORG")) {
		char c[96];
		vcComponent(value, 0, c, sizeof(c));		/* first ORG component */
		if (c[0]) vcAddStr(r, arena, a35fCompany, 0, c);
		return;
	}
	if (vcIsProp(np, "TITLE") || vcIsProp(np, "ROLE")) {
		char c[96]; vcUnescape(value, (UInt16) StrLen(value), c, sizeof(c));
		if (c[0]) vcAddStr(r, arena, a35fTitle, 0, c);
		return;
	}
	if (vcIsProp(np, "NICKNAME")) {
		char c[64]; vcComponent(value, 0, c, sizeof(c));
		if (c[0]) vcAddStr(r, arena, a35fNickname, 0, c);
		return;
	}
	if (vcIsProp(np, "TEL")) {
		char c[64]; UInt8 lab;
		vcLowerAll(np, ty, sizeof(ty));
		vcUnescape(value, (UInt16) StrLen(value), c, sizeof(c));
		if (!c[0]) return;
		lab = vcPhoneLabel(ty);
		vcAddStr(r, arena, a35fPhone, lab, c);
		vcTrackGroup(r, grp);
		if (lab == a35plOther) {
			/* unrecognized TYPE= token(s): keep the raw label as a passthrough
			 * custom field rather than silently losing it - the number itself
			 * must stay dialable, so the raw text can't be embedded in the
			 * phone value itself. */
			char rawty[48];
			vcTypeParamRaw(np, rawty, sizeof(rawty));
			if (rawty[0]) vcAddCustom(r, arena, "Phone Type", rawty);
		}
		return;
	}
	if (vcIsProp(np, "EMAIL")) {
		char c[128];
		vcLowerAll(np, ty, sizeof(ty));
		vcUnescape(value, (UInt16) StrLen(value), c, sizeof(c));
		if (c[0]) { vcAddStr(r, arena, a35fEmail, vcEmailLabel(ty), c); vcTrackGroup(r, grp); }
		return;
	}
	if (vcIsProp(np, "URL")) {
		char c[160]; vcUnescape(value, (UInt16) StrLen(value), c, sizeof(c));
		if (c[0]) vcAddStr(r, arena, a35fURL, 0, c);
		return;
	}
	if (vcIsProp(np, "NOTE")) {
		vcAddUnescaped(r, arena, a35fNote, 0, value, (UInt16) StrLen(value));
		return;
	}
	if (vcIsProp(np, "GENDER")) {
		char c[32]; vcComponent(value, 0, c, sizeof(c));
		if (c[0]) vcAddStr(r, arena, a35fGender, 0, c);
		return;
	}
	if (vcIsProp(np, "X-PRONOUNS")) {
		char c[32]; vcUnescape(value, (UInt16) StrLen(value), c, sizeof(c));
		if (c[0]) vcAddStr(r, arena, a35fPronouns, 0, c);
		return;
	}
	if (vcIsProp(np, "BDAY")) {
		char ymd[9]; if (vcParseDate(value, ymd)) vcAddStr(r, arena, a35fBirthday, 0, ymd);
		return;
	}
	if (vcIsProp(np, "ANNIVERSARY") || vcIsProp(np, "X-ANNIVERSARY")) {
		char ymd[9];
		if (vcParseDate(value, ymd)) {
			UInt8 lab;
			char store[80], rawty[48];
			vcTypeParam(np, ty, sizeof(ty));			/* lowercased */
			if (StrStr(ty, "engage")) lab = 0;
			else if (StrStr(ty, "rememb")) lab = 1;
			else if (StrStr(ty, "wedding") || StrStr(ty, "marriage")) lab = 2;
			else if (StrStr(ty, "work")) lab = 3;
			else if (ty[0]) lab = 4;					/* unknown -> Other + name */
			else lab = 2;								/* untyped -> Wedding */
			StrCopy(store, ymd);
			if (lab == 4) {								/* append custom name */
				vcTypeParamRaw(np, rawty, sizeof(rawty));
				StrNCat(store, rawty, sizeof(store));
			}
			vcAddStr(r, arena, a35fAnniversary, lab, store);
		}
		return;
	}
	if (vcIsProp(np, "ADR")) {
		char pobox[8], ext[48], street[96], city[48], region[48], zip[24], ctry[48];
		/* Worst case: line1(95)+line2(95)+ext(47)+city(47)+region(47)+zip(23)+
		 * ctry(47)+6 tabs+NUL = 408 - the OLD 7*48=336 buffer was a guaranteed-
		 * reachable stack overflow (didn't even need all 7 parts near their max
		 * at once) whenever an ADR property's components summed past 336 bytes,
		 * which any real multi-line street + city/region/country easily does.
		 * This runs in the PARSER (main segment, every vCard read - both the
		 * pre-split count pass and the split pass itself), so it's reachable
		 * on ANY vCard with a long-enough address, independent of duplicate
		 * filenames or anything downstream - matches a real-device "sometimes
		 * crashes right at OK to split" report exactly (data-dependent on
		 * which contact's address happens to be long). */
		char tab[420];
		vcLowerAll(np, ty, sizeof(ty));
		vcComponent(value, 0, pobox, sizeof(pobox));
		vcComponent(value, 1, ext, sizeof(ext));
		vcComponent(value, 2, street, sizeof(street));
		vcComponent(value, 3, city, sizeof(city));
		vcComponent(value, 4, region, sizeof(region));
		vcComponent(value, 5, zip, sizeof(zip));
		vcComponent(value, 6, ctry, sizeof(ctry));
		/* street may carry a '\n' (line2); split it */
		{
			char *nlp = StrChr(street, '\n');
			char line1[96], line2[96];
			line2[0] = '\0';
			if (nlp) { *nlp = '\0'; StrNCopy(line2, nlp + 1, sizeof(line2) - 1); line2[sizeof(line2)-1]='\0'; }
			StrNCopy(line1, street, sizeof(line1) - 1); line1[sizeof(line1)-1] = '\0';
			/* Line1\tLine2\tApt\tCity\tState\tZip\tCountry - bounded (StrNCat,
			 * total-dst-size) as defense in depth on top of the enlarged tab[]. */
			tab[0] = '\0';
			StrNCat(tab, line1, sizeof(tab)); StrNCat(tab, "\t", sizeof(tab));
			StrNCat(tab, line2, sizeof(tab)); StrNCat(tab, "\t", sizeof(tab));
			StrNCat(tab, ext,   sizeof(tab)); StrNCat(tab, "\t", sizeof(tab));
			StrNCat(tab, city,  sizeof(tab)); StrNCat(tab, "\t", sizeof(tab));
			StrNCat(tab, region,sizeof(tab)); StrNCat(tab, "\t", sizeof(tab));
			StrNCat(tab, zip,   sizeof(tab)); StrNCat(tab, "\t", sizeof(tab));
			StrNCat(tab, ctry,  sizeof(tab));
		}
		if (street[0] || city[0] || zip[0] || ctry[0]) {
			vcAddStr(r, arena, a35fAddress, vcAddrLabel(ty), tab);
			vcTrackGroup(r, grp);
		}
		return;
	}
	if (vcIsProp(np, "X-SOCIALPROFILE") || vcIsProp(np, "IMPP") ||
		vcIsProp(np, "X-WHATSAPP") || vcIsProp(np, "X-TELEGRAM") ||
		vcIsProp(np, "X-SIGNAL")) {
		char handle[128], custom[48], store[180];
		UInt8 lab;
		vcTypeParam(np, ty, sizeof(ty));			/* lowercased */
		vcTypeParamRaw(np, raw, sizeof(raw));		/* original case */
		/* prefer explicit property name for the dedicated X-* forms */
		if (vcIsProp(np, "X-WHATSAPP")) { StrCopy(ty, "whatsapp"); StrCopy(raw, "WhatsApp"); }
		else if (vcIsProp(np, "X-TELEGRAM")) { StrCopy(ty, "telegram"); StrCopy(raw, "Telegram"); }
		else if (vcIsProp(np, "X-SIGNAL")) { StrCopy(ty, "signal"); StrCopy(raw, "Signal"); }
		vcUnescape(value, (UInt16) StrLen(value), handle, sizeof(handle));
		lab = vcSocialLabel(ty, raw, custom, sizeof(custom));
		if (lab == a35socOther) {
			store[0] = '\0';
			StrCat(store, custom); StrCat(store, ": "); StrCat(store, handle);
			if (handle[0]) vcAddStr(r, arena, a35fSocial, a35socOther, store);
		} else if (handle[0]) {
			vcAddStr(r, arena, a35fSocial, lab, handle);
		}
		return;
	}
	/* X-ABRELATEDNAMES: Apple's actual (non-RFC) relationship export - carries no
	 * TYPE= of its own; the relation type comes from a paired item1.X-ABLabel
	 * line (e.g. "_$!<Mother>!$_"), patched in via vcTrackGroup below. */
	if (vcIsProp(np, "RELATED") || vcIsProp(np, "X-ABRELATEDNAMES")) {
		char nmv[64]; UInt16 tgt;
		vcTypeParam(np, ty, sizeof(ty));
		vcUnescape(value, (UInt16) StrLen(value), nmv, sizeof(nmv));
		if (!nmv[0]) return;
		if (vcFindByName(dbP, nmv, &tgt)) {
			A35Relation rel; UInt8 pk[a35RelationPackLen]; UInt32 uid;
			if (DmRecordInfo(dbP, tgt, NULL, &uid, NULL) == errNone) {
				rel.targetUniqueID = uid; rel.relType = vcRelFromToken(ty);
				A35PackRelation(&rel, pk);
				{ const UInt8 *v = vcArena(arena, pk, a35RelationPackLen);
				  if (v) { A35AddField(r, a35fRelation, 0, v, a35RelationPackLen); vcTrackGroup(r, grp); } }
			}
		} else {
			/* no match: passthrough "RELATED\0<name>" so data isn't lost */
			vcAddCustom(r, arena, "RELATED", nmv);
		}
		return;
	}
	if (vcIsProp(np, "CATEGORIES")) {
		const char *p = value;
		char cat[a35TagNameLen];
		while (*p) {
			UInt16 o = 0;
			while (*p && *p != ',' && o < sizeof(cat) - 1) {
				if (*p == '\\' && p[1]) p++;
				cat[o++] = *p++;
			}
			cat[o] = '\0';
			if (*p == ',') p++;
			if (cat[0]) {
				UInt16 tid = A35TagAdd(dbP, cat);
				if (tid) {
					UInt8 be[2];
					const UInt8 *v;
					be[0] = (UInt8) (tid >> 8); be[1] = (UInt8) tid;
					v = vcArena(arena, be, 2);
					if (v) A35AddField(r, a35fTag, 0, v, 2);
				}
			}
		}
		return;
	}

	/* unknown property -> passthrough as a35fCustom "NAME\0value" */
	{
		char name[64], text[256];
		char *semi = StrChr(np, ';');
		if (semi) *semi = '\0';
		StrNCopy(name, np, sizeof(name) - 1); name[sizeof(name)-1] = '\0';
		vcUnescape(value, (UInt16) StrLen(value), text, sizeof(text));
		vcAddCustom(r, arena, name, text);
	}
}

/* true if the name-part (gLine content up to and including the ':') is a
 * binary/opaque property (PHOTO/LOGO/SOUND/KEY) Address35 has no use for.
 * Strips a group prefix ("item1.PHOTO" -> "PHOTO") like vcParseLine does. */
static Boolean vcIsBinaryProp(const char *np)
{
	const char *p = np;
	const char *dot = StrChr(np, '.');
	const char *semi = StrChr(np, ';');
	const char *colon = StrChr(np, ':');
	if (dot && (!semi || dot < semi) && (!colon || dot < colon))
		p = dot + 1;
	return (Boolean) (vcIsProp(p, "PHOTO") || vcIsProp(p, "LOGO") ||
		vcIsProp(p, "SOUND") || vcIsProp(p, "KEY"));
}

Boolean A35VCardToRecord(DmOpenRef dbP, const char *text, UInt32 len,
	A35Record *r, VCardArena *arena)
{
	UInt32 i = 0;
	UInt16 ll = 0;
	Boolean began = false, ended = false;
	/* true while inside a PHOTO/LOGO/SOUND/KEY value: discard bytes (incl. all
	 * its folded continuation lines) instead of buffering/parsing them - these
	 * can be tens of KB of base64 that Address35 has no field for, and letting
	 * them through wastes gLine/arena space and (on real DragonBall-class
	 * hardware) real time. */
	Boolean skipVal = false;

	A35RecordInit(r);
	arena->used = 0;
	gLastGroup[0] = '\0';
	gLastFieldIdx = 0xffff;			/* item1.X-ABLabel pairing is per-card */

	while (i < len && !ended) {
		char ch = text[i++];
		if (ch == '\r') continue;
		if (ch != '\n') {						/* accumulate physical line */
			if (!skipVal) {
				if (ll < sizeof(gLine) - 1) gLine[ll++] = ch;
				if (ch == ':' && began) {
					gLine[ll] = '\0';
					if (vcIsBinaryProp(gLine))
						skipVal = true;
				}
			}
			continue;
		}
		/* got a physical line end; peek for a folding continuation */
		if (i < len && (text[i] == ' ' || text[i] == '\t')) {
			i++;								/* unfold: drop CRLF + 1 ws */
			continue;
		}
		if (skipVal) {							/* logical line (the binary value) ends */
			skipVal = false;
			ll = 0;
			continue;
		}
		gLine[ll] = '\0';
		if (!began) {
			if (vcIsProp(gLine, "BEGIN")) began = true;
		} else if (vcIsProp(gLine, "END")) {
			ended = true;
		} else {
			vcParseLine(dbP, r, arena, gLine);
		}
		ll = 0;
	}
	if (began && !ended && ll && !skipVal) {	/* last line w/o trailing NL */
		gLine[ll] = '\0';
		if (!vcIsProp(gLine, "END")) vcParseLine(dbP, r, arena, gLine);
	}

	return (Boolean) (began &&
		(A35FindField(r, a35fFirst, 0) || A35FindField(r, a35fLast, 0) ||
		 A35FindField(r, a35fCompany, 0)));
}

UInt32 A35VCardSpan(const char *text, UInt32 len)
{
	UInt32 i = 0, lineStart = 0;
	Boolean began = false;

	while (i <= len) {
		if (i == len || text[i] == '\n') {
			/* examine the line [lineStart..i) */
			const char *ls = text + lineStart;
			if (!began) { if (vcIsProp(ls, "BEGIN")) began = true; }
			else if (vcIsProp(ls, "END")) return (i < len) ? i + 1 : len;
			lineStart = i + 1;
		}
		if (i == len) break;
		i++;
	}
	return began ? len : 0;
}

/* -------------------------------------------------------- STREAMING (SD card) */

Err A35VCardOpenStream(VCardStream *s, UInt16 volRef, const char *path)
{
	FileRef f;
	Err err = VFSFileOpen(volRef, path, vfsModeRead, &f);
	if (err != errNone)
		return err;
	MemSet(s, sizeof(*s), 0);
	s->fileRef = (UInt32) f;
	VFSFileSize(f, &s->fileSize);		/* 0 on failure is fine: progress just stays hidden */
	return errNone;
}

void A35VCardCloseStream(VCardStream *s)
{
	VFSFileClose((FileRef) s->fileRef);
}

void A35VCardSetProgress(VCardStream *s, VCardProgressFn fn, void *ctx)
{
	s->progressFn = fn;
	s->progressCtx = ctx;
}

/* refill s->buf from the VFS file; returns false once nothing more is available */
static Boolean vcStreamFill(VCardStream *s)
{
	UInt32 nread = 0;
	Err err;

	if (s->eof)
		return false;
	err = VFSFileRead((FileRef) s->fileRef, sizeof(s->buf), s->buf, &nread);
	s->len = (UInt16) nread;
	s->pos = 0;
	s->readTotal += nread;
	if (s->progressFn && s->fileSize) {	/* division stays here (main); see VCardProgressFn */
		Int16 w = (Int16) ((UInt32) VC_PROGRESS_WIDTH * s->readTotal / s->fileSize);
		if (w > VC_PROGRESS_WIDTH) w = VC_PROGRESS_WIDTH;
		s->progressFn(s->progressCtx, w);
	}
	if (nread == 0 || (err != errNone && err != vfsErrFileEOF))
		s->eof = true;
	return (Boolean) (s->len > 0);
}

static Boolean vcStreamGetc(VCardStream *s, char *outCh)
{
	if (s->hasPeek) {
		*outCh = s->peekCh;
		s->hasPeek = false;
		return true;
	}
	if (s->pos >= s->len && !vcStreamFill(s))
		return false;
	*outCh = s->buf[s->pos++];
	return true;
}

/* push one byte back so the next vcStreamGetc returns it again (1-deep, used to
 * peek at the character after a line-ending to detect a folding continuation) */
static void vcStreamUngetc(VCardStream *s, char ch)
{
	s->peekCh = ch;
	s->hasPeek = true;
}

Boolean A35VCardStreamNext(DmOpenRef dbP, VCardStream *s, A35Record *r,
	VCardArena *arena)
{
	UInt16 ll = 0;
	Boolean began = false, ended = false, skipVal = false;
	char ch, peek;

	A35RecordInit(r);
	arena->used = 0;
	gLastGroup[0] = '\0';
	gLastFieldIdx = 0xffff;			/* item1.X-ABLabel pairing is per-card */

	/* skip forward (discarding whatever's there) until BEGIN:VCARD, or EOF */
	while (!began) {
		if (!vcStreamGetc(s, &ch))
			return false;				/* stream exhausted: no more cards */
		if (ch == '\r') continue;
		if (ch != '\n') {
			if (ll < sizeof(gLine) - 1) gLine[ll++] = ch;
			continue;
		}
		if (vcStreamGetc(s, &peek)) {
			if (peek == ' ' || peek == '\t') continue;	/* fold (not expected pre-BEGIN) */
			vcStreamUngetc(s, peek);
		}
		gLine[ll] = '\0'; ll = 0;
		if (vcIsProp(gLine, "BEGIN")) began = true;
	}

	while (!ended) {
		if (!vcStreamGetc(s, &ch))
			break;						/* truncated file: stop, return what we parsed */
		if (ch == '\r') continue;
		if (ch != '\n') {
			if (!skipVal) {
				if (ll < sizeof(gLine) - 1) gLine[ll++] = ch;
				if (ch == ':') {
					gLine[ll] = '\0';
					if (vcIsBinaryProp(gLine))
						skipVal = true;
				}
			}
			continue;
		}
		if (vcStreamGetc(s, &peek)) {
			if (peek == ' ' || peek == '\t') continue;	/* fold continuation */
			vcStreamUngetc(s, peek);
		}
		if (skipVal) { skipVal = false; ll = 0; continue; }
		gLine[ll] = '\0';
		if (vcIsProp(gLine, "END")) ended = true;
		else vcParseLine(dbP, r, arena, gLine);
		ll = 0;
	}

	/* FIXED: this used to gate on having a first/last/company field, meaning a
	 * single valid sub-card with real data but no name (e.g. an org-only
	 * business card, or just a phone number) made this return false - which
	 * callers (A35CardImport/A35CardSplitFile) treat as "stream exhausted,
	 * stop walking", silently dropping every remaining card in the file after
	 * it. `began` is always true here (guaranteed by the loop above), so this
	 * always successfully found and processed a BEGIN..END block (or an EOF-
	 * truncated one - see the "truncated file: stop, return what we parsed"
	 * comment above, same intent). Whether the card is worth KEEPING is the
	 * caller's own call via A35RecordHasData(fieldCount > 0) - a strictly
	 * broader, more correct check already applied at every call site - not
	 * this function's job to gate the WALK on. */
	return true;
}

void A35VCardNameFromRecord(const A35Record *r, char *out, UInt16 outLen)
{
	const char *first = A35GetStr(r, a35fFirst);
	const char *last = A35GetStr(r, a35fLast);
	const char *comp = A35GetStr(r, a35fCompany);

	out[0] = '\0';
	if (first[0] || last[0]) {
		StrNCopy(out, first, outLen - 1); out[outLen - 1] = '\0';
		if (first[0] && last[0]) StrNCat(out, " ", outLen);
		StrNCat(out, last, outLen);
	} else {
		StrNCopy(out, comp[0] ? comp : "(Unnamed)", outLen - 1);
		out[outLen - 1] = '\0';
	}
}
