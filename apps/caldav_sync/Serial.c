/*
 * Serial.c - CalDAV Sync's keep-alive hook for the shared serial framing
 * (../lib/relayproto.c, configured by relay_cfg.h). Runs about once a
 * second during every blocking read; also the place that notices a Cancel
 * tap. Must stay in the main segment (blocking-read path).
 */
#include "relay_cfg.h"

void CDDVSerialPoll(void)
{
	EventType ev;
	EvtResetAutoOffTimer();
	while (EvtGetEvent(&ev, 0), ev.eType != nilEvent) {
		if (ev.eType == penDownEvent) SyncProgressNotePenDown(ev.screenX, ev.screenY);
		else if (ev.eType == penUpEvent) SyncProgressNotePenUp(ev.screenX, ev.screenY);
	}
}
