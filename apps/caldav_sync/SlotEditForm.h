#ifndef CDDV_SLOTEDITFORM_H
#define CDDV_SLOTEDITFORM_H

#include "CalDAVSync.h"

/* Runs the modal per-slot edit dialog (frmSlotEdit) - mirrors Address35's
 * own focused-dialog convention (frmPhoneDlg/frmEmailDlg) rather than
 * cramming every server-type field group onto the main form. Edits happen
 * on a local working copy; on OK the copy (incl. a freshly-typed secret,
 * Sec35-encrypted) is written back into *s and this returns true. On
 * Cancel, *s is left untouched and this returns false. */
Boolean SlotEditFormRun(CDDVSlot *s, UInt8 slotIdx) SEC001;

#endif
