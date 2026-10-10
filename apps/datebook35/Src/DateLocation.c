/*
 * DateLocation.c - Db35 event Location feature: the Location edit dialog,
 * the Address35-contact picker, the Country picker, and the "Previous Loc"
 * (LocationsDB) picker+Delete. All modeled closely on Address35's own
 * address dialog/pickers (address35/EditDialogs.c: EditAddressDialog,
 * A35PickList, RelPickContact) -- see that file for the originals this
 * mirrors, and Datebook35.md for the feature writeup.
 *
 * Kept OUT of DateDay.c's sec001 (at the 32KB cap; DateDay.c only holds a
 * thin trigger that calls LocationEditForm(), see DetailsHandleEvent).
 * Segment: SEC002 (DateWeek/ToDoDB/dialogs), same as locdb.c -- tried
 * sec003 first (overflowed by ~450B) and main (also overflowed -- "coderes
 * is full" even though the raw summed .text stayed under the nominal cap,
 * so something beyond simple per-object byte-counting was involved; not
 * fully root-caused, not worth chasing further once sec002 -- which has
 * the most headroom of the four segments AND already hosts locdb.c, so
 * calls between the two files are cheap intra-segment calls -- just
 * worked). The country name STRING LITERALS still always land in main's
 * .text regardless of tagging (this compiler pools string-literal bytes
 * separately from the `static const char*[]` pointer array, which DOES
 * follow the tagged segment / A5-relative .data placement). See
 * [[app-reference-docs]] Datebook35.md.
 */
#include <PalmOS.h>

#include "Datebook.h"		/* datebookCreator, datebookUnsavedPrefID */
#include "DatebookRsc.h"	/* Location dialog/picker resource IDs     */
#include "DateLocation.h"
#include "enccore.h"		/* Sec35 shared key -- Address35's a35fAddress is
							 * an encrypted payload field, see kLocMiscEncrypted below */

/* ---- Address35 cross-DB read (mirrors AddrBday.c's approach) ---- */
#define kLocAd35Type		'DATA'
#define kLocAd35Creator		'Ad35'
#define kLocA35HdrSize		10		/* matches AddrBday.c's kA35HdrSize; keep in sync */
#define kLocFtLast			1		/* AddressDB35.h a35fLast    */
#define kLocFtFirst			2		/* AddressDB35.h a35fFirst   */
#define kLocFtCompany		5		/* AddressDB35.h a35fCompany */
#define kLocFtAddress		11		/* AddressDB35.h a35fAddress (TAB-packed
									 * Line1/Line2/Apt/City/State/Zip/Country) */

/* Address35 (AddressDB35.c) marks a record's MISC byte with this bit and
 * AES-CTR-encrypts payload field VALUES (nonce = record uid, field index) --
 * a35fAddress IS one of the encrypted fields (only name/company/tag/relation/
 * birthday/anniversary stay clear as sort keys, which is why AddrBday.c's raw
 * read never needed to decrypt anything). Must mirror A35GetRecord's own
 * decrypt step here, byte for byte, since Address35 has no cross-app decrypt
 * call to reuse. TTL matches AddressDB35.c's a35EncTtl (effectively "as long
 * as Sec35 says the key is valid" -- Sec35's own alarm enforces the real lock). */
#define kLocMiscEncrypted	0x01	/* AddressDB35.h a35MiscEncrypted */
#define kLocEncTtl			1000000000UL

/* ---- Country list -- a synced copy of Address35's kCountries[]
 * (EditDialogs.c:339), same idiom as AddrBday.c's kAnnivType[] copy: keep
 * in sync by hand, duplicating rather than sharing across apps. ---- */
static const char * const kLocCountries[] = {
	"(None)",
	"Afghanistan", "Albania", "Algeria", "Andorra", "Angola",
	"Antigua and Barbuda", "Argentina", "Armenia", "Australia", "Austria",
	"Azerbaijan", "Bahamas", "Bahrain", "Bangladesh", "Barbados", "Belarus",
	"Belgium", "Belize", "Benin", "Bhutan", "Bolivia",
	"Bosnia and Herzegovina", "Botswana", "Brazil", "Brunei", "Bulgaria",
	"Burkina Faso", "Burundi", "Cambodia", "Cameroon", "Canada",
	"Cape Verde", "Central African Republic", "Chad", "Chile", "China",
	"Colombia", "Comoros", "Congo", "Costa Rica", "Croatia", "Cuba",
	"Cyprus", "Czech Republic", "Denmark", "Djibouti", "Dominica",
	"Dominican Republic", "Ecuador", "Egypt", "El Salvador",
	"Equatorial Guinea", "Eritrea", "Estonia", "Eswatini", "Ethiopia",
	"Fiji", "Finland", "France", "Gabon", "Gambia", "Georgia", "Germany",
	"Ghana", "Greece", "Grenada", "Guatemala", "Guinea", "Guinea-Bissau",
	"Guyana", "Haiti", "Honduras", "Hong Kong", "Hungary", "Iceland",
	"India", "Indonesia", "Iran", "Iraq", "Ireland", "Israel", "Italy",
	"Ivory Coast", "Jamaica", "Japan", "Jordan", "Kazakhstan", "Kenya",
	"Kiribati", "Kosovo", "Kuwait", "Kyrgyzstan", "Laos", "Latvia",
	"Lebanon", "Lesotho", "Liberia", "Libya", "Liechtenstein", "Lithuania",
	"Luxembourg", "Macau", "Madagascar", "Malawi", "Malaysia", "Maldives",
	"Mali", "Malta", "Marshall Islands", "Mauritania", "Mauritius",
	"Mexico", "Micronesia", "Moldova", "Monaco", "Mongolia", "Montenegro",
	"Morocco", "Mozambique", "Myanmar", "Namibia", "Nauru", "Nepal",
	"Netherlands", "New Zealand", "Nicaragua", "Niger", "Nigeria",
	"North Korea", "North Macedonia", "Norway", "Oman", "Pakistan",
	"Palau", "Palestine", "Panama", "Papua New Guinea", "Paraguay", "Peru",
	"Philippines", "Poland", "Portugal", "Qatar", "Romania", "Russia",
	"Rwanda", "Saint Lucia", "Samoa", "San Marino", "Saudi Arabia",
	"Senegal", "Serbia", "Seychelles", "Sierra Leone", "Singapore",
	"Slovakia", "Slovenia", "Solomon Islands", "Somalia", "South Africa",
	"South Korea", "South Sudan", "Spain", "Sri Lanka", "Sudan", "Suriname",
	"Sweden", "Switzerland", "Syria", "Taiwan", "Tajikistan", "Tanzania",
	"Thailand", "Timor-Leste", "Togo", "Tonga", "Trinidad and Tobago",
	"Tunisia", "Turkey", "Turkmenistan", "Tuvalu", "Uganda", "Ukraine",
	"United Arab Emirates", "United Kingdom", "United States", "Uruguay",
	"Uzbekistan", "Vanuatu", "Vatican City", "Venezuela", "Vietnam",
	"Yemen", "Zambia", "Zimbabwe"
};
#define kLocCountryCount	(sizeof (kLocCountries) / sizeof (kLocCountries[0]))

/* ---- "last used country" unsaved pref (mirrors Address35's
 * A35GetLastCountry/A35SetLastCountry, Address35.c:91-121) ---- */
typedef struct { UInt16 version; Int16 lastCountry; } LocDefaults;
#define kLocDefaultsVer	1

static void LocLoadDefaults (LocDefaults* d) SEC002;
static Int16 LocGetLastCountry (void) SEC002;
static void LocSetLastCountry (Int16 idx) SEC002;

SEC002 static void LocLoadDefaults (LocDefaults* d)
{
	UInt16	size = sizeof (*d);
	UInt16	r = PrefGetAppPreferences (datebookCreator, datebookUnsavedPrefID,
		d, &size, false);
	if (r == noPreferenceFound || size < sizeof (*d) || d->version != kLocDefaultsVer)
		{
		d->version = kLocDefaultsVer;
		d->lastCountry = 0;
		}
}

SEC002 static Int16 LocGetLastCountry (void)
{
	LocDefaults	d;
	LocLoadDefaults (&d);
	return d.lastCountry;
}

SEC002 static void LocSetLastCountry (Int16 idx)
{
	LocDefaults	d;
	LocLoadDefaults (&d);
	d.lastCountry = idx;
	PrefSetAppPreferences (datebookCreator, datebookUnsavedPrefID,
		kLocDefaultsVer, &d, sizeof (d), false);	/* saved=false -> unsaved */
}

/* ---- tiny dialog helpers (clone Address35's EditDialogs.c idioms) ---- */
static Boolean LocDlgSysMenu (EventPtr ev) SEC002;
static void* DlgObj (FormPtr f, UInt16 id) SEC002;
static void DlgSetField (FormPtr f, UInt16 id, const char* s) SEC002;
static void LocReadField (FormPtr f, UInt16 id, char* dst, UInt16 dstLen) SEC002;
static void LocLower (char* s) SEC002;
static UInt16 LocFilterIdx (const char* const* items, UInt16 count, const char* term,
	char** ptrs, UInt16* orig) SEC002;

/* Feed system/menu events (command stroke, Edit-menu Cut/Copy/Paste) to the
 * ROM before FrmDispatchEvent, so they work in these hand-rolled event
 * loops (each form carries MENUID mnuLocEdit). */
SEC002 static Boolean LocDlgSysMenu (EventPtr ev)
{
	UInt16	error;
	if (SysHandleEvent (ev)) return true;
	if (MenuHandleEvent (0, ev, &error)) return true;
	return false;
}

SEC002 static void* DlgObj (FormPtr f, UInt16 id)
{
	return FrmGetObjectPtr (f, FrmGetObjectIndex (f, id));
}

SEC002 static void DlgSetField (FormPtr f, UInt16 id, const char* s)
{
	FieldPtr	fld = (FieldPtr) DlgObj (f, id);
	UInt16		len = s ? StrLen (s) : 0;
	MemHandle	h, old;
	char*		p;

	if (fld == NULL) return;
	h = MemHandleNew ((UInt32) (len + 1));
	if (h == NULL) return;
	p = (char*) MemHandleLock (h);
	if (s) StrCopy (p, s); else p[0] = '\0';
	MemHandleUnlock (h);
	old = FldGetTextHandle (fld);
	FldSetTextHandle (fld, h);
	if (old) MemHandleFree (old);
}

SEC002 static void LocReadField (FormPtr f, UInt16 id, char* dst, UInt16 dstLen)
{
	const char*	p = FldGetTextPtr ((FieldPtr) DlgObj (f, id));
	StrNCopy (dst, p ? p : "", dstLen - 1);
	dst[dstLen - 1] = '\0';
}

SEC002 static void LocLower (char* s)
{
	for (; *s; s++)
		if (*s >= 'A' && *s <= 'Z') *s = (char) (*s + 32);
}

/* substring-filter (case-insensitive) `items` into `ptrs`, recording each
 * kept item's ORIGINAL index in `orig` (clone of Address35's PickFilterIdx,
 * EditDialogs.c:95). */
SEC002 static UInt16 LocFilterIdx (const char* const* items, UInt16 count,
	const char* term, char** ptrs, UInt16* orig)
{
	char	lt[40];
	UInt16	i, n = 0, tl;

	StrNCopy (lt, term ? term : "", sizeof (lt) - 1);
	lt[sizeof (lt) - 1] = '\0';
	LocLower (lt);
	tl = StrLen (lt);
	for (i = 0; i < count; i++)
		{
		Boolean	hit = (Boolean) (tl == 0);
		if (! hit)
			{
			char	buf[72];
			StrNCopy (buf, items[i], sizeof (buf) - 1);
			buf[sizeof (buf) - 1] = '\0';
			LocLower (buf);
			hit = (Boolean) (StrStr (buf, lt) != NULL);
			}
		if (hit) { ptrs[n] = (char*) items[i]; orig[n] = i; n++; }
		}
	return n;
}

/***********************************************************************
 * LocPickStringList -- generic full-screen searchable list picker (clone
 * of Address35's A35PickList, EditDialogs.c:124-212). Reused for both the
 * Country picker (below) and the Address35-contact picker
 * (LocPickAddress35Contact), which builds its own item array first.
 * Returns the chosen item's index in the CALLER's `items`, or -1.
 ***********************************************************************/
static Int16 LocPickStringList (const char* title, const char* const* items,
	UInt16 count, Int16 initial) SEC002;
SEC002 static Int16 LocPickStringList (const char* title, const char* const* items,
	UInt16 count, Int16 initial)
{
	FormPtr		prev = FrmGetActiveForm ();
	FormPtr		frm;
	ListPtr		lst;
	MemHandle	ph, oh;
	char**		ptrs;
	UInt16*		orig;
	UInt16		shown;
	Int16		result = -1;
	Boolean		done = false;

	if (count == 0) return -1;

	ph = MemHandleNew ((UInt32) count * sizeof (char*));
	oh = MemHandleNew ((UInt32) count * sizeof (UInt16));
	if (! ph || ! oh)
		{
		if (ph) MemHandleFree (ph);
		if (oh) MemHandleFree (oh);
		return -1;
		}
	ptrs = (char**) MemHandleLock (ph);
	orig = (UInt16*) MemHandleLock (oh);

	frm = FrmInitForm (LocAddrPickDialog);
	FrmSetTitle (frm, (char*) title);
	lst = (ListPtr) DlgObj (frm, lstLocAddrPick);

	shown = LocFilterIdx (items, count, "", ptrs, orig);
	LstSetListChoices (lst, ptrs, shown);
	if (initial >= 0)
		{
		UInt16	k;
		for (k = 0; k < shown; k++)
			if (orig[k] == (UInt16) initial) { LstSetSelection (lst, (Int16) k); break; }
		}
	else
		LstSetSelection (lst, -1);

	FrmSetActiveForm (frm);
	FrmDrawForm (frm);
	FrmSetFocus (frm, FrmGetObjectIndex (frm, fldLocAddrPickFind));

	while (! done)
		{
		EventType	ev;
		Boolean		refilter = false;

		EvtGetEvent (&ev, evtWaitForever);

		if (ev.eType == lstSelectEvent && ev.data.lstSelect.listID == lstLocAddrPick)
			{
			Int16	sidx = ev.data.lstSelect.selection;
			if (sidx >= 0 && sidx < (Int16) shown) result = (Int16) orig[sidx];
			done = true;
			continue;
			}
		if (ev.eType == ctlSelectEvent)
			{
			UInt16	cid = ev.data.ctlSelect.controlID;
			if (cid == btnLocAddrPickCancel) { done = true; continue; }
			}
		if (ev.eType == keyDownEvent)
			{
			if (! LocDlgSysMenu (&ev)) FrmDispatchEvent (&ev);
			refilter = true;
			}
		else if (ev.eType == appStopEvent)
			{
			EvtAddEventToQueue (&ev);
			done = true;
			continue;
			}
		else if (! refilter && ev.eType != ctlSelectEvent)
			{
			if (! LocDlgSysMenu (&ev)) FrmDispatchEvent (&ev);
			}

		if (refilter)
			{
			const char*	t = FldGetTextPtr ((FieldPtr) DlgObj (frm, fldLocAddrPickFind));
			shown = LocFilterIdx (items, count, t ? t : "", ptrs, orig);
			LstSetListChoices (lst, ptrs, shown);
			LstSetSelection (lst, -1);
			LstDrawList (lst);
			}
		}

	FrmEraseForm (frm);
	FrmDeleteForm (frm);
	if (prev) FrmSetActiveForm (prev);
	MemHandleUnlock (ph); MemHandleUnlock (oh);
	MemHandleFree (ph); MemHandleFree (oh);
	return result;
}

/* ===================================================================
 * Address35 cross-DB read (never written back to -- read-only always)
 * =================================================================== */

static UInt16 LocRd16 (const UInt8* p) SEC002;
SEC002 static UInt16 LocRd16 (const UInt8* p)
{
	return (UInt16) (((UInt16) p[0] << 8) | p[1]);
}

static void LocCopyVal (char* out, UInt16 outLen, const UInt8* v, UInt16 len) SEC002;
SEC002 static void LocCopyVal (char* out, UInt16 outLen, const UInt8* v, UInt16 len)
{
	UInt16	n = len;
	if (n > outLen - 1) n = (UInt16) (outLen - 1);
	if (n) MemMove (out, v, n);
	out[n] = '\0';
}

/* copy TAB-delimited field #idx of src into out (clone of Address35's
 * AddrGetField, EditDialogs.c:478-489). */
static void LocAddrGetField (const char* src, UInt16 idx, char* out, UInt16 outLen) SEC002;
SEC002 static void LocAddrGetField (const char* src, UInt16 idx, char* out, UInt16 outLen)
{
	const char*	p = src ? src : "";
	UInt16		field = 0, o = 0;

	out[0] = '\0';
	while (*p && field < idx) { if (*p == '\t') field++; p++; }
	if (field < idx) return;
	while (*p && *p != '\t' && o < (UInt16) (outLen - 1))
		out[o++] = *p++;
	out[o] = '\0';
}

/* Read record #idx of an already-open Address35DB into a DlxLocation:
 * Name = "First Last" if present, else Company (a business contact with no
 * personal name on file); address = the a35fAddress TAB-block
 * split into Line1/Line2/Apt/City/State/Zip/Country (same 7-part layout
 * Address35's own EditAddressDialog/RecordForm.c read -- RecordForm.c:271
 * confirms City/State/Zip/Country are packed INTO a35fAddress, not
 * separate top-level fields, despite those enum values existing). */
static void LocReadAddress35Record (DmOpenRef db, UInt16 idx, DlxLocation* out) SEC002;
SEC002 static void LocReadAddress35Record (DmOpenRef db, UInt16 idx, DlxLocation* out)
{
	MemHandle		h;
	const UInt8*	rec;
	UInt16			count, off, fi, addrFi = 0;
	Boolean			encrypted;
	UInt32			uid = 0;
	char			first[40], last[40], company[40];
	const UInt8*	addrV = NULL;
	UInt16			addrLen = 0;

	MemSet (out, sizeof (DlxLocation), 0);

	h = DmQueryRecord (db, idx);
	if (! h) return;

	rec = (const UInt8*) MemHandleLock (h);
	encrypted = (Boolean) (rec[1] & kLocMiscEncrypted);
	count = LocRd16 (rec + 2);
	first[0] = last[0] = company[0] = '\0';
	off = kLocA35HdrSize;
	for (fi = 0; fi < count; fi++)
		{
		UInt8			type = rec[off];
		UInt16			len  = LocRd16 (rec + off + 2);
		const UInt8*	val  = rec + off + 4;

		switch (type)
			{
			case kLocFtFirst:   LocCopyVal (first, sizeof (first), val, len); break;
			case kLocFtLast:    LocCopyVal (last, sizeof (last), val, len); break;
			case kLocFtCompany: LocCopyVal (company, sizeof (company), val, len); break;
			case kLocFtAddress: addrV = val; addrLen = len; addrFi = fi; break;
			default: break;
			}
		off = (UInt16) (off + 4 + len);
		}
	if (encrypted) DmRecordInfo (db, idx, NULL, &uid, NULL);
	MemHandleUnlock (h);

	if (first[0] || last[0])
		{
		StrNCopy (out->name, first, sizeof (out->name) - 1);
		out->name[sizeof (out->name) - 1] = '\0';
		if (first[0] && last[0]) StrNCat (out->name, " ", sizeof (out->name));
		StrNCat (out->name, last, sizeof (out->name));
		}
	else if (company[0])
		{
		StrNCopy (out->name, company, sizeof (out->name) - 1);
		out->name[sizeof (out->name) - 1] = '\0';
		}

	if (addrV)
		{
		char	addrBuf[256];		/* matches Address35's own a35EditValLen cap */
		LocCopyVal (addrBuf, sizeof (addrBuf), addrV, addrLen);
		if (encrypted)
			{
			/* addrBuf now holds the (possibly truncated-to-255) copied prefix of
			 * the ciphertext -- AES-CTR is a per-position stream cipher, so
			 * decrypting just that prefix from byte 0 is valid. */
			UInt16	n = addrLen;
			if (n > sizeof (addrBuf) - 1) n = (UInt16) (sizeof (addrBuf) - 1);
			if (n)
				{
				UInt8	key[ENC_KEY_BYTES];
				if (EncGetKey (key, kLocEncTtl))
					{
					EncCrypt (key, (UInt8*) addrBuf, n, uid, addrFi);
					MemSet (key, ENC_KEY_BYTES, 0);
					}
				else
					{
					/* locked: mask, don't show ciphertext-as-text (matches
					 * A35GetRecord's own locked-record behavior) */
					UInt16	stars = (n > 7) ? 6 : (n > 1 ? (UInt16) (n - 1) : 0);
					MemSet (addrBuf, stars, '*');
					addrBuf[stars] = '\0';
					}
				}
			}
		LocAddrGetField (addrBuf, 0, out->line1, sizeof (out->line1));
		LocAddrGetField (addrBuf, 1, out->line2, sizeof (out->line2));
		LocAddrGetField (addrBuf, 2, out->apt, sizeof (out->apt));
		LocAddrGetField (addrBuf, 3, out->city, sizeof (out->city));
		LocAddrGetField (addrBuf, 4, out->state, sizeof (out->state));
		LocAddrGetField (addrBuf, 5, out->zip, sizeof (out->zip));
		LocAddrGetField (addrBuf, 6, out->country, sizeof (out->country));
		}
}

static Boolean LocReadAddress35ByUID (UInt32 uid, DlxLocation* out) SEC002;
SEC002 static Boolean LocReadAddress35ByUID (UInt32 uid, DlxLocation* out)
{
	DmOpenRef	db;
	UInt16		idx;

	MemSet (out, sizeof (DlxLocation), 0);

	db = DmOpenDatabaseByTypeCreator (kLocAd35Type, kLocAd35Creator, dmModeReadOnly);
	if (! db) return false;
	if (DmFindRecordByID (db, uid, &idx) != 0)
		{
		DmCloseDatabase (db);
		return false;
		}
	LocReadAddress35Record (db, idx, out);
	DmCloseDatabase (db);
	return (Boolean) (out->name[0] != 0 || out->line1[0] != 0);
}

/* Pick a contact from the whole Address35DB via LocPickStringList (rich
 * "Company"/"First Last" lines + Find). Returns the chosen record's stable
 * unique id, or 0 if cancelled/none. Never writes to Address35DB. */
static UInt32 LocPickAddress35Contact (void) SEC002;
SEC002 static UInt32 LocPickAddress35Contact (void)
{
	DmOpenRef	db;
	UInt16		total, i, all = 0;
	MemHandle	nh, uh, ph;
	char*		names;
	UInt32*		uids;
	char**		ptrs;
	UInt32		result = 0;
	Int16		sel;
	const UInt16 NL = 48;

	db = DmOpenDatabaseByTypeCreator (kLocAd35Type, kLocAd35Creator, dmModeReadOnly);
	if (! db) return 0;

	total = DmNumRecords (db);
	if (total == 0) { DmCloseDatabase (db); return 0; }

	nh = MemHandleNew ((UInt32) total * NL);
	uh = MemHandleNew ((UInt32) total * sizeof (UInt32));
	ph = MemHandleNew ((UInt32) total * sizeof (char*));
	if (! nh || ! uh || ! ph)
		{
		if (nh) MemHandleFree (nh);
		if (uh) MemHandleFree (uh);
		if (ph) MemHandleFree (ph);
		DmCloseDatabase (db);
		return 0;
		}
	names = (char*) MemHandleLock (nh);
	uids  = (UInt32*) MemHandleLock (uh);
	ptrs  = (char**) MemHandleLock (ph);

	for (i = 0; i < total; i++)
		{
		UInt16			attr = 0;
		UInt32			uid = 0;
		DlxLocation	tmp;

		if (DmRecordInfo (db, i, &attr, &uid, NULL) != errNone) continue;
		if ((attr & dmRecAttrDelete) || uid == 0) continue;

		LocReadAddress35Record (db, i, &tmp);
		if (! tmp.name[0]) continue;	/* nothing to show/pick */

		StrNCopy (names + (UInt32) all * NL, tmp.name, NL - 1);
		(names + (UInt32) all * NL)[NL - 1] = '\0';
		uids[all] = uid;
		all++;
		}

	for (i = 0; i < all; i++)
		ptrs[i] = names + (UInt32) i * NL;

	sel = LocPickStringList ("Pick a Contact", (const char* const*) ptrs, all, -1);
	if (sel >= 0 && sel < (Int16) all)
		result = uids[sel];

	MemHandleUnlock (nh); MemHandleUnlock (uh); MemHandleUnlock (ph);
	MemHandleFree (nh); MemHandleFree (uh); MemHandleFree (ph);
	DmCloseDatabase (db);
	return result;
}

/* ===================================================================
 * "Previous Loc" picker (LocationsDB history) + Delete -- dedicated loop,
 * not the shared LocPickStringList engine, since it needs a 3rd
 * interactive element (Del) beyond select/find/cancel.
 * =================================================================== */

static UInt32 LocPickHistory (void) SEC002;
SEC002 static UInt32 LocPickHistory (void)
{
	FormPtr		prev = FrmGetActiveForm ();
	FormPtr		frm;
	ListPtr		lst;
	DmOpenRef	db;
	UInt16		total, all;
	MemHandle	nh, uh, ah, ph, oh;
	char*		names;
	UInt32*		uids;
	char**		allPtrs;
	char**		ptrs;
	UInt16*		orig;
	UInt16		shown = 0;
	UInt32		result = 0;
	Boolean		done = false;
	const UInt16 NL = kLocSummaryLen;
	UInt16		i;
	/* Db35: tap-to-select, tap-AGAIN-to-confirm -- a single tap on a row
	 * used to immediately pick it and close the dialog, which meant there
	 * was no way to just highlight a row to then hit Delete on it (a tap on
	 * the row you want to delete instantly filled the form with it
	 * instead). The ROM List Manager still highlights the tapped row and
	 * fires lstSelectEvent on every tap regardless -- only a SECOND tap on
	 * the row that's ALREADY selected should actually confirm+close. */
	Int16		lastSel = -1;

	/* Don't create the DB just to browse it -- check read-only first. */
	if (LocDbGetDatabase (&db, dmModeReadOnly) != 0 || ! db)
		return 0;
	total = LocDbCount (db);
	DmCloseDatabase (db);
	if (total == 0) return 0;

	if (LocDbGetDatabase (&db, dmModeReadWrite) != 0 || ! db)
		return 0;

	nh = MemHandleNew ((UInt32) total * NL);
	uh = MemHandleNew ((UInt32) total * sizeof (UInt32));
	ah = MemHandleNew ((UInt32) total * sizeof (char*));
	ph = MemHandleNew ((UInt32) total * sizeof (char*));
	oh = MemHandleNew ((UInt32) total * sizeof (UInt16));
	if (! nh || ! uh || ! ah || ! ph || ! oh)
		{
		if (nh) MemHandleFree (nh);
		if (uh) MemHandleFree (uh);
		if (ah) MemHandleFree (ah);
		if (ph) MemHandleFree (ph);
		if (oh) MemHandleFree (oh);
		DmCloseDatabase (db);
		return 0;
		}
	names   = (char*) MemHandleLock (nh);
	uids    = (UInt32*) MemHandleLock (uh);
	allPtrs = (char**) MemHandleLock (ah);
	ptrs    = (char**) MemHandleLock (ph);
	orig    = (UInt16*) MemHandleLock (oh);

	all = 0;
	for (i = 0; i < total; i++)
		{
		DlxLocation	loc;
		UInt32		uid = 0;
		if (! LocDbEnumerate (db, i, &uid, &loc)) continue;
		LocSummary (&loc, names + (UInt32) all * NL, NL);
		uids[all] = uid;
		all++;
		}
	for (i = 0; i < all; i++)
		allPtrs[i] = names + (UInt32) i * NL;

	frm = FrmInitForm (LocHistoryPickDialog);
	lst = (ListPtr) DlgObj (frm, lstLocHistoryPick);

	shown = LocFilterIdx ((const char* const*) allPtrs, all, "", ptrs, orig);
	LstSetListChoices (lst, ptrs, shown);
	LstSetSelection (lst, -1);

	FrmSetActiveForm (frm);
	FrmDrawForm (frm);
	FrmSetFocus (frm, FrmGetObjectIndex (frm, fldLocHistoryFind));

	while (! done)
		{
		EventType	ev;
		Boolean		refilter = false;

		EvtGetEvent (&ev, evtWaitForever);

		if (ev.eType == lstSelectEvent && ev.data.lstSelect.listID == lstLocHistoryPick)
			{
			Int16	sidx = ev.data.lstSelect.selection;
			if (sidx >= 0 && sidx < (Int16) shown)
				{
				if (sidx == lastSel)
					{
					/* second tap on the already-selected row -- confirm */
					result = uids[orig[sidx]];
					done = true;
					}
				else
					/* first tap -- the ROM already highlighted it; just
					 * remember it so a repeat tap on THIS row confirms */
					lastSel = sidx;
				}
			continue;
			}
		if (ev.eType == ctlSelectEvent)
			{
			UInt16	cid = ev.data.ctlSelect.controlID;

			if (cid == btnLocHistoryCancel) { done = true; continue; }
			if (cid == btnLocHistoryDelete)
				{
				Int16	sidx = LstGetSelection (lst);
				if (sidx >= 0 && sidx < (Int16) shown &&
					 FrmAlert (DeleteLocationAlert) == DeleteLocationOk)
					{
					LocDbDelete (db, uids[orig[sidx]]);
					/* Rebuild "all" from scratch -- simplest correct
					 * refresh for a short-lived modal dialog. */
					all = 0;
					for (i = 0; i < total; i++)
						{
						DlxLocation	loc;
						UInt32		uid = 0;
						if (! LocDbEnumerate (db, i, &uid, &loc)) continue;
						LocSummary (&loc, names + (UInt32) all * NL, NL);
						uids[all] = uid;
						all++;
						}
					for (i = 0; i < all; i++)
						allPtrs[i] = names + (UInt32) i * NL;
					refilter = true;
					}
				}
			}
		if (ev.eType == keyDownEvent)
			{
			if (! LocDlgSysMenu (&ev)) FrmDispatchEvent (&ev);
			refilter = true;
			}
		else if (ev.eType == appStopEvent)
			{
			EvtAddEventToQueue (&ev);
			done = true;
			continue;
			}
		else if (! refilter && ev.eType != ctlSelectEvent && ev.eType != lstSelectEvent)
			{
			if (! LocDlgSysMenu (&ev)) FrmDispatchEvent (&ev);
			}

		if (refilter)
			{
			const char*	t = FldGetTextPtr ((FieldPtr) DlgObj (frm, fldLocHistoryFind));
			shown = LocFilterIdx ((const char* const*) allPtrs, all, t ? t : "", ptrs, orig);
			LstSetListChoices (lst, ptrs, shown);
			LstSetSelection (lst, -1);
			LstDrawList (lst);
			lastSel = -1;	/* row indices just shifted -- forget the pending "which row was tapped" state */
			}
		}

	FrmEraseForm (frm);
	FrmDeleteForm (frm);
	if (prev) FrmSetActiveForm (prev);
	MemHandleUnlock (nh); MemHandleUnlock (uh); MemHandleUnlock (ah);
	MemHandleUnlock (ph); MemHandleUnlock (oh);
	MemHandleFree (nh); MemHandleFree (uh); MemHandleFree (ah);
	MemHandleFree (ph); MemHandleFree (oh);
	DmCloseDatabase (db);
	return result;
}

/* ===================================================================
 * Location edit dialog
 * =================================================================== */

static void LocFillForm (FormPtr frm, const DlxLocation* loc) SEC002;
SEC002 static void LocFillForm (FormPtr frm, const DlxLocation* loc)
{
	DlgSetField (frm, fldLocName,  loc->name);
	DlgSetField (frm, fldLocLine1, loc->line1);
	DlgSetField (frm, fldLocLine2, loc->line2);
	DlgSetField (frm, fldLocApt,   loc->apt);
	DlgSetField (frm, fldLocCity,  loc->city);
	DlgSetField (frm, fldLocState, loc->state);
	DlgSetField (frm, fldLocZip,   loc->zip);
}

/* LocFillForm's OWN callers before FrmDrawForm don't need a redraw (the
 * form draws fresh right after). But DlgSetField (FldSetTextHandle) never
 * redraws on its own -- calling LocFillForm again LATER, after the form is
 * already visible (post Address35/Previous-Loc pick), needs an explicit
 * FldDrawField per field or the new text never appears (matches Address35's
 * own convention -- EditForm.c calls FldDrawField after a picked-value
 * update, EditDialogs.c's own DlgSetField never does). */
static void LocFillFormRedraw (FormPtr frm, const DlxLocation* loc) SEC002;
SEC002 static void LocFillFormRedraw (FormPtr frm, const DlxLocation* loc)
{
	LocFillForm (frm, loc);
	FldDrawField ((FieldPtr) DlgObj (frm, fldLocName));
	FldDrawField ((FieldPtr) DlgObj (frm, fldLocLine1));
	FldDrawField ((FieldPtr) DlgObj (frm, fldLocLine2));
	FldDrawField ((FieldPtr) DlgObj (frm, fldLocApt));
	FldDrawField ((FieldPtr) DlgObj (frm, fldLocCity));
	FldDrawField ((FieldPtr) DlgObj (frm, fldLocState));
	FldDrawField ((FieldPtr) DlgObj (frm, fldLocZip));
}

static Int16 LocFindCountryIndex (const char* name) SEC002;
SEC002 static Int16 LocFindCountryIndex (const char* name)
{
	UInt16	i;
	if (! name || ! name[0]) return 0;
	for (i = 1; i < kLocCountryCount; i++)
		if (StrCompare ((char*) name, (char*) kLocCountries[i]) == 0) return (Int16) i;
	return 0;
}

SEC002 Boolean LocationEditForm (DlxLink* inOut)
{
	FormPtr		prev = FrmGetActiveForm ();
	FormPtr		frm = FrmInitForm (LocationDialog);
	ControlPtr	pop = (ControlPtr) DlgObj (frm, popLocCountry);
	Int16		curCountry;
	Boolean		fromAddr35 = (Boolean) inOut->fromAddress35;
	UInt32		pickedLocDbUID = inOut->locDbRecUID;
	Boolean		ok = false, done = false;
	DlxLocation	snapshot;					/* fields as of the last Address35 pick (or
											 * on entry) -- see the OK-time edit check below */

	LocFillForm (frm, &inOut->loc);
	snapshot = inOut->loc;
	if (inOut->loc.country[0])
		curCountry = LocFindCountryIndex (inOut->loc.country);
	else
		{
		curCountry = LocGetLastCountry ();
		if (curCountry < 0 || curCountry >= (Int16) kLocCountryCount) curCountry = 0;
		}
	CtlSetLabel (pop, kLocCountries[curCountry]);

	FrmSetActiveForm (frm);
	FrmDrawForm (frm);
	FrmSetFocus (frm, FrmGetObjectIndex (frm, fldLocName));

	while (! done)
		{
		EventType	ev;

		EvtGetEvent (&ev, evtWaitForever);

		if (ev.eType == ctlSelectEvent)
			{
			UInt16	cid = ev.data.ctlSelect.controlID;

			if (cid == popLocCountry)
				{
				Int16	sel = LocPickStringList ("Country", kLocCountries,
					(UInt16) kLocCountryCount, curCountry);
				if (sel >= 0)
					{
					curCountry = sel;
					CtlSetLabel (pop, kLocCountries[sel]);
					CtlDrawControl (pop);
					}
				continue;
				}
			if (cid == btnLocAddress35)
				{
				UInt32	uid = LocPickAddress35Contact ();
				if (uid)
					{
					DlxLocation	tmp;
					if (LocReadAddress35ByUID (uid, &tmp))
						{
						LocFillFormRedraw (frm, &tmp);
						curCountry = LocFindCountryIndex (tmp.country);
						CtlSetLabel (pop, kLocCountries[curCountry]);
						CtlDrawControl (pop);
						fromAddr35 = true;
						pickedLocDbUID = 0;
						snapshot = tmp;
						}
					}
				continue;
				}
			if (cid == btnLocPreviousLoc)
				{
				UInt32	uid = LocPickHistory ();
				if (uid)
					{
					DlxLocation	tmp;
					DmOpenRef	hdb;
					if (LocDbGetDatabase (&hdb, dmModeReadOnly) == 0 && hdb)
						{
						if (LocDbGet (hdb, uid, &tmp))
							{
							LocFillFormRedraw (frm, &tmp);
							curCountry = LocFindCountryIndex (tmp.country);
							CtlSetLabel (pop, kLocCountries[curCountry]);
							CtlDrawControl (pop);
							fromAddr35 = false;
							pickedLocDbUID = uid;
							}
						DmCloseDatabase (hdb);
						}
					}
				continue;
				}
			if (cid == btnLocOk) { ok = true; done = true; continue; }
			if (cid == btnLocClear)
				{
				/* Wipe every field on screen, then fall through to the SAME
				 * OK commit path below (which reads the fields back) -- one
				 * tap clears the event's location and closes, same as OK
				 * but on blanked fields rather than whatever was typed. */
				DlxLocation	empty;
				MemSet (&empty, sizeof (empty), 0);
				LocFillFormRedraw (frm, &empty);
				curCountry = 0;
				CtlSetLabel (pop, kLocCountries[0]);
				CtlDrawControl (pop);
				fromAddr35 = false;
				pickedLocDbUID = 0;
				ok = true; done = true;
				continue;
				}
			if (cid == btnLocCancel) { done = true; continue; }
			}
		if (ev.eType == appStopEvent)
			{
			EvtAddEventToQueue (&ev);
			done = true;
			continue;
			}
		if (! LocDlgSysMenu (&ev))
			FrmDispatchEvent (&ev);
		}

	if (ok)
		{
		LocReadField (frm, fldLocName,  inOut->loc.name,  sizeof (inOut->loc.name));
		LocReadField (frm, fldLocLine1, inOut->loc.line1, sizeof (inOut->loc.line1));
		LocReadField (frm, fldLocLine2, inOut->loc.line2, sizeof (inOut->loc.line2));
		LocReadField (frm, fldLocApt,   inOut->loc.apt,   sizeof (inOut->loc.apt));
		LocReadField (frm, fldLocCity,  inOut->loc.city,  sizeof (inOut->loc.city));
		LocReadField (frm, fldLocState, inOut->loc.state, sizeof (inOut->loc.state));
		LocReadField (frm, fldLocZip,   inOut->loc.zip,   sizeof (inOut->loc.zip));

		if (curCountry > 0)
			{
			StrNCopy (inOut->loc.country, kLocCountries[curCountry], sizeof (inOut->loc.country) - 1);
			inOut->loc.country[sizeof (inOut->loc.country) - 1] = '\0';
			}
		else
			inOut->loc.country[0] = '\0';

		/* Db35: an Address35-sourced location the user then edited (any field,
		 * not just Name) should behave like a typed/history location from here
		 * on -- saved to LocationsDB for reuse. Never writes back to
		 * Address35DB itself either way -- that stays untouched regardless. */
		if (fromAddr35 && MemCmp (&inOut->loc, &snapshot, sizeof (DlxLocation)) != 0)
			{
			fromAddr35 = false;
			pickedLocDbUID = 0;
			}

		inOut->fromAddress35 = (UInt8) (fromAddr35 ? 1 : 0);
		inOut->locDbRecUID   = pickedLocDbUID;

		LocSetLastCountry (curCountry);
		}

	FrmEraseForm (frm);
	FrmDeleteForm (frm);
	if (prev) FrmSetActiveForm (prev);
	return ok;
}

/* ===================================================================
 * Details-dialog Location field glue (see DateLocation.h). gCurrentLoc is
 * the ONLY state this needs -- DateDay.c never touches it directly, only
 * through LocGadgetLoad/LocFieldTapped/LocGadgetCommit below.
 * =================================================================== */

static DlxLink gCurrentLoc;

/* Truncate `text` (mutated in place, buffer size bufLen) with a trailing
 * "..." if it needs more than maxLines lines to display at fld's own
 * current width/font -- LocSummary itself now returns the FULL comma-joined
 * address (every filled field, per user request), which routinely won't
 * fit the Details dialog's small 2-line preview; Day View shows the
 * untruncated text instead (multi-line, via DayViewGetDescriptionHeight's
 * own wrap/grow -- unaffected, this only touches the Details field's copy). */
static void LocEllipsize (FieldPtr fld, char* text, UInt16 bufLen, Int16 maxLines) SEC002;
SEC002 static void LocEllipsize (FieldPtr fld, char* text, UInt16 bufLen, Int16 maxLines)
{
	RectangleType	r;
	FontID			curFont;
	Int16				maxHeight;
	UInt16			len;

	FldGetBounds (fld, &r);
	curFont = FntSetFont (FldGetFont (fld));
	maxHeight = (Int16) (FntLineHeight () * maxLines);

	if (FldCalcFieldHeight (text, r.extent.x) > maxHeight)
		{
		len = StrLen (text);
		if (len > (UInt16) (bufLen - 4)) len = (UInt16) (bufLen - 4);
		while (len > 0)
			{
			text[len] = 0;
			StrCat (text, "...");
			if (FldCalcFieldHeight (text, r.extent.x) <= maxHeight) break;
			len--;
			}
		}

	FntSetFont (curFont);
}

/* Set the Location field's DISPLAYED text from gCurrentLoc -- a
 * placeholder when blank (an empty underlined field gives no hint it's
 * special/tappable), the (possibly ellipsized) summary otherwise. Uses the
 * same DlgSetField helper as every other field in this file. */
static void LocFieldRefresh (FormPtr frm) SEC002;
SEC002 static void LocFieldRefresh (FormPtr frm)
{
	char	buf[kLocSummaryLen];

	if (LocIsBlank (&gCurrentLoc.loc))
		StrCopy (buf, "Tap to set location");
	else
		{
		LocSummary (&gCurrentLoc.loc, buf, sizeof (buf));
		LocEllipsize ((FieldPtr) DlgObj (frm, DetailsLocationGadget), buf, sizeof (buf), 2);
		}
	DlgSetField (frm, DetailsLocationGadget, buf);
}

void LocGadgetLoad (FormPtr frm, UInt32 apptUid) SEC002;
SEC002 void LocGadgetLoad (FormPtr frm, UInt32 apptUid)
{
	DmOpenRef	db;

	if (LinkGetDatabase (&db, dmModeReadOnly) == 0 && db)
		{
		LinkGet (db, apptUid, &gCurrentLoc);
		DmCloseDatabase (db);
		}
	else
		MemSet (&gCurrentLoc, sizeof (gCurrentLoc), 0);

	LocFieldRefresh (frm);
}

/* Called from DetailsHandleEvent's fldEnterEvent case -- the field must
 * NEVER actually enter inline edit mode, so the caller marks the event
 * handled unconditionally (this always returns true). */
Boolean LocFieldTapped (FormPtr frm) SEC002;
SEC002 Boolean LocFieldTapped (FormPtr frm)
{
	if (LocationEditForm (&gCurrentLoc))
		{
		// Db35: the Details form is already visible at this point --
		// DlgSetField (FldSetTextHandle) alone never redraws an
		// already-drawn field, so an explicit FldDrawField is required
		// here (unlike LocGadgetLoad's call, which runs before
		// FrmDrawForm and needs none).
		LocFieldRefresh (frm);
		FldDrawField ((FieldPtr) DlgObj (frm, DetailsLocationGadget));
		}
	return true;
}

void LocGadgetCommit (UInt32 apptUid) SEC002;
SEC002 void LocGadgetCommit (UInt32 apptUid)
{
	DmOpenRef	linkDb;

	if (LinkGetDatabase (&linkDb, dmModeReadWrite) != 0 || ! linkDb)
		return;

	if (LocIsBlank (&gCurrentLoc.loc))
		{
		LinkDelete (linkDb, apptUid);
		DmCloseDatabase (linkDb);
		return;
		}

	/* Explicit-link rules (locdb.h): Address35-sourced locations are never
	 * written to LocationsDB; a history-sourced (or since-edited) location
	 * upserts its LocationsDB entry in place; a freshly-typed location
	 * (never picked) creates a new entry and remembers its uid. */
	if (! gCurrentLoc.fromAddress35)
		{
		DmOpenRef	histDb;
		if (LocDbGetDatabase (&histDb, dmModeReadWrite) == 0 && histDb)
			{
			if (! (gCurrentLoc.locDbRecUID &&
					 LocDbUpdate (histDb, gCurrentLoc.locDbRecUID, &gCurrentLoc.loc)))
				gCurrentLoc.locDbRecUID = LocDbAdd (histDb, &gCurrentLoc.loc);
			DmCloseDatabase (histDb);
			}
		}

	LinkSet (linkDb, apptUid, &gCurrentLoc);
	DmCloseDatabase (linkDb);
}

/* ===================================================================
 * Day View "@ location" suffix (see DateLocation.h)
 * =================================================================== */

MemHandle DayBuildLocSuffix (const char* desc, const char* locSummary) SEC002;
SEC002 MemHandle DayBuildLocSuffix (const char* desc, const char* locSummary)
{
	MemHandle	h;
	char*		p;
	UInt16		len;

	len = (UInt16) (StrLen (desc) + 3 + StrLen (locSummary) + 1);	/* " @ " + NUL */
	h = MemHandleNew (len);
	if (! h) return NULL;
	p = (char*) MemHandleLock (h);
	StrCopy (p, desc);
	StrCat (p, " @ ");
	StrCat (p, locSummary);
	MemHandleUnlock (h);
	return h;
}
