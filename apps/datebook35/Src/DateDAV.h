/*
 * DateDAV.h - read CalDAV Sync's synced calendar events for the Agenda.
 *
 * Datebook35 can't link CalDAV Sync's DB layer, so this is a tiny,
 * self-contained reader over DateBook35DAV.pdb's fixed-size record format -
 * same shape as AddrBday.h's own Address35 reader. Keep the mirrored struct
 * in DateDAV.c in sync with apps/caldav_sync/DAVDb.h's CDDVEvent.
 */
#ifndef DATEDAV_H
#define DATEDAV_H

#include <PalmOS.h>

/* "\x95 Alias - Title @ Location" - glyph (stdFont 0x95) + a space + slot
 * alias + " - " + title + " @ " + location if present. Long enough for the worst case
 * (CDDVEvent's own field caps: alias[24] + title[64] + location[64], plus
 * the glyph/separators) with room to spare - no longer pre-truncated to a
 * single visible line here (that was the ORIGINAL design mistake this
 * macro/comment fixes): Agenda still clips to one line at DRAW time
 * (WinDrawTruncChars, same as it already does for a real appointment's
 * description), but Day view wraps this to multiple lines exactly like a
 * real appointment's own description (FldCalcFieldHeight + textTableItem),
 * so pre-truncating here would have clipped text Day view could have shown
 * in full. */
#define kDateDavLineLen 192
#define kDateDavGlyph	0x95

/* Must match apps/caldav_sync/DAVDb.h's cddvNotesLen exactly - both apps
 * mirror CDDVEvent byte-for-byte (see the struct comment in DateDAV.c). */
#define kDateDavNotesLen 200

/* lines[] may be NULL - Month view only ever needs the times (to draw a
 * spanning bar, or set an untimed-day flag), never the text, so it
 * shouldn't need a kDateDavLineLen-sized per-day scratch buffer at all
 * (mattered for real - see DateMonth.c's own comment on the "region
 * datares is full" link error this avoided).
 *
 * Fill up to maxLines "\x95Alias - Title[ @ Location]" strings, plus each
 * event's own start/end TimeType (hour==-1, PalmOS's standard "untimed"
 * sentinel every ApptInfoType row already uses, for an all-day event) -
 * the caller needs the raw times too, not just display text, so a timed
 * DAV event can be inserted at its correct chronological position among
 * real appointments rather than always being dumped as an untimed row.
 * Matches every DateBook35DAV event whose LOCAL day (already timezone-
 * resolved at sync time - see DAVDb.h's own comment) equals (year, month,
 * day) - year is the agenda's TRUE full year (matches AddrBdayGet's own
 * convention). Returns the number written; 0 if CalDAV Sync was never
 * installed/synced (DateBook35DAV.pdb doesn't exist) or there's nothing
 * that day - a silent no-op either way, never an error.
 *
 * uidHashes[] (may be NULL if the caller doesn't need it, e.g. Agenda's
 * own appointment table never diffs rows) - a stable-per-event UInt32
 * derived from the event's own uid[48] string (DAVDb.h's CDDVEvent),
 * NOT a record index into anything, since DateDAVGet's whole point is
 * that Datebook35 never touches CalDAV Sync's DB layer directly. Needed
 * by any caller that does table-row diffing across redraws (Day/Week
 * view's own uniqueID-per-row convention, same idiom as
 * gDayTodoUID/gDayBdayUID in DateDay.c) - Agenda's table just fully
 * redraws every row, so it has never needed this. */
/* isRepeating[] (may be NULL, same convention as uidHashes[]) - whether the
 * SOURCE VEVENT's master had an RRULE (WiRSa's own determination, carried
 * through the wire/DB unchanged) - Day view's row icon is the only current
 * consumer.
 *
 * hasNotes[] (may be NULL, same convention) - whether the synced record's
 * notes field is non-empty. Free to compute (the full CDDVEvent, notes
 * included, is already read into memory for every other field this
 * function returns - no extra I/O), used by Day view to decide whether to
 * reserve/draw a note icon on the row, same as a real appointment's own
 * note icon. */
UInt16 DateDAVGet(UInt16 year, UInt8 month, UInt8 day,
	char lines[][kDateDavLineLen], TimeType startTimes[], TimeType endTimes[],
	UInt32 uidHashes[], Boolean isRepeating[], Boolean hasNotes[], UInt16 maxLines) SEC003;

/* Full-detail lookup for ONE specific occurrence, by (year, month, day) +
 * the same uidHash DateDAVGet's own uidHashes[] output already produces -
 * unambiguous because a single UID never produces two occurrences on the
 * same calendar day. Used only by Day view's read-only Details dialog
 * (DateDavDetails.c) at TAP time, not on every redraw - a live re-scan is
 * cheap here (one user-initiated lookup) vs. permanently growing every
 * view's own per-row cache with fields (notes, especially) most rows never
 * need. Any of the char* out params may be NULL if the caller doesn't need
 * that field. Returns false if no matching record was found (CalDAV Sync
 * uninstalled, or the record was deleted by a sync that ran between the
 * row being drawn and the tap). */
Boolean DateDAVGetOne(UInt16 year, UInt8 month, UInt8 day, UInt32 uidHash,
	char *alias, UInt16 aliasCap, char *title, UInt16 titleCap,
	char *location, UInt16 locationCap, char *notes, UInt16 notesCap,
	TimeType *startOut, TimeType *endOut, Boolean *allDayOut,
	Boolean *isRepeatingOut) SEC003;

/* DateBook35DAV.pdb's own modificationNumber, or 0 if it doesn't exist -
 * the same result-cache key DateMonth.c's own MonthScanAndDrawCalDav
 * already uses, factored out here (2026-09-17) so Week view's own cache
 * (DateWeek.c) doesn't need its own copy of the "DateBook35DAV" string
 * literal + DmOpenDatabaseInfo/DmDatabaseInfo dance - real bug found while
 * adding it: that copy alone pushed sec002res over its 32KB cap. Callers
 * in ANY segment can use this instead of duplicating the dance (DateWeek.c
 * already calls other DateDAV.c/sec003 functions cross-segment today, so
 * this adds no new risk). */
UInt32 DateDAVGetModNum(void) SEC003;

/* "CalDAV Sync" Options-menu item (DateDay.c) - plain switch-to-app bounce,
 * see DateDAV.c's own comment. Untagged (main segment) - no SEC00x here,
 * must match the untagged definition. */
void DateDAVGoToSync(void);

#endif /* DATEDAV_H */
