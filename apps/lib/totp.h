/*
 * totp.h - HMAC-SHA1 + RFC 6238 TOTP code generation, self-contained (this
 * is the first SHA-1/HMAC primitive anywhere in this codebase - vendored/
 * written small and focused, same "adopt a small proven piece" ethos as
 * aes.c, not a general-purpose crypto library). Used by the BitWarden app
 * to compute live 6-digit codes from a Bitwarden Login.totp field, which
 * is either a bare base32 secret or a full "otpauth://totp/...?secret=.."
 * URI - TotpGenerateCode handles both.
 */
#ifndef TOTP_H
#define TOTP_H

#include <PalmOS.h>

/* Writes a NUL-terminated 6-digit code (7 bytes incl. NUL) into codeOut.
 * `secretOrUri` is Login.totp exactly as stored (see BwItemFull.totp).
 * `unixSeconds` is real Unix time (NOT PalmOS's 1904 epoch - convert with
 * TotpUnixNow() below first). Returns false (codeOut left empty) if the
 * secret can't be parsed/decoded - never partially fills codeOut. */
Boolean TotpGenerateCode(const char *secretOrUri, UInt32 unixSeconds, char codeOut[7]);

/* PalmOS's TimGetSeconds() counts from 1 Jan 1904, not the Unix epoch (1
 * Jan 1970) that RFC 6238 (and every real TOTP verifier) requires - this
 * wraps that conversion in one place. Accuracy depends entirely on the
 * device's own clock being set correctly; nothing here can fix a wrong
 * device clock. */
UInt32 TotpUnixNow(void);

#endif
