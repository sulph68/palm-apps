#include "AesCrypt.h"
#include "aes.h"

/* All functions in this file live in the sec001 code segment (see AesCrypt.h /
 * sects.h), keeping the crypto + KDF out of the overflowing main code resource. */

/* Fixed 16-byte domain-separation constant for the KDF (not secret). */
#define KDF_IV_STR  "CryptoPad-KDFv1!"

/* Local database used only as a fallback device secret on devices that have no
 * hardware serial number. Created with the backup attribute cleared so the key
 * never leaves the device (and therefore is not present in a HotSync backup). */
#define DEVKEY_DB_NAME  "CryptoNoteDevKey"
#define DEVKEY_DB_TYPE  'DevK'
#define DEVKEY_CREATOR  'CrNt'

/* ---- forward declarations (all tagged into sec001) ---- */
static void   GatherDeviceKeyEntropy(UInt8 out[16]) AES_SECTION;
static UInt32 NextReseedCounter(void) AES_SECTION;
static void   DrbgSeed(void) AES_SECTION;
static void   DrbgBytes(UInt8 * out, UInt16 n) AES_SECTION;
static void   LoadOrCreateDeviceKey(UInt8 out[16]) AES_SECTION;
static void   GetDeviceSecret(void) AES_SECTION;
static void   AbsorbBlock(UInt8 state[16], const UInt8 block[16]) AES_SECTION;
static void   AbsorbData(UInt8 state[16], const UInt8 * data, UInt16 len) AES_SECTION;
static void   StretchOnce(UInt8 state[16], UInt16 counter) AES_SECTION;
static void   DeriveRecordKey(UInt8 outKey[16], Char * recordPassword,
                  Char * globalPassword, UInt8 * salt, UInt16 rounds) AES_SECTION;
static void   DeriveRecordKeyCached(UInt8 outKey[16], Char * recordPassword,
                  Char * globalPassword, UInt8 * salt, UInt16 rounds) AES_SECTION;

/* ---- cached device secret ---- */
static Boolean gDevReady = false;
static UInt8   gDev[32];
static UInt16  gDevLen = 0;

/* ---- session CTR-DRBG state (see DrbgSeed/DrbgBytes) ----
 * Zero-initialised at each process start, so gDrbgReady is false on every
 * launch and the DRBG is reseeded per session. Starting the block counter at
 * 0 each session is safe only if the seed can never repeat; the entropy pool
 * alone does not guarantee that (a replayed clock/tick trajectory - emulator
 * snapshot restore, clock rollback - could reproduce it), so DrbgSeed also
 * folds in a strictly monotonic reseed counter persisted in the local,
 * non-backed-up device-key DB. That counter advances on every launch and is
 * never present in a HotSync backup, so no two seeds collide. */
static Boolean gDrbgReady = false;
static UInt8   gDrbgKey[16];
static UInt8   gDrbgCtr[16];

/* ---- cached calibration ---- */
static Boolean gRoundsReady = false;
static UInt16  gRounds = 0;

/* ---- one-entry derived-key cache (collapses the verifier+body double
 * derivation of a single record into one ~1.5 s stretch) ---- */
#define KDF_CACHE_PWLEN 128
static Boolean gKeyCacheValid = false;
static char    gKeyCacheRecPw[KDF_CACHE_PWLEN];
static char    gKeyCacheGblPw[KDF_CACHE_PWLEN];
static UInt8   gKeyCacheSalt[16];
static UInt16  gKeyCacheRounds;
static UInt8   gKeyCacheKey[16];


/******************************************
 * GatherDeviceKeyEntropy:
 * Produce a 16-byte key for the fallback
 * device secret from as many independent
 * entropy sources as this platform exposes,
 * condensed through the same NIST-validated
 * AES/MMO compression used by the KDF.
 *
 * Used ONLY for the one-time generation of
 * the persistent device key. Unlike
 * GenerateRandomIV (which only needs IV
 * uniqueness, not secrecy), this key is a
 * real secret, so it must not depend on
 * SysRandom's 16-bit LCG alone.
 ******************************************/
static void GatherDeviceKeyEntropy(UInt8 out[16])
{
	UInt8   state[16];
	UInt8   pool[16];   /* deliberately left partly uninitialised (stack noise) */
	UInt8 * romP = 0;
	UInt16  romSz = 0;
	UInt32  t;
	UInt16  i;

	/* Domain-separated seed, distinct from the KDF's KDF_IV_STR. */
	MemMove(state, "CryptoPad-DevKy!", 16);

	/* Real-time clock and timer resolution: sources independent of the
	 * high-resolution tick counter sampled in the loop below. */
	t = TimGetSeconds();
	AbsorbData(state, (UInt8 *) &t, sizeof(t));
	t = SysTicksPerSecond();
	AbsorbData(state, (UInt8 *) &t, sizeof(t));

	/* Seed SysRandom once from the tick counter, matching the historical
	 * behaviour, but then fold in the whole session sequence rather than a
	 * single draw. Each iteration also re-absorbs fresh tick jitter - the
	 * low-order bits between successive TimGetTicks() reads are the useful,
	 * hard-to-predict part. */
	SysRandom((Int32) TimGetTicks());
	for (i = 0; i < 64; i++) {
		Int16 r = SysRandom(0);
		pool[0] = (UInt8) (r & 0xFF);
		pool[1] = (UInt8) ((r >> 8) & 0xFF);
		t = TimGetTicks();
		pool[2] = (UInt8) (t & 0xFF);
		pool[3] = (UInt8) ((t >> 8) & 0xFF);
		pool[4] = (UInt8) ((t >> 16) & 0xFF);
		pool[5] = (UInt8) ((t >> 24) & 0xFF);
		AbsorbData(state, pool, 6);
	}

	/* Opportunistic ROM token: normally absent/all-0xFF on the serial-less
	 * devices that reach the fallback (that is why we are here), but if any
	 * chip/version token is returned it can only add entropy. */
	if (SysGetROMToken(0, sysROMTokenSnum, &romP, &romSz) == 0 &&
		romP != NULL && romSz > 0 && romSz != 0xFFFF) {
		if (romSz > 64) romSz = 64;
		AbsorbData(state, romP, romSz);
	}

	/* Uninitialised stack noise (never harmful; may add a little entropy). */
	AbsorbData(state, pool, sizeof(pool));

	/* Final one-way condensation so the output is uniform and the pooled
	 * inputs cannot be trivially back-solved. */
	for (i = 0; i < 512; i++)
		StretchOnce(state, i);

	MemMove(out, state, 16);
	MemSet(state, 16, 0);
	MemSet(pool, sizeof(pool), 0);
}


/******************************************
 * DrbgSeed / DrbgBytes:
 * Session CTR-DRBG. DrbgSeed draws a fresh
 * AES key from the entropy pool and resets
 * the counter to zero; DrbgBytes produces
 * output by running AES-CTR over zero bytes
 * (i.e. emitting the raw keystream) and
 * persisting the advanced counter, so no
 * (key,counter) block is ever reused within
 * the session. Seeded lazily on first use.
 *
 * This replaces the SysRandom LCG as the
 * source for all IVs and salts: it needs
 * only uniqueness there, which the monotonic
 * counter guarantees far better than the
 * short-cycling LCG did. See the design
 * audit notes in AesCrypt.h / AUDIT_AES.md.
 ******************************************/
/******************************************
 * NextReseedCounter:
 * Strictly monotonic per-launch counter,
 * persisted as a 4-byte big-endian record
 * (index 1) in the local, non-backed-up
 * device-key DB. Read, increment, write
 * back, return the new value. Because the
 * DB is excluded from HotSync backups the
 * counter cannot be rolled back by a
 * restore, and it advances every launch, so
 * it breaks any replay of the entropy
 * gatherer's clock/tick inputs. Returns 0
 * only if the DB is unavailable (in which
 * case the seed still varies by entropy).
 ******************************************/
static UInt32 NextReseedCounter(void)
{
	LocalID   dbID;
	DmOpenRef db;
	MemHandle h;
	UInt16    idx;
	UInt16    attr;
	UInt8 *   p;
	UInt32    c = 0;
	UInt8     buf[4];

	dbID = DmFindDatabase(0, DEVKEY_DB_NAME);
	if (dbID == 0) {
		DmCreateDatabase(0, DEVKEY_DB_NAME, DEVKEY_CREATOR, DEVKEY_DB_TYPE, false);
		dbID = DmFindDatabase(0, DEVKEY_DB_NAME);
		if (dbID != 0 &&
			DmDatabaseInfo(0, dbID, 0, &attr, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0) {
			attr &= ~dmHdrAttrBackup;
			DmSetDatabaseInfo(0, dbID, 0, &attr, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		}
	}
	if (dbID == 0) return 0;

	db = DmOpenDatabase(0, dbID, dmModeReadWrite);
	if (db == 0) return 0;

	/* The device key lives at record 0; the counter lives at record 1. Ensure
	 * a record 0 exists first so the counter is always index 1. */
	while (DmNumRecords(db) < 2) {
		idx = DmNumRecords(db);
		h = DmNewRecord(db, &idx, 4);
		if (h == 0) { DmCloseDatabase(db); return 0; }
		DmReleaseRecord(db, idx, false);
	}

	h = DmQueryRecord(db, 1);
	if (h != 0 && MemHandleSize(h) >= 4) {
		p = MemHandleLock(h);
		c = ((UInt32) p[0] << 24) | ((UInt32) p[1] << 16) |
		    ((UInt32) p[2] << 8)  |  (UInt32) p[3];
		MemHandleUnlock(h);
	}

	c++;

	buf[0] = (UInt8) (c >> 24);
	buf[1] = (UInt8) (c >> 16);
	buf[2] = (UInt8) (c >> 8);
	buf[3] = (UInt8) c;
	h = DmGetRecord(db, 1);
	if (h != 0) {
		p = MemHandleLock(h);
		DmWrite(p, 0, buf, 4);
		MemHandleUnlock(h);
		DmReleaseRecord(db, 1, true);
	}

	DmCloseDatabase(db);
	return c;
}


static void DrbgSeed(void)
{
	UInt32 rc;

	GatherDeviceKeyEntropy(gDrbgKey);

	/* Fold in the persisted monotonic reseed counter so two launches with an
	 * identical entropy trajectory still derive different keys. Run it through
	 * the same AES/MMO step used elsewhere rather than XORing raw. */
	rc = NextReseedCounter();
	if (rc != 0) {
		UInt8 blk[16];
		MemSet(blk, 16, 0);
		blk[0] = (UInt8) (rc >> 24);
		blk[1] = (UInt8) (rc >> 16);
		blk[2] = (UInt8) (rc >> 8);
		blk[3] = (UInt8) rc;
		AbsorbBlock(gDrbgKey, blk);
		StretchOnce(gDrbgKey, 0);
		MemSet(blk, 16, 0);
	}

	MemSet(gDrbgCtr, 16, 0);
	gDrbgReady = true;
}

static void DrbgBytes(UInt8 * out, UInt16 n)
{
	struct AES_ctx ctx;

	if (!gDrbgReady) DrbgSeed();

	AES_init_ctx_iv(&ctx, gDrbgKey, gDrbgCtr);

	/* out = 0 XOR keystream = keystream; ctx.Iv is advanced past the blocks
	 * consumed, which we persist so the next call continues the counter. */
	MemSet(out, n, 0);
	AES_CTR_xcrypt_buffer(&ctx, out, n);

	MemMove(gDrbgCtr, ctx.Iv, 16);
	MemSet((UInt8 *) &ctx, sizeof(ctx), 0);
}


/******************************************
 * LoadOrCreateDeviceKey:
 * Fallback device secret: a random 16-byte
 * key kept in a local, non-backed-up DB.
 ******************************************/
static void LoadOrCreateDeviceKey(UInt8 out[16])
{
	LocalID  dbID;
	DmOpenRef db;
	MemHandle h;
	UInt16   idx = 0;
	UInt16   attr;
	UInt8 *  p;

	dbID = DmFindDatabase(0, DEVKEY_DB_NAME);
	if (dbID == 0) {
		DmCreateDatabase(0, DEVKEY_DB_NAME, DEVKEY_CREATOR, DEVKEY_DB_TYPE, false);
		dbID = DmFindDatabase(0, DEVKEY_DB_NAME);
		if (dbID != 0 &&
			DmDatabaseInfo(0, dbID, 0, &attr, 0, 0, 0, 0, 0, 0, 0, 0, 0) == 0) {
			attr &= ~dmHdrAttrBackup;
			DmSetDatabaseInfo(0, dbID, 0, &attr, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		}
	}
	if (dbID == 0) { MemSet(out, 16, 0); return; }

	db = DmOpenDatabase(0, dbID, dmModeReadWrite);
	if (db == 0) { MemSet(out, 16, 0); return; }

	/* Ensure record 0 exists. NextReseedCounter() may have created it first as
	 * a short placeholder, so decide whether the key is present by inspecting
	 * the record's contents (16 non-zero bytes), not the record count. */
	if (DmNumRecords(db) == 0) {
		h = DmNewRecord(db, &idx, 16);
		if (h != 0) DmReleaseRecord(db, idx, false);
	}

	h = DmQueryRecord(db, 0);
	if (h != 0) {
		Boolean haveKey = false;
		UInt16  sz = MemHandleSize(h);
		p = MemHandleLock(h);
		if (sz >= 16) {
			UInt16 i;
			for (i = 0; i < 16; i++)
				if (p[i] != 0) { haveKey = true; break; }
		}
		MemHandleUnlock(h);

		if (!haveKey) {
			UInt8 key[16];
			GatherDeviceKeyEntropy(key);
			h = DmResizeRecord(db, 0, 16);
			if (h != 0) {
				p = MemHandleLock(h);
				DmWrite(p, 0, key, 16);
				MemHandleUnlock(h);
				DmReleaseRecord(db, 0, true);
			}
			MemMove(out, key, 16);
			MemSet(key, 16, 0);
		} else {
			p = MemHandleLock(h);
			MemMove(out, p, 16);
			MemHandleUnlock(h);
		}
	} else MemSet(out, 16, 0);

	DmCloseDatabase(db);
}


/******************************************
 * GetDeviceSecret:
 * Loads (once) this device's secret: the
 * hardware serial number if available,
 * otherwise the random local fallback key.
 ******************************************/
static void GetDeviceSecret(void)
{
	UInt8 * romP = 0;
	UInt16  romSz = 0;
	Err     err;
	UInt16  i;
	Boolean allFF;

	if (gDevReady) return;

	err = SysGetROMToken(0, sysROMTokenSnum, &romP, &romSz);
	if (err == 0 && romP != NULL && romSz > 0 && romSz != 0xFFFF) {
		allFF = true;
		for (i = 0; i < romSz; i++)
			if (romP[i] != 0xFF) { allFF = false; break; }
		if (!allFF) {
			if (romSz > sizeof(gDev)) romSz = sizeof(gDev);
			MemMove(gDev, romP, romSz);
			gDevLen = romSz;
			gDevReady = true;
			return;
		}
	}

	LoadOrCreateDeviceKey(gDev);
	gDevLen = 16;
	gDevReady = true;
}


/******************************************
 * AbsorbBlock / AbsorbData:
 * MMO compression - state = E_state(block)
 * XOR block - over one or more 16-byte
 * zero-padded blocks of input.
 ******************************************/
static void AbsorbBlock(UInt8 state[16], const UInt8 block[16])
{
	struct AES_ctx ctx;
	UInt8 ct[16];
	UInt8 j;

	AES_init_ctx(&ctx, state);
	MemMove(ct, block, 16);
	AES_ECB_encrypt(&ctx, ct);
	for (j = 0; j < 16; j++)
		state[j] = ct[j] ^ block[j];
}

static void AbsorbData(UInt8 state[16], const UInt8 * data, UInt16 len)
{
	UInt16 nBlocks, i, chunk;
	UInt8  block[16];

	nBlocks = (len + 15) / 16;
	if (nBlocks == 0) nBlocks = 1;

	for (i = 0; i < nBlocks; i++) {
		MemSet(block, 16, 0);
		chunk = len - (UInt16)(i * 16);
		if (chunk > 16) chunk = 16;
		if (chunk > 0) MemMove(block, data + (i * 16), chunk);
		AbsorbBlock(state, block);
	}
}


/******************************************
 * StretchOnce:
 * One iteration of the work-factor loop.
 * A counter is mixed in to avoid fixed
 * points; the step is the same MMO
 * compression keyed by the running state.
 ******************************************/
static void StretchOnce(UInt8 state[16], UInt16 counter)
{
	struct AES_ctx ctx;
	UInt8 pt[16];
	UInt8 ct[16];
	UInt8 j;

	MemMove(pt, state, 16);
	pt[14] ^= (UInt8)(counter >> 8);
	pt[15] ^= (UInt8)(counter & 0xFF);

	AES_init_ctx(&ctx, state);
	MemMove(ct, pt, 16);
	AES_ECB_encrypt(&ctx, ct);
	for (j = 0; j < 16; j++)
		state[j] = ct[j] ^ pt[j];
}


/******************************************
 * CalibrateKdfRounds:
 * Measure how many StretchOnce iterations
 * run in ~1.5 s on this device. Cached.
 ******************************************/
UInt16 CalibrateKdfRounds(void)
{
	UInt8  st[16];
	UInt32 ticksPerSec, target, start, n;

	if (gRoundsReady) return gRounds;

	MemMove(st, KDF_IV_STR, 16);
	ticksPerSec = SysTicksPerSecond();
	target = (ticksPerSec * 3) / 2;          /* 1.5 s */
	if (target == 0) target = ticksPerSec;

	start = TimGetTicks();
	n = 0;
	while ((TimGetTicks() - start) < target) {
		StretchOnce(st, (UInt16) n);
		n++;
		if (n >= 0xFFF0UL) break;            /* cap to fit UInt16 storage */
	}
	if (n == 0) n = 1;

	gRounds = (UInt16) n;
	gRoundsReady = true;
	return gRounds;
}


/******************************************
 * DeriveRecordKey:
 * Fold all inputs into a seed, then stretch
 * it `rounds` times. See AesCrypt.h.
 ******************************************/
static void DeriveRecordKey(UInt8 outKey[16], Char * recordPassword,
	Char * globalPassword, UInt8 * salt, UInt16 rounds)
{
	UInt8  state[16];
	UInt32 i;

	GetDeviceSecret();

	MemMove(state, KDF_IV_STR, 16);
	AbsorbData(state, (UInt8 *) recordPassword, StrLen(recordPassword));
	if (globalPassword != 0 && *globalPassword != '\0')
		AbsorbData(state, (UInt8 *) globalPassword, StrLen(globalPassword));
	AbsorbData(state, gDev, gDevLen);
	AbsorbData(state, salt, AES_SALT_SIZE);

	for (i = 0; i < rounds; i++)
		StretchOnce(state, (UInt16) i);

	MemMove(outKey, state, 16);
	MemSet(state, 16, 0);
}

static void DeriveRecordKeyCached(UInt8 outKey[16], Char * recordPassword,
	Char * globalPassword, UInt8 * salt, UInt16 rounds)
{
	Char * g = (globalPassword != 0) ? globalPassword : "";

	if (gKeyCacheValid && gKeyCacheRounds == rounds &&
		StrCompare(gKeyCacheRecPw, recordPassword) == 0 &&
		StrCompare(gKeyCacheGblPw, g) == 0 &&
		MemCmp(gKeyCacheSalt, salt, 16) == 0) {
		MemMove(outKey, gKeyCacheKey, 16);
		return;
	}

	DeriveRecordKey(outKey, recordPassword, globalPassword, salt, rounds);

	if (StrLen(recordPassword) < KDF_CACHE_PWLEN && StrLen(g) < KDF_CACHE_PWLEN) {
		StrCopy(gKeyCacheRecPw, recordPassword);
		StrCopy(gKeyCacheGblPw, g);
		MemMove(gKeyCacheSalt, salt, 16);
		gKeyCacheRounds = rounds;
		MemMove(gKeyCacheKey, outKey, 16);
		gKeyCacheValid = true;
	} else gKeyCacheValid = false;
}


/******************************************
 * GenerateRandomIV:
 * Fills iv (or a salt) with 16 bytes from
 * the session CTR-DRBG (DrbgBytes). Suitable
 * for IV/salt uniqueness; the monotonic DRBG
 * counter guarantees no repeat within a
 * session, and the per-session key is drawn
 * from the entropy pool. Not claimed to be a
 * prediction-resistant DRBG - IVs and salts
 * are non-secret, only unique.
 ******************************************/
void GenerateRandomIV(UInt8 iv[AES_IV_SIZE])
{
	DrbgBytes(iv, AES_IV_SIZE);
}


/******************************************
 * AESCryptEx:
 * Hardened encryption/decryption entry
 * point. See AesCrypt.h for the rationale.
 ******************************************/
void AESCryptEx(Char * input, Char * output,
	Char * recordPassword, Char * globalPassword,
	UInt8 * iv, UInt8 * salt, UInt16 rounds,
	UInt16 nLength, Int16 encrypt)
{
	UInt8 key[AES_KEY_SIZE];
	UInt8 ivCopy[AES_IV_SIZE];
	struct AES_ctx ctx;

	/* CTR is its own inverse; encrypt is kept only for call-site symmetry. */
	(void) encrypt;

	DeriveRecordKeyCached(key, recordPassword, globalPassword, salt, rounds);

	MemMove(ivCopy, iv, AES_IV_SIZE);
	AES_init_ctx_iv(&ctx, key, ivCopy);

	if (input != output)
		MemMove(output, input, nLength);

	AES_CTR_xcrypt_buffer(&ctx, (UInt8 *) output, nLength);
	output[nLength] = '\0';

	MemSet(key, AES_KEY_SIZE, 0);
}

void AesCtrAdvanceIV(UInt8 iv[AES_IV_SIZE], UInt32 blocks)
{
	Int16 i;
	for (i = AES_IV_SIZE - 1; i >= 0 && blocks; i--)
		{
		UInt32 sum = (UInt32) iv[i] + (blocks & 0xFF);
		iv[i] = (UInt8) sum;
		blocks = (blocks >> 8) + (sum >> 8);
		}
}
