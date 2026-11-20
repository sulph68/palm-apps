/* DbForms.c -- see DbForms.h. sec004; no 32-bit multiply/divide here. */

#include <PalmOS.h>
#include "DbForms.h"
#include "SecDb.h"
#include "ViewList.h"
#include "Database.h"
#include "CryptoSec35.h"
#include "CardCrypt.h"
#include "DetailsForm.h"
#include "Utils.h"

#define kDbCreate	0xFE		/* the "Create New..." row of the Database list */

/* ------------------------------------------------------------------ *
 *  database lists (Memo Details pop-up, Manage Databases)
 * ------------------------------------------------------------------ */
static Char		gDbNames[kSrcMax + 1][dmDBNameLength + 8];
static Char*	gDbChoices[kSrcMax + 1];
static UInt8	gDbMap[kSrcMax + 1];
static UInt16	gDbCount;

/* Main first, then the user databases by name, then (optionally) Create New... */
static void DbListBuild (Boolean withCreate) UI4_SECTION;
static void DbListBuild (Boolean withCreate)
{
	UInt8 s, i, j;
	gDbCount = 0;
	gDbMap[gDbCount] = cryptoPadDB;
	SrcLabel (cryptoPadDB, gDbNames[gDbCount]);
	gDbCount++;
	for (s = 2; s < gSrcCount; s++)
		{
		gDbMap[gDbCount] = s;
		SrcLabel (s, gDbNames[gDbCount]);
		gDbCount++;
		}
	for (i = 2; i < gDbCount; i++)
		for (j = i; j > 1 && StrCaselessCompare (gDbNames[j - 1], gDbNames[j]) > 0; j--)
			{
			Char t[dmDBNameLength + 8];
			UInt8 m = gDbMap[j];
			StrCopy (t, gDbNames[j]); StrCopy (gDbNames[j], gDbNames[j - 1]); StrCopy (gDbNames[j - 1], t);
			gDbMap[j] = gDbMap[j - 1]; gDbMap[j - 1] = m;
			}
	if (withCreate)
		{
		StrCopy (gDbNames[gDbCount], "Create New\205");
		gDbMap[gDbCount] = kDbCreate;
		gDbCount++;
		}
	for (i = 0; i < gDbCount; i++) gDbChoices[i] = gDbNames[i];
}

static Int16 DbListIndexOf (UInt8 s) UI4_SECTION;
static Int16 DbListIndexOf (UInt8 s)
{
	UInt16 i;
	for (i = 0; i < gDbCount; i++) if (gDbMap[i] == s) return (Int16) i;
	return -1;
}

static void* Obj (FormPtr frm, UInt16 id) UI4_SECTION;
static void* Obj (FormPtr frm, UInt16 id)
{
	return FrmGetObjectPtr (frm, FrmGetObjectIndex (frm, id));
}

/* FrmCopyLabel does not erase a longer old text: hide, copy, show. */
static void SetLabel (FormPtr frm, UInt16 id, const Char* text) UI4_SECTION;
static void SetLabel (FormPtr frm, UInt16 id, const Char* text)
{
	UInt16 idx = FrmGetObjectIndex (frm, id);
	FrmHideObject (frm, idx);
	FrmCopyLabel (frm, id, text);
	FrmShowObject (frm, idx);
}

/* ------------------------------------------------------------------ *
 *  New / Rename Database dialog
 * ------------------------------------------------------------------ */
/* rename: s is the database; otherwise *outSrc gets the new one. */
static Boolean DbNameDialog (Boolean rename, UInt8 s, UInt8* outSrc) UI4_SECTION;
static Boolean DbNameDialog (Boolean rename, UInt8 s, UInt8* outSrc)
{
	Char buf[secDbSuffixMax + 1];

	buf[0] = 0;
	if (rename)
		{
		Char label[dmDBNameLength + 8];
		SrcLabel (s, label);
		StrNCopy (buf, label, secDbSuffixMax);
		buf[secDbSuffixMax] = 0;
		}

	for (;;)
		{
		FormPtr		frm = FrmInitForm (frmDbNameForm);
		UInt16		fldIdx = FrmGetObjectIndex (frm, fldDbName);
		FieldPtr	fld = (FieldPtr) FrmGetObjectPtr (frm, fldIdx);
		MemHandle	h = MemHandleNew (secDbSuffixMax + 1);
		UInt16		btn, err;
		Char*		t;

		FrmSetTitle (frm, rename ? "Rename Database" : "New Database");
		if (h)
			{
			StrCopy ((Char*) MemHandleLock (h), buf);
			MemHandleUnlock (h);
			FldSetTextHandle (fld, h);
			FldSetSelection (fld, 0, StrLen (buf));
			}
		FrmSetFocus (frm, fldIdx);
		btn = FrmDoDialog (frm);
		t = FldGetTextPtr (fld);
		StrNCopy (buf, t ? t : "", secDbSuffixMax);
		buf[secDbSuffixMax] = 0;
		FrmDeleteForm (frm);
		if (btn != btnDbNameOK) return false;

		err = rename ? SrcRename (s, buf) : SrcCreate (buf, outSrc);
		if (! err) { ViewInvalidate (); return true; }
		FrmAlert (err);
		}
}

/* ------------------------------------------------------------------ *
 *  read-only edit session
 * ------------------------------------------------------------------ */
Boolean			gEditViewOnly = false;
static MemHandle	gViewOnlyH = NULL;
static Boolean		gViewOnlyWarned = false;

static void FieldEditable (FieldPtr fld, Boolean on) UI4_SECTION;
static void FieldEditable (FieldPtr fld, Boolean on)
{
	FieldAttrType attr;
	FldGetAttributes (fld, &attr);
	attr.editable = on;
	FldSetAttributes (fld, &attr);
}

Boolean EditViewOnlyRetrieve (FieldPtr fieldP)
{
	Boolean enc;
	MemHandle h;

	gEditViewOnly = false;
	gViewOnlyWarned = false;
	if (currentDB == memoPadDB || currentRecord == noRecordSelected) return false;
	if (! SrcRecReadOnly (currentDB, currentRecord)) return false;

	h = SrcRecordText (currentDB, currentRecord, currentPasswordP, &enc);
	if (! h) return false;
	gViewOnlyH = h;
	FldSetTextHandle (fieldP, h);
	FieldEditable (fieldP, false);
	gEditViewOnly = true;
	return true;
}

Boolean EditViewOnlyRelease (FieldPtr fieldP)
{
	if (! gEditViewOnly) return false;
	FldSetTextHandle (fieldP, NULL);
	if (gViewOnlyH)
		{
		Char* p = (Char*) MemHandleLock (gViewOnlyH);
		MemSet (p, MemHandleSize (gViewOnlyH), 0);
		MemHandleUnlock (gViewOnlyH);
		MemHandleFree (gViewOnlyH);
		gViewOnlyH = NULL;
		}
	FieldEditable (fieldP, true);
	gEditViewOnly = false;
	RemoveCurrentPassword ();
	return true;
}

Boolean EditViewOnlyRefuse (void)
{
	if (currentDB == memoPadDB || currentRecord == noRecordSelected) return false;
	if (SrcIsRom (currentDB)) { FrmAlert (altMemoInRom); return true; }
	if (SrcRecReadOnly (currentDB, currentRecord)) { FrmAlert (altMemoReadOnly); return true; }
	return false;
}

Boolean EditViewOnlyBlockKey (void)
{
	if (! gEditViewOnly) return false;
	if (! gViewOnlyWarned)
		{
		gViewOnlyWarned = true;
		FrmAlert (SrcIsRom (currentDB) ? altMemoInRom : altMemoReadOnly);
		}
	return true;
}

UInt16 EditTitlePosition (void)
{
	Int16 p = ViewFind (currentDB, currentRecord);
	return (UInt16) (p < 0 ? 0 : p);
}

void EditMoveAfterSave (UInt8 to, Char* password)
{
	if (currentRecord != noRecordSelected)
		{
		UInt32		oldCr = SrcCrDate (currentDB), oldUid = 0;
		UInt16		newIdx = 0;
		Boolean		ok;
		WinHandle	busy;

		DmRecordInfo (SrcRef (currentDB), currentRecord, NULL, &oldUid, NULL);
		busy = CardBusyShow ("Moving...");
		ok = SrcMoveRecord (currentDB, currentRecord, to, password, &newIdx);
		CardBusyHide (busy);
		if (ok)
			{
			ViewOrderReplace (oldCr, oldUid, to, newIdx);
			currentDB = to;
			currentRecord = newIdx;
			}
		else
			FrmAlert (altMoveFailed);
		}
	if (password)
		{
		MemSet (password, StrLen (password), 0);
		MemPtrFree (password);
		}
	SrcSelect (cryptoPadDB);
	ViewInvalidate ();
	FrmGotoForm (frmMainForm);
}

/* ------------------------------------------------------------------ *
 *  Memo Details
 * ------------------------------------------------------------------ */
static UInt16	gDetCategory;
static Boolean	gDetCatEdited;
static UInt8	gDetTarget;
static Char		gDetDbLabel[dmDBNameLength + 8];

static void FmtDate (UInt32 secs, Char* out) UI4_SECTION;
static void FmtDate (UInt32 secs, Char* out)
{
	DateTimeType dt;
	UInt16 n;
	if (! secs) { StrCopy (out, "Unknown"); return; }
	TimSecondsToDateTime (secs, &dt);
	DateToAscii ((UInt8) dt.month, (UInt8) dt.day, (UInt16) dt.year,
		(DateFormatType) PrefGetPreference (prefDateFormat), out);
	StrCat (out, " ");
	n = (UInt16) StrLen (out);
	TimeToAscii ((UInt8) dt.hour, (UInt8) dt.minute,
		(TimeFormatType) PrefGetPreference (prefTimeFormat), out + n);
}

static Boolean DetPwProtected (void) UI4_SECTION;
static Boolean DetPwProtected (void)
{
	if (currentPasswordP != 0) return true;
	if (currentDB == memoPadDB) return false;
	return CryptoGetRecordFlag (SrcRef (currentDB), currentRecord);
}

static void DetailsInit (FormPtr frm) UI4_SECTION;
static void DetailsInit (FormPtr frm)
{
	UInt16	attr = 0;
	UInt32	created = 0, modified = 0;
	Char	buf[32];
	Boolean	rom = SrcIsRom (currentDB);
	ControlPtr ctl;

	DmRecordInfo (currentDBRef, currentRecord, &attr, NULL, NULL);
	gDetCategory = SrcRecCategory (currentDB, currentRecord);
	gDetCatEdited = false;
	gDetTarget = currentDB;
	CategoryGetName (dbMemoPadRef, gDetCategory, categoryName);
	CategorySetTriggerLabel ((ControlPtr) Obj (frm, popDetailsCategory), categoryName);

	SrcLabel (currentDB, gDetDbLabel);
	ctl = (ControlPtr) Obj (frm, popDetailsDb);
	CtlSetLabel (ctl, gDetDbLabel);
	if (rom || currentDB == memoPadDB) CtlSetEnabled (ctl, false);

	CtlSetValue ((ControlPtr) Obj (frm, chkDetailsPrivate), (attr & dmRecAttrSecret) != 0);
	ctl = (ControlPtr) Obj (frm, chkDetailsReadOnly);
	CtlSetValue (ctl, SrcRecReadOnly (currentDB, currentRecord));
	if (rom || currentDB == memoPadDB) CtlSetEnabled (ctl, false);

	SrcProtectionText (currentDB, currentRecord, DetPwProtected (), buf);
	FrmCopyLabel (frm, lblDetailsProt, buf);
	if (currentDB != memoPadDB)
		MetaGet (currentDBRef, currentRecord, &created, &modified, NULL);
	FmtDate (created, buf);
	FrmCopyLabel (frm, lblDetailsCreated, buf);
	FmtDate (modified, buf);
	FrmCopyLabel (frm, lblDetailsModified, buf);
}

static void DetailsPickDb (FormPtr frm) UI4_SECTION;
static void DetailsPickDb (FormPtr frm)
{
	ListPtr	lst = (ListPtr) Obj (frm, lstDetailsDb);
	Int16	sel;
	UInt8	m;

	if (SrcIsRom (currentDB) || currentDB == memoPadDB) return;
	DbListBuild (true);
	LstSetListChoices (lst, gDbChoices, (Int16) gDbCount);
	LstSetHeight (lst, (Int16) (gDbCount < 8 ? gDbCount : 8));
	LstSetSelection (lst, DbListIndexOf (gDetTarget));
	sel = LstPopupList (lst);
	if (sel < 0) return;
	m = gDbMap[sel];
	if (m == kDbCreate)
		{
		UInt8 s;
		if (! DbNameDialog (false, 0, &s)) return;
		m = s;
		}
	else if (SrcIsRom (m))
		{
		FrmAlert (altMoveIntoRom);
		return;
		}
	gDetTarget = m;
	SrcLabel (m, gDetDbLabel);
	CtlSetLabel ((ControlPtr) Obj (frm, popDetailsDb), gDetDbLabel);
}

/* OK: returns false to stay in the form. */
static Boolean DetailsApply (FormPtr frm) UI4_SECTION;
static Boolean DetailsApply (FormPtr frm)
{
	UInt16	attr = 0, updateCode = 0;
	UInt32	recordID = 0;
	Boolean	priv, ro, oldRo, dirty = false;
	Boolean	rom = SrcIsRom (currentDB);
	UInt16	oldCat = SrcRecCategory (currentDB, currentRecord);

	DmRecordInfo (currentDBRef, currentRecord, &attr, &recordID, NULL);
	priv = (Boolean) (CtlGetValue ((ControlPtr) Obj (frm, chkDetailsPrivate)) != 0);
	ro = (Boolean) (CtlGetValue ((ControlPtr) Obj (frm, chkDetailsReadOnly)) != 0);
	oldRo = SrcRecReadOnly (currentDB, currentRecord);

	if (rom)
		{
		if (gDetCategory != oldCat || priv != ((attr & dmRecAttrSecret) != 0))
			FrmAlert (altMemoInRom);
		FrmReturnToForm (frmEditForm);
		if (gDetCatEdited) FrmUpdateForm (frmEditForm, updateCategoryChanged);
		return true;
		}

	if (gDetTarget != currentDB && gDetTarget == cryptoPadDB && DetPwProtected ()
			&& prefsGlobalPasswordP != 0 && globalPasswordP == 0)
		{
		FrmAlert (altMoveNeedGlobal);
		return false;
		}

	if (gDetCategory != oldCat)
		{
		attr = (UInt16) ((attr & ~dmRecAttrCategoryMask) | gDetCategory);
		dirty = true;
		if (useSingleFont == separateFont)
			{
			GetTableFontOfCategory (gDetCategory);
			SetOrGetRecordFont (currentDBRef, recordID, setFont, fontTable);
			}
		updateCode |= updateCategoryChanged;
		}
	if (gDetCatEdited)
		{
		currentCategory = gDetCategory;
		updateCode |= updateCategoryChanged;
		}
	if (priv != ((attr & dmRecAttrSecret) != 0))
		{
		if (priv)
			{
			attr |= dmRecAttrSecret;
			if (PrivateRecordVisualStatus == showPrivateRecords)
				FrmAlert (privateRecordInfoAlert);
			}
		else attr &= ~dmRecAttrSecret;
		dirty = true;
		}
	if (dirty)
		{
		attr |= dmRecAttrDirty;
		DmSetRecordInfo (currentDBRef, currentRecord, &attr, NULL);
		}
	if (ro != oldRo && currentDB != memoPadDB)
		MetaSetReadOnly (currentDBRef, currentRecord, ro);

	FrmReturnToForm (frmEditForm);
	ViewInvalidate ();

	if (gDetTarget != currentDB)
		{
		EventType e;
		MemSet (&e, sizeof (e), 0);
		e.eType = dbMoveEvent;
		e.data.generic.datum[0] = gDetTarget;
		EvtAddEventToQueue (&e);
		}
	else if (ro != oldRo)
		/* reopen the memo in its new state from the list */
		FrmGotoForm (frmMainForm);
	else if (updateCode)
		FrmUpdateForm (frmEditForm, updateCode);
	return true;
}

Boolean DetailsFormHandleEvent (EventPtr eventP)
{
	Boolean	handled = false;
	FormPtr	frm = FrmGetActiveForm ();

	switch (eventP->eType)
		{
		case frmOpenEvent:
			DetailsInit (frm);
			FrmDrawForm (frm);
			handled = true;
			break;

		case ctlSelectEvent:
			switch (eventP->data.ctlSelect.controlID)
				{
				case btnDetailsOK:
					DetailsApply (frm);
					handled = true;
					break;

				case btnDetailsCancel:
					FrmReturnToForm (frmEditForm);
					if (gDetCatEdited) FrmUpdateForm (frmEditForm, updateCategoryChanged);
					handled = true;
					break;

				case btnDetailsDelete:
					if (EditViewOnlyRefuse ()) { handled = true; break; }
					if (DetailsFormDeleteCurrentRecord ())
						{
						FrmEraseForm (frm);
						FrmDeleteForm (frm);
						FrmCloseAllForms ();
						ViewInvalidate ();
						FrmGotoForm (frmMainForm);
						}
					handled = true;
					break;

				case popDetailsCategory:
					{
					Boolean upd;
					if (SrcIsRom (currentDB)) { FrmAlert (altMemoInRom); handled = true; break; }
					upd = CategorySelect (dbMemoPadRef, frm, popDetailsCategory, lstDetailsCategory,
						false, &gDetCategory, categoryName, 1, 0);
					gDetCatEdited = (Boolean) (upd || gDetCatEdited);
					handled = true;
					}
					break;

				case popDetailsDb:
					DetailsPickDb (frm);
					handled = true;
					break;
				}
			break;

		default:
			break;
		}
	return handled;
}

/* ------------------------------------------------------------------ *
 *  Manage Databases
 * ------------------------------------------------------------------ */
static void FmtSize (UInt32 bytes, Char* out) UI4_SECTION;
static void FmtSize (UInt32 bytes, Char* out)
{
	/* tenths of a KB without a 32-bit divide: (bytes * 10) >> 10 */
	UInt32 tenths = ((bytes << 3) + (bytes << 1)) >> 10;
	UInt32 whole = 0;
	UInt16 frac, n = 0;
	Int16 k;
	Char num[12];
	/* whole = tenths / 10, frac = tenths % 10, by subtracting 10 * 2^k
	 * (long division, no divide instruction) */
	for (k = 20; k >= 0; k--)
		if (tenths >= (10UL << k)) { tenths -= (10UL << k); whole += (1UL << k); }
	frac = (UInt16) tenths;
	StrIToA (num, (Int32) whole);
	StrCopy (out, num);
	n = (UInt16) StrLen (out);
	out[n++] = '.';
	out[n++] = (Char) ('0' + frac);
	out[n] = 0;
	StrCat (out, " KB");
}

static void ManageInfo (FormPtr frm) UI4_SECTION;
static void ManageInfo (FormPtr frm)
{
	ListPtr	lst = (ListPtr) Obj (frm, lstManageDb);
	Int16	sel = LstGetSelection (lst);
	UInt8	s;
	Char	buf[24];

	if (sel < 0 || sel >= (Int16) gDbCount) return;
	s = gDbMap[sel];
	StrIToA (buf, SrcCountRecords (s));
	SetLabel (frm, lblManageMemos, buf);
	FmtSize (SrcSizeBytes (s), buf);
	SetLabel (frm, lblManageSize, buf);
	SetLabel (frm, lblManageLoc, SrcIsRom (s) ? "ROM" : "RAM");
}

static void ManageFill (FormPtr frm, UInt8 select) UI4_SECTION;
static void ManageFill (FormPtr frm, UInt8 select)
{
	ListPtr	lst = (ListPtr) Obj (frm, lstManageDb);
	Int16	i;
	DbListBuild (false);
	LstSetListChoices (lst, gDbChoices, (Int16) gDbCount);
	i = DbListIndexOf (select);
	LstSetSelection (lst, (Int16) (i < 0 ? 0 : i));
	LstMakeItemVisible (lst, (Int16) (i < 0 ? 0 : i));
}

static UInt8 ManageSelected (FormPtr frm) UI4_SECTION;
static UInt8 ManageSelected (FormPtr frm)
{
	Int16 sel = LstGetSelection ((ListPtr) Obj (frm, lstManageDb));
	if (sel < 0 || sel >= (Int16) gDbCount) return cryptoPadDB;
	return gDbMap[sel];
}

static void ManageRedraw (FormPtr frm, UInt8 select) UI4_SECTION;
static void ManageRedraw (FormPtr frm, UInt8 select)
{
	ManageFill (frm, select);
	LstDrawList ((ListPtr) Obj (frm, lstManageDb));
	ManageInfo (frm);
}

Boolean ManageDbFormHandleEvent (EventPtr eventP)
{
	Boolean	handled = false;
	FormPtr	frm = FrmGetActiveForm ();
	UInt8	s;

	switch (eventP->eType)
		{
		case frmOpenEvent:
			ManageFill (frm, cryptoPadDB);
			FrmDrawForm (frm);
			ManageInfo (frm);
			handled = true;
			break;

		case lstSelectEvent:
			if (eventP->data.lstSelect.listID == lstManageDb)
				{
				ManageInfo (frm);
				handled = true;
				}
			break;

		case ctlSelectEvent:
			switch (eventP->data.ctlSelect.controlID)
				{
				case btnManageDone:
					FrmReturnToForm (0);
					ViewInvalidate ();
					FrmUpdateForm (frmMainForm, updateTable);
					handled = true;
					break;

				case btnManageNew:
					if (DbNameDialog (false, 0, &s)) ManageRedraw (frm, s);
					handled = true;
					break;

				case btnManageRename:
					s = ManageSelected (frm);
					if (s < 2) FrmAlert (altDbMainFixed);
					else if (SrcIsRom (s)) FrmAlert (altDbInRom);
					else if (DbNameDialog (true, s, NULL)) ManageRedraw (frm, s);
					handled = true;
					break;

				case btnManageDelete:
					s = ManageSelected (frm);
					if (s < 2) FrmAlert (altDbMainFixed);
					else if (SrcIsRom (s)) FrmAlert (altDbInRom);
					else
						{
						Char name[dmDBNameLength + 8], count[8];
						StrCopy (name, secDbPrefix);
						{
						Char label[dmDBNameLength + 8];
						SrcLabel (s, label);
						StrCat (name, label);
						}
						StrIToA (count, SrcCountRecords (s));
						if (FrmCustomAlert (altDeleteDbConfirm, name, count, "") == 0)
							{
							UInt16 err = SrcDelete (s);
							if (err) FrmAlert (err);
							currentRecord = noRecordSelected;
							currentDB = cryptoPadDB;
							ViewInvalidate ();
							ManageRedraw (frm, cryptoPadDB);
							}
						}
					handled = true;
					break;
				}
			break;

		default:
			break;
		}
	return handled;
}

/* ------------------------------------------------------------------ *
 *  Security
 * ------------------------------------------------------------------ */
static void SecApplyPrivacy (privateRecordViewEnum newStatus) UI4_SECTION;
static void SecApplyPrivacy (privateRecordViewEnum newStatus)
{
	Boolean wasHiding = (Boolean) (PrivateRecordVisualStatus == hidePrivateRecords);

	PrivateRecordVisualStatus = newStatus;
	if (wasHiding != (newStatus == hidePrivateRecords))
		{
		/* the open mode of the databases changes */
		UInt32 recordID;
		UInt16 mode = (newStatus == hidePrivateRecords) ?
			dmModeReadWrite : (dmModeReadWrite | dmModeShowSecret);
		SrcSelect (cryptoPadDB);
		SetApplicationPrefs (0);
		DmCloseDatabase (dbMemoPadRef);
		DmCloseDatabase (dbMainRef);
		dbCryptoPadRef = OpenOrCreateCryptoDatabase (mode);
		dbMemoPadRef = OpenMemoDatabase (mode);
		dbMainRef = dbCryptoPadRef;
		SrcReopenAll (mode);
		GetApplicationPrefs (&recordID);
		}
	currentRecord = noRecordSelected;
	ViewInvalidate ();
}

Boolean SecurityFormHandleEvent (EventPtr eventP)
{
	Boolean	handled = false;
	FormPtr	frm = FrmGetActiveForm ();
	ListPtr	lst;

	switch (eventP->eType)
		{
		case frmOpenEvent:
			lst = (ListPtr) Obj (frm, lstSecPrivacy);
			FrmCopyLabel (frm, lblSecDevice, DevHasDeviceID () ? "Available" : "Not available");
			LstSetSelection (lst, (Int16) PrivateRecordVisualStatus);
			CtlSetLabel ((ControlPtr) Obj (frm, popSecPrivacy),
				LstGetSelectionText (lst, (Int16) PrivateRecordVisualStatus));
			FrmDrawForm (frm);
			handled = true;
			break;

		case ctlSelectEvent:
			switch (eventP->data.ctlSelect.controlID)
				{
				case btnSecOK:
					{
					privateRecordViewEnum ns = (privateRecordViewEnum)
						LstGetSelection ((ListPtr) Obj (frm, lstSecPrivacy));
					if (ns != PrivateRecordVisualStatus)
						{
						/* as the Palm's own Security dialog: showing hidden or
						 * masked records asks for the Palm password */
						if (ns == showPrivateRecords)
							{
							if (! SecVerifyPW (showPrivateRecords)) { handled = true; break; }
							}
						PrefSetPreference (prefShowPrivateRecords, ns);
						FrmReturnToForm (0);
						SecApplyPrivacy (ns);
						FrmUpdateForm (frmMainForm, updateTable);
						}
					else FrmReturnToForm (0);
					handled = true;
					}
					break;

				case btnSecCancel:
					FrmReturnToForm (0);
					handled = true;
					break;
				}
			break;

		default:
			break;
		}
	return handled;
}
