/*
 * DateLocation.h - Db35 event Location feature: the Location edit dialog,
 * the Address35-contact picker, the "Previous Loc" (LocationsDB) picker,
 * and the Country picker -- all modeled closely on Address35's own address
 * dialog/pickers (address35/EditDialogs.c). See locdb.h for the storage
 * model and Datebook35.md for the full design writeup.
 */
#ifndef DATELOCATION_H
#define DATELOCATION_H

#include <PalmOS.h>
#include "locdb.h"

/* Runs the Location edit dialog as a modal round-trip. inOut is both the
 * starting state (may be blank/zeroed -- "no location yet") and, on a true
 * return, the edited result. Returns true if the user tapped OK (inOut
 * updated in place, including fromAddress35/locDbRecUID); false if
 * Cancel (inOut left completely untouched). Never touches Address35DB or
 * LocationsDB itself -- callers own the LinkSet/LocDbAdd/LocDbUpdate
 * commit step (see DetailsApply in DateDay.c). */
Boolean LocationEditForm (DlxLink* inOut) SEC002;

/* ---- Details-dialog Location field glue ----------------------------
 * The Location row (DatebookRsc.h DetailsLocationGadget, id 1321) is a
 * real EDITABLE MULTIPLELINES FIELD, not a GADGET -- same mechanism
 * Address35 uses for "tap this field to launch a sub-dialog instead of
 * editing inline" (EditForm.c intercepts fldEnterEvent for its
 * ftAddress/ftEmail/etc. rows before the ROM lets them become editable).
 * DateDay.c's sec001 is at the 32KB cap, so the actual edit/staging logic
 * lives entirely in this file (module-static gCurrentLoc); DateDay.c only
 * makes these 3 thin cross-segment calls:
 *   1. LocGadgetLoad(frm, apptUid) -- once, in DetailsInit (seeds
 *      gCurrentLoc AND sets the field's displayed text)
 *   2. LocFieldTapped(frm) -- from DetailsHandleEvent's fldEnterEvent case,
 *      when event->data.fldEnter.fieldID == DetailsLocationGadget (must
 *      mark the event handled so the ROM never lets the field itself take
 *      focus/enter edit mode)
 *   3. LocGadgetCommit(apptUid) -- once, in DetailsApply, on OK
 * ---------------------------------------------------------------------- */

/* Seed gCurrentLoc from LinkGetDatabase/LinkGet for the given appointment
 * uid (zeroed if none/db missing), and set the Location field's displayed
 * text to match (or a "Tap to set location" placeholder if blank). */
void LocGadgetLoad (FormPtr frm, UInt32 apptUid) SEC002;

/* Call from DetailsHandleEvent's fldEnterEvent case. Runs the Location
 * edit dialog and, on OK, updates gCurrentLoc + the field's displayed
 * text. Always returns true (the caller should always mark the event
 * handled -- this field must never actually enter inline edit mode). */
Boolean LocFieldTapped (FormPtr frm) SEC002;

/* Persist whatever gCurrentLoc is now (LinkSet/LinkDelete +
 * LocDbAdd/LocDbUpdate per the explicit-link rules -- see locdb.h) for the
 * given appointment uid. Idempotent -- safe to call even if the user never
 * touched the field this session. */
void LocGadgetCommit (UInt32 apptUid) SEC002;

/* ---- Day View "@ location" suffix ----
 * Builds a fresh MemHandle containing "desc @ locSummary" (NULL on alloc
 * failure). Used by DayViewGetDescription (DateDay.c) only when
 * editable==false and a prefetched summary exists for this row -- kept
 * here (not inline in DateDay.c) purely to keep sec001 minimal. */
MemHandle DayBuildLocSuffix (const char* desc, const char* locSummary) SEC002;

#endif /* DATELOCATION_H */
