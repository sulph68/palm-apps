/*
 * EditDialogs.c - assist dialogs for the edit form's richer field types.
 *
 * A35PickList: a reusable modal "searchable dropdown" - a list with type-ahead
 * (Graffiti/keyboard the first letters to jump to the match) plus a scrollbar,
 * run by its own nested event loop so it returns a chosen index synchronously.
 * Shared by the Email/Social/Address(Country)/Anniversary dialogs.
 *
 * EditEmailDialog: username field + domain dropdown (A35PickList) + an "Others"
 * checkbox that reveals a custom-domain field; returns "user@domain".
 */
#include <PalmOS.h>
#include "Main.h"

#define a35EditValLen	256		/* MUST match EditForm.c's field value cap (256) */

/* Modal-dialog event pump: like the main EventLoop, run SysHandleEvent AND MenuHandleEvent
 * before FrmDispatchEvent, so the command stroke / command toolbar and the Edit-menu
 * shortcuts (Cut/Copy/Paste) work in these dialogs' text fields.  (Each dialog form carries
 * MENUID mnuDlgEdit.)  Returns true if the event was consumed by the system/menu layer. */
static Boolean A35DlgSysMenu(EventPtr ev) SEC001;
static Boolean A35DlgSysMenu(EventPtr ev)
{
	UInt16 error;
	if (SysHandleEvent(ev)) return true;
	if (MenuHandleEvent(0, ev, &error)) return true;
	return false;
}

/* Ad35 seg: this whole file is sec001 (foreground-only dialogs). Its statics are
 * forward-declared with SEC001 so both their placement and any forward call use
 * the right segment (see the family's multiseg forward-decl gotcha). */
static void DlgLower(char *s) SEC001;
static void *DlgObj(FormPtr f, UInt16 id) SEC001;
static void DlgSetField(FormPtr f, UInt16 id, const char *s) SEC001;
static UInt16 PickFilterIdx(const char *const *items, UInt16 count, const char *term,
	char **ptrs, UInt16 *orig) SEC001;
static void AddrGetField(const char *src, UInt16 idx, char *out, UInt16 outLen) SEC001;
static Int16 DlgYearOf(FormPtr f) SEC001;
static Int16 DateDaysInMon(Int16 mon1, Int16 year) SEC001;
static void DateClamp(FormPtr f) SEC001;
static void DateBump(FormPtr f, UInt16 field, Int16 delta) SEC001;
static void NoteSyncScroll(FormPtr frm, FieldPtr fld) SEC001;
static void DateShowMonth(FormPtr f) SEC001;
static void DateShowDay(FormPtr f) SEC001;
static void DateShowYear(FormPtr f, Boolean blank) SEC001;
static void DlgMoveTo(FormPtr f, UInt16 id, Int16 x, Int16 y) SEC001;
static void DlgShowHide(FormPtr f, UInt16 id, Boolean show) SEC001;
static void DateLayout(FormPtr f, Boolean isAnniv) SEC001;
static void DlgTypeOther(FormPtr f, Boolean other) SEC001;
static void DlgYearUsable(FormPtr f, Boolean known) SEC001;
static void RelSetContactLabel(FormPtr f, UInt32 uid) SEC001;
static UInt16 RelFilter(const char *names, UInt16 nl, const UInt32 *uids,
	UInt16 all, const char *term, char **ptrs, UInt32 *shownUids) SEC001;
static UInt32 RelPickContact(UInt32 selfUid) SEC001;

/* ---- tiny helpers ------------------------------------------------------ */
static void DlgLower(char *s)
{
	for (; *s; s++)
		if (*s >= 'A' && *s <= 'Z')
			*s = (char) (*s + 32);
}

static void *DlgObj(FormPtr f, UInt16 id)
{
	return FrmGetObjectPtr(f, FrmGetObjectIndex(f, id));
}

static void DlgSetField(FormPtr f, UInt16 id, const char *s)
{
	FieldPtr fld = (FieldPtr) DlgObj(f, id);
	UInt16 len = s ? StrLen(s) : 0;
	MemHandle h, old;
	char *p;

	if (fld == NULL)
		return;
	h = MemHandleNew((UInt32) (len + 1));
	if (h == NULL)
		return;
	p = (char *) MemHandleLock(h);
	if (s) StrCopy(p, s); else p[0] = '\0';
	MemHandleUnlock(h);
	old = FldGetTextHandle(fld);
	FldSetTextHandle(fld, h);
	if (old)
		MemHandleFree(old);
}

/* substring-filter (case-insensitive) `items` into `ptrs`, recording each kept
 * item's ORIGINAL index in `orig` (parallel array) so the caller can map a
 * selection in the filtered list back to the caller's index space. */
static UInt16 PickFilterIdx(const char *const *items, UInt16 count, const char *term,
	char **ptrs, UInt16 *orig)
{
	char lt[40];
	UInt16 i, n = 0, tl;

	StrNCopy(lt, term ? term : "", sizeof(lt) - 1);
	lt[sizeof(lt) - 1] = '\0';
	DlgLower(lt);
	tl = StrLen(lt);
	for (i = 0; i < count; i++) {
		Boolean hit = (Boolean) (tl == 0);

		if (!hit) {
			char buf[72];
			StrNCopy(buf, items[i], sizeof(buf) - 1);
			buf[sizeof(buf) - 1] = '\0';
			DlgLower(buf);
			hit = (Boolean) (StrStr(buf, lt) != NULL);
		}
		if (hit) { ptrs[n] = (char *) items[i]; orig[n] = i; n++; }
	}
	return n;
}

/* ---- reusable searchable list picker ------------------------------------ *
 * Full-screen list + a "Find:" field/button (mirrors RelPickContact's Contact
 * Picker): every keystroke live-filters `items` by substring, case-insensitive.
 * Returns the chosen item's index in the CALLER's `items` array, or -1. */
Int16 A35PickList(const char *title, const char *const *items, UInt16 count,
	Int16 initial)
{
	FormPtr prev = FrmGetActiveForm();
	FormPtr frm = FrmInitForm(frmPick);
	ListPtr lst = (ListPtr) DlgObj(frm, lstPick);
	Int16 result = -1;
	Boolean done = false;
	UInt16 shown;
	MemHandle ph, ih;
	char **ptrs;
	UInt16 *orig;

	if (title)
		FrmSetTitle(frm, (char *) title);
	if (count == 0) { FrmDeleteForm(frm); return -1; }

	ph = MemHandleNew((UInt32) count * sizeof(char *));
	ih = MemHandleNew((UInt32) count * sizeof(UInt16));
	if (ph == NULL || ih == NULL) {
		if (ph) MemHandleFree(ph);
		if (ih) MemHandleFree(ih);
		FrmDeleteForm(frm);
		return -1;
	}
	ptrs = (char **) MemHandleLock(ph);
	orig = (UInt16 *) MemHandleLock(ih);

	if (initial < 0 || initial >= (Int16) count)
		initial = 0;
	shown = PickFilterIdx(items, count, "", ptrs, orig);	/* unfiltered: orig[i]==i */
	LstSetListChoices(lst, ptrs, shown);
	LstSetSelection(lst, initial);

	FrmSetActiveForm(frm);
	FrmDrawForm(frm);
	LstMakeItemVisible(lst, initial);	/* scroll so the current pick shows */
	LstDrawList(lst);					/* ...and redraw at that position */
	FrmSetFocus(frm, FrmGetObjectIndex(frm, fldPickFind));

	while (!done) {
		EventType ev;
		Boolean refilter = false;

		EvtGetEvent(&ev, evtWaitForever);

		if (ev.eType == lstSelectEvent &&
			ev.data.lstSelect.listID == lstPick) {
			Int16 sidx = ev.data.lstSelect.selection;
			if (sidx >= 0 && sidx < (Int16) shown)
				result = (Int16) orig[sidx];
			done = true;
			continue;
		}
		if (ev.eType == ctlSelectEvent) {
			UInt16 cid = ev.data.ctlSelect.controlID;
			if (cid == btnPickCancel) { result = -1; done = true; continue; }
		}
		if (ev.eType == keyDownEvent) {		/* live filter as the user types */
			if (!A35DlgSysMenu(&ev))
				FrmDispatchEvent(&ev);			/* field consumes the key first */
			refilter = true;
		} else if (ev.eType == appStopEvent) {
			EvtAddEventToQueue(&ev);
			result = -1;
			done = true;
			continue;
		} else if (!refilter && ev.eType != ctlSelectEvent) {
			if (!A35DlgSysMenu(&ev))
				FrmDispatchEvent(&ev);
		}
		if (refilter) {
			const char *t = FldGetTextPtr((FieldPtr) DlgObj(frm, fldPickFind));
			shown = PickFilterIdx(items, count, t ? t : "", ptrs, orig);
			LstSetListChoices(lst, ptrs, shown);
			LstSetSelection(lst, -1);
			LstDrawList(lst);
		}
	}

	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prev)
		FrmSetActiveForm(prev);
	MemHandleUnlock(ph); MemHandleUnlock(ih);
	MemHandleFree(ph); MemHandleFree(ih);
	return result;
}

/* ---- Email dialog ------------------------------------------------------ */
static const char * const kEmailDomains[] = {
	"gmail.com", "mac.com", "icloud.com", "outlook.com", "yahoo.com",
	"hotmail.com", "proton.me", "protonmail.com", "fastmail.com"
};
#define kEmailDomainCount	9

static const char * const kEmailLabels[] = { "Home", "Work", "iCloud", "Other" };
#define kEmailLabelCount	4

const char *EditEmailLabelName(UInt8 sub)
{
	return (sub < kEmailLabelCount) ? kEmailLabels[sub] : kEmailLabels[0];
}

Boolean EditEmailDialog(char *value, UInt16 maxLen, UInt8 *labelP)
{
	FormPtr prev = FrmGetActiveForm();
	FormPtr frm = FrmInitForm(frmEmailDlg);
	ControlPtr pop = (ControlPtr) DlgObj(frm, popEmailDomain);
	ControlPtr labelPop = (ControlPtr) DlgObj(frm, popEmailLabel);
	ControlPtr cb = (ControlPtr) DlgObj(frm, cbEmailOther);
	UInt16 customIdx = FrmGetObjectIndex(frm, fldEmailCustom);
	UInt16 phIdx = FrmGetObjectIndex(frm, lblEmailOther);	/* "Other" placeholder */
	Int16 curIdx = 0;
	UInt8 labelIdx = (labelP && *labelP < kEmailLabelCount) ? *labelP : 0;
	Boolean other = false, ok = false, done = false;
	char userbuf[81], dombuf[61];

	/* split existing "user@domain" */
	userbuf[0] = dombuf[0] = '\0';
	if (value && value[0]) {
		const char *at = StrChr(value, '@');
		if (at) {
			UInt16 un = (UInt16) (at - value);
			if (un > sizeof(userbuf) - 1) un = sizeof(userbuf) - 1;
			MemMove(userbuf, value, un);
			userbuf[un] = '\0';
			StrNCopy(dombuf, at + 1, sizeof(dombuf) - 1);
			dombuf[sizeof(dombuf) - 1] = '\0';
		} else {
			StrNCopy(userbuf, value, sizeof(userbuf) - 1);
			userbuf[sizeof(userbuf) - 1] = '\0';
		}
	}
	DlgSetField(frm, fldEmailUser, userbuf);
	if (dombuf[0]) {
		Int16 i;
		for (i = 0; i < kEmailDomainCount; i++)
			if (StrCompare(dombuf, kEmailDomains[i]) == 0) { curIdx = i; break; }
		if (i >= kEmailDomainCount) {			/* unknown -> Others */
			other = true;
			DlgSetField(frm, fldEmailCustom, dombuf);
		}
	}
	LstSetSelection((ListPtr) DlgObj(frm, lstEmailDomain), curIdx);
	CtlSetLabel(pop, kEmailDomains[curIdx]);
	LstSetSelection((ListPtr) DlgObj(frm, lstEmailLabel), labelIdx);
	CtlSetLabel(labelPop, kEmailLabels[labelIdx]);
	CtlSetValue(cb, (Int16) other);

	FrmSetActiveForm(frm);
	/* the custom-domain field shows a fake "Other" placeholder (a label) when
	 * unchecked; checking reveals + focuses the field. */
	if (other) { FrmShowObject(frm, customIdx); FrmHideObject(frm, phIdx); }
	else { FrmHideObject(frm, customIdx); FrmShowObject(frm, phIdx); }
	FrmDrawForm(frm);
	FrmSetFocus(frm, FrmGetObjectIndex(frm, fldEmailUser));

	while (!done) {
		EventType ev;

		EvtGetEvent(&ev, evtWaitForever);

		/* domain/label popups are inline (category-style) bound popups ->
		 * popSelect. Record the choice but fall through to FrmDispatchEvent so
		 * the ROM's default handler updates the trigger's visible label. */
		if (ev.eType == popSelectEvent &&
			ev.data.popSelect.controlID == popEmailDomain)
			curIdx = ev.data.popSelect.selection;
		if (ev.eType == popSelectEvent &&
			ev.data.popSelect.controlID == popEmailLabel)
			labelIdx = (UInt8) ev.data.popSelect.selection;
		if (ev.eType == ctlSelectEvent) {
			UInt16 cid = ev.data.ctlSelect.controlID;

			if (cid == cbEmailOther) {
				other = (Boolean) CtlGetValue(cb);
				if (other) {
					FrmHideObject(frm, phIdx);
					FrmShowObject(frm, customIdx);
					FrmSetFocus(frm, customIdx);
				} else {
					FrmHideObject(frm, customIdx);
					FrmShowObject(frm, phIdx);
				}
				continue;
			}
			if (cid == btnEmailOK) { ok = true; done = true; continue; }
			if (cid == btnEmailCancel) { done = true; continue; }
		}
		if (ev.eType == appStopEvent) {
			EvtAddEventToQueue(&ev);
			done = true;
			continue;
		}
		if (!A35DlgSysMenu(&ev))
			FrmDispatchEvent(&ev);
	}

	if (ok) {
		const char *u = FldGetTextPtr((FieldPtr) DlgObj(frm, fldEmailUser));
		const char *d = other
			? FldGetTextPtr((FieldPtr) DlgObj(frm, fldEmailCustom))
			: kEmailDomains[curIdx];
		char out[a35EditValLen];

		out[0] = '\0';
		if (u && u[0]) {
			StrNCopy(out, u, sizeof(out) - 1);
			out[sizeof(out) - 1] = '\0';
		}
		if (d && d[0]) {
			StrCat(out, "@");
			StrNCat(out, d, (Int16) sizeof(out));
		}
		StrNCopy(value, out, maxLen - 1);
		value[maxLen - 1] = '\0';
		if (labelP) *labelP = labelIdx;
	}

	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prev)
		FrmSetActiveForm(prev);
	return ok;
}

/* ---- Address dialog ---------------------------------------------------- */
/* value on disk is TAB-delimited: Line1 \t Line2 \t Apt \t City \t State \t Zip
 * \t Country.  Country is chosen from a searchable A35PickList. */
static const char * const kCountries[] = {
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
#define kCountryCount	(sizeof(kCountries) / sizeof(kCountries[0]))

/* ITU calling codes, aligned 1:1 with kCountries (index 0 = "(None)" -> "").
 * Used by the phone dial-code picker; the address country picker ignores it. */
static const char * const kDialCodes[] = {
	"",
	"+93", "+355", "+213", "+376", "+244",
	"+1", "+54", "+374", "+61", "+43",
	"+994", "+1", "+973", "+880", "+1", "+375",
	"+32", "+501", "+229", "+975", "+591",
	"+387", "+267", "+55", "+673", "+359",
	"+226", "+257", "+855", "+237", "+1",
	"+238", "+236", "+235", "+56", "+86",
	"+57", "+269", "+242", "+506", "+385", "+53",
	"+357", "+420", "+45", "+253", "+1",
	"+1", "+593", "+20", "+503",
	"+240", "+291", "+372", "+268", "+251",
	"+679", "+358", "+33", "+241", "+220", "+995", "+49",
	"+233", "+30", "+1", "+502", "+224", "+245",
	"+592", "+509", "+504", "+852", "+36", "+354",
	"+91", "+62", "+98", "+964", "+353", "+972", "+39",
	"+225", "+1", "+81", "+962", "+7", "+254",
	"+686", "+383", "+965", "+996", "+856", "+371",
	"+961", "+266", "+231", "+218", "+423", "+370",
	"+352", "+853", "+261", "+265", "+60", "+960",
	"+223", "+356", "+692", "+222", "+230",
	"+52", "+691", "+373", "+377", "+976", "+382",
	"+212", "+258", "+95", "+264", "+674", "+977",
	"+31", "+64", "+505", "+227", "+234",
	"+850", "+389", "+47", "+968", "+92",
	"+680", "+970", "+507", "+675", "+595", "+51",
	"+63", "+48", "+351", "+974", "+40", "+7",
	"+250", "+1", "+685", "+378", "+966",
	"+221", "+381", "+248", "+232", "+65",
	"+421", "+386", "+677", "+252", "+27",
	"+82", "+211", "+34", "+94", "+249", "+597",
	"+46", "+41", "+963", "+886", "+992", "+255",
	"+66", "+670", "+228", "+676", "+1868",
	"+216", "+90", "+993", "+688", "+256", "+380",
	"+971", "+44", "+1", "+598",
	"+998", "+678", "+379", "+58", "+84",
	"+967", "+260", "+263"
};
/* compile-time guard: kDialCodes must line up 1:1 with kCountries */
typedef char kDialCount_check[(sizeof(kDialCodes) / sizeof(kDialCodes[0]) ==
	kCountryCount) ? 1 : -1];

/* dial code string for a country index (0/"(None)" -> "") */
const char *A35DialCode(Int16 idx)
{
	if (idx <= 0 || idx >= (Int16) kCountryCount)
		return "";
	return kDialCodes[idx];
}

/* first country index whose dial code equals `code` (e.g. "+66"); 0 if none */
Int16 A35DialIndexOf(const char *code)
{
	UInt16 i;

	if (code == NULL || code[0] == '\0')
		return 0;
	for (i = 1; i < kCountryCount; i++)
		if (StrCompare(kDialCodes[i], code) == 0)
			return (Int16) i;
	return 0;
}

/* searchable picker of "Country (+NN)" rows ("(None)" first); returns the chosen
 * country index, or -1 on cancel.  Strings are built on the heap for A35PickList. */
Int16 A35DialPickList(Int16 initial)
{
	char **items = (char **) MemPtrNew((UInt32) kCountryCount * sizeof(char *));
	Int16 sel;
	UInt16 i;

	if (items == NULL)
		return -1;
	for (i = 0; i < kCountryCount; i++) {
		const char *nm = kCountries[i];
		const char *dc = kDialCodes[i];
		UInt16 len = (UInt16) (StrLen(nm) + (dc[0] ? StrLen(dc) + 4 : 0) + 1);
		char *s = (char *) MemPtrNew((UInt32) len);

		if (s != NULL) {
			StrCopy(s, nm);
			if (dc[0]) { StrCat(s, " ("); StrCat(s, dc); StrCat(s, ")"); }
		}
		items[i] = s ? s : (char *) nm;		/* fallback: the bare name */
	}
	sel = A35PickList("Dial code", (const char * const *) items,
		kCountryCount, initial);
	for (i = 0; i < kCountryCount; i++)
		if (items[i] != NULL && items[i] != (char *) kCountries[i])
			MemPtrFree(items[i]);
	MemPtrFree(items);
	return sel;
}

/* copy TAB-delimited field #idx of src into out */
static void AddrGetField(const char *src, UInt16 idx, char *out, UInt16 outLen)
{
	const char *p = src ? src : "";
	UInt16 field = 0, o = 0;

	out[0] = '\0';
	while (*p && field < idx) { if (*p == '\t') field++; p++; }
	if (field < idx) return;
	while (*p && *p != '\t' && o < (UInt16) (outLen - 1))
		out[o++] = *p++;
	out[o] = '\0';
}

/* MUST be file-scope: a function-local `static const` inside a SEC001 function is
 * emitted into the sec001 CODE segment yet accessed A5-relative (like .data), so
 * its address is wrong once sec001 moves -> garbage field ids -> crash. File-scope
 * static const goes to .data (A5-relative) correctly, like every other array here. */
static const UInt16 kAddrFldId[6] = { fldAddrLine1, fldAddrLine2, fldAddrApt,
	fldAddrCity, fldAddrState, fldAddrZip };

Boolean EditAddressDialog(char *value, UInt16 maxLen)
{
	FormPtr prev = FrmGetActiveForm();
	FormPtr frm = FrmInitForm(frmAddrDlg);
	ControlPtr pop = (ControlPtr) DlgObj(frm, popAddrCountry);
	Int16 curCountry = 0;
	Boolean ok = false, done = false;
	char buf[96];			/* a single address part (long line/city/province) */
	UInt16 i;

	for (i = 0; i < 6; i++) {
		AddrGetField(value, i, buf, sizeof(buf));
		DlgSetField(frm, kAddrFldId[i], buf);
	}
	AddrGetField(value, 6, buf, sizeof(buf));		/* country name */
	if (buf[0]) {
		for (i = 1; i < kCountryCount; i++)
			if (StrCompare(buf, kCountries[i]) == 0) { curCountry = i; break; }
	} else if (value == NULL || value[0] == '\0') {
		curCountry = A35GetLastCountry();		/* new address -> last-used country */
		if (curCountry < 0 || curCountry >= (Int16) kCountryCount)
			curCountry = 0;
	}
	CtlSetLabel(pop, kCountries[curCountry]);

	FrmSetActiveForm(frm);
	FrmDrawForm(frm);
	FrmSetFocus(frm, FrmGetObjectIndex(frm, fldAddrLine1));

	while (!done) {
		EventType ev;

		EvtGetEvent(&ev, evtWaitForever);

		if (ev.eType == ctlSelectEvent) {
			UInt16 cid = ev.data.ctlSelect.controlID;

			if (cid == popAddrCountry) {
				Int16 sel = A35PickList("Country", kCountries,
					(UInt16) kCountryCount, curCountry);
				if (sel >= 0) {
					curCountry = sel;
					CtlSetLabel(pop, kCountries[sel]);
					CtlDrawControl(pop);
				}
				continue;
			}
			if (cid == btnAddrOK) { ok = true; done = true; continue; }
			if (cid == btnAddrCancel) { done = true; continue; }
		}
		if (ev.eType == appStopEvent) {
			EvtAddEventToQueue(&ev);
			done = true;
			continue;
		}
		if (!A35DlgSysMenu(&ev))
			FrmDispatchEvent(&ev);
	}

	if (ok) {
		char out[a35EditValLen];
		const char *parts[7];
		Boolean any = false;

		for (i = 0; i < 6; i++)
			parts[i] = FldGetTextPtr((FieldPtr) DlgObj(frm, kAddrFldId[i]));
		parts[6] = (curCountry > 0) ? kCountries[curCountry] : "";

		out[0] = '\0';
		for (i = 0; i < 7; i++) {
			if (i)
				StrNCat(out, "\t", (Int16) sizeof(out));
			if (parts[i] && parts[i][0]) {
				StrNCat(out, parts[i], (Int16) sizeof(out));
				any = true;
			}
		}
		if (!any)
			out[0] = '\0';		/* wholly empty -> drop the field */
		StrNCopy(value, out, maxLen - 1);
		value[maxLen - 1] = '\0';
		A35SetLastCountry(curCountry);	/* remember for the next new address */
	}

	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prev)
		FrmSetActiveForm(prev);
	return ok;
}

/* ---- Phone dialog (half-screen; sidesteps the inline chip's scroll-redraw
 * artifacts by keeping the dial-code chip on its own dedicated row) --------- *
 * value = the bare number (no dial code prefix); *dialP = country index into
 * kCountries/kDialCodes (0 = "(None)"), in/out.  A "(None)" chip reads "Select
 * Dial Code" (a bare "+" would misleadingly imply a code is already set). */
Boolean EditPhoneDialog(char *value, UInt16 maxLen, UInt8 *dialP)
{
	FormPtr prev = FrmGetActiveForm();
	FormPtr frm = FrmInitForm(frmPhoneDlg);
	ControlType *chip = (ControlType *) DlgObj(frm, selPhoneDial);
	Int16 curDial = (dialP != NULL) ? (Int16) *dialP : 0;
	Boolean ok = false, done = false;
	char dialText[18];		/* persistent: CtlSetLabel keeps the pointer, not a copy */

	if (curDial <= 0 && (value == NULL || value[0] == '\0'))
		curDial = A35GetLastDial();	/* new/empty phone -> last-used dial code */
	if (curDial < 0 || curDial >= (Int16) kCountryCount)
		curDial = 0;

	DlgSetField(frm, fldPhoneNumber, value);
	{
		const char *dc = A35DialCode(curDial);
		StrNCopy(dialText, dc[0] ? dc : "Select Dial Code", sizeof(dialText) - 1);
		dialText[sizeof(dialText) - 1] = '\0';
		CtlSetLabel(chip, dialText);
	}

	FrmSetActiveForm(frm);
	FrmDrawForm(frm);
	FrmSetFocus(frm, FrmGetObjectIndex(frm, fldPhoneNumber));

	while (!done) {
		EventType ev;

		EvtGetEvent(&ev, evtWaitForever);

		if (ev.eType == ctlSelectEvent) {
			UInt16 cid = ev.data.ctlSelect.controlID;

			if (cid == selPhoneDial) {
				Int16 sel = A35DialPickList(curDial);
				if (sel >= 0) {
					const char *dc;
					curDial = sel;
					dc = A35DialCode(curDial);
					StrNCopy(dialText, dc[0] ? dc : "Select Dial Code",
						sizeof(dialText) - 1);
					dialText[sizeof(dialText) - 1] = '\0';
					CtlSetLabel(chip, dialText);
					CtlDrawControl(chip);
				}
				continue;
			}
			if (cid == btnPhoneOK) { ok = true; done = true; continue; }
			if (cid == btnPhoneCancel) { done = true; continue; }
		}
		if (ev.eType == appStopEvent) {
			EvtAddEventToQueue(&ev);
			done = true;
			continue;
		}
		if (!A35DlgSysMenu(&ev))
			FrmDispatchEvent(&ev);
	}

	if (ok) {
		const char *nt = FldGetTextPtr((FieldPtr) DlgObj(frm, fldPhoneNumber));
		StrNCopy(value, nt ? nt : "", maxLen - 1);
		value[maxLen - 1] = '\0';
		if (dialP != NULL) *dialP = (UInt8) curDial;
		if (curDial > 0)
			A35SetLastDial(curDial);	/* remember for the next new phone */
	}

	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prev)
		FrmSetActiveForm(prev);
	return ok;
}

/* ---- Note editor (full-screen, scrollable) ----------------------------- *
 * The note can be long (up to the field's MAXCHARS), so it lives in its own
 * MemHandle rather than the edit form's 256-byte per-field buffer.  *noteH is
 * in/out: read on entry, and on Done replaced with a fresh handle holding the
 * edited text (the old handle is freed).  Returns true if the user tapped Done. */
static void NoteSyncScroll(FormPtr frm, FieldPtr fld)
{
	UInt16 scrollPos, textHeight, fieldHeight;
	Int16 maxValue;
	ScrollBarPtr scl = (ScrollBarPtr) DlgObj(frm, sclNote);

	FldGetScrollValues(fld, &scrollPos, &textHeight, &fieldHeight);
	if (textHeight > fieldHeight)
		maxValue = (Int16) (textHeight - fieldHeight);
	else if (scrollPos)
		maxValue = (Int16) scrollPos;
	else
		maxValue = 0;
	SclSetScrollBar(scl, (Int16) scrollPos, 0, maxValue, (Int16) fieldHeight);
}

Boolean EditNoteDialog(MemHandle *noteH)
{
	FormPtr prev = FrmGetActiveForm();
	FormPtr frm = FrmInitForm(frmNoteDlg);
	FieldPtr fld = (FieldPtr) DlgObj(frm, fldNoteText);
	Boolean ok = false, done = false;

	{	/* give the field its own text handle, seeded from the current note */
		UInt16 len = 0;
		char *src = NULL;
		MemHandle th;
		char *p;

		if (*noteH) { src = (char *) MemHandleLock(*noteH); len = (UInt16) StrLen(src); }
		th = MemHandleNew((UInt32) (len + 1));
		if (th != NULL) {
			p = (char *) MemHandleLock(th);
			if (src) StrCopy(p, src); else p[0] = '\0';
			MemHandleUnlock(th);
			FldSetTextHandle(fld, th);
		}
		if (src) MemHandleUnlock(*noteH);
	}

	FrmSetActiveForm(frm);
	FrmDrawForm(frm);
	FrmSetFocus(frm, FrmGetObjectIndex(frm, fldNoteText));
	NoteSyncScroll(frm, fld);

	while (!done) {
		EventType ev;

		EvtGetEvent(&ev, evtWaitForever);

		if (ev.eType == ctlSelectEvent) {
			UInt16 cid = ev.data.ctlSelect.controlID;
			if (cid == btnNoteDone) { ok = true; done = true; continue; }
			if (cid == btnNoteCancel) { done = true; continue; }
		}
		if (ev.eType == sclRepeatEvent &&
			ev.data.sclRepeat.scrollBarID == sclNote) {
			Int16 delta = (Int16) (ev.data.sclRepeat.newValue -
				ev.data.sclRepeat.value);
			FldScrollField(fld, (UInt16) (delta < 0 ? -delta : delta),
				delta < 0 ? winUp : winDown);
			continue;
		}
		if (ev.eType == appStopEvent) {
			EvtAddEventToQueue(&ev);
			done = true;
			continue;
		}
		if (!A35DlgSysMenu(&ev))
			FrmDispatchEvent(&ev);
		if (ev.eType == keyDownEvent || ev.eType == fldChangedEvent ||
			ev.eType == fldEnterEvent)
			NoteSyncScroll(frm, fld);
	}

	if (ok) {			/* copy the edited text out into a fresh handle */
		char *ft = FldGetTextPtr(fld);
		UInt16 len = ft ? (UInt16) StrLen(ft) : 0;
		MemHandle nh = MemHandleNew((UInt32) (len + 1));

		if (nh != NULL) {
			char *p = (char *) MemHandleLock(nh);
			if (ft) StrCopy(p, ft); else p[0] = '\0';
			MemHandleUnlock(nh);
			if (*noteH) MemHandleFree(*noteH);
			*noteH = nh;
		} else {
			ok = false;		/* OOM: leave the note unchanged */
		}
	}

	FrmEraseForm(frm);		/* frees the field's text handle */
	FrmDeleteForm(frm);
	if (prev)
		FrmSetActiveForm(prev);
	return ok;
}

/* ---- Social dialog ----------------------------------------------------- */
static const char * const kSocialNet[] = {
	"Instagram", "Facebook", "WhatsApp", "X", "Telegram", "Signal"
};
#define kSocialNetCount	6

/* value: known platform -> "handle" (sub = a35socXxx); Other -> "Platform: handle"
 * (sub = a35socOther, so the custom platform name lives in the value). */
Boolean EditSocialDialog(char *value, UInt16 maxLen, UInt8 *subP)
{
	FormPtr prev = FrmGetActiveForm();
	FormPtr frm = FrmInitForm(frmSocialDlg);
	ControlPtr pop = (ControlPtr) DlgObj(frm, popSocialNet);
	ControlPtr cb = (ControlPtr) DlgObj(frm, cbSocialOther);
	UInt16 customIdx = FrmGetObjectIndex(frm, fldSocialCustom);
	UInt16 phIdx = FrmGetObjectIndex(frm, lblSocialOther);
	Int16 curNet = 0;
	Boolean other = false, ok = false, done = false;
	char handle[81], platform[41];

	handle[0] = platform[0] = '\0';
	if (*subP == a35socOther) {
		const char *sep = value ? StrStr(value, ": ") : NULL;
		other = true;
		if (sep) {
			UInt16 pn = (UInt16) (sep - value);
			if (pn > sizeof(platform) - 1) pn = sizeof(platform) - 1;
			MemMove(platform, value, pn);
			platform[pn] = '\0';
			StrNCopy(handle, sep + 2, sizeof(handle) - 1);
			handle[sizeof(handle) - 1] = '\0';
		} else if (value) {
			StrNCopy(handle, value, sizeof(handle) - 1);
			handle[sizeof(handle) - 1] = '\0';
		}
	} else {
		curNet = (*subP < kSocialNetCount) ? *subP : 0;
		if (value) {
			StrNCopy(handle, value, sizeof(handle) - 1);
			handle[sizeof(handle) - 1] = '\0';
		}
	}
	DlgSetField(frm, fldSocialHandle, handle);
	DlgSetField(frm, fldSocialCustom, platform);
	LstSetSelection((ListPtr) DlgObj(frm, lstSocialNet), curNet);
	CtlSetLabel(pop, kSocialNet[curNet]);
	CtlSetValue(cb, (Int16) other);

	FrmSetActiveForm(frm);
	if (other) { FrmShowObject(frm, customIdx); FrmHideObject(frm, phIdx); }
	else { FrmHideObject(frm, customIdx); FrmShowObject(frm, phIdx); }
	FrmDrawForm(frm);
	FrmSetFocus(frm, FrmGetObjectIndex(frm, fldSocialHandle));

	while (!done) {
		EventType ev;

		EvtGetEvent(&ev, evtWaitForever);

		if (ev.eType == popSelectEvent &&
			ev.data.popSelect.controlID == popSocialNet)
			curNet = ev.data.popSelect.selection;
		if (ev.eType == ctlSelectEvent) {
			UInt16 cid = ev.data.ctlSelect.controlID;

			if (cid == cbSocialOther) {
				other = (Boolean) CtlGetValue(cb);
				if (other) {
					FrmHideObject(frm, phIdx);
					FrmShowObject(frm, customIdx);
					FrmSetFocus(frm, customIdx);
				} else {
					FrmHideObject(frm, customIdx);
					FrmShowObject(frm, phIdx);
				}
				continue;
			}
			if (cid == btnSocialOK) { ok = true; done = true; continue; }
			if (cid == btnSocialCancel) { done = true; continue; }
		}
		if (ev.eType == appStopEvent) {
			EvtAddEventToQueue(&ev);
			done = true;
			continue;
		}
		if (!A35DlgSysMenu(&ev))
			FrmDispatchEvent(&ev);
	}

	if (ok) {
		const char *h = FldGetTextPtr((FieldPtr) DlgObj(frm, fldSocialHandle));
		char out[a35EditValLen];

		out[0] = '\0';
		if (other) {
			const char *p = FldGetTextPtr((FieldPtr) DlgObj(frm, fldSocialCustom));
			if (p && p[0]) {
				StrNCopy(out, p, sizeof(out) - 1);
				out[sizeof(out) - 1] = '\0';
				StrNCat(out, ": ", (Int16) sizeof(out));
			}
			if (h && h[0])
				StrNCat(out, h, (Int16) sizeof(out));
			*subP = a35socOther;
		} else {
			if (h && h[0]) {
				StrNCopy(out, h, sizeof(out) - 1);
				out[sizeof(out) - 1] = '\0';
			}
			*subP = (UInt8) curNet;
		}
		StrNCopy(value, out, maxLen - 1);
		value[maxLen - 1] = '\0';
	}

	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prev)
		FrmSetActiveForm(prev);
	return ok;
}

/* ---- Birthday / Anniversary date dialog (any-year "YYYYMMDD") ----------- */
static const char * const kDateMonth[12] = {
	"Jan", "Feb", "Mar", "Apr", "May", "Jun",
	"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
static const char * const kAnnivType[5] = {
	"Engagement", "Remembrance", "Wedding", "Work", "Other"
};
#define kAnnivTypeCount	5
#define kAnnivDefault	2			/* "Wedding" */

const char *EditAnnivTypeName(UInt8 t)
{
	return kAnnivType[(t < kAnnivTypeCount) ? t : kAnnivDefault];
}

const char *EditSocialNetName(UInt8 sub)
{
	return kSocialNet[(sub < kSocialNetCount) ? sub : 0];
}

/* relation types - order MUST match a35relXxx in AddressDB35.h */
static const char * const kRelType[] = {
	"Spouse", "Partner", "Child", "Parent", "Sibling",
	"Assistant", "Manager", "Friend", "Other"
};
#define kRelTypeCount	(sizeof(kRelType) / sizeof(kRelType[0]))

const char *EditRelTypeName(UInt8 t)
{
	return kRelType[(t < kRelTypeCount) ? t : (kRelTypeCount - 1)];
}

/* days in month mon1 (1-12). Unknown year (0) uses a leap year so a Feb-29
 * birthday can still be entered; a known year honours its actual leap status. */
/* file scope: a function-local `static const` in a SEC001 function is emitted into
 * the code segment but accessed A5-relative -> wrong address. See
 * [[multiseg-local-static-const]]. */
static const UInt8 kDaysInMon[12] = { 31,28,31,30,31,30,31,31,30,31,30,31 };

static Int16 DateDaysInMon(Int16 mon1, Int16 year)
{
	Int16 d;

	if (mon1 < 1 || mon1 > 12) return 31;
	d = (Int16) kDaysInMon[mon1 - 1];
	if (mon1 == 2) {
		Int16 y = (year > 0) ? year : 2000;
		if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) d = 29;
	}
	return d;
}

/* ---- date value pickers (whole-value push-buttons, not per-digit) ------ *
 * Month/Day/Year are each ONE push-button holding the whole value ("12", not
 * "1"+"2"); the field's up/down REPEATBUTTON arrows continuously WRAP the
 * whole value while held (month/day - year clamps instead, see DateBump).
 * Current values live in gCurMon/gCurDay/gCurYear (gCurYear==0 = unknown);
 * each button's label points at a persistent buffer (CtlSetLabel keeps the
 * pointer, not a copy). Not re-entrant - only one date dialog is open at a
 * time. */
static Int16 gCurMon, gCurDay, gCurYear;
static char  gMonStr[3];		/* "1".."12" + NUL */
static char  gDayStr[3];		/* "1".."31" + NUL */
static char  gYrStr[5];		/* "1".."9999" + NUL, or empty when Unknown */

enum { kDateFieldMonth, kDateFieldDay, kDateFieldYear };

static void DateShowMonth(FormPtr f)
{
	StrIToA(gMonStr, gCurMon);
	CtlSetLabel((ControlPtr) DlgObj(f, pbDateMonthVal), gMonStr);
}
static void DateShowDay(FormPtr f)
{
	StrIToA(gDayStr, gCurDay);
	CtlSetLabel((ControlPtr) DlgObj(f, pbDateDayVal), gDayStr);
}
static void DateShowYear(FormPtr f, Boolean blank)
{
	if (blank) gYrStr[0] = '\0';
	else StrIToA(gYrStr, gCurYear);
	CtlSetLabel((ControlPtr) DlgObj(f, pbDateYearVal), gYrStr);
}

static Int16 DlgYearOf(FormPtr f)		/* 0 = blank/unknown */
{
	(void) f;
	return gCurYear;
}

/* Clamp month 1-12, day 1..daysInMonth, year 1..9999 (unless Unknown), and
 * redraw the three value buttons. Same validity guarantees as before (Feb 30
 * etc. can never be shown), just operating on plain integers now instead of
 * round-tripping through digit-box text. */
static void DateClamp(FormPtr f)
{
	Boolean unk = (Boolean) CtlGetValue((ControlPtr) DlgObj(f, cbDateNoYear));
	Int16 dim;

	if (gCurMon < 1) gCurMon = 1; if (gCurMon > 12) gCurMon = 12;
	dim = DateDaysInMon(gCurMon, unk ? 0 : gCurYear);
	if (gCurDay < 1) gCurDay = 1; if (gCurDay > dim) gCurDay = dim;
	DateShowMonth(f);
	DateShowDay(f);
	if (!unk) {
		if (gCurYear < 1) gCurYear = 1; if (gCurYear > 9999) gCurYear = 9999;
		DateShowYear(f, false);
	}
}

/* Bump the whole value of field (month/day/year) by delta, then re-clamp the
 * whole date (a month/year change can shrink the valid day range). Month and
 * day WRAP (Dec+1->Jan, day 31+1->day 1 of the same month); year CLAMPS at
 * 1/9999 instead - wrapping a 4-digit year on a held arrow would be jarring,
 * and clamping is the standard behavior for an open-ended numeric range. */
static void DateBump(FormPtr f, UInt16 field, Int16 delta)
{
	if (field == kDateFieldMonth) {
		gCurMon = (Int16) (((gCurMon - 1 + delta) % 12 + 12) % 12 + 1);
	} else if (field == kDateFieldDay) {
		Boolean unk = (Boolean) CtlGetValue((ControlPtr) DlgObj(f, cbDateNoYear));
		Int16 dim = DateDaysInMon(gCurMon, unk ? 0 : gCurYear);
		gCurDay = (Int16) (((gCurDay - 1 + delta) % dim + dim) % dim + 1);
	} else {
		gCurYear += delta;
	}
	DateClamp(f);
}

static void DlgMoveTo(FormPtr f, UInt16 id, Int16 x, Int16 y)
{
	FrmSetObjectPosition(f, FrmGetObjectIndex(f, id), x, y);
}

static void DlgShowHide(FormPtr f, UInt16 id, Boolean show)
{
	if (show) FrmShowObject(f, FrmGetObjectIndex(f, id));
	else      FrmHideObject(f, FrmGetObjectIndex(f, id));
}

#define kDateRowY0	20
#define kDateRowStep	18

/* Lay out the date dialog. Rows top->bottom:
 *     [Type: <popup> [Other name]]   (Anniversary only)
 *      Month:  [12]  +/-
 *      Day:    [31]  +/-
 *      Year: [2026]  +/-
 *      [x] Unknown year
 *      OK / Cancel
 * Bottom-anchored, fixed size per type (no mid-life resize -> no save-behind
 * corruption). */
static void DateLayout(FormPtr f, Boolean isAnniv)
{
	Int16 y = kDateRowY0;
	Int16 rows = (Int16) (4 + (isAnniv ? 1 : 0));	/* Month/Day/Year/Unknown [+Type] */
	Int16 formH = (Int16) (kDateRowY0 + rows * kDateRowStep + 21);
	RectangleType r;

	if (isAnniv) {
		DlgMoveTo(f, lblDateType, 13, (Int16) (y + 1));
		DlgMoveTo(f, popDateType, 55, y);	/* DlgTypeOther sizes it so its right
						 * edge aligns with the digit boxes (x=119) */
		DlgMoveTo(f, fldDateTypeOther, 100, (Int16) (y + 1));
		y += kDateRowStep;
	}
	DlgShowHide(f, lblDateType, isAnniv);
	DlgShowHide(f, popDateType, isAnniv);

	/* Input controls shifted +10px right of the ROM/plugin positions so the fields
	 * aren't crammed against the labels. Row labels stay at x=6. */
	/* Month (labels at y+1; arrows up=y-1, down=y+6) */
	DlgMoveTo(f, lblDateDate, 13, (Int16) (y + 1));
	DlgMoveTo(f, pbDateMonthVal, 99, y);
	DlgMoveTo(f, btnDateMonthUp, 131, (Int16) (y - 1));
	DlgMoveTo(f, btnDateMonthDn, 131, (Int16) (y + 6));
	y += kDateRowStep;

	/* Day */
	DlgMoveTo(f, lblDateDay, 13, (Int16) (y + 1));
	DlgMoveTo(f, pbDateDayVal, 99, y);
	DlgMoveTo(f, btnDateDayUp, 131, (Int16) (y - 1));
	DlgMoveTo(f, btnDateDayDn, 131, (Int16) (y + 6));
	y += kDateRowStep;

	/* Year: right-aligned with Month/Day's right edge (99+24=123), so its left
	 * edge sits at 123-38=85 - arrows stay at x=131 same as every other row,
	 * only the value box itself shifts. */
	DlgMoveTo(f, lblDateYear, 13, (Int16) (y + 1));
	DlgMoveTo(f, pbDateYearVal, 85, y);
	DlgMoveTo(f, btnDateYearUp, 131, (Int16) (y - 1));
	DlgMoveTo(f, btnDateYearDn, 131, (Int16) (y + 6));
	y += kDateRowStep;

	/* Unknown: checkbox left-aligned with the year value box's (new) left edge,
	 * same row height/spacing as before - only the X shifted to match. */
	DlgMoveTo(f, cbDateNoYear, 82, (Int16) (y + 2));
	DlgMoveTo(f, lblDateUnknown, 96, (Int16) (y + 2));
	y += kDateRowStep;

	DlgMoveTo(f, btnDateOK, 13, (Int16) (y + 5));
	DlgMoveTo(f, btnDateCancel, 59, (Int16) (y + 5));

	r.topLeft.x = 2;
	r.topLeft.y = (Coord) (158 - formH);
	r.extent.x = 156;
	r.extent.y = formH;
	WinSetBounds(FrmGetWindowHandle(f), &r);
}

/* Type = "Other" reveals the custom-name field and shrinks the popup so the two
 * don't overlap; any other type spans the row with the field hidden. */
static void DlgTypeOther(FormPtr f, Boolean other)
{
	UInt16 idx = FrmGetObjectIndex(f, popDateType);
	RectangleType r;

	FrmGetObjectBounds(f, idx, &r);
	/* Normal: wide enough for the longest type name ("Remembrance", 11 chars)
	 * without truncation - was 65, too narrow (showed "Remembran"). Other:
	 * shrink to show just "Other"; the custom-name field takes the rest. */
	r.extent.x = other ? 48 : 85;
	FrmSetObjectBounds(f, idx, &r);
	DlgShowHide(f, fldDateTypeOther, other);
}

/* "Unknown" checked -> hide the year +/- arrows (the year digit boxes are blanked
 * by the caller); the boxes keep their slots so the dialog size is fixed. */
static void DlgYearUsable(FormPtr f, Boolean known)
{
	DlgShowHide(f, btnDateYearUp, known);
	DlgShowHide(f, btnDateYearDn, known);
}

/* value: "YYYYMMDD" in/out (may be ""). isAnniv adds the Type popup and uses
 * *typeP (anniversary type) in/out. title sets the form caption. */
Boolean EditDateDialog(char *value, UInt16 maxLen, Boolean isAnniv,
	UInt8 *typeP, const char *title)
{
	FormPtr prev = FrmGetActiveForm();
	FormPtr frm = FrmInitForm(frmDateDlg);
	A35Birthday bd;
	Int16 curType, defYear, savedYear;
	Boolean noYear, ok = false, done = false;
	DateTimeType now;

	TimSecondsToDateTime(TimGetSeconds(), &now);
	defYear = now.year;
	if (!A35ParseBirthday(value, &bd)) {		/* unset -> seed with today */
		bd.month = (UInt8) now.month;
		bd.day = (UInt8) now.day;
		bd.year = now.year;
		bd.flags = a35bdHasYear;
	}
	if (bd.month < 1 || bd.month > 12) bd.month = 1;
	if (bd.day < 1 || bd.day > 31) bd.day = 1;
	noYear = (Boolean) !(bd.flags & a35bdHasYear);
	savedYear = (bd.flags & a35bdHasYear) ? bd.year : defYear;
	curType = (*typeP < kAnnivTypeCount) ? (Int16) *typeP : kAnnivDefault;

	if (title) FrmSetTitle(frm, (char *) title);
	/* Seed the value buttons (labels only; the not-yet-active form isn't drawn,
	 * so no screen ghost -- FrmDrawForm paints them). Year blanked when Unknown. */
	gCurMon = (Int16) bd.month;
	gCurDay = (Int16) bd.day;
	gCurYear = noYear ? 0 : bd.year;
	DateShowMonth(frm);
	DateShowDay(frm);
	DateShowYear(frm, noYear);
	CtlSetValue((ControlPtr) DlgObj(frm, cbDateNoYear), (Int16) noYear);
	if (isAnniv) {
		LstSetSelection((ListPtr) DlgObj(frm, lstDateType), curType);
		CtlSetLabel((ControlPtr) DlgObj(frm, popDateType), kAnnivType[curType]);
		if (curType == a35AnnivOther)		/* custom name trails the date */
			DlgSetField(frm, fldDateTypeOther,
				(StrLen(value) > a35BirthdayStrLen)
					? value + a35BirthdayStrLen : "");
	}

	FrmSetActiveForm(frm);
	/* Year / Unknown / year field share one line; the year field is always shown
	 * (locked when Unknown). Birthday drops the Type row; Anniversary's "Other"
	 * reveals a custom-name field beside a shrunk Type popup. */
	DateLayout(frm, isAnniv);
	if (isAnniv)
		DlgTypeOther(frm, (Boolean) (curType == a35AnnivOther));
	DlgYearUsable(frm, (Boolean) !noYear);
	FrmDrawForm(frm);

	while (!done) {
		EventType ev;
		Boolean typeChanged = false;

		EvtGetEvent(&ev, evtWaitForever);

		if (ev.eType == popSelectEvent) {
			UInt16 cid = ev.data.popSelect.controlID;
			if (cid == popDateType) {
				curType = ev.data.popSelect.selection;
				typeChanged = true;	/* redraw AFTER dispatch, once the
							 * ROM has set the trigger label */
			}
		}
		if (ev.eType == ctlRepeatEvent || ev.eType == ctlSelectEvent) {
			UInt16 cid = (ev.eType == ctlRepeatEvent)
				? ev.data.ctlRepeat.controlID : ev.data.ctlSelect.controlID;

			if (cid == btnDateMonthUp || cid == btnDateMonthDn) {
				DateBump(frm, kDateFieldMonth, (Int16) (cid == btnDateMonthUp ? 1 : -1));
			} else if (cid == btnDateDayUp || cid == btnDateDayDn) {
				DateBump(frm, kDateFieldDay, (Int16) (cid == btnDateDayUp ? 1 : -1));
			} else if (cid == btnDateYearUp || cid == btnDateYearDn) {
				DateBump(frm, kDateFieldYear, (Int16) (cid == btnDateYearUp ? 1 : -1));
			} else if (ev.eType == ctlSelectEvent && cid == cbDateNoYear) {
				Boolean chk = (Boolean) CtlGetValue(
					(ControlPtr) DlgObj(frm, cbDateNoYear));
				if (chk) {			/* year unknown: remember + blank */
					if (gCurYear > 0) savedYear = gCurYear;
					gCurYear = 0;
					DateShowYear(frm, true);
				} else {			/* year known: restore + clamp */
					gCurYear = savedYear;
					DateClamp(frm);
				}
				DlgYearUsable(frm, (Boolean) !chk);
			} else if (ev.eType == ctlSelectEvent && cid == btnDateOK) {
				ok = true; done = true;
			} else if (ev.eType == ctlSelectEvent && cid == btnDateCancel) {
				done = true;
			}
		}
		if (ev.eType == appStopEvent) {
			EvtAddEventToQueue(&ev);
			done = true;
			continue;
		}
		if (!A35DlgSysMenu(&ev))
			FrmDispatchEvent(&ev);
		if (typeChanged) {		/* Type label set by ROM: fit popup + field */
			Int16 px, py;
			RectangleType er;

			FrmGetObjectPosition(frm,
				FrmGetObjectIndex(frm, popDateType), &px, &py);
			er.topLeft.x = 34; er.topLeft.y = py;
			er.extent.x = 116; er.extent.y = 14;
			WinEraseRectangle(&er, 0);
			DlgTypeOther(frm, (Boolean) (curType == a35AnnivOther));
			CtlDrawControl((ControlPtr) DlgObj(frm, popDateType));
		}
	}

	if (ok) {
		DateClamp(frm);				/* ensure the current values are a valid date */
		noYear = (Boolean) CtlGetValue((ControlPtr) DlgObj(frm, cbDateNoYear));
		bd.month = (UInt8) gCurMon;
		bd.day = (UInt8) gCurDay;
		if (noYear || gCurYear <= 0) { bd.flags = 0; bd.year = 0; }
		else { bd.flags = a35bdHasYear; bd.year = gCurYear; }
		A35FormatBirthday(&bd, value);		/* "YYYYMMDD"\0 (9 bytes) */
		if (isAnniv) {
			*typeP = (UInt8) curType;
			if (curType == a35AnnivOther) {	/* append custom name after date */
				const char *cn = FldGetTextPtr(
					(FieldPtr) DlgObj(frm, fldDateTypeOther));
				UInt16 ln = cn ? StrLen(cn) : 0;
				if (ln > (UInt16) (maxLen - a35BirthdayStrLen - 1))
					ln = (UInt16) (maxLen - a35BirthdayStrLen - 1);
				if (ln) MemMove(value + a35BirthdayStrLen, cn, ln);
				value[a35BirthdayStrLen + ln] = '\0';
			}
		}
	}

	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prev)
		FrmSetActiveForm(prev);
	return ok;
}

/* ---- Relation dialog --------------------------------------------------- */
static char gRelContactLbl[48];		/* CtlSetLabel keeps the pointer, not a copy */

static void RelSetContactLabel(FormPtr f, UInt32 uid)
{
	UInt16 idx;

	if (uid != 0 && A35FindByUniqueID(gAddrDB, uid, &idx))
		A35RecordDisplayName(gAddrDB, idx, gRelContactLbl, sizeof(gRelContactLbl));
	else
		StrCopy(gRelContactLbl, "Pick a contact\205");
	CtlSetLabel((ControlPtr) DlgObj(f, btnRelPick), gRelContactLbl);
}

/* case-insensitive substring filter over the contact set; fills ptrs[]/shownUids[]
 * with the matches and returns the count. */
static UInt16 RelFilter(const char *names, UInt16 nl, const UInt32 *uids,
	UInt16 all, const char *term, char **ptrs, UInt32 *shownUids)
{
	char lt[40];
	UInt16 i, n = 0, tl;

	StrNCopy(lt, term ? term : "", sizeof(lt) - 1);
	lt[sizeof(lt) - 1] = '\0';
	DlgLower(lt);
	tl = StrLen(lt);
	for (i = 0; i < all; i++) {
		const char *nm = names + (UInt32) i * nl;
		Boolean hit = (Boolean) (tl == 0);

		if (!hit) {
			char buf[72];
			StrNCopy(buf, nm, sizeof(buf) - 1);
			buf[sizeof(buf) - 1] = '\0';
			DlgLower(buf);
			hit = (Boolean) (StrStr(buf, lt) != NULL);
		}
		if (hit) { ptrs[n] = (char *) nm; shownUids[n] = uids[i]; n++; }
	}
	return n;
}

/* Pick a contact from the whole DB via a full-screen searchable list (rich
 * "[Salut] Name, Company, Title" lines + a Find field/button); returns the
 * chosen record's stable unique id, or 0 if cancelled. selfUid is excluded. */
static UInt32 RelPickContact(UInt32 selfUid)
{
	FormPtr prev = FrmGetActiveForm();
	FormPtr frm;
	ListPtr lst;
	UInt16 total = DmNumRecords(gAddrDB), i, all = 0, shown;
	MemHandle nh, uh, ph, sh;
	char *names;
	UInt32 *uids, *shownUids;
	char **ptrs;
	UInt32 result = 0;
	Boolean done = false;
	const UInt16 NL = 72;

	if (total == 0)
		return 0;
	nh = MemHandleNew((UInt32) total * NL);
	uh = MemHandleNew((UInt32) total * sizeof(UInt32));
	ph = MemHandleNew((UInt32) total * sizeof(char *));
	sh = MemHandleNew((UInt32) total * sizeof(UInt32));
	if (nh == NULL || uh == NULL || ph == NULL || sh == NULL) {
		if (nh) MemHandleFree(nh);
		if (uh) MemHandleFree(uh);
		if (ph) MemHandleFree(ph);
		if (sh) MemHandleFree(sh);
		return 0;
	}
	names = (char *) MemHandleLock(nh);
	uids = (UInt32 *) MemHandleLock(uh);
	ptrs = (char **) MemHandleLock(ph);
	shownUids = (UInt32 *) MemHandleLock(sh);

	for (i = 0; i < total; i++) {
		UInt16 attr = 0;
		UInt32 uid = 0;

		if (DmRecordInfo(gAddrDB, i, &attr, &uid, NULL) != errNone)
			continue;
		if ((attr & dmRecAttrDelete) || uid == 0 || uid == selfUid)
			continue;
		A35RecordPickName(gAddrDB, i, names + (UInt32) all * NL, NL);
		uids[all] = uid;
		all++;
	}

	frm = FrmInitForm(frmContactPick);
	lst = (ListPtr) DlgObj(frm, lstContactPick);
	shown = RelFilter(names, NL, uids, all, "", ptrs, shownUids);
	LstSetListChoices(lst, ptrs, shown);
	LstSetSelection(lst, -1);

	FrmSetActiveForm(frm);
	FrmDrawForm(frm);
	FrmSetFocus(frm, FrmGetObjectIndex(frm, fldContactFind));

	while (!done) {
		EventType ev;
		Boolean refilter = false;

		EvtGetEvent(&ev, evtWaitForever);

		if (ev.eType == lstSelectEvent &&
			ev.data.lstSelect.listID == lstContactPick) {
			Int16 sidx = ev.data.lstSelect.selection;
			if (sidx >= 0 && sidx < (Int16) shown)
				result = shownUids[sidx];
			done = true;
			continue;
		}
		if (ev.eType == ctlSelectEvent) {
			UInt16 cid = ev.data.ctlSelect.controlID;
			if (cid == btnContactCancel) { done = true; continue; }
		}
		if (ev.eType == keyDownEvent) {		/* live filter as the user types */
			if (!A35DlgSysMenu(&ev))
				FrmDispatchEvent(&ev);		/* field consumes the key first */
			refilter = true;
		} else if (ev.eType == appStopEvent) {
			EvtAddEventToQueue(&ev);
			done = true;
			continue;
		} else if (!refilter && ev.eType != ctlSelectEvent) {
			if (!A35DlgSysMenu(&ev))
				FrmDispatchEvent(&ev);
		}
		if (refilter) {
			const char *t = FldGetTextPtr((FieldPtr) DlgObj(frm, fldContactFind));
			shown = RelFilter(names, NL, uids, all, t ? t : "", ptrs, shownUids);
			LstSetListChoices(lst, ptrs, shown);
			LstSetSelection(lst, -1);
			LstDrawList(lst);
		}
	}

	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prev)
		FrmSetActiveForm(prev);
	MemHandleUnlock(nh); MemHandleUnlock(uh);
	MemHandleUnlock(ph); MemHandleUnlock(sh);
	MemHandleFree(nh); MemHandleFree(uh);
	MemHandleFree(ph); MemHandleFree(sh);
	return result;
}

Boolean EditRelationDialog(char *value, UInt16 maxLen, UInt8 *relTypeP,
	UInt32 selfUid)
{
	FormPtr prev = FrmGetActiveForm();
	FormPtr frm = FrmInitForm(frmRelDlg);
	Int16 curType = (*relTypeP < kRelTypeCount) ? (Int16) *relTypeP : 0;
	UInt32 targetUid = (value && value[0]) ? (UInt32) StrAToI(value) : 0;
	Boolean ok = false, done = false;

	(void) maxLen;
	LstSetSelection((ListPtr) DlgObj(frm, lstRelType), curType);
	CtlSetLabel((ControlPtr) DlgObj(frm, popRelType), kRelType[curType]);
	RelSetContactLabel(frm, targetUid);

	FrmSetActiveForm(frm);
	FrmDrawForm(frm);

	while (!done) {
		EventType ev;

		EvtGetEvent(&ev, evtWaitForever);

		if (ev.eType == popSelectEvent &&
			ev.data.popSelect.controlID == popRelType)
			curType = ev.data.popSelect.selection;
		if (ev.eType == ctlSelectEvent) {
			UInt16 cid = ev.data.ctlSelect.controlID;

			if (cid == btnRelPick) {
				UInt32 u = RelPickContact(selfUid);
				if (u != 0) {
					targetUid = u;
					RelSetContactLabel(frm, targetUid);
				}
				continue;
			} else if (cid == btnRelOK) {
				ok = true; done = true;
			} else if (cid == btnRelCancel) {
				done = true;
			}
		}
		if (ev.eType == appStopEvent) {
			EvtAddEventToQueue(&ev);
			done = true;
			continue;
		}
		if (!A35DlgSysMenu(&ev))
			FrmDispatchEvent(&ev);
	}

	if (ok) {
		if (targetUid != 0) StrIToA(value, (Int32) targetUid);
		else value[0] = '\0';
		*relTypeP = (UInt8) curType;
	}
	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prev)
		FrmSetActiveForm(prev);
	return ok;
}
