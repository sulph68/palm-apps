/*
 * RecField.c - see RecField.h. Everything here is SEC002 (foreground only).
 *
 * The text of a field is rebuilt from the record itself (a Local record, or the parsed vCard in
 * gCardRecord for a CardDAV / card contact), not taken from the wrapped display lines, so it is
 * complete: up to rfMax bytes. An address is shown one part per line, a birthday as a date, a
 * relation as "Type: Name".
 */
#include <PalmOS.h>
#include "Main.h"
#include "AddressRsc.h"
#include "AddressDB35.h"
#include "RecField.h"
#include "DavView.h"

#define rfMax		3500		/* characters of one field shown / copied */

static Int16 gRfLast = -1;		/* last tapped field: -1 = the name block, -2 = none yet */

static char *RfBuild(Int16 field, UInt16 *len) SEC002;
static void RfCat(char *buf, const char *s) SEC002;
static void RfUpdateScroll(FieldPtr fld, ScrollBarPtr scl) SEC002;
static void RfScroll(FieldPtr fld, ScrollBarPtr scl, Int16 lines) SEC002;

static void RfCat(char *buf, const char *s)
{
	StrNCat(buf, s, rfMax + 1);		/* total destination size (strlcat-style) */
}

/* the field's complete text in a new heap block (caller frees); NULL if unavailable */
static char *RfBuild(Int16 field, UInt16 *len)
{
	A35Record rec;
	MemHandle h = NULL;
	char *buf;

	if (gViewingCard) {
		rec = gCardRecord;
	} else if (gEditIndex == a35NoRecord ||
		A35GetRecord(gAddrDB, gEditIndex, &rec, &h) != errNone) {
		return NULL;
	}
	buf = (char *) MemPtrNew(rfMax + 1);
	if (buf == NULL) {
		if (h) A35ReleaseRecord(h);
		return NULL;
	}
	buf[0] = '\0';

	if (field < 0 || field >= (Int16) rec.fieldCount) {		/* the name block */
		const char *p[3];
		UInt16 i;
		p[0] = A35GetStr(&rec, a35fPrefix);
		p[1] = A35GetStr(&rec, a35fFirst);
		p[2] = A35GetStr(&rec, a35fMiddle);
		for (i = 0; i < 3; i++)
			if (p[i][0]) { if (buf[0]) RfCat(buf, " "); RfCat(buf, p[i]); }
		if (A35GetStr(&rec, a35fLast)[0]) { if (buf[0]) RfCat(buf, " "); RfCat(buf, A35GetStr(&rec, a35fLast)); }
		if (A35GetStr(&rec, a35fSuffix)[0]) { if (buf[0]) RfCat(buf, ", "); RfCat(buf, A35GetStr(&rec, a35fSuffix)); }
		if (A35GetStr(&rec, a35fCompany)[0]) { if (buf[0]) RfCat(buf, "\n"); RfCat(buf, A35GetStr(&rec, a35fCompany)); }
		if (A35GetStr(&rec, a35fTitle)[0]) { if (buf[0]) RfCat(buf, "\n"); RfCat(buf, A35GetStr(&rec, a35fTitle)); }
	} else {
		const A35Field *f = &rec.fields[field];
		UInt16 n = f->len, o = 0, k;

		if (f->type == a35fRelation) {
			A35Relation rel;
			UInt16 tgt;
			char nm[48];
			if (!gViewingCard && A35UnpackRelation(f->value, f->len, &rel) &&
				A35FindByUniqueID(gAddrDB, rel.targetUniqueID, &tgt)) {
				A35RecordDisplayName(gAddrDB, tgt, nm, sizeof(nm));
				RfCat(buf, EditRelTypeName(rel.relType));
				RfCat(buf, ": ");
				RfCat(buf, nm);
			}
		} else if (f->type == a35fBirthday || f->type == a35fAnniversary) {
			char ymd[24], disp[24];
			if (n > sizeof(ymd) - 1) n = sizeof(ymd) - 1;
			MemMove(ymd, f->value, n);
			ymd[n] = '\0';
			A35BirthdayDisplay(ymd, disp);
			RfCat(buf, disp);
		} else {
			if (n > rfMax) n = rfMax;
			for (k = 0; k < n; k++) {
				char c = (char) f->value[k];
				if (f->type == a35fAddress && c == '\t') {		/* one address part per line */
					if (o > 0 && buf[o - 1] != '\n') buf[o++] = '\n';
					continue;
				}
				if (f->type == a35fAddress && c == ' ' && (o == 0 || buf[o - 1] == '\n')) continue;
				buf[o++] = c;
			}
			while (o > 0 && (buf[o - 1] == '\n' || buf[o - 1] == ' ')) o--;
			buf[o] = '\0';
		}
	}
	if (h) A35ReleaseRecord(h);
	*len = (UInt16) StrLen(buf);
	return buf;
}

static void RfUpdateScroll(FieldPtr fld, ScrollBarPtr scl)
{
	UInt16 pos, textH, fieldH, max;

	FldGetScrollValues(fld, &pos, &textH, &fieldH);
	max = (textH > fieldH) ? (UInt16) (textH - fieldH) : 0;
	SclSetScrollBar(scl, (Int16) pos, 0, (Int16) max, (Int16) (fieldH > 1 ? fieldH - 1 : 1));
}

static void RfScroll(FieldPtr fld, ScrollBarPtr scl, Int16 lines)
{
	if (lines == 0) return;
	FldScrollField(fld, (UInt16) (lines < 0 ? -lines : lines), lines < 0 ? winUp : winDown);
	RfUpdateScroll(fld, scl);
}

void RecContactCopy(const RecLine *lines, UInt16 count)
{
	UInt16 i, cap = 4000;
	char *b = (char *) MemPtrNew(cap + 1);

	if (b == NULL) return;
	b[0] = '\0';
	for (i = 0; i < count; i++) {
		const RecLine *L = &lines[i];
		if (b[0]) {
			/* a wrapped name or company line joins with a space, everything else starts a new line */
			Boolean join = (Boolean) (i > 0 && lines[i - 1].kind == L->kind &&
				(L->kind == recHeaderLine || L->kind == recSubLine));
			UInt16 pl = StrLen(lines[i - 1].value);
			Boolean hyphen = (Boolean) (join && pl > 0 && lines[i - 1].value[pl - 1] == '-');
			StrNCat(b, join ? (hyphen ? "" : " ") : "\n", cap + 1);
		}
		if (L->kind == recFieldLine) {
			StrNCat(b, L->label, cap + 1);
			StrNCat(b, ": ", cap + 1);
		}
		StrNCat(b, L->value, cap + 1);
	}
	if (b[0]) ClipboardAddItem(clipboardText, b, StrLen(b));
	MemPtrFree(b);
}

void RecSetMe(void)
{
	if (gViewingCard) {					/* a CardDAV contact: its book's Me; an SD-card contact has none */
		if (gDavTitle[0] && FrmAlert(altSetMe) == 0)		/* 0 = OK */
			DavMeSetViewed();
		return;
	}
	if (gEditIndex != a35NoRecord && FrmAlert(altSetMe) == 0) {
		UInt32 uid = 0;
		DmRecordInfo(gAddrDB, gEditIndex, NULL, &uid, NULL);	/* stored by unique id: survives sort/HotSync */
		gMeUniqueID = uid;
	}
}

void RecFieldReset(void)
{
	gRfLast = -2;
}

void RecFieldCopy(void)
{
	UInt16 len;
	char *t = RfBuild(gRfLast < -1 ? -1 : gRfLast, &len);

	if (t == NULL) return;
	if (len) ClipboardAddItem(clipboardText, t, len);
	MemPtrFree(t);
}

void RecFieldShow(const RecLine *lines, UInt16 line)
{
	Int16 field = lines[line].field;
	const char *label = NULL;
	UInt16 j = line;
	UInt16 len;
	char *t;
	FormPtr frm, prev;
	FieldPtr fld;
	ScrollBarPtr scl;
	Boolean done = false;

	/* the label is on the field's first line: walk up from a continuation line */
	if (field >= 0)
		for (;;) {
			if (lines[j].kind == recFieldLine) { label = lines[j].label; break; }
			if (j == 0) break;
			j--;
		}
	gRfLast = field;
	t = RfBuild(field, &len);
	if (t == NULL) return;
	prev = FrmGetActiveForm();
	frm = FrmInitForm(frmRecField);
	fld = (FieldPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, fldRecField));
	scl = (ScrollBarPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, sclRecField));
	FrmSetTitle(frm, (char *) (label && label[0] ? label : "Name"));
	FrmSetActiveForm(frm);
	FldSetTextPtr(fld, t);
	FrmDrawForm(frm);
	FldRecalculateField(fld, true);
	RfUpdateScroll(fld, scl);

	while (!done) {
		EventType ev;

		EvtGetEvent(&ev, evtWaitForever);
		if (ev.eType == appStopEvent) {
			EvtAddEventToQueue(&ev);
			break;
		}
		if (ev.eType == ctlSelectEvent) {
			if (ev.data.ctlSelect.controlID == btnRecFieldDone) { done = true; continue; }
			if (ev.data.ctlSelect.controlID == btnRecFieldCopy) {
				if (len) ClipboardAddItem(clipboardText, t, len);
				CtlSetLabel((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, btnRecFieldCopy)), "Copied");
				continue;
			}
		}
		if (ev.eType == menuEvent && ev.data.menu.itemID == mniRecFieldCopy) {
			if (len) ClipboardAddItem(clipboardText, t, len);
			continue;
		}
		if (ev.eType == sclRepeatEvent) {
			RfScroll(fld, scl, (Int16) (ev.data.sclRepeat.newValue - ev.data.sclRepeat.value));
			continue;
		}
		if (ev.eType == keyDownEvent && EvtKeydownIsVirtual(&ev) &&
			(ev.data.keyDown.chr == vchrPageUp || ev.data.keyDown.chr == vchrPageDown)) {
			Int16 page = (Int16) (FldGetVisibleLines(fld) - 1);
			RfScroll(fld, scl, ev.data.keyDown.chr == vchrPageUp ? (Int16) -page : page);
			continue;
		}
		if (SysHandleEvent(&ev)) continue;
		if (MenuHandleEvent(NULL, &ev, NULL)) continue;
		FrmDispatchEvent(&ev);
	}

	FldSetTextPtr(fld, NULL);
	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prev) FrmSetActiveForm(prev);
	MemPtrFree(t);
}
