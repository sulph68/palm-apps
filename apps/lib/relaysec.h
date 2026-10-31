/*
 * relaysec.h - serial-link encryption for the WiRSa relay framing
 * (relayproto.c). No user interaction; no plaintext fallback.
 *
 * Key exchange (plaintext frames, types 0x70-0x74), run by RpSecure() right
 * after the port opens and before any app frame:
 *   Palm  -> RP_KEX_REQ  reqId(u32)
 *   WiRSa -> RP_KEX_PUB  reqId(u32) modLen(u16) modulus(modLen, big-endian)
 *                        r3(modLen: R^3 mod modulus, R = 2^(8*modLen))
 *                        wRand(16)            [RSA e = 3]
 *   Palm  -> RP_KEX_KEY  reqId(u32) RSA-PKCS#1-v1.5(secret16)
 *   WiRSa -> RP_KEX_OK   (encrypted) "RPOK"   or RP_KEX_FAIL reqId(u32)
 * Session key = AES-128-ECB(key = secret, block = wRand).
 *
 * Every other frame is encrypted: wire type = type | 0x80, wire body =
 * seq(u32) + AES-128-CTR(body), counter block = dir,0,0,0,seq,0..0 (dir 'P'
 * Palm->WiRSa, 'W' WiRSa->Palm), checksum over the bytes as sent. Mirrors
 * WiRSa's relay_proto.cpp.
 */
#ifndef RELAYSEC_H
#define RELAYSEC_H

#include <PalmOS.h>

#ifndef RP_SECTION
#define RP_SECTION
#endif

#define RP_KEX_REQ	0x70
#define RP_KEX_PUB	0x71
#define RP_KEX_KEY	0x72
#define RP_KEX_OK	0x73
#define RP_KEX_FAIL	0x74
#define RP_IS_KEX(t)	(((t) & 0xF0) == 0x70)
#define RP_ENC_FLAG	0x80

#define rpDirPalm	0x50	/* 'P' */
#define rpDirWirsa	0x57	/* 'W' */

/* RpSecure results. */
#define rpSecOk			0
#define rpSecNoReply	1	/* WiRSa never sent a usable first reply */
#define rpSecFailed		2	/* WiRSa replied, but the link could not be secured */
#define rpSecCancelled	3	/* user tapped Cancel */

/* True once RpSecure succeeded on the open port; cleared by RpOpen/RpClose. */
extern Boolean gRpSecured;
extern UInt32 gRpTxSeq;

/* How long each key exchange attempt waits for WiRSa's first reply (KEX_PUB).
 * 0 (default) = relayproto's 180 s idle wait. Screenless Mode sets a few
 * seconds while it has just started the mode (WiRSa's key is ready, so the
 * reply is quick): a lost KEX_REQ is then retried instead of waited out.
 * Not reset by RpOpen. */
extern UInt32 gRpKexReplyTicks;

UInt8 RpSecure(void) RP_SECTION;
/* AES-128-CTR in place with the session key, starting at 16-byte block
 * index blk of frame (dir, seq). Encrypt and decrypt are the same call. */
void RpSecXcrypt(UInt8 *buf, UInt16 len, UInt8 dir, UInt32 seq, UInt16 blk) RP_SECTION;
void RpSecWipe(void) RP_SECTION;

#endif
