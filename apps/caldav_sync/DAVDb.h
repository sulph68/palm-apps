#ifndef CDDV_DAVDB_H
#define CDDV_DAVDB_H

#include "CalDAVSync.h"

#define cddvDavDbName "DateBook35DAV"
#define cddvDavDbType 'DATA'

/* Must match apps/datebook35/Src/DateDAV.h's kDateDavNotesLen exactly -
 * both apps mirror this struct byte-for-byte, see DateDAV.c's own comment. */
#define cddvNotesLen 200

/* One synced calendar-event occurrence. utcStart/utcEnd are the permanent
 * canonical anchor (Palm-epoch UTC seconds), never touched after write.
 * For an all-day event utcStart/utcEnd hold its DATE at 00:00 with no zone
 * (not a UTC instant) and localStart/localEnd are copied unchanged.
 * localStart/localEnd are derived ONCE at write time via TimUTCToTimeZone
 * and are what Datebook35's reader displays directly; only
 * DAVDbRecomputeLocalTimes() ever rewrites them later, against the current
 * device timezone. notes/isRepeating are appended after the original
 * fields (not interleaved) to keep every byte-for-byte mirror/reader of
 * this struct stable. */
typedef struct {
	char uid[48];
	char alias[cddvAliasLen];		/* denormalized snapshot, refreshed every full-rebuild sync */
	char title[64];
	char location[64];
	UInt32 utcStart;
	UInt32 utcEnd;
	UInt32 localStart;
	UInt32 localEnd;
	Boolean allDay;
	UInt8 sourceSlot;
	UInt8 marker;					/* Datebook35's future read-only-detection tag */
	char notes[cddvNotesLen];		/* VEVENT's DESCRIPTION, truncated - may be empty */
	Boolean isRepeating;			/* master VEVENT had an RRULE (WiRSa's own determination) */
	/* Appended (2026-09-24, user request) - uid/alias/title/location/notes
	 * above are Sec35 ciphertext at rest, encrypted once at
	 * DAVDbCommitStaged time (not DAVDbAddEvent - see that function's
	 * own comment for why the record's FINAL real-database DmRecordInfo
	 * uniqueID, not its temp-staging one, has to be the nonce). AES-CTR
	 * ciphertext isn't NUL-safe, so each needs an explicit plaintext-length
	 * companion, same as every other Sec35-encrypted field in this suite.
	 * Datebook35's own DateDAV.c mirrors these too, same as every other
	 * field here. */
	UInt16 uidLen;
	UInt16 aliasLen;
	UInt16 titleLen;
	UInt16 locationLen;
	UInt16 notesLen;
} CDDVEvent;

/* Field-index nonce components for the ciphertext fields above - EncCrypt's
 * nonce is (this record's real DmRecordInfo uniqueID, one of these) for
 * whichever field, mirroring DateDB.c's own established (uniqueID, 0)=
 * description / (uniqueID, 1)=note convention for encrypted appointments.
 * DateDAV.c's own independent reader mirrors these exact values locally -
 * it can't #include this header (see that file's own comment). */
#define cddvEvFieldUid      0
#define cddvEvFieldAlias    1
#define cddvEvFieldTitle    2
#define cddvEvFieldLocation 3
#define cddvEvFieldNotes    4

/* Per-SLOT rebuild, not whole-database: a whole-DB temp-then-rename swap
 * (BitWarden Store.c's pattern) is wrong here, because "Sync Now" only
 * re-fetches ONE slot - swapping in a temp DB with just that slot's events
 * would wipe every other slot's already-synced events. "Sync All" is just
 * this same per-slot primitive called once per slot.
 *
 * DAVDbBeginSlotRebuild stages fresh events into a per-slot temp database
 * via DAVDbAddEvent - the real database is never touched until
 * DAVDbCommitStaged. On full success DAVDbStageSlotRebuild keeps the temp
 * DB; DAVDbCommitStaged then, for every slot staged since it last ran,
 * deletes that slot's old records and copies in the staged new ones, and
 * sorts the database once. Sync All stages each calendar and commits only
 * after its last one, once the WiRSa connection is closed (see DAVDb.c).
 * DAVDbCommitSlotRebuild is stage + commit in one call.
 * DAVDbAbortSlotRebuild only ever discards the temp DB, so a sync that
 * fails outright leaves the slot's existing data untouched instead of
 * erased. */
Err  DAVDbBeginSlotRebuild(UInt8 slotIdx) SEC003;
Err  DAVDbAddEvent(const CDDVEvent *ev) SEC003;
void DAVDbStageSlotRebuild(void) SEC003;
void DAVDbCommitStaged(void) SEC003;
void DAVDbCommitSlotRebuild(void) SEC003;
void DAVDbAbortSlotRebuild(void) SEC003;

void DAVDbDeleteAll(void) SEC003;

/* True if DateBook35DAV holds any event of slot slotIdx (sourceSlot is not
 * encrypted) - lets the stale-data cleanup skip slots with nothing to remove. */
Boolean DAVDbSlotHasRows(UInt8 slotIdx) SEC003;

/* Iterates every record, redoes ONLY localStart/localEnd via
 * TimUTCToTimeZone against the CURRENT prefTimeZone/
 * prefDaylightSavingAdjustment (all-day events keep their date) -
 * uid/title/location/utcStart/utcEnd untouched, then re-sorts by
 * localStart. Shows an A35ProgressBegin/A35ProgressBar/A35ProgressEnd-style
 * progress bar for the duration. */
void DAVDbRecomputeLocalTimes(void) SEC003;
/* The same busy toast, for other long jobs (Re-Sort Contacts). */
void DAVDbToast(const char *text) SEC003;
void DAVDbToastEnd(void) SEC003;

#endif
