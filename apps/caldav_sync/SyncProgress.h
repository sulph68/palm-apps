#ifndef CDDV_SYNCPROGRESS_H
#define CDDV_SYNCPROGRESS_H

#include "CalDAVSync.h"

/* Status+progress-bar modal shown for the duration of a sync (user
 * request, 2026-09-17: "Sync Now" gave no feedback at all while WiRSa
 * could be stuck retrying/reconnecting for many seconds - "does not
 * follow the style of the BitWarden app where status is always shown").
 * Mirrors BitWarden's own BwProgressShow/SetStatus/Bar/Hide (Dialogs.c,
 * read as reference, original code here) - same centered-modal shape,
 * same hand-drawn sawtooth bar (no true total to compute a real
 * percentage against). */
void SyncProgressShow(const char *title) SEC001;
void SyncProgressSetStatus(const char *status) SEC001;
void SyncProgressBar(UInt16 done) SEC001;
void SyncProgressHide(void) SEC001;

/* Cancel button (user request, 2026-09-17: "no Cancel button like in
 * BitWarden"). Mirrors BitWarden's own SyncProgressNotePenDown/Up/
 * Cancelled (Dialogs.c) exactly, including WHY it's a direct bounds check
 * against the pen event rather than dispatched through FrmHandleEvent
 * (crashed lazyFTP's own equivalent from inside a tight synchronous drain
 * loop) and WHY it requires BOTH the penDownEvent and penUpEvent to land
 * on the button (a real-device BitWarden bug: checking only where the
 * penUpEvent landed let a stray touch/drag merely passing through the
 * button's small rectangle count as a tap). Called from Serial.c's own
 * PollKeepAlive, which already runs roughly once a second during every
 * blocking wait in a sync - not just while items are streaming. */
void SyncProgressNotePenDown(Int16 x, Int16 y) SEC001;
void SyncProgressNotePenUp(Int16 x, Int16 y) SEC001;
Boolean SyncProgressCancelled(void) SEC001;

#endif
