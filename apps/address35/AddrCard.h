/*
 * AddrCard.h - Address35 SD-card vCard flow: Import/Export folders + the "Card"
 * browse source, layered on the generic CardIO VFS wrapper + the vCard engine.
 *
 * Card layout:
 *   /Address/           <- "Card" browse source (vCards directly here)
 *   /Address/Import/     <- Import menu reads vCards from here
 *   /Address/Export/     <- Export menu writes vCards here
 *
 * Foreground-only (menu-driven); lives in sec001.
 */
#ifndef ADDRCARD_H
#define ADDRCARD_H

#include <PalmOS.h>
#include "a35_seg.h"
#include "AddressDB35.h"
#include "CardIO.h"
#include "vcard.h"

/* one browsable vCard on the card: its file leaf + parsed display name */
typedef struct {
	char	file[cardMaxName];	/* leaf under /Address/ (e.g. jane_doe.vcf) */
	char	name[64];			/* display name for the list                */
} A35CardItem;

#define a35CardRoot		"/Address"
#define a35CardImportDir	"/Address/Import"
#define a35CardExportDir	"/Address/Export"

/* export scope (mirrors beam record-vs-category, plus tagged/all) */
enum { a35ExpRecord = 0, a35ExpCategory, a35ExpTagged, a35ExpAll };

#define a35MaxCardBrowse	128		/* max card vCards listed in one browse */

#ifdef __cplusplus
extern "C" {
#endif

/* Create /Address + /Address/Import + /Address/Export if absent. */
Err A35CardInit(UInt16 volRef) SEC001;

/* Lightweight text-only busy toast ("Reading...") - no progress bar, sized to
 * fit the message, screen-centered. For operations with no real progress
 * metric (a directory scan, a single-file preview read). `title` must be a
 * string literal / static storage (drawn once, not kept, but matches the
 * progress dialog's own convention below). */
void A35BusyBegin(const char *title) SEC001;
void A35BusyEnd(void) SEC001;

/* Real MODAL progress dialog with a full-width hand-drawn bar ("Processing
 * vCard...", "Importing..."), same idiom as Sec35's own "Working" progress
 * dialog - for operations that DO have a real progress metric (splitting or
 * importing multiple vCards). `title` must be a string literal / static
 * storage (FrmSetTitle keeps the pointer, not a copy). A35ProgressBar is also
 * usable directly as a VCardProgressFn callback for the streaming reader. */
void A35ProgressBegin(const char *title) SEC001;
void A35ProgressBar(void *ctx, Int16 barWidthPx) SEC001;
void A35ProgressEnd(void) SEC001;

/* Import every vCard in every .vcf under /Address/Import/ into db. *countP gets
 * the number of contacts added. */
Err A35CardImport(DmOpenRef db, UInt16 volRef, UInt16 *countP) SEC001;

/* Export contacts to /Address/Export/, one "firstname_lastname[_N].vcf" per
 * contact. scope = a35ExpXxx; category/tagId/recIndex select the set. *countP gets
 * the number written. */
Err A35CardExport(DmOpenRef db, UInt16 volRef, UInt16 scope,
	UInt16 category, UInt16 tagId, UInt16 recIndex, UInt16 *countP) SEC001;

/* --- "Card" browse source (vCards directly under /Address/, or a folder under
 * it) --- */

/* How many vCards are packed into the file at path (0 on read failure/empty).
 * Used to decide whether tapping a file should prompt "split into N contacts?"
 * before previewing/splitting it - browsing itself no longer auto-splits.
 * Streamed, not a whole-file read - see the .c file's own comment. */
UInt16 A35CardCountAt(UInt16 volRef, DmOpenRef db, const char *path) SEC001;

/* Split the (already-confirmed multi-person) file at path into one
 * "firstname_lastname[_N].vcf" per person under dir (normally the file's own
 * folder), then delete the original. total16 is the caller's own already-
 * computed A35CardCountAt result (avoids a redundant second full open+read
 * of the same card file). Returns the number of people written (0 if path
 * turned out not to be multi-person - left untouched in that case). */
UInt16 A35CardSplitFile(UInt16 volRef, DmOpenRef db, const char *dir, const char *path,
	UInt16 total16) SEC001;

/* List the browsable contacts under /Address/ (excludes Import/ and Export/).
 * Each file's first vCard is parsed for the display name. */
Err A35CardBrowseList(UInt16 volRef, DmOpenRef db, A35CardItem *items,
	UInt16 max, UInt16 *countP) SEC001;

/* Read + parse the first vCard of the file at absolute path into *r (values in
 * *arena). */
Err A35CardReadContact(UInt16 volRef, DmOpenRef db, const char *path,
	A35Record *r, VCardArena *arena) SEC001;

#ifdef __cplusplus
}
#endif

#endif /* ADDRCARD_H */
