/*
 * Serial.h - wire protocol to WiRSa's CalDAV Relay mode. Frame format:
 * type(1B) + lenHi(1B) + lenLo(1B) + body + checksum(1B XOR).
 */
#ifndef CDDV_SERIAL_H
#define CDDV_SERIAL_H

#include "CalDAVSync.h"

/* Max single-frame body size - must match WiRSa's own CDDV_MAX_BODY. */
#define cddvMaxFrameBody	3192
#define cddvCapFlagKex		0x01	/* reserved, unset in this build */

/* The confirmed baud switch. HELLO = baud u32, then an optional flags byte: cddvHelloOffer ("I will confirm before I switch") on
 * the first HELLO, cddvHelloConfirm on the second, which says the reply was read. A WiRSa that supports it sets
 * cddvCapHelloConfirm in the reply's capability byte and moves its port only on the confirm; an older one ignores the byte,
 * replies with 0 and moves at once. A reply that comes back damaged is answered with a new HELLO, up to cddvHelloTries. */
#define cddvHelloOffer		0x01
#define cddvHelloConfirm	0x02
#define cddvCapHelloConfirm	0x02
#define cddvHelloTries		3
#define cddvMaxRxType		0x26	/* the highest frame type WiRSa sends (CDDV_OAUTH_TOKEN) */

/* The type numbers follow Protocol-wire.md (WiRSa docs/protocol), section 6: the common
 * frames at 0x01-0x06, CalDAV's own in 0x20-0x2F. */
typedef enum {
	CDDV_HELLO = 0x01,
	CDDV_HELLO_ACK = 0x02,
	CDDV_BYE = 0x03,
	CDDV_ACK = 0x04,
	CDDV_SYNC_DONE = 0x05,
	CDDV_ERR = 0x06,		/* WiRSa -> Palm: code u8, message str */
	CDDV_SYNC_REQUEST = 0x20,
	CDDV_OAUTH_START = 0x21,
	CDDV_EVENT_ITEM = 0x22,
	/* contacts slots (caldav_relay.cpp WIRSA_FEATURE_CARDDAV), each acked
	 * with CDDV_ACK; body layouts in ContactDb.h / carddav_sync.md §2.4 */
	CDDV_CONTACT_ITEM = 0x23,
	CDDV_GROUP_ITEM = 0x24,
	CDDV_OAUTH_INFO = 0x25,
	CDDV_OAUTH_TOKEN = 0x26
} CDDVFrameType;

/* The HELLO_ACK version this app needs: 2 = the numbers above. An older WiRSa reports less
 * (or sends the rate alone) and uses the old numbers, so the app stops with a message
 * instead of waiting for frames that never come. */
#define cddvWireVersion	2

/* Framing lives in ../lib/relayproto.c (shared with BitWarden); these keep
 * this app's historical names. ACK/retry orchestration stays in
 * SyncEngine.c. gCddvLastRecvErr/gCddvLastLineErrs: last genuine receive
 * fault (0/0 after a plain timeout), for the "lost contact" message. */
#include "relayproto.h"
#define CDDVSerialOpen		RpOpen
#define CDDVSerialClose		RpClose
#define CDDVSerialSetBaud	RpSetBaud
#define CDDVSerialSendFrame	RpSendFrame
#define CDDVSerialRecvFrame	RpRecvFrame
#define CDDVSerialFlushInput	RpFlushInput
/* The HELLO_ACK is read by hunting for it (RpRecvFrameHunt): it has no lead byte and no retry, and over a wireless link a burst of
 * noise can arrive in front of it. */
#define CDDVSerialRecvHelloAck(body, cap, len)	RpRecvFrameHunt(CDDV_HELLO_ACK, body, cap, len)
#define gCddvLastRecvErr	gRpLastRecvErr
#define gCddvLastLineErrs	gRpLastLineErrs

#endif
