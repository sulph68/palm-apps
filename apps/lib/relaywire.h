/*
 * relaywire.h - shared big-endian / uint16-length-prefixed-string frame
 * body writer/reader for the WiRSa relay apps (see relayproto.h).
 *
 * Kept in its own file so every call into it is cross-file: real-device
 * testing showed these small helpers corrupting outgoing frames when tagged
 * into a secondary segment alongside their caller in the SAME .c file.
 * Placed in RP_SECTION (the app's seg header); the tag here must match the
 * definitions, since callers pick near vs far call from this declaration.
 */
#ifndef RELAYWIRE_H
#define RELAYWIRE_H

#include <PalmOS.h>

#ifndef RP_SECTION
#define RP_SECTION
#endif

typedef struct { UInt8 *buf; UInt16 cap; UInt16 len; } RpW;
typedef struct { const UInt8 *buf; UInt16 len; UInt16 pos; Boolean ok; } RpR;

void RpW8(RpW *w, UInt8 v) RP_SECTION;
void RpW16(RpW *w, UInt16 v) RP_SECTION;
void RpW32(RpW *w, UInt32 v) RP_SECTION;
void RpWBytes(RpW *w, const UInt8 *p, UInt16 n) RP_SECTION;
void RpWStr(RpW *w, const char *s) RP_SECTION;

UInt8 RpR8(RpR *r) RP_SECTION;
UInt16 RpR16(RpR *r) RP_SECTION;
UInt32 RpR32(RpR *r) RP_SECTION;
void RpRBytes(RpR *r, UInt8 *out, UInt16 n) RP_SECTION;
void RpRStr(RpR *r, char *out, UInt16 outCap) RP_SECTION;

#endif
