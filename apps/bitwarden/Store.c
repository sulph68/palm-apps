/*
 * Store.c - local at-rest vault store. AES-128-CTR (enccore.c/aes.c) keyed
 * by a key derived from the Bitwarden master password itself, NOT from
 * Sec35's shared suite passcode - the two are different secrets and must
 * stay independent (see BitWarden.h's own comment on BwUnlock). This file
 * owns:
 *   - its own session key cache (mirrors enccore.c's EncCacheKey/EncGetKey
 *     shape exactly, but under this app's own Feature Manager slot, so it
 *     can never be confused with the suite-wide Sec35 unlock state)
 *   - the on-device BitWardenDB.pdb record format
 *   - a small prefs blob (server URL, TOFU-pinned fingerprint, salt/
 *     rounds/check-value, TTL) via PrefGetAppPreferences/
 *     PrefSetAppPreferences, the same mechanism enccore.c itself already
 *     uses for its own small persisted bits.
 */
#include "BitWarden.h"
#include "enccore.h"

#define bwDBName	"BitWardenDB"
#define bwDBType	'BWit'
/* Real-device report (2026-09): cancelling (or otherwise failing) a sync
 * left the vault EMPTY - every previously-synced record gone, not just
 * whatever this attempt would have added. Root cause: BwStoreBeginSync
 * used to delete+recreate bwDBName itself immediately at the START of
 * every sync, before a single item had even arrived - a cancel/failure at
 * ANY point after that left only whatever partial content this one
 * attempt had managed to stream in (often nothing). Fixed by building into
 * a SEPARATE temporary database instead, only replacing the real one
 * (BwStoreEndSync) once the sync has FULLY succeeded (BW_SYNC_DONE) - a
 * cancel/failure anywhere before that (BwStoreAbortSync) just discards the
 * temp database and reopens the real one, completely untouched. */
#define bwDBNameTemp	"BitWardenDB_new"

#define bwPrefID	1
#define bwKeyFtr	2000	/* FtrPtrNew value = BwKeyBlock, under appCreator */
#define bwCachedPwFtr	2001	/* FtrPtrNew value = BwCachedPwBlock, under appCreator - see BwGetCachedPassword's own comment */
#define bwCachedPwMax	128

/* Baud rate - its own SEPARATE pref ID (not a BwPrefsBlock field) very
 * deliberately: LoadPrefs's own "sz == sizeof(gPrefs)" check means any
 * future BwPrefsBlock layout change (adding a field, like this session's
 * archived flag) already wipes the WHOLE block (server URL/email/2FA-
 * remember/local-vault salt) on next launch - see bwStoreFormatVersion's own
 * comment for why that's an accepted, existing cost for THAT struct. Adding
 * baud rate to it would mean picking a baud rate for the first time nukes
 * the user's local vault too, an unrelated and much bigger blast radius for
 * what should be a one-byte, independent setting. Real-device root cause
 * (2026-09): "stuck at connecting" / "bwReadFrame: no length byte" after
 * switching WiRSa's own serial-speed menu setting to 19200 for a flaky cable
 * - this app's own SrmOpen was hardcoded to 57600 (Serial.c), so the two
 * sides were talking at different rates the whole time (garbled bytes, not
 * a real protocol bug). kBwBauds/bwBaudCount mirror WiRSa's own bauds[]
 * (main.cpp) exactly, same order, so PrefsForm.c's popup index always maps
 * to a rate WiRSa itself can actually be configured to. */
#define bwBaudPrefID	2
#define bwDefaultBaudIndex	7	/* 57600 - preserves this app's prior hardcoded behavior for anyone who hasn't touched WiRSa's own baud setting */
static const UInt32 kBwBauds[] = { 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
#define bwBaudCount	9

UInt8 BwGetBaudIndex(void)
{
	UInt8 idx;
	UInt16 sz = sizeof(idx);
	if (PrefGetAppPreferences(appCreator, bwBaudPrefID, &idx, &sz, false) == noPreferenceFound
		|| sz != sizeof(idx) || idx >= bwBaudCount) {
		return bwDefaultBaudIndex;
	}
	return idx;
}

void BwSetBaudIndex(UInt8 idx)
{
	if (idx >= bwBaudCount) idx = bwDefaultBaudIndex;
	PrefSetAppPreferences(appCreator, bwBaudPrefID, 1, &idx, sizeof(idx), false);
}

UInt32 BwGetBaud(void)
{
	return kBwBauds[BwGetBaudIndex()];
}

/* Record header layout (big-endian on-disk convention, matches this
 * project's own AddressDB35.c rdU16/wrU16 idiom):
 *   [0]           itemType (0=folder marker, bwOrgMarkerType=org marker,
 *                 else a real PROTOCOL.md itemType 1..4)
 *   [1..2]        folderIndex (plaintext - fast filtering without
 *                 decrypting every record). For a folder/org MARKER record,
 *                 this instead holds the marker's own sequence index
 *                 (folder and org markers both reuse this one slot for that
 *                 - see BwStoreAddFolder/BwStoreAddOrg).
 *   [3..4]        orgIndex (plaintext, same fast-filtering reasoning).
 *                 Unused (0xFFFF) on marker records.
 *   [5..41]       guid[37] (plaintext, NUL-included - not sensitive by
 *                 itself, kept for a future edit/PUT feature per the
 *                 project plan; blank on marker records)
 *   [42..101]     fieldLen[30] (UInt16 each, CTR mode so ciphertext len ==
 *                 plaintext len - no padding to account for). A marker
 *                 record only ever uses fieldLen[0] (its name).
 *   [102]         archived (plaintext, 0/1 - same fast-filtering reasoning
 *                 as folderIndex/orgIndex above; meaningless/always 0 on
 *                 marker records, added bwStoreFormatVersion 4, see its own
 *                 comment)
 *   [103..]       field ciphertexts back to back, in bwField* order
 */
#define bwOrgMarkerType	0xFE
/* Own dedicated byte at the end of the fixed header (bwHdrFixedSize's own
 * comment) - kept as a separate #define rather than folded into
 * bwHdrFixedSize's expression so every read/write site can name it, instead
 * of every caller re-deriving "last byte of the header" via arithmetic. */
#define bwArchivedByteOff	(1 + 2 + 2 + bwGuidLen + bwFieldCount * 2)
#define bwHdrFixedSize	(bwArchivedByteOff + 1)
/* Not every field is bwMaxField-sized any more (see BitWardenRsc.h's own
 * comment on bwField512Count/bwMaxExtField) - size the shared scratch buffer
 * for the real worst case, not a uniform bwFieldCount*bwMaxField that would
 * both waste RAM and (more importantly) not even bound the actual per-field
 * writes below, which use FieldMaxLen(i) per field now. */
#define bwRecBufSize	(bwHdrFixedSize + bwField512Count * bwMaxField \
	+ (bwFieldCount - bwField512Count) * bwMaxExtField)

static UInt8 gRecBuf[bwRecBufSize];

/* The real per-field buffer size cap - bwMaxField for the original 6 fields,
 * bwMaxExtField for every Card/Identity field added after them. Used
 * everywhere a field's on-disk length is clamped before being copied into
 * its BwItemFull buffer - using a single global bwMaxField for this (the
 * old assumption, before Card/Identity got their own dedicated fields) would
 * let a corrupt/oversized on-disk length overflow one of the new, smaller
 * bwMaxExtField buffers. */
static UInt16 FieldMaxLen(UInt16 fieldIndex)
{
	return (fieldIndex < bwField512Count) ? bwMaxField : bwMaxExtField;
}

/* Write-only landing spot for BwStoreGetItemFull's own per-record field
 * indices that don't belong to that record's itemType (see its own comment)
 * - sized to the largest FieldMaxLen so it's safe regardless of which tier
 * of field ends up routed here. Never read back as meaningful data. */
static char gUnusedFieldScratch[bwMaxField];

typedef struct { UInt8 key[ENC_KEY_BYTES]; UInt32 unlockTime; } BwKeyBlock;

typedef struct {
	UInt8 saltSet;
	UInt8 salt[ENC_SALT_BYTES];
	UInt16 rounds;
	UInt8 check[8];
	UInt32 ttlSeconds;
	UInt8 hasFingerprint;
	UInt8 fingerprint[32];
	char serverURL[128];
	char email[80];
	UInt8 formatVersion;	/* bwStoreFormatVersion - see BwStoreInit */
	/* Bitwarden's own "remember this device for 2FA" token (PROTOCOL.md's
	 * SYNC/SYNC_DONE) - credential-adjacent (lets a future sync skip a real
	 * auth factor), so encrypted at rest, unlike the plaintext fields above
	 * which don't grant access to anything by themselves. See
	 * BwGetTwoFactorRemember/BwSetTwoFactorRemember for why this uses only
	 * the Sec35 key (not this app's own local-vault key, which isn't
	 * necessarily unlocked yet at the point the SYNC frame is being built). */
	UInt16 twoFactorRememberLen;	/* 0 = none stored */
	UInt8 twoFactorRememberEnc[bwTwoFactorRememberMax];
	/* Options > Security > 2FA - bwTwoFactorEntryText/bwTwoFactorEntryNumeric
	 * (BitWarden.h). Not secret, plaintext is fine. */
	UInt8 twoFactorEntryMode;
} BwPrefsBlock;
/* Session-cached master password is NOT in this struct (moved back out,
 * see BwGetCachedPassword's own comment below) - explicit user request
 * (2026-08): RAM-only, not written to prefs/storage, but still surviving
 * app exit and Sec35-protected. */

/* Bumped from the implicit pre-Sec35-wrap format (1) to 2 when the Sec35
 * outer-wrap layer was added - existing on-disk records are single-layer
 * (BitWarden-key-only) and would misread as garbage under the new
 * double-decrypt read path. Bumped again to 3 when Card/Identity got their
 * own dedicated per-field wire/storage slots (bwFieldCount 6 -> 30) -
 * bwHdrFixedSize is derived from bwFieldCount, so an old record's on-disk
 * header is a completely different size/shape now; reading it with the new
 * layout would misinterpret content bytes as length-prefix bytes and vice
 * versa, not just show stale data. BwStoreInit does a one-time full wipe
 * when the stored version doesn't match, then bumps it - same clean-slate
 * the app already does on every real sync (BwStoreBeginSync), just
 * triggered once proactively so stale-format bytes are never shown instead
 * of only being replaced on the next sync. Bumped again to 4 when the
 * archived flag got its own dedicated header byte (bwArchivedByteOff) -
 * bwHdrFixedSize grew by 1, so every field-length/field-data offset in an
 * old on-disk record is off by one byte under the new layout (same failure
 * mode as the v2->v3 bump: content bytes misread as length-prefix bytes and
 * vice versa). At the time of THIS bump, WiRSa didn't send the flag yet
 * (see PROTOCOL.md's own SYNC_ITEM note for when that changed) - this wipe
 * still applies the same mechanical safety net regardless. */
#define bwStoreFormatVersion	4

static const UInt8 kCheckMagic[8] = { 'B','W','C','H','K','0','0','1' };
/* Reserved nonceHi sentinel for the check value and folder-name encryption,
 * chosen because a real PalmOS DmUniqueID is 24-bit (0..0xFFFFFF) and can
 * never reach this range - see BwStoreAddFolder for the folder-name case. */
#define bwCheckNonceHi	0xFFFFFFFFUL
#define bwFolderNonceHi	0xFFFFFFFEUL
#define bwOrgNonceHi	0xFFFFFFFDUL

static DmOpenRef gDB = NULL;
static Boolean gPrefsLoaded = false;
static BwPrefsBlock gPrefs;

/* ------------------------------------------------------------------ *
 *  byte helpers (matches AddressDB35.c's own rdU16/wrU16 convention)
 * ------------------------------------------------------------------ */
static UInt16 rdU16(const UInt8 *p) { return (UInt16) (((UInt16) p[0] << 8) | (UInt16) p[1]); }
static void wrU16(UInt8 *p, UInt16 v) { p[0] = (UInt8) (v >> 8); p[1] = (UInt8) v; }

/* ------------------------------------------------------------------ *
 *  prefs blob
 * ------------------------------------------------------------------ */
static void LoadPrefs(void)
{
	UInt16 sz = sizeof(gPrefs);
	if (gPrefsLoaded) return;
	if (PrefGetAppPreferences(appCreator, bwPrefID, &gPrefs, &sz, false) != noPreferenceFound
		&& sz == sizeof(gPrefs)) {
		/* loaded fine */
	} else {
		MemSet(&gPrefs, sizeof(gPrefs), 0);
	}
	gPrefsLoaded = true;
}

static void SavePrefs(void)
{
	PrefSetAppPreferences(appCreator, bwPrefID, 1, &gPrefs, sizeof(gPrefs), false);
}

/* ------------------------------------------------------------------ *
 *  session key cache - own Feature Manager slot, own TTL, completely
 *  independent of Sec35's EncCacheKey/EncGetKey (different creator+ftr id)
 * ------------------------------------------------------------------ */
static void BwWipeKeyInternal(void)
{
	UInt32 v;
	if (FtrGet(appCreator, bwKeyFtr, &v) == 0 && v != 0) {
		BwKeyBlock zero;
		MemSet(&zero, sizeof(zero), 0);
		DmWrite((void *) v, 0, &zero, sizeof(zero));
	}
	FtrPtrFree(appCreator, bwKeyFtr);
}

static Err BwCacheKeyInternal(const UInt8 key[ENC_KEY_BYTES])
{
	BwKeyBlock blk;
	Err e;
	void *p;

	BwWipeKeyInternal();
	MemMove(blk.key, key, ENC_KEY_BYTES);
	blk.unlockTime = TimGetSeconds();

	e = FtrPtrNew(appCreator, bwKeyFtr, sizeof(BwKeyBlock), &p);
	if (e || !p) { MemSet(&blk, sizeof(blk), 0); return e ? e : dmErrMemError; }

	DmWrite(p, 0, &blk, sizeof(blk));
	MemSet(&blk, sizeof(blk), 0);
	return 0;
}

static Boolean BwGetKeyInternal(UInt8 keyOut[ENC_KEY_BYTES], UInt32 ttlSeconds)
{
	UInt32 v;
	BwKeyBlock *p;

	if (FtrGet(appCreator, bwKeyFtr, &v) != 0 || v == 0) return false;
	p = (BwKeyBlock *) v;
	if (TimGetSeconds() < p->unlockTime || TimGetSeconds() - p->unlockTime > ttlSeconds)
		{ BwWipeKeyInternal(); return false; }
	MemMove(keyOut, p->key, ENC_KEY_BYTES);
	return true;
}

Boolean BwIsUnlocked(void)
{
	UInt8 dummy[ENC_KEY_BYTES];
	return BwGetKeyInternal(dummy, BwGetTtlSeconds());
}

void BwLock(void)
{
	/* Real-device report (2026-09): "Lock Now" showed the masked list, but
	 * tapping any record instantly revealed it - MainEnsureLocalUnlock
	 * (MainForm.c) checks the session-cached master password FIRST and, if
	 * present, silently re-derives the key with no prompt at all. That
	 * cache is deliberately NOT wiped on sleep/app-exit (BitWarden.c's own
	 * comment - a passive-expiry design, governed only by its own TTL) -
	 * but "Lock Now" is a different thing entirely: an explicit, deliberate
	 * user request to re-secure the vault right now, not a passive timeout.
	 * Only called from two places (MainForm.c), both already explicit user
	 * actions (this menu item, and Delete Account, which already wipes the
	 * cache itself beforehand) - safe to wipe it here unconditionally
	 * without touching the passive sleep/TTL design at all. */
	BwWipeCachedPassword();
	BwWipeKeyInternal();
	BwStoreInvalidateNameCache();	/* decrypted plaintext titles - wipe on lock, see its own comment */
}

UInt32 BwGetTtlSeconds(void)
{
	LoadPrefs();
	return gPrefs.ttlSeconds ? gPrefs.ttlSeconds : 300;	/* default 5 min, same order as Sec35's own defaults */
}

void BwSetTtlSeconds(UInt32 seconds)
{
	LoadPrefs();
	gPrefs.ttlSeconds = seconds;
	SavePrefs();
}

UInt8 BwGetTwoFactorEntryMode(void)
{
	LoadPrefs();
	return gPrefs.twoFactorEntryMode;	/* 0 = bwTwoFactorEntryText, the default */
}

void BwSetTwoFactorEntryMode(UInt8 mode)
{
	LoadPrefs();
	gPrefs.twoFactorEntryMode = mode;
	SavePrefs();
}

Boolean BwHasVault(void)
{
	LoadPrefs();
	return gPrefs.saltSet != 0;
}

/* Forces the next BwUnlock() call to treat this as a first-ever unlock,
 * establishing a fresh local salt/rounds/check-value from whatever
 * password is given then. Used only when a sync's own WiRSa-side auth has
 * ALREADY proven the entered password is the real, current Bitwarden
 * master password, but the LOCAL vault's own salt was established from a
 * since-changed password (BwUnlock's normal check-value comparison would
 * otherwise just report "wrong password" forever, even though it's now
 * correct) - Sync.c's own fallback path, not called from anywhere else. */
void BwResetVaultForNewPassword(void)
{
	LoadPrefs();
	gPrefs.saltSet = 0;
	SavePrefs();
}

/* BitWarden-local salt RNG - scoped to this file on purpose. enccore.c's own
 * EncRandomBytes (a plain LCG seeded from tick count / wall clock / one
 * SysRandom() draw) is shared by Sec35 and every other suite app; changing
 * it in place would change their salts too. This is used ONLY for
 * gPrefs.salt below, so Sec35 and the rest of the suite keep their existing
 * behavior untouched. A minimal AES-CTR-DRBG-style construction (the same
 * idea as NIST SP 800-90A's CTR_DRBG: an AES key doubling as internal state,
 * used to generate output blocks and then re-seeded from that same output) -
 * built from EncCrypt (the existing public AES-CTR primitive) rather than
 * enccore.c's own private AbsorbBlock, since that's file-static there.
 * Entropy taps: many independent SysRandom()/TimGetTicks() samples spread
 * across a short real loop (each iteration's tick count carries genuine,
 * if modest, instruction-timing jitter - not just one fixed snapshot),
 * TimGetSeconds(), and this device's own ROM-serial-derived device secret
 * (EncDeviceSecret) - the same real per-device value EncDeriveKey itself
 * relies on, mixed in here too so two devices with similar startup timing
 * still can't land on related salts. */
static void BwRandomBytes(UInt8 *buf, UInt16 len) SEC001;

static void BwRandomBytes(UInt8 *buf, UInt16 len)
{
	UInt8 state[16];
	UInt8 dev[32];
	UInt16 devLen, i, produced;
	UInt32 tickAccum = TimGetTicks();
	UInt32 secs;

	MemSet(state, 16, 0);
	for (i = 0; i < 64; i++) {
		UInt16 r = (UInt16) SysRandom(0);
		tickAccum = tickAccum * 2654435761UL + TimGetTicks();
		state[i & 15] = (UInt8) (state[i & 15] ^ (UInt8) (tickAccum >> (i & 7)));
		state[(i + 7) & 15] = (UInt8) (state[(i + 7) & 15] ^ (UInt8) (r >> ((i & 1) * 8)));
	}
	secs = TimGetSeconds();
	state[0] = (UInt8) (state[0] ^ (UInt8) (secs >> 24));
	state[1] = (UInt8) (state[1] ^ (UInt8) (secs >> 16));
	state[2] = (UInt8) (state[2] ^ (UInt8) (secs >> 8));
	state[3] = (UInt8) (state[3] ^ (UInt8) secs);

	devLen = EncDeviceSecret(dev, sizeof(dev));
	for (i = 0; i < devLen; i++) state[i & 15] = (UInt8) (state[i & 15] ^ dev[i]);
	MemSet(dev, sizeof(dev), 0);

	produced = 0;
	while (produced < len) {
		UInt8 block[16];
		UInt16 n = (UInt16) ((len - produced >= 16) ? 16 : (len - produced));
		MemSet(block, 16, 0);
		EncCrypt(state, block, 16, 0, 0);	/* AES-CTR keystream under key=state, from a zero buffer */
		MemMove(buf + produced, block, n);
		for (i = 0; i < 16; i++) state[i] = (UInt8) (state[i] ^ block[i]);	/* re-seed from own output */
		state[0] = (UInt8) (state[0] ^ (UInt8) TimGetTicks());
		produced = (UInt16) (produced + n);
	}
	MemSet(state, 16, 0);
}

Boolean BwUnlock(const char *password)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt16 rounds;

	LoadPrefs();

	if (!gPrefs.saltSet) {
		/* First unlock ever (right before the very first sync): establish a
		 * fresh salt/rounds/check value from this password. 2000ms (not
		 * Sec35's own 800ms) - this gPrefs blob is saved=false (never
		 * HotSync-backed, see SavePrefs' own comment), and EncDeriveKey
		 * already folds in this device's own ROM-serial-derived secret
		 * (EncDeviceSecret), so an attacker needs physical access to THIS
		 * device to even attempt an offline guess at all - the longer
		 * stretch raises the cost of that specific attack (device theft),
		 * not a stolen-backup-alone one, which the device secret already
		 * blocks on its own. 2s is within the accepted UX cost for the
		 * (infrequent) unlock prompts this affects. Every caller shows a
		 * "Working..." indicator around this call - see
		 * MainEnsureLocalUnlock (MainForm.c) and BwSyncRun (Sync.c). Also
		 * used by the EXISTING-vault branch below (same EncDeriveKey cost,
		 * every regular unlock, not just this one-time setup path). */
		BwRandomBytes(gPrefs.salt, ENC_SALT_BYTES);
		gPrefs.rounds = EncCalibrateRounds(2000);
		EncDeriveKey(password, gPrefs.salt, gPrefs.rounds, key);
		MemMove(gPrefs.check, kCheckMagic, sizeof(kCheckMagic));
		EncCrypt(key, gPrefs.check, sizeof(gPrefs.check), bwCheckNonceHi, 0);
		gPrefs.saltSet = 1;
		SavePrefs();
		BwCacheKeyInternal(key);
		MemSet(key, sizeof(key), 0);
		return true;
	}

	rounds = gPrefs.rounds;
	EncDeriveKey(password, gPrefs.salt, rounds, key);
	{
		UInt8 check[8];
		MemMove(check, gPrefs.check, sizeof(check));
		EncCrypt(key, check, sizeof(check), bwCheckNonceHi, 0);
		if (MemCmp(check, kCheckMagic, sizeof(kCheckMagic)) != 0) {
			MemSet(key, sizeof(key), 0);
			return false;	/* wrong master password */
		}
	}
	BwCacheKeyInternal(key);
	MemSet(key, sizeof(key), 0);
	return true;
}

/* Renews the cached local-vault key's own TTL clock (a fresh unlockTime)
 * WITHOUT paying BwUnlock's real AES-stretch cost - re-caches whatever key
 * is already valid, unchanged. For BwSyncRun's own periodic "keep both
 * encryption layers alive for the rest of a long sync" touch (Sync.c): a
 * sync that skips the now-expensive BwUnlock re-verify when it's provably
 * redundant (see BwSyncRun's own comment) would otherwise let this key's
 * TTL run out mid-sync exactly as easily as before that skip existed -
 * every BwStoreAddItem call after that point would then silently no-op
 * with no local key cached, while the Palm keeps acking items to WiRSa as
 * if they'd been saved. No-op (returns false) if the key isn't currently
 * valid - nothing to renew; matches BwGetKeyInternal's own "expired ->
 * wipe" behavior rather than resurrecting an already-dead key. */
Boolean BwTouchLocalUnlock(void)
{
	UInt8 key[ENC_KEY_BYTES];
	if (!BwGetKeyInternal(key, BwGetTtlSeconds())) return false;
	BwCacheKeyInternal(key);
	MemSet(key, sizeof(key), 0);
	return true;
}

/* ------------------------------------------------------------------ *
 *  session-cached master password - shared by MainOpen's own local-unlock
 *  prompt and BwSyncRun's sync-triggered prompt, so once either one
 *  succeeds neither re-prompts again until BwGetTtlSeconds() (this app's
 *  own "Lock After" preference) actually elapses.
 *
 *  Real-device feedback (2026-08): "we do not erase the Master Password
 *  memory cache on power off or app exit any more ... honor the Lock
 *  After option strictly ... protected by the Sec35 password. If Sec35 is
 *  locked, it should auto prompt for the sec35 password by bouncing to
 *  sec35, and then bouncing back" - then, once tried via prefs: "can the
 *  cached master password stay in RAM-only storage instead of writing out
 *  to prefs but still be sec35 protected?" So: RAM-only (a Feature
 *  Manager FtrPtrNew block, exactly BwCacheKeyInternal's own mechanism
 *  above for the local-vault key - NOT gPrefs/PrefSetAppPreferences,
 *  which actually writes to persistent storage) - Feature Manager memory
 *  is the one place on this OS that's both RAM-only AND survives an app
 *  closing/relaunching (wiped only by a device reset, unlike a plain
 *  static array which resets with the app's own memory on every launch).
 *  Still encrypted under the CURRENT Sec35 key (same as
 *  twoFactorRememberEnc's own persisted version), so it's silently
 *  undecryptable the instant Sec35 itself locks, regardless of this
 *  cache's own TTL. BitWarden.c no longer wipes it on sleep/exit at all -
 *  its own TTL (checked here, against BwGetTtlSeconds(), NOT Sec35's
 *  separate TTL) is the only expiry mechanism, matching "Lock After"
 *  applying to this cache exactly the same way it already governs the
 *  local-vault key itself (BwGetKeyInternal). BwHasCachedPassword lets a
 *  caller distinguish "no usable cache, prompt for the password" from "a
 *  cache exists and hasn't expired, but Sec35 is what's blocking it right
 *  now" - the latter is exactly when MainEnsureLocalUnlock/BwSyncRun
 *  should bounce to Sec35 instead of immediately falling back to a full
 *  master-password prompt. */
#define bwCachedPwNonceHi	0xFFFFFFFCUL	/* next in the bwCheckNonceHi/bwFolderNonceHi/bwOrgNonceHi reserved range */

typedef struct { UInt8 enc[bwCachedPwMax]; UInt16 len; UInt32 time; } BwCachedPwBlock;

void BwWipeCachedPassword(void)
{
	UInt32 v;
	if (FtrGet(appCreator, bwCachedPwFtr, &v) == 0 && v != 0) {
		BwCachedPwBlock zero;
		MemSet(&zero, sizeof(zero), 0);
		DmWrite((void *) v, 0, &zero, sizeof(zero));
	}
	FtrPtrFree(appCreator, bwCachedPwFtr);
}

Boolean BwHasCachedPassword(void)
{
	UInt32 v;
	BwCachedPwBlock *p;
	UInt32 now;

	if (FtrGet(appCreator, bwCachedPwFtr, &v) != 0 || v == 0) return false;
	p = (BwCachedPwBlock *) v;
	if (p->len == 0) return false;
	now = TimGetSeconds();
	if (now < p->time || (now - p->time) > BwGetTtlSeconds()) {
		BwWipeCachedPassword();	/* TTL backstop - see this section's own comment */
		return false;
	}
	return true;
}

Boolean BwGetCachedPassword(char *outPassword, UInt16 outLen)
{
	UInt32 v;
	BwCachedPwBlock *p;
	UInt8 sec35Key[ENC_KEY_BYTES];
	UInt8 tmp[bwCachedPwMax];

	if (!BwHasCachedPassword()) return false;
	if (FtrGet(appCreator, bwCachedPwFtr, &v) != 0 || v == 0) return false;	/* can't happen - BwHasCachedPassword just confirmed it, defensive */
	p = (BwCachedPwBlock *) v;
	if (p->len >= outLen) return false;
	if (!EncGetKey(sec35Key, EncGetTtlSeconds())) return false;	/* Sec35 locked - see BwHasCachedPassword for the "should we bounce" check */

	MemMove(tmp, p->enc, p->len);
	EncCrypt(sec35Key, tmp, p->len, bwCachedPwNonceHi, 0);
	MemSet(sec35Key, sizeof(sec35Key), 0);

	MemMove(outPassword, tmp, p->len);
	outPassword[p->len] = 0;
	MemSet(tmp, sizeof(tmp), 0);
	return true;
}

void BwSetCachedPassword(const char *password)
{
	UInt32 v;
	UInt8 sec35Key[ENC_KEY_BYTES];
	UInt16 len = (UInt16) StrLen(password);
	Err e;
	BwCachedPwBlock blk;

	if (len >= bwCachedPwMax) len = bwCachedPwMax - 1;
	if (!EncGetKey(sec35Key, EncGetTtlSeconds())) return;	/* shouldn't happen - defensive */

	BwWipeCachedPassword();	/* replace any existing block cleanly - same reasoning as BwCacheKeyInternal's own call to BwWipeKeyInternal */
	e = FtrPtrNew(appCreator, bwCachedPwFtr, sizeof(BwCachedPwBlock), (void **) &v);
	if (e || !v) { MemSet(sec35Key, sizeof(sec35Key), 0); return; }

	MemSet(&blk, sizeof(blk), 0);
	MemMove(blk.enc, password, len);
	EncCrypt(sec35Key, blk.enc, len, bwCachedPwNonceHi, 0);
	MemSet(sec35Key, sizeof(sec35Key), 0);
	blk.len = len;
	blk.time = TimGetSeconds();
	DmWrite((void *) v, 0, &blk, sizeof(blk));
	MemSet(&blk, sizeof(blk), 0);
}

/* Renews this cache's own TTL clock without touching enc/len - same "cheap
 * TTL renewal, no re-derive" shape as BwTouchLocalUnlock. Rewrites the
 * WHOLE block (not just the time field) to avoid depending on this
 * compiler's struct layout/padding for a partial DmWrite offset - this
 * struct is tiny, so the extra few bytes written cost nothing. No-op if
 * there's nothing valid to renew (mirrors BwHasCachedPassword's own TTL
 * backstop - never resurrects an already-expired/absent cache). */
static void BwTouchCachedPassword(void)
{
	UInt32 v;
	BwCachedPwBlock *p;
	BwCachedPwBlock local;
	if (!BwHasCachedPassword()) return;
	if (FtrGet(appCreator, bwCachedPwFtr, &v) != 0 || v == 0) return;
	p = (BwCachedPwBlock *) v;
	MemMove(&local, p, sizeof(local));
	local.time = TimGetSeconds();
	DmWrite((void *) v, 0, &local, sizeof(local));
	MemSet(&local, sizeof(local), 0);
}

/* Keeps every session-scoped TTL alive (BitWarden's own local-vault key,
 * Sec35's shared key, and this cached master password) for as long as a
 * sync is actually running - not just at the moments a frame happens to
 * arrive. Real-device root cause (2026-09): a real vault sync can stall for
 * a while with NO frame arriving at all (a slow /api/sync fetch on WiRSa's
 * side, now retried up to 3x45s internally - see bitwarden_relay.cpp's own
 * retry-with-resume comment) - the OLD per-frame touch (called only after
 * BwSerialRecvFrame succeeded) never ran during that silence, so a stall
 * comfortably longer than a short "Lock After" setting let one or more of
 * these TTLs lapse mid-sync with nothing to notice or recover: every
 * BwStoreAddItem call after that point silently drops the item (see its own
 * `!BwGetKeyInternal(...) return` gate) while WiRSa still believes (and
 * reports) every item was sent+acked - real, hard-to-notice data loss,
 * exactly the "sync completed per WiRSa's own log but the Palm's list never
 * grew" report this fixes. Called both from Sync.c's own per-frame/post-2FA
 * touch points AND from Serial.c's PollKeepAlive (runs roughly once a
 * second during ANY blocking wait for the next frame, including a stall) -
 * self-throttled here so calling it from multiple places / every ~1s costs
 * nothing extra. */
#define bwSessionTouchTicks (SysTicksPerSecond() * 20)
static UInt32 gLastSessionTouchTick = 0;

void BwTouchSessionKeys(void)
{
	UInt32 now = TimGetTicks();
	UInt8 sec35Key[ENC_KEY_BYTES];
	if (now - gLastSessionTouchTick < (UInt32) bwSessionTouchTicks) return;
	gLastSessionTouchTick = now;
	BwTouchLocalUnlock();
	BwTouchCachedPassword();
	if (EncGetKey(sec35Key, EncGetTtlSeconds())) {
		EncCacheKey(sec35Key);
		MemSet(sec35Key, sizeof(sec35Key), 0);
	}
}

/* ------------------------------------------------------------------ *
 *  persisted "remember this device for 2FA" token (PROTOCOL.md's SYNC/
 *  SYNC_DONE) - lets a future sync skip Bitwarden's own interactive 2FA
 *  challenge, matching every real Bitwarden client's own behavior. Unlike
 *  gCachedPwEnc above (RAM-only, session-scoped, TTL-backstopped), this
 *  needs to survive app restarts - the whole point is skipping 2FA across
 *  DIFFERENT sync sessions, not just within one running app instance - so
 *  it's persisted via the same PrefGetAppPreferences/PrefSetAppPreferences
 *  mechanism as the rest of BwPrefsBlock, just with its own value encrypted
 *  first (credential-adjacent - unlike the salt/rounds/check-value fields,
 *  which are safe to read because they don't grant access to anything by
 *  themselves). Deliberately uses ONLY the Sec35 key (EncGetKey), not this
 *  app's own local-vault key: the local-vault key requires BwUnlock() to
 *  have already run, which happens AFTER the SYNC frame (containing this
 *  token) is already built in Sync.c - Sec35's own key has no such
 *  ordering dependency and this whole app is Sec35-mandatory anyway. */
/* bwTwoFactorRememberMax now lives in BitWarden.h (shared with Sync.c). */
#define bwTwoFactorNonceHi		0xFFFFFFFBUL	/* next in the bwCheckNonceHi/.../bwCachedPwNonceHi reserved range */

Boolean BwGetTwoFactorRemember(char *out, UInt16 outCap)
{
	UInt8 sec35Key[ENC_KEY_BYTES];
	UInt8 tmp[bwTwoFactorRememberMax];

	LoadPrefs();
	out[0] = 0;
	if (gPrefs.twoFactorRememberLen == 0 || gPrefs.twoFactorRememberLen >= outCap
		|| gPrefs.twoFactorRememberLen > bwTwoFactorRememberMax) return false;
	if (!EncGetKey(sec35Key, EncGetTtlSeconds())) return false;	/* Sec35 locked - can't decrypt right now */

	MemMove(tmp, gPrefs.twoFactorRememberEnc, gPrefs.twoFactorRememberLen);
	EncCrypt(sec35Key, tmp, gPrefs.twoFactorRememberLen, bwTwoFactorNonceHi, 0);
	MemSet(sec35Key, sizeof(sec35Key), 0);

	MemMove(out, tmp, gPrefs.twoFactorRememberLen);
	out[gPrefs.twoFactorRememberLen] = 0;
	MemSet(tmp, sizeof(tmp), 0);
	return true;
}

void BwSetTwoFactorRemember(const char *token)
{
	UInt8 sec35Key[ENC_KEY_BYTES];
	UInt16 len = (UInt16) StrLen(token);

	LoadPrefs();
	if (len == 0) {
		gPrefs.twoFactorRememberLen = 0;
		MemSet(gPrefs.twoFactorRememberEnc, sizeof(gPrefs.twoFactorRememberEnc), 0);
		SavePrefs();
		return;
	}
	if (len > bwTwoFactorRememberMax) len = bwTwoFactorRememberMax;
	if (!EncGetKey(sec35Key, EncGetTtlSeconds())) return;	/* shouldn't happen mid-sync - defensive */

	MemMove(gPrefs.twoFactorRememberEnc, token, len);
	EncCrypt(sec35Key, gPrefs.twoFactorRememberEnc, len, bwTwoFactorNonceHi, 0);
	MemSet(sec35Key, sizeof(sec35Key), 0);
	gPrefs.twoFactorRememberLen = len;
	SavePrefs();
}


/* ------------------------------------------------------------------ *
 *  server URL / TOFU pin
 * ------------------------------------------------------------------ */
void BwGetServerURL(char *out, UInt16 outLen)
{
	LoadPrefs();
	StrNCopy(out, gPrefs.serverURL, outLen - 1);
	out[outLen - 1] = 0;
}

void BwSetServerURL(const char *url)
{
	LoadPrefs();
	StrNCopy(gPrefs.serverURL, url, sizeof(gPrefs.serverURL) - 1);
	gPrefs.serverURL[sizeof(gPrefs.serverURL) - 1] = 0;
	SavePrefs();
}

void BwGetEmail(char *out, UInt16 outLen)
{
	LoadPrefs();
	StrNCopy(out, gPrefs.email, outLen - 1);
	out[outLen - 1] = 0;
}

void BwSetEmail(const char *email)
{
	LoadPrefs();
	StrNCopy(gPrefs.email, email, sizeof(gPrefs.email) - 1);
	gPrefs.email[sizeof(gPrefs.email) - 1] = 0;
	SavePrefs();
}

Boolean BwHasPinnedFingerprint(void)
{
	LoadPrefs();
	return gPrefs.hasFingerprint != 0;
}

void BwGetPinnedFingerprint(UInt8 out[32])
{
	LoadPrefs();
	MemMove(out, gPrefs.fingerprint, 32);
}

void BwSetPinnedFingerprint(const UInt8 fp[32])
{
	LoadPrefs();
	MemMove(gPrefs.fingerprint, fp, 32);
	gPrefs.hasFingerprint = 1;
	SavePrefs();
}

void BwClearPinnedFingerprint(void)
{
	LoadPrefs();
	gPrefs.hasFingerprint = 0;
	MemSet(gPrefs.fingerprint, 32, 0);
	SavePrefs();
}

/* ------------------------------------------------------------------ *
 *  database open/create (same DmFindDatabase/DmCreateDatabase idiom as
 *  AddressDB35.c's own A35GetDatabase). Deliberately does NOT set
 *  dmHdrAttrBackup - PalmOS record databases default to NOT being backed
 *  up by HotSync unless an app explicitly opts in by setting that bit, so
 *  simply never setting it keeps BitWardenDB out of every HotSync backup
 *  (security review, 2026-09: the local vault is a pure cache of the real
 *  Vaultwarden data anyway - BwStoreBeginSync already does a full wipe and
 *  re-fetch on every sync, so there's nothing to actually lose by not
 *  backing it up locally, only exposure to remove). This DOES mean a
 *  restored-from-backup device starts with an empty vault until the next
 *  Sync Now - expected and harmless, matching how a hard reset already
 *  behaves today. gPrefs (salt/rounds/check/serverURL/email/2FA token) was
 *  already excluded from backup via saved=false in SavePrefs, unrelated to
 *  this flag - see its own comment. */
void BwStoreInit(void)
{
	LocalID dbID = DmFindDatabase(0, bwDBName);
	if (dbID != 0) {
		gDB = DmOpenDatabase(0, dbID, dmModeReadWrite);
	} else {
		if (DmCreateDatabase(0, bwDBName, appCreator, bwDBType, false) != errNone) return;
		dbID = DmFindDatabase(0, bwDBName);
		gDB = DmOpenDatabase(0, dbID, dmModeReadWrite);
	}

	if (!gDB) return;
	LoadPrefs();
	if (gPrefs.formatVersion != bwStoreFormatVersion) {
		BwStoreBeginSync();	/* one-time full wipe - see bwStoreFormatVersion's comment */
		gPrefs.formatVersion = bwStoreFormatVersion;
		SavePrefs();
	}
}

void BwStoreClose(void)
{
	if (gDB) { DmCloseDatabase(gDB); gDB = NULL; }
}

/* See BwStoreCompactIfDue's own comment (below BwStoreBeginSync) for why
 * this state exists - declared here since BwStoreBeginSync needs to reset
 * it and C requires file-scope statics declared before use. */
/* Was 20 - real-device feedback (2026-08) reported "Palm lost mid-sync"
 * recurring even with the periodic-compaction mitigation already in
 * place, on a 740-byte item. Halved to keep the storage heap defragmented
 * more tightly (smaller, more frequent compactions instead of letting
 * more fragmentation build up between them) - doesn't eliminate DmNewRecord's
 * own unpredictable internal compaction risk (nothing short of avoiding
 * large writes entirely could), but narrows the window. Sync.c's own call
 * site was also moved to run BEFORE BwStoreAddItem instead of after - see
 * its own comment. */
#define bwCompactEveryItems	10
static UInt16 gStorageHeapID = 0xFFFF; /* 0xFFFF = not yet determined */
static UInt16 gItemsSinceCompact = 0;

/* ------------------------------------------------------------------ *
 *  sync write path - always a full wipe+rewrite (v1 has no incremental
 *  merge/edit), matching the plan's "read-only, always a fresh sync"
 *  simplicity. Folder names are encrypted too, same as every other vault
 *  field (the user's own requirement: only the right master password
 *  should reveal anything at rest, including folder names) - each folder
 *  gets its own reserved nonce range (bwFolderNonceHi, nonceLo=folder
 *  index) distinct from any record's own (uid, fieldIndex) nonces.
 * ------------------------------------------------------------------ */
/* Real-device root-cause fix (2026-08): this used to wipe the vault by
 * calling DmRemoveRecord() once per existing record (~780 on the test
 * account). That's exactly what produced the pathological storage-heap
 * fragmentation the periodic-compaction mitigations below (bwCompactEveryItems/
 * BwStoreCompactIfDue) were fighting - freeing several hundred variously-
 * sized record chunks one at a time leaves the heap a patchwork of gaps,
 * and the FIRST several DmNewRecord calls of the add phase that follows
 * are exactly the ones most likely to need a large internal compaction to
 * find room, before the periodic compact (every 10th item) ever gets a
 * chance to run. Real-device testing confirmed a single DmNewRecord call
 * can stall past even this device's maximum 3-minute auto-off setting in
 * that state - no per-item keep-awake call can help once the call that
 * needs protecting is itself longer than the whole auto-off window.
 * Deleting and recreating the whole database instead avoids ever creating
 * that fragmentation pattern in the first place: the OS releases the
 * entire prior allocation as one operation, and the fresh empty database
 * gives the add phase a genuinely clean heap to append into. This doesn't
 * replace BwStoreCompactIfDue's own periodic compaction during the add
 * phase (ordinary fragmentation from interleaved network/crypto
 * allocations over hundreds of items is still possible) - it targets the
 * specific worst case this function itself used to cause. Returns false
 * (see BitWarden.h's own comment) if the database couldn't be recreated -
 * the caller MUST treat that as a fatal sync error, not proceed. */
Boolean BwStoreBeginSync(void)
{
	LocalID dbID;
	if (!gDB) return false;

	BwKeepDeviceAwake();
	BwTouchActivity();

	DmCloseDatabase(gDB);
	gDB = NULL;

	/* Clean up any leftover temp DB from a previous cancelled/crashed sync
	 * (see this file's own bwDBNameTemp comment) before starting fresh -
	 * DmCreateDatabase would otherwise just fail if one is already there. */
	dbID = DmFindDatabase(0, bwDBNameTemp);
	if (dbID != 0) DmDeleteDatabase(0, dbID);

	if (DmCreateDatabase(0, bwDBNameTemp, appCreator, bwDBType, false) == errNone) {
		dbID = DmFindDatabase(0, bwDBNameTemp);
		gDB = DmOpenDatabase(0, dbID, dmModeReadWrite);
	}

	BwKeepDeviceAwake();
	BwTouchActivity();

	if (!gDB) {
		/* Couldn't even create the temp DB (e.g. card full) - reopen the
		 * REAL one so the app isn't left with no database at all; the
		 * caller still treats this false return as a fatal sync error. */
		dbID = DmFindDatabase(0, bwDBName);
		if (dbID != 0) gDB = DmOpenDatabase(0, dbID, dmModeReadWrite);
		return false;
	}

	gStorageHeapID = 0xFFFF;
	gItemsSinceCompact = 0;
	BwStoreInvalidateNameCache();	/* content is about to change - see its own comment */
	return true;
}

/* Called only once a sync has FULLY succeeded (BW_SYNC_DONE) - replaces the
 * real database with the temp one just streamed into. See bwDBNameTemp's
 * own comment for why this is a separate step from BwStoreBeginSync rather
 * than wiping the real database up front. */
void BwStoreEndSync(void)
{
	LocalID oldID, tempID;
	if (gDB) { DmCloseDatabase(gDB); gDB = NULL; }
	oldID = DmFindDatabase(0, bwDBName);
	if (oldID != 0) DmDeleteDatabase(0, oldID);
	tempID = DmFindDatabase(0, bwDBNameTemp);
	if (tempID != 0) {
		/* Rename only - every other field left untouched (NULL). */
		DmSetDatabaseInfo(0, tempID, (Char *) bwDBName, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	}
	gDB = DmOpenDatabase(0, DmFindDatabase(0, bwDBName), dmModeReadWrite);
	BwStoreInvalidateNameCache();	/* the real DB's own content just changed */
}

/* Called on ANY sync failure/cancellation once BwStoreBeginSync has already
 * succeeded (a cancelled sync, a lost connection, a malformed frame, etc.) -
 * discards the (possibly partial) temp database and reopens the real one,
 * completely untouched by this attempt. Safe to call even if BeginSync's
 * own temp-DB creation itself failed (DmFindDatabase/DmDeleteDatabase are
 * no-ops if the temp DB was never created). */
void BwStoreAbortSync(void)
{
	LocalID tempID;
	if (gDB) { DmCloseDatabase(gDB); gDB = NULL; }
	tempID = DmFindDatabase(0, bwDBNameTemp);
	if (tempID != 0) DmDeleteDatabase(0, tempID);
	gDB = DmOpenDatabase(0, DmFindDatabase(0, bwDBName), dmModeReadWrite);
	/* A list redraw while the temp DB was open (the progress dialog closing
	 * before this call) can have built the name/header caches from it. */
	BwStoreInvalidateNameCache();
}

/* Real-device testing (2026-08) ruled out every reset-the-auto-off-timer
 * approach for BwStoreAddItem's occasional very slow DmNewRecord (storage-
 * heap compaction is the leading theory) - confirmed even the DEVICE'S OWN
 * maximum auto-off setting (3 minutes) wasn't enough margin, and no amount
 * of resetting a countdown BEFORE a single uninterruptible ROM call can
 * help once that call's own worst case exceeds the auto-off ceiling
 * itself. This suite has no other precedent for this exact problem (no
 * app has hit a single call this slow before) - proactively defragmenting
 * the storage heap periodically, rather than only reacting to whatever
 * DmNewRecord decides to do on its own unpredictable schedule, is the only
 * remaining lever: keep each individual DmNewRecord call fast by not
 * letting fragmentation build up in the first place, instead of trying to
 * out-wait an occasional multi-minute stall after it's already started.
 * Called once every bwCompactEveryItems records - frequent enough to catch
 * fragmentation before it compounds, infrequent enough that MemHeapCompact
 * itself (also a real, if hopefully much smaller, stall) doesn't dominate.
 */
void BwStoreCompactIfDue(void)
{
	if (!gDB) return;
	gItemsSinceCompact++;
	if (gItemsSinceCompact < bwCompactEveryItems) return;
	gItemsSinceCompact = 0;

	if (gStorageHeapID == 0xFFFF) {
		/* Determine the real heap ID from an existing record's own handle -
		 * more reliable than guessing a card/heap-index combination, which
		 * can vary across ROM versions/devices. DmQueryRecord is the
		 * lightweight, no-lock-count variant - nothing to release. */
		MemHandle h = DmQueryRecord(gDB, 0);
		if (!h) return; /* no records yet (e.g. very first item) - try again next time */
		gStorageHeapID = MemHandleHeapID(h);
	}

	BwKeepDeviceAwake();
	BwTouchActivity();
	MemHeapCompact(gStorageHeapID);
	BwKeepDeviceAwake();
	BwTouchActivity();
}

/* Shared by BwStoreAddFolder/BwStoreAddOrg - both store their name as a
 * tiny encrypted marker record (markerType 0=folder, bwOrgMarkerType=org),
 * with the marker's own sequence index reused in the folderIndex header
 * slot (orgIndex slot left at bwOrgNone) so BwStoreGetFolderName/
 * BwStoreGetOrgName can find it by a linear scan (folder/org counts are
 * always small - tens at most). */
/* Returns false if this marker was NOT actually stored (either key
 * unavailable - see this function's own real-device note below - or
 * DmNewRecord itself failed) - see BwStoreAddFolder/BwStoreAddOrg's own
 * comment on why the caller (Sync.c) MUST treat that as fatal now, not
 * silently keep going. */
static Boolean AddMarker(UInt8 markerType, UInt16 index, const char *name, UInt32 nonceHi)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt8 sec35Key[ENC_KEY_BYTES];
	UInt16 nameLen = (UInt16) StrLen(name);
	UInt8 *p = gRecBuf;
	UInt16 idx;
	MemHandle h;

	if (!gDB || !BwGetKeyInternal(key, BwGetTtlSeconds())) return false;
	/* Sec35 outer wrap - see Store.c's top-of-file note. Both keys must be
	 * available to write at all. Real-device report (2026-09): Sec35
	 * locking mid-sync used to mean every write from this point on just
	 * silently no-op'd HERE while the caller kept acking items to WiRSa as
	 * if they'd been saved - the sync "succeeded" (SYNC_DONE, itemCount
	 * matching what WiRSa believes it sent) while actually storing nothing
	 * from the lock point onward. Now reported as a failure so the caller
	 * can abort the whole sync (BwStoreAbortSync) instead. */
	if (!EncGetKey(sec35Key, EncGetTtlSeconds())) { MemSet(key, sizeof(key), 0); return false; }
	if (nameLen > bwMaxField - 1) nameLen = bwMaxField - 1;

	MemSet(gRecBuf, sizeof(gRecBuf), 0);
	p[0] = markerType;
	wrU16(p + 1, index);
	wrU16(p + 3, bwOrgNone);	/* orgIndex slot unused on marker records */
	/* guid left blank for marker records */
	wrU16(p + bwHdrFixedSize - bwFieldCount * 2, nameLen);	/* fieldLen[0] = name length */
	MemMove(p + bwHdrFixedSize, name, nameLen);

	h = DmNewRecord(gDB, &idx, bwHdrFixedSize + nameLen);
	if (h != NULL) {
		UInt32 uid = 0;
		void *rp = MemHandleLock(h);
		DmWrite(rp, 0, gRecBuf, bwHdrFixedSize + nameLen);
		MemHandleUnlock(h);
		DmReleaseRecord(gDB, idx, true);

		DmRecordInfo(gDB, idx, NULL, &uid, NULL);
		h = DmGetRecord(gDB, idx);
		if (h != NULL) {
			rp = MemHandleLock(h);
			{
				UInt8 tmp[bwMaxField];
				MemMove(tmp, (UInt8 *) rp + bwHdrFixedSize, nameLen);
				EncCrypt(key, tmp, nameLen, nonceHi, index);
				/* Sec35 outer wrap - same nonce pair reused across both
				 * layers is fine (CTR keystreams are independent per-key;
				 * nonce reuse only matters within the same key). */
				EncCrypt(sec35Key, tmp, nameLen, nonceHi, index);
				DmWrite(rp, bwHdrFixedSize, tmp, nameLen);
			}
			MemHandleUnlock(h);
			DmReleaseRecord(gDB, idx, true);
		}
	}
	MemSet(key, sizeof(key), 0);
	MemSet(sec35Key, sizeof(sec35Key), 0);
	return h != NULL;
}

Boolean BwStoreAddFolder(UInt16 index, const char *name)
{
	return AddMarker(0, index, name, bwFolderNonceHi);
}

Boolean BwStoreAddOrg(UInt16 index, const char *name)
{
	return AddMarker(bwOrgMarkerType, index, name, bwOrgNonceHi);
}

/* Shared "not this record's type" placeholder - item->u (BitWarden.h) is a
 * union, so a field that doesn't belong to this record's own itemType must
 * never be read as a string (it aliases a DIFFERENT type's differently-laid-
 * out union member, i.e. unrelated/uninitialized bytes), only pointed at
 * this empty string instead. */
static const char kEmptyField[1] = "";

/* Returns false if this item was NOT actually stored - see AddMarker's own
 * comment on why the caller (Sync.c) must now treat that as fatal (abort
 * the whole sync) rather than silently continuing as if it succeeded.
 * Returns TRUE for the deliberate "empty note, nothing to store" skip
 * (BitWarden.h's own documented behavior) - that's not a failure. */
Boolean BwStoreAddItem(const BwItemFull *item)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt8 sec35Key[ENC_KEY_BYTES];
	UInt8 *p = gRecBuf;
	UInt16 lens[bwFieldCount];
	UInt16 off, i, total, fi;
	UInt16 idx;
	MemHandle h;
	Boolean stored;
	const char *fields[bwFieldCount];

	/* A secure-note item with no name AND no notes carries no real content
	 * at all - real-device feedback at 779-item vault scale: skipping these
	 * at write time (not filtering them at display time) means every list/
	 * search/filter pass downstream never even sees them, for free. Other
	 * item types always keep SOME meaningful field (a login without a
	 * name still has a URI/username; skipping those risked discarding
	 * something the user actually synced), so this check is deliberately
	 * scoped to bwTypeNote only. */
	if (item->itemType == bwTypeNote && item->name[0] == 0 && item->notes[0] == 0) return true;

	if (!gDB || !BwGetKeyInternal(key, BwGetTtlSeconds())) return false;
	/* Sec35 outer wrap - see AddMarker's own comment on this same pattern
	 * (real-device data-loss report, 2026-09). */
	if (!EncGetKey(sec35Key, EncGetTtlSeconds())) { MemSet(key, sizeof(key), 0); return false; }

	fields[bwFieldName] = item->name;
	fields[bwFieldNotes] = item->notes;
	for (fi = bwField512Count; fi < bwFieldCount; fi++) fields[fi] = kEmptyField;
	fields[bwFieldUser] = kEmptyField;
	fields[bwFieldPass] = kEmptyField;
	fields[bwFieldUri] = kEmptyField;
	fields[bwFieldTotp] = kEmptyField;

	switch (item->itemType) {
	case bwTypeCard:
		fields[bwFieldCardHolder] = item->u.card.holder;
		fields[bwFieldCardBrand] = item->u.card.brand;
		fields[bwFieldCardNumber] = item->u.card.number;
		fields[bwFieldCardExpMonth] = item->u.card.expMonth;
		fields[bwFieldCardExpYear] = item->u.card.expYear;
		fields[bwFieldCardCode] = item->u.card.code;
		break;
	case bwTypeIdentity:
		fields[bwFieldIdTitle] = item->u.identity.title;
		fields[bwFieldIdFirstName] = item->u.identity.firstName;
		fields[bwFieldIdMiddleName] = item->u.identity.middleName;
		fields[bwFieldIdLastName] = item->u.identity.lastName;
		fields[bwFieldIdUsername] = item->u.identity.username;
		fields[bwFieldIdCompany] = item->u.identity.company;
		fields[bwFieldIdSsn] = item->u.identity.ssn;
		fields[bwFieldIdPassportNumber] = item->u.identity.passportNumber;
		fields[bwFieldIdLicenseNumber] = item->u.identity.licenseNumber;
		fields[bwFieldIdEmail] = item->u.identity.email;
		fields[bwFieldIdPhone] = item->u.identity.phone;
		fields[bwFieldIdAddress1] = item->u.identity.address1;
		fields[bwFieldIdAddress2] = item->u.identity.address2;
		fields[bwFieldIdAddress3] = item->u.identity.address3;
		fields[bwFieldIdCity] = item->u.identity.city;
		fields[bwFieldIdState] = item->u.identity.state;
		fields[bwFieldIdPostalCode] = item->u.identity.postalCode;
		fields[bwFieldIdCountry] = item->u.identity.country;
		break;
	case bwTypeNote:
		break;	/* nothing extra - notes above already covers it */
	default:	/* bwTypeLogin */
		fields[bwFieldUser] = item->u.login.username;
		fields[bwFieldPass] = item->u.login.password;
		fields[bwFieldUri] = item->u.login.uri;
		fields[bwFieldTotp] = item->u.login.totp;
		break;
	}

	total = bwHdrFixedSize;
	for (i = 0; i < bwFieldCount; i++) {
		UInt16 ln = (UInt16) StrLen(fields[i]);
		UInt16 maxLen = FieldMaxLen(i);
		if (ln > maxLen - 1) ln = maxLen - 1;
		lens[i] = ln;
		total += ln;
	}
	if (total > bwRecBufSize) total = bwRecBufSize; /* defensive - shouldn't happen given the per-field cap above */

	MemSet(gRecBuf, sizeof(gRecBuf), 0);
	p[0] = item->itemType;
	wrU16(p + 1, item->folderIndex);
	wrU16(p + 3, item->orgIndex);
	MemMove(p + 5, item->guid, bwGuidLen);
	p[bwArchivedByteOff] = item->archived ? 1 : 0;
	off = bwHdrFixedSize;
	for (i = 0; i < bwFieldCount; i++) {
		wrU16(p + 5 + bwGuidLen + i * 2, lens[i]);
		MemMove(p + off, fields[i], lens[i]);
		off += lens[i];
	}

	h = DmNewRecord(gDB, &idx, off);
	stored = (h != NULL);
	if (h != NULL) {
		void *rp = MemHandleLock(h);
		DmWrite(rp, 0, gRecBuf, off);
		MemHandleUnlock(h);
		DmReleaseRecord(gDB, idx, true);

		/* DmNewRecord above is the one call in this function that can
		 * genuinely stall (real-device testing measured 100+ seconds once,
		 * consistent with an internal storage-heap compaction pass when
		 * contiguous free space runs low) - the caller (Sync.c) already
		 * calls BwKeepDeviceAwake() immediately before calling this whole
		 * function, but this second call covers the case where THIS pass
		 * (the per-field encrypt+write loop below) is itself slow enough to
		 * matter on its own, rather than assuming all the risk is before
		 * this point. BwKeepDeviceAwake() (NOT EvtResetAutoOffTimer()) -
		 * see BitWarden.h's own comment on why the distinction matters. */
		BwKeepDeviceAwake();
		BwTouchActivity();

		{
			UInt32 uid = 0;
			DmRecordInfo(gDB, idx, NULL, &uid, NULL);
			h = DmGetRecord(gDB, idx);
			if (h != NULL) {
				rp = MemHandleLock(h);
				{
					UInt8 tmp[bwMaxField];
					UInt16 fieldOff = bwHdrFixedSize;
					for (i = 0; i < bwFieldCount; i++) {
						if (lens[i]) {
							MemMove(tmp, (UInt8 *) rp + fieldOff, lens[i]);
							EncCrypt(key, tmp, lens[i], uid, i);
							EncCrypt(sec35Key, tmp, lens[i], uid, i);	/* Sec35 outer wrap */
							DmWrite(rp, fieldOff, tmp, lens[i]);
						}
						fieldOff += lens[i];
					}
				}
				MemHandleUnlock(h);
				DmReleaseRecord(gDB, idx, true);
			}
		}
	}
	MemSet(key, sizeof(key), 0);
	MemSet(sec35Key, sizeof(sec35Key), 0);
	return stored;
}

/* ------------------------------------------------------------------ *
 *  read path
 * ------------------------------------------------------------------ */
UInt16 BwStoreCount(void)
{
	UInt16 i, n, count = 0;
	if (!gDB) return 0;
	n = DmNumRecords(gDB);
	for (i = 0; i < n; i++) {
		MemHandle h = DmQueryRecord(gDB, i);
		if (h) {
			UInt8 t = ((const UInt8 *) MemHandleLock(h))[0];
			if (t != 0 && t != bwOrgMarkerType) count++;
			MemHandleUnlock(h);
		}
	}
	return count;
}

UInt16 BwStoreFolderCount(void)
{
	UInt16 i, n, count = 0;
	if (!gDB) return 0;
	n = DmNumRecords(gDB);
	for (i = 0; i < n; i++) {
		MemHandle h = DmQueryRecord(gDB, i);
		if (h && ((const UInt8 *) MemHandleLock(h))[0] == 0) count++;
		if (h) MemHandleUnlock(h);
	}
	return count;
}

UInt16 BwStoreOrgCount(void)
{
	UInt16 i, n, count = 0;
	if (!gDB) return 0;
	n = DmNumRecords(gDB);
	for (i = 0; i < n; i++) {
		MemHandle h = DmQueryRecord(gDB, i);
		if (h && ((const UInt8 *) MemHandleLock(h))[0] == bwOrgMarkerType) count++;
		if (h) MemHandleUnlock(h);
	}
	return count;
}

/* Shared by BwStoreGetFolderName/BwStoreGetOrgName - both scan for a
 * marker record of the given type+index and decrypt its name. */
static Boolean GetMarkerName(UInt8 markerType, UInt16 index, UInt32 nonceHi, char *out, UInt16 outLen)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt8 sec35Key[ENC_KEY_BYTES];
	UInt16 i, n;
	Boolean found = false;

	out[0] = 0;
	/* Sec35 outer layer checked FIRST and unconditionally - if it's not
	 * available, don't even attempt the inner (BitWarden-password) layer.
	 * Enforced here at the source so every caller (list, popup, record view)
	 * gets the masked/locked behavior for free rather than needing its own
	 * redundant check. */
	if (!gDB || !EncGetKey(sec35Key, EncGetTtlSeconds())) return false;
	if (!BwGetKeyInternal(key, BwGetTtlSeconds())) { MemSet(sec35Key, sizeof(sec35Key), 0); return false; }
	n = DmNumRecords(gDB);
	for (i = 0; i < n && !found; i++) {
		MemHandle h = DmQueryRecord(gDB, i);
		const UInt8 *rp;
		if (!h) continue;
		rp = (const UInt8 *) MemHandleLock(h);
		if (rp[0] == markerType && rdU16(rp + 1) == index) {
			UInt16 nameLen = rdU16(rp + bwHdrFixedSize - bwFieldCount * 2);
			UInt8 tmp[bwMaxField];
			if (nameLen >= bwMaxField) nameLen = bwMaxField - 1;
			MemMove(tmp, rp + bwHdrFixedSize, nameLen);
			EncCrypt(sec35Key, tmp, nameLen, nonceHi, index);	/* unwrap outer layer first */
			EncCrypt(key, tmp, nameLen, nonceHi, index);		/* then the existing inner layer */
			MemMove(out, tmp, nameLen);
			out[nameLen < outLen ? nameLen : outLen - 1] = 0;
			found = true;
		}
		MemHandleUnlock(h);
	}
	MemSet(key, sizeof(key), 0);
	MemSet(sec35Key, sizeof(sec35Key), 0);
	if (!found) out[0] = 0;
	return found;
}

Boolean BwStoreGetFolderName(UInt16 index, char *out, UInt16 outLen)
{
	return GetMarkerName(0, index, bwFolderNonceHi, out, outLen);
}

Boolean BwStoreGetOrgName(UInt16 index, char *out, UInt16 outLen)
{
	return GetMarkerName(bwOrgMarkerType, index, bwOrgNonceHi, out, outLen);
}

/* Returns the Nth real item's real DB index (folder/org markers are stored
 * as type-0/bwOrgMarkerType records interleaved in the same database - see
 * AddMarker). Every read-path function below takes a "real DB index",
 * found via this. */
static Boolean NthItemDbIndex(UInt16 n, UInt16 *dbIndexOut)
{
	UInt16 i, total, seen = 0;
	if (!gDB) return false;
	total = DmNumRecords(gDB);
	for (i = 0; i < total; i++) {
		MemHandle h = DmQueryRecord(gDB, i);
		UInt8 t;
		if (!h) continue;
		t = ((const UInt8 *) MemHandleLock(h))[0];
		MemHandleUnlock(h);
		if (t != 0 && t != bwOrgMarkerType) {
			if (seen == n) { *dbIndexOut = i; return true; }
			seen++;
		}
	}
	return false;
}

/* Raw-DB-index counterpart to NthItemDbIndex - for MainForm.c's own full-
 * table scan (MainRebuildMatches) ONLY. Every BwStoreGetItem* getter below
 * takes a "recIndex" (Nth real item, skipping folder/org markers) and pays
 * an O(n) NthItemDbIndex scan to resolve it - fine for the single-shot
 * lookups every other caller does (one tap = one lookup), but calling any
 * of them in a loop over every record turns an O(n) rebuild into O(n^2):
 * found 2026-08 via a 780-item fixture in POSE64 - a single type-filter
 * change took ~136s (~304,000 redundant DmQueryRecord/MemHandleLock calls)
 * even though MainForm.c's own cached-match-set rewrite had already fixed
 * the ORIGINAL (decrypt-cost) hang. BwStoreRawRecordCount +
 * BwStoreGetHeaderByDbIndex let a scanning caller walk the table in ONE
 * pass (like BwStoreCount already does) and do its own running recIndex
 * count instead of re-deriving it per record. Returns false for folder/org
 * marker records (and any hole) - the same "not a real item, skip" signal
 * a scanning caller needs, so it never has to know bwOrgMarkerType itself. */
UInt16 BwStoreRawRecordCount(void)
{
	return gDB ? DmNumRecords(gDB) : 0;
}

Boolean BwStoreGetHeaderByDbIndex(UInt16 dbIdx, UInt8 *type, UInt16 *folder, UInt16 *org, Boolean *archived)
{
	MemHandle h;
	const UInt8 *rp;
	UInt8 t;
	if (!gDB) return false;
	h = DmQueryRecord(gDB, dbIdx);
	if (!h) return false;
	rp = (const UInt8 *) MemHandleLock(h);
	t = rp[0];
	if (t == 0 || t == bwOrgMarkerType) { MemHandleUnlock(h); return false; }
	*type = t;
	*archived = (rp[bwArchivedByteOff] != 0);
	*folder = rdU16(rp + 1);
	*org = rdU16(rp + 3);
	MemHandleUnlock(h);
	return true;
}

UInt8 BwStoreGetItemType(UInt16 recIndex)
{
	UInt16 dbIdx;
	MemHandle h;
	UInt8 t;
	if (!NthItemDbIndex(recIndex, &dbIdx)) return 0;
	h = DmQueryRecord(gDB, dbIdx);
	if (!h) return 0;
	t = ((const UInt8 *) MemHandleLock(h))[0];
	MemHandleUnlock(h);
	return t;
}

UInt16 BwStoreGetItemOrg(UInt16 recIndex)
{
	UInt16 dbIdx;
	MemHandle h;
	const UInt8 *rp;
	UInt16 o;
	if (!NthItemDbIndex(recIndex, &dbIdx)) return bwOrgNone;
	h = DmQueryRecord(gDB, dbIdx);
	if (!h) return bwOrgNone;
	rp = (const UInt8 *) MemHandleLock(h);
	o = rdU16(rp + 3);
	MemHandleUnlock(h);
	return o;
}

UInt16 BwStoreGetItemFolder(UInt16 recIndex)
{
	UInt16 dbIdx;
	MemHandle h;
	const UInt8 *rp;
	UInt16 f;
	if (!NthItemDbIndex(recIndex, &dbIdx)) return bwFolderNone;
	h = DmQueryRecord(gDB, dbIdx);
	if (!h) return bwFolderNone;
	rp = (const UInt8 *) MemHandleLock(h);
	f = rdU16(rp + 1);
	MemHandleUnlock(h);
	return f;
}

/* Raw-DB-index counterpart to BwStoreGetItemName - see
 * BwStoreGetHeaderByDbIndex's own comment on why a scanning caller
 * (MainRebuildMatches, while a search term is active) must use this
 * instead of BwStoreGetItemName in a loop. */
Boolean BwStoreGetItemNameByDbIndex(UInt16 dbIdx, char *out, UInt16 outLen)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt8 sec35Key[ENC_KEY_BYTES];
	MemHandle h;
	const UInt8 *rp;
	UInt16 nameLen;
	UInt8 tmp[bwMaxField];
	UInt32 uid;

	out[0] = 0;
	if (!gDB) return false;
	/* Sec35 outer layer first, unconditionally - see GetMarkerName's own
	 * comment on why this is the enforcement point. */
	if (!EncGetKey(sec35Key, EncGetTtlSeconds())) return false;
	if (!BwGetKeyInternal(key, BwGetTtlSeconds())) { MemSet(sec35Key, sizeof(sec35Key), 0); return false; }

	h = DmQueryRecord(gDB, dbIdx);
	if (!h) { MemSet(key, sizeof(key), 0); MemSet(sec35Key, sizeof(sec35Key), 0); return false; }
	rp = (const UInt8 *) MemHandleLock(h);
	nameLen = rdU16(rp + 5 + bwGuidLen + bwFieldName * 2);
	if (nameLen >= bwMaxField) nameLen = bwMaxField - 1;
	MemMove(tmp, rp + bwHdrFixedSize, nameLen);
	MemHandleUnlock(h);

	DmRecordInfo(gDB, dbIdx, NULL, &uid, NULL);
	EncCrypt(sec35Key, tmp, nameLen, uid, bwFieldName);	/* unwrap outer layer first */
	EncCrypt(key, tmp, nameLen, uid, bwFieldName);			/* then the existing inner layer */
	MemSet(key, sizeof(key), 0);
	MemSet(sec35Key, sizeof(sec35Key), 0);

	MemMove(out, tmp, nameLen);
	out[nameLen < outLen ? nameLen : outLen - 1] = 0;
	return true;
}

/* Fast-path pair for MainForm.c's own search-title scan (MainRebuildMatches/
 * MainNarrowMatches while a search term is active) - real-device feedback
 * (2026-08) reported search as "extremely slow, too slow to be even
 * useful" even after the earlier O(n^2) index-resolution fix, because
 * BwStoreGetItemNameByDbIndex (and BwStoreGetItemName before it) calls
 * EncCrypt TWICE per record, and EncCrypt recomputes a full AES-128 key
 * schedule from scratch every single call - up to 1560 redundant key
 * schedules for one keystroke over a 780-item vault, even though both keys
 * (Sec35's, this app's own local-vault key) never change mid-scan. Call
 * BwStoreBeginNameScan ONCE before the loop, then BwStoreGetItemNameFast
 * per record - same two-layer decrypt, same result, but the AES key
 * schedule itself is built exactly twice per scan instead of twice per
 * record. Key material never leaves Store.c (BwStoreBeginNameScan reads it
 * internally and wipes its own copies immediately after building the
 * schedules) - callers only ever hold opaque EncKeySchedule blobs. */
Boolean BwStoreBeginNameScan(EncKeySchedule *sec35Sched, EncKeySchedule *localSched)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt8 sec35Key[ENC_KEY_BYTES];
	if (!EncGetKey(sec35Key, EncGetTtlSeconds())) return false;
	if (!BwGetKeyInternal(key, BwGetTtlSeconds())) { MemSet(sec35Key, sizeof(sec35Key), 0); return false; }
	EncKeyScheduleInit(sec35Sched, sec35Key);
	EncKeyScheduleInit(localSched, key);
	MemSet(key, sizeof(key), 0);
	MemSet(sec35Key, sizeof(sec35Key), 0);
	return true;
}

Boolean BwStoreGetItemNameFast(UInt16 dbIdx, EncKeySchedule *sec35Sched, EncKeySchedule *localSched,
	char *out, UInt16 outLen)
{
	MemHandle h;
	const UInt8 *rp;
	UInt16 nameLen;
	UInt8 tmp[bwMaxField];
	UInt32 uid;

	out[0] = 0;
	if (!gDB) return false;
	h = DmQueryRecord(gDB, dbIdx);
	if (!h) return false;
	rp = (const UInt8 *) MemHandleLock(h);
	nameLen = rdU16(rp + 5 + bwGuidLen + bwFieldName * 2);
	if (nameLen >= bwMaxField) nameLen = bwMaxField - 1;
	MemMove(tmp, rp + bwHdrFixedSize, nameLen);
	MemHandleUnlock(h);

	DmRecordInfo(gDB, dbIdx, NULL, &uid, NULL);
	EncCryptFast(sec35Sched, tmp, nameLen, uid, bwFieldName);	/* unwrap outer layer first */
	EncCryptFast(localSched, tmp, nameLen, uid, bwFieldName);	/* then the existing inner layer */

	MemMove(out, tmp, nameLen);
	out[nameLen < outLen ? nameLen : outLen - 1] = 0;
	return true;
}

/* ---- item cache: headers (cheap) + names (expensive), kept SEPARATE ---
 * Real-device feedback (2026-08, several rounds): even after
 * BwStoreBeginNameScan removed the per-record AES key-schedule recompute,
 * search was STILL noticeably slow - re-decrypting every candidate
 * record's name from scratch on every non-extending keystroke (the first
 * character typed, any backspace, a fresh search after a filter change).
 * gNameCache below fixes that: names are decrypted to lowercase EXACTLY
 * ONCE (lazily, on first SEARCH use), so every keystroke afterward -
 * including the very first one - is a plain in-memory StrStr, no crypto.
 *
 * The header fields (type/folderIndex/orgIndex) get their OWN separate
 * cache (gTypeCache/gFolderCache/gOrgCache) for a specific reason: they're
 * cheap to read (plaintext, no crypto at all) and MainRebuildMatches needs
 * them on EVERY rebuild, including ones with no search text at all (just
 * opening the list, or changing the type/folder/org filter) - building
 * them ALONGSIDE the (expensive, crypto-heavy) name cache would force a
 * full-vault DECRYPT on every plain list open, even for a session that
 * never searches at all. Keeping them independent means a header-only
 * rebuild costs one cheap DmQueryRecord+lock pass (no crypto), while the
 * FIRST search additionally pays for the name decrypt - once, not
 * per-keystroke - exactly matching each cache's own actual cost profile.
 *
 * Both dynamically allocated (MemPtrNew), not static arrays - this app's
 * storage-segment .bss budget has no slack for anything this size (up to
 * ~37KB of names alone for a 780-item vault), but the dynamic heap has
 * plenty of room. Both invalidated (freed outright, not just marked
 * stale) together by BwStoreBeginSync (content may have changed) and
 * BwLock (gNameCache holds decrypted plaintext titles - same "don't keep
 * secrets around once locked" hygiene as every other session key/schedule
 * in this app; gTypeCache/etc aren't secret at all, but share one
 * lifetime with gNameCache for simplicity). */
/* bwNameCacheEntryLen lives in BitWarden.h (shared with MainForm.c). */
static char *gNameCache = NULL;	/* gNameCacheCount * bwNameCacheEntryLen bytes, or NULL if not (yet) built - LOWERCASE, for search matching */
static char *gDisplayNameCache = NULL;	/* same shape, ORIGINAL case - see MainDrawRow's own comment on why this exists */
static UInt16 gNameCacheCount = 0;
static UInt8 *gTypeCache = NULL;	/* gHeaderCacheCount bytes */
static UInt16 *gFolderCache = NULL;	/* gHeaderCacheCount UInt16s */
static UInt16 *gOrgCache = NULL;	/* gHeaderCacheCount UInt16s */
/* Archive filter dimension (Options > Archived Items, MainForm.c) - plain
 * on-disk byte (bwArchivedByteOff), same fast-filtering reasoning as
 * gFolderCache/gOrgCache above; parallel array, same build pass/lifetime. */
static Boolean *gArchivedCache = NULL;	/* gHeaderCacheCount Booleans */
/* Real-device root-cause fix (2026-08): "scroll is very slow at 779
 * records" - every visible row's name lookup that missed gNameCache (i.e.
 * every plain scroll with no search active - BwStoreGetItemName's own
 * fallback path) paid NthItemDbIndex's O(n) linear scan PER ROW, so a
 * single scroll step (~14 visible rows) cost ~14n storage accesses -
 * cheap-feeling on a fast host, real lag on 16-33MHz hardware. This cache
 * records each recIndex's raw db index during the SAME single pass
 * EnsureHeaderCache already does (free - it's already walking every dbIdx
 * to find the type/folder/org triplet), so BwStoreGetItemNameByDbIndex can
 * be called directly - O(1) lookup, no scan - instead of going through
 * NthItemDbIndex at all. */
static UInt16 *gDbIndexCache = NULL;	/* gHeaderCacheCount UInt16s */
static UInt16 gHeaderCacheCount = 0;

void BwStoreInvalidateNameCache(void)
{
	if (gNameCache) { MemPtrFree(gNameCache); gNameCache = NULL; }
	if (gDisplayNameCache) { MemPtrFree(gDisplayNameCache); gDisplayNameCache = NULL; }
	gNameCacheCount = 0;
	if (gTypeCache) { MemPtrFree(gTypeCache); gTypeCache = NULL; }
	if (gFolderCache) { MemPtrFree(gFolderCache); gFolderCache = NULL; }
	if (gOrgCache) { MemPtrFree(gOrgCache); gOrgCache = NULL; }
	if (gDbIndexCache) { MemPtrFree(gDbIndexCache); gDbIndexCache = NULL; }
	if (gArchivedCache) { MemPtrFree(gArchivedCache); gArchivedCache = NULL; }
	gHeaderCacheCount = 0;
}

/* Cheap - no crypto, no EncKeySchedule involvement. Safe to build on any
 * rebuild, searching or not. */
static Boolean EnsureHeaderCache(void)
{
	UInt16 dbTotal, dbIdx, recIndex, count;

	if (gTypeCache) return true;	/* see EnsureNameCacheOnly's own comment on trusting this over a recount */

	dbTotal = BwStoreRawRecordCount();
	count = 0;
	for (dbIdx = 0; dbIdx < dbTotal; dbIdx++) {
		UInt8 type; UInt16 folder, org; Boolean archived;
		if (BwStoreGetHeaderByDbIndex(dbIdx, &type, &folder, &org, &archived)) count++;
	}
	if (count == 0) return true;	/* nothing to cache - not a failure */

	gTypeCache = (UInt8 *) MemPtrNew((UInt32) count * sizeof(UInt8));
	gFolderCache = (UInt16 *) MemPtrNew((UInt32) count * sizeof(UInt16));
	gOrgCache = (UInt16 *) MemPtrNew((UInt32) count * sizeof(UInt16));
	gDbIndexCache = (UInt16 *) MemPtrNew((UInt32) count * sizeof(UInt16));
	gArchivedCache = (Boolean *) MemPtrNew((UInt32) count * sizeof(Boolean));
	if (!gTypeCache || !gFolderCache || !gOrgCache || !gDbIndexCache || !gArchivedCache) {
		BwStoreInvalidateNameCache();
		return false;
	}
	gHeaderCacheCount = count;

	recIndex = 0;
	for (dbIdx = 0; dbIdx < dbTotal; dbIdx++) {
		UInt8 type; UInt16 folder, org; Boolean archived;
		if (!BwStoreGetHeaderByDbIndex(dbIdx, &type, &folder, &org, &archived)) continue;
		gTypeCache[recIndex] = type;
		gFolderCache[recIndex] = folder;
		gOrgCache[recIndex] = org;
		gDbIndexCache[recIndex] = dbIdx;
		gArchivedCache[recIndex] = archived;
		recIndex++;
	}
	return true;
}

/* Expensive - real AES decrypt for every real item's name (once, via
 * BwStoreBeginNameScan's own key-schedule caching). Only called when a
 * search is actually active. */
static Boolean EnsureNameCacheOnly(void)
{
	UInt16 dbTotal, dbIdx, recIndex;
	EncKeySchedule sec35Sched, localSched;

	/* gNameCache != NULL means "already built and still valid" - trust the
	 * explicit invalidation call sites (BwStoreBeginSync, BwLock) rather
	 * than re-verifying the record count here on every call. This function
	 * is called ONCE PER RECORD during a search scan - a real bug in an
	 * earlier version of this cache recomputed the count (a full O(n)
	 * header rescan) on every single call just to confirm the cache was
	 * still fresh, which silently turned the whole point of caching back
	 * into O(n^2) even though the CRYPTO part was genuinely cached. Found
	 * 2026-08 via POSE64 input starving out at >2s per keystroke even after
	 * this cache was added. */
	if (gNameCache) return true;
	if (!EnsureHeaderCache() || gHeaderCacheCount == 0) return gHeaderCacheCount == 0;

	gNameCache = (char *) MemPtrNew((UInt32) gHeaderCacheCount * bwNameCacheEntryLen);
	gDisplayNameCache = (char *) MemPtrNew((UInt32) gHeaderCacheCount * bwNameCacheEntryLen);
	if (!gNameCache || !gDisplayNameCache) {
		if (gNameCache) { MemPtrFree(gNameCache); gNameCache = NULL; }
		if (gDisplayNameCache) { MemPtrFree(gDisplayNameCache); gDisplayNameCache = NULL; }
		return false;	/* allocation failed - caller's findActive branch just finds nothing, same as Sec35/key being unavailable */
	}
	MemSet(gNameCache, (UInt32) gHeaderCacheCount * bwNameCacheEntryLen, 0);
	MemSet(gDisplayNameCache, (UInt32) gHeaderCacheCount * bwNameCacheEntryLen, 0);
	gNameCacheCount = gHeaderCacheCount;

	if (!BwStoreBeginNameScan(&sec35Sched, &localSched)) {
		if (gNameCache) { MemPtrFree(gNameCache); gNameCache = NULL; }
		if (gDisplayNameCache) { MemPtrFree(gDisplayNameCache); gDisplayNameCache = NULL; }
		gNameCacheCount = 0;
		return false;
	}

	dbTotal = BwStoreRawRecordCount();
	recIndex = 0;
	for (dbIdx = 0; dbIdx < dbTotal && recIndex < gNameCacheCount; dbIdx++) {
		UInt8 type; UInt16 folder, org; Boolean archived;
		char name[bwMaxField];
		if (!BwStoreGetHeaderByDbIndex(dbIdx, &type, &folder, &org, &archived)) continue;
		if (BwStoreGetItemNameFast(dbIdx, &sec35Sched, &localSched, name, sizeof(name))) {
			/* Same one decrypt feeds both caches - store original case for
			 * display first, then lowercase in place for search matching. */
			char *dispSlot = gDisplayNameCache + (UInt32) recIndex * bwNameCacheEntryLen;
			char *lowerSlot = gNameCache + (UInt32) recIndex * bwNameCacheEntryLen;
			StrNCopy(dispSlot, name, bwNameCacheEntryLen - 1);
			dispSlot[bwNameCacheEntryLen - 1] = 0;
			StrNCopy(lowerSlot, dispSlot, bwNameCacheEntryLen - 1);
			lowerSlot[bwNameCacheEntryLen - 1] = 0;
			StrToLower(lowerSlot, lowerSlot);
		}
		recIndex++;
	}
	EncKeyScheduleWipe(&sec35Sched);
	EncKeyScheduleWipe(&localSched);
	return true;
}

/* Public entry points for MainForm.c's search/filter loop - recIndex-
 * indexed like every other public getter. Each builds only the cache it
 * actually needs (see the big comment above on why these are kept
 * separate) - the cost of a full-vault pass is paid once per sync/unlock/
 * first-search, not once per keystroke or filter change. */
Boolean BwStoreGetCachedLowerName(UInt16 recIndex, char *out, UInt16 outLen)
{
	if (!EnsureNameCacheOnly() || !gNameCache || recIndex >= gNameCacheCount) { out[0] = 0; return false; }
	StrNCopy(out, gNameCache + (UInt32) recIndex * bwNameCacheEntryLen, outLen - 1);
	out[outLen - 1] = 0;
	return true;
}

/* Direct read-only pointer into the cache, for the hottest of all hot
 * loops (NameMatches, called once per real item on every non-extending
 * search keystroke) - skips the StrNCopy BwStoreGetCachedLowerName does
 * on every call, since the caller here only ever reads (StrStr), never
 * modifies. Still builds the cache on first use like the copying version.
 * NULL if the cache isn't available (search should be treated as "no
 * match", same as every other cache-miss case). */
const char *BwStoreGetCachedLowerNamePtr(UInt16 recIndex)
{
	if (!EnsureNameCacheOnly() || !gNameCache || recIndex >= gNameCacheCount) return NULL;
	return gNameCache + (UInt32) recIndex * bwNameCacheEntryLen;
}

/* Original-case counterpart, for row display (MainForm.c's MainDrawRow) -
 * see gDisplayNameCache's own comment on why this exists (every visible
 * row was re-decrypting from scratch on EVERY redraw, i.e. every
 * keystroke, even after search matching itself got cached). Deliberately
 * does NOT call EnsureNameCacheOnly - only useful once a search has
 * already built the cache; drawing the ~14 visible rows on a plain list
 * open with no search active should stay fast (no forced full-vault
 * decrypt) rather than paying for this cache just to draw them. Returns
 * false if the cache isn't built yet, so the caller falls back to its own
 * slower per-row decrypt path (BwStoreGetItemName), exactly as before
 * this cache existed. */
Boolean BwStoreGetCachedNameIfReady(UInt16 recIndex, char *out, UInt16 outLen)
{
	if (!gDisplayNameCache || recIndex >= gNameCacheCount) { out[0] = 0; return false; }
	StrNCopy(out, gDisplayNameCache + (UInt32) recIndex * bwNameCacheEntryLen, outLen - 1);
	out[outLen - 1] = 0;
	return true;
}

Boolean BwStoreGetCachedHeader(UInt16 recIndex, UInt8 *type, UInt16 *folder, UInt16 *org, Boolean *archived)
{
	if (!EnsureHeaderCache() || !gTypeCache || recIndex >= gHeaderCacheCount) return false;
	*type = gTypeCache[recIndex];
	*archived = gArchivedCache[recIndex];
	*folder = gFolderCache[recIndex];
	*org = gOrgCache[recIndex];
	return true;
}

/* Total real (non-marker) item count - lets a scanning caller loop by
 * recIndex directly against the header cache instead of walking raw db
 * indices via BwStoreGetHeaderByDbIndex itself, which is exactly the
 * per-record storage access this cache exists to avoid. Builds the header
 * cache on first call (cheap - see its own comment), never the name one. */
UInt16 BwStoreGetCachedCount(void)
{
	if (!EnsureHeaderCache()) return 0;
	return gHeaderCacheCount;
}

Boolean BwStoreGetItemName(UInt16 recIndex, char *out, UInt16 outLen)
{
	UInt16 dbIdx;
	out[0] = 0;
	/* Fast path - see gDbIndexCache's own comment (the "scroll is very
	 * slow" fix): O(1) once the (cheap, no-crypto) header cache is built,
	 * instead of NthItemDbIndex's O(n) scan on every single call - this
	 * function is MainDrawRow's own per-row fallback, called once per
	 * VISIBLE row on every redraw/scroll step. */
	if (EnsureHeaderCache() && gDbIndexCache && recIndex < gHeaderCacheCount)
		return BwStoreGetItemNameByDbIndex(gDbIndexCache[recIndex], out, outLen);
	if (!NthItemDbIndex(recIndex, &dbIdx)) return false;
	return BwStoreGetItemNameByDbIndex(dbIdx, out, outLen);
}

Boolean BwStoreGetItemFull(UInt16 recIndex, BwItemFull *out)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt8 sec35Key[ENC_KEY_BYTES];
	UInt16 dbIdx;
	MemHandle h;
	const UInt8 *rp;
	UInt16 lens[bwFieldCount], off, i, fi;
	UInt32 uid;
	char *fields[bwFieldCount];

	MemSet(out, sizeof(*out), 0);
	if (!NthItemDbIndex(recIndex, &dbIdx)) return false;
	/* Sec35 outer layer first, unconditionally - see GetMarkerName's own
	 * comment on why this is the enforcement point. */
	if (!EncGetKey(sec35Key, EncGetTtlSeconds())) return false;
	if (!BwGetKeyInternal(key, BwGetTtlSeconds())) { MemSet(sec35Key, sizeof(sec35Key), 0); return false; }

	h = DmQueryRecord(gDB, dbIdx);
	if (!h) { MemSet(key, sizeof(key), 0); MemSet(sec35Key, sizeof(sec35Key), 0); return false; }
	rp = (const UInt8 *) MemHandleLock(h);

	out->itemType = rp[0];
	out->archived = (rp[bwArchivedByteOff] != 0);
	out->folderIndex = rdU16(rp + 1);
	out->orgIndex = rdU16(rp + 3);
	MemMove(out->guid, rp + 5, bwGuidLen);
	out->guid[bwGuidLen - 1] = 0;

	/* out->u is a union (BitWarden.h) - only the member matching out->
	 * itemType (just read above) may be pointed at directly; every other
	 * field index is aliased into a DIFFERENT type's differently-laid-out
	 * union member, so it's routed at a shared scratch buffer instead. The
	 * read loop below always writes a NUL terminator to fields[i][0] even
	 * for zero-length fields (every field on disk, real or not) - doing
	 * that through an aliased union pointer for an irrelevant field would
	 * corrupt the START of whatever real field happens to share that same
	 * offset (e.g. Card's holder/Identity's title/Login's username are all
	 * the union's own first member, byte offset 0). */
	fields[bwFieldName] = out->name;
	fields[bwFieldNotes] = out->notes;
	for (fi = bwField512Count; fi < bwFieldCount; fi++) fields[fi] = gUnusedFieldScratch;
	fields[bwFieldUser] = gUnusedFieldScratch;
	fields[bwFieldPass] = gUnusedFieldScratch;
	fields[bwFieldUri] = gUnusedFieldScratch;
	fields[bwFieldTotp] = gUnusedFieldScratch;

	switch (out->itemType) {
	case bwTypeCard:
		fields[bwFieldCardHolder] = out->u.card.holder;
		fields[bwFieldCardBrand] = out->u.card.brand;
		fields[bwFieldCardNumber] = out->u.card.number;
		fields[bwFieldCardExpMonth] = out->u.card.expMonth;
		fields[bwFieldCardExpYear] = out->u.card.expYear;
		fields[bwFieldCardCode] = out->u.card.code;
		break;
	case bwTypeIdentity:
		fields[bwFieldIdTitle] = out->u.identity.title;
		fields[bwFieldIdFirstName] = out->u.identity.firstName;
		fields[bwFieldIdMiddleName] = out->u.identity.middleName;
		fields[bwFieldIdLastName] = out->u.identity.lastName;
		fields[bwFieldIdUsername] = out->u.identity.username;
		fields[bwFieldIdCompany] = out->u.identity.company;
		fields[bwFieldIdSsn] = out->u.identity.ssn;
		fields[bwFieldIdPassportNumber] = out->u.identity.passportNumber;
		fields[bwFieldIdLicenseNumber] = out->u.identity.licenseNumber;
		fields[bwFieldIdEmail] = out->u.identity.email;
		fields[bwFieldIdPhone] = out->u.identity.phone;
		fields[bwFieldIdAddress1] = out->u.identity.address1;
		fields[bwFieldIdAddress2] = out->u.identity.address2;
		fields[bwFieldIdAddress3] = out->u.identity.address3;
		fields[bwFieldIdCity] = out->u.identity.city;
		fields[bwFieldIdState] = out->u.identity.state;
		fields[bwFieldIdPostalCode] = out->u.identity.postalCode;
		fields[bwFieldIdCountry] = out->u.identity.country;
		break;
	case bwTypeNote:
		break;	/* nothing extra - notes above already covers it */
	default:	/* bwTypeLogin */
		fields[bwFieldUser] = out->u.login.username;
		fields[bwFieldPass] = out->u.login.password;
		fields[bwFieldUri] = out->u.login.uri;
		fields[bwFieldTotp] = out->u.login.totp;
		break;
	}

	off = bwHdrFixedSize;
	DmRecordInfo(gDB, dbIdx, NULL, &uid, NULL);
	for (i = 0; i < bwFieldCount; i++) {
		UInt16 ln = rdU16(rp + 5 + bwGuidLen + i * 2);
		UInt16 maxLen = FieldMaxLen(i);
		if (ln >= maxLen) ln = maxLen - 1;
		lens[i] = ln;
		if (ln) {
			MemMove(fields[i], rp + off, ln);
			EncCrypt(sec35Key, (UInt8 *) fields[i], ln, uid, i);	/* unwrap outer layer first */
			EncCrypt(key, (UInt8 *) fields[i], ln, uid, i);		/* then the existing inner layer */
		}
		fields[i][ln] = 0;
		off += ln;
	}
	MemHandleUnlock(h);
	MemSet(key, sizeof(key), 0);
	MemSet(sec35Key, sizeof(sec35Key), 0);
	return true;
}

