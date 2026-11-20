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

/* The type numbers follow Protocol-wire.md (WiRSa docs/protocol), section 6: the common frames at 0x01-0x06,
 * Mesh's own in 0x40-0x6F, the Palm-sent ones first (0x40-0x52), then WiRSa's (0x53-0x60). */
/* common */
#define mwHello		0x01
#define mwHelloAck	0x02
#define mwBye		0x03
#define mwAck		0x04
#define mwSyncDone	0x05
#define mwErr		0x06
/* Palm -> WiRSa (Ping either way) */
#define mwState		0x40
#define mwScan		0x41
#define mwPair		0x42
#define mwPin		0x43
#define mwUnpair	0x44
#define mwConnect	0x45
#define mwSync		0x46
#define mwSend		0x47
#define mwReq		0x48
#define mwNodeGet	0x49
#define mwRelayGet	0x4A
#define mwRelaySet	0x4B
#define mwClear		0x4C
#define mwHist		0x4D
#define mwPing		0x4E
#define mwHold		0x4F
#define mwCfgGet	0x50
#define mwCfgSet	0x51
#define mwCfgAct	0x52
#define mwSlotsGet	0x61	/* version 3: the kept nodes */
#define mwSlotSelect	0x62
/* WiRSa -> Palm */
#define mwStateR	0x53
#define mwScanR		0x54
#define mwPairR		0x55
#define mwPinNeed	0x56
#define mwNode		0x57
#define mwNodes		0x58
#define mwChan		0x59
#define mwMsg		0x5A
#define mwDeliv		0x5B
#define mwSendR		0x5C
#define mwRelayR	0x5D
#define mwDone		0x5E
#define mwNews		0x5F
#define mwCfgR		0x60
#define mwSlotsR	0x63

/* The confirmed baud switch (Protocol-wire.md 5.1): HELLO = baud u32, then an optional flags byte, mcHelloOffer on the first HELLO ("I will confirm
 * before I switch") and mcHelloConfirm on the second, which says the reply was read. A WiRSa that supports it sets mcCapHelloConfirm in the reply's
 * capability byte and moves only on the confirm; an older one ignores the byte, replies with 0 and moves at once. */
#define mcHelloOffer		0x01
#define mcHelloConfirm		0x02
#define mcCapHelloConfirm	0x02
#define mcHelloTries		3
#define mcMaxRxType			0x63	/* the highest frame type WiRSa sends */
#define mcSkipGapTicks		6		/* a frame found among noise: its bytes may be this far apart (60 ms) */

/* The HELLO_ACK version this app needs: 3 = the numbers above and several nodes. */
#define mwWireVersion	3

#define mfOut		1
#define mfDm		2
#define broadcastNum	0xFFFFFFFFUL

UInt8 gLinkState;				/* the node link as WiRSa reports it */
SlotInfo gSlot[MAX_SLOT];
UInt8 gNSlot;
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

MC_SEC static UInt8 SyncFrame(UInt8 t, RpR *r);
MC_SEC static void SyncFinish(void);

/* A fetch is under way and WiRSa waits for the ACK of every frame it sends: another request sent now would be taken for a
 * missing ACK and lost. So the fetch is carried to its end first (a few seconds at most). */
MC_SEC static void FinishSync(void)
{
	UInt8 t;
	RpR r;
	UInt8 res;
	UInt32 until = TimGetTicks() + SecTicks(10);

	while (gSyncPending && (Int32) (TimGetTicks() - until) < 0) {
		gRpIdleOverrideTicks = SecTicks(3);
		if (! RpRecvFrame(&t, gRx, sizeof(gRx), &gRxLen)) break;
		r.buf = gRx; r.len = gRxLen; r.pos = 0; r.ok = true;
		res = SyncFrame(t, &r);
		if (res == 1) { gSyncPending = false; SyncFinish(); }
		else if (res == 2) gSyncPending = false;
	}
}

/* Sends a frame; waits up to waitSec for one of type want (others are dropped; an ERR frame fails the
 * call with WiRSa's text). The reply body is left in gRx. */
MC_SEC static Boolean Req(UInt8 type, const UInt8 *body, UInt16 len, UInt8 want, UInt16 waitSec)
{
	UInt8 t;
	UInt32 until = TimGetTicks() + SecTicks(waitSec);

	if (! gWirsaUp) { SetText("Not connected to WiRSa."); return false; }
	if (gSyncPending) FinishSync();
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
		if (t == mwBye) { LinkDrop(); SetText("WiRSa ended Mesh mode."); return false; }
		if (t == mwPing) { RpSendFrame(mwPing, NULL, 0); continue; }	/* a probe is answered while waiting too */
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
	UInt8 ver, cap, tries;
	Boolean got = false;
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
	/* The HELLO offers a confirmed switch (mcHelloOffer): a WiRSa that supports it answers at this rate and moves only when the HELLO_CONFIRM
	 * below arrives, so a reply that is lost or damaged on the way costs nothing: the HELLO is sent again, and both ends are still at the idle
	 * rate. The reply is read by hunting for it (RpRecvFrameHunt): noise can arrive in front of it. An older WiRSa ignores the extra byte,
	 * answers without mcCapHelloConfirm and moves at once, as before. */
	for (tries = 0; tries < mcHelloTries && ! got; tries++) {
		if (tries) SysTaskDelay(SysTicksPerSecond() / 4);		/* what WiRSa sent for the lost reply has all arrived */
		RpFlushInput();
		w.buf = body; w.cap = sizeof(body); w.len = 0;
		RpW32(&w, baud);
		RpW8(&w, mcHelloOffer);
		if (! RpSendFrame(mwHello, body, w.len)) break;
		gRpIdleOverrideTicks = SecTicks(5);
		got = RpRecvFrameHunt(mwHelloAck, gRx, sizeof(gRx), &gRxLen);
	}
	if (! got) { SetText("WiRSa did not answer."); LinkDrop(); return false; }
	rd.buf = gRx; rd.len = gRxLen; rd.pos = 0; rd.ok = true;
	ver = RpR8(&rd); cap = RpR8(&rd);
	{ char name[24]; RpRStr(&rd, name, sizeof(name)); }
	neg = RpR32(&rd);
	if (! rd.ok || neg == 0) { SetText("WiRSa sent an unreadable reply."); LinkDrop(); return false; }
	if (ver < mwWireVersion) { SetText("This WiRSa is older than the app. Update WiRSa's firmware."); LinkDrop(); return false; }	/* an older WiRSa speaks the old type numbers */
	if (cap & mcCapHelloConfirm) {
		/* WiRSa is waiting at the idle rate: tell it the reply was read (the same rate again), then move. WiRSa moves when it gets this,
		 * so the pause keeps this end's first frame at the new rate from arriving before it has. */
		w.buf = body; w.cap = sizeof(body); w.len = 0;
		RpW32(&w, baud);
		RpW8(&w, mcHelloConfirm);
		if (! RpSendFrame(mwHello, body, w.len)) { SetText("Could not send to WiRSa."); LinkDrop(); return false; }
		SysTaskDelay(SysTicksPerSecond() / 10);
	}
	RpSetBaud(neg);
	gRpSkipJunkMax = mcMaxRxType;			/* noise ahead of a reply (a wireless TTL link) is hunted past: RecvHunted */
	gRpSkipGapTicks = mcSkipGapTicks;
	/* The node picked on the Palm (Pair Device, before connecting) is the one to link to, not the one WiRSa had shown or kept for the background. */
	if (gNodeId && LinkSlots()) {
		UInt16 i;

		for (i = 0; i < gNSlot; i++) {
			if (gSlot[i].id != gNodeId || (gSlot[i].state != 1 && gSlot[i].state != 2)) continue;
			if (! (gSlot[i].flags & 1) && LinkSlotSelect(gSlot[i].slot) && gSlot[i].state == 1) LinkReconnectNode();
			break;
		}
	}
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

/* The lowest `digits` hex digits of v, lower case, as a node id is written. (StrPrintF's width handling is not relied on.) */
MC_SEC void HexStr(char *out, UInt32 v, UInt16 digits)
{
	UInt16 i;
	UInt8 n;

	for (i = 0; i < digits; i++) {
		n = (UInt8) ((v >> (4 * (digits - 1 - i))) & 0xF);
		out[i] = (char) (n < 10 ? '0' + n : 'a' + (n - 10));
	}
	out[digits] = '\0';
}

MC_SEC UInt16 NodeByNum(UInt32 num, Boolean create)
{
	UInt16 i;

	for (i = 0; i < gNNodes; i++)
		if (gNodes[i].num == num) return i;
	if (! create || gNNodes >= gMaxNode) return 0xFFFF;
	MemSet(&gNodes[gNNodes], sizeof(Node), 0);
	gNodes[gNNodes].num = num;
	gNodes[gNNodes].longName[0] = '!';
	HexStr(gNodes[gNNodes].longName + 1, num, 8);
	HexStr(gNodes[gNNodes].shortName, num, 4);						/* until the node names itself: the end of its id */
	return gNNodes++;
}

void NodeTail(UInt16 node, char *out)
{
	HexStr(out, node < gNNodes ? gNodes[node].num : 0, 4);
}

/* The list entry of a node, from its card or a list record. */
MC_SEC static void StoreShort(UInt32 num, const char *lng, const char *sht, UInt32 heard, UInt8 fav, UInt8 key)
{
	UInt16 i = NodeByNum(num, true);
	Node *n;

	if (i == 0xFFFF) return;
	n = &gNodes[i];
	if (lng[0]) { StrNCopy(n->longName, lng, sizeof(n->longName) - 1); n->longName[sizeof(n->longName) - 1] = '\0'; }
	if (sht[0]) { StrNCopy(n->shortName, sht, sizeof(n->shortName) - 1); n->shortName[sizeof(n->shortName) - 1] = '\0'; }
	n->heard = SecToStamp(heard);
	n->fav = fav != 0; n->hasKey = key != 0;
}

/* One node in full (a NODE frame): into f, and its list entry brought up to date. */
MC_SEC static Boolean ParseNodeFull(RpR *r, NodeFull *f)
{
	UInt32 num = RpR32(r), heard, lat, lon;
	char id[12], lng[40], sht[8];
	UInt8 hasSnr, hasPos, isThis, fav, key;

	RpRStr(r, id, sizeof(id)); RpRStr(r, lng, sizeof(lng)); RpRStr(r, sht, sizeof(sht));
	f->hwNum = RpR8(r); f->roleNum = RpR8(r); f->snr = (Int8) RpR8(r); hasSnr = RpR8(r);
	heard = RpR32(r); f->hops = RpR8(r); f->batt = (Int8) RpR8(r); f->uptime = RpR32(r);
	f->util = RpR16(r); f->air = RpR16(r); hasPos = RpR8(r); lat = RpR32(r); lon = RpR32(r);
	isThis = RpR8(r);
	fav = r->pos < r->len ? RpR8(r) : 0;
	key = r->pos < r->len ? RpR8(r) : 0;
	if (! r->ok) return false;
	f->num = num;
	StrCopy(f->id, id); StrCopy(f->longName, lng); StrCopy(f->shortName, sht);
	if (! hasSnr) f->snr = 0;
	if ((Int8) f->hops < 0) f->hops = 0;
	f->heard = SecToStamp(heard);
	f->pos[0] = '\0';
	if (hasPos) LatLon(f->pos, (Int32) lat, (Int32) lon);
	f->fav = fav != 0; f->hasKey = key != 0; f->have = true;
	(void) isThis;
	StoreShort(num, lng, sht, heard, fav, key);
	return true;
}

/* The node list's first frame: this node in full. WiRSa sends it first, so it is entry 0. */
MC_SEC static void ApplyNode(RpR *r)
{
	if (ParseNodeFull(r, &gSelf) && gNNodes > 0 && gNodes[0].num != gSelf.num) {	/* it must be entry 0: swap it there */
		UInt16 i = NodeByNum(gSelf.num, false);

		if (i != 0xFFFF && i != 0) {
			Node t = gNodes[0];
			UInt16 k;

			gNodes[0] = gNodes[i]; gNodes[i] = t;
			for (k = 0; k < gNMsg; k++) {			/* messages and direct conversations point at nodes by place: they follow the swap */
				if (gMsg[k].from == 0) gMsg[k].from = i;
				else if (gMsg[k].from == i) gMsg[k].from = 0;
			}
			for (k = 0; k < gNConv; k++) {
				if (gConv[k].isChan) continue;
				if (gConv[k].index == 0) gConv[k].index = i;
				else if (gConv[k].index == i) gConv[k].index = 0;
			}
		}
	}
}

/* Several list entries to a frame: number, long name, short name, heard, flags (1 favourite, 2 key known). */
MC_SEC static void ApplyNodes(RpR *r)
{
	UInt8 n = RpR8(r), k;
	UInt32 num, heard;
	char lng[24], sht[6];
	UInt8 flags;

	for (k = 0; k < n && r->ok; k++) {
		num = RpR32(r); RpRStr(r, lng, sizeof(lng)); RpRStr(r, sht, sizeof(sht)); heard = RpR32(r); flags = RpR8(r);
		if (r->ok) StoreShort(num, lng, sht, heard, (UInt8) (flags & 1), (UInt8) ((flags >> 1) & 1));
	}
}

/* The card of a node: this node as the list had it, or WiRSa is asked (a NODE frame comes back). */
MC_SEC Boolean LinkNodeGet(UInt16 node)
{
	RpW w;
	RpR r;

	MemSet(&gCard, sizeof(gCard), 0);
	gCard.num = gNodes[node].num;
	StrCopy(gCard.longName, gNodes[node].longName); StrCopy(gCard.shortName, gNodes[node].shortName);
	gCard.id[0] = '!';
	HexStr(gCard.id + 1, gCard.num, 8);
	gCard.heard = gNodes[node].heard; gCard.fav = gNodes[node].fav; gCard.hasKey = gNodes[node].hasKey;
	gCard.batt = -1;
	if (! gWirsaUp) return false;
	w.buf = gTx; w.cap = sizeof(gTx); w.len = 0;
	RpW32(&w, gCard.num);
	if (! Req(mwNodeGet, gTx, w.len, mwNode, 4)) return false;
	r.buf = gRx; r.len = gRxLen; r.pos = 0; r.ok = true;
	return ParseNodeFull(&r, &gCard);
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
	if (! (flags & mfOut)) {					/* WiRSa started over (or restored texts from its card) and handed on what this Palm already holds */
		UInt16 fn = NodeByNum(from, false);

		for (k = 0; k < gNMsg; k++) {
			if (gMsg[k].out || gMsg[k].from != fn) continue;
			if (localId && gMsg[k].localId) { if (gMsg[k].localId == localId) return; }		/* the packet's own id: exact, with or without a time */
			else if (rx && gMsg[k].when == when && StrCompare(MsgText(&gMsg[k]), text) == 0) return;	/* an older copy has no id: same minute and text */
		}
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
	m = AddMsg(conv, (flags & mfOut) != 0, fi, text, when);
	m->seq = seq; m->localId = localId; m->status = status; m->err = err;
	if (! (flags & mfOut))		/* unread until it has been on the screen for a second (ChatDwell), also in the thread that is open */
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
	else if (t == mwNodes) { ApplyNodes(r); Ack(); }
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
	else if (t == mwBye) { LinkDrop(); SetText("WiRSa ended Mesh mode."); return 2; }	/* WiRSa left the mode */
	else if (t == mwPing) RpSendFrame(mwPing, NULL, 0);		/* WiRSa's probe: the app is here */
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
	if (gSelf.have) {						/* the Setup pages start from the node's own names */
		StrNCopy(gNodeCfg.longName, gSelf.longName, sizeof(gNodeCfg.longName) - 1);
		StrNCopy(gNodeCfg.shortName, gSelf.shortName, sizeof(gNodeCfg.shortName) - 1);
		StripName(gNodeCfg.longName); StripName(gNodeCfg.shortName);		/* these go back to the node on a save */
	}
}

/* A dialog is about to open (an alert, the keyboard): the app cannot answer WiRSa's probe meanwhile, so WiRSa is told not to
 * count the next two minutes against the Exit Timers. */
MC_SEC void LinkHold(void)
{
	UInt8 secs = 120;

	if (gWirsaUp && ! gSyncPending && ! gCfgPending) RpSendFrame(mwHold, &secs, 1);	/* not in the middle of a fetch: WiRSa would take it for a missing ACK */
}

/* "I am still here": sent every 5 seconds while linked and no request is out (a frame in the middle of a fetch would be lost).
 * WiRSa counts any frame as the Palm being there, so it only asks "are you there?" when these stop. */
MC_SEC void LinkKeepAlive(void)
{
	static UInt32 sNext;
	UInt32 now = TimGetTicks();

	if (! gWirsaUp || gSyncPending || gCfgPending || (Int32) (now - sNext) < 0) return;
	sNext = now + SecTicks(5);
	RpSendFrame(mwPing, NULL, 0);
}

/* A chat opened with a node whose public key is not known: WiRSa asks it for its node info (ours goes with the request), so the
 * first direct message can be encrypted. Nothing is stored or shown; once for each node while the app runs. */
MC_SEC void LinkAskKey(UInt16 node)
{
	static UInt8 asked[(MAX_NODE + 7) / 8];
	RpW w;

	if (! gWirsaUp || gSyncPending || gCfgPending || node == 0 || node >= gNNodes || gNodes[node].hasKey) return;
	if (asked[node >> 3] & (1 << (node & 7))) return;
	asked[node >> 3] |= (UInt8) (1 << (node & 7));
	w.buf = gTx; w.cap = sizeof(gTx); w.len = 0;
	RpW32(&w, gNodes[node].num); RpW8(&w, 0); RpW32(&w, 0); RpW8(&w, 1);
	RpW32(&w, 0); RpW32(&w, 0); RpWStr(&w, "");
	RpSendFrame(mwReq, gTx, w.len);
}

/* Tells WiRSa what this Palm has saved, so it can empty the SD card's copy (unless SD History is on). The answer
 * (a DONE) is read and ignored by LinkPump. */
MC_SEC void LinkHistSend(void)
{
	RpW w;

	if (! gWirsaUp) return;
	w.buf = gTx; w.cap = sizeof(gTx); w.len = 0;
	RpW8(&w, gPrefs.sdHist ? 1 : 0);
	RpW32(&w, gCursor);
	RpSendFrame(mwHist, gTx, w.len);
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
	r.buf = gRx; r.len = gRxLen; r.pos = 2; r.ok = true;
	NodeAdopt(RpR32(&r));					/* the node WiRSa shows: its data is the one on screen */
	r.pos = 0;
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
	for (i = 0; i < gNMsg; i++) if (gMsg[i].out && gMsg[i].localId && (gMsg[i].status == 0 || gMsg[i].status == 1 || gMsg[i].status == 4 || gMsg[i].status == 6)) nPend++;
	if (nPend > 24) nPend = 24;
	RpW8(&w, (UInt8) nPend);
	for (i = 0; i < gNMsg && nPend; i++)
		if (gMsg[i].out && gMsg[i].localId && (gMsg[i].status == 0 || gMsg[i].status == 1 || gMsg[i].status == 4 || gMsg[i].status == 6)) {
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

	if (gSyncPending) FinishSync();					/* WiRSa waits for the ack of every frame of a fetch: a frame sent now would be taken for a missing one */
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

	if (gSyncPending) FinishSync();					/* as in LinkSend */
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
		if (t == mwBye) {						/* WiRSa left the mode (exit timer, button): the link is over */
			gSyncPending = false; gCfgPending = false;
			LinkDrop();
			SetText("WiRSa ended Mesh mode.");
			res |= 32;
			break;
		}
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
		else if (t == mwStateR) {						/* the answer to LinkReconnectSend: the node link as it is now */
			UInt8 ls = RpR8(&r);

			if (r.ok && ls != gLinkState) { gLinkState = ls; res |= 32; }
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
			Busy("Pairing and loading the node...", 0);
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
	if (ok) {
		UInt32 nid = 0;

		if (r.pos < r.len) { RpR8(&r); nid = RpR32(&r); }		/* the slot, and the node's number (0 until it has said) */
		CacheSave();								/* the node shown until now keeps its data */
		NodeDataClear();
		gNodeId = 0;
		StrNCopy(gRelay.node, h->name, sizeof(gRelay.node) - 1);
		gLinkState = 2;
		if (nid) { if (! CacheLoadNode(nid)) gNodeId = nid; }		/* a node paired before has its data still */
	}
	return ok != 0;
}

/* keep: the node stays on WiRSa as an archive (its texts); not: everything of it goes. */
MC_SEC Boolean LinkUnpair(UInt8 slot, Boolean keep)
{
	UInt8 body[2];

	body[0] = slot; body[1] = keep ? 1 : 0;
	return Req(mwUnpair, body, 2, mwDone, 8);
}

/* The nodes WiRSa keeps: gSlot[0 .. gNSlot - 1]. */
MC_SEC Boolean LinkSlots(void)
{
	RpR r;
	UInt16 i, n;

	gNSlot = 0;
	if (! Req(mwSlotsGet, NULL, 0, mwSlotsR, 6)) return false;
	r.buf = gRx; r.len = gRxLen; r.pos = 0; r.ok = true;
	n = RpR8(&r);
	for (i = 0; i < n && i < MAX_SLOT && r.ok; i++) {
		gSlot[i].slot = RpR8(&r); gSlot[i].state = RpR8(&r); gSlot[i].id = RpR32(&r); gSlot[i].flags = RpR8(&r);
		RpRStr(&r, gSlot[i].name, sizeof(gSlot[i].name));
		gNSlot = (UInt8) (i + 1);
	}
	return r.ok;
}

/* WiRSa shows another node: its store and link become that node's. */
MC_SEC Boolean LinkSlotSelect(UInt8 slot)
{
	return Req(mwSlotSelect, &slot, 1, mwDone, 12);
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
	gRelay.sdFlush = (r.pos < r.len) ? RpR8(&r) : 4;			/* an older WiRSa sends none */
	return r.ok;
}

MC_SEC Boolean LinkRelaySet(UInt8 mode, UInt8 every, UInt8 sdFlush)
{
	RpW w;
	UInt8 body[3];

	w.buf = body; w.cap = sizeof(body); w.len = 0;
	RpW8(&w, mode); RpW8(&w, every); RpW8(&w, sdFlush);
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
	for (i = 0; i < MAX_CHAN; i++) { gNodeCfg.chan[i].role = 0; gNodeCfg.chan[i].name[0] = '\0'; gNodeCfg.chan[i].key[0] = '\0'; gNodeCfg.chan[i].link = 0; }
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
	gNodeCfg.mqtt.have = 0;						/* the MQTT module: a tail older WiRSa firmware does not send */
	gNodeCfg.mqtt.flags = 0;
	gNodeCfg.mqtt.address[0] = gNodeCfg.mqtt.user[0] = gNodeCfg.mqtt.pass[0] = gNodeCfg.mqtt.root[0] = '\0';
	if (r.pos < r.len && RpR8(&r)) {
		gNodeCfg.mqtt.flags = RpR8(&r);
		RpRStr(&r, gNodeCfg.mqtt.address, sizeof(gNodeCfg.mqtt.address));
		RpRStr(&r, gNodeCfg.mqtt.user, sizeof(gNodeCfg.mqtt.user));
		RpRStr(&r, gNodeCfg.mqtt.pass, sizeof(gNodeCfg.mqtt.pass));
		RpRStr(&r, gNodeCfg.mqtt.root, sizeof(gNodeCfg.mqtt.root));
		if (r.ok) gNodeCfg.mqtt.have = 1;
	}
	if (r.ok && r.pos < r.len) {					/* then each channel's MQTT up- and downlink */
		n = RpR8(&r);
		for (i = 0; i < n && r.ok; i++) {
			idx = RpR8(&r);
			role = RpR8(&r);
			if (idx < MAX_CHAN) gNodeCfg.chan[idx].link = role;
		}
	}
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

/* Sends the parts named in mask (1 owner, 2 radio, 4 role, 8 Bluetooth, 16 channels - those in chanMask). */
MC_SEC Boolean LinkCfgSet(UInt16 mask, UInt16 chanMask)
{
	RpW w;
	UInt8 i, n = 0;

	w.buf = gTx; w.cap = sizeof(gTx); w.len = 0;
	if (mask & 16) mask |= 64;						/* 64: each channel entry carries its MQTT flags */
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
				RpWStr(&w, gNodeCfg.chan[i].name); RpWStr(&w, gNodeCfg.chan[i].key); RpW8(&w, gNodeCfg.chan[i].link);
			}
	}
	if (mask & 32) {
		RpW8(&w, gNodeCfg.mqtt.flags);
		RpWStr(&w, gNodeCfg.mqtt.address); RpWStr(&w, gNodeCfg.mqtt.user);
		RpWStr(&w, gNodeCfg.mqtt.pass); RpWStr(&w, gNodeCfg.mqtt.root);
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

/* No fetch may be under way: its frames would take the reply. */
MC_SEC Boolean LinkReconnectSend(void)
{
	if (! gWirsaUp || gSyncPending || gCfgPending) return false;
	RpFlushInput();
	return RpSendFrame(mwConnect, NULL, 0);
}

/* "Sync now": WiRSa takes what the node holds right away (it connects first if the link is down). */
/* WiRSa asks the node for its node list again (a want_config) and waits until it has come in. */
MC_SEC Boolean LinkNodeList(void)
{
	UInt8 mode = 2;

	return Req(mwConnect, &mode, 1, mwStateR, 70);
}

MC_SEC Boolean LinkNodeNow(void)
{
	UInt8 keep = 1;

	return Req(mwConnect, &keep, 1, mwStateR, 40);
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
