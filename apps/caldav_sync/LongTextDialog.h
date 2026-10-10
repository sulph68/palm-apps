#ifndef CDDV_LONGTEXTDIALOG_H
#define CDDV_LONGTEXTDIALOG_H

#include "CalDAVSync.h"

/* Shared full-screen multi-line text editor for any long/URL-like field
 * (Server URL, Cal path, ICS feed URL, iCloud UUID, Google Calendar ID) -
 * mirrors Address35's EditNoteDialog/frmNoteDlg (read as reference):
 * hand-rolled modal loop, scrollbar synced to the field's own scroll
 * position, Cut/Copy/Paste via mnuDlgEdit. Populates the field from buf,
 * and on Done copies the edited text back into buf (bounded to bufCap,
 * silently truncated if longer - matches every other field's own MAXCHARS
 * ceiling in this app). Returns false (buf untouched) on Cancel. */
Boolean LongTextEditDialog(char *buf, UInt16 bufCap) SEC001;

#endif
