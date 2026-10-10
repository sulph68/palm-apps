/*
 * enccore.c - see enccore.h. Device-bound AES-128-CTR + stretched KDF + an
 * in-RAM (Feature Manager) session-key cache with a TTL. Uses the bundled
 * tiny-AES forward cipher (aes.c). Original code.
 */
#include <PalmOS.h>
#include "enccore.h"
#include "aes.h"

#define ENC_FALLBACK_PREF   1	/* unsaved pref holding the random device key */

/* ------------------------------------------------------------------ *
 * MMO (Matyas-Meyer-Oseas) one-way compression over AES:
 *     state = E_state(block) XOR block
 * ------------------------------------------------------------------ */
static void AbsorbBlock (UInt8 state[16], const UInt8 block[16])
{
	struct AES_ctx	ctx;
	UInt8			ct[16];
	UInt8			i;

	AES_init_ctx (&ctx, state);			/* key = current state          */
	MemMove (ct, block, 16);
	AES_ECB_encrypt (&ctx, ct);			/* ct = E_state(block)          */
	for (i = 0; i < 16; i++)
		state[i] = (UInt8)(ct[i] ^ block[i]);
	MemSet (&ctx, sizeof (ctx), 0);
}

static void AbsorbData (UInt8 state[16], const UInt8* data, UInt32 len)
{
	UInt8	block[16];
	UInt32	off = 0;

	while (off < len)
		{
		UInt16 n = (UInt16)((len - off >= 16) ? 16 : (len - off));
		MemSet (block, 16, 0);
		MemMove (block, data + off, n);
		AbsorbBlock (state, block);
		off += n;
		}
	/* domain/length terminator so distinct inputs don't collide */
	MemSet (block, 16, 0);
	block[0] = (UInt8) len;
	block[1] = (UInt8)(len >> 8);
	AbsorbBlock (state, block);
}

static void StretchOnce (UInt8 state[16], UInt16 counter)
{
	UInt8 block[16];
	MemMove (block, state, 16);
	block[0] = (UInt8)(block[0] ^ (UInt8) counter);
	block[1] = (UInt8)(block[1] ^ (UInt8)(counter >> 8));
	AbsorbBlock (state, block);
}

/* ------------------------------------------------------------------ */

void EncRandomBytes (UInt8* buf, UInt16 len)
{
	UInt16	i;
	UInt32	s = TimGetTicks () ^ ((UInt32) TimGetSeconds () << 8)
					^ ((UInt32)(UInt16) SysRandom (0) << 3);

	for (i = 0; i < len; i++)
		{
		s = s * 1103515245UL + 12345UL;					/* LCG step        */
		s ^= ((UInt32)(UInt16) SysRandom (0)) << (i & 15);
		s ^= TimGetTicks ();
		buf[i] = (UInt8)(s >> 16);
		}
}

static UInt16 EncFallbackKey (UInt8* out, UInt16 maxLen)
{
	UInt8	k[ENC_KEY_BYTES];
	UInt16	sz = sizeof (k);

	if (PrefGetAppPreferences (ENC_CREATOR, ENC_FALLBACK_PREF, k, &sz, false)
			== noPreferenceFound || sz != ENC_KEY_BYTES)
		{
		EncRandomBytes (k, ENC_KEY_BYTES);
		/* saved = false -> lives in the unsaved-prefs DB, NOT HotSync-backed */
		PrefSetAppPreferences (ENC_CREATOR, ENC_FALLBACK_PREF, 1,
			k, ENC_KEY_BYTES, false);
		}
	if (maxLen > ENC_KEY_BYTES) maxLen = ENC_KEY_BYTES;
	MemMove (out, k, maxLen);
	MemSet (k, sizeof (k), 0);
	return maxLen;
}

UInt16 EncDeviceSecret (UInt8* out, UInt16 maxLen)
{
	UInt8*	romP = 0;
	UInt16	romSz = 0;
	UInt16	i;
	Boolean	allFF;

	if (SysGetROMToken (0, sysROMTokenSnum, &romP, &romSz) == 0 &&
		 romP != NULL && romSz > 0 && romSz != 0xFFFF)
		{
		allFF = true;
		for (i = 0; i < romSz; i++)
			if (romP[i] != 0xFF) { allFF = false; break; }
		if (! allFF)
			{
			if (romSz > maxLen) romSz = maxLen;
			MemMove (out, romP, romSz);
			return romSz;
			}
		}
	return EncFallbackKey (out, maxLen);		/* no usable serial */
}

void EncDeriveKey (const char* passcode, const UInt8 salt[ENC_SALT_BYTES],
                   UInt16 rounds, UInt8 keyOut[ENC_KEY_BYTES])
{
	UInt8	state[16];
	UInt8	dev[32];
	UInt16	devLen, i;

	MemSet (state, 16, 0);
	AbsorbData (state, (const UInt8*) passcode, (UInt32) StrLen (passcode));
	devLen = EncDeviceSecret (dev, sizeof (dev));
	AbsorbData (state, dev, devLen);
	AbsorbData (state, salt, ENC_SALT_BYTES);

	for (i = 0; i < rounds; i++)
		StretchOnce (state, i);

	MemMove (keyOut, state, ENC_KEY_BYTES);
	MemSet (state, 16, 0);
	MemSet (dev, sizeof (dev), 0);
}

UInt16 EncCalibrateRounds (UInt16 targetMs)
{
	UInt8	state[16];
	UInt32	target = (SysTicksPerSecond () * targetMs) / 1000UL;
	UInt32	start, n = 0;

	if (target == 0) target = 1;
	MemSet (state, 16, 0x5A);
	start = TimGetTicks ();
	while ((TimGetTicks () - start) < target)
		{
		StretchOnce (state, (UInt16) n);
		n++;
		if (n >= 0xFFF0UL) break;			/* keep it in a UInt16 */
		}
	return (UInt16)((n == 0) ? 1 : n);
}

void EncCrypt (const UInt8 key[ENC_KEY_BYTES], UInt8* buf, UInt32 len,
               UInt32 nonceHi, UInt32 nonceLo)
{
	struct AES_ctx	ctx;
	UInt8			iv[16];

	MemSet (iv, 16, 0);
	iv[0] = (UInt8)(nonceHi >> 24); iv[1] = (UInt8)(nonceHi >> 16);
	iv[2] = (UInt8)(nonceHi >> 8);  iv[3] = (UInt8) nonceHi;
	iv[4] = (UInt8)(nonceLo >> 24); iv[5] = (UInt8)(nonceLo >> 16);
	iv[6] = (UInt8)(nonceLo >> 8);  iv[7] = (UInt8) nonceLo;

	AES_init_ctx_iv (&ctx, key, iv);
	AES_CTR_xcrypt_buffer (&ctx, (uint8_t*) buf, (size_t) len);
	MemSet (&ctx, sizeof (ctx), 0);
}

/* ---- session-key cache (Feature Manager) -------------------------------- */

typedef struct { UInt8 key[ENC_KEY_BYTES]; UInt32 unlockTime; } EncKeyBlock;

Err EncCacheKey (const UInt8 key[ENC_KEY_BYTES])
{
	EncKeyBlock	blk;
	void*		p = NULL;
	Err			e;

	EncWipeKey ();
	MemMove (blk.key, key, ENC_KEY_BYTES);
	blk.unlockTime = TimGetSeconds ();

	e = FtrPtrNew (ENC_CREATOR, ENC_KEY_FTR, sizeof (EncKeyBlock), &p);
	if (e || ! p) { MemSet (&blk, sizeof (blk), 0); return e ? e : dmErrMemError; }

	DmWrite (p, 0, &blk, sizeof (blk));
	MemSet (&blk, sizeof (blk), 0);
	return 0;
}

UInt32 EncGetTtlSeconds (void)
{
	UInt16	sz = sizeof (UInt32);
	UInt32	seconds = 300;		/* 5 minutes -- matches Sec35's own gPref default */

	if (PrefGetAppPreferences (ENC_CREATOR, ENC_TTL_PREF, &seconds, &sz, false)
			== noPreferenceFound || sz != sizeof (UInt32))
		return 300;
	return seconds;
}

void EncSetTtlSeconds (UInt32 seconds)
{
	PrefSetAppPreferences (ENC_CREATOR, ENC_TTL_PREF, 1, &seconds, sizeof (seconds), false);
}

Boolean EncGetLockOnSleep (void)
{
	UInt8	on = 0;
	UInt16	sz = sizeof (on);

	if (PrefGetAppPreferences (ENC_CREATOR, ENC_LOCKONSLEEP_PREF, &on, &sz, false)
			== noPreferenceFound || sz != sizeof (on))
		return false;
	return (Boolean) (on != 0);
}

void EncSetLockOnSleep (Boolean on)
{
	UInt8 v = (UInt8) (on ? 1 : 0);
	PrefSetAppPreferences (ENC_CREATOR, ENC_LOCKONSLEEP_PREF, 1, &v, sizeof (v), false);
}

/* SysNotifyProcPtr callback: fires from SysHandleEvent, inside whichever
 * suite app is currently resident, just before the device sleeps. Only wipes
 * if the user actually turned the feature on -- otherwise a no-op, so
 * registering this costs nothing when the checkbox is off. */
static Err EncSleepNotifyCallback (SysNotifyParamType *notifyParamsP)
{
	(void) notifyParamsP;
	if (EncGetLockOnSleep ())
		EncWipeKey ();
	return errNone;
}

void EncRegisterSleepWipe (void)
{
	UInt16	cardNo;
	LocalID	dbID;

	if (SysCurAppDatabase (&cardNo, &dbID) == errNone)
		SysNotifyRegister (cardNo, dbID, sysNotifySleepNotifyEvent,
			EncSleepNotifyCallback, sysNotifyNormalPriority, NULL);
}

void EncUnregisterSleepWipe (void)
{
	UInt16	cardNo;
	LocalID	dbID;

	if (SysCurAppDatabase (&cardNo, &dbID) == errNone)
		SysNotifyUnregister (cardNo, dbID, sysNotifySleepNotifyEvent, sysNotifyNormalPriority);
}

Boolean EncKeyPresent (void)
{
	UInt8	dummy[ENC_KEY_BYTES];
	Boolean	present = EncGetKey (dummy, EncGetTtlSeconds ());

	MemSet (dummy, ENC_KEY_BYTES, 0);
	return present;
}

Boolean EncGetKey (UInt8 keyOut[ENC_KEY_BYTES], UInt32 ttlSeconds)
{
	UInt32			v, now;
	EncKeyBlock*	p;

	if (FtrGet (ENC_CREATOR, ENC_KEY_FTR, &v) != 0 || v == 0) return false;
	p = (EncKeyBlock*) v;
	now = TimGetSeconds ();
	if (now < p->unlockTime || (now - p->unlockTime) > ttlSeconds)
		{ EncWipeKey (); return false; }			/* expired / clock rollback */

	MemMove (keyOut, p->key, ENC_KEY_BYTES);
	return true;
}

/* Read-only peek at how many seconds are left before EncGetKey/EncKeyPresent
 * would consider the cached key expired under ttlSeconds -- -1 if no key is
 * cached or it's already expired. Never mutates/wipes anything (unlike
 * EncGetKey), so it's safe to call purely for UI display (a countdown) on
 * every redraw without side effects. */
Int32 EncSecondsRemaining (UInt32 ttlSeconds)
{
	UInt32			v, now, elapsed;
	EncKeyBlock*	p;

	if (FtrGet (ENC_CREATOR, ENC_KEY_FTR, &v) != 0 || v == 0) return -1;
	p = (EncKeyBlock*) v;
	now = TimGetSeconds ();
	if (now < p->unlockTime) return -1;			/* clock rollback -- treat as expired */
	elapsed = now - p->unlockTime;
	if (elapsed > ttlSeconds) return -1;
	return (Int32) (ttlSeconds - elapsed);
}

void EncWipeKey (void)
{
	UInt32 v;
	if (FtrGet (ENC_CREATOR, ENC_KEY_FTR, &v) == 0 && v != 0)
		{
		EncKeyBlock zero;
		MemSet (&zero, sizeof (zero), 0);
		DmWrite ((void*) v, 0, &zero, sizeof (zero));	/* wipe before free */
		}
	FtrPtrFree (ENC_CREATOR, ENC_KEY_FTR);				/* no-op if absent */
}

/* ---- "a passcode is set" signal (independent of the cached key) ---------- */

void EncSetEncryptionActive (Boolean on)
{
	if (on)
		FtrSet (ENC_CREATOR, ENC_PWSET_FTR, 1);
	else
		FtrUnregister (ENC_CREATOR, ENC_PWSET_FTR);		/* no-op if absent */
}

Boolean EncEncryptionActive (void)
{
	UInt32 v;
	return (Boolean) (FtrGet (ENC_CREATOR, ENC_PWSET_FTR, &v) == 0);
}

/* ---- password entry method (Sec35 writes, other apps only read) --------- */

UInt8 EncGetPasswordEntryType (void)
{
	UInt8	type;
	UInt16	sz = sizeof (type);

	if (PrefGetAppPreferences (ENC_CREATOR, ENC_PWTYPE_PREF, &type, &sz, false)
			== noPreferenceFound || sz != sizeof (type))
		return kPwTypeText;		/* never set (Sec35 never launched) -> safe default */
	return type;
}

void EncSetPasswordEntryType (UInt8 type)
{
	/* saved = false -> unsaved-prefs DB, NOT HotSync-backed, same as
	 * EncDeviceSecret's fallback key -- purely a local UI preference. */
	PrefSetAppPreferences (ENC_CREATOR, ENC_PWTYPE_PREF, 1, &type, sizeof (type), false);
}
