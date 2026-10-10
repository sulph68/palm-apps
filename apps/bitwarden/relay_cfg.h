/*
 * relay_cfg.h - BitWarden's settings for the shared ../lib/relayproto.c
 * (included only by that file and Serial.c; found via the Makefile's -I.).
 */
#ifndef BW_RELAY_CFG_H
#define BW_RELAY_CFG_H

#include "BitWarden.h"

/* 1KB, not larger - datares is essentially full. */
#define RP_RECV_BUF_SIZE	1024

extern UInt32 gBwLastAutoOffResetTick;
void BwSerialPoll(void);

#define RP_APP_POLL()		BwSerialPoll()
#define RP_APP_CANCELLED()	BwProgressCancelled()
#define RP_APP_ON_OPEN()	(gBwLastAutoOffResetTick = TimGetTicks())

#endif
