/*
 * Link.c - the WiRSa link: the real calls behind the screens. Wire format: WiRSa's docs/Mesh-Palm.md.
 * Port, key exchange and framing are the shared relay code (../lib/relayproto.c, relaysec.c); WiRSa is
 * found and its Mesh mode started with wirsactl.c, as CalDAV Sync and Bitwarden do.
 *
 * The session stays open while the app runs (one request at a time, each with its own timeout);
 * WiRSa keeps collecting messages from the node when the app is closed.
 */
#include "MeshChat.h"
#include "relayproto.h"
#include "relaywire.h"
#include "relaysec.h"
#include "wirsactl.h"

/* Palm -> WiRSa */
#define mwHello		0x01
#define mwState		0x03
#define mwScan		0x05
#define mwPair		0x07
#define mwSync		0x09
#define mwAck		0x0A
#define mwBye		0x0B
#define mwSend		0x0F
#define mwRelayGet	0x11
#define mwRelaySet	0x13
#define mwClear		0x15
#define mwUnpair	0x17
#define mwConnect	0x19
#define mwReq		0x24
#define mwPin		0x1B
/* WiRSa -> Palm */
#define mwHelloAck	0x02
#define mwStateR	0x04
#define mwScanR		0x06
#define mwPairR		0x08
#define mwNode		0x0C
#define mwChan		0x0D
#define mwMsg		0x0E
#define mwSendR		0x10
#define mwRelayR	0x12
#define mwSyncDone	0x14
#define mwDone		0x16
#define mwDeliv		0x18
#define mwPinNeed	0x1A
#define mwNews		0x1C
#define mwCfgGet	0x20
#define mwCfgR		0x21
#define mwCfgSet	0x22
#define mwCfgAct	0x23
#define mwPing		0x1D
#define mwErr		0x1F

#define mfOut		1
#define mfDm		2
#define broadcastNum	0xFFFFFFFFUL

UInt8 gLinkState;				/* the node link as WiRSa reports it */
Boolean gWirsaUp;				/* the serial session is open and secured */
char gLinkText[48];
Boolean gCfgLoaded;				/* the node's settings (channels, names) were fetched in this session */
Boolean gSyncPending, gCfgPending, gNewsAgain;		/* a SYNC / CFG request is out and its answer is picked up by LinkPump */
UInt8 gCfgChans, gCfgStable;		/* channels in the last settings read, and how many reads in a row gave that many */
UInt8 gFillTries;				/* how often the data was asked for again while the lists were still empty */
Boolean gWantNodes = true, gWantChans = true;	/* what the next sync must fetch besides messages */
UInt32 gCursor;					/* highest message seq applied */
UInt32 gLastPollUtc;
UInt32 gWirsaUtc;				/* WiRSa's clock at the last STATE, 0 = unknown */
UInt16 gWirsaStored;			/* messages WiRSa holds */
UInt16 gWirsaNodes;
char gFirmware[16];
static UInt32 gLocalId;
static UInt8 gRx[960];
static UInt16 gRxLen;
static UInt8 gTx[640];

MC_SEC static Boolean CfgParse(void);

MC_SEC static void SetText(const char *s)
{
	StrNCopy(gLinkText, s, sizeof(gLinkText) - 1);
	gLinkText[sizeof(gLinkText) - 1] = '\0';
}

/* Drop the session without saying Bye (a Bye ends Mesh mode in Session mode). */
MC_SEC static void LinkDrop(void)
{
	gWirsaUp = false;
	RpClose();
	gRpKexReplyTicks = 0;
}

/* ---- one request / reply ---------------------------------------------------------------------- */

/* Sends a frame; waits up to waitSec for one of type want (others are dropped; an ERR frame fails the
 * call with WiRSa's text). The reply body is left in gRx. */
MC_SEC static Boolean Req(UInt8 type, const UInt8 *body, UInt16 len, UInt8 want, UInt16 waitSec)
{
	UInt8 t;
	UInt32 until = TimGetTicks() + SecTicks(waitSec);

	if (! gWirsaUp) { SetText("Not connected to WiRSa."); return false; }
	gSyncPending = false; gCfgPending = false;		/* the flush below drops their answers */
	RpFlushInput();
	if (! RpSendFrame(type, body, len)) { SetText("Could not send to WiRSa."); gWirsaUp = false; return false; }
	for (;;) {
		gRpIdleOverrideTicks = SecTicks(waitSec);
		if (! RpRecvFrame(&t, gRx, sizeof(gRx), &gRxLen)) {
			SetText("WiRSa did not answer.");
			LinkDrop();					/* the next call opens the link afresh */
			return false;
		}
		if (t == want) return true;
		if (t == mwErr) {
			RpR r;
			r.buf = gRx; r.len = gRxLen; r.pos = 0; r.ok = true;
			RpR8(&r);
			RpRStr(&r, gLinkText, sizeof(gLinkText));
			return false;
		}
		if ((Int32) (TimGetTicks() - until) > 0) { SetText("WiRSa did not answer."); return false; }
	}
}

/* ---- opening the session ---------------------------------------------------------------------- */

MC_SEC Boolean LinkOpen(void)
{
	UInt8 r, sr;
	RpW w;
	UInt8 body[8];
	UInt32 neg;
	RpR rd;
	UInt32 baud = BaudOf(gPrefs.baud);

	if (gWirsaUp) return true;
	gLinkText[0] = '\0';
	if (gPrefs.autoMode) {
		r = WCtlEnsureMode('T', baud);
		if (r == wctlErrReply) { SetText(gWCtlErrMsg[0] ? gWCtlErrMsg : "WiRSa refused to start Mesh mode."); return false; }
		if (r == wctlModeNotUp) { SetText("WiRSa did not switch to Mesh mode."); return false; }
		if (r != wctlOk) { SetText(r == wctlPortBusy ? "The serial port is in use." : "WiRSa did not answer."); return false; }
		gRpKexReplyTicks = SecTicks(5);
	}
	if (! RpOpen()) { SetText("Could not open the serial port."); return false; }
	sr = RpSecure();
	if (sr != rpSecOk) {
		RpClose();
		SetText(sr == rpSecFailed ? "The link to WiRSa cannot be secured." : "WiRSa did not answer. Is Mesh mode running?");
		return false;
	}
	gWirsaUp = true;
	gCfgLoaded = false; gFillTries = 0; gCfgChans = 0xFF; gCfgStable = 0;
	w.buf = body; w.cap = sizeof(body); w.len = 0;
	RpW32(&w, baud);
	if (! Req(mwHello, body, w.len, mwHelloAck, 5)) { LinkDrop(); return false; }
	rd.buf = gRx; rd.len = gRxLen; rd.pos = 0; rd.ok = true;
	RpR8(&rd); RpR8(&rd);
	{ char name[24]; RpRStr(&rd, name, sizeof(name)); }
	neg = RpR32(&rd);
	if (! rd.ok || neg == 0) { SetText("WiRSa sent an unreadable reply."); LinkDrop(); return false; }
	RpSetBaud(neg);
	return true;
}

MC_SEC void LinkClose(void)
{
	if (gWirsaUp) RpSendFrame(mwBye, NULL, 0);
	gWirsaUp = false;
	RpClose();
	gRpKexReplyTicks = 0;
}

/* ---- the nodes, channels and messages WiRSa holds ----------------------------------------------- */

MC_SEC UInt16 NodeByNum(UInt32 num, Boolean create)
{
	UInt16 i;

	for (i = 0; i < gNNodes; i++)
		if (gNodes[i].num == num) return i;
	if (! create || gNNodes >= MAX_NODE) return 0xFFFF;
	MemSet(&gNodes[gNNodes], sizeof(Node), 0);
	gNodes[gNNodes].num = num;
	gNodes[gNNodes].batt = -1;
	gNodes[gNNodes].hops = 0;
	StrCopy(gNodes[gNNodes].id, "!");
	StrPrintF(gNodes[gNNodes].id + 1, "%08lx", (UInt32) num);
	StrCopy(gNodes[gNNodes].longName, gNodes[gNNodes].id);
	StrCopy(gNodes[gNNodes].shortName, gNodes[gNNodes].id + 5);		/* until the node names itself: the end of its id */
	return gNNodes++;
}

MC_SEC static void ApplyNode(RpR *r)
{
	UInt32 num = RpR32(r);
	char id[12], lng[40], sht[8];
	UInt8 hw, role, hasSnr, hasPos, isThis, fav;
	Int8 snr, hops, batt;
	UInt32 heard, uptime;
	UInt16 util, air;
	Int32 lat, lon;
	UInt16 i;
	Node *n;

	RpRStr(r, id, sizeof(id)); RpRStr(r, lng, sizeof(lng)); RpRStr(r, sht, sizeof(sht));
	hw = RpR8(r); role = RpR8(r); snr = (Int8) RpR8(r); hasSnr = RpR8(r);
	heard = RpR32(r); hops = (Int8) RpR8(r); batt = (Int8) RpR8(r); uptime = RpR32(r);
	util = RpR16(r); air = RpR16(r); hasPos = RpR8(r); lat = (Int32) RpR32(r); lon = (Int32) RpR32(r);
	isThis = RpR8(r);
	fav = r->pos < r->len ? RpR8(r) : 0;			/* newer WiRSa: the node's favourite flag */
	if (! r->ok) return;
	i = NodeByNum(num, true);
	if (i == 0xFFFF) return;
	n = &gNodes[i];
	if (id[0]) StrNCopy(n->id, id, sizeof(n->id) - 1);
	if (lng[0]) StrNCopy(n->longName, lng, sizeof(n->longName) - 1);
	if (sht[0]) StrNCopy(n->shortName, sht, sizeof(n->shortName) - 1);
	n->hwNum = hw; n->roleNum = role; n->fav = fav != 0;
	n->snr = hasSnr ? snr : 0;
	n->hops = hops < 0 ? 0 : (UInt8) hops;
	n->batt = batt;
	n->heard = SecToStamp(heard);
	n->uptime = uptime; n->util = util; n->air = air;
	if (hasPos) LatLon(n->pos, lat, lon);
	(void) isThis;						/* WiRSa sends this node first, so it is index 0 */
}

MC_SEC static void ApplyChan(RpR *r)
{
	UInt8 idx = RpR8(r), role = RpR8(r);
	char name[16];

	RpRStr(r, name, sizeof(name));
	if (! r->ok || idx >= MAX_CHAN) return;
	StrCopy(gNodeCfg.chan[idx].name, name[0] ? name : (idx == 0 ? "Primary" : "Channel"));
	gNodeCfg.chan[idx].role = role;
	if (role) FindChan(idx, true);
}

MC_SEC static void ApplyMsg(RpR *r)
{
	UInt32 seq = RpR32(r);
	UInt8 chan = RpR8(r), flags = RpR8(r), status = RpR8(r);
	UInt32 from = RpR32(r), to = RpR32(r), rx = RpR32(r), localId = RpR32(r);
	char text[TXT_MAX];
	UInt16 conv, fi;
	Stamp when;
	Msg *m;
	UInt16 k;

	UInt8 err;

	RpRStr(r, text, sizeof(text));
	if (! r->ok) return;
	err = r->pos < r->len ? RpR8(r) : 0;
	if (seq > gCursor) gCursor = seq;
	if (flags & mfOut) {
		for (k = 0; k < gNMsg; k++)
			if (gMsg[k].out && localId && gMsg[k].localId == localId) {	/* ours: only its progress is news */
				gMsg[k].seq = seq; gMsg[k].status = status; gMsg[k].err = err;
				return;
			}
	}
	when = rx ? SecToStamp(rx) : NowUtc();
	if (rx && ! (flags & mfOut)) {					/* WiRSa started over and handed on what this Palm already holds */
		UInt8 fn = (UInt8) NodeByNum(from, false);

		for (k = 0; k < gNMsg; k++)
			if (gMsg[k].when == when && ! gMsg[k].out && gMsg[k].from == fn && StrCompare(MsgText(&gMsg[k]), text) == 0) return;
	}
	if (flags & mfDm) {
		UInt16 peer = NodeByNum((flags & mfOut) ? to : from, true);
		if (peer == 0xFFFF) return;
		conv = FindDm(peer, true);
	} else {
		conv = FindChan(chan, true);
	}
	if (conv == 0xFFFF) return;
	fi = NodeByNum(from, true);
	if (fi == 0xFFFF) fi = 0;
	m = AddMsg(conv, (flags & mfOut) != 0, (UInt8) fi, text, when);
	m->seq = seq; m->localId = localId; m->status = status; m->err = err;
	if (! (flags & mfOut) && ! (FrmGetActiveFormID() == frmChat && gCurConv == conv))
		gConv[conv].unread++;
}

MC_SEC static void Ack(void)
{
	RpSendFrame(mwAck, NULL, 0);
}

static UInt32 gAsyncTick;				/* the last time something happened in a pending request */

/* One frame of a sync answer: 0 go on, 1 done, 2 failed. */
MC_SEC static UInt8 SyncFrame(UInt8 t, RpR *r)
{
	UInt16 i;

	if (t == mwNode) { ApplyNode(r); Ack(); }
	else if (t == mwChan) { ApplyChan(r); Ack(); }
	else if (t == mwMsg) { ApplyMsg(r); Ack(); }
	else if (t == mwDeliv) {
		UInt32 id = RpR32(r);
		UInt8 st = RpR8(r);
		UInt8 er = r->pos < r->len ? RpR8(r) : 0;
		for (i = 0; i < gNMsg; i++)
			if (gMsg[i].out && gMsg[i].localId == id) { gMsg[i].status = st; gMsg[i].err = er; }
		Ack();
	}
	else if (t == mwSyncDone) return 1;
	else if (t == mwErr) { RpR8(r); RpRStr(r, gLinkText, sizeof(gLinkText)); return 2; }
	return 0;
}

MC_SEC static void SyncFinish(void)
{
	gLastSync = NowUtc();
	SortConv();
	{	/* the node hands its channels to WiRSa slowly: ask again until the count stops changing (WiRSa answers from its own store) */
		UInt8 ch = 0;
		UInt16 i;

		for (i = 0; i < gNConv; i++) if (gConv[i].isChan) ch++;
		if (ch == gCfgChans) { if (gCfgStable < 255) gCfgStable++; } else { gCfgChans = ch; gCfgStable = 0; }
	}
	gWantNodes = gNNodes == 0;					/* the node may not have handed its lists to WiRSa yet: ask again */
	gWantChans = gNConv == 0 || gCfgStable < 3;
	if (gNNodes > 0) {						/* the Setup pages start from the node's own names */
		StrNCopy(gNodeCfg.longName, gNodes[0].longName, sizeof(gNodeCfg.longName) - 1);
		StrNCopy(gNodeCfg.shortName, gNodes[0].shortName, sizeof(gNodeCfg.shortName) - 1);
	}
}

/* STATE, then the SYNC request is sent; gSyncPending says whether an answer is to come. */
MC_SEC static Boolean SyncBegin(void)
{
	RpR r;
	RpW w;
	UInt32 lastSeq;
	UInt16 i, nPend = 0;

	gSyncPending = false; gCfgPending = false;
	if (! Req(mwState, NULL, 0, mwStateR, 6)) return false;
	r.buf = gRx; r.len = gRxLen; r.pos = 0; r.ok = true;
	gLinkState = RpR8(&r);
	gMyBatt = (Int8) RpR8(&r);
	RpR32(&r);
	RpRStr(&r, gFirmware, sizeof(gFirmware));
	RpRStr(&r, gRelay.node, sizeof(gRelay.node));
	gWirsaNodes = RpR16(&r); gWirsaStored = RpR16(&r);
	lastSeq = RpR32(&r);
	gRelay.mode = RpR8(&r); gRelay.every = RpR8(&r);
	gWirsaUtc = RpR32(&r); gLastPollUtc = RpR32(&r);
	RpRStr(&r, gLinkText, sizeof(gLinkText));
	if (! r.ok) { SetText("WiRSa sent an unreadable state."); return false; }
	if (gCursor > lastSeq) {						/* WiRSa restarted: its numbering began again */
		gCursor = 0;							/* what the Palm holds stays: it is the history */
		gWantNodes = gWantChans = true;
	}
	if (gLinkState == 0) return true;				/* no node paired: nothing to fetch */

	w.buf = gTx; w.cap = sizeof(gTx); w.len = 0;
	RpW32(&w, gCursor);
	for (i = 0; i < gNMsg; i++) if (gMsg[i].out && gMsg[i].localId && (gMsg[i].status == 0 || gMsg[i].status == 1 || gMsg[i].status == 4)) nPend++;
	if (nPend > 24) nPend = 24;
	RpW8(&w, (UInt8) nPend);
	for (i = 0; i < gNMsg && nPend; i++)
		if (gMsg[i].out && gMsg[i].localId && (gMsg[i].status == 0 || gMsg[i].status == 1 || gMsg[i].status == 4)) {
			RpW32(&w, gMsg[i].localId);
			nPend--;
		}
	RpW8(&w, (UInt8) ((gWantNodes ? 1 : 0) | (gWantChans ? 2 : 0)));
	RpFlushInput();
	if (! RpSendFrame(mwSync, gTx, w.len)) { SetText("Could not send to WiRSa."); gWirsaUp = false; return false; }
	gSyncPending = true;
	gAsyncTick = TimGetTicks();
	return true;
}

/* STATE then SYNC, waiting for the answer: everything WiRSa knows that this Palm does not yet. */
MC_SEC Boolean LinkSync(void)
{
	RpR r;
	UInt8 t, res;
	UInt32 start;
	Boolean shown = false;

	if (! SyncBegin()) return false;
	if (! gSyncPending) return true;
	gSyncPending = false;
	start = TimGetTicks();
	for (;;) {
		if (! shown && (Int32) (TimGetTicks() - start) > 60) { Busy("Updating...", 0); shown = true; }	/* a long fetch says so */
		gRpIdleOverrideTicks = SecTicks(10);
		if (! RpRecvFrame(&t, gRx, sizeof(gRx), &gRxLen)) { if (shown) BusyDone(); SetText("WiRSa stopped answering during the sync."); return false; }
		r.buf = gRx; r.len = gRxLen; r.pos = 0; r.ok = true;
		res = SyncFrame(t, &r);
		if (res == 1) break;
		if (res == 2) { if (shown) BusyDone(); return false; }
	}
	if (shown) BusyDone();
	SyncFinish();
	return true;
}

/* The same without waiting: the answer comes through LinkPump, so the screen stays free. */
MC_SEC Boolean LinkSyncStart(void)
{
	if (gSyncPending || gCfgPending) return true;			/* one request at a time */
	return SyncBegin();
}

/* A pending request that has gone quiet is given up. True when one was. */
MC_SEC Boolean LinkAsyncCheck(void)
{
	if (gSyncPending && (Int32) (TimGetTicks() - gAsyncTick) > (Int32) SecTicks(15)) {
		gSyncPending = false;
		SetText("WiRSa stopped answering during the sync.");
		return true;
	}
	if (gCfgPending && (Int32) (TimGetTicks() - gAsyncTick) > (Int32) SecTicks(60)) {
		gCfgPending = false;
		return true;
	}
	return false;
}

/* ---- sending ------------------------------------------------------------------------------------ */

MC_SEC Boolean LinkSend(UInt16 conv, const char *text)
{
	RpW w;
	Msg *m;
	UInt32 to, id;
	UInt8 ch;

	if (! gLocalId) gLocalId = TimGetTicks() | 1;
	id = ++gLocalId;
	if (gConv[conv].isChan) { to = broadcastNum; ch = gConv[conv].index; }
	else { to = gNodes[gConv[conv].index].num; ch = 0; }
	m = AddMsg(conv, true, 0, text, NowUtc());
	m->localId = id;
	m->status = 0;
	w.buf = gTx; w.cap = sizeof(gTx); w.len = 0;
	RpW32(&w, to); RpW8(&w, ch); RpW32(&w, id); RpWStr(&w, text);
	/* Nothing waits for the answer: WiRSa's reply is picked up by LinkPump, so the screen stays free. */
	if (! gWirsaUp || ! RpSendFrame(mwSend, gTx, w.len)) {
		SetText("Not connected to WiRSa.");
		m->status = 3; m->err = 242; SortConv();
		return false;
	}
	SortConv();
	return true;
}

/* A request or a share that is not a text: kind 1 node info, 2 position request, 3 telemetry request, 4 traceroute,
 * 5 share a position (lat, lon in degrees x 1e7), 6 waypoint (also name). It shows in the chat as a bracketed line;
 * the other node's reply comes back as a message. */
MC_SEC Boolean LinkReq(UInt16 conv, UInt8 kind, Int32 lat, Int32 lon, const char *name)
{
	RpW w;
	Msg *m;
	UInt32 to, id;
	UInt8 ch;
	char label[TXT_MAX], pos[24];

	if (! gLocalId) gLocalId = TimGetTicks() | 1;
	id = ++gLocalId;
	if (gConv[conv].isChan) { to = broadcastNum; ch = gConv[conv].index; }
	else { to = gNodes[gConv[conv].index].num; ch = 0; }
	LatLon(pos, lat, lon);
	switch (kind) {
	case 1:		StrCopy(label, to == broadcastNum ? "[Shared my node info]" : "[Asked for node info]"); break;
	case 2:		StrCopy(label, "[Asked for position]"); break;
	case 3:		StrCopy(label, "[Asked for telemetry]"); break;
	case 4:		StrCopy(label, "[Traceroute]"); break;
	case 5:		StrCopy(label, "[Shared position "); StrCat(label, pos); StrCat(label, "]"); break;
	default:	StrCopy(label, "[Waypoint "); StrCat(label, name[0] ? name : "-"); StrCat(label, " "); StrCat(label, pos); StrCat(label, "]"); break;
	}
	m = AddMsg(conv, true, 0, label, NowUtc());
	m->localId = id;
	m->status = 0;
	w.buf = gTx; w.cap = sizeof(gTx); w.len = 0;
	RpW32(&w, to); RpW8(&w, ch); RpW32(&w, id); RpW8(&w, kind);
	RpW32(&w, (UInt32) lat); RpW32(&w, (UInt32) lon); RpWStr(&w, name);
	if (! gWirsaUp || ! RpSendFrame(mwReq, gTx, w.len)) {
		SetText("Not connected to WiRSa.");
		m->status = 3; m->err = 242; SortConv();
		return false;
	}
	SortConv();
	return true;
}

/* Looks at what WiRSa pushed without asking it anything: 0 nothing, 1 new things to fetch, 2 a send answered. */
MC_SEC UInt16 LinkPump(void)
{
	RpR r;
	UInt8 t;
	UInt16 res = 0, guard;

	for (guard = 0; guard < 8 && gWirsaUp && RpPending() > 0; guard++) {
		gRpIdleOverrideTicks = 30;				/* the bytes are already here: never wait long */
		if (! RpRecvFrame(&t, gRx, sizeof(gRx), &gRxLen)) break;
		r.buf = gRx; r.len = gRxLen; r.pos = 0; r.ok = true;
		gAsyncTick = TimGetTicks();
		if (gSyncPending && t != mwNews && t != mwPing && t != mwSendR && t != mwCfgR) {
			UInt8 sr = SyncFrame(t, &r);

			if (sr == 1) { gSyncPending = false; SyncFinish(); res |= 8; }
			else if (sr == 2) { gSyncPending = false; res |= 16; }
		}
		else if (gCfgPending && t == mwCfgR) { gCfgPending = false; if (CfgParse()) res |= 4; }
		else if (gCfgPending && t == mwErr) gCfgPending = false;		/* the node's settings are not there yet: asked again later */
		else if (t == mwNews) {
			UInt8 ls = RpR8(&r);						/* the news carries the node link as it is now */

			if (r.ok && ls != gLinkState) { gLinkState = ls; res |= 32; }
			res |= 1;
		}
		else if (t == mwPing) RpSendFrame(mwPing, NULL, 0);		/* WiRSa asks whether the app is still there */
		else if (t == mwSendR) {
			UInt32 id = RpR32(&r);
			UInt8 ok = RpR8(&r), code = 241;
			UInt16 i;
			char why[48];

			RpR32(&r);
			RpRStr(&r, why, sizeof(why));
			if (r.ok && r.pos < r.len) code = RpR8(&r);
			for (i = 0; i < gNMsg; i++)
				if (gMsg[i].out && gMsg[i].localId == id && gMsg[i].status == 0) {
					gMsg[i].status = ok ? 1 : 3;
					if (! ok) gMsg[i].err = code;
				}
			res |= 2;
		}
	}
	return res;
}

/* ---- scanning and pairing -------------------------------------------------------------------- */

MC_SEC Boolean LinkScan(void)
{
	RpR r;
	RpW w;
	UInt8 body[2], i, n;

	w.buf = body; w.cap = sizeof(body); w.len = 0;
	RpW8(&w, 5);
	if (! Req(mwScan, body, w.len, mwScanR, 12)) return false;
	r.buf = gRx; r.len = gRxLen; r.pos = 0; r.ok = true;
	n = RpR8(&r);
	if (n > MAX_SCAN) n = MAX_SCAN;
	gNScan = 0;
	for (i = 0; i < n; i++) {
		ScanHit *h = &gScan[gNScan];
		RpRBytes(&r, h->addr, 6);
		h->type = RpR8(&r);
		h->dbm = (Int8) RpR8(&r);
		RpRStr(&r, h->name, sizeof(h->name));
		if (! r.ok) break;
		if (! h->name[0]) StrCopy(h->name, "(no name)");
		gNScan++;
	}
	return true;
}

/* Pairing: WiRSa connects and the node starts the pairing; a random PIN exists only now, shown on the
 * node's screen. WiRSa sends mwPinNeed, the keypad takes the six digits, mwPin goes back, then mwPairR. */
MC_SEC Boolean LinkPair(const ScanHit *h)
{
	RpW w;
	RpR r;
	UInt8 ok, t;
	UInt32 until;
	char pin[8];
	UInt8 body[4];

	w.buf = gTx; w.cap = sizeof(gTx); w.len = 0;
	RpWBytes(&w, h->addr, 6);
	RpW8(&w, h->type);
	RpW32(&w, 0);
	RpWStr(&w, h->name);
	RpFlushInput();
	if (! RpSendFrame(mwPair, gTx, w.len)) { SetText("Could not send to WiRSa."); gWirsaUp = false; return false; }
	until = TimGetTicks() + SecTicks(75);
	for (;;) {
		gRpIdleOverrideTicks = SecTicks(75);
		if (! RpRecvFrame(&t, gRx, sizeof(gRx), &gRxLen)) { SetText("WiRSa did not answer."); return false; }
		if (t == mwPinNeed) {
			RpW pw;

			BusyDone();
			pin[0] = '\0';
			if (! NumPad(pin, 6, "PIN shown on the node")) StrCopy(pin, "0");
			pw.buf = body; pw.cap = sizeof(body); pw.len = 0;
			RpW32(&pw, (UInt32) StrAToI(pin));
			RpSendFrame(mwPin, body, pw.len);
			Busy("Pairing...", 0);
			continue;
		}
		if (t == mwPairR) break;
		if (t == mwErr) {
			r.buf = gRx; r.len = gRxLen; r.pos = 0; r.ok = true;
			RpR8(&r);
			RpRStr(&r, gLinkText, sizeof(gLinkText));
			return false;
		}
		if ((Int32) (TimGetTicks() - until) > 0) { SetText("WiRSa did not answer."); return false; }
	}
	r.buf = gRx; r.len = gRxLen; r.pos = 0; r.ok = true;
	ok = RpR8(&r);
	RpRStr(&r, gLinkText, sizeof(gLinkText));
	if (ok) { StrNCopy(gRelay.node, h->name, sizeof(gRelay.node) - 1); gLinkState = 2; gNNodes = 0; gNMsg = 0; gPoolUsed = 0; gNConv = 0; gCursor = 0; gWantNodes = gWantChans = true; }
	return ok != 0;
}

MC_SEC Boolean LinkUnpair(void)
{
	if (! Req(mwUnpair, NULL, 0, mwDone, 8)) return false;
	gLinkState = 0; gRelay.node[0] = '\0';
	return true;
}

/* ---- WiRSa's relay settings ------------------------------------------------------------------- */

MC_SEC Boolean LinkRelayGet(void)
{
	RpR r;

	if (! Req(mwRelayGet, NULL, 0, mwRelayR, 6)) return false;
	r.buf = gRx; r.len = gRxLen; r.pos = 0; r.ok = true;
	gRelay.mode = RpR8(&r); gRelay.every = RpR8(&r); gLinkState = RpR8(&r);
	RpRStr(&r, gRelay.node, sizeof(gRelay.node));
	RpR16(&r); gWirsaStored = RpR16(&r); gLastPollUtc = RpR32(&r);
	gMyBatt = (Int8) RpR8(&r);
	return r.ok;
}

MC_SEC Boolean LinkRelaySet(UInt8 mode, UInt8 every)
{
	RpW w;
	UInt8 body[2];

	w.buf = body; w.cap = sizeof(body); w.len = 0;
	RpW8(&w, mode); RpW8(&w, every);
	return Req(mwRelaySet, body, w.len, mwDone, 6);
}

MC_SEC Boolean LinkClearWirsa(void)
{
	RpW w;
	UInt8 body[4];

	w.buf = body; w.cap = sizeof(body); w.len = 0;
	RpW32(&w, 0);
	if (! Req(mwClear, body, w.len, mwDone, 6)) return false;
	gWirsaStored = 0;
	return true;
}

/* ---- the node's own settings -------------------------------------------------------------------- */

/* Reads them from WiRSa (which has them from the node's own start-up dump) into gNodeCfg. */
MC_SEC static Boolean CfgParse(void)
{
	RpR r;
	UInt8 n, i, idx, role, ok;
	UInt32 pin;
	char name[16], key[48];

	r.buf = gRx; r.len = gRxLen; r.pos = 0; r.ok = true;
	ok = RpR8(&r);
	if (! ok) { RpRStr(&r, gLinkText, sizeof(gLinkText)); return false; }
	RpRStr(&r, gNodeCfg.longName, sizeof(gNodeCfg.longName));
	RpRStr(&r, gNodeCfg.shortName, sizeof(gNodeCfg.shortName));
	gNodeCfg.region = RpR8(&r);
	gNodeCfg.preset = RpR8(&r);
	gNodeCfg.hop = RpR8(&r);
	gNodeCfg.txPower = RpR8(&r);
	gNodeCfg.role = RpR8(&r);
	gNodeCfg.btMode = RpR8(&r);
	pin = RpR32(&r);
	if (pin) StrPrintF(gNodeCfg.pin, "%06lu", (UInt32) pin); else StrCopy(gNodeCfg.pin, "123456");
	n = RpR8(&r);
	for (i = 0; i < MAX_CHAN; i++) { gNodeCfg.chan[i].role = 0; gNodeCfg.chan[i].name[0] = '\0'; gNodeCfg.chan[i].key[0] = '\0'; }
	for (i = 0; i < n && r.ok; i++) {
		idx = RpR8(&r); role = RpR8(&r);
		RpRStr(&r, name, sizeof(name));
		RpRStr(&r, key, sizeof(key));
		if (idx >= MAX_CHAN) continue;
		StrNCopy(gNodeCfg.chan[idx].name, name, sizeof(gNodeCfg.chan[idx].name) - 1);
		gNodeCfg.chan[idx].role = role;
		StrNCopy(gNodeCfg.chan[idx].key, key, sizeof(gNodeCfg.chan[idx].key) - 1);
		if (role) FindChan(idx, true);
	}
	NodeClockSet(RpR32(&r));
	if (! r.ok) { SetText("WiRSa sent unreadable settings."); return false; }
	gNodeCfg.dirty = false;
	gCfgLoaded = true;
	return true;
}

MC_SEC Boolean LinkCfgGet(void)
{
	gCfgPending = false;
	if (! Req(mwCfgGet, NULL, 0, mwCfgR, 45)) return false;
	return CfgParse();
}

/* Asks for them without waiting: the answer is read by LinkPump. */
MC_SEC Boolean LinkCfgStart(void)
{
	if (! gWirsaUp || gSyncPending || gCfgPending) return false;
	if (! RpSendFrame(mwCfgGet, NULL, 0)) return false;
	gCfgPending = true;
	gAsyncTick = TimGetTicks();
	return true;
}

/* Sends the parts named in mask (1 owner, 2 radio, 4 role, 8 Bluetooth, 16 channels - those in chanMask). */
MC_SEC Boolean LinkCfgSet(UInt16 mask, UInt16 chanMask)
{
	RpW w;
	UInt8 i, n = 0;

	w.buf = gTx; w.cap = sizeof(gTx); w.len = 0;
	RpW16(&w, mask);
	if (mask & 1) { RpWStr(&w, gNodeCfg.longName); RpWStr(&w, gNodeCfg.shortName); }
	if (mask & 2) { RpW8(&w, gNodeCfg.region); RpW8(&w, gNodeCfg.preset); RpW8(&w, gNodeCfg.hop); RpW8(&w, gNodeCfg.txPower); }
	if (mask & 4) RpW8(&w, gNodeCfg.role);
	if (mask & 8) { RpW8(&w, gNodeCfg.btMode); RpW32(&w, gNodeCfg.btMode == 1 ? (UInt32) StrAToI(gNodeCfg.pin) : 0); }
	if (mask & 16) {
		for (i = 0; i < MAX_CHAN; i++) if (chanMask & (1 << i)) n++;
		RpW8(&w, n);
		for (i = 0; i < MAX_CHAN; i++)
			if (chanMask & (1 << i)) {
				RpW8(&w, i); RpW8(&w, gNodeCfg.chan[i].role);
				RpWStr(&w, gNodeCfg.chan[i].name); RpWStr(&w, gNodeCfg.chan[i].key);
			}
	}
	return Req(mwCfgSet, gTx, w.len, mwDone, 40);
}

/* 0: set the node's clock to this Palm's; 1: restart the node. */
MC_SEC Boolean LinkAct(UInt8 action)
{
	RpW w;
	UInt8 body[8];

	w.buf = body; w.cap = sizeof(body); w.len = 0;
	RpW8(&w, action);
	RpW32(&w, StampToSec(NowUtc()));
	if (! Req(mwCfgAct, body, w.len, mwDone, 20)) return false;
	if (action == 0) { gNodeClockKnown = true; gNodeSkew = 0; }	/* the node now has this Palm's time */
	return true;
}

MC_SEC Boolean LinkReconnectNode(void)
{
	return Req(mwConnect, NULL, 0, mwStateR, 30);
}

/* "Sync now": WiRSa takes what the node holds right away (it connects first if the link is down). */
MC_SEC Boolean LinkNodeNow(void)
{
	UInt8 keep = 1;

	return Req(mwConnect, &keep, 1, mwStateR, 40);
}


/* ---- the copy kept on the Palm, so a launch fetches only what is new ---------------------------- */

typedef struct { UInt32 cursor; Stamp lastSync; Int8 batt; UInt16 nNodes, nConv, nMsg, pool; RelayCfg relay; } CacheHead;

#define cacheCreator	'McHt'
#define cacheVersion	5

MC_SEC void CacheSave(void)
{
	CacheHead h;

	h.cursor = gCursor; h.lastSync = gLastSync; h.batt = gMyBatt;
	h.nNodes = gNNodes; h.nConv = gNConv; h.nMsg = gNMsg; h.pool = gPoolUsed; h.relay = gRelay;
	PrefSetAppPreferences(cacheCreator, 1, cacheVersion, &h, sizeof(h), true);
	PrefSetAppPreferences(cacheCreator, 2, cacheVersion, &gNodeCfg, sizeof(gNodeCfg), true);
	PrefSetAppPreferences(cacheCreator, 3, cacheVersion, gNodes, (UInt16) (gNNodes * sizeof(Node)), true);
	PrefSetAppPreferences(cacheCreator, 4, cacheVersion, gConv, (UInt16) (gNConv * sizeof(Conv)), true);
	PrefSetAppPreferences(cacheCreator, 5, cacheVersion, gMsg, (UInt16) (gNMsg * sizeof(Msg)), true);
	PrefSetAppPreferences(cacheCreator, 6, cacheVersion, gPool, gPoolUsed, true);
}

MC_SEC void CacheLoad(void)
{
	CacheHead h;
	UInt16 sz = sizeof(h);

	if (PrefGetAppPreferences(cacheCreator, 1, &h, &sz, true) != cacheVersion || sz != sizeof(h)) return;
	if (h.nNodes > MAX_NODE || h.nConv > MAX_CONV || h.nMsg > MAX_MSG || h.pool > POOL_SIZE) return;
	sz = (UInt16) (h.nNodes * sizeof(Node));
	if (sz && (PrefGetAppPreferences(cacheCreator, 3, gNodes, &sz, true) != cacheVersion || sz != h.nNodes * sizeof(Node))) return;
	sz = (UInt16) (h.nConv * sizeof(Conv));
	if (sz && (PrefGetAppPreferences(cacheCreator, 4, gConv, &sz, true) != cacheVersion || sz != h.nConv * sizeof(Conv))) return;
	sz = (UInt16) (h.nMsg * sizeof(Msg));
	if (sz && (PrefGetAppPreferences(cacheCreator, 5, gMsg, &sz, true) != cacheVersion || sz != h.nMsg * sizeof(Msg))) return;
	sz = h.pool;
	if (sz && (PrefGetAppPreferences(cacheCreator, 6, gPool, &sz, true) != cacheVersion || sz != h.pool)) return;
	{	/* the node's settings are separate: a build that changed them must not lose the messages */
		NodeCfg cfg;

		sz = sizeof(cfg);
		if (PrefGetAppPreferences(cacheCreator, 2, &cfg, &sz, true) == cacheVersion && sz == sizeof(cfg)) gNodeCfg = cfg;
	}
	gNNodes = h.nNodes; gNConv = h.nConv; gNMsg = h.nMsg; gPoolUsed = h.pool;
	gCursor = h.cursor; gLastSync = h.lastSync; gMyBatt = h.batt; gRelay = h.relay;
	gWantNodes = gNNodes == 0;
	gWantChans = gNConv == 0;
	SortConv();
}

/* Marks a node as a favourite on the Meshtastic node itself (or takes the mark away). */
MC_SEC Boolean LinkFav(UInt32 num, Boolean on)
{
	RpW w;
	UInt8 body[8];

	w.buf = body; w.cap = sizeof(body); w.len = 0;
	RpW8(&w, on ? 2 : 3);
	RpW32(&w, num);
	return Req(mwCfgAct, body, w.len, mwDone, 20);
}
