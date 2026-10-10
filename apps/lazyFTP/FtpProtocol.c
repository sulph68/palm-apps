/*
 * FtpProtocol.c - FTP control connection, PASV data connection, and the
 * command set (LIST/CWD/PWD/DELE/RMD/RNFR+RNTO/MKD/RETR/STOR). Original
 * code; axxpac_ftp_src was read only as design/protocol reference (see
 * project memory lazyftp-project.md).
 */
#include "FtpProtocol.h"
#include "LzTPLog.h"
#include "Listing.h"	/* ListingIngestBegin/Entry/End + the on-device cache
						   (ListingCacheValidFor/ActivateExisting) - FtpList
						   populates/reads Listing.c's own storage directly */
#include <TimeMgr.h>	/* TimGetTicks - real-device diagnostic timing in FtpList,
						   see its own comment */
#include <ExgMgr.h>		/* ExgDBWrite - FtpStoreDatabase */

#define ftpCtrlTimeoutTicks   (15 * SysTicksPerSecond())
/* Widened from 20s -> 60s: real-device report of "larger folders refuse to
 * load, error in data connection" traced by the user to real latency on
 * their SLIP+NAT link, not a hung/dead connection - 20s wasn't enough
 * headroom for a single NetLibReceive call mid-transfer on a slow link,
 * and every such near-miss showed as a hard "data connection error"
 * instead of just taking longer. */
#define ftpDataTimeoutTicks   (60 * SysTicksPerSecond())
/* Real-device bug found this session (see project memory wirsa-slip-nat-
 * project.md): a single NetLibReceive call with the full 60s timeout above
 * doesn't appear to unblock early just because the data connection actually
 * closed with no further payload - the WiRSa SLIP gateway's own serial log
 * showed it detecting the remote FTP server closing the connection and
 * sending its FIN to the Palm almost instantly, but lazyFTP still sat
 * blocked for the FULL 60 seconds regardless, and by the time it noticed,
 * the gateway (correctly, per normal TCP idle-connection hygiene) had long
 * since forgotten the connection - every retry after that point hit "no
 * entry" on the gateway and looked like a broken transfer, even though the
 * data had actually arrived intact in milliseconds. FtpPollReceive() below
 * polls in short increments instead of one long blocking call, so a closed
 * connection is noticed within ~ftpPollTicks instead of up to the full
 * budget - while still tolerating the same overall ftpDataTimeoutTicks for
 * a connection that's genuinely just slow, not closed. Originally only used
 * for the data connection (hence the old name FtpDataReceive) - also used
 * for the CONTROL connection's FtpReadLine now (see that function's own
 * comment): the plain blocking NetLibReceive it used to call directly has
 * the exact same real-device timeout-unreliability this fix was written
 * for, just never ported over to that call site until now. */
#define ftpPollTicks          (2 * SysTicksPerSecond())
/* Real-device bug found+fixed this session: a single NetLibGetHostByName
 * attempt failed with netErrTimeout (err=4626) on a real SLIP link where
 * EraNTP's own DNS resolution of a DIFFERENT hostname, over the exact same
 * connection, succeeded - matches EraNTP's own documented real-hardware
 * lesson verbatim (see project memory palmos-netlib-udp-client-working-
 * pattern.md): "the first lookup right after NetLibOpen can spuriously fail
 * while the link is still settling - retry before giving up". lazyFTP's
 * single 15s attempt with no retry was exactly the gap EraNTP's own
 * dnsMaxAttempts/dnsTimeoutTicks pattern was built to cover; matching it
 * here verbatim (5s/attempt, up to 3 attempts) rather than one long wait. */
#define ftpDnsAttemptTicks    (5 * SysTicksPerSecond())
#define ftpDnsMaxAttempts     3
#define ftpChunkLen           8192		/* doubled from the suite's original 4096
										   (2026-08-21) at the user's request, to
										   give the app fewer, larger reads to do
										   per NetLibReceive() call. NOTE: this is
										   the size of the buffer WE pass to
										   NetLibReceive, not PalmOS's own internal
										   SLIP/UART receive buffer (which this
										   constant cannot influence - that's ROM
										   code, not app-tunable) - real-device
										   testing showed a "got=0 entries" failure
										   (ZERO bytes ever reaching the app in 60s)
										   for large directories, which a bigger
										   destination buffer alone cannot fix if
										   nothing was arriving in the first place;
										   the actual fix for THAT is pacing on the
										   WiRSa gateway side (NAT_TCP_MAX_WINDOW,
										   see wirsa-slip-nat-project memory). This
										   change is a complementary, low-risk
										   improvement (fewer read round-trips for
										   whatever DOES arrive), not a replacement
										   for that fix. See address35-vcard-
										   uint16-truncation-fix in project memory
										   for why 4096 was chosen originally. */

/* Shared data-chunk buffer for FtpList/FtpRetrieve/FtpStore. Each of those
 * used to declare its OWN function-local `static char buf[ftpChunkLen]` -
 * harmless at the old 4096 size, but doubling ftpChunkLen to 8192 (see its
 * own comment) meant THREE separate 8192-byte statics (24576 bytes total)
 * overflowed PalmOS's "datares" (near/static data) region at link time
 * ("region datares is full"), a much tighter budget than the app's general
 * heap. Since this is a single-threaded, purely synchronous app - FtpList/
 * FtpRetrieve/FtpStore never run concurrently, only ever one blocking call
 * in flight at a time - one shared buffer is safe and cuts the total
 * footprint to a THIRD of three separate ones, while still getting the
 * larger per-call chunk size everywhere. */
static char gFtpChunkBuf[ftpChunkLen];

typedef struct {
	Boolean connected;
	UInt16  netRefNum;
	Boolean netOpened;
	NetSocketRef ctrlSock;
	char    curPath[ftpPathLen];
	char    server[ftpNameLen];	/* identifies which remote dir a cached
									   listing belongs to - see Listing.c's
									   on-device cache DB */
	UInt16  port;
	/* user/pass: the ACTUAL resolved credentials (anonymous fallback
	 * already applied - see FtpConnect) from the last FULLY successful
	 * connect. Populated only at the moment a connect succeeds, cleared on
	 * an explicit FtpDisconnectServer()/FtpDisconnectNetwork() - a non-empty gFtp.user is the signal
	 * FtpAutoReconnect() uses to tell "we had a real working session, safe
	 * to silently reconnect" apart from "this is a first-ever connect
	 * attempt that just happened to fail, don't second-guess it". */
	char    user[ftpNameLen];
	char    pass[ftpNameLen];
} FtpState;

static FtpState gFtp;

/* Guards FtpAutoReconnect against recursing into itself if the reconnect
 * attempt's own FtpCmd calls also fail - deliberately file-scope, NOT part
 * of gFtp, because FtpConnect() does a MemSet(&gFtp,...) partway through
 * its own execution (mid-reconnect), which would otherwise wipe a
 * same-struct guard flag right when it's needed most. */
static Boolean gFtpReconnecting = false;

/* Optional caller-registered hook, invoked by FtpAutoReconnect the moment a
 * silent reconnect attempt actually begins - see FtpSetStatusHook's own doc
 * comment in FtpProtocol.h. Real-hardware report this session: a busy
 * dialog stayed frozen on stale text (e.g. "Accessing...") for the ENTIRE
 * duration of a silent reconnect happening deep inside FtpCmd, since
 * nothing below MainForm.c's own LzTPBusyBegin call had any way to reach
 * back up and change what's on screen. NULL (default) = no-op, so this
 * whole mechanism is a pure opt-in with zero effect on any caller that
 * doesn't register one. */
static FtpStatusProc gFtpStatusHook = NULL;

SEC004 void FtpSetStatusHook(FtpStatusProc proc)
{
	gFtpStatusHook = proc;
}

/* See FtpConnRetryProc's own doc comment in FtpProtocol.h for why this is a
 * separate hook from gFtpStatusHook above, not a reuse of it. */
static FtpConnRetryProc gFtpConnRetryHook = NULL;

SEC004 void FtpSetConnRetryHook(FtpConnRetryProc proc)
{
	gFtpConnRetryHook = proc;
}

#define ftpRetryIntervalSecs 5

/* Optional caller-registered hook for byte-level transfer progress - see
 * FtpSetProgressHook's own doc comment in FtpProtocol.h. */
static FtpProgressProc gFtpProgressHook = NULL;

SEC004 void FtpSetProgressHook(FtpProgressProc proc)
{
	gFtpProgressHook = proc;
}

/* Optional caller-registered hook for transfer sub-phase changes - see
 * FtpSetPhaseHook's own doc comment in FtpProtocol.h. */
static FtpPhaseProc gFtpPhaseHook = NULL;

SEC004 void FtpSetPhaseHook(FtpPhaseProc proc)
{
	gFtpPhaseHook = proc;
}

/* Optional caller-registered hook polled for mid-transfer cancellation -
 * see FtpSetCancelQuery's own doc comment in FtpProtocol.h. */
static FtpCancelQueryProc gFtpCancelQuery = NULL;

SEC004 void FtpSetCancelQuery(FtpCancelQueryProc proc)
{
	gFtpCancelQuery = proc;
}

/* ---- transport boundary -------------------------------------------------
 * The only place that touches raw sockets - see FtpProtocol.h. A future
 * FTPS integration changes only these four functions. */
static NetSocketRef FtpTransportOpen(const char *host, UInt16 port,
	char *errMsg, UInt16 errMsgLen)
{
	NetSocketRef sock;
	NetSocketAddrINType remote;
	Err err;
	UInt8 ipBytes[4];
	/* large (~568B) struct - static, not stack-local, matching the same
	 * fix already applied to FtpList/FtpRetrieve/FtpStore's 4KB chunk
	 * buffers (see project memory lazyftp-project.md: stack overflow is a
	 * recurring class of bug in this codebase on PalmOS's tiny ~4KB
	 * default app stack). */
	static NetHostInfoBufType hostBuf;

	MemSet(&remote, sizeof(remote), 0);
	remote.family = netSocketAddrINET;
	remote.port = NetHToNS(port);

	/* Resolve the address BEFORE opening any socket - matches EraNTP's own
	 * proven real-SLIP-hardware sequence exactly (NetLibOpen -> DNS/IP-parse
	 * -> only THEN NetLibSocketOpen). Real bug found this session: with a
	 * TCP socket already open, NetLibGetHostByName reliably timed out
	 * (netErrTimeout, err=4626) on a real SLIP link where EraNTP's own DNS
	 * resolution of a different hostname, over the identical connection and
	 * with NO other socket open at the time, succeeded - retrying the same
	 * failing call more times (the earlier fix) didn't help, which is
	 * consistent with a resource/link-state conflict from the already-open
	 * socket rather than a transient timing issue. */
	{
		UInt16 i;
		Boolean numeric = true;
		for (i = 0; host[i]; i++) {
			if (!((host[i] >= '0' && host[i] <= '9') || host[i] == '.')) { numeric = false; break; }
		}
		if (numeric) {
			/* dotted-quad literal - parse directly, no DNS (mirrors EraNTP's
			 * ParseIPv4 escape hatch) */
			UInt16 octet = 0, idx = 0;
			MemSet(ipBytes, 4, 0);
			for (i = 0; host[i] && idx < 4; i++) {
				if (host[i] == '.') { ipBytes[idx++] = (UInt8) octet; octet = 0; }
				else octet = (UInt16) (octet * 10 + (host[i] - '0'));
			}
			if (idx < 4) ipBytes[idx] = (UInt8) octet;
			MemMove(&remote.addr, ipBytes, sizeof(remote.addr));
		} else {
			UInt16 attempt;
			Boolean resolved = false;
			for (attempt = 0; attempt < ftpDnsMaxAttempts; attempt++) {
				if (NetLibGetHostByName(gFtp.netRefNum, host, &hostBuf,
						ftpDnsAttemptTicks, &err) != 0) {
					resolved = true;
					break;
				}
			}
			if (!resolved) {
				StrPrintF(errMsg, "Could not resolve %s (err=%ld)", host, (long) err);
				return -1;
			}
			MemMove(&remote.addr, hostBuf.hostInfo.addrListP[0], sizeof(remote.addr));
		}
	}

	sock = NetLibSocketOpen(gFtp.netRefNum, netSocketAddrINET, netSocketTypeStream,
		netSocketProtoIPTCP, ftpCtrlTimeoutTicks, &err);
	if (sock == -1) {
		StrPrintF(errMsg, "Could not open socket (err=%ld)", (long) err);
		return -1;
	}

	if (NetLibSocketConnect(gFtp.netRefNum, sock, (NetSocketAddrType *) &remote,
			sizeof(remote), ftpCtrlTimeoutTicks, &err) == -1) {
		StrPrintF(errMsg, "Could not connect (err=%ld)", (long) err);
		NetLibSocketClose(gFtp.netRefNum, sock, ftpCtrlTimeoutTicks, &err);
		return -1;
	}
	return sock;
}

/* Simple bitwise CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no table -
 * keeps this out of the const-data budget, cheap enough per-byte for a
 * transfer this app's own real link speed already bottlenecks on) - used to
 * verify an upload actually arrived intact server-side (see
 * FtpStoreDatabase) rather than trusting a clean STOR reply alone, after a
 * real-hardware report of uploads coming out silently truncated despite
 * FtpTransportSend's own short-send fix reporting success. Not a
 * cryptographic checksum - just enough to catch the truncation/corruption
 * class of bug actually seen here (a genuinely adversarial sender could
 * still craft a CRC collision, but that's not this app's threat model). */
static UInt16 Crc16Update(UInt16 crc, const UInt8 *data, UInt16 len)
{
	UInt16 i;
	for (i = 0; i < len; i++) {
		UInt8 bit;
		crc = (UInt16) (crc ^ ((UInt16) data[i] << 8));
		for (bit = 0; bit < 8; bit++) {
			if (crc & 0x8000) crc = (UInt16) ((crc << 1) ^ 0x1021);
			else crc = (UInt16) (crc << 1);
		}
	}
	return crc;
}

/* Real bug found+fixed live (this session, "upload silently corrupts the
 * remote file"): a single NetLibSend call was trusted to send the WHOLE
 * `len` requested, and every caller only checked the return value against
 * -1 (a genuine error) - never against `len` itself. NetLibSend can do a
 * legitimate PARTIAL send (the same underlying reason NetLibReceive can do
 * a partial read - see RetrieveDbReadProc's own near-identical fix in
 * FtpProtocol.c), silently dropping the unsent remainder with no error
 * returned at all: `StoreDbWriteProc`'s only check (`== -1`) treated a
 * short-but-positive return as complete success. Confirmed live: a
 * database uploaded through lazyFTP itself (`Address35DB.pdb`) came back
 * with visibly corrupted bytes partway through its header when downloaded
 * back and inspected byte-for-byte against a host-side reference. FIXED by
 * looping here until `len` bytes are actually sent (or a genuine
 * error/zero-progress ends it early), mirroring the receive-side fix -
 * every caller's existing `== -1` check keeps working unchanged since a
 * full, successful send still returns exactly `len`.
 *
 * STILL NOT ENOUGH on real hardware: user confirmed on a real device
 * (POSE64 never reproduced it - fast/local networking masks it) that
 * uploads were still coming out truncated even with the loop above. A
 * single NetLibSend call handed the FULL remaining `len` (still up to
 * ftpChunkLen=8192 bytes in one call) - over a slow real SLIP link, one
 * huge request is far more likely to hit `ftpCtrlTimeoutTicks` before the
 * link has physically finished draining it, and (unlike the receive side,
 * which already had `ftpDataTimeoutTicks`/polling tuned for this exact
 * class of slow-link problem - see FtpPollReceive's own comment) this send
 * path had never been given the same "small chunks, not one giant one"
 * treatment. FIXED (2nd pass) by additionally bounding each individual
 * NetLibSend call to ftpSendChunkLen regardless of how much the caller
 * asked for - many small, more-likely-to-fully-complete-within-timeout
 * sends instead of few large, more-likely-to-time-out-mid-flight ones.
 * 536 (not some arbitrary round number) is the classic minimum-MTU TCP
 * segment size (576-byte minimum IPv4 MTU - 40 bytes of IP+TCP headers) -
 * the same conservative, universally-safe-over-any-link size real-world TCP
 * stacks fall back to before path-MTU discovery, which fits this app's own
 * slow-real-SLIP-link reality far better than picking a bigger, faster-on-
 * a-good-link-but-riskier-on-a-bad-one number like the read side's 8192. */
#define ftpSendChunkLen 536

static Int16 FtpTransportSend(NetSocketRef sock, const void *buf, UInt16 len)
{
	const UInt8 *p = (const UInt8 *) buf;
	UInt16 sent = 0;

	while (sent < len) {
		Err err;
		UInt16 want = (UInt16) (len - sent);
		Int16 n;
		if (want > ftpSendChunkLen) want = ftpSendChunkLen;
		n = NetLibSend(gFtp.netRefNum, sock, (void *) (p + sent), want,
			0, NULL, 0, ftpCtrlTimeoutTicks, &err);
		if (n <= 0) return -1;	/* real error, or zero progress - don't spin forever */
		sent = (UInt16) (sent + n);
	}
	return (Int16) sent;
}

/* Poll-based read, originally just for the data connection (FtpList/
 * FtpRetrieve) - REWRITTEN (2026-08-21) after real-device instrumentation
 * proved the previous approach (repeated short-timeout FtpTransportReceive/
 * NetLibReceive calls) never worked as intended: a failure's poll-count vs
 * elapsed-time breakdown showed only 3 polls consumed the FULL 60-second
 * budget, not the expected ~30 - meaning each 2-second-REQUESTED
 * NetLibReceive call was actually blocking for ~20 REAL seconds regardless.
 * NetLibReceive simply does not reliably honor a short timeout on this
 * SLIP-based connection type, so no amount of polling-strategy tuning
 * around IT could ever detect anything faster - the short-poll loop was
 * still bound by NetLibReceive's own, much longer, real wait every single
 * call.
 *
 * Fixed by never asking NetLibReceive to wait at all: NetLibSelect(...,
 * timeout=0, ...) checks readiness first (also non-blocking), and
 * NetLibReceive is only called - with its OWN timeout at 0, since there's
 * nothing left to wait for - once select has ALREADY confirmed data is
 * waiting. This is the exact pattern already proven on real hardware by
 * EraNTP (see palmos-netlib-udp-client-working-pattern project memory),
 * adapted from UDP to TCP. Real elapsed time is tracked via TimGetTicks()
 * directly (not an internal "assume each call took its requested timeout"
 * counter, which is exactly what silently broke the old approach) and
 * SysTaskDelay() paces the polling loop so it sleeps between checks
 * instead of spinning the CPU at 100%.
 *
 * Generalized (this session) to take its own timeout budget rather than
 * hardcoding ftpDataTimeoutTicks, so FtpReadLine (the CONTROL connection)
 * can reuse this exact same fix with ftpCtrlTimeoutTicks instead - it used
 * to call FtpTransportReceive directly (a plain blocking NetLibReceive),
 * which has the identical real-device timeout-unreliability this function
 * was written to work around, just never ported to that call site until
 * now. Real-hardware report: lazyFTP intermittently failing to connect to
 * some servers with a "Connection lost" error that cleared up after 2-3
 * retries - a password issue would fail consistently every attempt, not
 * intermittently, which pointed at the control connection's read path
 * instead (see FtpReadLine's own comment for the rest of this fix).
 *
 * Preserves the exact same (return value, *errOut) contract as before:
 * n>0 = got data, n==0 = clean EOF, n<0 = a genuine error/timeout.
 * pollCountOut (may be NULL) counts individual select() checks made, for
 * the same real-device diagnostic purpose as before. */
static Int16 FtpPollReceive(NetSocketRef sock, void *buf, UInt16 len, Int32 budgetTicks,
	Err *errOut, UInt16 *pollCountOut)
{
	UInt32 startTick = TimGetTicks();

	for (;;) {
		NetFDSetType readSet;
		Int16 selResult;
		Err selErr = errNone;

		netFDZero(&readSet);
		netFDSet(sock, &readSet);
		selResult = NetLibSelect(gFtp.netRefNum, (UInt16) (sock + 1), &readSet,
			NULL, NULL, 0, &selErr);
		if (pollCountOut) (*pollCountOut)++;

		if (selResult > 0 && netFDIsSet(sock, &readSet)) {
			Err err;
			Int16 n = NetLibReceive(gFtp.netRefNum, sock, buf, len, 0, NULL, NULL, 0, &err);
			if (errOut) *errOut = err;
			return n;
		}

		if ((Int32) (TimGetTicks() - startTick) >= budgetTicks) {
			/* Exhausted the full budget across repeated non-blocking checks
			 * with nothing ever ready - a genuine stall, not just one slow
			 * read. */
			if (errOut) *errOut = netErrTimeout;
			return -1;
		}
		/* Real-hardware crash found this session ("Fatal Alert" reported
		 * after a network drop mid-operation): NetLibSelect here is always
		 * called with timeout=0 (non-blocking, by design - see this
		 * function's own comment for why) and SysTaskDelay is a plain OS
		 * sleep, neither of which is the kind of genuine blocking-with-
		 * timeout NetLib call the OS uses to know an app is legitimately
		 * waiting on the network and extend auto-off accordingly. Unlike
		 * FtpTransportSend (which DOES block inside a real NetLibSend call
		 * each time and gets that for free), this loop can busy-poll for
		 * the FULL budget (up to 60s on the data connection) with nothing
		 * the OS recognizes as activity - exactly the class of bug already
		 * hit and fixed elsewhere in this suite (Address35/Sec35 progress
		 * bars). A real network drop (this function's very reason to time
		 * out at all) is precisely the scenario that runs this loop to its
		 * full budget. */
		EvtResetAutoOffTimer();
		SysTaskDelay(ftpPollTicks);
	}
}

static void FtpTransportClose(NetSocketRef sock)
{
	Err err;
	if (sock != -1) NetLibSocketClose(gFtp.netRefNum, sock, ftpCtrlTimeoutTicks, &err);
}

/* Real bug found+fixed live (this session, the actual root cause of the
 * "upload truncates" saga - see FtpStoreDatabase's own comment for the full
 * diagnostic trail): NetLibSend only copies data into the OS/driver's own
 * OUTGOING queue and returns success as soon as it's accepted there - it
 * does NOT wait for the data to actually be transmitted over the wire,
 * especially over a slow real SLIP link where the app can hand data to
 * NetLibSend far faster than ~9600-19200 baud serial can physically drain
 * it. NetLibSocketClose, called right after our own send loop finished
 * (which only proves the OS ACCEPTED everything, not that it SENT
 * everything), was closing the connection while several KB could still be
 * sitting unsent in that queue.
 *
 * FIRST attempt: enabling the standard SO_LINGER-equivalent socket option
 * (`netSocketOptSockLinger`) on the data socket right after opening it -
 * this is SUPPOSED to make a subsequent close() block until the queue has
 * actually drained. Two independent real-device gateway logs proved this
 * did NOT work: the second log's FIN arrived within ~35ms of the last data
 * chunk being forwarded (not the up-to-30s `ftpLingerSeconds` this app
 * asked for), and the amount that actually reached the gateway was WORSE
 * than the first attempt (7766 of 14539 bytes, vs 10466 before) - this
 * particular Net.lib/SLIP driver combination silently does not honor this
 * option (`NetLibSocketOptionSet`'s own return isn't checked - by design,
 * best-effort - but the observed behavior confirms it had no real effect).
 * Left the option-set call in (harmless, and correct in principle - may
 * still help on a real Net.lib implementation that DOES honor it) but it is
 * NOT the actual fix.
 *
 * REAL fix: an explicit, generous, PACED wall-clock delay after the send
 * loop finishes and BEFORE closing - `FtpTransportDrainWait`, sized to the
 * actual bytes just sent at a deliberately conservative assumed throughput
 * (`ftpDrainBytesPerSec`), computed directly from this session's own two
 * real gateway logs (~1495 bytes/sec measured in the second one). This
 * doesn't rely on any OS/driver cooperation at all - it just gives the
 * physical serial link literal wall-clock time to catch up before the
 * connection (and whatever's still in its queue) is torn down. Paced in
 * ~1-second increments with EvtResetAutoOffTimer() between them (a
 * multi-second blocking wait wasn't anticipated by any existing caller's
 * own auto-off handling - see BusyDialog.c's own comment on that
 * responsibility normally sitting with the UI layer, not this deep in the
 * protocol code). */
#define ftpLingerSeconds 30
#define ftpDrainBytesPerSec 1200	/* deliberately conservative - real logs measured ~1495 B/s */
#define ftpDrainMinSeconds 2
#define ftpDrainMaxSeconds 30

static void FtpTransportEnableLinger(NetSocketRef sock)
{
	NetSocketLingerType linger;
	Err err;
	linger.onOff = 1;
	linger.time = ftpLingerSeconds;
	NetLibSocketOptionSet(gFtp.netRefNum, sock, netSocketOptLevelSocket,
		netSocketOptSockLinger, &linger, sizeof(linger), ftpCtrlTimeoutTicks, &err);
}

static void FtpTransportDrainWait(UInt32 bytesSent)
{
	UInt32 seconds = bytesSent / ftpDrainBytesPerSec;
	UInt32 i;
	if (seconds < ftpDrainMinSeconds) seconds = ftpDrainMinSeconds;
	if (seconds > ftpDrainMaxSeconds) seconds = ftpDrainMaxSeconds;
	for (i = 0; i < seconds; i++) {
		EvtResetAutoOffTimer();
		SysTaskDelay(SysTicksPerSecond());
	}
}

/* Real bug found+fixed this session: a real (flaky SLIP-link) control-
 * connection read/send failure left every caller's `resp`/`textOut` buffer
 * completely untouched (both FtpReadReply's and FtpCmd's I/O-failure paths
 * just `return false` without ever writing to it) - since those buffers are
 * always plain stack locals at the CALL SITE (e.g. FtpEnterPasv's `char
 * resp[ftpReplyLen]`), an uninitialized read failure showed whatever
 * leftover stack bytes happened to be there, live-observed on real
 * hardware as "PASV: <junk characters>" and "CWD: CWD:" (the latter is
 * FtpCmd's own just-built command string leaking through via stack
 * reuse). Also: a genuinely dead control connection was never detected as
 * such - every subsequent command kept trying (and failing the same
 * confusing way) instead of the app recognizing "no longer connected."
 * Fix: (1) both I/O-failure paths below now always write a clear message
 * before returning false, (2) they now also tear down the connection state
 * via FtpMarkDisconnected() so FtpIsConnected() correctly reports false
 * afterward instead of leaving the app to keep hammering a dead socket. */
/* Real bug found+fixed this session (reported as "disconnect doesn't
 * help" + unexplained spurious traffic saturating the link even after
 * giving up on a folder): a single NetLibClose(refnum, false) only
 * DECREMENTS the open count and leaves the interface in a lazy
 * "close-wait" state - it does NOT actually tear the connection down.
 * This exact real-hardware gotcha is already documented in project memory
 * (palmos-netlib-udp-client-working-pattern.md, from EraNTP's own hard-won
 * lesson) but wasn't applied here: "a real user-facing Disconnect must
 * drain the whole count via NetLibClose(refnum, true) (immediate) in a
 * loop, then NetLibFinishCloseWait - skipping this left the OS still
 * showing Connected until the user manually disconnected via Prefs." The
 * SAME lingering-connection class of bug plausibly also explains the
 * "spurious traffic" - a connection PalmOS itself still considers half-open
 * can keep retransmitting in the background over the SLIP link long after
 * our own code has moved on and forgotten about it. */
static void FtpNetLibFullClose(void)
{
	UInt16 count = 0;
	Err err;
	if (NetLibOpenCount(gFtp.netRefNum, &count) != errNone) count = 1;
	while (count > 0) {
		NetLibClose(gFtp.netRefNum, true);	/* immediate, not graceful */
		count--;
	}
	err = NetLibFinishCloseWait(gFtp.netRefNum);
	(void) err;
}

static void FtpMarkDisconnected(void)
{
	if (gFtp.ctrlSock != -1) {
		FtpTransportClose(gFtp.ctrlSock);
		gFtp.ctrlSock = -1;
	}
	if (gFtp.netOpened) {
		FtpNetLibFullClose();
		gFtp.netOpened = false;
	}
	gFtp.connected = false;
}

/* ---- control-connection line/reply I/O --------------------------------
 * Real-hardware bug found this session: intermittent "Connection lost"
 * errors on the very first connect attempt to some servers (cleared up
 * after 2-3 retries) - initially suspected to be a wrong-password bug, but
 * a genuinely wrong password would fail every attempt, not intermittently.
 * This used to call FtpTransportReceive (a plain blocking NetLibReceive)
 * one byte at a time - the exact same real-device NetLibReceive timeout-
 * unreliability already found and fixed for the DATA connection (see
 * FtpPollReceive's own comment) was never ported to this, the CONTROL
 * connection's only read path, which is what the USER/PASS exchange (and
 * every other FTP command) goes through - an early/short/spurious read
 * here reads as "the connection died" even though the server was still
 * about to reply. Switched to the same FtpPollReceive fix used for the
 * data connection. */
static Boolean FtpReadLine(char *out, UInt16 outLen)
{
	UInt16 n = 0;
	char c;

	while (n + 1 < outLen) {
		Int16 got = FtpPollReceive(gFtp.ctrlSock, &c, 1, ftpCtrlTimeoutTicks, NULL, NULL);
		if (got != 1) { out[n] = '\0'; return n > 0; }
		if (c == '\n') break;
		if (c != '\r') out[n++] = c;
	}
	out[n] = '\0';
	return true;
}

/* Reads one reply (handling the "nnn-...\r\nnnn ...\r\n" multi-line
 * continuation convention), returns the 3-digit code and the LAST line's
 * text (matches the "command: response" two-line error alert design). */
static Boolean FtpReadReply(Int16 *codeOut, char *textOut, UInt16 textLen)
{
	char line[ftpReplyLen];
	Int16 code = -1;

	do {
		if (!FtpReadLine(line, sizeof(line))) {
			StrNCopy(textOut, "Connection lost", textLen - 1);
			textOut[textLen - 1] = '\0';
			FtpMarkDisconnected();
			return false;
		}
		if (StrLen(line) >= 4 &&
				line[0] >= '0' && line[0] <= '9' &&
				line[1] >= '0' && line[1] <= '9' &&
				line[2] >= '0' && line[2] <= '9') {
			code = (Int16) ((line[0] - '0') * 100 + (line[1] - '0') * 10 + (line[2] - '0'));
			if (line[3] == ' ') {
				StrNCopy(textOut, &line[4], textLen - 1);
				textOut[textLen - 1] = '\0';
				*codeOut = code;
				return true;
			}
			/* line[3] == '-': multi-line, keep reading until "code " (not "-") */
		}
	} while (true);
}

static Boolean FtpAutoReconnect(void);	/* defined below FtpConnect - see its own comment */

/* Returning `false` from FtpCmd means a TRANSPORT-level failure (send
 * failed, or no reply ever came back) - a legitimate FTP-level rejection
 * (bad password, file not found, etc.) still returns `true` here with the
 * real 4xx/5xx code in *codeOut, so this is exactly the right, narrow
 * signal for "the connection itself died" without misfiring on ordinary
 * command failures (important: this is also what keeps the auto-reconnect
 * retry below from spuriously triggering during FtpConnect's OWN normal
 * auth-rejection handling). On that signal, try ONE silent reconnect+retry
 * before giving up - see FtpAutoReconnect's own comment for the real-
 * hardware idle-disconnect motivation. */
static Boolean FtpCmd(const char *cmd, const char *arg, Int16 *codeOut,
	char *textOut, UInt16 textLen)
{
	/* Real-hardware crash found this session, with a precise differential:
	 * a MANUAL "Connect" (shallow call chain - DoConnect straight into
	 * FtpConnect) never crashed on the exact same real redial that
	 * reliably crashed when triggered AUTOMATICALLY (MainRefreshList ->
	 * FtpList -> FtpCmd -> FtpAutoReconnect -> FtpConnect - several more
	 * stack frames deep, several of them with their own sizeable local
	 * buffers). PalmOS's default app stack is a tiny ~4KB, and stack
	 * overflow from exactly this shape (large stack-local buffers piling
	 * up across a deep call chain) is an ALREADY-ESTABLISHED, previously-
	 * fixed bug class in this exact codebase (see FtpTransportOpen's own
	 * static NetHostInfoBufType, and the shared static gFtpChunkBuf used
	 * by FtpList/FtpRetrieve/FtpStore instead of stack-local buffers, both
	 * for the identical reason). `line` is one of several large locals in
	 * the specific chain that crashes (FtpCmd/FtpAutoReconnect/FtpConnect
	 * - see their own matching comments) - made static here, matching the
	 * same established fix. Safe: FtpCmd IS genuinely re-entered (the
	 * nested FtpAutoReconnect() call below can itself call FtpCmd again,
	 * for its own CWD navigate-back), but `line` is always fully rebuilt
	 * from this function's own `cmd`/`arg` parameters immediately before
	 * every use - both before AND after the nested call - so sharing the
	 * storage across invocations never reads stale/overwritten content. */
	static char line[ftpPathLen + 16];
	/* Also static, same reason as `line` above (this function sits in the
	 * exact deep re-entrant call chain that stack-overflowed once already -
	 * see this function's own header comment) - a log-line-formatting
	 * buffer is just as much a stack-local as `line` would be otherwise. */
	static char logLine[ftpReplyLen + 40];
	Boolean ok, isPass = (Boolean) (arg && StrCompare(cmd, "PASS") == 0);

	/* Password redacted - this log is meant to be read by the user
	 * debugging a connection, but never persists past the current run and
	 * could still be glanced at by someone else in the meantime. */
	if (isPass) StrCopy(logLine, "> PASS ****");
	else if (arg) StrPrintF(logLine, "> %s %s", cmd, arg);
	else StrPrintF(logLine, "> %s", cmd);
	LzTPLogWrite(logLine);

	if (arg) StrPrintF(line, "%s %s\r\n", cmd, arg);
	else StrPrintF(line, "%s\r\n", cmd);
	if (FtpTransportSend(gFtp.ctrlSock, line, (UInt16) StrLen(line)) == -1) {
		StrNCopy(textOut, "Connection lost", textLen - 1);
		textOut[textLen - 1] = '\0';
		FtpMarkDisconnected();
		LzTPLogWrite("! send failed - connection lost");
		ok = false;
	} else {
		ok = FtpReadReply(codeOut, textOut, textLen);
		if (ok) {
			StrPrintF(logLine, "< %d %s", (int) *codeOut, textOut);
			LzTPLogWrite(logLine);
		} else {
			LzTPLogWrite("! no reply - connection lost");
		}
	}

	if (!ok && !gFtpReconnecting && FtpAutoReconnect()) {
		LzTPLogWrite("~ retrying command after silent reconnect");
		if (arg) StrPrintF(line, "%s %s\r\n", cmd, arg);
		else StrPrintF(line, "%s\r\n", cmd);
		if (FtpTransportSend(gFtp.ctrlSock, line, (UInt16) StrLen(line)) == -1) {
			StrNCopy(textOut, "Connection lost", textLen - 1);
			textOut[textLen - 1] = '\0';
			FtpMarkDisconnected();
			LzTPLogWrite("! retry send failed - connection lost");
			return false;
		}
		ok = FtpReadReply(codeOut, textOut, textLen);
		if (ok) {
			StrPrintF(logLine, "< %d %s", (int) *codeOut, textOut);
			LzTPLogWrite(logLine);
		} else {
			LzTPLogWrite("! retry: no reply - connection lost");
		}
		return ok;
	}
	return ok;
}

SEC004 Boolean FtpNoop(void)
{
	Int16 code;
	char resp[ftpReplyLen];

	if (!FtpIsConnected()) return false;
	return FtpCmd("NOOP", NULL, &code, resp, sizeof(resp));
}

/* cmd/response-shaped failure message, matching the two-line error alert
 * (altErr2: "^1:\n^2") this suite's other apps already use for FTP-specific
 * errors. */
static void FtpFail(char *errMsg, UInt16 errMsgLen, const char *cmd, const char *resp)
{
	StrPrintF(errMsg, "%s: %s", cmd, resp);
	(void) errMsgLen;
}

/* ---- PASV data connection ---------------------------------------------
 * PASV-only by design (see FtpProtocol.h) - the server tells us an address
 * to connect TO, avoiding the "server connects back to the client" problem
 * that's unworkable behind the kind of NAT/SLIP link this suite has already
 * hit real trouble with (see palmos-netlib-udp-client-working-pattern). */
static Boolean FtpEnterPasv(NetSocketRef *dataSock, char *errMsg, UInt16 errMsgLen)
{
	Int16 code;
	char resp[ftpReplyLen];
	char host[ftpNameLen];
	UInt16 h[4], p[2];
	char *s;

	if (!FtpCmd("PASV", NULL, &code, resp, sizeof(resp)) || code != 227) {
		FtpFail(errMsg, errMsgLen, "PASV", resp);
		return false;
	}

	/* No sscanf-equivalent in this SDK's Str library - parse the 6
	 * comma-separated numbers by hand. */
	s = StrChr(resp, '(');
	if (s) s++;
	{
		UInt16 i;
		UInt16 *fields[6];
		fields[0] = &h[0]; fields[1] = &h[1]; fields[2] = &h[2];
		fields[3] = &h[3]; fields[4] = &p[0]; fields[5] = &p[1];
		for (i = 0; i < 6; i++) {
			UInt16 v = 0;
			Boolean any = false;
			if (!s) break;
			while (*s == ' ') s++;
			while (*s >= '0' && *s <= '9') { v = (UInt16) (v * 10 + (*s - '0')); s++; any = true; }
			if (!any) break;
			*fields[i] = v;
			if (*s == ',') s++;
		}
		if (i != 6) {
			StrPrintF(errMsg, "Could not parse PASV reply: %s", resp);
			return false;
		}
	}
	StrPrintF(host, "%d.%d.%d.%d", h[0], h[1], h[2], h[3]);

	*dataSock = FtpTransportOpen(host, (UInt16) (p[0] * 256 + p[1]), errMsg, errMsgLen);
	return *dataSock != -1;
}

/* ---- connection lifecycle ---------------------------------------------- */
SEC004 Boolean FtpIsConnected(void)
{
	return gFtp.connected;
}

SEC004 const char *FtpCurPath(void)
{
	return gFtp.curPath;
}

/* Builds an absolute path from a directory + a bare name - needed by Cut/Paste
 * (FileActions.c) to construct RNFR/RNTO arguments that survive navigating
 * away from the source directory before pasting. Nothing in this codebase
 * needed this before: ordinary CWD navigation only ever sends a bare
 * relative name to the server and re-derives the new absolute path via a
 * follow-up PWD (see FtpChangeDir) - it never had to build a path string
 * itself. `dir` is expected in FtpCurPath()'s own format (root is the
 * literal "/", no trailing slash otherwise - see FtpConnect's initial
 * `StrCopy(gFtp.curPath, "/")`), so only those two cases need handling to
 * avoid a double slash. Truncates (like every other StrNCopy-based path
 * build in this app) rather than failing outright on a pathological depth. */
SEC004 char *FtpJoinPath(const char *dir, const char *name, char *out, UInt16 outLen)
{
	/* Bounded by construction (StrNCat/StrNCopy, not StrPrintF - see
	 * strncat-strlcat-semantics-gotcha in project memory: StrNCat's size arg
	 * is the TOTAL destination size, not "remaining space", so passing
	 * outLen at every step is correct here, not a double-subtraction bug). */
	if (outLen == 0) return out;
	out[0] = '\0';
	if (dir[0] == '/' && dir[1] == '\0') {
		StrNCat(out, "/", outLen);
	} else {
		StrNCat(out, dir, outLen);
		StrNCat(out, "/", outLen);
	}
	StrNCat(out, name, outLen);
	return out;
}

SEC004 const char *FtpCurServer(void)
{
	return gFtp.server;
}

SEC004 UInt16 FtpCurPort(void)
{
	return gFtp.port;
}

SEC004 Boolean FtpConnect(const FtpConnParams *p, char *errMsg, UInt16 errMsgLen)
{
	/* static, not stack-local - see FtpCmd's own comment for the real
	 * stack-overflow bug this whole reconnect-chain pass fixes. Safe: the
	 * `gFtpReconnecting` guard (set before this function can ever be
	 * called from FtpAutoReconnect) prevents this function's own internal
	 * FtpCmd calls (USER/PASS/TYPE) from ever triggering a NESTED
	 * FtpConnect - there is only ever one active FtpConnect frame using
	 * this buffer at a time. */
	static char resp[ftpReplyLen];
	/* Same static-not-stack-local reasoning as `resp` above - see FtpCmd's
	 * own comment. Sized for errMsg (caller-supplied, up to ftpReplyLen)
	 * plus a short prefix, not just the short "Connecting to..." line - a
	 * couple of these log calls below format errMsg's own content in. */
	static char connLog[ftpReplyLen + 40];
	Int16 code;
	UInt16 openErr;
	Err err;
	const char *user = (p->user[0]) ? p->user : "anonymous";
	const char *pass = (p->user[0]) ? p->pass : "lazyftp@palm.local";

	errMsg[0] = '\0';
	MemSet(&gFtp, sizeof(gFtp), 0);
	gFtp.ctrlSock = -1;
	StrNCopy(gFtp.server, p->server, sizeof(gFtp.server) - 1);
	gFtp.port = p->port;

	StrPrintF(connLog, "Connecting to %s:%d", p->server, (int) p->port);
	LzTPLogWrite(connLog);

	/* A full connect is several separate blocking calls chained together
	 * (socket open+connect, USER, PASS, TYPE, each up to ftpCtrlTimeoutTicks)
	 * - even if each individual NetLib call is well-behaved regarding
	 * auto-off, resetting explicitly at each major step boundary is cheap
	 * insurance against the accumulated total running long on a slow/flaky
	 * real link, matching this suite's established pattern for any
	 * operation that can take this long overall (see FtpPollReceive's own
	 * comment for the specific real-hardware crash this session that this
	 * mirrors). */
	EvtResetAutoOffTimer();

	if (SysLibFind("Net.lib", &gFtp.netRefNum) != errNone) {
		StrNCopy(errMsg, "Net.lib not found", errMsgLen - 1);
		return false;
	}
	err = NetLibOpen(gFtp.netRefNum, &openErr);
	if (openErr != errNone || (err != errNone && err != netErrAlreadyOpen)) {
		StrPrintF(errMsg, "Could not open network (err=%ld)", (long) err);
		return false;
	}
	gFtp.netOpened = true;

	/* Retry loop: only for a TRANSPORT-level failure (socket/DNS open, or
	 * the greeting banner never arriving) - see FtpConnParams.retryCount's
	 * own comment for the real-hardware motivation (intermittent
	 * "Connection lost" right after a successful TCP connect, on servers
	 * that connect fine from a PC every time - points at a NAT/relay race
	 * on the SLIP side, not the server). A real auth rejection (bad
	 * password etc.) is a DIFFERENT failure class handled further below,
	 * after this loop, and is deliberately NOT retried here - retrying a
	 * wrong password can't ever succeed, it would just make the user wait
	 * out N pointless 5-second delays before the same alert they'd have
	 * gotten immediately. */
	{
		UInt8 attempt;
		Boolean bannerOk = false;

		for (attempt = 0; attempt <= p->retryCount; attempt++) {
			if (attempt > 0) {
				UInt8 secLeft;
				for (secLeft = ftpRetryIntervalSecs; secLeft > 0; secLeft--) {
					if (gFtpConnRetryHook) {
						StrPrintF(connLog, "Retry %d in %ds...", (int) attempt, (int) secLeft);
						gFtpConnRetryHook(connLog);
					}
					EvtResetAutoOffTimer();
					SysTaskDelay(SysTicksPerSecond());
				}

				/* Real-hardware finding: the underlying SLIP link can drop
				 * WHILE this loop is waiting between failed attempts -
				 * confirmed via the Logs feature showing the SUBSEQUENT
				 * attempt fail immediately with netErrNotOpen (network not
				 * open), not a fresh timeout. This is the same class of
				 * problem FtpAutoReconnect already handles for an
				 * ESTABLISHED session, but that mechanism's own guard
				 * (`!gFtp.user[0]`) deliberately refuses to help during the
				 * INITIAL connect, since there's no prior working session to
				 * resume yet - so a dedicated force-reopen belongs here.
				 * Unconditional (not "only if it looks dead") because a
				 * clean way to probe real link liveness isn't exposed here,
				 * and the user's own report was specific: it should just
				 * always try to reconnect before each retry. Busy overlay
				 * torn down FIRST (NULL title - see LzTPBusyRetitle's own
				 * doc comment) before touching the network at all: the exact
				 * same reasoning LzTPBusyStatusHook already established -
				 * a real crash happened once from this overlay colliding
				 * with PalmOS's own system-level redial UI needing the same
				 * screen region at the same moment. */
				if (gFtpConnRetryHook) gFtpConnRetryHook(NULL);
				if (gFtp.netOpened) {
					FtpNetLibFullClose();
					gFtp.netOpened = false;
				}
				err = NetLibOpen(gFtp.netRefNum, &openErr);
				if (openErr == errNone && (err == errNone || err == netErrAlreadyOpen)) {
					gFtp.netOpened = true;
				} else {
					StrPrintF(connLog, "! could not reopen network for retry (err=%ld)", (long) err);
					LzTPLogWrite(connLog);
					/* gFtp.netOpened stays false - the FtpTransportOpen call
					 * below will fail fast too, logged the same as any other
					 * failed attempt; the loop just moves on from there. */
				}

				if (gFtpConnRetryHook) gFtpConnRetryHook("Connecting...");
				StrPrintF(connLog, "~ retry attempt %d of %d", (int) (attempt + 1),
					(int) (p->retryCount + 1));
				LzTPLogWrite(connLog);
			}

			gFtp.ctrlSock = FtpTransportOpen(p->server, p->port, errMsg, errMsgLen);
			if (gFtp.ctrlSock == -1) {
				StrPrintF(connLog, "! socket open failed: %s", errMsg);
				LzTPLogWrite(connLog);
				continue;
			}

			if (!FtpReadReply(&code, resp, sizeof(resp)) || code != 220) {
				FtpFail(errMsg, errMsgLen, "CONNECT", resp);
				StrPrintF(connLog, "! %s", errMsg);
				LzTPLogWrite(connLog);
				FtpTransportClose(gFtp.ctrlSock);
				gFtp.ctrlSock = -1;
				continue;
			}

			StrPrintF(connLog, "< %d %s", (int) code, resp);
			LzTPLogWrite(connLog);
			bannerOk = true;
			break;
		}
		if (!bannerOk) goto fail;
	}

	EvtResetAutoOffTimer();	/* socket open + connect can alone eat a big chunk of the budget */

	if (!FtpCmd("USER", user, &code, resp, sizeof(resp))) goto fail_io;
	if (code == 331) {
		if (!FtpCmd("PASS", pass, &code, resp, sizeof(resp))) goto fail_io;
	}
	if (code != 230) {
		/* The classic "wrong password" (or wrong username) case - FtpCmd
		 * already logged the USER/PASS exchange itself (password redacted),
		 * this makes the actual rejection reason easy to find without
		 * having to scan back through every "< nnn ..." line for the one
		 * that isn't 230/331. */
		FtpFail(errMsg, errMsgLen, "USER/PASS", resp);
		StrPrintF(connLog, "! login rejected: %s", errMsg);
		LzTPLogWrite(connLog);
		goto fail;
	}

	if (!FtpCmd("TYPE", "I", &code, resp, sizeof(resp)) || code != 200) {
		FtpFail(errMsg, errMsgLen, "TYPE", resp);
		StrPrintF(connLog, "! %s", errMsg);
		LzTPLogWrite(connLog);
		goto fail;
	}

	StrCopy(gFtp.curPath, "/");
	/* Saved only HERE, at the moment of full success - see FtpState's own
	 * comment for why this is the exact signal FtpAutoReconnect relies on
	 * to distinguish "we had a real session" from "this very attempt just
	 * failed" (including a nested attempt made BY FtpAutoReconnect itself). */
	StrNCopy(gFtp.user, user, sizeof(gFtp.user) - 1);
	StrNCopy(gFtp.pass, pass, sizeof(gFtp.pass) - 1);
	gFtp.connected = true;
	LzTPLogWrite("Connected");
	return true;

fail_io:
	StrNCopy(errMsg, "Connection lost", errMsgLen - 1);
fail:
	FtpTransportClose(gFtp.ctrlSock);
	gFtp.ctrlSock = -1;
	if (gFtp.netOpened) { FtpNetLibFullClose(); gFtp.netOpened = false; }
	return false;
}

/* Real-hardware concern (user-raised): PalmOS/the underlying network driver
 * can silently tear down an idle PPP/SLIP link on its own timeout,
 * independent of this app's own gFtp.connected bookkeeping -
 * FtpIsConnected() would keep reporting "connected" (wrong network-
 * availability expectations) right up until the next command's transport-
 * level failure reveals the truth. Rather than just surfacing that failure
 * and making the user manually hit Connect again, FtpCmd (the one function
 * nearly every FTP operation funnels its wire-level exchange through) now
 * calls this to attempt exactly ONE silent reconnect - using the same
 * server/port/user/pass that got us connected in the first place - and
 * re-navigates back to the directory the user was actually browsing before
 * the caller retries its original failed command once. */
static Boolean FtpAutoReconnect(void)
{
	/* All four of this function's own locals are static, not stack-local -
	 * see FtpCmd's own comment for the real stack-overflow bug this whole
	 * pass fixes (this function in particular sits in the MIDDLE of the
	 * exact chain that crashed: FtpCmd -> HERE -> FtpConnect, and on its
	 * own already carried ~700+ bytes of stack-local buffers - a bigger
	 * single contributor than any other function in the chain). Safe: the
	 * `gFtpReconnecting` guard immediately below means only ever one
	 * active invocation of this function exists at a time - no reentrancy
	 * risk at all, unlike FtpCmd's own more nuanced case. */
	static FtpConnParams p;
	static char errMsg[ftpReplyLen];
	static char savedPath[ftpPathLen];
	Boolean ok;

	if (gFtpReconnecting) return false;
	if (!gFtp.user[0]) return false;	/* never had a fully-successful session to resume */

	gFtpReconnecting = true;
	LzTPLogWrite("Reconnecting (idle link silently dropped?)...");
	if (gFtpStatusHook) gFtpStatusHook("Reconnecting...");
	StrNCopy(savedPath, gFtp.curPath, sizeof(savedPath) - 1);
	savedPath[sizeof(savedPath) - 1] = '\0';

	MemSet(&p, sizeof(p), 0);
	StrNCopy(p.server, gFtp.server, sizeof(p.server) - 1);
	p.port = gFtp.port;
	StrNCopy(p.user, gFtp.user, sizeof(p.user) - 1);
	StrNCopy(p.pass, gFtp.pass, sizeof(p.pass) - 1);

	ok = FtpConnect(&p, errMsg, sizeof(errMsg));
	if (ok && StrCompare(savedPath, "/") != 0) {
		Int16 code;
		static char resp[ftpReplyLen];
		EvtResetAutoOffTimer();	/* the CWD below is one more blocking step after FtpConnect's own already-reset budget */
		/* Best-effort: if navigating back fails (e.g. the folder was
		 * deleted server-side while we were disconnected), just stay at
		 * root rather than treating the whole reconnect as a failure - the
		 * caller's own retried command will surface any real problem. */
		if (FtpCmd("CWD", savedPath, &code, resp, sizeof(resp)) && code == 250) {
			StrNCopy(gFtp.curPath, savedPath, sizeof(gFtp.curPath) - 1);
		}
	}
	gFtpReconnecting = false;
	return ok;
}

/* Closes the FTP SESSION only - sends QUIT, closes the control socket,
 * clears connected state - but deliberately leaves the underlying Net.lib
 * connection (gFtp.netOpened) alone. The "Disconnect Server" counterpart to
 * "Disconnect Network" (FtpDisconnectNetwork) below - lets the user drop
 * back to a clean, disconnected screen without tearing down the actual
 * SLIP/PPP link, which on real hardware can itself take a while to
 * re-establish (dial, handshake) - a cost this should never force on
 * someone who just wants to leave one server and connect to another. */
SEC004 void FtpDisconnectServer(void)
{
	Int16 code;
	char resp[ftpReplyLen];

	if (gFtp.ctrlSock != -1) {
		FtpCmd("QUIT", NULL, &code, resp, sizeof(resp));	/* best-effort */
		FtpTransportClose(gFtp.ctrlSock);
		gFtp.ctrlSock = -1;
	}
	gFtp.connected = false;
	/* Explicit user-initiated disconnect "forgets" the session - an
	 * empty gFtp.user disables FtpAutoReconnect until the user manually
	 * reconnects again, so it never fights a deliberate Disconnect. */
	gFtp.user[0] = '\0';
	gFtp.pass[0] = '\0';
}

/* Closes the server session (if still open) AND tears down the actual
 * underlying network connection - the stronger of the two disconnect
 * actions (see FtpDisconnectServer's own comment for why they're separate
 * at all). There is no such thing as "network disconnected but still
 * connected to a server" - closing the server session first is not
 * optional, just sequenced before the network goes away under it. */
SEC004 void FtpDisconnectNetwork(void)
{
	if (gFtp.ctrlSock != -1) FtpDisconnectServer();
	if (gFtp.netOpened) {
		FtpNetLibFullClose();
		gFtp.netOpened = false;
	}
}

/* ---- directory operations ----------------------------------------------
 * Unix `ls -l` format only (known v1 limitation, matches the axxpac
 * reference's own scope) - anchored on the fixed token layout
 * (perms links owner group size month day time-or-year name...) rather
 * than a naive last-whitespace split, so filenames with embedded spaces
 * still come through intact.
 *
 * Field separator is whitespace - SPACE *or* TAB. Real-device bug found
 * testing against WiRSa's own vendored FTP server (MultiFTPServer): its
 * LIST lines are tab-delimited between fields (e.g.
 * "drwxrwsr-x\t2\t<user>\t4096\t<time>\t<name>"), unlike every
 * space-padded Unix server this parser had been tested against before.
 * The old space-only `*s == ' '`/`*s != ' '` checks never matched a tab,
 * so the "skip permissions token" loop ran straight past every tab-joined
 * field (nothing stopped it until the first literal space, deep inside
 * the time-of-day sub-field) and the whole line failed to parse - the
 * data arrived and was ACKed fine over the wire, it just silently
 * produced zero entries, which looked like "the server responded but
 * lazyFTP shows no listing". */
#define FTP_IS_WS(c) ((c) == ' ' || (c) == '\t')

/* True if [b,e) is a non-empty run of ASCII digits. */
static Boolean FtpTokIsDigits(const char *b, const char *e)
{
	if (b == e) return false;
	while (b < e) {
		if (*b < '0' || *b > '9') return false;
		b++;
	}
	return true;
}

/* True if [b,e) is a 3-letter English month abbreviation (case-insensitive) -
 * "Jan".."Dec". Used as the parse anchor below: the size/date/name tail of
 * a `ls -l` line is otherwise ambiguous in field COUNT (see below), but a
 * month abbreviation is essentially unmistakable for anything else that
 * legitimately appears in this position. */
static Boolean FtpTokIsMonth(const char *b, const char *e)
{
	static const char *months = "janfebmaraprmayjunjulaugsepoctnovdec";
	char lc[3];
	UInt16 i;
	if ((e - b) != 3) return false;
	for (i = 0; i < 3; i++) {
		char c = b[i];
		if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
		lc[i] = c;
	}
	for (i = 0; i < 12; i++) {
		if (lc[0] == months[i * 3] && lc[1] == months[i * 3 + 1] && lc[2] == months[i * 3 + 2]) return true;
	}
	return false;
}

/* ---- directory operations ----------------------------------------------
 * Unix `ls -l` format only (known v1 limitation, matches the axxpac
 * reference's own scope) - anchored on a fixed OVERALL layout (perms links
 * <name fields> size month day time-or-year name...) rather than a naive
 * last-whitespace split, so filenames with embedded spaces still come
 * through intact.
 *
 * Field separator is whitespace - SPACE *or* TAB (see FTP_IS_WS's own
 * definition site for the real-device tab-delimited-server bug that
 * required this).
 *
 * The number of "name" fields between the link count and the size varies
 * by server and is NOT hardcoded: standard Unix `ls -l` (vsftpd, proftpd,
 * etc.) emits owner AND group (2 fields); WiRSa's own vendored FTP server
 * (MultiFTPServer's generateFileLine()) emits only a single user field, no
 * group. A fixed-count skip is correct for one and silently wrong for the
 * other (real-device bug: size always parsed as 0 talking to WiRSa's own
 * server, because the fixed-3-token skip consumed the size token itself as
 * if it were "group"). Some servers additionally show a raw numeric UID
 * instead of a resolved username when the name can't be looked up, so
 * "the first all-digit token after the link count" is ALSO not reliable
 * (that UID would look exactly like a size).
 *
 * Fix: scan forward from the link count for a token matching a month
 * abbreviation (Jan..Dec) - this is the one token in the whole line that
 * cannot plausibly be confused with an owner/group name or a numeric UID.
 * Whatever token immediately precedes it is the size, however many name
 * fields came before that (0, 1, 2, or more). This correctly handles both
 * server dialects above plus the numeric-UID case, with no server-specific
 * special-casing. Known remaining limitation (unchanged from before, still
 * within the documented "ls -l format only" scope): servers using a
 * non-English or purely numeric/ISO date column (no month abbreviation at
 * all) won't match and the line is rejected rather than misparsed. */
static Boolean FtpParseListLine(const char *line, FtpDirEntry *e)
{
	const char *s = line;
	const char *tokStart, *tokEnd;
	const char *sizeStart = NULL, *sizeEnd = NULL;
	UInt32 size;

	if (!*s) return false;
	e->is_dir = (line[0] == 'd');

	/* skip permissions token */
	while (*s && !FTP_IS_WS(*s)) s++;
	/* skip the link-count token */
	while (FTP_IS_WS(*s)) s++;
	while (*s && !FTP_IS_WS(*s)) s++;

	/* Scan tokens looking for the month anchor; remember each token seen
	 * along the way as the current best guess for "size" (the token right
	 * before the month is the real one). */
	for (;;) {
		while (FTP_IS_WS(*s)) s++;
		tokStart = s;
		while (*s && !FTP_IS_WS(*s)) s++;
		tokEnd = s;
		if (tokStart == tokEnd) return false;		/* ran out of line, no month found */
		if (FtpTokIsMonth(tokStart, tokEnd)) break;	/* found the anchor */
		sizeStart = tokStart;
		sizeEnd = tokEnd;
	}
	if (!sizeStart || !FtpTokIsDigits(sizeStart, sizeEnd)) return false;	/* malformed - no numeric size before the month */
	size = 0;
	{
		const char *p = sizeStart;
		while (p < sizeEnd) { size = size * 10 + (UInt32) (*p - '0'); p++; }
	}
	e->size = size;

	/* skip day, time-or-year (2 more whitespace-separated tokens - the
	 * month token itself was already consumed by the anchor search above) */
	{
		UInt16 tok;
		for (tok = 0; tok < 2; tok++) {
			while (FTP_IS_WS(*s)) s++;
			while (*s && !FTP_IS_WS(*s)) s++;
		}
	}
	while (FTP_IS_WS(*s)) s++;
	if (!*s) return false;

	StrNCopy(e->name, s, ftpNameLen - 1);
	e->name[ftpNameLen - 1] = '\0';
	return true;
}

SEC004 Boolean FtpList(Boolean *outTruncated, char *errMsg, UInt16 errMsgLen)
{
	NetSocketRef dataSock;
	Int16 code;
	/* static, not stack-local - this function sits directly above FtpCmd
	 * in the exact call chain a real stack-overflow crash was found in
	 * this session (see FtpCmd's own comment) - FtpList is never
	 * re-entered (its own FtpCmd calls' nested auto-reconnect never calls
	 * back into FtpList), so sharing this storage is safe and every byte
	 * trimmed off THIS function's own frame is extra headroom for
	 * whatever gets called several frames further down. */
	static char resp[ftpReplyLen];
	char *buf = gFtpChunkBuf;	/* too big for the stack - see the real-device
								   crash this caused; shared across FtpList/
								   FtpRetrieve/FtpStore, see its own comment */
	static char line[ftpReplyLen];
	UInt16 lineLen = 0;
	Int16 n;
	Err recvErr = 0;
	Boolean ok = true;
	UInt16 pollCount = 0;
	/* Real-device diagnostic timing (2026-08-21): WiRSa's own gateway log
	 * confirmed it delivers a full listing and closes correctly within
	 * milliseconds, then correctly detects and replies to the client's own
	 * (very late) FIN - yet the observed end-to-end delay before lazyFTP
	 * reports an error has been 60-130+ seconds, well past FtpPollReceive's
	 * own ~60s budget, with no way to see from the WiRSa side alone where
	 * the REST of that time goes (FtpTransportClose's own up-to-15s
	 * "graceful close" wait? FtpReadReply's up-to-15s wait for the control
	 * connection's "226" reply, if that's ALSO delayed for some reason? Real
	 * user reaction time reading an alert before the next tap?). lazyFTP has
	 * no separate debug channel WiRSa's own serial log can show (the same
	 * physical serial port is busy carrying the FTP session itself), so on
	 * a failure this measures each step and reports the breakdown directly
	 * in the SAME error alert the user already sees and can report back. */
	UInt32 tStart = TimGetTicks();
	UInt32 tPasvDone, tListCmdDone, tDataLoopDone, tCloseDone, tReplyDone;
	UInt32 entriesSeen = 0;

	if (outTruncated) *outTruncated = false;

	/* Fast path: an on-device cache already represents this exact
	 * directory and hasn't gone stale - skip the network fetch entirely
	 * (see Listing.c's ListingCacheValidFor/ActivateExisting and the
	 * user's own design: large directories spill to LazyFTPDBCache so
	 * ordinary page/filter navigation never needs to re-fetch). */
	if (ListingCacheValidFor(gFtp.server, gFtp.port, gFtp.curPath)) {
		ListingCacheActivateExisting(gFtp.server, gFtp.port, gFtp.curPath);
		return true;
	}

	ListingIngestBegin(gFtp.server, gFtp.port, gFtp.curPath);
	if (!FtpEnterPasv(&dataSock, errMsg, errMsgLen)) return false;
	tPasvDone = TimGetTicks();

	if (!FtpCmd("LIST", NULL, &code, resp, sizeof(resp)) || (code != 150 && code != 125)) {
		FtpFail(errMsg, errMsgLen, "LIST", resp);
		FtpTransportClose(dataSock);
		return false;
	}
	tListCmdDone = TimGetTicks();

	/* Real bug found this session (reported as "listing long directories
	 * fails" on a real, flaky link): NetLibReceive returns 0 on a genuine
	 * EOF (the server closing the data connection after sending the full
	 * listing - the correct, expected way this loop should end) but -1 on
	 * a real error INCLUDING A TIMEOUT (see NetMgr.h's own doc comment:
	 * "Returns # of bytes received, or -1 on error"). The old `while (n >
	 * 0)` loop treated both identically, so a single slow/stalled read
	 * partway through a LONG listing (more reads => more chances to hit
	 * one on a real link) silently truncated the results and still
	 * returned success - a longer directory is exactly what makes this
	 * more likely to trigger, matching the reported symptom. Now a real
	 * error is reported as such instead of silently swallowed.
	 *
	 * SECOND real bug found right after, from real-device retest: the
	 * first fix for the above `return false`d IMMEDIATELY on a data error,
	 * skipping the FtpReadReply() call below entirely. But the FTP SERVER
	 * still sends its final "226 Transfer complete" reply on the CONTROL
	 * connection regardless of whether OUR data read succeeded - it has no
	 * idea our end had trouble. Skipping that read left it sitting unread
	 * in the control socket's receive queue, so the VERY NEXT command
	 * (e.g. the CWD from tapping a folder) read THAT STALE reply instead
	 * of its own, permanently desyncing the whole control-connection
	 * lockstep from then on - explains "connection lost"/"no such file or
	 * directory"/"complete" all firing on totally unrelated subsequent
	 * commands, and why only a full reconnect (which resets everything)
	 * recovered. Fix: never return early out of this loop - always fall
	 * through to the shared cleanup below so the control reply is drained
	 * no matter how the data read went, matching FtpRetrieve's existing
	 * (already correct) shape. */
	do {
		n = FtpPollReceive(dataSock, buf, ftpChunkLen, ftpDataTimeoutTicks, &recvErr, &pollCount);
		if (n > 0) {
			Int16 i;
			for (i = 0; i < n; i++) {
				char c = buf[i];
				if (c == '\n') {
					line[lineLen] = '\0';
					if (lineLen > 0) {
						FtpDirEntry e;
						if (FtpParseListLine(line, &e)) {
							entriesSeen++;
							if (!ListingIngestEntry(&e) && outTruncated) *outTruncated = true;
						}
					}
					lineLen = 0;
				} else if (c != '\r' && lineLen + 1 < sizeof(line)) {
					line[lineLen++] = c;
				}
			}
		} else if (n < 0 && ok) {
			/* Data-connection-only failure - the CONTROL connection is a
			 * separate socket and is very possibly still fine (e.g. the
			 * server was just slow generating a long listing), so this
			 * deliberately does NOT tear down the whole session the way a
			 * control-connection I/O failure does - the user can just
			 * retry the same folder without a full reconnect. errMsg itself
			 * is built AFTER the timing checkpoints below, once the full
			 * breakdown is available - just remember that we failed here. */
			ok = false;
		}
	} while (n > 0);
	tDataLoopDone = TimGetTicks();

	FtpTransportClose(dataSock);
	tCloseDone = TimGetTicks();
	FtpReadReply(&code, resp, sizeof(resp));	/* 226 completion - MUST always be
		read regardless of `ok`, to keep the control connection's reply
		lockstep in sync for whatever command comes next (see comment above) */
	tReplyDone = TimGetTicks();

	ListingIngestEnd();

	if (!ok) {
		UInt16 tps = SysTicksPerSecond();
		StrPrintF(errMsg,
			"LIST: data error (err=%ld) got=%lu entries polls=%u\n"
			"pasv=%lums cmd=%lums data=%lums close=%lums reply=%lums total=%lums",
			(long) recvErr, (unsigned long) entriesSeen, (unsigned int) pollCount,
			(unsigned long) ((tPasvDone - tStart) * 1000 / tps),
			(unsigned long) ((tListCmdDone - tPasvDone) * 1000 / tps),
			(unsigned long) ((tDataLoopDone - tListCmdDone) * 1000 / tps),
			(unsigned long) ((tCloseDone - tDataLoopDone) * 1000 / tps),
			(unsigned long) ((tReplyDone - tCloseDone) * 1000 / tps),
			(unsigned long) ((tReplyDone - tStart) * 1000 / tps));
	}
	return ok;
}

SEC004 Boolean FtpChangeDir(const char *name, char *errMsg, UInt16 errMsgLen)
{
	Int16 code;
	char resp[ftpReplyLen];

	if (!FtpCmd("CWD", name, &code, resp, sizeof(resp)) || code != 250) {
		FtpFail(errMsg, errMsgLen, "CWD", resp);
		return false;
	}
	if (FtpCmd("PWD", NULL, &code, resp, sizeof(resp)) && code == 257) {
		char *a = StrChr(resp, '\"');
		if (a) {
			char *b = StrChr(a + 1, '\"');
			if (b) {
				UInt16 len = (UInt16) (b - a - 1);
				if (len >= ftpPathLen) len = ftpPathLen - 1;
				MemMove(gFtp.curPath, a + 1, len);
				gFtp.curPath[len] = '\0';
			}
		}
	}
	return true;
}

SEC004 Boolean FtpParentDir(char *errMsg, UInt16 errMsgLen)
{
	return FtpChangeDir("..", errMsg, errMsgLen);
}

SEC004 Boolean FtpMakeDir(const char *name, char *errMsg, UInt16 errMsgLen)
{
	Int16 code;
	char resp[ftpReplyLen];

	if (!FtpCmd("MKD", name, &code, resp, sizeof(resp)) || code != 257) {
		FtpFail(errMsg, errMsgLen, "MKD", resp);
		return false;
	}
	return true;
}

SEC004 Boolean FtpDelete(const char *name, Boolean isDir, char *errMsg, UInt16 errMsgLen)
{
	Int16 code;
	char resp[ftpReplyLen];
	const char *cmd = isDir ? "RMD" : "DELE";

	if (!FtpCmd(cmd, name, &code, resp, sizeof(resp)) || code != 250) {
		FtpFail(errMsg, errMsgLen, cmd, resp);
		return false;
	}
	return true;
}

#define ftpDeleteMaxEntriesPerLevel 128
#define ftpDeleteMaxDepth 12

/* One directory LEVEL's raw LIST, heap-allocated (freed by the caller) -
 * deliberately separate from FtpList/Listing.c's own UI-facing listing and
 * on-device cache machinery entirely; this is a one-shot, backend-only
 * enumeration for the recursive delete below, and reusing FtpList would
 * clobber whatever cache/paging state the UI's OWN listing is relying on
 * mid-operation (this can visit many directories the user never actually
 * navigates to). Capped at ftpDeleteMaxEntriesPerLevel - a real directory
 * with more entries than that at one level only gets partially processed,
 * reported honestly via *outTruncated rather than silently dropped. */
static Boolean CollectListLevel(FtpDirEntry **outEntries, UInt16 *outCount,
	Boolean *outTruncated, char *errMsg, UInt16 errMsgLen)
{
	NetSocketRef dataSock;
	Int16 code;
	char resp[ftpReplyLen];
	char *buf = gFtpChunkBuf;
	char line[ftpReplyLen];
	UInt16 lineLen = 0;
	Int16 n;
	Err recvErr = 0;
	Boolean ok = true;
	FtpDirEntry *entries;
	UInt16 count = 0;

	*outTruncated = false;
	entries = (FtpDirEntry *) MemPtrNew((UInt32) ftpDeleteMaxEntriesPerLevel * sizeof(FtpDirEntry));
	if (!entries) {
		StrNCopy(errMsg, "Out of memory", errMsgLen - 1);
		return false;
	}

	if (!FtpEnterPasv(&dataSock, errMsg, errMsgLen)) { MemPtrFree(entries); return false; }
	if (!FtpCmd("LIST", NULL, &code, resp, sizeof(resp)) || (code != 150 && code != 125)) {
		FtpFail(errMsg, errMsgLen, "LIST", resp);
		FtpTransportClose(dataSock);
		MemPtrFree(entries);
		return false;
	}

	do {
		n = FtpPollReceive(dataSock, buf, ftpChunkLen, ftpDataTimeoutTicks, &recvErr, NULL);
		if (n > 0) {
			Int16 i;
			for (i = 0; i < n; i++) {
				char c = buf[i];
				if (c == '\n') {
					line[lineLen] = '\0';
					if (lineLen > 0) {
						FtpDirEntry e;
						if (FtpParseListLine(line, &e) &&
							StrCompare(e.name, ".") != 0 && StrCompare(e.name, "..") != 0) {
							if (count < ftpDeleteMaxEntriesPerLevel) {
								entries[count++] = e;
							} else {
								*outTruncated = true;
							}
						}
					}
					lineLen = 0;
				} else if (c != '\r' && lineLen + 1 < sizeof(line)) {
					line[lineLen++] = c;
				}
			}
		} else if (n < 0 && ok) {
			ok = false;
			StrPrintF(errMsg, "LIST: data connection error (err=%ld)", (long) recvErr);
		}
	} while (n > 0);

	FtpTransportClose(dataSock);
	FtpReadReply(&code, resp, sizeof(resp));

	if (!ok) {
		MemPtrFree(entries);
		return false;
	}
	*outEntries = entries;
	*outCount = count;
	return true;
}

/* Assumes the caller has already CWD'd INTO the directory to empty. Deletes
 * every file directly, recurses into every subdirectory (descending via
 * CWD, then RMD's it once empty on the way back out) - depth-capped
 * (ftpDeleteMaxDepth) against a pathological/circular structure. Always
 * tries to navigate back up to where it started even if something failed
 * partway, so the connection isn't left stranded deep inside a partially-
 * deleted tree. */
static Boolean DeleteDirRecursive(UInt16 depth, Boolean *outTruncated, char *errMsg, UInt16 errMsgLen)
{
	FtpDirEntry *entries;
	UInt16 count, i;
	Boolean levelTruncated;
	Boolean ok = true;

	if (depth >= ftpDeleteMaxDepth) {
		StrNCopy(errMsg, "Folder structure too deep to delete", errMsgLen - 1);
		return false;
	}

	if (!CollectListLevel(&entries, &count, &levelTruncated, errMsg, errMsgLen)) return false;
	if (levelTruncated && outTruncated) *outTruncated = true;

	for (i = 0; i < count; i++) {
		if (!ok) break;
		if (entries[i].is_dir) {
			char cwdErr[ftpReplyLen];
			if (!FtpChangeDir(entries[i].name, cwdErr, sizeof(cwdErr))) {
				StrNCopy(errMsg, cwdErr, errMsgLen - 1);
				ok = false;
				break;
			}
			ok = DeleteDirRecursive((UInt16) (depth + 1), outTruncated, errMsg, errMsgLen);
			{
				char upErr[ftpReplyLen];
				if (!FtpParentDir(upErr, sizeof(upErr))) {
					/* Always attempt to recover navigation state regardless
					 * of 'ok' - but don't let a successful recovery's
					 * non-error clobber a real earlier failure reason, and
					 * don't let a recovery FAILURE overwrite a more
					 * specific one either. */
					if (ok) { StrNCopy(errMsg, upErr, errMsgLen - 1); ok = false; }
				}
			}
			if (ok && !FtpDelete(entries[i].name, true, errMsg, errMsgLen)) ok = false;
		} else {
			if (!FtpDelete(entries[i].name, false, errMsg, errMsgLen)) ok = false;
		}
	}

	MemPtrFree(entries);
	return ok;
}

SEC004 Boolean FtpDeleteCurrentFolderRecursive(Boolean *outTruncated, char *errMsg, UInt16 errMsgLen)
{
	char myName[ftpNameLen];
	const char *path;
	Int16 i;
	Int16 lastSlash = -1;
	Boolean ok;

	if (outTruncated) *outTruncated = false;

	/* Need our OWN name (relative to the parent) before descending, so we
	 * can RMD it by name once we've climbed back out - FtpCurPath() won't
	 * still say this once FtpParentDir() below has run. */
	path = FtpCurPath();
	for (i = 0; path[i]; i++) {
		if (path[i] == '/') lastSlash = i;
	}
	if (lastSlash < 0 || !path[lastSlash + 1]) {
		StrNCopy(errMsg, "Cannot delete the root directory", errMsgLen - 1);
		return false;
	}
	StrNCopy(myName, path + lastSlash + 1, sizeof(myName) - 1);
	myName[sizeof(myName) - 1] = '\0';

	ok = DeleteDirRecursive(0, outTruncated, errMsg, errMsgLen);

	{
		char upErr[ftpReplyLen];
		if (!FtpParentDir(upErr, sizeof(upErr))) {
			if (ok) StrNCopy(errMsg, upErr, errMsgLen - 1);
			return false;
		}
	}
	if (!ok) return false;
	return FtpDelete(myName, true, errMsg, errMsgLen);
}

SEC004 Boolean FtpRename(const char *oldName, const char *newName, char *errMsg, UInt16 errMsgLen)
{
	Int16 code;
	char resp[ftpReplyLen];

	if (!FtpCmd("RNFR", oldName, &code, resp, sizeof(resp)) || code != 350) {
		FtpFail(errMsg, errMsgLen, "RNFR", resp);
		return false;
	}
	if (!FtpCmd("RNTO", newName, &code, resp, sizeof(resp)) || code != 250) {
		FtpFail(errMsg, errMsgLen, "RNTO", resp);
		return false;
	}
	return true;
}

/* ---- file transfer ------------------------------------------------------
 * recvProc/sendProc are called once per chunk (up to ftpChunkLen bytes),
 * matching ExgDBReadProcPtr/ExgDBWriteProcPtr's own shape so Download.c/
 * Upload.c can bridge directly to ExgDBRead/ExgDBWrite - see FtpProtocol.h. */
SEC004 Boolean FtpRetrieve(const char *name, FtpRecvProc recvProc, void *userData,
	char *errMsg, UInt16 errMsgLen)
{
	NetSocketRef dataSock;
	Int16 code;
	char resp[ftpReplyLen];
	char *buf = gFtpChunkBuf;	/* too big for the stack; shared, see its own comment */
	Int16 n;
	Err recvErr;
	Boolean ok = true;

	if (!FtpEnterPasv(&dataSock, errMsg, errMsgLen)) return false;

	if (!FtpCmd("RETR", name, &code, resp, sizeof(resp)) || (code != 150 && code != 125)) {
		FtpFail(errMsg, errMsgLen, "RETR", resp);
		FtpTransportClose(dataSock);
		return false;
	}

	/* Same real bug as FtpList (see its own comment) - a data-connection
	 * timeout/error (n == -1) was indistinguishable from a clean EOF
	 * (n == 0), so a stalled download silently kept whatever partial data
	 * had arrived and STILL reported success as if the whole file came
	 * through - worse here than for LIST since it means a truncated file
	 * could get installed/saved without any indication it's incomplete. */
	do {
		n = FtpPollReceive(dataSock, buf, ftpChunkLen, ftpDataTimeoutTicks, &recvErr, NULL);
		if (n > 0 && ok) {
			if (!recvProc(buf, (UInt32) n, userData)) {
				ok = false;
				StrNCopy(errMsg, "Transfer aborted", errMsgLen - 1);
			}
		} else if (n < 0 && ok) {
			ok = false;
			StrPrintF(errMsg, "RETR: data connection error (err=%ld)", (long) recvErr);
		}
	} while (n > 0);

	FtpTransportClose(dataSock);
	FtpReadReply(&code, resp, sizeof(resp));
	return ok;
}

typedef struct {
	NetSocketRef dataSock;
	Boolean ok;
	Boolean declined;	/* user tapped Cancel on the overwrite prompt */
	UInt32 recvd;	/* cumulative bytes pulled so far - see gFtpProgressHook */
	UInt32 total;	/* FTP SIZE queried once before the pass starts - 0 if unknown */
} RetrieveDbCtx;

/* ExgDBReadProcPtr shape (Core/System/ExgMgr.h) - the mirror of
 * StoreDbWriteProc: ROM's ExgDBRead PULLS chunks by calling this with a
 * buffer it already owns and *sizeP set to how much it wants, so this can
 * write straight into dataP - no intermediate copy needed (unlike
 * FtpRetrieve's own recvProc, which is itself a PUSH callback and can't
 * drive ExgDBRead's PULL loop directly - same directional mismatch as
 * FtpStoreDatabase's own comment explains for the upload side). Caps the
 * request to ftpChunkLen per call regardless of what ExgDBRead asks for.
 *
 * Loops on FtpPollReceive until `want` bytes are actually accumulated (or a
 * genuine EOF/error ends the transfer early) rather than trusting a single
 * call to fill the whole request - a single FtpPollReceive call only
 * returns however many bytes one NetLibReceive happened to deliver (normal
 * TCP fragmentation), which FtpRetrieve's own raw-byte-stream recvProc
 * tolerates fine but a structured PDB/PRC parser has no reason to expect.
 * Investigated as the suspected cause of a real "download says install
 * failed" report this session, and kept as a genuine correctness fix even
 * though it turned out NOT to be the actual root cause that time (see
 * [[lazyftp-project]]) - a byte-for-byte/order-sensitive-hash comparison
 * against a host-side reference download confirmed the OLD single-shot code
 * already transferred that particular file correctly; the real bug was the
 * test file itself being malformed (POSE64's own stock installer rejected
 * the identical bytes too, independent of this app entirely). */
static Err RetrieveDbReadProc(void *dataP, UInt32 *sizeP, void *userDataP)
{
	RetrieveDbCtx *ctx = (RetrieveDbCtx *) userDataP;
	UInt16 want = (UInt16) (*sizeP > ftpChunkLen ? ftpChunkLen : *sizeP);
	UInt16 got = 0;
	UInt8 *buf = (UInt8 *) dataP;

	if (!ctx->ok) { *sizeP = 0; return exgErrUserCancel; }

	while (got < want) {
		Err recvErr;
		Int16 n = FtpPollReceive(ctx->dataSock, buf + got, (UInt16) (want - got), ftpDataTimeoutTicks, &recvErr, NULL);
		if (n < 0) {
			ctx->ok = false;
			*sizeP = 0;
			return exgErrUnknown;
		}
		if (n == 0) break;	/* clean EOF - return whatever was accumulated so far */
		got += (UInt16) n;
	}
	ctx->recvd += got;
	if (gFtpProgressHook) gFtpProgressHook(ctx->recvd, ctx->total);
	if (gFtpCancelQuery && gFtpCancelQuery()) ctx->ok = false;
	*sizeP = (UInt32) got;
	return errNone;
}

/* ExgDBDeleteProcPtr - ROM calls this if a database with the same name/
 * creator/type already exists on the device, asking permission to delete
 * it before installing the incoming one (the "install-conflict handling"
 * this whole feature was originally deferred for - see FileActions.c's own
 * comment). A synchronous confirm alert from inside this callback is fine -
 * ExgDBRead is already a single blocking call on this app's one thread, same
 * as any other blocking FTP call already wrapped in a busy dialog.
 *
 * Real bug found live: returning true alone isn't enough - ROM does NOT
 * delete the existing database on the caller's behalf despite the name
 * "delete proc"; it expects THIS callback to have actually done it.
 * Skipping the real DmDeleteDatabase call here made ExgDBRead's own
 * subsequent create-database step fail with dmErrAlreadyExists (537) even
 * though the user had already agreed to replace it - confirmed via a real
 * upload-then-download-same-name round trip against a real FTP server in
 * POSE64.
 *
 * SECOND real bug found live: tapping "Cancel" here still reported
 * "installed" afterward. Returning false from this callback alone doesn't
 * reliably make ExgDBRead itself fail/abort with a distinct error this code
 * can detect - so ctx->declined is now set directly, HERE, the moment the
 * user actually declines, and FtpRetrieveDatabase checks that flag itself
 * rather than trusting ExgDBRead's own return code to reflect it. */
static Boolean RetrieveDbDeleteProc(const char *nameP, UInt16 version, UInt16 cardNo,
	LocalID dbID, void *userDataP)
{
	RetrieveDbCtx *ctx = (RetrieveDbCtx *) userDataP;
	if (FrmCustomAlert(altConfirmOverwrite, nameP, NULL, NULL) != 0) {	/* 0 = "Replace" */
		ctx->declined = true;
		ctx->ok = false;
		return false;
	}
	if (DmDeleteDatabase(cardNo, dbID) != errNone) {
		ctx->ok = false;
		return false;
	}
	return true;
}

/* Downloads a remote file and installs it as a real on-device database
 * (PRC or PDB, ROM's ExgDBRead figures out which from the incoming byte
 * stream itself - this doesn't need to know or care). *outNeedsReset is set
 * true if ROM says the newly-installed database needs a soft reset to take
 * effect (e.g. a system extension/patch) - caller's job to tell the user. */
SEC004 Boolean FtpRetrieveDatabase(const char *name, Boolean *outNeedsReset,
	char *errMsg, UInt16 errMsgLen)
{
	RetrieveDbCtx ctx;
	Int16 code;
	char resp[ftpReplyLen];
	LocalID dbID;
	Boolean needReset = false;
	Err err;

	ctx.total = 0;
	if (gFtpProgressHook) {
		/* Best-effort, control-connection only (no PASV needed) - a server
		 * that doesn't support SIZE (or any other failure) just means
		 * progress reports total=0, not a hard error; only bothered with
		 * at all when someone's actually listening for it. */
		char sizeResp[ftpReplyLen];
		Int16 sizeCode;
		if (FtpCmd("SIZE", name, &sizeCode, sizeResp, sizeof(sizeResp)) && sizeCode == 213) {
			UInt32 v = 0;
			Boolean any = false;
			char *s = sizeResp;
			while (*s == ' ') s++;
			while (*s >= '0' && *s <= '9') { v = v * 10 + (UInt32) (*s - '0'); s++; any = true; }
			if (any) ctx.total = v;
		}
	}

	if (!FtpEnterPasv(&ctx.dataSock, errMsg, errMsgLen)) return false;

	if (!FtpCmd("RETR", name, &code, resp, sizeof(resp)) || (code != 150 && code != 125)) {
		FtpFail(errMsg, errMsgLen, "RETR", resp);
		FtpTransportClose(ctx.dataSock);
		return false;
	}

	ctx.ok = true;
	ctx.declined = false;
	ctx.recvd = 0;
	if (gFtpProgressHook) gFtpProgressHook(0, ctx.total);
	err = ExgDBRead(RetrieveDbReadProc, RetrieveDbDeleteProc, &ctx, &dbID, 0, &needReset, false);

	FtpTransportClose(ctx.dataSock);
	FtpReadReply(&code, resp, sizeof(resp));

	if (outNeedsReset) *outNeedsReset = needReset;

	/* Checked ahead of (and instead of trusting) ExgDBRead's own return
	 * code - see RetrieveDbDeleteProc's own comment for why: a declined
	 * overwrite wasn't reliably turning into a distinct error `err` from
	 * ExgDBRead itself, so a Cancel tap here was still falling through to
	 * "installed" below. */
	if (ctx.declined) {
		errMsg[0] = '\0';	/* user declined the overwrite prompt - not a real error, dismiss silently */
		return false;
	}
	if (err != errNone || !ctx.ok) {
		if (err != errNone) {
			/* dmErrCorruptDatabase (521) here means the SOURCE file itself
			 * is malformed, not a transfer bug - verified by reproducing the
			 * identical failure via POSE64's own stock installer on the same
			 * bytes, independent of this app entirely. See [[lazyftp-project]].
			 * A user Cancel tap (see RetrieveDbReadProc's gFtpCancelQuery
			 * poll) lands here too, as exgErrUserCancel - errMsg still gets
			 * set to a generic "Install failed", but the caller can tell it
			 * apart from a real error via LzTPProgressCancelled() and
			 * suppress the alert (same silent-dismiss idea as ctx.declined
			 * above, just one frame further up since this isn't a distinct
			 * ctx flag). */
			StrPrintF(errMsg, "Install failed (err=%d)", (int) err);
		} else {
			StrNCopy(errMsg, "Transfer failed", errMsgLen - 1);
		}
		return false;
	}
	return true;
}

SEC004 Boolean FtpStore(const char *name, FtpSendProc sendProc, void *userData,
	char *errMsg, UInt16 errMsgLen)
{
	NetSocketRef dataSock;
	Int16 code;
	char resp[ftpReplyLen];
	char *buf = gFtpChunkBuf;	/* too big for the stack; shared, see its own comment */
	Boolean ok = true;
	UInt32 totalSent = 0;

	if (!FtpEnterPasv(&dataSock, errMsg, errMsgLen)) return false;
	FtpTransportEnableLinger(dataSock);	/* see its own comment - not actually
											   effective on the Net.lib/SLIP
											   combination this was tested
											   against, kept as a harmless
											   best-effort; FtpTransportDrainWait
											   below is the real fix */

	if (!FtpCmd("STOR", name, &code, resp, sizeof(resp)) || (code != 150 && code != 125)) {
		FtpFail(errMsg, errMsgLen, "STOR", resp);
		FtpTransportClose(dataSock);
		return false;
	}

	do {
		UInt32 len = ftpChunkLen;
		if (!sendProc(buf, &len, userData)) {
			ok = false;
			StrNCopy(errMsg, "Read failed", errMsgLen - 1);
			break;
		}
		if (len == 0) break;	/* source exhausted */
		if (FtpTransportSend(dataSock, buf, (UInt16) len) == -1) {
			ok = false;
			StrNCopy(errMsg, "Send failed", errMsgLen - 1);
			break;
		}
		totalSent += len;
	} while (true);

	if (ok) FtpTransportDrainWait(totalSent);	/* see its own comment - the real fix */

	FtpTransportClose(dataSock);
	FtpReadReply(&code, resp, sizeof(resp));
	return ok;
}

typedef struct {
	NetSocketRef dataSock;
	Boolean ok;
	UInt16 crc;		/* running Crc16Update over the file's content, byte 0
					   onward - compared against a verification RETR's own
					   CRC once the transfer completes, see
					   FtpStoreDatabaseAttempt */
	UInt32 sent;	/* total bytes actually handed to FtpTransportSend this
					   pass - used to size FtpTransportDrainWait */
	UInt32 total;	/* DmDatabaseSize() queried once before the pass starts -
					   0 if unknown; see gFtpProgressHook */
} StoreDbCtx;

/* ExgDBWriteProcPtr shape (Core/System/ExgMgr.h) - ROM's ExgDBWrite calls
 * this repeatedly, PUSHING each chunk of the database's serialized bytes as
 * it produces them (record list, records, resources, in whatever order/
 * chunking ROM itself chooses - same mechanism beam-send uses for a whole
 * database, not just one record). Forwards each chunk to the already-open
 * STOR data socket - see FtpStoreDatabase's own comment for why this can't
 * reuse FtpStore's sendProc callback shape.
 *
 * `*sizeP` is UInt32 but FtpTransportSend takes a UInt16 length - loop in
 * ftpChunkLen-sized pieces rather than a single `(UInt16) *sizeP` cast,
 * which would silently truncate (and permanently drop the remainder of) any
 * single push from ROM larger than 65535 bytes. */
static Err StoreDbWriteProc(const void *dataP, UInt32 *sizeP, void *userDataP)
{
	StoreDbCtx *ctx = (StoreDbCtx *) userDataP;
	const UInt8 *p = (const UInt8 *) dataP;
	UInt32 remain = *sizeP;

	while (ctx->ok && remain > 0) {
		UInt16 chunk = (UInt16) (remain > ftpChunkLen ? ftpChunkLen : remain);

		ctx->crc = Crc16Update(ctx->crc, p, chunk);
		if (FtpTransportSend(ctx->dataSock, p, chunk) == -1) {
			ctx->ok = false;
			break;
		}
		ctx->sent += chunk;
		if (gFtpProgressHook) gFtpProgressHook(ctx->sent, ctx->total);
		if (gFtpCancelQuery && gFtpCancelQuery()) {
			ctx->ok = false;
			break;
		}
		p += chunk;
		remain -= chunk;
	}
	return ctx->ok ? errNone : exgErrUserCancel;
}

typedef struct {
	UInt16 crc;
	UInt32 recvd;	/* cumulative bytes read back so far this verify pass */
	UInt32 total;	/* expected size - reuses the just-uploaded byte count
					   (StoreDbCtx.total), since this IS that same file */
} VerifyCrcCtx;

/* FtpRecvProc shape - accumulates a CRC over a post-upload verification
 * RETR instead of actually keeping the bytes, see FtpStoreDatabase. The CRC
 * is computed AS each chunk arrives, so "downloading" and "CRC checking" are
 * literally the same pass over the same bytes, not two separate waits - the
 * progress hook here reports real re-download progress so the CRC-Checking
 * phase's bar fills 0->100% along with it instead of sitting frozen for the
 * whole verify RETR (real user complaint: the bar didn't move at all during
 * this phase, since FtpRetrieve itself never drove gFtpProgressHook before). */
static Boolean VerifyCrcRecvProc(const void *data, UInt32 len, void *userData)
{
	VerifyCrcCtx *ctx = (VerifyCrcCtx *) userData;
	if (gFtpCancelQuery && gFtpCancelQuery()) return false;	/* FtpRetrieve
		treats a false return as "Transfer aborted", same as any other
		recvProc failure - see its own comment */
	ctx->crc = Crc16Update(ctx->crc, (const UInt8 *) data, (UInt16) len);
	ctx->recvd += len;
	if (gFtpProgressHook) gFtpProgressHook(ctx->recvd, ctx->total);
	return true;
}

#define ftpStoreMaxAttempts 3	/* fresh STOR + verify, full re-upload on mismatch - see FtpStoreDatabase */

/* Runs ONE fresh STOR pass (STOR overwrites/truncates any prior remote
 * content, so each attempt starts clean) and verifies the result by
 * immediately re-downloading the whole file and comparing a CRC-16
 * computed during THIS pass's ExgDBWrite against one computed from what
 * the verification RETR actually reads back. Returns true only on a
 * confirmed-correct upload; leaves errMsg empty (and returns false) on a
 * verification mismatch so the caller knows a plain retry - not a hard
 * error - is the right response. */
static Boolean FtpStoreDatabaseAttempt(const char *remoteName, const char *dbName,
	LocalID dbID, UInt16 cardNo, char *errMsg, UInt16 errMsgLen)
{
	StoreDbCtx ctx;
	Int16 code;
	char resp[ftpReplyLen];
	Err err;

	errMsg[0] = '\0';
	if (!FtpEnterPasv(&ctx.dataSock, errMsg, errMsgLen)) return false;
	FtpTransportEnableLinger(ctx.dataSock);	/* harmless best-effort, not the
												   real fix - see its own comment */

	if (!FtpCmd("STOR", remoteName, &code, resp, sizeof(resp)) || (code != 150 && code != 125)) {
		FtpFail(errMsg, errMsgLen, "STOR", resp);
		FtpTransportClose(ctx.dataSock);
		return false;
	}

	ctx.ok = true;
	ctx.crc = 0xFFFF;
	ctx.sent = 0;
	ctx.total = 0;
	{
		/* Best-effort - a failed query just means progress reports total=0
		 * (caller treats that as "no percentage available"), not a hard
		 * error. Not byte-for-byte identical to what ExgDBWrite actually
		 * transmits (its own PDB/PRC header framing differs slightly), but
		 * close enough to drive a percentage bar - this is progress
		 * feedback, not a correctness check (the CRC verify RETR is). */
		UInt32 numRecords, totalBytes, dataBytes;
		if (DmDatabaseSize(cardNo, dbID, &numRecords, &totalBytes, &dataBytes) == errNone) {
			ctx.total = totalBytes;
		}
	}
	if (gFtpProgressHook) gFtpProgressHook(0, ctx.total);
	err = ExgDBWrite(StoreDbWriteProc, &ctx, dbName, dbID, cardNo);
	if (err != errNone && ctx.ok) {
		ctx.ok = false;
		StrPrintF(errMsg, "Database export failed (err=%d)", (int) err);
	} else if (!ctx.ok && errMsg[0] == '\0') {
		StrNCopy(errMsg, "Send failed", errMsgLen - 1);
	}

	if (ctx.ok && ctx.sent > 0) FtpTransportDrainWait(ctx.sent);

	FtpTransportClose(ctx.dataSock);
	FtpReadReply(&code, resp, sizeof(resp));
	if (!ctx.ok) return false;

	{
		VerifyCrcCtx vctx;
		vctx.crc = 0xFFFF;
		vctx.recvd = 0;
		vctx.total = ctx.total;
		if (gFtpPhaseHook) gFtpPhaseHook("CRC Checking");	/* also resets the
			bar to 0% via LzTPProgressSetPhase - VerifyCrcRecvProc's own
			gFtpProgressHook calls then drive it forward for real */
		if (!FtpRetrieve(remoteName, VerifyCrcRecvProc, &vctx, errMsg, errMsgLen)) {
			return false;	/* errMsg already set by FtpRetrieve itself */
		}
		if (vctx.crc == ctx.crc) return true;
	}

	errMsg[0] = '\0';	/* mismatch, not a hard error - caller may retry */
	return false;
}

/* Real-hardware report (this session): even with FtpTransportSend's own
 * short-send fix and an explicit drain-wait before closing, an upload
 * could still come out silently corrupted on a slow real link - a clean
 * STOR reply just means the SERVER accepted a connection close, not that
 * every byte we THINK we sent actually got there intact. Verifies every
 * attempt (FtpStoreDatabaseAttempt: re-download + CRC compare) and, on a
 * mismatch, retries with a full fresh STOR - an earlier version of this
 * tried to resume via APPE from whatever SIZE reported already landed, but
 * that assumes the already-landed prefix is byte-correct, just incomplete.
 * Real-device testing disproved that: uploads came back with the CORRECT
 * total size but WRONG content, meaning the link/gateway can corrupt
 * bytes mid-stream, not just truncate - a scenario a length-based resume
 * can't detect or recover from (root cause has since been fixed at the
 * WiRSa gateway's TCP relay, see project memory). A full fresh retry has
 * no such assumption to get wrong: every attempt starts from a clean,
 * verified-empty state. */
SEC004 Boolean FtpStoreDatabase(const char *remoteName, const char *dbName,
	LocalID dbID, UInt16 cardNo, char *errMsg, UInt16 errMsgLen)
{
	UInt16 attempt;

	for (attempt = 0; attempt < ftpStoreMaxAttempts; attempt++) {
		/* Reset the phase label back to "Uploading" at the start of every
		 * attempt (incl. retries after a mismatch) - FtpStoreDatabaseAttempt
		 * itself only ever switches it forward to "CRC Checking" once the
		 * STOR completes. */
		if (gFtpPhaseHook) gFtpPhaseHook("Uploading");
		if (FtpStoreDatabaseAttempt(remoteName, dbName, dbID, cardNo, errMsg, errMsgLen)) {
			return true;
		}
		if (errMsg[0] != '\0') return false;	/* a real transport/protocol error, not a verification mismatch */
	}

	StrPrintF(errMsg, "Upload verification failed after %u attempt(s) - please retry",
		(unsigned) ftpStoreMaxAttempts);
	return false;
}
