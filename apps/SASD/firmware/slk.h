#ifndef _SLK_H_
#define _SLK_H_

//equivalent to serial link manager in PalmOS, but simiplified for our use case
//this is equivalent to IP

#include <stdbool.h>
// #include "PalmTypes.h"
#include <stdint.h>
#include "bufvec.h"
#include "swap.h"


#define PKT_ADDR_DESKTOP_LINK	3

#define PKT_TYP_LOOPBACK_TEST	3
#define PKT_TYP_HOTSYNC			2


#pragma pack (push,1)
PACKED_PRE
struct SlkHdr {
	uint8_t magic[3];	//filled by this code
	uint8_t dstSlk, srcSck, pktTyp;
	uint16_t dataLen;	//for this api, local endianness
	uint8_t tid;
	uint8_t csum;		//filled by this code
}
PACKED_POST
;
#pragma pack (pop)



bool slkRx(void *slkState, struct SlkHdr *hdr, struct bv *bv, uint32_t endTime);	//packets over buffer len are dropped

void slkTx(void *slkState, struct SlkHdr *hdr, struct bv *bv);

uint16_t slkTxStart(void *slkState, struct SlkHdr *hdr, uint16_t dataLen);		//returns state
uint16_t slkTxBytes(void *slkState, uint16_t state, void *dataP, uint16_t num);	//updates and returns state
void slkTxFinish(void *slkState, uint16_t state);



//externally provided funcs
uint32_t slkExtGetCurTime(void);
uint16_t slkExtRx(void *slkState, void *dst, uint16_t nBytes, uint32_t endTime);
void slkExtTx(void *slkState, const void *src, uint16_t len);



#endif
