/******************************************************************************
 *
 * Copyright (c) 1999 Palm Computing, Inc. or its subsidiaries.
 * All rights reserved.
 *
 * File: DateAgenda.c
 *
 * Description:
 *	  Display and support code for Datebook's Agenda view.
 *
 * History:
 *		May 28, 1999	Created by Russell Brenner
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	5/28/99	Initial Revision
 *
 *****************************************************************************/

#include <PalmOS.h>
#include <FntGlue.h>
#include <WinGlue.h>	/* WinGlueDrawTruncChars: "..." ellipsis for long categories */

#include "Datebook.h"
#include "DateTime.h"
#include "ToDoDB.h"
#include "datepick.h"  /* Db35: era-aware date picker (agenda Go to Date) */
#include "era.h"        /* Db35: era-aware date formatting for To Do due dates */
#include "AddrBday.h"   /* Db35: Address35 birthdays/anniversaries in the agenda */
#include "DateDAV.h"    /* Db35: CalDAV Sync's synced calendar events in the agenda */
#include "enccore.h"    /* Db35: decrypt encrypted ToDo35 descriptions for the agenda */

extern ECApptDBValidate (DmOpenRef dbP);

/* Deliberately UNTAGGED (main segment) relay hop - EncGetKey lives in
 * enccore.c, ALSO tagged sec003 (ENCCORE_SECTION), so calling it directly
 * from this file (a DIFFERENT sec003 file) hit the documented same-
 * segment-across-files gotcha at LINK time: "relocation truncated to
 * fit: DISP16 EncGetKey" - a direct bsr assuming a safe same-segment
 * branch, but this cross-compiler's short-branch resolution for that
 * shape is unreliable once sec003 grows past a certain point (tipped
 * over by DateMonth.c's own new CalDAV Sync code growing sec003's total
 * size, even though this call site itself is untouched). Routing through
 * an untagged hop turns it into two ordinary cross-segment indirect
 * calls instead of one unsafe same-segment direct one - same fix as
 * DateDay.c's own DayBuildLocSuffixHop. */
static Boolean EncGetKeyHop (UInt8 *keyOut, UInt32 ttlSeconds)
{
	return EncGetKey (keyOut, ttlSeconds);
}

/* Same fix, same reason, hit by the SAME sec003 growth (this time from
 * DateDAV.c's new DateDAVGetOne + the new DateDavDetails.c, both sec003 -
 * see DateDavDetails.c) - EncCrypt is defined in enccore.c, also sec003. */
static void EncCryptHop (const UInt8 *key, UInt8 *buf, UInt32 len, UInt32 nonceHi, UInt32 nonceLo)
{
	EncCrypt (key, buf, len, nonceHi, nonceLo);
}


/***********************************************************************
 *
 *	Internal Constants
 *
 ***********************************************************************/

#define minDatebookRows					1
#define maxDatebookRows					6
#define spaceAroundSeparator			2
#define scrollerInset					1

#define minToDoRows						0

#define adjustSeparatorTop				2
#define adjustCategoryTop				2
#define adjustCategoryBottom			0

//��� Copied from ToDo.c. Remove when obviated by ToDoLib

#define toDoPrefsVersionNum			3
#define todoPrefID						0x00
#define toDoDBName					"ToDo35DB"
#define toDo35Creator					'Td35'
#define toDoDBType						'DATA'
#define ToDo35NotInstalledAlert			8902	// Db35: shown when ToDo35 is absent

// Db35: the agenda's ToDo section is served by ToDo35 (the era-aware ToDo app).
// True iff the ToDo35 application is installed.
static Boolean ToDo35Installed (void)
{
	DmSearchStateType	st;
	UInt16				card;
	LocalID				dbID = 0;
	return (DmGetNextDatabaseByTypeCreator (true, &st, sysFileTApplication,
				toDo35Creator, true, &card, &dbID) == errNone && dbID != 0);
}

// Db35: the ToDo category trigger is right-aligned (arrow near the screen edge,
// label growing LEFT), so long category names (Business, Personal) don't clip
// off the right. CategorySetTriggerLabel keeps the left x and grows the width,
// so re-anchor the right edge after the label is set.
static void AgendaRightAlignCategory (FormType *frm)
{
	UInt16			idx;
	RectangleType	r;
	if (! frm) return;
	idx = FrmGetObjectIndex (frm, AgendaToDoCategoryTrigger);
	FrmGetObjectBounds (frm, idx, &r);
	r.topLeft.x = 156 - r.extent.x;
	if (r.topLeft.x < 2) r.topLeft.x = 2;
	FrmSetObjectBounds (frm, idx, &r);
}

// Fonts used by application
#define noteTitleFont					boldFont

// Columns in the ToDo table of the list view.
#define completedColumn					0
#define priorityColumn					1
#define descColumn						2
#define dueDateColumn					3
#define categoryColumn					4

// The agenda's To Do table needs 5 columns (completed/priority/desc/dueDate/
// category), but pilrc 3.2.90 can't emit >3-column tables so it starts with 3.
// Grow the live TableType to 5 columns in C at agenda init (swap columnAttrs +
// items for larger STATIC arrays, never freed), and restore on close so the form
// tears down as built. Without this, enabling ToDo "Show Due Dates/Categories"
// makes the agenda touch columns 3/4 -> invalid-column crash. Needs
// ALLOW_ACCESS_TO_INTERNALS_OF_TABLES (build.sh). Same idea as ToDo35's
// ListExpandColumns.
#define kAgToDoColumns					5
#define kAgToDoMaxRows					20

static TableColumnAttrType	sAgCols[kAgToDoColumns];
static TableItemType			sAgItems[kAgToDoColumns * kAgToDoMaxRows];
static TableColumnAttrType *	sAgSavedCols;
static TableItemType *		sAgSavedItems;
static Int16					sAgSavedNumCols;

SEC003 void AgendaExpandToDoColumns (TablePtr table)
{
	UInt16 old = table->numColumns;
	if (old >= kAgToDoColumns) return;
	if (table->numRows > kAgToDoMaxRows) return;

	sAgSavedCols    = table->columnAttrs;
	sAgSavedItems   = table->items;
	sAgSavedNumCols = old;

	MemSet (sAgCols, sizeof (sAgCols), 0);
	MemMove (sAgCols, table->columnAttrs, old * sizeof (TableColumnAttrType));
	MemSet (sAgItems, sizeof (sAgItems), 0);

	table->columnAttrs = sAgCols;
	table->items       = sAgItems;
	table->numColumns  = kAgToDoColumns;
}

SEC003 void AgendaRestoreToDoColumns (TablePtr table)
{
	if (table->columnAttrs != sAgCols) return;
	table->columnAttrs = sAgSavedCols;
	table->items       = sAgSavedItems;
	table->numColumns  = sAgSavedNumCols;
}

// Strip the (4-digit era) year from a formatted date, leaving month/day for the
// due-date column. Chopping a fixed 3 chars (assuming "/YY") left a stray digit
// on era dates; cut at the separator instead. (Mirrors ToDo35's helper.)
SEC003 Char * StripYearForColumn (Char * s, DateFormatType fmt)
{
	Char * p;

	if (fmt == dfYMDWithSlashes || fmt == dfYMDWithDots || fmt == dfYMDWithDashes)
		{
		for (p = s; *p && *p != '/' && *p != '.' && *p != '-'; p++) ;
		return (*p) ? p + 1 : s;
		}
	for (p = s + StrLen (s) - 1; p > s && *p != '/' && *p != '.' && *p != '-'; p--) ;
	if (p > s) *p = 0;
	return s;
}

#define spaceBeforeDesc					2
#define spaceBeforeCategory			2

#define newToDoSize  					16
#define defaultPriority					1

#define maxNoteTitleLen					40

// Field numbers, used to indicate where search string was found.
#define descSearchFieldNum				0
#define noteSearchFieldNum				1

#define noAppointments					0xffff
#define noTasks							0xffff

// Db35: birthdays/anniversaries from Address35 are appended to the day's
// appointment list as untimed rows. A row is a birthday when its recordNum is
// >= kBdaySentinel; (recordNum - kBdaySentinel) indexes gBdayLines. 0xF000 can't
// collide with real appt record numbers or the noAppointments (0xffff) marker.
#define kBdaySentinel					0xF000
#define kMaxBday						12
static char gBdayLines[kMaxBday][kAddrBdayLineLen];
static UInt16 gBdayIndex[kMaxBday];		// Address35 record index behind each line

// Db35: CalDAV Sync's synced calendar events, same scheme as birthdays above
// but a separate sentinel range (0xE000, well clear of kBdaySentinel's own
// 0xF000..0xF00B) and no "goto" target - a DAV row has no separate app view
// to switch to, so tapping one just shows its own full text (never truncated
// display text is lost, DateDAVGet already built each line to fit).
#define kDavSentinel					0xE000
#define kMaxDav							16
static char gDavLines[kMaxDav][kDateDavLineLen];
static TimeType gDavStart[kMaxDav];
static TimeType gDavEnd[kMaxDav];
static UInt16 gDavCount = 0;

/* Result-cache (user report, 2026-09-17: "Datebook35 is terrible slow" -
 * traced to CalDAV Sync growing DateBook35DAV.pdb from ~27 to 78 real
 * events). Same precedent already proven for Month view
 * (MonthScanAndDrawCalDav, DateMonth.c) and now Week view
 * (WeekRefreshCalDavCache, DateWeek.c) - DateDAVGet does a full
 * DmQueryRecord scan of the WHOLE database on every call, and Agenda was
 * paying that cost on every single date change even when returning to a
 * day it had ALREADY just scanned (e.g. paging forward then back) with
 * DateBook35DAV.pdb completely unchanged in between. gDavLines/Start/End
 * above are deliberately left untouched on a cache hit - AgendaShowDetail's
 * own tap-lookup (~line 1400/2582) already indexes them by absolute slot
 * number and needs no changes either way. */
static Boolean gDavCacheValid = false;
static DateType gDavCacheDate;
static UInt32 gDavCacheModNum;

typedef struct {
	UInt16				currentCategory;
	FontID			v20NoteFont;		// For 2.0 compatibility (BGT)
	Boolean			showAllCategories;
	Boolean 			showCompletedItems;
	Boolean 			showOnlyDueItems;
	Boolean			showDueDates;
	Boolean			showPriorities;
	Boolean			showCategories;
	Boolean			saveBackup;
	Boolean			changeDueDate;
	
	// Version 3 preferences
	FontID			listFont;
	FontID			noteFont;		// For 3.0 and later units.	(BGT)
	
	UInt8				reserved;
} ToDoPreferenceType;


/***********************************************************************
 *
 *	Globals
 *
 ***********************************************************************/

static AgendaType					Agenda;
extern privateRecordViewEnum	PrivateRecordVisualStatus;
extern UInt16				PendingUpdate;					// code of pending day view update


/***********************************************************************
 *
 *	Internal Functions
 *
 ***********************************************************************/

void					AgendaViewInitDatebook (
								FormPtr							frm ) SEC003;
void					AgendaViewInitToDo (
								FormPtr							frm ) SEC003;
static void			AgendaViewChangeDate (
								FormType*						inFormP,
								DateType							inDate ) SEC003;
static void AgendaViewCountDisplayLines (
								FormType*						inFormP,
								UInt16							inApptCount,
								UInt16							inToDoCount,
								UInt16*							outApptLinesP,
								UInt16*							outToDoLinesP ) SEC003;
static void 		AgendaViewLayout (
								FormType*						inFormP,
								UInt16							inApptCount,
								UInt16							inToDoCount ) SEC003;
static void			AgendaViewLayoutTables (
								FormType*						inFormP,
								UInt16							inApptLineCount,
								UInt16							inToDoLineCount ) SEC003;
static void 		AgendaViewSetTopAppointment () SEC003;
static void			AgendaViewSetTopTask () SEC003;
static void			AgendaViewLayoutAppointments (
								FormType*						inFormP,
								RectangleType*					ioBoundsP,
								UInt16							inApptLineCount ) SEC003;
static void			AgendaViewLayoutSeparator (
								FormType*						inFormP,
								RectangleType*					ioBoundsP ) SEC003;
static UInt16		AgendaViewObjectSetTop (
								FormType*						inFormP,
								UInt16							inObjectID,
								UInt16							inTop ) SEC003;
static void			AgendaViewLayoutTasks (
								FormType*						inFormP,
								RectangleType*					ioBoundsP,
								UInt16							inToDoLineCount ) SEC003;
static void			AgendaViewUpdateScroller (
								FormType*						inFormP,
								UInt16							inObjectID,
								UInt16							inAssocObjectID,
								UInt16							inRecordCount,
								UInt16							inVisibleCount,
								UInt16							inLineHeight ) SEC003;
static void			AgendaViewEraseObject (
								FormType*						inFormP,
								UInt16							inObjectID ) SEC003;
static void			AgendaViewSetScrollThumb (
								FormType*						inFormP,
								UInt16							inObjectID,
								UInt16							inValue ) SEC003;
static void			AgendaViewRefreshLayoutTasks (
								FormType*						inFormP ) SEC003;
static void			AgendaViewFillTitle (
								FormPtr							frm ) SEC003;
static void			AgendaViewFillAppointmentsTitle (
								FormType*						frmP ) SEC003;
static void			AgendaViewFillAppointments (
								FormType*						inFormP ) SEC003;
static void			AgendaViewFillTasksTitle (
								FormType*						inFormP,
								RectangleType*					ioBoundsP );
static void			AgendaViewFillTasks (
								FormType*						inFormP ) SEC003;
static void			AgendaDayViewScroll (
								Int16								delta) SEC003;
static void			AgendaViewGoToDate ( ) SEC003;

static void			AgendaViewDrawDate (
								FormType*						inFormP ) SEC003;
static void			AgendaViewDrawTime (
								FormType*						inFormP ) SEC003;
static void			AgendaViewDrawSeparator (
								FormType*						inFormP ) SEC003;
static void			AgendaDateToDOWDM (
								DateType							inDate,
								DateFormatType					inFormat,
								Char*								outAscii ) SEC003;

static Err			AgendaViewGetApptDesc (
								void*								table,
								Int16								row,
								Int16								column,
								Boolean							editable,
								MemHandle*						dataH,
								Int16*							dataOffset,
								Int16*							textAllocSize,
								FieldPtr							fld ) SEC003;
static Boolean		AgendaDividerDraw (
								FormGadgetType*				inGadgetP,
								UInt16							inCommand,
								void*								inParamP ) SEC003;
static void			AgendaTitleDraw (
								void*								inTableP,
								Int16								inRow,
								Int16								inColumn,
								RectangleType*					inBoundsP ) SEC003;
static void			AgendaApptDrawTime (
								void*								inTableP,
								Int16								inRow,
								Int16								inColumn,
								RectangleType*					inBoundsP ) SEC003;
static void			AgendaApptDrawDesc (
								void*								inTableP,
								Int16								inRow,
								Int16								inColumn,
								RectangleType*					inBoundsP ) SEC003;
	

Boolean				AgendaViewHandleTblEnter (
								EventType*							event ) SEC003;

static MemHandle	AgendaLoadAppointments (
								DateType							inDate ) SEC003;
static void			AgendaFreeAppointments ( ) SEC003;
static MemHandle	AgendaGetAppointments ( ) SEC003;
static void			AgendaSetAppointments (
								const MemHandle				inAppointmentsH,
								UInt16							inAppointmentCount ) SEC003;
static UInt16		AgendaGetAppointmentCount ( ) SEC003;
static void			AgendaViewGetScrollableRect (
								FormType*						inFormP,
								RectangleType*					outBoundsP ) SEC003;
static UInt16		AgendaViewGetSeparatorHeight (
								FormType*						inFormP ) SEC003;
static UInt16		AgendaViewGetDatebookDefaultHeight (
								FormType*						inFormP ) SEC003;

static Boolean		SeekRecord (
								DmOpenRef						inDB,
								UInt16*							inIndexP,
								UInt16							inOffset,
								Int16								inDirection,
								UInt16							inCategory,
								DateType							inDate,
								Boolean							inShowCompleted,
								Boolean							inShowDueOnly ) SEC003;
static UInt16		CountRecords (
								DmOpenRef						inDB,
								UInt16*							outIndices,
								UInt16							inIndexCount,
								UInt16							inCategory,
								DateType							inDate,
								Boolean							inShowCompleted,
								Boolean							inShowDueOnly ) SEC003;
static void ListInitTableRow (TablePtr table, UInt16 row, UInt16 recordNum, 
	Int16 rowHeight) SEC003;

void					LaunchToDoWithRecord (
								DmOpenRef					inDB,
								UInt16						inRecordNum ) SEC003;
void					GoToAppointment (
								DmOpenRef					inDB,
								UInt16						inRecordNum ) SEC003;


/***********************************************************************
 *
 *	Temporary Stuff
 *
 ***********************************************************************/

//��� Temporary hack. Move prefs code into new To Do library.
//��� Use new API instead of globals to access these settings.
static FontID		NoteFont = stdFont;						// font used in note view
static UInt16		CurrentCategory = dmAllCategories;	// currently displayed category
static char			CategoryName [dmCategoryLength];		// name of the current category
static Boolean		ShowAllCategories = true;				// true if all categories are being displayed
static Boolean 	ShowCompletedItems = true;				// true if completed items are being displayed
static Boolean 	ShowOnlyDueItems = false;				// true if only due items are displayed
static Boolean		ShowDueDates = false;					// true if due dates are displayed in the list view
static Boolean		ShowPriorities = true;					// true if priorities are displayed in the list view
static Boolean		ShowCategories = false;					// true if categories are displayed in the list view
static Boolean		SaveBackup = true;						// true if save backup to PC is the default
static Boolean		ChangeDueDate = false;					// true if due date is changed to completion date when completed
static FontID		ListFont = stdFont;						// font used to draw to do item
static UInt16		TopVisibleRecord = 0;
// Number of system ticks (1/60 seconds) to display crossed out item
// before they're erased.
#define crossOutDelay					40


static UInt16 ListViewGetColumnWidth (UInt16 column) SEC003;
static Err ListViewGetDescription (void* table, UInt16 row, UInt16 column,
	Boolean editable, MemHandle* textH, UInt16* textOffset, UInt16* textAllocSize,
	FieldPtr fld) SEC003;
static Boolean ListViewSaveDescription (void* table, UInt16 row, UInt16 column) SEC003;
static void ListViewDrawDueDate (void* table, Int16 row, Int16 column, 
	RectanglePtr bounds) SEC003;
static void ListViewDrawCategory (void* table, Int16 row, Int16 column, 
	RectanglePtr bounds) SEC003;
static void			ListViewDrawDesc (
								void*								inTableP,
								Int16								inRow,
								Int16								inColumn,
								RectangleType*					inBoundsP ) SEC003;
static UInt16 ListViewSelectCategory (void) SEC003;
static void ListViewDrawTable (UInt16 updateCode) SEC003;
static void ListViewScroll (Int16 delta) SEC003;
static void ChangeCategory (UInt16 category) SEC003;

Err	ToDoAppInfoInit(DmOpenRef dbP) SEC003;

static UInt16 CountMemoryChunks(UInt16 ownerID);
static void WatchMemoryChunkCount(void);

Char* GetToDoNotePtr (ToDoDBRecordPtr recordP) SEC003;
static void ListViewCrossOutItem (Int16 row) SEC003;
static void ListViewChangeCompleteStatus (Int16 row, UInt16 complete) SEC003;

extern void ECToDoDBValidate(DmOpenRef dbP);

extern void GoToItem (GoToParamsPtr goToParams, Boolean launchingApp);

static void			CheckDBLeaks (
								DmOpenRef						inDB ) SEC003;
static void ToDoLoadPrefs () SEC003;


// Checking for leaks takes a long time with large databases,
// so turn it off, by default. At some point, there may be a better
// mechanism for toggling settings like this from the build environment,
// but, for now, just unlock the file and comment out the unwanted setting.
// (Note: This used to be keyed of off ERROR_CHECK_LEVEL, but this is still
// set to ERROR_CHECK_FULL during alpha testing.)

//#define CHECK_DB_LEAKS(db)		CheckDBLeaks (db)
#define CHECK_DB_LEAKS(db)


/***********************************************************************
 *
 * FUNCTION:    AgendaViewInit
 *
 * DESCRIPTION: Initialize the Agenda view of the Datebook application.
 *
 * PARAMETERS:  frm - pointer to the day view form.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	5/28/99	Initial Revision
 *
 ***********************************************************************/
SEC003 void
AgendaViewInit (
	FormPtr							frm )
{
	SystemPreferencesType sysPrefs;
	UInt16 mode;
	FontID curFont;

	PrefGetPreferences (&sysPrefs);
	
#if EMULATION_LEVEL != EMULATION_NONE
	ECApptDBValidate (ApptDB);
#endif
	
	ToDoLoadPrefs();

	Agenda.apptDB = ApptDB;
	Agenda.apptTopVisibleIndex = 0;
	Agenda.apptFont = ApptDescFont;
	Agenda.apptLineCount = 0;
	
	curFont = FntSetFont (Agenda.apptFont);
	Agenda.apptLineHeight = FntLineHeight ();
	FntSetFont (curFont);
	
	mode = (PrivateRecordVisualStatus == hidePrivateRecords) ?
					dmModeReadOnly : (dmModeReadOnly | dmModeShowSecret);
	
	ToDoGetDatabase (&Agenda.todoDB, mode);	//��� What should be done on failure?
	Agenda.todoLineCount = 0;

	// Db35: the ToDo section needs ToDo35. If it's not installed, tell the user
	// once (the section will otherwise just be silently empty).
	{
		static Boolean sToDo35Warned = false;
		if (! sToDo35Warned && ! ToDo35Installed ())
			{
			FrmAlert (ToDo35NotInstalledAlert);
			sToDo35Warned = true;
			}
	}
	
#ifdef USE_NATIVE_TODO_FONT
	Agenda.todoFont = ListFont;
	curFont = FntSetFont (Agenda.todoFont);
	Agenda.todoLineHeight = FntLineHeight ();
	FntSetFont (curFont);
#else
	Agenda.todoFont = Agenda.apptFont;	// same as appointments
	Agenda.todoLineHeight = Agenda.apptLineHeight;
#endif

	Agenda.timeFont = boldFont;
	Agenda.timeFormat = sysPrefs.timeFormat;
	
//	Agenda.date = Date;
	Agenda.dateFont = boldFont;
	Agenda.dateFormat =		(DateFormatType) PrefGetPreference (prefDateFormat);
	Agenda.longDateFormat =	(DateFormatType) PrefGetPreference (prefLongDateFormat);
	
	AgendaViewInitDatebook (frm);
	AgendaViewInitToDo (frm);
	
	FrmSetGadgetHandler (frm, FrmGetObjectIndex (frm, AgendaDivider), AgendaDividerDraw);

	// Load the appointments and tasks into the table object that will display them.
	//��� Not yet sure what details govern this stuff
	if (PendingUpdate && ItemSelected)
		{
		AgendaViewChangeDate (frm, Date);
		}
	else
		{
//		AgendaViewFillTable (frm);
		AgendaViewChangeDate (frm, Date);
		}

	// Highlight the Agenda View push button.
//	FrmSetControlGroupSelection (frm, AgendaViewGroup, AgendaAgendaViewButton);
}


/***********************************************************************
 *
 * FUNCTION:    AgendaViewInitDatebook
 *
 * DESCRIPTION: Initialize the Datebook portion of the Agenda view.
 *
 * PARAMETERS:  frm - pointer to the agenda view form.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	5/28/99	Initial Revision
 *
 ***********************************************************************/
SEC003 void
AgendaViewInitDatebook (
	FormPtr							frm )
{
	UInt16 row;
	UInt16 rowsInTable;
	TablePtr table;

	// Initialize the table used to display the day's agenda.
	table = FrmGetObjectPtr (frm, FrmGetObjectIndex (frm, AgendaDatebookTable));

	rowsInTable = TblGetNumberOfRows (table);
	for (row = 0; row < rowsInTable; row++)
		{		
		TblSetItemStyle (table, row, agendaApptTimeColumn, customTableItem);
		TblSetItemStyle (table, row, agendaApptDescColumn, customTableItem);
		}

	TblSetColumnUsable (table, agendaApptTimeColumn, true);
	TblSetColumnUsable (table, agendaApptDescColumn, true);
	TblSetColumnMasked (table, agendaApptDescColumn, true);

	TblSetColumnSpacing (table, agendaApptTimeColumn, agendaApptTimeColumnSpacing);

	// Set the callback routine that will load the description field.
	// (No callback for saving, since this is a read-only view.)
	TblSetLoadDataProcedure (table, agendaApptDescColumn, AgendaViewGetApptDesc);
	
	// Set the callback routine that draws the time field.
	TblSetCustomDrawProcedure (table, agendaApptTimeColumn, AgendaApptDrawTime);
	TblSetCustomDrawProcedure (table, agendaApptDescColumn, AgendaApptDrawDesc);
}


/***********************************************************************
 *
 * FUNCTION:    AgendaViewGetApptDesc
 *
 * DESCRIPTION: This routine returns a pointer to the description field
 *              of a appointment record.  This routine is called by 
 *              the table object as a callback routine when it wants to 
 *              display or edit the appointment's description.
 *
 * PARAMETERS:  inTable			- pointer to the Day View table (TablePtr)
 *              inRow			- row in the table
 *              inColumn		- column in the table
 *              inEditable		- true if the field will be edited by the table
 *					 outHandleP		- handle to the appointment record
 *              outOffsetP		- offset within the record of the desc field (returned)
 *              outSizeP		- allocated size the the description field (returned)
 *					 ioFieldP		- Table Mgr's field description
 *
 * RETURNED:    handle of the appointment record
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	6/4/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static Err AgendaViewGetApptDesc (
	void*								inTableP,
	Int16								inRow,
	Int16								inColumn,
	Boolean							inEditable,
	MemHandle*						outHandleP,
	Int16*							outOffsetP,
	Int16*							outSizeP,
	FieldType*						ioFieldP )
{
#pragma unused (inColumn, inEditable)

	Err								err = errNone;
	UInt16							apptIndex;
	ApptInfoPtr						appts;
	UInt16							recordNum;
	MemHandle						recordH;
	ApptDBRecordType				apptRec;
	FieldAttrType					attributes;
	
	Assert (inColumn == agendaApptDescColumn );
	Assert (outHandleP);
	Assert (outOffsetP);
	Assert (outSizeP);
	Assert (ioFieldP);
	
	*outHandleP = 0;

	// Override the default field attributes
	FldGetAttributes (ioFieldP, &attributes);
	attributes.underlined = false;
	attributes.editable = false;
	FldSetAttributes (ioFieldP, &attributes);

	// Get the appointment that corresponds to the table item.
	// The index of the appointment in the appointment list is stored
	// as the row id.
	apptIndex = TblGetRowID (inTableP, inRow);
	
	// If there aren't any appointments, show a message to that effect
	if (apptIndex == noAppointments)
		{
		*outHandleP = DmGetResource (strRsc, agendaNoAppointmentsStrID);
		*outOffsetP = 0;
		*outSizeP = MemHandleSize (*outHandleP);
		goto Exit;
		}

	// Get the record index of the next appointment
	appts = MemHandleLock (AgendaGetAppointments ());
	recordNum = appts[apptIndex].recordNum;
	MemPtrUnlock (appts);

	if (recordNum >= kDavSentinel)		// Db35: birthday or CalDAV row -- not editable
		{
		*outOffsetP = 0;
		*outSizeP = 0;
		goto Exit;
		}

	// Get the offset and length of the description field
	err = ApptGetRecord (Agenda.apptDB, recordNum, &apptRec, &recordH);
	
	if ( err == errNone )
		{
		*outHandleP = recordH;
		
		if ( apptRec.description != NULL )
			{
			*outOffsetP = (UInt16) ((UInt32) apptRec.description - (UInt32) apptRec.when);
			*outSizeP = StrLen (apptRec.description) + 1;  // one for null terminator
			}
		else
			{
			*outOffsetP = 0;
			*outSizeP = 0;
			}
		}
		
	MemHandleUnlock (recordH);
	
Exit:
	return (err);
}


/***********************************************************************
 *
 * FUNCTION:    AgendaViewChangeDate
 *
 * DESCRIPTION: This routine displays the date passed.
 *
 * PARAMETERS:  date - date to display
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/29/95	Initial Revision
 *
 ***********************************************************************/
SEC003 static void AgendaViewChangeDate (
	FormType*						inFormP,
	DateType							inDate )
{
	CHECK_DB_LEAKS (Agenda.todoDB);

	// Keep the new date for future reference
	Date = inDate;
	Agenda.timeSeconds = TimGetSeconds ();
	
	// Find out how many appointments and tasks to display
	AgendaLoadAppointments (inDate);		// sets Agenda.apptCount
	
	// Tally up the number of displayable tasks and store the index
	// of the first in TopVisibleRecord
	Agenda.todoCount = CountRecords (Agenda.todoDB, &TopVisibleRecord, 1, CurrentCategory,
										Date, ShowCompletedItems, ShowOnlyDueItems);

	AgendaViewDrawDate (inFormP);
	AgendaViewDrawTime (inFormP);
		
	// Get all the appointments and empty time slots on the new day.
	AgendaViewEraseObject (inFormP, AgendaDatebookTable);
	AgendaViewEraseObject (inFormP, AgendaToDoTable);
	AgendaViewLayout (inFormP, Agenda.apptCount, Agenda.todoCount);
	
	// Determine the top items to display
	AgendaViewSetTopAppointment ();
//	AgendaViewSetTopTask ();
	
	// Fill the display tables
	AgendaViewFillAppointments (inFormP);
	AgendaViewFillTasks (inFormP);

	// Update the scroll arrows
	AgendaViewSetScrollThumb (inFormP, AgendaDatebookScroller, Agenda.apptTopVisibleIndex);
	AgendaViewSetScrollThumb (inFormP, AgendaToDoScroller, 0);

	// Draw the new day's events
	FrmSetControlGroupSelection (inFormP, AgendaViewGroup, AgendaAgendaViewButton);
	FrmDrawForm (inFormP);
	
	CHECK_DB_LEAKS (Agenda.todoDB);
}


/***********************************************************************
 *
 * FUNCTION:    AgendaViewLayout
 *
 * DESCRIPTION: This routine displays the date passed.
 *
 * PARAMETERS:  date - date to display
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	9/13/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static void AgendaViewLayout (
	FormType*						inFormP,
	UInt16							inApptCount,
	UInt16							inToDoCount )
{
	UInt16							apptLineCount;
	UInt16							todoLineCount;
	
	AgendaViewCountDisplayLines (inFormP, inApptCount, inToDoCount,
											&apptLineCount, &todoLineCount);
	
	// DOLATER rbb - Enhance performance by processing events during screen refresh,
	// checking to see if the displayed date has changed and, if so, starting over.
	// This code should also use offscreen drawing so that the user will never have
	// to see a partial redraw.
	
	if (Agenda.apptLineCount != apptLineCount)
		{
//		AgendaViewEraseObject (inFormP, AgendaDatebookTable);
		AgendaViewEraseObject (inFormP, AgendaDatebookScroller);
		AgendaViewEraseObject (inFormP, AgendaDivider);
		AgendaViewEraseObject (inFormP, AgendaToDoCategoryTrigger);
		}
		
	if ( (Agenda.apptLineCount != apptLineCount) || (Agenda.todoLineCount != todoLineCount) )
		{
		// Erase To Do
//		AgendaViewEraseObject (inFormP, AgendaToDoTable);
		AgendaViewEraseObject (inFormP, AgendaToDoScroller);
		}

	AgendaViewLayoutTables (inFormP, apptLineCount, todoLineCount);

	AgendaViewUpdateScroller (inFormP, AgendaDatebookScroller,AgendaDatebookTable,
										inApptCount, apptLineCount, Agenda.apptLineHeight);

	AgendaViewUpdateScroller (inFormP, AgendaToDoScroller, AgendaToDoTable,
										inToDoCount, todoLineCount, Agenda.todoLineHeight);
	
	Agenda.apptLineCount = apptLineCount;
	Agenda.todoLineCount = todoLineCount;
}


SEC003 static void AgendaViewCountDisplayLines (
	FormType*						inFormP,
	UInt16							inApptCount,
	UInt16							inToDoCount,
	UInt16*							outApptLinesP,
	UInt16*							outToDoLinesP )
{
	UInt16							apptCount;
	UInt16							todoCount;
	RectangleType					agendaBounds;
	UInt16							agendaHeight;	
	UInt16							separatorHeight;	
	UInt16							apptSectionHeight;	
	UInt16							todoSectionHeight;	
	UInt16							apptDefaultCount;
	UInt16							todoDefaultCount;
	UInt16							apptDisplayCount;
	UInt16							todoDisplayCount;
	UInt16							apptDisplayHeight;
	UInt16							todoDisplayHeight;
	UInt16							remainingDisplayHeight;
	UInt16							remainingDisplayCount;
	
	// Compensate for empty sections, which will show something like,
	// "No Appointments Today"
	apptCount = max (minDatebookRows, inApptCount);
	todoCount = max (minToDoRows, inToDoCount);
	
	// Get the default sizes for each of the view sections
	AgendaViewGetScrollableRect (inFormP, &agendaBounds);
	agendaHeight = agendaBounds.extent.y;
	separatorHeight = AgendaViewGetSeparatorHeight (inFormP);
	apptSectionHeight = AgendaViewGetDatebookDefaultHeight (inFormP);
	todoSectionHeight = agendaHeight - separatorHeight - apptSectionHeight;
	
	// Convert those sizes into line counts
	apptDefaultCount = apptSectionHeight / Agenda.apptLineHeight;
	todoDefaultCount = todoSectionHeight / Agenda.todoLineHeight;
	
	// Adjust the layout for optimum viewing
	if (apptCount <= apptDefaultCount)
		{
		// All appointments fit within the standard area
		apptDisplayCount = apptCount;
		apptDisplayHeight = apptDisplayCount * Agenda.apptLineHeight;
		
		if (todoCount <= todoDefaultCount)
			{
			// All tasks fit within the standard area
			todoDisplayCount = todoCount;
			todoDisplayHeight = todoDisplayCount * Agenda.todoLineHeight;
			}
		else
			{
			// Extra tasks can grow into the unused appointment area
			remainingDisplayHeight = agendaHeight - separatorHeight - apptDisplayHeight;
			remainingDisplayCount = remainingDisplayHeight / Agenda.todoLineHeight;
			todoDisplayCount = min (todoCount, remainingDisplayCount);
			todoDisplayHeight = todoDisplayCount * Agenda.todoLineHeight;
			}
		}
	else if (todoCount <= todoDefaultCount)
		{
		// All tasks fit within the standard area
		todoDisplayCount = todoCount;
		todoDisplayHeight = todoDisplayCount * Agenda.todoLineHeight;
		
		// Extra appointments can grow into the unused task area
		remainingDisplayHeight = agendaHeight - separatorHeight - todoDisplayHeight;
		remainingDisplayCount = remainingDisplayHeight / Agenda.apptLineHeight;
		apptDisplayCount = min (apptCount, remainingDisplayCount);
		apptDisplayHeight = apptDisplayCount * Agenda.apptLineHeight;
		}
	else
		{
		// Both the appointment and task sections are full, so use the default layout
		apptDisplayCount = apptDefaultCount;
		apptDisplayHeight = apptDisplayCount * Agenda.apptLineHeight;
		todoDisplayCount = todoDefaultCount;
		todoDisplayHeight = todoDisplayCount * Agenda.todoLineHeight;
		}
	
	*outApptLinesP = apptDisplayCount;
	*outToDoLinesP = todoDisplayCount;
}


/***********************************************************************
 *
 * FUNCTION:    AgendaViewSetTopAppointment
 *
 * DESCRIPTION: This routine determines the first appointment that should
 *              be visible on the current day.  For all dates other than
 *              today the fisrt time slot of the appointment list
 *              is the first visible appointment.  For today the time
 *              slot that stats before to the current time should be the top 
 *              visible time slot.
 *
 * PARAMETERS:  nothing.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/29/95	Initial Revision
 *
 ***********************************************************************/
SEC003 static void AgendaViewSetTopAppointment ()
{
	UInt16 i;
	TimeType time;
	DateTimeType dateTime;
	MemHandle apptsH;
	ApptInfoPtr apptsP;

	Agenda.apptTopVisibleIndex = 0;

	TimSecondsToDateTime (TimGetSeconds (), &dateTime);

	// If the current date is not today, then the first appointment 
	// is the first one visible.
	if ( (dateTime.year - firstYear != Date.year) ||
		  (dateTime.month != Date.month) ||
		  (dateTime.day != Date.day))
		{
		return;
		}

	// If the current date is today, then the top visible appointment is
	// the appointment with the greatest end time that is before the 
	// current time.
	time.hours = dateTime.hour;
	time.minutes = dateTime.minute;
	
	apptsH = AgendaGetAppointments ();
	if (apptsH)
		{
		apptsP = MemHandleLock (apptsH);
		for (i = 0; i < Agenda.apptCount; i++)
			{
			if (TimeToInt (apptsP[i].endTime) < TimeToInt (time))
				Agenda.apptTopVisibleIndex = i;
			}

		MemPtrUnlock (apptsP);
		}
}


/***********************************************************************
 *
 * FUNCTION:    AgendaViewSetTopTask
 *
 * DESCRIPTION: This routine determines the first task that should
 *              be visible on the current day.  For all dates other than
 *              today the fisrt time slot of the appointment list
 *              is the first visible appointment.  For today the time
 *              slot that stats before to the current time should be the top 
 *              visible time slot.
 *
 * PARAMETERS:  nothing.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/29/95	Initial Revision
 *
 ***********************************************************************/
SEC003 static void AgendaViewSetTopTask ()
{
	TopVisibleRecord = 0;
}


SEC003 static void
AgendaViewLayoutTables (
	FormType*						inFormP,
	UInt16							inApptLineCount,
	UInt16							inToDoLineCount )
{
	RectangleType					scrollableRect;
	RectangleType					apptRect;
	RectangleType					separatorRect;
	RectangleType					todoRect;

	AgendaViewGetScrollableRect (inFormP, &scrollableRect);
	
	apptRect = scrollableRect;
	
	AgendaViewLayoutAppointments (inFormP, &apptRect, inApptLineCount);
	
	separatorRect.topLeft.x = scrollableRect.topLeft.x;
	separatorRect.topLeft.y = apptRect.topLeft.y + apptRect.extent.y;
	separatorRect.extent.x = scrollableRect.extent.x;
	separatorRect.extent.y = scrollableRect.extent.y - apptRect.extent.y;
	
	AgendaViewLayoutSeparator (inFormP, &separatorRect);
	
	todoRect.topLeft.x = scrollableRect.topLeft.x;
	todoRect.topLeft.y = separatorRect.topLeft.y + separatorRect.extent.y;
	todoRect.extent.x = scrollableRect.extent.x;
	todoRect.extent.y = scrollableRect.extent.y - apptRect.extent.y - separatorRect.extent.y;
	
	AgendaViewLayoutTasks (inFormP, &todoRect, inToDoLineCount);
}


SEC003 static void
AgendaViewLayoutAppointments (
	FormType*						inFormP,
	RectangleType*					ioBoundsP,
	UInt16							inApptLineCount )
{
#pragma unused (inFormP)
	TableType*						tableP;
	UInt16							row;
	UInt16							rowCount;
	RectangleType					bounds;

	tableP = GetObjectPtr (AgendaDatebookTable);
	rowCount = TblGetNumberOfRows (tableP);

	// Set the usability for each row in the table. If there are fewer appointments
	// than rows, the remaining rows will be unusable
	for (row = 0; row < rowCount; row++)
		{
		if ((row < inApptLineCount)
				&& (!TblRowUsable (tableP, row)
						|| (TblGetRowHeight (tableP, row) != Agenda.apptLineHeight)) )
			{
			TblMarkRowInvalid (tableP, row);
			}
			
		TblSetRowUsable (tableP, row, row < inApptLineCount);
		TblSetRowHeight (tableP, row, Agenda.apptLineHeight);
		}
	
	// Adjust the table size so that it doesn't overlap the To Do table
	TblGetBounds (tableP, &bounds);
	bounds.extent.y = inApptLineCount * Agenda.apptLineHeight;
	TblSetBounds (tableP, &bounds);
	ioBoundsP->extent.y = inApptLineCount * Agenda.apptLineHeight;
}


SEC003 static void
AgendaViewLayoutSeparator (
	FormType*						inFormP,
	RectangleType*					ioBoundsP )
{
	RectangleType					dividerBounds;
	UInt16							top = ioBoundsP->topLeft.y;
	
	AgendaViewObjectSetTop (inFormP, AgendaDivider, top);
	AgendaViewObjectSetTop (inFormP, AgendaToDoCategoryList, top + adjustCategoryTop);
	AgendaViewObjectSetTop (inFormP, AgendaToDoCategoryTrigger, top + adjustCategoryTop);
	
	FrmGetObjectBounds (inFormP, FrmGetObjectIndex (inFormP, AgendaDivider), &dividerBounds);
	ioBoundsP->topLeft.y = dividerBounds.topLeft.y;
	ioBoundsP->extent.y = dividerBounds.extent.y;
}


SEC003 static UInt16
AgendaViewObjectSetTop (
	FormType*						inFormP,
	UInt16							inObjectID,
	UInt16							inTop )
{
	RectangleType					bounds;
	UInt16							index;

	index = FrmGetObjectIndex (inFormP, inObjectID);
	FrmGetObjectBounds (inFormP, index, &bounds);
	bounds.topLeft.y = inTop;
	FrmSetObjectBounds (inFormP, index, &bounds);
	
	return bounds.topLeft.y + bounds.extent.y;
}

SEC003 static void
AgendaViewLayoutTasks (
	FormType*						inFormP,
	RectangleType*					ioBoundsP,
	UInt16							inToDoLineCount )
{
#pragma unused (inFormP)
	TableType*						tableP;
	UInt16							row;
	UInt16							rowCount;
	RectangleType					bounds;

	tableP = GetObjectPtr (AgendaToDoTable);
	rowCount = TblGetNumberOfRows (tableP);
	TblGetBounds (tableP, &bounds);
	
	// Set the usability for each row in the table. If there are fewer tasks
	// than rows, the remaining rows will be unusable
	for (row = 0; row < rowCount; row++)
		{
		if ((row < inToDoLineCount)
				&& (!TblRowUsable (tableP, row)
						|| (ioBoundsP->topLeft.y != bounds.topLeft.y)
						|| (TblGetRowHeight (tableP, row) != Agenda.todoLineHeight)) )
			{
			TblMarkRowInvalid (tableP, row);
			}
			
		TblSetRowUsable (tableP, row, row < inToDoLineCount);
		TblSetRowHeight (tableP, row, Agenda.todoLineHeight);
		}
	
	// For drawing purposes, the table should extend to the bottom of the
	// displayable area, regardless of the number of displayable lines.
	TblSetBounds (tableP, ioBoundsP);
}


SEC003 static void
AgendaViewUpdateScroller (
	FormType*						inFormP,
	UInt16							inObjectID,
	UInt16							inAssocObjectID,
	UInt16							inRecordCount,
	UInt16							inVisibleCount,
	UInt16							inLineHeight )
{
	RectangleType					assocBounds;
	UInt16							scrollIndex;
	RectangleType					scrollBounds;
	UInt16							scrollMax;
	Int16								currentValue;
	Int16								currentMin;
	Int16								currentMax;
	Int16								currentPageSize;
	
	FrmGetObjectBounds (inFormP, FrmGetObjectIndex (inFormP, inAssocObjectID), &assocBounds);
	
	scrollMax = max (0, (Int32) inRecordCount - inVisibleCount);	// without the typecast, max ignores the 0!!!
	
	// Resize the scroll bar to be slightly inset from the provided bounds.
	// To avoid runtime warnings (debug only), make sure that it is also
	// at least 1 pixel tall.
	scrollIndex = FrmGetObjectIndex (inFormP, inObjectID);
	FrmGetObjectBounds (inFormP, scrollIndex, &scrollBounds);
	
	scrollBounds.topLeft.y = assocBounds.topLeft.y + scrollerInset;
	scrollBounds.extent.y = max (1, (Int32) inVisibleCount * inLineHeight - (2 * scrollerInset));
	FrmSetObjectBounds (inFormP, scrollIndex, &scrollBounds);
	
	SclGetScrollBar (GetObjectPtr (inObjectID), &currentValue, &currentMin,
							&currentMax, &currentPageSize);
	currentValue = min (currentValue, scrollMax);
	SclSetScrollBar (GetObjectPtr (inObjectID), currentValue, 0, scrollMax, inVisibleCount);
}


SEC003 static void
AgendaViewEraseObject (
	FormType*						inFormP,
	UInt16							inObjectID )
{
	RectangleType					bounds;
	
	if (FrmVisible(inFormP))
		{
		FrmGetObjectBounds (inFormP, FrmGetObjectIndex (inFormP, inObjectID), &bounds);
		WinEraseRectangle (&bounds, 0);
		}
}


SEC003 static void
AgendaViewSetScrollThumb (
	FormType*						inFormP,
	UInt16							inObjectID,
	UInt16							inValue )
{
#pragma unused (inFormP)
	ScrollBarType*					scrollBarP;
	Int16								scrollValue;
	Int16								scrollMin;
	Int16								scrollMax;
	Int16								scrollPage;

	scrollBarP = GetObjectPtr (inObjectID);
	SclGetScrollBar (scrollBarP, &scrollValue, &scrollMin, &scrollMax, &scrollPage);
	SclSetScrollBar (scrollBarP, inValue, scrollMin, scrollMax, scrollPage);
}


SEC003 static void
AgendaViewRefreshLayoutTasks (
	FormType*						inFormP )
{
	UInt16							oldApptLineCount;

	// This routine is called when the To Do section of the agenda has been altered.
	// Even though only the To Do section has changed, the heuristic divider may
	// need to be repositioned if the Datebook section overflows its default size.

	oldApptLineCount = Agenda.apptLineCount;

	AgendaViewLayout (inFormP, Agenda.apptCount, Agenda.todoCount);
	
	if (Agenda.apptLineCount != oldApptLineCount)
		{
		AgendaViewFillAppointments (inFormP);
		AgendaViewSetScrollThumb (inFormP, AgendaDatebookScroller, Agenda.apptTopVisibleIndex);
		}

	AgendaViewFillTasks (inFormP);
}


/***********************************************************************
 *
 * FUNCTION:    AgendaApptDrawTime
 *
 * DESCRIPTION: Draw the starting time of an appointment.  This
 *              routine is called by the table object as a callback 
 *              routine.
 *
 * PARAMETERS:  table  - pointer to the memo Day table (TablePtr)
 *              row    - row of the table to draw
 *              column - column of the table to draw
 *              bounds - region to draw in 
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	6/4/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static void
AgendaApptDrawTime (
	void*								inTableP,
	Int16								inRow,
	Int16								inColumn,
	RectangleType*					inBoundsP )
{
#pragma unused (inColumn)
	UInt16							apptIndex;
	ApptInfoPtr						appts;
	TimeType							startTime;

	// Get the appointment that corresponds to the table item.
	// The index of the appointment in the appointment list is stored
	// as the row id.
	apptIndex = TblGetRowID (inTableP, inRow);

	// When there are no appointments, a special index is stored. If this is the case, don't
	// try to draw the time; the description column will show "No Appointments"
	if (apptIndex != noAppointments)
		{
		// Get the record index of the next appointment
		appts = MemHandleLock (AgendaGetAppointments ());
		startTime = appts[apptIndex].startTime;
		MemPtrUnlock (appts);
		
		DrawTime (startTime, TimeFormat, apptTimeFont, rightAlign, inBoundsP);
		}
}


/***********************************************************************
 *
 * FUNCTION:    AgendaApptDrawDesc
 *
 * DESCRIPTION: Draw the description of an appointment.  This
 *              routine is called by the table object as a callback 
 *              routine.
 *
 * PARAMETERS:  table  - pointer to the memo Day table (TablePtr)
 *              row    - row of the table to draw
 *              column - column of the table to draw
 *              bounds - region to draw in 
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	8/30/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static void
AgendaApptDrawDesc (
	void*								inTableP,
	Int16								inRow,
	Int16								inColumn,
	RectangleType*					inBoundsP )
{
#pragma unused (inColumn)
	Err								err = errNone;
	UInt16							apptIndex;
	ApptInfoPtr						apptsP;
	UInt16							recordNum;
	MemHandle						recordH = NULL;
	ApptDBRecordType				apptRec;
	MemHandle						textH = NULL;
	Char*								textP = NULL;
	FontID							curFont;
	Char*								tempP;
	UInt16							tempCount = 0;

	// Get the appointment that corresponds to the table item.
	// The index of the appointment in the appointment list is stored
	// as the row id.
	apptIndex = TblGetRowID (inTableP, inRow);

	// When there are no appointments, a special index is stored. If this is the case, don't
	// try to draw the time; the description column will show "No Appointments"
	if (apptIndex != noAppointments)
		{
		// Get the record index of the next appointment
		apptsP = MemHandleLock (AgendaGetAppointments ());
		recordNum = apptsP[apptIndex].recordNum;
		MemPtrUnlock (apptsP);

		if (recordNum >= kBdaySentinel)			// Db35: a birthday/anniversary row
			{
			textP = gBdayLines[recordNum - kBdaySentinel];
			}
		else if (recordNum >= kDavSentinel)		// Db35: a CalDAV Sync event row
			{
			textP = gDavLines[recordNum - kDavSentinel];
			}
		else
			{
			// Get the offset and length of the description field
			err = ApptGetRecord (Agenda.apptDB, recordNum, &apptRec, &recordH);

			if ( err == errNone )
				textP = apptRec.description;
			}
		}
	else
		{
		textH = DmGetResource (strRsc, agendaNoAppointmentsStrID);
		textP = MemHandleLock (textH);
		}
	
	if (textP)
		{
		curFont = FntSetFont (Agenda.apptFont);

		tempP = textP;
		while (*tempP && *tempP != linefeedChr)
			{
			++tempCount;
			++tempP;
			}
		WinDrawTruncChars (textP, tempCount, inBoundsP->topLeft.x, inBoundsP->topLeft.y,
									inBoundsP->extent.x);
		FntSetFont (curFont);
		}
	
	if (recordH)
		{
		MemHandleUnlock (recordH);
		}
	
	if (textH)
		{
		MemHandleUnlock (textH);
		DmReleaseResource (textH);
		}
}


SEC003 static Boolean
AgendaDividerDraw (
	FormGadgetType*				inGadgetP,
	UInt16							inCommand,
	void*								inParamP )
{
#pragma unused (inParamP)
	Boolean							handled = false;
	
	switch (inCommand)
		{
		case formGadgetDrawCmd:
			WinDrawLine (	inGadgetP->rect.topLeft.x,
								inGadgetP->rect.topLeft.y + adjustSeparatorTop,
								inGadgetP->rect.topLeft.x + inGadgetP->rect.extent.x - 1,
								inGadgetP->rect.topLeft.y + adjustSeparatorTop );
			handled = true;
			break;
		
		case formGadgetEraseCmd:
			WinEraseLine (	inGadgetP->rect.topLeft.x,
								inGadgetP->rect.topLeft.y + adjustSeparatorTop,
								inGadgetP->rect.topLeft.x + inGadgetP->rect.extent.x - 1,
								inGadgetP->rect.topLeft.y + adjustSeparatorTop );
			handled = true;
			break;
		}
	
	return handled;
}


/***********************************************************************
 *
 * FUNCTION:    AgendaTitleDraw
 *
 * DESCRIPTION: Draw the current date and time
 *
 * PARAMETERS:  frm - pointer to the agenda view form.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	6/4/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static void
AgendaTitleDraw (
	void*								inTableP,
	Int16								inRow,
	Int16								inColumn,
	RectangleType*					inBoundsP )
{
	DateTimeType					now;
	char								timeStr[timeStringLength];
	char								dateStr[longDateStrLength];
	FontID							origFont;
	UInt16							x;
	UInt16							len;

	TimSecondsToDateTime (TimGetSeconds (), &now);
	
	WinInvertRectangle (inBoundsP, 0);
	origFont = FntSetFont (TblGetItemFont (inTableP, inRow, inColumn));
	
	switch (inColumn)
		{
		case agendaTitleDateColumn:
			DateToAscii (now.month, now.day, now.year, Agenda.longDateFormat, dateStr);

			x = inBoundsP->topLeft.x + agendaTitleInset;
			WinInvertChars (dateStr, StrLen (dateStr), x, inBoundsP->topLeft.y);
			break;
		
		case agendaTitleTimeColumn:
			TimeToAscii (now.hour, now.minute, Agenda.timeFormat, timeStr);
			
			len = StrLen (timeStr);
			x = inBoundsP->topLeft.x + ( inBoundsP->extent.x - FntCharsWidth (timeStr, len) );
			x -= agendaTitleInset;
			WinInvertChars (timeStr,  len, x, inBoundsP->topLeft.y);
			break;
		}
	
	FntSetFont (origFont);
}


/***********************************************************************
 *
 * FUNCTION:    AgendaViewDrawDate
 *
 * DESCRIPTION: Draw the current date
 *
 * PARAMETERS:  inFormP - pointer to the agenda view form.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	6/4/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static void
AgendaViewDrawDate (
	FormType*						inFormP )
{
#pragma unused (inFormP)

	// DOLATER rbb 7/8/99 - Add constants, in DateAgenda.h or DateTime.h, for
	// string length and trim offsets.
	static Char						titleP [dowLongDateStrLength];
	ControlType*					controlP;
	Int8							trimOffset = 0;

	AgendaDateToDOWDM (Date, Agenda.longDateFormat, titleP);
	
	
	controlP = (ControlType*) GetObjectPtr (AgendaCurrentDayButton);
	CtlSetLabel (controlP, titleP);
}


/***********************************************************************
 *
 * FUNCTION:    AgendaViewDrawTime
 *
 * DESCRIPTION: Draw the current time
 *
 * PARAMETERS:  inFormP - pointer to the agenda view form.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	6/4/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static void
AgendaViewDrawTime (
	FormType*						inFormP )
{
#pragma unused (inFormP)
	static Char						timeStr [timeStringLength];
	DateTimeType 					dateTime;
	
	TimSecondsToDateTime (Agenda.timeSeconds, &dateTime);
	TimeToAscii (dateTime.hour, dateTime.minute, Agenda.timeFormat, timeStr);
	
	FrmCopyTitle (inFormP, timeStr);
}


/***********************************************************************
 *
 * FUNCTION:    AgendaViewDrawSeparator
 *
 * DESCRIPTION: Draw the separator line between the calendar and task sections
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	6/4/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static void
AgendaViewDrawSeparator (
	FormType*						inFormP )
{
#pragma unused (inFormP)
	FormGadgetType*					separatorP;
	RectangleType						bounds;
	FormGadgetType*					dividingLineP;
	ControlType*						categoryTriggerP;
	IndexedColorType					oldForeColor;
	
	separatorP = GetObjectPtr (AgendaSeparatorDefaultArea);
	dividingLineP = GetObjectPtr (AgendaDivider);
	
	bounds = separatorP->rect;
	bounds.topLeft.y = dividingLineP->rect.topLeft.y;
	bounds.extent.y = dividingLineP->rect.extent.y;
	WinEraseRectangle (&bounds, 0);
	
	oldForeColor = WinSetForeColor(UIColorGetTableEntryIndex(UIFormFrame));
	(dividingLineP->handler) (dividingLineP, formGadgetDrawCmd, 0);
	WinSetForeColor(oldForeColor);

	categoryTriggerP = GetObjectPtr (AgendaToDoCategoryTrigger);
	CtlDrawControl (categoryTriggerP);
	
}


/***********************************************************************
 *
 * FUNCTION:    AgendaDateToDOWDM
 *
 * DESCRIPTION: Similar to DateToAscii, but accepts DateType and masks
 *					 out century 
 *
 * PARAMETERS:  
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	6/28/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static void
AgendaDateToDOWDM (
	DateType						inDate,
	DateFormatType					inFormat,
	Char*							outAscii )
{
#pragma unused (inFormat)
//	DateToDOWDMFormat (inDate.month, inDate.day, inDate.year + firstYear,
//								inFormat, outAscii);

	// Allocate a block to hold the description and copy the template 
	// string into it.
	MemHandle formatStrH = DmGetResource (strRsc, agendaDateFormatStrID);
	Char* formatStrP = MemHandleLock (formatStrH);
	
	DateTemplateToAscii(formatStrP, inDate.month, inDate.day, inDate.year + firstYear,
								outAscii, longDateStrLength+10);
								
	MemHandleUnlock (formatStrH);
	DmReleaseResource (formatStrH);
}


/***********************************************************************
 *
 * FUNCTION:    AgendaDayViewScroll
 *
 * DESCRIPTION: This routine scrolls the list of ToDo items
 *              in the direction specified.
 *
 * PARAMETERS:  direction - winUp or dowm
 *              oneLine   - if true the list is scrolled by a single line,
 *                          if false the list is scrolled by a full screen.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	2/21/95	Initial Revision
 *			rbb	4/14/99	Uses new ListViewDrawTable
 *
 ***********************************************************************/
SEC003 static void
AgendaDayViewScroll (
	Int16								delta)
{
	UInt16 							apptIndex;
	UInt16							apptCount;
	UInt16							rowCount;
	UInt16							newIndex;
	TableType* 						tableP;

	if (delta == 0)
		{
		return;
		}
	
	tableP = GetObjectPtr (AgendaDatebookTable);
	TblReleaseFocus (tableP);
	
	apptIndex = Agenda.apptTopVisibleIndex;
	apptCount = AgendaGetAppointmentCount();
	rowCount = TblGetLastUsableRow (tableP) + 1;
	
	ErrFatalDisplayIf (apptIndex == noAppointments, "AgendaDayViewScroll called with no appointments");
	
	newIndex = max (0, (Int32) apptIndex + delta);
	newIndex = min (newIndex, apptCount - rowCount);

	Agenda.apptTopVisibleIndex = newIndex;
	
	AgendaViewFillAppointments (FrmGetActiveForm());
	TblRedrawTable (tableP);
}


/***********************************************************************
 *
 * FUNCTION:    AgendaViewGoToDate
 *
 * DESCRIPTION: This routine displays the date picker so that the 
 *              user can select a date to navigate to.  If the date
 *              picker is confirmed, the date selected is displayed.
 *
 *              This routine is called when a "go to" button is pressed.
 *              
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/29/95	Initial Revision
 *
 ***********************************************************************/
SEC003 static void
AgendaViewGoToDate ( void )
{
	Char* title;
	MemHandle titleH;
	Int16 month, day, year;

	// Get the title for the date picker dialog box.
	titleH = DmGetResource (strRsc, goToDateTitleStrID);
	title = MemHandleLock (titleH);

	day = Date.day;
	month = Date.month;
	year = Date.year + firstYear;

	// Display the date picker (Db35: era-aware picker, not the ROM SelectDay
	// which clamps to 2031 and shows the window year).
	if (DatePickSelectDay (selectDayByDay, &month, &day, &year, title))
		{
		Date.day = day;
		Date.month = month;
		Date.year = year - firstYear;

		AgendaViewChangeDate (FrmGetActiveForm(), Date);
		}
		
	MemHandleUnlock (titleH);
}


#pragma mark -
/***********************************************************************
 *
 * FUNCTION:    AgendaViewInitToDo
 *
 * DESCRIPTION: Initialize the To Do portion of the Agenda view.
 *
 * PARAMETERS:  frm - pointer to the day view form.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	5/28/99	Initial Revision
 *
 ***********************************************************************/
SEC003 void
AgendaViewInitToDo (
	FormPtr							frm )
{
	// Get the To Do prefs
	UInt16 row;
	UInt16 rowsInTable;
	UInt16 width;
	FontID fontID;
	TablePtr table;
	RectangleType r;
	Boolean showCategories = ShowCategories && (CurrentCategory == dmAllCategories);
	ControlPtr ctl;
	

	table = FrmGetObjectPtr (frm, FrmGetObjectIndex (frm, AgendaToDoTable));

	AgendaExpandToDoColumns (table);	// grow 3->5 columns (see helper); pilrc caps at 3

	rowsInTable = TblGetNumberOfRows (table);
	for (row = 0; row < rowsInTable; row++)
		{
		TblSetItemStyle (table, row, completedColumn, checkboxTableItem);
		TblSetItemStyle (table, row, priorityColumn, numericTableItem);
		TblSetItemStyle (table, row, descColumn, customTableItem);
		TblSetItemStyle (table, row, dueDateColumn, customTableItem);
		TblSetItemStyle (table, row, categoryColumn, customTableItem);


		// Set the font used to draw the text of the row.
		if (ListFont == stdFont)
			fontID = boldFont;
		else
			fontID = ListFont;
		TblSetItemFont (table, row, priorityColumn, fontID);
		TblSetItemFont (table, row, descColumn, ListFont);
		TblSetItemFont (table, row, dueDateColumn, ListFont);
		TblSetItemFont (table, row, categoryColumn, ListFont);

//		TblSetRowUsable (table, row, false);
		}


	TblSetColumnUsable (table, completedColumn, true);
	TblSetColumnUsable (table, priorityColumn, ShowPriorities);
	TblSetColumnUsable (table, descColumn, true);
	TblSetColumnUsable (table, dueDateColumn, ShowDueDates);
	TblSetColumnUsable (table, categoryColumn, showCategories);
	
	TblSetColumnMasked (table, completedColumn, true);
	TblSetColumnMasked (table, priorityColumn, true);
	TblSetColumnMasked (table, descColumn, true);
	TblSetColumnMasked (table, dueDateColumn, true);
	TblSetColumnMasked (table, categoryColumn, true);
	
	
	// Set the spacing after the complete column.
	if (ShowPriorities)
		{
		TblSetColumnSpacing (table, completedColumn, 0);
		TblSetColumnSpacing (table, priorityColumn, spaceBeforeDesc);
		}
	else
		{
		TblSetColumnSpacing (table, completedColumn, spaceBeforeDesc);
		}	

	if (ShowDueDates && showCategories)
		{
		TblSetColumnSpacing (table, dueDateColumn, spaceBeforeCategory);
		}


	// Set the width of the priorities column.
	if (ShowPriorities)
		{
		width = ListViewGetColumnWidth (priorityColumn);
		TblSetColumnWidth (table, priorityColumn, width);
		}


	// Set the width of the due date column.
	if (ShowDueDates)
		{
		width = ListViewGetColumnWidth (dueDateColumn);
		TblSetColumnWidth (table, dueDateColumn, width);
		}


	// Set the width of the category column.
	if (showCategories)
		{
		width = ListViewGetColumnWidth (categoryColumn);
		TblSetColumnWidth (table, categoryColumn, width);
		}


	// Set the width of the description column.
	TblGetBounds (table, &r);
	width = r.extent.x;
	width -= TblGetColumnWidth (table, completedColumn) + 
				TblGetColumnSpacing (table, completedColumn);
	if (ShowPriorities)
		width -= TblGetColumnWidth (table, priorityColumn) + 
				   TblGetColumnSpacing (table, priorityColumn);
	if (ShowDueDates)
		width -= TblGetColumnWidth (table, dueDateColumn) + 
				   TblGetColumnSpacing (table, dueDateColumn);
	if (showCategories)
		width -= TblGetColumnWidth (table, categoryColumn) + 
				   TblGetColumnSpacing (table, categoryColumn);


	TblSetColumnWidth (table, descColumn, width);	


	// Set the callback routines that will load and save the 
	// description field.
//	TblSetLoadDataProcedure (table, descColumn, (TableLoadDataFuncPtr)ListViewGetDescription);
//	TblSetSaveDataProcedure (table, descColumn, (TableSaveDataFuncPtr)ListViewSaveDescription);

	// Set the callback routines that draws the various fields
	TblSetCustomDrawProcedure (table, dueDateColumn, ListViewDrawDueDate);
	TblSetCustomDrawProcedure (table, categoryColumn, ListViewDrawCategory);
	TblSetCustomDrawProcedure (table, descColumn, ListViewDrawDesc);


	// Set the label of the category trigger.
	ctl = GetObjectPtr (AgendaToDoCategoryTrigger);
	CategoryGetName (Agenda.todoDB, CurrentCategory, CategoryName);
	CategorySetTriggerLabel (ctl, CategoryName);
	AgendaRightAlignCategory (FrmGetActiveForm ());
}


/***********************************************************************
 *
 * FUNCTION:    AgendaViewFillTitle
 *
 * DESCRIPTION: 
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	5/28/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static void
AgendaViewFillTitle (
	FormPtr							frm )
{
#pragma unused (frm)

}


/***********************************************************************
 *
 * FUNCTION:    AgendaViewFillAppointmentsTitle
 *
 * DESCRIPTION: 
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	5/28/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static void
AgendaViewFillAppointmentsTitle (
	FormPtr							frm )
{
#pragma unused (frm)

}


/***********************************************************************
 *
 * FUNCTION:    AgendaViewFillAppointments
 *
 * DESCRIPTION: 
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	5/28/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static void
AgendaViewFillAppointments (
	FormType*						inFormP )
{
#pragma unused (inFormP)

	TableType*						tableP;
	RectangleType					tableBounds;
	UInt16							row;
	UInt16							lastRow;
	FontID							oldFont;
	UInt16							apptIndex;
	UInt16							apptCount;
	UInt16							rowHeight = 0;
	UInt16							displayableHeight = 0;
	UInt16							virtualHeight = 0;
	UInt16 							attr;
	Boolean 							masked;
	UInt16							maxIndex;

	// Get the height of the table and the width of the description column.
	tableP = GetObjectPtr (AgendaDatebookTable);
	TblGetBounds (tableP, &tableBounds);
	
	row = 0;
	lastRow = TblGetLastUsableRow (tableP);
	
	oldFont = FntSetFont (Agenda.apptFont);
	rowHeight = FntLineHeight ();
	FntSetFont (oldFont);
	
	apptCount = AgendaGetAppointmentCount();
	maxIndex = max (0, (Int16) apptCount - (Int16) lastRow - 1);
	apptIndex = min (Agenda.apptTopVisibleIndex, maxIndex);
	Agenda.apptTopVisibleIndex = apptIndex;
	
	// Associate each row with the appropriate appointment
	while ( (apptIndex < apptCount) && (row <= lastRow) )
		{
		// Also ensure that we're within our drawing area
		if ( displayableHeight + rowHeight <= tableBounds.extent.y )
			{
			MemHandle				apptsH;
			ApptInfoType*			apptsP;
			
			TblSetRowID (tableP, row, apptIndex);
			TblMarkRowInvalid (tableP, row);
			
			//Mask if appropriate
			apptsH = AgendaGetAppointments ();
			if (apptsH)
				{
				apptsP = MemHandleLock (apptsH);
				if (apptsP[apptIndex].recordNum >= kDavSentinel)
					masked = false;			// Db35: a birthday/CalDAV row is never private
				else
					{
					DmRecordInfo (ApptDB, apptsP[apptIndex].recordNum, &attr, NULL, NULL);
					masked = (((attr & dmRecAttrSecret) && PrivateRecordVisualStatus == maskPrivateRecords));
					}
				TblSetRowMasked(tableP,row,masked);

				MemPtrUnlock (apptsP);
				}
			
			apptIndex++;
			row++;
			displayableHeight += rowHeight;
			}
		else
			{
			break;
			}
		}
	
	if (apptCount == 0)
		{
		// Set the ID to a special value to trigger the display of the "no appointments" text
		TblSetRowID (tableP, 0, 0xffff);
		TblSetRowMasked (tableP, row, false);
		TblMarkRowInvalid (tableP, row);
		}
}


#pragma mark -
/***********************************************************************
 *
 * FUNCTION:    AgendaLoadAppointments
 *
 * DESCRIPTION: 
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	5/28/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static MemHandle
AgendaLoadAppointments (
	DateType							inDate )
{
	MemHandle						appointmentsH;
	UInt16							count;
	UInt16							nBday;
	UInt16							nDav;

	// Load the day's appointments from the database
	ApptGetAppointments (Agenda.apptDB, inDate, 1, &appointmentsH, &count);

	// Db35: append this day's Address35 birthdays/anniversaries as untimed rows.
	// Pass the TRUE full year (wide DateType stores year as full-firstYear), so the
	// age/ordinal is correct in any era.
	nBday = AddrBdayGet ((UInt16) (inDate.year + firstYear),
		inDate.month, inDate.day, gBdayLines, gBdayIndex, kMaxBday);
	if (nBday > 0)
		{
		UInt16 total = (UInt16) (count + nBday);
		Boolean ok = true;

		if (appointmentsH == NULL)
			appointmentsH = MemHandleNew ((UInt32) total * sizeof (ApptInfoType));
		else
			ok = (Boolean) (MemHandleResize (appointmentsH,
					(UInt32) total * sizeof (ApptInfoType)) == errNone);
		if (appointmentsH != NULL && ok)
			{
			ApptInfoPtr appts = MemHandleLock (appointmentsH);
			UInt16 i;
			for (i = 0; i < nBday; i++)
				{
				// untimed (whole TimeType == -1), sentinel record number
				MemSet (&appts[count + i].startTime, sizeof (TimeType), 0xFF);
				MemSet (&appts[count + i].endTime, sizeof (TimeType), 0xFF);
				appts[count + i].recordNum = (UInt16) (kBdaySentinel + i);
				}
			MemPtrUnlock (appts);
			count = total;
			}
		}

	// Db35: append this day's CalDAV Sync events - timed rows keep their real
	// start/end TimeType (so they sort into their correct chronological slot
	// among real appointments, same as any ApptDB row), all-day events are
	// untimed like birthdays above. Silently does nothing if CalDAV Sync was
	// never installed/synced (DateDAVGet's own contract). Result-cache (see
	// gDavCacheValid's own comment) - only re-runs DateDAVGet's full-database
	// scan when this exact day or DateBook35DAV.pdb itself has actually
	// changed since the last time this ran.
	{
	UInt32 modNum = DateDAVGetModNum ();
	if (! (gDavCacheValid &&
		gDavCacheDate.year == inDate.year &&
		gDavCacheDate.month == inDate.month &&
		gDavCacheDate.day == inDate.day &&
		gDavCacheModNum == modNum))
		{
		gDavCount = DateDAVGet ((UInt16) (inDate.year + firstYear),
			inDate.month, inDate.day, gDavLines, gDavStart, gDavEnd, NULL, NULL, NULL, kMaxDav);
		gDavCacheValid = true;
		gDavCacheDate = inDate;
		gDavCacheModNum = modNum;
		}
	}
	nDav = gDavCount;
	if (nDav > 0)
		{
		UInt16 total = (UInt16) (count + nDav);
		Boolean ok = true;

		if (appointmentsH == NULL)
			appointmentsH = MemHandleNew ((UInt32) total * sizeof (ApptInfoType));
		else
			ok = (Boolean) (MemHandleResize (appointmentsH,
					(UInt32) total * sizeof (ApptInfoType)) == errNone);
		if (appointmentsH != NULL && ok)
			{
			ApptInfoPtr appts = MemHandleLock (appointmentsH);
			UInt16 i;
			for (i = 0; i < nDav; i++)
				{
				appts[count + i].startTime = gDavStart[i];
				appts[count + i].endTime = gDavEnd[i];
				appts[count + i].recordNum = (UInt16) (kDavSentinel + i);
				}
			MemPtrUnlock (appts);
			count = total;
			}
		}

	// Replace the current appointments with our new data (frees any old data)
	AgendaSetAppointments (appointmentsH, count);

	return appointmentsH;
}


/***********************************************************************
 *
 * FUNCTION:    AgendaFreeAppointments
 *
 * DESCRIPTION: 
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	5/28/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static void
AgendaFreeAppointments ( )
{
	if ( Agenda.apptsH != NULL )
		{
		MemHandleFree (Agenda.apptsH);
		}
	
	Agenda.apptsH = NULL;
	Agenda.apptCount = 0;
}


/***********************************************************************
 *
 * FUNCTION:    AgendaGetAppointments
 *
 * DESCRIPTION: 
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	5/28/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static MemHandle
AgendaGetAppointments ( )
{
	return Agenda.apptsH;
}


/***********************************************************************
 *
 * FUNCTION:    AgendaSetAppointments
 *
 * DESCRIPTION: 
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	5/28/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static void
AgendaSetAppointments (
	const MemHandle				inAppointmentsH,
	UInt16							inAppointmentCount )
{
	AgendaFreeAppointments ();
	
	Agenda.apptsH = inAppointmentsH;
	Agenda.apptCount = inAppointmentCount;
}


/***********************************************************************
 *
 * FUNCTION:    AgendaGetAppointmentCount
 *
 * DESCRIPTION: 
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	5/28/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static UInt16
AgendaGetAppointmentCount ( )
{
	return Agenda.apptCount;
}


#pragma mark -
/***********************************************************************
 *
 * FUNCTION:    AgendaViewGetScrollableRect
 *
 * DESCRIPTION: Returns the area into which the agenda can be drawn.
 *					 This is the whole screen, except for the title and
 *					 control areas.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    outBoundsP	- Screen area for agenda contents
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	6/14/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static void
AgendaViewGetScrollableRect (
	FormType*						inFormP,
	RectangleType*					outBoundsP )
{
	FrmGetObjectBounds (inFormP, FrmGetObjectIndex(inFormP, AgendaScrollableArea), outBoundsP);
}


/***********************************************************************
 *
 * FUNCTION:    AgendaViewGetSeparatorHeight
 *
 * DESCRIPTION: Returns the height of the area between the datebook and
 *					 to do sections of the agenda view, into which the category
 *					 popup is drawn
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    outBoundsP	- Screen area for agenda contents
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	6/14/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static UInt16
AgendaViewGetSeparatorHeight (
	FormType*						inFormP )
{
	RectangleType					bounds;
	
	FrmGetObjectBounds (inFormP, FrmGetObjectIndex(inFormP, AgendaSeparatorDefaultArea), &bounds);
	
	return bounds.extent.y;
}


/***********************************************************************
 *
 * FUNCTION:    AgendaViewGetDatebookDefaultHeight
 *
 * DESCRIPTION: Returns the height of the area between the datebook and
 *					 to do sections of the agenda view, into which the category
 *					 popup is drawn
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    outBoundsP	- Screen area for agenda contents
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	6/14/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static UInt16
AgendaViewGetDatebookDefaultHeight (
	FormType*						inFormP )
{
	RectangleType					agendaBounds;
	RectangleType					separatorBounds;
	
	AgendaViewGetScrollableRect (inFormP, &agendaBounds);
	FrmGetObjectBounds (inFormP, FrmGetObjectIndex(inFormP, AgendaSeparatorDefaultArea),
								&separatorBounds);
	
	return separatorBounds.topLeft.y - agendaBounds.topLeft.y;
}


/***********************************************************************
 *
 * FUNCTION:    AgendaViewHandleEvent
 *
 * DESCRIPTION: This routine is the event handler for the Agenda View
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
 *			rbb	6/2/99	Added button for Agenda view
 *			rbb	11/15/99 Refresh clock, as needed, on nil events
 *			rbb	11/15/99	Enhanced usage of the Datebook hard key
 *
 ***********************************************************************/
SEC003 Boolean
AgendaViewHandleEvent (
	EventType*							event)
{
	FormPtr frm;
	DateType date;
	Boolean handled = false;
//	MonthPtr monthP;

	if (event->eType == keyDownEvent)
		{
		if (EvtKeydownIsVirtual(event))
			{
			// Datebook key pressed? Only the Datebook button will reach this
			//	point.
			if (TxtCharIsHardKey(event->data.keyDown.modifiers, event->data.keyDown.chr))
				{
				// If a date other than today is being viewed, or if the user powered up
				// by pressing the Datebook hard key, show today in the agenda view
				DateSecondsToDate (TimGetSeconds (), &date);
				if (DateToInt (date) != DateToInt (Date) ||
					 ((event->data.keyDown.modifiers & poweredOnKeyMask)))
					{
					AgendaViewChangeDate (FrmGetActiveForm (), date);
					}
				
				// otherwise, cycle through to the day view
				else
					FrmGotoForm (DayView);
					
				handled = true;
				}
				
			else if (event->data.keyDown.chr == vchrPageUp)
				{
				frm = FrmGetActiveForm ();
				DateAdjust (&Date, -1);
				AgendaViewChangeDate (frm, Date);
				handled = true;
				}	
			else if (event->data.keyDown.chr == vchrPageDown)
				{
				frm = FrmGetActiveForm ();
				DateAdjust (&Date, +1);
				AgendaViewChangeDate (frm, Date);
				handled = true;
				}
			}
		}


	else if (event->eType == ctlSelectEvent)
		{
		switch (event->data.ctlSelect.controlID)
			{
			case AgendaGoToButton:
			case AgendaCurrentDayButton:
				AgendaViewGoToDate ();
				handled = true;
				break;

			case AgendaDayViewButton:
				FrmGotoForm (DayView);
				handled = true;
				break;

			case AgendaWeekViewButton:
				FrmGotoForm (WeekView);
				handled = true;
				break;

			case AgendaMonthViewButton:
				FrmGotoForm (MonthView);
				handled = true;
				break;

			case AgendaToDoCategoryTrigger:
				ListViewSelectCategory ();
				handled = true;
				break;
			}
		}


	else if (event->eType == ctlRepeatEvent)
		{
		switch (event->data.ctlRepeat.controlID)
			{
			case AgendaPreviousDayButton:
				frm = FrmGetActiveForm ();
				DateAdjust (&Date, -1);
				AgendaViewChangeDate (frm, Date);
//				handled = true;
				break;

			case AgendaNextDayButton:
				frm = FrmGetActiveForm ();
				DateAdjust (&Date, +1);
				AgendaViewChangeDate (frm, Date);
//				handled = true;
				break;
			}
		}

	else if (event->eType == frmOpenEvent)
		{
		frm = FrmGetActiveForm ();
		AgendaViewInit (frm);
//		FrmDrawForm (frm);
		handled = true;
		}


	else if (event->eType == frmUpdateEvent)
		{
		FrmDrawForm (FrmGetActiveForm ());
		handled = true;		
		}

	else if (event->eType == frmCloseEvent)
		{
		// Undo AgendaExpandToDoColumns so the form tears down with its own arrays.
		frm = FrmGetActiveForm ();
		AgendaRestoreToDoColumns (FrmGetObjectPtr (frm,
			FrmGetObjectIndex (frm, AgendaToDoTable)));
		AgendaFreeAppointments ();
		DmCloseDatabase (Agenda.todoDB);
		}

	else if (event->eType == sclRepeatEvent)
		{
		Int16 delta = event->data.sclRepeat.newValue - event->data.sclRepeat.value;

		if (event->data.sclRepeat.scrollBarID == AgendaToDoScroller)
			{
			ListViewScroll (delta);
			}
		else if (event->data.sclRepeat.scrollBarID == AgendaDatebookScroller)
			{
			AgendaDayViewScroll (delta);
			}
		}

	else if (event->eType == tblEnterEvent)
		{
		if (event->data.tblEnter.tableID == AgendaDatebookTable)
			{
			UInt16 apptIndex = TblGetRowID (event->data.tblEnter.pTable, event->data.tblEnter.row);
			UInt16 recordNum = noAppointments;
			
			if (apptIndex != noAppointments)
				{
				// Get the record index of the next appointment
				MemHandle apptsH = AgendaGetAppointments ();
				if (apptsH)
					{
					ApptInfoType* apptsP = MemHandleLock (apptsH);
					recordNum = apptsP[apptIndex].recordNum;
					MemPtrUnlock (apptsP);
					}
				}

			if (recordNum >= kBdaySentinel && recordNum != noAppointments)
				{
				// Db35: a birthday/anniversary row links to its Address35
				// contact -- switch to Address35 on that record.
				AddrBdayGoTo (gBdayIndex[recordNum - kBdaySentinel]);
				}
			else if (recordNum >= kDavSentinel && recordNum < kBdaySentinel)
				{
				// Db35: a CalDAV Sync event row is read-only with no
				// separate app view to switch to -- just show its own
				// full text (DateDAVGet already built it to fit, so this
				// is never a truncated/lossy display).
				FrmCustomAlert (altDavInfo, gDavLines[recordNum - kDavSentinel], NULL, NULL);
				}
			else if (recordNum < kDavSentinel || recordNum == noAppointments)
				{
				// Db35: land the day view on the occurrence the agenda is showing
				// (the currently-selected agenda date), not "next from today". Stashed
				// in a Feature because the GoTo app-relaunch discards our globals.
				FtrSet (datebookCreator, agendaGotoDateFeature,
					(((UInt32)(UInt16) Date.year) << 16) |
					(((UInt32) Date.month) << 8) | (UInt32) Date.day);

				GoToAppointment (Agenda.apptDB, recordNum);
				}
			handled = true;
			}
		else if (event->data.tblEnter.tableID == AgendaToDoTable)
			{
			if (event->data.tblEnter.column != completedColumn)
				{
				UInt16 recordNum = TblGetRowID (event->data.tblEnter.pTable, event->data.tblEnter.row);
				LaunchToDoWithRecord (Agenda.todoDB, recordNum);
				handled = true;
				}
			}
		}
		

	// An item in the table has been selected.
	else if (event->eType == tblSelectEvent)
		{
//		DayViewItemSelected (event);
//		handled = true;
		if (event->data.tblEnter.tableID == AgendaToDoTable)
			{
			if (event->data.tblEnter.column == completedColumn)
				{
				Int16 on = TblGetItemInt (event->data.tblEnter.pTable,
										  event->data.tblEnter.row,
										  event->data.tblEnter.column);
				ListViewChangeCompleteStatus (event->data.tblEnter.row, on);
				}
			}
		}
	
	
	// Refresh the clock, as needed
	else if (event->eType == nilEvent)
		{
		UInt32 now = TimGetSeconds ();
		
		if ((now / minutesInSeconds) != (Agenda.timeSeconds / minutesInSeconds))
			{
			Agenda.timeSeconds = now;
			AgendaViewDrawTime (FrmGetActiveForm ());
			}
		}


	return (handled);
}


/***********************************************************************
 *
 * FUNCTION:    LaunchToDoWithRecord
 *
 * DESCRIPTION: Switch to the To Do application and display the given
 *				record. If there are any problems retrieving the record,
 *				launch the app without it.
 *
 * PARAMETERS:  inRecordNum  - a pointer to an EventType structure
 *
 * RETURNED:    true if the event was handled and should not be passed
 *              to a higher level handler.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	4/8/96	Initial Revision
 *			rbb	6/2/99	Added button for Agenda view
 *
 ***********************************************************************/
SEC003 void
LaunchToDoWithRecord (
	DmOpenRef					inDB,
	UInt16						inRecordNum )
{
	MemHandle					recordH;
	ToDoDBRecordType*			toDoRecP;
	UInt16						length;
	GoToParamsType*				gotoParamsP;
	UInt16						launchCmd = sysAppLaunchCmdNormalLaunch;
	MemPtr						launchParamsP = NULL;

	// If we're emulating, do nothing so that Gremlins can be run
	// without quitting when we ask to switch applications
#if EMULATION_LEVEL != EMULATION_NONE
	return;
#endif

	recordH = DmQueryRecord( inDB, inRecordNum);
	
	if (recordH)
		{
		toDoRecP = (ToDoDBRecordType*) MemHandleLock (recordH);
		length = StrLen (&toDoRecP->description);
		MemHandleUnlock (recordH);
		
		// Create the param block (system is responsible for disposal)
		gotoParamsP = MemPtrNew(sizeof(GoToParamsType));
		if (gotoParamsP)
			{
			// Fill it in
			MemPtrSetOwner (gotoParamsP, 0);
			gotoParamsP->recordNum		= inRecordNum;			
			gotoParamsP->matchPos		= length;				// put cursor at end of string
			gotoParamsP->matchFieldNum	= descSearchFieldNum;	// same for Datebook and ToDo
			gotoParamsP->searchStrLen	= 0;	// length of match (for Datebook, ignored by ToDo)
			gotoParamsP->matchCustom	= 0;	// length of match (for ToDo, ignored by Datebook)

			DmOpenDatabaseInfo(inDB, &gotoParamsP->dbID, NULL, NULL, &gotoParamsP->dbCardNo, NULL);
		
			launchCmd = sysAppLaunchCmdGoTo;
			launchParamsP = gotoParamsP;
			}
		else
			{
			ErrNonFatalDisplay ("Not enough memory to go to requested record");
			}
		}
	else
		{
		ErrDisplay ("Couldn't find the displayed ToDo record!!!");
		}

	// Db35: the agenda's ToDo section is served by ToDo35. If it isn't installed,
	// tell the user rather than trying to switch to a missing app.
	if (! ToDo35Installed ())
		{
		FrmAlert (ToDo35NotInstalledAlert);
		if (launchParamsP) MemPtrFree (launchParamsP);
		return;
		}

	AppLaunchWithCommand (toDo35Creator, launchCmd, launchParamsP);
}


/***********************************************************************
 *
 * FUNCTION:    GoToAppointment
 *
 * DESCRIPTION: Switch to the To Do application and display the given
 *				record. If there are any problems retrieving the record,
 *				launch the app without it.
 *
 * PARAMETERS:  inRecordNum  - a pointer to an EventType structure
 *
 * RETURNED:    true if the event was handled and should not be passed
 *              to a higher level handler.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	4/8/96	Initial Revision
 *			rbb	6/2/99	Added button for Agenda view
 *
 ***********************************************************************/
SEC003 void
GoToAppointment (
	DmOpenRef					inDB,
	UInt16						inRecordNum )
{
	MemHandle					recordH = NULL;
	ApptDBRecordType			apptRec;
	UInt16						length;
	GoToParamsType*				gotoParamsP;
	UInt16						launchCmd = sysAppLaunchCmdNormalLaunch;
	MemPtr						launchParamsP = NULL;
	Err							err;
	
	if (inRecordNum != noAppointments)
		{
		err = ApptGetRecord (inDB, inRecordNum, &apptRec, &recordH);
		
		if (recordH)
			{
			// recordH is locked by ApptGetRecord and referenced by apptRec
			length = StrLen (apptRec.description);
			MemHandleUnlock (recordH);
			
			// Create the param block (system is responsible for disposal)
			gotoParamsP = MemPtrNew(sizeof(GoToParamsType));
			if (gotoParamsP)
				{
				// Fill it in
				MemPtrSetOwner (gotoParamsP, 0);
				gotoParamsP->recordNum		= inRecordNum;			
				gotoParamsP->matchPos		= length;				// put cursor at end of string
				gotoParamsP->matchFieldNum	= descSearchFieldNum;	// same for Datebook and ToDo
				gotoParamsP->searchStrLen	= 0;	// length of match (for Datebook, ignored by ToDo)
				gotoParamsP->matchCustom	= 0;	// length of match (for ToDo, ignored by Datebook)

				DmOpenDatabaseInfo(inDB, &gotoParamsP->dbID, NULL, NULL, &gotoParamsP->dbCardNo, NULL);
				
				launchCmd = sysAppLaunchCmdGoTo;
				launchParamsP = gotoParamsP;

	#if EMULATION_LEVEL == EMULATION_NONE
				AppLaunchWithCommand (datebookCreator, launchCmd, launchParamsP);
	#else
				GoToItem (gotoParamsP, false);
				MemPtrFree (gotoParamsP);
	#endif
				
				goto Exit;
				}
			else
				{
				ErrNonFatalDisplay ("Not enough memory to go to requested record");
				}
			}
		else
			{
			ErrDisplay ("Couldn't find the displayed Datebook record!!!");
			}
		}
	
	// If there are no records displayed or if there was an error, jump to the Day view
	// without making a selection
	FrmGotoForm (DayView);
	
Exit:
	return;
}


/***********************************************************************
 *
 * FUNCTION:    AgendaViewHandleTblEnter
 *
 * DESCRIPTION: This routine is the event handler for the Agenda View
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
 *			rbb	6/2/99	Added button for Agenda view
 *
 ***********************************************************************/
SEC003 Boolean
AgendaViewHandleTblEnter (
	EventType*							event )
{
	Boolean							handled = false;
	
	if (event->data.tblEnter.tableID == AgendaDatebookTable)
		{
		FrmGotoForm (DayView);
		handled = true;
		}
	else if (event->data.tblEnter.tableID == AgendaToDoTable)
		{
		GoToParamsType*				gotoParamsP;
		UInt16						recordNum;
		MemHandle					recordH;
		ToDoDBRecordType*			toDoRecP;
		UInt16						length;
		Err							err;
		
		UInt16						launchCmd = sysAppLaunchCmdNormalLaunch;
		MemPtr						launchParamsP = NULL;
		
		// If we're emulating, do nothing so that Gremlins can be run
		// without quitting when we ask to switch applications
#if EMULATION_LEVEL != EMULATION_NONE
		goto Exit;
#endif

		if (Agenda.todoDB)
			{
			recordNum = TblGetRowID (event->data.tblEnter.pTable, event->data.tblEnter.row);
			
			if (recordNum != noTasks)
				{
				recordH = DmQueryRecord( Agenda.todoDB, recordNum);
				
				if (recordH)
					{
					toDoRecP = (ToDoDBRecordType*) MemHandleLock (recordH);
					length = StrLen (&toDoRecP->description);
					MemHandleUnlock (recordH);
					
					// Create the param block (system is responsible for disposal)
					gotoParamsP = MemPtrNew(sizeof(GoToParamsType));
					if (!gotoParamsP) {
						err = memErrNotEnoughSpace;
						ErrNonFatalDisplayIf(err, "");
						goto Exit;
						}
						
					// Fill it in
					MemPtrSetOwner (gotoParamsP, 0);
					gotoParamsP->searchStrLen	= 0;	// Ignored for ToDo
					gotoParamsP->recordNum		= TblGetRowID ( event->data.tblEnter.pTable,
																event->data.tblEnter.row);
					gotoParamsP->matchPos		= length;
					gotoParamsP->matchFieldNum	= descSearchFieldNum;
					gotoParamsP->matchCustom	= 0;	// For ToDo, this is the length of the match
				
					launchCmd = sysAppLaunchCmdGoTo;
					launchParamsP = &gotoParamsP;
					}
				else
					{
					err = dmErrCantFind;
					ErrDisplay ("Couldn't find the displayed ToDo record!!!");
					}
				}
			}
			
		AppLaunchWithCommand (toDo35Creator, launchCmd, launchParamsP);
		handled = true;
		}
	else
		{
		ErrDisplay ("Unexpected value in event->data.tblEnter.tableID");
		}
	
Exit:
	return handled;
}


#pragma mark -
/***********************************************************************
 *
 * FUNCTION:    AgendaLoadTasks
 *
 * DESCRIPTION: 
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	5/28/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static void
AgendaViewFillTasks (
	FormType*						inFormP )
{
#pragma unused (inFormP)

	UInt16							row;
	UInt16							recordNum;
	UInt32							uniqueID;
	TableType*						tableP;
	UInt16 							attr;
	Boolean 							masked;
	
	CHECK_DB_LEAKS (Agenda.todoDB);
	// Get the height of the table and the width of the description
	// column.
	tableP = GetObjectPtr (AgendaToDoTable);

	row = 0;
	recordNum = TopVisibleRecord;
	
	// Load records into the table.
	while (row < Agenda.todoLineCount)
		{
		// Get the next record in the current category.
		if ( !SeekRecord (Agenda.todoDB, &recordNum, 0, dmSeekForward,
								CurrentCategory, Date, ShowCompletedItems, ShowOnlyDueItems ))
			{
			ErrDisplay ("Record not found");
			break;
			}
		
		// Redraw the row if the unique id of the record does not match the unique id
		// stored in the row, or if the row has otherwise been invalidated
		DmRecordInfo (Agenda.todoDB, recordNum, NULL, &uniqueID, NULL);
		if ( (TblGetRowData (tableP, row) != uniqueID) || (TblRowInvalid (tableP, row)) )
			{
			ListInitTableRow (tableP, row, recordNum, Agenda.todoLineHeight);
			}

		else 
			{
			// If the contents of the item or its display characteristics have changed,
			// invalidate the item
			Boolean redraw = false;
			
			if (recordNum != TblGetRowID (tableP, row))
				{
				TblSetRowID (tableP, row, recordNum);
				redraw = true;
				}
			
			//Mask if appropriate
			DmRecordInfo (Agenda.todoDB, recordNum, &attr, NULL, NULL);
		   masked = (((attr & dmRecAttrSecret) && PrivateRecordVisualStatus == maskPrivateRecords));	
		   
		   if (masked != TblRowMasked (tableP, row))
		   	{
				TblSetRowMasked (tableP, row, masked);
				redraw = true;
				}
			
			if (redraw)
				{
				TblMarkRowInvalid (tableP, row);
				}
			}
			
		row++;
		recordNum++;
		}
	
	CHECK_DB_LEAKS (Agenda.todoDB);
}


/***********************************************************************
 *
 * FUNCTION:    SeekRecord
 *
 * DESCRIPTION: Given the index of a ToDo record, this routine scans 
 *              forwards or backwards for displayable ToDo records.           
 *
 * PARAMETERS:  indexP  - pointer to the index of a record to start from;
 *                        the index of the record sought is returned in
 *                        this parameter.
 *
 *              offset  - number of records to skip:   
 *                        	0 - seek from the current record to the
 *                             next display record, if the current record is
 *                             a display record, its index is retuned.
 *                         1 - seek foreward, skipping one displayable 
 *                             record
 *                        -1 - seek backwards, skipping one displayable 
 *                             record
 *                             
 *
 * RETURNED:    false is return if a displayable record was not found.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	4/11/95	Initial Revision
 *
 ***********************************************************************/
SEC003 static Boolean
SeekRecord (
	DmOpenRef						inDB,
	UInt16*							ioIndexP,
	UInt16							inOffset,
	Int16								inDirection,
	UInt16							inCategory,
	DateType							inDate,
	Boolean							inShowCompleted,
	Boolean							inShowDueOnly )
{
	MemHandle						recordH = NULL;
	ToDoDBRecordType*				toDoRec;
	Boolean							found = false;
	Err								err;

	ErrFatalDisplayIf ( (inOffset > 1), "Invalid offset");

	while (!found)
		{
		err = DmSeekRecordInCategory (inDB, ioIndexP, inOffset, inDirection, inCategory);
		if (err)
			{
			break;
			}
	
		if ( inShowCompleted && (! inShowDueOnly))
			{
			found = true;
			break;
			}
		
		if (inOffset == 0)
			{
			inOffset = 1;
			}

		recordH = DmQueryRecord (inDB, *ioIndexP);
		
		// Since we got the index from DmSeekRecordInCategory, the query should
		// always succeed
		ErrFatalDisplayIf (!recordH, "Couldn't query record");
		
		if (recordH)
			{
			toDoRec = (ToDoDBRecordPtr) MemHandleLock (recordH);
			
			if ( (inShowCompleted) || (! (toDoRec->priority & completeFlag)))
				{
				if (!inShowDueOnly
						|| (DateToInt (toDoRec->dueDate) == toDoNoDueDate)
						|| (DateToInt (toDoRec->dueDate) <= DateToInt (inDate)) )
					{
					found = true;
					}
				}
			
			MemHandleUnlock (recordH);	
			}
		}

	CHECK_DB_LEAKS (Agenda.todoDB);
	return (found);
}


SEC003 static UInt16
CountRecords (
	DmOpenRef						inDB,
	UInt16*							outIndices,
	UInt16							inIndexCount,
	UInt16							inCategory,
	DateType							inDate,
	Boolean							inShowCompleted,
	Boolean							inShowDueOnly )
{
	UInt16							recordNum = 0;
	UInt16							count = 0;
	
	while (SeekRecord (inDB, &recordNum, 0, dmSeekForward, inCategory, inDate,
								inShowCompleted, inShowDueOnly) )
		{
		if (count < inIndexCount)
			{
			outIndices[count] = recordNum;
			}
			
		++recordNum;
		++count;
		}
	
	return count;
}


/***********************************************************************
 *
 * FUNCTION:    ListInitTableRow
 *
 * DESCRIPTION: This routine initialize a row in the ToDo list.
 *
 * PARAMETERS:  table      - pointer to the table of ToDo items
 *              row        - row number (first row is zero)
 *              recordNum  - the index of the record display in the row
 *              rowHeight  - height of the row in pixels
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	5/1/95		Initial Revision
 *
 ***********************************************************************/
SEC003 static void ListInitTableRow (TablePtr table, UInt16 row, UInt16 recordNum, 
	Int16 rowHeight)
{
	UInt32							uniqueID;
	MemHandle						recordH;
	ToDoDBRecordType*				toDoRec;
	UInt16 							attr;
	Boolean 						masked;

	CHECK_DB_LEAKS (Agenda.todoDB);
	// Get a pointer to the ToDo record.
	recordH = DmQueryRecord( Agenda.todoDB, recordNum);
	toDoRec = (ToDoDBRecordType*) MemHandleLock (recordH);

	// Make the row usable.
	TblSetRowUsable (table, row, true);
	
	// Set the height of the row to the height of the description.
	TblSetRowHeight (table, row, rowHeight);
	
	// Store the record number as the row id.
	TblSetRowID (table, row, recordNum);
	
	// Store the unique id of the record in the table.
	DmRecordInfo (Agenda.todoDB, recordNum, NULL, &uniqueID, NULL);
	TblSetRowData (table, row, uniqueID);
	
	//Mask if appropriate
	DmRecordInfo (Agenda.todoDB, recordNum, &attr, NULL, NULL);
   	masked = (((attr & dmRecAttrSecret) && PrivateRecordVisualStatus == maskPrivateRecords));	
	TblSetRowMasked(table,row,masked);

	// Set the checkbox that indicates the completion status.
	TblSetItemInt (table, row, completedColumn, 
		(toDoRec->priority & completeFlag) == completeFlag);

	// Store the priority in the table.
	TblSetItemInt (table, row, priorityColumn, 
		toDoRec->priority & priorityOnly);
	
	// Store the due date in the table.
	TblSetItemInt (table, row, dueDateColumn, (*(Int16 *) &toDoRec->dueDate));

	// Mark the row invalid so that it will drawn when we call the 
	// draw routine.
	TblMarkRowInvalid (table, row);
	
	MemHandleUnlock (recordH);
	CHECK_DB_LEAKS (Agenda.todoDB);
}


/***********************************************************************
 *
 * FUNCTION:    ListViewGetColumnWidth
 *
 * DESCRIPTION: This routine returns the width of the specified 
 *              column.
 *
 * PARAMETERS:	 column - column of the list table
 *
 * RETURNED:	 width of the column in pixels
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	9/10/97	Initial Revision
 *
 ***********************************************************************/
SEC003 static UInt16 ListViewGetColumnWidth (UInt16 column)
{
	Char		chr;
	Char		dateBuffer [longDateStrLength];
	UInt16		width;
	FontID	curFont;
	Char*	dateStr;


	curFont = FntSetFont (Agenda.todoFont);


	if (column == priorityColumn)
		{
		if (Agenda.todoFont == stdFont)
			FntSetFont (boldFont);
		chr = '1';
		width = (FntCharWidth (chr) - 1) + 6;
		}

	else if (column == dueDateColumn)
		{
		EraDateToAscii (12, 31, 1997,	Agenda.dateFormat, dateBuffer);

		// Remove the (4-digit era) year, leaving month/day.
		dateStr = StripYearForColumn (dateBuffer, Agenda.dateFormat);

		width = FntCharsWidth (dateStr, StrLen (dateStr));

		// Get the width of the character that indicates the item is due.
		// Don't count the whitespace in the character.
		if (Agenda.todoFont == stdFont)
			FntSetFont (boldFont);
		chr = '!';
		width += FntCharWidth (chr) - 1;
		}

	// Size the category column such that is can display about five 
	// characters.
	else if (column == categoryColumn)
		{
		// fit the wider of "Business" / "Personal"; longer names get a "..." ellipsis
		UInt16 wB = FntCharsWidth ("Business", 8);
		UInt16 wP = FntCharsWidth ("Personal", 8);
		width = (wB > wP ? wB : wP) + 2;
		}

	FntSetFont (curFont);

	return (width);
}


/***********************************************************************
 *
 * FUNCTION:    ListViewGetDescription
 *
 * DESCRIPTION: This routine returns a pointer to the description field
 *              of a ToDo record.  This routine is called by the table 
 *              object as a callback routine when it wants to display or
 *              edit a ToDo description.
 *
 * PARAMETERS:  table  - pointer to the ToDo list table (TablePtr)
 *              row    - row of the table
 *              column - column of the table
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	2/21/95		Initial Revision
 *
 ***********************************************************************/
SEC003 static Err ListViewGetDescription (void* table, UInt16 row, UInt16 column,
	Boolean editable, MemHandle* textH, UInt16* textOffset, UInt16* textAllocSize,
	FieldPtr fld)
{
#pragma unused (column, editable)

	UInt16 recordNum;
	MemHandle recordH;
	FieldAttrType attr;
	ToDoDBRecordPtr toDoRec;
	
	// Get the record number that corresponds to the table item.
	// The record number is stored as the row id.
	recordNum = TblGetRowID (table, row);
	recordH = DmQueryRecord( Agenda.todoDB, recordNum);
	ErrFatalDisplayIf ((! recordH), "Record not found");
	
	toDoRec = (ToDoDBRecordPtr) MemHandleLock (recordH);

	*textOffset = &toDoRec->description - ((Char *) toDoRec);
	*textAllocSize = StrLen (&toDoRec->description) + 1;  // one for null terminator
	*textH = recordH;
	
	MemHandleUnlock (recordH);

	// Set the field to support auto-shift.
	if (fld)
		{
		FldGetAttributes (fld, &attr);
		attr.underlined = false;
		attr.editable = false;
		attr.singleLine = true;
		FldSetAttributes (fld, &attr);
		}


	return (0);
}


/***********************************************************************
 *
 * FUNCTION:    ListViewSaveDescription
 *
 * DESCRIPTION: This routine is called by the table object, as a callback  
 *              routine, when it wants to save a ToDo description.
 *              The description is edit in place (directly in the database 
 *              record),  so we don't need to save it here,  we do however
 *              want to capture the current edit state.
 *
 * PARAMETERS:  table  - pointer to the memo list table (TablePtr)
 *              row    - row of the table to draw
 *              column - column of the table to draw 
 *
 * RETURNED:    true if the table needs to be redrawn
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	2/21/95		Initial Revision
 *
 ***********************************************************************/
static Boolean		RecordDirty = false;			// true if a record has been modified
static UInt16		ListEditPosition = 0;		// position of the insertion point in the desc field
static UInt16		ListEditSelectionLength;	// length of the current selection.

SEC003 static Boolean ListViewSaveDescription (void* table, UInt16 row, UInt16 column)
{
#pragma unused (column)

	UInt16 recordNum;
	UInt16 selectStart;
	UInt16 selectEnd;
	Boolean dirty;
	FieldPtr fld;
	
	
	// Get the record number that corresponds to the table item to save.
	recordNum = TblGetRowID (table, row);

	// If the description has been modified mark the record dirty, any 
	// change make to the ToDo's description were written directly
	// to the ToDo record.
	fld = TblGetCurrentField (table);
	dirty = FldDirty (fld);
	if (dirty)
		DirtyRecord (Agenda.todoDB, recordNum);

	// Save the dirty state, we're need it if we auto-delete an empty record.
	RecordDirty = dirty;

	// Check if the top of the description is scroll off the top of the 
	// field, if it is then redraw the field.
	if (FldGetScrollPosition (fld))
		{
		FldSetSelection (fld, 0, 0);
		FldSetScrollPosition (fld, 0);
		ListEditPosition = 0;
		ListEditSelectionLength = 0;
		}

	// Save the insertion point position, and length of the selection.  
	// We'll need the insertion point position an selection length
	// if we put the table back into edit mode. 
	else
		{
		ListEditPosition = FldGetInsPtPosition (fld);
		
		FldGetSelection (fld, &selectStart, &selectEnd);
		ListEditSelectionLength = selectEnd - selectStart;
		if (ListEditSelectionLength)
			ListEditPosition = selectStart;
		}

	return (false);
}


/***********************************************************************
 *
 * FUNCTION:    ListViewDrawDueDate
 *
 * DESCRIPTION: This routine draws a ToDo items due date.
 *
 * PARAMETERS:	 table  - pointer to a table object
 *              row    - row the item is in
 *              column - column the item is in
 *              bounds - region to draw in
 *
 * RETURNED:	 nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	9/14/95	Initial Revision
 *
 ***********************************************************************/
SEC003 static void ListViewDrawDueDate (void* table, Int16 row, Int16 column, 
	RectanglePtr bounds)
{
#pragma unused (column)

	char dueChr;
	char dateBuffer [longDateStrLength];
	Char* dateStr;
	UInt16 dateStrLen;
	UInt16 dueChrWidth;
	Int16 drawX, drawY;
	FontID curFont;
	FontID fontID;
	DateType date;
	DateTimeType today;
	Int32 todayL, dateL;
	

	// Get the due date of the item being drawn straight from the record via the
	// row id -- the era DateType is 4 bytes, too wide for the table's 2-byte item
	// int (which drew garbage like "221/1").
	{
		MemHandle			ddRecH = DmQueryRecord (Agenda.todoDB, TblGetRowID ((TablePtr) table, row));
		ToDoDBRecordPtr	ddRec  = (ToDoDBRecordPtr) MemHandleLock (ddRecH);
		date = ddRec->dueDate;
		MemHandleUnlock (ddRecH);
	}


	// If there is no date draw a dash to indicate such.
	if ((UInt16) DateToInt (date) == 0xFFFF)
		{
		curFont = FntSetFont (stdFont);
		drawX = bounds->topLeft.x + ((bounds->extent.x - 5) >> 1);
		drawY = bounds->topLeft.y + ((FntLineHeight () + 1) / 2);
		WinDrawLine (drawX, drawY, drawX+5, drawY);		
		FntSetFont (curFont);
		return;
		}
	
	// Get the width of the character that indicates the item is due.  Don't
	// count the whitespace in the character.
	if (Agenda.todoFont == stdFont)
		fontID = boldFont;
	else
		fontID = Agenda.todoFont;
	curFont = FntSetFont (fontID);
	dueChr = '!';
	dueChrWidth = FntCharWidth (dueChr) - 1;

	FntSetFont (Agenda.todoFont);
	
	EraDateToAscii (date.month, date.day, date.year + firstYear,
					Agenda.dateFormat, dateBuffer);

	// Remove the (4-digit era) year, leaving month/day.
	dateStr = StripYearForColumn (dateBuffer, Agenda.dateFormat);


	// Draw the due date, right aligned.
	dateStrLen = StrLen (dateStr);
	drawX = bounds->topLeft.x + bounds->extent.x - dueChrWidth -
		FntCharsWidth (dateStr, dateStrLen);
	drawY = bounds->topLeft.y ;
	WinDrawChars (dateStr, dateStrLen, drawX, drawY);
	
	
	// If the date is on or before today draw an exclamation mark.
	TimSecondsToDateTime (TimGetSeconds(), &today);

	todayL = ( ((Int32) today.year) << 16) + 
				( ((Int32) today.month) << 8) + 
				  ((Int32) today.day);

	dateL = ( ((Int32) date.year + firstYear) << 16) + 
			  ( ((Int32) date.month) << 8) + 
				 ((Int32) date.day);
	
	if (dateL < todayL)
		{
		drawX = bounds->topLeft.x + bounds->extent.x - dueChrWidth;
		FntSetFont (fontID);
		WinDrawChars (&dueChr, 1, drawX, drawY);
		}

	FntSetFont (curFont);
}


/***********************************************************************
 *
 * FUNCTION:    ListViewDrawCategory
 *
 * DESCRIPTION: This routine draws a ToDo item's category name.
 *
 * PARAMETERS:	 table  - pointer to a table object
 *              row    - row the item is in
 *              column - column the item is in
 *              bounds - region to draw in
 *
 * RETURNED:	 nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	3/21/96	Initial Revision
 *
 ***********************************************************************/
SEC003 static void ListViewDrawCategory (void* table, Int16 row, Int16 column, 
	RectanglePtr bounds)
{
#pragma unused (column)

	Int16 width;
	Int16 length;
	UInt16 attr;
	UInt16 category;
	UInt16 recordNum;
	Boolean fits;
	Char categoryName [dmCategoryLength];
	FontID curFont;

	curFont = FntSetFont (Agenda.todoFont);

	// Get the category of the item in the specified row.
	recordNum = TblGetRowID (table, row);
	DmRecordInfo (Agenda.todoDB, recordNum, &attr, NULL, NULL);
	category = attr & dmRecAttrCategoryMask;

	// Draw the category name, truncated with a "..." ellipsis if wider than the
	// column (full name still shows in the popup).
	CategoryGetName (Agenda.todoDB, category, categoryName);
	WinGlueDrawTruncChars (categoryName, StrLen (categoryName),
		bounds->topLeft.x, bounds->topLeft.y, bounds->extent.x);

	FntSetFont (curFont);
}


/***********************************************************************
 *
 * FUNCTION:    ListViewDrawDesc
 *
 * DESCRIPTION: Draw the description of a task.  This
 *              routine is called by the table object as a callback 
 *              routine.
 *
 * PARAMETERS:  table  - pointer to the memo Day table (TablePtr)
 *              row    - row of the table to draw
 *              column - column of the table to draw
 *              bounds - region to draw in 
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb	8/30/99	Initial Revision
 *
 ***********************************************************************/
SEC003 static void
ListViewDrawDesc (
	void*								inTableP,
	Int16								inRow,
	Int16								inColumn,
	RectangleType*					inBoundsP )
{
#pragma unused (inColumn)
	UInt16							recordNum;
	MemHandle						recordH = NULL;
	ToDoDBRecordType*				todoRecP;
	FontID							curFont;
	Char*								tempP;
	UInt16							tempCount = 0;

	// Get a pointer to the ToDo record.
	recordNum = TblGetRowID (inTableP, inRow);
	recordH = DmQueryRecord( Agenda.todoDB, recordNum);
	
	ErrFatalDisplayIf (!recordH, "Missing record");
	
	if (recordH)
		{
		char			descBuf[64];			/* Db35: decrypted / masked copy */
		const Char*		drawP;

		todoRecP = (ToDoDBRecordType*) MemHandleLock (recordH);

		curFont = FntSetFont (Agenda.todoFont);

		// Db35: an encrypted ToDo35 record (priority bit 0x40) stores [descLen:2 BE][noteLen:2 BE]
		// [encDesc][encNote] after the priority byte -- decrypt the description (nonce uid,0) into a
		// local buffer, or mask "******" when Sec35 is locked.  Otherwise draw the plaintext in place.
		if (todoRecP->priority & encryptedFlag)
			{
			const UInt8*	b = (const UInt8*) &todoRecP->description;
			UInt16			descLen = (UInt16) (((UInt16) b[0] << 8) | b[1]);
			UInt32			uid = 0;
			UInt8			key[ENC_KEY_BYTES];
			if (descLen > 63) descLen = 63;
			DmRecordInfo (Agenda.todoDB, recordNum, NULL, &uid, NULL);
			if (EncGetKeyHop (key, 1000000000UL))
				{
				MemMove (descBuf, b + 4, descLen);
				if (descLen) EncCryptHop (key, (UInt8*) descBuf, descLen, uid, 0);
				descBuf[descLen] = '\0';
				MemSet (key, ENC_KEY_BYTES, 0);
				}
			else
				StrCopy (descBuf, "******");
			drawP = descBuf;
			}
		else
			drawP = &todoRecP->description;

		tempP = (Char*) drawP;
		while (*tempP && *tempP != linefeedChr)
			{
			++tempCount;
			++tempP;
			}
		WinDrawTruncChars (drawP, tempCount,
									inBoundsP->topLeft.x, inBoundsP->topLeft.y,
									inBoundsP->extent.x);
		FntSetFont (curFont);

		MemHandleUnlock (recordH);
		}
}


/***********************************************************************
 *
 * FUNCTION:    ListViewSelectCategory
 *
 * DESCRIPTION: This routine handles selection, creation and deletion of
 *              categories in the List View. 
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    The index of the new category.
 *
 *              The following global variables are modified:
 *							CurrentCategory
 *							ShowAllCategories
 *							CategoryName
 *
 * REVISION HISTORY:
 *			Name	Date			Description
 *			----	----			-----------
 *			art	03/10/95		Initial Revision
 *			rbb	04/14/99		Uses new ListViewDrawTable
 *			gap	08/13/99		Update to use new constants categoryHideEditCategory & categoryDefaultEditCategoryString.
 *
 ***********************************************************************/
SEC003 static UInt16 ListViewSelectCategory (void)
{
	FormPtr frm;
	UInt16 category;
	Boolean categoryEdited;
	UInt16 updateCode = updateCategoryChanged;
	
	// Process the category popup list.  
	category = CurrentCategory;

	frm = FrmGetActiveForm();
	categoryEdited = CategorySelect (Agenda.todoDB, frm, AgendaToDoCategoryTrigger,
  					    AgendaToDoCategoryList, true, &category, CategoryName, 1, categoryHideEditCategory);
	AgendaRightAlignCategory (frm);   // Db35: keep long names from clipping (see helper)

	// If the option for category column is set and we switched to/from "All",
	// the table will need to be rebuilt with/without the column
	if ( ShowCategories && (CurrentCategory != category) &&
			( (category == dmAllCategories) || (CurrentCategory == dmAllCategories) ))
		{
		updateCode = updateDisplayOptsChanged;
		}
		
	if (category == dmAllCategories)
		ShowAllCategories = true;
	else
		ShowAllCategories = false;
		
	if ( (categoryEdited) || (CurrentCategory != category) || ShowCategories)
		{
		ChangeCategory (category);
		
		Agenda.todoCount = CountRecords (Agenda.todoDB, &TopVisibleRecord, 1, CurrentCategory,
											Date, ShowCompletedItems, ShowOnlyDueItems);

		// Display the new category.
		ListViewDrawTable (updateCode);
		}

	return (category);
}


/***********************************************************************
 *
 * FUNCTION:    ListViewDrawTable
 *
 * DESCRIPTION: Updates the entire list view, such as when changing categories 
 *
 * PARAMETERS:  updateCode - indicates how (or whether) to rebuild the table
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			rbb		4/14/99		Initial Revision
 *
 ***********************************************************************/
SEC003 static void ListViewDrawTable (UInt16 updateCode)
{
	FormType*							formP;
	
	
	formP = FrmGetActiveForm ();
	
	// If the display options or font were changed, the attributes for each
	// cell need to be regenerated
	if ( (updateCode == updateDisplayOptsChanged) 
			|| (updateCode == updateFontChanged)
			|| (updateCode == updateItemMove) )
		{
		AgendaViewInitToDo (formP);
		TblMarkTableInvalid (GetObjectPtr (AgendaToDoTable));
		}
	
	// There are other valid update codes used by the ToDo app, but I'm
	// not expecting to see them here.
	ErrFatalDisplayIf (		(updateCode != updateDisplayOptsChanged)
								&& (updateCode != updateFontChanged)
								&& (updateCode != updateCategoryChanged)
								&& (updateCode != updateGoTo)
								&& (updateCode != updateItemMove)
							, "Unexpected update code"
							);
	
	AgendaViewRefreshLayoutTasks (formP);
		
	TblRedrawTable (GetObjectPtr (AgendaDatebookTable));
	AgendaViewDrawSeparator (formP);
	TblRedrawTable (GetObjectPtr (AgendaToDoTable));
}


/***********************************************************************
 *
 * FUNCTION:    ListViewScroll
 *
 * DESCRIPTION: This routine scrolls the list of ToDo items
 *              in the direction specified.
 *
 * PARAMETERS:  direction - winUp or dowm
 *              oneLine   - if true the list is scrolled by a single line,
 *                          if false the list is scrolled by a full screen.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art		2/21/95		Initial Revision
 *			rbb		4/14/99		Uses new ListViewDrawTable
 *
 ***********************************************************************/
SEC003 static void ListViewScroll (Int16 delta)
{
	TablePtr 			table;
	Int16					seekDirection;
	UInt16				count;
	Int16					scrollValue;
	Int16					scrollMin;
	Int16					scrollMax;
	Int16					pageSize;
	UInt16				recordNum;

	if (delta == 0)
		{
		return;
		}
	
	table = GetObjectPtr (AgendaToDoTable);
	TblReleaseFocus (table);
	
	seekDirection = ( delta > 0 ? dmSeekForward : dmSeekBackward );
	count = ( delta > 0 ? delta : -delta );
	
	SclGetScrollBar (GetObjectPtr (AgendaToDoScroller), &scrollValue, &scrollMin,
							&scrollMax, &pageSize);
							
	// Find the new top visible record
	recordNum = TopVisibleRecord;
	while (count)
		{
		if (!SeekRecord (Agenda.todoDB, &recordNum, 1, seekDirection,
						CurrentCategory, Date, ShowCompletedItems, ShowOnlyDueItems))
			{
			break;
			}
		
		TopVisibleRecord = recordNum;
		--count;
		}
	
	ListViewDrawTable(updateGoTo);
}


/***********************************************************************
 *
 * FUNCTION:    GetToDoNotePtr
 *
 * DESCRIPTION: This routine returns a pointer to the note field in a to 
 *              do record.
 *
 * PARAMETERS:  recordP - pointer to a ToDo record
 *
 * RETURNED:    pointer to a null-terminated note
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	4/15/95	Initial Revision
 *
 ***********************************************************************/
SEC003 Char* GetToDoNotePtr (ToDoDBRecordPtr recordP)
{
	return (&recordP->description + StrLen (&recordP->description) + 1);
}


/***********************************************************************
 *
 * FUNCTION:    ListViewCrossOutItem
 *
 * DESCRIPTION: This routine is called when a ToDo item is marked 
 *              complete.  If completed item are not display then
 *              we display an animation of a line being drawn through
 *              the ToDo item.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	3/21/96	Initial Revision
 *
 ***********************************************************************/
SEC003 static void ListViewCrossOutItem (Int16 row)
{
	UInt16 width;
	UInt16 length;
	UInt16 maxWidth;
	UInt16 recordNum;
	UInt16 charsToDraw;
	Int16 x, y;
	Int16 lineHeight;
	FontID curFont;
	Char* chars;
	TablePtr table;
	RectangleType r;
	RectangleType tableR;
	MemHandle recordH;
	ToDoDBRecordPtr toDoRec;

	table = GetObjectPtr (AgendaToDoTable);

	// Get a pointer to the ToDo record.
	recordNum = TblGetRowID (table, row);
	recordH = DmQueryRecord( Agenda.todoDB, recordNum);
	toDoRec = (ToDoDBRecordPtr) MemHandleLock (recordH);

	curFont = FntSetFont (Agenda.todoFont);
	lineHeight = FntLineHeight ();	

	TblGetBounds (table, &tableR);

	TblGetItemBounds (table, row, descColumn, &r);
	maxWidth = r.extent.x;
	
	// If the record has a note, leave space for the note indicator.
	if (*GetToDoNotePtr (toDoRec))
		maxWidth -= tableNoteIndicatorWidth;

	chars = &toDoRec->description;
	length = 0;

	y = r.topLeft.y + (Agenda.todoLineHeight >> 1);

	if (*chars)
		{
		// Get the number of character on each line.
		length = FldWordWrap (chars, maxWidth);
		charsToDraw = length;
		
		// Don't draw the linefeedChr character.
		if ((charsToDraw) && chars[charsToDraw-1] == linefeedChr)
			charsToDraw--;
	
		// Don't draw trailing spaces to tabs.
		while (charsToDraw && (chars[charsToDraw-1] == spaceChr || 
			chars[charsToDraw-1] == tabChr))
			charsToDraw--;
		
		// Draw a line over the character.
		width = FntLineWidth (chars, charsToDraw);
		x = r.topLeft.x;
		while (width)
			{
			WinDrawLine (x, y, x, y);
			x++;
			width--;
			}
		}

	MemHandleUnlock (recordH);

	FntSetFont (curFont);
}


/***********************************************************************
 *
 * FUNCTION:    ListViewChangeCompleteStatus
 *
 * DESCRIPTION: This routine is called when a ToDo item is marked 
 *              complete.  If completed items are not displayed
 *              (a preference setting),  this routine will remove the
 *              item from the list.
 *
 * PARAMETERS:  row      - row in the table
 *              complete - true if the item is marked complete
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	5/1/95	Initial Revision
 *
 ***********************************************************************/
SEC003 static void ListViewChangeCompleteStatus (Int16 row, UInt16 complete)
{
	UInt16 recordNum;
	UInt32 ticks;
	TablePtr table;
	DateType dueDate;
	DateTimeType today;
	UInt32 mode;
	DmOpenRef todoDB;
	
	// Open a read/write copy of the database (the rest of the agenda view
	// uses read-only)
	mode = (PrivateRecordVisualStatus == hidePrivateRecords) ?
					dmModeReadWrite : (dmModeReadWrite | dmModeShowSecret);
	ToDoGetDatabase (&todoDB, mode);

	table = GetObjectPtr (AgendaToDoTable);
	recordNum = TblGetRowID (table, row);
	
	// If completed item are not shown then display an animation 
	// of a line being drawn through the item.
	if (! ShowCompletedItems)
		{
		ticks = TimGetTicks ();
		ListViewCrossOutItem (row);
		}

	// Update the record to reflect the new completion status.
	ToDoChangeRecord (todoDB, &recordNum, toDoComplete, &complete);

	// Should the due date be changed to the completion date?
	if (complete && ChangeDueDate)
		{
		TimSecondsToDateTime (TimGetSeconds (), &today);
		dueDate.year = today.year - firstYear;
		dueDate.month = today.month;
		dueDate.day = today.day;
		ToDoChangeRecord (todoDB, &recordNum, toDoDueDate, &dueDate);
//		CurrentRecord = recordNum;
		}

	// Mark the record dirty.
	DirtyRecord (todoDB, recordNum);



	// If completed items are shown and the due date was changed, redraw the list
	if (ShowCompletedItems)
		{
		if (complete && ChangeDueDate)
			{
			AgendaViewSetTopTask ();
			ListViewDrawTable (updateItemMove);
			}
		}

	// If completed items are hidden, update the table.
	else
		{
		UInt16	distance = 0;
		Int16		seekDirection = dmSeekForward;
		Int16		scrollValue;
		Int16		scrollMin;
		Int16		scrollMax;
		Int16		pageSize;
		
		// Wait long enough for the user to see the cross-out animation
		while (TimGetTicks () - ticks < crossOutDelay)
			{
			}
		
		// Remove this record from the count of displayable records
		Agenda.todoCount -= 1;
		
		// Reconfigure the scroll bar for one less record
		SclGetScrollBar (GetObjectPtr (AgendaToDoScroller), &scrollValue, &scrollMin,
								&scrollMax, &pageSize);
		scrollMax = max (scrollMax - 1, 0);
		
		// If we're already at the bottom of the scroller, look backward to find
		// a new record for the top of the list
		if ((scrollValue >= scrollMax) && scrollValue != 0)
			{
			distance = 1;
			seekDirection = dmSeekBackward;
			scrollValue = scrollMax;
			}
		
		SclSetScrollBar (GetObjectPtr (AgendaToDoScroller), scrollValue, scrollMin,
								scrollMax, pageSize);
			
		// Find the new top visible record
		SeekRecord (todoDB, &TopVisibleRecord, distance, seekDirection,
							CurrentCategory, Date, ShowCompletedItems, ShowOnlyDueItems );
		
		// Refresh the appointment and task tables
		ListViewDrawTable (updateItemMove);
		}

	DmCloseDatabase (todoDB);
}


/***********************************************************************
 *
 * FUNCTION:    ChangeCategory
 *
 * DESCRIPTION: This routine updates the global variables that keep track
 *              of category information.  
 *
 * PARAMETERS:  category  - new category (index)
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	3/10/95	Initial Revision
 *
 ***********************************************************************/
SEC003 static void ChangeCategory (UInt16 category)
{
	CurrentCategory = category;
	TopVisibleRecord = 0;
}


#if 0
/************************************************************
 *
 *  FUNCTION: ToDoAppInfoInit
 *
 *  DESCRIPTION: Create an app info chunk if missing.  Set
 *		the strings to a default.
 *
 *  PARAMETERS: database pointer
 *
 *  RETURNS: 0 if successful, errorcode if not
 *
 *  CREATED: 1/20/95 
 *
 *  BY: Roger Flores
 *
 *************************************************************/
#define LocalizedAppInfoStr			1000

SEC003 Err	ToDoAppInfoInit(DmOpenRef dbP)
{
	UInt16 cardNo;
	UInt16 wordValue;
	MemHandle h;
	LocalID dbID;
	LocalID appInfoID;
	ToDoAppInfoPtr	nilP = 0;
	ToDoAppInfoPtr appInfoP;
	
	if (DmOpenDatabaseInfo(dbP, &dbID, NULL, NULL, &cardNo, NULL))
		return dmErrInvalidParam;

	if (DmDatabaseInfo(cardNo, dbID, NULL, NULL, NULL, NULL, NULL, NULL, 
		 NULL, &appInfoID, NULL, NULL, NULL))
		return dmErrInvalidParam;
	
	if (appInfoID == NULL)
		{
		h = DmNewHandle(dbP, sizeof (ToDoAppInfoType));
		if (! h) return dmErrMemError;

		appInfoID = MemHandleToLocalID (h);
		DmSetDatabaseInfo(cardNo, dbID, NULL, NULL, NULL, NULL, NULL, NULL, 
			NULL, &appInfoID, NULL, NULL, NULL);
		}
	
	appInfoP = MemLocalIDToLockedPtr(appInfoID, cardNo);

	// Clear the app info block.
	DmSet (appInfoP, 0, sizeof(ToDoAppInfoType), 0);

	// Initialize the categories.
	CategoryInitialize ((AppInfoPtr) appInfoP, LocalizedAppInfoStr);

	// I don't know what this field is used for.
	wordValue = 0xFFFF;
	DmWrite (appInfoP, (UInt32)&nilP->dirtyAppInfo, &wordValue,
		sizeof(appInfoP->dirtyAppInfo));

	// Initialize the sort order.
	DmSet (appInfoP, (UInt32)&nilP->sortOrder, sizeof(appInfoP->sortOrder), 
		soPriorityDueDate);

	MemPtrUnlock (appInfoP);

	return 0;
}
#endif


/***********************************************************************
 *
 * FUNCTION:    ToDoLoadPrefs
 *
 * DESCRIPTION: Read the preferences and handle previous and future
 *					 versions of the prefs.
 *
 * PARAMETERS:  Nothing.
 *
 * RETURNED:    Nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			BGT	01/08/98	Initial Revision
 *			kwk	06/23/99	Use glue code for default Note/ListFont if prefs
 *								don't provide that information.
 *
 ***********************************************************************/
 SEC003 void ToDoLoadPrefs(void)
 {
	ToDoPreferenceType prefs;
	UInt16 prefsSize;
	Int16 prefsVersion;

	// Read the preferences / saved-state information. If we get an older version of
	// the prefs, sync our new note font field with the original pref field.
	prefsSize = sizeof (ToDoPreferenceType);
	prefsVersion = PrefGetAppPreferences (toDo35Creator, todoPrefID, &prefs, &prefsSize, true);
	if (prefsVersion > toDoPrefsVersionNum) {
		prefsVersion = noPreferenceFound;
	}
	if (prefsVersion > noPreferenceFound)
		{
		if (prefsVersion < toDoPrefsVersionNum) {
			prefs.noteFont = prefs.v20NoteFont;
		}
		CurrentCategory = prefs.currentCategory;
		
		// Since the FontSelect call only lets the user pick the std, stdBold, and largeBold
		// fonts, remap the largeFont to largeBoldFont. DOLATER kwk - this code makes assumptions
		// about how the FontSelect call works, which might not be true in the future, especially
		// for other character encodings.
		if (prefs.noteFont == largeFont)
			NoteFont = largeBoldFont;
		else
			NoteFont = prefs.noteFont;
		
		ShowAllCategories = prefs.showAllCategories;
		ShowCompletedItems = prefs.showCompletedItems;
		ShowOnlyDueItems = prefs.showOnlyDueItems;
		ShowDueDates = prefs.showDueDates;
		ShowPriorities = prefs.showPriorities;
		ShowCategories = prefs.showCategories;
		ChangeDueDate = prefs.changeDueDate;
		SaveBackup = prefs.saveBackup;
		
		// Agenda view does not honor ToDo font preferences
		// Support transferal of preferences from the second version of the preferences.
		if (prefsVersion == toDoPrefsVersionNum)
			{
			ListFont = prefs.listFont;
			}
		else
			{
			ListFont = FntGlueGetDefaultFontID(defaultSystemFont);
			}
		}
	else
		{
		NoteFont = FntGlueGetDefaultFontID(defaultSystemFont);
		}
}





SEC003 static void
CheckDBLeaks (
	DmOpenRef						inDB )
{
	UInt8								highest;
	UInt32							count;
	UInt32							busy;

	DmGetDatabaseLockState (inDB, &highest, &count, &busy);
	ErrNonFatalDisplayIf (highest > 0, "Records left locked");
	ErrNonFatalDisplayIf (busy > 0, "Records left busy");
}

