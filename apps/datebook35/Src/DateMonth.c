/******************************************************************************
 *
 * Copyright (c) 1996-1999 Palm Computing, Inc. or its subsidiaries.
 * All rights reserved.
 *
 * File: DateMonth.c
 *
 * Description:
 *	  This is the Datebook application's month view module.
 *
 * History:
 *		April 8, 1996	Created by Art Lamb
 *
 *****************************************************************************/

#include <PalmOS.h>
#include "Datebook.h"
#include "ToDoDB.h"     /* Db35: month-view ToDo markers (Td35 due dates) */
#include "DateDAV.h"    /* Db35: month-view CalDAV Sync markers/bars */

#include "era.h"        /* Db35: real-era-year date display */
#include "datepick.h"  /* Db35: era-aware full-range date picker */
extern void PrintApptRecord (DmOpenRef dbP, UInt16 index);


/***********************************************************************
 *
 *	Internal Constants
 *
 ***********************************************************************/
#define linesInMonthPlusTitle			7
#define maxWeeksInMonth					6

// Day number's margins
#define textLeftMargin					3
#define textTopMargin					1


// Event indicators
#define lunchStartTime					0x0b1e		// 11:30 am
#define lunchMidTime						0x0c0f		// 12:15 pm
#define lunchEndTime						0xd00			//  1:00 pm

#define timeIndicatorWidth				2
#define timeIndicatorTopMargin		3
#define timeIndicatorRightMargin		2

#define untimeIndicatorTopMargin		13
#define untimeIndicatorRightMargin	5

#define repeatIndicatorBottomMargin	2


#define morningHeight					4
#define lunchHeight						3
#define afternoonHeight					4

#define drawDaySelected					true
#define dontDrawDaySelected			false

#define TodayIsVisible(monthP,today) ((monthP->month == today.month) && (monthP->year == today.year))

/***********************************************************************
 *
 *	Internal Structutes
 *
 ***********************************************************************/
typedef struct {
	RectangleType			bounds;
	Int16						month;			// month displayed
	Int16						year;				// year displayed
	DateTimeType			selected;
} MonthType;

typedef MonthType * MonthPtr;



/***********************************************************************
 *
 * FUNCTION:    FirstDayOfMonth
 *
 * DESCRIPTION: This routine returns the day-of-the-week, adjusted by the 
 *              preference setting that specifies the first day-of-
 *              the-week.  If the date passed is a Tuesday and the 
 *              start day of week is Monday, this routine will return
 *              a value of one.
 *
 * PARAMETERS:  monthP     - pointer to MontnType structure
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	4/8/96	Initial Revision
 *
 ***********************************************************************/
SEC003 static UInt16 FirstDayOfMonth (MonthPtr monthP)
	{
	return (DayOfWeek (monthP->month, 1, monthP->year) - 
			StartDayOfWeek + daysInWeek) % daysInWeek;
	}


/***********************************************************************
 *
 * FUNCTION:    MonthDrawInversionEffect
 *
 * DESCRIPTION: This routine does an inversion effect by swapping colors
 *					 this is NOT undoable by calling it a second time, rather
 *					 it just applies a selected look on top of already
 *					 rendered data.  (It's kind of a hack.)
 *
 * PARAMETERS:	 rP - pointer to a rectangle to 'invert'
 *					 cornerDiam - corner diameter
 *					 
 * RETURNED:	 nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			bob	10/28/99	Initial revision
 *			jmp	11/14/99	Modified for use with Datebook's MonthView (i.e., backcolor
 *								changed to UIObjectFill).
 *
 ***********************************************************************/
SEC003 static void MonthDrawInversionEffect (const RectangleType *rP, UInt16 cornerDiam) 
{
	WinPushDrawState();
	WinSetDrawMode(winSwap);
	WinSetPatternType (blackPattern);
	WinSetBackColor(UIColorGetTableEntryIndex(UIObjectFill));
	WinSetForeColor(UIColorGetTableEntryIndex(UIObjectSelectedFill));
	WinPaintRectangle(rP, cornerDiam);
	
	if (UIColorGetTableEntryIndex(UIObjectSelectedFill) != UIColorGetTableEntryIndex(UIObjectForeground)) {
		WinSetBackColor(UIColorGetTableEntryIndex(UIObjectForeground));
		WinSetForeColor(UIColorGetTableEntryIndex(UIObjectSelectedForeground));
		WinPaintRectangle(rP, cornerDiam);
	}
	WinPopDrawState();
}


/***********************************************************************
 *
 * FUNCTION:    GetDayBounds
 *
 * DESCRIPTION: This routine returns the bounds of the specified day
 *
 * PARAMETERS:  monthP  - pointer to month object
 *              day     - day of month
 *              r       - returned: bounds of the day     
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	4/10/96	Initial Revision
 *
 ***********************************************************************/
SEC003 static void GetDayBounds (MonthPtr monthP, UInt16 day, RectanglePtr r)
	{
	UInt16 cell;
	Int16	cellWidth;
	Int16 cellHeight;

	cell = day - 1 + FirstDayOfMonth (monthP);
	cellWidth = monthP->bounds.extent.x / daysInWeek;
	cellHeight = monthP->bounds.extent.y / linesInMonthPlusTitle;
	r->topLeft.x = monthP->bounds.topLeft.x + (cell % daysInWeek) * cellWidth;
	r->topLeft.y = monthP->bounds.topLeft.y + ((cell / daysInWeek) + 1) * cellHeight;
	r->extent.x = cellWidth;
	r->extent.y = cellHeight;
	}
	

/***********************************************************************
 *
 * FUNCTION:    GetDaySelectionBounds
 *
 * DESCRIPTION: This routine returns the selection bounds of the specified day
 *
 * PARAMETERS:  monthP  - pointer to month object
 *              day     - day of month
 *              r       - returned: bounds of the day selection 
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			jmp	11/14/99	Initial Revision
 *			jmp	11/30/99	Grrr... swapped meaning of "cell" and "day" and
 *								broke selection of Sunday vs. Monday start-of-week
 *								selection.  Fixed that!
 *
 ***********************************************************************/
SEC003 static void GetDaySelectionBounds(MonthPtr monthP, UInt16 day, RectanglePtr r)
{
	UInt16 cell;
	Int16	cellWidth;
	Int16 cellHeight;
	Int16 charWidth;
	
	cell = day - 1 + FirstDayOfMonth (monthP);
	cellWidth = monthP->bounds.extent.x / daysInWeek;
	cellHeight = monthP->bounds.extent.y / linesInMonthPlusTitle;
	charWidth = FntCharWidth('0');
	r->topLeft.x = monthP->bounds.topLeft.x + (cell % daysInWeek) * cellWidth;
	r->topLeft.y = monthP->bounds.topLeft.y + ((cell / daysInWeek) + 1) * cellHeight;
	r->extent.x = ((day < 10) ? charWidth : charWidth * 2) + (textLeftMargin * 2);
	r->extent.y = FntLineHeight () + (textTopMargin * 2);
	RctInsetRectangle (r, 1);
}

/***********************************************************************
 *
 * FUNCTION:    DrawAppointment
 *
 * DESCRIPTION: This routine draw an appointment indicator.
 *
 * PARAMETERS:  dayR  - bounds of the day's drawing region
 *              start - appointment's start time
 *              end   - appointment's end time
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	4/8/96	Initial Revision
 *
 ***********************************************************************/
SEC003 static void DrawAppointment (RectanglePtr dayR, TimeType start, TimeType end)
{
	Int16 x, y;
	RectangleType r;
	
	// Db35: untimed appointments are drawn as a small circle under the date
	// numeral by MonthDrawMarkers (see DrawMonth), not the ROM "+" here.
	if (TimeToInt(start) == apptNoTime)
		return;

	// Draw the indicator for an timed appointment.
	x = dayR->topLeft.x + dayR->extent.x - timeIndicatorWidth - 
		timeIndicatorRightMargin;
	y = dayR->topLeft.y + timeIndicatorTopMargin;

	// Draw indicator for a morning appointment.
	if (TimeToInt(start) < lunchStartTime)
		{
		r.topLeft.x = x;
		r.topLeft.y = y;
		r.extent.x = timeIndicatorWidth;
		r.extent.y = morningHeight;
		WinDrawRectangle (&r, 0);
		}

	// Draw indicator for a lunch appointment.
	if ((TimeToInt(start) >= lunchStartTime && TimeToInt(start) < lunchEndTime) ||
		 (TimeToInt(start) < lunchStartTime && TimeToInt(end) > lunchMidTime))
		{
		r.topLeft.x = x;
		r.topLeft.y = y + morningHeight  + 1;
		r.extent.x = timeIndicatorWidth;
		r.extent.y = lunchHeight;
		WinDrawRectangle (&r, 0);
		}

	// Draw indicator for a afternoon appointment.
	if (TimeToInt(start) >= lunchEndTime ||
		(TimeToInt(start) < lunchEndTime && TimeToInt(end) > lunchEndTime))
		{
		r.topLeft.x = x;
		r.topLeft.y = y + morningHeight + lunchHeight + 2;
		r.extent.x = timeIndicatorWidth;
		r.extent.y = afternoonHeight;
		WinDrawRectangle (&r, 0);
		}
}

/***********************************************************************
 *
 * FUNCTION:    DrawTimedAppointments
 *
 * DESCRIPTION: This routine draws appointment indicators for all the 
 *              appointment in a month.
 *
 * PARAMETERS:  monthP  - month object
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	4/10/96	Initial Revision
 *
 ***********************************************************************/
#define kMonthSpanMaxDays	30	/* Db35: matches DateDB.c's own span cap */

/* ---- Db35: month-view untimed-event (circle) + ToDo (square) markers ---- *
 * Precompute per-day flags once, then draw: a 3x3 circle under the FIRST date
 * numeral for days with an untimed event, and a 3x3 square under the SECOND
 * numeral for days with a ToDo due. Undated ToDos put a square on Today only.
 * Declared here (rather than just above MonthScanMarkers) because
 * ScanAndDrawAppointments -- which sets gMonHasUntimed[] -- is defined first. */
static Boolean gMonHasUntimed[32];	/* index by day 1..31 */
static Boolean gMonHasTodo[32];
static Boolean gMonUndatedTodo;

/* Db35: draw a (possibly multi-day) appointment/occurrence's indicator on
 * EVERY day of [occDate, occDate+span] that falls within monthP's grid,
 * clamping start/end per day the same way ApptPlaceSpanning (DateDB.c)
 * clamps for Day/Week/Agenda -- the day it really starts shows the real
 * start, the day it really ends shows the real end, any day strictly
 * between shows the whole day. A single day out-of-grid is just skipped
 * (not a scan-stopping condition -- span is capped at 30, cheap either
 * way). */
static void DrawSpanningAppointment (MonthPtr monthP, DateType occDate,
	UInt8 span, TimeType startTime, TimeType endTime) SEC003;
static void DrawSpanningAppointment (MonthPtr monthP, DateType occDate,
	UInt8 span, TimeType startTime, TimeType endTime)
{
	Int16 i;
	RectangleType dayR;

	for (i = 0; i <= (Int16) span; i++)
		{
		DateType d = occDate;
		TimeType dayStart, dayEnd;

		DateAdjust (&d, i);
		if (d.month != monthP->month || d.year != monthP->year - firstYear)
			continue;

		if (i == 0) dayStart = startTime;
		else { dayStart.hours = 0; dayStart.minutes = 0; }

		if (i == span) dayEnd = endTime;
		else { dayEnd.hours = 23; dayEnd.minutes = 59; }

		GetDayBounds (monthP, d.day, &dayR);
		DrawAppointment (&dayR, dayStart, dayEnd);
		}
}

/* Db35 perf: DrawTimedAppointments, DrawRepeatingAppointments and
 * MonthScanMarkers's own untimed-marker loops used to each independently
 * re-scan ApptDB's date-sorted forward range and/or its repeating-records
 * prefix -- up to 5 separate full scans of the same records for one month
 * draw, each paying for ApptGetRecord's per-record AES-CTR decrypt even
 * though NONE of this logic ever reads description/note text. Merged into
 * ONE scan per range (forward / repeating) that reads each record ONCE (via
 * the non-decrypting ApptGetWhenRepeat) and runs all the ORIGINAL loop
 * bodies -- verbatim, each with its own local date cursor, exactly as they
 * ran when they were separate outer-loop passes -- against that one read.
 * This is a scan-count/decrypt-count reduction, not a logic rewrite: every
 * gate, backward-lookback, and stopping condition below is preserved
 * byte-for-byte from the original three functions.
 *
 * One real behavioral fix was required, not just a merge: MonthScanMarkers's
 * forward loop used to start exactly at the 1st of the month and stop at the
 * first non-same-month record (safe there, since it never looked further
 * back). DrawTimedAppointments' forward loop starts kMonthSpanMaxDays EARLIER
 * (for spanning appointments) and stops once a record's start is past the
 * month's end. Merging onto the wider range with the OLD "stop at first non-
 * same-month record" rule would break out immediately on the very first
 * (correctly-skipped, prior-month) record and never reach any in-month
 * records at all -- so the merged loop uses DrawTimedAppointments' own
 * "recStartDays > monthEndDays" stop condition throughout, and the untimed-
 * marker set is gated purely by the (unchanged) sameMonth check, with no
 * separate early break. */
SEC003 static void ScanAndDrawAppointments (MonthPtr monthP)
{
	UInt16	recordNum;
	DateType date, tempDate, monthStart;
	Int32 monthFirstDays, monthEndDays, recStartDays;
	Boolean untimed, sameMonth;
	MemHandle recordH;
	ApptDBRecordType apptRec;
	UInt8 span;

	monthStart.day = 1;
	monthStart.month = monthP->month;
	monthStart.year = monthP->year - firstYear;

	monthFirstDays = (Int32) DateToDays (monthStart);
	tempDate = monthStart;
	DateAdjust (&tempDate, DaysInMonth (monthP->month, monthP->year));
	monthEndDays = (Int32) DateToDays (tempDate) - 1;

	/* ---- forward pass: non-repeating appointments ---- *
	 * (was DrawTimedAppointments' first loop + MonthScanMarkers' first loop) */
	tempDate = monthStart;
	DateAdjust (&tempDate, -kMonthSpanMaxDays);
	ApptFindFirst (ApptDB, tempDate, &recordNum);
	while (true)
		{
		recordH = DmQueryNextInCategory (ApptDB, &recordNum, dmAllCategories);
		if (! recordH) break;

		ApptGetWhenRepeat (ApptDB, recordNum, &apptRec, &recordH);
		span = apptRec.endDateOffset;
		recStartDays = (Int32) DateToDays (apptRec.when->date);
		untimed = TimeToInt(apptRec.when->startTime) == apptNoTime;

		// Sorted by start date -- once a record's OWN start is past the
		// whole month, nothing later (in either loop's data) can matter.
		if (recStartDays > monthEndDays)
			{
			MemHandleUnlock (recordH);
			break;
			}

		if ((recStartDays + (Int32) span >= monthFirstDays) &&
			 ((ShowUntimedAppts && untimed) || (ShowTimedAppts && (! untimed))))
			DrawSpanningAppointment (monthP, apptRec.when->date, span,
				apptRec.when->startTime, apptRec.when->endTime);

		sameMonth = (apptRec.when->date.month == monthP->month) &&
					(apptRec.when->date.year == monthP->year - firstYear);
		if (sameMonth && untimed &&
			apptRec.when->date.day >= 1 && apptRec.when->date.day <= 31)
			gMonHasUntimed[apptRec.when->date.day] = true;

		MemHandleUnlock (recordH);
		recordNum++;
		}


	/* ---- repeating pass ---- *
	 * (was DrawTimedAppointments' second loop + DrawRepeatingAppointments +
	 * MonthScanMarkers' second loop -- repeating appointments are stored at
	 * the beginning of the database.) */
	recordNum = 0;
	while (true)
		{
		recordH = DmQueryNextInCategory (ApptDB, &recordNum, dmAllCategories);
		if (! recordH) break;

		ApptGetWhenRepeat (ApptDB, recordNum, &apptRec, &recordH);
		untimed = TimeToInt(apptRec.when->startTime) == apptNoTime;

		if (apptRec.repeat)
			{
			/* (a) was DrawTimedAppointments' repeating-draw walk */
			if ((ShowUntimedAppts && untimed) || (ShowTimedAppts && (! untimed)))
				{
				UInt8 back;

				span = apptRec.endDateOffset;

				// Db35: backward lookback -- an occurrence that started up to
				// span days before the 1st of this month can still span into
				// it. Same idiom as DateDB.c's ApptGetAppointments.
				for (back = 1; back <= span && back <= kMonthSpanMaxDays; back++)
					{
					DateType candidate = monthStart;
					DateAdjust (&candidate, -(Int16) back);
					if (ApptRepeatsOnDate (&apptRec, candidate))
						DrawSpanningAppointment (monthP, candidate, span,
							apptRec.when->startTime, apptRec.when->endTime);
					}

				date = monthStart;
				while (ApptNextRepeat (&apptRec, &date))
					{
					DateType temp;

					if ((date.month != monthP->month) ||
						(date.year != monthP->year - firstYear))
						break;

					DrawSpanningAppointment (monthP, date, span,
						apptRec.when->startTime, apptRec.when->endTime);

					temp = date;
					DateAdjust (&date, 1);
					if (DateToInt (temp) == DateToInt (date)) break;
					}
				}

			/* (b) was DrawRepeatingAppointments (daily-tick, only if shown) */
			if (ShowDailyRepeatingAppts &&
				apptRec.repeat->repeatType == repeatDaily &&
				apptRec.repeat->repeatFrequency == 1)
				{
				Int16 x1, x2, y;
				DateType temp;
				Boolean first, last;
				RectangleType dayR;

				date = monthStart;

				// if the first day of this daily repeating event is in this month
				// then we draw a vertical tick.  If it's not (it's in the prior
				// month) then don't.
				first = (Boolean) (DateToInt(apptRec.when->date) >= DateToInt(date));

				while (ApptNextRepeat (&apptRec, &date))
					{
					if ((date.month != monthP->month) ||
						 (date.year != monthP->year - firstYear))
						break;

					GetDayBounds (monthP, date.day, &dayR);
					y = dayR.topLeft.y + dayR.extent.y - repeatIndicatorBottomMargin;
					x1 = dayR.topLeft.x;
					x2 = dayR.topLeft.x + dayR.extent.x;

					last = (DateToInt (date) == DateToInt (apptRec.repeat->repeatEndDate));

					if (first)
						{
						first = false;
						WinDrawLine (x1+1, y-1, x1+1, y+1);
						x1 += 2;
						}

					if (last)
						{
						WinDrawLine (x2-1, y-1, x2-1, y+1);
						x2 -= 2;
						}

					WinDrawGrayLine (x1, y, x2, y);

					temp = date;
					DateAdjust (&date, 1);
					if (DateToInt (temp) == DateToInt (date)) break;
					}
				}

			/* (c) was MonthScanMarkers' untimed-repeating-marker walk */
			if (untimed)
				{
				date = monthStart;
				while (ApptNextRepeat (&apptRec, &date))
					{
					DateType temp;
					if (date.month != monthP->month ||
						date.year != monthP->year - firstYear) break;
					if (date.day >= 1 && date.day <= 31) gMonHasUntimed[date.day] = true;
					temp = date; DateAdjust (&date, 1);
					if (DateToInt (temp) == DateToInt (date)) break;
					}
				}
			}
		MemHandleUnlock (recordH);

		// If the record has no repeating info we've reached the end of the
		// repeating appointments.
		if (! apptRec.repeat) break;

		recordNum++;
		}
}

/* Db35 perf (plan Part C): MonthScanMarkers' ToDo35 scan is a single,
 * already-non-duplicated pass, but it re-walks EVERY ToDo35 record on every
 * Month View redraw regardless of whether anything relevant changed --
 * unlike ApptDB, ToDo35 isn't due-date sorted, so there's no cheap seek/
 * early-break the way ScanAndDrawAppointments has; only an index avoids the
 * full walk. Building a real uid->due-date index is bigger scope (the plan
 * recommends folding it into the separately-tracked Day View birthday/ToDo
 * injection cache task, which also needs it), so this is a smaller, Month-
 * View-local result cache instead: skip the scan entirely when redrawing the
 * SAME displayed month and ToDo35's own modificationNumber (DmDatabaseInfo)
 * hasn't changed since the last scan -- any real add/edit/delete/complete-
 * toggle bumps modNum, so this can never serve stale data, it only skips
 * redundant re-scans of unchanged data (e.g. a frmUpdateEvent after a
 * covering dialog closes with nothing actually touched). */
static UInt16  gMonTodoCachedMonth = 0;	/* 0 = never cached (valid months are 1-12) */
static UInt16  gMonTodoCachedYear;
static UInt32  gMonTodoCachedModNum;
static Boolean gMonTodoCachedHasTodo[32];
static Boolean gMonTodoCachedUndatedTodo;

SEC003 static void MonthScanMarkers (MonthPtr monthP)
{
	UInt16 i, n;
	DmOpenRef todoDB;

	/* Db35 perf: the untimed-appointment marker flags (gMonHasUntimed[]) used
	 * to be set by two more ApptDB scans right here -- they're now set by
	 * ScanAndDrawAppointments instead (which needs to visit those same records
	 * anyway, to draw them), see DrawMonth's call-order comment for why this
	 * function must still run FIRST (it owns the reset). Only the ToDo35 scan
	 * (gMonHasTodo[]/gMonUndatedTodo) is still done here. */
	for (i = 0; i < 32; i++) { gMonHasUntimed[i] = false; gMonHasTodo[i] = false; }
	gMonUndatedTodo = false;

	/* ToDo items due this month + any undated (Td35 DB) */
	if (ToDoGetDatabase (&todoDB, dmModeReadOnly) == errNone && todoDB) {
		UInt16	cardNo;
		LocalID	dbID;
		UInt32	modNum = 0;

		if (DmOpenDatabaseInfo (todoDB, &dbID, NULL, NULL, &cardNo, NULL) == errNone)
			/* cardNo, dbID, name, attr, version, crDate, modDate, bckUpDate,
			 * modNum, appInfoID, sortInfoID, type, creator */
			DmDatabaseInfo (cardNo, dbID, NULL, NULL, NULL, NULL, NULL, NULL,
				&modNum, NULL, NULL, NULL, NULL);

		if (gMonTodoCachedMonth == monthP->month && gMonTodoCachedYear == monthP->year &&
			gMonTodoCachedModNum == modNum)
			{
			/* same month, ToDo35 unchanged since we last scanned it -- reuse */
			for (i = 1; i <= 31; i++) gMonHasTodo[i] = gMonTodoCachedHasTodo[i];
			gMonUndatedTodo = gMonTodoCachedUndatedTodo;
			}
		else
			{
			n = DmNumRecords (todoDB);
			for (i = 0; i < n; i++) {
				MemHandle h = DmQueryRecord (todoDB, i);
				ToDoDBRecordPtr rec;
				if (! h) continue;
				rec = (ToDoDBRecordPtr) MemHandleLock (h);
				if (! (rec->priority & 0x80)) {		/* not completed */
					DateType due = rec->dueDate;
					if (DateToInt (due) == toDoNoDueDate) {
						gMonUndatedTodo = true;
					} else if (due.month == monthP->month &&
							   due.year == monthP->year - firstYear &&
							   due.day >= 1 && due.day <= 31) {
						gMonHasTodo[due.day] = true;
					}
				}
				MemHandleUnlock (h);
			}

			gMonTodoCachedMonth = monthP->month;
			gMonTodoCachedYear = monthP->year;
			gMonTodoCachedModNum = modNum;
			for (i = 1; i <= 31; i++) gMonTodoCachedHasTodo[i] = gMonHasTodo[i];
			gMonTodoCachedUndatedTodo = gMonUndatedTodo;
			}
		DmCloseDatabase (todoDB);
	}
}

/* Db35: month-view CalDAV Sync markers/bars -- same result-cache precedent
 * as MonthScanMarkers's own ToDo35 scan just above (keyed off
 * DateBook35DAV.pdb's own modificationNumber via DmDatabaseInfo), since
 * DateDAVGet itself does a full per-day DmQueryRecord scan and calling it
 * once per visible day (up to 31 times) on EVERY redraw would be exactly
 * the kind of redundant-rescan cost that precedent was written to avoid.
 * Unlike the ToDo scan (a simple per-day boolean), a timed CalDAV event
 * needs its own real start/end time to draw a spanning bar via the
 * already-existing DrawSpanningAppointment, so the cache holds a small
 * flat list of (day, start, end) alongside the untimed boolean array.
 * Called AFTER ScanAndDrawAppointments (see DrawMonth's own call-order
 * comment - MonthScanMarkers OWNS the gMonHasUntimed[] reset, so anything
 * that SETS more of those flags, this included, must run after it) and
 * BEFORE MonthDrawMarkers (which reads gMonHasUntimed[] to actually draw
 * the circle marker). */
static UInt16   gMonDavCachedMonth = 0;	/* 0 = never cached */
static UInt16   gMonDavCachedYear;
static UInt32   gMonDavCachedModNum;
static Boolean  gMonDavCachedUntimed[32];
#define kMaxMonDavTimed 48
static UInt8    gMonDavCachedTimedDay[kMaxMonDavTimed];
static TimeType gMonDavCachedTimedStart[kMaxMonDavTimed];
static TimeType gMonDavCachedTimedEnd[kMaxMonDavTimed];
static UInt16   gMonDavCachedTimedCount;

/* Per-day scratch for the (cache-miss) rescan path - static, not a stack
 * local: this project has real crash history from large stack-resident
 * structs. No text buffer here at all - DateDAVGet's lines[] parameter is
 * NULL-able (added for exactly this caller): Month view never displays
 * the text, only start/end times, so paying for a kDateDavLineLen-sized
 * per-day text buffer would be pure waste - a real, measured "region
 * datares is full" link error (the app's data/BSS segment, 32KB like the
 * code segments) before this fix. */
#define kMonDavPerDayMax 8
static TimeType gMonDavScratchStart[kMonDavPerDayMax];
static TimeType gMonDavScratchEnd[kMonDavPerDayMax];

SEC003 static void MonthScanAndDrawCalDav (MonthPtr monthP)
{
	UInt16 lastDay, d, i;
	DmOpenRef db;
	LocalID dbID;
	UInt32 modNum = 0;
	UInt16 cardNo;

	dbID = DmFindDatabase (0, "DateBook35DAV");
	if (dbID == 0) return;		/* CalDAV Sync never installed/synced */
	db = DmOpenDatabase (0, dbID, dmModeReadOnly);
	if (! db) return;
	if (DmOpenDatabaseInfo (db, &dbID, NULL, NULL, &cardNo, NULL) == errNone)
		/* cardNo, dbID, name, attr, version, crDate, modDate, bckUpDate,
		 * modNum, appInfoID, sortInfoID, type, creator */
		DmDatabaseInfo (cardNo, dbID, NULL, NULL, NULL, NULL, NULL, NULL,
			&modNum, NULL, NULL, NULL, NULL);
	DmCloseDatabase (db);

	lastDay = DaysInMonth (monthP->month, monthP->year);

	if (gMonDavCachedMonth == monthP->month && gMonDavCachedYear == monthP->year &&
		gMonDavCachedModNum == modNum)
		{
		/* same month, DateBook35DAV unchanged since we last scanned it -- reuse */
		for (d = 1; d <= lastDay; d++)
			if (gMonDavCachedUntimed[d]) gMonHasUntimed[d] = true;
		for (i = 0; i < gMonDavCachedTimedCount; i++)
			{
			DateType occDate;
			occDate.day = gMonDavCachedTimedDay[i];
			occDate.month = monthP->month;
			occDate.year = (UInt8) (monthP->year - firstYear);
			DrawSpanningAppointment (monthP, occDate, 0,
				gMonDavCachedTimedStart[i], gMonDavCachedTimedEnd[i]);
			}
		return;
		}

	for (d = 1; d <= 31; d++) gMonDavCachedUntimed[d] = false;
	gMonDavCachedTimedCount = 0;

	for (d = 1; d <= lastDay; d++)
		{
		UInt16 n = DateDAVGet (monthP->year, monthP->month, (UInt8) d,
			NULL, gMonDavScratchStart, gMonDavScratchEnd,
			NULL, NULL, NULL, kMonDavPerDayMax);
		for (i = 0; i < n; i++)
			{
			if (gMonDavScratchStart[i].hours == (UInt8) apptNoTime)
				{
				gMonHasUntimed[d] = true;
				gMonDavCachedUntimed[d] = true;
				}
			else
				{
				DateType occDate;
				occDate.day = (UInt8) d;
				occDate.month = monthP->month;
				occDate.year = (UInt8) (monthP->year - firstYear);
				DrawSpanningAppointment (monthP, occDate, 0,
					gMonDavScratchStart[i], gMonDavScratchEnd[i]);

				if (gMonDavCachedTimedCount < kMaxMonDavTimed)
					{
					UInt16 c = gMonDavCachedTimedCount;
					gMonDavCachedTimedDay[c] = (UInt8) d;
					gMonDavCachedTimedStart[c] = gMonDavScratchStart[i];
					gMonDavCachedTimedEnd[c] = gMonDavScratchEnd[i];
					gMonDavCachedTimedCount++;
					}
				}
			}
		}

	gMonDavCachedMonth = monthP->month;
	gMonDavCachedYear = monthP->year;
	gMonDavCachedModNum = modNum;
}

SEC003 static void MonthDrawMarkers (MonthPtr monthP, Boolean selectDay)
{
	DateTimeType today;
	Int16 lastDay, i, charW, numH;
	Boolean todayVisible, todayHl;

	FntSetFont (stdFont);
	charW = FntCharWidth ('0');
	numH = FntLineHeight ();
	lastDay = DaysInMonth (monthP->month, monthP->year);

	TimSecondsToDateTime (TimGetSeconds (), &today);
	todayVisible = (Boolean) (today.month == monthP->month &&
		today.year == monthP->year && TodayIsVisible (monthP, today));
	todayHl = (Boolean) (selectDay && todayVisible);	/* today's cell is highlighted */

	for (i = 1; i <= lastDay; i++) {
		RectangleType cell;
		Int16 mx, my, yAdj;
		Boolean todo;

		GetDayBounds (monthP, i, &cell);
		mx = cell.topLeft.x + textLeftMargin;		/* under the first numeral */
		/* on the highlighted "today" cell keep the lower position (clears the
		 * inverted box); on every other cell nudge the markers 1px up to centre. */
		yAdj = (Int16) ((todayHl && i == today.day) ? 0 : -1);
		my = cell.topLeft.y + textTopMargin + numH - 1;	/* ~2px below the numeral */

		if (gMonHasUntimed[i]) {			/* 4x4 "rounded square" (corners omitted)
						 * under the first numeral; +2px down clears the selection
						 * highlight */
			Int16 cy = (Int16) (my + 2 + yAdj);
			WinDrawLine (mx + 1, cy,     mx + 2, cy);		/* top    */
			WinDrawLine (mx + 1, cy + 3, mx + 2, cy + 3);	/* bottom */
			WinDrawLine (mx,     cy + 1, mx,     cy + 2);	/* left   */
			WinDrawLine (mx + 3, cy + 1, mx + 3, cy + 2);	/* right  */
		}

		todo = gMonHasTodo[i] ||
			(gMonUndatedTodo && todayVisible && i == today.day);
		if (todo) {						/* square under the second numeral */
			RectangleType r;
			r.topLeft.x = (Coord) (mx + charW + 1);	/* +1px right */
			r.topLeft.y = (Coord) (my + 3 + yAdj);	/* +3px down (−1 off today) */
			r.extent.x = 2; r.extent.y = 2;
			WinDrawRectangleFrame (simpleFrame, &r);
		}
	}
}


/***********************************************************************
 *
 * FUNCTION: 	 DrawMonth
 *
 * DESCRIPTION: Draw the month object.
 *
 * PARAMETERS:  monthP - pointer to month object to draw
 *					 selectDay - if true, draw "today" selected if possible
 *
 * RETURNED:	 nothing
 *
 *	HISTORY:
 *		04/08/96	rsf	Created by Roger Flores.
 *		08/26/99	kwk	Use DateTemplateToAscii to get the DOW first letter.
 *		10/18/99	gap	Get DOW labels from daysOfWeekInitialsStrID resource and
 *							use StartDayOfWeek to determine which letter is start day.
 *		10/31/99	jmp	Eliminate WinInvertRectangle(); draw day as selected instead.
 *							Also, always call DrawAppointmentsInMonth(), as DrawMonth()
 *							was always followed by that call -- helps consolidate various
 *							drawing problems.
 *		11/14/99	jmp	Updated the drawing such that non-selectable areas of the MonthView
 *							are in standard Form colors while selectable areas use control-
 *							style colors.  Also, added selectDay argument to allow today's
 *							date to either by drawn selected or not.
 *
 ***********************************************************************/
SEC003 static void DrawMonth (MonthPtr monthP, Boolean selectDay)
	{
	Int16				drawX, drawY;
	Int16				cellWidth, cellHeight;
	Int16				charWidth;
	Int16				lastDay;
	Char				dowTemplate[] = "^1s";
	Char				dayInAscii[3];
	UInt16			dow;
	UInt16			i;
	Int16				x, y;
	DateTimeType	today;
	RectangleType	r;
	UInt8 			dayOfWeek;
 	Char *			dayLabels;
	UInt16			labelLength;
	Char*				label;

	WinPushDrawState();
	WinSetBackColor(UIColorGetTableEntryIndex(UIFormFill));
	WinSetForeColor(UIColorGetTableEntryIndex(UIObjectForeground));
	WinSetTextColor(UIColorGetTableEntryIndex(UIObjectForeground));
	FntSetFont (boldFont);
	
	// Make sure the "unselectable" parts of the MonthView are
	// drawn in the proper colors first.
	WinEraseRectangle (&monthP->bounds, 0);
	
	cellWidth = monthP->bounds.extent.x / daysInWeek;
	cellHeight = monthP->bounds.extent.y / linesInMonthPlusTitle;

	// Get the resource that contains the first letter of each day.
 	dayLabels = MemHandleLock (DmGetResource (strRsc, daysOfWeekInitialsStrID));

	// Calculate length of one item in string */
	labelLength = StrLen (dayLabels) / daysInWeek;

	// Draw the days of the week labels right justified to the number columns	
	// Be sure to draw the labels with respect to current setting of StartDayOfWeek. Somes locales
	// consider Monday the first day while others use Sunday.  There is also a user preference 
	// selection in Prefs app/Formats panelthat will allow the user to change first day of week.
	drawY = 	monthP->bounds.topLeft.y + (cellHeight - FntLineHeight()) / 2;
	for (i = 0; i <= daysInWeek; i++)
		{
		dayOfWeek = (i + StartDayOfWeek) % daysInWeek;
		label = &dayLabels[labelLength * dayOfWeek];
		drawX = monthP->bounds.topLeft.x + (cellWidth * i) + ((cellWidth - FntCharsWidth (label, labelLength)) / 2);
		WinDrawChars (label, labelLength, drawX, drawY);
		}

	// Unlock the day of week label resource now that we are done with it.
	MemPtrUnlock (dayLabels);
	
	// Set the background color behind the grid in control-style colors since
	// the grid is selectable.
	WinSetBackColor(UIColorGetTableEntryIndex(UIObjectFill));
	RctCopyRectangle(&monthP->bounds, &r);
	r.topLeft.y += cellHeight;
	r.extent.y -= cellHeight;
	WinEraseRectangle (&r, 0);
	
	// Draw the grid.  Change the foreground color temporarily to get the right effect,
	// and put it back when we're done.
	WinSetForeColor(WinRGBToIndex(&colorLine));
	x = monthP->bounds.topLeft.x;
	y = monthP->bounds.topLeft.y + cellHeight;
	for (i = 0; i < daysInWeek + 1; i++)
		{
		WinDrawLine (x, y, x, y + (cellHeight * maxWeeksInMonth));
		x += cellWidth;
		}

	x = monthP->bounds.topLeft.x;
	for (i = 0; i < maxWeeksInMonth + 1; i++)
		{
		WinDrawLine (x, y, x + (cellWidth * daysInWeek) , y);
		y += cellHeight;
		}
	WinSetForeColor(UIColorGetTableEntryIndex(UIObjectForeground));

	// Draw the days of the month.
	FntSetFont (stdFont);
	charWidth = FntCharWidth('0');
	
	dow = FirstDayOfMonth (monthP);
	drawX = monthP->bounds.topLeft.x + (dow * cellWidth) + textLeftMargin;
	drawY = monthP->bounds.topLeft.y + cellHeight + textTopMargin;
	lastDay = DaysInMonth(monthP->month, monthP->year);
	for (i=1; i <= lastDay; i++, dow++)
		{
		if (dow == daysInWeek)
			{
			drawX = monthP->bounds.topLeft.x + textLeftMargin;
			drawY += cellHeight;
			dow = 0;
			}
		StrIToA (dayInAscii, i);
		WinDrawChars (dayInAscii, 
						  (i < 10) ? 1 : 2, 
//						  (i < 10) ? drawX + charWidth : drawX,
						  drawX,
						  drawY);
		drawX += cellWidth;
		}

	// Display a rectangle around today's day if it's visible.
	TimSecondsToDateTime(TimGetSeconds(), &today);
	if (TodayIsVisible(monthP, today) && selectDay)
		{
		GetDaySelectionBounds(monthP, today.day, &r);
		MonthDrawInversionEffect (&r, 0);
		}

	/* Db35: MonthScanMarkers FIRST -- it resets gMonHasUntimed[]/gMonHasTodo[] to
	 * all-false before its own ToDo35 scan, so it must run before
	 * ScanAndDrawAppointments sets any gMonHasUntimed[] flags, or they'd be wiped
	 * right back out. */
	MonthScanMarkers (monthP);

	/* Db35 perf: draws appointment indicators AND sets the untimed-event marker
	 * flags in one pass -- see ScanAndDrawAppointments' own comment. */
	ScanAndDrawAppointments (monthP);

	/* Db35: synced CalDAV Sync events - bars for timed, gMonHasUntimed[]
	 * flags for all-day. Must run after MonthScanMarkers (which owns the
	 * gMonHasUntimed[] reset) and before MonthDrawMarkers (which reads it). */
	MonthScanAndDrawCalDav (monthP);

	/* Db35: circle (untimed event) + square (ToDo due) markers under the numerals */
	MonthDrawMarkers (monthP, selectDay);

	WinPopDrawState ();
	}


/***********************************************************************
 *
 * FUNCTION: 	 MapPointToItem
 *
 * DESCRIPTION: Return return the item at x, y.
 *
 * PARAMETERS:  x, y - coordinate
 *              r - the bounds of the item area (not the MTWTFSS area)
 *
 * RETURNED:	 item number (doesn't check for invalid bounds!)
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			roger	11/15/94	Initial Revision
 *
 ***********************************************************************/
SEC003 static Int16 MapPointToItem (Int16 x, Int16 y, RectanglePtr r)
	{
	Int16 itemNumber;

	itemNumber = daysInWeek * (((y - r->topLeft.y) /
		(r->extent.y / linesInMonthPlusTitle)) - 1);
	itemNumber += ((x - r->topLeft.x) / (r->extent.x / daysInWeek));
	return itemNumber;
	}


/***********************************************************************
 *
 * FUNCTION:    MonthViewGetMonthPtr
 *
 * DESCRIPTION: This routine returns a pointer to the month object.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:	 pointer to a MonthType structure
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	4/10/96	Initial Revision
 *			jmp	10/6/99	Replace FrmGetActiveForm() with FrmGetFormPtr();
 *
 ***********************************************************************/
SEC003 static MonthPtr MonthViewGetMonthPtr (void)
	{
	FormPtr frm;
	
	frm = FrmGetFormPtr (MonthView);
	return (FrmGetGadgetData (frm, FrmGetObjectIndex (frm, MonthGadget)));
	}


/***********************************************************************
 *
 * FUNCTION:    MonthViewSetTitle
 *
 * DESCRIPTION: This routine set the title of the Month View
 *
 * PARAMETERS:  monthP  - pointer to month object (MonthType)
 *
 * RETURNED:	 nothing
 *
 * HISTORY:
 *		04/10/96	art	Created by Art Lamb.
 *		09/08/99	kwk	Use DateTemplateToAscii & load format from string resource.
 *		10/6/99	jmp	Replace FrmGetActiveForm() with FrmGetFormPtr();
 *
 ***********************************************************************/
SEC003 static void MonthViewSetTitle (MonthPtr monthP)
	{
	Char title [longDateStrLength];
	MemHandle templateH;
	Char* templateP;
	
	templateH = DmGetResource(strRsc, MonthViewTitleStrID);
	templateP = (Char*)MemHandleLock(templateH);
	EraDateTemplateToAscii(templateP, monthP->month, 1, monthP->year, true, title, sizeof(title) - 1);
	MemHandleUnlock(templateH);
	
	FrmCopyTitle (FrmGetFormPtr (MonthView), title);
	TimeDisplayed = false;
	}

	
/***********************************************************************
 *
 * FUNCTION:    MonthViewHideTime
 *
 * DESCRIPTION: If the title of the Day View is displaying the current 
 *              time, this routine will change the title to the standard
 *					 title (the current date).
 *
 * PARAMETERS:  nothing
 *
 * PARAMETERS:  hide - true to always hide, false hide only if
 *                     to time has been display for the require
 *                      length of time.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	5/14/96	Initial Revision
 *			grant	2/2/99	Use TimeToWait(), don't use EvtSetNullEventTick()
 *
 ***********************************************************************/
SEC003 static void MonthViewHideTime (Boolean hide)
{
	MonthPtr 	monthP;
	if (TimeDisplayed)
		{
		if (hide || TimeToWait() == 0)
			{
			monthP = MonthViewGetMonthPtr ();
			MonthViewSetTitle (monthP);
			}
		}
}


/***********************************************************************
 *
 * FUNCTION:    MonthSelectDayRect
 *
 * DESCRIPTION: This routine selects or unselects day in the month
 *					 object.
 *
 * PARAMETERS:	 monthP   - pointer to control object (ControlType)
 *              r        - pointer to day's rectangle
 *					 selected - says whether to draw the rectangle selected
 *									or unselected
 *
 * RETURNED:	nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			jmp	10/31/99	Initial Revision.
 *			jmp	11/06/99	Oops, was just doing a WinResetClip() instead of
 *								save/restore WinSetClip().
 *			jmp	11/14/99	Added support for drawing the current day's selection
 *								unselected to get the right selection effect on
 *								"today."
 *			jmp	11/19/99	Use WinScreenLock()/WinScreenUnlock() to eliminate all of
 *								the ugly redrawing effects.
 *       jmp   12/02/99 Fix bug #24377.  Don't call WinScreenUnlock() if WinScreenLock()
 *                      fails.
 *                          
 ***********************************************************************/
SEC003 static void MonthSelectDayRect (MonthPtr monthP, RectangleType *r, Boolean selected)
{
	RectangleType savedClip;
	UInt8 * lockedWinP;
	
	// Draw everything offscreen to begin with -- we have a lot to redraw, and
	// it's somewhat unsightly to see it all happen onscreen.  But still restrict
	// the clipping rectangle to get the effect we want.
	//
	lockedWinP = WinScreenLock(winLockCopy);
	WinGetClip(&savedClip);
	WinSetClip(r);
	
	// Always redraw unselected first.
	//
	DrawMonth(monthP, !selected);
	
	// If selection is desired, do that now.
	//
	if (selected)
	{
		RectangleType todayRect;
		DateTimeType today;

		// First, draw the entire rectangle selected.
		//
		MonthDrawInversionEffect(r, 0);
		
		// If we're drawing "today," then draw the
		// day selection itself unselected.
		//
		TimSecondsToDateTime(TimGetSeconds(), &today);
		GetDaySelectionBounds(monthP, today.day, &todayRect);
		
		if (TodayIsVisible(monthP, today) && RctPtInRectangle(r->topLeft.x, r->topLeft.y, &todayRect))
		{
			WinSetClip(&todayRect);
			DrawMonth(monthP, dontDrawDaySelected);
		}
	}

	// Now that we're done drawing, reset the clipping rectangle, and put everything we've done back
	// onscreen.
	//
	WinSetClip(&savedClip);
	if (lockedWinP)
		WinScreenUnlock();
}


/***********************************************************************
 *
 * FUNCTION:    MonthSelectDay
 *
 * DESCRIPTION: This routine selects a day in the month object.
 *
 * PARAMETERS:	 monthP  - pointer to control object (ControlType)
 *              eventP  - pointer to an EventType structure.
 *              dateP   - returned: pointer to the date selected
 *
 * RETURNED:	true if the event was handle or false if it was not.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	4/9/96	Initial Revision
 *			jmp	10/31/99	Eliminate WinInvertRectangle() by calling
 *								MonthSelectDayRect() instead.
 *
 ***********************************************************************/
SEC003 static Boolean MonthSelectDay (MonthPtr monthP, EventType* eventP, DatePtr dateP)
	{
	Int16				x, y;
	Int16       	item;
	Int16       	daysInMonth;
	Int16       	firstDayInMonth;
	Int16				cellWidth, cellHeight;
	Boolean			penDown;
	Boolean			selected;
	RectangleType	r;
	
	firstDayInMonth = FirstDayOfMonth (monthP);
	daysInMonth = DaysInMonth (monthP->month, monthP->year);

	// Restrict the selection bounds.  The top should be moved down
	// to exclude selecting the day of week titles.  The bottom should
	// be moved up to the last week with a day in it.
	RctCopyRectangle (&(monthP->bounds), &r);
	r.topLeft.y += r.extent.y / linesInMonthPlusTitle;
	r.extent.y = (r.extent.y / linesInMonthPlusTitle) *
		(((firstDayInMonth + daysInMonth - 1) / 7) + 1);

	if (! RctPtInRectangle (eventP->screenX, eventP->screenY, &r))
		return false;

	// Compute the bounds of the day the pen is on.
	item = MapPointToItem (eventP->screenX, eventP->screenY, &monthP->bounds);

	cellWidth = monthP->bounds.extent.x / daysInWeek;
	cellHeight = monthP->bounds.extent.y / linesInMonthPlusTitle;
	r.topLeft.x = monthP->bounds.topLeft.x + (item % daysInWeek) * cellWidth + 1;
	r.topLeft.y = monthP->bounds.topLeft.y + ((item / daysInWeek) + 1) * cellHeight + 1;
	r.extent.x = cellWidth - 1;
	r.extent.y = cellHeight - 1;

	// Draw the selected day.
	selected	= true;
	MonthSelectDayRect (monthP, &r, selected);

	// Track the pen until it's released, highlight or unhighlight the
	// item as the pen move in and out of it.
	do 
		{
		PenGetPoint (&x, &y, &penDown);

		if (RctPtInRectangle (x, y, &r))
			{
			if (!selected)
				{
				selected = true;
				MonthSelectDayRect (monthP, &r, selected);
				}
			}

		else if (selected)
			{
			selected = false;
			MonthSelectDayRect (monthP, &r, selected);
			}

		} while (penDown);


	// Exit if the pen was released outside the item.
	if (!selected)
		return (false);

	selected = false;
	MonthSelectDayRect (monthP, &r, selected);

	dateP->month = monthP->month;
	dateP->year = monthP->year - firstYear;

	// Is the selection is outside of the month?
	if ((unsigned) (item - firstDayInMonth) >= (unsigned) daysInMonth)
		{
		// Move to the month before or after the current month.
		dateP->day = 1;
		DateAdjust (dateP, (item - firstDayInMonth));
		}
	else
		{
		dateP->day = item - firstDayInMonth + 1;
		}

	return true;
	}



/***********************************************************************
 *
 * FUNCTION:    MonthViewShowTime
 *
 * DESCRIPTION: This routine display the current time in the title of the
 *              month view.
 *
 * PARAMETERS:  frm - pointer to the day view form.
 *
 * RETURNED:    nothing
 *
 * NOTE:        The global variable TimeDisplayed is set to true by this
 *              routine.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/15/96	Initial Revision
 *			jmp	10/6/99	Replace FrmGetActiveForm() with FrmGetFormPtr();
 *
 ***********************************************************************/
SEC003 static void MonthViewShowTime (void)
{
	Char				title[timeStringLength];
	DateTimeType 	dateTime;

	TimSecondsToDateTime (TimGetSeconds (), &dateTime);
	TimeToAscii (dateTime.hour, dateTime.minute, TimeFormat, title);
	FrmCopyTitle (FrmGetFormPtr (MonthView), title);
	
	TimeDisplayed = true;
	TimeDisplayTick = TimGetTicks () + timeDisplayTicks;
}


/***********************************************************************
 *
 * FUNCTION:		MonthViewGotoDate
 *
 * DESCRIPTION:	This routine displays the date picker so that the 
 *						user can select a month to navigate to.  If the date
 *						picker is confirmed, the month selected month is 
 *                confirmed
 *
 *						This routine is called when a "go to" button is pressed.
 *              
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	5/20/96	Initial Revision
 *			jmp	10/31/99 Move WinEraseRectangle() & DrawAppointmentsInMonth()
 *								into DrawMonth() itself.
 *
 ***********************************************************************/
SEC003 static void MonthViewGotoDate (void)
	{
	Char *		title;
	MemHandle	titleH;
	MonthPtr 	monthP;
	Int16 		day;

	// Get the title for the date picker dialog box.
	titleH = DmGetResource (strRsc, goToDateTitleStrID);
	title = MemHandleLock (titleH);

	monthP = MonthViewGetMonthPtr ();

	// Display the date picker.
	day = 1;
	if (DatePickSelectDay (selectDayByMonth, &monthP->month, &day, &monthP->year, title))
		{
		MonthViewSetTitle (monthP);
		DrawMonth (monthP, drawDaySelected);
		}

	MemHandleUnlock (titleH);
	}



/***********************************************************************
 *
 * FUNCTION:    MonthViewScroll
 *
 * DESCRIPTION: This routine scrolls the month view foreward or backwards
 *              by a month.
 *
 * PARAMETERS:  monthP  - pointer to month object (MonthType)
 *              direction - winUp (backwards) or winDown (foreward)
 *
 * RETURNED:	 nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	4/8/96	Initial Revision
 *			jmp	10/31/99 Move WinEraseRectangle() & DrawAppointmentsInMonth()
 *								into DrawMonth() itself.
 *
 ***********************************************************************/
SEC003 static void MonthViewScroll (MonthPtr monthP, WinDirectionType direction)
{
	// Move backward one month.
	if (direction == winUp)
		{
		if (monthP->month > january)
			monthP->month--;
		else if (monthP->year > firstYear)
			{
			monthP->month = december;
			monthP->year--;
			}
		else
			return;
		}
		
	// Move foreward one month.
	else
		{
		if (monthP->month < december)
			monthP->month++;
		else if (monthP->year < 9999)		/* Db35 era: was lastYear (2031) */
			{
			monthP->month = january;
			monthP->year++;
			}
		else
			return;
		}
	
	MonthViewSetTitle (monthP);
	DrawMonth (monthP, drawDaySelected);
}


/***********************************************************************
 *
 * FUNCTION:    MonthViewDoCommand
 *
 * DESCRIPTION: This routine preforms the menu command specified.
 *
 * PARAMETERS:  command  - menu item id
 *
 * RETURNED:    true if 
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/17/96	Initial Revision
 *			jmp	10/31/99 Move WinEraseRectangle() & DrawAppointmentsInMonth()
 *								into DrawMonth() itself.
 *
 ***********************************************************************/
SEC003 static Boolean MonthViewDoCommand (UInt16 command)
{
	MonthPtr monthP = MonthViewGetMonthPtr();
	
	// Preferences
	if (command == MonthPreferencesCmd)
		FrmPopupForm (PreferencesDialog);

	// Display Options
	else if (command == MonthDisplayOptionsCmd)
		FrmPopupForm (DisplayDialog);

	// About
	else if (command == MonthSecurityCmd)
		{
		DoSecurity();
		DrawMonth (monthP, drawDaySelected);
		}
		
	// About
	else if (command == MonthGetInfoCmd)
		ShowAboutDb35 ();

	return (true);
}


/***********************************************************************
 *
 * FUNCTION:    MonthViewInit
 *
 * DESCRIPTION: This routine initialize the month view.
 *
 * PARAMETERS:  frm - pointer of the Month View form
 *
 * RETURNED:	 nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	4/8/96	Initial Revision
 *
 ***********************************************************************/
SEC003 static void MonthViewInit (FormPtr frm)
{
	UInt16 objIndex;
	Int16 width;
	MonthPtr monthP;

	// Create a month object, initialize it, and store a pointer to it in the 
	// month gadget of the Month View.
	monthP = MemPtrNew (sizeof (MonthType));
	if (monthP)
		{
		objIndex = FrmGetObjectIndex (frm, MonthGadget);

		FrmGetObjectBounds (frm, objIndex, &monthP->bounds);
		width = monthP->bounds.extent.x;
		monthP->bounds.extent.x = (width / daysInWeek * daysInWeek) + 1;
		monthP->bounds.topLeft.x = (width - monthP->bounds.extent.x) / 2;

		monthP->month = Date.month;
		monthP->year = Date.year + firstYear;

		FrmSetGadgetData (frm, objIndex, monthP);

		MonthViewSetTitle (monthP);

		// Highlight the Month View push button.
		FrmSetControlGroupSelection (frm, DayViewGroup, MonthMonthViewButton);
		}

	TimeDisplayed = false;
}


/***********************************************************************
 *
 * FUNCTION:    MonthViewHandleEvent
 *
 * DESCRIPTION: This routine is the event handler for the Month View
 *              of the Datebook application.
 *
 * PARAMETERS:  event  - a pointer to an EventType structure
 *
 * RETURNED:    true if the event was handled and should not be passed
 *              to a higher level handler.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	4/8/96	Initial Revision
 *			CSS	06/22/99	Standardized keyDownEvent handling
 *								(TxtCharIsHardKey, commandKeyMask, etc.)
 *			rbb	6/2/99	Added button for Agenda view
 *			jmp	10/6/99	Fix bug #22497:  Perform a FrmDrawForm() before
 *								updating the rest of the form on frmUpdateEvents;
 *								also, replace FrmGetActiveForm() with FrmGetFormPtr()
 *								as that is more correct in these modern times of
 *								generating update events when bits fail to be allocated.
 *			jmp	10/31/99 Move WinEraseRectangle() & DrawAppointmentsInMonth()
 *								into DrawMonth() itself.
 *
 ***********************************************************************/
 SEC003 Boolean MonthViewHandleEvent (EventType* event)
{
	UInt16 daysInMonth;
	FormPtr frm;
	DateType date;
	Boolean handled = false;
	MonthPtr monthP;

	if (event->eType == penDownEvent)
		{
		monthP = MonthViewGetMonthPtr ();
		if (MonthSelectDay (monthP, event, &date))
			{
			Date = date;
			FrmGotoForm (DayView);
			handled = true;
			}
		}

	
	else if (event->eType == keyDownEvent)
		{
		MonthViewHideTime (true);

		if (TxtCharIsHardKey(event->data.keyDown.modifiers, event->data.keyDown.chr))
			{
			DateSecondsToDate (TimGetSeconds (), &date);
			Date = date;
			if (EventInCurrentView ||
				(event->data.keyDown.modifiers & poweredOnKeyMask))
				FrmGotoForm (DayView);
			else
				FrmGotoForm (AgendaView);				
			handled = true;
			EventInCurrentView = true;
			}
		else if (EvtKeydownIsVirtual(event))
			{
			if (event->data.keyDown.chr == vchrPageUp)
				{
				monthP = MonthViewGetMonthPtr ();
				MonthViewScroll (monthP, winUp);
				handled = true;
				}	
			else if (event->data.keyDown.chr == vchrPageDown)
				{
				monthP = MonthViewGetMonthPtr ();
				MonthViewScroll (monthP, winDown);
				handled = true;
				}
			
			EventInCurrentView = true;
			}
		}

	
	else if (event->eType == ctlSelectEvent)
		{
		switch (event->data.ctlSelect.controlID)
			{
			case MonthDayViewButton:
			case MonthWeekViewButton:
			case MonthAgendaViewButton:
				monthP = MonthViewGetMonthPtr ();
				Date.year = monthP->year - firstYear;
				Date.month = monthP->month;
				daysInMonth = DaysInMonth (Date.month, Date.year + firstYear);
				Date.day = min (Date.day, daysInMonth);
				break;
			}
		switch (event->data.ctlSelect.controlID)
			{
			case MonthGoToButton:
				MonthViewGotoDate ();
				handled = true;
				break;

			case MonthDayViewButton:
				FrmGotoForm (DayView);
				handled = true;
				break;

			case MonthWeekViewButton:
				FrmGotoForm (WeekView);
				handled = true;
				break;

			case MonthAgendaViewButton:
				FrmGotoForm (AgendaView);
				handled = true;
				break;
			}
		}


	else if (event->eType == ctlRepeatEvent)
		{
		switch (event->data.ctlRepeat.controlID)
			{
			case MonthPrevButton:
				monthP = MonthViewGetMonthPtr ();
				MonthViewScroll (monthP, winUp);
				break;
			
			case MonthNextButton:
				monthP = MonthViewGetMonthPtr ();
				MonthViewScroll (monthP, winDown);
				break;
			}
		}


	else if (event->eType == menuEvent)
		{
		handled = MonthViewDoCommand (event->data.menu.itemID);
		}
		

	else if (event->eType == frmOpenEvent)
		{
		frm = FrmGetFormPtr (MonthView);
		MonthViewInit (frm);
		FrmDrawForm (frm);

		monthP = MonthViewGetMonthPtr ();
		DrawMonth (monthP, drawDaySelected);
		EventInCurrentView = false;
		handled = true;
		}


	else if (event->eType == frmUpdateEvent)
		{
		frm = FrmGetFormPtr (MonthView);
		FrmDrawForm (frm);
		
		monthP = MonthViewGetMonthPtr ();
		DrawMonth (monthP, drawDaySelected);
		handled = true;		
		}

	else if (event->eType == frmCloseEvent)
		{
		monthP = MonthViewGetMonthPtr ();
		if (monthP)
			MemPtrFree (monthP);
		}


	else if (event->eType == frmTitleEnterEvent)
		{
		Boolean penDown;
		Coord x, y;
		
		//Wait 1/20 sec before displaying time, to avoid flicker before menu pops up
		SysTaskDelay (timeDisplayWaitTicks);
		PenGetPoint (&x, &y, &penDown);
		if (penDown)
			{
			// If the pen is in the title toggle between the current date
			// and the current time.
			if (TimeDisplayed)
				MonthViewHideTime (true);
			else
				MonthViewShowTime ();
			}
		}


	else if (event->eType == nilEvent)
		{
		MonthViewHideTime (false);
		}

	return (handled);
}

