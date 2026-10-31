/*
 * relayproto.c - see relayproto.h. Merged from BitWarden's and CalDAV
 * Sync's former private Serial.c copies (identical logic apart from the
 * app hooks now supplied by relay_cfg.h).
 */
#include "relayproto.h"
#include "relaysec.h"
#include "relay_cfg.h"

#define rpByteTimeoutTicks	(SysTicksPerSecond() * 3)
/* WiRSa's first reply can take 60-100s (BitWarden's PBKDF2 + login). */
#define rpIdleTimeoutTicks	(SysTicksPerSecond() * 180)
/* One blocking SrmReceive for the whole timeout would let auto-off fire and
 * miss Cancel taps - poll in 1s slices, calling RP_APP_POLL() each time. */
#define rpPollTicks			(SysTicksPerSecond())

static UInt16 gPortId = 0;
static Boolean gOpen = false;
Err gRpLastRecvErr = 0;
UInt16 gRpLastLineErrs = 0;
UInt32 gRpIdleOverrideTicks = 0;
UInt8 gRpLeadByte = 0;
Boolean gRpAllowPlain = false;
Err gRpOpenErr = 0;

/* Explicit receive buffer - the Serial Manager default overruns on a burst. */
static UInt8 gSerialRecvBuf[RP_RECV_BUF_SIZE];

Boolean RpOpen(void)
{
	if (gOpen) return true;
	gRpOpenErr = SrmOpen(serPortCradlePort, rpBaudIdle, &gPortId);
	if (gRpOpenErr != errNone) return false;
	SrmSetReceiveBuffer(gPortId, gSerialRecvBuf, sizeof(gSerialRecvBuf));
	SrmReceiveFlush(gPortId, 0);
	gOpen = true;
	gRpLastRecvErr = 0;
	gRpIdleOverrideTicks = 0;
	RpSecWipe();
	RP_APP_ON_OPEN();
	return true;
}

void RpSetBaud(UInt32 baud)
{
	if (!gOpen) return;
	SrmClose(gPortId);
	gOpen = false;
	if (SrmOpen(serPortCradlePort, baud, &gPortId) != errNone) return;
	SrmSetReceiveBuffer(gPortId, gSerialRecvBuf, sizeof(gSerialRecvBuf));
	SrmReceiveFlush(gPortId, 0);
	gOpen = true;
}

void RpClose(void)
{
	if (!gOpen) return;
	SrmClose(gPortId);
	gOpen = false;
	RpSecWipe();
}

#ifdef RP_WITH_PENDING
/* Bytes already waiting in the serial buffer (a look at the local buffer, nothing is sent). */
RP_SECTION UInt32 RpPending(void)
{
	UInt32 n = 0;

	if (!gOpen) return 0;
	if (SrmReceiveCheck(gPortId, &n) != errNone) return 0;
	return n;
}
#endif

RP_SECTION void RpFlushInput(void)
{
	if (gOpen) SrmReceiveFlush(gPortId, 0);
}

/* Blocking read of exactly len bytes. timeoutTicks bounds the SILENCE gap,
 * not the total: the deadline extends on every chunk of progress. A cancel
 * tap or a genuine line error ends the read early. A UART line fault
 * latches until SrmClearErr, and SrmGetStatus must be read before clearing. */
static Boolean ReadExact(UInt8 *buf, UInt32 len, Int32 timeoutTicks)
{
	UInt32 got = 0;
	UInt32 deadline = TimGetTicks() + timeoutTicks;
	while (got < len) {
		Err err;
		Int32 remaining = (Int32) (deadline - TimGetTicks());
		Int32 pollTimeout;
		UInt32 n;
		if (remaining <= 0) return false;
		pollTimeout = (remaining < rpPollTicks) ? remaining : rpPollTicks;
		n = SrmReceive(gPortId, buf + got, len - got, pollTimeout, &err);
		got += n;
		RP_APP_POLL();
		/* Checked after EVERY pass, not only while the read is incomplete: with
		 * data always waiting (WiRSa streams from a queue) a read finishes in
		 * one pass, and a Cancel tap used to go unseen until the sync ended. */
		if (RP_APP_CANCELLED()) return false;
		if (n > 0) deadline = TimGetTicks() + timeoutTicks;
		if (got < len && err != errNone && err != serErrTimeOut) {
			gRpLastRecvErr = err;
			SrmGetStatus(gPortId, NULL, &gRpLastLineErrs);
			SrmClearErr(gPortId);
			return false;
		}
	}
	return true;
}

/* Strict: until RpSecure succeeds only key-exchange frames (plaintext by
 * definition) may be sent; every other frame goes out encrypted - see
 * relaysec.h for the wire format. */
#ifdef RP_SEND_BODY_MAX
/* Whole frame copied into one buffer and sent with a single SrmSend. Apps
 * that can afford the buffer (CalDAV Sync) keep this, their original,
 * proven send - in POSE a separately-sent trailing checksum byte was seen
 * arriving seconds late. */
static UInt8 gSendBuf[3 + 4 + RP_SEND_BODY_MAX + 1];

RP_SECTION Boolean RpSendFrame(UInt8 type, const UInt8 *body, UInt16 bodyLen)
{
	UInt8 chk;
	UInt16 i, p, wireLen;
	Err err;
	Boolean enc = !RP_IS_KEX(type) && !(gRpAllowPlain && !gRpSecured);
	UInt32 seq = 0;

	if (!gOpen) return false;
	if (enc && !gRpSecured) return false;
	if (bodyLen > RP_SEND_BODY_MAX) return false;
	if (gRpLeadByte) {
		SrmSend(gPortId, &gRpLeadByte, 1, &err);
		if (err != errNone) return false;
	}

	wireLen = enc ? (UInt16) (bodyLen + 4) : bodyLen;
	gSendBuf[0] = enc ? (UInt8) (type | RP_ENC_FLAG) : type;
	gSendBuf[1] = (UInt8) (wireLen >> 8);
	gSendBuf[2] = (UInt8) (wireLen & 0xFF);
	p = 3;
	if (enc) {
		seq = gRpTxSeq++;
		gSendBuf[3] = (UInt8) (seq >> 24);
		gSendBuf[4] = (UInt8) (seq >> 16);
		gSendBuf[5] = (UInt8) (seq >> 8);
		gSendBuf[6] = (UInt8) seq;
		p = 7;
	}
	if (bodyLen) MemMove(gSendBuf + p, body, bodyLen);
	if (enc && bodyLen) RpSecXcrypt(gSendBuf + p, bodyLen, rpDirPalm, seq, 0);
	chk = 0;
	for (i = 0; i < (UInt16) (p + bodyLen); i++) chk ^= gSendBuf[i];
	gSendBuf[p + bodyLen] = chk;

	SrmSend(gPortId, gSendBuf, (UInt32) (p + bodyLen + 1), &err);
	if (err != errNone) return false;
	SrmSendWait(gPortId);
	return true;
}
#else
/* Header, body and checksum as separate SrmSends - no frame-sized buffer
 * (BitWarden has no globals headroom for one; proven on its hardware). An
 * encrypted body goes out in 64-byte chunks encrypted on the stack.
 * SrmSendWait before returning, since callers go straight into a receive. */
RP_SECTION Boolean RpSendFrame(UInt8 type, const UInt8 *body, UInt16 bodyLen)
{
	UInt8 hdr[7];
	UInt8 chunk[64];
	UInt8 chk;
	UInt16 i, hdrLen, off, n, wireLen;
	Err err;
	Boolean enc = !RP_IS_KEX(type) && !(gRpAllowPlain && !gRpSecured);
	UInt32 seq = 0;

	if (!gOpen) return false;
	if (enc && !gRpSecured) return false;
	if (gRpLeadByte) {
		SrmSend(gPortId, &gRpLeadByte, 1, &err);
		if (err != errNone) return false;
	}

	wireLen = enc ? (UInt16) (bodyLen + 4) : bodyLen;
	hdr[0] = enc ? (UInt8) (type | RP_ENC_FLAG) : type;
	hdr[1] = (UInt8) (wireLen >> 8);
	hdr[2] = (UInt8) (wireLen & 0xFF);
	hdrLen = 3;
	if (enc) {
		seq = gRpTxSeq++;
		hdr[3] = (UInt8) (seq >> 24);
		hdr[4] = (UInt8) (seq >> 16);
		hdr[5] = (UInt8) (seq >> 8);
		hdr[6] = (UInt8) seq;
		hdrLen = 7;
	}
	chk = 0;
	for (i = 0; i < hdrLen; i++) chk ^= hdr[i];

	SrmSend(gPortId, hdr, hdrLen, &err);
	if (err != errNone) return false;
	if (enc) {
		for (off = 0; off < bodyLen; off += n) {
			n = (UInt16) (bodyLen - off);
			if (n > sizeof(chunk)) n = sizeof(chunk);
			MemMove(chunk, body + off, n);
			RpSecXcrypt(chunk, n, rpDirPalm, seq, (UInt16) (off >> 4));
			for (i = 0; i < n; i++) chk ^= chunk[i];
			SrmSend(gPortId, chunk, n, &err);
			if (err != errNone) return false;
		}
	} else if (bodyLen) {
		for (i = 0; i < bodyLen; i++) chk ^= body[i];
		SrmSend(gPortId, body, bodyLen, &err);
		if (err != errNone) return false;
	}
	SrmSend(gPortId, &chk, 1, &err);
	if (err != errNone) return false;
	SrmSendWait(gPortId);
	return true;
}
#endif

/* Strict: a plaintext frame is accepted only if it is a key-exchange
 * frame; an encrypted one only once the link is secured. Anything else is
 * dropped (returns false, like a corrupt frame). */
Boolean RpRecvFrame(UInt8 *typeOut, UInt8 *body, UInt16 bodyCap, UInt16 *bodyLenOut)
{
	UInt8 hdr[3];
	UInt8 seqb[4];
	UInt16 len;
	UInt8 chk, rxChk;
	UInt16 i;
	Boolean enc;

	if (!gOpen) return false;
	if (gRpLeadByte) {
		/* Skip menu text and anything else up to the lead byte. */
		do {
			if (!ReadExact(hdr, 1, gRpIdleOverrideTicks ? (Int32) gRpIdleOverrideTicks : rpIdleTimeoutTicks)) return false;
		} while (hdr[0] != gRpLeadByte);
		if (!ReadExact(hdr, 1, rpByteTimeoutTicks)) return false;
	} else if (!ReadExact(hdr, 1, gRpIdleOverrideTicks ? (Int32) gRpIdleOverrideTicks : rpIdleTimeoutTicks)) return false;
	if (!ReadExact(hdr + 1, 2, rpByteTimeoutTicks)) return false;
	len = (UInt16) (((UInt16) hdr[1] << 8) | hdr[2]);
	chk = hdr[0] ^ hdr[1] ^ hdr[2];
	enc = (hdr[0] & RP_ENC_FLAG) != 0;
	if (enc) {
		if (len < 4) return false;
		if (!ReadExact(seqb, 4, rpByteTimeoutTicks)) return false;
		chk ^= seqb[0] ^ seqb[1] ^ seqb[2] ^ seqb[3];
		len -= 4;
	}

	if (len > bodyCap) {
		/* Drain and drop, keeping the stream in sync for the next frame. */
		UInt8 junk[64];
		UInt16 remaining = len;
		while (remaining > 0) {
			UInt16 chunk = (remaining > sizeof(junk)) ? (UInt16) sizeof(junk) : remaining;
			if (!ReadExact(junk, chunk, rpByteTimeoutTicks)) return false;
			remaining -= chunk;
		}
		ReadExact(&rxChk, 1, rpByteTimeoutTicks);
		return false;
	}

	if (len && !ReadExact(body, len, rpByteTimeoutTicks)) return false;
	if (!ReadExact(&rxChk, 1, rpByteTimeoutTicks)) return false;

	for (i = 0; i < len; i++) chk ^= body[i];
	if (chk != rxChk) return false;

	if (enc) {
		if (!gRpSecured) return false;
		if (len) RpSecXcrypt(body, len, rpDirWirsa,
			((UInt32) seqb[0] << 24) | ((UInt32) seqb[1] << 16) | ((UInt32) seqb[2] << 8) | seqb[3], 0);
	} else if (!RP_IS_KEX(hdr[0]) && !gRpAllowPlain) {
		return false;
	}

	*typeOut = (UInt8) (hdr[0] & (UInt8) ~RP_ENC_FLAG);
	*bodyLenOut = len;
	return true;
}
