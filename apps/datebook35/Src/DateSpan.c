/*
 * DateSpan.c - Db35 multi-day event support. See DateSpan.h.
 *
 * The "Set Date & Time" sub-dialog (form DateSpanDialog) is opened by
 * Event Details' Start:/End: triggers (DateDay.c, DetailsHandleEvent) --
 * mirrors DateLocation.c's LocationEditForm structure closely (a hand-rolled
 * synchronous FrmInitForm/EvtGetEvent/FrmDispatchEvent loop, no SysMenu
 * helper needed here since this dialog has no text fields to Cut/Copy/Paste).
 */
#include <PalmOS.h>

#include <SelTime.h>		/* SelectOneTime */

#include "Datebook.h"		/* firstYear, TimeFormat, ShortDateFormat */
#include "DatebookRsc.h"	/* DateSpan*, DetailsStart/EndSelector      */
#include "DateSpan.h"
#include "DateDB.h"			/* apptNoTime */
#include "era.h"			/* EraDateToDOWDMFormat */
#include "datepick.h"		/* DatePickSelectDay */

#define kSpanMaxDays	30		/* matches DateDB.c's ApptGetAppointments lookback cap */

static void *SpanObj (FormPtr frm, UInt16 id) SEC002;
static void *SpanObj (FormPtr frm, UInt16 id)
{
	return FrmGetObjectPtr (frm, FrmGetObjectIndex (frm, id));
}

void DateSpanFormatLabel (DateType date, TimeType time, Char *buf) SEC002;
void DateSpanFormatLabel (DateType date, TimeType time, Char *buf)
{
	Char *p = buf;
	Char full[8], short2[8];
	UInt16 realYear = (UInt16) (date.year + firstYear);
	Int16 flen, i, slen;

	if (TimeToInt (time) != apptNoTime)
		{
		TimeToAscii (time.hours, time.minutes, TimeFormat, p);
		p += StrLen (p);
		*p++ = spaceChr;
		}
	EraDateToDOWDMFormat (date.month, date.day, realYear, ShortDateFormat, p);

	/* Db35: "2:00 pm Wed 8/12/2026" clipped against the Details trigger's
	 * width -- keep the weekday (wanted), shrink the year to 2 digits
	 * instead. Find wherever EraDateToDOWDMFormat wrote the 4-digit real
	 * year (don't assume it's at the end -- some date-format preferences
	 * put the year first) and replace it with its last 2 digits, same
	 * search-from-the-end idiom era.c's own SubstYear uses internally. */
	StrPrintF (full, "%d", realYear);
	StrPrintF (short2, "%02d", realYear % 100);
	flen = StrLen (full);
	slen = StrLen (p);
	for (i = (Int16) (slen - flen); i >= 0; i--)
		{
		if (StrNCompare (p + i, full, flen) == 0)
			{
			MemMove (p + i, short2, 2);
			MemMove (p + i + 2, p + i + flen, (UInt32) (slen - (i + flen) + 1));
			break;
			}
		}
}

/* Shared dialog body. startDate/startTime are the anchor (the Start moment,
 * already committed) -- NULL-equivalent (TimeToInt(*timeP)==apptNoTime with
 * isStart) when editing Start itself, in which case the Time row is always
 * shown/usable and a "No Time" checkbox is offered. When editing End
 * (isStart false), the Date picker is clamped to
 * [startDate, startDate+kSpanMaxDays] and the Time row is hidden/unusable
 * whenever startTime is untimed (an untimed event has no per-day time to
 * set on either end). Returns true (OK) with *dateP/*timeP updated, or
 * false (Cancel) unchanged. */
static Boolean DateSpanEditMoment (DateType startDate, TimeType startTime,
	Boolean isStart, DateType *dateP, TimePtr timeP) SEC002;
static Boolean DateSpanEditMoment (DateType startDate, TimeType startTime,
	Boolean isStart, DateType *dateP, TimePtr timeP)
{
	FormPtr		prev = FrmGetActiveForm ();
	FormPtr		frm;
	ControlPtr	dateCtl, timeCtl, noTimeChk = NULL;
	Char		*label;
	Boolean		ok = false, done = false;
	Boolean		untimed = isStart ? (TimeToInt (*timeP) == apptNoTime)
									: (TimeToInt (startTime) == apptNoTime);
	Boolean		timeRowUsable = isStart || !untimed;
	DateType	workDate = *dateP;
	TimeType	workTime = *timeP;

	/* Db35: defensive -- if the Time row is usable but *timeP still holds
	 * the apptNoTime sentinel (e.g. End's endTime wasn't defaulted when
	 * Start just turned the event from untimed to timed), formatting it
	 * below would produce garbage ("/:i5 pm"-style) instead of a sane
	 * default. DetailsHandleEvent's Start case is the real fix (defaults
	 * endTime when the transition happens) -- this is just a safety net. */
	if (timeRowUsable && TimeToInt (workTime) == apptNoTime)
		{ workTime.hours = 12; workTime.minutes = 0; }

	frm = FrmInitForm (DateSpanDialog);
	FrmSetActiveForm (frm);
	FrmSetTitle (frm, (Char *) (isStart ? "Start" : "End"));

	dateCtl = (ControlPtr) SpanObj (frm, DateSpanDateSelector);
	timeCtl = (ControlPtr) SpanObj (frm, DateSpanTimeSelector);

	label = (Char *) CtlGetLabel (dateCtl);
	EraDateToDOWDMFormat (workDate.month, workDate.day,
		(UInt16) (workDate.year + firstYear), ShortDateFormat, label);
	CtlSetLabel (dateCtl, label);

	// Db35: always show the Time trigger (never FrmHideObject it) -- when
	// untimed, it displays "No time" same as Start's does, just disabled
	// when there's nothing to set (End, whole event untimed). Hiding it
	// outright left a confusing gap instead of a visibly-inert control.
	label = (Char *) CtlGetLabel (timeCtl);
	if (untimed)
		{
		Char *rscP = MemHandleLock (DmGetResource (strRsc, noTimeStrID));
		StrCopy (label, rscP);
		MemPtrUnlock (rscP);
		}
	else
		TimeToAscii (workTime.hours, workTime.minutes, TimeFormat, label);
	CtlSetLabel (timeCtl, label);
	CtlSetEnabled (timeCtl, timeRowUsable);

	if (isStart)
		{
		noTimeChk = (ControlPtr) SpanObj (frm, DateSpanNoTimeCheckbox);
		CtlSetValue (noTimeChk, untimed);
		}
	else
		FrmHideObject (frm, FrmGetObjectIndex (frm, DateSpanNoTimeCheckbox));

	FrmDrawForm (frm);

	while (! done)
		{
		EventType ev;

		EvtGetEvent (&ev, evtWaitForever);

		if (ev.eType == ctlSelectEvent)
			{
			UInt16 cid = ev.data.ctlSelect.controlID;

			if (cid == DateSpanDateSelector)
				{
				Int16 month = workDate.month, day = workDate.day;
				Int16 year = (Int16) (workDate.year + firstYear);
				DateType lo = startDate, hi = startDate;

				if (! isStart)
					DateAdjust (&hi, kSpanMaxDays);

				if (DatePickSelectDay (selectDayByDay, &month, &day, &year, "Date"))
					{
					DateType picked;
					picked.month = (UInt8) month;
					picked.day = (UInt8) day;
					picked.year = (Int16) (year - firstYear);

					if (! isStart &&
						(DateToDays (picked) < DateToDays (lo) ||
						 DateToDays (picked) > DateToDays (hi)))
						{
						FrmAlert (altSpanOutOfRange);
						}
					else
						{
						workDate = picked;
						label = (Char *) CtlGetLabel (dateCtl);
						EraDateToDOWDMFormat (workDate.month, workDate.day,
							(UInt16) (workDate.year + firstYear), ShortDateFormat, label);
						CtlSetLabel (dateCtl, label);
						CtlDrawControl (dateCtl);
						}
					}
				continue;
				}

			if (cid == DateSpanTimeSelector && timeRowUsable)
				{
				Int16 h = workTime.hours, m = workTime.minutes;

				if (TimeToInt (workTime) == apptNoTime) { h = 0; m = 0; }
				if (SelectOneTime (&h, &m, "Time"))
					{
					workTime.hours = (UInt8) h;
					workTime.minutes = (UInt8) m;
					untimed = false;
					if (noTimeChk) CtlSetValue (noTimeChk, false);
					label = (Char *) CtlGetLabel (timeCtl);
					TimeToAscii (workTime.hours, workTime.minutes, TimeFormat, label);
					CtlSetLabel (timeCtl, label);
					CtlDrawControl (timeCtl);
					}
				continue;
				}

			if (cid == DateSpanNoTimeCheckbox)
				{
				untimed = CtlGetValue (noTimeChk);
				continue;
				}

			if (cid == DateSpanOkButton) { ok = true; done = true; continue; }
			if (cid == DateSpanCancelButton) { done = true; continue; }
			}
		else if (ev.eType == appStopEvent)
			{
			EvtAddEventToQueue (&ev);
			done = true;
			continue;
			}

		FrmDispatchEvent (&ev);
		}

	FrmEraseForm (frm);
	FrmDeleteForm (frm);
	if (prev) FrmSetActiveForm (prev);

	if (ok)
		{
		*dateP = workDate;
		if (isStart)
			{
			if (untimed)
				TimeToInt (*timeP) = apptNoTime;
			else
				*timeP = workTime;
			}
		else if (timeRowUsable)
			/* Db35: End's picked time was only ever landing in the LOCAL
			 * workTime, never written back through timeP -- this `else if`
			 * branch didn't exist before, so OK silently discarded any
			 * time change made in the End dialog. timeRowUsable (not
			 * `!untimed`) gates it: when the whole event is untimed, End's
			 * Time row is disabled and workTime was never touched, so
			 * leave *timeP alone rather than writing back a stale value. */
			*timeP = workTime;
		}

	return ok;
}

Boolean DateSpanEditStart (DateType *dateP, TimePtr timeP) SEC002;
Boolean DateSpanEditStart (DateType *dateP, TimePtr timeP)
{
	return DateSpanEditMoment (*dateP, *timeP, true, dateP, timeP);
}

Boolean DateSpanEditEnd (DateType startDate, TimeType startTime,
	UInt8 *offsetP, TimePtr endTimeP) SEC002;
Boolean DateSpanEditEnd (DateType startDate, TimeType startTime,
	UInt8 *offsetP, TimePtr endTimeP)
{
	DateType endDate = startDate;
	Boolean ok;

	DateAdjust (&endDate, *offsetP);
	ok = DateSpanEditMoment (startDate, startTime, false, &endDate, endTimeP);
	if (ok)
		{
		Int32 days = (Int32) DateToDays (endDate) - (Int32) DateToDays (startDate);
		if (days < 0) days = 0;
		if (days > kSpanMaxDays) days = kSpanMaxDays;
		*offsetP = (UInt8) days;
		/* endTimeP: when the appointment is untimed (Time row was hidden),
		 * DateSpanEditMoment never touched it -- leave as-is (caller's
		 * existing value, still apptNoTime). */
		}
	return ok;
}
