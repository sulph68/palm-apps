/*
 * RecordForm.c - Address35 read-only detail (record) view.
 *
 * Tapping a contact in the list opens this page: the name (bold) + org lines,
 * then every non-empty field as "Label: value", section-ordered as stored.
 * "Edit" opens frmEdit; "Done" returns to the list. It's a custom-drawn table
 * windowed over a precomputed display-line array, with a scrollbar for overflow.
 */
#include <PalmOS.h>
#include "Main.h"
#include "RecField.h"

#define kRecCol			0

#define recFieldMax		256		/* raw field-value copy before splitting; big enough
									 * for a full international address (was recValLen,
									 * which clipped long addresses' zip+country) */
#define recNoteMaxLines	20		/* note preview: show up to N lines, then "(more...)" */
#define recNoteWrapPx	100		/* approx value-column width used to wrap note lines */


static RecLine	gRecLines[a35MaxFields + recNoteMaxLines + 6 + 24];	/* +24: wrapped continuation lines */
static Int16	gRecField = -1;		/* record field currently being emitted */
static UInt16	gRecCount;
static UInt16	gRecTop;
static Int16	gRecColonX;	/* colon column = widest label + a small gap */
static Int16	gRecValueX;

/* This whole (foreground-only) view lives in sec001 to keep main under the 32KB
 * cap. Every function is SEC001 so multigen resolves cross-segment calls AND the
 * function-pointer callbacks (RecDrawLine, RecordFormHandleEvent) correctly. */
static void *RecObj(UInt16 id) SEC001;
static TableType *RecTable(void) SEC001;
static const char *RecPhoneLabel(UInt8 l) SEC001;
static const char *RecSocialLabel(UInt8 l) SEC001;
static const char *RecEmailLabel(UInt8 l) SEC001;
static void RecFieldLabel(UInt8 type, UInt8 label, char *out) SEC001;
static void RecAddLine(UInt8 kind, const char *lab, const char *val) SEC001;
static void RecAddrField(const char *src, UInt16 idx, char *out, UInt16 outLen) SEC001;
static void RecEmitNote(const char *note) SEC001;
static void RecBuild(void) SEC001;
static void RecDrawLine(void *table, Int16 row, Int16 column, RectangleType *bounds) SEC001;
static void RecLoad(void) SEC001;
static void RecUpdateScroll(void) SEC001;
static void RecRedraw(void) SEC001;
static void RecInit(void) SEC001;
static void RecSetCategoryLabel(void) SEC001;
static void RecSelectCategory(void) SEC001;

/* ---- helpers ----------------------------------------------------------- */
static void *RecObj(UInt16 id)
{
	FormPtr f = FrmGetActiveForm();
	return FrmGetObjectPtr(f, FrmGetObjectIndex(f, id));
}
static TableType *RecTable(void)
{
	return (TableType *) RecObj(tblRecord);
}

static const char *RecPhoneLabel(UInt8 l)
{
	switch (l) {
	case a35plWork:	return "Work";
	case a35plHome:	return "Home";
	case a35plFax:	return "Fax";
	case a35plMain:	return "Main";
	case a35plPager:return "Pager";
	case a35plOther:return "Other";
	default:		return "Mobile";
	}
}
static const char *RecSocialLabel(UInt8 l)
{
	switch (l) {
	case a35socInstagram:	return "Instagram";
	case a35socFacebook:	return "Facebook";
	case a35socWhatsApp:	return "WhatsApp";
	case a35socX:			return "X";
	case a35socTelegram:	return "Telegram";
	case a35socSignal:		return "Signal";
	default:				return "Social";
	}
}
static const char *RecEmailLabel(UInt8 l)
{
	switch (l) {
	case a35elWork:		return "Work";
	case a35elICloud:	return "iCloud";
	case a35elOther:	return "Other";
	default:			return "Home";		/* a35elHome */
	}
}
static void RecFieldLabel(UInt8 type, UInt8 label, char *out)
{
	const char *s;

	if (type == a35fEmail) {			/* "Email (Home)" etc - unlike Phone, the
						 * bare label alone doesn't say it's an email */
		StrCopy(out, "Email (");
		StrCat(out, RecEmailLabel(label));
		StrCat(out, ")");
		return;
	}
	switch (type) {
	case a35fPhone:		s = RecPhoneLabel(label); break;
	case a35fSocial:
	case a35fIM:		s = RecSocialLabel(label); break;
	case a35fURL:		s = "URL"; break;
	case a35fAddress:	s = "Address"; break;
	case a35fCity:		s = "City"; break;
	case a35fState:		s = "State"; break;
	case a35fZip:		s = "Zip Code"; break;
	case a35fCountry:	s = "Country"; break;
	case a35fNickname:	s = "Nickname"; break;
	case a35fPronouns:	s = "Pronouns"; break;
	case a35fBirthday:	s = "Birthday"; break;
	case a35fAnniversary:	s = "Anniversary"; break;
	case a35fGender:	s = "Gender"; break;
	case a35fNote:		s = "Note"; break;
	default:			s = "Other"; break;
	}
	StrCopy(out, s);
}

/* ---- display-line list ------------------------------------------------- */
static void RecAddLine(UInt8 kind, const char *lab, const char *val)
{
	RecLine *L;

	if (gRecCount >= (UInt16) (sizeof(gRecLines) / sizeof(gRecLines[0])))
		return;
	L = &gRecLines[gRecCount++];
	L->kind = kind;
	L->field = gRecField;
	if (lab) { StrNCopy(L->label, lab, sizeof(L->label) - 1); L->label[sizeof(L->label) - 1] = '\0'; }
	else L->label[0] = '\0';
	if (val) { StrNCopy(L->value, val, recValLen - 1); L->value[recValLen - 1] = '\0'; }
	else L->value[0] = '\0';
}

/* Emit a note as up to recNoteMaxLines wrapped display lines (first line labelled
 * "Note", the rest continuations), honouring explicit newlines; append "(more...)"
 * if the note is longer.  Reads the full field value, not the 256-byte copy. */
static void RecEmitNote(const char *note)
{
	FontID of = FntSetFont(stdFont);
	const char *p = note ? note : "";
	UInt16 lineNo = 0;
	Boolean firstLine = true;

	while (*p && lineNo < recNoteMaxLines) {
		const char *nl = StrChr(p, '\n');
		UInt16 avail, runLen, o = 0;
		Boolean lead;
		Int16 tw, fit, k;
		char ln[recValLen];

		if (nl) avail = (UInt16) (nl - p);
		else avail = (UInt16) StrLen(p);

		fit = FntWidthToOffset(p, avail, recNoteWrapPx, &lead, &tw);
		if (fit >= (Int16) avail) runLen = avail;
		else if (fit > 0) runLen = (UInt16) fit;
		else runLen = 1;

		for (k = 0; (UInt16) k < runLen && o < (UInt16) (sizeof(ln) - 1); k++)
			ln[o++] = p[k];
		ln[o] = '\0';
		if (firstLine) RecAddLine(recFieldLine, "Note", ln);
		else RecAddLine(recContLine, NULL, ln);
		firstLine = false;
		lineNo++;
		p += runLen;
		if (runLen == avail && nl)	/* consumed the run including its newline */
			p++;
	}
	if (*p)						/* text remains beyond the previewed lines */
		RecAddLine(recContLine, NULL, "(more...)");
	FntSetFont(of);
}

/* copy TAB-delimited field #idx of src into out */
static void RecAddrField(const char *src, UInt16 idx, char *out, UInt16 outLen)
{
	const char *p = src ? src : "";
	UInt16 field = 0, o = 0;

	out[0] = '\0';
	while (*p && field < idx) { if (*p == '\t') field++; p++; }
	if (field < idx) return;
	while (*p && *p != '\t' && o < (UInt16) (outLen - 1))
		out[o++] = *p++;
	out[o] = '\0';
}

static void RecBuild(void)
{
	A35Record rec;
	MemHandle h;
	UInt16 i;
	const char *prefix, *first, *middle, *last, *company, *title, *suffix;
	char name[130];

	gRecCount = 0;
	gRecTop = 0;
	gRecField = -1;			/* header lines below belong to no editable field */
	if (gViewingCard) {
		rec = gCardRecord;	/* read-only card vCard; values live in gCardArena */
		h = NULL;
	} else if (gEditIndex == a35NoRecord ||
		A35GetRecord(gAddrDB, gEditIndex, &rec, &h) != errNone) {
		return;
	}

	prefix = A35GetStr(&rec, a35fPrefix);
	first = A35GetStr(&rec, a35fFirst);
	middle = A35GetStr(&rec, a35fMiddle);
	last = A35GetStr(&rec, a35fLast);
	company = A35GetStr(&rec, a35fCompany);
	title = A35GetStr(&rec, a35fTitle);
	suffix = A35GetStr(&rec, a35fSuffix);

	/* Bounded (StrNCat, size=sizeof(name) total-dst-size per its strlcat-style
	 * semantics - see [[strncat-strlcat-semantics-gotcha]]), NOT plain StrCat:
	 * each of prefix/first/middle/last/suffix can be up to 63 bytes (vcard.c's
	 * per-N-component cap), so all 5 combined can reach ~320 bytes - a real
	 * stack overflow into this 130-byte buffer for an imported vCard with
	 * long name components (typed-in DB contacts stay short in practice, but
	 * imported ones aren't bounded by any UI field-length limit). Confirmed
	 * as the cause of a real-device "fatal exception" that only surfaced
	 * later (e.g. on app close) - a stack smash corrupts a return address
	 * that isn't used until some later, unrelated call returns. */
	name[0] = '\0';
	if (prefix[0]) StrNCat(name, prefix, sizeof(name));		/* "Mr. First Last, Jr." */
	if (first[0]) { if (name[0]) StrNCat(name, " ", sizeof(name)); StrNCat(name, first, sizeof(name)); }
	if (middle[0]) { if (name[0]) StrNCat(name, " ", sizeof(name)); StrNCat(name, middle, sizeof(name)); }
	if (last[0]) { if (name[0]) StrNCat(name, " ", sizeof(name)); StrNCat(name, last, sizeof(name)); }
	if (suffix[0]) { if (name[0]) StrNCat(name, ", ", sizeof(name)); StrNCat(name, suffix, sizeof(name)); }

	if (name[0]) RecAddLine(recHeaderLine, NULL, name);
	else if (company[0]) RecAddLine(recHeaderLine, NULL, company);
	else RecAddLine(recHeaderLine, NULL, "-Unnamed-");

	if (name[0] && company[0]) RecAddLine(recSubLine, NULL, company);
	if (title[0]) RecAddLine(recSubLine, NULL, title);

	for (i = 0; i < rec.fieldCount; i++) {
		UInt8 t = rec.fields[i].type;
		char lab[24], val[recFieldMax];
		UInt16 n;

		gRecField = (Int16) i;		/* lines emitted below map back to this field */

		if (t == a35fPrefix || t == a35fFirst || t == a35fMiddle ||
			t == a35fLast || t == a35fSuffix || t == a35fCompany ||
			t == a35fTitle || t == a35fTag || t == a35fNone)
			continue;			/* prefix/name/suffix/company/title are in the header */
		if (t == a35fRelation) {		/* "Spouse: Jane Doe" */
			A35Relation rel;
			UInt16 tgt;
			char nm[48];
			if (A35UnpackRelation(rec.fields[i].value, rec.fields[i].len, &rel) &&
				A35FindByUniqueID(gAddrDB, rel.targetUniqueID, &tgt)) {
				A35RecordDisplayName(gAddrDB, tgt, nm, sizeof(nm));
				RecAddLine(recFieldLine, EditRelTypeName(rel.relType), nm);
			}
			continue;
		}
		if (rec.fields[i].len == 0)
			continue;
		n = rec.fields[i].len;
		if (n > recFieldMax - 1) n = recFieldMax - 1;
		MemMove(val, rec.fields[i].value, n);
		val[n] = '\0';
		{
			UInt8 label = (UInt8) (rec.fields[i].label & a35LabelMask);

			/* social "Other": value is "Platform: handle" - split so the custom
			 * platform name becomes the label. */
			if (t == a35fSocial && label == a35socOther) {
				char *sep = StrStr(val, ": ");
				if (sep) {
					*sep = '\0';
					RecAddLine(recFieldLine, val, sep + 2);
					continue;
				}
			}
			/* address: TAB-delimited Line1/Line2/Apt/City/State/Zip/Country ->
			 * a multi-line block (each part trimmed, empties skipped). */
			if (t == a35fAddress) {
				/* heap, not stack: 7 parts of up to 129 characters + the city/state/zip line */
				char (*pt)[recValLen] = (char (*)[recValLen]) MemPtrNew(7 * recValLen + 400);
				char *line;
				UInt16 k;

				if (pt == NULL) continue;
				line = (char *) pt + 7 * recValLen;

				for (k = 0; k < 7; k++) {
					Int16 e, st = 0;
					RecAddrField(val, k, pt[k], recValLen);
					e = (Int16) StrLen(pt[k]);
					while (e > 0 && pt[k][e - 1] == ' ') pt[k][--e] = '\0';
					while (pt[k][st] == ' ') st++;
					if (st) StrCopy(pt[k], pt[k] + st);
				}
				RecAddLine(recFieldLine, "Address", pt[0]);
				if (pt[1][0]) RecAddLine(recContLine, NULL, pt[1]);
				if (pt[2][0]) {
					/* bounded (StrNCat, total-dst-size) - pt[] parts can each
					 * be up to 71 bytes, so an unbounded StrCat here could
					 * overflow this 160-byte buffer; see the identical fix
					 * + rationale on RecBuild's name[] just above. */
					StrCopy(line, "Apt ");
					StrNCat(line, pt[2], 400);
					RecAddLine(recContLine, NULL, line);
				}
				line[0] = '\0';
				if (pt[3][0]) StrNCat(line, pt[3], 400);
				if (pt[4][0]) { if (line[0]) StrNCat(line, ", ", 400); StrNCat(line, pt[4], 400); }
				if (pt[5][0]) { if (line[0]) StrNCat(line, " ", 400); StrNCat(line, pt[5], 400); }
				if (line[0]) RecAddLine(recContLine, NULL, line);
				if (pt[6][0]) RecAddLine(recContLine, NULL, pt[6]);
				MemPtrFree(pt);
				continue;
			}
			/* birthday/anniversary: "YYYYMMDD" -> "Jun 15, 1990"; the
			 * anniversary Type is shown as the row label. */
			if (t == a35fBirthday || t == a35fAnniversary) {
				char disp[24];
				A35BirthdayDisplay(val, disp);
				if (t == a35fAnniversary) {
					/* "Other" -> the custom name after the date */
					const char *tl = (label == a35AnnivOther &&
						StrLen(val) > a35BirthdayStrLen)
						? val + a35BirthdayStrLen
						: EditAnnivTypeName(label);
					RecAddLine(recFieldLine, tl, disp);
				} else
					RecAddLine(recFieldLine, "Birthday", disp);
				continue;
			}
			if (t == a35fNote) {	/* multi-line preview from the FULL value */
				RecEmitNote((const char *) rec.fields[i].value);
				continue;
			}
			RecFieldLabel(t, label, lab);
			RecAddLine(recFieldLine, lab, val);
		}
	}
	if (h) A35ReleaseRecord(h);		/* card mode has no locked DB record */

	/* colon sits just right of the widest label; values get the rest */
	{
		Int16 maxw = 0;
		UInt16 i;
		FontID o = FntSetFont(stdFont);

		for (i = 0; i < gRecCount; i++)
			if (gRecLines[i].kind == recFieldLine) {
				Int16 w = FntCharsWidth(gRecLines[i].label,
					StrLen(gRecLines[i].label));
				if (w > maxw) maxw = w;
			}
		FntSetFont(o);
		gRecColonX = (Int16) (maxw + 3);
		gRecValueX = (Int16) (gRecColonX + 5);
	}

	/* wrap a too-long email: "user" on its line, "@domain" on a continuation */
	{
		Int16 vw = (Int16) (148 - gRecValueX);
		UInt16 cap = (UInt16) (sizeof(gRecLines) / sizeof(gRecLines[0]));
		UInt16 i;
		FontID o = FntSetFont(stdFont);

		for (i = 0; i < gRecCount && gRecCount < cap; i++) {
			char *at;
			if (gRecLines[i].kind != recFieldLine)
				continue;
			at = StrChr(gRecLines[i].value, '@');
			if (at && at > gRecLines[i].value &&
				FntCharsWidth(gRecLines[i].value,
					StrLen(gRecLines[i].value)) > vw) {
				MemMove(&gRecLines[i + 2], &gRecLines[i + 1],
					(gRecCount - (i + 1)) * sizeof(RecLine));
				gRecLines[i + 1].kind = recContLine;
				gRecLines[i + 1].label[0] = '\0';
				StrCopy(gRecLines[i + 1].value, at);	/* "@domain" */
				*at = '\0';								/* this line -> "user" */
				gRecCount++;
				i++;
			}
		}
		FntSetFont(o);
	}

	/* Wrap every line that is wider than its column onto continuation lines, so nothing is
	 * clipped with "..." (the bold name too): a continuation keeps the kind of a header /
	 * sub line, a field value goes on at the value column. Same in Local and CardDAV views. */
	{
		UInt16 cap = (UInt16) (sizeof(gRecLines) / sizeof(gRecLines[0]));
		UInt16 i;

		for (i = 0; i < gRecCount && gRecCount < cap; i++) {
			RecLine *L = &gRecLines[i];
			Boolean wide = (Boolean) (L->kind == recHeaderLine || L->kind == recSubLine);
			Int16 avail = (Int16) (148 - (wide ? 0 : gRecValueX));
			UInt16 len = StrLen(L->value), n;
			FontID o = FntSetFont(L->kind == recHeaderLine ? boldFont : stdFont);

			n = (avail > 8) ? FntWordWrap(L->value, (UInt16) avail) : 0;
			FntSetFont(o);
			if (n == 0 || n >= len)
				continue;
			{
				RecLine *N;
				const char *rest = L->value + n;
				Int16 e;

				MemMove(&gRecLines[i + 2], &gRecLines[i + 1],
					(gRecCount - (i + 1)) * sizeof(RecLine));
				N = &gRecLines[i + 1];
				N->kind = wide ? L->kind : recContLine;
				N->field = L->field;
				N->label[0] = '\0';
				while (*rest == ' ') rest++;
				StrCopy(N->value, rest);
				L->value[n] = '\0';
				e = (Int16) StrLen(L->value);
				while (e > 0 && L->value[e - 1] == ' ') L->value[--e] = '\0';
				gRecCount++;
			}
		}
	}
}

/* ---- custom row draw --------------------------------------------------- */
static void RecDrawLine(void *table, Int16 row, Int16 column,
	RectangleType *bounds)
{
	UInt16 i = (UInt16) (gRecTop + row);
	Int16 x = (Int16) (bounds->topLeft.x + 2);
	Int16 y = bounds->topLeft.y;
	Int16 right = (Int16) (bounds->topLeft.x + bounds->extent.x - 2);
	RecLine *L;

	(void) table;
	(void) column;
	if (i >= gRecCount)
		return;
	L = &gRecLines[i];

	if (L->kind == recHeaderLine) {
		FntSetFont(boldFont);
		WinDrawTruncChars(L->value, StrLen(L->value), x, y, (Int16) (right - x));
		FntSetFont(stdFont);
	} else if (L->kind == recSubLine) {
		FntSetFont(stdFont);
		WinDrawTruncChars(L->value, StrLen(L->value), x, y, (Int16) (right - x));
	} else if (L->kind == recContLine) {			/* address continuation */
		Int16 vx = (Int16) (x + gRecValueX);
		FntSetFont(stdFont);
		if (vx < right)
			WinDrawTruncChars(L->value, StrLen(L->value), vx, y,
				(Int16) (right - vx));
	} else {						/* recFieldLine: label : value */
		Int16 cx = (Int16) (x + gRecColonX);
		Int16 vx = (Int16) (x + gRecValueX);

		FntSetFont(stdFont);
		WinDrawChars(L->label, StrLen(L->label), x, y);
		WinDrawChars(":", 1, cx, y);
		if (vx < right)
			WinDrawTruncChars(L->value, StrLen(L->value), vx, y,
				(Int16) (right - vx));
	}
}

/* ---- table load / scroll ----------------------------------------------- */
static void RecLoad(void)
{
	TableType *t = RecTable();
	UInt16 rows = TblGetNumberOfRows(t), r, lh;
	FontID o;

	o = FntSetFont(stdFont);
	lh = FntLineHeight();
	FntSetFont(o);

	for (r = 0; r < rows; r++) {
		UInt16 i = (UInt16) (gRecTop + r);

		if (i < gRecCount) {
			TblSetItemStyle(t, r, kRecCol, customTableItem);
			TblSetRowHeight(t, r, lh);
			TblSetRowID(t, r, i);
			TblSetRowUsable(t, r, true);
			TblMarkRowInvalid(t, r);
		} else {
			TblSetRowUsable(t, r, false);
		}
	}
	TblSetColumnUsable(t, kRecCol, true);
}

static void RecUpdateScroll(void)
{
	ScrollBarType *sb = (ScrollBarType *) RecObj(sclRecord);
	Int16 rows = (Int16) TblGetNumberOfRows(RecTable());
	Int16 max = (Int16) ((gRecCount > (UInt16) rows) ? gRecCount - rows : 0);
	Int16 v = (Int16) gRecTop;

	if (v > max)
		v = max;
	SclSetScrollBar(sb, v, 0, max, rows);
}

static void RecRedraw(void)
{
	RecLoad();
	RecUpdateScroll();
	TblRedrawTable(RecTable());
}

/* ---- category (change without entering Edit) ---------------------------- */
/* CategorySetTriggerLabel keeps a POINTER to this string, not a copy (same
 * gotcha CtlSetLabel has) -- must be persistent storage, not a stack local,
 * or the label goes blank once FrmDrawForm actually paints it. Matches
 * MainForm's gCategoryName / EditForm's gEditCatName. */
static char gRecCatName[dmCategoryLength];

static void RecSetCategoryLabel(void)
{
	ControlType *ctl = (ControlType *) RecObj(popRecordCategory);
	UInt16 attr = 0;

	if (gEditIndex == a35NoRecord || ctl == NULL)
		return;
	DmRecordInfo(gAddrDB, gEditIndex, &attr, NULL, NULL);
	CategoryGetName(gAddrDB, (UInt16) (attr & dmRecAttrCategoryMask), gRecCatName);
	CategorySetTriggerLabel(ctl, gRecCatName);
}

/* Built the same way as EditForm.c's EditSelectCategory (ROM CategorySelect
 * doesn't report the new category back reliably either), but writes straight
 * to the DB record's attr bits instead of a pending gEditCat -- there's no
 * Save step here, the change is immediate. The list itself shows no category
 * column, so nothing else needs to refresh. */
static void RecSelectCategory(void)
{
	FormPtr f = FrmGetActiveForm();
	ListPtr lst = (ListPtr) RecObj(lstRecordCategory);
	static char  names[20][24];		/* >dmCategoryLength: fits "Edit Categories..." */
	static char *choices[20];
	Int16   meaning[20];			/* >=0 category index; -101 Edit Categories */
	Int16   n = 0, i, sel, m, cur = -1;
	UInt16  attr = 0, cat;

	if (gEditIndex == a35NoRecord)
		return;
	DmRecordInfo(gAddrDB, gEditIndex, &attr, NULL, NULL);
	cat = (UInt16) (attr & dmRecAttrCategoryMask);

	for (i = 1; i < dmRecNumCategories && n < 16; i++) {
		CategoryGetName(gAddrDB, i, names[n]);
		if (names[n][0]) {
			choices[n] = names[n]; meaning[n] = i;
			if ((UInt16) i == cat) cur = n;
			n++;
		}
	}
	CategoryGetName(gAddrDB, dmUnfiledCategory, names[n]);
	if (names[n][0]) {
		choices[n] = names[n]; meaning[n] = dmUnfiledCategory;
		if (cat == dmUnfiledCategory) cur = n;
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
		if (!CategorySelect(gAddrDB, f, popRecordCategory, lstRecordCategory, false,
				&cat, name, 1, 0)) {
			RecSetCategoryLabel();		/* cancelled: leave unchanged */
			return;
		}
	} else {
		cat = (UInt16) m;
	}

	DmRecordInfo(gAddrDB, gEditIndex, &attr, NULL, NULL);
	attr = (UInt16) ((attr & ~dmRecAttrCategoryMask) | cat | dmRecAttrDirty);
	DmSetRecordInfo(gAddrDB, gEditIndex, &attr, NULL);
	RecSetCategoryLabel();
}

static void RecInit(void)
{
	FormPtr f = FrmGetActiveForm();
	TableType *t = RecTable();

	gEditFocusOrigin = -1;		/* fresh view: no field targeted for the editor */
	RecFieldReset();
	TblSetCustomDrawProcedure(t, kRecCol, RecDrawLine);
	TblSetColumnUsable(t, kRecCol, true);
	RecBuild();
	/* Configure the table rows BEFORE FrmDrawForm paints them. Drawing the
	 * form first would render the custom-draw cells while their rows still
	 * hold stale/default state, flashing a column of missing-glyph boxes
	 * until RecRedraw repainted the real text. */
	RecLoad();
	RecUpdateScroll();
	/* card view is read-only: "Edit" becomes a wider "Import" button; the
	 * category popup only applies to a real DB record, hide it for a card. */
	{
		UInt16 idx = FrmGetObjectIndex(f, btnRecordEdit);
		UInt16 catIdx = FrmGetObjectIndex(f, popRecordCategory);
		RectangleType r;
		FrmGetObjectBounds(f, idx, &r);
		r.extent.x = (Int16) (gViewingCard ? 44 : 28);
		FrmSetObjectBounds(f, idx, &r);
		CtlSetLabel((ControlPtr) RecObj(btnRecordEdit),
			gViewingCard ? "Import" : "Edit");
		FrmSetTitle(f, (char *) (gViewingCard ? (gDavTitle[0] ? gDavTitle : "Card Contact") : "Address35"));
		if (gViewingCard) {
			FrmHideObject(f, catIdx);
		} else {
			FrmShowObject(f, catIdx);
			RecSetCategoryLabel();
		}
	}
	FrmDrawForm(f);
}

Boolean RecordFormHandleEvent(EventPtr eventP)
{
	Boolean handled = false;

	switch (eventP->eType) {
	case frmOpenEvent:
		RecInit();
		handled = true;
		break;

	case tblSelectEvent:
		/* remember which field was tapped so "Edit" can jump straight to it */
		if (eventP->data.tblSelect.tableID == tblRecord) {
			UInt16 line = (UInt16) (gRecTop + eventP->data.tblSelect.row);
			if (line < gRecCount) {
				gEditFocusOrigin = gRecLines[line].field;
				RecFieldShow(gRecLines, line);	/* the whole field, read-only, with Copy */
			}
			handled = true;
		}
		break;

	case ctlSelectEvent:
		switch (eventP->data.ctlSelect.controlID) {
		case btnRecordDone:
			gViewingCard = false;			/* leave card-browse if we were in it */
			FrmGotoForm(frmMain);
			handled = true;
			break;
		case btnRecordEdit:
			if (gViewingCard) {				/* "Import" -> add card contact to DB */
				UInt16 idx;
				if (A35NewRecord(gAddrDB, &gCardRecord, &idx) == errNone)
					A35EncryptRecordInPlace(gAddrDB, idx);	/* seal (foreground) */
				gViewingCard = false;
				gEditIndex = a35NoRecord;
				FrmGotoForm(frmMain);
			} else {
				FrmGotoForm(frmEdit);
			}
			handled = true;
			break;
		case popRecordCategory:
			if (!gViewingCard) RecSelectCategory();
			handled = true;
			break;
		}
		break;

	case menuEvent:
		switch (eventP->data.menu.itemID) {
		case mniRecordSetMe:
			RecSetMe();
			handled = true;
			break;
		case mniRecordEdit:
			if (!gViewingCard) FrmGotoForm(frmEdit);
			handled = true;
			break;
		case mniRecordDuplicate:
			/* copy this contact and open the copy for editing (like the ROM). */
			if (!gViewingCard && gEditIndex != a35NoRecord) {
				UInt16 newIdx = a35NoRecord;
				if (A35DuplicateRecord(gAddrDB, gEditIndex, &newIdx) == errNone) {
					gEditIndex = newIdx;
					FrmGotoForm(frmEdit);
				}
			}
			handled = true;
			break;
		case mniRecordDelete:
			if (!gViewingCard && gEditIndex != a35NoRecord) {
				A35ForgetMeIfDeleted(gEditIndex);
				DmRemoveRecord(gAddrDB, gEditIndex);
				gEditIndex = a35NoRecord;
				FrmGotoForm(frmMain);
			}
			handled = true;
			break;
		case mniRecordBeam:
			if (!gViewingCard && gEditIndex != a35NoRecord)
				A35BeamRecord(gAddrDB, gEditIndex);
			handled = true;
			break;
		case mniRecordCopy:
			RecFieldCopy();
			handled = true;
			break;
		case mniRecordCopyAll:
			RecContactCopy(gRecLines, gRecCount);
			handled = true;
			break;
		}
		break;

	case keyDownEvent:
		/* Hardware up/down scroll buttons page the record detail up/down. */
		if (eventP->data.keyDown.chr == vchrPageUp ||
			eventP->data.keyDown.chr == vchrPageDown) {
			Int16 rows = (Int16) TblGetNumberOfRows(RecTable());
			Int16 max, v;
			if (rows < 1) rows = 1;
			max = (Int16) ((gRecCount > (UInt16) rows) ? gRecCount - rows : 0);
			v = (Int16) gRecTop +
				((eventP->data.keyDown.chr == vchrPageDown) ? rows : (Int16) -rows);
			if (v > max) v = max;
			if (v < 0) v = 0;
			if ((UInt16) v != gRecTop) {
				gRecTop = (UInt16) v;
				RecRedraw();
			}
			handled = true;
		}
		break;

	case sclRepeatEvent:
		if (eventP->data.sclRepeat.scrollBarID == sclRecord) {
			gRecTop = (UInt16) eventP->data.sclRepeat.newValue;
			RecLoad();
			RecUpdateScroll();
			TblRedrawTable(RecTable());
		}
		break;

	default:
		break;
	}
	return handled;
}
