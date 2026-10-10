/*
 * LzTPSec35.c - Sec35 bounce gate + per-slot password encrypt/decrypt.
 * The bounce gate (LzTPSec35Installed/LzTPSwitchToSec35) mirrors Datebook35/
 * Address35/CryptoNote/ToDo35's own identical CryptoSwitchToSec35-style
 * functions verbatim (same enccore.h constants, same mechanism) - read as
 * reference, original code.
 */
#include "LzTPSec35.h"

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
SEC002 void LzTPLoadPrefs(LzTPConnPrefs *p)
{
	UInt16 size = sizeof(*p);
	Int16 r = PrefGetAppPreferences(appCreator, 0, p, &size, true);
	if (r == noPreferenceFound || size < sizeof(*p) || p->version != lzPrefsVersion) {
		MemSet(p, sizeof(*p), 0);
		p->version = lzPrefsVersion;
	}
}

SEC002 void LzTPSavePrefs(const LzTPConnPrefs *p)
{
	PrefSetAppPreferences(appCreator, 0, lzPrefsVersion, (void *) p, sizeof(*p), true);
}

SEC002 Boolean LzTPSec35Installed(void)
{
	DmSearchStateType st;
	UInt16 card;
	LocalID dbID;
	return (Boolean) (DmGetNextDatabaseByTypeCreator(true, &st, sysFileTApplication,
		ENC_CREATOR, true, &card, &dbID) == errNone);
}

SEC002 Boolean LzTPSec35Unlocked(void)
{
	UInt8 key[ENC_KEY_BYTES];
	Boolean ok = EncGetKey(key, lzEncTtl);
	if (ok) MemSet(key, ENC_KEY_BYTES, 0);
	return ok;
}

SEC002 void LzTPSwitchToSec35(void)
{
	DmSearchStateType st;
	UInt16 card;
	LocalID dbID;
	if (DmGetNextDatabaseByTypeCreator(true, &st, sysFileTApplication, ENC_CREATOR, true,
			&card, &dbID) != errNone)
		return;
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
	if (!EncGetKey(key, lzEncTtl)) return false;	/* locked - can't save right now */

	MemSet(slot->encPass, sizeof(slot->encPass), 0);
	MemMove(slot->encPass, plainPass, len);
	EncCrypt(key, slot->encPass, len, 0, (UInt32) slotIndex);
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
	if (!EncGetKey(key, lzEncTtl)) return false;	/* locked */
	if (len >= outLen) len = (UInt16) (outLen - 1);

	MemMove(outPlain, slot->encPass, len);
	EncCrypt(key, (UInt8 *) outPlain, len, 0, (UInt32) slotIndex);
	outPlain[len] = '\0';
	MemSet(key, ENC_KEY_BYTES, 0);
	return true;
}
