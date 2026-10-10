/******************************************************************************
 *
 * Copyright (c) 1998-1999 Palm Computing, Inc. or its subsidiaries.
 * All rights reserved.
 *
 * File: DatebookRsc.h
 *
 *****************************************************************************/

// Day View
#define DayView								1000
#define DayTitle								1001
#define DayPrevWeekButton					1002
#define DayDOW1Button						1003		// first day of week push button
#define DayDOW2Button						1004		// second day of week push button
#define DayDOW3Button						1005
#define DayDOW4Button						1006
#define DayDOW5Button						1007
#define DayDOW6Button						1008
#define DayDOW7Button						1009
#define DayNextWeekButton					1010
#define DayDayViewButton					1011
#define DayWeekViewButton					1012
#define DayMonthViewButton					1013
#define DayAgendaViewButton				1014
#define DayNewButton							1015
#define DayDetailsButton					1016
#define DayGoToButton						1017
#define DayUpButton							1018
#define DayDownButton						1019
#define DayTable								1020
#define DayOfWeekGroup						1
#define DayViewGroup							2


// Week View
#define WeekView								1100
#define WeekPrevButton						1102
#define WeekNextButton						1103
#define WeekNumberLabel						1104
#define WeekDayViewButton					1105
#define WeekWeekViewButton					1106
#define WeekMonthViewButton				1107
#define WeekAgendaViewButton				1108
#define WeekGoToButton						1109
#define WeekUpButton							1110
#define WeekDownButton						1111
#define WeekDayDisplay						1112


// Month View 
#define MonthView								1200
#define MonthPrevButton						1202
#define MonthNextButton						1203
#define MonthDayViewButton					1204
#define MonthWeekViewButton				1205
#define MonthMonthViewButton				1206
#define MonthAgendaViewButton				1207
#define MonthGoToButton						1208
#define MonthGadget							1209


// Details Dialog Box
#define DetailsDialog						1300
// Db35: "Start:"/"End:" rows -- replaced the old separate "Time:" (combined
// ROM SelectTime start+end trigger) and "Date:" (start-date-only trigger)
// rows, same two row-slots/IDs, so an event can span multiple days. Each
// shows a single combined "7:30pm Fri 11/12/2035"-style label; tapping
// opens DateSpan.c's shared Date+Time sub-dialog. See DateSpan.h.
#define DetailsStartSelector				1303
#define DetailsEndSelector					1305
#define DetailsAlarmCheckbox				1307
#define DetailsAlarmAdvanceField			1308
#define DetailsAlarmAdvanceSelector		1310
#define DetailsAlarmAdvanceList			1311
#define DetailsRepeatSelector				1313
#define DetailsPrivateCheckbox			1315
#define DetailsOkButton						1316
#define DetailsCancelButton				1317
#define DetailsDeleteButton				1318
#define DetailsNoteButton					1319
// Db35: Location field -- a real EDITABLE MULTIPLELINES FIELD (name kept
// as "...Gadget" from an earlier GADGET-based attempt that didn't receive
// taps reliably; functionally a field now). Same "tap intercepts
// fldEnterEvent to launch a sub-dialog instead of editing inline" idiom
// Address35's EditForm.c uses for its ftAddress/ftEmail/etc rows. 2-line
// preview (or a "Tap to set location" placeholder when blank); tap opens
// LocationDialog (DateLocation.c's LocFieldTapped, via DetailsHandleEvent).
#define DetailsLocationLabel				1320
#define DetailsLocationGadget				1321
// Db35: Title field -- the FIRST row of the dialog, above Time. Editing
// happens here now instead of inline in the Day View table row (see
// DayViewHandleEvent's tblEnterEvent handler); a plain EDITABLE FIELD, no
// tap-interception needed since Title edits directly in place (unlike
// Location, which launches a sub-dialog).
#define DetailsTitleLabel					1322
#define DetailsTitleField					1323

// Db35: "Set Date & Time" sub-dialog (DateSpan.c) -- shared by the Start:
// and End: triggers above. "Date:" opens the era day picker
// (DatePickSelectDay); "Time:" opens the ROM's single-moment picker
// (SelectOneTime, SelTime.h) -- no start/end pairing, unlike the old ROM
// SelectTime this replaces. "No Time" only meaningful/shown for Start (a
// whole-appointment property); End's Time row is hidden when the
// appointment is currently untimed.
#define DateSpanDialog						1360
#define DateSpanDateSelector				1361
#define DateSpanTimeSelector				1362
#define DateSpanNoTimeCheckbox			1363
#define DateSpanOkButton					1364
#define DateSpanCancelButton				1365
// "End date must be..." rejection when the End picker is pushed out of
// [start date, start date + 30] -- re-prompts (DateSpan.c re-loops back to
// the picker), same INFORMATION-ALERT idiom used elsewhere in this app.
#define altSpanOutOfRange					1370
// Db35: tapping an agenda row from CalDAV Sync's DateBook35DAV.pdb shows
// its own full text (read-only, no separate app view to switch to). Used
// by Agenda/Week only - Day view's own tap opens the REAL DetailsDialog
// below instead, populated from CalDAV data (see DetailsInitFromDav).
#define altDavInfo							1371
// Db35: shown when the user actually TRIES to edit/delete/duplicate a
// CalDAV item from within the real Event Details dialog (typing into
// Title/Location, tapping Start/End/Repeat/Delete, or Duplicate/Beam from
// the Record menu) - never on a plain view-only tap (opening Details,
// viewing the Note, tapping OK/Cancel are all silent, no alert).
#define altDavReadOnly						1372


// Repeat Dialog Box.
// Note: the code is depends on the current order of the ui object.
#define RepeatDialog							1400
#define RepeatNone							1402
#define RepeatDaily							1403
#define RepeatWeekly							1404
#define RepeatMonthly						1405
#define RepeatYearly							1406
#define RepeatEveryLabel					1407
#define RepeatFrequenceField				1408
#define RepeatDaysLabel						1409
#define RepeatWeeksLabel					1410
#define RepeatMonthsLabel					1411
#define RepeatYearsLabel					1412
#define RepeatEndOnLabel					1413
#define RepeatEndOnTrigger					1415
#define RepeatEndOnList						1416
#define RepeatRepeatOnLabel				1417
#define RepeatDayOfWeek1PushButton		1418
#define RepeatDayOfWeek2PushButton		1419
#define RepeatDayOfWeek3PushButton		1420
#define RepeatDayOfWeek4PushButton		1421
#define RepeatDayOfWeek5PushButton		1422
#define RepeatDayOfWeek6PushButton		1423
#define RepeatDayOfWeek7PushButton		1424
#define RepeatByLabel						1425
#define RepeatByDayPushButon				1426
#define RepeatByDatePushButon				1427
#define RepeatDescField						1428
#define RepeatNoRepeatLabel				1429
#define RepeatOkButton						1430
#define RepeatCancelButton					1431
#define RepeatDescRectGadget				1440
#define RepeatTypeGroup						1
#define RepeatByGroup						2

// Purge Dialog
#define PurgeDialog							1500
#define PurgeRangeList						1506
#define PurgeSaveBackup						1507
#define PurgeOk								1508
#define PurgeCancel							1509


// Delete Appt Dialog
#define DeleteApptDialog					1600
#define DeleteApptSaveBackup				1604
#define DeleteApptOk							1606
#define DeleteApptCancel					1607


// Monthly Repeat Dialog
#define MonthlyRepeatDialog				1700
#define MonthlyRepeatFourthButton		1704
#define MonthlyRepeatLastButton			1705
#define MonthlyRepeatOk						1706
#define MonthlyRepeatCancel				1707
#define MonthlyRepeatWeekGroup			1


// Preference Dialog
#define PreferencesDialog					1800
#define PreferStartUpButton				1803
#define PreferStartDownButton				1804
#define PreferEndUpButton					1806
#define PreferEndDownButton				1807
#define PreferAlarmCheckbox				1809
#define PreferAlarmField					1810
#define PreferAlarmUnitTrigger			1812
#define PreferAlarmList						1813
#define PreferOkButton						1814
#define PreferCancelButton					1815
#define PreferAlarmSoundTrigger			1817
#define PreferAlarmSoundList				1818
#define PreferRemindMeTrigger				1819
#define PreferRemindMeList					1820
#define PreferPlayEveryTrigger			1821
#define PreferPlayEveryList				1822


// Display Options Dialog
#define DisplayDialog						1900
#define DisplayShowTimeBarsCheckbox		1903
#define DisplayCompressDayViewCheckbox	1904
#define DisplayShowTimedCheckbox			1906
#define DisplayShowUntimedCheckbox		1907
#define DisplayShowRepeatingCheckbox	1908
#define DisplayOkButton						1909
#define DisplayCancelButton				1910


// Alarm Dialog
#define AlarmDialog							2000
#define AlarmSnoozeButton					2002
#define AlarmOKButton						2003
#define AlarmDescGadget						2004


// Exception Dialog
#define RangeDialog							2100
#define RangeCurrentButton					2103
#define RangeFutureButton					2104
#define RangeAllButton						2105
#define RangeCancelButton					2106


// Agenda View
#define AgendaView							2200
#define AgendaDivider						2202
#define AgendaDayViewButton				2203
#define AgendaWeekViewButton				2204
#define AgendaMonthViewButton				2205
#define AgendaAgendaViewButton			2206
#define AgendaGoToButton					2207
#define AgendaToDoCategoryTrigger		2208
#define AgendaToDoCategoryList			2209
#define AgendaDatebookTable				2210
#define AgendaSeparatorDefaultArea		2212
#define AgendaToDoTable						2213
#define AgendaScrollableArea				2214
#define AgendaPreviousDayButton			2215
#define AgendaNextDayButton				2216
#define AgendaCurrentDayButton			2217
#define AgendaDatebookScroller			2218
#define AgendaToDoScroller					2219
#define AgendaViewGroup						1			// Collection of view-switching buttons


// Delete Note Alert
#define DeleteNoteAlert						2001
#define DeleteNoteYes						0
#define DeleteNoteNo             		1

// Select An Item Alert
#define SelectItemAlert						2002

// Repeat type changed Alert
#define RepeatTypeChangedAlert			2006
#define RepeatTypeChangedOkButton		0
#define RepeatTypeChangedCancelButton	1

// Alarm time text contains invalid chars Alert
#define AlarmInvalidAlert					2007

// Db35: "Duplicate Event" repeat-scope prompt (custom FORM, not ALERT -- see
// ToDo35's identical DuplicateRepeatDlg precedent: an ALERT auto-sizes its
// buttons from label text with no width override).
#define DuplicateRepeatDlg					2300
#define btnDupThisOnly						2301
#define btnDupAll								2302
#define btnDupCancel							2303
#define DuplicateThisOccurrence			0
#define DuplicateAllOccurrences			1
#define DuplicateCancel						2

// Db35: Location edit dialog (modeled on Address35's frmAddrDlg -- see
// EditDialogs.c:497-583 -- with Line1/Line2 shrunk to single-line, a Name
// field added, and two picker buttons -- "Address35" / "Previous Loc" --
// above Name). Hand-authored FORM, no Constructor original (location35.rcp).
#define LocationDialog						2400
#define btnLocAddress35						2401
#define btnLocPreviousLoc					2402
#define fldLocName							2403
#define fldLocLine1							2404
#define fldLocLine2							2405
#define fldLocApt								2406
#define fldLocCity							2407
#define fldLocState							2408
#define fldLocZip								2409
// Db35: same UX as Address35's own Country field -- a POPUPTRIGGER that
// launches a full-screen searchable picker (kLocCountries[] in
// DateLocation.c is a synced copy of Address35's kCountries[], same idiom
// as AddrBday.c's kAnnivType[] copy). Last-picked country is remembered
// across events via an UNSAVED app-pref (datebookUnsavedPrefID), mirroring
// Address35's own A35GetLastCountry/A35SetLastCountry.
#define popLocCountry						2410
#define btnLocOk								2412
#define btnLocCancel							2413
#define btnLocClear							2414

// Db35: generic full-screen searchable list picker (list + bottom Find
// bar, no Delete) -- reused for BOTH the "Address35" contact picker
// (modeled on Address35's frmContactPick, EditDialogs.c:1336-1443) AND the
// Country picker (modeled on Address35's frmPick/A35PickList,
// EditDialogs.c:124-212), since the two are structurally identical; the
// title is set at runtime (FrmSetTitle) per use, not fixed in the resource.
#define LocAddrPickDialog					2420
#define lstLocAddrPick						2421
#define fldLocAddrPickFind					2422
#define btnLocAddrPickCancel				2424

// Db35: full-screen "Previous Loc" picker (LocationsDB history) -- same
// list+Find-bar shell as LocAddrPickDialog, plus a NEW inline Delete
// button (no existing precedent for delete-inside-a-picker in this
// codebase; genuinely new UI).
#define LocHistoryPickDialog				2430
#define lstLocHistoryPick					2431
#define fldLocHistoryFind					2432
#define btnLocHistoryCancel				2434
#define btnLocHistoryDelete				2435

// Db35: minimal Edit-only MENU (Undo/Cut/Copy/Paste/Select All -- same
// items+IDs as the Day View's own Edit pulldown, MENU 1000) attached via
// MENUID to all 3 new Location forms, since they're the first Datebook35
// dialogs with real text-entry fields outside the main views.
#define mnuLocEdit							2440

// Db35: "Delete this location?" confirm alert for the Previous Loc
// picker's Del button (matches Address35's altSetMe-style CONFIRMATION
// ALERT, 0=OK/Delete 1=Cancel).
#define DeleteLocationAlert				2480
#define DeleteLocationOk						0
#define DeleteLocationCancel				1

// Bitmaps
#define AlarmClockBitmap					1000

// Menus
#define DayViewMenu							1000
#define NoteViewMenu							1001
#define WeekViewMenu							1002

// Menu commands
#define NewItemCmd							100
#define DeleteCmd								101
#define CreateNoteCmd						102
#define DeleteNoteCmd						103
#define PurgeCmd								104
#define SendRecordCmd						105
#define DuplicateCmd							106
#define UndoCmd								200
#define CutCmd									201
#define CopyCmd								202
#define PasteCmd								203
#define KeyboardCmd							205

#define DayFontsCmd							300
#define DayPreferencesCmd					301
#define DayDisplayOptionsCmd				302
#define DayPhoneLookup						303
#define DayCalDavSyncCmd					306
#define DaySecurityCmd						304
#define DayGetInfoCmd						305

#define WeekPreferencesCmd					300
#define WeekSecurityCmd						301
#define WeekGetInfoCmd						302

#define MonthPreferencesCmd				300
#define MonthDisplayOptionsCmd			301
#define MonthSecurityCmd					302
#define MonthGetInfoCmd						303


// Strings
#define findDatebookHeaderStrID			100	// Find results section header
#define goToDateTitleStrID					101	// Date picker title
#define setTimeTitleStrID					102	// Time picker title
#define startDateTitleStrID				103	// Date picker title for selecting appt's date
#define endDateTitleStrID					104	// Date picker title for selecting repeat end date
#define daysOfWeekInitialsStrID			105	// Initials of the days of the week names	
#define noTimeStrID							126   // No time
#define repeatTypesStrID					127	// Names of repeat types.
#define dailyRepeatStrID					128	// description template
#define weeklyRepeat1DayStrID				129	// repeat description template
#define weeklyRepeat2DayStrID				130	// repeat description template
#define weeklyRepeat3DayStrID				131	// repeat description template
#define weeklyRepeat4DayStrID				132	// repeat description template
#define weeklyRepeat5DayStrID				133	// repeat description template
#define weeklyRepeat6DayStrID				134	// repeat description template
#define weeklyRepeat7DayStrID				135	// repeat description template
#define monthtlyByDayRepeatStrID			136	// repeat description template
#define monthtlyByDateRepeatStrID		137	// repeat description template
#define yearlyRepeatStrID					138	// repeat description template
#define freqOrdinalsStrID					139	// 1st, 2nd, 3rd, ..., 99st
#define weekOrdinalsStrID					140	// 1st, 2nd, 3rd, 4th, and last
#define dayOrdinalsStrID					141	// 1st - 31st
#define repeatNoneStrID						142	// "No Repeat"
#define everyDayRepeatStrID				143	// "Every day"
#define everyWeekRepeat1DayStrID			144	// "Every week on <DOW name>"
#define everyWeekRepeat2DayStrID			145	// "Every week on <DOW name> and <DOW name>"
#define everyWeekRepeat3DayStrID			146	//
#define everyWeekRepeat4DayStrID			147	//
#define everyWeekRepeat5DayStrID			148	//
#define everyWeekRepeat6DayStrID			149	//
#define everyWeekRepeat7DayStrID			150	//
#define everyMonthByDayRepeatStrID		151	// "The ^w ^d of every month"
#define everyMonthByDateRepeatStrID		152	// "The ^x of every month"
#define everyYearRepeatStrID				153	// "^m ^x every year"
#define repeatMonthNamesStrID				154	// "January February..."
#define repeatFullDOWNamesStrID			155	// "Sunday Monday..."
#define repeatShortDOWNamesStrID			156	// "Sun Mon..."

#define beamDescriptionStrID				1000
#define beamFilenameStrID					1001
// Db35: relocated 1002-1008 -> 6002-6008. At the ROM IDs 1002-1008 these day-view
// date-format templates ("^4s ^2r ^0r" ...) were being surfaced by the device's
// HotSync app-name display and prepended to the tAIN ("^4s ^2r ^0rDateBk35"). The
// app loads them by ID and passes the template to EraDateTemplateToAscii, so moving
// them to an app-private range keeps date display intact while HotSync no longer
// finds a date-format string at the IDs it reads.
#define DayViewFirstTitleTemplateStrID	6002	// dfMDYLongWithComma
#define DayViewLastTitleTemplateStrID	6008	// dfYMDLongWithSpace
	
#define weekNumberTemplateStrID			1100	// Week ^1
#define WeekViewTitleFullDateStrID		1101	// "Mmm 'YY"
#define WeekViewTitleShortDateStrID		1102	// "Mmm"
#define WeekViewTitleEmptyDateStrID		1103	// ""
#define WeekViewTitleTwoDatesStrID		1104	// "^0 - ^1"
#define WeekViewTitleOneDateStrID		1105	// "^0"

#define MonthViewTitleStrID				1200	// "Sep 1999"

#define alarmTitleStrID						2000	// "Reminder"
#define drawAlarmDateTemplateStrID		2001	// "^0, ^1" -> "Wednesday, 08/11/99"
#define agendaDateFormatStrID				2200
#define agendaNoAppointmentsStrID		2201

#define defaultAlarmSoundNameID			1800	// must be equal to alarm name in General Panel's MidiSounds.rsrc

// String lists
#define freqOrdinal2ndStrlID				100	// 2nd (other)

// Soft constants

#define weekViewYearFirst					1100	// True => display year first ('YY MMM - MMM)
#define soundTriggerLabelWidth			1800	// Pixel width for sound trigger label.

