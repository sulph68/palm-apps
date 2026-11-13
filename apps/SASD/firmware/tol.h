#ifndef _TOL_H_
#define _TOL_H_

/* Hooks of the tolerance code (Src/tol.inc) in the shared slk and frag layers. Everything here compiles away unless SASD_TOL is defined.
 * The hooks take the slk state pointer (struct Globals * in Slot.c). */

#include <stdint.h>

#ifdef SASD_TOL

#define SD_FLAG_DIAG		0x00001000UL	/* reader flag: "I take CMD_DIAG uploads" (only the WiRSa diagnostic firmware sets it) */
#define SD_FLAG_PIPE		0x00002000UL	/* reader flag: "you may send a request and its data back to back" */
#define CMD_DIAG			0x70

/* event types (8-byte records: type, a, b, tick, x) */
#define DGE_SEND			1	/* a=tid b=attempt                                  a request goes out                   */
#define DGE_SENT			2	/* a=tid                                            SerSend returned (queued)            */
#define DGE_RX0				3	/* a=first byte b=bytes in this read x=ticks since SENT   first byte after SENT          */
#define DGE_RXERR			4	/* a=err&ff b=line error bits x=bytes pending       SerReceive returned an error         */
#define DGE_LOCK			5	/* a=tid b=dataLen x=junk bytes skipped              slkRx found a header                 */
#define DGE_BADCRC			6	/* a=tid b=dataLen                                  header found, CRC wrong              */
#define DGE_ACK				7	/* a=tid b=code|gotTid<<8 x=attempt                 what fragPrvRecvAck decided          */
#define DGE_END				8	/* a=tid b=ok<<8|attempts x=ticks                   exchange finished                    */
#define DGE_RXHEAD			9	/* a=bytes received b=h0h1 x=h2h3                   first bytes after SENT               */
#define DGE_TOUT			10	/* a=stage(0 hunt,1 body,2 crc) b=bytes into the body   slkRx gave up at endTime            */
#define DGE_TXFIX			11	/* a=stage(0 timeout,1 reopen failed) b=bytes sent x=bytes left in the queue   SerSend returned but the bytes did not leave */

/* DGE_ACK codes */
#define DGA_OK				1
#define DGA_MISMATCH		2	/* addressing / packet type / tid differs */
#define DGA_NOTACK			3	/* not an ack, or the offset differs */
#define DGA_OOM				4
#define DGA_NONE			5	/* nothing acceptable before the end time */

void dgNote(void *st, uint8_t t, uint8_t a, uint16_t b, uint16_t x);
struct bv;
void slkPickBuf(void *st, uint8_t tid, struct bv *head);	/* where the payload of a reply with this tid goes (two requests in flight) */
uint8_t dgTries(void *st);						/* sends allowed for the current exchange */
uint16_t dgWait(void *st, uint8_t attempt);		/* ticks to wait for the ack after send number `attempt` */

#endif
#endif
