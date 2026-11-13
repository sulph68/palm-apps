#define	BUILDING_APP_OR_LIB		1
#include <PalmOS.h>
#include <SlotDrvrLib.h>
#include <ExpansionMgr.h>
#include <SerialMgrOld.h>
#include <VFSMgr.h>
#include <stddef.h>
#include "maxSpeed.h"
#include "bufvec.h"
#include "proto.h"
#include "iface.h"
#include "frag.h"
#include "tol.h"


#define MY_CRID					'PpSS'


#ifdef SASD_TOL
#define SERIAL_BUFFER_REQD		2080		/* room for a flood of junk bytes on top of a data packet */
#else
#define SERIAL_BUFFER_REQD		(SD_PROTO_MAX_MSG + 32)
#endif




//Oppan Sony Style ...
	struct CardPartitionLayoutV1 {			//allocated by us with owner of zero. freed by caller
		UInt32 version;						// 0x00 - the value 1 is is stored here
		UInt32 partStartMinus1;				// 0x04 - if part is to start at sector 0x19, then 0x18 will be stored here
	};

	struct CardMetricsTypeV1 {				//used by expansion manager v1 (sony 3.5)
		UInt32 version;						// 0x00 - the value 1 is is stored here
		UInt32 totalSectors;				// 0x04
		UInt16 bytesPerSector;				// 0x08
		UInt8 sectorsPerBlock;				// 0x0a
		UInt8 rfu;							// 0x0b - the value 0 is written here
		UInt16 sectorsPerHead;				// 0x0c
		UInt16 headsPerCylinder;			// 0x0e
		UInt32 unknown_1;					// 0x10 - the value 1 is is stored here
		struct CardPartitionLayoutV1 *pl;	// 0x14 - allocated by the driver and freed by called. 8 bytes
	};


struct Globals {

	//library/slot
	UInt16 refCnt, slotRef;
	UInt8 nextTxTid;
	
	//serial port
	UInt16 serLibRef;
	void *serialBuf;
	UInt32 lastRate;
		
	//serial if
	uint16_t(*serRx)(struct Globals *g, void *dstP, uint16_t nBytes, uint32_t endTime);
	void (*serTx)(struct Globals *g, const void *srcP, uint16_t len);
	
	//SD card/reader
	struct SdInfoData sdi;
	
	//RX buffer
	UInt8 secBuf[slotSectorSize + 1];

#ifdef SASD_TOL
	struct Dg *dg;
	UInt16 fifoPos, fifoLen;
#ifdef TXTEST
	UInt16 txTestN;
#endif
	UInt8 fifo[256];
	Boolean pairOn;
	UInt8 pairTidA, pairTidB;
	struct bv *pairBvA, *pairBvB;
	struct bv pairJunk;
	UInt8 pairJunkBuf[8];
	UInt16 slotLibRef, notifyCard;		/* for the unmount notification */
	LocalID notifyDb;
	Boolean notifyOn, selfUnmount;		/* selfUnmount: the driver itself is removing the card (wake alarm) */
#endif
};



static Boolean isSonyEarlyVFS(void)	//PalmOS 3.5 Sony devices have an early implementation of ExpMgr/VFSMgr that differs in a few ways that we care about. This funcs tells us if this is such a device
{
	UInt32 expnVer;
	
	return errNone == FtrGet(sysFileCExpansionMgr, expFtrIDVersion, &expnVer) && expnVer == 1;
}

static UInt8 slotPrvGetNextTxTid(struct Globals *g)
{
	g->nextTxTid++;
	if (g->nextTxTid == 0xff)
		g->nextTxTid = 1;
	
	return g->nextTxTid;
}

static Err slotPrvPortClose(struct Globals *g);

static Err slotPrvPortOpen(struct Globals *g, UInt32 rate)
{
	Err e;
	UInt32 bufSz;
	
#ifdef SASD_TOL
	if (g->serialBuf)		/* already open (a second wake alarm, a reconnect): one open at a time, or the first never gets closed and the port stays held */
		slotPrvPortClose(g);
#endif
	e = SysLibFind("Serial Library", &g->serLibRef);
	if (e != errNone)
		goto out;
	
	e = SerOpen(g->serLibRef, serPortDefault, rate);
	if (e != errNone)
		goto out;
	
	bufSz = SERIAL_BUFFER_REQD;
	g->serialBuf = MemPtrNew(bufSz);
#ifdef SASD_TOL
	if (!g->serialBuf) {		/* little memory: the stock size still works */
		bufSz = SD_PROTO_MAX_MSG + 32;
		g->serialBuf = MemPtrNew(bufSz);
	}
#endif
	if (!g->serialBuf) {
	
		e = memErrNotEnoughSpace;
		goto out_close_port;
	}
	
	MemPtrSetOwner(g->serialBuf, 0);
	
	e = SerSetReceiveBuffer(g->serLibRef, g->serialBuf, bufSz);
	if (e != errNone)
		goto out_free_buf;
	
	return errNone;

out_free_buf:
	MemPtrFree(g->serialBuf);

out_close_port:
	SerClose(g->serLibRef);

out:
	return e;
}

static Err slotPrvPortClose(struct Globals *g)
{
	Err e = errNone;
	
	if (g->serialBuf) {
		
		(void)SerSetReceiveBuffer(g->serLibRef, NULL, 0);
		MemPtrFree(g->serialBuf);
		e = SerClose(g->serLibRef);		/* serErrStillOpen: someone else has the port open too */
		g->serialBuf = NULL;
	}
	return e;
}

static Boolean slotPrvPortSetRate(struct Globals *g, UInt32 rate)
{
	SerSettingsType ss;
	Err e;
	
	e = SerGetSettings(g->serLibRef, &ss);
	if (ss.baudRate != rate) {
		
		ss.baudRate = rate;
		e = SerSetSettings(g->serLibRef, &ss);
	}
	
	if (e == errNone)
		return true;
	
	SysFatalAlert("set rate fail");
	
	return false;
}

#ifdef SASD_TOL
static void dgExchBegin(struct Globals *g, UInt8 tid, UInt8 cmd);
static void dgExchEnd(struct Globals *g, Boolean ok);
static void dgRx(struct Globals *g, const UInt8 *p, UInt16 n, Err e);
static void dgAlloc(struct Globals *g);
static void dgFree(struct Globals *g);
static void dgNewLink(struct Globals *g);
static void dgDump(struct Globals *g, Boolean force);
#if DG_LOG
static void dgCrumb(struct Globals *g, UInt8 t, UInt8 a, UInt16 b, UInt16 x);
#else
#define dgCrumb(g, t, a, b, x)	((void)0)
#endif
static Boolean dgHard(struct Globals *g);
static Boolean slotPrvUsePair(struct Globals *g);
static Boolean slotPrvPktExchangePair(struct Globals *g, void *sendA, UInt16 lenA, void *recvA, UInt16 maxA, Boolean *okA, void *sendB, UInt16 lenB, struct bv *bvB);
static UInt16 slotPrvSerRxHard(struct Globals *g, UInt8 *dst, UInt16 nBytes, UInt32 endTime);
#endif

static Boolean slotPrvPktExchangeEx(struct Globals *g, void *send, UInt16 sendLen, struct bv *bv)
{
	struct SlkHdr slkHdrTx = {.dstSlk = PKT_ADDR_SASD, .srcSck = PKT_ADDR_SASD, .pktTyp = PKT_TYP_SASD, .tid = slotPrvGetNextTxTid(g), };
#ifdef SASD_TOL
	Boolean ok;
	
	dgExchBegin(g, slkHdrTx.tid, *(const UInt8*)send);
	ok = fragSend(g, &slkHdrTx, send, sendLen, bv);
	dgExchEnd(g, ok);
	return ok;
#else
	return fragSend(g, &slkHdrTx, send, sendLen, bv);
#endif
}

static Boolean slotPrvPktExchange(struct Globals *g, void *send, UInt16 sendLen, void *recv, UInt16 recvLenMax)
{
	struct bv bv = BV_INIT(recv, recvLenMax, NULL);
	
	return slotPrvPktExchangeEx(g, send, sendLen, &bv);
}

#ifdef SASD_TOL
#include "tol.inc"
#endif

static Err slotPrvCommsEstablish(struct Globals *g)
{
	struct SdBaudData setBaud, resultBaud;
	UInt32 maxBaudRate = getMaxSpeed();
	UInt8 i, cmdInfo = CMD_GET_INFO;
	struct SdPingData ping, pong;
	struct SdInfoData info;
	Boolean nonstandardRate;
	Err e;

retry:
#ifdef SASD_TOL
	dgNewLink(g);
#endif
	info.flags = 0;
	g->sdi.flags = 0;	//zero them to start with
	
	//open serial port & slk socket (try last rate if we have one)
	nonstandardRate = !!g->lastRate;
	e = slotPrvPortOpen(g,  nonstandardRate ? g->lastRate : SD_BAUDRATE_INITIAL);
	if (e != errNone)
		return e;
		
	//generate ping message
	ping.cmd = CMD_PING;
	for (i = 0; i < sizeof(ping.garbage); i++)
		ping.garbage[i] = SysRandom(0);
	
	//exchange with reader
	if (!slotPrvPktExchange(g, &ping, sizeof(ping), &pong, sizeof(pong))) {
	
		if (nonstandardRate) {
		
			slotPrvPortClose(g);
			g->lastRate = 0;
			goto retry;
		}
		goto fail;
	}
	
	//verify further
	if (pong.cmd != RSP_PONG || MemCmp(ping.garbage, pong.garbage, sizeof(ping.garbage)))
		goto fail;
	
	//set baud rate
	setBaud.cmd = CMD_SET_BAUD;
	setBaud.rate = maxBaudRate;
	if (!slotPrvPktExchange(g, &setBaud, sizeof(setBaud), &resultBaud, sizeof(resultBaud))) {
		//SysFatalAlert("baud fail");
		goto fail;
	}
	
	//verify further
	if (resultBaud.cmd != RSP_OK || !resultBaud.rate || resultBaud.rate > maxBaudRate) {
		//SysFatalAlert("baud reply bad 1");
		goto fail;
	}
	
	//set rate now...
	if (!slotPrvPortSetRate(g, resultBaud.rate)){
		//SysFatalAlert("baud set");
		goto fail;
	}

	//get info
	if (!slotPrvPktExchange(g, &cmdInfo, sizeof(cmdInfo), &info, sizeof(info))){
		//SysFatalAlert("info err");
		goto fail;
	}

	//verify further
	if (info.cmd != RSP_INFO || !(info.flags & SD_FLAG_COMMS)){
		//SysFatalAlert("info bad");
		goto fail;
	}
	
	g->lastRate = resultBaud.rate;
	MemMove(&g->sdi, &info, sizeof(struct SdInfoData));

	return errNone;

fail:
	slotPrvPortClose(g);
	return e;
}

static void slotPrvInfoAlert(const char *str)
{
	DmOpenRef db;
	
	db = DmOpenDatabaseByTypeCreator(sysFileTSlotDriver, MY_CRID, dmModeReadOnly);
	FrmCustomAlert(1000, str, " ", " ");
	DmCloseDatabase(db);
}

static uint16_t slotPrvSerRxV2(struct Globals *g, void *dstP, uint16_t nBytes, uint32_t endTime)
{
	uint8_t *dst = (uint8_t*)dstP;
	uint16_t done = 0, leftBytes;
	int32_t leftTime;
		
#ifdef SASD_TOL
	if (dgHard(g))
		return slotPrvSerRxHard(g, (UInt8*)dstP, nBytes, endTime);
#endif
	while ((leftBytes = nBytes - done) != 0 && (leftTime = endTime - TimGetTicks()) >= 0) {
		
		Err e = errNone;
		uint32_t now;
		
		(void)SerClearErr(g->serLibRef);
#ifdef SASD_TOL
		dgCrumb(g, 0x21, 0, leftBytes, leftTime > 65535L ? 65535U : (UInt16)leftTime);
#endif
		now = SerReceive(g->serLibRef, dst, leftBytes, leftTime, &e);
#ifdef SASD_TOL
		dgRx(g, dst, (UInt16)now, e);
#endif
		done += now;
		dst += now;
	}
	
	if (done && done != nBytes) {
	/*
		char x[32];
		
		StrPrintF(x, "done %u/%u time %lu (exp %lu)", done, nBytes, TimGetTicks(), endTime);
		slotPrvInfoAlert(x);
	*/
	}
	
	return done;
}

#ifdef SASD_TOL
/* SerSend only queues the bytes. If the Serial Manager's transmitter stops (seen: four resends logged by the Palm, nothing on the line, and a reopen of the port - the wake
 * handshake - makes it work again) the queue never empties: wait for it to drain in the time the bytes need plus 120 ms, and if it does not, flush and reopen the port. The
 * exchange then goes on as if the packet had been lost, and its resend goes out. */
static void slotPrvTxDrain(struct Globals *g, uint16_t len)
{
	UInt32 rate = g->lastRate ? g->lastRate : SD_BAUDRATE_INITIAL, left = 0;
	UInt8 tries;
	Err e;

	e = SerSendWait(g->serLibRef, (Int32)(((UInt32)len * 1500UL) / rate + 12));
#ifdef TXTEST
	if (++g->txTestN % TXTEST == 0)
		e = serErrTimeOut;
#endif
	if (e == errNone)
		return;
	(void)SerSendCheck(g->serLibRef, &left);
	dgNote(g, 11, 0, len, (uint16_t)left);
	(void)SerClearErr(g->serLibRef);
	(void)SerSendFlush(g->serLibRef);
	slotPrvPortClose(g);
	g->fifoPos = g->fifoLen = 0;			/* bytes of the old session are no use to the new one */
	for (tries = 0; tries < 3; tries++) {
		if (slotPrvPortOpen(g, rate) == errNone)
			return;
		dgNote(g, 11, 1, len, tries);
		SysTaskDelay(10);
	}
}
#endif

static void slotPrvSerTxV2(struct Globals *g, const void *srcP, uint16_t len)
{
	const uint8_t *src = (const uint8_t*)srcP;
#ifdef SASD_TOL
	UInt8 stuck = 0;
	uint16_t origLen = len;
#endif
	
	while (len) {
	
		Err e = errNone;
		uint32_t now;
		
#ifdef SASD_TOL
		dgCrumb(g, 0x20, stuck, len, 0);
#endif
		now = SerSend(g->serLibRef, src, len, &e);
		if (e == errNone)	//errNone means entire buffer was sent
			break;
		
		src += now;
		len -= now;
#ifdef SASD_TOL
		/* nothing goes out and the error does not go away: the stock loop spins for ever. Clear it, wait a tick, and after 0.6 s give up: the exchange times out and is sent again. */
		if (now)
			stuck = 0;
		else {
			(void)SerClearErr(g->serLibRef);
			if (++stuck > 60)
				break;
			SysTaskDelay(1);
		}
#endif
	}
#ifdef SASD_TOL
	slotPrvTxDrain(g, origLen);
#endif
}

static uint16_t slotPrvSerRxV1(struct Globals *g, void *dstP, uint16_t nBytes, uint32_t endTime)
{
	uint8_t *dst = (uint8_t*)dstP;
	uint16_t done = 0, wanted;
	int32_t leftTime;
		
	while ((wanted = nBytes - done) && (leftTime = endTime - TimGetTicks()) >= 0) {
		
		UInt32 numAvail;
		Err e;
		
		e = SerReceiveCheck(g->serLibRef, &numAvail);
		if (e != errNone) {
			(void)SerClearErr(g->serLibRef);
			continue;
		}
		
		if (numAvail > wanted)
			numAvail = wanted;
		else if (numAvail == 0)	//so we do not busy-loop here
			numAvail = 1;
		
		e = SerReceive10(g->serLibRef, dst, numAvail, leftTime);
		if (e == errNone) {
		
			done += numAvail;
			dst += numAvail;
		}
	}
	
	return done;
}

static void slotPrvSerTxV1(struct Globals *g, const void *srcP, uint16_t len)
{
	const uint8_t *src = (const uint8_t*)srcP;
	
	//there is no flow control on the other side, so belt it all out...
	(void)SerSend10(g->serLibRef, srcP, len);
}

#ifdef SASD_TOL
/* The volume of this card was unmounted (by the user, an app or the file system after errors): stop holding the serial port. The notification comes from the UI task, after the unmount.
 * Another volume going away (a real slot beside this one) is not ours: if a volume of this slot is still mounted, nothing is done. */
static void slotPrvCloseAlarm(UInt16 almProcCmd, SysAlarmTriggeredParamType *paramP)
{
	struct Globals *g = (struct Globals*)paramP->ref;
	UInt32 iter = vfsIteratorStart;
	UInt16 vol;
	VolumeInfoType vi;
	Err e;
	
	if (g->selfUnmount || !g->serialBuf)
		return;
	
	while (iter != vfsIteratorStop && VFSVolumeEnumerate(&vol, &iter) == errNone) {
		if (VFSVolumeInfo(vol, &vi) == errNone && vi.slotLibRefNum == g->slotLibRef && vi.slotRefNum == g->slotRef)
			return;
	}
	
	g->sdi.flags = 0;		/* no card, no link: the next wake sees a card that came back and mounts it again */
	e = slotPrvPortClose(g);
	dgCrumb(g, 0x36, (UInt8)e, 0, 0);		/* the SerClose result: 8 = serErrStillOpen */
}

static Err slotPrvVolumeGone(SysNotifyParamType *np)
{
	struct Globals *g = (struct Globals*)np->userDataP;
	
	dgCrumb(g, 0x37, g->selfUnmount, g->serialBuf != NULL, 0);
	/* the port is closed from an alarm procedure, as it was opened (the serial manager ties the open to the context that made it) */
	if (!g->selfUnmount && g->serialBuf)
		(void)AlmSetProcAlarm(slotPrvCloseAlarm, (UInt32)g, TimGetSeconds());
	return errNone;
}

static void slotPrvNotifyOn(struct Globals *g)
{
	UInt32 ver;
	DmSearchStateType st;
	
	if (FtrGet(sysFtrCreator, sysFtrNumNotifyMgrVersion, &ver) != errNone)
		return;
	if (DmGetNextDatabaseByTypeCreator(true, &st, sysFileTSlotDriver, MY_CRID, false, &g->notifyCard, &g->notifyDb) != errNone)
		return;
	if (SysNotifyRegister(g->notifyCard, g->notifyDb, sysNotifyVolumeUnmountedEvent, slotPrvVolumeGone, sysNotifyNormalPriority, g) == errNone)
		g->notifyOn = true;
}

static void slotPrvNotifyOff(struct Globals *g)
{
	if (g->notifyOn) {
		(void)SysNotifyUnregister(g->notifyCard, g->notifyDb, sysNotifyVolumeUnmountedEvent, sysNotifyNormalPriority);
		g->notifyOn = false;
	}
}
#endif

static void slotPrvOpenAlarm(UInt16 almProcCmd, SysAlarmTriggeredParamType *paramP)
{
	struct Globals *g = (struct Globals*)paramP->ref;
	Err e;
	
	e = slotPrvCommsEstablish(g);
	if (e == errNone && (g->sdi.flags & (SD_FLAG_HAVE_CARD | SD_FLAG_COMMS)) == (SD_FLAG_HAVE_CARD | SD_FLAG_COMMS)) {
		
		e = ExpCardInserted(g->slotRef);
		if (e != errNone)
			SysFatalAlert("Cannot 'insert' card");
	}
}

Err SlotOpen(UInt16 slotLibRefNum)
{
	SysLibTblEntryPtr entry = SysLibTblEntry(slotLibRefNum);
	struct Globals *g = (struct Globals*)entry->globalsP;
	Err e;
	
	if (!g) {
	
		UInt32 ver;
		
		entry->globalsP = g = (struct Globals*)MemChunkNew(0, sizeof(struct Globals), memNewChunkFlagNonMovable);
		MemSet(g, sizeof(struct Globals), 0);
#ifdef SASD_TOL
		dgAlloc(g);
#endif
		
		if (errNone != ExpSlotRegister(slotLibRefNum, &g->slotRef))
			SysFatalAlert("Cannot init slot");
#ifdef SASD_TOL
		g->slotLibRef = slotLibRefNum;
		slotPrvNotifyOn(g);
#endif
		
		if (errNone == FtrGet(sysFtrCreator, sysFtrNumROMVersion, &ver) && ver >= sysMakeROMVersion(2, 0, 0, 0, 0)) {
		
			g->serRx = slotPrvSerRxV2;
			g->serTx = slotPrvSerTxV2;
		}
		else {
			
			g->serRx = slotPrvSerRxV1;
			g->serTx = slotPrvSerTxV1;
		}
		
		//XXX: open is triggered before the serial library is ready, so we use a procedural alarm to delay our resumption a bit.
		//Procedural alarms are available in PalmOS 3.2 or newer
	
		e = AlmSetProcAlarm(slotPrvOpenAlarm, (UInt32)g, TimGetSeconds());
		if (e != errNone) {
			MemPtrFree(g);
			return e;
		}
	}
	
	g->refCnt++;
	
	return errNone;
}

static struct Globals* slotPrvGetGlobals(UInt16 slotLibRefNum)
{
	SysLibTblEntryPtr entry = SysLibTblEntry(slotLibRefNum);
	struct Globals *g = (struct Globals*)entry->globalsP;
	
	if (!g || !g->refCnt)
		SysFatalAlert("NOT OPEN");
	
	return g;
}

Err SlotClose(UInt16 slotLibRefNum)
{
	SysLibTblEntryPtr entry = SysLibTblEntry(slotLibRefNum);
	struct Globals *g = slotPrvGetGlobals(slotLibRefNum);
		
	if (--g->refCnt)
		return expErrStillOpen;
	
	ExpSlotUnregister(g->slotRef);
	slotPrvPortClose(g);
#ifdef SASD_TOL
	slotPrvNotifyOff(g);
	(void)AlmSetProcAlarm(slotPrvCloseAlarm, (UInt32)g, 0);	/* a close still waiting must not run on freed memory */
	dgFree(g);
#endif
	MemChunkFree(g);
	entry->globalsP = NULL;
	
	return errNone;
}


Err SlotSleep(UInt16 slotLibRefNum)
{
	struct Globals *g = slotPrvGetGlobals(slotLibRefNum);
	
#ifdef SASD_TOL
	dgCrumb(g, 0x30, 0, 0, 0);
#endif
	slotPrvPortClose(g);
	
	return errNone;
}

static void slotPrvWakeAlarm(UInt16 almProcCmd, SysAlarmTriggeredParamType *paramP)
{
	struct Globals *g = (struct Globals*)paramP->ref;
	Boolean hadCard, haveCard, removed = false, inserted = false;
	struct SdInfoData sdi;
	Err e = errNone;
	
	
#ifdef SASD_TOL
	dgCrumb(g, 0x33, 0, 0, 0);
#endif
	MemMove(&sdi, &g->sdi, sizeof(struct SdInfoData));
	e = slotPrvCommsEstablish(g);
#ifdef SASD_TOL
	dgCrumb(g, 0x34, (UInt8)e, 0, 0);
#endif
	hadCard = (sdi.flags & (SD_FLAG_HAVE_CARD | SD_FLAG_COMMS)) == (SD_FLAG_HAVE_CARD | SD_FLAG_COMMS);
	haveCard = (g->sdi.flags & (SD_FLAG_HAVE_CARD | SD_FLAG_COMMS)) == (SD_FLAG_HAVE_CARD | SD_FLAG_COMMS);
		
	if (hadCard && haveCard)
		removed = inserted = !!MemCmp(&sdi, &g->sdi, sizeof(struct SdInfoData));
	else if (hadCard)
		removed = true;
	else if (haveCard)
		inserted = true;

#ifdef SASD_TOL
	g->selfUnmount = true;
#endif
	if (removed)
		(void)ExpCardRemoved(g->slotRef);
#ifdef SASD_TOL
	g->selfUnmount = false;
#endif
	
	if (inserted) {
		e = ExpCardInserted(g->slotRef);
		if (e != errNone)
			SysFatalAlert("Cannot 'insert' card");
	}
}

Err SlotWake(UInt16 slotLibRefNum)
{
	struct Globals *g = slotPrvGetGlobals(slotLibRefNum);

#ifdef SASD_TOL
	dgCrumb(g, 0x32, 0, 0, 0);
#endif
	//XXX: wake is triggered before the serial library is ready, so we use a procedural alarm to delay our resumption a bit.
	//Procedural alarms are available in PalmOS 3.2 or newer

	return AlmSetProcAlarm(slotPrvWakeAlarm, (UInt32)g, TimGetSeconds());
}

Err SlotReturnNoError(UInt16 slotLibRefNum)
{
	struct Globals *g = slotPrvGetGlobals(slotLibRefNum);
	
	return errNone;
}

Err SlotReturnUnsupportedOp(UInt16 slotLibRefNum)
{
	struct Globals *g = slotPrvGetGlobals(slotLibRefNum);
	
	return expErrUnsupportedOperation;
}

UInt32 SlotLibAPIVersion(UInt16 slotLibRefNum)
{
	return isSonyEarlyVFS() ? 1 : slotDrvrAPIVersion;
}

Err SlotCustomControl(UInt16 slotLibRefNum, UInt32 apiCreator, UInt16 apiSelector, void *valueP, UInt16 *valueLenP)
{
	struct Globals *g = slotPrvGetGlobals(slotLibRefNum);
	UInt8 txGetBatt = CMD_GET_BATT, txFwUpd = CMD_FW_UPDATE_START, rx[2];
	
	if (apiCreator == SASD_API_CREATOR && valueP && valueLenP) switch (apiSelector) {
		case SASD_VERIFY:
			if (*valueLenP != sizeof(UInt8))
				break;
			(*(UInt8*)valueP) ^= 0xff;
			return errNone;
	
		case SASD_GET_VERS:
			if (*valueLenP != sizeof(struct SdVersionInfo))
				break;
			
			MemMove(valueP, &g->sdi.ver, sizeof(struct SdVersionInfo));
			return errNone;
		
		case SASD_BATTERY_LVL:
			if (*valueLenP != sizeof(struct SdBatteryData))
				break;
			
			if (!(g->sdi.flags & SD_FLAG_COMMS) || !slotPrvPktExchange(g, &txGetBatt, sizeof(txGetBatt), valueP, sizeof(struct SdBatteryData)) || ((struct SdBatteryData*)valueP)->cmd != RSP_OK)
				return expErrEnumerationEmpty;
			return errNone;
		
		case SASD_FW_UPDATE_START:
			if (*valueLenP)
				break;
			
			if (!(g->sdi.flags & SD_FLAG_COMMS) || !slotPrvPktExchange(g, &txFwUpd, sizeof(txFwUpd), rx, sizeof(rx[0])) || rx[0] != RSP_OK)
				return expErrEnumerationEmpty;
			
			g->sdi.flags = 0;
			slotPrvPortClose(g);
			return errNone;
		
		case SASD_GET_IFACE_SPEED:
			if (*valueLenP != sizeof(UInt32))
				break;
			
			if (!(g->sdi.flags & SD_FLAG_COMMS))
				return expErrEnumerationEmpty;
			
			*(UInt32*)valueP = g->lastRate;
			return errNone;
		
		case SASD_GET_FLAGS:
			if (*valueLenP != sizeof(UInt32))
				break;
			
			if (!(g->sdi.flags & SD_FLAG_COMMS))
				return expErrEnumerationEmpty;
			
			*(UInt32*)valueP = g->sdi.flags;
			return errNone;
		
		default:
			break;
	}
	
	return expErrUnsupportedOperation;
}

struct {
	UInt8 manuf;
	UInt16 oem;
	char name[18];
} const static sdManufs[] = {
	{1, 'PA', "Panasonic"},
	{2, 'TM', "Toshiba"},
	{3, 'SD', "Sandisk"},
	{3, 'PT', "Sandisk"},
	{6, 'RK', "Fuji"},
	{9, 'AP', "ATP"},
	{17, 'D ', "PNY"},
	{25, 'DY', "Integral Platinum"},
	{48, 'PR', "Integral Platinum"},
	{27, 'SM', "Samsung"},
	{28, 'SV',"Transcend"},
	{116, 'JE',"Transcend"},
	{116, 'J\\',"Transcend"},
	{29, 'AD', "Corsair"},
	{29, 'AF', "AData"},
	{39, 'PH', "Phison"},
	{40, 'BE', "Lexar/PNY"},
	{49, 'SP', "Silicon Power"},
	{65, '42', "Kingston"},
	{130, 'JT', "Sony"},
};

Err SlotCardInfo(UInt16 slotLibRefNum, UInt16 slotRefNum, ExpCardInfoType *infoP)
{
	struct Globals *g = slotPrvGetGlobals(slotLibRefNum);
	UInt32 numKb = g->sdi.numBlocks;
	const char *manuf = NULL;
	UInt8 divShift;
	char letter;
	
	if (!(g->sdi.flags & SD_FLAG_HAVE_CARD))
		return expErrCardNotPresent;
	
	infoP->capabilityFlags = expCapabilityHasStorage | ((g->sdi.flags & SD_FLAG_READ_ONLY) ? expCapabilityReadOnly : 0);
	
	if (g->sdi.flags & SD_FLAG_IS_MMC) {
		
		switch (g->sdi.manuf) {
			case 1:		manuf = "Infineon";			break;
			case 2:		manuf = "Sandisk";			break;
			case 6:		manuf = "Hitachi";			break;
			case 22:	manuf = "Mobipocket eBook";	break;
			case 44:	manuf = "PNY";				break;
		}
	}
	else {
		
		UInt8 i;
		
		for (i = 0; i < sizeof(sdManufs) / sizeof(*sdManufs); i++) {
			if (g->sdi.manuf == sdManufs[i].manuf && g->sdi.oem == sdManufs[i].oem) {
				manuf = sdManufs[i].name;
				break;
			}
		}
	}
	
	if (manuf)
		StrCopy(infoP->manufacturerStr, manuf);
	else if (g->sdi.flags & SD_FLAG_IS_MMC)
		StrPrintF(infoP->manufacturerStr, "?: 'm-%d'", g->sdi.manuf);
	else
		StrPrintF(infoP->manufacturerStr, "?: 's-%d-%04x'", g->sdi.manuf, g->sdi.oem);
	
	if (numKb >= (1UL << (divShift = 31)))
		letter = 'T';
	else if (numKb >= (1UL << (divShift = 21)))
		letter = 'G';
	else if (numKb >= (1UL << (divShift = 11)))
		letter = 'M';
	else {
		divShift = 1;
		letter ='K';
	}
	
	if (numKb & ((1UL << divShift) - 1))
		StrPrintF(infoP->deviceClassStr, "%lu.%lu%c ", numKb >> divShift, (numKb & ((1UL << divShift) - 1)) * 10 >> divShift, letter);
	else
		StrPrintF(infoP->deviceClassStr, "%lu%c ", numKb >> divShift, letter);
	
	if (g->sdi.flags & SD_FLAG_IS_MMC) {
		
		if (!(g->sdi.flags & SD_FLAG_IS_NEW_VER))
			StrCat(infoP->deviceClassStr, "MMC");
		else if (g->sdi.flags & SD_FLAG_IS_HIGH_CAP)
			StrCat(infoP->deviceClassStr, "HS-MMC");
		else
			StrCat(infoP->deviceClassStr, "MMCplus");
	}
	else {
		
		if (!(g->sdi.flags & SD_FLAG_IS_NEW_VER))
			StrCat(infoP->deviceClassStr, "SDv1");
		else if (g->sdi.flags & SD_FLAG_IS_HIGH_CAP)
			StrCat(infoP->deviceClassStr, "SDxC");
		else
			StrCat(infoP->deviceClassStr, "SDv2");
	}
	
	StrCopy(infoP->productStr, "Storage");
	StrIToA(infoP->deviceUniqueIDStr, g->sdi.snum);
	
	return errNone;
}

Err SlotMediaType(UInt16 slotLibRefNum, UInt16 slotRefNum, UInt32 *mediaTypeP)
{
	*mediaTypeP = expMediaType_SecureDigital;
	return errNone;
}

Boolean SlotCardIsFilesystemSupported(UInt16 slotLibRefNum, UInt16 slotRefNum, UInt32 filesystemType)
{
	return filesystemType == vfsFilesystemType_VFAT || filesystemType == vfsFilesystemType_FAT;
}

Err SlotCardMetrics(UInt16 slotLibRefNum, UInt16 slotRefNum, CardMetricsPtr metricsV2)
{
	struct Globals *g = slotPrvGetGlobals(slotLibRefNum);
	
	if (!(g->sdi.flags & SD_FLAG_HAVE_CARD))
		return expErrCardNotPresent;
	
	if (isSonyEarlyVFS()) {	//sony style :)
	
		struct CardPartitionLayoutV1 *pl = MemPtrNew(sizeof(struct CardPartitionLayoutV1));
		struct CardMetricsTypeV1 *metricsV1 = (struct CardMetricsTypeV1*)metricsV2;
		if (!pl)
			return memErrNotEnoughSpace;
		
		MemPtrSetOwner(pl, 0);

		MemSet(metricsV1, sizeof(*metricsV1), 0);
		metricsV1->version = 1;
		metricsV1->totalSectors = g->sdi.numBlocks;
		metricsV1->bytesPerSector = slotSectorSize;
		metricsV1->sectorsPerBlock = 1;				//cannot be zero or VFS gets sad in PrvGetBestChunkSize()
		metricsV1->pl = pl;
		
		pl->version = 1;
		pl->partStartMinus1 = 0;	//start right away (no MBR-less option here)
	}
	else {

		MemSet(metricsV2, sizeof(*metricsV2), 0);
		metricsV2->totalSectors = g->sdi.numBlocks;
		metricsV2->bytesPerSector = slotSectorSize;
		metricsV2->partitionSize = g->sdi.numBlocks - 1;	//tell it to not use an MBR
		metricsV2->sectorsPerBlock = 1;				//cannot be zero or VFS gets sad in PrvGetBestChunkSize()
	}
		
	return errNone;
}

static void slotPrvResetState(struct Globals *g)
{
	UInt8 cmd = CMD_RESET_STATE;
	
	(void)slotPrvPktExchange(g, &cmd, 1, NULL, 0);
}

Err SlotCardSectorRead(UInt16 slotLibRefNum, UInt16 slotRefNum, UInt32 sectorNumber, UInt8 *bufferP, UInt32 *numSectorsP)
{
	struct SdReadWriteReq rwr = {.cmd = CMD_READ, .sector = sectorNumber, };
	struct Globals *g = slotPrvGetGlobals(slotLibRefNum);
	struct SdErrorData errResp;
	Err e = errNone;
	UInt32 i, i0 = 0;
#ifdef SASD_TOL
	Boolean piped = false;
#endif
	
	if (!(g->sdi.flags & SD_FLAG_HAVE_CARD)) {
		SysFatalAlert("err1\n");
		return expErrCardNotPresent;
	}
	
	if (!numSectorsP || !bufferP){
		SysFatalAlert("err2\n");
		return sysErrParamErr;
	}
	
	if (sectorNumber >= g->sdi.numBlocks){
		SysFatalAlert("err3\n");
		return expErrCardBadSector;
	}
	
	rwr.numSec = *numSectorsP;
	
#ifdef SASD_TOL
	dgCrumb(g, 0x50, 0, (UInt16)sectorNumber, (UInt16)*numSectorsP);
	dgDump(g, false);
	if (slotPrvUsePair(g) && *numSectorsP) {		/* slow link: the request and the first sector's CTS go out back to back */
		UInt8 cts0 = NOTICE_CTS, rplType0 = 0;
		struct bv replyData0 = BV_INIT(bufferP, slotSectorSize, NULL);
		struct bv replyType0 = BV_INIT(&rplType0, 1, &replyData0);
		Boolean okA = false;
		
		piped = slotPrvPktExchangePair(g, &rwr, sizeof(rwr), &errResp, sizeof(errResp), &okA, &cts0, 1, &replyType0) && rplType0 == NOTICE_XFER && (!okA || errResp.cmd == RSP_OK);
	}
	if (!piped) {
#endif
	if (!slotPrvPktExchange(g, &rwr, sizeof(rwr), &errResp, sizeof(errResp))) {
#ifdef SASD_TOL
		/* the stock driver stops the whole device with a fatal alert here; a failed request is reported as an error and the caller can retry */
		slotPrvResetState(g);
		dgDump(g, true);
#else
		SysFatalAlert("err4a");
#endif
		return sysErrParamErr;
	}
	
	if (errResp.cmd != RSP_OK){
#ifndef SASD_TOL
		SysFatalAlert( "err4b");
#endif
		return sysErrParamErr;
	}
#ifdef SASD_TOL
	}
	else {
		i0 = 1;
		bufferP += slotSectorSize;
	}
#endif
	
	for (i = i0; i < *numSectorsP; i++, bufferP += slotSectorSize) {
		
		UInt8 cts = NOTICE_CTS, rplType;
		struct bv replyData = BV_INIT(bufferP, slotSectorSize, NULL);
		struct bv replyType = BV_INIT(&rplType, 1, &replyData);
		
		if (!slotPrvPktExchangeEx(g, &cts, 1, &replyType) || rplType != NOTICE_XFER)
			break;
	}

	if (i != *numSectorsP) {
		slotPrvResetState(g);
#ifdef SASD_TOL
		dgDump(g, true);
#endif
	}
	
	*numSectorsP = i;
	if(e)
		SysFatalAlert("err5\n");
	return e;
}

Err SlotCardSectorWrite(UInt16 slotLibRefNum, UInt16 slotRefNum, UInt32 sectorNumber, UInt8 *bufferP, UInt32 *numSectorsP)
{
	struct SdReadWriteReq rwr = {.cmd = CMD_WRITE, .sector = sectorNumber, };
	struct Globals *g = slotPrvGetGlobals(slotLibRefNum);
	struct SdErrorData errResp;
	Err e = errNone;
	UInt32 i, i0 = 0;
#ifdef SASD_TOL
	Boolean piped = false;
#endif
	
	if (!(g->sdi.flags & SD_FLAG_HAVE_CARD))
		return expErrCardNotPresent;
	
	if (g->sdi.flags & SD_FLAG_READ_ONLY)
		return expErrCardReadOnly;
	
	if (!numSectorsP)
		return sysErrParamErr;
	
	if (sectorNumber >= g->sdi.numBlocks)
		return expErrCardBadSector;
	
	rwr.numSec = *numSectorsP;
	
#ifdef SASD_TOL
	dgCrumb(g, 0x52, 0, (UInt16)sectorNumber, (UInt16)*numSectorsP);
	dgDump(g, false);
	if (slotPrvUsePair(g) && *numSectorsP) {		/* slow link: the request and the first sector go out back to back */
		struct SdErrorData errB;
		struct bv bvB = BV_INIT(&errB, sizeof(errB), NULL);
		Boolean okA = false;
		
		g->secBuf[0] = NOTICE_XFER;
		if (bufferP)
			MemMove(&g->secBuf[1], bufferP, slotSectorSize);
		else
			MemSet(&g->secBuf[1], slotSectorSize, 0);
		errB.cmd = 0;
		piped = slotPrvPktExchangePair(g, &rwr, sizeof(rwr), &errResp, sizeof(errResp), &okA, g->secBuf, slotSectorSize + 1, &bvB) && errB.cmd == RSP_OK && (!okA || errResp.cmd == RSP_OK);
	}
	if (!piped)
#endif
	if (!slotPrvPktExchange(g, &rwr, sizeof(rwr), &errResp, sizeof(errResp)) || errResp.cmd != RSP_OK)
		return sysErrParamErr;
	
	g->secBuf[0] = NOTICE_XFER;
	if (!bufferP)
		MemSet(&g->secBuf[1], slotSectorSize, 0);
#ifdef SASD_TOL
	if (piped) {
		i0 = 1;
		if (bufferP)
			bufferP += slotSectorSize;
	}
#endif
	
	for (i = i0; i < *numSectorsP; i++) {
	
		if (bufferP) {
			MemMove(&g->secBuf[1], bufferP, slotSectorSize);
			bufferP += slotSectorSize;
		}
		
		if (!slotPrvPktExchange(g, g->secBuf, slotSectorSize + 1, &errResp, sizeof(errResp)) || errResp.cmd != RSP_OK)
			break;
	}
	
	
	if (i != *numSectorsP) {
		slotPrvResetState(g);
#ifdef SASD_TOL
		dgDump(g, true);
#endif
	}

	*numSectorsP = i;
	
	return e;
}

Err SlotCardPresent(UInt16 slotLibRefNum, UInt16 slotRefNum)
{
	struct Globals *g = slotPrvGetGlobals(slotLibRefNum);
	
	return (g->sdi.flags & SD_FLAG_HAVE_CARD) ? errNone : expErrCardNotPresent;
}



#ifdef __GNUC__

	/* top-level asm: a function body would get a frame (linkw) from this old gcc and shift every offset in the table */
	extern void SerialSDdispatchTable(void);
	asm(
		".text\n"
		".even\n"
		".globl SerialSDdispatchTable\n"
		"SerialSDdispatchTable:\n"
		"dc.w\t116\n"
		"dc.w\t40\n"
		"dc.w\t44\n"
		"dc.w\t48\n"
		"dc.w\t52\n"
		"dc.w\t56\n"
		"dc.w\t60\n"
		"dc.w\t64\n"
		"dc.w\t68\n"
		"dc.w\t72\n"
		"dc.w\t76\n"
		"dc.w\t80\n"
		"dc.w\t84\n"
		"dc.w\t88\n"
		"dc.w\t92\n"
		"dc.w\t96\n"
		"dc.w\t100\n"
		"dc.w\t104\n"
		"dc.w\t108\n"
		"dc.w\t112\n"
		"jmp\tSlotOpen(%pc)\n"
		"jmp\tSlotClose(%pc)\n"
		"jmp\tSlotSleep(%pc)\n"
		"jmp\tSlotWake(%pc)\n"
		"jmp\tSlotLibAPIVersion(%pc)\n"
		"jmp\tSlotCustomControl(%pc)\n"
		"jmp\tSlotCardPresent(%pc)\n"
		"jmp\tSlotCardInfo(%pc)\n"
		"jmp\tSlotMediaType(%pc)\n"
		"jmp\tSlotCardIsFilesystemSupported(%pc)\n"
		"jmp\tSlotCardMetrics(%pc)\n"
		"jmp\tSlotReturnNoError(%pc)\n"
		"jmp\tSlotCardSectorRead(%pc)\n"
		"jmp\tSlotCardSectorWrite(%pc)\n"
		"jmp\tSlotReturnNoError(%pc)\n"
		"jmp\tSlotMediaType(%pc)\n"
		"jmp\tSlotReturnUnsupportedOp(%pc)\n"
		"jmp\tSlotReturnUnsupportedOp(%pc)\n"
		"jmp\tSlotReturnUnsupportedOp(%pc)\n"
		".ascii\t\"SerialSD\\0\"\n"
		".even\n"
	);

#else
	
	#define numberOfAPIs        		19
	#define libDispatchEntry(index)		(2 * (1 + numberOfAPIs) + index * 4)

	static void asm SerialSDdispatchTable(void)
	{
		/* Offset to library name */
		DC.W		@Name
		
	    /* entry point offsets */
		DC.W		libDispatchEntry(0)
		DC.W		libDispatchEntry(1)
		DC.W		libDispatchEntry(2)
		DC.W		libDispatchEntry(3)
		DC.W		libDispatchEntry(4)
		DC.W		libDispatchEntry(5)
		DC.W		libDispatchEntry(6)
		DC.W		libDispatchEntry(7)
		DC.W		libDispatchEntry(8)
		DC.W		libDispatchEntry(9)
		DC.W		libDispatchEntry(10)
		DC.W		libDispatchEntry(11)
		DC.W		libDispatchEntry(12)
		DC.W		libDispatchEntry(13)
		DC.W		libDispatchEntry(14)
		DC.W		libDispatchEntry(15)
		DC.W		libDispatchEntry(16)
		DC.W		libDispatchEntry(17)
		DC.W		libDispatchEntry(18)
	
	
	    /* entry jumps */
		JMP         SlotOpen
		JMP         SlotClose
		JMP         SlotSleep
		JMP         SlotWake
		JMP         SlotLibAPIVersion
		JMP         SlotCustomControl
		JMP         SlotCardPresent
		JMP         SlotCardInfo
		JMP         SlotMediaType			//was: SlotCardMediaType
		JMP         SlotCardIsFilesystemSupported
		JMP         SlotCardMetrics
		JMP         SlotReturnNoError		//was: SlotCardLowLevelFormat
		JMP         SlotCardSectorRead
		JMP         SlotCardSectorWrite
		JMP         SlotReturnNoError		//was: SlotPowerCheck
		JMP         SlotMediaType
		JMP         SlotReturnUnsupportedOp	//was: SlotCardReserve
		JMP         SlotReturnUnsupportedOp	//was: SlotCardRelease
		JMP         SlotReturnUnsupportedOp	//was: SlotCardGetSerialPort
	
	@Name:
		DC.B		"SerialSD"
	}
#endif


Err __Startup__(UInt16 refNum, SysLibTblEntryType *entryP)
{
	entryP->dispatchTblP = (void**)&SerialSDdispatchTable;
	entryP->globalsP = NULL;
	return 0;
}



UInt32 slkExtGetCurTime(void)
{
	return TimGetTicks();
}

uint16_t slkExtRx(void *slkState, void *dstP, uint16_t nBytes, uint32_t endTime)
{
	struct Globals *g = (struct Globals*)slkState;
	
	return g->serRx(g, dstP, nBytes, endTime);
}

void slkExtTx(void *slkState, const void *srcP, uint16_t len)
{
	struct Globals *g = (struct Globals*)slkState;
	
	g->serTx(g, srcP, len);
}


#ifdef __GNUC__

	void* memset(void *dst, int val, size_t len) {
		MemSet(dst, len, val);
		return dst;
	}

	void* memcpy(void *dst, const void *src, size_t len) {
		MemMove(dst, src, len);
		return dst;
	}



#endif




