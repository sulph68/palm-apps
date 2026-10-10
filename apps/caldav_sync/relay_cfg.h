/*
 * relay_cfg.h - CalDAV Sync's settings for the shared ../lib/relayproto.c
 * (included only by that file and Serial.c; found via the Makefile's -I.).
 */
#ifndef CDDV_RELAY_CFG_H
#define CDDV_RELAY_CFG_H

#include "Serial.h"
#include "SyncProgress.h"

#define RP_RECV_BUF_SIZE	4096
/* Single-buffer send (relayproto.c) - CalDAV's original send path. */
#define RP_SEND_BODY_MAX	cddvMaxFrameBody

void CDDVSerialPoll(void);

#define RP_APP_POLL()		CDDVSerialPoll()
#define RP_APP_CANCELLED()	SyncProgressCancelled()
#define RP_APP_ON_OPEN()	(gRpLastLineErrs = 0)

#endif
