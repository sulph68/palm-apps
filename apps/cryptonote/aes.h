/* aes.h - portable AES-128 implementation, forward (encrypt) direction only.
 *
 * Modeled on the public API of tiny-AES-c (https://github.com/kokke/tiny-AES-c,
 * released to the public domain / Unlicense) but trimmed for this project:
 * only AES-128 is implemented (no AES-192/256), and only the forward cipher
 * is implemented (no InvSubBytes/InvMixColumns/decrypt round function), since
 * CTR mode and the password-derivation routine in AesCrypt.c only ever need
 * to *encrypt* blocks - CTR mode decrypts by re-encrypting the same keystream
 * and XORing. This roughly halves the code size versus a full ECB+CBC+CTR
 * implementation, which matters on a 68K Palm OS target.
 *
 * Validated against the official NIST SP 800-38A Appendix F.1.1 (ECB) and
 * F.5.1 (CTR) AES-128 test vectors before being adapted into this project;
 * see the project notes for the test harness.
 *
 * This file (and aes.c) are released to the public domain, consistent with
 * the license of the code they are modeled on.
 */

#ifndef _AES_H_
#define _AES_H_

#include <stdint.h>
#include <stddef.h>
#include "sects.h"

#define AES_BLOCKLEN 16
#define AES_KEYLEN   16
#define AES_keyExpSize 176

struct AES_ctx
{
  uint8_t RoundKey[AES_keyExpSize];
  uint8_t Iv[AES_BLOCKLEN];
};

void AES_init_ctx(struct AES_ctx* ctx, const uint8_t* key) AES_SECTION;
void AES_init_ctx_iv(struct AES_ctx* ctx, const uint8_t* key, const uint8_t* iv) AES_SECTION;
void AES_ctx_set_iv(struct AES_ctx* ctx, const uint8_t* iv) AES_SECTION;

/* Single 16-byte block, forward (encrypt) direction only. */
void AES_ECB_encrypt(const struct AES_ctx* ctx, uint8_t* buf) AES_SECTION;

/* Same function serves encrypt and decrypt in CTR mode. */
void AES_CTR_xcrypt_buffer(struct AES_ctx* ctx, uint8_t* buf, size_t length) AES_SECTION;

#endif
