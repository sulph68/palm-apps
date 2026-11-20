/* ViewList.h -- the main list as one view over every database (SecDb.h),
 * in the user's order: alphabetical by title, or manual (an order table in the
 * RAM database "CryptoNoteOrder", so memos in ROM can be reordered too). */
#ifndef __VIEWLIST_H__
#define __VIEWLIST_H__

#include <PalmOS.h>
#include "sects.h"

/* Find bar state (MainForm.c) */
extern Boolean	gDbFindActive;
extern char		gDbFindLower[40];
extern UInt16	gDbFindLen;
extern UInt16	gDbFindMatchCount;

void	ViewInvalidate (void) UI4_SECTION;
void	ViewBuild (void) UI4_SECTION;			/* rebuilds if invalid */
void	ViewFree (void) UI4_SECTION;
UInt16	ViewCount (void) UI4_SECTION;
Boolean	ViewGet (UInt16 pos, UInt8* src, UInt16* idx) UI4_SECTION;
Int16	ViewFind (UInt8 src, UInt16 idx) UI4_SECTION;	/* -1 if not in the view */

/* Manual order: move the memo at view position `from` to just before the one
 * at `to` (to == ViewCount() puts it last). */
void	ViewOrderMove (UInt16 from, UInt16 to) UI4_SECTION;
void	ViewOrderToTop (UInt8 src, UInt16 idx) UI4_SECTION;
/* A memo moved to another database keeps its place. */
void	ViewOrderReplace (UInt32 oldCrDate, UInt32 oldUid, UInt8 newSrc, UInt16 newIdx) UI4_SECTION;

#endif
