/*
 * relay_cfg.h - BitWarden's settings for the shared ../lib/relayproto.c
 * (included only by that file and Serial.c; found via the Makefile's -I.).
 */
#ifndef BW_RELAY_CFG_H
#define BW_RELAY_CFG_H

#include "BitWarden.h"

/* Frames up to ~1.6 KB arrive back to back (WiRSa streams from a queue); 4 KB fits since Sync.c shares one item buffer. */
#define RP_RECV_BUF_SIZE	4096

extern UInt32 gBwLastAutoOffResetTick;
void BwSerialPoll(void);

#define RP_APP_POLL()		BwSerialPoll()
#define RP_APP_CANCELLED()	BwProgressCancelled()
#define RP_APP_ON_OPEN()	(gBwLastAutoOffResetTick = TimGetTicks())

#endif
