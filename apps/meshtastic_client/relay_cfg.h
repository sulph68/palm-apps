/*
 * relay_cfg.h - the Meshtastic client's settings for ../lib/relayproto.c and relaysec.c. The longest
 * frame is a message (about 270 bytes) or a node (about 140); every request has its own timeout.
 */
#ifndef MC_RELAY_CFG_H
#define MC_RELAY_CFG_H

#include <PalmOS.h>

#define RP_RECV_BUF_SIZE	1024
#define RP_SEND_BODY_MAX	320

void McPoll(void);
Boolean McCancelled(void);

#define RP_APP_POLL()		McPoll()
#define RP_APP_CANCELLED()	McCancelled()
#define WCTL_CANCELLED()	McCancelled()
#define RP_APP_ON_OPEN()	(gRpLastLineErrs = 0)

#endif
