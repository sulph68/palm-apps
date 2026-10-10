/*
 * EraNTP.c - single-screen NTP client that sets the device clock and
 * cooperates with the HackMaster Y2K35 era plugin (../lib/hackcheck.h,
 * ../lib/y2k35.h, ../lib/datelib.h).
 *
 * Behavior (agreed design, see project chat):
 *  - "Sync Now" queries the Server field's NTP host (UDP/123, plain SNTP -
 *    no round-trip-delay correction, just trusts the Transmit Timestamp).
 *  - If the real year is > 2031, the clock is always re-based via datelib's
 *    year-type match (dl_reBaseOffset/dl_dateToSeconds) - same math the
 *    plugin's own control panel uses - REGARDLESS of whether the plugin is
 *    installed, so the device clock is always left in a safe, plausible
 *    state either way.
 *  - If the plugin is active (HcCheck()==HC_OK), the true date is ALSO
 *    published to its Feature/pref slots (FtrSet + PrefSetAppPreferences,
 *    same creator/ids as y2k35.h) so its own control panel echoes the real
 *    date and every era-aware app sees it. If not active, this step is
 *    skipped silently - the rebase-only clock set is still safe on its own.
 *  - "Set device clock" unchecked = dry run: query + compare only, no
 *    TimSetSeconds, no publish.
 *  - No periodic/alarm sync - explicit "Sync Now" only. No HotSync-backed
 *    prefs - the Server field/checkbox live in the OS's UNSAVED prefs DB
 *    (see A35Defaults precedent in address35/Address35.c) since there's
 *    nothing here worth backing up.
 *
 * Single segment, single form, no menu (the Server field is short enough
 * that Graffiti/typing alone is fine), no locale variants - edit EraNTP.rcp
 * directly to tweak the layout, matching Sec35's build shape.
 */
#include <PalmOS.h>
#include <NetMgr.h>
#include "datelib.h"
#include "hackcheck.h"
#include "y2k35.h"

/* ---- resource ids (Sec35's convention: no separate resource header) ---- */
#define frmMain			1000
#define fldServer		1002
#define chkSetClock		1003
#define lblDevice		1004
#define lblEra			1005
#define lblNtp			1006
#define fldLog			1007
#define sclLog			1008
#define btnDone			1009
#define btnSyncNow		1010
#define mniConnect		1601
#define mniDisconnect	1602

#define appCreator		'Entp'
#define unsavedPrefID	0
#define defaultsVer		1
#define ntpPort			123
#define ntpPacketLen	48
#define ntpTimeoutTicks	(10 * SysTicksPerSecond())	/* per-attempt reply-wait budget, polled (not
													   blocked on) via NetLibSelect - see SyncTick */
#define dnsTimeoutTicks	(5 * SysTicksPerSecond())	/* NetLibGetHostByName has no non-blocking form,
													   so this is a real (bounded) UI stall per
													   attempt - kept short so a Stop tap queued
													   during it is picked up promptly once it returns */
#define dnsMaxAttempts	3							/* the first lookup right after NetLibOpen can
													   fail on real hardware while the link is still
													   settling -- retry before giving up */
#define logCapacity		4000						/* matches EraNTP.rcp's fldLog MAXCHARS */
#define tickTimeout		SysTicksPerSecond()			/* ~1s Device/Era refresh, same idiom as Sec35's countdown label */
#define syncPollTicks	(SysTicksPerSecond() / 5)	/* ~200ms event-loop cadence while a sync is in
													   flight, so the clock keeps ticking and a Stop
													   tap is picked up quickly */

/* NTP (1900) -> Palm (1904) epoch delta: 1900/1901/1902/1903 are all
 * non-leap (1900 is div-100-not-400; 1901-03 aren't div-4), so exactly
 * 1460 days = 126144000 seconds separate the two epochs. */
#define ntpToPalmEpochDelta	126144000UL

/* ---- unsaved ("last used") prefs: Server text + checkbox state -------- */
typedef struct {
	UInt16	version;
	Boolean	setClock;
	char	server[64];
} EraNtpDefaults;

/* ---- globals (single-segment app, no hack constraints here) ----------- */
static char gLogBuf[logCapacity + 1];
static UInt16 gLogLen = 0;

/* ---- Sync state machine ------------------------------------------------
 * Palm OS 4.x/68K has no user-level threads, so "run the query in the
 * background" means driving a resumable state machine one small step per
 * event-loop tick instead of one long blocking call - see EventLoop and
 * SyncTick. Every step here is either fast/local (open/bind/send) or
 * explicitly polled non-blocking (NetLibSelect before receive), EXCEPT
 * NetLibGetHostByName, which has no non-blocking form on this API and
 * genuinely blocks the UI for up to dnsTimeoutTicks per attempt - the one
 * real limitation of this design, called out here rather than hidden. */
typedef enum {
	kSyncIdle = 0,
	kSyncOpening,		/* SysLibFind/NetLibOpen/SocketOpen/Bind - fast, local */
	kSyncResolving,		/* one blocking NetLibGetHostByName attempt */
	kSyncSending,		/* build + send one UDP packet - fast, local */
	kSyncWaiting		/* socket sent; polling NetLibSelect for a reply */
} SyncPhase;

static SyncPhase gSyncPhase = kSyncIdle;
static UInt16    gNetRefNum;
static Boolean   gNetOpened;
static NetSocketRef gSock = -1;
static NetSocketAddrINType gRemote;
static UInt8     gPacket[ntpPacketLen];
static UInt32    gWaitStartTick;
static UInt16    gDnsAttempt;
static UInt16    gSendAttempt;
static char      gServer[64];
static Boolean   gSetClockFlag;
static char      gResolvedIp[16];
static char      gErrMsg[64];

/* ---- unsaved prefs -------------------------------------------------- */
static void LoadDefaults(EraNtpDefaults *d)
{
	UInt16 size = sizeof(*d);
	Int16 r = PrefGetAppPreferences(appCreator, unsavedPrefID, d, &size, false);
	if (r == noPreferenceFound || size < sizeof(*d) || d->version != defaultsVer) {
		d->version = defaultsVer;
		d->setClock = true;
		StrCopy(d->server, "pool.ntp.org");
	}
}

static void StoreDefaults(const EraNtpDefaults *d)
{
	PrefSetAppPreferences(appCreator, unsavedPrefID, defaultsVer,
		(void *) d, sizeof(*d), false);		/* saved=false -> unsaved prefs DB */
}

/* ---- small helpers ---------------------------------------------------- */
static void *Obj(UInt16 id)
{
	FormPtr f = FrmGetActiveForm();
	return FrmGetObjectPtr(f, FrmGetObjectIndex(f, id));
}

/* "Aug 19 2026 14:32:07" - fixed width-ish, fits comfortably in a 150px row. */
static void FormatDlDateTime(const DlDateTime *dt, char *out, UInt16 outLen)
{
	static const char *mon[] = { "Jan","Feb","Mar","Apr","May","Jun",
		"Jul","Aug","Sep","Oct","Nov","Dec" };
	const char *m = (dt->month >= 1 && dt->month <= 12) ? mon[dt->month - 1] : "???";
	/* outLen is always >= 48 at every call site (buf[48]) - plenty for the
	 * longest possible value here (a 10-digit year), so plain StrPrintF is
	 * safe; StrNPrintF isn't linkable in this SDK/glue configuration. */
	StrPrintF(out, "%s %d %ld %02d:%02d:%02d",
		m, (int) dt->day, (long) dt->year,
		(int) dt->hour, (int) dt->minute, (int) dt->second);
	(void) outLen;
}

/* ---- Device / Era comparison rows -------------------------------------
 * Both derive from ONE TimGetSeconds() read per refresh, so they can never
 * show a value one tick apart from each other - see the file header. */
static void RefreshComparison(void)
{
	DateTimeType dt;
	DlDateTime   dl;
	char         buf[48], valBuf[48];
	HcState      hc;

	TimSecondsToDateTime(TimGetSeconds(), &dt);
	dl.year    = dt.year;		/* DateTimeType.year is already absolute (unlike
	                             * the packed 7-bit DateType.year) - no +1904 */
	dl.month   = dt.month;
	dl.day     = dt.day;
	dl.hour    = dt.hour;
	dl.minute  = dt.minute;
	dl.second  = dt.second;
	dl.weekday = dt.weekDay;
	FormatDlDateTime(&dl, valBuf, sizeof(valBuf));
	StrPrintF(buf, "Device: %s", valBuf);
	FrmCopyLabel(FrmGetActiveForm(), lblDevice, buf);

	hc = HcCheck();
	if (hc == HC_NOT_INSTALLED) {
		FrmCopyLabel(FrmGetActiveForm(), lblEra, "Era: Not Installed");
	} else if (hc == HC_NOT_ENABLED) {
		FrmCopyLabel(FrmGetActiveForm(), lblEra, "Era: Not Enabled");
	} else {
		char valBuf[48];
		Hc_CurrentRealDate(&dl);
		FormatDlDateTime(&dl, valBuf, sizeof(valBuf));
		StrPrintF(buf, "Era: %s", valBuf);
		FrmCopyLabel(FrmGetActiveForm(), lblEra, buf);
	}
}

/* Empties the log so each Sync Now tap starts fresh - only the current
 * sync's trace should be visible, not a running history across taps. */
static void LogClear(void)
{
	FieldPtr fld = (FieldPtr) Obj(fldLog);
	MemHandle old = FldGetTextHandle(fld);

	gLogLen = 0;
	gLogBuf[0] = '\0';
	FldSetTextHandle(fld, NULL);
	if (old) MemHandleFree(old);
	FldRecalculateField(fld, true);
	SclSetScrollBar((ScrollBarPtr) Obj(sclLog), 0, 0, 0, 0);
}

/* ---- log field: rebuilt wholesale on every append (a handful of KB, only
 * on an explicit Sync Now tap - no incremental-edit complexity needed). --- */
static void LogAppend(const char *text)
{
	FieldPtr fld = (FieldPtr) Obj(fldLog);
	MemHandle h;
	char *p;
	UInt16 addLen = (UInt16) StrLen(text);
	UInt16 scrollPos, textHeight, fieldHeight;
	ScrollBarPtr bar;
	UInt16 maxValue;

	if (gLogLen > 0 && gLogLen + 1 < logCapacity) {
		gLogBuf[gLogLen++] = '\n';
	}
	if (gLogLen + addLen >= logCapacity)			/* cap -- never crash, just stop appending */
		addLen = (logCapacity > gLogLen) ? (logCapacity - gLogLen) : 0;
	MemMove(gLogBuf + gLogLen, text, addLen);
	gLogLen = (UInt16) (gLogLen + addLen);
	gLogBuf[gLogLen] = '\0';

	h = MemHandleNew((UInt32) gLogLen + 1);
	if (!h) return;
	p = (char *) MemHandleLock(h);
	MemMove(p, gLogBuf, gLogLen + 1);
	MemHandleUnlock(h);

	{
		MemHandle old = FldGetTextHandle(fld);
		FldSetTextHandle(fld, h);
		FldRecalculateField(fld, true);
		if (old) MemHandleFree(old);
	}

	/* scroll to the newest line + keep the scrollbar in sync, verbatim the
	 * canonical pattern from the Field Manager docs (FldGetScrollValues). */
	FldSetScrollPosition(fld, gLogLen);
	FldGetScrollValues(fld, &scrollPos, &textHeight, &fieldHeight);
	maxValue = (textHeight > fieldHeight) ? (UInt16) (textHeight - fieldHeight) : 0;
	bar = (ScrollBarPtr) Obj(sclLog);
	SclSetScrollBar(bar, scrollPos, 0, maxValue, fieldHeight > 0 ? fieldHeight - 1 : 0);
}

/* Dotted-quad "a.b.c.d" -> 4 address bytes, no DNS involved. Lets a real
 * device with a working link but a flaky/incomplete resolver still sync by
 * typing a server's IP directly, and doubles as a way to tell "DNS is the
 * problem" apart from "the NTP exchange itself is the problem" when
 * troubleshooting. */
static Boolean ParseIPv4(const char *s, UInt8 out[4])
{
	UInt16 i;
	for (i = 0; i < 4; i++) {
		UInt16 v = 0, digits = 0;
		while (*s >= '0' && *s <= '9') {
			v = (UInt16) (v * 10 + (*s - '0'));
			if (v > 255) return false;
			s++; digits++;
		}
		if (digits == 0) return false;
		out[i] = (UInt8) v;
		if (i < 3) {
			if (*s != '.') return false;
			s++;
		}
	}
	return *s == '\0';
}

/* ---- NTP client, driven as a state machine from EventLoop --------------
 * Plain SNTP: send a minimal client request, trust the server's Transmit
 * Timestamp as "close enough" wall-clock time (no offset/delay math) - this
 * is a testing/exercise tool, not a stratum-accurate NTP client. */

static void SyncFinish(Boolean ok);
static void SyncTick(void);

/* Tap "Sync Now": read the form, reset the log, open the network, and kick
 * off the state machine (running the first step immediately rather than
 * waiting for the next tick, so there's no dead time before "Querying..."
 * actually starts doing anything). */
static void SyncStart(void)
{
	EraNtpDefaults d;
	char line[96];

	if (gSyncPhase != kSyncIdle) return;	/* Stop is what the button says now */

	gSetClockFlag = (Boolean) CtlGetValue((ControlPtr) Obj(chkSetClock));
	{
		FieldPtr f = (FieldPtr) Obj(fldServer);
		char *t = f ? (char *) FldGetTextPtr(f) : NULL;
		StrNCopy(gServer, (t && t[0]) ? t : "pool.ntp.org", sizeof(gServer) - 1);
		gServer[sizeof(gServer) - 1] = '\0';
	}

	LoadDefaults(&d);
	d.setClock = gSetClockFlag;
	StrNCopy(d.server, gServer, sizeof(d.server) - 1);
	d.server[sizeof(d.server) - 1] = '\0';
	StoreDefaults(&d);

	LogClear();
	StrPrintF(line, "Querying %s...", gServer);
	LogAppend(line);

	gNetOpened = false;
	gSock = -1;
	gResolvedIp[0] = '\0';
	gErrMsg[0] = '\0';
	gDnsAttempt = 0;
	gSendAttempt = 0;

	CtlSetLabel((ControlPtr) Obj(btnSyncNow), "Stop");
	gSyncPhase = kSyncOpening;
	SyncTick();
}

/* Tap "Stop" (same button, relabeled) while a sync is in flight. */
static void SyncCancel(void)
{
	Err err;

	if (gSyncPhase == kSyncIdle) return;
	if (gSock != -1) { NetLibSocketClose(gNetRefNum, gSock, 0, &err); gSock = -1; }
	if (gNetOpened) { NetLibClose(gNetRefNum, false); gNetOpened = false; }
	gSyncPhase = kSyncIdle;
	CtlSetLabel((ControlPtr) Obj(btnSyncNow), "Sync Now");
	LogAppend("Terminated");
}

/* Advances the sync exactly one step. Called once immediately from
 * SyncStart, then once per EventLoop tick (every syncPollTicks, ~200ms)
 * while gSyncPhase != kSyncIdle. */
static void SyncTick(void)
{
	Err err = errNone;
	char line[96];

	switch (gSyncPhase) {
	case kSyncIdle:
		break;

	case kSyncOpening:
		{
			UInt16 openErr;
			if (SysLibFind("Net.lib", &gNetRefNum) != errNone) {
				StrNCopy(gErrMsg, "Net.lib not found", sizeof(gErrMsg) - 1);
				SyncFinish(false);
				break;
			}
			err = NetLibOpen(gNetRefNum, &openErr);
			if (openErr != errNone || (err != errNone && err != netErrAlreadyOpen)) {
				StrPrintF(gErrMsg, "Could not open network (err=%ld if=%u)",
					(long) err, (unsigned) openErr);
				SyncFinish(false);
				break;
			}
			gNetOpened = true;

			gSock = NetLibSocketOpen(gNetRefNum, netSocketAddrINET, netSocketTypeDatagram,
				netSocketProtoIPUDP, ntpTimeoutTicks, &err);
			if (gSock == -1) {
				StrPrintF(gErrMsg, "Could not open socket (err=%ld)", (long) err);
				SyncFinish(false);
				break;
			}

			/* Explicitly bind to an OS-chosen local port before sending
			 * anything - see the working-pattern note in project memory
			 * (palmos-netlib-udp-client-working-pattern.md): a hand-opened
			 * UDP socket left unbound can behave unreliably for receiving
			 * replies on some real-hardware network drivers (SLIP among
			 * them), confirmed on a real device. */
			{
				NetSocketAddrINType local;
				MemSet(&local, sizeof(local), 0);
				local.family = netSocketAddrINET;
				local.port = 0;
				if (NetLibSocketBind(gNetRefNum, gSock, (NetSocketAddrType *) &local,
						sizeof(local), ntpTimeoutTicks, &err) == -1) {
					StrPrintF(gErrMsg, "Could not bind socket (err=%ld)", (long) err);
					SyncFinish(false);
					break;
				}
			}

			MemSet(&gRemote, sizeof(gRemote), 0);
			gRemote.family = netSocketAddrINET;
			gRemote.port   = NetHToNS(ntpPort);

			{
				UInt8 ipBytes[4];
				if (ParseIPv4(gServer, ipBytes)) {
					/* server typed as a raw IP - skip DNS entirely */
					MemMove(&gRemote.addr, ipBytes, sizeof(gRemote.addr));
					NetLibAddrINToA(gNetRefNum, gRemote.addr, gResolvedIp);
					StrPrintF(line, "Resolved: %s", gResolvedIp);
					LogAppend(line);
					gSyncPhase = kSyncSending;
				} else {
					gSyncPhase = kSyncResolving;
				}
			}
		}
		break;

	case kSyncResolving:
		{
			/* NetLibGetHostByName has no non-blocking form on this API, so
			 * this one call is a genuine (bounded, dnsTimeoutTicks) UI
			 * stall - the one part of this flow that can't be made fully
			 * non-blocking on Palm OS 4.x. Kept short so a Stop tap that
			 * arrives during it is still picked up promptly once it
			 * returns, and retried across separate ticks (not a tight
			 * internal loop) so the UI gets to breathe between attempts. */
			NetHostInfoBufType hostBuf;
			if (NetLibGetHostByName(gNetRefNum, gServer, &hostBuf, dnsTimeoutTicks, &err) != 0) {
				MemMove(&gRemote.addr, hostBuf.hostInfo.addrListP[0], sizeof(gRemote.addr));
				NetLibAddrINToA(gNetRefNum, gRemote.addr, gResolvedIp);
				StrPrintF(line, "Resolved: %s", gResolvedIp);
				LogAppend(line);
				gSyncPhase = kSyncSending;
			} else {
				gDnsAttempt++;
				if (gDnsAttempt >= dnsMaxAttempts) {
					StrPrintF(gErrMsg, "Could not resolve server name (err=%ld)", (long) err);
					SyncFinish(false);
				}
				/* else: retried on the next tick */
			}
		}
		break;

	case kSyncSending:
		MemSet(gPacket, sizeof(gPacket), 0);
		gPacket[0] = 0x1B;					/* LI=0, VN=3, Mode=3 (client) */
		/* Deliberately NOT calling NetLibSocketConnect - see the working-
		 * pattern memory note: a "connected" UDP socket filters incoming
		 * datagrams by strict peer match, and on some real-hardware/NAT
		 * paths a genuine reply's apparent source can differ just enough
		 * to be silently dropped. Passing the destination directly to each
		 * send instead (unconnected send-to/receive-from) avoids that. */
		if (NetLibSend(gNetRefNum, gSock, gPacket, sizeof(gPacket), 0,
				&gRemote, sizeof(gRemote), ntpTimeoutTicks, &err) == -1) {
			StrPrintF(gErrMsg, "Send failed (err=%ld)", (long) err);
			SyncFinish(false);
			break;
		}
		gWaitStartTick = TimGetTicks();
		gSyncPhase = kSyncWaiting;
		break;

	case kSyncWaiting:
		{
			/* Non-blocking poll (timeout=0) - this is the phase that used
			 * to dominate the wait (up to dnsMaxAttempts * ntpTimeoutTicks),
			 * so making it pollable rather than one long blocking receive
			 * is what actually keeps the clock ticking and Stop responsive. */
			NetFDSetType readSet;
			Int16 selResult;
			Err selErr = errNone;
			netFDZero(&readSet);
			netFDSet(gSock, &readSet);
			selResult = NetLibSelect(gNetRefNum, (UInt16) (gSock + 1), &readSet,
				NULL, NULL, 0, &selErr);
			if (selResult > 0 && netFDIsSet(gSock, &readSet)) {
				Int16 n = NetLibReceive(gNetRefNum, gSock, gPacket, sizeof(gPacket), 0,
					NULL, NULL, 0, &err);
				if (n >= 48) {
					SyncFinish(true);
					break;
				}
				/* short/garbage read - fall through to the timeout/retry check */
			}
			if (TimGetTicks() - gWaitStartTick >= (UInt32) ntpTimeoutTicks) {
				gSendAttempt++;
				if (gSendAttempt >= dnsMaxAttempts) {
					StrPrintF(gErrMsg, "No/short reply from server (err=%ld)", (long) err);
					SyncFinish(false);
				} else {
					gSyncPhase = kSyncSending;	/* resend on the next tick */
				}
			}
			/* else: still within budget, polled again next tick */
		}
		break;
	}
}

/* Common exit for both success and failure: close the socket/net, restore
 * the button, and (on success) run the exact same rebase/log/set/publish
 * tail the old synchronous DoSyncNow used to run right after NtpQuery
 * returned - gPacket already holds the 48-byte reply. */
static void SyncFinish(Boolean ok)
{
	Err err;
	char line[96];

	if (gSock != -1) { NetLibSocketClose(gNetRefNum, gSock, ntpTimeoutTicks, &err); gSock = -1; }
	if (gNetOpened) { NetLibClose(gNetRefNum, false); gNetOpened = false; }

	gSyncPhase = kSyncIdle;
	CtlSetLabel((ControlPtr) Obj(btnSyncNow), "Sync Now");

	if (!ok) {
		StrPrintF(line, "FAILED: %s", gErrMsg);
		LogAppend(line);
		RefreshComparison();
		return;
	}

	{
		DlDateTime ntp;
		UInt32 raw, ntpSeconds, palmSeconds, localSeconds;
		Int16 tz, dst;
		HcState hc;

		/* Transmit Timestamp: bytes 40-43, big-endian seconds since 1900.
		 * Copy via MemMove (not a pointer cast) to sidestep m68k alignment
		 * faults. */
		MemMove(&raw, &gPacket[40], 4);
		ntpSeconds = NetNToHL(raw);
		if (ntpSeconds < ntpToPalmEpochDelta) {	/* server clock before 1904? bogus reply */
			LogAppend("FAILED: Server returned an implausible time");
			RefreshComparison();
			return;
		}
		palmSeconds = ntpSeconds - ntpToPalmEpochDelta;

		/* NTP is UTC; convert to the device's own configured local time
		 * (same prefs the Date & Time panel already uses). */
		tz  = (Int16) PrefGetPreference(prefTimeZone);
		dst = (Int16) PrefGetPreference(prefDaylightSavingAdjustment);
		localSeconds = TimUTCToTimeZone(palmSeconds, tz, dst);

		{
			DateTimeType dt;
			TimSecondsToDateTime(localSeconds, &dt);
			ntp.year    = dt.year;		/* already absolute, see RefreshComparison */
			ntp.month   = dt.month;
			ntp.day     = dt.day;
			ntp.hour    = dt.hour;
			ntp.minute  = dt.minute;
			ntp.second  = dt.second;
			ntp.weekday = dt.weekDay;
		}

		/* Rebase math (same year-type match the plugin's own control panel
		 * uses) - computed unconditionally so the delta shown below and any
		 * actual clock-set further down agree on the exact same target
		 * seconds. */
		{
			dl_i32 offset = dl_reBaseOffset((dl_i32) ntp.year);
			dl_i32 basedYear = (dl_i32) ntp.year - offset;
			dl_i64 masterSecs = dl_dateToSeconds((dl_i32) ntp.year, (dl_i16) ntp.month,
				(dl_i16) ntp.day, (dl_i16) ntp.hour, (dl_i16) ntp.minute, (dl_i16) ntp.second);
			UInt32 based32 = (UInt32) dl_dateToSeconds(basedYear, (dl_i16) ntp.month,
				(dl_i16) ntp.day, (dl_i16) ntp.hour, (dl_i16) ntp.minute, (dl_i16) ntp.second);
			Int32 delta = (Int32) (based32 - TimGetSeconds());

			/* Log gets the full retrieved date/time (the detailed trace);
			 * the always-visible comparison row gets just the adjustment
			 * (the at-a-glance number). */
			{
				char buf[48];
				FormatDlDateTime(&ntp, buf, sizeof(buf));
				StrPrintF(line, "NTP: %s", buf);
				LogAppend(line);
			}
			StrPrintF(line, "NTP: %s%ld sec", delta >= 0 ? "+" : "", (long) delta);
			FrmCopyLabel(FrmGetActiveForm(), lblNtp, line);

			hc = HcCheck();

			if (!gSetClockFlag) {
				LogAppend("Set device clock: OFF - no changes made");
				RefreshComparison();
				return;
			}

			if (offset != 0) {
				if (hc == HC_OK) {
					StrPrintF(line, "Real year %ld exceeds 2031 - rebasing "
						"(era plugin will track true date).", (long) ntp.year);
				} else {
					StrPrintF(line, "Real year %ld exceeds 2031 and no active era "
						"plugin found.", (long) ntp.year);
					LogAppend(line);
					StrPrintF(line, "Rebasing device clock to best-matched year "
						"%ld (weekday/leap-exact).", (long) basedYear);
				}
				LogAppend(line);
			}

			TimSetSeconds(based32);
			LogAppend("Setting clock... OK");

			if (hc == HC_OK) {
				Y2k35Pref p;
				p.master = masterSecs;
				p.offset = offset;
				FtrSet(Y2K35_CREATOR, Y2K35_FTR_OFFSET, (UInt32) offset);
				FtrSet(Y2K35_CREATOR, Y2K35_FTR_MHI, (UInt32) (masterSecs >> 32));
				FtrSet(Y2K35_CREATOR, Y2K35_FTR_MLO, (UInt32) (masterSecs & 0xFFFFFFFFUL));
				PrefSetAppPreferences(Y2K35_CREATOR, Y2K35_PREF_ID, 1, &p, sizeof(p), true);
				LogAppend("Publishing to plugin... OK");
			}
		}
	}

	LogAppend("Sync complete.");
	RefreshComparison();
}

/* ---- Manual Network > Connect/Disconnect ------------------------------
 * Net.lib's open count is refcounted, so a manual Connect held open here
 * doesn't fight with Sync Now's own self-contained Open/Close pairs (each
 * NtpQuery call just nudges the count up and back down by one) - it just
 * keeps the link warm across syncs and lets a slow real-hardware connection
 * (routing/DHCP still settling right after Open) finish settling before a
 * time-sensitive NTP exchange starts, instead of racing it. Disconnect is
 * there to manually reset a connection that's gotten stuck. */

/* Own IP + DNS server(s) actually in effect, appended after Connect - the
 * concrete numbers to check against whatever the working SNTP app / SLIP
 * setup expects. netSettingRTPrimaryDNS/RTSecondaryDNS are what an
 * interface that obtains DNS dynamically (PPP/SLIP) actually resolved,
 * separate from any static DNS configured in Network prefs - on a SLIP
 * link this is usually the one that matters. */
static void AppendNetworkInfo(UInt16 refNum)
{
	UInt32 ifCreator;
	UInt16 ifInstance = 0;
	UInt16 len;
	NetIPAddr addr;
	char ipBuf[16];
	char line[64];

	/* Try the known interface creators directly (SLIP first, since that's
	 * what's actually in use) rather than trusting netSettingDefaultIFCreator
	 * - on at least one real setup that setting came back all-zero, which
	 * NetLibIFSettingGet then silently resolved to the LOOPBACK interface
	 * (127.0.0.1) instead of erroring, masking the real SLIP address. */
	addr = 0;
	ifCreator = netIFCreatorSLIP;
	len = sizeof(addr);
	if (NetLibIFSettingGet(refNum, ifCreator, ifInstance, netIFSettingActualIPAddr,
			&addr, &len) != errNone || addr == 0) {
		ifCreator = netIFCreatorPPP;
		addr = 0; len = sizeof(addr);
		if (NetLibIFSettingGet(refNum, ifCreator, ifInstance, netIFSettingActualIPAddr,
				&addr, &len) != errNone || addr == 0) {
			/* neither known type answered - fall back to whatever the OS
			 * considers "default", for whatever that's worth here */
			ifCreator = 0;
			len = sizeof(ifCreator);
			NetLibSettingGet(refNum, netSettingDefaultIFCreator, &ifCreator, &len);
			len = sizeof(ifInstance);
			NetLibSettingGet(refNum, netSettingDefaultIFInstance, &ifInstance, &len);
			addr = 0; len = sizeof(addr);
			NetLibIFSettingGet(refNum, ifCreator, ifInstance, netIFSettingActualIPAddr, &addr, &len);
		}
	}

	{
		char c[5];
		c[0] = (char) ((ifCreator >> 24) & 0xFF);
		c[1] = (char) ((ifCreator >> 16) & 0xFF);
		c[2] = (char) ((ifCreator >> 8) & 0xFF);
		c[3] = (char) (ifCreator & 0xFF);
		c[4] = '\0';
		StrPrintF(line, "IF: %s #%u", c, (unsigned) ifInstance);
		LogAppend(line);
	}

	if (addr) {
		NetLibAddrINToA(refNum, addr, ipBuf);
		StrPrintF(line, "My IP: %s", ipBuf);
	} else {
		StrCopy(line, "My IP: (none)");
	}
	LogAppend(line);

	addr = 0; len = sizeof(addr);
	NetLibSettingGet(refNum, netSettingPrimaryDNS, &addr, &len);
	if (addr) { NetLibAddrINToA(refNum, addr, ipBuf); StrPrintF(line, "DNS1: %s", ipBuf); }
	else StrCopy(line, "DNS1: (none)");
	LogAppend(line);

	addr = 0; len = sizeof(addr);
	NetLibSettingGet(refNum, netSettingRTPrimaryDNS, &addr, &len);
	if (addr) { NetLibAddrINToA(refNum, addr, ipBuf); StrPrintF(line, "RT DNS1: %s", ipBuf); }
	else StrCopy(line, "RT DNS1: (none)");
	LogAppend(line);
}

static void DoConnect(void)
{
	UInt16 refNum;
	Err err, openErr;

	LogClear();
	if (SysLibFind("Net.lib", &refNum) != errNone) {
		LogAppend("Connect: Net.lib not found");
		return;
	}
	err = NetLibOpen(refNum, &openErr);
	if (openErr != errNone || (err != errNone && err != netErrAlreadyOpen)) {
		LogAppend("Connect: failed to open network");
		return;
	}
	LogAppend(err == netErrAlreadyOpen ? "Connect: already connected" : "Connect: OK");
	AppendNetworkInfo(refNum);
}

static void DoDisconnect(void)
{
	UInt16 refNum;
	UInt16 count = 0;
	UInt16 i;
	Err err;
	Boolean wasOpen;

	LogClear();
	if (SysLibFind("Net.lib", &refNum) != errNone) {
		LogAppend("Disconnect: Net.lib not found");
		return;
	}

	/* A single NetLibClose(refnum, false) only drops the open count by one
	 * and defers the actual teardown to a lazy "close-wait" state - the
	 * Network prefs panel can keep showing "Connected" for a while after
	 * that, which is what a real device showed. Drain the whole open count
	 * with immediate=true so the interface actually comes down now,
	 * regardless of how many opens are outstanding (ours, or anything else
	 * still holding a reference). */
	NetLibOpenCount(refNum, &count);
	wasOpen = count > 0;
	for (i = 0; i < count; i++)
		NetLibClose(refNum, true);
	err = NetLibFinishCloseWait(refNum);	/* force-complete a lingering close-wait, if any */

	LogAppend(!wasOpen ? "Disconnect: was not connected" :
		(err == errNone ? "Disconnect: OK" : "Disconnect: closed (still finishing)"));
}

/* ---- form init / event handling --------------------------------------- */
static void FormInit(void)
{
	EraNtpDefaults d;
	FieldPtr f;

	LoadDefaults(&d);

	f = (FieldPtr) Obj(fldServer);
	{
		MemHandle h = MemHandleNew((UInt32) StrLen(d.server) + 1);
		if (h) {
			char *p = (char *) MemHandleLock(h);
			StrCopy(p, d.server);
			MemHandleUnlock(h);
			FldSetTextHandle(f, h);
		}
	}
	CtlSetValue((ControlPtr) Obj(chkSetClock), (Int16) d.setClock);

	gLogLen = 0;
	gLogBuf[0] = '\0';

	FrmDrawForm(FrmGetActiveForm());
	RefreshComparison();
}

static Boolean MainFormHandleEvent(EventPtr eventP)
{
	Boolean handled = false;

	switch (eventP->eType) {
	case frmOpenEvent:
		FormInit();
		handled = true;
		break;

	case ctlSelectEvent:
		switch (eventP->data.ctlSelect.controlID) {
		case btnDone:
			{
				/* No parent form to return to (single-form app) - closing the
				 * app is done by enqueueing appStopEvent ourselves, same event
				 * the OS sends when the user switches away, which the main
				 * loop below already handles. */
				EventType stopEvent;
				MemSet(&stopEvent, sizeof(stopEvent), 0);
				stopEvent.eType = appStopEvent;
				EvtAddEventToQueue(&stopEvent);
			}
			handled = true;
			break;
		case btnSyncNow:
			if (gSyncPhase == kSyncIdle) SyncStart();
			else SyncCancel();
			handled = true;
			break;
		}
		break;

	/* Dragging the log's scrollbar generates sclRepeatEvent (not routed to
	 * the field automatically - PalmOS fields never drive their own
	 * scrollbar, same as CryptoNote's/Address35's own field+scrollbar
	 * pairs). Scroll the field by the dragged delta, then FldGetScrollValues
	 * is the one authoritative source for the resulting position (rather
	 * than trusting newValue directly, since FldScrollField clamps at the
	 * field's real top/bottom). */
	case sclRepeatEvent:
		if (eventP->data.sclRepeat.scrollBarID == sclLog) {
			FieldPtr fld = (FieldPtr) Obj(fldLog);
			Int16 delta = (Int16) (eventP->data.sclRepeat.newValue
				- eventP->data.sclRepeat.value);
			if (delta != 0 && FldScrollable(fld, delta > 0 ? winDown : winUp)) {
				UInt16 scrollPos, textHeight, fieldHeight, maxValue;
				FldScrollField(fld, (UInt16) (delta > 0 ? delta : -delta),
					delta > 0 ? winDown : winUp);
				FldGetScrollValues(fld, &scrollPos, &textHeight, &fieldHeight);
				maxValue = (textHeight > fieldHeight) ? (UInt16) (textHeight - fieldHeight) : 0;
				SclSetScrollBar((ScrollBarPtr) Obj(sclLog), scrollPos, 0,
					maxValue, fieldHeight > 0 ? fieldHeight - 1 : 0);
			}
		}
		handled = true;
		break;

	case menuEvent:
		switch (eventP->data.menu.itemID) {
		case mniConnect:
			DoConnect();
			handled = true;
			break;
		case mniDisconnect:
			DoDisconnect();
			handled = true;
			break;
		}
		break;

	default:
		break;
	}
	return handled;
}

/* ---- app entry --------------------------------------------------------- */
static Err StartApplication(void)
{
	FrmGotoForm(frmMain);
	return errNone;
}

static void StopApplication(void)
{
	FieldPtr f = (FieldPtr) Obj(fldLog);
	MemHandle h = FldGetTextHandle(f);

	/* Quitting mid-sync (e.g. Done tapped while "Stop" is showing) must not
	 * leak the socket/net-lib open - abort silently, no UI to update. */
	if (gSyncPhase != kSyncIdle) {
		Err err;
		if (gSock != -1) NetLibSocketClose(gNetRefNum, gSock, 0, &err);
		if (gNetOpened) NetLibClose(gNetRefNum, false);
		gSyncPhase = kSyncIdle;
	}

	FldSetTextHandle(f, NULL);
	if (h) MemHandleFree(h);
	FrmCloseAllForms();
}

/* Dispatches frmLoadEvent -> FrmInitForm + wires the (single) form's event
 * handler. Same shape as Sec35's own AppHandleEvent (sec35.c:2735). */
static Boolean AppHandleEvent(EventType *e)
{
	if (e->eType == frmLoadEvent) {
		FormPtr frm = FrmInitForm(e->data.frmLoad.formID);
		FrmSetActiveForm(frm);
		FrmSetEventHandler(frm, MainFormHandleEvent);
		return true;
	}
	return false;
}

static void EventLoop(void)
{
	EventType e;
	UInt16 uerr;
	UInt32 lastClockTick = 0;

	do {
		/* ~1s tick while frmMain is idle so Device/Era keep ticking live
		 * (same bounded-EvtGetEvent idiom as Sec35's countdown label); a
		 * much shorter poll while a sync is in flight so SyncTick() gets
		 * to advance the state machine promptly and a Stop tap is picked
		 * up quickly - see the SyncPhase comment for why this exists. */
		UInt32 timeout;
		if (FrmGetActiveFormID() != frmMain) timeout = evtWaitForever;
		else if (gSyncPhase != kSyncIdle) timeout = syncPollTicks;
		else timeout = tickTimeout;
		EvtGetEvent(&e, timeout);

		if (e.eType == nilEvent) {
			if (FrmGetActiveFormID() == frmMain) {
				UInt32 now = TimGetTicks();
				if (now - lastClockTick >= (UInt32) tickTimeout) {
					RefreshComparison();
					lastClockTick = now;
				}
				if (gSyncPhase != kSyncIdle) SyncTick();
			}
			continue;
		}
		if (SysHandleEvent(&e)) continue;
		if (MenuHandleEvent(0, &e, &uerr)) continue;
		if (AppHandleEvent(&e)) continue;
		FrmDispatchEvent(&e);
	} while (e.eType != appStopEvent);
}

UInt32 PilotMain(UInt16 cmd, void *cmdPBP, UInt16 launchFlags)
{
	Err err;

	if (cmd == sysAppLaunchCmdNormalLaunch) {
		err = StartApplication();
		if (err) return err;
		EventLoop();
		StopApplication();
	}

	return errNone;
}
