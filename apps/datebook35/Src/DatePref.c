/******************************************************************************
 *
 * Copyright (c) 1995-1999 Palm Computing, Inc. or its subsidiaries.
 * All rights reserved.
 *
 * File: DatePref.c
 *
 * Description:
 *	  This module contains the routines that handle the Datebook
 *   applications's preferences.
 *
 *   Db35: alarm support has been removed (alarms cannot work under era
 *   re-basing -- they schedule on absolute UInt32 seconds which hit the 2040
 *   wall). The Preferences dialog therefore now only holds the day Start Time
 *   and End Time settings; all Alarm Preset and Alarm Sound (MIDI, Remind Me,
 *   Play Every) UI and code has been deleted.
 *
 *****************************************************************************/

#include <PalmOS.h>
#include "Datebook.h"


/***********************************************************************
 *
 *	Global variables (only valid while editing the datebook's preferences)
 *
 **********************************************************************/
static UInt16	PrefDayStartHour;
static UInt16	PrefDayEndHour;


/***********************************************************************
 *
 * FUNCTION:    PreferencesUpdateScrollers
 *
 * DESCRIPTION: Enable/disable the start- and end-time up/down repeat buttons.
 *
 ***********************************************************************/
SEC002 static void PreferencesUpdateScrollers (void)
{
	FormPtr frm;
	UInt16 upIndex;
	UInt16 downIndex;

	// Update the start time scrollers.
	frm = FrmGetActiveForm ();
	upIndex = FrmGetObjectIndex (frm, PreferStartUpButton);
	downIndex = FrmGetObjectIndex (frm, PreferStartDownButton);
	FrmUpdateScrollers (frm, upIndex, downIndex,
		PrefDayStartHour<23, PrefDayStartHour>0);

	// Update the end time scrollers.
	upIndex = FrmGetObjectIndex (frm, PreferEndUpButton);
	downIndex = FrmGetObjectIndex (frm, PreferEndDownButton);
	FrmUpdateScrollers (frm, upIndex, downIndex,
		PrefDayEndHour<23, PrefDayEndHour>0);
}


/***********************************************************************
 *
 * FUNCTION:    PreferencesDrawTime
 *
 * DESCRIPTION: This routine draw the time passed at the location specified.
 *
 * PARAMETERS:  hour        - hour to draw, minutes is assumed to be zero
 *              isStartTime - true if we're drawing the start time
 *
 ***********************************************************************/
SEC002 static void PreferencesDrawTime (UInt8 hour, Boolean isStartTime)
{
	Char str[timeStringLength];
	UInt16 id;
	UInt16 len;
	UInt16 index;
	Int16 x, y;
	FontID curFont;
	FormPtr frm;
	RectangleType r;

	// Compute the drawing bounds.
	if (isStartTime)
		id = PreferStartUpButton;
	else
		id = PreferEndUpButton;

	frm = FrmGetActiveForm ();
	index = FrmGetObjectIndex (frm, id);
	FrmGetObjectPosition (frm, index, &x, &y);

	r.topLeft.x = x - dayRangeTimeWidth - 5;
	r.topLeft.y = y + 2;
	r.extent.x = dayRangeTimeWidth;
	r.extent.y = dayRangeTimeHeight;

	// Format the time into a string.
	TimeToAscii (hour, 0, TimeFormat, str);
	len = StrLen (str);

	// Draw a frame around the time.
	WinDrawRectangleFrame (simpleFrame, &r);

	WinEraseRectangle (&r, 0);

	// Draw the time.
	curFont = FntSetFont (boldFont);
	x = r.topLeft.x + (r.extent.x - FntCharsWidth (str, len)) - 3;
	y = r.topLeft.y + ((r.extent.y - FntLineHeight ()) / 2);
	WinDrawChars (str, len, x, y);
	FntSetFont (curFont);
}


/***********************************************************************
 *
 * FUNCTION:    PreferencesApply
 *
 * DESCRIPTION: This routine applies the changes made in the Preferences Dialog.
 *
 ***********************************************************************/
SEC002 static UInt16 PreferencesApply (UInt16 dayStartHour, UInt16 dayEndHour)
{
	UInt16 updateCode = 0;

	if ((dayStartHour != DayStartHour) || (dayEndHour != DayEndHour))
		{
		DayStartHour = dayStartHour;
		DayEndHour = dayEndHour;
		updateCode = updateDisplayOptsChanged;
		}

	// Db35: alarm preset / sound settings removed.
	DatebookSavePrefs ();

	return (updateCode);
}


/***********************************************************************
 *
 * FUNCTION:    PreferencesHandleEvent
 *
 * DESCRIPTION: This routine handles events in the Preferences dialog.
 *
 ***********************************************************************/
SEC002 Boolean PreferencesHandleEvent (EventType * event)
{
	UInt16 updateCode;
	FormPtr frm;
	Boolean handled = false;

	if (event->eType == ctlSelectEvent)
		{
		switch (event->data.ctlSelect.controlID)
			{
			case PreferOkButton:
				updateCode = PreferencesApply (PrefDayStartHour, PrefDayEndHour);
				FrmReturnToForm (0);
				if (updateCode)
					FrmUpdateForm (FrmGetFormId (FrmGetActiveForm()), updateCode);
				handled = true;
				break;

			case PreferCancelButton:
				FrmReturnToForm (0);
				handled = true;
				break;
			}
		}

	else if (event->eType == ctlRepeatEvent)
		{
		switch (event->data.ctlRepeat.controlID)
			{
			case PreferStartDownButton:
				PrefDayStartHour--;
				PreferencesDrawTime (PrefDayStartHour, true);
				break;

			case PreferStartUpButton:
				PrefDayStartHour++;
				PreferencesDrawTime (PrefDayStartHour, true);
				if (PrefDayEndHour < PrefDayStartHour)
					{
					PrefDayEndHour = PrefDayStartHour;
					PreferencesDrawTime (PrefDayEndHour, false);
					}
				break;

			case PreferEndDownButton:
				PrefDayEndHour--;
				if (PrefDayEndHour < PrefDayStartHour)
					{
					PrefDayStartHour = PrefDayEndHour;
					PreferencesDrawTime (PrefDayStartHour, true);
					}
				PreferencesDrawTime (PrefDayEndHour, false);
				break;

			case PreferEndUpButton:
				PrefDayEndHour++;
				PreferencesDrawTime (PrefDayEndHour, false);
				break;
			}
		PreferencesUpdateScrollers ();
		}

	else if (event->eType == frmOpenEvent)
		{
		PrefDayStartHour = DayStartHour;
		PrefDayEndHour = DayEndHour;

		frm = FrmGetActiveForm ();
		FrmDrawForm (frm);
		PreferencesUpdateScrollers ();
		PreferencesDrawTime (DayStartHour, true);
		PreferencesDrawTime (DayEndHour, false);

		handled = true;
		}

	else if (event->eType == frmUpdateEvent)
		{
		frm = FrmGetActiveForm ();
		FrmDrawForm (frm);
		PreferencesDrawTime (DayStartHour, true);
		PreferencesDrawTime (DayEndHour, false);
		handled = true;
		}

	return (handled);
}
