/*
 * DateSpan.h - Db35 multi-day event support: the "Set Date & Time"
 * sub-dialog shared by Event Details' Start:/End: rows, and the combined
 * date+time label formatter both rows use.
 *
 * Kept OUT of DateDay.c's sec001 (at the 32KB cap) -- same "thin trigger,
 * bulk logic lives elsewhere" split DateLocation.c already established for
 * the Location feature. Segment: SEC002 (most headroom, same as
 * ApptGetAppointments's own multi-day placement logic in DateDB.c).
 */
#ifndef DATESPAN_H
#define DATESPAN_H

#include <PalmOS.h>

#ifndef SEC002
#define SEC002
#endif

/* Format "7:30pm Fri 11/12/2035" (or just "Fri 11/12/2035" for an untimed
 * moment) into buf. buf is CALLER-owned, persistent storage (the same
 * CtlSetLabel-keeps-a-pointer-not-a-copy gotcha as every other trigger
 * label in this app applies -- pass a control's own CtlGetLabel buffer,
 * not a stack local). */
void DateSpanFormatLabel (DateType date, TimeType time, Char *buf) SEC002;

/* Edit the Start moment (date + time, with a "No Time" toggle -- a
 * whole-appointment property). Returns true if OK was pressed, updating
 * *dateP/*timeP in place (*timeP may come back as the apptNoTime sentinel).
 * false (Cancel): both left unchanged. */
Boolean DateSpanEditStart (DateType *dateP, TimePtr timeP) SEC002;

/* Edit the End moment relative to the (already-committed) start moment.
 * *offsetP (days after startDate) and *endTimeP are updated in place on OK;
 * the picked end date is clamped to [startDate, startDate+30]. The Time row
 * is hidden/unusable when startTime is the apptNoTime sentinel (an untimed
 * event has no per-day time to set, only a date range). Returns true if OK
 * was pressed, false (Cancel) leaves both unchanged. */
Boolean DateSpanEditEnd (DateType startDate, TimeType startTime,
	UInt8 *offsetP, TimePtr endTimeP) SEC002;

#endif /* DATESPAN_H */
