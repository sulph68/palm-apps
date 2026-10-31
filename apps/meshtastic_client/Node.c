/*
 * Node.c - the node's own settings, one page each: owner name, LoRa radio, device and Bluetooth,
 * channels, node clock and reboot. Changes are collected in gNodeCfg and sent to the node in one
 * batch when Setup is closed (the node restarts once). Mock-up: nothing is sent.
 */
#include "MeshChat.h"

static NodeCfg gSnap;				/* gNodeCfg when Setup opened, for Cancel */
static char gSetupLine[56];
static UInt16 gChanSel;
static char gLabel1[16], gLabel2[16], gLabel3[16];
static char gActNode[48], gActPalm[48], gActDiff[32];
static char gTxBuf[4], gPinBuf[8];
MC_SEC static void ActRun(UInt8 action, const char *busy, const char *done);
MC_SEC static void HideObj(FormPtr frm, UInt16 id);

MC_SEC static void SetupLine(void)
{
	StrCopy(gSetupLine, gNodeCfg.longName);
	if (gFirmware[0]) { StrCat(gSetupLine, "  fw "); StrCat(gSetupLine, gFirmware); }
}

#define ST_ROWS		6
static UInt16 gMask, gChanMask;			/* what differs from the node's own values, as CfgMask says */
static UInt8 gDevWhich;					/* 0 the role, 1 the Bluetooth pairing: what frmDevice shows */
static char gVal[ST_ROWS][40];

/* What differs from the node's own values, as the parts LinkCfgSet knows. */
MC_SEC static UInt16 CfgMask(UInt16 *chanMask)
{
	UInt16 mask = 0, i;

	*chanMask = 0;
	if (StrCompare(gNodeCfg.longName, gSnap.longName) || StrCompare(gNodeCfg.shortName, gSnap.shortName)) mask |= 1;
	if (gNodeCfg.region != gSnap.region || gNodeCfg.preset != gSnap.preset || gNodeCfg.hop != gSnap.hop || gNodeCfg.txPower != gSnap.txPower) mask |= 2;
	if (gNodeCfg.role != gSnap.role) mask |= 4;
	if (gNodeCfg.btMode != gSnap.btMode || (gNodeCfg.btMode == 1 && StrCompare(gNodeCfg.pin, gSnap.pin))) mask |= 8;
	for (i = 0; i < MAX_CHAN; i++)
		if (StrCompare(gNodeCfg.chan[i].name, gSnap.chan[i].name) || StrCompare(gNodeCfg.chan[i].key, gSnap.chan[i].key) ||
			gNodeCfg.chan[i].role != gSnap.chan[i].role) *chanMask |= (UInt16) (1 << i);
	if (*chanMask) mask |= 16;
	return mask;
}

MC_SEC static const char *RowLabel(UInt16 i)
{
	switch (i) {
	case 0:		return "Owner";
	case 1:		return "LoRa";
	case 2:		return "Role";
	case 3:		return "Bluetooth";
	case 4:		return "Channels";
	}
	return "Node clock";
}

/* The value of each row, from the buffer. */
MC_SEC static void SetupValues(void)
{
	UInt16 i, n = 0;

	gMask = CfgMask(&gChanMask);
	StrCopy(gVal[0], gNodeCfg.longName);
	StrCopy(gVal[1], RegionName(gNodeCfg.region));
	StrCat(gVal[1], ", ");
	StrCat(gVal[1], PresetName(gNodeCfg.preset));
	StrCopy(gVal[2], DevRoleName(gNodeCfg.role));
	StrCopy(gVal[3], BtName(gNodeCfg.btMode));
	for (i = 0; i < MAX_CHAN; i++) if (gNodeCfg.chan[i].role) n++;
	gVal[4][0] = '\0';
	PutNum(gVal[4], n);
	StrCat(gVal[4], " configured");
	NodeClockShort(gVal[5]);
}

MC_SEC static void SetupDrawRow(void *table, Int16 row, Int16 column, RectangleType *b)
{
	Coord x = b->topLeft.x, y = (Coord) (b->topLeft.y + 1);
	FontID old = FntSetFont(boldFont);

	if (gMask & (1 << row)) WinDrawChars("\225", 1, (Coord) (x + 1), y);		/* a bullet: changed, not sent yet */
	WinDrawChars(RowLabel(row), (Int16) StrLen(RowLabel(row)), (Coord) (x + 8), y);
	FntSetFont(stdFont);
	WinDrawChars(":", 1, (Coord) (x + 62), y);
	WinDrawTruncChars(gVal[row], (Int16) StrLen(gVal[row]), (Coord) (x + 68), y, (Int16) (b->extent.x - 70));
	FntSetFont(old);
}

MC_SEC static TableType *SetupTable(void)
{
	FormPtr frm = FrmGetActiveForm();
	return (TableType *) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, tblSetup));
}

MC_SEC static void SetupLoad(void)
{
	TableType *t = SetupTable();
	UInt16 r;

	SetupValues();
	for (r = 0; r < ST_ROWS; r++) {
		TblSetItemStyle(t, r, 0, customTableItem);
		TblSetRowHeight(t, r, 13);
		TblSetRowUsable(t, r, true);
		TblMarkRowInvalid(t, r);
	}
	TblSetColumnUsable(t, 0, true);
}

MC_SEC static void SetupOpenPage(UInt16 row)
{
	switch (row) {
	case 0: FrmPopupForm(frmOwner); break;
	case 1: FrmPopupForm(frmLora); break;
	case 2: gDevWhich = 0; FrmPopupForm(frmDevice); break;
	case 3: gDevWhich = 1; FrmPopupForm(frmDevice); break;
	case 4: FrmPopupForm(frmChList); break;
	case 5: FrmPopupForm(frmActions); break;
	}
}

/* Save: send what changed to the node (it restarts to take it). */
MC_SEC static void SetupSave(void)
{
	UInt16 chanMask, mask = CfgMask(&chanMask);
	Boolean ok;

	if (! mask) { Info("Nothing was changed.", NULL, NULL); return; }
	if (! Ask("Send the changes to the node?\n", "The node restarts to take them.", NULL)) return;
	if (! gWirsaUp) { Info("Not connected.", "Use Options > Connect first.", NULL); return; }
	Busy("Updating the node...", 0);
	ok = LinkCfgSet(mask, chanMask);
	BusyDone();
	if (! ok) { Info(gLinkText[0] ? gLinkText : "The node did not take the changes.", NULL, NULL); return; }
	Info("Sent. The node is restarting;", "it comes back in a few seconds.", NULL);
	gWantChans = true;				/* the chat list takes the channel names from the next sync */
	FrmReturnToForm(frmMain);
	FrmUpdateForm(frmMain, updRefresh);
}

MC_SEC Boolean SetupHandleEvent(EventPtr e)
{
	FormPtr frm;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		gSnap = gNodeCfg;
		gNodeCfg.dirty = false;
		SetupLine();
		SetFieldPtr(fldSetupNode, gSetupLine);
		FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, gadSetupLine), LineGadget);
		TblSetCustomDrawProcedure(SetupTable(), 0, SetupDrawRow);
		SetupLoad();
		FrmDrawForm(frm);
		return true;

	case frmUpdateEvent:
		SetupLine();
		SetFieldPtr(fldSetupNode, gSetupLine);
		SetupLoad();
		FrmDrawForm(FrmGetActiveForm());
		return true;

	case tblSelectEvent:
		if (e->data.tblSelect.tableID == tblSetup) {
			SetupOpenPage((UInt16) e->data.tblSelect.row);
			return true;
		}
		break;

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnSetupDone:
			SetupSave();
			return true;
		case btnSetupReboot:
			if (Ask("Restart the node?\n", "It is off the air for a few seconds.", NULL))
				ActRun(1, "Restarting the node...", "Sent. The node is restarting.");
			return true;
		case btnSetupCancel:
			if (CfgMask(&gChanMask) && ! Ask("Throw away the changes?", NULL, NULL)) return true;
			gNodeCfg = gSnap;
			FrmReturnToForm(frmMain);
			FrmUpdateForm(frmMain, updRefresh);
			return true;
		}
		break;

	case frmCloseEvent:
		FldSetTextPtr(GetField(fldSetupNode), NULL);
		break;

	default:
		break;
	}
	return false;
}

/* ---- frmOwner ------------------------------------------------------------------------------ */

MC_SEC Boolean OwnerHandleEvent(EventPtr e)
{
	FormPtr frm;
	char lng[40], sht[5];

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		SetFieldEdit(fldOwnerLong, gNodeCfg.longName);
		SetFieldEdit(fldOwnerShort, gNodeCfg.shortName);
		FrmDrawForm(frm);
		FrmSetFocus(frm, FrmGetObjectIndex(frm, fldOwnerLong));
		return true;

	case menuEvent:
		return EditMenu(e->data.menu.itemID);

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnOwnerOk:
			GetFieldEdit(fldOwnerLong, lng, sizeof(lng));
			GetFieldEdit(fldOwnerShort, sht, sizeof(sht));
			if (! lng[0] || ! sht[0]) { Info("Both names are needed.", NULL, NULL); return true; }
			StrCopy(gNodeCfg.longName, lng);
			StrCopy(gNodeCfg.shortName, sht);
			gNodeCfg.dirty = true;
			FrmReturnToForm(frmSetup);
			FrmUpdateForm(frmSetup, updRefresh);
			return true;
		case btnOwnerCancel:
			FrmReturnToForm(frmSetup);
			return true;
		}
		break;

	default:
		break;
	}
	return false;
}

/* ---- frmLora ------------------------------------------------------------------------------- */

MC_SEC Boolean LoraHandleEvent(EventPtr e)
{
	FormPtr frm;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		SetPopup(popLoraRegion, lstLoraRegion, gNodeCfg.region, gLabel1, sizeof(gLabel1));
		SetPopup(popLoraPreset, lstLoraPreset, gNodeCfg.preset, gLabel2, sizeof(gLabel2));
		SetPopup(popLoraHop, lstLoraHop, gNodeCfg.hop - 1, gLabel3, sizeof(gLabel3));
		SetFieldPtr(fldLoraNote, "TX power 0 = the node's default. Changing the radio restarts the node, and it has to match the others on your mesh.");
		gTxBuf[0] = '\0';
		PutNum(gTxBuf, gNodeCfg.txPower);
		SetFieldPtr(fldLoraTx, gTxBuf);
		FrmDrawForm(frm);
		return true;

	case popSelectEvent:
		if (e->data.popSelect.selection < 0) break;
		if (e->data.popSelect.listID == lstLoraRegion)
			SetPopup(popLoraRegion, lstLoraRegion, e->data.popSelect.selection, gLabel1, sizeof(gLabel1));
		else if (e->data.popSelect.listID == lstLoraPreset)
			SetPopup(popLoraPreset, lstLoraPreset, e->data.popSelect.selection, gLabel2, sizeof(gLabel2));
		else if (e->data.popSelect.listID == lstLoraHop)
			SetPopup(popLoraHop, lstLoraHop, e->data.popSelect.selection, gLabel3, sizeof(gLabel3));
		else
			break;
		return true;

	case frmCloseEvent:
		FldSetTextPtr(GetField(fldLoraNote), NULL);
		FldSetTextPtr(GetField(fldLoraTx), NULL);
		break;

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnLoraTxSet:
			if (gTxBuf[0] == '0') gTxBuf[0] = '\0';			/* 0 = the default: start from a blank */
			NumPad(gTxBuf, 2, "TX power");
			if (! gTxBuf[0]) StrCopy(gTxBuf, "0");
			SetFieldPtr(fldLoraTx, gTxBuf);
			return true;
		case btnLoraOk:
			if (StrAToI(gTxBuf) > 30) { Info("TX power can be 0 to 30 dBm.", NULL, NULL); return true; }
			gNodeCfg.region = (UInt8) GetListSel(lstLoraRegion);
			gNodeCfg.preset = (UInt8) GetListSel(lstLoraPreset);
			gNodeCfg.hop = (UInt8) (GetListSel(lstLoraHop) + 1);
			gNodeCfg.txPower = (UInt8) StrAToI(gTxBuf);
			gNodeCfg.dirty = true;
			FrmReturnToForm(frmSetup);
			FrmUpdateForm(frmSetup, updRefresh);
			return true;
		case btnLoraCancel:
			FrmReturnToForm(frmSetup);
			return true;
		}
		break;

	default:
		break;
	}
	return false;
}

MC_SEC static void HideObj(FormPtr frm, UInt16 id)
{
	FrmHideObject(frm, FrmGetObjectIndex(frm, id));
}

/* ---- frmDevice ----------------------------------------------------------------------------- */

MC_SEC Boolean DeviceHandleEvent(EventPtr e)
{
	FormPtr frm;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		SetPopup(popDevRole, lstDevRole, gNodeCfg.role, gLabel1, sizeof(gLabel1));
		SetPopup(popDevBt, lstDevBt, gNodeCfg.btMode, gLabel2, sizeof(gLabel2));
		StrCopy(gPinBuf, gNodeCfg.pin);
		SetFieldPtr(fldDevPin, gPinBuf);
		FrmSetTitle(frm, gDevWhich == 0 ? "Role" : "Bluetooth");
		if (gDevWhich != 0) {
			HideObj(frm, lblDevRole); HideObj(frm, popDevRole);
		} else {
			HideObj(frm, lblDevBt); HideObj(frm, popDevBt); HideObj(frm, lblDevPin);
			HideObj(frm, fldDevPin); HideObj(frm, btnDevPinSet); HideObj(frm, lblDevHint);
		}
		FrmDrawForm(frm);
		return true;

	case popSelectEvent:
		if (e->data.popSelect.selection < 0) break;
		if (e->data.popSelect.listID == lstDevRole)
			SetPopup(popDevRole, lstDevRole, e->data.popSelect.selection, gLabel1, sizeof(gLabel1));
		else if (e->data.popSelect.listID == lstDevBt)
			SetPopup(popDevBt, lstDevBt, e->data.popSelect.selection, gLabel2, sizeof(gLabel2));
		else
			break;
		return true;

	case frmCloseEvent:
		FldSetTextPtr(GetField(fldDevPin), NULL);
		break;

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnDevPinSet:
			if (NumPad(gPinBuf, 6, "PIN"))
				SetFieldPtr(fldDevPin, gPinBuf);
			return true;
		case btnDevOk:
			if (gDevWhich == 1 && GetListSel(lstDevBt) == 1 && StrLen(gPinBuf) != 6) {
				Info("A fixed PIN is 6 digits.", NULL, NULL);
				return true;
			}
			if (gDevWhich == 0) {
				gNodeCfg.role = (UInt8) GetListSel(lstDevRole);
			} else {
				gNodeCfg.btMode = (UInt8) GetListSel(lstDevBt);
				StrCopy(gNodeCfg.pin, gPinBuf);
			}
			gNodeCfg.dirty = true;
			FrmReturnToForm(frmSetup);
			FrmUpdateForm(frmSetup, updRefresh);
			return true;
		case btnDevCancel:
			FrmReturnToForm(frmSetup);
			return true;
		}
		break;

	default:
		break;
	}
	return false;
}

/* ---- frmChList / frmChEdit ----------------------------------------------------------------- */

MC_SEC static void DrawChanRow(Int16 i, RectangleType *b, Char **unused)
{
	char t[40];
	const char *role = gNodeCfg.chan[i].role == 1 ? "primary" : "secondary";

	t[0] = '\0';
	PutNum(t, i);
	WinDrawChars(t, (Int16) StrLen(t), (Coord) (b->topLeft.x + 1), b->topLeft.y);
	if (gNodeCfg.chan[i].role == 0) {
		WinDrawChars("(not used)", 10, (Coord) (b->topLeft.x + 14), b->topLeft.y);
		return;
	}
	WinDrawTruncChars(gNodeCfg.chan[i].name, (Int16) StrLen(gNodeCfg.chan[i].name),
		(Coord) (b->topLeft.x + 14), b->topLeft.y, 70);
	WinDrawChars(role, (Int16) StrLen(role), (Coord) (b->topLeft.x + 90), b->topLeft.y);
}

MC_SEC Boolean ChListHandleEvent(EventPtr e)
{
	FormPtr frm;
	ListPtr lst;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		lst = (ListPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, lstChan));
		LstSetDrawFunction(lst, DrawChanRow);
		LstSetListChoices(lst, NULL, MAX_CHAN);
		LstSetSelection(lst, noListSelection);
		FrmDrawForm(frm);
		return true;

	case frmUpdateEvent:
		FrmDrawForm(FrmGetActiveForm());
		return true;

	case lstSelectEvent:
		if (e->data.lstSelect.listID == lstChan && e->data.lstSelect.selection >= 0) {
			gChanSel = (UInt16) e->data.lstSelect.selection;
			FrmPopupForm(frmChEdit);
			return true;
		}
		break;

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnChBack:
			FrmReturnToForm(frmSetup);
			FrmUpdateForm(frmSetup, updRefresh);
			return true;
		}
		break;

	default:
		break;
	}
	return false;
}

/* A made-up 32-byte key in base64 (44 characters). */
MC_SEC static void NewKey(char *out)
{
	UInt16 i;

	for (i = 0; i < 43; i++)
		{
			UInt16 v = (UInt16) (SysRandom(0) & 63);
			out[i] = (char) (v < 26 ? 'A' + v : v < 52 ? 'a' + (v - 26) : v < 62 ? '0' + (v - 52) : v == 62 ? '+' : '/');
		}
	out[43] = '=';
	out[44] = '\0';
}

MC_SEC Boolean ChEditHandleEvent(EventPtr e)
{
	FormPtr frm;
	char name[12], key[46];

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		SetFieldEdit(fldChName, gNodeCfg.chan[gChanSel].name);
		SetPopup(popChRole, lstChRole, gNodeCfg.chan[gChanSel].role, gLabel1, sizeof(gLabel1));
		SetFieldEdit(fldChKey, gNodeCfg.chan[gChanSel].key);
		SetFieldPtr(fldChKeyNote, "Key (base64): everyone on this channel needs the same one.");
		FrmDrawForm(frm);
		FrmSetFocus(frm, FrmGetObjectIndex(frm, fldChName));
		return true;

	case popSelectEvent:
		if (e->data.popSelect.listID == lstChRole && e->data.popSelect.selection >= 0) {
			SetPopup(popChRole, lstChRole, e->data.popSelect.selection, gLabel1, sizeof(gLabel1));
			return true;
		}
		break;

	case menuEvent:
		return EditMenu(e->data.menu.itemID);

	case frmCloseEvent:
		FldSetTextPtr(GetField(fldChKeyNote), NULL);
		break;

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnChNewKey:
			NewKey(key);
			SetFieldEdit(fldChKey, key);
			return true;
		case btnChOk: {
			Int16 role = GetListSel(lstChRole);
			GetFieldEdit(fldChName, name, sizeof(name));
			GetFieldEdit(fldChKey, key, sizeof(key));
			if (role != 0 && ! name[0]) { Info("The channel needs a name.", NULL, NULL); return true; }
			if (gChanSel == 0 && role != 1) { Info("Slot 0 is the primary channel.", NULL, NULL); return true; }
			if (gChanSel != 0 && role == 1) { Info("Only slot 0 can be the primary channel.", NULL, NULL); return true; }
			StrCopy(gNodeCfg.chan[gChanSel].name, name);
			StrCopy(gNodeCfg.chan[gChanSel].key, key);
			gNodeCfg.chan[gChanSel].role = (UInt8) role;
			gNodeCfg.dirty = true;
			FrmReturnToForm(frmChList);
			FrmUpdateForm(frmChList, updRefresh);
			return true;
		}
		case btnChCancel:
			FrmReturnToForm(frmChList);
			return true;
		}
		break;

	default:
		break;
	}
	return false;
}

/* ---- frmActions ----------------------------------------------------------------------------- */

MC_SEC static void ActFill(void)
{
	FmtDateTime(NowUtc(), gActPalm);
	NodeClockText(gActNode, gActDiff);
	SetFieldPtr(fldActNode, gActNode);
	SetFieldPtr(fldActPalm, gActPalm);
	SetFieldPtr(fldActDiff, gActDiff);
}

MC_SEC static void ActRun(UInt8 action, const char *busy, const char *done)
{
	Boolean ok;

	if (! gWirsaUp) { Info("Not connected.", "Use Options > Connect first.", NULL); return; }
	Busy(busy, 0);
	ok = LinkAct(action);
	BusyDone();
	Info(ok ? done : (gLinkText[0] ? gLinkText : "The node did not take it."), NULL, NULL);
	if (FrmGetActiveFormID() == frmActions) {			/* Reboot Node on the Settings card has no clock fields */
		ActFill();
		FrmDrawForm(FrmGetActiveForm());
	}
}

MC_SEC Boolean ActionsHandleEvent(EventPtr e)
{
	switch (e->eType) {
	case frmOpenEvent:
		ActFill();
		FrmDrawForm(FrmGetActiveForm());
		return true;

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnActSetTime:
			ActRun(0, "Setting the node's clock...", "Done: the node's clock is set to this Palm's.");
			return true;
		case btnActDone:
			FrmReturnToForm(frmSetup);
			return true;
		}
		break;

	case frmCloseEvent:
		FldSetTextPtr(GetField(fldActNode), NULL);
		FldSetTextPtr(GetField(fldActPalm), NULL);
		FldSetTextPtr(GetField(fldActDiff), NULL);
		break;

	default:
		break;
	}
	return false;
}
