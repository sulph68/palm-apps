/*
 * relaysec.c - see relaysec.h. Lives entirely in RP_SECTION (a secondary
 * segment in both apps), so: every function here is tagged, including the
 * static helpers (a secondary-segment function calling an untagged
 * same-file function crashes); no division/modulo and no 32x32 multiply
 * (libgcc is only reliably reachable from main) - the bignum code uses
 * 16-bit limbs, whose 16x16->32 products compile to a native mulu.w; no
 * local static const tables and no C initializers (memset/memcpy calls).
 */
#include <PalmOS.h>
#include "aes.h"
#include "relayproto.h"
#include "relaysec.h"
#include "relay_cfg.h"

#define rpMaxModBytes	128
#define rpMinModBytes	64
#define rpMaxLimbs		(rpMaxModBytes / 2)
#define rpPoolCreator	'Rp35'
#define rpKexOkTicks	(SysTicksPerSecond() * 20)
#define rpMaxStrayFrames	8
#define rpKexAttempts	3
#define rpIdleTicks		(SysTicksPerSecond() * 180)	/* relayproto.c's default frame wait */

Boolean gRpSecured = false;
UInt32 gRpKexReplyTicks = 0;
UInt32 gRpTxSeq = 0;
static struct AES_ctx gRpAes;

/* Transient key-exchange workspace, heap-allocated for the exchange only. */
typedef struct {
	UInt16 n[rpMaxLimbs];
	UInt16 r3[rpMaxLimbs];
	UInt16 x[rpMaxLimbs];
	UInt16 y[rpMaxLimbs];
	UInt16 t[rpMaxLimbs + 1];
	UInt8 em[rpMaxModBytes];
	UInt8 key[4 + rpMaxModBytes];
	UInt8 rx[4 + 2 + 2 * rpMaxModBytes + 16 + 8];
	struct AES_ctx aes;
	UInt8 blk[16];
	UInt8 secret[16];
} RpKexWork;

static UInt32 Get32(const UInt8 *p) RP_SECTION;
static void Put32(UInt8 *p, UInt32 v) RP_SECTION;
static Int16 Cmp(const UInt16 *a, const UInt16 *b, UInt16 s) RP_SECTION;
static void Sub(UInt16 *a, const UInt16 *b, UInt16 s) RP_SECTION;
static void MonMul(UInt16 *out, const UInt16 *a, const UInt16 *b, const UInt16 *n,
                   UInt16 n0, UInt16 s, UInt16 *t) RP_SECTION;
static Boolean RsaEncrypt(RpKexWork *w, const UInt8 *mod, const UInt8 *r3, UInt16 k, UInt8 *out) RP_SECTION;
static void RndInit(RpKexWork *w) RP_SECTION;
static void RndBytes(RpKexWork *w, UInt8 *out, UInt16 len, UInt32 *ctr) RP_SECTION;
static Boolean RecvNoted(UInt8 *t, UInt8 *buf, UInt16 cap, UInt16 *len, Boolean *sawData) RP_SECTION;
static UInt8 Exchange(RpKexWork *w, UInt32 *ctr, Boolean *sawData) RP_SECTION;

RP_SECTION static UInt32 Get32(const UInt8 *p)
{
	return ((UInt32) p[0] << 24) | ((UInt32) p[1] << 16) | ((UInt32) p[2] << 8) | (UInt32) p[3];
}

RP_SECTION static void Put32(UInt8 *p, UInt32 v)
{
	p[0] = (UInt8) (v >> 24); p[1] = (UInt8) (v >> 16); p[2] = (UInt8) (v >> 8); p[3] = (UInt8) v;
}

/* Little-endian 16-bit limbs throughout. */
RP_SECTION static Int16 Cmp(const UInt16 *a, const UInt16 *b, UInt16 s)
{
	while (s > 0) {
		s--;
		if (a[s] != b[s]) return (a[s] > b[s]) ? 1 : -1;
	}
	return 0;
}

RP_SECTION static void Sub(UInt16 *a, const UInt16 *b, UInt16 s)
{
	UInt32 br = 0;
	UInt16 j;
	for (j = 0; j < s; j++) {
		UInt32 d = (UInt32) a[j] - (UInt32) b[j] - br;
		a[j] = (UInt16) d;
		br = (d >> 16) & 1;
	}
}

/* out = a * b * R^-1 mod n, R = 2^(16s): CIOS with the multiply and the
 * reduction fused into one pass per limb of b, walked with post-increment
 * pointers (the 68000's cheapest addressing). a, b < n; out may alias a or
 * b (the result is only written from t at the end). t: s+1 limbs scratch.
 * Every product is 16x16->32 (mulu.w); sums stay within 32 bits. */
RP_SECTION static void MonMul(UInt16 *out, const UInt16 *a, const UInt16 *b, const UInt16 *n,
                              UInt16 n0, UInt16 s, UInt16 *t)
{
	UInt16 i, cnt, bi, m;
	UInt32 u, v, c1, c2;
	const UInt16 *ap, *np;
	UInt16 *tp;

	MemSet(t, (Int32) (s + 1) * 2, 0);
	for (i = 0; i < s; i++) {
		bi = b[i];
		ap = a;
		np = n;
		tp = t;
		u = (UInt32) *ap++ * (UInt32) bi + (UInt32) *tp;
		m = (UInt16) ((UInt16) u * n0);
		v = (UInt32) m * (UInt32) *np++ + (UInt32) (UInt16) u;	/* low half is 0 by choice of m */
		c1 = u >> 16;
		c2 = v >> 16;
		for (cnt = (UInt16) (s - 1); cnt != 0; cnt--) {
			u = (UInt32) *ap++ * (UInt32) bi + (UInt32) tp[1] + c1;
			c1 = u >> 16;
			v = (UInt32) m * (UInt32) *np++ + (UInt32) (UInt16) u + c2;
			c2 = v >> 16;
			*tp++ = (UInt16) v;
		}
		/* tp = &t[s-1]; tp[1] = t[s] (0 or 1 by the CIOS bound t < 2n). */
		u = (UInt32) tp[1] + c1;
		v = (UInt32) (UInt16) u + c2;
		tp[0] = (UInt16) v;
		tp[1] = (UInt16) ((u >> 16) + (v >> 16));
	}
	if (t[s] || Cmp(t, n, s) >= 0) Sub(t, n, s);
	MemMove(out, t, (Int32) s * 2);
}

/* out (k bytes, big-endian) = em^3 mod mod, in three Montgomery
 * multiplications: m*m -> m^2/R, *m -> m^3/R^2, *(R^3 mod n) -> m^3. r3 =
 * R^3 mod mod is supplied by WiRSa (computing it here would cost more than
 * the encryption itself). e = 3 is safe for key transport under PKCS#1
 * v1.5's 100+ random padding bytes. em < mod is guaranteed by the 00 02
 * prefix and mod's nonzero top byte. False if r3 is not < mod. */
RP_SECTION static Boolean RsaEncrypt(RpKexWork *w, const UInt8 *mod, const UInt8 *r3, UInt16 k, UInt8 *out)
{
	UInt16 s = (UInt16) (k / 2);
	UInt16 i, n0, inv;

	for (i = 0; i < s; i++) {
		w->n[i] = (UInt16) (((UInt16) mod[k - 2 - 2 * i] << 8) | mod[k - 1 - 2 * i]);
		w->r3[i] = (UInt16) (((UInt16) r3[k - 2 - 2 * i] << 8) | r3[k - 1 - 2 * i]);
		w->y[i] = (UInt16) (((UInt16) w->em[k - 2 - 2 * i] << 8) | w->em[k - 1 - 2 * i]);
	}
	if (Cmp(w->r3, w->n, s) >= 0) return false;

	/* n0 = -n^-1 mod 2^16 (Newton: 3 -> 6 -> 12 -> 24 correct bits). */
	inv = w->n[0];
	inv = (UInt16) (inv * (UInt16) (2 - (UInt16) (w->n[0] * inv)));
	inv = (UInt16) (inv * (UInt16) (2 - (UInt16) (w->n[0] * inv)));
	inv = (UInt16) (inv * (UInt16) (2 - (UInt16) (w->n[0] * inv)));
	inv = (UInt16) (inv * (UInt16) (2 - (UInt16) (w->n[0] * inv)));
	n0 = (UInt16) (0 - inv);

	MonMul(w->x, w->y, w->y, w->n, n0, s, w->t);		/* m^2 / R */
	MonMul(w->x, w->x, w->y, w->n, n0, s, w->t);		/* m^3 / R^2 */
	MonMul(w->x, w->x, w->r3, w->n, n0, s, w->t);		/* m^3 */

	for (i = 0; i < s; i++) {
		out[k - 2 - 2 * i] = (UInt8) (w->x[i] >> 8);
		out[k - 1 - 2 * i] = (UInt8) w->x[i];
	}
	return true;
}

/* Random source: a 16-byte pool kept in feature memory (survives app
 * exits, reset on a device reset), re-keyed every exchange with fresh
 * timer/SysRandom input and expanded with AES in counter mode. */
RP_SECTION static void RndInit(RpKexWork *w)
{
	UInt32 v[4];
	UInt16 i;

	for (i = 0; i < 4; i++) {
		if (FtrGet(rpPoolCreator, i, &v[i]) != errNone)
			v[i] = TimGetTicks() ^ ((UInt32) SysRandom(0) << 16) ^ TimGetSeconds() ^ ((UInt32) i << 8);
		Put32(w->blk + 4 * i, v[i]);
	}
	AES_init_ctx(&w->aes, w->blk);
	Put32(w->blk, TimGetTicks());
	Put32(w->blk + 4, TimGetSeconds());
	Put32(w->blk + 8, ((UInt32) SysRandom(0) << 16) | (UInt32) (UInt16) SysRandom(0));
	Put32(w->blk + 12, 0x52503335UL);
	AES_ECB_encrypt(&w->aes, w->blk);
	AES_init_ctx(&w->aes, w->blk);
	/* Store a one-way successor, not the live key, as the next pool. */
	MemSet(w->blk, 16, 0xA5);
	Put32(w->blk, TimGetTicks());
	AES_ECB_encrypt(&w->aes, w->blk);
	for (i = 0; i < 4; i++) FtrSet(rpPoolCreator, i, Get32(w->blk + 4 * i));
}

RP_SECTION static void RndBytes(RpKexWork *w, UInt8 *out, UInt16 len, UInt32 *ctr)
{
	UInt16 i;
	while (len > 0) {
		UInt16 n = (len > 16) ? 16 : len;
		Put32(w->blk, (*ctr)++);
		Put32(w->blk + 4, TimGetTicks());
		Put32(w->blk + 8, ((UInt32) SysRandom(0) << 16) | (UInt32) (UInt16) SysRandom(0));
		Put32(w->blk + 12, 0);
		AES_ECB_encrypt(&w->aes, w->blk);
		for (i = 0; i < n; i++) out[i] = w->blk[i];
		out += n;
		len -= n;
	}
}

RP_SECTION void RpSecXcrypt(UInt8 *buf, UInt16 len, UInt8 dir, UInt32 seq, UInt16 blk)
{
	UInt8 iv[16];
	MemSet(iv, 16, 0);
	iv[0] = dir;
	Put32(iv + 4, seq);
	iv[14] = (UInt8) (blk >> 8);
	iv[15] = (UInt8) blk;
	AES_ctx_set_iv(&gRpAes, iv);
	AES_CTR_xcrypt_buffer(&gRpAes, buf, len);
}

RP_SECTION void RpSecWipe(void)
{
	gRpSecured = false;
	gRpTxSeq = 0;
	MemSet(&gRpAes, sizeof(gRpAes), 0);
}

/* RpRecvFrame, also noting whether anything arrived: a failure well short
 * of the idle wait means bytes came but didn't form a usable frame. */
RP_SECTION static Boolean RecvNoted(UInt8 *t, UInt8 *buf, UInt16 cap, UInt16 *len, Boolean *sawData)
{
	UInt32 idle = gRpIdleOverrideTicks ? gRpIdleOverrideTicks : (UInt32) rpIdleTicks;
	UInt32 start = TimGetTicks();
	if (RpRecvFrame(t, buf, cap, len)) { *sawData = true; return true; }
	if (TimGetTicks() - start + SysTicksPerSecond() < idle) *sawData = true;
	return false;
}

/* One complete exchange. rpSecNoReply here means nothing usable arrived;
 * *sawData tells whether anything arrived at all. */
RP_SECTION static UInt8 Exchange(RpKexWork *w, UInt32 *ctr, Boolean *sawData)
{
	UInt8 t, res;
	UInt16 len, k, i, tries;
	UInt32 id, savedIdle;
	const UInt8 *mod, *r3, *wRand;

	RndBytes(w, w->key, 4, ctr);
	id = Get32(w->key);
	if (!RpSendFrame(RP_KEX_REQ, w->key, 4)) return rpSecNoReply;

	/* A reply carrying another request's id is a leftover from an earlier,
	 * abandoned attempt - skip it and keep waiting. gRpKexReplyTicks, if
	 * set, bounds the wait so a lost KEX_REQ is retried (relaysec.h). */
	savedIdle = gRpIdleOverrideTicks;
	if (gRpKexReplyTicks) gRpIdleOverrideTicks = gRpKexReplyTicks;
	for (tries = 0; ; tries++) {
		if (!RecvNoted(&t, w->rx, sizeof(w->rx), &len, sawData)) {
			gRpIdleOverrideTicks = savedIdle;
			return RP_APP_CANCELLED() ? rpSecCancelled : rpSecNoReply;
		}
		if (len >= 4 && Get32(w->rx) == id) break;
		if (tries >= rpMaxStrayFrames) {
			gRpIdleOverrideTicks = savedIdle;
			return rpSecNoReply;
		}
	}
	gRpIdleOverrideTicks = savedIdle;
	if (t != RP_KEX_PUB || len < 6) return rpSecFailed;		/* RP_KEX_FAIL or garbage */
	k = (UInt16) (((UInt16) w->rx[4] << 8) | w->rx[5]);
	if (k < rpMinModBytes || k > rpMaxModBytes || (k & 1) || len != (UInt16) (6 + 2 * k + 16)) return rpSecFailed;
	mod = w->rx + 6;
	r3 = mod + k;
	wRand = r3 + k;
	if (mod[0] == 0 || (mod[k - 1] & 1) == 0) return rpSecFailed;

	/* Secret drawn after the reply arrived, so its timing jitter is mixed in. */
	RndBytes(w, w->secret, 16, ctr);
	w->em[0] = 0x00;
	w->em[1] = 0x02;
	RndBytes(w, w->em + 2, (UInt16) (k - 3 - 16), ctr);
	for (i = 2; i < (UInt16) (k - 16 - 1); i++) if (w->em[i] == 0) w->em[i] = 0x5A;
	w->em[k - 17] = 0x00;
	MemMove(w->em + k - 16, w->secret, 16);

	Put32(w->key, id);
	if (!RsaEncrypt(w, mod, r3, k, w->key + 4)) return rpSecFailed;
	if (!RpSendFrame(RP_KEX_KEY, w->key, (UInt16) (4 + k))) return rpSecFailed;

	/* Session key = AES-128-ECB(secret, wRand). Derived in gRpAes, not
	 * w->aes - that stays the random generator's state for a retry. */
	MemMove(w->blk, wRand, 16);
	AES_init_ctx(&gRpAes, w->secret);
	AES_ECB_encrypt(&gRpAes, w->blk);
	AES_init_ctx(&gRpAes, w->blk);
	gRpTxSeq = 0;
	gRpSecured = true;

	savedIdle = gRpIdleOverrideTicks;
	gRpIdleOverrideTicks = rpKexOkTicks;
	res = rpSecFailed;
	if (!RecvNoted(&t, w->rx, sizeof(w->rx), &len, sawData)) {
		if (RP_APP_CANCELLED()) res = rpSecCancelled;
	} else if (t == RP_KEX_OK && len == 4 &&
	           w->rx[0] == 'R' && w->rx[1] == 'P' && w->rx[2] == 'O' && w->rx[3] == 'K') {
		res = rpSecOk;
	}
	gRpIdleOverrideTicks = savedIdle;
	return res;
}

/* Up to rpKexAttempts exchanges: stray or corrupt bytes on the line (a
 * known WiRSa trait) must not cost the sync, so any failure where
 * something did arrive restarts the exchange on a flushed line with a new
 * id. A WiRSa that never answers, or a Cancel, ends it at once - unless
 * gRpKexReplyTicks is set (WiRSa known to be there), then silence retries. */
RP_SECTION UInt8 RpSecure(void)
{
	RpKexWork *w;
	UInt8 res = rpSecFailed;
	UInt16 attempt;
	UInt32 ctr = 0;
	Boolean saw = false;

	RpSecWipe();
	w = (RpKexWork *) MemPtrNew(sizeof(RpKexWork));
	if (!w) return rpSecFailed;
	MemSet(w, sizeof(RpKexWork), 0);
	RndInit(w);

	for (attempt = 0; attempt < rpKexAttempts; attempt++) {
		Boolean sawNow = false;
		res = Exchange(w, &ctr, &sawNow);
		if (sawNow) saw = true;
		if (res == rpSecOk || res == rpSecCancelled) break;
		RpSecWipe();
		/* Total silence normally means no WiRSa - stop. With a short reply
		 * wait (gRpKexReplyTicks) WiRSa is known to be there, so silence
		 * means the KEX_REQ was lost: try again. */
		if (!sawNow && !gRpKexReplyTicks) break;
		SysTaskDelay(SysTicksPerSecond() / 2);
		RpFlushInput();
	}
	if (res == rpSecNoReply && saw) res = rpSecFailed;	/* something answered, but unusably */
	if (res != rpSecOk) RpSecWipe();
	MemSet(w, sizeof(RpKexWork), 0);
	MemPtrFree(w);
	return res;
}
