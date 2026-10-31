/*
 * Views.c - the main screen (channels, direct messages or nodes in one table, picked from a popup in the
 * title bar, the way Address35 does its categories) and the node card. Second code segment (MC_UI): no
 * 32-bit multiply/divide and no const tables in here.
 */
#include "MeshChat.h"

UInt8 gCat;							/* 0 channels, 1 direct messages, 2 nodes */
UInt16 gNodeView[MAX_NODE];			/* the nodes shown, favourites first, then by name */
UInt16 gNNodeView;

#define MAIN_ROWS		11
#define NODE_ROWS		10			/* the Nodes list leaves room for the Find row */

static char gTitle[40], gInfo[44], gCatLabel[16], gNodeTitle[40];
static char gFindShown[32];
static UInt16 gView[MAX_CONV];		/* conversations of the category */
static UInt16 gNView;
static UInt16 gTop;					/* first item shown */

/* ---- a line under the status row ------------------------------------------------------------ */

MC_UI Boolean LineGadget(FormGadgetType *g, UInt16 cmd, void *param)
{
	if (cmd == formGadgetDrawCmd)
		WinDrawLine(g->rect.topLeft.x, g->rect.topLeft.y, (Coord) (g->rect.topLeft.x + g->rect.extent.x - 1), g->rect.topLeft.y);
	return true;
}

/* ---- what the table lists ------------------------------------------------------------------- */

static MC_UI Char Low(Char c)
{
	return (c >= 'A' && c <= 'Z') ? (Char) (c + ('a' - 'A')) : c;
}

static MC_UI Boolean Contains(const char *hay, const char *needle)
{
	UInt16 i, j;

	if (! needle[0]) return true;
	for (i = 0; hay[i]; i++) {
		for (j = 0; needle[j] && hay[i + j] && Low(hay[i + j]) == Low(needle[j]); j++) ;
		if (! needle[j]) return true;
	}
	return false;
}

static MC_UI Int16 NodeCmp(UInt16 a, UInt16 b)
{
	if (gNodes[a].fav != gNodes[b].fav) return gNodes[a].fav ? -1 : 1;
	return StrCaselessCompare(gNodes[a].longName, gNodes[b].longName);
}

static MC_UI void BuildView(void)
{
	UInt16 i, j, t;
	char *find = NULL;

	gNView = 0; gNNodeView = 0;
	if (gCat < 2) {
		for (i = 0; i < gNOrder; i++)
			if (gConv[gOrder[i]].isChan == (gCat == 0)) gView[gNView++] = gOrder[i];
		if (gCat == 0)								/* channels by slot: 0, 1, 2 ... */
			for (i = 1; i < gNView; i++) {
				t = gView[i];
				for (j = i; j > 0 && gConv[gView[j - 1]].index > gConv[t].index; j--) gView[j] = gView[j - 1];
				gView[j] = t;
			}
		return;
	}
	if (FrmGetActiveFormID() == frmMain) find = FldGetTextPtr(GetField(fldMainFind));
	for (i = 1; i < gNNodes; i++)						/* node 0 is this one */
		if (! find || Contains(gNodes[i].longName, find) || Contains(gNodes[i].shortName, find))
			gNodeView[gNNodeView++] = i;
	for (i = 1; i < gNNodeView; i++) {
		t = gNodeView[i];
		for (j = i; j > 0 && NodeCmp(gNodeView[j - 1], t) > 0; j--) gNodeView[j] = gNodeView[j - 1];
		gNodeView[j] = t;
	}
}

static MC_UI UInt16 Total(void)
{
	return gCat == 2 ? gNNodeView : gNView;
}

static MC_UI UInt16 Visible(void)
{
	return gCat == 2 ? NODE_ROWS : MAIN_ROWS;
}

static MC_UI TableType *MainTable(void)
{
	FormPtr frm = FrmGetActiveForm();
	return (TableType *) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, tblMain));
}

/* ---- drawing the rows ----------------------------------------------------------------------- */

static MC_UI void DrawMainRow(void *table, Int16 row, Int16 column, RectangleType *b)
{
	UInt16 k = (UInt16) (gTop + row);
	char name[56], t[32], cnt[8];
	Int16 right = (Int16) (b->topLeft.x + b->extent.x - 2), tw, nameW;
	Boolean bold;
	FontID old;

	if (k >= Total()) return;
	t[0] = '\0'; cnt[0] = '\0';
	if (gCat == 2) {
		const Node *n = &gNodes[gNodeView[k]];

		StrCopy(name, n->longName);
		bold = n->fav;
		{	/* the time text is worked out once for a node and day, not on every redraw */
			static Stamp sHeard[MAX_NODE];
			static Int32 sDay = -1;
			static char sTxt[MAX_NODE][14];
			static Boolean sOk[MAX_NODE];
			UInt16 ni = gNodeView[k];
			Int32 today = DayOf(NowUtc());

			if (today != sDay) { sDay = today; MemSet(sOk, sizeof(sOk), 0); }
			if (! sOk[ni] || sHeard[ni] != n->heard) {
				FmtListTime(n->heard, t);
				StrNCopy(sTxt[ni], t, 13);
				sTxt[ni][13] = '\0';
				sHeard[ni] = n->heard; sOk[ni] = true;
			}
			StrCopy(t, sTxt[ni]);
		}
	} else {
		const Conv *v = &gConv[gView[k]];

		if (v->isChan) {
			name[0] = '\0';
			PutNum(name, v->index);
			StrCat(name, ": ");
			ConvLabel(gView[k], name + StrLen(name));
		} else {
			ConvLabel(gView[k], name);
		}
		bold = v->unread != 0;
		FmtListTime(v->last, t);
		if (v->unread) {
			StrCopy(cnt, "(");
			PutNum(cnt, v->unread);
			StrCat(cnt, ")");
		}
	}
	old = FntSetFont(bold ? boldFont : stdFont);		/* a row with news is bold all through: name, count and time */
	tw = FntCharsWidth(t, (Int16) StrLen(t));
	if (t[0]) WinDrawChars(t, (Int16) StrLen(t), (Coord) (right - tw), b->topLeft.y);
	if (cnt[0]) {
		StrCat(name, " - ");
		StrCat(name, cnt);						/* the count sits beside the name */
	}
	nameW = (Int16) (right - tw - (b->topLeft.x + 2) - 4);
	WinDrawTruncChars(name, (Int16) StrLen(name), (Coord) (b->topLeft.x + 2), b->topLeft.y, nameW);
	FntSetFont(old);
}

static MC_UI void TableLoad(void)
{
	TableType *t = MainTable();
	UInt16 r, vis = Visible(), total = Total();
	FontID o = FntSetFont(stdFont);
	UInt16 lh = FntLineHeight();

	FntSetFont(o);
	for (r = 0; r < MAIN_ROWS; r++) {
		if (r < vis && gTop + r < total) {
			TblSetItemStyle(t, r, 0, customTableItem);
			TblSetRowHeight(t, r, lh);
			TblSetRowUsable(t, r, true);
			TblMarkRowInvalid(t, r);
		} else {
			TblSetRowUsable(t, r, false);
		}
	}
	TblSetColumnUsable(t, 0, true);
}

static MC_UI void ScrollSet(void)
{
	FormPtr frm = FrmGetActiveForm();
	ScrollBarPtr sb = (ScrollBarPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, sclMain));
	UInt16 total = Total(), vis = Visible();

	SclSetScrollBar(sb, (Int16) gTop, 0, (Int16) (total > vis ? total - vis : 0), (Int16) vis);
}

static MC_UI void NoteShown(void);

static MC_UI void TableRedraw(void)
{
	RectangleType r;
	FormPtr frm = FrmGetActiveForm();

	FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, tblMain), &r);
	r.extent.y = (Coord) (Visible() * 11);			/* not the Find row below the Nodes list */
	WinEraseRectangle(&r, 0);
	TableLoad();
	ScrollSet();
	TblRedrawTable(MainTable());
	NoteShown();
}

/* Which node each row shows, so a search redraws only the rows that changed. */
static UInt16 gShownNode[MAIN_ROWS];

static MC_UI void NoteShown(void)
{
	UInt16 r, k;

	for (r = 0; r < MAIN_ROWS; r++) {
		k = (UInt16) (gTop + r);
		gShownNode[r] = (gCat == 2 && r < NODE_ROWS && k < gNNodeView) ? gNodeView[k] : 0xFFFF;
	}
}

static MC_UI void FindRedraw(void)
{
	TableType *t = MainTable();
	UInt16 r, k, id;
	RectangleType b;

	TableLoad();
	ScrollSet();
	for (r = 0; r < NODE_ROWS; r++) {
		k = (UInt16) (gTop + r);
		id = k < gNNodeView ? gNodeView[k] : 0xFFFF;
		if (id == gShownNode[r]) continue;
		TblGetItemBounds(t, r, 0, &b);
		WinEraseRectangle(&b, 0);
		if (id != 0xFFFF) DrawMainRow(t, (Int16) r, 0, &b);
		gShownNode[r] = id;
	}
}

/* Moves the list to a new top: the rows that stay are moved as bits, only the ones that come in are drawn. */
static MC_UI void ScrollTo(UInt16 nt)
{
	Int16 d = (Int16) nt - (Int16) gTop, a = d < 0 ? (Int16) -d : d;
	UInt16 vis = Visible(), k, row;
	RectangleType r, vac, b;
	TableType *t = MainTable();

	if (d == 0) return;
	gTop = nt;
	if (a >= (Int16) vis || Total() < vis) {
		TableLoad();
		TblRedrawTable(t);
		NoteShown();
		return;
	}
	FrmGetObjectBounds(FrmGetActiveForm(), FrmGetObjectIndex(FrmGetActiveForm(), tblMain), &r);
	r.extent.y = (Coord) (vis * 11);
	WinScrollRectangle(&r, d > 0 ? winUp : winDown, (Coord) (a * 11), &vac);
	for (k = 0; k < (UInt16) a; k++) {
		row = d > 0 ? vis - a + k : k;
		TblGetItemBounds(t, row, 0, &b);
		WinEraseRectangle(&b, 0);
		DrawMainRow(t, (Int16) row, 0, &b);
	}
	NoteShown();
}

static MC_UI void ShowOne(UInt16 id, Boolean on)
{
	FormPtr frm = FrmGetActiveForm();
	UInt16 ix = FrmGetObjectIndex(frm, id);

	if (on) FrmShowObject(frm, ix); else FrmHideObject(frm, ix);
}

/* The Nodes list shares the screen with the Find row: its scrollbar is 5 pixels shorter there. */
static MC_UI void ShowFind(Boolean on)
{
	FormPtr frm = FrmGetActiveForm();
	UInt16 ix = FrmGetObjectIndex(frm, sclMain);
	RectangleType r;

	ShowOne(lblMainFind, on);
	ShowOne(fldMainFind, on);
	ShowOne(btnMainClear, on);
	FrmGetObjectBounds(frm, ix, &r);
	r.extent.y = on ? 112 : 121;
	FrmSetObjectBounds(frm, ix, &r);
	FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, tblMain), &r);		/* the table too, or its first draw covers the Find row */
	r.extent.y = on ? 110 : 121;
	TblSetBounds(MainTable(), &r);
}

/* Everything the form shows, from the data. */
static MC_UI void MainFill(void)
{
	FormPtr frm = FrmGetActiveForm();
	UInt16 total, vis;

	StatusLine(gInfo, 100);
	StrCopy(gTitle, gRelay.node[0] ? gRelay.node : "Meshtastic");
	FrmSetTitle(frm, gTitle);
	SetPopup(popMainCat, lstMainCat, gCat, gCatLabel, sizeof(gCatLabel));
	SetFieldPtr(fldMainInfo, gInfo);
	BuildView();
	total = Total(); vis = Visible();
	if (gTop + vis > total) gTop = total > vis ? (UInt16) (total - vis) : 0;
	TableLoad();
	ScrollSet();
}

static MC_UI void SetCategory(UInt8 cat)
{
	FormPtr frm = FrmGetActiveForm();

	if (cat == gCat) return;
	gCat = cat;
	gTop = 0;
	gFindShown[0] = '\0';
	SetPopup(popMainCat, lstMainCat, gCat, gCatLabel, sizeof(gCatLabel));
	ShowFind(gCat == 2);
	if (gCat == 2) {
		SetFieldEdit(fldMainFind, "");
		FrmSetFocus(frm, FrmGetObjectIndex(frm, fldMainFind));
		gNodesRefresh = true;						/* the list is asked for once it is on screen */
	}
	BuildView();
	TableRedraw();
}

/* Typing in the Find line narrows the nodes list (the field does not post a change event for a key). */
MC_UI void MainFindPoll(void)
{
	char *p;

	if (gCat != 2) return;
	p = FldGetTextPtr(GetField(fldMainFind));
	if (! p) p = "";
	if (StrCompare(p, gFindShown) == 0) return;
	StrNCopy(gFindShown, p, sizeof(gFindShown) - 1);
	gFindShown[sizeof(gFindShown) - 1] = '\0';
	gTop = 0;
	BuildView();
	FindRedraw();
}

static MC_UI void PageBy(Int16 dir)
{
	UInt16 total = Total(), vis = Visible();
	Int16 v = (Int16) gTop + dir * (Int16) vis;

	if (v > (Int16) (total > vis ? total - vis : 0)) v = (Int16) (total > vis ? total - vis : 0);
	if (v < 0) v = 0;
	if ((UInt16) v == gTop) return;
	ScrollTo((UInt16) v);
	ScrollSet();
}

MC_UI Boolean MainHandleEvent(EventPtr e)
{
	FormPtr frm;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, gadMainBatt), BattGadget);
		FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, gadMainLine), LineGadget);
		TblSetCustomDrawProcedure(MainTable(), 0, DrawMainRow);
		gFindShown[0] = '\0';
		MainFill();
		ShowFind(gCat == 2);
		FrmDrawForm(frm);
		NoteShown();
		if (gCat == 2) {
			FrmSetFocus(frm, FrmGetObjectIndex(frm, fldMainFind));
			gNodesRefresh = true;
		}
		return true;

	case frmUpdateEvent:
		MainFill();
		FrmDrawForm(FrmGetActiveForm());
		NoteShown();
		return true;

	case popSelectEvent:
		if (e->data.popSelect.listID == lstMainCat) {
			SetCategory((UInt8) e->data.popSelect.selection);
			return true;
		}
		break;

	case tblSelectEvent:
		if (e->data.tblSelect.tableID == tblMain) {
			UInt16 k = (UInt16) (gTop + e->data.tblSelect.row);

			if (k >= Total()) return true;
			if (gCat == 2) {
				gCurNode = gNodeView[k];
				FrmPopupForm(frmNode);
			} else {
				gCurConv = gView[k];
				FrmGotoForm(frmChat);
			}
			return true;
		}
		break;

	case sclRepeatEvent:
		if (e->data.sclRepeat.scrollBarID == sclMain) {
			ScrollTo((UInt16) e->data.sclRepeat.newValue);
		}
		break;

	case fldChangedEvent:
		MainFindPoll();
		break;

	case keyDownEvent:
		if (e->data.keyDown.chr == pageUpChr || e->data.keyDown.chr == vchrPageUp) { PageBy(-1); return true; }
		if (e->data.keyDown.chr == pageDownChr || e->data.keyDown.chr == vchrPageDown) { PageBy(1); return true; }
		break;

	case ctlSelectEvent:
		if (e->data.ctlSelect.controlID == btnMainClear) {
			SetFieldEdit(fldMainFind, "");
			MainFindPoll();
			return true;
		}
		break;

	case menuEvent:
		switch (e->data.menu.itemID) {
		case mniMainSetup:		OpenSetup(); return true;
		case mniMainPair:		FrmPopupForm(frmRelay); return true;
		case mniMainConnect:	DoConnect(); return true;
		case mniMainDisconnect:	DoDisconnect(); return true;
		case mniMainSync:		DoSync(); return true;
		case mniMainClear:		FrmPopupForm(frmClear); return true;
		case mniMainPrefs:		FrmPopupForm(frmPrefs); return true;
		case mniMainAbout:
			Info("Meshtastic 0.3\nTalks to a Meshtastic node through WiRSa's Mesh mode.", NULL, NULL);
			return true;
		}
		return EditMenu(e->data.menu.itemID);

	case frmCloseEvent:
		FldSetTextPtr(GetField(fldMainInfo), NULL);
		break;

	default:
		break;
	}
	return false;
}

/* ---- the node card ---------------------------------------------------------------------------
 * Built like Address35's card view (RecordForm.c): a TABLE with one custom-drawn column and no border,
 * a scroll bar beside it. Lines are "Label: value"; a few of them do something when tapped. */

#define NI_ROWS		14
#define NI_VIS		9
#define NI_CHATS	12
#define NI_POS		13

static char gNiVal[NI_ROWS][36];
static UInt16 gNiTop;
static Int16 gNiColonX, gNiValueX;

static MC_UI const char *NiLabel(UInt16 i)
{
	switch (i) {
	case 0:		return "Long";
	case 1:		return "Short";
	case 2:		return "ID";
	case 3:		return "Heard";
	case 4:		return "Signal";
	case 5:		return "Battery";
	case 6:		return "Device";
	case 7:		return "Role";
	case 8:		return "Version";
	case 9:		return "Uptime";
	case 10:	return "Util";
	case 11:	return "Air TX";
	case 12:	return "Chats";
	}
	return "Position";
}

static MC_UI void NodeInfoFill(void)
{
	const Node *n = &gNodes[gCurNode];
	UInt16 i, dm, count = 0;
	Int16 maxw = 0, w;

	StrCopy(gNiVal[0], n->longName);
	StrCopy(gNiVal[1], n->shortName[0] && StrCompare(n->shortName, "?") != 0 ? n->shortName : n->id + 5);
	StrCopy(gNiVal[2], n->id);
	if (gCurNode == 0) StrCopy(gNiVal[3], "this node");
	else FmtStampShort(n->heard, gNiVal[3]);
	if (gCurNode == 0) {
		StrCopy(gNiVal[4], "-");
	} else {
		StrCopy(gNiVal[4], "SNR ");
		PutNum(gNiVal[4], n->snr);
		StrCat(gNiVal[4], ", ");
		PutNum(gNiVal[4], n->hops);
		StrCat(gNiVal[4], n->hops == 1 ? " hop" : " hops");
	}
	gNiVal[5][0] = '\0';
	if (n->batt >= 0) { PutNum(gNiVal[5], n->batt); StrCat(gNiVal[5], "%"); } else StrCopy(gNiVal[5], "unknown");
	StrCopy(gNiVal[6], HwName(n->hwNum));
	StrCopy(gNiVal[7], RoleName(n->roleNum));
	StrCopy(gNiVal[8], gCurNode == 0 && gFirmware[0] ? gFirmware : "-");
	FmtUptime(n->uptime, gNiVal[9]);
	FmtPct10(n->util, gNiVal[10]);
	FmtPct10(n->air, gNiVal[11]);
	dm = FindDm(gCurNode, false);
	if (dm != 0xFFFF)
		for (i = 0; i < gNMsg; i++) if (gMsg[i].conv == dm) count++;
	if (gCurNode == 0) {
		StrCopy(gNiVal[12], "-");
	} else if (count) {
		gNiVal[12][0] = '\0';
		PutNum(gNiVal[12], count);
		StrCat(gNiVal[12], count == 1 ? " message" : " messages");
	} else {
		StrCopy(gNiVal[12], "none yet");
	}
	StrCopy(gNiVal[13], n->pos[0] ? n->pos : "none");

	FntSetFont(stdFont);
	for (i = 0; i < NI_ROWS; i++) {
		w = FntCharsWidth(NiLabel(i), (Int16) StrLen(NiLabel(i)));
		if (w > maxw) maxw = w;
	}
	gNiColonX = (Int16) (maxw + 13);		/* the labels are drawn bold: room for them, and two characters more */
	gNiValueX = (Int16) (gNiColonX + 5);
}

static MC_UI Boolean NiTappable(UInt16 i)
{
	if (i == NI_CHATS) return gCurNode != 0;
	if (i == NI_POS) return gNodes[gCurNode].pos[0] != '\0';
	return false;
}

static MC_UI TableType *NiTable(void)
{
	FormPtr frm = FrmGetActiveForm();
	return (TableType *) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, tblNodeInfo));
}

static MC_UI void NiDrawLine(void *table, Int16 row, Int16 column, RectangleType *bounds)
{
	UInt16 i = (UInt16) (gNiTop + row);
	Int16 x = (Int16) (bounds->topLeft.x + 2), y = bounds->topLeft.y;
	Int16 right = (Int16) (bounds->topLeft.x + bounds->extent.x - 2), len, vx;

	if (i >= NI_ROWS) return;
	len = (Int16) StrLen(gNiVal[i]);
	FntSetFont(boldFont);
	WinDrawChars(NiLabel(i), (Int16) StrLen(NiLabel(i)), x, y);
	FntSetFont(stdFont);
	WinDrawChars(":", 1, (Coord) (x + gNiColonX), y);
	vx = (Int16) (x + gNiValueX);
	if (len > 0 && vx < right) {
		Int16 room = (Int16) (right - vx), vw = FntCharsWidth(gNiVal[i], len);

		WinDrawTruncChars(gNiVal[i], len, vx, y, room);
		if (NiTappable(i))							/* a line that does something: underlined */
			WinDrawLine(vx, (Coord) (y + FntLineHeight() - 1), (Coord) (vx + (vw < room ? vw : room)),
				(Coord) (y + FntLineHeight() - 1));
	}
}

static MC_UI void NiLoad(void)
{
	TableType *t = NiTable();
	UInt16 r, lh;
	FontID o = FntSetFont(stdFont);

	lh = FntLineHeight();
	FntSetFont(o);
	for (r = 0; r < NI_VIS; r++) {
		if (gNiTop + r < NI_ROWS) {
			TblSetItemStyle(t, r, 0, customTableItem);
			TblSetRowHeight(t, r, lh);
			TblSetRowUsable(t, r, true);
			TblMarkRowInvalid(t, r);
		} else {
			TblSetRowUsable(t, r, false);
		}
	}
	TblSetColumnUsable(t, 0, true);
}

static MC_UI void NiScroll(void)
{
	FormPtr frm = FrmGetActiveForm();
	ScrollBarPtr sb = (ScrollBarPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, sclNodeInfo));

	SclSetScrollBar(sb, (Int16) gNiTop, 0, NI_ROWS - NI_VIS, NI_VIS);
}

static MC_UI void NiRedraw(void)
{
	NiLoad();
	NiScroll();
	TblRedrawTable(NiTable());
}

static MC_UI void NodeInfoAct(UInt16 row)
{
	UInt16 dm;

	if (row == NI_CHATS && gCurNode != 0) {
		dm = FindDm(gCurNode, true);
		if (dm == 0xFFFF) { Info("No room for another conversation.", NULL, NULL); return; }
		gCurConv = dm;
		gCat = 1;
		FrmGotoForm(frmChat);
	} else if (row == NI_POS) {
		if (gNodes[gCurNode].pos[0])
			Info("Opening this position in a map app is planned.", NULL, NULL);
		else
			Info("This node has not reported a position.", NULL, NULL);
	}
}

MC_UI Boolean NodeHandleEvent(EventPtr e)
{
	FormPtr frm;
	UInt16 dm;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		NodeInfoFill();
		StrCopy(gNodeTitle, gNodes[gCurNode].longName);
		FrmSetTitle(frm, gNodeTitle);
		FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, gadNodeLine), LineGadget);
		gNiTop = 0;
		TblSetCustomDrawProcedure(NiTable(), 0, NiDrawLine);
		TblSetColumnUsable(NiTable(), 0, true);
		NiLoad();
		NiScroll();
		CtlSetValue((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, ckNodeFav)), gNodes[gCurNode].fav ? 1 : 0);
		if (gCurNode == 0) {							/* your own node: no direct message, no favourite */
			FrmHideObject(frm, FrmGetObjectIndex(frm, btnNodeMsg));
			FrmHideObject(frm, FrmGetObjectIndex(frm, ckNodeFav));
		}
		FrmDrawForm(frm);
		return true;

	case tblSelectEvent:
		if (e->data.tblSelect.tableID == tblNodeInfo) {
			UInt16 line = (UInt16) (gNiTop + e->data.tblSelect.row);

			if (line < NI_ROWS && NiTappable(line))
				NodeInfoAct(line);
			return true;
		}
		break;

	case sclRepeatEvent:
		if (e->data.sclRepeat.scrollBarID == sclNodeInfo) {
			gNiTop = (UInt16) e->data.sclRepeat.newValue;
			NiRedraw();
		}
		break;

	case keyDownEvent:
		if (e->data.keyDown.chr == vchrPageUp || e->data.keyDown.chr == vchrPageDown
				|| e->data.keyDown.chr == pageUpChr || e->data.keyDown.chr == pageDownChr) {
			Boolean down = e->data.keyDown.chr == vchrPageDown || e->data.keyDown.chr == pageDownChr;
			Int16 v = (Int16) gNiTop + (down ? NI_VIS : -NI_VIS);

			if (v > NI_ROWS - NI_VIS) v = NI_ROWS - NI_VIS;
			if (v < 0) v = 0;
			if ((UInt16) v != gNiTop) {
				gNiTop = (UInt16) v;
				NiRedraw();
			}
			return true;
		}
		break;

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case ckNodeFav:
			gNodes[gCurNode].fav = e->data.ctlSelect.on != 0;
			if (gWirsaUp && ! LinkFav(gNodes[gCurNode].num, gNodes[gCurNode].fav))
				Info(gLinkText[0] ? gLinkText : "The node was not told.", NULL, NULL);
			CacheSave();
			return true;
		case btnNodeMsg:
			dm = FindDm(gCurNode, true);
			if (dm == 0xFFFF) { Info("No room for another conversation.", NULL, NULL); return true; }
			gCurConv = dm;
			gCat = 1;
			FrmGotoForm(frmChat);
			return true;
		case btnNodeBack:
			FrmReturnToForm(0);
			return true;
		}
		break;

	default:
		break;
	}
	return false;
}
