/*
 * ContactDb.h - the per-slot contacts databases (CardDAV / iCloud Contacts
 * slots). Design: apps/carddav_sync.md §3.4. Read by Address35 (a separate,
 * hand-kept reader - keep the layout below in step with it).
 *
 * Database "Address35DAV_<n>" (n = slot number 1..8), type 'DATA', creator
 * CalDAV Sync's - never 'Ad35', so CatSync (lib/catsync.c) never picks one.
 * Built as "Address35DAV_t<n>" and swapped in only after a successful sync.
 *
 * Record (version 2):
 *   [0] version (2)  [1] flags (bit 0: text encrypted, bit 1: list value
 *   encrypted, bit 2: list value empty but the text has IMPP / X-SOCIALPROFILE /
 *   X-WHATSAPP / X-TELEGRAM / X-SIGNAL lines)  [2] list value length n  [3..] list value (first phone,
 *   else first email; max 32; Sec35, nonce (cddvContactListNonce, record
 *   uniqueID)) - the one value Address35's list shows right of the name,
 *   decrypted per visible row;
 *   then three sort names, readable, each u8 length + bytes (max 63):
 *   last, first (N components 0 and 1), company (ORG component 0) - the
 *   keys Address35's own comparator uses;
 *   then the CDDV_CONTACT_ITEM body exactly as WiRSa sent it (caldav_relay.cpp):
 *   uid str, displayName str, sortName str, nDates u8,
 *   {year u16, month u8, day u8, kind u8, name str}*nDates,
 *   nCategories u8, {str}*nCategories, text str
 * (str = u16 big-endian length + bytes, Palm Latin). Names and dates stay
 * readable, like Address35's own clear fields, so lists sort and Datebook35
 * can find birthdays without decrypting; only the text (phones, emails,
 * addresses, notes...) is Sec35 AES-CTR, nonce (cddvContactNonce, record
 * uniqueID). Records are sorted the way Address35 sorts its own: primary
 * and secondary key from Address35DB's AppInfo (primarySort/secondarySort),
 * a missing key first, caseless then exact - after every contacts sync and
 * on Record > Re-Sort Contacts (ContactDbResort).
 *
 * AppInfo: AppInfoType (standard categories) then
 *   u8 version (1), u8 slotType (CDDVServerType), u8 stale (0/1),
 *   u8 aliasLen, alias[cddvAliasLen] (Sec35, nonce (cddvContactAliasNonce,
 *   slot index 0..7)).
 * Categories: the server's groups (iCloud group cards) and CATEGORIES
 * values, up to 15 named in alphabetical order (index 0 = Unfiled); each
 * contact goes under the alphabetically first one it belongs to (record
 * attribute category bits), else Unfiled. Its own database, so never mixed
 * with Address35's local categories.
 * Stale (set when the slot stops being a contacts slot): readers hide the
 * database; the next sync's cleanup (ContactDbCleanup) deletes it.
 *
 * "Address35DAVBDA": birthdays and anniversaries of every non-stale
 * Address35DAV_<n>, rebuilt whenever one changes. 12 records, record m-1 =
 * month m: u16 count, then count entries in day order:
 *   day u8, kind u8 (0 birthday, 1 engagement, 2 remembrance, 3 wedding,
 *   4 work, 5 other), year u16 (0 = unknown), slot u8 (0..7: database
 *   Address35DAV_<slot+1>), record u16 (index in it), nameLen u8 + name
 *   (display name, max 31), otherLen u8 + other (kind 5: the anniversary's
 *   own name, max 31). Not encrypted (names and dates only, like
 *   Address35's own clear fields). Read by Datebook35.
 */
#ifndef CONTACTDB_H
#define CONTACTDB_H

#include "CalDAVSync.h"

#define cddvContactDbPrefix		"Address35DAV_"
#define cddvContactBdaName		"Address35DAVBDA"
#define cddvContactDbType		'DATA'
#define cddvContactRecVersion	2
#define cddvContactFlagEncText	0x01
#define cddvContactFlagEncList	0x02
#define cddvContactFlagSocial	0x04		/* no phone/email, but IM / social handles (Phone Lookup lists it) */
#define cddvContactListMax		32
#define cddvContactKeyMax		63
#define cddvContactListNonce	0x4C495354UL	/* 'LIST' */
#define cddvContactNonce		0x43415244UL	/* 'CARD' */
#define cddvContactAliasNonce	0x414C4941UL	/* 'ALIA' */

/* Start a fresh temp database for slotIdx (0..7). */
Err     ContactDbBegin(UInt8 slotIdx) SEC003;
/* Store one CDDV_CONTACT_ITEM body; false if malformed or out of memory. */
Boolean ContactDbAdd(const UInt8 *body, UInt16 len) SEC003;
/* Keep one CDDV_GROUP_ITEM body until commit (it becomes a category). */
Boolean ContactDbAddGroup(const UInt8 *body, UInt16 len) SEC003;
/* Successful sync: write the info block and close the temp database; it
 * replaces Address35DAV_<n> at the next ContactDbCommitStaged (after the
 * link to WiRSa is closed - sorting a big book takes seconds, longer than
 * WiRSa waits between Sync All slots; same reason as DAVDbCommitStaged). */
Err     ContactDbStage(UInt8 slotType, const char *aliasPlain) SEC003;
/* For every staged temp database: categories from its groups, sort, swap
 * it in; then rebuild Address35DAVBDA. No-op if nothing is staged. */
void    ContactDbCommitStaged(void) SEC003;
/* Record > Re-Sort Contacts: sort every Address35DAV_<n> by Address35's
 * current sort preference, then rebuild Address35DAVBDA. */
void    ContactDbResort(void) SEC003;
/* Drop the temp database (failed or cancelled sync). */
void    ContactDbAbort(void) SEC003;
/* Delete slot slotIdx's contacts database (Clear DB, Delete slot), then
 * rebuild Address35DAVBDA. */
void    ContactDbDelete(UInt8 slotIdx) SEC003;
/* Slot slotIdx stopped being a contacts slot (Save, Duplicate overwrite):
 * flag its database stale and drop it from Address35DAVBDA. */
void    ContactDbMarkStale(UInt8 slotIdx) SEC003;
/* Start of every Sync Now / Sync All: delete the contacts database of a
 * slot that is empty or no longer a contacts slot, and the DateBook35DAV
 * rows of a slot that is empty or now a contacts slot - even if the sync
 * then fails, since that data is wrong either way. */
void    ContactDbCleanup(const CDDVPrefsType *p) SEC003;

#endif
