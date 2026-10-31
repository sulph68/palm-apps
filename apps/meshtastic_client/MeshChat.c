/*
 * MeshChat.c - the Meshtastic client: main screen, chat, new message, nodes, preferences, and the
 * app loop. The link to WiRSa (finding it, starting the Mesh mode, syncing, sending) is Link.c.
 */
#include "MeshChat.h"

static UInt32 gNextCheck;				/* tick of the next look at the serial buffer */
static UInt32 gNextFill;				/* tick of the next fetch while the lists are still empty */
Boolean gNodesRefresh;			/* the Nodes list asks for the node list once it is on screen */
static Boolean gPollFailed;			/* the last background sync failed */
static void RedrawActive(void);

/* ---- helpers ---------------------------------------------------------------------------- */

FieldPtr GetField(UInt16 id)
{
	FormPtr frm = FrmGetActiveForm();
	return (FieldPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, id));
}

void SetFieldPtr(UInt16 id, char *text)
{
	FieldPtr fld = GetField(id);
	FldSetTextPtr(fld, text);
	FldRecalculateField(fld, true);
	if (FrmVisible(FrmGetActiveForm()))
		FldDrawField(fld);
}

void SetFieldEdit(UInt16 id, const char *text)
{
	FieldPtr fld = GetField(id);
	MemHandle old = FldGetTextHandle(fld);
	UInt16 len = (UInt16) StrLen(text);
	MemHandle h = MemHandleNew(len + 1);

	if (! h) return;
	StrCopy((char *) MemHandleLock(h), text);
	MemHandleUnlock(h);
	FldSetTextHandle(fld, h);
	if (old) MemHandleFree(old);
	FldRecalculateField(fld, true);
	if (FrmVisible(FrmGetActiveForm()))
		FldDrawField(fld);
}

void GetFieldEdit(UInt16 id, char *out, UInt16 cap)
{
	char *p = FldGetTextPtr(GetField(id));
	out[0] = '\0';
	if (p) {
		StrNCopy(out, p, cap - 1);
		out[cap - 1] = '\0';
	}
}

void SetPopup(UInt16 popID, UInt16 lstID, Int16 sel, char *label, UInt16 cap)
{
	FormPtr frm = FrmGetActiveForm();
	ListPtr lst = (ListPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, lstID));

	LstSetSelection(lst, sel);
	StrNCopy(label, LstGetSelectionText(lst, sel), cap - 1);
	label[cap - 1] = '\0';
	CtlSetLabel((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, popID)), label);
}

Int16 GetListSel(UInt16 lstID)
{
	FormPtr frm = FrmGetActiveForm();
	return LstGetSelection((ListPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, lstID)));
}

Boolean EditMenu(UInt16 id)
{
	FormPtr frm = FrmGetActiveForm();
	UInt16 ix = FrmGetFocus(frm);
	FieldPtr fld = NULL;

	if (ix != noFocus && FrmGetObjectType(frm, ix) == frmFieldObj)
		fld = (FieldPtr) FrmGetObjectPtr(frm, ix);
	switch (id) {
	case mniEditUndo:		if (fld) FldUndo(fld); return true;
	case mniEditCut:		if (fld) FldCut(fld); return true;
	case mniEditCopy:		if (fld) FldCopy(fld); return true;
	case mniEditPaste:		if (fld) FldPaste(fld); return true;
	case mniEditSelectAll:	if (fld) FldSetSelection(fld, 0, FldGetTextLength(fld)); return true;
	case mniEditKeyboard:	SysKeyboardDialog(kbdDefault); return true;
	case mniEditGraffiti:	SysGraffitiReferenceDialog(referenceDefault); return true;
	}
	return false;
}

void Info(const char *a, const char *b, const char *c)
{
	FrmCustomAlert(altInfo, a, b ? b : "", c ? c : "");
	RedrawActive();
}

Boolean Ask(const char *a, const char *b, const char *c)
{
	Boolean yes = FrmCustomAlert(altAsk, a, b ? b : "", c ? c : "") == 0;

	RedrawActive();
	return yes;
}

/* The save-behind of an alert or box can leave the form half repainted, so repaint the whole form. */
static void RedrawActive(void)
{
	FormPtr frm = FrmGetActiveForm();

	if (frm) FrmDrawForm(frm);
}

/* ---- the progress dialog ---------------------------------------------------------------------
 * A real form (frmBusy: title, a few lines of text, Cancel), shown over the screen and taken away again,
 * the way the Syncing form of Bitwarden and CalDAV Sync is. The code that waits calls McPoll(). */
static FormPtr gBusyFrm, gBusyPrev;
static char gBusyTitle[16];
static RectangleType gCancelBtn;			/* the Cancel button of a cancellable dialog */
static Boolean gCancelShown, gCancelled, gDownInCancel;

/* The serial code calls these while it waits: keep the Palm awake and watch for a tap on Cancel. A tap counts
 * only when the pen went down and came up inside the button, so a stray touch passing over it does not. */
void McPoll(void)
{
	EventType ev;

	EvtResetAutoOffTimer();
	if (! gCancelShown) return;
	while (EvtGetEvent(&ev, 0), ev.eType != nilEvent) {
		if (ev.eType == penDownEvent)
			gDownInCancel = RctPtInRectangle(ev.screenX, ev.screenY, &gCancelBtn);
		else if (ev.eType == penUpEvent) {
			if (gDownInCancel && RctPtInRectangle(ev.screenX, ev.screenY, &gCancelBtn)) gCancelled = true;
			gDownInCancel = false;
		}
	}
}

Boolean McCancelled(void)
{
	return gCancelled;
}

static void McClearCancel(void)
{
	gCancelled = false;				/* a Cancel tap is only meant for the wait it was made in */
}

/* Shows the dialog. The title is the first word of the text ("Finding", "Updating", ...). */
static void BusyBox(const char *msg, UInt16 ticks, Boolean cancel)
{
	UInt16 i, n = 0;

	if (gBusyFrm) {						/* one dialog at a time: change the text of the one showing */
		FormPtr f = gBusyFrm;

		FrmEraseForm(f);
		FrmDeleteForm(f);
		gBusyFrm = NULL;
		FrmSetActiveForm(gBusyPrev);
	}
	gBusyPrev = FrmGetActiveForm();
	gBusyFrm = FrmInitForm(frmBusy);
	if (! gBusyFrm) return;
	while (msg[n] && msg[n] != ' ' && n < sizeof(gBusyTitle) - 1) { gBusyTitle[n] = msg[n]; n++; }
	gBusyTitle[n] = '\0';
	FrmSetTitle(gBusyFrm, gBusyTitle);
	FrmSetActiveForm(gBusyFrm);
	{
		FieldPtr fld = (FieldPtr) FrmGetObjectPtr(gBusyFrm, FrmGetObjectIndex(gBusyFrm, fldBusyText));

		FldSetTextPtr(fld, (char *) msg);
		FldRecalculateField(fld, false);
	}
	i = FrmGetObjectIndex(gBusyFrm, btnBusyCancel);
	if (! cancel) FrmHideObject(gBusyFrm, i);
	FrmDrawForm(gBusyFrm);
	gCancelShown = cancel;
	gCancelled = false;
	gDownInCancel = false;
	if (cancel) FrmGetObjectBounds(gBusyFrm, i, &gCancelBtn);
	if (ticks) SysTaskDelay(ticks);
}

void Busy(const char *msg, UInt16 ticks)
{
	BusyBox(msg, ticks, false);
}

/* The same, with a Cancel button: McCancelled() says it was tapped. */
void BusyCancel(const char *msg)
{
	BusyBox(msg, 0, true);
}

void BusyDone(void)
{
	gCancelShown = false;
	if (! gBusyFrm) return;
	FldSetTextPtr((FieldPtr) FrmGetObjectPtr(gBusyFrm, FrmGetObjectIndex(gBusyFrm, fldBusyText)), NULL);
	FrmEraseForm(gBusyFrm);
	FrmDeleteForm(gBusyFrm);
	gBusyFrm = NULL;
	if (gBusyPrev) FrmSetActiveForm(gBusyPrev);
	gBusyPrev = NULL;
}

/* ---- the simulated link ------------------------------------------------------------------- */

/* Cuts a line to fit `px` pixels (with ".." at the end), so the status never reaches the battery. */
static void FitWidth(char *s, Int16 px)
{
	Int16 n = (Int16) StrLen(s);

	FntSetFont(stdFont);
	if (FntCharsWidth(s, n) <= px) return;
	while (n > 0 && FntCharsWidth(s, n) + FntCharsWidth("..", 2) > px) n--;
	s[n] = '.'; s[n + 1] = '.'; s[n + 2] = '\0';
}

/* Linked, but what the node holds (settings, channels, nodes) has not all arrived yet: a few more tries. */
static Boolean Updating(void)
{
	return gWirsaUp && gLinkState == 2 && (gSyncPending || gCfgPending ||
		(gFillTries < 40 && (gCfgStable < 3 || gNConv == 0 || gNNodes == 0 || gWantNodes || gWantChans)));
}

void StatusLine(char *gInfoLine, Int16 px)
{
	char t[32];

	if (gPollFailed) {
		StrCopy(gInfoLine, gLinkText);
	} else if (! gWirsaUp) {
		StrCopy(gInfoLine, "Not connected");
	} else if (gLinkState == 0) {
		StrCopy(gInfoLine, "No node paired");
	} else if (gLinkState == 2) {
		if (Updating()) {
			StrCopy(gInfoLine, "Updating...");
		} else {
			StrCopy(gInfoLine, "Linked ");
			FmtClock(gLastSync, t);
			StrCat(gInfoLine, t);
		}
	} else {
		StrCopy(gInfoLine, gLinkState == 3 ? "Link failed" : "Disconnected");
	}
	FitWidth(gInfoLine, px);
}

/* Battery of the paired node: a bar and the percentage, right aligned in the gadget. */
Boolean BattGadget(FormGadgetType *g, UInt16 cmd, void *param)
{
	RectangleType r, bar;
	char t[8];
	Int16 w, pct;

	if (cmd != formGadgetDrawCmd || ! gWirsaUp || gLinkState != 2 || gMyBatt < 0) return true;
	pct = gMyBatt > 100 ? 100 : gMyBatt;
	t[0] = '\0';
	PutNum(t, pct);
	StrCat(t, "%");
	FntSetFont(stdFont);
	w = FntCharsWidth(t, (Int16) StrLen(t));
	r = g->rect;
	WinEraseRectangle(&r, 0);
	WinDrawChars(t, (Int16) StrLen(t), (Coord) (r.topLeft.x + r.extent.x - w), r.topLeft.y - 1);
	RctSetRectangle(&bar, (Coord) (r.topLeft.x + r.extent.x - w - 28), (Coord) (r.topLeft.y + 2), 24, 5);
	WinDrawRectangleFrame(simpleFrame, &bar);
	RctSetRectangle(&bar, (Coord) (bar.topLeft.x + 1), (Coord) (bar.topLeft.y + 1), (Coord) (22 * pct / 100), 3);
	WinDrawRectangle(&bar, 0);
	return true;
}

/* Bring the link up (first time) or fetch what WiRSa holds. Says what went wrong unless quiet. */
static Boolean RunLink(Boolean quiet)
{
	Boolean ok = true;

	if (! gWirsaUp) {
		if (! quiet) BusyCancel("Finding WiRSa and starting Mesh mode...");
		ok = LinkOpen();
	}
	if (ok) ok = LinkSyncStart();				/* the answer is read in the event loop: the screen stays free */
	if (! quiet) BusyDone();
	if (ok) CacheSave();
	gPollFailed = ! ok;
	if (! ok && ! quiet && ! McCancelled()) Info(gLinkText[0] ? gLinkText : "WiRSa did not answer.", NULL, NULL);
	return ok;
}

/* ---- frmMain ----------------------------------------------------------------------------- */

void DoSync(void)
{
	Boolean ok;

	if (! gWirsaUp) { Info("Not connected.", "Use Options > Connect first.", NULL); return; }
	BusyCancel("Asking the node for news...");		/* WiRSa looks at the node now; its own schedule goes on */
	ok = LinkNodeNow();
	BusyDone();
	if (! ok && McCancelled()) return;
	if (! ok) { Info(gLinkText[0] ? gLinkText : "WiRSa did not answer.", NULL, NULL); return; }
	RunLink(false);
	FrmUpdateForm(FrmGetActiveFormID(), updRefresh);
}

/* The node's settings come from WiRSa first, so the pages show what the node has now. */
void OpenSetup(void)
{
	Boolean ok;

	if (! gWirsaUp) { Info("Not connected.", "Use Options > Connect first.", NULL); return; }
	BusyCancel("Updating settings from the node...");
	ok = LinkCfgGet();
	BusyDone();
	if (! ok && McCancelled()) return;
	if (! ok) { Info(gLinkText[0] ? gLinkText : "WiRSa did not answer.", NULL, NULL); return; }
	FrmPopupForm(frmSetup);
}

void DoConnect(void)
{
	if (gWirsaUp) { Info("Already connected.", NULL, NULL); return; }
	RunLink(false);
	FrmUpdateForm(FrmGetActiveFormID(), updRefresh);
}

/* Hangs up: Bye to WiRSa (in Session mode that ends Mesh mode), the serial port is released. */
void DoDisconnect(void)
{
	if (! gWirsaUp) { Info("Not connected.", NULL, NULL); return; }
	CacheSave();
	LinkClose();
	gPollFailed = false;
	FrmUpdateForm(FrmGetActiveFormID(), updRefresh);
}


/* ---- scrolling lists ------------------------------------------------------------------------ */

/* The hardware Up/Down buttons page the list on the setup forms that have one. */
static Boolean PageList(WChar chr)
{
	static const UInt16 kForm[2] = { frmChList, frmRelay };
	static const UInt16 kList[2] = { lstChan, lstScan };
	FormPtr frm;
	ListPtr lst;
	UInt16 i, id = FrmGetActiveFormID();
	Int16 vis;

	if (chr != pageUpChr && chr != pageDownChr) return false;
	for (i = 0; i < 2; i++) {
		if (kForm[i] != id) continue;
		frm = FrmGetActiveForm();
		lst = (ListPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, kList[i]));
		vis = LstGetVisibleItems(lst);
		LstScrollList(lst, chr == pageDownChr ? winDown : winUp, (Int16) (vis > 1 ? vis - 1 : 1));
		return true;
	}
	return false;
}

/* ---- frmPrefs ------------------------------------------------------------------------------ */

static char gBaudLabel[8];

Boolean PrefsHandleEvent(EventPtr e)
{
	FormPtr frm;
	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		SetPopup(popPrefBaud, lstPrefBaud, gPrefs.baud % 7, gBaudLabel, sizeof(gBaudLabel));
		CtlSetValue((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, ckPrefAuto)), gPrefs.autoMode ? 1 : 0);
		SetFieldPtr(fldPrefNote1, "Finds WiRSa, picks the speed and starts the mode on its own.");
		FrmDrawForm(frm);
		return true;

	case popSelectEvent:
		if (e->data.popSelect.listID == lstPrefBaud && e->data.popSelect.selection >= 0) {
			SetPopup(popPrefBaud, lstPrefBaud, e->data.popSelect.selection, gBaudLabel, sizeof(gBaudLabel));
			return true;
		}
		break;

	case frmCloseEvent:
		FldSetTextPtr(GetField(fldPrefNote1), NULL);
		break;

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnPrefOk:
			frm = FrmGetActiveForm();
			{
				UInt8 oldBaud = gPrefs.baud;
				Boolean oldAuto = gPrefs.autoMode;

				gPrefs.baud = (UInt8) GetListSel(lstPrefBaud);
				gPrefs.autoMode = CtlGetValue((ControlPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, ckPrefAuto))) != 0;
				PrefsSave();
				if (gWirsaUp && (gPrefs.baud != oldBaud || gPrefs.autoMode != oldAuto)) {
					LinkClose();				/* the next Sync connects again with the new settings */
					gPollFailed = false;
				}
			}
			FrmReturnToForm(frmMain);
			FrmUpdateForm(frmMain, updRefresh);
			return true;
		case btnPrefCancel:
			FrmReturnToForm(frmMain);
			return true;
		}
		break;

	default:
		break;
	}
	return false;
}

/* ---- numeric keypad ----------------------------------------------------------------------- *
 * The same keypad as Sec35's and BitWarden's: ten digit keys and Del as GADGETs drawn by hand
 * (a gadget fires no ctlSelectEvent, so the pen is tracked here: a key inverts on pen down and counts
 * on pen up if the pen is still on it), the digits echoed centred above, OK and Cancel as buttons. */

#define KEY_DIGITS		10
static const UInt16 kKeyIds[KEY_DIGITS] = {
	gadKey0, gadKey1, gadKey2, gadKey3, gadKey4, gadKey5, gadKey6, gadKey7, gadKey8, gadKey9
};

static void KeyDraw(FormPtr frm, UInt16 gadgetId, const char *label, Boolean inverted)
{
	RectangleType r;
	FontID prev;
	Coord w, h;

	FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, gadgetId), &r);
	WinEraseRectangle(&r, 3);
	WinDrawRectangleFrame(((3 << 8) | 1), &r);
	prev = FntSetFont(boldFont);
	w = FntCharsWidth((Char *) label, (Int16) StrLen(label));
	h = FntLineHeight();
	if (inverted) {
		RectangleType inv = r;
		inv.topLeft.x += 1; inv.topLeft.y += 1;
		inv.extent.x -= 2; inv.extent.y -= 2;
		WinInvertRectangle(&inv, 2);
	}
	WinDrawChars((Char *) label, (Int16) StrLen(label), (Coord) (r.topLeft.x + (r.extent.x - w) / 2),
		(Coord) (r.topLeft.y + (r.extent.y - h) / 2));
	FntSetFont(prev);
}

/* 0..9 for a digit key, -1 for Del, -2 for no key. */
static Int16 KeyHit(FormPtr frm, Coord x, Coord y)
{
	UInt16 i;
	RectangleType r;

	for (i = 0; i < KEY_DIGITS; i++) {
		FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, kKeyIds[i]), &r);
		if (x >= r.topLeft.x && x < r.topLeft.x + r.extent.x && y >= r.topLeft.y && y < r.topLeft.y + r.extent.y)
			return (Int16) i;
	}
	FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, gadKeyDel), &r);
	if (x >= r.topLeft.x && x < r.topLeft.x + r.extent.x && y >= r.topLeft.y && y < r.topLeft.y + r.extent.y)
		return -1;
	return -2;
}

static void KeyRedraw(FormPtr frm, Int16 key, Boolean inverted)
{
	char label[2];

	if (key >= 0) {
		label[0] = (char) ('0' + key);
		label[1] = '\0';
		KeyDraw(frm, kKeyIds[key], label, inverted);
	} else if (key == -1) {
		KeyDraw(frm, gadKeyDel, "Del", inverted);
	}
}

static void KeyEcho(FormPtr frm, const char *text)
{
	RectangleType r;
	FontID prev;

	FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, fldKeyDisplay), &r);
	WinEraseRectangle(&r, 0);
	prev = FntSetFont(boldFont);
	WinDrawChars((Char *) text, (Int16) StrLen(text),
		(Coord) (r.topLeft.x + (r.extent.x - FntCharsWidth((Char *) text, (Int16) StrLen(text))) / 2),
		(Coord) (r.topLeft.y + (r.extent.y - FntLineHeight()) / 2));
	FntSetFont(prev);
}

/* Ask for a number. buf holds the current digits (at most maxLen) and gets the new ones when OK is
 * tapped. title must stay valid: use the label of the field the number goes in. */
Boolean NumPad(char *buf, UInt16 maxLen, const char *title)
{
	FormPtr frm, prevFrm;
	EventType event;
	char digits[9];
	UInt16 len, i, btn = 0;
	Int16 active = -2;

	if (maxLen > 8) maxLen = 8;
	prevFrm = FrmGetActiveForm();
	frm = FrmInitForm(frmKeypad);
	if (! frm) return false;
	FrmSetActiveForm(frm);
	FrmSetTitle(frm, (char *) title);
	StrNCopy(digits, buf, maxLen);
	digits[maxLen] = '\0';
	len = (UInt16) StrLen(digits);

	FrmDrawForm(frm);
	KeyEcho(frm, digits);
	for (i = 0; i < KEY_DIGITS; i++)
		KeyRedraw(frm, (Int16) i, false);
	KeyRedraw(frm, -1, false);

	while (! btn) {
		EvtGetEvent(&event, evtWaitForever);
		if (event.eType == appStopEvent) {
			EvtAddEventToQueue(&event);
			btn = btnKeyCancel;
			break;
		}
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, NULL)) continue;

		if (event.eType == penDownEvent) {
			Int16 hit = KeyHit(frm, event.screenX, event.screenY);
			if (hit != -2) {
				active = hit;
				KeyRedraw(frm, active, true);
				SndPlaySystemSound(sndClick);
			}
		} else if (event.eType == penMoveEvent && active != -2) {
			if (KeyHit(frm, event.screenX, event.screenY) != active) {
				KeyRedraw(frm, active, false);
				active = -2;
			}
		} else if (event.eType == penUpEvent) {
			if (active != -2) {
				KeyRedraw(frm, active, false);
				if (active >= 0) {
					if (len < maxLen) {
						digits[len++] = (char) ('0' + active);
						digits[len] = '\0';
						KeyEcho(frm, digits);
					}
				} else if (len > 0) {
					digits[--len] = '\0';
					KeyEcho(frm, digits);
				}
				active = -2;
			}
		} else if (event.eType == ctlSelectEvent) {
			if (event.data.ctlSelect.controlID == btnKeyOk) btn = btnKeyOk;
			else if (event.data.ctlSelect.controlID == btnKeyCancel) btn = btnKeyCancel;
		}
		FrmHandleEvent(frm, &event);
	}

	if (btn == btnKeyOk)
		StrCopy(buf, digits);
	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prevFrm) {
		FrmSetActiveForm(prevFrm);
		FrmDrawForm(prevFrm);
	}
	return btn == btnKeyOk;
}

/* ---- app ------------------------------------------------------------------------------------ */

static Boolean AppHandleEvent(EventPtr e)
{
	if (e->eType == frmLoadEvent) {
		UInt16 id = e->data.frmLoad.formID;
		FormPtr frm = FrmInitForm(id);
		FrmSetActiveForm(frm);
		switch (id) {
		case frmMain:		FrmSetEventHandler(frm, MainHandleEvent); break;
		case frmChat:		FrmSetEventHandler(frm, ChatHandleEvent); break;
		case frmNode:		FrmSetEventHandler(frm, NodeHandleEvent); break;
		case frmMsg:		FrmSetEventHandler(frm, MsgHandleEvent); break;
		case frmPos:		FrmSetEventHandler(frm, PosHandleEvent); break;
		case frmPrefs:		FrmSetEventHandler(frm, PrefsHandleEvent); break;
		case frmSetup:		FrmSetEventHandler(frm, SetupHandleEvent); break;
		case frmOwner:		FrmSetEventHandler(frm, OwnerHandleEvent); break;
		case frmLora:		FrmSetEventHandler(frm, LoraHandleEvent); break;
		case frmDevice:		FrmSetEventHandler(frm, DeviceHandleEvent); break;
		case frmChList:		FrmSetEventHandler(frm, ChListHandleEvent); break;
		case frmChEdit:		FrmSetEventHandler(frm, ChEditHandleEvent); break;
		case frmActions:	FrmSetEventHandler(frm, ActionsHandleEvent); break;
		case frmRelay:		FrmSetEventHandler(frm, RelayHandleEvent); break;
		case frmClear:		FrmSetEventHandler(frm, ClearHandleEvent); break;
		}
		return true;
	}
	return false;
}

/* Changes when news arrives: new messages, or a sent one moving on. */
static UInt32 Signature(void)
{
	UInt32 v = gCursor * 31 + gNMsg * 7 + gNNodes;
	UInt16 i;

	for (i = 0; i < gNMsg; i++) v += gMsg[i].status * (i + 1) + (gMsg[i].out ? 0 : 1);
	return v + gLinkState * 1000;
}

static void EventLoop(void)
{
	EventType e;
	UInt16 err;

	do {
		UInt16 fid = FrmGetActiveFormID();

		McClearCancel();
		/* With the link up the wait is short only to look at the local serial buffer: nothing is asked of
		 * WiRSa, it tells the Palm when there is news. */
		EvtGetEvent(&e, gWirsaUp ? 20 : evtWaitForever);
		if (e.eType == nilEvent) {
			if (! gWirsaUp) continue;
			if (LinkAsyncCheck()) { gPollFailed = true; FrmUpdateForm(fid, updRefresh); }
			if (Updating() && ! gSyncPending && ! gCfgPending && (fid == frmMain || fid == frmChat) && (Int32) (TimGetTicks() - gNextFill) >= 0) {
				Coord px, py;
				Boolean down;

				gNextFill = TimGetTicks() + 3 * SysTicksPerSecond();
				EvtGetPen(&px, &py, &down);
				if (down || EvtEventAvail()) continue;
				gFillTries++;						/* asked again, a few times, without waiting for the answer */
				gPollFailed = ! LinkSyncStart();
				continue;
			}
			if (gNodesRefresh && fid == frmMain) {		/* the page is drawn: now fetch the node list */
				gNodesRefresh = false;
				gWantNodes = true;
				if (! gSyncPending && ! gCfgPending) LinkSyncStart();
				continue;
			}
			/* EvtGetEvent can return early (the serial driver wakes it), so look at the clock too */
			if ((Int32) (TimGetTicks() - gNextCheck) >= 0) {
				UInt16 got;
				Coord px, py;
				Boolean down;

				gNextCheck = TimGetTicks() + 10;
				EvtGetPen(&px, &py, &down);
				if (down || EvtEventAvail()) continue;		/* never start work while the user is touching the screen */
				if (RpPending() == 0) continue;
				{
					UInt32 before = Signature();

					got = LinkPump();
					if (got & 16) gPollFailed = true;
					if (got & 8) gPollFailed = false;
					if (got & 1) gNewsAgain = true;
					if (gNewsAgain && ! gSyncPending && ! gCfgPending) {	/* WiRSa has news: fetch just that */
						gNewsAgain = false;
						gPollFailed = ! LinkSyncStart();
					}
					if (got & 12) { CacheSave(); if (fid == frmMain || fid == frmChat) FrmUpdateForm(fid, updRefresh); }	/* settings, channels, nodes: kept for the next launch */
					else if (got & 32) {					/* the link to the node changed: say so at once */
						if (fid == frmMain || fid == frmChat) FrmUpdateForm(fid, updRefresh);
					}
					else if (got && Signature() != before) {
						CacheSave();
						if (fid == frmMain || fid == frmChat) FrmUpdateForm(fid, updRefresh);
					}
				}
			}
			continue;
		}
		if (e.eType == keyDownEvent && PageList(e.data.keyDown.chr)) continue;
		if (SysHandleEvent(&e)) continue;
		if (MenuHandleEvent(0, &e, &err)) continue;
		if (AppHandleEvent(&e)) continue;
		FrmDispatchEvent(&e);
		/* the field posts no change event for a one-line edit, so the counters follow the keys and menu */
		if (e.eType == keyDownEvent || e.eType == menuEvent) {
			UInt16 id = FrmGetActiveFormID();
			if (id == frmChat) ChatKeyPoll();
			else if (id == frmMain) MainFindPoll();
		}
	} while (e.eType != appStopEvent);
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
	if (cmd == sysAppLaunchCmdNormalLaunch) {
		DataInit();
		FrmGotoForm(frmMain);
		EventLoop();
		FrmCloseAllForms();
		CacheSave();
		LinkClose();
	}
	return 0;
}
