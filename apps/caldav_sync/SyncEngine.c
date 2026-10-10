/*
 * SyncEngine.c - per-slot sync orchestration: negotiate baud, send
 * SYNC_REQUEST, receive+ACK EVENT_ITEM frames into DAVDb.c, until
 * SYNC_DONE/SYNC_ERROR/timeout.
 */
#include "SyncEngine.h"
#include "Serial.h"
#include "CDDVWire.h"
#include "relaysec.h"
#include "CDDVSec35.h"
#include "CDDVPrefs.h"	/* CDDVGetBaud() - offered rate for CDDV_HELLO negotiation */
#include "DAVDb.h"
#include "SyncProgress.h"

#define cddvItemAckRetries    40
#define cddvItemAckDelayTicks (SysTicksPerSecond() / 2)	/* 500ms */

/* Resolves the past/future window in effect for this slot. Popup selection
 * 0 means "Default" (inherit p->pastDefault/futureDefault); a real
 * override value is (sel - 1). */
static void ResolveWindow(const CDDVPrefsType *p, const CDDVSlot *s, UInt8 *pastOut, UInt8 *futureOut)
{
	*pastOut = s->pastOverrideSel ? (UInt8) (s->pastOverrideSel - 1) : (UInt8) p->pastDefault;
	*futureOut = s->futureOverrideSel ? (UInt8) (s->futureOverrideSel - 1) : (UInt8) p->futureDefault;
}

static Boolean SendAckWithRetry(void)
{
	UInt8 i;
	for (i = 0; i < cddvItemAckRetries; i++) {
		if (CDDVSerialSendFrame(CDDV_ITEM_ACK, NULL, 0)) return true;
		SysTaskDelay(cddvItemAckDelayTicks);
	}
	return false;
}

/* Static, not stack-local - this platform's default stack is too small for
 * buffers this size. Safe shared since only one sync runs at a time. */
static UInt8 gSyncBody[cddvMaxFrameBody];
static char gSyncSecret[cddvSecretMaxLen];
static CDDVEvent gSyncEv;
static CDDVR gSyncR;
static char gSyncErrMsg[128];
static char gSyncStatusBuf[cddvAliasLen + 32];

/* Plaintext decrypts of this slot's own Sec35-ciphertext fields (CDDVSec35.h)
 * for the duration of one SyncEngineRunOne call - server/path/uuid/user/
 * alias/googleClientId/googleClientSecret are all ciphertext at rest now,
 * same "explicit companion length, not NUL-safe" reasoning as secret
 * already had. Decrypted once near the top of SyncEngineRunOne, sent over
 * the wire and used for on-screen status text from here, never written
 * back - same "static, not stack-local" reasoning as gSyncSecret above. */
static char gSyncServer[cddvUrlLen];
static char gSyncPath[cddvPathLen];
static char gSyncUuid[40];
static char gSyncUser[cddvUserLen];
static char gSyncAlias[cddvAliasLen];
static char gSyncGoogleClientId[cddvGoogleClientIdLen];
static char gSyncGoogleClientSecret[cddvGoogleClientSecretLen];

/* True once CDDV_HELLO/CDDV_HELLO_ACK has negotiated a baud for the
 * current connection. Cleared only when this file closes the connection,
 * so a keepOpen call that finds the port already open correctly skips
 * renegotiating. */
static Boolean gCddvConnNegotiated = false;

/* keepOpen: on success, leaves the connection open/negotiated instead of
 * sending BYE and closing - lets a Sync All batch reuse one connection
 * across slots. Always closes on failure. */
SEC004 Boolean SyncEngineRunOne(CDDVPrefsType *p, UInt8 slotIdx, Boolean silent, Boolean keepOpen)
{
	CDDVSlot *s;
	CDDVW w;
	UInt8 *body = gSyncBody;
	UInt8 rxType;
	UInt16 rxLen;
	char *secret = gSyncSecret;
	UInt8 pastWin, futureWin;
	Boolean synced = false;
	Boolean rebuildOpen = false;
	UInt16 eventCount = 0;

	if (slotIdx >= cddvMaxSlots) return false;
	s = &p->slots[slotIdx];
	if (s->aliasLen == 0) return false;	/* nothing configured in this slot */

	/* A locked Sec35 would otherwise decrypt every field below to empty and
	 * fail with confusing errors. Every server type needs this now - even
	 * ICS Feed's own "credential" (the feed URL itself, in ->server) is
	 * Sec35 ciphertext same as everything else in this slot, so the old
	 * ICS Feed exemption no longer applies. silent mode never bounces to
	 * Sec35 - would abort the rest of a Sync All batch. */
	if (CDDVSec35Installed() && !CDDVSec35Unlocked()) {
		if (!silent) {
			FrmCustomAlert(altErr, "Sec35 is locked - unlock it, then sync again.", NULL, NULL);
			CDDVSwitchToSec35();
		}
		return false;
	}

	gSyncErrMsg[0] = '\0';

	if (!CDDVSerialOpen()) {
		StrCopy(gSyncErrMsg, "Could not open the serial connection to WiRSa.");
		if (!silent) FrmCustomAlert(altErr, gSyncErrMsg, NULL, NULL);
		goto finish;
	}
	SyncProgressShow("Connecting...");

	/* Baud negotiation: offer this device's configured target via
	 * CDDV_HELLO, WiRSa caps it to its own max and replies via
	 * CDDV_HELLO_ACK, both sides switch. Skipped if already negotiated
	 * (Sync All reusing an open connection). */
	if (!gCddvConnNegotiated) {
		UInt32 negotiatedBaud;

		/* Secure the serial link before anything else goes over it (no
		 * plaintext fallback). No reply or a cancel keeps the existing
		 * "lost contact" handling, as for an unanswered HELLO. */
		{
			UInt8 sr = RpSecure();
			if (sr != rpSecOk) {
				SyncProgressHide();
				StrCopy(gSyncErrMsg, (sr == rpSecFailed) ? "Serial link cannot be secured!" : "Lost contact with WiRSa negotiating baud rate.");
				if (!silent) FrmCustomAlert(altErr, gSyncErrMsg, NULL, NULL);
				goto finish;
			}
		}

		SyncProgressSetStatus("Negotiating rate...");

		w.buf = body; w.cap = sizeof(gSyncBody); w.len = 0;
		cw32(&w, (UInt32) CDDVGetBaud());

		if (!CDDVSerialSendFrame(CDDV_HELLO, body, w.len)) {
			SyncProgressHide();
			StrCopy(gSyncErrMsg, "Lost contact with WiRSa negotiating baud rate.");
			if (!silent) FrmCustomAlert(altErr, gSyncErrMsg, NULL, NULL);
			goto finish;
		}

		if (!CDDVSerialRecvFrame(&rxType, body, sizeof(gSyncBody), &rxLen) || rxType != CDDV_HELLO_ACK) {
			SyncProgressHide();
			StrCopy(gSyncErrMsg, "Lost contact with WiRSa negotiating baud rate.");
			if (!silent) FrmCustomAlert(altErr, gSyncErrMsg, NULL, NULL);
			goto finish;
		}

		gSyncR.buf = body; gSyncR.len = rxLen; gSyncR.pos = 0; gSyncR.ok = true;
		negotiatedBaud = cr32(&gSyncR);
		if (!gSyncR.ok || negotiatedBaud == 0) {
			SyncProgressHide();
			StrCopy(gSyncErrMsg, "WiRSa sent a malformed baud negotiation reply.");
			if (!silent) FrmCustomAlert(altErr, gSyncErrMsg, NULL, NULL);
			goto finish;
		}

		CDDVSerialSetBaud(negotiatedBaud);
		gCddvConnNegotiated = true;
		StrPrintF(gSyncStatusBuf, "Negotiated %ld baud", (Int32) negotiatedBaud);
		SyncProgressSetStatus(gSyncStatusBuf);
	}

	/* sizeof(gSyncSecret)/sizeof(gSyncBody), not sizeof(secret)/sizeof(body) -
	 * those are pointers now. */
	CDDVDecryptSlotSecret(s, slotIdx, secret, sizeof(gSyncSecret));	/* empty for ICS feed, or if Sec35 is locked */
	/* Every other identifying/config field is Sec35 ciphertext at rest too
	 * now (CDDVSec35.h) - decrypted here into the shadow buffers declared
	 * above, which is what actually goes out on the wire below. Leaves
	 * gWorking-equivalent p->slots[slotIdx] untouched (still ciphertext),
	 * same as secret's own decrypt-a-copy pattern just above. */
	CDDVDecryptSlotField(slotIdx, cddvFieldServer, (UInt8 *) s->server, s->serverLen, gSyncServer, sizeof(gSyncServer));
	CDDVDecryptSlotField(slotIdx, cddvFieldPath, (UInt8 *) s->path, s->pathLen, gSyncPath, sizeof(gSyncPath));
	CDDVDecryptSlotField(slotIdx, cddvFieldUuid, (UInt8 *) s->uuid, s->uuidLen, gSyncUuid, sizeof(gSyncUuid));
	CDDVDecryptSlotField(slotIdx, cddvFieldUser, (UInt8 *) s->user, s->userLen, gSyncUser, sizeof(gSyncUser));
	CDDVDecryptSlotField(slotIdx, cddvFieldAlias, (UInt8 *) s->alias, s->aliasLen, gSyncAlias, sizeof(gSyncAlias));
	CDDVDecryptSlotField(slotIdx, cddvFieldGoogleClientId, (UInt8 *) s->googleClientId, s->googleClientIdLen,
		gSyncGoogleClientId, sizeof(gSyncGoogleClientId));
	CDDVDecryptSlotField(slotIdx, cddvFieldGoogleClientSecret, (UInt8 *) s->googleClientSecret, s->googleClientSecretLen,
		gSyncGoogleClientSecret, sizeof(gSyncGoogleClientSecret));
	ResolveWindow(p, s, &pastWin, &futureWin);

	w.buf = body; w.cap = sizeof(gSyncBody); w.len = 0;
	cw8(&w, (UInt8) s->serverType);
	cwStr(&w, gSyncServer);
	cwStr(&w, gSyncPath);
	cwStr(&w, gSyncUuid);
	cwStr(&w, gSyncUser);
	cwStr(&w, secret);
	cw8(&w, pastWin);
	cw8(&w, futureWin);
	/* Palm's own current UTC estimate, so WiRSa doesn't depend on its own
	 * NTP succeeding to compute the sync window. Must stay numerically
	 * consistent with caldav_relay.cpp's UNIX_TO_PALM_EPOCH constant. */
	{
		UInt32 localSecs = TimGetSeconds();
		Int16 tz = (Int16) PrefGetPreference(prefTimeZone);
		Int16 dst = (Int16) PrefGetPreference(prefDaylightSavingAdjustment);
		UInt32 utcPalmEpoch = TimTimeZoneToUTC(localSecs, tz, dst);
		cw32(&w, (UInt32) (utcPalmEpoch - cddvUnixToPalmEpoch));
	}
	/* Appended, not interleaved - same discipline as every other field
	 * added to this frame after the original layout. Google's OAuth Client
	 * ID/Secret live only on the Palm (see CalDAVSync.h) - sent fresh every
	 * sync so WiRSa never has to persist them. Empty strings for every
	 * other server type. */
	cwStr(&w, gSyncGoogleClientId);
	cwStr(&w, gSyncGoogleClientSecret);
	MemSet(secret, sizeof(gSyncSecret), 0);	/* don't leave the plaintext secret around longer than needed */
	MemSet(gSyncGoogleClientId, sizeof(gSyncGoogleClientId), 0);
	MemSet(gSyncGoogleClientSecret, sizeof(gSyncGoogleClientSecret), 0);

	if (!CDDVSerialSendFrame(CDDV_SYNC_REQUEST, body, w.len)) {
		SyncProgressHide();
		StrCopy(gSyncErrMsg, "Lost contact with WiRSa sending the sync request.");
		if (!silent) FrmCustomAlert(altErr, gSyncErrMsg, NULL, NULL);
		goto finish;
	}
	SyncProgressSetStatus("Waiting for WiRSa...");

	if (DAVDbBeginSlotRebuild(slotIdx) != errNone) {
		SyncProgressHide();
		StrCopy(gSyncErrMsg, "Could not open the calendar database.");
		if (!silent) FrmCustomAlert(altErr, gSyncErrMsg, NULL, NULL);
		goto finish;
	}
	rebuildOpen = true;

	while (true) {
		if (!CDDVSerialRecvFrame(&rxType, body, sizeof(gSyncBody), &rxLen)) {
			SyncProgressHide();
			/* A cancel and a genuine dropped connection look identical here -
			 * distinguish via SyncProgressCancelled() before alerting. */
			if (SyncProgressCancelled()) {
				StrCopy(gSyncErrMsg, "Cancelled by user.");
			} else {
				/* err/le: gCddvLastRecvErr/gCddvLastLineErrs, meaningful only
				 * for a genuine line fault (0/0 for a plain timeout or
				 * checksum mismatch). le bits: 1=parity, 2=HW overrun,
				 * 4=framing, 8=break, 0x10=handshake, 0x20=SW overrun,
				 * 0x40=carrier lost. */
				StrPrintF(gSyncErrMsg, "Lost contact with WiRSa mid-sync. Previously synced events for this calendar were left untouched. err=%d le=%d",
					gCddvLastRecvErr, gCddvLastLineErrs);
				if (!silent) FrmCustomAlert(altErr, gSyncErrMsg, NULL, NULL);
			}
			break;
		}

		if (rxType == CDDV_EVENT_ITEM) {
			MemSet(&gSyncEv, sizeof(gSyncEv), 0);
			gSyncR.buf = body; gSyncR.len = rxLen; gSyncR.pos = 0; gSyncR.ok = true;

			crStr(&gSyncR, gSyncEv.uid, sizeof(gSyncEv.uid));
			crStr(&gSyncR, gSyncEv.title, sizeof(gSyncEv.title));
			crStr(&gSyncR, gSyncEv.location, sizeof(gSyncEv.location));
			gSyncEv.utcStart = cr32(&gSyncR);
			gSyncEv.utcEnd = cr32(&gSyncR);
			gSyncEv.allDay = (Boolean) (cr8(&gSyncR) != 0);
			/* Appended fields - must match caldav_relay.cpp's own write order. */
			crStr(&gSyncR, gSyncEv.notes, sizeof(gSyncEv.notes));
			gSyncEv.isRepeating = (Boolean) (cr8(&gSyncR) != 0);
			gSyncEv.sourceSlot = slotIdx;
			StrNCopy(gSyncEv.alias, gSyncAlias, sizeof(gSyncEv.alias) - 1);

			if (gSyncR.ok) {
				/* Timed events arrive as UTC instants - convert to local
				 * here. An all-day event's start/end are its DATE at 00:00
				 * with no zone (iCalendar VALUE=DATE / Google "date"), so it
				 * stays on that date in every time zone, like a native
				 * untimed appointment. */
				Int16 tz = (Int16) PrefGetPreference(prefTimeZone);
				Int16 dst = (Int16) PrefGetPreference(prefDaylightSavingAdjustment);
				gSyncEv.localStart = gSyncEv.allDay ? gSyncEv.utcStart : TimUTCToTimeZone(gSyncEv.utcStart, tz, dst);
				gSyncEv.localEnd = gSyncEv.allDay ? gSyncEv.utcEnd : TimUTCToTimeZone(gSyncEv.utcEnd, tz, dst);
				DAVDbAddEvent(&gSyncEv);
				eventCount++;
			}
			StrPrintF(gSyncStatusBuf, "Fetching data for %s... %u", gSyncAlias, eventCount);
			SyncProgressSetStatus(gSyncStatusBuf);
			SyncProgressBar(eventCount);

			if (!SendAckWithRetry()) {
				SyncProgressHide();
				if (!SyncProgressCancelled()) {
					StrCopy(gSyncErrMsg, "Lost contact with WiRSa mid-sync (ack). Previously synced events for this calendar were left untouched.");
					if (!silent) FrmCustomAlert(altErr, gSyncErrMsg, NULL, NULL);
				}
				break;
			}
			EvtResetAutoOffTimer();
		} else if (rxType == CDDV_SYNC_DONE) {
			synced = true;
			break;
		} else if (rxType == CDDV_SYNC_ERROR) {
			gSyncR.buf = body; gSyncR.len = rxLen; gSyncR.pos = 0; gSyncR.ok = true;
			crStr(&gSyncR, gSyncErrMsg, sizeof(gSyncErrMsg));
			SyncProgressHide();
			if (!gSyncErrMsg[0]) StrCopy(gSyncErrMsg, "WiRSa reported a sync error.");
			if (!silent) FrmCustomAlert(altErr, gSyncErrMsg, NULL, NULL);
			break;
		} else {
			/* Unrecognized frame type - ignore and keep waiting. */
			continue;
		}
	}

	SyncProgressHide();

	if (rebuildOpen) {
		if (synced) DAVDbCommitSlotRebuild();
		else DAVDbAbortSlotRebuild();
	}

finish:
	/* A successful sync (synced=true) with keepOpen leaves the connection
	 * open/negotiated for the next slot to reuse. Any failure always
	 * closes - a broken connection isn't worth preserving. BYE is
	 * best-effort; SendFrame/Close both no-op if never opened. */
	if (!keepOpen || !synced) {
		CDDVSerialSendFrame(CDDV_BYE, NULL, 0);
		CDDVSerialClose();
		gCddvConnNegotiated = false;
	}
	s->lastSyncTime = TimGetSeconds();
	s->lastSyncOk = synced;
	s->lastSyncEventCount = eventCount;
	StrNCopy(s->lastSyncErrMsg, gSyncErrMsg, sizeof(s->lastSyncErrMsg) - 1);
	return synced;
}
