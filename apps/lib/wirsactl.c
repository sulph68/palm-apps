/*
 * wirsactl.c - see wirsactl.h. Built on relayproto.c (framing, with its
 * gRpLeadByte/gRpAllowPlain switches on), relaywire.c and relaysec.c.
 */
#include "wirsactl.h"
#include "relayproto.h"
#include "relaywire.h"
#include "relaysec.h"

UInt32 gWCtlBaud = rpBaudIdle;
void (*gWCtlProgress)(UInt32 baud) = 0;
UInt32 gWCtlMenuBaud = 0;
WCtlInfo gWCtlInfo;
UInt8 gWCtlErrCode = 0;
char gWCtlErrMsg[48];

/* An app that can show a Cancel button defines WCTL_CANCELLED() in its relay_cfg.h. */
#ifndef WCTL_CANCELLED
#define WCTL_CANCELLED()	(false)
#endif

static UInt8 PingOnce(void) WCTL_SECTION;
static UInt32 RateAt(UInt16 i) WCTL_SECTION;

WCTL_SECTION UInt8 WCtlRequest(UInt8 type, const UInt8 *body, UInt16 bodyLen,
	UInt8 *reply, UInt16 replyCap, UInt16 *replyLen, UInt32 timeoutTicks)
{
	UInt32 end = TimGetTicks() + timeoutTicks;
	UInt8 t, result = wctlNoReply;
	UInt16 n;

	RpFlushInput();
	if (!RpSendFrame(type, body, bodyLen))
		return wctlNoReply;
	for (;;) {
		Int32 left = (Int32) (end - TimGetTicks());
		if (left <= 0 || WCTL_CANCELLED())
			break;
		gRpIdleOverrideTicks = (UInt32) left;
		if (!RpRecvFrame(&t, reply, replyCap, &n))
			continue;
		if (t == wctlReplyOf(type)) {
			*replyLen = n;
			result = wctlOk;
			break;
		}
		if (t == wctlTypeErr) {
			RpR r;
			r.buf = reply; r.len = n; r.pos = 0; r.ok = true;
			gWCtlErrCode = RpR8(&r);
			RpRStr(&r, gWCtlErrMsg, sizeof(gWCtlErrMsg));
			if (! r.ok) gWCtlErrMsg[0] = '\0';
			result = wctlErrReply;
			break;
		}
		/* anything else is a stale reply - keep waiting */
	}
	gRpIdleOverrideTicks = 0;
	return result;
}

/* One PING at the port's current rate; fills gWCtlInfo on success. */
WCTL_SECTION static UInt8 PingOnce(void)
{
	UInt8 req[4], rep[64];
	UInt16 n;
	UInt32 nonce = ((UInt32) SysRandom(0) << 16) ^ (UInt32) SysRandom(0) ^ TimGetTicks();
	UInt16 tps = SysTicksPerSecond();
	UInt32 ticks;
	UInt8 r;
	RpW w;
	RpR rd;
	WCtlInfo info;

	if (gWCtlProgress)
		gWCtlProgress(gWCtlBaud);

	/* Half a second plus the time a ~64-byte PONG takes at the rate.
	 * No division or 32-bit multiply: libgcc isn't reachable when this
	 * file runs in a secondary segment (Bitwarden). */
	ticks = tps >> 1;
	if (gWCtlBaud < 9600)
		ticks += tps;
	else
		ticks += tps >> 2;

	w.buf = req; w.cap = sizeof(req); w.len = 0;
	RpW32(&w, nonce);
	r = WCtlRequest(wctlTypePing, req, w.len, rep, sizeof(rep), &n, ticks);
	if (r != wctlOk)
		return r;

	rd.buf = rep; rd.len = n; rd.pos = 0; rd.ok = true;
	if (RpR32(&rd) != nonce)
		return wctlBadReply;
	info.proto = RpR8(&rd);
	RpRStr(&rd, info.fw, sizeof(info.fw));
	info.flags = RpR16(&rd);
	info.mode = RpR8(&rd);
	info.menuBaud = RpR32(&rd);
	if (! rd.ok)
		return wctlBadReply;
	gWCtlInfo = info;
	gWCtlMenuBaud = info.menuBaud;
	return wctlOk;
}

WCTL_SECTION void WCtlSetBaud(UInt32 baud)
{
	RpSetBaud(baud);
	gWCtlBaud = baud;
}

/* A switch, not a table: a constant table read from a secondary segment
 * comes back as garbage on this toolchain. */
WCTL_SECTION static UInt32 RateAt(UInt16 i)
{
	switch (i) {
	case 0: return 9600;
	case 1: return 115200;
	case 2: return 57600;
	case 3: return 38400;
	case 4: return 28800;
	case 5: return 19200;
	default: return 14400;
	}
}

WCTL_SECTION UInt8 WCtlFind(void)
{
	UInt32 tried = gWCtlBaud;
	UInt16 i;

	for (i = 0; i < 7; i++) {
		if (RateAt(i) == tried)
			continue;
		if (WCTL_CANCELLED())
			break;
		WCtlSetBaud(RateAt(i));
		if (PingOnce() == wctlOk)
			return wctlOk;
	}
	WCtlSetBaud(tried);
	return wctlNoReply;
}

WCTL_SECTION UInt8 WCtlBegin(Boolean sweep)
{
	UInt32 first = gWCtlBaud;
	UInt8 r;

	if (! RpOpen())
		return wctlPortBusy;
	gRpLeadByte = wctlLeadByte;
	gRpAllowPlain = true;
	if (gWCtlBaud != rpBaudIdle)
		RpSetBaud(gWCtlBaud);
	r = PingOnce();
	/* Only the rates WiRSa can be listening at: the modes' 9600 and its
	 * menu rate. No full sweep here - a PING at the wrong rate reaches a
	 * running mode as junk (WCtlFind does the sweep, on request). */
	if (r != wctlOk && first != rpBaudIdle) {
		WCtlSetBaud(rpBaudIdle);
		r = PingOnce();
	}
	if (r != wctlOk && gWCtlMenuBaud && gWCtlMenuBaud != first && gWCtlMenuBaud != rpBaudIdle) {
		WCtlSetBaud(gWCtlMenuBaud);
		r = PingOnce();
	}
	if (r != wctlOk && sweep)
		r = WCtlFind();
	if (r != wctlOk) {
		gWCtlBaud = first;
		WCtlEnd();
	}
	return r;
}

WCTL_SECTION UInt8 WCtlBeginFind(void)
{
	UInt8 r;

	if (! RpOpen())
		return wctlPortBusy;
	gRpLeadByte = wctlLeadByte;
	gRpAllowPlain = true;
	gWCtlBaud = rpBaudIdle;
	r = PingOnce();
	if (r != wctlOk)
		r = WCtlFind();
	if (r != wctlOk)
		WCtlEnd();
	return r;
}

WCTL_SECTION void WCtlEnd(void)
{
	RpClose();
	gRpLeadByte = 0;
	gRpAllowPlain = false;
}

WCTL_SECTION UInt8 WCtlSecure(void)
{
	return (RpSecure() == rpSecOk) ? wctlOk : wctlSecFailed;
}

WCTL_SECTION void WCtlUnsecure(void)
{
	RpSecWipe();
}

WCTL_SECTION UInt8 WCtlSetSpeed(UInt32 want, UInt32 *got)
{
	UInt8 req[4], rep[8];
	UInt16 n;
	UInt32 rate;
	UInt8 r;
	RpW w;
	RpR rd;

	w.buf = req; w.cap = sizeof(req); w.len = 0;
	RpW32(&w, want);
	r = WCtlRequest(wctlTypeSpeed, req, w.len, rep, sizeof(rep), &n, SysTicksPerSecond() * 2);
	if (r != wctlOk)
		return r;
	rd.buf = rep; rd.len = n; rd.pos = 0; rd.ok = true;
	(void) RpR8(&rd);		/* ok */
	rate = RpR32(&rd);
	if (! rd.ok || rate == 0)
		return wctlBadReply;

	/* WiRSa moves once its reply is out: give it a moment, then follow. */
	SysTaskDelay(SysTicksPerSecond() >> 3);
	WCtlSetBaud(rate);
	if (PingOnce() == wctlOk) {
		*got = rate;
		return wctlOk;
	}
	WCtlSetBaud(rpBaudIdle);
	(void) PingOnce();
	return wctlNoReply;
}

/* ---- relay apps' Screenless Mode ---------------------------------------- */

WCTL_SECTION UInt8 WCtlEnsureMode(UInt8 mode, UInt32 menuBaud)
{
	UInt8 rep[8], body[2];
	UInt16 n, i;
	UInt8 r;

	/* WiRSa is either in a mode (9600) or its menu. menuBaud (the app's Baud
	 * setting) is only a guess at the menu rate - it is the fastest rate the
	 * app offers for a sync, not necessarily WiRSa's menu rate - so if
	 * neither answers, try every rate. */
	gWCtlMenuBaud = menuBaud;
	gWCtlBaud = rpBaudIdle;
	r = WCtlBegin(true);
	if (r != wctlOk)
		return r;
	body[0] = mode;
	body[1] = 1;		/* flag: WiRSa goes back to its menu when the session ends */
	if (gWCtlInfo.mode == mode) {
		/* Already running: just pass the flag (older firmware refuses -
		 * WCtlLeaveMode afterwards covers that). */
		(void) WCtlRequest(wctlTypeEnter, body, 2, rep, sizeof(rep), &n, SysTicksPerSecond() * 3);
		WCtlEnd();
		return wctlOk;
	}
	if (gWCtlInfo.mode != 'M') {
		r = WCtlRequest(wctlTypeExit, (const UInt8 *) "EXIT", 4, rep, sizeof(rep), &n,
			SysTicksPerSecond() * 3);
		if (r != wctlOk) {
			WCtlEnd();
			/* WiRSa answered the PING, so don't let the caller sync blind */
			return (r == wctlErrReply) ? r : wctlModeNotUp;
		}
		/* WiRSa restarts its UART at the menu rate */
		SysTaskDelay(SysTicksPerSecond() >> 1);
		WCtlSetBaud(gWCtlInfo.menuBaud ? gWCtlInfo.menuBaud : menuBaud);
	}
	r = WCtlRequest(wctlTypeEnter, body, 2, rep, sizeof(rep), &n, SysTicksPerSecond() * 5);
	WCtlEnd();
	if (r == wctlErrReply)
		return r;
	if (gWCtlInfo.menuBaud)
		menuBaud = gWCtlInfo.menuBaud;		/* the real menu rate, for later */

	/* The mode clears the line and may generate its key as it starts:
	 * ask at 9600 once a second until the mode itself answers. */
	r = wctlNoReply;
	for (i = 0; i < 20; i++) {
		if (WCTL_CANCELLED())
			break;
		SysTaskDelay(SysTicksPerSecond());
		gWCtlBaud = rpBaudIdle;
		gWCtlMenuBaud = 0;		/* 9600 only */
		r = WCtlBegin(false);
		if (r == wctlPortBusy)
			break;
		if (r == wctlOk) {
			Boolean there = (Boolean) (gWCtlInfo.mode == mode);
			WCtlEnd();
			if (there)
				break;
			r = wctlNoReply;
		}
	}
	gWCtlMenuBaud = menuBaud;
	gWCtlBaud = rpBaudIdle;
	/* WiRSa answered above, so a mode that never answers is a failure -
	 * syncing anyway would just hang waiting for it. */
	if (r != wctlOk && r != wctlPortBusy)
		r = wctlModeNotUp;
	return r;
}

WCTL_SECTION UInt8 WCtlLeaveMode(UInt32 menuBaud)
{
	UInt8 rep[8];
	UInt16 n, i;
	UInt8 r = wctlNoReply;

	/* Right after its BYE the mode is still busy for a moment (CalDAV shows
	 * "Completed!" for 1.5 s, restarting the UART at 9600 can drop what
	 * already arrived, and junk can hold its frame reader for its 3 s byte
	 * timeout), so one PING isn't enough: keep asking for ~12 s, at 9600
	 * (the mode) and at the menu rate (in case it already left). WiRSa
	 * reports its real menu rate in every PONG. */
	if (gWCtlInfo.menuBaud)
		menuBaud = gWCtlInfo.menuBaud;
	for (i = 0; i < 10; i++) {
		if (i)
			SysTaskDelay(SysTicksPerSecond());
		gWCtlMenuBaud = menuBaud;
		gWCtlBaud = rpBaudIdle;
		r = WCtlBegin(false);
		if (r != wctlOk)
			continue;		/* incl. a briefly busy port */
		if (gWCtlInfo.mode == 'M') {			/* already back in its menu */
			WCtlEnd();
			break;
		}
		r = WCtlRequest(wctlTypeExit, (const UInt8 *) "EXIT", 4, rep, sizeof(rep), &n,
			SysTicksPerSecond() * 3);
		WCtlEnd();
		if (r == wctlOk)
			break;
		/* wctlErrReply: "a session is still running" - try again */
	}
	gWCtlMenuBaud = menuBaud;
	gWCtlBaud = rpBaudIdle;
	return r;
}
