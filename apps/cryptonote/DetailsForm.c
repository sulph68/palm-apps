#include "DetailsForm.h"
#include "Database.h"
#include "Utils.h"

static UInt16 DetailsFormApplyChanges(void);
static void DetailsFormInit(void);

UInt16 category;
Boolean categoryEdited;


static void DetailsFormInit(void)
{
	UInt16 recAttributes;
	ControlPtr controlP;

	DmRecordInfo(currentDBRef, currentRecord, &recAttributes, 0, 0);

	/* Updates the current record category */

	category = (recAttributes & dmRecAttrCategoryMask);

	CategoryGetName(dbMemoPadRef, category, categoryName);
	CategorySetTriggerLabel(GetActiveFormObjectPtr(popDetailsCategory), categoryName);

	/* Updates the private checkbox */

	controlP = GetActiveFormObjectPtr(chkDetailsPrivate);
	CtlSetValue(controlP, (recAttributes & dmRecAttrSecret) == dmRecAttrSecret);
}

Boolean DetailsFormDeleteCurrentRecord(void)
{
	FormPtr formP;
	FieldPtr fieldP;

	Char *  textP;
	UInt32 recordID;
	Boolean empty;

	formP = FrmGetFormPtr(frmEditForm);
	fieldP = FrmGetObjectPtr(formP, FrmGetObjectIndex(formP, fldEditText));

	// Find out if the field is empty.
	textP = FldGetTextPtr (fieldP);
	empty = (*textP == 0);

	// Display an alert to confirm the operation.
	if (!empty) {
		if (currentDB == memoPadDB)	{
			UInt16 btn;
			FormPtr alert;
			Boolean backup;
			UInt16 ctlIndex;

			alert = FrmInitForm (frmDeleteForm);
	
			ctlIndex = FrmGetObjectIndex (alert, chkDeleteBackup);
			FrmSetControlValue (alert, ctlIndex, saveBackup);
			btn = FrmDoDialog (alert);
			backup = FrmGetControlValue (alert, ctlIndex);
	
			FrmDeleteForm (alert);

			if (btn  == btnDeleteCancel)
				return (false);
	
			saveBackup = backup;
		} else {
			if (FrmAlert(altConfirmDeleteAlert) == 1) return (false);
		}
	}

	// Clear the handle value in the field, otherwise the handle
	// will be freed when the form is disposed of.
	FldSetTextHandle (fieldP, 0);

	// Delete or archive the record.
	if (empty && (! FldDirty (fieldP))) {
		// Delete the record in the Font database.
		if ((useSingleFont == separateFont) 
			&& (currentDB == cryptoPadDB)) {
			DmRecordInfo (currentDBRef, currentRecord, 0, &recordID, 0);
			DeleteFontRecord(currentDBRef, recordID);
		}

		DmRemoveRecord (currentDBRef, currentRecord);
	} else
		{
		if (saveBackup && (currentDB == memoPadDB))
			DmArchiveRecord (dbMemoPadRef, currentRecord);
		else
			DmDeleteRecord (currentDBRef, currentRecord);
			DmMoveRecord (currentDBRef, currentRecord, DmNumRecords (currentDBRef));
		}

	memosInCategory--;
	currentRecord = noRecordSelected;

	RemoveCurrentPassword();

	return (true);
}


static UInt16 DetailsFormApplyChanges(void)
{
	UInt16 recAttributes;
	Boolean private;
	ControlPtr controlP;
	UInt16 updateCode = 0;
	UInt32 recordID;
	Boolean dirty = false;

	DmRecordInfo(currentDBRef, currentRecord, &recAttributes, &recordID, 0);
	
	/* Updates the memo category based on the category list */
	if (currentCategory != category) {
		recAttributes &= ~dmRecAttrCategoryMask;
		recAttributes |= category;
		dirty = true;

		/* Updates the tableFont of the record */
		if (useSingleFont == separateFont) {
			GetTableFontOfCategory(category);
			SetOrGetRecordFont(currentDBRef, recordID, setFont, fontTable);
		}

		updateCode |= updateCategoryChanged;
	}

	/* If the current category was deleted, renamed, or merged with
	* another category, then the list view needs to be redrawn.
	*/
	if (categoryEdited)
		{
		currentCategory = category;
		updateCode |= updateCategoryChanged;
		}


	/* Updates the secret bit based on the private checkbox */

	controlP = GetActiveFormObjectPtr(chkDetailsPrivate);
	private = (CtlGetValue(controlP) != 0);

	if (private != ((recAttributes & dmRecAttrSecret) == dmRecAttrSecret)) {
		if (private)  {
			recAttributes |= dmRecAttrSecret;
			if (PrivateRecordVisualStatus == showPrivateRecords)
				FrmAlert(privateRecordInfoAlert);
		} else recAttributes &= ~dmRecAttrSecret;

		dirty = true;
	}

	/* Apply changes in the record attributes */
	if (dirty) {
		recAttributes |= dmRecAttrDirty;
		DmSetRecordInfo(currentDBRef, currentRecord, &recAttributes, 0);
	}

	return (updateCode);
}


Boolean DetailsFormHandleEvent(EventPtr eventP)
{
	Boolean handled = false;
	Int16 updateCode;
	Boolean categoryUpdated;


	switch (eventP->eType) {
		case ctlSelectEvent:
			switch (eventP->data.ctlSelect.controlID) {
				case btnDetailsOK:
					updateCode = DetailsFormApplyChanges();
					FrmReturnToForm(frmEditForm);
	
					/* if the current category has be deleted, renamed or
					 * merged with another category, we need to update the
					 * edit form.
					 */
					if (updateCode)
						FrmUpdateForm (frmEditForm, updateCode);			
					handled = true;
					break;

				case btnDetailsCancel:
					if (categoryEdited)
						FrmUpdateForm (frmEditForm, updateCategoryChanged);
					FrmReturnToForm(frmEditForm);
					handled = true;
					break;

				/* Remove the current memo and return to the main form */
				case btnDetailsDelete:
					if (DetailsFormDeleteCurrentRecord()) {
						FormPtr		frm;

						frm = FrmGetActiveForm();
						FrmEraseForm (frm);
						FrmDeleteForm (frm);
						FrmCloseAllForms();
						FrmGotoForm(frmMainForm);
					}
					handled = true;
					break;

				/* Changes the memo category */
				case popDetailsCategory:
					categoryUpdated = CategorySelect(dbMemoPadRef, FrmGetActiveForm(),
									popDetailsCategory, lstDetailsCategory,
									false, &category, categoryName, 1, 0);
					categoryEdited = (categoryUpdated || categoryEdited);
					handled = true;
			}
			break;

		case frmOpenEvent:
			DetailsFormInit();
			FrmDrawForm(FrmGetActiveForm());
			category = currentCategory;
			categoryEdited = false;
			handled = true;
			break;
	}

	return handled;
}

