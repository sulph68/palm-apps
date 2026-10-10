/*
 * CDDVSec35.h - Sec35 integration for every identifying/config field in a
 * slot (server/path/uuid/user/alias/googleClientId/googleClientSecret), not
 * just the secret. Originally only secret was encrypted (matching Address35/
 * lazyFTP's "encrypt payload, not everything" pattern) - widened 2026-09-24
 * at the user's own request, since this app's whole reason to exist is
 * storing calendar account details, and every one of those fields is real
 * personal information (server hostnames, usernames/Apple IDs, calendar
 * IDs/UUIDs, OAuth credentials). Directly mirrors apps/lazyFTP/LzTPSec35.h/.c
 * for the original secret-only mechanism - read as reference for that part.
 */
#ifndef CDDVSEC35_H
#define CDDVSEC35_H

#include "CalDAVSync.h"

Boolean CDDVSec35Installed(void) SEC002;
Boolean CDDVSec35Unlocked(void) SEC002;
void    CDDVSwitchToSec35(void) SEC002;

/* Encrypts plainSecret into slot->secret/secretLen using the per-slot index
 * as the AES-CTR nonce (stable and unique per slot, same idea lazyFTP's
 * LzTPEncryptSlotPassword uses). No-op (returns false, slot left unchanged)
 * if Sec35 is locked - caller can still use the freshly typed/received
 * plaintext for an immediate sync, it just can't be saved for later until
 * unlocked. */
Boolean CDDVEncryptSlotSecret(CDDVSlot *slot, UInt8 slotIndex, const char *plainSecret) SEC002;

/* Decrypts slot->secret into outPlain. Returns false (outPlain left empty)
 * if locked or no secret is saved (e.g. an ICS Feed slot needs none). */
Boolean CDDVDecryptSlotSecret(const CDDVSlot *slot, UInt8 slotIndex,
	char *outPlain, UInt16 outLen) SEC002;

/* ---- generic per-field encrypt/decrypt (server/path/uuid/user/alias/
 * googleClientId/googleClientSecret) ---------------------------------------
 * Same AES-CTR-in-place scheme as the secret above, but secret's own nonce
 * (nonceHi=0, nonceLo=slotIndex) is left untouched for backward
 * compatibility with already-saved ciphertext - every OTHER field on the
 * same slot needs its own distinct nonce or CTR mode's keystream-reuse
 * weakness applies (XORing two fields' ciphertext on the same slot would
 * leak the XOR of their plaintexts). nonceHi = one of the CDDVField*
 * constants below (all nonzero, all distinct from secret's own 0 and from
 * each other), nonceLo = slotIndex, same as secret. */
typedef enum {
	cddvFieldServer = 1,
	cddvFieldPath,
	cddvFieldUuid,
	cddvFieldUser,
	cddvFieldAlias,
	cddvFieldGoogleClientId,
	cddvFieldGoogleClientSecret
} CDDVField;

/* Encrypts plainText (NUL-terminated) into cipherOut (cipherCap bytes) and
 * sets *outLen to the ciphertext/plaintext length (same length, AES-CTR is a
 * stream cipher). Empty plainText just zeroes cipherOut and sets *outLen=0 -
 * always succeeds, even locked (nothing to encrypt). Returns false only if
 * Sec35 is locked AND plainText is non-empty (cipherOut/*outLen untouched -
 * caller keeps whatever was there before, same "can't save right now"
 * contract as CDDVEncryptSlotSecret). */
Boolean CDDVEncryptSlotField(UInt8 slotIndex, CDDVField field,
	UInt8 *cipherOut, UInt16 cipherCap, const char *plainText, UInt16 *outLen) SEC002;

/* Decrypts cipherIn/cipherLen into outPlain (NUL-terminated, outCap bytes
 * incl. NUL). cipherLen==0 just empties outPlain and returns true (nothing
 * to decrypt - not a failure). Returns false (outPlain left empty) only if
 * locked and there was real ciphertext to decrypt. */
Boolean CDDVDecryptSlotField(UInt8 slotIndex, CDDVField field,
	const UInt8 *cipherIn, UInt16 cipherLen, char *outPlain, UInt16 outCap) SEC002;

/* One-time migration: if slot->fieldsEncrypted is already true, a fast
 * no-op. Otherwise encrypts server/path/uuid/user/alias/googleClientId/
 * googleClientSecret in place (they arrived here as plaintext, forward-
 * copied byte-for-byte by CDDVLoadPrefs's v4-or-earlier migration branch)
 * and sets fieldsEncrypted=true. Call once per slot from
 * CalDAVSync.c's StartApplication, AFTER the app's own Sec35 unlock gate has
 * already confirmed EncKeyPresent() - returns false (slot left untouched,
 * still plaintext) if Sec35 turns out to be locked anyway, so a caller that
 * skips its own precondition check fails safe instead of silently leaving
 * a slot in a half-migrated state. */
Boolean CDDVEncryptSlotFieldsIfNeeded(CDDVSlot *slot, UInt8 slotIndex) SEC002;

/* ---- raw AES-CTR in place, explicit caller-supplied nonce ---------------
 * Used by DAVDb.c to encrypt DateBook35DAV.pdb's own event records, whose
 * natural per-record nonce is each record's own DmRecordInfo() uniqueID,
 * not a slot index - same convention DateDB.c's own appointment encryption
 * already established (nonce = (uniqueID, fieldIndex)). Same call both
 * directions (AES-CTR is its own inverse) - true on success, false (buf
 * left untouched) if Sec35 is locked. A no-op success for len==0. */
Boolean CDDVCryptBytes(UInt32 nonceHi, UInt32 nonceLo, UInt8 *buf, UInt16 len) SEC002;

#endif
