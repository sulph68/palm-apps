/*
 * BitWarden.h - shared types and cross-file declarations. Resource IDs
 * live in BitWardenRsc.h (a separate, PalmOS.h-free header - pilrc can't
 * parse a real `#include <PalmOS.h>`).
 */
#ifndef BITWARDEN_H
#define BITWARDEN_H

#include <PalmOS.h>
#include "BitWardenRsc.h"
#include "enccore.h"	/* EncKeySchedule - see BwStoreBeginNameScan */

/* One fully-decrypted item, used to hand data from Store.c to RecordForm.c
 * (and from Sync.c into Store.c mid-sync, before encryption). Field
 * lengths match PROTOCOL.md's v1 per-field ceiling. bwMaxField/bwGuidLen
 * live in BitWardenRsc.h. */
/* Login/Card/Identity's fields live in a union, not a flat struct - only
 * one static instance is ever live at a time (RecordForm.c's
 * gCurrentItem, Sync.c's gSyncItem), and datares is tightly capped: a flat
 * struct with all three types side by side blew past the link budget. The
 * union brings each instance down to header + name + notes + the largest
 * single type's own fields (Identity's, ~2.3KB). */
typedef struct {
	char guid[bwGuidLen];
	UInt8 itemType;
	Boolean archived;	/* Options > Archived Items filter (MainForm.c) - own dedicated on-disk byte, see Store.c's bwArchivedByteOff */
	UInt16 folderIndex;
	UInt16 orgIndex;
	char name[bwMaxField];
	char notes[bwMaxField];
	union {
		struct {
			char username[bwMaxField];
			char password[bwMaxField];
			char uri[bwMaxField];
			char totp[bwMaxField];
		} login;
		/* PROTOCOL.md's Card sub-object - number is the sensitive field
		 * (masked behind Show, like a password). */
		struct {
			char holder[bwMaxExtField];
			char brand[bwMaxExtField];
			char number[bwMaxExtField];
			char expMonth[bwMaxExtField];
			char expYear[bwMaxExtField];
			char code[bwMaxExtField];
		} card;
		/* PROTOCOL.md's Identity sub-object - matches the real Bitwarden
		 * Identity item type's fields (confirmed against the live web
		 * vault UI). ssn/passportNumber/licenseNumber are the sensitive ones. */
		struct {
			char title[bwMaxExtField];
			char firstName[bwMaxExtField];
			char middleName[bwMaxExtField];
			char lastName[bwMaxExtField];
			char username[bwMaxExtField];
			char company[bwMaxExtField];
			char ssn[bwMaxExtField];
			char passportNumber[bwMaxExtField];
			char licenseNumber[bwMaxExtField];
			char email[bwMaxExtField];
			char phone[bwMaxExtField];
			char address1[bwMaxExtField];
			char address2[bwMaxExtField];
			char address3[bwMaxExtField];
			char city[bwMaxExtField];
			char state[bwMaxExtField];
			char postalCode[bwMaxExtField];
			char country[bwMaxExtField];
		} identity;
	} u;
} BwItemFull;

/* ---- Store.c: local at-rest store (enccore AES-128-CTR) --------------- */
void BwStoreInit(void);
void BwStoreClose(void);

/* True once a master password has ever been established locally (salt/
 * rounds/check value exist in prefs) - false only before the very first
 * successful sync. */
Boolean BwHasVault(void);

/* Derives the local key from `password`, verifies it against the stored
 * check value (or, on the very first unlock ever, establishes a fresh
 * salt/rounds/check value from this password). Caches the derived key
 * (own session cache, independent of Sec35's shared EncCacheKey/EncGetKey)
 * with the configured TTL. Returns false only for a genuine wrong-password
 * case against an already-established vault. */
Boolean BwUnlock(const char *password);
/* Renews the cached local key's TTL clock without re-deriving it. */
Boolean BwTouchLocalUnlock(void);
/* Renews the local-vault key's TTL, Sec35's shared key's TTL, and the
 * cached master password's TTL together - self-throttled to ~20s, safe to
 * call as often as once per second. Called from Sync.c during an active
 * sync's loop and from Serial.c's PollKeepAlive (runs during any blocking
 * wait for the next frame, including a stall). */
void BwTouchSessionKeys(void);
void BwResetVaultForNewPassword(void);
Boolean BwIsUnlocked(void);
void BwLock(void);
UInt32 BwGetTtlSeconds(void);
void BwSetTtlSeconds(UInt32 seconds);

/* Serial baud rate to WiRSa - must match WiRSa's own configured serial-
 * speed setting (its bauds[] in main.cpp). Index into the same 9-entry
 * list Preferences' popup shows (300/1200/2400/4800/9600/19200/38400/
 * 57600/115200); BwGetBaud() resolves an index to the real UInt32 rate
 * Serial.c passes to SrmOpen. */
UInt8 BwGetBaudIndex(void);
void BwSetBaudIndex(UInt8 idx);
UInt32 BwGetBaud(void);

/* Options > Security > 2FA (Dialogs.c's BwTwoFactorPrefsPromptRun /
 * BwTwoFactorPromptRun) - how the 2FA code field takes input. */
#define bwTwoFactorEntryText		0
#define bwTwoFactorEntryNumeric		1
UInt8 BwGetTwoFactorEntryMode(void);
void BwSetTwoFactorEntryMode(UInt8 mode);

/* Sync write path - BwStoreBeginSync builds into a separate temporary
 * database (this app always does a full resync, no incremental merge/edit
 * in v1), not the real one, so a cancelled/failed sync leaves every
 * previously-synced record intact. Prefs (server URL, pinned fingerprint,
 * salt/rounds/check, TTL) are untouched either way. Requires
 * BwIsUnlocked() already true. Wipes via a whole-database delete+recreate,
 * not per-record DmRemoveRecord, to avoid storage-heap fragmentation.
 * Returns false on the rare case where creating the temp database itself
 * fails - the caller must check this and abort the sync, since a false
 * return leaves no local database open: every later BwStoreAddItem call
 * would otherwise silently no-op while the Palm keeps acking items as if
 * saved. Exactly one of BwStoreEndSync (full success) or BwStoreAbortSync
 * (everything else) must be called before this sync attempt's own
 * BwSyncRun call returns. */
Boolean BwStoreBeginSync(void);
/* Each returns false if not actually stored (Sec35 or the local vault key
 * unavailable, or the write itself failed) - the caller (Sync.c) must
 * treat a false return as fatal and abort the whole sync
 * (BwStoreAbortSync), not keep acking items to WiRSa as if saved. Returns
 * true for the one deliberate non-error skip (an empty secure note - see
 * BwStoreAddItem), same as a real successful store. */
Boolean BwStoreAddFolder(UInt16 index, const char *name);
Boolean BwStoreAddOrg(UInt16 index, const char *name);
Boolean BwStoreAddItem(const BwItemFull *item);
/* Call only after a sync has fully succeeded (BW_SYNC_DONE) - replaces the
 * real database with the temp one BwStoreBeginSync built. */
void BwStoreEndSync(void);
/* Call on any sync failure/cancellation instead - discards the (possibly
 * partial) temp database and reopens the real one, untouched. */
void BwStoreAbortSync(void);
/* Call once per synced item (folder/org/cipher, any type) - internally a
 * no-op except every bwCompactEveryItems calls, when it proactively
 * defragments the storage heap. DmNewRecord can occasionally stall well
 * past a simple reset-the-auto-off-timer's ability to cover it, so this
 * keeps each individual call fast instead of trying to out-wait it after
 * the fact. */
void BwStoreCompactIfDue(void);

/* Read path - all require BwIsUnlocked() already true. */
UInt16 BwStoreCount(void);
UInt16 BwStoreFolderCount(void);
UInt16 BwStoreOrgCount(void);
Boolean BwStoreGetFolderName(UInt16 index, char *out, UInt16 outLen);
Boolean BwStoreGetOrgName(UInt16 index, char *out, UInt16 outLen);
/* Fast, no decrypt - folderIndex/orgIndex/itemType are stored plaintext in
 * the record header specifically so filtering doesn't require decrypting
 * every record. */
UInt16 BwStoreGetItemFolder(UInt16 recIndex);
UInt16 BwStoreGetItemOrg(UInt16 recIndex);
UInt8 BwStoreGetItemType(UInt16 recIndex);
/* Decrypts just the name field - used for list-row drawing (lazy, one row
 * at a time). */
Boolean BwStoreGetItemName(UInt16 recIndex, char *out, UInt16 outLen);
/* Decrypts every field - used when opening RecordForm on a single item. */
Boolean BwStoreGetItemFull(UInt16 recIndex, BwItemFull *out);

/* Raw-DB-index scanning helpers - MainForm.c's MainRebuildMatches only.
 * Every getter above takes a "recIndex" (Nth real item, skipping folder/
 * org markers) and pays an O(n) internal scan to resolve it each call -
 * fine for a single lookup, but calling one in a loop over every record
 * turns a full-table rebuild into O(n^2). A scanning caller should walk
 * BwStoreRawRecordCount() raw indices in one pass instead, tracking its
 * own running recIndex count, using these two to read each candidate
 * record exactly once. BwStoreGetHeaderByDbIndex returns false for
 * folder/org marker records (and any hole) - skip those. */
UInt16 BwStoreRawRecordCount(void);
Boolean BwStoreGetHeaderByDbIndex(UInt16 dbIdx, UInt8 *type, UInt16 *folder, UInt16 *org, Boolean *archived);
Boolean BwStoreGetItemNameByDbIndex(UInt16 dbIdx, char *out, UInt16 outLen);

/* Fast-path pair - internal to Store.c's own EnsureNameCache now (see
 * BwStoreGetCachedLowerName below), exposed for that one caller. Call
 * BwStoreBeginNameScan once before a scanning loop (false = Sec35 locked
 * or local vault key unavailable), then BwStoreGetItemNameFast per
 * candidate record. */
Boolean BwStoreBeginNameScan(EncKeySchedule *sec35Sched, EncKeySchedule *localSched);
Boolean BwStoreGetItemNameFast(UInt16 dbIdx, EncKeySchedule *sec35Sched, EncKeySchedule *localSched,
	char *out, UInt16 outLen);

/* Persistent (dynamic-heap) item cache for MainForm.c's own search/filter
 * loop - the name cache (crypto-heavy) and header cache (cheap) are kept
 * separate and each builds lazily on first use. recIndex-indexed like
 * every other public getter. Invalidate (BwStoreInvalidateNameCache, wipes
 * both) after anything that could change record content (Sync.c calls
 * this from BwStoreBeginSync) or on lock (BwLock calls this too). */
#define bwNameCacheEntryLen	48
Boolean BwStoreGetCachedLowerName(UInt16 recIndex, char *out, UInt16 outLen);
/* Direct pointer, read-only - skips the copy for the hottest search loop. */
const char *BwStoreGetCachedLowerNamePtr(UInt16 recIndex);
/* Original-case counterpart for row display - only ever returns cached
 * data (false if not built yet), unlike every other getter here. */
Boolean BwStoreGetCachedNameIfReady(UInt16 recIndex, char *out, UInt16 outLen);
Boolean BwStoreGetCachedHeader(UInt16 recIndex, UInt8 *type, UInt16 *folder, UInt16 *org, Boolean *archived);
UInt16 BwStoreGetCachedCount(void);
void BwStoreInvalidateNameCache(void);

/* ---- prefs (server URL / TOFU pin) ------------------------------------ */
void BwGetServerURL(char *out, UInt16 outLen);
void BwSetServerURL(const char *url);
void BwGetEmail(char *out, UInt16 outLen);
void BwSetEmail(const char *email);
Boolean BwHasPinnedFingerprint(void);
void BwGetPinnedFingerprint(UInt8 out[32]);
void BwSetPinnedFingerprint(const UInt8 fp[32]);
void BwClearPinnedFingerprint(void);

/* ---- serial framing: shared ../lib/relayproto.c --------------------- */
/* Framing lives in ../lib/relayproto.c (shared with CalDAV Sync); these
 * keep this app's historical names. Serial.c now only holds this app's
 * keep-alive hook (relay_cfg.h). gBwIdleOverrideTicks: 0 = default 180s
 * wait for the next frame; Sync.c shortens it once the first frame has
 * arrived. Reset to 0 by every BwSerialOpen. */
#include "relayproto.h"
#define BwSerialOpen		RpOpen
#define BwSerialClose		RpClose
#define BwSerialSetBaud		RpSetBaud
#define BwSerialSendFrame	RpSendFrame
#define BwSerialRecvFrame	RpRecvFrame
#define gBwLastRecvErr		gRpLastRecvErr
#define gBwLastLineErrs		gRpLastLineErrs
#define gBwIdleOverrideTicks	gRpIdleOverrideTicks

/* ---- Sync.c: orchestrates HELLO/TRUST/SYNC, TOFU prompt, progress UI -- */
/* Prompts for the master password if not already unlocked this session,
 * runs the full TRUST (if first time / fingerprint unset)+SYNC exchange
 * with progress UI, writes results into the store. Returns false if the
 * user cancels or the sync fails (an alert is already shown before
 * returning in the failure case - callers don't need to show their own). */
Boolean BwSyncRun(void);

/* Start/end sync chime - same 3-note sequences as CalDAV Sync's own
 * PlaySyncChime (caldav_sync/MainForm.c), matched note-for-note on the
 * user's own request. Callers bracket their own BwSyncRun() call (not
 * called from inside BwSyncRun itself - that function has several early
 * "prerequisite not met" returns before any real sync activity begins, e.g.
 * no server URL set, which shouldn't chime as if a sync actually started).
 * SEC001 - main .text has ~20 bytes free (BitWarden's own segment budget is
 * razor thin, see bw_seg.h), so this must not land there. */
void BwPlaySyncChime(Boolean isStart) SEC001;
/* Set by both SyncNowWithChime wrappers to the sync result; MainForm shows
 * "Bitwarden sync successful!" once the refreshed list is drawn, then clears it. */
extern Boolean gBwSyncOkPending;

/* Session-cached master password (Store.c) - persisted in prefs, encrypted
 * under the current Sec35 key, expires against this app's own "Lock
 * After" (BwGetTtlSeconds), not wiped on sleep/app-exit. Both MainOpen's
 * own local-unlock prompt and BwSyncRun's own prompt use this instead of
 * re-prompting once either one succeeds, as long as the TTL hasn't
 * elapsed. BwHasCachedPassword checks the cache/TTL without requiring
 * Sec35 to be unlocked (BwGetCachedPassword does) - true from
 * BwHasCachedPassword but false from BwGetCachedPassword specifically
 * means Sec35 is what's blocking this, the signal a caller needs to
 * decide to bounce to Sec35 instead of falling back to a full prompt. */
Boolean BwHasCachedPassword(void);
Boolean BwGetCachedPassword(char *outPassword, UInt16 outLen);
void BwSetCachedPassword(const char *password);
void BwWipeCachedPassword(void);

/* Persisted "remember this device for 2FA" token (PROTOCOL.md's SYNC/
 * SYNC_DONE) - unlike the cached password above, this survives app
 * restarts (encrypted in prefs under the Sec35 key alone, not this app's
 * own local-vault key). Sync.c reads this to populate SYNC's own
 * rememberedTwoFactorToken field, and calls BwSetTwoFactorRemember with
 * whatever SYNC_DONE's updatedTwoFactorToken comes back as (empty = leave
 * the stored value unchanged).
 *
 * bwTwoFactorRememberMax is sized to Vaultwarden's real TwoFactorToken
 * (a JWT, measured 596 bytes) - 608 is the largest value that still fits
 * the linker's 32KB datares cap, since this constant scales into three
 * separate static buffers. */
#define bwTwoFactorRememberMax	608
Boolean BwGetTwoFactorRemember(char *out, UInt16 outCap);
void BwSetTwoFactorRemember(const char *token);

/* ---- BitWarden.c: shared "device is genuinely awake" timestamp --------
 * EventLoop's own sleep-gap detector only samples wall-clock time once per
 * iteration of its own top-level EvtGetEvent loop - any code that blocks
 * on its own separate loop (a modal dialog waiting on typing, or Serial.c's
 * own polling during a long sync) never updates that sample until it
 * returns, so the entire blocking duration gets misread as sleep. Any loop
 * that blocks on its own EvtGetEvent - Dialogs.c's three modal dialogs,
 * Serial.c's PollKeepAlive - calls this each time it wakes, so EventLoop's
 * next gap check measures only genuine idle time. ------------------------ */
void BwTouchActivity(void);

/* Thin wrapper around EvtResetAutoOffTimer() - centralized here so
 * BwStoreAddItem's own keep-alive coverage (DmNewRecord can occasionally
 * stall 100+ seconds, likely storage-heap compaction) shares the exact
 * same call as Serial.c's own PollKeepAlive. EvtSetAutoOffTimer(SetAtLeast,
 * N) was tried instead on the theory an explicit floor would be more
 * robust - real-device testing showed the opposite (device slept sooner,
 * mid-PBKDF2-wait) - stick to EvtResetAutoOffTimer, the pattern already
 * proven in Address35/Sec35 for this same long-blocking-operation problem. */
void BwKeepDeviceAwake(void);

/* ---- BitwardenSec35.c: Sec35 install/passcode/bounce gate - mirrors
 * apps/cryptonote/CryptoSec35.c's own shape. Every stored field is wrapped
 * a SECOND time with the Sec35 key on top of this app's own existing
 * password-derived key (Store.c) - Sec35 does not replace it. ------------ */
Boolean BwSec35Installed(void);
void BwSwitchToSec35(void);
/* !EncKeyPresent() -> BwSwitchToSec35() + return true (caller must abort
 * whatever it was about to do); otherwise returns false and does nothing. */
Boolean BwSec35LockedBounce(void);

/* ---- Forms: frmMain / the four frmRecord[Login|Note|Card|Identity] forms
 * / frmPrefs are real navigational forms, registered the normal way
 * (FrmSetEventHandler from the app's own event loop) - the four record
 * forms (one per itemType) all share RecordFormHandleEvent, which
 * dispatches internally on FrmGetActiveForm().
 * frmUnlock/frmTrust are transient modal popups, run via their own
 * hand-rolled blocking event loop (Dialogs.c) instead - FrmDoDialog does
 * not reliably dispatch frmOpenEvent through a custom handler on this
 * SDK/glue (see lazyFTP's PromptForm.c, same finding). --------------------- */
Boolean MainFormHandleEvent(EventPtr e);
/* True while a debounced search fire is still waiting (MainForm.c's own
 * keyDownEvent/nilEvent handling) - BitWarden.c's EventLoop uses this to
 * decide whether it needs a bounded EvtGetEvent timeout right now, since
 * evtWaitForever would otherwise starve the nilEvent that debounce timer
 * depends on. */
Boolean MainHasPendingSearch(void);
/* Same shape/reasoning as MainHasPendingSearch above, for frmRecordLogin's
 * own live TOTP countdown (RecordForm.c). */
Boolean RecordHasTotpActive(void);
Boolean RecordFormHandleEvent(EventPtr e);
Boolean PrefsFormHandleEvent(EventPtr e);

/* Wipes RecordForm.c's own already-decrypted gCurrentItem (the real
 * password/SSN/card number behind the Show toggle, plaintext in RAM the
 * moment a record is opened) - called from BitWarden.c's sleep-wipe
 * paths. Neither BwWipeCachedPassword nor Sec35's own key wipe touches this. */
void BwRecordDataWipe(void);

/* Dialogs.c - transient modal popups, blocking "Run" style like lazyFTP's
 * PromptFormRun. outPassword is zeroed by the caller when no longer
 * needed - these functions themselves don't retain a copy past return. */
Boolean BwUnlockPromptRun(char *outPassword, UInt16 outLen);
Boolean BwTrustPromptRun(const UInt8 fingerprint[32]);
void BwFingerprintViewRun(const UInt8 fingerprint[32]) SEC001;	/* SEC001 required here - see Dialogs.c */
/* Vaultwarden 2FA challenge (PROTOCOL.md's TWO_FACTOR_REQUIRED) -
 * providerLabel is whatever WiRSa/mock sent (e.g. "Authenticator app
 * code"), shown above the code entry field. Not masked - see BitWardenRsc.h. */
Boolean BwTwoFactorPromptRun(const char *providerLabel, char *outCode, UInt16 outLen);
/* Options > Security > 2FA - lets the user pick BwGetTwoFactorEntryMode's
 * value (text field vs numeric keypad) for the code field above. SEC001
 * (2026-09-30) to free main-segment room for the serial-link encryption
 * hook; only OS traps and cross-file calls, reached only from the menu. */
Boolean BwTwoFactorPrefsPromptRun(void) SEC001;
void BwProgressShow(const char *title);
void BwProgressSetStatus(const char *status);
void BwProgressBar(UInt16 done);	/* kept main-segment, not SEC001 - see Dialogs.c */
void BwProgressHide(void) SEC001;	/* SEC001 required here - see Dialogs.c */
/* Cancel button on the sync progress dialog - see Dialogs.c's comment on
 * BwProgressNotePenUp for the full mechanism. BwSyncRun (Sync.c) polls
 * BwProgressCancelled() at each safe checkpoint and aborts cleanly if set.
 * SEC001 tags required here too (Dialogs.c tags the real definitions). */
void BwProgressNotePenDown(Int16 x, Int16 y) SEC001;
void BwProgressNotePenUp(Int16 x, Int16 y) SEC001;
Boolean BwProgressCancelled(void) SEC001;

/* Set by MainForm before opening the right frmRecord* form for this item's
 * type (index into the store's current filtered/visible list is resolved
 * to a real record index first). */
extern UInt16 gRecordIndex;

#endif
