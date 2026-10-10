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

#endif
