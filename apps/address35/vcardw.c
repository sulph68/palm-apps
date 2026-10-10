/*
 * vcardw.c - vCard EMITTER (Address35 record -> vCard 4.0 text).
 *
 * Split out of vcard.c so it can live in a secondary code segment (SEC002): the
 * emitter is only reached from the SEND path (beam/export), never from the
 * no-globals RECEIVE sublaunch, which keeps the parser (vcard.c) alone in main.
 * All functions are SEC002; forward-declared so cross-segment calls resolve.
 */
#include "vcard.h"

typedef struct {
	char *	buf;
	UInt16	cap;
	UInt16	pos;
	UInt16	col;		/* column on the current folded physical line */
	Boolean	ovf;
} VCOut;

static void vcPutRaw(VCOut *o, char c) SEC002;
static void vcNL(VCOut *o) SEC002;
static void vcPut(VCOut *o, char c) SEC002;
static void vcPuts(VCOut *o, const char *s) SEC002;
static void vcPutText(VCOut *o, const char *s) SEC002;
static void vcPutComp(VCOut *o, const char *s) SEC002;
static const char *vcPhoneType(UInt8 l) SEC002;
static const char *vcEmailType(UInt8 l) SEC002;
static const char *vcAddrType(UInt8 l) SEC002;
static const char *vcSocialName(UInt8 l) SEC002;
static const char *vcRelToken(UInt8 t) SEC002;
static void vcEmitDate(VCOut *o, const char *ymd) SEC002;
static void vcTabField(const char *src, UInt16 idx, char *out, UInt16 outLen) SEC002;

static void vcPutRaw(VCOut *o, char c)
{
	if (o->pos >= (UInt16) (o->cap - 1)) { o->ovf = true; return; }
	o->buf[o->pos++] = c;
}

static void vcNL(VCOut *o)			/* end a logical line */
{
	vcPutRaw(o, '\r');
	vcPutRaw(o, '\n');
	o->col = 0;
}

/* one content octet, folding at 75 cols (RFC 6350 5.8.1) */
static void vcPut(VCOut *o, char c)
{
	if (o->col >= 75) {
		vcPutRaw(o, '\r'); vcPutRaw(o, '\n'); vcPutRaw(o, ' ');
		o->col = 1;
	}
	vcPutRaw(o, c);
	o->col++;
}

static void vcPuts(VCOut *o, const char *s)
{
	while (*s) vcPut(o, *s++);
}

/* TEXT value: escape \ , ; and newline */
static void vcPutText(VCOut *o, const char *s)
{
	for (; *s; s++) {
		char c = *s;
		if (c == '\\' || c == ',' || c == ';') { vcPut(o, '\\'); vcPut(o, c); }
		else if (c == '\n') { vcPut(o, '\\'); vcPut(o, 'n'); }
		else if (c == '\r') { /* drop */ }
		else vcPut(o, c);
	}
}

/* a structured component (N/ADR): escape ; , \ and newline, no field split */
static void vcPutComp(VCOut *o, const char *s)
{
	vcPutText(o, s);			/* same escaping; caller emits the ';' separators */
}

static const char *vcPhoneType(UInt8 l)
{
	switch (l) {
	case a35plHome:		return "home";
	case a35plWork:		return "work";
	case a35plFax:		return "fax";
	case a35plMain:		return "voice";
	case a35plPager:	return "pager";
	case a35plOther:	return "other";
	default:			return "cell";		/* a35plMobile */
	}
}

static const char *vcEmailType(UInt8 l)
{
	switch (l) {
	case a35elWork:		return "work";
	case a35elICloud:	return "icloud";
	case a35elOther:	return "other";
	default:			return "home";		/* a35elHome */
	}
}

static const char *vcAddrType(UInt8 l)
{
	switch (l) {
	case a35alWork:		return "work";
	case a35alOther:	return "other";
	default:			return "home";		/* a35alHome */
	}
}

/* social network display name (a35soc*), used as X-SOCIALPROFILE TYPE */
static const char *vcSocialName(UInt8 l)
{
	switch (l) {
	case a35socInstagram:	return "Instagram";
	case a35socFacebook:	return "Facebook";
	case a35socWhatsApp:	return "WhatsApp";
	case a35socX:			return "X";
	case a35socTelegram:	return "Telegram";
	case a35socSignal:		return "Signal";
	default:				return "Other";
	}
}

/* lowercase RELATED TYPE token for a relation type (a35rel*) */
static const char *vcRelToken(UInt8 t)
{
	switch (t) {
	case a35relSpouse:		return "spouse";
	case a35relPartner:		return "partner";
	case a35relChild:		return "child";
	case a35relParent:		return "parent";
	case a35relSibling:		return "sibling";
	case a35relAssistant:	return "assistant";
	case a35relManager:		return "manager";
	case a35relFriend:		return "friend";
	default:				return "contact";
	}
}

static const char *const vcAnnivType[5] = {
	"Engagement", "Remembrance", "Wedding", "Work", "Other"
};

/* emit "YYYYMMDD" as vCard date: "YYYY-MM-DD" or "--MM-DD" (year 0000) */
static void vcEmitDate(VCOut *o, const char *ymd)
{
	if (ymd[0] == '0' && ymd[1] == '0' && ymd[2] == '0' && ymd[3] == '0') {
		vcPut(o, '-'); vcPut(o, '-');
		vcPut(o, ymd[4]); vcPut(o, ymd[5]);
		vcPut(o, '-'); vcPut(o, ymd[6]); vcPut(o, ymd[7]);
	} else {
		vcPut(o, ymd[0]); vcPut(o, ymd[1]); vcPut(o, ymd[2]); vcPut(o, ymd[3]);
		vcPut(o, '-'); vcPut(o, ymd[4]); vcPut(o, ymd[5]);
		vcPut(o, '-'); vcPut(o, ymd[6]); vcPut(o, ymd[7]);
	}
}

/* copy TAB-delimited field #idx of src into out (trimmed) */
static void vcTabField(const char *src, UInt16 idx, char *out, UInt16 outLen)
{
	const char *p = src ? src : "";
	UInt16 field = 0, o = 0;
	Int16 e;

	out[0] = '\0';
	while (*p && field < idx) { if (*p == '\t') field++; p++; }
	if (field < idx) return;
	while (*p && *p != '\t' && o < (UInt16) (outLen - 1)) out[o++] = *p++;
	out[o] = '\0';
	for (e = (Int16) StrLen(out); e > 0 && out[e - 1] == ' '; ) out[--e] = '\0';
}

/* Copy a singleton field's value as a NUL-terminated C string. A35GetStr returns
 * a pointer straight into the packed record, which is NOT NUL-terminated (fields
 * abut with no terminator), so it must be length-copied here or the emitter reads
 * past the value into the next field (trailing garbage glyph on beamed names). */
static void vcGetField(const A35Record *r, UInt8 type, char *out, UInt16 outLen) SEC002;
static void vcGetField(const A35Record *r, UInt8 type, char *out, UInt16 outLen)
{
	const A35Field *f = A35FindField(r, type, 0);
	UInt16 n = (f != NULL) ? f->len : 0;
	if (n > (UInt16) (outLen - 1)) n = (UInt16) (outLen - 1);
	if (n) MemMove(out, f->value, n);
	out[n] = '\0';
}

UInt16 A35RecordToVCard(DmOpenRef dbP, const A35Record *r, char *buf, UInt16 bufLen)
{
	VCOut o;
	UInt16 i;
	char prefix[40], first[48], middle[40], last[48], company[88], title[88], nick[48];
	char suffix[40];
	char nm[160];
	Boolean anyCat = false;

	o.buf = buf; o.cap = bufLen; o.pos = 0; o.col = 0; o.ovf = false;

	vcGetField(r, a35fPrefix, prefix, sizeof(prefix));
	vcGetField(r, a35fFirst, first, sizeof(first));
	vcGetField(r, a35fMiddle, middle, sizeof(middle));
	vcGetField(r, a35fLast, last, sizeof(last));
	vcGetField(r, a35fCompany, company, sizeof(company));
	vcGetField(r, a35fTitle, title, sizeof(title));
	vcGetField(r, a35fNickname, nick, sizeof(nick));
	vcGetField(r, a35fSuffix, suffix, sizeof(suffix));

	vcPuts(&o, "BEGIN:VCARD"); vcNL(&o);
	vcPuts(&o, "VERSION:4.0"); vcNL(&o);

	/* N: family;given;additional;prefixes;suffixes */
	vcPuts(&o, "N:");
	vcPutComp(&o, last); vcPut(&o, ';');
	vcPutComp(&o, first); vcPut(&o, ';');
	vcPutComp(&o, middle); vcPut(&o, ';');
	vcPutComp(&o, prefix); vcPut(&o, ';');
	vcPutComp(&o, suffix);
	vcNL(&o);

	/* FN: assembled display name (never empty). Bounded (StrNCat, total-dst-size
	 * per its strlcat-style semantics - see [[strncat-strlcat-semantics-gotcha]]),
	 * NOT plain StrCat: prefix/first/middle/last are each copied into their own
	 * buffer above (40/48/40/48 bytes), so all 4 at max length already sum to
	 * 176 - more than this 160-byte nm[] can hold even before accounting for
	 * the separating spaces. A real, guaranteed-reachable stack overflow for a
	 * source vCard with long enough name components (matches an "occasional"
	 * real-device split crash - only names near their max length triggered it,
	 * same bug family as [[address35-sdcard-ux-fixes]]'s RecordForm.c name[]
	 * fix). */
	nm[0] = '\0';
	if (prefix[0]) StrNCat(nm, prefix, sizeof(nm));
	if (first[0])  { if (nm[0]) StrNCat(nm, " ", sizeof(nm)); StrNCat(nm, first, sizeof(nm)); }
	if (middle[0]) { if (nm[0]) StrNCat(nm, " ", sizeof(nm)); StrNCat(nm, middle, sizeof(nm)); }
	if (last[0])   { if (nm[0]) StrNCat(nm, " ", sizeof(nm)); StrNCat(nm, last, sizeof(nm)); }
	if (!nm[0])    StrNCopy(nm, company[0] ? company : "Unnamed", sizeof(nm) - 1);
	vcPuts(&o, "FN:"); vcPutText(&o, nm); vcNL(&o);

	if (company[0]) { vcPuts(&o, "ORG:"); vcPutText(&o, company); vcNL(&o); }
	if (title[0])   { vcPuts(&o, "TITLE:"); vcPutText(&o, title); vcNL(&o); }
	if (nick[0])    { vcPuts(&o, "NICKNAME:"); vcPutText(&o, nick); vcNL(&o); }

	for (i = 0; i < r->fieldCount; i++) {
		UInt8 t = r->fields[i].type;
		UInt8 label = (UInt8) (r->fields[i].label & a35LabelMask);
		UInt16 n = r->fields[i].len;
		char val[264];

		if (t == a35fTag) { anyCat = true; continue; }		/* collected below */
		if (n == 0 && t != a35fRelation) continue;
		if (n > sizeof(val) - 1) n = sizeof(val) - 1;
		if (t != a35fRelation) { MemMove(val, r->fields[i].value, n); val[n] = '\0'; }

		switch (t) {
		case a35fPhone:
			vcPuts(&o, "TEL;TYPE="); vcPuts(&o, vcPhoneType(label));
			vcPut(&o, ':'); vcPutText(&o, val); vcNL(&o);
			break;
		case a35fEmail:
			vcPuts(&o, "EMAIL;TYPE="); vcPuts(&o, vcEmailType(label));
			vcPut(&o, ':'); vcPutText(&o, val); vcNL(&o);
			break;
		case a35fURL:
			vcPuts(&o, "URL:"); vcPutText(&o, val); vcNL(&o);
			break;
		case a35fNote:
			vcPuts(&o, "NOTE:"); vcPutText(&o, val); vcNL(&o);
			break;
		case a35fGender:
			vcPuts(&o, "GENDER:"); vcPutText(&o, val); vcNL(&o);
			break;
		case a35fPronouns:
			vcPuts(&o, "X-PRONOUNS:"); vcPutText(&o, val); vcNL(&o);
			break;
		case a35fBirthday:
			if (StrLen(val) >= a35BirthdayStrLen) {
				vcPuts(&o, "BDAY:"); vcEmitDate(&o, val); vcNL(&o);
			}
			break;
		case a35fAnniversary:
			if (StrLen(val) >= a35BirthdayStrLen) {
				const char *tn = (label == 4 && StrLen(val) > a35BirthdayStrLen)
					? val + a35BirthdayStrLen
					: vcAnnivType[(label < 5) ? label : 2];
				vcPuts(&o, "ANNIVERSARY;TYPE="); vcPutText(&o, tn);
				vcPut(&o, ':'); vcEmitDate(&o, val); vcNL(&o);
			}
			break;
		case a35fSocial: {
			const char *net = vcSocialName(label);
			const char *handle = val;
			char netbuf[48];
			if (label == a35socOther) {				/* "Platform: handle" */
				char *sep = StrStr(val, ": ");
				if (sep) {
					UInt16 pl = (UInt16) (sep - val);
					if (pl > sizeof(netbuf) - 1) pl = sizeof(netbuf) - 1;
					MemMove(netbuf, val, pl); netbuf[pl] = '\0';
					net = netbuf; handle = sep + 2;
				}
			}
			vcPuts(&o, "X-SOCIALPROFILE;TYPE="); vcPutText(&o, net);
			vcPut(&o, ':'); vcPutText(&o, handle); vcNL(&o);
			break;
		}
		case a35fAddress: {
			char pt[7][64];
			UInt16 k;
			for (k = 0; k < 7; k++) vcTabField(val, k, pt[k], sizeof(pt[k]));
			/* ADR: pobox;ext(apt);street;locality;region;postal;country */
			vcPuts(&o, "ADR;TYPE="); vcPuts(&o, vcAddrType(label));
			vcPuts(&o, ":;");
			vcPutComp(&o, pt[2]); vcPut(&o, ';');			/* apt -> ext */
			vcPutComp(&o, pt[0]);							/* line1 -> street */
			if (pt[1][0]) { vcPut(&o, '\\'); vcPut(&o, 'n'); vcPutComp(&o, pt[1]); }
			vcPut(&o, ';');
			vcPutComp(&o, pt[3]); vcPut(&o, ';');			/* city */
			vcPutComp(&o, pt[4]); vcPut(&o, ';');			/* state */
			vcPutComp(&o, pt[5]); vcPut(&o, ';');			/* zip */
			vcPutComp(&o, pt[6]);							/* country */
			vcNL(&o);
			break;
		}
		case a35fRelation: {
			A35Relation rel;
			UInt16 tgt;
			char tn[64];
			if (A35UnpackRelation(r->fields[i].value, r->fields[i].len, &rel) &&
				A35FindByUniqueID(dbP, rel.targetUniqueID, &tgt)) {
				A35RecordDisplayName(dbP, tgt, tn, sizeof(tn));
				vcPuts(&o, "RELATED;TYPE="); vcPuts(&o, vcRelToken(rel.relType));
				vcPuts(&o, ";VALUE=text:"); vcPutText(&o, tn); vcNL(&o);
			}
			break;
		}
		case a35fCustom: {
			/* stored "label\0text" -> passthrough "LABEL:text" */
			const char *lbl = (const char *) r->fields[i].value;
			UInt16 ll = (UInt16) StrLen(lbl);
			if (ll + 1 < r->fields[i].len) {
				vcPuts(&o, lbl); vcPut(&o, ':');
				vcPutText(&o, (const char *) r->fields[i].value + ll + 1);
				vcNL(&o);
			}
			break;
		}
		default: break;
		}
	}

	/* CATEGORIES from tag fields */
	if (anyCat) {
		Boolean firstCat = true;
		vcPuts(&o, "CATEGORIES:");
		for (i = 0; i < r->fieldCount; i++) {
			if (r->fields[i].type == a35fTag && r->fields[i].len >= 2) {
				UInt16 tid = (UInt16) (((UInt16) r->fields[i].value[0] << 8) |
					r->fields[i].value[1]);
				char tn[a35TagNameLen];
				if (A35TagName(dbP, tid, tn) && tn[0]) {
					if (!firstCat) vcPut(&o, ',');
					vcPutText(&o, tn);
					firstCat = false;
				}
			}
		}
		vcNL(&o);
	}

	vcPuts(&o, "END:VCARD"); vcNL(&o);

	if (o.ovf) return 0;
	o.buf[o.pos] = '\0';
	return o.pos;
}
