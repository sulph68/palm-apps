#ifndef _FRAG_H_
#define _FRAG_H_

#include <stdbool.h>
#include <stdint.h>
#include "bufvec.h"
#include "slk.h"

//this is the encapsulation layer hotsync over serial uses
//it is similar to SCTP and builds on top of slk.{h,c}'s unreliable datagram ability

//e do not allow zero-length ACKS for convenience. we do accept them


#define FRAG_MAX_RESENDS			4
#define FRAG_ACK_WAIT_TIME			21		//150ms
#define FRAG_MAX_CHUNK_SZ			524		//must be at least ten since initial negotiation requires non-fragmented packets and uses a 10-byte payload

struct FragSendState {
	uint16_t curPos;
	uint16_t totalLen;
	struct SlkHdr slkHdr;
};

struct FragAckInfo {
	uint16_t ofst;
	uint8_t flags;
};


//slk header is just for addressing and packet typing
bool fragRecv(void *slkState, struct SlkHdr *slkHdr, void *dstP, uint16_t *lenP /* in / out */, uint32_t endTime, bool autoAck, struct FragAckInfo *fi);

//tid taken from slkHdr
bool fragSend(void *slkState, struct SlkHdr *slkHdr, const void *srcP, uint16_t len, struct bv *bufAckRx);
#ifdef SASD_TOL
//two single-fragment requests back to back (A then B); true when B's reply was seen. *okA tells whether A's reply was seen too.
bool fragSendPair(void *slkState, struct SlkHdr *hA, const void *srcA, uint16_t lenA, struct SlkHdr *hB, const void *srcB, uint16_t lenB, uint8_t tries, bool *okA);
#endif
bool fragAck(void *slkState, struct SlkHdr *reqSlkHdr, struct FragAckInfo *fi, void *srcP, uint16_t len);


bool fragSendStartData(void *slkState, struct FragSendState *fs, struct SlkHdr *slkHdr, uint16_t totalLen);
bool fragSendContinue(void *slkState, struct FragSendState *fs, const void *srcP, uint16_t len, struct bv *bufAckRx);	//auto-finishes when you provide enough data





#endif
