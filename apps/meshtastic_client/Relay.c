/*
 * Relay.c - the WiRSa side of the link: which node WiRSa talks to (scan and pair), whether it
 * stays awake or wakes now and then, and clearing stored messages. The calls are in Link.c.
 */
#include "MeshChat.h"

/* The nodes in the Paired: list: those this Palm holds data for, and those WiRSa keeps. */
typedef struct { UInt32 id; UInt8 slot, state, flags; char text[36]; } NodeItem;	/* slot 0xFF: only on this Palm; state 1 paired, 2 archived */
static NodeItem gItem[MAX_SLOT];
static UInt16 gNItem;
static char *gItemPtr[MAX_SLOT];
static char gNodeLabel[36];
static char gModeLabel[20], gEveryLabel[10], gFlushLabel[12], gAgeLabel[24];
static char gClearInfo[64];

static const UInt16 kClearDays[5] = { 0, 1, 3, 7, 30 };

/* ---- frmRelay: Pair Device --------------------------------------------------------------------- */

MC_UI static NodeItem *ItemOf(UInt32 id)
{
	UInt16 i;

	for (i = 0; i < gNItem; i++) if (id && gItem[i].id == id) return &gItem[i];
	return NULL;
}

MC_UI static NodeItem *ItemAdd(UInt32 id, const char *name)
{
	NodeItem *it;

	if (gNItem >= MAX_SLOT) return NULL;
	it = &gItem[gNItem++];
	it->id = id; it->slot = 0xFF; it->state = 0; it->flags = 0;
	StrNCopy(it->text, name, 24);
	it->text[24] = '\0';
	return it;
}

/* The list from the databases on this Palm and, when WiRSa is connected, its kept nodes. */
MC_UI static void RelayFill(void)
{
	CacheEntry ent[MAX_SLOT];
	FormPtr frm = FrmGetActiveForm();
	ListPtr lst = (ListPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, lstRelayNode));
	UInt16 i, n, sel = 0;
	NodeItem *it;

	gNItem = 0;
	n = CacheList(ent, MAX_SLOT);
	for (i = 0; i < n; i++) ItemAdd(ent[i].id, ent[i].name);
	for (i = 0; i < gNSlot && gWirsaUp; i++) {
		it = ItemOf(gSlot[i].id);
		if (! it) it = ItemAdd(gSlot[i].id, gSlot[i].name);
		if (! it) continue;
		it->slot = gSlot[i].slot; it->state = gSlot[i].state; it->flags = gSlot[i].flags;
		StrNCopy(it->text, gSlot[i].name, 24);
	}
	if (gNodeId && ! ItemOf(gNodeId)) ItemAdd(gNodeId, gRelay.node);
	if (gNItem == 0) ItemAdd(0, "none");
	for (i = 0; i < gNItem; i++) {
		it = &gItem[i];
		it->text[24] = '\0';
		if (it->flags & 2) StrCat(it->text, " [B]");
		else if (it->state == 2) StrCat(it->text, " (archived)");
		gItemPtr[i] = it->text;
		if (gNodeId && it->id == gNodeId) sel = i;
	}
	LstSetListChoices(lst, gItemPtr, gNItem);
	LstSetSelection(lst, (Int16) sel);
	StrNCopy(gNodeLabel, gItem[sel].text, sizeof(gNodeLabel) - 1);
	gNodeLabel[sizeof(gNodeLabel) - 1] = '\0';
	CtlSetLabel((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, popRelayNode)), gNodeLabel);
}

/* The three settings show the node that is shown. */
MC_UI static void RelayPopups(void)
{
	SetPopup(popRelayMode, lstRelayMode, gRelay.mode, gModeLabel, sizeof(gModeLabel));
	SetPopup(popRelayEvery, lstRelayEvery, gRelay.every, gEveryLabel, sizeof(gEveryLabel));
	SetPopup(popRelayFlush, lstRelayFlush, gRelay.sdFlush, gFlushLabel, sizeof(gFlushLabel));
}

/* Another node is picked: this one's data is saved and that one's shown. When WiRSa is connected and keeps it, WiRSa follows. */
MC_UI static void RelaySwitch(Int16 sel)
{
	NodeItem *it;
	Boolean ok = true, connect = false;

	if (sel < 0 || sel >= (Int16) gNItem) return;
	it = &gItem[sel];
	if (it->id == 0 || it->id == gNodeId) { RelayFill(); return; }
	CacheSave();
	if (gWirsaUp && it->slot != 0xFF) {
		Busy("Switching node...", 0);
		ok = LinkSlotSelect(it->slot);
		BusyDone();
		if (! ok) { Info(gLinkText[0] ? gLinkText : "WiRSa did not switch.", NULL, NULL); RelayFill(); return; }
		connect = it->state == 1;								/* a paired node is linked to; an archive has no link */
	}
	if (! CacheLoadNode(it->id)) { NodeDataClear(); gNodeId = it->id; StrNCopy(gRelay.node, it->text, 24); gRelay.node[24] = '\0'; }
	if (gWirsaUp && it->slot != 0xFF) {
		if (connect) { LinkReconnectSend(); LinkRestarting(); }	/* the Connecting form below waits for the node */
		else { Busy("Updating...", 0); LinkSync(); LinkRelayGet(); BusyDone(); }
	}
	RelayFill();
	RelayPopups();
	FrmDrawForm(FrmGetActiveForm());
	FrmUpdateForm(frmMain, updFull);
	if (connect) ConnectOpen();
}

/* The WiRSa session must be up (Connect): say so if it is not. */
static Boolean NeedWirsa(void)
{
	if (gWirsaUp) return true;
	Info("Not connected. Connect first.", NULL, NULL);
	return false;
}

static void DrawScanRow(Int16 i, RectangleType *b, Char **unused)
{
	char t[16];
	Int16 w;

	WinDrawTruncChars(gScan[i].name, (Int16) StrLen(gScan[i].name), (Coord) (b->topLeft.x + 1), b->topLeft.y, 112);
	t[0] = '\0';
	PutNum(t, gScan[i].dbm);
	w = FntCharsWidth(t, (Int16) StrLen(t));
	WinDrawChars(t, (Int16) StrLen(t), (Coord) (b->topLeft.x + b->extent.x - w - 2), b->topLeft.y);
}

static void ScanListSet(void)
{
	FormPtr frm = FrmGetActiveForm();
	ListPtr lst = (ListPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, lstScan));

	LstSetDrawFunction(lst, DrawScanRow);
	LstSetListChoices(lst, NULL, (Int16) gNScan);
	LstSetSelection(lst, noListSelection);
	if (FrmVisible(frm)) LstDrawList(lst);
}

static void RelayScan(void)
{
	Boolean ok;

	if (! NeedWirsa()) return;
	BusyCancel("Scanning for nodes (about 6 s)...");
	ok = LinkScan();
	BusyDone();
	if (! ok && McCancelled()) return;
	if (! ok) { Info(gLinkText[0] ? gLinkText : "The scan failed.", NULL, NULL); return; }
	ScanListSet();
	if (gNScan == 0) Info("No Meshtastic node found.", "Is its Bluetooth on, and is it", " in range?");
}

static void RelayPair(void)
{
	Int16 sel = GetListSel(lstScan);
	Boolean ok;

	if (sel < 0) { Info("Scan, then pick the node.", NULL, NULL); return; }
	if (! NeedWirsa()) return;
	BusyCancel("Pairing and loading the node. This takes up to a minute. If the node shows a PIN, you will be asked for it.");
	ok = LinkPair(&gScan[sel]);
	BusyDone();
	if (! ok && McCancelled()) return;
	if (! ok) { Info(gLinkText[0] ? gLinkText : "Pairing failed.", NULL, NULL); return; }
	LinkSync();
	LinkSlots();
	LinkRelayGet();
	RelayFill();
	RelayPopups();
	FrmDrawForm(FrmGetActiveForm());
	FrmUpdateForm(frmMain, updRefresh);
}

MC_UI static void RelayUnpair(void)
{
	Boolean ok;

	NodeItem *it = ItemOf(gNodeId);
	Int16 choice;

	if (! it) { Info("No node is paired.", NULL, NULL); return; }
	choice = AskUnpair(it->text);
	if (choice == 2) return;
	if (it->slot != 0xFF) {							/* WiRSa keeps it */
		if (choice == 0 && it->state == 2) { Info("Already kept as an archive.", NULL, NULL); return; }
		if (! NeedWirsa()) return;
		Busy(choice == 0 ? "Keeping the node..." : "Removing the node...", 0);
		ok = LinkUnpair(it->slot, choice == 0);
		BusyDone();
		if (! ok) { Info(gLinkText[0] ? gLinkText : "WiRSa did not take it.", NULL, NULL); return; }
	} else if (choice == 0) {
		Info("Only on this Palm already.", NULL, NULL);
		return;
	}
	if (choice == 1) {							/* its data goes from this Palm too */
		CacheDelete(it->id);
		gNodeId = 0;
		NodeDataClear();
		gRelay.node[0] = '\0';
	}
	else gLinkState = 0;
	if (gWirsaUp) { LinkSlots(); Busy("Updating...", 0); LinkSync(); LinkRelayGet(); BusyDone(); }
	RelayFill();
	RelayPopups();
	FrmDrawForm(FrmGetActiveForm());
	FrmUpdateForm(frmMain, updRefresh);
}

/* Done: mode and interval go to WiRSa if they changed. */
static void RelayDone(void)
{
	UInt8 mode = (UInt8) GetListSel(lstRelayMode), every = (UInt8) GetListSel(lstRelayEvery), flush = (UInt8) GetListSel(lstRelayFlush);
	Boolean ok;

	NodeItem *cur = ItemOf(gNodeId);

	if ((mode != gRelay.mode || every != gRelay.every || flush != gRelay.sdFlush) && gWirsaUp && ! (cur && cur->slot == 0xFF)) {	/* a node WiRSa does not keep has no setting there */
		Busy("Saving to WiRSa...", 0);
		ok = LinkRelaySet(mode, every, flush);
		BusyDone();
		if (! ok) { Info(gLinkText[0] ? gLinkText : "WiRSa did not take it.", NULL, NULL); return; }
		gRelay.mode = mode; gRelay.every = every; gRelay.sdFlush = flush;
	}
	FrmReturnToForm(frmMain);
	FrmUpdateForm(frmMain, updFull);
}

static void RelayClearStore(void)
{
	Boolean ok;

	if (! Ask("Delete the messages stored on WiRSa?\n", "The ones not yet on this Palm are lost.", NULL)) return;
	if (! NeedWirsa()) return;
	Busy("Clearing WiRSa's store...", 0);
	ok = LinkClearWirsa();
	BusyDone();
	if (! ok) { Info(gLinkText[0] ? gLinkText : "WiRSa did not clear it.", NULL, NULL); return; }
	Info("WiRSa's store is empty.", NULL, NULL);
}

Boolean RelayHandleEvent(EventPtr e)
{
	FormPtr frm;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, gadRelayLine), LineGadget);
		if (gWirsaUp) { Busy("Fetching...", 0); LinkRelayGet(); LinkSlots(); BusyDone(); }
		RelayPopups();
		RelayFill();
		ScanListSet();
		FrmDrawForm(frm);
		return true;

	case frmUpdateEvent:
		RelayFill();
		FrmDrawForm(FrmGetActiveForm());
		return true;

	case popSelectEvent:
		if (e->data.popSelect.selection < 0) break;
		if (e->data.popSelect.listID == lstRelayNode) {
			RelaySwitch(e->data.popSelect.selection);
			return true;
		}
		if (e->data.popSelect.listID == lstRelayMode) {
			SetPopup(popRelayMode, lstRelayMode, e->data.popSelect.selection, gModeLabel, sizeof(gModeLabel));
			return true;
		}
		if (e->data.popSelect.listID == lstRelayEvery) {
			SetPopup(popRelayEvery, lstRelayEvery, e->data.popSelect.selection, gEveryLabel, sizeof(gEveryLabel));
			return true;
		}
		if (e->data.popSelect.listID == lstRelayFlush) {
			SetPopup(popRelayFlush, lstRelayFlush, e->data.popSelect.selection, gFlushLabel, sizeof(gFlushLabel));
			return true;
		}
		break;

	case menuEvent:
		if (e->data.menu.itemID == mniRelayClear) { RelayClearStore(); return true; }
		break;

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnRelayScan:	RelayScan(); return true;
		case btnScanPair:	RelayPair(); return true;
		case btnRelayUnpair:	RelayUnpair(); return true;
		case btnRelayDone:	RelayDone(); return true;
		}
		break;

	case frmCloseEvent:
		break;

	default:
		break;
	}
	return false;
}

/* ---- frmClear -------------------------------------------------------------------------------- */

static void ClearFill(void)
{
	UInt16 n = CountOlder(kClearDays[GetListSel(lstClearAge) < 0 ? 0 : GetListSel(lstClearAge)]);

	gClearInfo[0] = '\0';
	PutNum(gClearInfo, n);
	StrCat(gClearInfo, n == 1 ? " message will be deleted." : " messages will be deleted.");
	SetFieldPtr(fldClearInfo, gClearInfo);
}

Boolean ClearHandleEvent(EventPtr e)
{
	FormPtr frm;
	UInt16 days, n;
	char msg[40];

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		SetPopup(popClearAge, lstClearAge, 0, gAgeLabel, sizeof(gAgeLabel));
		ClearFill();
		FrmDrawForm(frm);
		return true;

	case popSelectEvent:
		if (e->data.popSelect.listID == lstClearAge && e->data.popSelect.selection >= 0) {
			SetPopup(popClearAge, lstClearAge, e->data.popSelect.selection, gAgeLabel, sizeof(gAgeLabel));
			ClearFill();
			return true;
		}
		break;

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnClearOk:
			days = kClearDays[GetListSel(lstClearAge) < 0 ? 0 : GetListSel(lstClearAge)];
			n = CountOlder(days);
			if (n == 0) { Info("Nothing to delete.", NULL, NULL); return true; }
			if (! Ask("Delete the messages from this Palm?\n", gClearInfo, NULL)) return true;
			ClearOlder(days);
			msg[0] = '\0';
			StrCopy(msg, "Deleted ");
			PutNum(msg, n);
			StrCat(msg, ".");
			Info(msg, NULL, NULL);
			{
				UInt16 back = gClearConv == 0xFFFF ? frmMain : frmChat;		/* the form that asked */

				gClearConv = 0xFFFF;
				FrmReturnToForm(back);
				FrmUpdateForm(back, updRefresh);
			}
			return true;
		case btnClearCancel:
			{
				UInt16 back = gClearConv == 0xFFFF ? frmMain : frmChat;

				gClearConv = 0xFFFF;
				FrmReturnToForm(back);
			}
			return true;
		}
		break;

	case frmCloseEvent:
		FldSetTextPtr(GetField(fldClearInfo), NULL);
		gClearConv = 0xFFFF;
		break;

	default:
		break;
	}
	return false;
}
