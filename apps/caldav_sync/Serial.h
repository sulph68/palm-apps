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

typedef enum {
	CDDV_HELLO = 0x01,
	CDDV_HELLO_ACK = 0x02,
	CDDV_SYNC_REQUEST = 0x03,
	CDDV_EVENT_ITEM = 0x04,
	CDDV_ITEM_ACK = 0x05,
	CDDV_SYNC_DONE = 0x06,
	CDDV_SYNC_ERROR = 0x07,
	CDDV_OAUTH_START = 0x08,
	CDDV_OAUTH_INFO = 0x09,
	CDDV_OAUTH_TOKEN = 0x0A,
	CDDV_BYE = 0x0B
} CDDVFrameType;

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
#define gCddvLastRecvErr	gRpLastRecvErr
#define gCddvLastLineErrs	gRpLastLineErrs

#endif
