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
#define CL_NEW		8				/* the "New" mark: on its own line, or (with CL_HEAD) in the date heading above the first unread message */

typedef struct {
	UInt8	msg;					/* index in gMsg, 0xFF for a date heading or the New line (then off is the message it belongs to, len the unread count) */
	UInt8	len;
	UInt16	off;					/* into the message's text; for a heading, the message it dates */
	UInt8	flags;
	UInt8	pad;
} CLine;

static CLine gLine[CL_MAX];
static UInt16 gNLine, gTop;
static Int16 gSelA = -1, gSelB = -1;	/* the selected messages (gMsg indexes), -1 none */
static char gTitle[40], gTitleShown[40], gInfo[44], gCnt[12];		/* gTitle: the name in full; gTitleShown: what the title bar has (cut to fit) */

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
static char gDetailText[860];

/* ---- a message's pieces ------------------------------------------------------------------------ */

static MC_UI void Prefix(const Msg *m, char *out)
{
	if (m->out) StrCopy(out, "Me: ");
	else {
		if (m->from >= gNNodes) StrCopy(out, "?");
		else if (gNodes[m->from].shortName[0] && StrCompare(gNodes[m->from].shortName, "?") != 0) {
			StrNCopy(out, gNodes[m->from].shortName, sizeof(gNodes[0].shortName) - 1);
			out[sizeof(gNodes[0].shortName) - 1] = '\0';
		}
		else NodeTail(m->from, out);		/* not named yet (or saved so): the end of the node id */
		out[7] = '\0';
		StrCat(out, ": ");
	}
}

/* What shows beside the time of an outgoing message (0x18 the ellipsis, 0x16 the Symbol font's tick, the others bold):
 * ...  sending, or the keys are being exchanged   ?  sent, no confirmation yet   ~  a DM only a relay confirmed
 * tick delivered (to the recipient, or to the mesh for a channel)   !  failed. */
#define MK_DOTS		0x18
#define MK_TICK		0x16
#define MK_KEYS		0x14			/* exchanging keys (the normal font) */

static MC_UI char Mark(const Msg *m)
{
	if (! m->out) return 0;
	switch (m->status) {
	case 0:		return MK_DOTS;
	case 6:		return MK_KEYS;
	case 2:		return MK_TICK;
	case 3:		return '!';
	case 4:		return '~';
	}
	return '?';
}

static MC_UI FontID MarkFont(char mk)
{
	return mk == MK_TICK ? symbolFont : mk == MK_KEYS ? stdFont : boldFont;		/* the ellipsis is in the normal fonts, the tick in the Symbol font */
}

/* The room kept for "x 12:59 pm" at the right of a message's last line (the widest time the formats give). */
static MC_UI Int16 TimeW(const Msg *m)
{
	Int16 w;
	char mk = Mark(m);

	FntSetFont(stdFont);
	w = FntCharsWidth("12:59 pm", 8);
	if (mk) {
		FntSetFont(MarkFont(mk));
		w = (Int16) (w + FntCharsWidth(&mk, 1) + 3);
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

/* ---- what is new --------------------------------------------------------------------------------------- */

/* Conv.unread counts the newest incoming messages of a thread that were not read yet. They are read in order (see ChatDwell), so the
 * unread ones are always the newest ones and no per-message flag is kept (the saved data keeps its format).
 * The "New" mark sits before the first message that was unread when the thread was opened. It stays there while the thread is open
 * (messages read meanwhile do not move it), its count is what is still unread, and it goes once nothing is. */
static Boolean gDivOn;					/* the thread was opened with unread messages: show the mark */
static UInt16 gReadNow;					/* messages read since the thread was opened */
static UInt16 gDivMsg = 0xFFFF;			/* gMsg index the mark is before */
static UInt8 gDivCnt;					/* the count it shows */
static UInt16 gDivLine = 0xFFFF;		/* its line, when laid out */
static UInt8 gDwellMsg = 0xFF;			/* the first unread message while it is waited on */
static UInt32 gDwellAt;
static UInt16 gDwellTop, gDwellLines;

/* The gMsg index of the n-th newest incoming message of this thread (the oldest one there is if n is more than there are), 0xFFFF if none. */
static MC_UI UInt16 NthNewest(UInt16 n)
{
	UInt16 i, found = 0xFFFF;

	for (i = gNMsg; i > 0 && n; i--) {
		if (gMsg[i - 1].conv != gCurConv || gMsg[i - 1].out) continue;
		found = (UInt16) (i - 1);
		n--;
	}
	return found;
}

static MC_UI void DivFind(void)
{
	UInt16 k = (UInt16) (gConv[gCurConv].unread + gReadNow);

	gDivMsg = 0xFFFF;
	if (! gDivOn || gConv[gCurConv].unread == 0) return;		/* the mark goes once everything is read */
	gDivMsg = NthNewest(k);
	k = gConv[gCurConv].unread;			/* the count is what is still unread: it counts down while reading */
	gDivCnt = k > 255 ? 255 : (UInt8) k;
}

/* The lines of the conversation, leaving out the first `skip` messages. False if they do not all fit. */
static MC_UI Boolean Layout(UInt16 skip)
{
	UInt16 i;
	char pre[12];
	Int32 lastDay = -1, day;
	Boolean ok = true;

	gNLine = 0;
	gDivLine = 0xFFFF;
	for (i = 0; i < gNMsg && ok; i++) {
		const Msg *m = &gMsg[i];
		const char *text;
		UInt16 off = 0, n, shown, off2;
		Int16 pw, tw, lw;
		Boolean first = true, mk;

		if (m->conv != gCurConv) continue;
		if (skip) { skip--; continue; }
		text = MsgText(m);
		day = DayOf(m->when);
		mk = (Boolean) (gDivMsg != 0xFFFF && i >= gDivMsg && gDivLine == 0xFFFF);	/* the mark's message, or the first one laid out when it is older than those */
		if (mk) gDivLine = gNLine;
		if (day != lastDay) {
			lastDay = day;
			ok = Add(0xFF, i, mk ? gDivCnt : 0, (UInt8) (CL_HEAD | (mk ? CL_NEW : 0)));	/* the mark shares the heading when the day changes here */
			if (! ok) break;
		}
		else if (mk) {
			ok = Add(0xFF, i, gDivCnt, CL_NEW);
			if (! ok) break;
		}
		Prefix(m, pre);
		FntSetFont(boldFont);
		pw = NameWidth(pre, (Int16) StrLen(pre));
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

/* Only the newest messages are laid out: the screen is small, and wrapping a whole long thread is slow. gSkip is how many of the
 * thread's oldest messages are left out; scrolling to the top lays out a few more (MoreOlder). */
#define WIN_MSGS	6				/* what a thread opens with: enough to fill the screen twice, unless the messages are short (OpenWindow) */
#define MORE_MSGS	12				/* what scrolling to the top adds */
#define WIN_LINES	(LIST_ROWS * 2)
#define WIN_NEW		30				/* the most messages a thread opens with to reach its first unread one */
static UInt16 gSkip;

static MC_UI UInt16 ThreadCount(void)
{
	UInt16 i, n = 0;

	for (i = 0; i < gNMsg; i++) if (gMsg[i].conv == gCurConv) n++;
	return n;
}

static MC_UI void BuildLines(void)
{
	UInt16 n = ThreadCount();

	DivFind();
	if (gSkip > n) gSkip = n;
	while (! Layout(gSkip) && gSkip < n) gSkip = (UInt16) (gSkip + (n - gSkip + 1) / 2);	/* a window too long for the table: fewer */
}

static MC_UI UInt16 MaxTop(void)
{
	return gNLine > LIST_ROWS ? (UInt16) (gNLine - LIST_ROWS) : 0;
}

static MC_UI void ScrollSet(void)
{
	FormPtr frm = FrmGetActiveForm();
	ScrollBarPtr sb = (ScrollBarPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, sclChat));

	/* With older messages still to load the bar's top is kept one step above the first line, so the user can always scroll up to ask for them. */
	SclSetScrollBar(sb, (Int16) (gTop + (gSkip ? 1 : 0)), 0, (Int16) (MaxTop() + (gSkip ? 1 : 0)), LIST_ROWS);
}

/* A thread is opened with its newest messages only: a few, and more when they do not fill the screen twice (short ones). The rest is laid out when
 * the user scrolls up. Wrapping the text is what takes the time, so the fewer messages, the quicker the thread is there. */
static MC_UI void OpenWindow(void)
{
	UInt16 n = ThreadCount(), tries;

	gSkip = n > WIN_MSGS ? (UInt16) (n - WIN_MSGS) : 0;
	DivFind();
	if (gDivMsg != 0xFFFF) {			/* the window reaches back to the first unread message and one before it (up to WIN_NEW messages) */
		UInt16 i, before = 0, want;

		for (i = 0; i < gDivMsg; i++) if (gMsg[i].conv == gCurConv) before++;
		want = (UInt16) (n - before + 1);
		if (want > WIN_NEW) want = WIN_NEW;
		if (n > want && (UInt16) (n - want) < gSkip) gSkip = (UInt16) (n - want);
		else if (n <= want) gSkip = 0;
	}
	for (tries = 0; tries < 8; tries++) {
		BuildLines();
		if (gSkip == 0 || gNLine >= WIN_LINES) break;
		gSkip = gSkip > WIN_MSGS ? (UInt16) (gSkip - WIN_MSGS) : 0;
	}
	gTop = MaxTop();
	if (gDivLine != 0xFFFF && gDivLine < gTop) gTop = gDivLine;		/* the mark at the top: read down from there */
	ScrollSet();
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
	char buf[44], pre[12];
	Int16 w;
	RectangleType r;

	if (l->flags & (CL_HEAD | CL_NEW)) {
		Coord x;

		if (l->flags & CL_HEAD) FmtDay(gMsg[l->off].when, buf);
		else buf[0] = '\0';
		if (l->flags & CL_NEW) {
			if (buf[0]) StrCat(buf, " - ");
			StrCat(buf, "New (");
			StrIToA(buf + StrLen(buf), l->len);
			StrCat(buf, ")");
		}
		FntSetFont(boldFont);
		w = FntCharsWidth(buf, (Int16) StrLen(buf));
		x = (Coord) (x0 + (152 - w) / 2);
		WinDrawChars(buf, (Int16) StrLen(buf), x, y);
		FntSetFont(stdFont);
		if (l->flags & CL_HEAD)										/* a date heading (with or without the mark): the rule under it */
			WinDrawLine(x0, (Coord) (y + 10), (Coord) (x0 + 151), (Coord) (y + 10));
		else {														/* the mark alone: two rules either side of it, at half height */
			WinDrawLine(x0, (Coord) (y + 5), (Coord) (x - 4), (Coord) (y + 5));
			WinDrawLine(x0, (Coord) (y + 6), (Coord) (x - 4), (Coord) (y + 6));
			WinDrawLine((Coord) (x + w + 3), (Coord) (y + 5), (Coord) (x0 + 151), (Coord) (y + 5));
			WinDrawLine((Coord) (x + w + 3), (Coord) (y + 6), (Coord) (x0 + 151), (Coord) (y + 6));
		}
		return;
	}
	{
		const Msg *m = &gMsg[l->msg];
		Coord x = (Coord) (x0 + 2);

		FntSetFont(stdFont);
		if (l->flags & CL_FIRST) {
			Prefix(m, pre);
			FntSetFont(boldFont);
			DrawName(pre, (Int16) StrLen(pre), x, y, 100);
			x = (Coord) (x + NameWidth(pre, (Int16) StrLen(pre)));
			FntSetFont(stdFont);
		}
		if (l->len) WinDrawChars(MsgText(m) + l->off, l->len, x, y);
		if (l->flags & CL_LAST) {
			char mk = Mark(m);

			FmtClock(m->when, buf);
			w = FntCharsWidth(buf, (Int16) StrLen(buf));
			WinDrawChars(buf, (Int16) StrLen(buf), (Coord) (x0 + 150 - w), y);
			if (mk) {
				FntSetFont(MarkFont(mk));
				WinDrawChars(&mk, 1, (Coord) (x0 + 150 - w - 3 - FntCharsWidth(&mk, 1)), (Coord) (mk == MK_TICK ? y + 1 : y));
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
	RectangleType r, rows, strip, line, vac, oldClip;
	FormPtr frm = FrmGetActiveForm();
	Shown now[LIST_ROWS], old[LIST_ROWS];
	UInt16 k;
	Int16 s, shift = 0;

	FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, gadChatList), &r);
	if (! gShownOk) { DrawList(&r); return; }
	rows = r;
	rows.extent.y = LIST_ROWS * 11;							/* the lines; the pixels below them belong to no line */
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
			WinScrollRectangle(&rows, shift > 0 ? winUp : winDown, (Coord) ((shift > 0 ? shift : -shift) * 11), &vac);	/* not the strip below: it would carry pixels of the lines into it */
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
	strip = r;
	strip.topLeft.y = (Coord) (r.topLeft.y + LIST_ROWS * 11);
	strip.extent.y = (Coord) (r.extent.y - LIST_ROWS * 11);
	WinEraseRectangle(&strip, 0);								/* whatever got there stays out of sight */
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
		FlatName(gPopLbl[n]);
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
	if (! gWirsaUp) { Info("Not connected. Connect first.", NULL, NULL); return; }
	if (! LinkSend(gCurConv, text))
		Info(gLinkText[0] ? gLinkText : "The message was not sent.", NULL, NULL);
	gReadNow = (UInt16) (gReadNow + gConv[gCurConv].unread);		/* one who answers has read the thread (the mark goes) */
	gConv[gCurConv].unread = 0;
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
	if (! gWirsaUp) { Info("Not connected. Connect first.", NULL, NULL); return; }
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

/* Chat > Clear messages: the same box as the main screen's, for this conversation only (the Palm's copy: WiRSa keeps its own). */
static MC_UI void ChatClear(void)
{
	gClearConv = gCurConv;
	gSelA = gSelB = -1;
	FrmPopupForm(frmClear);
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

/* The top is reached with older messages left out: lay out another few and keep the screen where it was. */
static MC_UI void MoreOlder(void)
{
	UInt16 before = gNLine, was = gSkip;

	if (! gSkip) return;
	gSkip = gSkip > MORE_MSGS ? (UInt16) (gSkip - MORE_MSGS) : 0;
	BuildLines();
	if (gSkip == was) return;
	gTop = (UInt16) (gTop + gNLine - before);
	gShownOk = false;
}

static MC_UI void Scroll(Int16 lines)
{
	Int16 v = (Int16) gTop + lines;

	if (v < 0 && gSkip) { MoreOlder(); v = (Int16) gTop + lines; }
	if (v < 0) v = 0;
	if (v > (Int16) MaxTop()) v = (Int16) MaxTop();
	if ((UInt16) v == gTop) return;
	gTop = (UInt16) v;
	ScrollSet();
	PaintList();
}

/* ---- reading ---------------------------------------------------------------------------------------------- */

static MC_UI Boolean WhollyShown(UInt16 msg)
{
	UInt16 k, a = 0xFFFF, b = 0;

	for (k = 0; k < gNLine; k++) {
		if (gLine[k].msg != msg) continue;
		if (a == 0xFFFF) a = k;
		b = k;
	}
	return a != 0xFFFF && a >= gTop && b < (UInt16) (gTop + LIST_ROWS);
}

/* Scrolled past: all of the message's lines are above the screen, or it is older than the messages laid out (a thread opens with its
 * newest WIN_NEW at most, at the oldest of those when there are more unread). */
static MC_UI Boolean ScrolledPast(UInt16 msg)
{
	UInt16 k, b = 0xFFFF;

	for (k = 0; k < gNLine; k++) if (gLine[k].msg == msg) b = k;
	return b != 0xFFFF ? b < gTop : (Boolean) (gSkip != 0);
}

static MC_UI Boolean Seen(UInt16 msg)
{
	return WhollyShown(msg) || ScrolledPast(msg);
}

/* Nothing is unread any more: the New mark is taken out. A mark on its own line above the screen moves what is shown up by
 * a line, so the place is kept. */
static MC_UI void DivDrop(void)
{
	if (gDivLine != 0xFFFF && gDivLine < gTop && ! (gLine[gDivLine].flags & CL_HEAD)) gTop--;
	gDivOn = false;
	Rebuild(false);
}

/* The thread is opened (or switched to): which messages are new is decided now. */
static MC_UI void NewStart(void)
{
	gReadNow = 0;
	gDivOn = gConv[gCurConv].unread > 0;
	gDwellMsg = 0xFF;
}

MC_UI Boolean ChatWantsTick(void)
{
	return gConv[gCurConv].unread > 0;
}

/* The unread messages that are wholly on the screen, or were scrolled past, are read once the screen has stood still for a second. They
 * are taken in order, from the first unread one. The New mark counts down and goes when none is left. Called while idle. */
MC_UI void ChatDwell(void)
{
	UInt16 u = gConv[gCurConv].unread, f, got = 0;

	if (! u) { gDwellMsg = 0xFF; return; }
	f = NthNewest(u);
	if (f == 0xFFFF) { gConv[gCurConv].unread = 0; return; }		/* none of them is held any more */
	if (! Seen(f)) { gDwellMsg = 0xFF; return; }
	if (gDwellMsg != (UInt8) f || gDwellTop != gTop || gDwellLines != gNLine) {		/* a new first message, or the screen moved: the second starts again */
		gDwellMsg = (UInt8) f; gDwellTop = gTop; gDwellLines = gNLine;
		gDwellAt = TimGetTicks();
		return;
	}
	if (TimGetTicks() - gDwellAt < SysTicksPerSecond()) return;
	while (gConv[gCurConv].unread > 0) {
		f = NthNewest(gConv[gCurConv].unread);
		if (f == 0xFFFF || ! Seen(f)) break;
		gConv[gCurConv].unread--;
		gReadNow++;
		got++;
	}
	gDwellMsg = 0xFF;
	if (! got) return;
	if (gDivOn && gConv[gCurConv].unread == 0) DivDrop();
	else if (gDivLine != 0xFFFF) {						/* the mark's count: a changed line is drawn again */
		gDivCnt = gConv[gCurConv].unread > 255 ? 255 : (UInt8) gConv[gCurConv].unread;
		gLine[gDivLine].len = gDivCnt;
		PaintList();
	}
	StatusLine(gInfo, STATUS_PX);
	SetFieldPtr(fldChatInfo, gInfo);
	BattRedraw(gadChatBatt);							/* the unread note may be gone */
}

/* Another conversation from the pop-up: the form stays, only its title, list and lines change (reloading the form and drawing all of it again is the slow part). */
static MC_UI void SwitchConv(UInt16 conv)
{
	FormPtr frm = FrmGetActiveForm();
	FieldPtr fld = GetField(fldChatEntry);
	UInt16 len = FldGetTextLength(fld);

	gCurConv = conv;
	ConvLabel(gCurConv, gTitle);
	FlatName(gTitle);
	FitTitle(frm, popChatConv, gTitle, gTitleShown, sizeof(gTitleShown));
	RetitleForm(frm, popChatConv, gTitleShown);
	NewStart();
	PopFill();
	if (! gConv[gCurConv].isChan) LinkAskKey(gConv[gCurConv].index);
	if (len) FldDelete(fld, 0, len);						/* a typed text does not go to the other thread */
	Counter(true);
	StatusLine(gInfo, STATUS_PX);
	SetFieldPtr(fldChatInfo, gInfo);
	BattRedraw(gadChatBatt);
	gSelA = gSelB = -1;
	gShownOk = false;
	gTop = 0;
	OpenWindow();
	PaintList();
}

/* ---- the form -------------------------------------------------------------------------------------------- */

/* Chat > Mark all read: the open conversation's messages count as read and its "New" mark goes. */
static MC_UI void ChatMarkRead(void)
{
	if (! gConv[gCurConv].unread) { Info("Nothing here is unread.", NULL, NULL); return; }
	if (! Ask("Mark all messages in this conversation as read?", NULL, NULL)) return;
	gConv[gCurConv].unread = 0;
	gDwellMsg = 0xFF;
	if (gDivOn) DivDrop(); else Rebuild(false);
	StatusLine(gInfo, STATUS_PX);
	SetFieldPtr(fldChatInfo, gInfo);
	BattRedraw(gadChatBatt);
}

MC_UI Boolean ChatHandleEvent(EventPtr e)
{
	FormPtr frm;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		ConvLabel(gCurConv, gTitle);
		FlatName(gTitle);
		FitTitle(frm, popChatConv, gTitle, gTitleShown, sizeof(gTitleShown));
		FrmSetTitle(frm, gTitleShown);
		NewStart();
		FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, gadChatBatt), BattGadget);
		FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, gadChatLine), LineGadget);
		FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, gadChatSep), LineGadget);
		FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, gadChatList), ListGadget);
		FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, gadChatCnt), CntGadget);
		StatusLine(gInfo, STATUS_PX);
		SetFieldPtr(fldChatInfo, gInfo);
		PopFill();
		if (! gConv[gCurConv].isChan) LinkAskKey(gConv[gCurConv].index);		/* a node whose key is not known is asked for it, quietly */
		gSelA = gSelB = -1;
		gShownOk = false;
		StrCopy(gCnt, "000/233");
		gTop = 0;
		OpenWindow();
		FrmDrawForm(frm);
		FrmSetFocus(frm, FrmGetObjectIndex(frm, fldChatEntry));
		return true;

	case frmUpdateEvent:
		StatusLine(gInfo, STATUS_PX);
		SetFieldPtr(fldChatInfo, gInfo);
		BattRedraw(gadChatBatt);					/* the unread note may have come or gone */
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
			else if (sel > 0 && sel < 9 && gPopConv[sel] != gCurConv) SwitchConv(gPopConv[sel]);
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
			Int16 nv = (Int16) (e->data.sclRepeat.newValue - (gSkip ? 1 : 0));

			if (nv < 0) { gTop = 0; MoreOlder(); ScrollSet(); }
			else gTop = (UInt16) nv;
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
		case mniChatRead:	ChatMarkRead(); return true;
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

static char gDetailHead[200];
static Coord gMsgRuleY;

/* The header (who, when, how it went) is a field of its own, then a line, then the scrolling part: the reason it failed, and the text. */
static MC_UI void BuildDetail(void)
{
	const Msg *m = &gMsg[gDetail];
	char tmp[48];
	char *p = gDetailHead;

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
	if (m->out) {
		StrCat(p, "\nStatus: ");
		StrCat(p, StatusWords(m));
	}
	FlatName(p);
	p = gDetailText;
	p[0] = '\0';
	if (m->out && m->status == 3) {
		StrCat(p, "Error: ");
		StrCat(p, ErrTitle(m->err));
		if (m->err && m->err < 240) {
			StrCat(p, " (");
			PutNum(p, m->err);
			StrCat(p, ")");
		}
		StrCat(p, "\nMeaning: ");
		StrCat(p, ErrMeaning(m->err));
		StrCat(p, "\nWhat to do: ");
		StrCat(p, ErrAction(m->err));
		StrCat(p, "\n\n");
	}
	StrCat(p, MsgText(m));
	FlatName(p);
}

/* The fields follow the header's height: three lines for a received message, four for one sent. The text ends 8 px above Done (139). */
static MC_UI void MsgLayout(void)
{
	FormPtr frm = FrmGetActiveForm();
	RectangleType r;
	Coord headH = (Coord) ((gMsg[gDetail].out ? 4 : 3) * 11), top = (Coord) (17 + headH + 4);

	FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, fldMsgHead), &r);
	r.extent.y = headH;
	FrmSetObjectBounds(frm, FrmGetObjectIndex(frm, fldMsgHead), &r);
	FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, fldMsgText), &r);
	r.topLeft.y = top; r.extent.y = (Coord) (131 - top);
	FrmSetObjectBounds(frm, FrmGetObjectIndex(frm, fldMsgText), &r);
	FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, sclMsg), &r);
	r.topLeft.y = top; r.extent.y = (Coord) (131 - top);
	FrmSetObjectBounds(frm, FrmGetObjectIndex(frm, sclMsg), &r);
	gMsgRuleY = (Coord) (17 + headH + 2);			/* the line sits 2 pixels under the last header line */
}

static MC_UI void MsgRule(void)
{
	WinDrawLine(4, gMsgRuleY, 141, gMsgRuleY);
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
		SetFieldPtr(fldMsgHead, gDetailHead);
		SetFieldPtr(fldMsgText, gDetailText);
		MsgLayout();
		FrmDrawForm(FrmGetActiveForm());
		MsgRule();
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
		FldSetTextPtr(GetField(fldMsgHead), NULL);
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
