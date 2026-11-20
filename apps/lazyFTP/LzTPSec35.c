/*
 * LzTPSec35.c - Sec35 bounce gate + per-slot password encrypt/decrypt.
 * The bounce gate (LzTPSec35Installed/LzTPSwitchToSec35) mirrors Datebook35/
 * Address35/CryptoNote/ToDo35's own identical CryptoSwitchToSec35-style
 * functions verbatim (same enccore.h constants, same mechanism) - read as
 * reference, original code.
 */
#include "LzTPSec35.h"
#include "encrekey.h"

/* read-time TTL: Sec35's own alarm enforces the real lock, matches every
 * other app's identical convention (a35EncTtl, kCryptoEncTtl, ...) */
#define lzEncTtl 1000000000UL

/* saved=true (not false) - these are real persistent settings the user
 * expects to survive across sessions (server/port/user/pass), matching
 * Address35's own main-prefs precedent (a35PrefID=0, saved=true), NOT the
 * "throwaway last-used UI state" category (saved=false) that pattern was
 * mistakenly copied from originally - real bug found+fixed this session:
 * settings silently failed to persist across an app relaunch with
 * saved=false. */
/* Key scheme 2 (per-store keys, enccore.h): the saved passwords use the
 * ENC_STORE_LZTP key. The marker is a bit in the PREFERENCE's own version
 * number (not the struct's version field, so older builds still load it).
 * Passwords still under the old scheme read as locked until Sec35 converts
 * them (the bounce below asks for that); a save keeps whatever scheme the
 * passwords are in, so nothing is ever mixed. */
#define lzPrefScheme2	0x4000
static Boolean gLzScheme2 = true;

static Boolean LzTPAnyPassword(const LzTPConnPrefs *p) SEC002;
static Boolean LzTPAnyPassword(const LzTPConnPrefs *p)
{
	UInt8 i;
	for (i = 0; i < lzMaxSlots; i++) if (p->slots[i].passLen) return true;
	return false;
}

SEC002 void LzTPLoadPrefs(LzTPConnPrefs *p)
{
	UInt16 size = sizeof(*p);
	Int16 r = PrefGetAppPreferences(appCreator, 0, p, &size, true);
	if (r == noPreferenceFound || size < sizeof(*p) || p->version != lzPrefsVersion) {
		MemSet(p, sizeof(*p), 0);
		p->version = lzPrefsVersion;
		gLzScheme2 = true;
	} else
		gLzScheme2 = (Boolean) ((r & lzPrefScheme2) || !LzTPAnyPassword(p));
}

SEC002 void LzTPSavePrefs(const LzTPConnPrefs *p)
{
	PrefSetAppPreferences(appCreator, 0, (Int16) (lzPrefsVersion | (gLzScheme2 ? lzPrefScheme2 : 0)),
		(void *) p, sizeof(*p), true);
}

SEC002 Boolean LzTPSec35Installed(void)
{
	DmSearchStateType st;
	UInt16 card;
	LocalID dbID;
	return (Boolean) (DmGetNextDatabaseByTypeCreator(true, &st, sysFileTApplication,
		ENC_CREATOR, true, &card, &dbID) == errNone);
}

SEC002 void LzTPSwitchToSec35(void)
{
	DmSearchStateType st;
	UInt16 card;
	LocalID dbID;
	if (DmGetNextDatabaseByTypeCreator(true, &st, sysFileTApplication, ENC_CREATOR, true,
			&card, &dbID) != errNone)
		return;
	if (!gLzScheme2)								/* old-scheme passwords: Sec35 converts */
		FtrSet(ENC_CREATOR, kSecConvertFtr, 1);
	FtrSet(ENC_CREATOR, kSecReturnFtr, appCreator);	/* return-to creator */
	SysUIAppSwitch(card, dbID, kSecLaunchUnlock, NULL);
}

SEC002 Boolean LzTPEncryptSlotPassword(LzTPSlot *slot, UInt8 slotIndex, const char *plainPass)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt16 len = (UInt16) StrLen(plainPass);

	if (len == 0) {
		MemSet(slot->encPass, sizeof(slot->encPass), 0);
		slot->passLen = 0;
		return true;
	}
	if (len > lzPassMaxLen) len = lzPassMaxLen;
	if (!(gLzScheme2 && EncGetStoreKey(key, ENC_STORE_LZTP))) return false;	/* locked - can't save right now */

	slot->passGen++;							/* a fresh nonce for every save */
	if (slot->passGen == 0) slot->passGen = 1;	/* 0 stays the old builds' nonce */
	MemSet(slot->encPass, sizeof(slot->encPass), 0);
	MemMove(slot->encPass, plainPass, len);
	EncCrypt(key, slot->encPass, len, (UInt32) slot->passGen, (UInt32) slotIndex);
	slot->passLen = len;
	MemSet(key, ENC_KEY_BYTES, 0);
	return true;
}

SEC002 Boolean LzTPDecryptSlotPassword(const LzTPSlot *slot, UInt8 slotIndex,
	char *outPlain, UInt16 outLen)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt16 len = slot->passLen;

	outPlain[0] = '\0';
	if (len == 0) return false;
	if (!(gLzScheme2 && EncGetStoreKey(key, ENC_STORE_LZTP))) return false;	/* locked */
	if (len >= outLen) len = (UInt16) (outLen - 1);

	MemMove(outPlain, slot->encPass, len);
	EncCrypt(key, (UInt8 *) outPlain, len, (UInt32) slot->passGen, (UInt32) slotIndex);
	outPlain[len] = '\0';
	MemSet(key, ENC_KEY_BYTES, 0);
	return true;
}

/* ---- kSecLaunchRekey: Sec35 asks lazyFTP to re-encrypt its saved passwords.
 * A change or the scheme conversion re-keys them (nonce (passGen, slot), under the new key); a removal
 * or forget clears them (there is nowhere safe to keep them without a Sec35
 * password); a set has nothing to do. Written back in one go, marked scheme
 * 2, with a block checkpoint so a reset part-way cannot re-key twice. */
SEC002 void LzTPRekey(EncRekeyParam *p)
{
	LzTPConnPrefs pr;
	UInt16 size = sizeof(pr);
	Int16 r;
	UInt8 kf[ENC_KEY_BYTES], kt[ENC_KEY_BYTES], i;
	Boolean hf, ht;

	p->result = kRekeyResultDone;
	r = PrefGetAppPreferences(appCreator, 0, &pr, &size, true);
	if (r == noPreferenceFound || size < sizeof(pr) || pr.version != lzPrefsVersion)
		return;
	if (!EncRekeyBlobBegin(p, 0, appCreator, &pr, sizeof(pr))) { MemSet(&pr, sizeof(pr), 0); return; }
	EncRekeyKeys(p, ENC_STORE_LZTP, (Boolean) ((r & lzPrefScheme2) != 0), kf, &hf, kt, &ht);
	for (i = 0; i < lzMaxSlots; i++) {
		LzTPSlot *s = &pr.slots[i];
		if (s->passLen > lzPassMaxLen) s->passLen = lzPassMaxLen;
		if (!s->passLen) continue;
		if (p->op == kRekeyRemove || p->op == kRekeyWipe) {
			MemSet(s->encPass, sizeof(s->encPass), 0);
			s->passLen = 0;
		} else if (hf && ht) {
			EncCrypt(kf, s->encPass, s->passLen, (UInt32) s->passGen, (UInt32) i);
			EncCrypt(kt, s->encPass, s->passLen, (UInt32) s->passGen, (UInt32) i);
		}
	}
	PrefSetAppPreferences(appCreator, 0, (Int16) (lzPrefsVersion | lzPrefScheme2), &pr, sizeof(pr), true);
	EncRekeyBlobEnd(p, 0, appCreator);
	MemSet(&pr, sizeof(pr), 0);
	MemSet(kf, sizeof(kf), 0);
	MemSet(kt, sizeof(kt), 0);
}
