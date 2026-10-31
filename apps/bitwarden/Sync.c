/*
 * Sync.c - orchestrates one HELLO / (TRUST) / SYNC exchange with WiRSa's
 * BitWarden Relay mode over Serial.c, following PROTOCOL.md exactly (this
 * file's frame body encode/decode is the Palm-side twin of
 * bitwarden_relay.cpp's BwWriter/BwReader and mock_wirsa.py's
 * FrameWriter/FrameReader - all three were built against the same spec).
 *
 * The master password is always prompted fresh here, even if the local
 * store is already unlocked - BwIsUnlocked()'s cached key is derived from
 * our own local salt (Store.c), completely independent from what WiRSa
 * needs to log into the real Bitwarden server (which uses the account's
 * own salt=email). The two must not be conflated; see BitWarden.h.
 */
#include "BitWarden.h"
#include "Wire.h"
#include "relaysec.h"

#define BW_HELLO		0x01
#define BW_HELLO_ACK	0x02
#define BW_TRUST		0x03
#define BW_TRUST_RESULT	0x04
#define BW_SYNC			0x05
#define BW_FOLDER		0x0C
#define BW_ORG			0x0D
#define BW_SYNC_ITEM	0x06
#define BW_SYNC_DONE	0x07
#define BW_SYNC_ERROR	0x08
#define BW_BYE			0x0B
/* Palm -> WiRSa, empty body - flow-control ack for FOLDER/ORG/SYNC_ITEM.
 * WiRSa blocks after each one until this ack comes back (see
 * bitwarden_relay.cpp's bwWriteFrameAcked) - sent here only after the
 * corresponding BwStoreAdd* call has durably written the record, so a lost
 * ack on the wire never means "WiRSa thinks this was saved but it wasn't." */
#define BW_ITEM_ACK		0x0E
#define BW_TWO_FACTOR_REQUIRED	0x0F	/* WiRSa -> Palm */
#define BW_TWO_FACTOR_SUBMIT	0x10	/* Palm -> WiRSa */
/* WiRSa -> Palm, body: `string statusText`. Purely cosmetic live status-
 * label update for the one long stretch (prelogin/PBKDF2/login/api-sync-
 * fetch) between sending SYNC and the first real FOLDER/ORG/SYNC_ITEM/
 * SYNC_ERROR/TWO_FACTOR_REQUIRED frame - see the receive loop below. */
#define BW_SYNC_STATUS	0x11

/* PROTOCOL.md SYNC_ERROR code - bad email/master password. */
#define BW_ERR_AUTH_FAILED	5

/* Sized to the true worst-case SYNC_ITEM (an Identity item: 18
 * bwMaxExtField(128) fields plus header/name/notes = 3412 bytes), not a
 * round number - datares needs every spare byte for Serial.c's receive
 * buffer. BwSerialRecvFrame's own "drain and drop oversized frame"
 * fallback still protects against anything larger. */
#define bwWireBodyCap	3412
static UInt8 gWireBody[bwWireBodyCap];

/* Static (not stack) scratch - BwItemFull alone is ~3KB; this crashed on
 * real hardware as a loop-local variable inside BwSyncRun even though
 * POSE64 tolerated it. Same discipline as Store.c's gRecBuf and
 * bitwarden_relay.cpp's gFrameBody/gScratch* buffers - BwSyncRun processes
 * one frame at a time, never re-entrantly, so static reuse is safe. */
/* One item buffer for both Sync.c and RecordForm.c (they never run at the
 * same time: a sync is modal and no record is open during it) - the second
 * 3.4 KB copy would not leave room for the 4 KB serial receive buffer. */
BwItemFull gBwItemShared;
#define gSyncItem gBwItemShared
static char gSyncNameBuf[bwMaxField];
/* Shared scratch for whatever updatedTwoFactorToken SYNC_DONE or
 * SYNC_ERROR carries - datares has no room for a second full-size copy;
 * the two are mutually exclusive branches of the same receive loop. */
static char gNewTokenBuf[bwTwoFactorRememberMax];
/* Sized for the SYNC frame's true worst case: serverURL(160) +
 * pinnedFingerprint(32) + email(80) + masterPassword(128) + kdf fields(10)
 * + rememberedTwoFactorToken(bwTwoFactorRememberMax) with each string's
 * own 2-byte length prefix, comes to 1023 of 1024 - essentially no slack
 * (wU16 writes the full intended length up front regardless of `cap`, so
 * exceeding this would desync the frame). Can't be bumped further, same
 * datares ceiling as bwTwoFactorRememberMax. */
static UInt8 gSyncWBuf[1024];

/* Body writer/reader (BwW/BwR + wU8/wU16/wU32/wBytes/wStr/rU8/rU16/rBytes/
 * rStr) live in Wire.c/Wire.h, called cross-file only - see Wire.c's
 * header for why (same-file SEC001 alongside their only caller here once
 * corrupted outgoing frames). */

static Boolean SendFrameAndWait(UInt8 type, const UInt8 *body, UInt16 bodyLen, UInt8 *rxType, UInt16 *rxLen)
{
	if (!BwSerialSendFrame(type, body, bodyLen)) return false;
	if (!BwSerialRecvFrame(rxType, gWireBody, bwWireBodyCap, rxLen)) return false;
	return true;
}

/* SEC001 for main-segment budget - safe direction (main-segment BwSyncRun
 * calling a SEC001 function in the same file). Deliberately does not call
 * SendFrameAndWait above (that would be a SEC001 function calling back
 * into a same-file main function - the documented "5th multi-segment
 * gotcha") - BwSerialSendFrame/BwSerialRecvFrame are called directly
 * instead, both already cross-file (Serial.c) regardless of caller segment.
 *
 * Baud negotiation, mirrors WiRSa's own SASD mode and CalDAV Sync's
 * identical fix: both sides open at the shared bwBaudIdle bootstrap rate
 * (BwSerialOpen; WiRSa's own BitWarden Relay entry does the same) - offers
 * this device's configured target (BwGetBaud, Store.c) via BW_HELLO,
 * WiRSa caps it to its own configured max and replies with the actual
 * negotiated rate via BW_HELLO_ACK, then both sides switch. On failure the
 * caller still needs to BwSerialClose()/wipe the password itself, same as
 * every other failure branch in BwSyncRun. */
static Boolean BwNegotiateBaud(void) SEC001;

static Boolean BwNegotiateBaud(void)
{
	UInt8 rxType;
	UInt16 rxLen;
	BwW w;
	BwR r;
	UInt32 negotiatedBaud;

	/* Secure the serial link before anything else goes over it (no
	 * plaintext fallback). No reply at all keeps the existing message. */
	{
		UInt8 sr = RpSecure();
		if (sr != rpSecOk) {
			BwProgressHide();
			if (sr == rpSecFailed) FrmCustomAlert(altErr, "Serial link cannot be secured!", NULL, NULL);
			else if (sr == rpSecNoReply) FrmCustomAlert(altErr, "WiRSa did not respond. Check the connection and try again.", NULL, NULL);
			return false;
		}
	}

	BwProgressSetStatus("Negotiating rate...");

	w.buf = gSyncWBuf; w.cap = sizeof(gSyncWBuf); w.len = 0;
	wU32(&w, BwGetBaud());

	if (!BwSerialSendFrame(BW_HELLO, gSyncWBuf, w.len) ||
	    !BwSerialRecvFrame(&rxType, gWireBody, bwWireBodyCap, &rxLen) ||
	    rxType != BW_HELLO_ACK) {
		BwProgressHide();
		/* A cancel tap can abort this wait directly (Serial.c's ReadExact
		 * checks BwProgressCancelled() every ~1s poll) - distinguish that
		 * from a real "WiRSa didn't respond" failure. */
		if (!BwProgressCancelled()) FrmCustomAlert(altErr, "WiRSa did not respond. Check the connection and try again.", NULL, NULL);
		return false;
	}

	r.buf = gWireBody; r.len = rxLen; r.pos = 0; r.ok = true;
	/* BW_HELLO_ACK's body is not just the baud value - it's WiRSa's own
	 * pre-existing version(u8)+capFlags(u8)+agent str(u16 len + bytes)
	 * first (bitwarden_relay.cpp's bwHandleHello), with the negotiated
	 * baud appended after those, not replacing them. Must consume the
	 * same 3 fields first, in the same order, before the baud field is
	 * reachable. CalDAV's own HELLO_ACK has no pre-existing fields, so
	 * this doesn't apply there. */
	{
		UInt8 version;
		UInt8 capFlags;
		char agentBuf[24];
		version = rU8(&r);
		capFlags = rU8(&r);
		rStr(&r, agentBuf, sizeof(agentBuf));
		(void) version; (void) capFlags;	/* not used by this side today - just consumed to reach the baud field below */
	}
	negotiatedBaud = rU32(&r);
	if (!r.ok || negotiatedBaud == 0) {
		BwProgressHide();
		FrmCustomAlert(altErr, "WiRSa sent a malformed baud negotiation reply.", NULL, NULL);
		return false;
	}

	BwSerialSetBaud(negotiatedBaud);
	StrPrintF(gSyncNameBuf, "Negotiated %ld baud", (Int32) negotiatedBaud);
	BwProgressSetStatus(gSyncNameBuf);
	return true;
}

/* Single-char codes distinguish which of the 4 call sites raised "Lost
 * contact" (all shared identical wording before this) without a full-word
 * suffix pushing main-segment code size over coderes's own cap. Built via
 * one shared helper, reusing gSyncNameBuf as scratch (its own prior
 * content is already stale and about to be discarded by the abort that
 * follows regardless). R=waiting for next frame, F=folder ack, O=org ack,
 * I=item ack, T=two-factor submit. */
static void ShowLostContactAlert(char where) SEC001;

static void ShowLostContactAlert(char where)
{
	/* err=%d/le=%d - gBwLastRecvErr/gBwLastLineErrs (Serial.c), only
	 * meaningful for [R] (a genuine SrmReceive error, not a timeout),
	 * shown unconditionally to save code size. 0/0 for every other tag / a
	 * genuine [R] timeout. le is SrmGetStatus's own bitmask (SerialMgr.h):
	 * 1=parity, 2=HW overrun, 4=framing, 8=break, 0x10=handshake,
	 * 0x20=SW overrun, 0x40=carrier lost. */
	StrPrintF(gSyncNameBuf, "Lost contact with WiRSa during sync [%c] err=%d le=%d.", where, gBwLastRecvErr, gBwLastLineErrs);
	FrmCustomAlert(altErr, gSyncNameBuf, NULL, NULL);
}

/* SEC001 for main-segment budget - same already-proven-safe shape as
 * ShowLostContactAlert above (main-segment BwSyncRun calling a SEC001
 * function in the SAME file). Body is just rStr (Wire.c, cross-file - fine,
 * unrelated to the main/sec001 boundary) plus BwProgressSetStatus. */
static void HandleSyncStatusFrame(BwR *r) SEC001;

static void HandleSyncStatusFrame(BwR *r)
{
	rStr(r, gSyncNameBuf, sizeof(gSyncNameBuf));
	if (r->ok) BwProgressSetStatus(gSyncNameBuf);
}

/* Every abort path below sends BW_BYE and closes the port immediately,
 * without waiting to see if WiRSa has anything more to say. But WiRSa's
 * own login (which may have rotated the 2FA-remember token) already
 * succeeded before whatever mid-sync failure triggered this abort, and
 * WiRSa's own error-reporting path (sendErr, bitwarden_relay.cpp) tries to
 * report that fresh token via SYNC_ERROR regardless of why the sync
 * failed - but only if the Palm is still listening. Call this right after
 * sending BW_BYE, before closing - one short (3s), best-effort attempt to
 * catch a trailing SYNC_ERROR and pull its token out, reusing gNewTokenBuf
 * (never live at the same time as the main loop's own use of it). If
 * nothing arrives in time, this is a silent no-op. */
static void TryCatchTrailingToken(void) SEC001;

static void TryCatchTrailingToken(void)
{
	UInt8 rxType; UInt16 rxLen;
	UInt32 saved = gBwIdleOverrideTicks;
	gBwIdleOverrideTicks = SysTicksPerSecond() * 3;
	if (BwSerialRecvFrame(&rxType, gWireBody, bwWireBodyCap, &rxLen) && rxType == BW_SYNC_ERROR) {
		BwR r;
		r.buf = gWireBody; r.len = rxLen; r.pos = 0; r.ok = true;
		(void) rU8(&r);				/* errorCode - not needed here */
		rStr(&r, gSyncNameBuf, sizeof(gSyncNameBuf));	/* message - not needed here, just skip past it */
		rStr(&r, gNewTokenBuf, sizeof(gNewTokenBuf));
		if (r.ok && gNewTokenBuf[0]) BwSetTwoFactorRemember(gNewTokenBuf);
		MemSet(gNewTokenBuf, sizeof(gNewTokenBuf), 0);
	}
	gBwIdleOverrideTicks = saved;
}

/* Shared by all three ack-send sites below (BW_FOLDER/BW_ORG/BW_SYNC_ITEM
 * all send this same flow-control frame) - matches bitwarden_relay.cpp's
 * own "attempt N/3" retry cadence for its /api/sync refetch, so a
 * Palm-side retry window overlaps a firmware-side one instead of one side
 * giving up while the other is still trying. Retries the send on the same
 * already-open port (no close/reopen - confirmed unnecessary and costs a
 * ~0.5s window where WiRSa's own retried bytes could be missed). 5
 * attempts to match bitwarden_relay.cpp's own bwWriteFrameAcked. */
static Boolean SendItemAckWithRetry(void)
{
	UInt16 attempt;
	for (attempt = 0; attempt < 5; attempt++) {
		if (attempt > 0) SysTaskDelay(SysTicksPerSecond() / 2);
		if (BwSerialSendFrame(BW_ITEM_ACK, NULL, 0)) return true;
	}
	return false;
}

/* Same "retry in place before giving up" cadence as SendItemAckWithRetry
 * above, for the other half of every FOLDER/ORG/SYNC_ITEM round trip -
 * waiting for WiRSa's next frame. BwSerialRecvFrame's own internal timeout
 * (bwIdleTimeoutTicks) already tolerates a single long stall; this loop
 * retries a genuine dead read in place, up to 5 attempts, before the
 * caller shows "Lost contact" and gives up for real. */
static Boolean RecvFrameWithRetry(UInt8 *rxType, UInt16 *rxLen)
{
	UInt16 attempt;
	for (attempt = 0; attempt < 5; attempt++) {
		if (BwSerialRecvFrame(rxType, gWireBody, bwWireBodyCap, rxLen)) return true;
		if (BwProgressCancelled()) return false;
	}
	return false;
}

/* Start/end sync chime - see BitWarden.h's own comment. Note-for-note match
 * of CalDAV Sync's PlaySyncChime (caldav_sync/MainForm.c): raw SndDoCmd
 * (sndCmdFreqDurationAmp, present since PalmOS v1), amplitude from
 * SndGetDefaultVolume's sysAmpP so it follows the device's actual System
 * Sound level (Off/Low/Medium/High) rather than always playing at max. */
static void PlayTone(UInt32 freq, UInt16 durMs, UInt16 amp) SEC001;

static void PlayTone(UInt32 freq, UInt16 durMs, UInt16 amp)
{
	SndCommandType cmd;
	cmd.cmd = sndCmdFreqDurationAmp;
	cmd.param1 = (Int32) freq;
	cmd.param2 = durMs;
	cmd.param3 = amp;
	SndDoCmd(NULL, &cmd, false);
}

void BwPlaySyncChime(Boolean isStart)
{
	UInt16 sysAmp;
	SndGetDefaultVolume(NULL, &sysAmp, NULL);
	if (sysAmp == 0) return;

	if (isStart) {
		PlayTone(1400, 75, sysAmp);
		PlayTone(1500, 113, sysAmp);
		PlayTone(2400, 450, sysAmp);
	} else {
		PlayTone(2400, 75, sysAmp);
		PlayTone(1800, 113, sysAmp);
		PlayTone(1200, 450, sysAmp);
	}
}

Boolean BwSyncRun(void)
{
	char password[128];
	char email[80];
	char serverURL[160];
	UInt8 pinnedFp[32];
	UInt8 rxType;
	UInt16 rxLen;
	UInt16 itemCount = 0, folderCount = 0, orgCount = 0;
	Boolean ok = false;
	Boolean passwordFromCache;

	BwGetServerURL(serverURL, sizeof(serverURL));
	BwGetEmail(email, sizeof(email));
	if (serverURL[0] == 0 || email[0] == 0) {
		FrmCustomAlert(altErr, "Set a server URL and email in Preferences first.", NULL, NULL);
		return false;
	}

	/* Session-cached password first (set by either this same prompt or the
	 * local-vault unlock in MainForm's MainOpen) - skip prompting if one is
	 * cached and its own TTL ("Lock After") hasn't expired. */
	passwordFromCache = BwGetCachedPassword(password, sizeof(password));
	if (!passwordFromCache) {
		if (BwHasCachedPassword()) {
			/* Cache exists and hasn't expired, but Sec35 itself is locked
			 * (the cache is encrypted under Sec35's key) - bounce there
			 * instead of prompting for the master password again; same
			 * "abort now, retry once unlocked" pattern as
			 * MainEnsureLocalUnlock's own identical check. Sync Now just
			 * needs to be tapped again after Sec35 comes back. */
			BwSwitchToSec35();
			return false;
		}
		if (!BwUnlockPromptRun(password, sizeof(password))) return false;
	}
	if (password[0] == 0) {
		FrmCustomAlert(altErr, "Enter your master password.", NULL, NULL);
		return false;
	}

	BwProgressShow("Connecting...");

	if (!BwSerialOpen()) {
		BwProgressHide();
		FrmCustomAlert(altErr, "Could not open the serial port.", NULL, NULL);
		MemSet(password, sizeof(password), 0);
		return false;
	}

	/* On failure BwNegotiateBaud has already shown its own alert (unless
	 * cancelled) - this call site only needs the shared close/wipe/return
	 * every other failure branch in this function already does. */
	if (!BwNegotiateBaud()) {
		BwSerialClose();
		MemSet(password, sizeof(password), 0);
		return false;
	}
	BwProgressSetStatus("Preparing sync...");

	if (!BwHasPinnedFingerprint()) {
		BwW w;
		BwR r;

		BwProgressSetStatus("Checking server...");
		w.buf = gSyncWBuf; w.cap = sizeof(gSyncWBuf); w.len = 0;
		wStr(&w, serverURL);

		if (!SendFrameAndWait(BW_TRUST, gSyncWBuf, w.len, &rxType, &rxLen) || rxType != BW_TRUST_RESULT) {
			BwProgressHide();
			if (!BwProgressCancelled()) FrmCustomAlert(altErr, "Could not reach the server to check its certificate.", NULL, NULL);
			BwSerialClose();
			MemSet(password, sizeof(password), 0);
			return false;
		}

		r.buf = gWireBody; r.len = rxLen; r.pos = 0; r.ok = true;
		{
			UInt8 status = rU8(&r);
			rBytes(&r, pinnedFp, 32);
			if (!r.ok || status != 0) {
				BwProgressHide();
				FrmCustomAlert(altErr, "Could not verify the server's certificate.", NULL, NULL);
				BwSerialClose();
				MemSet(password, sizeof(password), 0);
				return false;
			}
		}

		BwProgressHide();
		if (!BwTrustPromptRun(pinnedFp)) {
			BwSerialClose();
			MemSet(password, sizeof(password), 0);
			return false;
		}
		BwSetPinnedFingerprint(pinnedFp);
		BwProgressShow("Authenticating...");
	} else {
		BwGetPinnedFingerprint(pinnedFp);
		BwProgressSetStatus("Authenticating...");
	}

	if (BwProgressCancelled()) {
		BwProgressHide();
		BwSerialSendFrame(BW_BYE, NULL, 0);
		BwSerialClose();
		MemSet(password, sizeof(password), 0);
		return false;
	}

	{
		BwW w;
		/* static, not stack-local: BwSyncRun already carries password[128]/
		 * email[80]/serverURL[160] live in the same frame, deep in an
		 * event-handler call chain - a stack-local buffer growth here once
		 * produced a real-device "lost WiRSa connection" (silent stack
		 * overflow, not a frame-contents bug). */
		static char rememberedToken[bwTwoFactorRememberMax];
		/* Bitwarden's own "remember this device for 2FA" token, if cached
		 * from a previous successful 2FA login - lets WiRSa skip the
		 * interactive 2FA challenge this time (PROTOCOL.md's SYNC/
		 * TWO_FACTOR_REQUIRED). Empty if never completed, or Sec35 is
		 * unexpectedly locked - WiRSa falls back to a normal prompt either way. */
		if (!BwGetTwoFactorRemember(rememberedToken, sizeof(rememberedToken))) rememberedToken[0] = 0;

		w.buf = gSyncWBuf; w.cap = sizeof(gSyncWBuf); w.len = 0;
		wStr(&w, serverURL);
		wBytes(&w, pinnedFp, 32);
		wStr(&w, email);
		wStr(&w, password);
		wU8(&w, 0);			/* kdfType 0 = ask the server (prelogin) */
		wU32(&w, 0);		/* kdfIterations - unused when kdfType is 0 */
		wU32(&w, 0);		/* kdfMemoryKiB - unused when kdfType is 0 */
		wU8(&w, 0);			/* kdfParallelism - unused when kdfType is 0 */
		wStr(&w, rememberedToken);
		MemSet(rememberedToken, sizeof(rememberedToken), 0);

		if (!BwSerialSendFrame(BW_SYNC, gSyncWBuf, w.len)) {
			BwProgressHide();
			FrmCustomAlert(altErr, "Failed to send the sync request.", NULL, NULL);
			BwSerialClose();
			MemSet(password, sizeof(password), 0);
			MemSet(gSyncWBuf, sizeof(gSyncWBuf), 0);
			return false;
		}
		MemSet(gSyncWBuf, sizeof(gSyncWBuf), 0);
	}

	/* Establish/verify the local at-rest key now, using the same password
	 * just sent to WiRSa, before wiping it - every BwStoreAddItem/
	 * BwStoreAddFolder below silently no-ops without a cached key. If this
	 * fails, the local vault's own salt was established from a since-
	 * changed master password - but WiRSa's own auth two frames ago
	 * already proved this password is current, so re-establish the local
	 * vault fresh rather than reporting a confusing "wrong password". */
	/* Skipped when `password` came from the cache (not a fresh
	 * BwUnlockPromptRun type-in) and BwIsUnlocked() is still true -
	 * BwSetCachedPassword and a successful BwUnlock always run together,
	 * so a cached password and a still-valid local key can never have
	 * diverged; re-deriving would just reproduce the same key at real
	 * cost (BwUnlock's stretch takes ~2s). A freshly typed password is
	 * always verified regardless, since it could be wrong or reveal the
	 * real Bitwarden password changed (BwResetVaultForNewPassword fallback). */
	if (!(passwordFromCache && BwIsUnlocked())) {
		/* A progress dialog is already showing - update its status text
		 * rather than calling BwProgressShow again (not re-entrant: it
		 * captures FrmGetActiveForm() as the form to restore on Hide). */
		BwProgressSetStatus("Working...");
		if (!BwUnlock(password)) {
			BwResetVaultForNewPassword();
			BwUnlock(password);
		}
	}
	/* Cache for this session - re-caching a value that already came from
	 * the cache is harmless. */
	BwSetCachedPassword(password);
	MemSet(password, sizeof(password), 0);

	/* WiRSa's own login+PBKDF2+/api/sync fetch happens entirely inside
	 * this one request - this label is just the initial guess; WiRSa now
	 * sends its own BW_SYNC_STATUS updates (e.g. "Iterating..." while
	 * PBKDF2 runs) as it moves through that work, handled in the receive
	 * loop below alongside FOLDER/SYNC_ITEM/SYNC_ERROR. */
	BwProgressSetStatus("Fetching vault...");

	if (!BwStoreBeginSync()) {
		/* False means the local database couldn't be recreated (e.g. card
		 * full) - must abort here, since every BwStoreAddItem call below
		 * would otherwise silently no-op with no local database open. */
		BwProgressHide();
		FrmCustomAlert(altErr, "Could not reset the local vault database. Sync aborted.", NULL, NULL);
		BwSerialSendFrame(BW_BYE, NULL, 0);
		BwSerialClose();
		return false;
	}

	for (;;) {
		BwR r;
		if (!RecvFrameWithRetry(&rxType, &rxLen)) {
			BwProgressHide();
			/* This single wait is where most of a sync's real time goes
			 * (WiRSa's PBKDF2/login/api-sync-fetch, plus a possible 2FA
			 * prompt) - also where a Cancel tap is noticed almost
			 * immediately, so this failure is the common case for a
			 * cancelled sync, not just a genuine loss - skip the alarming
			 * alert when it was the user's own request. */
			if (!BwProgressCancelled()) ShowLostContactAlert('R');
			/* Only failing to receive here - sending is likely still fine,
			 * so tell WiRSa we're giving up instead of silently closing,
			 * rather than leaving it sitting in its own long wait unable
			 * to service the next retry. */
			BwSerialSendFrame(BW_BYE, NULL, 0);
			TryCatchTrailingToken();
			BwSerialClose();
			BwStoreAbortSync();	/* leave every previously-synced record intact - see BitWarden.h's own note */
			return false;
		}

		r.buf = gWireBody; r.len = rxLen; r.pos = 0; r.ok = true;

		if (rxType == BW_SYNC_STATUS) {
			/* Purely cosmetic - deliberately does NOT fall through to the
			 * gBwIdleOverrideTicks shortening below: a status ping isn't "a
			 * sync actively flowing" the way a real FOLDER/ORG/SYNC_ITEM is,
			 * and the wait for WiRSa's NEXT frame after this one can still
			 * legitimately be as long as the un-shortened default (e.g.
			 * PBKDF2 alone has been observed taking >90s - see
			 * bitwarden_relay.cpp's own note). */
			HandleSyncStatusFrame(&r);
			continue;
		}

		/* Shorten every subsequent wait now that a sync is actively
		 * flowing - see gBwIdleOverrideTicks's own comment (Serial.c/
		 * BitWarden.h) for why the FIRST wait above must stay at the long
		 * default. Matches WiRSa's own per-item patience closely enough
		 * that neither side waits drastically longer than the other has
		 * already given up. Idempotent - harmless to set every iteration. */
		gBwIdleOverrideTicks = SysTicksPerSecond() * 12;
		BwTouchSessionKeys();

		if (rxType == BW_FOLDER) {
			UInt16 idx = rU16(&r);
			rStr(&r, gSyncNameBuf, sizeof(gSyncNameBuf));
			if (r.ok && !BwStoreAddFolder(idx, gSyncNameBuf)) {
				/* Sec35 (or the local vault) locking mid-sync used to mean
				 * every write from here on silently no-op'd while the sync
				 * kept "succeeding" - the previous vault is still fully
				 * intact (BwStoreAbortSync restores it untouched), so
				 * abort now rather than let a partial result look clean. */
				BwProgressHide();
				FrmCustomAlert(altErr, "Sec35 or the local vault locked mid-sync. Sync aborted to protect your data - unlock and sync again.", NULL, NULL);
				BwSerialSendFrame(BW_BYE, NULL, 0);
				TryCatchTrailingToken();
				BwSerialClose();
				BwStoreAbortSync();
				return false;
			}
			folderCount++;
			BwProgressSetStatus("Syncing folders...");
			if (!SendItemAckWithRetry()) {
				BwProgressHide();
				ShowLostContactAlert('F');
				BwSerialSendFrame(BW_BYE, NULL, 0); /* best-effort - see the recv-failure case above for why */
				TryCatchTrailingToken();
				BwSerialClose();
				BwStoreAbortSync();	/* leave every previously-synced record intact - see BitWarden.h's own note */
				return false;
			}
			BwKeepDeviceAwake();
			BwTouchActivity();
			BwStoreCompactIfDue();
		} else if (rxType == BW_ORG) {
			UInt16 idx = rU16(&r);
			rStr(&r, gSyncNameBuf, sizeof(gSyncNameBuf));
			if (r.ok && !BwStoreAddOrg(idx, gSyncNameBuf)) {
				/* See the BW_FOLDER branch's own comment. */
				BwProgressHide();
				FrmCustomAlert(altErr, "Sec35 or the local vault locked mid-sync. Sync aborted to protect your data - unlock and sync again.", NULL, NULL);
				BwSerialSendFrame(BW_BYE, NULL, 0);
				TryCatchTrailingToken();
				BwSerialClose();
				BwStoreAbortSync();
				return false;
			}
			orgCount++;
			BwProgressSetStatus("Syncing organizations...");
			if (!SendItemAckWithRetry()) {
				BwProgressHide();
				ShowLostContactAlert('O');
				BwSerialSendFrame(BW_BYE, NULL, 0); /* best-effort - see the recv-failure case above for why */
				TryCatchTrailingToken();
				BwSerialClose();
				BwStoreAbortSync();	/* leave every previously-synced record intact - see BitWarden.h's own note */
				return false;
			}
			BwKeepDeviceAwake();
			BwTouchActivity();
			BwStoreCompactIfDue();
		} else if (rxType == BW_SYNC_ITEM) {
			MemSet(&gSyncItem, sizeof(gSyncItem), 0);
			rStr(&r, gSyncItem.guid, sizeof(gSyncItem.guid));
			gSyncItem.itemType = rU8(&r);
			gSyncItem.folderIndex = rU16(&r);
			gSyncItem.orgIndex = rU16(&r);
			gSyncItem.archived = (Boolean) (rU8(&r) != 0);
			rStr(&r, gSyncItem.name, sizeof(gSyncItem.name));
			rStr(&r, gSyncItem.notes, sizeof(gSyncItem.notes));
			/* Type-specific tail - see PROTOCOL.md's own SYNC_ITEM section.
			 * Nothing in the frame itself says how many strings follow;
			 * both sides derive it from itemType, already read above. */
			switch (gSyncItem.itemType) {
			case bwTypeCard:
				rStr(&r, gSyncItem.u.card.holder, sizeof(gSyncItem.u.card.holder));
				rStr(&r, gSyncItem.u.card.brand, sizeof(gSyncItem.u.card.brand));
				rStr(&r, gSyncItem.u.card.number, sizeof(gSyncItem.u.card.number));
				rStr(&r, gSyncItem.u.card.expMonth, sizeof(gSyncItem.u.card.expMonth));
				rStr(&r, gSyncItem.u.card.expYear, sizeof(gSyncItem.u.card.expYear));
				rStr(&r, gSyncItem.u.card.code, sizeof(gSyncItem.u.card.code));
				break;
			case bwTypeIdentity:
				rStr(&r, gSyncItem.u.identity.title, sizeof(gSyncItem.u.identity.title));
				rStr(&r, gSyncItem.u.identity.firstName, sizeof(gSyncItem.u.identity.firstName));
				rStr(&r, gSyncItem.u.identity.middleName, sizeof(gSyncItem.u.identity.middleName));
				rStr(&r, gSyncItem.u.identity.lastName, sizeof(gSyncItem.u.identity.lastName));
				rStr(&r, gSyncItem.u.identity.username, sizeof(gSyncItem.u.identity.username));
				rStr(&r, gSyncItem.u.identity.company, sizeof(gSyncItem.u.identity.company));
				rStr(&r, gSyncItem.u.identity.ssn, sizeof(gSyncItem.u.identity.ssn));
				rStr(&r, gSyncItem.u.identity.passportNumber, sizeof(gSyncItem.u.identity.passportNumber));
				rStr(&r, gSyncItem.u.identity.licenseNumber, sizeof(gSyncItem.u.identity.licenseNumber));
				rStr(&r, gSyncItem.u.identity.email, sizeof(gSyncItem.u.identity.email));
				rStr(&r, gSyncItem.u.identity.phone, sizeof(gSyncItem.u.identity.phone));
				rStr(&r, gSyncItem.u.identity.address1, sizeof(gSyncItem.u.identity.address1));
				rStr(&r, gSyncItem.u.identity.address2, sizeof(gSyncItem.u.identity.address2));
				rStr(&r, gSyncItem.u.identity.address3, sizeof(gSyncItem.u.identity.address3));
				rStr(&r, gSyncItem.u.identity.city, sizeof(gSyncItem.u.identity.city));
				rStr(&r, gSyncItem.u.identity.state, sizeof(gSyncItem.u.identity.state));
				rStr(&r, gSyncItem.u.identity.postalCode, sizeof(gSyncItem.u.identity.postalCode));
				rStr(&r, gSyncItem.u.identity.country, sizeof(gSyncItem.u.identity.country));
				break;
			case bwTypeNote:
				break;	/* nothing extra - notes above already covers it */
			default:	/* bwTypeLogin */
				rStr(&r, gSyncItem.u.login.username, sizeof(gSyncItem.u.login.username));
				rStr(&r, gSyncItem.u.login.password, sizeof(gSyncItem.u.login.password));
				rStr(&r, gSyncItem.u.login.uri, sizeof(gSyncItem.u.login.uri));
				rStr(&r, gSyncItem.u.login.totp, sizeof(gSyncItem.u.login.totp));
				break;
			}
			/* BwStoreAddItem is a pure local Data Manager operation - no
			 * serial waiting, so none of PollKeepAlive's own keep-alive
			 * calls run during it. DmNewRecord can occasionally stall well
			 * over a minute (storage heap compaction is the leading
			 * theory), confirmed on real hardware NOT caused by the device
			 * sleeping (screen stayed on throughout). BwKeepDeviceAwake()
			 * below is kept for its own auto-off purpose regardless; the
			 * actual fix for the resulting ack-send failures is the retry
			 * logic in SendItemAckWithRetry. */
			BwKeepDeviceAwake();
			BwTouchActivity();
			/* Runs before BwStoreAddItem, not after - compacting only
			 * after an item was already added can't help that item's own
			 * DmNewRecord call if fragmentation was already bad enough to
			 * trigger an internal compaction inside it. */
			BwStoreCompactIfDue();
			{
				Boolean itemStored = !r.ok || BwStoreAddItem(&gSyncItem);
				MemSet(&gSyncItem, sizeof(gSyncItem), 0);
				if (!itemStored) {
					/* See the BW_FOLDER branch's own comment - same
					 * real-device data-loss report. */
					BwProgressHide();
					FrmCustomAlert(altErr, "Sec35 or the local vault locked mid-sync. Sync aborted to protect your data - unlock and sync again.", NULL, NULL);
					BwSerialSendFrame(BW_BYE, NULL, 0);
					TryCatchTrailingToken();
					BwSerialClose();
					BwStoreAbortSync();
					return false;
				}
			}
			itemCount++;
			if (!SendItemAckWithRetry()) {
				BwProgressHide();
				ShowLostContactAlert('I');
				BwSerialSendFrame(BW_BYE, NULL, 0); /* best-effort - see the recv-failure case above for why */
				TryCatchTrailingToken();
				BwSerialClose();
				BwStoreAbortSync();	/* leave every previously-synced record intact - see BitWarden.h's own note */
				return false;
			}
			/* Fresh auto-off window right here, not just whatever's left
			 * over from PollKeepAlive's own throttled-to-15s resets - at
			 * least a full auto-off cycle (PalmOS's minimum 30s) of margin
			 * for WiRSa to prepare and send the next item. */
			BwKeepDeviceAwake();
			BwTouchActivity();
			{
				char status[32];
				StrPrintF(status, "Syncing... %u items", itemCount);
				BwProgressSetStatus(status);
				BwProgressBar(itemCount);
			}
		} else if (rxType == BW_TWO_FACTOR_REQUIRED) {
			/* Vaultwarden 2FA challenge mid-sync - see PROTOCOL.md's own
			 * TWO_FACTOR_REQUIRED/TWO_FACTOR_SUBMIT section. WiRSa/mock
			 * keeps its TLS connection + derived keys alive across this
			 * pause; this is a detour in the same sync attempt, not a new
			 * one. Same hide-progress/show-modal-prompt/reshow-progress
			 * shape already used for the TRUST prompt above. */
			UInt8 provider = rU8(&r);
			char label[100];
			char code[32];
			rStr(&r, label, sizeof(label));
			(void) provider;	/* not surfaced beyond the label text WiRSa already built */
			if (!r.ok) {
				BwProgressHide();
				FrmCustomAlert(altErr, "Malformed two-factor request from WiRSa.", NULL, NULL);
				ok = false;
				break;
			}
			BwProgressHide();
			if (!BwTwoFactorPromptRun(label, code, sizeof(code))) {
				/* User cancelled - tell WiRSa so it can stop cleanly
				 * instead of sitting on its own 180s wait until it times out. */
				BwSerialSendFrame(BW_BYE, NULL, 0);
				BwSerialClose();
				BwStoreAbortSync();	/* leave every previously-synced record intact - see BitWarden.h's own note */
				return false;
			}
			/* BwTwoFactorPromptRun just blocked on the user, possibly for
			 * a while - explicitly touch again now, right before the sync
			 * resumes its own BwStoreAddItem-heavy work, in case the
			 * response took close to the full TTL. */
			BwTouchSessionKeys();
			BwProgressShow("Verifying code...");
			{
				BwW cw;
				cw.buf = gSyncWBuf; cw.cap = sizeof(gSyncWBuf); cw.len = 0;
				wStr(&cw, code);
				if (!BwSerialSendFrame(BW_TWO_FACTOR_SUBMIT, gSyncWBuf, cw.len)) {
					BwProgressHide();
					ShowLostContactAlert('T');
					ok = false;
					MemSet(code, sizeof(code), 0);
					break;
				}
			}
			MemSet(code, sizeof(code), 0);
			/* Loop continues - WiRSa responds with either another
			 * TWO_FACTOR_REQUIRED (wrong code), SYNC_ERROR, or the normal
			 * FOLDER/SYNC_ITEM/SYNC_DONE flow. */
		} else if (rxType == BW_SYNC_DONE) {
			/* itemCount/folderCount/orgCount aren't read here - the
			 * Palm's own counters are authoritative; SYNC_DONE's copies
			 * exist for WiRSa-side/debug-log symmetry only.
			 * updatedTwoFactorToken is new information - empty means no
			 * change, leave whatever's already stored untouched. */
			(void) rU16(&r); (void) rU16(&r); (void) rU16(&r);
			rStr(&r, gNewTokenBuf, sizeof(gNewTokenBuf));
			if (r.ok && gNewTokenBuf[0]) BwSetTwoFactorRemember(gNewTokenBuf);
			MemSet(gNewTokenBuf, sizeof(gNewTokenBuf), 0);
			ok = true;
			break;
		} else if (rxType == BW_SYNC_ERROR) {
			UInt8 code = rU8(&r);
			char msg[128];
			rStr(&r, msg, sizeof(msg));
			/* updatedTwoFactorToken, same field/convention SYNC_DONE
			 * carries - Vaultwarden rotates (invalidates) the
			 * remember-device token the moment login succeeds, regardless
			 * of what happens afterward, so a sync that fails after a
			 * successful login still needs to pick up the fresh token or
			 * every later attempt falls back to a fresh interactive 2FA
			 * prompt even though 2FA had just been completed. */
			rStr(&r, gNewTokenBuf, sizeof(gNewTokenBuf));
			if (r.ok && gNewTokenBuf[0]) BwSetTwoFactorRemember(gNewTokenBuf);
			MemSet(gNewTokenBuf, sizeof(gNewTokenBuf), 0);
			BwProgressHide();
			FrmCustomAlert(altErr2, "SYNC", msg, NULL);
			ok = false;
			/* A cached password that WiRSa's own auth just rejected (e.g.
			 * the real Bitwarden password changed since it was cached)
			 * must not keep getting silently retried. */
			if (code == BW_ERR_AUTH_FAILED) BwWipeCachedPassword();
			break;
		} else {
			/* unknown frame type mid-sync - ignore and keep waiting, same
			 * forward-compatible tolerance as bitwarden_relay.cpp's own
			 * default case */
		}
	}

	/* BYE and close BEFORE finishing the vault: BwStoreEndSync (sort and
	 * replace) takes well over WiRSa's 15 s negotiate timeout on a large
	 * vault, after which WiRSa is back at 9600 and a late BYE at the
	 * negotiated rate reaches it as junk (found on hardware - Screenless
	 * Mode then couldn't send WiRSa back to its menu). Same reason CalDAV
	 * Sync commits its calendars only after its own BYE. */
	BwSerialSendFrame(BW_BYE, NULL, 0);
	BwSerialClose();

	if (ok) {
		BwStoreEndSync();
		BwProgressHide();
	} else {
		/* Shared exit for every "ok=false; break;" path (2FA malformed,
		 * 2FA-submit send failure, SYNC_ERROR) - the handful of direct
		 * `return false;` sites elsewhere in this function each call this
		 * same cleanup themselves, since they bypass this shared ending. */
		BwStoreAbortSync();
	}
	return ok;
}
