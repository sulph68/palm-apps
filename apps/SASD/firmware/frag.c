#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "swap.h"
#include "frag.h"
#include "slk.h"
#include "tol.h"

#pragma pack (push,1)
PACKED_PRE
struct FragHdr {		//fragmentation is the second-level wrapper for all hotsync protocols. there is another version for laager data XFERs, but fuck it
	uint8_t typ;		//1 = data, 2 = ack
	uint8_t flags;		//0x40 = last, 0x80 = first, 0x10 = long header
	uint16_t ofst;		//for fist fragment: size of total data, BE
}
PACKED_POST
;
#pragma pack (pop)

//acks have 2 bytes of garbage data in them. Ask PalmOS why...


#define FRAG_TYP_DATA			1		//data that will be acked
#define FRAG_TYP_ACK			2		//can have payload but is not fragmentable
#define FRAG_FLAG_LARGE			0x10
#define FRAG_FLAG_OOM			0x20
#define FRAG_FLAG_LAST			0x40
#define FRAG_FLAG_FIRST			0x80


static bool fragPrvRecvFrag(void *slkState, struct SlkHdr *slkHdr, struct FragHdr *fragHdr, struct bv *buf, uint32_t endTime)
{
	struct bv recvVec = BV_INIT(fragHdr, sizeof(struct FragHdr), buf);
	
	//we ignore loopback test packets
	do {
		
		//rx
		if (!slkRx(slkState, slkHdr, &recvVec, endTime))
			return false;
		
	} while (slkHdr->pktTyp == PKT_TYP_LOOPBACK_TEST);
	
	//verify that we got at least enough bytes to make up a fragment header
	if (slkHdr->dataLen < sizeof(struct FragHdr))
		return false;
	
	//it could be a packet...
	return true;
}

static bool fragPrvRecvAck(void *slkState, const struct SlkHdr *sentSlkHdr, const struct FragHdr *sentFragHdr, struct bv *buf, uint32_t endTime)
{
	struct FragHdr gotFragHdr;
	struct SlkHdr gotSlkHdr;
	
	while (fragPrvRecvFrag(slkState, &gotSlkHdr, &gotFragHdr, buf, endTime)) {
	
		//make sure packet type, addresses, and TID match
		if (gotSlkHdr.dstSlk != sentSlkHdr->srcSck || gotSlkHdr.srcSck != sentSlkHdr->dstSlk || gotSlkHdr.pktTyp != sentSlkHdr->pktTyp || gotSlkHdr.tid != sentSlkHdr->tid) {
#ifdef SASD_TOL
			dgNote(slkState, DGE_ACK, sentSlkHdr->tid, (uint16_t)(DGA_MISMATCH | ((uint16_t)gotSlkHdr.tid << 8)), 0);
#endif
			continue;
		}
		
		//make sure this is an ack/RSP packet and has the proper offset
		if (gotFragHdr.typ != FRAG_TYP_ACK || gotFragHdr.ofst != sentFragHdr->ofst) {
#ifdef SASD_TOL
			dgNote(slkState, DGE_ACK, sentSlkHdr->tid, (uint16_t)(DGA_NOTACK | ((uint16_t)gotFragHdr.typ << 8)), 0);
#endif
			continue;
		}
		
		if (gotFragHdr.flags & FRAG_FLAG_OOM) {
			
			//fprintf(stderr, "Reply claims OOM\n");
#ifdef SASD_TOL
			dgNote(slkState, DGE_ACK, sentSlkHdr->tid, DGA_OOM, 0);
#endif
			return false;
		}
		
#ifdef SASD_TOL
		dgNote(slkState, DGE_ACK, sentSlkHdr->tid, DGA_OK, 0);
#endif
		return true;
	}
	
#ifdef SASD_TOL
	dgNote(slkState, DGE_ACK, sentSlkHdr->tid, DGA_NONE, 0);
#endif
	return false;
}

static bool fragPrvSendFragWithAckWait(void *slkState, struct SlkHdr *slkHdr, struct FragHdr *fHdr, struct bv *bufTx, struct bv *bufRx)
{
	struct bv sendVec = BV_INIT(fHdr, sizeof(struct FragHdr), bufTx);

	uint_fast8_t i;
	
#ifdef SASD_TOL
	uint_fast8_t tries = dgTries(slkState);
	
	for (i = 0; i < tries; i++) {
	
		dgNote(slkState, DGE_SEND, slkHdr->tid, (uint16_t)i, 0);
		slkTx(slkState, slkHdr, &sendVec);
		dgNote(slkState, DGE_SENT, slkHdr->tid, 0, 0);
		
		if (fragPrvRecvAck(slkState, slkHdr, fHdr, bufRx, slkExtGetCurTime() + dgWait(slkState, (uint8_t)i)))
			return true;
	}
#else
	for (i = 0; i < FRAG_MAX_RESENDS; i++) {
	
		slkTx(slkState, slkHdr, &sendVec);
		
		if (fragPrvRecvAck(slkState, slkHdr, fHdr, bufRx, slkExtGetCurTime() + FRAG_ACK_WAIT_TIME))
			return true;
	}
#endif

	return false;
}

static void fragPrvSendAck(void *slkState, const struct SlkHdr *reqSlkHdr, const struct FragHdr *reqFragHdr, struct bv *bufTx)
{
	struct SlkHdr slk = {.dstSlk = reqSlkHdr->srcSck, .srcSck = reqSlkHdr->dstSlk, .pktTyp = reqSlkHdr->pktTyp, .tid = reqSlkHdr->tid, };
	struct FragHdr fh = {.typ = FRAG_TYP_ACK, .flags = reqFragHdr->flags, .ofst = reqFragHdr->ofst, };
	struct bv sendVec = BV_INIT(&fh, sizeof(struct FragHdr), bufTx);
	
	slkTx(slkState, &slk, &sendVec);
}

bool fragAck(void *slkState, struct SlkHdr *reqSlkHdr, struct FragAckInfo *fi, void *srcP, uint16_t len)
{
	struct FragHdr reconstructedReqFragHdr = {.typ = FRAG_TYP_DATA, .flags = fi->flags, .ofst = fi->ofst, };
	struct bv bv = BV_INIT(srcP, len, NULL);
	
	fragPrvSendAck(slkState, reqSlkHdr, &reconstructedReqFragHdr, len ? &bv : NULL);
	
	return true;
}

bool fragSendStartData(void *slkState, struct FragSendState *fs, struct SlkHdr *slkHdr, uint16_t totalLen)
{
	fs->curPos = 0;
	fs->slkHdr = *slkHdr;
	fs->totalLen = totalLen;
	
	return true;
}

bool fragSendContinue(void *slkState, struct FragSendState *fs, const void *srcP, uint16_t len, struct bv *bufAckRx)
{
	uint8_t *src = (uint8_t*)srcP;
	uint16_t now, ofst;
	
	//zero-length segments mean nothing
	if (!len)
		return true;
	
	//verify we have space
	if (fs->totalLen - fs->curPos < len)
		return false;
	
	//send one or more chunks
	for (ofst = 0; ofst < len; ofst += now, fs->curPos += now) {
		
		struct FragHdr fHdr = {.typ = FRAG_TYP_DATA, };
		struct bv bv = BV_INIT(src + ofst, 0, NULL);
		
		now = len - ofst;
		if (now > FRAG_MAX_CHUNK_SZ)
			now = FRAG_MAX_CHUNK_SZ;
		
		if (fs->curPos)
			fHdr.ofst = TO_U16(fs->curPos);
		else {
			fHdr.ofst = TO_U16(fs->totalLen);
			fHdr.flags |= FRAG_FLAG_FIRST;
		}
		
		if (fs->curPos + now == fs->totalLen)
			fHdr.flags |= FRAG_FLAG_LAST;
		
		//fprintf(stderr, "  sending frag with %u bytes\n", now);
		
		BV_SET_LEN(&bv, now);
		
		if (!fragPrvSendFragWithAckWait(slkState, &fs->slkHdr, &fHdr, &bv, bufAckRx))
			return false;
	}
	
	return true;
}

#ifdef SASD_TOL
bool fragSendPair(void *slkState, struct SlkHdr *hA, const void *srcA, uint16_t lenA, struct SlkHdr *hB, const void *srcB, uint16_t lenB, uint8_t tries, bool *okAP)
{
	struct FragHdr fA = {.typ = FRAG_TYP_DATA, .flags = FRAG_FLAG_FIRST | FRAG_FLAG_LAST, .ofst = TO_U16(lenA), };
	struct FragHdr fB = {.typ = FRAG_TYP_DATA, .flags = FRAG_FLAG_FIRST | FRAG_FLAG_LAST, .ofst = TO_U16(lenB), };
	struct bv dA = BV_INIT((void*)srcA, lenA, NULL), vA = BV_INIT(&fA, sizeof(struct FragHdr), &dA);
	struct bv dB = BV_INIT((void*)srcB, lenB, NULL), vB = BV_INIT(&fB, sizeof(struct FragHdr), &dB);
	bool okA = false, okB = false;
	uint_fast8_t i;
	
	for (i = 0; i < tries && !okB; i++) {
		
		uint32_t endTime;
		
		if (!okA) {		//the first request is still needed (or its reply was not seen): send both, in order
			dgNote(slkState, DGE_SEND, hA->tid, (uint16_t)i, 0);
			slkTx(slkState, hA, &vA);
			dgNote(slkState, DGE_SENT, hA->tid, 0, 0);
		}
		dgNote(slkState, DGE_SEND, hB->tid, (uint16_t)i, 0);
		slkTx(slkState, hB, &vB);
		dgNote(slkState, DGE_SENT, hB->tid, 0, 0);
		
		endTime = slkExtGetCurTime() + dgWait(slkState, (uint8_t)i);
		
		while (!okB) {
			
			struct FragHdr got;
			struct SlkHdr gh;
			struct bv rv = BV_INIT(&got, sizeof(struct FragHdr), NULL);		//slkPickBuf chains the right reply buffer behind this
			
			if (!slkRx(slkState, &gh, &rv, endTime) || gh.dataLen < sizeof(struct FragHdr))
				break;
			
			if (gh.dstSlk != hB->srcSck || gh.srcSck != hB->dstSlk || gh.pktTyp != hB->pktTyp || got.typ != FRAG_TYP_ACK)
				continue;
			
			if (gh.tid == hB->tid && got.ofst == fB.ofst) {
				if (got.flags & FRAG_FLAG_OOM)
					break;
				dgNote(slkState, DGE_ACK, hB->tid, DGA_OK, 0);
				okB = true;
			}
			else if (gh.tid == hA->tid && got.ofst == fA.ofst) {
				if (!okA)
					dgNote(slkState, DGE_ACK, hA->tid, DGA_OK, 0);
				okA = true;
			}
		}
		if (!okB)
			dgNote(slkState, DGE_ACK, hB->tid, DGA_NONE, 0);
	}
	
	*okAP = okA;
	return okB;
}
#endif

bool fragSend(void *slkState, struct SlkHdr *slkHdr, const void *srcP, uint16_t len, struct bv *bufAckRx)
{
	struct FragSendState fs;
	
	return fragSendStartData(slkState, &fs, slkHdr, len) && fragSendContinue(slkState, &fs, srcP, len, bufAckRx);
}

bool fragRecv(void *slkState, struct SlkHdr *slkHdr, void *dstP, uint16_t *lenP /* in / out */, uint32_t endTime, bool autoAck, struct FragAckInfo *fi)
{
	uint32_t len = *lenP, done = 0, expected;
	uint8_t *dst = (uint8_t*)dstP;
	bool waitForFirst = true;
	
	while(1) {
		
		struct FragHdr nowFragHdr;
		struct SlkHdr nowSlkHdr;
		uint16_t nowBytes, nowOfst;
		struct bv bv = BV_INIT(dst, len - done, NULL);
		
		if (!fragPrvRecvFrag(slkState, &nowSlkHdr, &nowFragHdr, &bv, endTime))
			return false;
		
		nowBytes = nowSlkHdr.dataLen - sizeof(struct FragHdr);
		
		//only accept data packets
		if (nowFragHdr.typ != FRAG_TYP_DATA)
			continue;
		
		//if we're waiting for the first packet and this is not one, ignore it
		if (waitForFirst && !(nowFragHdr.flags & FRAG_FLAG_FIRST))
			continue;
		else if (waitForFirst) {	//first packet
			
			expected = TO_U16(nowFragHdr.ofst);
			nowOfst = 0;
			waitForFirst = false;
			
			//reject if it is too long
			if (expected > len)
				return false;
			
			*slkHdr = nowSlkHdr;
		}
		else 
			nowOfst = TO_U16(nowFragHdr.ofst);
		
		//verify packet addressing and type is same as first was (ignoreable if not)
		if (nowSlkHdr.dstSlk != slkHdr->dstSlk || nowSlkHdr.srcSck != slkHdr->srcSck || nowSlkHdr.pktTyp != slkHdr->pktTyp || nowSlkHdr.tid != slkHdr->tid)
			continue;
		
		//verify the offset if as expected (out-of-order packet is ignoreable)
		if (nowOfst != done)
			continue;
		
		//verify we did not overrun the end (mistake by sender - failure)
		if (expected - done < nowBytes)
			return false;
		
		//verify end packet properly ends, if this is the end (mistake by sender - failure)
		if (!(nowFragHdr.flags & FRAG_FLAG_LAST) != !(expected - done == nowBytes))
			return false;
		
		//if we got this far, we're accepting this fragment - ACK it (except for last packet in a series if auto-ack is off
		if (autoAck || !(nowFragHdr.flags & FRAG_FLAG_LAST))
			fragPrvSendAck(slkState, &nowSlkHdr, &nowFragHdr, NULL);
		
		if (fi) {
			fi->ofst = nowFragHdr.ofst;
			fi->flags = nowFragHdr.flags;
		}
		
		//account for it
		done += nowBytes;
		
		if (expected == done) {
			*lenP = done;
			return true;
		}
	}		
}




