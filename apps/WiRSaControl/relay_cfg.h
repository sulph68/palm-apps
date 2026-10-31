/*
 * relay_cfg.h - WiRSaControl's settings for ../lib/relayproto.c and
 * relaysec.c. Control frames are small; every request has its own short
 * timeout, so there is no Cancel.
 */
#ifndef WCS_RELAY_CFG_H
#define WCS_RELAY_CFG_H

#include <PalmOS.h>

#define RP_RECV_BUF_SIZE	1024
#define RP_SEND_BODY_MAX	256

#define RP_APP_POLL()		EvtResetAutoOffTimer()
#define RP_APP_CANCELLED()	(false)
#define RP_APP_ON_OPEN()	(gRpLastLineErrs = 0)

#endif
