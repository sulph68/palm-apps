/*
 * Relay.c - the WiRSa side of the link: which node WiRSa talks to (scan and pair), whether it
 * stays awake or wakes now and then, and clearing stored messages. The calls are in Link.c.
 */
#include "MeshChat.h"

static char gRelayNode[44];
static char gModeLabel[20], gEveryLabel[10], gAgeLabel[24];
static char gClearInfo[64];

static const UInt16 kClearDays[5] = { 0, 1, 3, 7, 30 };

/* ---- frmRelay: Pair Device --------------------------------------------------------------------- */

static void RelayFill(void)
{
	StrCopy(gRelayNode, gRelay.node[0] ? gRelay.node : "none");
	SetFieldPtr(fldRelayNode, gRelayNode);
}

/* The WiRSa session must be up (Options > Connect): say so if it is not. */
static Boolean NeedWirsa(void)
{
	if (gWirsaUp) return true;
	Info("Not connected.", "Use Options > Connect on the main", " screen first.");
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
	BusyCancel("Pairing: look at the node's screen for a PIN...");
	ok = LinkPair(&gScan[sel]);
	BusyDone();
	if (! ok && McCancelled()) return;
	if (! ok) { Info(gLinkText[0] ? gLinkText : "Pairing failed.", NULL, NULL); return; }
	LinkSync();
	RelayFill();
	FrmDrawForm(FrmGetActiveForm());
	FrmUpdateForm(frmMain, updRefresh);
}

static void RelayUnpair(void)
{
	Boolean ok;

	if (! gRelay.node[0]) { Info("No node is paired.", NULL, NULL); return; }
	if (! Ask("Forget the paired node?\n", gRelay.node, NULL)) return;
	if (! NeedWirsa()) return;
	Busy("Forgetting the node...", 0);
	ok = LinkUnpair();
	BusyDone();
	if (! ok) { Info(gLinkText[0] ? gLinkText : "WiRSa did not forget it.", NULL, NULL); return; }
	RelayFill();
	FrmDrawForm(FrmGetActiveForm());
	FrmUpdateForm(frmMain, updRefresh);
}

/* Done: mode and interval go to WiRSa if they changed. */
static void RelayDone(void)
{
	UInt8 mode = (UInt8) GetListSel(lstRelayMode), every = (UInt8) GetListSel(lstRelayEvery);
	Boolean ok;

	if ((mode != gRelay.mode || every != gRelay.every) && gWirsaUp) {
		Busy("Saving to WiRSa...", 0);
		ok = LinkRelaySet(mode, every);
		BusyDone();
		if (! ok) { Info(gLinkText[0] ? gLinkText : "WiRSa did not take it.", NULL, NULL); return; }
		gRelay.mode = mode; gRelay.every = every;
	}
	FrmReturnToForm(frmMain);
	FrmUpdateForm(frmMain, updRefresh);
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
		if (gWirsaUp) { Busy("Fetching...", 0); LinkRelayGet(); BusyDone(); }
		SetPopup(popRelayMode, lstRelayMode, gRelay.mode, gModeLabel, sizeof(gModeLabel));
		SetPopup(popRelayEvery, lstRelayEvery, gRelay.every, gEveryLabel, sizeof(gEveryLabel));
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
		if (e->data.popSelect.listID == lstRelayMode) {
			SetPopup(popRelayMode, lstRelayMode, e->data.popSelect.selection, gModeLabel, sizeof(gModeLabel));
			return true;
		}
		if (e->data.popSelect.listID == lstRelayEvery) {
			SetPopup(popRelayEvery, lstRelayEvery, e->data.popSelect.selection, gEveryLabel, sizeof(gEveryLabel));
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
		FldSetTextPtr(GetField(fldRelayNode), NULL);
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
			FrmReturnToForm(frmMain);
			FrmUpdateForm(frmMain, updRefresh);
			return true;
		case btnClearCancel:
			FrmReturnToForm(frmMain);
			return true;
		}
		break;

	case frmCloseEvent:
		FldSetTextPtr(GetField(fldClearInfo), NULL);
		break;

	default:
		break;
	}
	return false;
}
