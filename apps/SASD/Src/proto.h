#ifndef _PROTO_H_
#define _PROTO_H_

//protocol is big-endian

#include <stdint.h>

//slk-layer things
#define PKT_TYP_SASD			0x10
#define PKT_ADDR_SASD			0x08



#define SD_BAUDRATE_INITIAL		9600
#define SD_PROTO_MAX_MSG		528	//in bytes including all tokens

#define CMD_PING				0x00	//struct SdPingData, reply is RSP_PONG in ACK packet
#define CMD_SET_BAUD			0x05	//struct SdBaudData offer -> struct SdBaudData accepted
#define CMD_GET_INFO			0x01	//no data, reply is RSP_INFO in ACK packet
#define CMD_READ				0x02	//struct SdReadWriteReq. reply is RSP_OK ack or ack with struct SdErrorData, then each xfer is done via CTS (with seqNo checking) + data-block-or-error-in-ack
#define CMD_WRITE				0x03	//struct SdReadWriteReq, reply is RSP_OK ack or ack with struct SdErrorData, then each xfer is done via XFER (with seqNo checking) + RSP_OK-ACK-or-error-in-ack
#define CMD_RESET_STATE			0x04	//no data both ways. resets current read/write state to avoid seq errors
#define CMD_FW_UPDATE_START		0x06	//no data. reply is ACK
#define CMD_GET_BATT			0x07	//no data, reply is struct SdBatteryData


#define RSP_PONG				0x40	//struct SdPingData	(echoed)
#define RSP_OK					0x41
#define RSP_ERROR				0x42	//struct SdErrorData
#define RSP_INFO				0x43	//struct SdInfoData

#define NOTICE_CTS				0x80
#define NOTICE_XFER				0x81



struct SdBaudData {
	uint8_t cmd;
	uint8_t rfu[3];
	uint32_t rate;
};

struct SdPingData {
	uint8_t cmd;
	uint8_t garbage[31];
};

struct SdErrorData {
	uint8_t cmd;
	uint8_t errorType;
	uint16_t rfu1;
	uint32_t rfu2;
};

struct SdBatteryData {
	uint8_t cmd;
	uint8_t percent;
	uint16_t voltage;
};

#define SD_ERR_NONE				0x00
#define SD_ERR_BOUNDS			0x01
#define SD_ERR_SEQUENCE			0x02
#define SD_ERR_STATE			0x03	//wrong state for packet
#define SD_ERR_SEQ2				0x04	//differen type of sequencing error
#define SD_ERR_CARD				0x05

#define SD_FLAG_COMMS			0x00000001UL	//comms with reader established. alwsy set when coming from reader
#define SD_FLAG_HAVE_CARD		0x00000002UL
#define SD_FLAG_READ_ONLY		0x00000004UL
#define SD_FLAG_IS_MMC			0x00000008UL
#define SD_FLAG_IS_NEW_VER		0x00000010UL	//SDv2+, MMCv4+
#define SD_FLAG_IS_HIGH_CAP		0x00000020UL
#define SD_FLAG_HAS_CMD_23		0x00000040UL
#define SD_FLAG_HAS_DISCARD		0x00000080UL

struct SdVersionInfo {
	uint8_t hwVers;
	uint8_t fwVers[3];	//maj.min.bfx
};

struct SdInfoData {
	uint8_t cmd;
	uint8_t manuf;		//from CID
	uint16_t oem;		//from CID
	uint32_t numBlocks;
	uint32_t flags;
	uint32_t snum;		//from CID
	struct SdVersionInfo ver;
};

struct SdReadWriteReq {
	uint8_t cmd;
	uint8_t rfu[3];
	uint32_t sector;
	uint32_t numSec;
};

//each block is sent as a separate xfer after a CTS command


#endif


