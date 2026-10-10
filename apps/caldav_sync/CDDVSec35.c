/*
 * CDDVSec35.c - Sec35 bounce gate + per-slot secret encrypt/decrypt.
 * Directly mirrors apps/lazyFTP/LzTPSec35.c's own identical mechanism
 * (same enccore.h constants, same CryptoSwitchToSec35-style bounce every
 * "35" suite app uses) - read as reference, original code, generalized here
 * to wrap either an app-specific password or an OAuth refresh token rather
 * than only a password.
 */
#include "CDDVSec35.h"

/* read-time TTL: Sec35's own alarm enforces the real lock, matches every
 * other app's identical convention (a35EncTtl, kCryptoEncTtl, lzEncTtl...) */
#define cddvEncTtl 1000000000UL

SEC002 Boolean CDDVSec35Installed(void)
{
	DmSearchStateType st;
	UInt16 card;
	LocalID dbID;
	return (Boolean) (DmGetNextDatabaseByTypeCreator(true, &st, sysFileTApplication,
		ENC_CREATOR, true, &card, &dbID) == errNone);
}

SEC002 Boolean CDDVSec35Unlocked(void)
{
	UInt8 key[ENC_KEY_BYTES];
	Boolean ok = EncGetKey(key, cddvEncTtl);
	if (ok) MemSet(key, ENC_KEY_BYTES, 0);
	return ok;
}

SEC002 void CDDVSwitchToSec35(void)
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

SEC002 Boolean CDDVEncryptSlotSecret(CDDVSlot *slot, UInt8 slotIndex, const char *plainSecret)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt16 len = (UInt16) StrLen(plainSecret);

	if (len == 0) {
		MemSet(slot->secret, sizeof(slot->secret), 0);
		slot->secretLen = 0;
		return true;
	}
	if (len > cddvSecretMaxLen) len = cddvSecretMaxLen;
	if (!EncGetKey(key, cddvEncTtl)) return false;	/* locked - can't save right now */

	MemSet(slot->secret, sizeof(slot->secret), 0);
	MemMove(slot->secret, plainSecret, len);
	EncCrypt(key, slot->secret, len, 0, (UInt32) slotIndex);
	slot->secretLen = len;
	MemSet(key, ENC_KEY_BYTES, 0);
	return true;
}

SEC002 Boolean CDDVDecryptSlotSecret(const CDDVSlot *slot, UInt8 slotIndex,
	char *outPlain, UInt16 outLen)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt16 len = slot->secretLen;

	outPlain[0] = '\0';
	if (len == 0) return false;
	if (!EncGetKey(key, cddvEncTtl)) return false;	/* locked */
	if (len >= outLen) len = (UInt16) (outLen - 1);

	MemMove(outPlain, slot->secret, len);
	EncCrypt(key, (UInt8 *) outPlain, len, 0, (UInt32) slotIndex);
	outPlain[len] = '\0';
	MemSet(key, ENC_KEY_BYTES, 0);
	return true;
}

SEC002 Boolean CDDVEncryptSlotField(UInt8 slotIndex, CDDVField field,
	UInt8 *cipherOut, UInt16 cipherCap, const char *plainText, UInt16 *outLen)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt16 len = (UInt16) StrLen(plainText);

	if (len == 0) {
		MemSet(cipherOut, cipherCap, 0);
		*outLen = 0;
		return true;
	}
	if (len > cipherCap) len = cipherCap;
	if (!EncGetKey(key, cddvEncTtl)) return false;	/* locked - can't save right now */

	MemSet(cipherOut, cipherCap, 0);
	MemMove(cipherOut, plainText, len);
	EncCrypt(key, cipherOut, len, (UInt32) field, (UInt32) slotIndex);
	*outLen = len;
	MemSet(key, ENC_KEY_BYTES, 0);
	return true;
}

SEC002 Boolean CDDVDecryptSlotField(UInt8 slotIndex, CDDVField field,
	const UInt8 *cipherIn, UInt16 cipherLen, char *outPlain, UInt16 outCap)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt16 len = cipherLen;

	outPlain[0] = '\0';
	if (len == 0) return true;	/* nothing to decrypt - not a failure */
	if (!EncGetKey(key, cddvEncTtl)) return false;	/* locked */
	if (len >= outCap) len = (UInt16) (outCap - 1);

	MemMove(outPlain, cipherIn, len);
	EncCrypt(key, (UInt8 *) outPlain, len, (UInt32) field, (UInt32) slotIndex);
	outPlain[len] = '\0';
	MemSet(key, ENC_KEY_BYTES, 0);
	return true;
}

/* Reused for every field this migration pass touches - avoids a 7-times
 * repeated "decrypt would be a no-op here, just plaintext-copy-in-place and
 * set the length" dance by going through the exact same MemMove(cipherOut,
 * plainText, len) + EncCrypt() sequence CDDVEncryptSlotField already does,
 * just with the source and destination being the SAME buffer (safe -
 * EncCrypt operates in place on cipherOut itself once MemMove has already
 * copied plainText's bytes into it; identical to CDDVEncryptSlotField's own
 * plainText != cipherOut case in every other respect). */
static Boolean CDDVEncryptFieldInPlace(UInt8 slotIndex, CDDVField field,
	char *buf, UInt16 bufCap, UInt16 *lenOut)
{
	char plain[cddvUrlLen];	/* largest of the fields this is called for */
	UInt16 plainLen = (UInt16) StrLen(buf);

	if (plainLen == 0) { *lenOut = 0; return true; }
	if (plainLen >= sizeof(plain)) plainLen = sizeof(plain) - 1;
	MemMove(plain, buf, plainLen);
	plain[plainLen] = '\0';
	return CDDVEncryptSlotField(slotIndex, field, (UInt8 *) buf, bufCap, plain, lenOut);
}

SEC002 Boolean CDDVEncryptSlotFieldsIfNeeded(CDDVSlot *slot, UInt8 slotIndex)
{
	if (slot->fieldsEncrypted) return true;

	if (!CDDVEncryptFieldInPlace(slotIndex, cddvFieldServer, slot->server, sizeof(slot->server), &slot->serverLen))
		return false;
	if (!CDDVEncryptFieldInPlace(slotIndex, cddvFieldPath, slot->path, sizeof(slot->path), &slot->pathLen))
		return false;
	if (!CDDVEncryptFieldInPlace(slotIndex, cddvFieldUuid, slot->uuid, sizeof(slot->uuid), &slot->uuidLen))
		return false;
	if (!CDDVEncryptFieldInPlace(slotIndex, cddvFieldUser, slot->user, sizeof(slot->user), &slot->userLen))
		return false;
	if (!CDDVEncryptFieldInPlace(slotIndex, cddvFieldAlias, slot->alias, sizeof(slot->alias), &slot->aliasLen))
		return false;
	if (!CDDVEncryptFieldInPlace(slotIndex, cddvFieldGoogleClientId, slot->googleClientId,
			sizeof(slot->googleClientId), &slot->googleClientIdLen))
		return false;
	if (!CDDVEncryptFieldInPlace(slotIndex, cddvFieldGoogleClientSecret, slot->googleClientSecret,
			sizeof(slot->googleClientSecret), &slot->googleClientSecretLen))
		return false;

	slot->fieldsEncrypted = true;
	return true;
}

SEC002 Boolean CDDVCryptBytes(UInt32 nonceHi, UInt32 nonceLo, UInt8 *buf, UInt16 len)
{
	UInt8 key[ENC_KEY_BYTES];
	if (len == 0) return true;
	if (!EncGetKey(key, cddvEncTtl)) return false;
	EncCrypt(key, buf, len, nonceHi, nonceLo);
	MemSet(key, ENC_KEY_BYTES, 0);
	return true;
}
