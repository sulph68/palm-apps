/*
 * totp.c - SHA-1 (FIPS 180-1) + HMAC-SHA1 (RFC 2104) + base32 (RFC 4648) +
 * TOTP (RFC 6238) / HOTP (RFC 4226). Written from the published algorithm
 * specs (the classic public-domain "Steve Reid" SHA-1 structure), not
 * copied from elsewhere - kept in this single small file, same "vendor a
 * small proven piece, not a general library" ethos as aes.c.
 */
#include "totp.h"

/* ------------------------------------------------------------------ *
 *  SHA-1 core
 * ------------------------------------------------------------------ */
typedef struct {
	UInt32 state[5];
	UInt32 lenLow, lenHigh;	/* message length in bits, as two 32-bit halves */
	UInt8  buf[64];
	UInt16 bufLen;
} Sha1Ctx;

static UInt32 Rol32(UInt32 v, UInt8 bits) { return (v << bits) | (v >> (32 - bits)); }

static void Sha1Transform(UInt32 st[5], const UInt8 block[64])
{
	UInt32 w[80];
	UInt32 a = st[0], b = st[1], c = st[2], d = st[3], e = st[4];
	UInt8 i;

	for (i = 0; i < 16; i++) {
		UInt16 o = (UInt16) (i * 4);
		w[i] = ((UInt32) block[o] << 24) | ((UInt32) block[o + 1] << 16)
			 | ((UInt32) block[o + 2] << 8) | (UInt32) block[o + 3];
	}
	for (i = 16; i < 80; i++)
		w[i] = Rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

	for (i = 0; i < 80; i++) {
		UInt32 f, k, temp;
		if (i < 20)      { f = (b & c) | ((~b) & d);      k = 0x5A827999UL; }
		else if (i < 40) { f = b ^ c ^ d;                 k = 0x6ED9EBA1UL; }
		else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCUL; }
		else             { f = b ^ c ^ d;                 k = 0xCA62C1D6UL; }
		temp = Rol32(a, 5) + f + e + k + w[i];
		e = d; d = c; c = Rol32(b, 30); b = a; a = temp;
	}
	st[0] += a; st[1] += b; st[2] += c; st[3] += d; st[4] += e;
}

static void Sha1Init(Sha1Ctx *ctx)
{
	ctx->state[0] = 0x67452301UL; ctx->state[1] = 0xEFCDAB89UL;
	ctx->state[2] = 0x98BADCFEUL; ctx->state[3] = 0x10325476UL;
	ctx->state[4] = 0xC3D2E1F0UL;
	ctx->lenLow = 0; ctx->lenHigh = 0; ctx->bufLen = 0;
}

static void Sha1Update(Sha1Ctx *ctx, const UInt8 *data, UInt32 len)
{
	UInt32 i;
	UInt32 bitsAdded = len << 3;
	UInt32 oldLow = ctx->lenLow;
	ctx->lenLow += bitsAdded;
	if (ctx->lenLow < oldLow) ctx->lenHigh++;			/* carry */
	ctx->lenHigh += (len >> 29);						/* bits above 32 (len*8 overflow) */

	for (i = 0; i < len; i++) {
		ctx->buf[ctx->bufLen++] = data[i];
		if (ctx->bufLen == 64) {
			Sha1Transform(ctx->state, ctx->buf);
			ctx->bufLen = 0;
		}
	}
}

/* Appends one byte straight into the block buffer, transforming on fill -
 * deliberately bypasses Sha1Update's bit-length accounting, which must
 * only ever count real message bytes, never the padding/length-suffix
 * bytes appended here (an earlier version routed padding through
 * Sha1Update itself, which silently corrupted the recorded message
 * length - caught by testing against known SHA-1/TOTP vectors before
 * this ever shipped, not by inspection). */
static void Sha1AppendRaw(Sha1Ctx *ctx, UInt8 b)
{
	ctx->buf[ctx->bufLen++] = b;
	if (ctx->bufLen == 64) { Sha1Transform(ctx->state, ctx->buf); ctx->bufLen = 0; }
}

static void Sha1Final(Sha1Ctx *ctx, UInt8 digest[20])
{
	UInt8 lenBytes[8];
	UInt8 i;
	UInt32 finalLow = ctx->lenLow, finalHigh = ctx->lenHigh;	/* snapshot before padding */

	Sha1AppendRaw(ctx, 0x80);
	while (ctx->bufLen != 56) Sha1AppendRaw(ctx, 0);

	lenBytes[0] = (UInt8) (finalHigh >> 24); lenBytes[1] = (UInt8) (finalHigh >> 16);
	lenBytes[2] = (UInt8) (finalHigh >> 8);  lenBytes[3] = (UInt8) finalHigh;
	lenBytes[4] = (UInt8) (finalLow >> 24);  lenBytes[5] = (UInt8) (finalLow >> 16);
	lenBytes[6] = (UInt8) (finalLow >> 8);   lenBytes[7] = (UInt8) finalLow;
	for (i = 0; i < 8; i++) Sha1AppendRaw(ctx, lenBytes[i]);

	for (i = 0; i < 5; i++) {
		digest[i * 4]     = (UInt8) (ctx->state[i] >> 24);
		digest[i * 4 + 1] = (UInt8) (ctx->state[i] >> 16);
		digest[i * 4 + 2] = (UInt8) (ctx->state[i] >> 8);
		digest[i * 4 + 3] = (UInt8) ctx->state[i];
	}
}

/* ------------------------------------------------------------------ *
 *  HMAC-SHA1 (RFC 2104)
 * ------------------------------------------------------------------ */
#define kShaBlockLen 64

static void HmacSha1(const UInt8 *key, UInt16 keyLen, const UInt8 *msg, UInt32 msgLen, UInt8 out[20])
{
	UInt8 keyBlock[kShaBlockLen];
	UInt8 ipad[kShaBlockLen], opad[kShaBlockLen];
	UInt8 inner[20];
	Sha1Ctx ctx;
	UInt16 i;

	MemSet(keyBlock, kShaBlockLen, 0);
	if (keyLen > kShaBlockLen) {
		Sha1Init(&ctx);
		Sha1Update(&ctx, key, keyLen);
		Sha1Final(&ctx, keyBlock);		/* first 20 bytes used, rest already zero */
	} else {
		MemMove(keyBlock, key, keyLen);
	}

	for (i = 0; i < kShaBlockLen; i++) {
		ipad[i] = (UInt8) (keyBlock[i] ^ 0x36);
		opad[i] = (UInt8) (keyBlock[i] ^ 0x5c);
	}

	Sha1Init(&ctx);
	Sha1Update(&ctx, ipad, kShaBlockLen);
	Sha1Update(&ctx, msg, msgLen);
	Sha1Final(&ctx, inner);

	Sha1Init(&ctx);
	Sha1Update(&ctx, opad, kShaBlockLen);
	Sha1Update(&ctx, inner, 20);
	Sha1Final(&ctx, out);
}

/* ------------------------------------------------------------------ *
 *  base32 (RFC 4648) - the standard TOTP secret encoding. Case-
 *  insensitive, tolerates '=' padding and stray whitespace (some
 *  otpauth:// URIs/QR exports include either).
 * ------------------------------------------------------------------ */
static Int8 Base32Val(char c)
{
	if (c >= 'A' && c <= 'Z') return (Int8) (c - 'A');
	if (c >= 'a' && c <= 'z') return (Int8) (c - 'a');
	if (c >= '2' && c <= '7') return (Int8) (c - '2' + 26);
	return -1;
}

static UInt16 Base32Decode(const char *in, UInt8 *out, UInt16 outCap)
{
	UInt32 buf = 0;
	UInt8 bits = 0;
	UInt16 outLen = 0;
	const char *p;

	for (p = in; *p; p++) {
		Int8 v = Base32Val(*p);
		if (v < 0) continue;		/* skip '=', whitespace, separators */
		buf = (buf << 5) | (UInt8) v;
		bits += 5;
		if (bits >= 8) {
			bits -= 8;
			if (outLen >= outCap) break;
			out[outLen++] = (UInt8) (buf >> bits);
		}
	}
	return outLen;
}

/* ------------------------------------------------------------------ *
 *  otpauth:// URI secret extraction - Login.totp is either a bare
 *  base32 secret or a full "otpauth://totp/Label?secret=XXXX&..." URI.
 * ------------------------------------------------------------------ */
static Boolean ExtractSecretParam(const char *uri, char *out, UInt16 outCap)
{
	const char *tag = StrStr((char *) uri, "secret=");
	const char *p;
	UInt16 n = 0;

	if (!tag) return false;
	p = tag + 7;
	while (*p && *p != '&' && n < outCap - 1) out[n++] = *p++;
	out[n] = 0;
	return n > 0;
}

/* ------------------------------------------------------------------ */

UInt32 TotpUnixNow(void)
{
	/* PalmOS epoch (1 Jan 1904) to Unix epoch (1 Jan 1970): 66 years
	 * including 17 leap days = 24107 days = 2082844800 seconds. Required
	 * for interop with any real TOTP verifier (RFC 6238 mandates Unix
	 * time) - depends entirely on the device's own clock being correct. */
	return TimGetSeconds() - 2082844800UL;
}

Boolean TotpGenerateCode(const char *secretOrUri, UInt32 unixSeconds, char codeOut[7])
{
	char secretBuf[128];
	const char *secretText = secretOrUri;
	UInt8 keyBytes[80];
	UInt16 keyLen;
	UInt8 counter[8];
	UInt8 mac[20];
	UInt8 offset;
	UInt32 code;
	UInt32 step;

	codeOut[0] = 0;
	if (!secretOrUri || !secretOrUri[0]) return false;

	if (StrNCompare((char *) secretOrUri, "otpauth://", 10) == 0) {
		if (!ExtractSecretParam(secretOrUri, secretBuf, sizeof(secretBuf))) return false;
		secretText = secretBuf;
	}

	keyLen = Base32Decode(secretText, keyBytes, sizeof(keyBytes));
	if (keyLen == 0) return false;

	step = unixSeconds / 30;
	counter[0] = 0; counter[1] = 0; counter[2] = 0; counter[3] = 0;	/* step fits in 32 bits until year 2106+ */
	counter[4] = (UInt8) (step >> 24);
	counter[5] = (UInt8) (step >> 16);
	counter[6] = (UInt8) (step >> 8);
	counter[7] = (UInt8) step;

	HmacSha1(keyBytes, keyLen, counter, 8, mac);

	offset = (UInt8) (mac[19] & 0x0F);
	code = (((UInt32) (mac[offset] & 0x7F)) << 24)
		 | (((UInt32) mac[offset + 1]) << 16)
		 | (((UInt32) mac[offset + 2]) << 8)
		 | ((UInt32) mac[offset + 3]);
	code %= 1000000UL;

	StrPrintF(codeOut, "%06lu", code);
	return true;
}
