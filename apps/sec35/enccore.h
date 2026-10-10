/*
 * enccore.h - Sec35 shared crypto + session-key core.
 *
 * Device-bound AES-128-CTR with a stretched key derivation, plus an in-RAM
 * session-key cache (Feature Manager) with a TTL. Reuses the bundled tiny-AES
 * (aes.c/aes.h). This is the reusable spine that Sec35 and (later) the era apps
 * link; SecTest exercises it. See apps/sec35/DESIGN.md.
 */
#ifndef ENCCORE_H
#define ENCCORE_H

#include <PalmOS.h>

#define ENC_KEY_BYTES   16
#define ENC_SALT_BYTES  16

/* Shared identifiers -- MUST match in every binary that shares the key. */
#define ENC_CREATOR     'Sec3'
#define ENC_KEY_FTR     1000	/* FtrPtrNew value = EncKeyBlock (key + unlockTime) */

/* Unlock hand-off: a locked app FtrSet(ENC_CREATOR, kSecReturnFtr, itsCreator) then
 * SysUIAppSwitch()es to Sec35 with kSecLaunchUnlock (no param chunk - avoids
 * cross-app MemPtrFree). Sec35 pops its unlock dialog directly and, once unlocked,
 * switches back to that creator and clears the feature. */
#define kSecLaunchUnlock	0x9000
#define kSecReturnFtr		1001	/* Ftr(ENC_CREATOR, 1001) = return-to creator */
#define ENC_PWSET_FTR		1002	/* Ftr(ENC_CREATOR, 1002) present = a passcode is set */

/* This device's binding secret: the hardware serial number if present, else a
 * random per-device fallback stored in unsaved (non-backed-up) preferences.
 * Returns the length written into 'out' (<= maxLen), 0 on failure. */
UInt16 EncDeviceSecret (UInt8* out, UInt16 maxLen);

/* Best-effort random bytes (for salts / the fallback device key). */
void EncRandomBytes (UInt8* buf, UInt16 len);

/* Derive a 16-byte key = stretch( passcode || deviceSecret || salt , rounds ).
 * Deterministic for the same inputs on the same device. */
void EncDeriveKey (const char* passcode, const UInt8 salt[ENC_SALT_BYTES],
                   UInt16 rounds, UInt8 keyOut[ENC_KEY_BYTES]);

/* Number of stretch iterations that take ~targetMs on this device (>=1). */
UInt16 EncCalibrateRounds (UInt16 targetMs);

/* AES-128-CTR, in place. Encrypt and decrypt are the same call. 'nonceHi:nonceLo'
 * seed the top 8 bytes of the 128-bit counter (low 8 bytes start at 0). */
void EncCrypt (const UInt8 key[ENC_KEY_BYTES], UInt8* buf, UInt32 len,
               UInt32 nonceHi, UInt32 nonceLo);

/* ---- session-key cache (shared RAM, not in any HotSync backup) ---------- */

/* Store {key, unlockTime = now} in a persistent-RAM feature. Replaces any
 * existing cached key. */
Err EncCacheKey (const UInt8 key[ENC_KEY_BYTES]);

/* If a key is cached AND (now - unlockTime) <= ttlSeconds AND now >= unlockTime
 * (no clock rollback), copy it out and return true; otherwise wipe it and
 * return false. */
Boolean EncGetKey (UInt8 keyOut[ENC_KEY_BYTES], UInt32 ttlSeconds);

/* Read-only peek at how many seconds are left before EncGetKey/EncKeyPresent
 * would consider the cached key expired under ttlSeconds -- -1 if no key is
 * cached or it's already expired. Never mutates/wipes anything, safe to call
 * purely for UI display (a countdown) on every redraw. */
Int32 EncSecondsRemaining (UInt32 ttlSeconds);

/* True if a key is cached AND still within the current auto-lock TTL (see
 * EncGetTtlSeconds) -- self-wipes and returns false if it's actually expired,
 * same as EncGetKey. This is the check nearly every app uses to decide
 * "is the session unlocked" (masking, gating, Sec35's own status label), so it
 * must not just report bare presence: the real enforcement is a background
 * AlmSetAlarm that calls EncWipeKey when the TTL elapses (works even while the
 * device is powered off), but that's a single point of failure if an alarm is
 * ever missed/delayed -- EncKeyPresent is the fallback that makes every access
 * self-correct against the real elapsed time regardless of whether that alarm
 * actually fired. */
Boolean EncKeyPresent (void);

/* Zero and free the cached key. */
void EncWipeKey (void);

/* ---- shared auto-lock TTL (Sec35's Auto Lock Handheld setting) ---------- *
 * Published cross-app the same way as EncGetPasswordEntryType/
 * EncSetPasswordEntryType (UNSAVED prefs, not HotSync-backed): Sec35 is the
 * only writer (on load and whenever the setting changes), every app's
 * EncKeyPresent call reads it to self-check the cached key's real age. Not a
 * secret -- same category of value as the salt, safe to store in the clear.
 * 300 (5 minutes) if never published, matching Sec35's own gPref default. */
#define ENC_TTL_PREF	3	/* unsaved pref holding a single UInt32: seconds */

UInt32 EncGetTtlSeconds (void);
void   EncSetTtlSeconds (UInt32 seconds);

/* ---- lock on sleep (Sec35's "Lock on Power-Off" checkbox) --------------- *
 * Published the same way as the TTL/password-entry-method prefs above.
 * Whether the cached key should be wiped immediately when the device is about
 * to sleep/power off, rather than waiting for the TTL to elapse. Off by
 * default (0) -- matches pre-existing behavior (TTL-only) if never set. */
#define ENC_LOCKONSLEEP_PREF	4	/* unsaved pref holding a single UInt8: 0/1 */

Boolean EncGetLockOnSleep (void);
void    EncSetLockOnSleep (Boolean on);

/* Register/unregister this app's OWN sleep-notify handler (wipes the cached
 * key on sysNotifySleepNotifyEvent when EncGetLockOnSleep is on). Uses a raw
 * function pointer (SysNotifyRegister), which is only meaningful while the
 * registering app's code is actually resident -- call EncRegisterSleepWipe
 * once from StartApplication and EncUnregisterSleepWipe from StopApplication
 * in EVERY app that links this file (Sec35 + the four era apps), so the
 * registration's lifetime always exactly matches app residency. This means
 * the wipe only fires if the device sleeps while one of THIS SUITE's own apps
 * happens to be foreground -- sleeping during an unrelated app won't trigger
 * it (PalmOS classic has no true background-service equivalent for this).
 * The TTL self-check (EncKeyPresent) is the backstop for that gap, and for
 * the SDK's own documented case where the sleep notification isn't
 * guaranteed to be broadcast at all (e.g. an emergency low-battery
 * shutdown). */
void EncRegisterSleepWipe (void);
void EncUnregisterSleepWipe (void);

/* "A passcode is set" signal (survives lock; Sec35 owns it). Independent of the
 * cached key: true whenever encryption is ACTIVE, even while locked / with no
 * record encrypted yet. Sec35 asserts it; era apps read it to prompt+seal. */
void EncSetEncryptionActive (Boolean on);
Boolean EncEncryptionActive (void);

/* Password entry method (Sec35's Preferences > Password Type), shared cross-app
 * via the UNSAVED prefs area (saved=false, same mechanism as EncDeviceSecret's
 * fallback key, NOT HotSync-backed) so other apps (e.g. CryptoNote) can offer
 * the same masked-entry style without depending on Sec35's own Sec35Pref
 * struct layout. Sec35 is the only writer; other apps only ever read.
 * kPwTypeText is the correct default whenever nothing has been set yet. */
enum { kPwTypeText, kPwTypeNumeric };
#define ENC_PWTYPE_PREF	2	/* unsaved pref holding a single UInt8 */

UInt8 EncGetPasswordEntryType (void);
void  EncSetPasswordEntryType (UInt8 type);

#endif /* ENCCORE_H */
