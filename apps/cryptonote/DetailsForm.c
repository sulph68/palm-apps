#include "DetailsForm.h"
#include "Database.h"
#include "Utils.h"
#include "DbForms.h"

/* Memo Details itself is in DbForms.c (full-screen form with Database,
 * Read-only, Protection and dates, 2026-11-19). This file keeps the delete
 * path the edit form's menu shares with it. */

Boolean DetailsFormDeleteCurrentRecord(void)
{
	FormPtr formP;
	FieldPtr fieldP;

	Char *  textP;
	UInt32 recordID;
	Boolean empty;

	/* a read-only memo, or one in ROM, cannot be deleted */
	if (EditViewOnlyRefuse ()) return (false);

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
			&& (currentDB != memoPadDB)) {
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


