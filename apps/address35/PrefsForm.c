/*
 * PrefsForm.c - Address35 Preferences.
 *
 * Per-DB preferences kept in the AppInfo:
 *   - Name field label term (Surname / Last Name / Family Name).
 *   - Sort order: a Primary and a Secondary key, each Last Name / First Name /
 *     Company (a35sortXxx). The popup list order IS the a35sortXxx value, so the
 *     list selection index is the stored key.
 * Changing the sort order re-sorts the whole DB (A35ChangeSortOrder) and asks the
 * list form to redraw.
 */
#include <PalmOS.h>
#include "Main.h"

/* This whole form is foreground-only UI -> sec001, to keep it off main's 32KB cap. */
static void *PrefsObj(UInt16 id) SEC001;
static void PrefsSetPopup(UInt16 lstId, UInt16 trigId, Int16 sel) SEC001;
static void PrefsInit(void) SEC001;

static void *PrefsObj(UInt16 id)
{
	FormPtr f = FrmGetActiveForm();
	return FrmGetObjectPtr(f, FrmGetObjectIndex(f, id));
}

/* set a popup's list selection + trigger label from a value */
static void PrefsSetPopup(UInt16 lstId, UInt16 trigId, Int16 sel)
{
	ListPtr lst = (ListPtr) PrefsObj(lstId);
	ControlPtr trig = (ControlPtr) PrefsObj(trigId);

	if (sel < 0) sel = 0;
	LstSetSelection(lst, sel);
	CtlSetLabel(trig, LstGetSelectionText(lst, sel));
}

static void PrefsInit(void)
{
	FormPtr f = FrmGetActiveForm();
	UInt8 term = A35GetSurnameTerm(gAddrDB);
	UInt8 pri = a35sortLast, sec = a35sortFirst;

	if (term > a35termFamilyName)
		term = a35termSurname;
	PrefsSetPopup(lstPrefsSurname, popPrefsSurname, (Int16) term);

	A35GetSortOrder(gAddrDB, &pri, &sec);
	PrefsSetPopup(lstPrefsPrimary, popPrefsPrimary, (Int16) pri);
	PrefsSetPopup(lstPrefsSecondary, popPrefsSecondary, (Int16) sec);

	CtlSetValue((ControlPtr) PrefsObj(cbPrefsShowHidden), (Int16) gShowHiddenFiles);

	FrmDrawForm(f);
}

Boolean PrefsFormHandleEvent(EventPtr eventP) SEC001;
Boolean PrefsFormHandleEvent(EventPtr eventP)
{
	Boolean handled = false;

	switch (eventP->eType) {
	case frmOpenEvent:
		PrefsInit();
		handled = true;
		break;

	case ctlSelectEvent:
		switch (eventP->data.ctlSelect.controlID) {
		case btnPrefsOK: {
			Int16 term = LstGetSelection((ListPtr) PrefsObj(lstPrefsSurname));
			Int16 pri  = LstGetSelection((ListPtr) PrefsObj(lstPrefsPrimary));
			Int16 sec  = LstGetSelection((ListPtr) PrefsObj(lstPrefsSecondary));
			UInt8 oldPri = a35sortLast, oldSec = a35sortFirst;
			Boolean showHidden = (Boolean) CtlGetValue((ControlPtr) PrefsObj(cbPrefsShowHidden));

			if (term >= 0)
				A35SetSurnameTerm(gAddrDB, (UInt8) term);

			A35GetSortOrder(gAddrDB, &oldPri, &oldSec);
			if (pri >= 0 && sec >= 0 &&
				((UInt8) pri != oldPri || (UInt8) sec != oldSec)) {
				A35ChangeSortOrder(gAddrDB, (UInt8) pri, (UInt8) sec);
				gTopVisibleRecord = 0;			/* order changed: back to top */
			}
			if (showHidden != gShowHiddenFiles) {
				A35SetShowHiddenFiles(showHidden);
				MainInvalidateCardDir();
			}
			FrmReturnToForm(0);
			FrmUpdateForm(frmMain, 0);			/* re-sorted / relabeled -> redraw */
			handled = true;
			break;
		}
		case btnPrefsCancel:
			FrmReturnToForm(0);
			handled = true;
			break;
		}
		break;

	default:
		break;
	}
	return handled;
}
