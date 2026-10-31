/*
 * Chat.c - one conversation. The messages are drawn by hand in a gadget so each can be several lines high,
 * carry its time on the last line, and be selected as a block (tap one, tap another: everything between).
 * Second code segment (MC_UI): no 32-bit multiply/divide and no const tables in here.
 */
#include "MeshChat.h"

#define CL_MAX		400
#define LIST_ROWS	8				/* 8 lines of 11 pixels in the message gadget */
#define TXT_W		148				/* the width messages are wrapped to (the gadget is 152 wide) */

#define CL_FIRST	1				/* the line starts a message: the sender's name goes in front */
#define CL_LAST		2				/* the line ends a message: the time goes at its right */
#define CL_HEAD		4				/* a date heading */

typedef struct {
	UInt8	msg;					/* index in gMsg, 0xFF for a date heading */
	UInt8	len;
	UInt16	off;					/* into the message's text; for a heading, the message it dates */
	UInt8	flags;
	UInt8	pad;
} CLine;

static CLine gLine[CL_MAX];
static UInt16 gNLine, gTop;
static Int16 gSelA = -1, gSelB = -1;	/* the selected messages (gMsg indexes), -1 none */
static char gTitle[40], gInfo[44], gCnt[12];

/* What each of the eight rows shows now: only a row that differs from this is drawn again. */
typedef struct {
	UInt8	msg, len, flags, mark, sel, pad;
	UInt16	off;
	Int32	when;
} Shown;
static Shown gShown[LIST_ROWS];
static Boolean gShownOk;				/* gShown is what the gadget really displays */
static UInt8 gDetail;					/* the message frmMsg shows (gMsg index) */
static UInt8 gPosKind;					/* what frmPos sends: 5 position, 6 waypoint */
static char gPosLat[16], gPosLon[16], gPosName[32];
static char gDetailText[640];

/* ---- a message's pieces ------------------------------------------------------------------------ */

static MC_UI void Prefix(const Msg *m, char *out)
{
	if (m->out) StrCopy(out, "Me: ");
	else {
		if (m->from >= gNNodes) StrCopy(out, "?");
		else if (gNodes[m->from].shortName[0] && StrCompare(gNodes[m->from].shortName, "?") != 0) StrNCopy(out, gNodes[m->from].shortName, 7);
		else StrCopy(out, gNodes[m->from].id + 5);		/* not named yet (or saved so): the end of the node id */
		out[7] = '\0';
		StrCat(out, ": ");
	}
}

/* A bold "!" for a message that failed, a bold "?" while it has no acknowledgement (sending, sent, or not known),
 * nothing once it is acknowledged. */
static MC_UI char Mark(const Msg *m)
{
	if (! m->out) return 0;
	if (m->status == 3) return '!';
	if (m->status == 2) return 0;
	return '?';
}

/* The room kept for "! 12:59 pm" at the right of a message's last line (the widest time the formats give). */
static MC_UI Int16 TimeW(const Msg *m)
{
	Int16 w;

	FntSetFont(stdFont);
	w = FntCharsWidth("12:59 pm", 8);
	if (Mark(m)) {
		FntSetFont(boldFont);
		w = (Int16) (w + FntCharsWidth("!", 1) + 3);
		FntSetFont(stdFont);
	}
	return w;
}

static MC_UI Boolean Add(UInt8 msg, UInt16 off, UInt8 len, UInt8 flags)
{
	if (gNLine >= CL_MAX) return false;
	gLine[gNLine].msg = msg;
	gLine[gNLine].off = off;
	gLine[gNLine].len = len;
	gLine[gNLine].flags = flags;
	gNLine++;
	return true;
}

/* The lines of the conversation, leaving out the first `skip` messages. False if they do not all fit. */
static MC_UI Boolean Layout(UInt16 skip)
{
	UInt16 i;
	char pre[12];
	Int32 lastDay = -1, day;
	Boolean ok = true;

	gNLine = 0;
	for (i = 0; i < gNMsg && ok; i++) {
		const Msg *m = &gMsg[i];
		const char *text = MsgText(m);
		UInt16 off = 0, n, shown, off2;
		Int16 pw, tw, lw;
		Boolean first = true;

		if (m->conv != gCurConv) continue;
		if (skip) { skip--; continue; }
		day = DayOf(m->when);
		if (day != lastDay) {
			lastDay = day;
			ok = Add(0xFF, i, 0, CL_HEAD);
			if (! ok) break;
		}
		Prefix(m, pre);
		FntSetFont(boldFont);
		pw = FntCharsWidth(pre, (Int16) StrLen(pre));
		FntSetFont(stdFont);
		tw = TimeW(m);
		FntSetFont(stdFont);
		for (;;) {
			Boolean end;
			UInt8 fl = first ? CL_FIRST : 0;

			n = FntWordWrap(text + off, (UInt16) (first ? TXT_W - pw : TXT_W));
			if (n == 0 && text[off]) n = 1;
			shown = n;
			while (shown > 0 && (text[off + shown - 1] == '\n' || text[off + shown - 1] == ' ')) shown--;
			off2 = (UInt16) (off + n);
			end = text[off2] == '\0';
			if (end) {
				lw = (Int16) ((first ? pw : 0) + FntCharsWidth(text + off, (Int16) shown));
				if (lw + tw + 6 <= TXT_W) fl |= CL_LAST;
			}
			ok = Add((UInt8) i, off, (UInt8) shown, fl);
			if (! ok) break;
			if (end) {
				if (! (fl & CL_LAST)) ok = Add((UInt8) i, off2, 0, CL_LAST);		/* the time alone on a line */
				break;
			}
			off = off2;
			first = false;
		}
	}
	return ok;
}

static MC_UI void BuildLines(void)
{
	UInt16 skip = 0;

	while (! Layout(skip) && skip < MAX_MSG) skip++;		/* too long for the table: drop the oldest */
}

static MC_UI UInt16 MaxTop(void)
{
	return gNLine > LIST_ROWS ? (UInt16) (gNLine - LIST_ROWS) : 0;
}

static MC_UI void ScrollSet(void)
{
	FormPtr frm = FrmGetActiveForm();
	ScrollBarPtr sb = (ScrollBarPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, sclChat));

	SclSetScrollBar(sb, (Int16) gTop, 0, (Int16) MaxTop(), LIST_ROWS);
}

/* ---- drawing ------------------------------------------------------------------------------------ */

static MC_UI Boolean Selected(UInt8 msg)
{
	Int16 lo, hi;

	if (gSelA < 0 || msg == 0xFF) return false;
	lo = gSelA < gSelB ? gSelA : gSelB;
	hi = gSelA < gSelB ? gSelB : gSelA;
	return (Int16) msg >= lo && (Int16) msg <= hi;
}

static MC_UI void DrawLine(const CLine *l, Coord x0, Coord y)
{
	char buf[32], pre[12];
	Int16 w;
	RectangleType r;

	if (l->flags & CL_HEAD) {
		FmtDay(gMsg[l->off].when, buf);
		FntSetFont(boldFont);
		w = FntCharsWidth(buf, (Int16) StrLen(buf));
		WinDrawChars(buf, (Int16) StrLen(buf), (Coord) (x0 + (152 - w) / 2), y);
		FntSetFont(stdFont);
		WinDrawLine(x0, (Coord) (y + 10), (Coord) (x0 + 151), (Coord) (y + 10));
		return;
	}
	{
		const Msg *m = &gMsg[l->msg];
		Coord x = (Coord) (x0 + 2);

		FntSetFont(stdFont);
		if (l->flags & CL_FIRST) {
			Prefix(m, pre);
			FntSetFont(boldFont);
			WinDrawChars(pre, (Int16) StrLen(pre), x, y);
			x = (Coord) (x + FntCharsWidth(pre, (Int16) StrLen(pre)));
			FntSetFont(stdFont);
		}
		if (l->len) WinDrawChars(MsgText(m) + l->off, l->len, x, y);
		if (l->flags & CL_LAST) {
			char mk = Mark(m);

			FmtClock(m->when, buf);
			w = FntCharsWidth(buf, (Int16) StrLen(buf));
			WinDrawChars(buf, (Int16) StrLen(buf), (Coord) (x0 + 150 - w), y);
			if (mk) {
				FntSetFont(boldFont);
				WinDrawChars(&mk, 1, (Coord) (x0 + 150 - w - 3 - FntCharsWidth(&mk, 1)), y);
				FntSetFont(stdFont);
			}
			WinDrawGrayLine(x0, (Coord) (y + 10), (Coord) (x0 + 151), (Coord) (y + 10));
		}
		if (Selected(l->msg)) {
			RctSetRectangle(&r, x0, y, 152, 11);
			WinInvertRectangle(&r, 0);
		}
	}
}

static MC_UI void Snap(UInt16 k, Shown *s)
{
	MemSet(s, sizeof(Shown), 0);
	if (k >= gNLine) { s->msg = 0xFE; return; }
	s->msg = gLine[k].msg; s->len = gLine[k].len; s->flags = gLine[k].flags; s->off = gLine[k].off;
	if (s->msg != 0xFF) {
		const Msg *m = &gMsg[s->msg];

		s->mark = (UInt8) Mark(m);
		s->when = m->when;
		s->sel = Selected(s->msg) ? 1 : 0;
	} else {
		s->when = gMsg[s->off].when;
	}
}

/* The same line, whatever its mark or selection. */
static MC_UI Boolean SameGeo(const Shown *a, const Shown *b)
{
	return a->msg == b->msg && a->len == b->len && a->flags == b->flags && a->off == b->off && a->when == b->when;
}

static MC_UI void DrawList(const RectangleType *r)
{
	UInt16 k;
	RectangleType e = *r, oldClip;

	WinGetClip(&oldClip);
	WinSetClip(&e);										/* the eighth line's rule falls below the gadget */
	WinEraseRectangle(&e, 0);
	for (k = 0; k < LIST_ROWS; k++) {
		if (gTop + k < gNLine)
			DrawLine(&gLine[gTop + k], r->topLeft.x, (Coord) (r->topLeft.y + k * 11));
		Snap((UInt16) (gTop + k), &gShown[k]);
	}
	gShownOk = true;
	WinSetClip(&oldClip);
}

/* The rule under a line, alone (a line moved up from the last row, whose rule had fallen below the gadget). */
static MC_UI void DrawRule(const CLine *l, Coord x0, Coord y)
{
	RectangleType r;

	if (l->flags & CL_HEAD) WinDrawLine(x0, (Coord) (y + 10), (Coord) (x0 + 151), (Coord) (y + 10));
	else if (l->flags & CL_LAST) WinDrawGrayLine(x0, (Coord) (y + 10), (Coord) (x0 + 151), (Coord) (y + 10));
	else return;
	if (Selected(l->msg)) {
		RctSetRectangle(&r, x0, (Coord) (y + 10), 152, 1);
		WinInvertRectangle(&r, 0);
	}
}

/* Brings the gadget up to date without clearing it: lines that only moved (the list scrolled or grew) are moved
 * by the screen itself, then just the rows that differ are drawn - a new message, a mark that changed. */
static MC_UI void PaintList(void)
{
	RectangleType r, line, vac, oldClip;
	FormPtr frm = FrmGetActiveForm();
	Shown now[LIST_ROWS], old[LIST_ROWS];
	UInt16 k;
	Int16 s, shift = 0;

	FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, gadChatList), &r);
	if (! gShownOk) { DrawList(&r); return; }
	for (k = 0; k < LIST_ROWS; k++) Snap((UInt16) (gTop + k), &now[k]);
	for (k = 0; k < LIST_ROWS && SameGeo(&gShown[k], &now[k]); k++) ;
	WinGetClip(&oldClip);
	WinSetClip(&r);
	if (k < LIST_ROWS) {
		for (s = 1; s < LIST_ROWS && ! shift; s++) {
			for (k = 0; k + s < LIST_ROWS && SameGeo(&gShown[k + s], &now[k]); k++) ;
			if (k + s >= LIST_ROWS) { shift = s; break; }					/* what was lower is now higher */
			for (k = 0; k + s < LIST_ROWS && SameGeo(&gShown[k], &now[k + s]); k++) ;
			if (k + s >= LIST_ROWS) shift = (Int16) -s;
		}
		if (shift) {
			MemMove(old, gShown, sizeof(old));
			WinScrollRectangle(&r, shift > 0 ? winUp : winDown, (Coord) ((shift > 0 ? shift : -shift) * 11), &vac);
			for (k = 0; k < LIST_ROWS; k++) {
				Int16 from = (Int16) k + shift;

				if (from >= 0 && from < LIST_ROWS) MemMove(&gShown[k], &old[from], sizeof(Shown));
				else { MemSet(&gShown[k], sizeof(Shown), 0); gShown[k].msg = 0xFD; }
			}
			if (shift > 0 && old[LIST_ROWS - 1].msg < 0xFD) {
				UInt16 row = (UInt16) (LIST_ROWS - 1 - shift);

				DrawRule(&gLine[gTop + row], r.topLeft.x, (Coord) (r.topLeft.y + row * 11));
			}
		}
	}
	for (k = 0; k < LIST_ROWS; k++) {
		if (MemCmp(&gShown[k], &now[k], sizeof(Shown)) == 0) continue;
		RctSetRectangle(&line, r.topLeft.x, (Coord) (r.topLeft.y + k * 11), 152, 11);
		WinEraseRectangle(&line, 0);
		if (gTop + k < gNLine) DrawLine(&gLine[gTop + k], r.topLeft.x, line.topLeft.y);
		MemMove(&gShown[k], &now[k], sizeof(Shown));
	}
	WinSetClip(&oldClip);
}

static MC_UI Boolean ListGadget(FormGadgetType *g, UInt16 cmd, void *param)
{
	if (cmd == formGadgetDrawCmd) DrawList(&g->rect);
	return true;
}

/* The counter. */
static MC_UI void CntDraw(const RectangleType *r)
{
	RectangleType e = *r;
	Int16 w;

	WinEraseRectangle(&e, 0);
	FntSetFont(stdFont);
	w = FntCharsWidth(gCnt, (Int16) StrLen(gCnt));
	WinDrawChars(gCnt, (Int16) StrLen(gCnt), (Coord) (r->topLeft.x + (r->extent.x - w) / 2), (Coord) (r->topLeft.y - 1));
}

/* The pop-up at the top right: back to the main menu, then the channels - or the 8 latest direct messages. */
static char gPopLbl[9][26], gPopCat[12];
static char *gPopPtr[9];
static UInt16 gPopConv[9];

static MC_UI void PopFill(void)
{
	FormPtr frm = FrmGetActiveForm();
	ListPtr lst = (ListPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, lstChatConv));
	Boolean ch = gConv[gCurConv].isChan;
	UInt16 i, c, n = 1;
	Int16 sel = -1;
	char t[44];

	StrCopy(gPopLbl[0], "Main Menu");
	for (i = 0; i < (ch ? 8 : gNOrder) && n < 9; i++) {
		if (ch) {							/* channels by slot, the number in front */
			for (c = 0; c < gNConv; c++) if (gConv[c].isChan && gConv[c].index == i) break;
			if (c >= gNConv) continue;
			t[0] = '\0';
			PutNum(t, i);
			StrCat(t, ": ");
			ConvLabel(c, t + StrLen(t));
		} else {
			c = gOrder[i];
			if (gConv[c].isChan) continue;
			ConvLabel(c, t);
		}
		StrNCopy(gPopLbl[n], t, 25);
		gPopLbl[n][25] = '\0';
		gPopConv[n] = c;
		if (c == gCurConv) sel = (Int16) n;
		n++;
	}
	for (i = 0; i < n; i++) gPopPtr[i] = gPopLbl[i];
	LstSetListChoices(lst, gPopPtr, n);
	LstSetHeight(lst, n);
	{	/* as wide as the longest name and one character more, against the right edge */
		RectangleType r;
		Int16 w = 0, lw;

		FntSetFont(stdFont);
		for (i = 0; i < n; i++) { lw = FntCharsWidth(gPopLbl[i], (Int16) StrLen(gPopLbl[i])); if (lw > w) w = lw; }
		w = (Int16) (w + FntCharsWidth("m", 1) + 6);
		if (w > 150) w = 150;
		FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, lstChatConv), &r);
		r.topLeft.x = (Coord) (160 - w);
		r.extent.x = w;
		FrmSetObjectBounds(frm, FrmGetObjectIndex(frm, lstChatConv), &r);
	}
	LstSetSelection(lst, sel);
	StrCopy(gPopCat, ch ? "Channels" : "Direct Msg");
	CtlSetLabel((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, popChatConv)), gPopCat);
}

static MC_UI Boolean CntGadget(FormGadgetType *g, UInt16 cmd, void *param)
{
	if (cmd == formGadgetDrawCmd) CntDraw(&g->rect);
	return true;
}

static MC_UI void Counter(Boolean force)
{
	FormPtr frm = FrmGetActiveForm();
	char *p = FldGetTextPtr(GetField(fldChatEntry));
	char buf[12];
	RectangleType r;
	UInt16 n;

	buf[0] = '\0';
	n = p ? TextBytes(p) : 0;
	if (n < 100) StrCat(buf, "0");
	if (n < 10) StrCat(buf, "0");
	PutNum(buf, n);
	StrCat(buf, "/233");
	if (! force && StrCompare(buf, gCnt) == 0) return;
	StrCopy(gCnt, buf);
	FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, gadChatCnt), &r);
	CntDraw(&r);
}

MC_UI void ChatKeyPoll(void)
{
	Counter(false);
}

/* ---- building and keeping the place -------------------------------------------------------------- */

static MC_UI void Refill(Boolean toEnd)
{
	Boolean atEnd = gTop >= MaxTop();

	BuildLines();
	if (toEnd || atEnd) gTop = MaxTop();
	else if (gTop > MaxTop()) gTop = MaxTop();
	ScrollSet();
}

static MC_UI void Rebuild(Boolean toEnd)
{
	Refill(toEnd);
	PaintList();
}

/* ---- selection and copy ---------------------------------------------------------------------------- */

static MC_UI void ShowDetail(UInt8 msg)
{
	gSelA = gSelB = -1;
	PaintList();								/* the form hides the list: take the selection off first */
	gDetail = msg;
	FrmPopupForm(frmMsg);
}

/* First tap selects, a second tap on another extends to a range, a third starts over; a second tap on the one
 * selected message opens it in full. */
static UInt8 gLastTapMsg = 0xFF;
static UInt32 gLastTapTick;

static MC_UI void SelectMsg(UInt8 msg)
{
	UInt32 now = TimGetTicks();
	Boolean again = gLastTapMsg == msg && (Int32) (now - gLastTapTick) < (Int32) SysTicksPerSecond() * 2;

	gLastTapMsg = msg;
	gLastTapTick = now;
	if (again) { gLastTapMsg = 0xFF; ShowDetail(msg); return; }		/* two taps on one message */
	if (gSelA < 0) gSelA = gSelB = msg;
	else if (gSelA == gSelB && gSelA == msg) { ShowDetail(msg); return; }
	else if (gSelA == gSelB) gSelB = msg;
	else gSelA = gSelB = msg;
	PaintList();
}

static MC_UI void SelectAll(void)
{
	Int16 i;

	gSelA = gSelB = -1;
	for (i = 0; i < (Int16) gNMsg; i++) {
		if (gMsg[i].conv != gCurConv) continue;
		if (gSelA < 0) gSelA = i;
		gSelB = i;
	}
	PaintList();
}

/* The selected messages as text, "Name: text", one per line. */
static MC_UI void CopySelection(void)
{
	Int16 lo = gSelA < gSelB ? gSelA : gSelB, hi = gSelA < gSelB ? gSelB : gSelA, i;
	UInt16 size = 4;
	MemHandle h;
	char *p, pre[12];

	for (i = lo; i <= hi; i++)
		if (gMsg[i].conv == gCurConv) size = (UInt16) (size + StrLen(MsgText(&gMsg[i])) + 14);
	h = MemHandleNew(size);
	if (! h) return;
	p = (char *) MemHandleLock(h);
	p[0] = '\0';
	for (i = lo; i <= hi; i++) {
		if (gMsg[i].conv != gCurConv) continue;
		Prefix(&gMsg[i], pre);
		StrCat(p, pre);
		StrCat(p, MsgText(&gMsg[i]));
		if (i < hi) StrCat(p, "\n");
	}
	ClipboardAddItem(clipboardText, p, (UInt16) StrLen(p));
	MemHandleUnlock(h);
	MemHandleFree(h);
}

/* ---- actions ----------------------------------------------------------------------------------------- */

static MC_UI void ChatInfo(void)
{
	static char a[80], b[80], c[80];
	const Conv *v = &gConv[gCurConv];

	if (! v->isChan) {					/* a person: everything about the node, full screen */
		gCurNode = v->index;
		FrmPopupForm(frmNode);
		return;
	}
	StrCopy(a, "Channel ");
	StrCat(a, gTitle);
	StrCopy(b, "\nSlot ");
	PutNum(b, v->index);
	StrCat(b, v->index == 0 ? ", primary." : ", secondary.");
	StrCopy(c, "\nEveryone with the same key can read it.");
	Info(a, b, c);
}

/* bell: the text goes out with ":bell:" in front (the alert marker Meshtastic apps show as a bell). */
static MC_UI void ChatSend(Boolean bell)
{
	FieldPtr fld = GetField(fldChatEntry);
	char *p = FldGetTextPtr(fld);
	char buf[TXT_MAX + 2];
	const char *text = p;

	if (bell) {
		StrCopy(buf, ":bell:");
		if (p && p[0]) {
			StrCat(buf, " ");
			StrNCopy(buf + 7, p, TXT_MAX - 8);
		}
		buf[TXT_MAX - 1] = '\0';
		text = buf;
	} else if (! p || ! p[0]) return;
	if (TextBytes(text) > 233) {
		Info("Too long: a message can be 233 bytes.", NULL, NULL);
		return;
	}
	if (! gWirsaUp) { Info("Not connected.", "Use Options > Connect first.", NULL); return; }
	if (! LinkSend(gCurConv, text))
		Info(gLinkText[0] ? gLinkText : "The message was not sent.", NULL, NULL);
	if (p) FldDelete(fld, 0, FldGetTextLength(fld));
	Counter(false);
	SortConv();
	gSelA = gSelB = -1;
	Rebuild(true);
}

/* The Send menu: node info, position, telemetry, traceroute go to the other node and its answer comes back as a
 * message; a position or a waypoint is asked for first. */
static MC_UI void ChatReq(UInt8 kind)
{
	if (! gWirsaUp) { Info("Not connected.", "Use Options > Connect first.", NULL); return; }
	if (gConv[gCurConv].isChan && kind >= 2 && kind <= 4) {
		Info("This goes to one person.", "Open a direct message with them first (Chat > Reply privately).", NULL);
		return;
	}
	if (kind == 5 || kind == 6) { gPosKind = kind; FrmPopupForm(frmPos); return; }
	if (! LinkReq(gCurConv, kind, 0, 0, ""))
		Info(gLinkText[0] ? gLinkText : "The request was not sent.", NULL, NULL);
	SortConv();
	gSelA = gSelB = -1;
	Rebuild(true);
}

/* Chat > Clear messages: this conversation only, and only the Palm's copy. */
static MC_UI void ChatClear(void)
{
	if (! Ask("Delete all messages of this conversation?\n", "Only on this Palm: WiRSa keeps its copy.", NULL)) return;
	DropConv(gCurConv);
	gSelA = gSelB = -1;
	SortConv();
	Rebuild(true);
}

/* From a channel: a direct message goes to one node, so it is asked which: whoever spoke last. */
static MC_UI void ChatDm(void)
{
	Int16 i;
	UInt16 dm;

	if (! gConv[gCurConv].isChan) { Info("This already is a private conversation.", NULL, NULL); return; }
	for (i = (Int16) gNMsg - 1; i >= 0; i--)
		if (gMsg[i].conv == gCurConv && ! gMsg[i].out) break;
	if (i < 0) { Info("Nobody has spoken here yet.", NULL, NULL); return; }
	if (! Ask("Write privately to the node that spoke last here?\n", gNodes[gMsg[i].from].longName, NULL)) return;
	dm = FindDm(gMsg[i].from, true);
	if (dm == 0xFFFF) { Info("No room for another conversation.", NULL, NULL); return; }
	gCurConv = dm;
	gCat = 1;
	FrmGotoForm(frmChat);
}

static MC_UI void Scroll(Int16 lines)
{
	Int16 v = (Int16) gTop + lines;

	if (v < 0) v = 0;
	if (v > (Int16) MaxTop()) v = (Int16) MaxTop();
	if ((UInt16) v == gTop) return;
	gTop = (UInt16) v;
	ScrollSet();
	PaintList();
}

/* ---- the form -------------------------------------------------------------------------------------------- */

MC_UI Boolean ChatHandleEvent(EventPtr e)
{
	FormPtr frm;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		ConvLabel(gCurConv, gTitle);
		FrmSetTitle(frm, gTitle);
		gConv[gCurConv].unread = 0;
		FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, gadChatBatt), BattGadget);
		FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, gadChatLine), LineGadget);
		FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, gadChatSep), LineGadget);
		FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, gadChatList), ListGadget);
		FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, gadChatCnt), CntGadget);
		StatusLine(gInfo, 100);
		SetFieldPtr(fldChatInfo, gInfo);
		PopFill();
		gSelA = gSelB = -1;
		gShownOk = false;
		StrCopy(gCnt, "000/233");
		gTop = 0;
		Refill(true);
		FrmDrawForm(frm);
		FrmSetFocus(frm, FrmGetObjectIndex(frm, fldChatEntry));
		return true;

	case frmUpdateEvent:
		gConv[gCurConv].unread = 0;
		StatusLine(gInfo, 100);
		SetFieldPtr(fldChatInfo, gInfo);
		Refill(false);
		PaintList();								/* news only redraws the messages, not the whole screen */
		return true;

	case fldChangedEvent:
		Counter(false);
		break;

	case popSelectEvent:
		if (e->data.popSelect.listID == lstChatConv) {
			Int16 sel = e->data.popSelect.selection;

			if (sel == 0) { gCat = gConv[gCurConv].isChan ? 0 : 1; FrmGotoForm(frmMain); }
			else if (sel > 0 && sel < 9 && gPopConv[sel] != gCurConv) { gCurConv = gPopConv[sel]; FrmGotoForm(frmChat); }
			return true;
		}
		break;

	case penDownEvent: {
		RectangleType r;

		frm = FrmGetActiveForm();
		FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, gadChatList), &r);
		if (RctPtInRectangle(e->screenX, e->screenY, &r)) {
			UInt16 k = (UInt16) (gTop + (e->screenY - r.topLeft.y) / 11);

			if (k < gNLine && gLine[k].msg != 0xFF) SelectMsg(gLine[k].msg);
			return true;
		}
		break;
	}

	case sclRepeatEvent:
		if (e->data.sclRepeat.scrollBarID == sclChat) {
			gTop = (UInt16) e->data.sclRepeat.newValue;
			PaintList();
		}
		break;

	case keyDownEvent:
		{
			WChar c = e->data.keyDown.chr;

			/* page keys, the 5-way rocker, a thumb wheel or jog dial */
			if (c == pageUpChr || c == vchrPageUp || c == 0x0132 || c == 0x012E || c == 0xF10C) { Scroll(-(LIST_ROWS - 1)); return true; }
			if (c == pageDownChr || c == vchrPageDown || c == 0x0133 || c == 0x012F || c == 0xF10D) { Scroll(LIST_ROWS - 1); return true; }
			if (c == 0x0503) {			/* five-way navigation: up / down in the key code */
				if (e->data.keyDown.keyCode & 0x0001) { Scroll(-(LIST_ROWS - 1)); return true; }	/* up */
				if (e->data.keyDown.keyCode & 0x0002) { Scroll(LIST_ROWS - 1); return true; }	/* down */
			}
		}
		break;

	case menuEvent:
		switch (e->data.menu.itemID) {
		case mniChatInfo:	ChatInfo(); return true;
		case mniChatDm:		ChatDm(); return true;
		case mniChatAll:	SelectAll(); return true;
		case mniChatDetail:
			if (gSelA < 0) Info("Tap a message first.", NULL, NULL);
			else ShowDetail((UInt8) gSelB);
			return true;
		case mniChatClear:	ChatClear(); return true;
		case mniSendAlert:	ChatSend(true); return true;
		case mniSendInfo:	ChatReq(1); return true;
		case mniSendPos:	ChatReq(2); return true;
		case mniSendTele:	ChatReq(3); return true;
		case mniSendTrace:	ChatReq(4); return true;
		case mniSendShare:	ChatReq(5); return true;
		case mniSendWay:	ChatReq(6); return true;
		case mniEditCopy:
			if (gSelA >= 0) { CopySelection(); return true; }
			break;
		}
		return EditMenu(e->data.menu.itemID);

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnChatSend:	ChatSend(false); return true;
		}
		break;

	case frmCloseEvent:
		FldSetTextPtr(GetField(fldChatInfo), NULL);
		break;

	default:
		break;
	}
	return false;
}

/* ---- one message in full ---------------------------------------------------------------------------------- */

static MC_UI void BuildDetail(void)
{
	const Msg *m = &gMsg[gDetail];
	char tmp[48];
	char *p = gDetailText;

	if (m->out) StrCopy(p, "From: Me\n");
	else {
		StrCopy(p, "From: ");
		StrCat(p, gNodes[m->from].longName[0] ? gNodes[m->from].longName : gNodes[m->from].shortName);
		StrCat(p, "\n");
	}
	ConvLabel(m->conv, tmp);
	StrCat(p, m->out ? "To: " : "In: ");
	StrCat(p, tmp);
	StrCat(p, "\nTime: ");
	FmtDateTime(m->when, tmp);
	StrCat(p, tmp);
	StrCat(p, "\n");
	if (m->out) {
		StrCat(p, "Status: ");
		StrCat(p, StatusWords(m->status));
		StrCat(p, "\n");
		if (m->status == 3) {
			StrCat(p, "Reason: ");
			StrCat(p, ErrWords(m->err));
			if (m->err && m->err < 240) {
				StrCat(p, " (error ");
				PutNum(p, m->err);
				StrCat(p, ")");
			}
			StrCat(p, "\n");
		}
	}
	StrCat(p, "\n");
	StrCat(p, MsgText(m));
}

static MC_UI void MsgScroll(void)
{
	FormPtr frm = FrmGetActiveForm();
	FieldPtr fld = GetField(fldMsgText);
	ScrollBarPtr sb = (ScrollBarPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, sclMsg));
	UInt16 pos, th, fh;

	FldGetScrollValues(fld, &pos, &th, &fh);
	SclSetScrollBar(sb, (Int16) pos, 0, th > fh ? (Int16) (th - fh) : 0, (Int16) (fh > 1 ? fh - 1 : 1));
}

MC_UI Boolean MsgHandleEvent(EventPtr e)
{
	FieldPtr fld;

	switch (e->eType) {
	case frmOpenEvent:
		BuildDetail();
		SetFieldPtr(fldMsgText, gDetailText);
		FrmDrawForm(FrmGetActiveForm());
		MsgScroll();
		return true;

	case sclRepeatEvent:
		if (e->data.sclRepeat.scrollBarID == sclMsg) {
			Int16 d = (Int16) (e->data.sclRepeat.newValue - e->data.sclRepeat.value);

			fld = GetField(fldMsgText);
			if (d) FldScrollField(fld, (UInt16) (d < 0 ? -d : d), d < 0 ? winUp : winDown);
			MsgScroll();
		}
		break;

	case keyDownEvent:
		fld = GetField(fldMsgText);
		if (e->data.keyDown.chr == pageUpChr || e->data.keyDown.chr == vchrPageUp) { FldScrollField(fld, 9, winUp); MsgScroll(); return true; }
		if (e->data.keyDown.chr == pageDownChr || e->data.keyDown.chr == vchrPageDown) { FldScrollField(fld, 9, winDown); MsgScroll(); return true; }
		break;

	case ctlSelectEvent:
		if (e->data.ctlSelect.controlID == btnMsgDone) { FrmReturnToForm(0); return true; }
		break;

	case frmCloseEvent:
		FldSetTextPtr(GetField(fldMsgText), NULL);
		break;

	default:
		break;
	}
	return false;
}

/* ---- a position or a waypoint to send ---------------------------------------------------------------------- */

MC_UI Boolean PosHandleEvent(EventPtr e)
{
	FormPtr frm;
	Int32 lat, lon;
	char la[16], lo[16], nm[32];
	Boolean ok;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		FrmSetTitle(frm, gPosKind == 6 ? "Waypoint" : "Share position");
		SetFieldEdit(fldPosLat, gPosLat);
		SetFieldEdit(fldPosLon, gPosLon);
		if (gPosKind == 6) SetFieldEdit(fldPosName, gPosName);
		else {
			FrmHideObject(frm, FrmGetObjectIndex(frm, lblPosName));
			FrmHideObject(frm, FrmGetObjectIndex(frm, fldPosName));
		}
		FrmDrawForm(frm);
		FrmSetFocus(frm, FrmGetObjectIndex(frm, fldPosLat));
		return true;

	case ctlSelectEvent:
		if (e->data.ctlSelect.controlID == btnPosCancel) { FrmReturnToForm(0); return true; }
		if (e->data.ctlSelect.controlID != btnPosSend) break;
		GetFieldEdit(fldPosLat, la, sizeof(la));
		GetFieldEdit(fldPosLon, lo, sizeof(lo));
		if (! ParseDeg(la, &lat) || lat > 900000000L || lat < -900000000L) {
			Info("Latitude: degrees from -90 to 90, like 1.3521", NULL, NULL);
			return true;
		}
		if (! ParseDeg(lo, &lon) || lon > 1800000000L || lon < -1800000000L) {
			Info("Longitude: degrees from -180 to 180, like 103.8198", NULL, NULL);
			return true;
		}
		nm[0] = '\0';
		if (gPosKind == 6) {
			GetFieldEdit(fldPosName, nm, sizeof(nm));
			StrCopy(gPosName, nm);
		}
		StrCopy(gPosLat, la);
		StrCopy(gPosLon, lo);
		ok = LinkReq(gCurConv, gPosKind, lat, lon, nm);
		SortConv();
		FrmReturnToForm(0);
		if (! ok) Info(gLinkText[0] ? gLinkText : "It was not sent.", NULL, NULL);
		gSelA = gSelB = -1;
		FrmUpdateForm(frmChat, updRefresh);
		return true;

	default:
		break;
	}
	return false;
}
