/*
 * AddrBday.h - read Address35 birthdays/anniversaries for the Agenda.
 *
 * Datebook35 can't link Address35's DB layer, so this is a tiny, self-contained
 * reader over the Address35DB packed record format. Keep the constants in sync
 * with apps/address35/AddressDB35.h.
 */
#ifndef ADDRBDAY_H
#define ADDRBDAY_H

#include <PalmOS.h>

#define kAddrBdayLineLen	64

/* Fill up to maxLines "Name - [Nth ]Birthday" / "Name - [Nth ]Anniversary" strings
 * (and the Address35 record index of each, into recIndex[]) for every Address35
 * record whose birthday/anniversary falls on (month, day) - any year. When the
 * stored year is known, "year" (the agenda's true full year, e.g. 2035) yields the
 * age/ordinal ("31st Birthday"). Returns the number written; 0 if not installed. */
UInt16 AddrBdayGet(UInt16 year, UInt8 month, UInt8 day,
	char lines[][kAddrBdayLineLen], UInt16 recIndex[], UInt16 maxLines) SEC003;

/* CardDAV / iCloud contacts (Address35DAVBDA, written by CalDAV Sync): their
 * entries are appended after Address35's own. Their recIndex[] value is
 * kDavRecFlag | slot (0..7) << 12 | record index (0..4095) in Address35DAV_<slot+1>;
 * AddrBdayGoTo turns that into the GoTo Address35's DavGoTo expects. Returns the
 * new line count; sec004. */
#define kDavRecFlag		0x8000u
#define kDavGoToMagic	0x44415600UL	/* matches address35 davGoToMagic */
UInt16 AddrBdayDav(UInt16 year, UInt8 month, UInt8 day,
	char lines[][kAddrBdayLineLen], UInt16 recIndex[], UInt16 out, UInt16 maxLines) SEC004;
void AddrBdayLine(char *L, const char *nm, UInt16 age, const char *label) SEC003;

/* Day view birthday rows: height (a multiple of the line height, capped at maxHeight) and
 * drawing of text wrapped to the column width. sec004. */
Int16 DayBdayHeight(const char *d, Int16 width, Int16 maxHeight) SEC004;
void DayBdayDraw(const char *d, const RectangleType *b) SEC004;
UInt16 DayBdayHit(TablePtr table, Int16 x, Int16 y) SEC004;

/* Switch to Address35, opening it on record index recIndex (via GoTo). */
void AddrBdayGoTo(UInt16 recIndex) SEC003;

#endif /* ADDRBDAY_H */
