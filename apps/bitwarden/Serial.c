/*
 * Serial.c - BitWarden's keep-alive hook for the shared serial framing
 * (../lib/relayproto.c, configured by relay_cfg.h). Runs about once a
 * second during every blocking read in a sync.
 *
 * Must stay a plain main-segment function: tagging the blocking-read path
 * SEC001 once caused a real-device Fatal Exception on every sync.
 */
#include "BitWarden.h"
#include "relay_cfg.h"

#define bwAutoOffResetTicks	(SysTicksPerSecond() * 15)

UInt32 gBwLastAutoOffResetTick = 0;

void BwSerialPoll(void)
{
	UInt32 now = TimGetTicks();
	if (now - gBwLastAutoOffResetTick >= bwAutoOffResetTicks) {
		/* BwKeepDeviceAwake (BitWarden.h), not EvtResetAutoOffTimer() -
		 * the latter resets to the user's own default and would undo a
		 * longer floor set elsewhere in this sync (around BwStoreAddItem's
		 * occasional 100+s stalls). */
		BwKeepDeviceAwake();
		gBwLastAutoOffResetTick = now;
	}
	{
		EventType ev;
		/* The one place that notices a Cancel tap promptly during a sync -
		 * see BwProgressNotePenUp (Dialogs.c) for why this is a direct
		 * bounds check rather than dispatching through FrmHandleEvent.
		 * Every other event type is silently discarded here. */
		while (EvtGetEvent(&ev, 0), ev.eType != nilEvent) {
			if (ev.eType == penDownEvent) BwProgressNotePenDown(ev.screenX, ev.screenY);
			else if (ev.eType == penUpEvent) BwProgressNotePenUp(ev.screenX, ev.screenY);
		}
	}
	/* Keeps EventLoop's own sleep-gap check (BitWarden.h) from mistaking a
	 * long-but-legitimate sync for the device having slept. */
	BwTouchActivity();
	/* Keeps the local-vault/Sec35/cached-password TTL from expiring during
	 * a long stall between frames (see BwStoreAddItem's own comment). */
	BwTouchSessionKeys();
}
