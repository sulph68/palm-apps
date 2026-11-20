/* DbForms.h -- Memo Details, Manage Databases, the New/Rename Database
 * dialog, Security, and the read-only edit session (all in sec004). */
#ifndef __DBFORMS_H__
#define __DBFORMS_H__

#include <PalmOS.h>
#include "sects.h"

Boolean	DetailsFormHandleEvent (EventPtr eventP) UI4_SECTION;
Boolean	ManageDbFormHandleEvent (EventPtr eventP) UI4_SECTION;
Boolean	SecurityFormHandleEvent (EventPtr eventP) UI4_SECTION;

/* Read-only (or in ROM) memos open as a view: the text, decrypted in memory
 * when it has a password, in a field that cannot be edited; nothing is
 * written back. */
extern Boolean gEditViewOnly;
Boolean	EditViewOnlyRetrieve (FieldPtr fieldP) UI4_SECTION;	/* true: handled */
Boolean	EditViewOnlyRelease (FieldPtr fieldP) UI4_SECTION;		/* true: was a view, nothing to save */
Boolean	EditViewOnlyRefuse (void) UI4_SECTION;				/* true: alert shown, refuse the change */
Boolean	EditViewOnlyBlockKey (void) UI4_SECTION;

/* After the edit form saved the memo: move it (Memo Details > Database). */
void	EditMoveAfterSave (UInt8 to, Char* password) UI4_SECTION;
UInt16	EditTitlePosition (void) UI4_SECTION;	/* position for "Memo # of N" */

#endif
