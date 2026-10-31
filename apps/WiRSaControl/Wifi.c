/*
 * Wifi.c - frmWifi: scan for networks, pick one (or type a hidden SSID),
 * enter the password (masked; "Show password" reveals it) and Connect.
 * The password goes over the encrypted link (WCtlSecure); WiRSa only saves
 * the network if it connects.
 */
#include <PalmOS.h>
#include "WiRSaControl.h"
#include "relayproto.h"
#include "relaywire.h"

#define kMaxNets	16

static char gNetSsid[kMaxNets][33];
static char gNetText[kMaxNets][48];
static char *gNetPtrs[kMaxNets];
static UInt16 gNumNets;
static WCMasked gPass;

static ListPtr WifiList(void)
{
	FormPtr frm = FrmGetActiveForm();
	return (ListPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, lstWifiNets));
}

static void WifiScan(void)
{
	static UInt8 rep[kMaxNets * 40 + 1];	/* static: too big for the stack */
	UInt16 n, i, count;
	RpR r;
	UInt8 res;

	BusyShow("Scanning...");
	res = WCtlBegin(SweepOK());
	if (res == wctlOk) {
		res = WCtlRequest(wctlTypeWifiScan, NULL, 0, rep, sizeof(rep), &n, SysTicksPerSecond() * 20);
		WCtlEnd();
	}
	BusyHide();
	if (res != wctlOk) { ShowResult(res); return; }

	r.buf = rep; r.len = n; r.pos = 0; r.ok = true;
	count = RpR8(&r);
	gNumNets = 0;
	for (i = 0; i < count && gNumNets < kMaxNets; i++) {
		char num[12];
		Int8 rssi;
		UInt8 open;
		RpRStr(&r, gNetSsid[gNumNets], sizeof(gNetSsid[0]));
		rssi = (Int8) RpR8(&r);
		open = RpR8(&r);
		if (! r.ok) break;
		StrNCopy(gNetText[gNumNets], gNetSsid[gNumNets], 30);
		gNetText[gNumNets][30] = '\0';
		StrPrintF(num, "  %d%s", (Int16) rssi, open ? " open" : "");
		StrCat(gNetText[gNumNets], num);
		gNetPtrs[gNumNets] = gNetText[gNumNets];
		gNumNets++;
	}
	LstSetListChoices(WifiList(), gNetPtrs, gNumNets);
	LstSetSelection(WifiList(), noListSelection);
	LstDrawList(WifiList());
	if (gNumNets == 0)
		FrmCustomAlert(altInfo, "No networks found.", NULL, NULL);
}

static void WifiConnect(void)
{
	char ssid[33];
	UInt8 body[2 + 32 + 2 + 64], rep[8];
	UInt16 n;
	RpW w;
	UInt8 r;

	GetFieldEdit(fldWifiSsid, ssid, sizeof(ssid));
	if (! ssid[0]) {
		FrmCustomAlert(altError, "Enter or pick a network (SSID) first.", NULL, NULL);
		return;
	}
	w.buf = body; w.cap = sizeof(body); w.len = 0;
	RpWStr(&w, ssid);
	RpWStr(&w, gPass.real);

	BusyShow("Connecting...");
	r = WCtlBegin(SweepOK());
	if (r == wctlOk) {
		r = WCtlSecure();
		if (r == wctlOk) {
			/* WiRSa tests the network before saving it: allow 40 s */
			r = WCtlRequest(wctlTypeWifiSet, body, w.len, rep, sizeof(rep), &n, SysTicksPerSecond() * 40);
			if (r == wctlOk && (n < 1 || rep[0] != 1)) {
				r = wctlErrReply;
				gWCtlErrCode = wctlErrFailed;
				StrCopy(gWCtlErrMsg, "WiRSa could not connect to that network. Nothing was saved.");
			}
		}
		WCtlUnsecure();
		WCtlEnd();
	}
	MemSet(body, sizeof(body), 0);
	BusyHide();
	if (r != wctlOk) { ShowResult(r); return; }

	FrmCustomAlert(altInfo, "Connected. WiRSa saved the network.", NULL, NULL);
	MemSet(&gPass, sizeof(gPass), 0);
	FrmReturnToForm(frmMain);
	FrmUpdateForm(frmMain, updRefresh);
}

Boolean WifiHandleEvent(EventPtr e)
{
	FormPtr frm;

	switch (e->eType) {
	case frmOpenEvent:
		frm = FrmGetActiveForm();
		MemSet(&gPass, sizeof(gPass), 0);
		gPass.fieldID = fldWifiPass;
		gNumNets = 0;
		LstSetListChoices(WifiList(), gNetPtrs, 0);
		if (gStatus.valid && gStatus.ssid[0])
			SetFieldEdit(fldWifiSsid, gStatus.ssid);
		FrmDrawForm(frm);
		FrmSetFocus(frm, FrmGetObjectIndex(frm, fldWifiSsid));
		return true;

	case lstSelectEvent:
		if (e->data.lstSelect.listID == lstWifiNets && e->data.lstSelect.selection >= 0
				&& e->data.lstSelect.selection < (Int16) gNumNets) {
			SetFieldEdit(fldWifiSsid, gNetSsid[e->data.lstSelect.selection]);
			frm = FrmGetActiveForm();
			FrmSetFocus(frm, FrmGetObjectIndex(frm, fldWifiPass));
			return true;
		}
		break;

	case keyDownEvent:
		frm = FrmGetActiveForm();
		if (FrmGetFocus(frm) == FrmGetObjectIndex(frm, fldWifiPass) && MaskKey(&gPass, e))
			return true;
		break;

	case menuEvent:
		return EditMenu(e->data.menu.itemID, &gPass, 1);

	case ctlSelectEvent:
		switch (e->data.ctlSelect.controlID) {
		case btnWifiScan:		WifiScan(); return true;
		case btnWifiConnect:	WifiConnect(); return true;
		case chkWifiShow:
			gPass.show = e->data.ctlSelect.on;
			MaskRedraw(&gPass);
			return true;
		case btnWifiCancel:
			MemSet(&gPass, sizeof(gPass), 0);
			FrmReturnToForm(frmMain);
			return true;
		}
		break;

	default:
		break;
	}
	return false;
}
