/*
 * relayproto.h - shared Palm-side serial framing for every WiRSa relay app
 * (BitWarden, CalDAV Sync, and any future one). Palm counterpart of WiRSa's
 * own relay_proto.h/.cpp.
 *
 * Frame format: type(1B) + lenHi(1B) + lenLo(1B) + body + checksum(1B),
 * checksum = XOR of type, lenHi, lenLo and every body byte.
 *
 * Compiled separately into each app (same pattern as aes.c/enccore.c). Each
 * app supplies:
 *   - RP_SECTION (in its force-included seg header): segment for
 *     RpSendFrame and the relaywire.c helpers. Empty = main.
 *   - relay_cfg.h (found via the app's own -I.), included only by
 *     relayproto.c: RP_RECV_BUF_SIZE, RP_APP_POLL(), RP_APP_CANCELLED(),
 *     RP_APP_ON_OPEN().
 *
 * Open/SetBaud/Close/RecvFrame stay in the main segment on purpose - both
 * apps hit real-device crashes when the blocking-read path was tagged into a
 * secondary segment.
 */
#ifndef RELAYPROTO_H
#define RELAYPROTO_H

#include <PalmOS.h>

#ifndef RP_SECTION
#define RP_SECTION
#endif

/* Both ends bootstrap every connection at this rate, then negotiate up via
 * the app's own HELLO/HELLO_ACK exchange and RpSetBaud. */
#define rpBaudIdle	9600UL

/* Last genuine SrmReceive error / SrmGetStatus lineErrs bitmask (never set
 * by a plain timeout) - lets callers say which fault ended a sync. */
extern Err gRpLastRecvErr;
extern UInt16 gRpLastLineErrs;

/* 0 = default 180s wait for a frame's first byte; non-zero = this many
 * ticks instead. Reset to 0 by RpOpen. */
extern UInt32 gRpIdleOverrideTicks;

Boolean RpOpen(void);
void RpClose(void);
/* Close+reopen at the new rate (SrmControl/srmCtlSetBaudRate does not take
 * effect on this device's cradle-port driver). No-op if not open. */
void RpSetBaud(UInt32 baud);

/* The section tag must be visible at every call site - the compiler picks
 * near vs far call from this declaration, and a mismatch is a wild jump. */
Boolean RpSendFrame(UInt8 type, const UInt8 *body, UInt16 bodyLen) RP_SECTION;
/* Discards whatever is waiting in the receive buffer. */
void RpFlushInput(void) RP_SECTION;
/* false on timeout, cancel, line error, oversized (drained) frame or
 * checksum mismatch; never partial. */
Boolean RpRecvFrame(UInt8 *typeOut, UInt8 *body, UInt16 bodyCap, UInt16 *bodyLenOut);

#endif
