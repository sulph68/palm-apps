/*
 * CDDVWire.h - frame body writer/reader. The code lives in the shared
 * ../lib/relaywire.c (sec004 via RP_SECTION in cddv_seg.h); these keep this
 * app's historical names. Calls into it stay cross-file - see relaywire.h.
 */
#ifndef CDDVWIRE_H
#define CDDVWIRE_H

#include "CalDAVSync.h"
#include "relaywire.h"

typedef RpW CDDVW;
typedef RpR CDDVR;

#define cw8		RpW8
#define cw16	RpW16
#define cw32	RpW32
#define cwBytes	RpWBytes
#define cwStr	RpWStr
#define cr8		RpR8
#define cr16	RpR16
#define cr32	RpR32
#define crBytes	RpRBytes
#define crStr	RpRStr

#endif
