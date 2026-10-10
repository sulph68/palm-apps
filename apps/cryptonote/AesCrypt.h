/* AesCrypt.h
 *
 * Palm OS glue around the portable AES-128 core in aes.c/aes.h. Provides
 * CryptoPad's encryption entry point (AESCryptEx) plus a hardened, stretched
 * key-derivation function.
 *
 * Key derivation (DeriveRecordKey, internal) folds together several inputs and
 * runs a deliberately slow, iterated AES-based compression so that guessing a
 * password offline is expensive:
 *
 *   recordKey = Stretch( record-password
 *                        || global-password   (only if one is configured)
 *                        || device-secret     (this device's ROM serial)
 *                        || per-record-salt , rounds )
 *
 * Why each input:
 *   - record-password : the per-record secret the user types.
 *   - global-password : an optional second secret (Prefs "global password").
 *                       When set it is required IN ADDITION to the record
 *                       password - it no longer replaces it.
 *   - device-secret   : SysGetROMToken(sysROMTokenSnum), i.e. the device's
 *                       hardware serial. It is never stored in the database, so
 *                       a stolen backup cannot be decrypted on another device.
 *                       (See GetDeviceSecret; a random, non-backed-up local key
 *                       is used as a fallback on devices without a serial.)
 *   - per-record-salt : 16 random bytes stored in the record. Non-secret; makes
 *                       identical passwords yield different keys and defeats
 *                       precomputation.
 *   - rounds          : work factor, calibrated to ~1.5 s on this device at
 *                       encryption time (CalibrateKdfRounds) and stored in the
 *                       record so decryption reproduces the same key.
 *
 * CTR mode details (random per-encryption IV, why CTR fits the record layout)
 * are unchanged from before; see the notes at the call sites in Database.h.
 */

#ifndef __AESCRYPT_H__
#define __AESCRYPT_H__

#include <PalmOS.h>
#include "sects.h"

#define AES_ENCRYPT 1
#define AES_DECRYPT 0

#define AES_KEY_SIZE  16
#define AES_IV_SIZE   16
#define AES_SALT_SIZE 16

#ifdef  __cplusplus
extern "C" {
#endif

/* Fills iv (or a salt) with 16 pseudo-random bytes from Palm OS's SysRandom.
 * Adequate for CTR IV uniqueness and for salts (neither needs to be secret or
 * unpredictable to an attacker, only unique). */
void GenerateRandomIV(UInt8 iv[AES_IV_SIZE]) AES_SECTION;

/* Returns the number of stretch rounds that take ~1.5 s on this device. The
 * result is measured once (via TimGetTicks) and cached for the session. Store
 * the returned value in the record so the same key can be re-derived later. */
UInt16 CalibrateKdfRounds(void) AES_SECTION;

/* Hardened encrypt/decrypt. Derives a record key from recordPassword,
 * globalPassword (pass 0 if no global password is configured), the device
 * secret and salt, stretched by rounds, then runs AES-128-CTR over nLength
 * bytes of input into output (output needs nLength+1 bytes; output[nLength] is
 * set to '\0'). CTR is its own inverse, so encrypt only affects call-site
 * symmetry, not the operation. */
void AESCryptEx(Char * input, Char * output,
                Char * recordPassword, Char * globalPassword,
                UInt8 * iv, UInt8 * salt, UInt16 rounds,
                UInt16 nLength, Int16 encrypt) AES_SECTION;

/* Advances a CTR IV/counter by `blocks` AES blocks (16 bytes each) --
 * i.e. computes the IV that AESCryptEx would need to correctly decrypt
 * starting at absolute byte offset `blocks*16` into a stream that was
 * originally encrypted in one pass from the given base IV. CTR mode's
 * keystream for block i is AES(key, IV+i), so this lets a large body be
 * decrypted in independent pieces (each piece's own AESCryptEx call,
 * fresh IV computed via this function) instead of needing the whole
 * body in one buffer/one call -- as long as each piece STARTS at a
 * 16-byte-aligned offset from the stream's true start (mid-block starts
 * need the containing block decrypted from its own start and the
 * unwanted leading bytes discarded; this function only computes the
 * counter, it doesn't handle that alignment for you). iv is modified
 * in place; treated as a big-endian 128-bit counter, matching aes.c's
 * own increment logic in AES_CTR_xcrypt_buffer. */
void AesCtrAdvanceIV(UInt8 iv[AES_IV_SIZE], UInt32 blocks) AES_SECTION;

#ifdef  __cplusplus
}
#endif

#endif /* __AESCRYPT_H__ */
