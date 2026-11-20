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
#include "sects.h"		/* ENCCORE_SECTION: main by default; a segment in Address35 */

/* Per-store key code: same section as the rest of enccore unless the host app
 * says otherwise (Datebook35 puts it in sec004 -- its sec003 is full). */
#ifndef ENCSTORE_SECTION
#define ENCSTORE_SECTION ENCCORE_SECTION
#endif

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
UInt16 EncDeviceSecret (UInt8* out, UInt16 maxLen) ENCCORE_SECTION;

/* Best-effort random bytes (for salts / the fallback device key). */
void EncRandomBytes (UInt8* buf, UInt16 len) ENCCORE_SECTION;

/* Mix unpredictable input (e.g. keystroke timing and pen position) into the
 * randomness pool EncRandomBytes draws from (unsaved prefs). */
void EncAddEntropy (const void* data, UInt16 len) ENCCORE_SECTION;

/* Derive a 16-byte key = stretch( passcode || deviceSecret || salt , rounds ).
 * Deterministic for the same inputs on the same device. */
void EncDeriveKey (const char* passcode, const UInt8 salt[ENC_SALT_BYTES],
                   UInt16 rounds, UInt8 keyOut[ENC_KEY_BYTES]) ENCCORE_SECTION;

/* Number of stretch iterations that take ~targetMs on this device (>=1). */
UInt16 EncCalibrateRounds (UInt16 targetMs) ENCCORE_SECTION;

/* AES-128-CTR, in place. Encrypt and decrypt are the same call. 'nonceHi:nonceLo'
 * seed the top 8 bytes of the 128-bit counter (low 8 bytes start at 0). */
void EncCrypt (const UInt8 key[ENC_KEY_BYTES], UInt8* buf, UInt32 len,
               UInt32 nonceHi, UInt32 nonceLo) ENCCORE_SECTION;

/* ---- key-schedule cache, for hot loops reusing the SAME key many times -- */

/* Every EncCrypt call above recomputes a full AES-128 key schedule from
 * scratch (AES_init_ctx_iv), even when the SAME key is about to be used
 * again immediately with only the nonce differing - fine for a one-off
 * decrypt, but a real cost in a loop over many records with the same two
 * keys (found 2026-08: BitWarden's search-title match pass was doing this
 * 2x per record, up to 1560 unnecessary key schedules for one keystroke
 * over a 780-item vault, on top of the actual encryption work). This pair
 * lets a caller build the key-dependent part ONCE and reuse it across many
 * EncCryptFast calls that only vary the nonce/data - same AES-128-CTR
 * result as EncCrypt, just without redoing the key schedule every time.
 * Opaque (sized to fit aes.h's struct AES_ctx, enforced by a compile-time
 * check in enccore.c) so callers don't need to know AES internals. */
#define ENC_KEY_SCHEDULE_BYTES	192
typedef struct { UInt8 opaque[ENC_KEY_SCHEDULE_BYTES]; } EncKeySchedule;

void EncKeyScheduleInit (EncKeySchedule *sched, const UInt8 key[ENC_KEY_BYTES]) ENCCORE_SECTION;
void EncCryptFast (EncKeySchedule *sched, UInt8* buf, UInt32 len,
                    UInt32 nonceHi, UInt32 nonceLo) ENCCORE_SECTION;
void EncKeyScheduleWipe (EncKeySchedule *sched) ENCCORE_SECTION;

/* ---- session-key cache (shared RAM, not in any HotSync backup) ---------- */

/* Store {key, unlockTime = now} in a persistent-RAM feature. Replaces any
 * existing cached key. */
Err EncCacheKey (const UInt8 key[ENC_KEY_BYTES]) ENCCORE_SECTION;

/* If a key is cached AND (now - unlockTime) <= ttlSeconds AND now >= unlockTime
 * (no clock rollback), copy it out and return true; otherwise wipe it and
 * return false. */
Boolean EncGetKey (UInt8 keyOut[ENC_KEY_BYTES], UInt32 ttlSeconds) ENCCORE_SECTION;

/* Read-only peek at how many seconds are left before EncGetKey/EncKeyPresent
 * would consider the cached key expired under ttlSeconds -- -1 if no key is
 * cached or it's already expired. Never mutates/wipes anything, safe to call
 * purely for UI display (a countdown) on every redraw. */
Int32 EncSecondsRemaining (UInt32 ttlSeconds) ENCSTORE_SECTION;	/* Sec35 only; off a full main */

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
Boolean EncKeyPresent (void) ENCCORE_SECTION;

/* Zero and free the cached key. */
void EncWipeKey (void) ENCCORE_SECTION;

/* ---- shared auto-lock TTL (Sec35's Auto Lock Handheld setting) ---------- *
 * Published cross-app the same way as EncGetPasswordEntryType/
 * EncSetPasswordEntryType (UNSAVED prefs, not HotSync-backed): Sec35 is the
 * only writer (on load and whenever the setting changes), every app's
 * EncKeyPresent call reads it to self-check the cached key's real age. Not a
 * secret -- same category of value as the salt, safe to store in the clear.
 * 300 (5 minutes) if never published, matching Sec35's own gPref default. */
#define ENC_TTL_PREF	3	/* unsaved pref holding a single UInt32: seconds */

UInt32 EncGetTtlSeconds (void) ENCCORE_SECTION;
void   EncSetTtlSeconds (UInt32 seconds) ENCCORE_SECTION;

/* ---- lock on sleep (Sec35's "Lock on Power-Off" checkbox) --------------- *
 * Published the same way as the TTL/password-entry-method prefs above.
 * Whether the cached key should be wiped immediately when the device is about
 * to sleep/power off, rather than waiting for the TTL to elapse. Off by
 * default (0) -- matches pre-existing behavior (TTL-only) if never set. */
#define ENC_LOCKONSLEEP_PREF	4	/* unsaved pref holding a single UInt8: 0/1 */
#define ENC_PWSET_PREF		5	/* unsaved pref holding a single UInt8: a passcode is set (0/1) */
#define ENC_POOL_PREF		6	/* unsaved pref: 16-byte randomness pool (EncRandomBytes) */

Boolean EncGetLockOnSleep (void) ENCCORE_SECTION;
void    EncSetLockOnSleep (Boolean on) ENCCORE_SECTION;

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
void EncRegisterSleepWipe (void) ENCCORE_SECTION;
void EncUnregisterSleepWipe (void) ENCCORE_SECTION;

/* ---- "a passcode is set" signal (survives lock; Sec35 owns it) ---------- *
 * Independent of the cached key: true whenever encryption is ACTIVE for the suite,
 * even while locked and even if no record is encrypted yet. Sec35 asserts it on
 * launch + on set/clear; the era apps read it to decide whether to prompt+seal. */
void EncSetEncryptionActive (Boolean on) ENCCORE_SECTION;
Boolean EncEncryptionActive (void) ENCCORE_SECTION;

/* ---- password entry method (Sec35's Preferences > Password Type) -------- *
 * Purely a UI input choice (masked Text Entry vs. numeric keypad), shared
 * cross-app via the UNSAVED prefs area (PrefGetAppPreferences saved=false --
 * same mechanism as EncDeviceSecret's fallback key, NOT HotSync-backed) so
 * any app offering password entry can match whichever style the user picked
 * in Sec35, without needing Sec35 to be running or depending on its full
 * internal Sec35Pref struct layout. Sec35 is the only writer; other apps
 * only ever read. kPwTypeText is the correct default whenever nothing has
 * been set yet (fresh device, Sec35 never launched). */
enum { kPwTypeText, kPwTypeNumeric };
#define ENC_PWTYPE_PREF	2	/* unsaved pref holding a single UInt8: kPwTypeText/kPwTypeNumeric */

UInt8 EncGetPasswordEntryType (void) ENCCORE_SECTION;
void  EncSetPasswordEntryType (UInt8 type) ENCCORE_SECTION;


/* ======================================================================== *
 * Per-store keys (key scheme 2)
 *
 * The Sec35 key (derived from the passcode) is never used to encrypt data
 * directly any more. Each STORE -- one set of encrypted data, named for the
 * data, not for the app reading it -- gets its own key:
 *     storeKey = AES-128-ECB(SecKey, "S35K" || storeId || 00 02 || zero pad)
 * so two stores can use the same nonce numbering without sharing a keystream
 * (Address35 uid 5 and ToDo35 uid 5, lazyFTP slot 0 and CalDAV slot 0, the
 * password check value). Every reader of a store asks for THAT store's key
 * (Datebook35 reading ToDo35 records uses ENC_STORE_TD35).
 *
 * Scheme 1 (the old one) encrypted everything with the Sec35 key itself. A
 * database still in scheme 1 is told apart by its header version field: the
 * owner sets ENC_DBVER_STOREKEYS once it has converted it (EncDbSetStoreKeys).
 * ======================================================================== */

#define ENC_SCHEME			2

#define ENC_STORE_SEC35V	'S35v'	/* Sec35's own password check value       */
#define ENC_STORE_A35		'Ad35'	/* Address35DB records                    */
#define ENC_STORE_TD35		'Td35'	/* ToDo35DB records                       */
#define ENC_STORE_DB35		'Db35'	/* Datebook35DB records                   */
#define ENC_STORE_DBLOC		'DbLk'	/* Datebook35 AppointmentLocationDB       */
#define ENC_STORE_DBLHIST	'DbLh'	/* Datebook35 LocationsDB (Previous Loc)  */
#define ENC_STORE_DAVEV		'CDev'	/* DateBook35DAV (CalDAV events)          */
#define ENC_STORE_DAVCON	0x43446300UL	/* 'CDc' + slot (0..): Address35DAV_<slot+1> */
#define ENC_STORE_CRNT		'CrNt'	/* CryptoNoteDB (Sec35 layer)             */
#define ENC_STORE_LZTP		'LzTP'	/* lazyFTP saved passwords                */
#define ENC_STORE_CDDV		'CDps'	/* CalDAVRelayDB secrets + settings       */
#define ENC_STORE_BW		'BWrd'	/* BitWarden: vault outer layer, cached master
								 * password, 2FA token (disjoint nonceHi ranges) */

/* Header version bit meaning "this database is in key scheme 2". Only the top
 * bit is set; the low bits are kept (CryptoNoteDB uses them: version 37, and
 * CryptoNote only re-initialises when it is below 31, so 0x8025 is safe; the
 * other suite databases had 0). Survives HotSync and Bkp1 backups (it is part
 * of the PDB header). */
#define ENC_DBVER_STOREKEYS	0x8000

/* The key for one store, from the CACHED Sec35 key (false if locked or the
 * auto-lock time has passed -- same rule as EncKeyPresent). Derived keys are
 * cached beside the Sec35 key in the same feature block and wiped with it, so
 * a per-record caller costs no more than EncGetKey did. Same argument shape
 * as EncGetKey (key, UInt32) so swapping a call costs no code bytes. */
Boolean EncGetStoreKey (UInt8 keyOut[ENC_KEY_BYTES], UInt32 storeId) ENCSTORE_SECTION;

/* Derive a store key from an explicit Sec35 key (rekey handlers: they hold
 * the old and the new Sec35 key, neither of which is the cached one). */
void EncDeriveStoreKey (const UInt8 secKey[ENC_KEY_BYTES], UInt32 storeId,
                        UInt8 keyOut[ENC_KEY_BYTES]) ENCSTORE_SECTION;

/* Scheme marker of an open database (true = scheme 2). */
Boolean EncDbHasStoreKeys (DmOpenRef db) ENCSTORE_SECTION;
void    EncDbSetStoreKeys (DmOpenRef db) ENCSTORE_SECTION;

/* The same marker by database ID (card 0), for code that has not opened it. */
Boolean EncDbIdHasStoreKeys (LocalID id) ENCSTORE_SECTION;
void    EncDbIdSetStoreKeys (LocalID id) ENCSTORE_SECTION;

/* For a reader of ANOTHER app's database: the store key, but only if that
 * database is already in scheme 2 (false = treat as locked; the owner app or
 * Sec35 converts it). */
Boolean EncGetStoreKeyDb (UInt8 keyOut[ENC_KEY_BYTES], UInt32 storeId,
                          DmOpenRef db) ENCSTORE_SECTION;

/* ---- rekey hand-off: Sec35 -> each app (sublaunch with new globals) ---- *
 * Sec35 never opens another app's database. For a password set / change /
 * removal / forget, and for the one-time scheme 1 -> 2 conversion, it
 * sublaunches every installed suite app with kSecLaunchRekey and this block;
 * the app re-encrypts its OWN stores with its own code and writes `result`.
 * Keys are Sec35 keys (not store keys); the app derives its store keys. */
#define kSecLaunchRekey		0x9001

enum {
	kRekeySet = 1,		/* no password before: encrypt plaintext with newKey     */
	kRekeyChange,		/* oldKey -> newKey                                     */
	kRekeyRemove,		/* oldKey -> plaintext (stored passwords are cleared)   */
	kRekeyWipe,			/* forgotten: delete what is encrypted, no key          */
	kRekeyConvert		/* scheme 1 -> 2 under the same key (oldKey == newKey)  */
};

enum {
	kRekeyResultNone = 0,	/* app did not answer (not a suite app / old version) */
	kRekeyResultDone,
	kRekeyResultLowMem,		/* stopped cleanly; ask again after freeing memory    */
	kRekeyResultFailed
};

typedef struct EncRekeyParamTag {
	UInt16	op;						/* kRekeyXxx                                 */
	UInt16	result;					/* written by the app                        */
	UInt32	opSerial;				/* same value when Sec35 resumes the SAME op */
	UInt8	oldKey[ENC_KEY_BYTES];	/* zero when unused                          */
	UInt8	newKey[ENC_KEY_BYTES];
	RectangleType progress;			/* screen rect the app may fill as a bar     */
} EncRekeyParam;

/* Set by an app before it bounces to Sec35 because one of its databases is
 * still in scheme 1: Sec35 then runs the conversion before returning. */
#define kSecConvertFtr		1003

#endif /* ENCCORE_H */
