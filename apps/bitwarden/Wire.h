/*
 * Wire.h - PROTOCOL.md body writer/reader. The code lives in the shared
 * ../lib/relaywire.c (sec001 via RP_SECTION in bw_seg.h); these keep this
 * app's historical names. Calls into it stay cross-file - see relaywire.h.
 */
#ifndef BW_WIRE_H
#define BW_WIRE_H

#include "relaywire.h"

typedef RpW BwW;
typedef RpR BwR;

#define wU8		RpW8
#define wU16	RpW16
#define wU32	RpW32
#define wBytes	RpWBytes
#define wStr	RpWStr
#define rU8		RpR8
#define rU16	RpR16
#define rU32	RpR32
#define rBytes	RpRBytes
#define rStr	RpRStr

#endif
