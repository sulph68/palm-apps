/*
 * CardCrypt.h - encrypt/decrypt a card note to/from a self-contained file.
 *
 * The on-disk container serialises the SAME AES-128-CTR + hardened-KDF scheme
 * CryptoPad uses for its DB records (see AesCrypt.h / EncryptRecord), just to a
 * flat file instead of a Data Manager record. Byte layout:
 *
 *   [0]  magic     "CNbin1"   (6 bytes; identifies our encrypted files)
 *   [6]  flag      1 byte     (1 = encrypted; reserved for versioning)
 *   [7]  pwIV      16 bytes   (CTR IV for the password verifier)
 *   [23] bodyIV    16 bytes   (CTR IV for the note body)
 *   [39] kdfSalt   16 bytes   (per-file KDF salt, non-secret)
 *   [55] kdfRounds UInt16     (KDF work factor, calibrated at encrypt time)
 *   [57] passwdLen UInt16     (length of the password verifier below)
 *   [59] verifier  passwdLen  (AES-CTR of the password under its own key)
 *   [..] ciphertext rest      (AES-CTR of the note text)
 *
 * The key folds in the per-file password, the global password (if configured)
 * and this device's secret - identical to the record model, so a card file is
 * exactly as strong (and as device-bound) as a DB note.
 */
#ifndef CARDCRYPT_H
#define CARDCRYPT_H

#include <PalmOS.h>
#include "sects.h"     /* AES_SECTION: keep this crypto in sec001 (see below) */

/* CardCrypt lives in the sec001 secondary segment alongside AESCryptEx to keep
 * the main code segment under the 32 KB limit. It is only ever called from the
 * foreground app (card open/save/menu), where every segment is loaded, so the
 * no-globals-sublaunch rule doesn't apply. The section tag must be on both the
 * prototypes and the definitions or cross-segment calls jump to garbage. */

#define cardCryptHdrLen  59        /* fixed header size, before the verifier */
#define cardCryptMagic   "CNbin1"
#define cardCryptMagicLen 6

/* CardCryptDecrypt result codes. */
#define cardCryptOK            0
#define cardCryptBadPassword   1
#define cardCryptNotEncrypted  2
#define cardCryptCorrupt       3

/* True if the buffer looks like one of our encrypted containers. */
Boolean CardCryptIsEncrypted(const UInt8 *data, UInt32 size) AES_SECTION;

/* Build an encrypted container from plaintext. On success *outH holds a fresh
 * handle (caller MemHandleFree's it) of *outSize bytes. */
Err CardCryptEncrypt(const char *plaintext, UInt32 len, const char *password,
                     MemHandle *outH, UInt32 *outSize) AES_SECTION;

/* Verify the password and decrypt. On cardCryptOK, *outPlainH is a fresh,
 * NUL-terminated handle (caller frees) of *outLen bytes. Returns
 * cardCryptBadPassword if the password is wrong, cardCryptNotEncrypted if the
 * buffer isn't ours, cardCryptCorrupt on a malformed container. */
Int16 CardCryptDecrypt(const UInt8 *data, UInt32 size, const char *password,
                       MemHandle *outPlainH, UInt32 *outLen) AES_SECTION;

/* Modal "enter password" dialog (frmCardPassword). Returns true and fills out
 * (up to outSize) only when OK is tapped with a non-empty password. */
Boolean CardPromptPassword(char *out, UInt16 outSize) AES_SECTION;

/* Auto-dismissing status box (e.g. "Encrypting...") for the ~1.5 s KDF. Draw it
 * with CardBusyShow just before a CardCryptEncrypt/Decrypt and restore the screen
 * with CardBusyHide right after. */
WinHandle CardBusyShow(const char *msg) AES_SECTION;
void      CardBusyHide(WinHandle saved) AES_SECTION;

#endif /* CARDCRYPT_H */
