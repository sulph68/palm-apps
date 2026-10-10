/*
 * RecordForm.c - read-only display of one decrypted vault item. Four
 * separate, purpose-built forms (frmRecordLogin/Note/Card/Identity), not one
 * shared Login-shaped layout with relabeled/hidden rows - each type shows
 * exactly its own fields. MainForm.c picks which form to open via
 * BwStoreGetItemType(); this file dispatches internally on
 * FrmGetFormId(FrmGetActiveForm()).
 *
 * Login/Card/Identity all share the SAME dynamic mechanism (AddFieldLine/
 * BuildLoginFields/BuildCardFields/BuildIdentityFields below): one row per
 * POPULATED field only (a LABEL can't conditionally disappear the way a
 * table row can just not exist, and Bitwarden's real Identity type has up
 * to 18 possible fields - a fixed FIELD per field would either be mostly
 * blank or need per-record layout math), each field's own bold "Label:" row
 * followed by its value on its own wrapped line(s) below (never truncated
 * to one line - explicit design instruction), spanning nearly the whole
 * form with one scrollbar. Notes is included as just another field in that
 * same list, always last, wrapped like everything else. Note is the one
 * exception - a single item has only one field worth showing, so it keeps
 * its own plain FIELD widget (PalmOS wraps/scrolls it for free) - but still
 * shares the same bottom Show/Hide+Done row as the other three for visual
 * consistency.
 */
#include "BitWarden.h"
#include "totp.h"

static BwItemFull gCurrentItem;
/* Shared by whichever single "sensitive" field the active form has
 * (Login's password / Card's number / Identity's SSN) - only one is ever
 * masked at a time since only one form is ever open. */
static char gMaskedField[bwMaxField];
static Boolean gShowSensitive = false;

static void *GetObjectPtr(UInt16 objID)
{
	return FrmGetObjectPtr(FrmGetActiveForm(), FrmGetObjectIndex(FrmGetActiveForm(), objID));
}

/* ---- Login/Card/Identity: dynamic field list, populated fields only - see
 * BitWardenRsc.h's own comment on why this exists instead of fixed
 * per-field FIELD widgets. Shared between all three forms; which table/
 * scrollbar object IDs to draw into is resolved per-call from whichever
 * form is actually active.
 *
 * Layout, per explicit design instruction - a field's own bold "Label:"
 * line, then its value on its OWN line(s) below (underlined, like a real
 * text field row - "ready for edit when we want to implement it"), wrapped
 * across as many lines as it needs rather than truncated to one. Notes
 * joins this same list as just another field (also wrapped, never
 * truncated/capped, always last) instead of a separate widget. Since every
 * row is exactly one line of text regardless of whether it's a label or a
 * value line, this is still a plain fixed-row-height table underneath -
 * scrolling/windowing (FieldsScroll etc. below) doesn't need to know or
 * care which kind a given row is.
 *
 * bwFieldLineMax has to cover the worst realistic case: Identity's own 18
 * fields (each at least a label + one value line) plus a note wrapped
 * across many lines (bwMaxField=512 chars max on the wire - see
 * PROTOCOL.md - divided by roughly bwValueWrapPx's own capacity) - kept as
 * tight as that allows (link error otherwise: "region datares is full",
 * this app's own static-data budget is real and tight - see BitWarden.h's
 * own comment on the Card/Identity union). text is smaller than
 * bwMaxExtField on purpose: each row only ever holds ONE wrapped line's
 * worth of text, never a whole field value. */
#define bwFieldLineMax		64
#define bwValueWrapPx		140		/* value lines get nearly the table's own full width now that the label isn't sharing the row */
#define bwFieldLineValueMax	48
/* Must match every frmRecordLogin/Card/Identity's own TABLE ROWS count in
 * BitWardenEn.rcp - NOT bwFieldLineMax. This bounds the table's own
 * allocated row-slot array (only ever needs to cover the visible window,
 * reused via scrolling - same windowed-rendering idea as MainForm.c's own
 * list), regardless of how many logical field-lines exist in gFieldLines. */
#define bwFieldTableRows	24
typedef struct {
	char text[bwFieldLineValueMax];
	Boolean isLabel;	/* bold "Field:" line vs an (optionally masked, always underlined) value line */
	Boolean sensitive;	/* only meaningful when !isLabel */
} BwFieldLine;

static BwFieldLine gFieldLines[bwFieldLineMax];
static UInt16 gFieldLineCount;
static UInt16 gFieldTop;

/* Live TOTP countdown (2026-08 user report: v1 only computed the code once
 * at open, no refresh) - index into gFieldLines[] of the TOTP value row
 * (its own single line - a 6-digit code + " (Ns)" suffix always fits
 * within bwValueWrapPx, never wraps), or -1 when frmRecordLogin's current
 * item has no TOTP secret. RecordFormHandleEvent's own nilEvent case
 * refreshes it once a second; BitWarden.c's EventLoop needs
 * RecordHasTotpActive() to know to give this form a bounded nilEvent
 * timeout at all (evtWaitForever otherwise, same as every other form). */
static Int16 gTotpValueLine = -1;

/* Word-wraps one field's value across as many rows as it needs - same
 * FntWidthToOffset-driven algorithm already proven in Address35's own
 * RecEmitNote, just no longer capped to a handful of lines (every field
 * gets this now, not just Notes, and Notes itself must never truncate -
 * explicit design instruction). Honors explicit '\n's. */
static void AddWrappedValue(const char *value, Boolean sensitive)
{
	FontID of = FntSetFont(stdFont);
	const char *p = value;

	while (*p && gFieldLineCount < bwFieldLineMax) {
		const char *nl = StrChr(p, '\n');
		UInt16 avail, runLen;
		Boolean lead;
		Int16 tw, fit;
		BwFieldLine *L;

		avail = nl ? (UInt16) (nl - p) : (UInt16) StrLen(p);
		if (avail > bwFieldLineValueMax - 1) avail = bwFieldLineValueMax - 1;

		fit = FntWidthToOffset(p, avail, bwValueWrapPx, &lead, &tw);
		if (fit >= (Int16) avail) runLen = avail;
		else if (fit > 0) runLen = (UInt16) fit;
		else runLen = 1;	/* always make forward progress even on a single very-wide char */

		L = &gFieldLines[gFieldLineCount++];
		MemMove(L->text, p, runLen);
		L->text[runLen] = 0;
		L->isLabel = false;
		L->sensitive = sensitive;

		p += runLen;
		if (runLen == avail && nl) p++;	/* consumed the run including its newline */
	}
	FntSetFont(of);
}

static void AddFieldLine(const char *label, const char *value, Boolean sensitive)
{
	BwFieldLine *L;
	if (!value || !value[0] || gFieldLineCount >= bwFieldLineMax) return;	/* empty -> not shown at all, per design */
	L = &gFieldLines[gFieldLineCount++];
	StrNCopy(L->text, label, sizeof(L->text) - 1);
	L->text[sizeof(L->text) - 1] = 0;
	L->isLabel = true;
	L->sensitive = false;
	AddWrappedValue(value, sensitive);
}

/* Login fields - Password masked behind Show like every other type's own
 * sensitive field. TOTP shows a live "code  (Ns)" countdown, refreshed
 * once a second by RecordFormHandleEvent's own nilEvent case (see
 * gTotpValueLine's own comment) - totp.c hardcodes a 30s step, so the
 * countdown display's own 30s assumption always matches the code's real
 * expiry, not just the common case. */
static void BuildLoginFields(void)
{
	gFieldLineCount = 0;
	gTotpValueLine = -1;
	AddFieldLine("Username", gCurrentItem.u.login.username, false);
	AddFieldLine("Password", gCurrentItem.u.login.password, true);
	AddFieldLine("URL", gCurrentItem.u.login.uri, false);
	if (gCurrentItem.u.login.totp[0]) {
		char code[7];
		UInt32 now = TotpUnixNow();
		if (TotpGenerateCode(gCurrentItem.u.login.totp, now, code)) {
			char display[16];
			UInt16 labelIdx = gFieldLineCount;
			StrPrintF(display, "%s  (%lus)", code, (unsigned long) (30 - (now % 30)));
			AddFieldLine("TOTP", display, false);
			gTotpValueLine = (Int16) (labelIdx + 1);	/* AddFieldLine's own label takes labelIdx; its value line follows right after */
		} else {
			AddFieldLine("TOTP", "(invalid)", false);
		}
	}
	AddFieldLine("Notes", gCurrentItem.notes, false);
}

/* Card fields, in the order Bitwarden's own web vault shows them. Number and
 * the CVV code are both as sensitive as a password. */
static void BuildCardFields(void)
{
	gFieldLineCount = 0;
	AddFieldLine("Holder", gCurrentItem.u.card.holder, false);
	AddFieldLine("Brand", gCurrentItem.u.card.brand, false);
	AddFieldLine("Number", gCurrentItem.u.card.number, true);
	if (gCurrentItem.u.card.expMonth[0] || gCurrentItem.u.card.expYear[0]) {
		char expiry[8];
		StrPrintF(expiry, "%s/%s", gCurrentItem.u.card.expMonth, gCurrentItem.u.card.expYear);
		AddFieldLine("Expiry", expiry, false);
	}
	AddFieldLine("Code", gCurrentItem.u.card.code, true);
	AddFieldLine("Notes", gCurrentItem.notes, false);
}

/* Identity fields, in the order Bitwarden's own web vault shows them -
 * every field the real product has (see PROTOCOL.md's SYNC_ITEM section for
 * where company/ssn/passportNumber/licenseNumber come from, since the
 * import/export field reference this was checked against omits them). SSN/
 * passport/license are as sensitive as a password. */
static void BuildIdentityFields(void)
{
	gFieldLineCount = 0;
	AddFieldLine("Title", gCurrentItem.u.identity.title, false);
	AddFieldLine("First Name", gCurrentItem.u.identity.firstName, false);
	AddFieldLine("Middle Name", gCurrentItem.u.identity.middleName, false);
	AddFieldLine("Last Name", gCurrentItem.u.identity.lastName, false);
	AddFieldLine("Username", gCurrentItem.u.identity.username, false);
	AddFieldLine("Company", gCurrentItem.u.identity.company, false);
	AddFieldLine("SSN", gCurrentItem.u.identity.ssn, true);
	AddFieldLine("Passport #", gCurrentItem.u.identity.passportNumber, true);
	AddFieldLine("License #", gCurrentItem.u.identity.licenseNumber, true);
	AddFieldLine("Email", gCurrentItem.u.identity.email, false);
	AddFieldLine("Phone", gCurrentItem.u.identity.phone, false);
	AddFieldLine("Address 1", gCurrentItem.u.identity.address1, false);
	AddFieldLine("Address 2", gCurrentItem.u.identity.address2, false);
	AddFieldLine("Address 3", gCurrentItem.u.identity.address3, false);
	AddFieldLine("City", gCurrentItem.u.identity.city, false);
	AddFieldLine("State", gCurrentItem.u.identity.state, false);
	AddFieldLine("Postal Code", gCurrentItem.u.identity.postalCode, false);
	AddFieldLine("Country", gCurrentItem.u.identity.country, false);
	AddFieldLine("Notes", gCurrentItem.notes, false);
}

static TableType *FieldsTable(void)
{
	UInt16 id;
	switch (FrmGetFormId(FrmGetActiveForm())) {
	case frmRecordLogin: id = tblRecordLogin; break;
	case frmRecordCard:  id = tblRecordCard; break;
	default:             id = tblRecordId; break;	/* frmRecordIdentity */
	}
	return (TableType *) GetObjectPtr(id);
}

static ScrollBarType *FieldsScrollBar(void)
{
	UInt16 id;
	switch (FrmGetFormId(FrmGetActiveForm())) {
	case frmRecordLogin: id = sclRecordLogin; break;
	case frmRecordCard:  id = sclRecordCard; break;
	default:             id = sclRecordId; break;	/* frmRecordIdentity */
	}
	return (ScrollBarType *) GetObjectPtr(id);
}

/* Same masking mechanics as ApplySensitiveDisplay below, just drawing a
 * whole "Label: value" row instead of one FIELD's worth of text - erase
 * first (this row can legitimately draw shorter content than whatever was
 * there before, e.g. after toggling Show changes the visible length; same
 * reasoning as MainForm.c's own MainDrawRow). */
static void FieldsDrawLine(void *table, Int16 row, Int16 column, RectangleType *bounds)
{
	UInt16 i = (UInt16) (gFieldTop + row);
	BwFieldLine *L;
	Int16 x, right;
	(void) table;
	(void) column;
	FntSetFont(stdFont);
	WinEraseRectangle(bounds, 0);
	if (i >= gFieldLineCount) return;
	L = &gFieldLines[i];
	right = (Int16) (bounds->topLeft.x + bounds->extent.x);

	if (L->isLabel) {
		char labelColon[bwFieldLineValueMax + 2];
		FntSetFont(boldFont);
		StrPrintF(labelColon, "%s:", L->text);
		WinDrawChars(labelColon, (Int16) StrLen(labelColon), bounds->topLeft.x, bounds->topLeft.y);
		return;
	}

	/* Value line - own row below its label, indented slightly, underlined
	 * like a real text field row ("ready for edit when we want to
	 * implement it" - explicit design instruction). Already wrapped to fit
	 * this row's own width (AddWrappedValue), so no truncation needed.
	 * WinDrawGrayLine (a dithered/50%-gray line, not solid) matches the
	 * native Field Manager's own dotted-underline look for an UNDERLINED
	 * field, rather than drawing a plain solid rule under the text. */
	x = (Int16) (bounds->topLeft.x + 4);
	if (L->sensitive && !gShowSensitive) {
		char masked[bwFieldLineValueMax];
		UInt16 n = (UInt16) StrLen(L->text), k;
		for (k = 0; k < n; k++) masked[k] = '*';
		masked[n] = 0;
		WinDrawChars(masked, (Int16) n, x, bounds->topLeft.y);
	} else {
		WinDrawChars(L->text, (Int16) StrLen(L->text), x, bounds->topLeft.y);
	}
	WinDrawGrayLine(bounds->topLeft.x, (Int16) (bounds->topLeft.y + bounds->extent.y - 1),
		(Int16) (right - 2), (Int16) (bounds->topLeft.y + bounds->extent.y - 1));
}

static UInt16 FieldsVisibleRows(void)
{
	RectangleType bounds;
	Int16 lineHeight = FntLineHeight();
	TblGetBounds(FieldsTable(), &bounds);
	return (UInt16) (lineHeight ? (bounds.extent.y / lineHeight) : 1);
}

/* Same shape as MainForm.c's own MainLoadRows/MainUpdateScrollBar - marks
 * only as many rows usable as there are lines left to show from gFieldTop,
 * and sizes the scrollbar to the real remaining count. */
static void FieldsLoadRows(void)
{
	TableType *t = FieldsTable();
	UInt16 rows = FieldsVisibleRows();
	Int16 lineHeight = FntLineHeight();
	UInt16 r = 0;
	RectangleType bounds;

	TblGetBounds(t, &bounds);
	WinEraseRectangle(&bounds, 0);

	for (; r < rows && (UInt16) (gFieldTop + r) < gFieldLineCount; r++) {
		TblSetRowHeight(t, r, lineHeight);
		TblSetItemStyle(t, r, 0, customTableItem);
		TblSetRowUsable(t, r, true);
		TblMarkRowInvalid(t, r);
	}
	for (; r < bwFieldTableRows; r++) TblSetRowUsable(t, r, false);

	{
		ScrollBarType *sb = FieldsScrollBar();
		Int16 shown = (Int16) ((gFieldLineCount - gFieldTop < rows) ? gFieldLineCount - gFieldTop : rows);
		Int16 max = (Int16) ((gFieldLineCount > rows) ? gFieldLineCount - rows : 0);
		SclSetScrollBar(sb, (Int16) gFieldTop, 0, max, (Int16) (shown ? shown : 1));
	}
}

static void FieldsScroll(Int16 deltaLines)
{
	Int16 nt = (Int16) gFieldTop + deltaLines;
	UInt16 rows = FieldsVisibleRows();
	Int16 max = (Int16) ((gFieldLineCount > rows) ? gFieldLineCount - rows : 0);
	if (nt < 0) nt = 0;
	if (nt > max) nt = max;
	gFieldTop = (UInt16) nt;
	FieldsLoadRows();
	TblRedrawTable(FieldsTable());
}

/* Note's own masking (bottom-row Show/Hide, same as every other type - see
 * BitWardenRsc.h's own comment on why a note gets this treatment at all).
 * Preserves newlines so the masked text still wraps roughly like the real
 * one instead of collapsing to a single asterisk run. `draw` must be false
 * when called from RecordOpen - see the identical reasoning this used to
 * live on ApplySensitiveDisplay, before Login/Card/Identity all moved to
 * the shared dynamic table and left this as the one remaining FIELD-based
 * mask. */
/* Real bug (2026-08 user report): fldRecordNoteText had no scrollbar at
 * all, and MULTIPLELINES only controls wrapping, not scrolling - any note
 * taller than the field's own visible area was silently clipped with no
 * indication there was more, and no way to reach it. This mirrors the
 * shared field table's own FieldsLoadRows scrollbar refresh (values are in
 * LINES, not pixels, per FldGetScrollValues). */
static void NoteUpdateScrollBar(void)
{
	FieldType *fld = (FieldType *) GetObjectPtr(fldRecordNoteText);
	ScrollBarType *sb = (ScrollBarType *) GetObjectPtr(sclRecordNote);
	UInt16 scrollPos, textHeight, fieldHeight;
	Int16 max;
	FldGetScrollValues(fld, &scrollPos, &textHeight, &fieldHeight);
	max = (Int16) ((textHeight > fieldHeight) ? textHeight - fieldHeight : 0);
	SclSetScrollBar(sb, (Int16) scrollPos, 0, max, (Int16) (fieldHeight ? fieldHeight : 1));
}

static void ApplyNoteDisplay(Boolean draw)
{
	FieldType *fld = (FieldType *) GetObjectPtr(fldRecordNoteText);
	if (!gShowSensitive) {
		const char *src = gCurrentItem.notes;
		UInt16 len = (UInt16) StrLen(src), i;
		if (len >= bwMaxField) len = bwMaxField - 1;
		for (i = 0; i < len; i++) gMaskedField[i] = (src[i] == '\n') ? '\n' : '*';
		gMaskedField[len] = 0;
		FldSetTextPtr(fld, gMaskedField);
	} else {
		FldSetTextPtr(fld, gCurrentItem.notes);
	}
	FldRecalculateField(fld, true);
	if (draw) FldDrawField(fld);
	NoteUpdateScrollBar();
}

/* PROTOCOL.md's SYNC_ITEM section documents exactly what the generic
 * username/password/uri/totp wire slots mean per itemType (WiRSa/
 * mock_wirsa populate them accordingly: card holder/number/expiry, or
 * identity name/SSN/email) - this just routes them into whichever form's
 * own fields are actually on screen. */
static void RecordOpen(UInt16 formID)
{
	FormPtr frm = FrmGetActiveForm();

	if (!BwStoreGetItemFull(gRecordIndex, &gCurrentItem)) {
		FrmCustomAlert(altErr, "Could not read this item.", NULL, NULL);
	}
	gShowSensitive = false;
	FrmCopyTitle(frm, gCurrentItem.name[0] ? gCurrentItem.name : "(untitled)");

	switch (formID) {
	case frmRecordNote:
		ApplyNoteDisplay(false);
		break;
	case frmRecordLogin:
		gFieldTop = 0;
		BuildLoginFields();
		TblSetCustomDrawProcedure(FieldsTable(), 0, FieldsDrawLine);
		TblSetColumnUsable(FieldsTable(), 0, true);
		FieldsLoadRows();
		break;
	case frmRecordCard:
		gFieldTop = 0;
		BuildCardFields();
		TblSetCustomDrawProcedure(FieldsTable(), 0, FieldsDrawLine);
		TblSetColumnUsable(FieldsTable(), 0, true);
		FieldsLoadRows();
		break;
	default:	/* frmRecordIdentity */
		gFieldTop = 0;
		BuildIdentityFields();
		TblSetCustomDrawProcedure(FieldsTable(), 0, FieldsDrawLine);
		TblSetColumnUsable(FieldsTable(), 0, true);
		FieldsLoadRows();
		break;
	}

	FrmDrawForm(frm);
}

static void RecordClose(void)
{
	/* Drop the decrypted item from RAM the moment we leave this form -
	 * nothing here should linger longer than the view that's showing it. */
	MemSet(&gCurrentItem, sizeof(gCurrentItem), 0);
	MemSet(gMaskedField, sizeof(gMaskedField), 0);
	MemSet(gFieldLines, sizeof(gFieldLines), 0);
	gFieldLineCount = 0;
	gFieldTop = 0;
	gTotpValueLine = -1;
}

/* BitWarden.c's EventLoop needs this to know whether frmRecordLogin needs a
 * bounded nilEvent timeout right now (same reasoning/shape as MainForm.c's
 * own MainHasPendingSearch) - only true while that form is actually active
 * AND its current item has a TOTP secret, so every other form keeps its
 * normal evtWaitForever (no effect on the sleep-detection gap-heuristic or
 * battery anywhere else). */
Boolean RecordHasTotpActive(void)
{
	return gTotpValueLine >= 0 && FrmGetFormId(FrmGetActiveForm()) == frmRecordLogin;
}

/* Refreshes just the TOTP row's own text in place (code + countdown) and
 * redraws - NOT a full BuildLoginFields() rebuild, which would needlessly
 * redo every other row and reset scroll position on every tick. Silently
 * does nothing if gTotpValueLine is stale (wrong form active, or the row
 * index somehow out of range) - called unconditionally from nilEvent, so
 * no active TOTP field must be the common, cheap case. */
static void RecordTotpTick(void) SEC001;

/* Deliberately self-contained - no calls to any OTHER function in THIS
 * file (RecordForm.c), only ROM traps and totp.c's own (different file,
 * safely relocated by the linker regardless of segment). Real crash found
 * live in POSE64 tagging a DIFFERENT function SEC001 while it called back
 * into a same-file main-segment function (see BwDebugEstablishAndDumpKeys's
 * own comment in Store.c) - narrowed down to specifically the sec001-calls-
 * back-into-main DIRECTION, same file, regardless of static/extern linkage.
 * Duplicates RecordHasTotpActive's own tiny condition and hardcodes
 * tblRecordLogin directly (TOTP only ever shows on frmRecordLogin, unlike
 * FieldsTable's own three-way dispatch) rather than calling either. */
static void RecordTotpTick(void)
{
	char code[7];
	char display[16];
	UInt32 now;
	TableType *table;
	Int16 visRow;

	if (gTotpValueLine < 0 || FrmGetFormId(FrmGetActiveForm()) != frmRecordLogin) return;
	if (gTotpValueLine >= (Int16) gFieldLineCount || !gCurrentItem.u.login.totp[0]) return;
	now = TotpUnixNow();
	if (!TotpGenerateCode(gCurrentItem.u.login.totp, now, code)) return;
	StrPrintF(display, "%s  (%lus)", code, (unsigned long) (30 - (now % 30)));
	StrNCopy(gFieldLines[gTotpValueLine].text, display, sizeof(gFieldLines[gTotpValueLine].text) - 1);
	gFieldLines[gTotpValueLine].text[sizeof(gFieldLines[gTotpValueLine].text) - 1] = 0;

	table = (TableType *) FrmGetObjectPtr(FrmGetActiveForm(), FrmGetObjectIndex(FrmGetActiveForm(), tblRecordLogin));
	visRow = (Int16) (gTotpValueLine - gFieldTop);
	if (visRow >= 0 && visRow < bwFieldTableRows) {
		TblMarkRowInvalid(table, visRow);
		TblRedrawTable(table);
	}
}

/* Called from BitWarden.c's sleep-wipe paths (both the sysNotifySleepNotifyEvent
 * callback and EventLoop's own wall-clock-gap backstop) - real-device feedback:
 * a record left open across a sleep still had its already-decrypted fields
 * (including the real, un-masked password behind the Show toggle) sitting in
 * this file's own gCurrentItem, entirely independent of whether the cached
 * master password or Sec35's own key got wiped - neither of those un-decrypts
 * data that's already in RAM. Wiping the buffer here alone isn't enough (the
 * on-screen pixels don't repaint themselves just because the buffer changed
 * under them) - BitWarden.c's own EventLoop is responsible for also forcing
 * navigation back to frmMain when it detects this case, which is what
 * actually clears the visible glyphs. */
void BwRecordDataWipe(void)
{
	RecordClose();
}

Boolean RecordFormHandleEvent(EventPtr e)
{
	Boolean handled = false;

	switch (e->eType) {
	case frmOpenEvent:
		RecordOpen(FrmGetFormId(FrmGetActiveForm()));
		handled = true;
		break;

	case frmCloseEvent:
		RecordClose();
		break;

	case ctlSelectEvent:
		{
			UInt16 ctl = e->data.ctlSelect.controlID;
			if (ctl == btnRecordShow || ctl == btnRecordCardShow || ctl == btnRecordIdShow
					|| ctl == btnRecordNoteShow) {
				gShowSensitive = !gShowSensitive;
				CtlSetLabel((ControlType *) GetObjectPtr(ctl), gShowSensitive ? "Hide" : "Show");
				if (FrmGetFormId(FrmGetActiveForm()) == frmRecordNote) {
					ApplyNoteDisplay(true);
				} else {
					/* TblRedrawTable alone is a no-op here - the Table
					 * Manager only repaints rows it's been told are invalid
					 * (FieldsLoadRows already does that for a scroll; do it
					 * here too, not just re-draw, so a masked/revealed
					 * value of a DIFFERENT length than before actually
					 * shows up instead of the stale previous content). */
					FieldsLoadRows();
					TblRedrawTable(FieldsTable());
				}
				handled = true;
			} else if (ctl == btnRecordDone || ctl == btnRecordNoteDone
					|| ctl == btnRecordCardDone || ctl == btnRecordIdDone) {
				FrmGotoForm(frmMain);
				handled = true;
			}
		}
		break;

	/* Card/Identity's own field table can overflow its visible area (up to
	 * 18 possible Identity fields) - same scrollbar-drag + hardware page
	 * up/down handling as MainForm.c's own table. */
	case sclRepeatEvent:
		switch (FrmGetFormId(FrmGetActiveForm())) {
		case frmRecordLogin:
		case frmRecordCard:
		case frmRecordIdentity:
			FieldsScroll((Int16) (e->data.sclRepeat.newValue - e->data.sclRepeat.value));
			handled = true;
			break;
		case frmRecordNote:
			{
				FieldType *fld = (FieldType *) GetObjectPtr(fldRecordNoteText);
				Int16 delta = (Int16) (e->data.sclRepeat.newValue - e->data.sclRepeat.value);
				FldScrollField(fld, (UInt16) (delta < 0 ? -delta : delta), delta < 0 ? winUp : winDown);
				NoteUpdateScrollBar();
				handled = true;
			}
			break;
		default:
			break;
		}
		break;

	case keyDownEvent:
		if (e->data.keyDown.chr == vchrPageUp || e->data.keyDown.chr == vchrPageDown) {
			switch (FrmGetFormId(FrmGetActiveForm())) {
			case frmRecordLogin:
			case frmRecordCard:
			case frmRecordIdentity:
				{
					Int16 page = (Int16) FieldsVisibleRows();
					if (page < 1) page = 1;
					FieldsScroll((Int16) (e->data.keyDown.chr == vchrPageDown ? page : -page));
					handled = true;
				}
				break;
			case frmRecordNote:
				/* Real bug (2026-08 user report): frmRecordNote's own
				 * fldRecordNoteText has no scrollbar AND was missing from
				 * this hardware up/down handling entirely - a plain PalmOS
				 * FIELD does NOT auto-scroll just because MULTIPLELINES is
				 * set (that only controls wrapping), so any note taller
				 * than the field's own 109px was simply clipped with no
				 * way to reach the rest. FldScrollField clamps at both
				 * ends on its own - safe to call without checking
				 * FldScrollable first. */
				{
					FieldType *fld = (FieldType *) GetObjectPtr(fldRecordNoteText);
					UInt16 visLines = FldGetVisibleLines(fld);
					if (visLines < 1) visLines = 1;
					FldScrollField(fld, visLines, e->data.keyDown.chr == vchrPageDown ? winDown : winUp);
					NoteUpdateScrollBar();
					handled = true;
				}
				break;
			default:
				break;
			}
		}
		break;

	/* Live TOTP countdown - see gTotpValueLine's own comment. Fires
	 * roughly once a second only while frmRecordLogin is open with an
	 * active TOTP field (BitWarden.c's own EventLoop gives this form a
	 * bounded nilEvent timeout only in that case - RecordHasTotpActive()),
	 * so this is a no-op call everywhere else. */
	case nilEvent:
		RecordTotpTick();
		break;

	default:
		break;
	}
	return handled;
}
