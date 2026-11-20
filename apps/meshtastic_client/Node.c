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

#define ST_ROWS		7
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
			gNodeCfg.chan[i].role != gSnap.chan[i].role || gNodeCfg.chan[i].link != gSnap.chan[i].link) *chanMask |= (UInt16) (1 << i);
	if (*chanMask) mask |= 16;
	if (gNodeCfg.mqtt.have != gSnap.mqtt.have || gNodeCfg.mqtt.flags != gSnap.mqtt.flags ||
		StrCompare(gNodeCfg.mqtt.address, gSnap.mqtt.address) || StrCompare(gNodeCfg.mqtt.user, gSnap.mqtt.user) ||
		StrCompare(gNodeCfg.mqtt.pass, gSnap.mqtt.pass) || StrCompare(gNodeCfg.mqtt.root, gSnap.mqtt.root)) mask |= 32;
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
	case 5:		return "MQTT";
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
	if (! gNodeCfg.mqtt.have) StrCopy(gVal[5], "not read");
	else if (! (gNodeCfg.mqtt.flags & 1)) StrCopy(gVal[5], "Off");
	else StrNCopy(gVal[5], gNodeCfg.mqtt.address[0] ? gNodeCfg.mqtt.address : "default server", sizeof(gVal[5]) - 1);
	NodeClockShort(gVal[6]);
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
	case 5: FrmPopupForm(frmMqtt); break;
	case 6: FrmPopupForm(frmActions); break;
	}
}

/* Save: send what changed to the node (it restarts to take it). */
MC_SEC static void SetupSave(void)
{
	UInt16 chanMask, mask = CfgMask(&chanMask);
	Boolean ok;

	if (! mask) { Info("Nothing was changed.", NULL, NULL); return; }
	if ((mask & 32) && (mask & 31)) {				/* the node turns its Bluetooth off as soon as it takes the MQTT settings */
		Info("MQTT is saved on its own,", "because the node restarts.", "Save the other changes first.");
		return;
	}
	if (! Ask("Send the changes to the node?\n", "The node restarts to take them.", NULL)) return;
	if (! gWirsaUp) { Info("Not connected. Connect first.", NULL, NULL); return; }
	Busy("Updating the node...", 0);
	ok = LinkCfgSet(mask, chanMask);
	BusyDone();
	if (! ok) { Info(gLinkText[0] ? gLinkText : "The node did not take the changes.", NULL, NULL); return; }
	Info("Sent. Node restarting. It will reconnect shortly...", NULL, NULL);
	gWantChans = true;				/* the chat list takes the channel names from the next sync */
	FrmReturnToForm(frmMain);
	FrmUpdateForm(frmMain, updFull);
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
				ActRun(1, "Restarting the node...", "Sent. Node restarting. It will reconnect shortly...");
			return true;
		case btnSetupCancel:
			if (CfgMask(&gChanMask) && ! Ask("Discard changes?", NULL, NULL)) return true;
			gNodeCfg = gSnap;
			FrmReturnToForm(frmMain);
			FrmUpdateForm(frmMain, updFull);
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
	if (! gNodeCfg.chan[i].name[0])
		WinDrawChars("(default)", 9, (Coord) (b->topLeft.x + 14), b->topLeft.y);	/* an empty name is the default channel */
	else
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

/* ---- channel key entry, masked like the suite's passwords: the field shows '*'s (the just-typed character in
 * clear for a second), Show reveals it all; the real key lives only in gChKeyPlain, wiped with the field
 * text when the form closes. Edits are append / backspace at the end; Copy, Cut and Paste work on the real key. */
#define chKeyMax		44
#define chKeyRevealSecs	1
UInt32 gChKeyRevealUntil = 0;
static char gChKeyPlain[chKeyMax + 1];
static Boolean gChKeyShow = false, gChKeyRevealLast = false;

MC_SEC static void ChKeyRedraw(void)
{
	FieldPtr fld = GetField(fldChKey);
	UInt16 len = (UInt16) StrLen(gChKeyPlain), i, cur;
	char disp[chKeyMax + 1];

	for (i = 0; i < len; i++)
		disp[i] = (char) ((gChKeyShow || (gChKeyRevealLast && i == len - 1)) ? gChKeyPlain[i] : '*');
	disp[len] = '\0';
	cur = FldGetTextLength(fld);
	if (cur) { FldSetInsPtPosition(fld, cur); FldDelete(fld, 0, cur); }
	if (len) FldInsert(fld, disp, len);
	FldSetInsPtPosition(fld, len);
	if (FrmVisible(FrmGetActiveForm())) FldDrawField(fld);
	MemSet(disp, sizeof(disp), 0);
}

MC_SEC static void ChKeySet(const char *key)
{
	StrNCopy(gChKeyPlain, key, chKeyMax);
	gChKeyPlain[chKeyMax] = '\0';
	gChKeyRevealLast = false;
	gChKeyRevealUntil = 0;
	ChKeyRedraw();
}

MC_SEC void ChKeyRevealTick(void)
{
	if (gChKeyRevealUntil && TimGetTicks() >= gChKeyRevealUntil) {
		gChKeyRevealUntil = 0;
		gChKeyRevealLast = false;
		ChKeyRedraw();
	}
}

/* True if the key field has the focus. */
MC_SEC static Boolean ChKeyFocused(void)
{
	FormPtr frm = FrmGetActiveForm();
	UInt16 ix = FrmGetFocus(frm);
	return (Boolean) (ix != noFocus && FrmGetObjectId(frm, ix) == fldChKey);
}

MC_SEC static Boolean ChKeyKeyDown(EventPtr e)
{
	WChar c = e->data.keyDown.chr;
	UInt16 len = (UInt16) StrLen(gChKeyPlain);

	if (c == chrBackspace) {
		if (len) gChKeyPlain[len - 1] = '\0';
		gChKeyRevealLast = false;
		gChKeyRevealUntil = 0;
		ChKeyRedraw();
		return true;
	}
	if (TxtCharIsPrint(c) && c != ' ' && len < chKeyMax) {
		gChKeyPlain[len] = (char) c;
		gChKeyPlain[len + 1] = '\0';
		gChKeyRevealLast = true;
		gChKeyRevealUntil = TimGetTicks() + (SysTicksPerSecond() * chKeyRevealSecs);
		ChKeyRedraw();
		return true;
	}
	return false;
}

/* Copy / Cut / Paste on the key field act on the real key (the field only holds '*'s); Undo does nothing there. */
MC_SEC static Boolean ChKeyEditMenu(UInt16 id)
{
	FieldPtr fld = GetField(fldChKey);
	UInt16 a, b, len = (UInt16) StrLen(gChKeyPlain), i;

	FldGetSelection(fld, &a, &b);
	if (a > len) a = len;
	if (b > len) b = len;
	switch (id) {
	case mniEditUndo:
		return true;
	case mniEditCopy:
	case mniEditCut:
		if (b <= a) return true;
		ClipboardAddItem(clipboardText, &gChKeyPlain[a], (UInt16) (b - a));
		if (id == mniEditCut) {
			for (i = a; i + (b - a) <= len; i++) gChKeyPlain[i] = gChKeyPlain[i + (b - a)];
			gChKeyRevealLast = false;
			gChKeyRevealUntil = 0;
			ChKeyRedraw();
		}
		return true;
	case mniEditPaste: {
		UInt16 clipLen;
		MemHandle h = ClipboardGetItem(clipboardText, &clipLen);
		const char *p;
		if (! h || ! clipLen) return true;
		p = (const char *) MemHandleLock(h);
		for (i = 0; i < clipLen && len < chKeyMax; i++)
			if (p[i] > ' ') gChKeyPlain[len++] = p[i];		/* no spaces or line breaks in a key */
		gChKeyPlain[len] = '\0';
		MemHandleUnlock(h);
		gChKeyRevealLast = false;
		gChKeyRevealUntil = 0;
		ChKeyRedraw();
		return true;
	}
	}
	return false;
}

/* The form is closing: wipe the real key and the field's text. */
MC_SEC static void ChKeyEnd(void)
{
	FieldPtr fld = GetField(fldChKey);
	UInt16 cur = FldGetTextLength(fld);
	MemHandle h;

	if (cur) FldDelete(fld, 0, cur);
	h = FldGetTextHandle(fld);
	if (h) { MemSet(MemHandleLock(h), (Int32) MemHandleSize(h), 0); MemHandleUnlock(h); }
	MemSet(gChKeyPlain, sizeof(gChKeyPlain), 0);
	gChKeyShow = gChKeyRevealLast = false;
	gChKeyRevealUntil = 0;
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
		gChKeyShow = false;
		CtlSetValue((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, ckChKeyShow)), 0);
		SetFieldEdit(fldChKey, "");
		ChKeySet(gNodeCfg.chan[gChanSel].key);
		CtlSetValue((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, ckChUplink)), gNodeCfg.chan[gChanSel].link & 1 ? 1 : 0);
		CtlSetValue((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, ckChDownlink)), gNodeCfg.chan[gChanSel].link & 2 ? 1 : 0);
		SetFieldPtr(fldChKeyNote, "Empty key = AQ== (default). Primary name may be empty.");
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
		if (ChKeyFocused() && ChKeyEditMenu(e->data.menu.itemID)) return true;
		return EditMenu(e->data.menu.itemID);

	case keyDownEvent:
		if (ChKeyFocused() && ChKeyKeyDown(e)) return true;
		break;

	case frmCloseEvent:
		ChKeyEnd();
		FldSetTextPtr(GetField(fldChKeyNote), NULL);
		break;

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnChNewKey:
			NewKey(key);
			ChKeySet(key);
			MemSet(key, sizeof(key), 0);
			return true;
		case ckChKeyShow:
			gChKeyShow = (Boolean) (e->data.ctlSelect.on != 0);
			gChKeyRevealLast = false;
			gChKeyRevealUntil = 0;
			ChKeyRedraw();
			return true;
		case btnChOk: {
			Int16 role = GetListSel(lstChRole);
			GetFieldEdit(fldChName, name, sizeof(name));
			StrCopy(key, gChKeyPlain);		/* the real key: the field only shows '*'s */
			if (role != 0 && gChanSel != 0 && ! name[0]) { Info("The channel needs a name.", NULL, NULL); return true; }	/* the primary's may be empty */
			if (role != 0 && ! key[0]) StrCopy(key, "AQ==");								/* an empty key is the default one */
			if (gChanSel == 0 && role != 1) { Info("Slot 0 is the primary channel.", NULL, NULL); return true; }
			if (gChanSel != 0 && role == 1) { Info("Only slot 0 can be the primary channel.", NULL, NULL); return true; }
			StrCopy(gNodeCfg.chan[gChanSel].name, name);
			StrCopy(gNodeCfg.chan[gChanSel].key, key);
			frm = FrmGetActiveForm();
			gNodeCfg.chan[gChanSel].link = (UInt8) ((CtlGetValue((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, ckChUplink))) ? 1 : 0) |
				(CtlGetValue((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, ckChDownlink))) ? 2 : 0));
			gNodeCfg.chan[gChanSel].role = (UInt8) role;
			gNodeCfg.dirty = true;
			ChKeyEnd();					/* no frmCloseEvent with FrmReturnToForm */
			FrmReturnToForm(frmChList);
			FrmUpdateForm(frmChList, updRefresh);
			return true;
		}
		case btnChCancel:
			ChKeyEnd();					/* no frmCloseEvent with FrmReturnToForm */
			FrmReturnToForm(frmChList);
			return true;
		}
		break;

	default:
		break;
	}
	return false;
}

/* ---- frmMqtt ------------------------------------------------------------------------------- */

MC_SEC static void MqCheck(FormPtr frm, UInt16 id, Boolean on)
{
	CtlSetValue((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, id)), on ? 1 : 0);
}

MC_SEC static UInt8 MqBit(FormPtr frm, UInt16 id, UInt8 bit)
{
	return CtlGetValue((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, id))) ? bit : 0;
}

MC_SEC Boolean MqttHandleEvent(EventPtr e)
{
	FormPtr frm;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		SetFieldEdit(fldMqAddr, gNodeCfg.mqtt.address);
		SetFieldEdit(fldMqUser, gNodeCfg.mqtt.user);
		SetFieldEdit(fldMqPass, gNodeCfg.mqtt.pass);
		SetFieldEdit(fldMqRoot, gNodeCfg.mqtt.root);
		MqCheck(frm, ckMqEnabled, gNodeCfg.mqtt.flags & 1);
		MqCheck(frm, ckMqEncrypt, gNodeCfg.mqtt.flags & 2);
		MqCheck(frm, ckMqJson, gNodeCfg.mqtt.flags & 4);
		MqCheck(frm, ckMqTls, gNodeCfg.mqtt.flags & 8);
		MqCheck(frm, ckMqProxy, gNodeCfg.mqtt.flags & 16);
		MqCheck(frm, ckMqMap, gNodeCfg.mqtt.flags & 32);
		FrmDrawForm(frm);
		FrmSetFocus(frm, FrmGetObjectIndex(frm, fldMqAddr));
		return true;

	case menuEvent:
		return EditMenu(e->data.menu.itemID);

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnMqOk: {
			Boolean ok;

			frm = FrmGetActiveForm();
			GetFieldEdit(fldMqAddr, gNodeCfg.mqtt.address, sizeof(gNodeCfg.mqtt.address));
			GetFieldEdit(fldMqUser, gNodeCfg.mqtt.user, sizeof(gNodeCfg.mqtt.user));
			GetFieldEdit(fldMqPass, gNodeCfg.mqtt.pass, sizeof(gNodeCfg.mqtt.pass));
			GetFieldEdit(fldMqRoot, gNodeCfg.mqtt.root, sizeof(gNodeCfg.mqtt.root));
			gNodeCfg.mqtt.flags = (UInt8) (MqBit(frm, ckMqEnabled, 1) | MqBit(frm, ckMqEncrypt, 2) | MqBit(frm, ckMqJson, 4) |
				MqBit(frm, ckMqTls, 8) | MqBit(frm, ckMqProxy, 16) | MqBit(frm, ckMqMap, 32));
			gNodeCfg.mqtt.have = 1;
			if (gNodeCfg.mqtt.flags == gSnap.mqtt.flags && gSnap.mqtt.have && ! StrCompare(gNodeCfg.mqtt.address, gSnap.mqtt.address) &&
				! StrCompare(gNodeCfg.mqtt.user, gSnap.mqtt.user) && ! StrCompare(gNodeCfg.mqtt.pass, gSnap.mqtt.pass) &&
				! StrCompare(gNodeCfg.mqtt.root, gSnap.mqtt.root)) {		/* nothing changed: nothing to send */
				FrmReturnToForm(frmSetup);
				return true;
			}
			/* The node turns its Bluetooth off and restarts when it takes the MQTT settings, so they go on their own, now. */
			if (! AskOk("Send config and reboot now?", NULL, NULL)) {
				MemMove(&gNodeCfg.mqtt, &gSnap.mqtt, sizeof(gNodeCfg.mqtt));		/* cancelled: the old settings stay */
				FrmReturnToForm(frmSetup);
				FrmUpdateForm(frmSetup, updRefresh);
				return true;
			}
			if (! gWirsaUp) { Info("Not connected.", "Use Connect first.", NULL); return true; }
			Busy("Updating the node...", 0);
			ok = LinkCfgSet(32, 0);
			BusyDone();
			if (! ok) { Info(gLinkText[0] ? gLinkText : "The node did not take the changes.", NULL, NULL); return true; }
			MemMove(&gSnap.mqtt, &gNodeCfg.mqtt, sizeof(gNodeCfg.mqtt));			/* sent: no longer a pending change */
			Info("Sent. Node restarting. It will reconnect shortly...", NULL, NULL);
			LinkRestarting();
			gWantChans = true;
			gWantNodes = true;
			FrmReturnToForm(frmMain);					/* both forms close; the main screen shows the link coming back */
			FrmUpdateForm(frmMain, updFull);
			return true;
		}
		case btnMqCancel:
			FrmReturnToForm(frmSetup);
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

	if (! gWirsaUp) { Info("Not connected. Connect first.", NULL, NULL); return; }
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
