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
static void AbsorbBlock (UInt8 state[16], const UInt8 block[16]) ENCCORE_SECTION;
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

static void AbsorbData (UInt8 state[16], const UInt8* data, UInt32 len) ENCCORE_SECTION;
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

static void StretchOnce (UInt8 state[16], UInt16 counter) ENCCORE_SECTION;
static void StretchOnce (UInt8 state[16], UInt16 counter)
{
	UInt8 block[16];
	MemMove (block, state, 16);
	block[0] = (UInt8)(block[0] ^ (UInt8) counter);
	block[1] = (UInt8)(block[1] ^ (UInt8)(counter >> 8));
	AbsorbBlock (state, block);
}

/* ------------------------------------------------------------------ */

/* ---- randomness (salts, the fallback device key, operation serials) ----
 * A 16-byte pool in the unsaved prefs (ENC_POOL_PREF), carried across
 * launches. Each draw absorbs whatever varies right now -- tick counter, the
 * clock, SysRandom, free dynamic heap, battery voltage -- into the pool with
 * the AES-based MMO compression the key derivation uses, then produces output
 * blocks from the pool by counter (one AES block each) and stirs the pool
 * again, so no two draws share output even with an unchanged clock. Sec35
 * feeds the timing and pen position of every keystroke and tap in its
 * password dialogs into the pool (EncAddEntropy): human timing jitters by a
 * few ticks per stroke, real unpredictability a Palm otherwise lacks. No
 * 32-bit multiply or divide (this runs from code segments; libgcc rule). */
static void EncPoolLoad (UInt8 pool[16]) ENCCORE_SECTION;
static void EncPoolLoad (UInt8 pool[16])
{
	UInt16 sz = 16;
	if (PrefGetAppPreferences (ENC_CREATOR, ENC_POOL_PREF, pool, &sz, false) == noPreferenceFound
			|| sz != 16)
		MemSet (pool, 16, 0);
}

static void EncPoolSave (const UInt8 pool[16]) ENCCORE_SECTION;
static void EncPoolSave (const UInt8 pool[16])
{
	PrefSetAppPreferences (ENC_CREATOR, ENC_POOL_PREF, 1, pool, 16, false);
}

static void EncPoolStir (UInt8 pool[16]) ENCCORE_SECTION;
static void EncPoolStir (UInt8 pool[16])
{
	struct {
		UInt32	ticks, secs;
		UInt16	r1, r2, volts, pad;
		UInt32	heapFree, heapMax;
	} n;
	UInt16 id;

	MemSet (&n, sizeof (n), 0);
	n.ticks = TimGetTicks ();
	n.secs  = TimGetSeconds ();
	n.r1 = (UInt16) SysRandom (0);
	id = MemHeapID (0, 0);					/* heap 0 = the dynamic heap */
	MemHeapFreeBytes (id, &n.heapFree, &n.heapMax);
	n.volts = SysBatteryInfo (false, NULL, NULL, NULL, NULL, NULL, NULL);
	n.r2 = (UInt16) SysRandom (0);
	AbsorbData (pool, (const UInt8*) &n, sizeof (n));
	MemSet (&n, sizeof (n), 0);
}

void EncAddEntropy (const void* data, UInt16 len)
{
	UInt8 pool[16];
	EncPoolLoad (pool);
	AbsorbData (pool, (const UInt8*) data, len);
	EncPoolSave (pool);
	MemSet (pool, 16, 0);
}

void EncRandomBytes (UInt8* buf, UInt16 len)
{
	UInt8	pool[16], blk[16];
	UInt16	off = 0, ctr = 0, n;

	EncPoolLoad (pool);
	EncPoolStir (pool);
	while (off < len)
		{
		MemMove (blk, pool, 16);
		StretchOnce (blk, (UInt16) (0x5200 + ctr++));	/* output block = f(pool, ctr) */
		n = (UInt16) ((len - off >= 16) ? 16 : (len - off));
		MemMove (buf + off, blk, n);
		off += n;
		}
	StretchOnce (pool, 0xA5A5);						/* forward: next draw differs */
	EncPoolSave (pool);
	MemSet (pool, 16, 0);
	MemSet (blk, 16, 0);
}

static UInt16 EncFallbackKey (UInt8* out, UInt16 maxLen) ENCCORE_SECTION;
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

/* EncKeySchedule must be big enough to hold a real struct AES_ctx - caught
 * at compile time (a negative array size) rather than silently truncating
 * the round-key table if aes.h's own sizes ever change. */
typedef char EncKeyScheduleSizeCheck[(sizeof (EncKeySchedule) >= sizeof (struct AES_ctx)) ? 1 : -1];

void EncKeyScheduleInit (EncKeySchedule *sched, const UInt8 key[ENC_KEY_BYTES])
{
	AES_init_ctx ((struct AES_ctx *) sched, key);
}

void EncCryptFast (EncKeySchedule *sched, UInt8* buf, UInt32 len,
                    UInt32 nonceHi, UInt32 nonceLo)
{
	struct AES_ctx *ctx = (struct AES_ctx *) sched;
	UInt8 iv[16];

	MemSet (iv, 16, 0);
	iv[0] = (UInt8)(nonceHi >> 24); iv[1] = (UInt8)(nonceHi >> 16);
	iv[2] = (UInt8)(nonceHi >> 8);  iv[3] = (UInt8) nonceHi;
	iv[4] = (UInt8)(nonceLo >> 24); iv[5] = (UInt8)(nonceLo >> 16);
	iv[6] = (UInt8)(nonceLo >> 8);  iv[7] = (UInt8) nonceLo;

	AES_ctx_set_iv (ctx, iv);
	AES_CTR_xcrypt_buffer (ctx, (uint8_t*) buf, (size_t) len);
}

void EncKeyScheduleWipe (EncKeySchedule *sched)
{
	MemSet (sched, sizeof (*sched), 0);
}

/* ---- session-key cache (Feature Manager) -------------------------------- */

/* The cached Sec35 key, plus the store keys derived from it this session
 * (EncGetStoreKey). One feature block, wiped and freed as a whole. Every app
 * of the suite must agree on this layout (they are updated together). */
#define ENC_STORE_CACHE	16
typedef struct { UInt32 id; UInt8 key[ENC_KEY_BYTES]; } EncStoreEntry;
typedef struct {
	UInt8			key[ENC_KEY_BYTES];
	UInt32			unlockTime;
	UInt16			nStores;
	EncStoreEntry	store[ENC_STORE_CACHE];
} EncKeyBlock;

Err EncCacheKey (const UInt8 key[ENC_KEY_BYTES])
{
	EncKeyBlock	blk;
	void*		p = NULL;
	Err			e;

	EncWipeKey ();
	MemSet (&blk, sizeof (blk), 0);
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
static Err EncSleepNotifyCallback (SysNotifyParamType *notifyParamsP) ENCCORE_SECTION;
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
	/* cached by a pre-scheme-2 Sec35 (still in RAM across the upgrade): treat
	 * as locked, so the first use after the update is a clean unlock */
	if (MemPtrSize (p) < sizeof (EncKeyBlock)) { EncWipeKey (); return false; }
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
	if (MemPtrSize (p) < sizeof (EncKeyBlock)) return -1;	/* pre-scheme-2 block */
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
		/* Wipe before free -- only the chunk's REAL size: a block cached by a
		 * pre-scheme-2 Sec35 (still in RAM across the upgrade) is only
		 * {key, unlockTime}, and writing a whole new-size EncKeyBlock over it
		 * was a fatal DmWriteCheck (found on the device, 2026-11-16). */
		UInt32 n = MemPtrSize ((void*) v);
		if (n > 0) DmSet ((void*) v, 0, n, 0);
		}
	FtrPtrFree (ENC_CREATOR, ENC_KEY_FTR);				/* no-op if absent */
}

/* ---- per-store keys (scheme 2) ----------------------------------------- */

void EncDeriveStoreKey (const UInt8 secKey[ENC_KEY_BYTES], UInt32 storeId,
                        UInt8 keyOut[ENC_KEY_BYTES])
{
	struct AES_ctx	ctx;
	UInt8			b[16];

	MemSet (b, 16, 0);
	b[0] = 'S'; b[1] = '3'; b[2] = '5'; b[3] = 'K';
	b[4] = (UInt8)(storeId >> 24); b[5] = (UInt8)(storeId >> 16);
	b[6] = (UInt8)(storeId >> 8);  b[7] = (UInt8) storeId;
	b[8] = 0; b[9] = ENC_SCHEME;
	AES_init_ctx (&ctx, secKey);
	AES_ECB_encrypt (&ctx, b);
	MemMove (keyOut, b, ENC_KEY_BYTES);
	MemSet (&ctx, sizeof (ctx), 0);
	MemSet (b, 16, 0);
}

Boolean EncGetStoreKey (UInt8 keyOut[ENC_KEY_BYTES], UInt32 storeId)
{
	UInt32			v, now;
	EncKeyBlock*	p;
	UInt16			i;
	EncStoreEntry	e;

	if (FtrGet (ENC_CREATOR, ENC_KEY_FTR, &v) != 0 || v == 0) return false;
	p = (EncKeyBlock*) v;
	/* A block cached by a pre-scheme-2 Sec35 (still in RAM across the upgrade)
	 * is only {key, unlockTime}: never read or write past it -- lock instead. */
	if (MemPtrSize (p) < sizeof (EncKeyBlock)) { EncWipeKey (); return false; }
	now = TimGetSeconds ();
	if (now < p->unlockTime || (now - p->unlockTime) > EncGetTtlSeconds ())
		{ EncWipeKey (); return false; }			/* expired / clock rollback */

	for (i = 0; i < p->nStores && i < ENC_STORE_CACHE; i++)
		if (p->store[i].id == storeId)
			{ MemMove (keyOut, p->store[i].key, ENC_KEY_BYTES); return true; }

	EncDeriveStoreKey (p->key, storeId, keyOut);
	if (p->nStores < ENC_STORE_CACHE)			/* full: still correct, just not cached */
		{
		UInt16 n = (UInt16)(p->nStores + 1);
		e.id = storeId;
		MemMove (e.key, keyOut, ENC_KEY_BYTES);
		DmWrite (p, OffsetOf (EncKeyBlock, store) + p->nStores * sizeof (EncStoreEntry), &e, sizeof (e));
		DmWrite (p, OffsetOf (EncKeyBlock, nStores), &n, sizeof (n));
		MemSet (&e, sizeof (e), 0);
		}
	return true;
}

Boolean EncDbHasStoreKeys (DmOpenRef db)
{
	LocalID	id;
	UInt16	card, ver = 0;

	if (! db || DmOpenDatabaseInfo (db, &id, NULL, NULL, &card, NULL) != errNone)
		return false;
	if (DmDatabaseInfo (card, id, NULL, NULL, &ver, NULL, NULL, NULL, NULL,
			NULL, NULL, NULL, NULL) != errNone)
		return false;
	return (Boolean)((ver & ENC_DBVER_STOREKEYS) != 0);
}

void EncDbSetStoreKeys (DmOpenRef db)
{
	LocalID	id;
	UInt16	card, ver = 0;

	if (! db || DmOpenDatabaseInfo (db, &id, NULL, NULL, &card, NULL) != errNone)
		return;
	if (DmDatabaseInfo (card, id, NULL, NULL, &ver, NULL, NULL, NULL, NULL,
			NULL, NULL, NULL, NULL) != errNone)
		return;
	if (ver & ENC_DBVER_STOREKEYS) return;
	ver |= ENC_DBVER_STOREKEYS;
	DmSetDatabaseInfo (card, id, NULL, NULL, &ver, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL);
}

Boolean EncDbIdHasStoreKeys (LocalID id)
{
	UInt16 ver = 0;
	if (! id || DmDatabaseInfo (0, id, NULL, NULL, &ver, NULL, NULL, NULL, NULL,
			NULL, NULL, NULL, NULL) != errNone)
		return false;
	return (Boolean)((ver & ENC_DBVER_STOREKEYS) != 0);
}

void EncDbIdSetStoreKeys (LocalID id)
{
	UInt16 ver = 0;
	if (! id || DmDatabaseInfo (0, id, NULL, NULL, &ver, NULL, NULL, NULL, NULL,
			NULL, NULL, NULL, NULL) != errNone)
		return;
	if (ver & ENC_DBVER_STOREKEYS) return;
	ver |= ENC_DBVER_STOREKEYS;
	DmSetDatabaseInfo (0, id, NULL, NULL, &ver, NULL, NULL, NULL, NULL,
		NULL, NULL, NULL, NULL);
}

Boolean EncGetStoreKeyDb (UInt8 keyOut[ENC_KEY_BYTES], UInt32 storeId, DmOpenRef db)
{
	if (! EncDbHasStoreKeys (db)) return false;
	return EncGetStoreKey (keyOut, storeId);
}

/* ---- "a passcode is set" signal (independent of the cached key) ---------- */

/* Kept in the UNSAVED prefs (survives a soft reset, unlike a Feature; not
 * HotSync-backed, same as the TTL / lock-on-sleep settings), with the old
 * Feature as well for any reader built before this. Sec35 is the only writer:
 * on set/change/remove/forget, on every launch and at every soft reset. It is
 * a hint, not a lock: apps decide "locked" from their own encrypted records
 * first (ToDo35's pattern) and use this only for data not yet encrypted. */
void EncSetEncryptionActive (Boolean on)
{
	UInt8 v = (UInt8) (on ? 1 : 0);
	PrefSetAppPreferences (ENC_CREATOR, ENC_PWSET_PREF, 1, &v, sizeof (v), false);
	if (on)
		FtrSet (ENC_CREATOR, ENC_PWSET_FTR, 1);
	else
		FtrUnregister (ENC_CREATOR, ENC_PWSET_FTR);		/* no-op if absent */
}

Boolean EncEncryptionActive (void)
{
	UInt8	v = 0;
	UInt16	sz = sizeof (v);
	UInt32	f;

	if (PrefGetAppPreferences (ENC_CREATOR, ENC_PWSET_PREF, &v, &sz, false) != noPreferenceFound
			&& sz == sizeof (v))
		return (Boolean) (v != 0);
	/* never written (Sec35 not opened since the update): the old Feature */
	return (Boolean) (FtrGet (ENC_CREATOR, ENC_PWSET_FTR, &f) == 0);
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
