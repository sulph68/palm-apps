/*
 * LzTPSec35.h - Sec35 integration for the 3 saved connection slots' saved
 * passwords. Only the password field is encrypted (server/port/username
 * stay clear), matching Address35's own "encrypt payload fields, not
 * everything" pattern (see AddressDB35.c's A35GetRecord/A35EncryptRecordInPlace,
 * read as reference here - original code).
 */
#ifndef LZTPSEC35_H
#define LZTPSEC35_H

#include "LazyFTP.h"
#include "FtpProtocol.h"
#include "enccore.h"

#define lzMaxSlots      3
#define lzPassMaxLen    32
#define lzPrefsVersion  1

typedef struct {
	char       server[64];
	UInt16     port;
	char       user[64];
	UInt8      encPass[lzPassMaxLen];	/* ciphertext, or zeroed if none saved */
	UInt16     passLen;					/* 0 = no saved password */
	FtpSecMode secMode;					/* reserved, unused in v1 - see
										   the FTPS-extensibility plan in
										   project memory lazyftp-project.md */
} LzTPSlot;

typedef struct {
	UInt16   version;
	UInt8    activeSlot;		/* 0..lzMaxSlots-1, last used */
	LzTPSlot slots[lzMaxSlots];
} LzTPConnPrefs;

void    LzTPLoadPrefs(LzTPConnPrefs *p) SEC002;
void    LzTPSavePrefs(const LzTPConnPrefs *p) SEC002;

Boolean LzTPSec35Installed(void) SEC002;
Boolean LzTPSec35Unlocked(void) SEC002;
void    LzTPSwitchToSec35(void) SEC002;

/* Encrypts plainPass into slot->encPass/passLen using the per-slot index as
 * the AES-CTR nonce (stable and unique per slot, same idea as Address35
 * using (uniqueID, fieldIndex) - see AddressDB35.c). No-op (returns false,
 * slot left unchanged) if Sec35 is locked - the caller can still USE the
 * freshly typed plaintext for an immediate connect, it just can't be saved
 * for later until unlocked. */
Boolean LzTPEncryptSlotPassword(LzTPSlot *slot, UInt8 slotIndex, const char *plainPass) SEC002;

/* Decrypts slot->encPass into outPlain. Returns false (outPlain left empty)
 * if locked or no password is saved. */
Boolean LzTPDecryptSlotPassword(const LzTPSlot *slot, UInt8 slotIndex,
	char *outPlain, UInt16 outLen) SEC002;

#endif
