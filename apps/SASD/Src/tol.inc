/* Tolerance for slow, noisy serial links (a wireless TTL link such as NOWttl), and the packet log that made it possible. Included by Slot.c after
 * slotPrvPktExchange; compiled with -DSASD_TOL (always, in this Makefile). Details and measurements: ../SASD_driver.md.
 *
 * Always on:
 *   - the wait for an ack follows the link (dgWait): twice the slowest of the last 16 acks, between the stock 210 ms and 1 s; a send that got nothing doubles the next wait.
 *     On a direct cable acks take 5 to 10 ticks, so the wait stays at the stock value.
 *   - bulk receive (slotPrvSerRxHard): everything the serial manager holds is taken in one call into a FIFO, so a flood of junk bytes from line spikes cannot overflow the serial
 *     manager's queue while the driver reads one byte per call.
 *   - request and data back to back (slotPrvPktExchangePair), only when the reader sets SD_FLAG_PIPE and the link is slow (acks of PAIR_MIN_LAT ticks or more); stopped for
 *     30 s after a pair that needed a resend, because on a direct cable the data behind the request was damaged.
 *   - the readings of the link are forgotten at every connect (dgNewLink).
 *
 * With DG_LOG=1 (make LOG=1) also a log, per packet exchange: when each request went out, when the first byte of the answer arrived, serial line errors, junk skipped in front of the
 * packet header, CRC results, and what the ack matcher decided. Clean first-try exchanges are mostly dropped from the event ring (kept: 1 in 16); counters and histograms cover
 * all of them. It is uploaded to the reader as CMD_DIAG packets (kind 1 = summary in slices, kind 2 = events) when the reader says it takes them (SD_FLAG_DIAG in its INFO
 * reply): when 128 exchanges have passed, when the ring is three quarters full, every 60 s, and after a failed transfer; never during the mount and never from sleep.
 */

#ifndef DG_LOG
#define DG_LOG			0		/* 1: keep the packet log and upload it; 0: only what the tolerance needs (link readings, pair guard) */
#endif
#ifndef PAIR_MIN_LAT
#define PAIR_MIN_LAT		12		/* ticks: below this the link is a direct cable and the stock sequence stays */
#endif
#define DG_CHUNK_EVENTS		18		/* short packets: the long ones (about 480 bytes) are the ones the link damages */
#define DG_SUMMARY_SLICE	100
#define DG_RING_DEFAULT		512

struct DgEv {
	UInt8 t, a;
	UInt16 b;
	UInt16 tick;
	UInt16 x;
};

struct Dg {
	UInt8 on, quiet, dumpFails, log;
	UInt8 var, cmd, tid, rxArm;
	UInt8 isData, junkThis, attempts, pad0;
	UInt8 head[6];
	UInt16 nrx, sentTick, nEv, dumpId;

	UInt32 dex, wr, startWr, dumped, lastDump, exStart, rxBytes, exTotal, dumpedExTotal, dumpedDex, noDumpUntil;

	/* counters */
	UInt16 vEx[4], vOk1[4], vOkN[4], vFail[4], vSends[4];
	UInt32 vTicks[4];
	UInt16 cEx[8], cRetry[8], cFail[8];
	UInt16 attHist[9];
	UInt16 ackDelay[16];
	UInt16 lineErr[8];
	UInt16 errOther;
	UInt16 junkExch, repaired;
	UInt16 lat[16], latIdx, pairs, pairFail;
	UInt32 pairBlockUntil, keepWr;		/* keepWr: events below this (the last session's crumbs) are never trimmed away */
	UInt32 junkBytes;
	UInt16 badCrc, mismatch, notAck, oom, ackNone, ackOk, dumpOk, dumpFail;
	UInt8 *crumb;			/* breadcrumb record in the storage heap (kept across a reset), see dgCrumb */
	UInt16 cidx;
	UInt8 lastStage;
	UInt8 pad1;

	struct DgEv ring[1];
};

static UInt8 dgClass(UInt8 cmd)
{
	switch (cmd) {
		case 0x00:	return 0;	/* PING */
		case 0x01:	return 1;	/* INFO */
		case 0x02:	return 2;	/* READ request */
		case 0x03:	return 3;	/* WRITE request */
		case 0x05:	return 4;	/* SET_BAUD */
		case 0x80:	return 5;	/* CTS */
		case 0x81:	return 6;	/* XFER */
	}
	return 7;
}

static void dgPut16(UInt8 *p, UInt16 v)
{
	p[0] = (UInt8)(v >> 8);
	p[1] = (UInt8)v;
}

static void dgPut32(UInt8 *p, UInt32 v)
{
	p[0] = (UInt8)(v >> 24);
	p[1] = (UInt8)(v >> 16);
	p[2] = (UInt8)(v >> 8);
	p[3] = (UInt8)v;
}

/* Breadcrumbs: the last CR_N steps the driver took, written into a resource of the driver's own database (survives a reset) before each step. If the Palm hangs, the next start of the driver
 * puts them into the log as events of type 0x80|stage (the first is the type 0xFF marker: a=entries, b/x=seconds of the last step) and they go out with the first upload. */
#define CR_N			32
#define CR_MAGIC		0xC4B1
#define CR_HDR			8
#define CR_SIZE			(CR_HDR + CR_N * 8)
#define CR_TX			0x20	/* a=stuck count b=bytes to send x=bytes SerSend took        entering / looping in the transmit loop */
#define CR_RX			0x21	/* b=bytes wanted x=ticks left                                entering SerReceive (repeats not written) */
#define CR_SLEEP		0x30
#define CR_WAKE			0x32
#define CR_WAKEALM		0x33
#define CR_WAKEALM_END	0x34
#define CR_XB			0x40	/* a=cmd b=tid                                               an exchange starts */
#define CR_XE			0x41	/* a=ok b=tid                                                an exchange ends */
#define CR_DUMP			0x42
#define CR_DUMP_END		0x43
#define CR_RD			0x50	/* b=sector x=count                                          SlotCardSectorRead */
#define CR_WR			0x52

#if DG_LOG
static void dgCrumb(struct Globals *g, UInt8 t, UInt8 a, UInt16 b, UInt16 x)
{
	struct Dg *d = g->dg;
	struct DgEv e;
	UInt16 hdr[4];

	if (!d || !d->crumb)
		return;
	if (t == CR_RX && d->lastStage == CR_RX)
		return;
	d->lastStage = t;

	e.t = t;
	e.a = a;
	e.b = b;
	e.tick = (UInt16)TimGetTicks();
	e.x = x;
	DmWrite(d->crumb, CR_HDR + (d->cidx & (CR_N - 1)) * 8, &e, 8);
	d->cidx++;
	hdr[0] = CR_MAGIC;
	hdr[1] = d->cidx;
	hdr[2] = (UInt16)(TimGetSeconds() >> 16);
	hdr[3] = (UInt16)TimGetSeconds();
	DmWrite(d->crumb, 0, hdr, 8);
}

/* open the record; if this is the first start since the reset, the old crumbs go into the ring */
static void dgCrumbOpen(struct Dg *d)
{
	DmOpenRef db;
	MemHandle h;
	UInt16 hdr[4], i, n, k;
	UInt32 ftr;
	UInt8 *p;

	db = DmOpenDatabaseByTypeCreator(sysFileTSlotDriver, MY_CRID, dmModeReadWrite);	/* the driver's own database: it carries a zeroed 'crmb' resource (see Makefile) */
	if (!db)
		return;
	h = DmGet1Resource('crmb', 0);
	if (!h || MemHandleSize(h) < CR_SIZE) {
		DmCloseDatabase(db);
		return;
	}
	p = (UInt8*)MemHandleLock(h);

	if (FtrGet(MY_CRID, 0x4352, &ftr) != errNone) {			/* first start since the reset: what the last session left */
		MemMove(hdr, p, 8);
		(void)FtrSet(MY_CRID, 0x4352, 1);
		if (hdr[0] == CR_MAGIC && hdr[1]) {
			n = hdr[1] > CR_N ? CR_N : hdr[1];
			d->wr = 0;
			if (d->log) {
				struct DgEv *e = &d->ring[d->wr++ & (d->nEv - 1)];
				e->t = 0xFF;
				e->a = (UInt8)n;
				e->b = hdr[2];
				e->tick = 0;
				e->x = hdr[3];
				for (k = 0; k < n; k++) {
					i = (UInt16)(hdr[1] - n + k);
					e = &d->ring[d->wr++ & (d->nEv - 1)];
					MemMove(e, p + CR_HDR + (i & (CR_N - 1)) * 8, 8);
					e->t |= 0x80;
				}
				d->keepWr = d->wr;
				d->startWr = d->wr;
			}
		}
	}
	DmReleaseResource(h);
	DmCloseDatabase(db);		/* the resource stays locked, so its place in the storage heap does not change */
	d->crumb = p;
	d->cidx = 0;
	hdr[0] = CR_MAGIC;
	hdr[1] = 0;
	hdr[2] = hdr[3] = 0;
	DmWrite(p, 0, hdr, 8);
}

#endif

static void dgAlloc(struct Globals *g)
{
	UInt16 n;
	struct Dg *d = NULL;

	if (!DG_LOG) {		/* no log: a state block with a one event ring */
		d = (struct Dg*)MemChunkNew(0, sizeof(struct Dg), memNewChunkFlagNonMovable);
		if (d) {
			MemSet(d, sizeof(struct Dg), 0);
			d->nEv = 1;
			d->on = 1;
		}
		g->dg = d;
		return;
	}

	for (n = DG_RING_DEFAULT; n >= 128 && !d; n >>= 1) {
		d = (struct Dg*)MemChunkNew(0, sizeof(struct Dg) + (UInt32)(n - 1) * sizeof(struct DgEv), memNewChunkFlagNonMovable);
		if (d) {
			MemSet(d, sizeof(struct Dg), 0);
			d->nEv = n;
			d->on = 1;
			d->log = 1;
		}
	}
#if DG_LOG
	if (d)
		dgCrumbOpen(d);
#endif
	g->dg = d;
}

static void dgNewLink(struct Globals *g)
{
	struct Dg *d = g->dg;

	if (d) {
		MemSet(d->lat, sizeof(d->lat), 0);
		d->latIdx = 0;
		d->pairBlockUntil = 0;
	}
}

static void dgFree(struct Globals *g)
{
	if (g->dg) {
#if DG_LOG
		if (g->dg->crumb)
			MemPtrUnlock(g->dg->crumb);
#endif
		MemChunkFree(g->dg);
		g->dg = NULL;
	}
}

static void dgPut(struct Dg *d, UInt8 t, UInt8 a, UInt16 b, UInt16 x)
{
	struct DgEv *e;

	if (!d->log || !d->on || d->quiet)
		return;

	e = &d->ring[d->wr & (d->nEv - 1)];
	e->t = t;
	e->a = a;
	e->b = b;
	e->tick = (UInt16)TimGetTicks();
	e->x = x;
	d->wr++;
}

void dgNote(void *st, uint8_t t, uint8_t a, uint16_t b, uint16_t x)
{
	struct Dg *d = ((struct Globals*)st)->dg;

	if (!d || !d->on)
		return;

	dgCrumb((struct Globals*)st, t, a, b, x);

	/* how long the link takes, for the ack wait: from the end of the last send to the accepted ack */
	if (t == DGE_SENT)
		d->sentTick = (UInt16)TimGetTicks();
	else if (t == DGE_ACK && (b & 0xff) == DGA_OK) {
		d->lat[d->latIdx & 15] = (UInt16)((UInt16)TimGetTicks() - d->sentTick);
		d->latIdx++;
	}

	if (d->quiet)
		return;

	switch (t) {
		case DGE_SEND:
			d->nrx = 0;
			d->rxArm = 0;
			d->junkThis = 0;
			d->attempts = (UInt8)(b + 1);
			break;
		case DGE_SENT:
			d->sentTick = (UInt16)TimGetTicks();
			d->rxArm = 1;
			break;
		case DGE_LOCK:
			if (x & 0x8000)
				d->repaired++;
			if (x & 0x7fff) {
				d->junkThis = 1;
				d->junkBytes += (x & 0x7fff);
			}
			break;
		case DGE_BADCRC:
			d->badCrc++;
			break;
		case DGE_ACK:
			dgPut(d, DGE_RXHEAD, (UInt8)(d->nrx > 255 ? 255 : d->nrx), (UInt16)((d->head[0] << 8) | d->head[1]), (UInt16)((d->head[2] << 8) | d->head[3]));
			switch (b & 0xff) {
				case DGA_OK:		d->ackOk++;		break;
				case DGA_MISMATCH:	d->mismatch++;	break;
				case DGA_NOTACK:	d->notAck++;	break;
				case DGA_OOM:		d->oom++;		break;
				default:			d->ackNone++;	break;
			}
			break;
	}

	dgPut(d, t, a, b, x);
}

/* called after every SerReceive in the driver's byte reader */
static void dgRx(struct Globals *g, const UInt8 *p, UInt16 n, Err e)
{
	struct Dg *d = g->dg;
	UInt16 i, le, delay;
	UInt32 pend;
	Boolean cts, dsr;

	if (!d || !d->on || !d->log || d->quiet)
		return;

	if (e != errNone && e != serErrTimeOut) {

		le = SerGetStatus(g->serLibRef, &cts, &dsr);
		pend = 0;
		(void)SerReceiveCheck(g->serLibRef, &pend);
		if (!le)
			d->errOther++;
		for (i = 0; i < 7; i++)
			if (le & (1 << i))
				d->lineErr[i]++;
		dgPut(d, DGE_RXERR, (UInt8)e, le, (UInt16)pend);
	}

	if (n) {

		d->rxBytes += n;

		if (d->rxArm) {

			if (!d->nrx) {

				delay = (UInt16)((UInt16)TimGetTicks() - d->sentTick);
				d->ackDelay[delay > 15 ? 15 : delay]++;
				dgPut(d, DGE_RX0, p[0], n, delay);
			}

			for (i = 0; i < n; i++) {
				if (d->nrx < 6)
					d->head[d->nrx] = p[i];
				if (d->nrx < 65535)
					d->nrx++;
			}
		}
	}
}

static void dgExchBegin(struct Globals *g, UInt8 tid, UInt8 cmd)
{
	struct Dg *d = g->dg;

	if (!d || !d->on || d->quiet)
		return;

	dgCrumb(g, CR_XB, cmd, tid, 0);
	d->cmd = cmd;
	d->tid = tid;
	d->isData = (cmd == 0x02 || cmd == 0x03 || cmd == 0x04 || cmd == 0x80 || cmd == 0x81);
	d->var = 0;
	d->startWr = d->wr;
	d->exStart = TimGetTicks();
	d->attempts = 0;
}

static void dgExchEnd(struct Globals *g, Boolean ok)
{
	struct Dg *d = g->dg;
	UInt32 el;
	UInt8 v, c, at;

	if (!d || !d->on || d->quiet)
		return;

	dgCrumb(g, CR_XE, (UInt8)ok, d->tid, 0);
	el = TimGetTicks() - d->exStart;
	v = d->var;
	c = dgClass(d->cmd);
	at = d->attempts;

	d->vEx[v]++;
	d->vSends[v] += at;
	d->vTicks[v] += el;
	d->cEx[c]++;
	d->attHist[at > 8 ? 8 : at]++;
	if (!ok) {
		d->vFail[v]++;
		d->cFail[c]++;
	}
	else if (at > 1) {
		d->vOkN[v]++;
		d->cRetry[c]++;
	}
	else
		d->vOk1[v]++;

	if (d->junkThis)
		d->junkExch++;

	dgPut(d, DGE_END, d->tid, (UInt16)(((ok ? 1 : 0) << 8) | at), (UInt16)(el > 65535UL ? 65535UL : el));

	d->exTotal++;
	if (d->isData)
		d->dex++;

	/* most clean first-try exchanges are not worth ring space */
	if (ok && at == 1 && (d->dex & 15))
		d->wr = d->startWr > d->keepWr ? d->startWr : d->keepWr;
}

uint8_t dgTries(void *st)
{
	struct Dg *d = ((struct Globals*)st)->dg;

	return 4;
}

static UInt16 dgPeakLat(struct Globals *g)
{
	struct Dg *d = g->dg;
	UInt16 i, m = 0;

	if (!d)
		return 0;
	for (i = 0; i < 16; i++)
		if (d->lat[i] > m)
			m = d->lat[i];
	return m;
}

/* The wait for an ack follows the link: twice the slowest of the last 16 acks, never below the stock 21 ticks (210 ms) and never above 100 (1 s). On a
 * direct cable acks take 5 to 10 ticks, so this stays at the stock value; over NOWttl they took 17 to 43. A send that got nothing doubles the next wait. */
uint16_t dgWait(void *st, uint8_t attempt)
{
	UInt16 w = (UInt16)(dgPeakLat((struct Globals*)st) * 2);

	if (w < FRAG_ACK_WAIT_TIME)
		w = FRAG_ACK_WAIT_TIME;
	if (attempt >= 2)
		w = (UInt16)(w * 2);
	return w > 100 ? 100 : w;
}

/* two requests in flight only on a slow link and only when the reader says it copes (SD_FLAG_PIPE): a direct cable keeps the stock sequence */
static Boolean slotPrvUsePair(struct Globals *g)
{
	struct Dg *d = g->dg;

	return d && (g->sdi.flags & SD_FLAG_PIPE) && dgPeakLat(g) >= PAIR_MIN_LAT && (Int32)(TimGetTicks() - d->pairBlockUntil) >= 0;
}

void slkPickBuf(void *st, uint8_t tid, struct bv *head)
{
	struct Globals *g = (struct Globals*)st;

	if (!g->pairOn)
		return;
	if (tid == g->pairTidB)
		head->next = g->pairBvB;
	else if (tid == g->pairTidA)
		head->next = g->pairBvA;
	else
		head->next = &g->pairJunk;
}

static Boolean slotPrvPktExchangePair(struct Globals *g, void *sendA, UInt16 lenA, void *recvA, UInt16 maxA, Boolean *okA, void *sendB, UInt16 lenB, struct bv *bvB)
{
	struct SlkHdr hA = {.dstSlk = PKT_ADDR_SASD, .srcSck = PKT_ADDR_SASD, .pktTyp = PKT_TYP_SASD, };
	struct SlkHdr hB = {.dstSlk = PKT_ADDR_SASD, .srcSck = PKT_ADDR_SASD, .pktTyp = PKT_TYP_SASD, };
	struct bv bvA = BV_INIT(recvA, maxA, NULL);
	bool a = false, ok;
	struct Dg *d = g->dg;

	hA.tid = slotPrvGetNextTxTid(g);
	hB.tid = slotPrvGetNextTxTid(g);
	g->pairTidA = hA.tid;
	g->pairTidB = hB.tid;
	g->pairBvA = &bvA;
	g->pairBvB = bvB;
	g->pairJunk.next = NULL;
	g->pairJunk.ptr = g->pairJunkBuf;
	g->pairJunk.len = sizeof(g->pairJunkBuf);
	g->pairOn = true;

	dgExchBegin(g, hB.tid, *(const UInt8*)sendB);
	ok = fragSendPair(g, &hA, sendA, lenA, &hB, sendB, lenB, 4, &a);
	dgExchEnd(g, ok);

	g->pairOn = false;
	if (d && d->on && !d->quiet) {
		d->pairs++;
		if (!ok)
			d->pairFail++;
		/* a pair that needed a resend: on a direct cable the data behind the request got damaged (measured). Back to one request at a time for 30 s. */
		if (!ok || d->attempts > 1)
			d->pairBlockUntil = TimGetTicks() + 3000UL;
	}
	*okA = a;
	return ok;
}

/* variant 1/3 receive: bulk reads into the driver FIFO (Globals fifo/fifoPos/fifoLen), bytes served from there */
static Boolean dgHard(struct Globals *g)
{
	return true;
}

static UInt16 slotPrvSerRxHard(struct Globals *g, UInt8 *dst, UInt16 nBytes, UInt32 endTime)
{
	UInt16 done = 0, take;
	Int32 leftTime;
	UInt32 avail, got;
	Err e;

	while (done < nBytes) {

		if (g->fifoLen) {

			take = (UInt16)(nBytes - done);
			if (take > g->fifoLen)
				take = g->fifoLen;
			MemMove(dst + done, g->fifo + g->fifoPos, take);
			g->fifoPos += take;
			g->fifoLen -= take;
			done += take;
			continue;
		}

		leftTime = (Int32)(endTime - TimGetTicks());
		if (leftTime < 0)
			break;

		(void)SerClearErr(g->serLibRef);
		dgCrumb(g, CR_RX, 1, nBytes - done, leftTime > 65535L ? 65535U : (UInt16)leftTime);

		avail = 0;
		(void)SerReceiveCheck(g->serLibRef, &avail);
		if (avail > sizeof(g->fifo))
			avail = sizeof(g->fifo);
		if (!avail)
			avail = 1;				/* nothing queued: wait for one byte, up to the end time */

		e = errNone;
		got = SerReceive(g->serLibRef, g->fifo, avail, leftTime, &e);
		dgRx(g, g->fifo, (UInt16)got, e);
		g->fifoPos = 0;
		g->fifoLen = (UInt16)got;
	}

	return done;
}

static UInt16 dgSummary(struct Dg *d, UInt8 *p)
{
	UInt8 *q = p;
	UInt16 i;

	*q++ = 1;											/* layout version */
	*q++ = d->var;
	dgPut16(q, d->dumpId); q += 2;
	dgPut32(q, TimGetTicks()); q += 4;
	dgPut32(q, d->wr); q += 4;
	dgPut32(q, d->dex); q += 4;
	dgPut32(q, d->rxBytes); q += 4;
	dgPut16(q, d->nEv); q += 2;
	for (i = 0; i < 4; i++) {							/* per schedule */
		dgPut16(q, d->vEx[i]); q += 2;
		dgPut16(q, d->vOk1[i]); q += 2;
		dgPut16(q, d->vOkN[i]); q += 2;
		dgPut16(q, d->vFail[i]); q += 2;
		dgPut16(q, d->vSends[i]); q += 2;
		dgPut32(q, d->vTicks[i]); q += 4;
	}
	for (i = 0; i < 8; i++) {							/* per command class */
		dgPut16(q, d->cEx[i]); q += 2;
		dgPut16(q, d->cRetry[i]); q += 2;
		dgPut16(q, d->cFail[i]); q += 2;
	}
	for (i = 0; i < 9; i++) { dgPut16(q, d->attHist[i]); q += 2; }
	for (i = 0; i < 16; i++) { dgPut16(q, d->ackDelay[i]); q += 2; }
	for (i = 0; i < 8; i++) { dgPut16(q, d->lineErr[i]); q += 2; }
	dgPut16(q, d->errOther); q += 2;
	dgPut16(q, d->repaired); q += 2;
	dgPut16(q, d->pairs); q += 2;
	dgPut16(q, d->pairFail); q += 2;
	dgPut16(q, d->junkExch); q += 2;
	dgPut32(q, d->junkBytes); q += 4;
	dgPut16(q, d->badCrc); q += 2;
	dgPut16(q, d->mismatch); q += 2;
	dgPut16(q, d->notAck); q += 2;
	dgPut16(q, d->oom); q += 2;
	dgPut16(q, d->ackNone); q += 2;
	dgPut16(q, d->ackOk); q += 2;
	dgPut16(q, d->dumpOk); q += 2;
	dgPut16(q, d->dumpFail); q += 2;

	return (UInt16)(q - p);
}

static Boolean dgSendPkt(struct Globals *g, UInt8 kind, UInt16 idx, UInt16 len)
{
	UInt8 resp[8];

	g->secBuf[0] = CMD_DIAG;
	g->secBuf[1] = kind;
	dgPut16(g->secBuf + 2, idx);
	dgPut16(g->secBuf + 4, g->dg->dumpId);

	return slotPrvPktExchange(g, g->secBuf, 6 + len, resp, sizeof(resp));
}

/* upload the log to the reader. force: the Palm is going to sleep / the slot closes, send what is there. Called only between
 * exchanges, when no read or write is in progress (g->secBuf is free). */
static void dgDump(struct Globals *g, Boolean force)
{
	struct Dg *d = g->dg;
	UInt32 pending, from, k, n, i;
	UInt16 idx = 0, len, total, off, now;
	UInt8 *p;
	Boolean ok;

	if (!d || !d->on || !d->log || !(g->sdi.flags & SD_FLAG_DIAG) || !(g->sdi.flags & SD_FLAG_COMMS))
		return;
	if (d->noDumpUntil && (Int32)(TimGetTicks() - d->noDumpUntil) < 0)
		return;

	pending = d->wr - d->dumped;
	if (force) {
		if (!pending && d->exTotal == d->dumpedExTotal)
			return;
	}
	else if (d->dex < 24)								/* never during the mount: its first reads are what the Palm judges the card by */
		return;
	else if (pending < (UInt32)(d->nEv >> 2) * 3 && (d->dex - d->dumpedDex) < 128 && !(pending && (TimGetTicks() - d->lastDump) > 6000UL))
		return;

	dgCrumb(g, CR_DUMP, 0, 0, 0);
	d->quiet = 1;
	d->dumpId++;

	/* the summary goes in slices of at most 100 bytes: kind 1, data = total length, offset, bytes */
	total = dgSummary(d, g->secBuf + 250);
	ok = true;
	for (off = 0; ok && off < total; off += now) {

		now = total - off;
		if (now > DG_SUMMARY_SLICE)
			now = DG_SUMMARY_SLICE;
		dgPut16(g->secBuf + 6, total);
		dgPut16(g->secBuf + 8, off);
		MemMove(g->secBuf + 10, g->secBuf + 250 + off, now);
		ok = dgSendPkt(g, 1, idx++, (UInt16)(4 + now));
	}

	from = (pending > d->nEv) ? d->wr - d->nEv : d->dumped;
	for (k = from; ok && k < d->wr; k += n) {

		n = d->wr - k;
		if (n > DG_CHUNK_EVENTS)
			n = DG_CHUNK_EVENTS;

		dgPut32(g->secBuf + 6, k);
		p = g->secBuf + 10;
		for (i = 0; i < n; i++) {
			struct DgEv *e = &d->ring[(k + i) & (d->nEv - 1)];

			p[0] = e->t;
			p[1] = e->a;
			dgPut16(p + 2, e->b);
			dgPut16(p + 4, e->tick);
			dgPut16(p + 6, e->x);
			p += 8;
		}
		ok = dgSendPkt(g, 2, idx++, (UInt16)(4 + n * 8));
	}

	d->quiet = 0;
	dgCrumb(g, CR_DUMP_END, ok, 0, 0);
	d->lastDump = TimGetTicks();

	if (ok) {
		d->dumpedExTotal = d->exTotal;
		d->dumpedDex = d->dex;
		d->dumped = d->wr;
		d->dumpOk++;
		d->dumpFails = 0;
		d->noDumpUntil = 0;
	}
	else {
		d->dumpFail++;
		d->dumpFails++;
		d->noDumpUntil = (TimGetTicks() + (d->dumpFails >= 3 ? 6000UL : 1500UL)) | 1;	/* rest the link for a while, then try again */
	}
}
