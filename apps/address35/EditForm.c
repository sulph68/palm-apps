/*
 * EditForm.c - Address35 contact editor v3: inline-editable, section-ordered.
 *
 * The edit form is a scrolling window of physical rows, each a [type selector |
 * editable value field], onto a section-sorted field array (gFields). The row's
 * selector shows the field type; tapping it pops a shared type list, and picking
 * a type re-sorts the field into its section. Standard fields (First/Last/Company/
 * Title/Email/Mobile/Address) are always present (even empty). "+ Field" adds a
 * field (inserted into its section).
 *
 * The editor "kind" values are laid out in section order, so sorting by kind
 * gives section order, and the kind is also the shared type list's item index.
 * (v3 M1: text values for every type; Gender/Pronouns/Social/date pickers next.)
 */
#include <PalmOS.h>
#include "Main.h"

enum {
	ftPrefix = 0, ftFirst, ftMiddle, ftLast, ftSuffix, ftNickname, ftPronouns,
	ftGender,													/* Name */
	ftCompany, ftTitle,										/* Organization */
	ftEmail, ftMobile, ftWork, ftHome, ftSocial, ftURL,		/* Phone & Email */
	ftAddress, ftCity, ftState, ftZip, ftCountry,			/* Address */
	ftBirthday, ftAnniversary, ftRelation,					/* Personal */
	ftNote, ftOther,
	ftKindCount
};

#define a35EditMaxFields	48		/* match a35MaxFields (DB cap) so a fully-loaded
										 * contact never drops trailing fields on edit */
#define a35EditValLen		256		/* big enough for a full international postal
										 * address (7 TAB-delimited parts); was 120,
										 * which truncated long addresses' zip+country */

/* `big` holds the full text of an oversized value (a Note, which can exceed the
 * 256-byte inline buffer); NULL for normal fields.  `value` then holds a short
 * preview for display.  The handle travels with the struct through the sort. */
typedef struct { UInt8 kind; UInt8 sub; UInt8 mark; Int16 origin; MemHandle big; char value[a35EditValLen]; } EditField;
/* gFields is ~12KB (48 * 256) - too big for the app's global (.bss) region, so it
 * lives in the dynamic heap: allocated on frmOpen (EditInit), freed on frmClose. */
static EditField *gFields;
static UInt16 gFieldCount;
static UInt16 gFieldTop;
/* packed A35Relation bytes must outlive the A35AddField reference until the
 * record is packed in EditSave, so keep a per-field persistent buffer. */
static UInt8 gRelPack[a35EditMaxFields][a35RelationPackLen];

#define kAddrSpan	3			/* the Address inline row is 3 slots tall */
#define kNoteMax	2000		/* max note length (matches fldNoteText MAXCHARS) */
#define kNoteEditLines	2		/* note preview rows in the edit form */
#define kNoteValW	87			/* pixel width of a row's value box */
#define kNoField	0xffff
static UInt16 gSlotField[kEditRows];	/* field index shown at each physical slot */

static UInt16  gEditCat;
static Boolean gEditDeleted;
static Boolean gEditSaved;
static char    gEditCatName[dmCategoryLength];
/* CtlSetLabel keeps the pointer, not a copy - each row needs a persistent buffer */
static char    gRowLabel[kEditRows][24];

#define EditIsPhoneKind(k)	((k) == ftMobile || (k) == ftWork || (k) == ftHome)

/* SHORT labels (<=8 chars) shown on the row trigger to keep the value field wide */
static const char * const kShortLabel[ftKindCount] = {
	"Salut.", "First", "Middle", "Last", "Suffix", "Nickname", "Pronouns", "Gender",
	"Company", "Title",
	"Email", "Mobile", "Work", "Home", "Social", "URL",
	"Address", "City", "State", "Zip", "Country",
	"Birthday", "Anniv.", "Relation",
	"Note", "Other"
};

/* the type popup: user-facing choices (full names), a SUBSET of kinds in menu
 * order (City/State/Zip/Country are captured by the Address dialog, not here).
 * kPickKind[] maps a list selection index -> kind. */
static const UInt8 kPickKind[] = {
	ftPrefix, ftFirst, ftMiddle, ftLast, ftSuffix, ftNickname, ftPronouns, ftGender,
	ftCompany, ftTitle,
	ftEmail, ftMobile, ftWork, ftHome, ftSocial, ftURL,
	ftAddress, ftBirthday, ftAnniversary, ftRelation, ftNote, ftOther
};
#define kPickCount	(sizeof(kPickKind) / sizeof(kPickKind[0]))

/* display names, 1:1 with kPickKind - the field-type picker (A35PickList, so it
 * gets the Find-row/Graffiti search like the Country/dial-code pickers). */
static const char * const kFieldTypeNames[] = {
	"Salutation", "First Name", "Middle Name", "Last Name", "Suffix", "Nickname",
	"Pronouns", "Gender", "Company", "Title",
	"Email", "Mobile", "Work", "Home", "Social", "URL",
	"Address", "Birthday", "Anniversary", "Relationship", "Note", "Other"
};

static const UInt8 kStandard[] = {
	ftFirst, ftLast, ftCompany, ftTitle, ftEmail, ftMobile, ftAddress
};
#define kStandardCount	7

/* value pick-lists (text is stored verbatim as the field value) */
static const char * const kGenderOpt[] = {
	"Female", "Male", "Non-binary", "Other", "Unspecified"
};
#define kGenderCount	5
static const char * const kPronounOpt[] = {
	"she/her", "he/him", "they/them", "Other"
};
#define kPronounCount	4
static const char * const kPrefixOpt[] = {
	"Mr.", "Mrs.", "Ms.", "Miss", "Dr.", "Prof.", "Mx."
};
#define kPrefixCount	7

/* Ad35 seg: these foreground-only edit helpers live in sec001 to keep main under
 * its 32KB code cap.  SEC001 must be declared here (before the definitions and all
 * call sites) so every cross-segment call knows the target segment (see the
 * multiseg forward-decl gotcha).  None do 32/64-bit division, use function
 * pointers, or hold function-local static const, so the other multiseg traps don't
 * apply; the const tables above are file-scope (A5-relative), reachable from sec001. */
static void AddrMultiline(const char *value, char *out, UInt16 outLen) SEC001;
static void NotePreview(const char *note, Int16 pxWidth, UInt16 maxLines,
	char *out, UInt16 outLen) SEC001;
static void NoteStoreFull(UInt16 mi, const char *text) SEC001;
static void EditPhoneDisplay(UInt16 mi, char *out, UInt16 outLen) SEC001;
static void EditTypePick(UInt16 slot) SEC001;
static void EditKindDisplay(UInt16 mi, char *out, UInt16 outLen) SEC001;
static void EditLoadViewport(void) SEC001;
static void EditSelectCategory(void) SEC001;
static void EditInit(void) SEC001;
static void EditSave(void) SEC001;
static void DelLoadViewport(FormPtr f) SEC001;
static void EditRemoveFieldsDialog(void) SEC001;

/* ---- helpers ----------------------------------------------------------- */
static void *ObjInForm(FormPtr f, UInt16 id)
{
	return FrmGetObjectPtr(f, FrmGetObjectIndex(f, id));
}
static void *EditObj(UInt16 id)
{
	return ObjInForm(FrmGetActiveForm(), id);
}

static void EditKindLabel(UInt8 kind, char *out)
{
	if (kind == ftLast) {
		switch (A35GetSurnameTerm(gAddrDB)) {		/* <=8 chars */
		case a35termLastName:	StrCopy(out, "Last"); break;
		case a35termFamilyName:	StrCopy(out, "Family"); break;
		default:				StrCopy(out, "Surname"); break;
		}
	} else if (kind < ftKindCount) {
		StrCopy(out, kShortLabel[kind]);
	} else {
		out[0] = '\0';
	}
}

static void KindToTLV(const EditField *fl, UInt8 *type, UInt8 *label)
{
	*label = 0;
	switch (fl->kind) {
	case ftPrefix:		*type = a35fPrefix; break;
	case ftFirst:		*type = a35fFirst; break;
	case ftMiddle:		*type = a35fMiddle; break;
	case ftLast:		*type = a35fLast; break;
	case ftSuffix:		*type = a35fSuffix; break;
	case ftNickname:	*type = a35fNickname; break;
	case ftPronouns:	*type = a35fPronouns; break;
	case ftCompany:		*type = a35fCompany; break;
	case ftTitle:		*type = a35fTitle; break;
	case ftEmail:		*type = a35fEmail; *label = fl->sub; break;
	case ftMobile:		*type = a35fPhone; *label = a35plMobile; break;
	case ftWork:		*type = a35fPhone; *label = a35plWork; break;
	case ftHome:		*type = a35fPhone; *label = a35plHome; break;
	case ftSocial:		*type = a35fSocial; *label = fl->sub; break;
	case ftURL:			*type = a35fURL; break;
	case ftAddress:		*type = a35fAddress; break;
	case ftCity:		*type = a35fCity; break;
	case ftState:		*type = a35fState; break;
	case ftZip:			*type = a35fZip; break;
	case ftCountry:		*type = a35fCountry; break;
	case ftBirthday:	*type = a35fBirthday; break;
	case ftAnniversary:	*type = a35fAnniversary; *label = fl->sub; break;
	case ftRelation:	*type = a35fRelation; break;	/* value packed on save */
	case ftGender:		*type = a35fGender; break;
	case ftNote:		*type = a35fNote; break;
	default:			*type = a35fOther; break;
	}
}

static UInt8 TLVToKind(UInt8 type, UInt8 label, UInt8 *sub)
{
	*sub = 0;
	switch (type) {
	case a35fPrefix:	return ftPrefix;
	case a35fFirst:		return ftFirst;
	case a35fMiddle:	return ftMiddle;
	case a35fLast:		return ftLast;
	case a35fSuffix:	return ftSuffix;
	case a35fNickname:	return ftNickname;
	case a35fPronouns:	return ftPronouns;
	case a35fCompany:	return ftCompany;
	case a35fTitle:		return ftTitle;
	case a35fEmail:		*sub = label; return ftEmail;
	case a35fPhone:
		if (label == a35plWork) return ftWork;
		if (label == a35plHome) return ftHome;
		return ftMobile;
	case a35fSocial:	*sub = label; return ftSocial;
	case a35fIM:		return ftSocial;
	case a35fURL:		return ftURL;
	case a35fAddress:	return ftAddress;
	case a35fCity:		return ftCity;
	case a35fState:		return ftState;
	case a35fZip:		return ftZip;
	case a35fCountry:	return ftCountry;
	case a35fBirthday:	return ftBirthday;
	case a35fAnniversary:	*sub = label; return ftAnniversary;
	case a35fRelation:	return ftRelation;	/* value unpacked in EditFieldsFromRecord */
	case a35fGender:	return ftGender;
	case a35fNote:		return ftNote;
	default:			return ftOther;
	}
}

/* ---- field array (section-sorted) -------------------------------------- */
static void EditSortFields(void)
{
	UInt16 i, j;

	for (i = 1; i < gFieldCount; i++) {
		EditField tmp = gFields[i];
		j = i;
		while (j > 0 && gFields[j - 1].kind > tmp.kind) {
			gFields[j] = gFields[j - 1];
			j--;
		}
		gFields[j] = tmp;
	}
}

static void EditAppend(UInt8 kind, UInt8 sub, const char *v)
{
	if (gFieldCount >= a35EditMaxFields)
		return;
	gFields[gFieldCount].kind = kind;
	gFields[gFieldCount].sub = sub;
	gFields[gFieldCount].mark = 0;
	gFields[gFieldCount].big = NULL;	/* no oversized value yet (drops any stale
										 * duplicate pointer left by field removal) */
	gFields[gFieldCount].origin = -1;	/* set by EditFieldsFromRecord for real fields */
	StrNCopy(gFields[gFieldCount].value, v ? v : "", a35EditValLen - 1);
	gFields[gFieldCount].value[a35EditValLen - 1] = '\0';
	gFieldCount++;
}

static Boolean EditHasKind(UInt8 kind)
{
	UInt16 i;

	for (i = 0; i < gFieldCount; i++)
		if (gFields[i].kind == kind)
			return true;
	return false;
}

/* Split a stored phone value's leading "+NN " dial code into gFields[mi].sub (a
 * country index) and strip it from value, so the edit field shows just the local
 * number and the chip shows the code.  No prefix -> sub stays 0 ("(None)"). */
static void PhoneSplit(UInt16 mi)
{
	char *v = gFields[mi].value;
	char code[8];
	UInt16 k = 1, ci = 1;
	Int16 idx;

	if (v[0] != '+')
		return;
	code[0] = '+';
	while (v[k] >= '0' && v[k] <= '9' && ci < (UInt16) (sizeof(code) - 1))
		code[ci++] = v[k++];
	code[ci] = '\0';
	if (v[k] != ' ')				/* not a "+NN " prefix; leave as the user typed */
		return;
	idx = A35DialIndexOf(code);
	if (idx <= 0)					/* unknown code; keep the whole value visible */
		return;
	gFields[mi].sub = (UInt8) idx;
	StrCopy(v, v + k + 1);			/* drop "code " (k = the space) */
}

static void EditFieldsFromRecord(const A35Record *recP)
{
	UInt16 i;

	gFieldTop = 0;
	gFieldCount = 0;
	if (recP != NULL) {
		for (i = 0; i < recP->fieldCount; i++) {
			UInt8 sub;
			UInt16 before = gFieldCount;

			if (recP->fields[i].type == a35fRelation) {
				/* value is a packed A35Relation -> edit as "<uid decimal>" + relType */
				A35Relation rel;
				char ub[12];
				if (A35UnpackRelation(recP->fields[i].value,
						recP->fields[i].len, &rel)) {
					StrIToA(ub, (Int32) rel.targetUniqueID);
					EditAppend(ftRelation, rel.relType, ub);
				}
			} else {
				UInt8 kind = TLVToKind(recP->fields[i].type,
					(UInt8) (recP->fields[i].label & a35LabelMask), &sub);
				EditAppend(kind, sub,
					recP->fields[i].len ? (const char *) recP->fields[i].value : "");
				/* a note can exceed the inline buffer: keep the full text in `big`
				 * and show a preview (EditAppend already stored the truncated copy) */
				if (kind == ftNote && gFieldCount > before && recP->fields[i].len)
					NoteStoreFull(before, (const char *) recP->fields[i].value);
				/* split a phone's "+NN " dial code into the chip */
				if (EditIsPhoneKind(kind) && gFieldCount > before)
					PhoneSplit(before);
			}
			if (gFieldCount > before)		/* tag with its record field index */
				gFields[before].origin = (Int16) i;
		}
	}
	for (i = 0; i < kStandardCount; i++)		/* standard fields always shown */
		if (!EditHasKind(kStandard[i]))
			EditAppend(kStandard[i], 0, "");
	EditSortFields();

	if (recP == NULL) {		/* new contact: seed phone chips with last-used dial code */
		Int16 ld = A35GetLastDial();
		if (ld > 0)
			for (i = 0; i < gFieldCount; i++)
				if (EditIsPhoneKind(gFields[i].kind))
					gFields[i].sub = (UInt8) ld;
	}
}

/* one-line summary of a TAB-delimited address: each part trimmed, empties
 * dropped, joined with ", " */
static void AddrSummary(const char *value, char *out, UInt16 outLen)
{
	const char *p = value ? value : "";
	UInt16 o = 0;
	Boolean first = true;

	out[0] = '\0';
	while (1) {
		char field[64];
		UInt16 f = 0, k, st;

		while (*p && *p != '\t' && f < sizeof(field) - 1)
			field[f++] = *p++;
		field[f] = '\0';
		while (f > 0 && field[f - 1] == ' ')		/* trim trailing */
			field[--f] = '\0';
		st = 0;
		while (field[st] == ' ')					/* trim leading */
			st++;
		if (field[st]) {
			if (!first && o < outLen - 3) { out[o++] = ','; out[o++] = ' '; }
			for (k = st; field[k] && o < outLen - 1; k++)
				out[o++] = field[k];
			out[o] = '\0';
			first = false;
		}
		if (*p == '\t') p++;
		else break;
	}
}

/* extract + trim TAB-delimited field #idx of src */
static void AddrField(const char *src, UInt16 idx, char *out, UInt16 outLen)
{
	const char *p = src ? src : "";
	UInt16 field = 0, o = 0, st = 0;
	Int16 e;

	out[0] = '\0';
	while (*p && field < idx) { if (*p == '\t') field++; p++; }
	if (field < idx) return;
	while (*p && *p != '\t' && o < (UInt16) (outLen - 1))
		out[o++] = *p++;
	out[o] = '\0';
	e = (Int16) StrLen(out);
	while (e > 0 && out[e - 1] == ' ') out[--e] = '\0';
	while (out[st] == ' ') st++;
	if (st) StrCopy(out, out + st);
}

/* multi-line address (linefeeds) for the inline edit field */
static void AddrMultiline(const char *value, char *out, UInt16 outLen)
{
	char pt[7][72], line[160];		/* parts sized for long (e.g. Thai) address lines */
	UInt16 k;
	Boolean firstLine = true;

	for (k = 0; k < 7; k++)
		AddrField(value, k, pt[k], sizeof(pt[k]));
	out[0] = '\0';
#define ADDR_LINE(s)	do { \
		if (!firstLine) StrNCat(out, "\n", (Int16)outLen); \
		StrNCat(out, (s), (Int16)outLen); \
		firstLine = false; \
	} while (0)
	if (pt[0][0]) ADDR_LINE(pt[0]);
	if (pt[1][0]) ADDR_LINE(pt[1]);
	if (pt[2][0]) { StrCopy(line, "Apt "); StrCat(line, pt[2]); ADDR_LINE(line); }
	line[0] = '\0';
	if (pt[3][0]) StrCat(line, pt[3]);
	if (pt[4][0]) { if (line[0]) StrCat(line, ", "); StrCat(line, pt[4]); }
	if (pt[5][0]) { if (line[0]) StrCat(line, " "); StrCat(line, pt[5]); }
	if (line[0]) ADDR_LINE(line);
	if (pt[6][0]) ADDR_LINE(pt[6]);
#undef ADDR_LINE
}

static UInt16 AddrLineCount(UInt16 mi)
{
	char ml[a35EditValLen];
	UInt16 n = 1, k;

	AddrMultiline(gFields[mi].value, ml, sizeof(ml));
	for (k = 0; ml[k]; k++)
		if (ml[k] == '\n')
			n++;
	return n;
}

/* Build an <=maxLines preview of `note`, wrapped to pxWidth (stdFont), lines
 * joined with '\n'; append "..." on the last line when text remains.  Newlines
 * in the note are flattened to spaces so the preview is a compact excerpt. */
static void NotePreview(const char *note, Int16 pxWidth, UInt16 maxLines,
	char *out, UInt16 outLen)
{
	FontID of = FntSetFont(stdFont);
	char flat[400];
	const char *src = note ? note : "";
	const char *p;
	UInt16 fo = 0, o = 0, line;
	Int16 ellW = FntCharsWidth("...", 3);
	Boolean capped;

	for (; src[fo] && fo < (UInt16) (sizeof(flat) - 1); fo++) {
		char c = src[fo];
		flat[fo] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
	}
	flat[fo] = '\0';
	capped = (Boolean) (src[fo] != '\0');	/* note longer than the flat buffer */

	p = flat;
	for (line = 0; line < maxLines && *p; line++) {
		Boolean last = (Boolean) (line + 1 == maxLines);
		UInt16 remain = (UInt16) StrLen(p);
		Boolean lead;
		Int16 tw, fit, k;
		Boolean moreAfter;

		fit = FntWidthToOffset(p, remain, pxWidth, &lead, &tw);
		if (fit > (Int16) remain) fit = (Int16) remain;
		moreAfter = (Boolean) ((UInt16) fit < remain || capped);
		if (last && moreAfter) {
			fit = FntWidthToOffset(p, remain, (Int16) (pxWidth - ellW),
				&lead, &tw);
			if (fit < 0) fit = 0;
		}
		if (fit <= 0) fit = 1;
		if (line > 0 && o < (UInt16) (outLen - 1))
			out[o++] = '\n';
		for (k = 0; k < fit && o < (UInt16) (outLen - 1); k++)
			out[o++] = p[k];
		p += fit;
		if (last && moreAfter && o < (UInt16) (outLen - 4)) {
			out[o] = '\0';
			StrCat(out, "...");
			o = (UInt16) (o + 3);
		}
	}
	out[o] = '\0';
	FntSetFont(of);
}

/* Store the full text of a note field in its `big` handle and put a compact
 * preview in `value` (shown in the edit row).  Truncates to kNoteMax. */
static void NoteStoreFull(UInt16 mi, const char *text)
{
	UInt16 len = text ? (UInt16) StrLen(text) : 0;
	MemHandle h;
	char *p;

	if (len > kNoteMax) len = kNoteMax;
	if (gFields[mi].big != NULL) {		/* replace any existing */
		MemHandleFree(gFields[mi].big);
		gFields[mi].big = NULL;
	}
	h = MemHandleNew((UInt32) (len + 1));
	if (h != NULL) {
		p = (char *) MemHandleLock(h);
		if (text) StrNCopy(p, text, len);
		p[len] = '\0';
		MemHandleUnlock(h);
		gFields[mi].big = h;
	}
	NotePreview(text ? text : "", kNoteValW, kNoteEditLines,
		gFields[mi].value, a35EditValLen);
}

#define kViewTop	16
#define kViewBottom	128		/* rows occupy y 16..128 (buttons are at 147) */

static Int16 EditLineH(void)
{
	FontID o = FntSetFont(stdFont);
	Int16 h = FntLineHeight();
	FntSetFont(o);
	return h;
}

/* a long email wraps to 2 lines ("user" / "@domain") in the 87px value field */
static Boolean EditEmailWraps(UInt16 mi)
{
	const char *v = gFields[mi].value;
	const char *at;
	Int16 w;
	FontID o;

	if (gFields[mi].kind != ftEmail)
		return false;
	at = StrChr(v, '@');
	if (at == NULL || at == v)
		return false;
	o = FntSetFont(stdFont);
	w = FntCharsWidth(v, StrLen(v));
	FntSetFont(o);
	return (Boolean) (w > 87);
}

/* pixel height of a field's value box (address = its exact line count) */
static Int16 EditFieldHeight(UInt16 mi)
{
	if (gFields[mi].kind == ftAddress)
		return (Int16) (AddrLineCount(mi) * EditLineH() + 2);
	if (gFields[mi].kind == ftNote)
		return (Int16) (kNoteEditLines * EditLineH() + 2);
	if (EditEmailWraps(mi))
		return (Int16) (2 * EditLineH() + 2);
	return 11;
}

/* ---- viewport (physical rows onto the array) --------------------------- */
static void EditSetRowValue(UInt16 row, const char *s)
{
	FieldPtr fld = (FieldPtr) EditObj(fldEditRowBase + row);
	MemHandle old, nh;
	char *p;
	UInt16 len = s ? StrLen(s) : 0;

	if (fld == NULL)
		return;
	old = FldGetTextHandle(fld);
	nh = MemHandleNew((UInt32) (len + 1));
	if (nh == NULL)
		return;
	p = (char *) MemHandleLock(nh);
	if (s) StrCopy(p, s); else p[0] = '\0';
	MemHandleUnlock(nh);
	FldSetTextHandle(fld, nh);
	if (old != NULL)
		MemHandleFree(old);
}

static void EditSyncViewport(void)
{
	UInt16 slot;

	for (slot = 0; slot < kEditRows; slot++) {
		UInt16 mi = gSlotField[slot];
		const char *v;

		if (mi == kNoField)
			continue;
		/* address, email, social, birthday & anniversary are dialog-edited; their
		 * inline text is a formatted display that differs from the stored value,
		 * so don't read it back */
		if (gFields[mi].kind == ftAddress || gFields[mi].kind == ftEmail ||
			gFields[mi].kind == ftSocial || gFields[mi].kind == ftBirthday ||
			gFields[mi].kind == ftAnniversary || gFields[mi].kind == ftRelation ||
			gFields[mi].kind == ftNote ||		/* note edited via full-screen dialog */
			EditIsPhoneKind(gFields[mi].kind))	/* phone edited via its own dialog */
			continue;
		v = FldGetTextPtr((FieldPtr) EditObj(fldEditRowBase + slot));
		StrNCopy(gFields[mi].value, v ? v : "", a35EditValLen - 1);
		gFields[mi].value[a35EditValLen - 1] = '\0';
	}
}

/* Row display text for a phone field: "+NN number" if a dial code is set, else
 * just the bare number.  The row is read-only; EditPhoneDialog does the editing. */
static void EditPhoneDisplay(UInt16 mi, char *out, UInt16 outLen)
{
	const char *dc = A35DialCode((Int16) gFields[mi].sub);

	out[0] = '\0';
	if (dc[0]) {
		StrNCopy(out, dc, outLen - 1); out[outLen - 1] = '\0';
		StrNCat(out, " ", (Int16) outLen);
	}
	StrNCat(out, gFields[mi].value, (Int16) outLen);
}

/* Inline display text for a dialog-edited field: social shows "handle (Network)"
 * and anniversary shows "15 Jun 1997 (Wedding)"; birthday is just the date. */
static void EditKindDisplay(UInt16 mi, char *out, UInt16 outLen)
{
	const char *v = gFields[mi].value;
	UInt8 kind = gFields[mi].kind;

	out[0] = '\0';
	if (kind == ftRelation) {
		/* "<Type>: <target name>"; value is the target's unique id (decimal) */
		UInt16 tgt;
		char nm[48];
		StrNCopy(out, EditRelTypeName(gFields[mi].sub), outLen - 1);
		out[outLen - 1] = '\0';
		if (v && v[0] &&
			A35FindByUniqueID(gAddrDB, (UInt32) StrAToI(v), &tgt)) {
			A35RecordDisplayName(gAddrDB, tgt, nm, sizeof(nm));
			StrNCat(out, ": ", (Int16) outLen);
			StrNCat(out, nm, (Int16) outLen);
		}
		return;
	}
	if (kind == ftBirthday || kind == ftAnniversary) {
		char ds[24];
		A35BirthdayDisplay(v, ds);
		StrNCopy(out, ds, outLen - 1); out[outLen - 1] = '\0';
		if (kind == ftAnniversary && out[0]) {
			StrNCat(out, " (", (Int16) outLen);
			if (gFields[mi].sub == a35AnnivOther &&
				v && StrLen(v) > a35BirthdayStrLen)	/* custom name after date */
				StrNCat(out, v + a35BirthdayStrLen,
					(Int16) outLen);
			else
				StrNCat(out, EditAnnivTypeName(gFields[mi].sub),
					(Int16) outLen);
			StrNCat(out, ")", (Int16) outLen);
		}
	} else if (kind == ftSocial) {
		const char *net;
		if (gFields[mi].sub == a35socOther) {		/* value = "Platform: handle" */
			const char *sep = v ? StrStr(v, ": ") : NULL;
			if (sep == NULL) { StrNCopy(out, v ? v : "", outLen - 1); out[outLen - 1] = '\0'; return; }
			StrNCopy(out, sep + 2, outLen - 1); out[outLen - 1] = '\0';	/* handle */
			net = v;									/* platform name is the prefix */
			StrNCat(out, " (", (Int16) outLen);
			{ UInt16 n = (UInt16) (sep - v), avail = (UInt16) (outLen - StrLen(out) - 2);
			  if (n > avail) n = avail;
			  StrNCat(out, net, (Int16) n); }
			StrNCat(out, ")", (Int16) outLen);
		} else {
			StrNCopy(out, v ? v : "", outLen - 1); out[outLen - 1] = '\0';
			if (out[0]) {
				StrNCat(out, " (", (Int16) outLen);
				StrNCat(out, EditSocialNetName(gFields[mi].sub),
					(Int16) outLen);
				StrNCat(out, ")", (Int16) outLen);
			}
		}
	} else {
		StrNCopy(out, v ? v : "", outLen - 1); out[outLen - 1] = '\0';
	}
}

/* position + size physical slot's value field at (y) with the given height */
static void EditSetSlotField(FormPtr f, UInt16 slot, Int16 y, Int16 fieldH,
	Boolean multi, const char *text)
{
	UInt16 fi = FrmGetObjectIndex(f, fldEditRowBase + slot);
	FieldPtr fld = (FieldPtr) ObjInForm(f, fldEditRowBase + slot);
	FieldAttrType attr;
	RectangleType rc;

	if (fld == NULL)
		return;
	FldGetAttributes(fld, &attr);
	attr.singleLine = multi ? 0 : 1;
	FldSetAttributes(fld, &attr);
	rc.topLeft.x = 63;
	rc.topLeft.y = (Int16) (y + 1);
	rc.extent.x = 87;
	rc.extent.y = fieldH;
	FrmSetObjectBounds(f, fi, &rc);
	EditSetRowValue(slot, text);
	FldRecalculateField(fld, false);
}

/* lay the section-sorted fields into physical rows at cumulative y positions,
 * so a multi-line address just makes its own row taller (no grid gaps). */
static void EditLoadViewport(void)
{
	FormPtr f = FrmGetActiveForm();
	UInt16 slot = 0, mi = gFieldTop, r;
	Int16 y = kViewTop;
	RectangleType clear;

	clear.topLeft.x = 0;
	clear.topLeft.y = kViewTop;
	clear.extent.x = 152;
	clear.extent.y = (Int16) (kViewBottom - kViewTop + 2);
	WinEraseRectangle(&clear, 0);

	for (r = 0; r < kEditRows; r++)
		gSlotField[r] = kNoField;

	while (slot < kEditRows && mi < gFieldCount) {
		Boolean isAddr = (Boolean) (gFields[mi].kind == ftAddress);
		Int16 fh = EditFieldHeight(mi);

		if (y + fh > kViewBottom) {
			if (slot == 0)			/* clamp the first field to what fits */
				fh = (Int16) (kViewBottom - y - 1);
			else
				break;
		}
		gSlotField[slot] = mi;

		FrmSetObjectPosition(f, FrmGetObjectIndex(f, selEditRowBase + slot), 0, y);
		FrmSetObjectPosition(f, FrmGetObjectIndex(f, colEditRowBase + slot), 58,
			(Int16) (y + 1));
		EditKindLabel(gFields[mi].kind, gRowLabel[slot]);
		CtlSetLabel((ControlPtr) ObjInForm(f, selEditRowBase + slot),
			gRowLabel[slot]);
		if (isAddr) {
			char ml[a35EditValLen];
			AddrMultiline(gFields[mi].value, ml, sizeof(ml));
			EditSetSlotField(f, slot, y, fh, true, ml);
		} else if (EditEmailWraps(mi)) {
			char em[a35EditValLen];
			char *at;
			StrNCopy(em, gFields[mi].value, sizeof(em) - 1);
			em[sizeof(em) - 1] = '\0';
			at = StrChr(em, '@');			/* "user@domain" -> "user\n@domain" */
			if (at) { MemMove(at + 1, at, StrLen(at) + 1); *at = '\n'; }
			EditSetSlotField(f, slot, y, fh, true, em);
		} else if (gFields[mi].kind == ftNote) {
			/* multi-line 2-row preview; the value already holds the preview text */
			EditSetSlotField(f, slot, y, fh, true, gFields[mi].value);
		} else if (gFields[mi].kind == ftBirthday ||
				gFields[mi].kind == ftAnniversary ||
				gFields[mi].kind == ftSocial ||
				gFields[mi].kind == ftRelation) {
			char ds[a35EditValLen];		/* date/social/relation display text */
			EditKindDisplay(mi, ds, sizeof(ds));
			EditSetSlotField(f, slot, y, fh, false, ds);
		} else if (EditIsPhoneKind(gFields[mi].kind)) {
			/* dialog-edited (own modal, avoids the inline chip's scroll redraw
			 * artifacts); the row just shows "+NN number" (or the bare number) */
			char ds[a35EditValLen];
			EditPhoneDisplay(mi, ds, sizeof(ds));
			EditSetSlotField(f, slot, y, fh, false, ds);
		} else {
			EditSetSlotField(f, slot, y, fh, false, gFields[mi].value);
		}
		FrmShowObject(f, FrmGetObjectIndex(f, selEditRowBase + slot));
		FrmShowObject(f, FrmGetObjectIndex(f, fldEditRowBase + slot));
		FrmShowObject(f, FrmGetObjectIndex(f, colEditRowBase + slot));

		y = (Int16) (y + fh + 3);
		slot++;
		mi++;
	}
	for (; slot < kEditRows; slot++) {
		FrmHideObject(f, FrmGetObjectIndex(f, selEditRowBase + slot));
		FrmHideObject(f, FrmGetObjectIndex(f, fldEditRowBase + slot));
		FrmHideObject(f, FrmGetObjectIndex(f, colEditRowBase + slot));
	}
}

/* topmost field index when scrolled all the way down */
static Int16 EditMaxTop(void)
{
	Int16 h = 0;
	UInt16 i = gFieldCount;

	while (i > 0) {
		Int16 fh = EditFieldHeight(i - 1);
		Int16 add = (h == 0) ? fh : (Int16) (fh + 3);

		if (h + add > (Int16) (kViewBottom - kViewTop))
			break;
		h = (Int16) (h + add);
		i--;
	}
	return (Int16) i;
}

static void EditUpdateScroll(void)
{
	ScrollBarPtr sb = (ScrollBarPtr) EditObj(sclEditFields);
	Int16 max = EditMaxTop();
	Int16 val = (Int16) gFieldTop;

	if (val > max)
		val = max;
	if (sb != NULL)
		SclSetScrollBar(sb, val, 0, max, kEditRows);
}

static void EditScroll(Int16 newTop)
{
	Int16 max = EditMaxTop();

	EditSyncViewport();
	if (newTop < 0)
		newTop = 0;
	if (newTop > max)
		newTop = max;
	gFieldTop = (UInt16) newTop;
	EditLoadViewport();
	EditUpdateScroll();
	FrmDrawForm(FrmGetActiveForm());
}

/* find the physical slot currently showing field index mi (kNoField if none) */
static UInt16 EditSlotOf(UInt16 mi)
{
	UInt16 s;

	for (s = 0; s < kEditRows; s++)
		if (gSlotField[s] == mi)
			return s;
	return kNoField;
}

/* scroll so field index mi is visible, then put the cursor in its value field */
static void EditFocusField(UInt16 mi)
{
	FormPtr f = FrmGetActiveForm();
	UInt16 slot;

	if (mi < gFieldTop)
		gFieldTop = mi;
	EditLoadViewport();
	slot = EditSlotOf(mi);
	if (slot == kNoField) {			/* fell off the bottom -> bring to top */
		gFieldTop = mi;
		EditLoadViewport();
		slot = EditSlotOf(mi);
	}
	EditUpdateScroll();
	FrmDrawForm(f);

	if (slot != kNoField) {
		FieldPtr fld = (FieldPtr) EditObj(fldEditRowBase + slot);
		FrmSetFocus(f, FrmGetObjectIndex(f, fldEditRowBase + slot));
		if (fld != NULL)
			FldSetInsPtPosition(fld, FldGetTextLength(fld));
	}
}

static Int16 KindToPickIndex(UInt8 kind)
{
	UInt16 i;

	for (i = 0; i < kPickCount; i++)
		if (kPickKind[i] == kind)
			return (Int16) i;
	return 0;
}

static void EditApplyType(UInt16 slot, Int16 sel)
{
	UInt16 mi = gSlotField[slot], i, newmi = 0;

	if (mi == kNoField || sel < 0 || sel >= (Int16) kPickCount)
		return;
	EditSyncViewport();
	gFields[mi].kind = kPickKind[sel];
	if (kPickKind[sel] != ftSocial)
		gFields[mi].sub = 0;
	for (i = 0; i < gFieldCount; i++)		/* track the field across the re-sort */
		gFields[i].mark = 0;
	gFields[mi].mark = 1;
	EditSortFields();
	for (i = 0; i < gFieldCount; i++)
		if (gFields[i].mark) { newmi = i; gFields[i].mark = 0; break; }
	EditFocusField(newmi);			/* keep it on-screen + focus its value */
}

/* tap a row's type-selector trigger -> searchable list (A35PickList), so the
 * field type can be found by Graffiti/keyboard input, not just tap-and-scroll. */
static void EditTypePick(UInt16 slot)
{
	UInt16 mi = gSlotField[slot];
	Int16 sel;

	if (mi == kNoField)
		return;
	sel = A35PickList("Field Type", kFieldTypeNames, kPickCount,
		KindToPickIndex(gFields[mi].kind));
	if (sel >= 0)
		EditApplyType(slot, sel);
}

/* tap a pick-list value cell (Gender/Pronouns) -> choose text, no keyboard */
static Boolean EditPickValue(UInt16 slot, UInt16 listID,
	const char * const *opts, UInt16 nopt)
{
	UInt16 mi = gSlotField[slot];
	ListPtr lst = (ListPtr) EditObj(listID);
	FieldPtr fld;
	Int16 cur = -1, sel, i;

	if (mi == kNoField || lst == NULL)
		return false;
	for (i = 0; i < (Int16) nopt; i++)
		if (StrCompare(gFields[mi].value, opts[i]) == 0) {
			cur = i;
			break;
		}
	LstSetSelection(lst, cur);
	{
		Int16 fx, fy;
		FrmGetObjectPosition(FrmGetActiveForm(),
			FrmGetObjectIndex(FrmGetActiveForm(), fldEditRowBase + slot),
			&fx, &fy);
		LstSetPosition(lst, 63, fy);
	}
	sel = LstPopupList(lst);
	if (sel >= 0 && sel < (Int16) nopt) {
		StrNCopy(gFields[mi].value, opts[sel], a35EditValLen - 1);
		gFields[mi].value[a35EditValLen - 1] = '\0';
		EditSetRowValue(slot, gFields[mi].value);
		fld = (FieldPtr) EditObj(fldEditRowBase + slot);
		if (fld != NULL)
			FldDrawField(fld);
	}
	return true;
}

/* ---- category ---------------------------------------------------------- */
static void EditSetCategoryLabel(void)
{
	ControlType *ctl = (ControlType *) EditObj(popEditCategory);

	CategoryGetName(gAddrDB, gEditCat, gEditCatName);
	if (ctl != NULL)
		CategorySetTriggerLabel(ctl, gEditCatName);
}

/* Build the picker ourselves (named categories + Unfiled + "Edit Categories")
 * and show it with LstPopupList - the same reliable path the main list uses.
 * ROM CategorySelect() did not update gEditCat here, so the record kept its old
 * category on save. */
static void EditSelectCategory(void)
{
	FormPtr f = FrmGetActiveForm();
	ListPtr lst = (ListPtr) EditObj(lstEditCategory);
	static char  names[20][24];		/* >dmCategoryLength: fits "Edit Categories..." */
	static char *choices[20];
	Int16   meaning[20];			/* >=0 category index; -101 Edit Categories */
	Int16   n = 0, i, sel, m, cur = -1;

	for (i = 1; i < dmRecNumCategories && n < 16; i++) {
		CategoryGetName(gAddrDB, i, names[n]);
		if (names[n][0]) {
			choices[n] = names[n]; meaning[n] = i;
			if ((UInt16) i == gEditCat) cur = n;
			n++;
		}
	}
	CategoryGetName(gAddrDB, dmUnfiledCategory, names[n]);
	if (names[n][0]) {
		choices[n] = names[n]; meaning[n] = dmUnfiledCategory;
		if (gEditCat == dmUnfiledCategory) cur = n;
		n++;
	}
	StrCopy(names[n], "Edit Categories\205"); choices[n] = names[n]; meaning[n] = -101; n++;

	LstSetListChoices(lst, choices, n);
	LstSetSelection(lst, cur);
	LstSetHeight(lst, n < 11 ? n : 11);
	sel = LstPopupList(lst);
	if (sel < 0) return;
	m = meaning[sel];

	if (m == -101) {			/* Edit Categories -> ROM dialog */
		char name[dmCategoryLength];
		UInt16 cat = gEditCat;
		if (CategorySelect(gAddrDB, f, popEditCategory, lstEditCategory, false,
				&cat, name, 1, 0))
			gEditCat = cat;
	} else {
		gEditCat = (UInt16) m;
	}
	EditSetCategoryLabel();
}

/* ---- init / save ------------------------------------------------------- */
static void EditInit(void)
{
	FormPtr f = FrmGetActiveForm();
	A35Record rec;
	MemHandle h;
	Int16 focusField = -1;

	gEditDeleted = false;
	gEditSaved = false;

	if (gFields == NULL)
		gFields = (EditField *) MemPtrNew(sizeof(EditField) * a35EditMaxFields);
	if (gFields == NULL) {			/* OOM: open empty rather than crash */
		gFieldCount = 0;
		return;
	}
	MemSet(gFields, sizeof(EditField) * a35EditMaxFields, 0);

	if (gEditIndex != a35NoRecord) {
		UInt16 attr = 0;
		DmRecordInfo(gAddrDB, gEditIndex, &attr, NULL, NULL);
		gEditCat = (UInt16) (attr & dmRecAttrCategoryMask);
	} else {
		gEditCat = gShowAllCategories ? dmUnfiledCategory : gCurrentCategory;
	}
	EditSetCategoryLabel();

	if (gEditIndex != a35NoRecord &&
		A35GetRecord(gAddrDB, gEditIndex, &rec, &h) == errNone) {
		EditFieldsFromRecord(&rec);
		A35ReleaseRecord(h);
	} else {
		EditFieldsFromRecord(NULL);
	}

	{	/* if a field was tapped in the record view, scroll it to the top */
		Int16 fo = gEditFocusOrigin;
		gEditFocusOrigin = -1;
		if (fo >= 0 && gEditIndex != a35NoRecord) {
			UInt16 j;
			for (j = 0; j < gFieldCount; j++)
				if (gFields[j].origin == fo) {
					gFieldTop = j;
					focusField = (Int16) j;
					break;
				}
		}
	}

	EditLoadViewport();
	EditUpdateScroll();
	FrmDrawForm(f);

	if (focusField >= 0) {		/* place the text cursor in the tapped field */
		UInt16 slot;
		for (slot = 0; slot < kEditRows; slot++)
			if (gSlotField[slot] == (UInt16) focusField) {
				FrmSetFocus(f, FrmGetObjectIndex(f, fldEditRowBase + slot));
				break;
			}
	}
}

static void EditApplyCategory(UInt16 index)
{
	UInt16 attr = 0;

	DmRecordInfo(gAddrDB, index, &attr, NULL, NULL);
	attr = (UInt16) ((attr & ~dmRecAttrCategoryMask) | gEditCat | dmRecAttrDirty);
	DmSetRecordInfo(gAddrDB, index, &attr, NULL);
}

static void EditSave(void)
{
	A35Record r;
	UInt16 i;

	EditSyncViewport();
	A35RecordInit(&r);
	for (i = 0; i < gFieldCount; i++) {
		UInt8 type, label;

		if (!gFields[i].value[0])
			continue;
		if (gFields[i].kind == ftRelation) {	/* pack uid+relType (persist buf) */
			A35Relation rel;
			rel.targetUniqueID = (UInt32) StrAToI(gFields[i].value);
			rel.relType = gFields[i].sub;
			A35PackRelation(&rel, gRelPack[i]);
			A35AddField(&r, a35fRelation, 0, gRelPack[i], a35RelationPackLen);
			continue;
		}
		if (gFields[i].kind == ftNote) {	/* full text lives in `big`, not value */
			if (gFields[i].big != NULL) {	/* stays LOCKED until after the pack */
				char *nt = (char *) MemHandleLock(gFields[i].big);
				KindToTLV(&gFields[i], &type, &label);
				A35AddStr(&r, type, label, nt);
			}
			continue;
		}
		/* phone: prepend the chosen "+NN " dial code back onto the number so the
		 * stored value is vCard-friendly ("+66 812...") (form closes after save,
		 * so overwriting the working value here is safe) */
		if (EditIsPhoneKind(gFields[i].kind) && gFields[i].sub > 0) {
			const char *dc = A35DialCode((Int16) gFields[i].sub);
			if (dc[0] && gFields[i].value[0] != '+') {
				char num[a35EditValLen];
				StrNCopy(num, gFields[i].value, sizeof(num) - 1);
				num[sizeof(num) - 1] = '\0';
				StrCopy(gFields[i].value, dc);
				StrNCat(gFields[i].value, " ", (Int16) a35EditValLen);
				StrNCat(gFields[i].value, num, (Int16) a35EditValLen);
			}
		}
		KindToTLV(&gFields[i], &type, &label);
		A35AddStr(&r, type, label, gFields[i].value);
	}

	if (!A35RecordHasData(&r)) {
		if (gEditIndex != a35NoRecord) {
			A35ForgetMeIfDeleted(gEditIndex);
			DmRemoveRecord(gAddrDB, gEditIndex);
		}
		gEditIndex = a35NoRecord;		/* nothing to view -> back to list */
		return;
	}
	if (gEditIndex == a35NoRecord) {
		UInt16 idx = dmMaxRecordIndex;
		if (A35NewRecord(gAddrDB, &r, &idx) == errNone) {
			EditApplyCategory(idx);
			A35EncryptRecordInPlace(gAddrDB, idx);	/* seal (foreground) */
			gEditIndex = idx;			/* so Done can open its view */
		}
	} else {
		A35ChangeRecord(gAddrDB, &gEditIndex, &r);	/* re-seals internally */
		EditApplyCategory(gEditIndex);
	}

	/* the note `big` handles were locked while packed; release them now (same
	 * condition as the lock above: non-empty preview + a handle) */
	for (i = 0; i < gFieldCount; i++)
		if (gFields[i].kind == ftNote && gFields[i].value[0] &&
			gFields[i].big != NULL)
			MemHandleUnlock(gFields[i].big);

	/* auto-reciprocal: mirror each relation onto the target contact (this record
	 * now has a stable unique id) so e.g. Bob's "Spouse: Jane" gives Jane
	 * "Spouse: Bob". */
	if (gEditIndex != a35NoRecord) {
		UInt32 selfUid = 0;
		DmRecordInfo(gAddrDB, gEditIndex, NULL, &selfUid, NULL);
		for (i = 0; i < gFieldCount; i++)
			if (gFields[i].kind == ftRelation && gFields[i].value[0])
				A35AddReciprocalRelation(gAddrDB,
					(UInt32) StrAToI(gFields[i].value), selfUid,
					A35ReciprocalRelType(gFields[i].sub));
	}
}

static void EditDelete(void)
{
	gEditDeleted = true;
	if (gEditIndex != a35NoRecord) {
		A35ForgetMeIfDeleted(gEditIndex);
		DmRemoveRecord(gAddrDB, gEditIndex);
	}
	FrmGotoForm(frmMain);
}

/* add a new field: default to Other (sorts last), scroll to it, focus its value */
static void EditAddNewField(void)
{
	UInt16 i, newmi = 0;

	EditSyncViewport();
	if (gFieldCount >= a35EditMaxFields)
		return;
	for (i = 0; i < gFieldCount; i++)
		gFields[i].mark = 0;
	EditAppend((UInt8) ftOther, 0, "");
	gFields[gFieldCount - 1].mark = 1;
	EditSortFields();
	for (i = 0; i < gFieldCount; i++)
		if (gFields[i].mark) { newmi = i; gFields[i].mark = 0; break; }
	EditFocusField(newmi);
}

/* ---- remove-fields dialog (checkbox list; uncheck to delete) ----------- */
static Boolean gKeep[a35EditMaxFields];
static UInt16 gDelTop;

static char gDelLabel[kDelRows][80];	/* persistent checkbox labels ("Label : Value") */

static void DelSyncViewport(FormPtr f)
{
	UInt16 r;

	for (r = 0; r < kDelRows; r++) {
		UInt16 mi = (UInt16) (gDelTop + r);
		if (mi >= gFieldCount)
			break;
		gKeep[mi] = (Boolean) CtlGetValue(
			(ControlPtr) ObjInForm(f, cbDelRowBase + r));
	}
}

static void DelLoadViewport(FormPtr f)
{
	UInt16 r;

	for (r = 0; r < kDelRows; r++) {
		UInt16 mi = (UInt16) (gDelTop + r);
		UInt16 ci = FrmGetObjectIndex(f, cbDelRowBase + r);

		if (mi < gFieldCount) {
			ControlPtr cb = (ControlPtr) ObjInForm(f, cbDelRowBase + r);
			char val[a35EditValLen];
			Int16 w, l;
			Boolean fit;

			EditKindLabel(gFields[mi].kind, gDelLabel[r]);	/* short label */
			EditKindDisplay(mi, val, sizeof(val));	/* value shown next to the label */
			if (val[0]) {
				StrNCat(gDelLabel[r], " : ",
					(Int16) (sizeof(gDelLabel[r]) - StrLen(gDelLabel[r]) - 1));
				StrNCat(gDelLabel[r], val,
					(Int16) (sizeof(gDelLabel[r]) - StrLen(gDelLabel[r]) - 1));
			}
			/* keep the "Label : Value" to one line: truncate to the checkbox
			 * width and end with an ellipsis when it doesn't fit. */
			FntSetFont(stdFont);
			w = 132; l = (Int16) StrLen(gDelLabel[r]);
			FntCharsInWidth(gDelLabel[r], &w, &l, &fit);
			if (!fit && l > 1) {
				gDelLabel[r][l - 1] = (char) 0x85;	/* ellipsis */
				gDelLabel[r][l] = '\0';
			}
			CtlSetLabel(cb, gDelLabel[r]);
			CtlSetValue(cb, (Int16) gKeep[mi]);
			FrmShowObject(f, ci);
		} else {
			FrmHideObject(f, ci);
		}
	}
}

static void DelScrollbar(FormPtr f)
{
	ScrollBarPtr sb = (ScrollBarPtr) ObjInForm(f, sclDelField);
	Int16 max = (Int16) ((gFieldCount > kDelRows) ? gFieldCount - kDelRows : 0);
	Int16 v = (Int16) gDelTop;

	if (v > max) v = max;
	SclSetScrollBar(sb, v, 0, max, kDelRows);
}

static void EditRemoveFieldsDialog(void)
{
	FormPtr prev = FrmGetActiveForm();
	FormPtr frm;
	UInt16 i, n;
	Boolean done = false, apply = false;

	EditSyncViewport();
	if (gFieldCount == 0)
		return;
	for (i = 0; i < gFieldCount; i++)
		gKeep[i] = true;
	gDelTop = 0;

	frm = FrmInitForm(frmDelField);
	FrmSetActiveForm(frm);
	DelLoadViewport(frm);
	DelScrollbar(frm);
	FrmDrawForm(frm);

	while (!done) {
		EventType ev;

		EvtGetEvent(&ev, evtWaitForever);

		if (ev.eType == sclRepeatEvent &&
			ev.data.sclRepeat.scrollBarID == sclDelField) {
			DelSyncViewport(frm);
			gDelTop = (UInt16) ev.data.sclRepeat.newValue;
			DelLoadViewport(frm);
			DelScrollbar(frm);
			FrmDrawForm(frm);
			continue;
		}
		if (ev.eType == ctlSelectEvent) {
			UInt16 cid = ev.data.ctlSelect.controlID;

			/* checkboxes toggle themselves; read them on OK/scroll */
			if (cid == btnDelFieldCancel) { done = true; continue; }
			if (cid == btnDelFieldOK) {
				UInt16 removed = 0;
				DelSyncViewport(frm);
				for (i = 0; i < gFieldCount; i++)
					if (!gKeep[i]) removed++;
				if (removed == 0) { done = true; continue; }
				if (FrmAlert(altDelFieldWarn) == 0) { apply = true; done = true; }
				continue;
			}
		}
		if (ev.eType == appStopEvent) {
			EvtAddEventToQueue(&ev);
			done = true;
			continue;
		}
		if (!SysHandleEvent(&ev))
			FrmDispatchEvent(&ev);
	}

	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prev)
		FrmSetActiveForm(prev);

	if (apply) {
		for (i = 0; i < gFieldCount; i++)	/* free removed notes' big handles */
			if (!gKeep[i] && gFields[i].big != NULL) {
				MemHandleFree(gFields[i].big);
				gFields[i].big = NULL;
			}
		for (i = 0, n = 0; i < gFieldCount; i++)
			if (gKeep[i]) { if (n != i) gFields[n] = gFields[i]; n++; }
		gFieldCount = n;
		gFieldTop = 0;
	}
	/* full-screen dialog erased the edit form; repaint it either way */
	EditLoadViewport();
	EditUpdateScroll();
	FrmDrawForm(prev);
}

Boolean EditFormHandleEvent(EventPtr eventP)
{
	Boolean handled = false;

	switch (eventP->eType) {
	case frmOpenEvent:
		EditInit();
		handled = true;
		break;

	case sclRepeatEvent:
		if (eventP->data.sclRepeat.scrollBarID == sclEditFields)
			EditScroll(eventP->data.sclRepeat.newValue);
		break;

	case fldEnterEvent: {
		UInt16 fid = eventP->data.fldEnter.fieldID;

		if (fid >= fldEditRowBase && fid < fldEditRowBase + kEditRows) {
			UInt16 slot = (UInt16) (fid - fldEditRowBase);
			UInt16 mi = gSlotField[slot];

			if (mi != kNoField) {
				if (gFields[mi].kind == ftPrefix)
					handled = EditPickValue(slot, lstEditPrefix,
						kPrefixOpt, kPrefixCount);
				else if (gFields[mi].kind == ftGender)
					handled = EditPickValue(slot, lstEditGender,
						kGenderOpt, kGenderCount);
				else if (gFields[mi].kind == ftPronouns)
					handled = EditPickValue(slot, lstEditPronouns,
						kPronounOpt, kPronounCount);
				else if (gFields[mi].kind == ftEmail) {
					/* the row keeps showing the plain address (its value box has
					 * its own long-address wrap logic, EditEmailWraps); the label
					 * (Home/Work/iCloud/Other), chosen in-dialog, is stored/
					 * exported and shown as the row header in the read-only
					 * Record view (RecEmailLabel), not appended here. */
					if (EditEmailDialog(gFields[mi].value, a35EditValLen,
							&gFields[mi].sub)) {
						EditSetRowValue(slot, gFields[mi].value);
						FldDrawField((FieldPtr) EditObj(fldEditRowBase + slot));
					}
					handled = true;
				} else if (gFields[mi].kind == ftSocial) {
					if (EditSocialDialog(gFields[mi].value, a35EditValLen,
							&gFields[mi].sub)) {
						char sd[a35EditValLen];
						EditKindDisplay(mi, sd, sizeof(sd));
						EditSetRowValue(slot, sd);
						FldDrawField((FieldPtr) EditObj(fldEditRowBase + slot));
					}
					handled = true;
				} else if (gFields[mi].kind == ftAddress) {
					if (EditAddressDialog(gFields[mi].value, a35EditValLen)) {
						char ml[a35EditValLen];
						AddrMultiline(gFields[mi].value, ml, sizeof(ml));
						EditSetRowValue(slot, ml);
						FldDrawField((FieldPtr) EditObj(fldEditRowBase + slot));
					}
					handled = true;
				} else if (gFields[mi].kind == ftNote) {
					if (EditNoteDialog(&gFields[mi].big)) {
						const char *nt = "";
						if (gFields[mi].big)
							nt = (const char *) MemHandleLock(gFields[mi].big);
						NotePreview(nt, kNoteValW, kNoteEditLines,
							gFields[mi].value, a35EditValLen);
						if (gFields[mi].big)
							MemHandleUnlock(gFields[mi].big);
						EditSetRowValue(slot, gFields[mi].value);
						FldDrawField((FieldPtr) EditObj(fldEditRowBase + slot));
					}
					handled = true;
				} else if (EditIsPhoneKind(gFields[mi].kind)) {
					if (EditPhoneDialog(gFields[mi].value, a35EditValLen,
							&gFields[mi].sub)) {
						char ds[a35EditValLen];
						EditPhoneDisplay(mi, ds, sizeof(ds));
						EditSetRowValue(slot, ds);
						FldDrawField((FieldPtr) EditObj(fldEditRowBase + slot));
					}
					handled = true;
				} else if (gFields[mi].kind == ftBirthday ||
						gFields[mi].kind == ftAnniversary) {
					Boolean anniv =
						(Boolean) (gFields[mi].kind == ftAnniversary);
					if (EditDateDialog(gFields[mi].value, a35EditValLen, anniv,
							&gFields[mi].sub,
							anniv ? "Anniversary" : "Birthday")) {
						char ds[a35EditValLen];
						EditKindDisplay(mi, ds, sizeof(ds));
						EditSetRowValue(slot, ds);
						FldDrawField((FieldPtr) EditObj(fldEditRowBase + slot));
					}
					handled = true;
				} else if (gFields[mi].kind == ftRelation) {
					UInt32 self = 0;	/* exclude this record from the picker */
					if (gEditIndex != a35NoRecord)
						DmRecordInfo(gAddrDB, gEditIndex, NULL, &self, NULL);
					if (EditRelationDialog(gFields[mi].value, a35EditValLen,
							&gFields[mi].sub, self)) {
						char ds[a35EditValLen];
						EditKindDisplay(mi, ds, sizeof(ds));
						EditSetRowValue(slot, ds);
						FldDrawField((FieldPtr) EditObj(fldEditRowBase + slot));
					}
					handled = true;
				}
			}
		}
		break;
	}

	case ctlSelectEvent: {
		UInt16 cid = eventP->data.ctlSelect.controlID;

		if (cid >= selEditRowBase && cid < selEditRowBase + kEditRows) {
			EditTypePick((UInt16) (cid - selEditRowBase));
			handled = true;
			break;
		}
		switch (cid) {
		case btnEditDone:
			EditSave();
			gEditSaved = true;			/* frmClose must not save twice */
			FrmGotoForm(gEditIndex != a35NoRecord ? frmRecord : frmMain);
			handled = true;
			break;
		case btnEditDelete:
			EditDelete();
			handled = true;
			break;
		case btnEditAddField:
			EditAddNewField();
			handled = true;
			break;
		case btnEditDelField:
			EditRemoveFieldsDialog();
			handled = true;
			break;
		case popEditCategory:
			EditSelectCategory();
			handled = true;
			break;
		}
		break;
	}

	case menuEvent:
		switch (eventP->data.menu.itemID) {
		case mniEditDelete:
			EditDelete();
			handled = true;
			break;
		case mniEditBeam:
			EditSave();				/* persist current edits, then beam */
			if (gEditIndex != a35NoRecord)
				A35BeamRecord(gAddrDB, gEditIndex);
			handled = true;
			break;
		}
		break;

	case frmCloseEvent:
		if (!gEditDeleted && !gEditSaved)
			EditSave();
		if (gFields != NULL) {		/* release the heap-allocated field array */
			UInt16 fi;
			for (fi = 0; fi < gFieldCount; fi++)	/* free note `big` handles */
				if (gFields[fi].big != NULL)
					MemHandleFree(gFields[fi].big);
			MemPtrFree(gFields);
			gFields = NULL;
		}
		break;

	default:
		break;
	}
	return handled;
}
