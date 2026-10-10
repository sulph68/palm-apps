/*
 * relaywire.c - see relaywire.h. Merged from BitWarden's Wire.c and CalDAV
 * Sync's CDDVWire.c (identical code).
 */
#include "relaywire.h"

RP_SECTION void RpW8(RpW *w, UInt8 v) { if (w->len < w->cap) w->buf[w->len++] = v; }
RP_SECTION void RpW16(RpW *w, UInt16 v) { RpW8(w, (UInt8) (v >> 8)); RpW8(w, (UInt8) (v & 0xFF)); }
RP_SECTION void RpW32(RpW *w, UInt32 v) { RpW16(w, (UInt16) (v >> 16)); RpW16(w, (UInt16) (v & 0xFFFF)); }
RP_SECTION void RpWBytes(RpW *w, const UInt8 *p, UInt16 n) { UInt16 i; for (i = 0; i < n && w->len < w->cap; i++) w->buf[w->len++] = p[i]; }
RP_SECTION void RpWStr(RpW *w, const char *s) { UInt16 n = (UInt16) StrLen(s); RpW16(w, n); RpWBytes(w, (const UInt8 *) s, n); }

RP_SECTION UInt8 RpR8(RpR *r) { if (r->pos >= r->len) { r->ok = false; return 0; } return r->buf[r->pos++]; }
RP_SECTION UInt16 RpR16(RpR *r) { UInt16 hi = RpR8(r), lo = RpR8(r); return (UInt16) ((hi << 8) | lo); }
RP_SECTION UInt32 RpR32(RpR *r) { UInt16 hi = RpR16(r), lo = RpR16(r); return ((UInt32) hi << 16) | lo; }

/* (UInt32) casts matter: m68k-palmos-gcc has 16-bit int, so a plain
 * "pos + n" can wrap on a crafted length prefix and pass the bounds check. */
RP_SECTION void RpRBytes(RpR *r, UInt8 *out, UInt16 n)
{
	if ((UInt32) r->pos + (UInt32) n > (UInt32) r->len) { r->ok = false; return; }
	MemMove(out, r->buf + r->pos, n);
	r->pos += n;
}

RP_SECTION void RpRStr(RpR *r, char *out, UInt16 outCap)
{
	UInt16 n = RpR16(r);
	UInt16 copy;
	if (!r->ok || (UInt32) r->pos + (UInt32) n > (UInt32) r->len) { r->ok = false; out[0] = 0; return; }
	copy = (n < outCap - 1) ? n : (UInt16) (outCap - 1);
	MemMove(out, r->buf + r->pos, copy);
	out[copy] = 0;
	r->pos += n;	/* advance by the real length even if truncated into out */
}
