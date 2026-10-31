#ifndef CDDV_SYNCENGINE_H
#define CDDV_SYNCENGINE_H

#include "CalDAVSync.h"

/* Runs a full sync for one slot: connects to WiRSa, negotiates baud,
 * sends SYNC_REQUEST, receives+ACKs EVENT_ITEM frames, writes them via
 * DAVDb.c. Blocking, shows its own progress UI. Returns true on success.
 *
 * silent: suppresses the modal failure alert (error still recorded into
 * lastSyncErrMsg) and skips the Sec35-unlock bounce - lets a Sync All loop
 * continue past one failed slot instead of stalling on a tap.
 *
 * keepOpen: on success, leaves the connection open and negotiated for the
 * next call to reuse (skips its own HELLO). Always closes on failure. */
Boolean SyncEngineRunOne(CDDVPrefsType *p, UInt8 slotIdx, Boolean silent, Boolean keepOpen) SEC004;

/* Screenless Mode (Preferences): put WiRSa into CalDAV Relay before a
 * session (Sync Now, Sync All, Google sign-in) and back to its main menu
 * after. Both do nothing when Screenless Mode is off. Begin returns false
 * only if WiRSa refused (alert shown) - skip the session then; End is
 * always called. */
Boolean SyncScreenlessBegin(void) SEC004;
void SyncScreenlessEnd(void) SEC004;

#endif
