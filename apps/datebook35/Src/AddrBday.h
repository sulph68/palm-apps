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

/* Switch to Address35, opening it on record index recIndex (via GoTo). */
void AddrBdayGoTo(UInt16 recIndex) SEC003;

#endif /* ADDRBDAY_H */
