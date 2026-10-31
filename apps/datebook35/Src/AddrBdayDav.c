/*
 * AddrBdayDav.c - birthdays and anniversaries of the CardDAV / iCloud contacts,
 * read from Address35DAVBDA (written by CalDAV Sync, apps/caldav_sync/ContactDb.h).
 * Own segment (sec004): sec003 has no room left.
 *
 * 12 records, record month-1: u16 count (big-endian), then count entries in day
 * order: day u8, kind u8 (0 birthday, 1 engagement, 2 remembrance, 3 wedding,
 * 4 work, 5 other), year u16 (0 = unknown), slot u8 (0..7), record u16, nameLen
 * u8 + name, otherLen u8 + other (kind 5: the anniversary's own name).
 */
#include <PalmOS.h>
#include "AddrBday.h"
#include "DateDAV.h"		/* kDateDavGlyph */
#include "enccore.h"
#include "Datebook.h"		/* ApptDescFont, ApptsH */
#include "DateDB.h"		/* ApptInfoType, emptySlot */

#define kBdaName		"Address35DAVBDA"
#define kBdaDbType		'DATA'

/* Slot's alias for the row prefix, like a CalDAV event row's "\x95 Alias - ": Sec35-encrypted
 * in the AppInfo of Address35DAV_<slot+1> (AppInfoType 276 bytes, then u8 version, slotType,
 * stale, aliasLen, alias[24]; nonce 'ALIA', slot index). "******" while Sec35 is locked,
 * cut to kAliasMax so the name still fits the line. */
#define kAppInfoStd		276
#define kAliasMax		10
#define kAliasNonce		0x414C4941UL
static void DavAlias(UInt8 slot, char *out) SEC004;
static void DavAlias(UInt8 slot, char *out)
{
	char nm[dmDBNameLength];
	UInt8 key[ENC_KEY_BYTES];
	LocalID id, ai;
	DmOpenRef db;
	UInt16 n = 0;

	StrCopy(out, "******");
	StrPrintF(nm, "Address35DAV_%d", (Int16) (slot + 1));
	id = DmFindDatabase(0, nm);
	if (id == 0 || (db = DmOpenDatabase(0, id, dmModeReadOnly)) == NULL)
		return;
	if (DmDatabaseInfo(0, id, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ai, NULL, NULL, NULL) == errNone &&
		ai != 0 && EncGetKey(key, 1000000000UL)) {
		const UInt8 *a = (const UInt8 *) MemLocalIDToLockedPtr(ai, 0);
		if (a != NULL) {
			n = a[kAppInfoStd + 3];
			if (n > 24) n = 24;
			MemMove(out, a + kAppInfoStd + 4, n);
			MemPtrUnlock((MemPtr) a);
			EncCrypt(key, (UInt8 *) out, n, kAliasNonce, slot);
			if (n > kAliasMax) n = kAliasMax;
			out[n] = '\0';
		}
		MemSet(key, sizeof(key), 0);
	}
	DmCloseDatabase(db);
}

UInt16 AddrBdayDav(UInt16 year, UInt8 month, UInt8 day,
	char lines[][kAddrBdayLineLen], UInt16 recIndex[], UInt16 out, UInt16 maxLines)
{
	LocalID id;
	DmOpenRef db;
	MemHandle h;
	const UInt8 *r;
	UInt16 size, count, pos, i;
	char alias[8][kAliasMax + 1];
	UInt8 have = 0;			/* bit per slot: alias[] filled */

	if (month < 1 || month > 12)
		return out;
	id = DmFindDatabase(0, kBdaName);
	if (id == 0 || (db = DmOpenDatabase(0, id, dmModeReadOnly)) == NULL)
		return out;
	h = (month <= DmNumRecords(db)) ? DmQueryRecord(db, (UInt16) (month - 1)) : NULL;
	if (h != NULL) {
		r = (const UInt8 *) MemHandleLock(h);
		size = (UInt16) MemHandleSize(h);
		count = (size >= 2) ? (UInt16) (((UInt16) r[0] << 8) | r[1]) : 0;
		pos = 2;
		for (i = 0; i < count && out < maxLines; i++) {
			UInt8 d, kind, slot, nl, ol;
			UInt16 by, rec, ay;
			const UInt8 *nm, *ot;
			char name[48], lbl[32];
			const char *label;

			if ((UInt16) (pos + 8) > size)
				break;
			d = r[pos]; kind = r[pos + 1];
			by = (UInt16) (((UInt16) r[pos + 2] << 8) | r[pos + 3]);
			slot = r[pos + 4];
			rec = (UInt16) (((UInt16) r[pos + 5] << 8) | r[pos + 6]);
			nl = r[pos + 7];
			nm = r + pos + 8;
			if ((UInt16) (pos + 9 + nl) > size)
				break;
			ol = r[pos + 8 + nl];
			ot = nm + nl + 1;
			if ((UInt16) (pos + 9 + nl + ol) > size)
				break;
			pos = (UInt16) (pos + 9 + nl + ol);
			if (d != day || slot > 7 || rec > 0x0FFF)
				continue;

			if (!(have & (1 << slot))) {
				DavAlias(slot, alias[slot]);
				have |= (UInt8) (1 << slot);
			}
			name[0] = (char) kDateDavGlyph;
			name[1] = ' ';
			StrCopy(name + 2, alias[slot]);
			StrCat(name, " - ");
			if (nl) {
				UInt16 at = (UInt16) StrLen(name);
				MemMove(name + at, nm, nl);		/* nl <= 31 (index cap) */
				name[at + nl] = '\0';
			} else {
				StrCat(name, "?");
			}
			if (kind == 5 && ol) {
				MemMove(lbl, ot, ol);
				lbl[ol] = '\0';
				label = lbl;
			} else {
				switch (kind) {
				case 0:  label = "Birthday"; break;
				case 1:  label = "Engagement"; break;
				case 2:  label = "Remembrance"; break;
				case 4:  label = "Work"; break;
				case 5:  label = "Other"; break;
				default: label = "Wedding"; break;
				}
			}
			/* years below 1800 are Apple's "no year" placeholder (1604) */
			ay = (by >= 1800 && year > by) ? (UInt16) (year - by) : 0;
			AddrBdayLine(lines[out], name, ay, label);
			recIndex[out] = (UInt16) (kDavRecFlag | ((UInt16) slot << 12) | rec);
			out++;
		}
		MemHandleUnlock(h);
	}
	DmCloseDatabase(db);
	return out;
}

/* Day view birthday/anniversary rows wrap onto as many lines as the text needs, local and
 * CardDAV alike (the row is a custom table item: DayViewGetDescriptionHeight calls
 * DayBdayHeight, the draw callback calls DayBdayDraw). */
Int16 DayBdayHeight(const char *d, Int16 width, Int16 maxHeight)
{
	FontID prev = FntSetFont(ApptDescFont);
	Int16 lh = FntLineHeight(), n = 0;
	UInt16 c;

	while (*d && (Int16) ((n + 1) * lh) <= maxHeight && (c = FntWordWrap(d, (UInt16) width)) != 0) {
		d += c;
		n++;
	}
	FntSetFont(prev);
	return (Int16) ((n ? n : 1) * lh);
}

void DayBdayDraw(const char *d, const RectangleType *b)
{
	FontID prev = FntSetFont(ApptDescFont);
	Int16 lh = FntLineHeight(), y = b->topLeft.y;
	UInt16 c;

	do {
		c = *d ? FntWordWrap(d, (UInt16) b->extent.x) : 0;
		if (c)
			WinDrawTruncChars(d, (Int16) c, b->topLeft.x, y, b->extent.x);
		WinDrawGrayLine(b->topLeft.x, (Coord) (y + lh - 1), (Coord) (b->topLeft.x + b->extent.x - 1), (Coord) (y + lh - 1));
		d += c;
		y = (Int16) (y + lh);
	} while (*d && y < b->topLeft.y + b->extent.y);
	FntSetFont(prev);
}

/* The table sends no tap for the second and later lines of a wrapped custom row, so the Day
 * view asks here on every pen-down: the recordNum of the birthday row under (x, y), else 0. */
extern MemHandle ApptsH;		/* DateGlobals.c */
#define kBdayRowBase	0xF100u		/* DateDay.c kDayBdayBase */
UInt16 DayBdayHit(TablePtr table, Int16 x, Int16 y)
{
	RectangleType tb;
	UInt16 r, n = TblGetNumberOfRows(table), rn = 0;
	Int16 top;

	TblGetBounds(table, &tb);
	if (!RctPtInRectangle(x, y, &tb))
		return 0;
	top = tb.topLeft.y;
	for (r = 0; r < n && TblRowUsable(table, r); r++) {
		Int16 h = TblGetRowHeight(table, r);
		if (y < top + h) {
			ApptInfoPtr ap = (ApptInfoPtr) MemHandleLock(ApptsH);
			rn = ap[TblGetRowID(table, r)].recordNum;
			MemPtrUnlock(ap);
			return (rn >= kBdayRowBase && rn != emptySlot) ? rn : 0;
		}
		top = (Int16) (top + h);
	}
	return 0;
}
