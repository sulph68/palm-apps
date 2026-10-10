/*
 * OAuthRelay.c - Google sign-in over the CalDAV Relay serial link: sends
 * OAUTH_START, receives WiRSa's IP+PIN via OAUTH_INFO, shows them plus the
 * google_auth.py invocation, waits (bounded, cancellable) for OAUTH_TOKEN,
 * stores the result via CDDVEncryptSlotSecret. Runs at the idle baud the
 * whole time - no CDDV_HELLO negotiation, since OAuth traffic is a
 * handful of short strings, not a calendar sync.
 *
 * Reuses SyncProgress.c's existing modal/Cancel-button machinery for the
 * long wait rather than building a second one - it's already wired into
 * Serial.c's own PollKeepAlive, so a Cancel tap is noticed the same way a
 * sync's own Cancel is. The one visible seam: that modal's title bar text
 * is a fixed "Syncing" (baked into frmSyncProgress's own .rcp resource) -
 * not worth changing, since overriding a form's title at runtime needs the
 * placeholder text to already be at least as long as the real one (a real
 * bug already hit and fixed elsewhere in this suite), and "Syncing" is
 * shorter than "Google Sign-In". The status line underneath makes the
 * actual context clear regardless.
 */
#include "OAuthRelay.h"
#include "Serial.h"
#include "CDDVWire.h"
#include "relaysec.h"
#include "CDDVSec35.h"
#include "SyncProgress.h"

/* ~5 minutes - enough for a real browser consent click-through, matches
 * WiRSa's own listener timeout (CDDV_OAUTH_TIMEOUT_MS, caldav_relay.cpp). */
#define cddvOAuthWaitTicks	(SysTicksPerSecond() * 300)

/* Static, not stack-local - this function sits deep in a modal dialog's
 * own event-handler call chain (SlotEditForm's button tap), matching this
 * suite's established discipline for any multi-hundred-byte scratch buffer. */
static UInt8 gOAuthBody[cddvMaxFrameBody];

SEC004 Boolean OAuthRelayRunGoogleSignIn(CDDVSlot *slot, UInt8 slotIndex)
{
	UInt8 rxType;
	UInt16 rxLen;
	UInt32 deadline;
	Boolean ok = false;
	Boolean cancelled = false;

	if (!CDDVSerialOpen()) {
		FrmCustomAlert(altErr, "Could not open the serial port.", NULL, NULL);
		return false;
	}

	/* Secure the serial link before anything else goes over it (no
	 * plaintext fallback). */
	{
		UInt8 sr = RpSecure();
		if (sr != rpSecOk) {
			FrmCustomAlert(altErr, (sr == rpSecFailed) ? "Serial link cannot be secured!" : "WiRSa did not respond. Make sure CalDAV Relay mode is active on WiRSa.", NULL, NULL);
			CDDVSerialClose();
			return false;
		}
	}

	if (!CDDVSerialSendFrame(CDDV_OAUTH_START, NULL, 0) ||
	    !CDDVSerialRecvFrame(&rxType, gOAuthBody, cddvMaxFrameBody, &rxLen)) {
		FrmCustomAlert(altErr, "WiRSa did not respond. Make sure CalDAV Relay mode is active on WiRSa.", NULL, NULL);
		CDDVSerialClose();
		return false;
	}

	if (rxType == CDDV_SYNC_ERROR) {
		/* WiRSa declined to start (e.g. no Client ID configured there yet) -
		 * the body is the error message it wants shown. */
		char msg[128];
		CDDVR r;
		r.buf = gOAuthBody; r.len = rxLen; r.pos = 0; r.ok = true;
		crStr(&r, msg, sizeof(msg));
		FrmCustomAlert(altErr, r.ok && msg[0] ? msg : "WiRSa reported an error.", NULL, NULL);
		CDDVSerialClose();
		return false;
	}
	if (rxType != CDDV_OAUTH_INFO) {
		FrmCustomAlert(altErr, "Unexpected reply from WiRSa.", NULL, NULL);
		CDDVSerialClose();
		return false;
	}

	{
		char ip[40];
		char pin[8];
		char msg[192];
		UInt16 port;
		CDDVR r;
		r.buf = gOAuthBody; r.len = rxLen; r.pos = 0; r.ok = true;
		crStr(&r, ip, sizeof(ip));
		port = cr16(&r);
		crStr(&r, pin, sizeof(pin));
		if (!r.ok) {
			FrmCustomAlert(altErr, "WiRSa sent a malformed reply.", NULL, NULL);
			CDDVSerialClose();
			return false;
		}
		(void) port;	/* not shown - google_auth.py defaults to the same fixed port WiRSa listens on */
		StrPrintF(msg, "On your computer, run:\n\npython3 google_auth.py --ip %s --pin %s\n\nTap OK, then complete sign-in in your browser.", ip, pin);
		if (FrmCustomAlert(altGoogleOAuthInstructions, msg, NULL, NULL) != 0) {
			/* user tapped Cancel before even starting - still owe WiRSa a
			 * BYE so it doesn't sit holding its own listener open. */
			CDDVSerialSendFrame(CDDV_BYE, NULL, 0);
			CDDVSerialClose();
			return false;
		}
	}

	SyncProgressShow("Google Sign-In");
	SyncProgressSetStatus("Waiting for sign-in...");

	deadline = TimGetTicks() + cddvOAuthWaitTicks;
	for (;;) {
		if (TimGetTicks() > deadline) break;
		if (!CDDVSerialRecvFrame(&rxType, gOAuthBody, cddvMaxFrameBody, &rxLen)) {
			/* CDDVSerialRecvFrame's own internal wait (Serial.c) already
			 * polls for a Cancel tap roughly once a second via the same
			 * PollKeepAlive/SyncProgressCancelled machinery a sync uses -
			 * a false return here is either that, or its own (shorter)
			 * internal idle timeout - either way, loop back and check
			 * which, against our own longer overall deadline. */
			if (SyncProgressCancelled()) { cancelled = true; break; }
			continue;
		}
		if (rxType == CDDV_OAUTH_TOKEN) {
			char token[cddvSecretMaxLen];
			CDDVR r;
			r.buf = gOAuthBody; r.len = rxLen; r.pos = 0; r.ok = true;
			crStr(&r, token, sizeof(token));
			if (r.ok && token[0]) {
				ok = CDDVEncryptSlotSecret(slot, slotIndex, token);
			}
			MemSet(token, sizeof(token), 0);
			break;
		}
		if (rxType == CDDV_SYNC_ERROR) {
			char msg[128];
			CDDVR r;
			r.buf = gOAuthBody; r.len = rxLen; r.pos = 0; r.ok = true;
			crStr(&r, msg, sizeof(msg));
			SyncProgressHide();
			FrmCustomAlert(altErr, r.ok && msg[0] ? msg : "WiRSa reported an error.", NULL, NULL);
			CDDVSerialSendFrame(CDDV_BYE, NULL, 0);
			CDDVSerialClose();
			return false;
		}
		if (SyncProgressCancelled()) { cancelled = true; break; }
		/* unknown/unexpected frame type - keep waiting */
	}

	SyncProgressHide();
	if (!ok && !cancelled) {
		FrmCustomAlert(altErr, "Google sign-in did not complete (timed out, or the token could not be saved).", NULL, NULL);
	}

	CDDVSerialSendFrame(CDDV_BYE, NULL, 0);
	CDDVSerialClose();
	return ok;
}
