/*
 * AddrBday.c - self-contained reader over the Address35DB packed record format,
 * used by the Agenda to surface birthdays/anniversaries (cross-DB read, like the
 * ToDo35 section). MUST match apps/address35/AddressDB35.h.
 *
 * Packed record: [0]ver [1]misc [2..3]fieldCount(BE) [4..5]offLast [6..7]offFirst
 *   [8..9]offCompany, then FieldEntry[]: u8 type; u8 label; u16 len(BE); u8 val[len].
 */
#include <PalmOS.h>
#include "AddrBday.h"

#define kAd35Type		'DATA'
#define kAd35Creator	'Ad35'

/* field type ids (AddressDB35.h a35fXxx) */
#define kFtLast			1
#define kFtFirst		2
#define kFtCompany		5
#define kFtBirthday		12
#define kFtAnniversary	24

#define kA35HdrSize		10
#define kBdayStrLen		8			/* "YYYYMMDD" */

/* anniversary type names, indexed by the field's label byte. Index 4 ("Other")
 * carries a custom name appended after the 8-char date (Address35 Main.h
 * a35AnnivOther). Keep in sync with EditDialogs.c kAnnivType. */
#define kAnnivOther		4
static const char *const kAnnivType[5] = {
	"Engagement", "Remembrance", "Wedding", "Work", "Other"
};

/* keep the whole reader in sec003 with its caller (the agenda) */
static UInt16 rd16(const UInt8 *p) SEC003;
static Boolean AddrBdayMatches(const UInt8 *v, UInt16 len, UInt8 month, UInt8 day) SEC003;
static void AddrCopyVal(char *out, UInt16 outLen, const UInt8 *v, UInt16 len) SEC003;
static UInt16 AddrBdayYear(const UInt8 *v) SEC003;
static void AddrBdayName(char *nm, UInt16 nmLen,
	const char *first, const char *last, const char *company) SEC003;
static void AddrBdayLine(char *L, const char *nm, UInt16 age, const char *label) SEC003;

static UInt16 rd16(const UInt8 *p) { return (UInt16) (((UInt16) p[0] << 8) | p[1]); }

/* parse the 4-digit "YYYY" prefix of a "YYYYMMDD" value; 0 means year unknown */
static UInt16 AddrBdayYear(const UInt8 *v)
{
	return (UInt16) ((v[0] - '0') * 1000 + (v[1] - '0') * 100 +
		(v[2] - '0') * 10 + (v[3] - '0'));
}

/* build the display name: "First Last", else Company, else "?" */
static void AddrBdayName(char *nm, UInt16 nmLen,
	const char *first, const char *last, const char *company)
{
	if (first[0] || last[0]) {
		StrNCopy(nm, first, nmLen - 1); nm[nmLen - 1] = '\0';
		if (first[0] && last[0]) StrNCat(nm, " ", nmLen);	/* maxLen = buffer size */
		StrNCat(nm, last, nmLen);
	} else {
		StrNCopy(nm, company[0] ? company : "?", nmLen - 1); nm[nmLen - 1] = '\0';
	}
}

/* "<name> - [<age><ord> ]<label>", truncated to fit a line. Ordinal is derived
 * from the printed digits (NOT % / -- 32-bit division from a secondary segment
 * hits an unreachable libgcc and crashes; see [[multiseg-libgcc-gotcha]]). */
static void AddrBdayLine(char *L, const char *nm, UInt16 age, const char *label)
{
	char suffix[28];
	UInt16 nmMax;

	StrCopy(suffix, " - ");
	if (age > 0) {
		char nb[8];
		const char *ord;
		UInt16 n;
		char lastc, tens;

		StrIToA(nb, (Int32) age);
		n = (UInt16) StrLen(nb);
		lastc = nb[n - 1];
		tens = (n >= 2) ? nb[n - 2] : '0';
		if (tens == '1')		ord = "th";	/* 11..19 (and x11..x13) */
		else if (lastc == '1')	ord = "st";
		else if (lastc == '2')	ord = "nd";
		else if (lastc == '3')	ord = "rd";
		else					ord = "th";
		StrCat(suffix, nb);
		StrCat(suffix, ord);
		StrCat(suffix, " ");
	}
	StrCat(suffix, label);
	nmMax = (UInt16) (kAddrBdayLineLen - 1 - StrLen(suffix));
	StrNCopy(L, nm, nmMax);
	L[nmMax] = '\0';
	StrCat(L, suffix);
}

/* true iff the field value is a "YYYYMMDD" whose month/day == (month,day) */
static Boolean AddrBdayMatches(const UInt8 *v, UInt16 len, UInt8 month, UInt8 day)
{
	UInt8 m, d;

	if (v == NULL || len < 8)
		return false;
	m = (UInt8) ((v[4] - '0') * 10 + (v[5] - '0'));
	d = (UInt8) ((v[6] - '0') * 10 + (v[7] - '0'));
	return (Boolean) (m == month && d == day);
}

/* copy a length-counted value as a C string into out (truncating) */
static void AddrCopyVal(char *out, UInt16 outLen, const UInt8 *v, UInt16 len)
{
	UInt16 n = len;

	if (n > outLen - 1) n = (UInt16) (outLen - 1);
	if (n) MemMove(out, v, n);
	out[n] = '\0';
}

SEC003 UInt16 AddrBdayGet(UInt16 year, UInt8 month, UInt8 day,
	char lines[][kAddrBdayLineLen], UInt16 recIndex[], UInt16 maxLines)
{
	DmOpenRef db;
	UInt16 total, i, out = 0;

	db = DmOpenDatabaseByTypeCreator(kAd35Type, kAd35Creator, dmModeReadOnly);
	if (db == NULL)
		return 0;
	total = DmNumRecords(db);
	for (i = 0; i < total && out < maxLines; i++) {
		MemHandle h = DmQueryRecord(db, i);
		const UInt8 *rec;
		UInt16 count, off, fi;
		char first[40], last[40], company[40];
		const UInt8 *bdayV = NULL, *annivV = NULL;
		UInt16 bdayLen = 0, annivLen = 0;
		UInt8 annivLabel = 0;			/* anniversary type index */

		if (h == NULL)
			continue;
		rec = (const UInt8 *) MemHandleLock(h);
		count = rd16(rec + 2);
		first[0] = last[0] = company[0] = '\0';
		off = kA35HdrSize;
		for (fi = 0; fi < count; fi++) {
			UInt8 type = rec[off];
			UInt8 label = rec[off + 1];
			UInt16 len = rd16(rec + off + 2);
			const UInt8 *val = rec + off + 4;

			switch (type) {
			case kFtFirst:		AddrCopyVal(first, sizeof(first), val, len); break;
			case kFtLast:		AddrCopyVal(last, sizeof(last), val, len); break;
			case kFtCompany:	AddrCopyVal(company, sizeof(company), val, len); break;
			case kFtBirthday:	bdayV = val; bdayLen = len; break;
			case kFtAnniversary:	annivV = val; annivLen = len; annivLabel = label; break;
			default: break;
			}
			off = (UInt16) (off + 4 + len);
		}

		if (bdayV != NULL && AddrBdayMatches(bdayV, bdayLen, month, day) &&
			out < maxLines) {
			char nm[48];
			UInt16 by = AddrBdayYear(bdayV);		/* 0 => unknown year */
			UInt16 age = (by != 0 && year > by) ? (UInt16) (year - by) : 0;
			AddrBdayName(nm, sizeof(nm), first, last, company);
			AddrBdayLine(lines[out], nm, age, "Birthday");
			recIndex[out] = i;
			out++;
		}
		if (annivV != NULL && AddrBdayMatches(annivV, annivLen, month, day) &&
			out < maxLines) {
			char nm[48], lblbuf[24];
			const char *lbl;
			UInt16 ay = AddrBdayYear(annivV);
			UInt16 age = (ay != 0 && year > ay) ? (UInt16) (year - ay) : 0;

			/* show the anniversary TYPE ("Wedding"/"Engagement"/...), or the
			 * custom name for an "Other"-typed anniversary. */
			if (annivLabel == kAnnivOther && annivLen > kBdayStrLen) {
				UInt16 cn = (UInt16) (annivLen - kBdayStrLen);
				if (cn > sizeof(lblbuf) - 1) cn = sizeof(lblbuf) - 1;
				MemMove(lblbuf, annivV + kBdayStrLen, cn);
				lblbuf[cn] = '\0';
				lbl = lblbuf;
			} else {
				lbl = kAnnivType[(annivLabel < 5) ? annivLabel : 2];
			}
			AddrBdayName(nm, sizeof(nm), first, last, company);
			AddrBdayLine(lines[out], nm, age, lbl);
			recIndex[out] = i;
			out++;
		}
		MemHandleUnlock(h);
	}
	DmCloseDatabase(db);
	return out;
}

/* Switch to Address35, opening it on record index recIndex. Address35's
 * sysAppLaunchCmdGoTo handler reads GoToParamsType.recordNum as the record
 * index; ownership of the param block passes to the OS via MemPtrSetOwner. */
SEC003 void AddrBdayGoTo(UInt16 recIndex)
{
	DmSearchStateType st;
	UInt16 cardNo;
	LocalID dbID;
	GoToParamsPtr gp;

	/* SysUIAppSwitch wants the APPLICATION ('appl'), not the 'DATA' database. */
	if (DmGetNextDatabaseByTypeCreator(true, &st, sysFileTApplication, kAd35Creator,
			true, &cardNo, &dbID) != errNone || dbID == 0)
		return;
	gp = (GoToParamsPtr) MemPtrNew(sizeof(GoToParamsType));
	if (gp == NULL)
		return;
	MemSet(gp, sizeof(GoToParamsType), 0);
	MemPtrSetOwner(gp, 0);			/* survive the app switch */
	gp->recordNum = recIndex;		/* Address35 GoTo reads this as its record index */
	SysUIAppSwitch(cardNo, dbID, sysAppLaunchCmdGoTo, gp);
}
