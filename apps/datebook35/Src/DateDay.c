/******************************************************************************
 *
 * Copyright (c) 1995-1999 Palm Computing, Inc. or its subsidiaries.
 * All rights reserved.
 *
 * File: DateDay.c
 *
 * Description:
 *	  This is the Datebook application's main module.  This module
 *   starts the application, dispatches events, and stops
 *   the application.
 *
 * History:
 *		June 12, 1995	Created by Art Lamb
 *			Name		Date		Description
 *			----		----		-----------
 *			???		????		Initial Revision
 *			frigino	970909	Added alarmSoundUniqueRecID to DatebookPreferenceType
 *									to remember the alarm sound to play. Moved
 *									DatebookPreferenceType out of this file.
 *			grant		3/5/99	Removed dependece on MemDeref and MemoryPrv.h.
 *									DetailsH was a handle that was always left locked;
 *									replaced by a pointer DetailsP.
 *			rbb		4/9/99	Removed time bar and end-time for zero-duration appts
 *			rbb		4/22/99	Added snooze
 *			rbb		6/10/99	Removed obsoleted code that worked around
 *									single-segment linker limitation
 *			grant		6/28/99	New global - RepeatDetailsP.  When editing an event's details,
 *									there is one details info block that is pointed to by either
 *									DetailsP or RepeatDetailsP but not both.  When the "Details"
 *									form is active, then DetailsP is valid and RepeatDetailsP
 *									should be NULL.  And vice versa for the "Repeat" form.
 *			gap		8/27/99	Replaced call of ExceptionAlert with call to RangeDialog
 *									in DetailsApply().
 *			gap		9/8/99	Added current, current & future, or all occurrences support
 *									for addition/removal of notes.
 *
 *****************************************************************************/

#include <PalmOS.h>
#include <Graffiti.h>
#include <SelTime.h>
#include <SysEvtMgr.h>

#include "Datebook.h"
#include "datepick.h"   /* Db35: era-aware full-range SelectDay replacement */
#include "era.h"        /* Db35: real-era-year date display helpers */
#include "ToDoDB.h"     /* Db35: day-view ToDo rows (Td35 due-today + undated) */
#include "todoext.h"    /* Db35: per-ToDo time (extras DB) for timed day rows */
#include "enccore.h"    /* Db35: EncKeyPresent -- mask/copy-edit encrypted descriptions */
#include "a35lookup.h"  /* Db35: Phone Lookup answered by Address35 (ROM lookup fallback) */
#include "AddrBday.h"   /* Db35: day-view birthday/anniversary rows (Address35) */
#include "DateDAV.h"    /* Db35: day-view CalDAV Sync event rows */
#include "DateLocation.h" /* Db35: Details-dialog Location gadget (event location) */
#include "DateSpan.h"   /* Db35: Details-dialog Start:/End: multi-day span editing */

extern ECApptDBValidate (DmOpenRef dbP);


/***********************************************************************
 *
 *	Global variables, declarded in DateGlobals.c.  Because of a bug in
 *  the Metrowerks compiler, we must compile the globals separately with
 *	 PC-relative strings turned off.
 *
 ***********************************************************************/
extern	MemHandle				ApptsH;
extern	UInt16					NumAppts;
extern	MemHandle				ApptsOnlyH;
extern	UInt16					NumApptsOnly;

extern	UInt16					TopVisibleAppt;
extern privateRecordViewEnum	PrivateRecordVisualStatus;
extern	UInt16					PendingUpdate;						// code of pending day view update


// The following global variables are used to keep track of the edit
// state of the application.
extern 	UInt16					CurrentRecord;						// record being edited
extern 	Boolean					ItemSelected;						// true if a day view item is selected
extern 	UInt16					DayEditPosition;					// position of the insertion point in the desc field
extern	UInt16					DayEditSelectionLength;			// length of the current selection.
extern	Boolean					RecordDirty;						// true if a record has been modified


// The following global variables are only valid while editng the detail
// of an appointment.
extern	void*						DetailsP;
extern	DateType					RepeatEndDate;
extern	RepeatType				RepeatingEventType;
extern	UInt16					RepeatStartOfWeek;				// status of Repeat Dialog.

// This global variable is only valid while editing the repeat info of an appointment.
extern	void *				RepeatDetailsP;

/* Db35: set alongside CurrentRecord at a row-tap-to-Details call site to the
 * TAPPED ROW's index into the Day View's appts[] array; kDetailsApptUnknown
 * (the default at every OTHER CurrentRecord assignment -- menu Go-to,
 * keyboard nav, etc.) tells DetailsInit "no specific row, fall back to
 * Date". Needed because a repeating+spanning event can show TWO rows on the
 * same day (yesterday's occurrence continuing past midnight, and today's own
 * fresh occurrence) -- Date alone can't tell DetailsInit which one was
 * tapped, but appts[index].startTime can (a continuation row's clamped
 * start won't match the record's real per-occurrence startTime). */
#define kDetailsApptUnknown 0xFFFF
static UInt16 gDetailsApptIndex = kDetailsApptUnknown;

extern	UInt16				TimeBarColumns;					// Number of columns of time bars.

// The following structure is used by the details dialog to hold
// changes made to an appointment record.
typedef struct {
	Boolean					secret;
	UInt8						endDateOffset;	/* Db35: days after when.date the event ends
															 * (0 = same day) -- was `reserved` padding */
	ApptDateTimeType		when;
	RepeatInfoType 		repeat;		/* Db35: alarm field removed */
	MemHandle				descH;		/* Db35: staged Title field text -- see DetailsInit/Apply */
} DetailsType;

typedef DetailsType * DetailsPtr;


typedef enum {
	dateRangeNone,
	dateRangeCurrent,
	dateRangeCurrentAndFuture,
	dateRangeAll
} DateRangeType;


/***********************************************************************
 *
 *	Internal Functions
 *
 ***********************************************************************/

/* Db35: these forward declarations MUST carry the SEC001 segment attribute.
   Without it, a call made *before* the function's (SEC001-tagged) definition is
   compiled as a call into the main segment -- a3(main base)+offset -- and jumps
   to garbage (main+offset instead of sec001+offset). That was the New-Event
   crash: DayViewClearEditState (early) calls DayViewLayoutDay (late). */
static void DayViewLoadTable (void) SEC001;

static void DayViewLayoutDay (Boolean retieve) SEC001;


static void DayViewDrawTimeBars (void) SEC001;

static void DayViewInitRow (TablePtr table, UInt16 row, UInt16 apptIndex,
	Int16 rowHeight, UInt32 uniqueID, UInt16 iconsWidth, FontID fontID) SEC001;

static void DayViewUpdateDisplay (UInt16 updateCode) SEC001;

static Err SplitRepeatingEvent (UInt16* indexP) SEC001;

/* Db35: main segment, not SEC001 - sec001 has no headroom left. Safe to
 * relocate: only ever invoked indirectly via the FrmSetGadgetHandler
 * callback pointer below (RepeatInit), never called directly by name
 * elsewhere in this file, so the same-file "SEC001 calling back into main"
 * risk (see db35_seg.h) doesn't apply - indirect/pointer calls already work
 * cross-segment throughout this file (every *HandleEvent form handler is
 * registered and invoked the same way). */
static Boolean RepeatDescRectHandler(FormGadgetType *gadgetP, UInt16 cmd, void *paramP);

/* Db35: Details dialog's own Record menu (Duplicate Event / Beam Event) --
 * see DetailsHandleEvent's menuEvent case and DetailsDoCommand below. */
static void DetailsDuplicateEvent (void) SEC001;
static Boolean DetailsDoCommand (UInt16 command);


/* Db35: day-view ToDo35 rows ------------------------------------------------ *
 * ToDo35 items that are due on the shown day (plus undated ones, on today only)
 * are prepended to the appointment list as untimed sentinel rows.  Their
 * recordNum is (kDayTodoBase + slot); every day-view routine that reads a
 * recordNum out of ApptsH must treat IsDayTodo() rows like empty slots and NOT
 * hit ApptDB (they carry a ToDo index, not an appointment record). Tapping one
 * jumps to ToDo35 on that record, exactly like the Agenda view.
 * IsDayTodo() is upper-bounded at kDayBdayBase (below) so it does NOT also
 * match the separate birthday/anniversary sentinel range just above it. */
#define kDayTodoBase	0xF000u
#define kMaxDayTodo		24
#define IsDayTodo(rn)	((rn) >= kDayTodoBase && (rn) < kDayBdayBase)

/* Db35: day-view CalDAV Sync event rows -------------------------------------- *
 * Same timed/untimed split as the ToDo rows above (an all-day CalDAV event is
 * untimed like a birthday, but a real timed event needs to sort into its own
 * hour slot like a real appointment) - mirrors DayInjectTodos's own two-pass
 * structure, NOT DayInjectBdays's simpler always-untimed one. Placed in its
 * OWN range BELOW kDayTodoBase (rather than above kDayBdayBase, which
 * IsDayBday checks unbounded-above) so it can never collide with either
 * existing range. recordNum is (kDayDavBase + slot); tapping one shows a
 * read-only info alert (no separate app to switch to), exactly like the
 * Agenda view's own altDavInfo handling. */
#define kDayDavBase		0xE000u
#define kMaxDayDav		16
#define IsDayDav(rn)	((rn) >= kDayDavBase && (rn) < kDayTodoBase)

/* Db35: day-view Address35 birthday/anniversary rows ------------------------ *
 * Same idiom as the ToDo rows above, in a separate sentinel range (kDayTodoBase
 * + kMaxDayTodo leaves plenty of headroom before this starts) so the two never
 * collide. Always untimed (no per-occurrence time), so unlike ToDo rows there
 * is no timed/untimed split -- every one prepends to the top of the day.
 * Tapping one jumps to Address35 on that contact, exactly like the Agenda view. */
#define kDayBdayBase	0xF100u
#define kMaxDayBday		12
#define IsDayBday(rn)	((rn) >= kDayBdayBase && (rn) != emptySlot)

/* Db35: the 3 sentinel ranges above are contiguous - kDayDavBase..
 * (kDayTodoBase-1), kDayTodoBase..(kDayBdayBase-1), kDayBdayBase..emptySlot
 * exclusive - so "is this ANY sentinel row" (IsDayTodo||IsDayBday||IsDayDav,
 * used at several call sites) is algebraically just one range check, not
 * three separate 2-comparison ones; sec001 is hard-full (link error:
 * "region sec001res is full"), so every byte here matters - this is a real
 * fix, not a style preference. */
#define IsDaySentinelRow(rn)	((rn) >= kDayDavBase && (rn) != emptySlot)

/* Same algebra, minus the Dav sub-range: IsDayTodo||IsDayBday is exactly
 * [kDayTodoBase, emptySlot) minus emptySlot - one range check instead of
 * IsDayTodo's own 2-comparison range check plus IsDayBday's check (sec001
 * is hard-full; every byte matters). */
#define IsDayTodoOrBday(rn)	((rn) >= kDayTodoBase && (rn) != emptySlot)

extern void LaunchToDoWithRecord (DmOpenRef inDB, UInt16 inRecordNum) SEC003;

/* Db35 copy-edit (mirrors ToDo35 ListViewGetDescription): an ENCRYPTED appointment's
 * description is never edited in place (storage stays ciphertext).  For a textTableItem the
 * ROM keeps the load-proc's returned handle PER ROW for the whole table draw, so each DISPLAYED
 * row needs its OWN stable handle -- a single shared buffer gets freed out from under earlier
 * rows -> stale/blank cells + "Non-word-aligned handle".  gDayRowH[row] = the per-row decrypted
 * (or masked "******") DISPLAY copy; gDayEditH = the one active inline-EDIT copy, re-encrypted
 * into storage by DayViewSaveDescription.  Read-only masked when locked. */
#define kDayMaxRows 16
static MemHandle	gDayRowH[kDayMaxRows];
static MemHandle	gDayEditH = NULL;
static Int16		gDayEditRow = -1;

/* Db35: Note View's owned edit-staging copy -- same purpose as gDayEditH above,
 * for the separate Note field/form. See NoteViewLoadRecord/NoteViewSave. */
static MemHandle	gNoteEditH = NULL;

/* Db35: per-row "did this row's last load include an '@ location' suffix"
 * flag, compared each DayViewLoadTable pass so a location-only change (which
 * touches no field DayViewLoadTable's own uniqueID/startTime/font/iconsWidth
 * checks look at) still gets TblMarkRowInvalid'd -- see DayViewLoadTable.
 * Same idiom as the pre-existing TblRowMasked/masked check right above it. */
static Boolean		gDayRowHasLoc[kDayMaxRows];

/* Db35: per-row cheap fingerprint of the record's description TEXT, same
 * idiom/purpose as gDayRowHasLoc right above -- a TITLE-only edit (no time/
 * date/repeat/font/icon change) touches none of DayViewLoadTable's other
 * "does this row need reloading" checks either, so the row's cached display
 * copy (DaySetRowCopy) went stale until some UNRELATED change happened to
 * also touch the row (confirmed: visible as "title update is delayed until
 * I tap something else"). A full string compare against the record on every
 * load pass would also fix it but costs an extra decrypt per visible row;
 * this is a cheap sum-of-bytes checksum instead -- a collision would just
 * mean one missed refresh (rare, low severity), not a correctness bug. */
static UInt16		gDayRowDescHash[kDayMaxRows];

/* Db35: an appt row that has a note/repeat icon is a narrowTextTableItem, which the ROM treats as
 * a "custom item" and INVERTS entirely on selection (see DayViewSelectIconRect's comment).  The
 * original edited the record in place so the invert never showed; our in-RAM copy-edit lets it show
 * on the icon row.  So while a row is being edited we temporarily demote it to a plain textTableItem
 * (full-width field, no icon reserve -> clean cursor edit); its icon reappears the moment focus
 * leaves the row.  gDayEditStyleRow = that demoted row; gDayEditStyleIcons = its saved icon width. */
static Int16		gDayEditStyleRow = -1;
static Int16		gDayEditStyleIcons = 0;

/* Demote the tapped icon row to a plain text item so the ROM edits it cleanly (before grab-focus).
 * No-op for a row that has no icon (already a clean textTableItem). */
SEC001 static void DayEnterEditStyle (TablePtr table, Int16 row)
{
	UInt16 iw = TblGetItemInt (table, row, descColumn);
	if (iw != 0)								/* icon row: remember to restore its icon on unfocus */
		{
		gDayEditStyleRow = row;
		gDayEditStyleIcons = (Int16) iw;
		}
	// Set the edited row to a plain text item for EVERY row (icon and no-icon).  For an icon row
	// this demotes narrowTextTableItem -> textTableItem so the ROM doesn't invert it as a "custom
	// item".  For a no-icon row it's the same value, but the CALL itself is what makes the ROM
	// redraw the row with its proper ROUNDED selection highlight on the FIRST edit after launch --
	// without it the row keeps a raw square pen-invert until an icon row (which always calls this)
	// is tapped.  Verified in POSE: first-tap no-icon 8:00 now draws the rounded time + clean desc.
	TblSetItemStyle (table, row, descColumn, textTableItem);
	TblSetItemInt (table, row, descColumn, 0);
}

/* Restore the demoted row to its narrowTextTableItem style so its icon draws again (on unfocus). */
SEC001 static void DayRestoreEditStyle (TablePtr table)
{
	if (gDayEditStyleRow < 0) return;
	TblSetItemStyle (table, gDayEditStyleRow, descColumn, narrowTextTableItem);
	TblSetItemInt (table, gDayEditStyleRow, descColumn, gDayEditStyleIcons);
	gDayEditStyleRow = -1;
	gDayEditStyleIcons = 0;
}

/* Install `h` as row `row`'s display copy (freeing the row's previous one) and return it. */
SEC001 static MemHandle DaySetRowCopy (Int16 row, MemHandle h)
{
	if (row < 0 || row >= kDayMaxRows)
		return h;								/* out of range: won't happen (11-row table) */
	if (gDayRowH[row])
		MemHandleFree (gDayRowH[row]);
	gDayRowH[row] = h;
	return h;
}

/* Free every per-row display copy + any pending edit copy (day reload / app exit). */
SEC001 static void DayViewFreeRowCopies (void)
{
	Int16 i;
	for (i = 0; i < kDayMaxRows; i++)
		{
		if (gDayRowH[i]) { MemHandleFree (gDayRowH[i]); gDayRowH[i] = NULL; }
		gDayRowHasLoc[i] = false;
		gDayRowDescHash[i] = 0;
		}
	if (gDayEditH) { MemHandleFree (gDayEditH); gDayEditH = NULL; }
	gDayEditRow = -1;
}

/* Decrypt recordNum's description into a fresh handle (NULL on failure). ApptGetRecord returns
 * the decrypted text in its reused buffer locked once; we copy it out and unlock. */
SEC001 static MemHandle DayCopyDescPlain (UInt16 recordNum)
{
	ApptDBRecordType	apptRec;
	MemHandle			recordH, h = NULL;
	UInt16				dl;

	if (ApptGetRecord (ApptDB, recordNum, &apptRec, &recordH) != 0)
		return NULL;
	if (apptRec.description != NULL)
		{
		dl = (UInt16) StrLen (apptRec.description);
		h = MemHandleNew ((UInt32) dl + 1);
		if (h)
			{ char* p = (char*) MemHandleLock (h); MemMove (p, apptRec.description, dl); p[dl] = 0; MemHandleUnlock (h); }
		}
	MemHandleUnlock (recordH);
	return h;
}

/* Decrypt recordNum's note into a fresh handle (NULL on failure/no note). Same technique as
 * DayCopyDescPlain, for Note View's gNoteEditH staging copy. */
SEC002 static MemHandle DayCopyNotePlain (UInt16 recordNum)
{
	ApptDBRecordType	apptRec;
	MemHandle			recordH, h = NULL;
	UInt16				nl;

	if (ApptGetRecord (ApptDB, recordNum, &apptRec, &recordH) != 0)
		return NULL;
	if (apptRec.note != NULL)
		{
		nl = (UInt16) StrLen (apptRec.note);
		h = MemHandleNew ((UInt32) nl + 1);
		if (h)
			{ char* p = (char*) MemHandleLock (h); MemMove (p, apptRec.note, nl); p[nl] = 0; MemHandleUnlock (h); }
		}
	MemHandleUnlock (recordH);
	return h;
}

/* Build a fresh masked "******" handle. */
SEC001 static MemHandle DayMaskCopy (void)
{
	MemHandle h = MemHandleNew (7);
	if (h)
		{ char* p = (char*) MemHandleLock (h); MemSet (p, 6, '*'); p[6] = 0; MemHandleUnlock (h); }
	return h;
}

/* Db35 lock gate (mirror of ToDo35's TdLockedBounce).  When there ARE encrypted appointments
 * but we have no key (session locked), hand off to Sec35 to unlock; the edit/tap is blocked.
 * Returns true if it bounced (caller must abort); false = proceed (no passcode / unlocked). */
SEC001 static Boolean Sec35Installed (void)
{
	DmSearchStateType st;
	UInt16 card;
	LocalID dbID;
	return (Boolean)(DmGetNextDatabaseByTypeCreator (true, &st, sysFileTApplication,
		ENC_CREATOR, true, &card, &dbID) == errNone);
}

SEC001 static void SwitchToSec35 (void)
{
	DmSearchStateType st;
	UInt16 card;
	LocalID dbID;
	if (DmGetNextDatabaseByTypeCreator (true, &st, sysFileTApplication, ENC_CREATOR, true,
			&card, &dbID) != errNone)
		return;
	FtrSet (ENC_CREATOR, kSecReturnFtr, 'Db35');		/* return-to creator */
	SysUIAppSwitch (card, dbID, kSecLaunchUnlock, NULL);
}

/* Shared by DayLockedBounce and DayOpenDavDetails (their own real
 * appointment vs. CalDAV-row lock gates) - factored out so the actual
 * Sec35Installed+SwitchToSec35 call pair exists once, in sec001 (which has
 * headroom), rather than once per caller in whichever segment each of them
 * happens to live in. */
SEC001 static void DaySwitchToSec35IfInstalled (void)
{
	if (Sec35Installed ())
		SwitchToSec35 ();
}

SEC001 static Boolean DayLockedBounce (void)
{
	if (! EncKeyPresent () && ApptDbHasEncrypted (ApptDB))
		{
		DaySwitchToSec35IfInstalled ();
		return true;
		}
	return false;
}

static UInt16	gDayTodoCount = 0;
static UInt16	gDayTodoRec[kMaxDayTodo];		/* ToDo35 DB record index      */
static char		gDayTodoDesc[kMaxDayTodo][64];	/* cached description for draw  */
static UInt32	gDayTodoUID[kMaxDayTodo];		/* row uniqueID: 0xE0<<24|realUID
												 * -- distinct per ToDo record so the
												 * table re-inits the row when the
												 * day (hence the item) changes.    */
static Boolean	gDayTodoTimed[kMaxDayTodo];		/* Db35: this todo has a start time */
static TimeType	gDayTodoStart[kMaxDayTodo];		/* Db35: its start time (if timed)  */
static Boolean	gDayTodoNote[kMaxDayTodo];		/* Db35: has a note   -> right icon  */
static Boolean	gDayTodoRepeat[kMaxDayTodo];		/* Db35: repeats      -> right icon  */

static UInt16	gDayBdayCount = 0;
static char		gDayBdayLines[kMaxDayBday][kAddrBdayLineLen];	/* "Name - Nth Birthday" */
static UInt16	gDayBdayRec[kMaxDayBday];			/* Address35 record index of each */
static UInt32	gDayBdayUID[kMaxDayBday];			/* pseudo-uid for table row tracking */

static UInt16	gDayDavCount = 0;
static char		gDayDavLines[kMaxDayDav][kDateDavLineLen];	/* "[time ]Title[ - Location]" */
static UInt32	gDayDavHash[kMaxDayDav];			/* DateDAVGet's own per-event uid hash */
static UInt32	gDayDavUID[kMaxDayDav];				/* 0xC0<<24 | gDayDavHash - row uniqueID */
static Boolean	gDayDavTimed[kMaxDayDav];
static TimeType	gDayDavStart[kMaxDayDav];
static TimeType	gDayDavEnd[kMaxDayDav];
static Boolean	gDayDavRepeat[kMaxDayDav];			/* Db35: source VEVENT had an RRULE -> right icon */
static Boolean	gDayDavNote[kMaxDayDav];			/* Db35: synced notes field non-empty -> right icon */

/* Db35: "which CalDAV occurrence is Details/NoteView currently showing" -
 * the explicit (year,month,day,uidHash) key DetailsOpenForDav (Datebook.h,
 * called by ANY view's own CalDAV-row tap handler - Day's DayOpenDavDetails
 * and Week's WeekOpenDavDetails both route through it) sets, and
 * DetailsInitFromDav/NoteViewLoadRecordFromDav/NoteViewDavTitle/
 * DayDavRepeatOverrideLabel all read from - NOT any view's own internal
 * row/slot numbering (Day view's single-current-day model doesn't
 * generalize to Week's seven simultaneous days). gDavTargetIsRepeating is
 * filled in by DetailsInitFromDav's own DateDAVGetOne call (the only one of
 * these four that already fetches it) since a SECOND network-free lookup
 * just for this one flag would be wasteful. */
static UInt16	gDavTargetYear;
static UInt8	gDavTargetMonth;
static UInt8	gDavTargetDay;
static UInt32	gDavTargetUidHash;
static Boolean	gDavTargetIsRepeating;

/* Db35: event Location -- prefetched once per day-load (DayInjectLocationText),
 * indexed by apptIndex (NOT row -- covers every appointment for the day, not
 * just visible table rows), so DayViewGetDescription's per-draw lookup is a
 * plain array read, never a per-draw DB scan. Empty string = no location.
 * Capped well under the ROM's own apptMaxPerDay(100)+ToDo/birthday sentinel
 * max(~136) -- a day with more rows than this just skips the suffix for the
 * overflow ones (graceful, not a crash); realistic day loads are far fewer. */
#define kMaxDayLocRows	48
static char		gDayLocText[kMaxDayLocRows][kLocSummaryLen];

static void DayInjectTodos (void) SEC001;
static void DayLaunchTodo (UInt16 slot) SEC001;
static void DayInjectBdays (void) SEC001;
static void DayInjectCalDav (void) SEC002;
static Err DayDavGetDescHandle (Int16 row, UInt16 recordNum,
	MemHandle *textH, Int16 *textOffset, Int16 *textAllocSize) SEC002;
static MemHandle DayBuildLocSuffixHop (const char *desc, const char *locSummary);
static Int16 DayApptCalcUnmaskedHeight (UInt16 recordNum, Int16 apptIndex,
	ApptDBRecordType *apptRec, Int16 width, Int16 maxHeight, Int16 *iconsWidthOut) SEC002;
static Int16 DayDavIconsWidth (UInt16 slot, Boolean *hasNoteOut, Boolean *hasRepeatOut) SEC002;
static Int16 DayDavCalcHeight (UInt16 recordNum, Int16 width, Int16 maxHeight, Int16 *iconsWidthOut) SEC002;
static void DetailsInitFromDav (DetailsPtr details) SEC003;
static void NoteViewLoadRecordFromDav (FieldPtr fld);
static Boolean NoteViewDeleteNoteFromDav (void);
static void DayOpenDavDetails (UInt16 rn, Boolean toNote) SEC002;
/* Db35: forward-declared here (its real definition is much further down,
 * after DayInjectCalDav) purely so DayDavSelectIcons (which needs to call
 * it and is placed EARLIER in the file, right after DayOpenDavDetails) has
 * a visible prototype - C requires one before first use for a `static`
 * function. */
static void DayViewSelectIconRect (TablePtr table, Int16 row, RectangleType *r, Boolean selected) SEC001;
/* Db35: deliberately UNTAGGED (main segment, ~8KB free) - sec001/sec002/
 * sec003 are all at or past their own limits. Only calls same-file
 * functions (DayViewSelectIconRect/DayOpenDavDetails/DayDavIconsWidth,
 * sec001/sec002) and ROM traps - every one of those is a safe cross-
 * segment call from main regardless of which segment it ends up in. */
static Boolean DayDavSelectIcons (TablePtr table, Int16 row, UInt16 recordNum,
	RectangleType itemR, UInt16 iconsWidth, Int16 x, Int16 y);
static void DetailsSetDavLocationField (const char *location);
static Boolean DayDavRepeatOverrideLabel (void);
static void NoteViewDavTitle (char *buf, UInt16 bufCap);
static void DayInjectLocationText (void) SEC001;


/***********************************************************************
 *
 * FUNCTION:    DoSecurity
 *
 * DESCRIPTION: Bring up security dialog and then reopen database if
 *						necessary.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name		Date		Description
 *			----		----		-----------
 *			jaq		6/12/99		Initial Revision
 *
 ***********************************************************************/
SEC001 void DoSecurity (void)
{
	Boolean		wasHiding;
	UInt16 mode;

	wasHiding = (PrivateRecordVisualStatus == hidePrivateRecords);
	 
	PrivateRecordVisualStatus = SecSelectViewStatus();
	
	if (wasHiding ^ (PrivateRecordVisualStatus == hidePrivateRecords)) //xor on two logical values - mode to open DB has changed
		{
		// Close the application's data file.
		DmCloseDatabase (ApptDB);	
		
		mode = (PrivateRecordVisualStatus == hidePrivateRecords) ?
			dmModeReadWrite : (dmModeReadWrite | dmModeShowSecret);
		
		ApptDB = DmOpenDatabaseByTypeCreator(datebookDBType, datebookCreator, mode);
		ErrFatalDisplayIf(!ApptDB,"Can't reopen DB");
		}
		
	//For safety, simply reset the currentRecord
	//CurrentRecord = noRecordSelected;
}


/***********************************************************************
 *
 * FUNCTION:    SetDateToNextOccurrence
 *
 * DESCRIPTION: This routine set the "current date" global variable to
 *              the date that the specified record occurs on.  If the
 *              record is a repeating appointmnet, we set the date to 
 *              the next occurrence of the appointment.  If we are beyond
 *              the end date of the repeating appointment, we set the 
 *              date to the last occurrence of the event.
 *
 * PARAMETERS:  recordNum - index of appointment record 
 *
 * RETURNED:    true if successful, false if not.  It's posible that 
 *              a repeating event may have no displayable occurrences.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	8/24/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static Boolean SetDateToNextOccurrence (UInt16 recordNum)
{
	Boolean dateSet = true;
	MemHandle recordH;
	DateType today;
	DateTimeType dateTime;
	ApptDBRecordType apptRec;

	UInt32	agendaGoto;
	Boolean	fromAgenda;

	// Db35: did we get here by tapping an appointment in the agenda? If so, a
	// Feature holds the occurrence date the agenda was showing (survives the GoTo
	// app-relaunch that discards globals). Consume it either way.
	fromAgenda = (FtrGet (datebookCreator, agendaGotoDateFeature, &agendaGoto) == errNone);
	if (fromAgenda)
		FtrUnregister (datebookCreator, agendaGotoDateFeature);

	ApptGetRecord (ApptDB, recordNum, &apptRec, &recordH);

	if (! apptRec.repeat)
		Date = apptRec.when->date;

	// Db35: navigated from the agenda -- land on the occurrence the agenda was
	// showing (the selected agenda date), not "next occurrence from today".
	else if (fromAgenda)
		{
		Date.year  = (Int16) (agendaGoto >> 16);
		Date.month = (UInt8) ((agendaGoto >> 8) & 0xff);
		Date.day   = (UInt8) (agendaGoto & 0xff);
		}

	// If the appointment is a repeating event,  go to the date of the
	// next occurrence of the appointment.
	else
		{
		// Get today's date.
		TimSecondsToDateTime (TimGetSeconds (), &dateTime);
		today.year = dateTime.year - firstYear;
		today.month = dateTime.month;
		today.day = dateTime.day;

		Date = today;
		if ( ! ApptNextRepeat (&apptRec, &Date))
			{
			// If we are beyond the end date of the repeating event, display
			// the last occurrence of the event.
			Date = apptRec.repeat->repeatEndDate;
			while ( ! ApptNextRepeat (&apptRec, &Date))
				{
				// It posible that there are no occurences that are displayable
				// (ex: an expections is created for each occurrences),  if so
				// just go to today.
				if (DateToInt (Date) == DateToInt (apptRec.when->date))
					{
					ErrDisplay ("No displayable occurrences of event");
					Date = today;
					dateSet = false;
					break;
					}
				DateAdjust (&Date, -1);
				}
			}
		}
	MemHandleUnlock (recordH);

	return (dateSet);
}


/***********************************************************************
 *
 * FUNCTION:    GetTime
 *
 * DESCRIPTION: This routine selects the start and end time of an event.
 *
 * PARAMETERS:  startP  - passed:   current start time
 *                        returned: selected start time
 *              endtP   - passed:   current end time
 *                        returned: selected end time
 *              titleID - resource id of the title to display in the 
 *                        time picker dialog.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	10/24/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static Boolean GetTime (TimePtr startP, TimePtr endP, UInt16 titleStrID)
{
	Char* title;
	TimeType start, end;
	Boolean selected;
	Boolean untimed;
	DateTimeType dateTime;
	Int16	firstDisplayHour;
	
	
	// Get today's date, are we displaying today?
	TimSecondsToDateTime (TimGetSeconds (), &dateTime);
	if (Date.year == dateTime.year - firstYear &&
				Date.month == dateTime.month &&
				Date.day == dateTime.day)
		{
		firstDisplayHour = dateTime.hour;
		}
	else //not today
		{
		firstDisplayHour = DayStartHour;
		}

	// If the event is untimed, pass the default start time
	// and duration.
	if (TimeToInt (*startP) == apptNoTime)
		{
		untimed = true;
		start.hours = min (firstDisplayHour + 1, hoursPerDay-1);
		start.minutes = 0;
		end.hours = min (firstDisplayHour + 2, hoursPerDay);
		end.minutes = 0;
		}
	else
		{
		untimed = false;
		start = *startP;
		end = *endP;
		}
		
	
	title = MemHandleLock(DmGetResource (strRsc, titleStrID));

	selected = SelectTime (&start, &end, untimed, title, DayStartHour, DayEndHour,firstDisplayHour);

	MemPtrUnlock (title);
	
	if (selected)
		{
		*startP = start;
		*endP = end;
		}
	
	return (selected);
}


/***********************************************************************
 *
 * FUNCTION:    GetFocusObjectPtr
 *
 * DESCRIPTION: This routine returns a pointer to the field object, in 
 *              the current form, that has the focus.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    pointer to a field object or NULL of there is no fucus
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	2/21/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static FieldPtr GetFocusObjectPtr (void)
{
	FormPtr frm;
	UInt16 focus;
	FormObjectKind objType;
	
	frm = FrmGetActiveForm ();
	focus = FrmGetFocus (frm);
	if (focus == noFocus)
		return (NULL);
		
	objType = FrmGetObjectType (frm, focus);
	
	if (objType == frmFieldObj)
		return (FrmGetObjectPtr (frm, focus));
	
	else if (objType == frmTableObj)
		return (TblGetCurrentField (FrmGetObjectPtr (frm, focus)));
	
	return (NULL);
}


/***********************************************************************
 *
 * FUNCTION:    ShowObject
 *
 * DESCRIPTION: This routine set an object usable and draws the object if
 *              the form it is in is visible.
 *
 * PARAMETERS:  frm      - pointer to a form
 *              objectID - id of the object to set usable
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	2/21/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static void ShowObject (FormPtr frm, UInt16 objectID)
{
	FrmShowObject (frm, FrmGetObjectIndex (frm, objectID));
}


/***********************************************************************
 *
 * FUNCTION:    HideObject
 *
 * DESCRIPTION: This routine set an object not-usable and erases it
 *              if the form it is in is visible.
 *
 * PARAMETERS:  frm      - pointer to a form
 *              objectID - id of the object to set not usable
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	2/21/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static void HideObject (FormPtr frm, UInt16 objectID)
{
	FrmHideObject (frm, FrmGetObjectIndex (frm, objectID));
}


/***********************************************************************
 *
 * FUNCTION:    DeleteRecord
 *
 * DESCRIPTION: This routine deletes the specified appointment record.
 *
 * PARAMETERS:  record index
 *
 * RETURNED:    true if the record was deleted, false if the delete 
 *              operation was canceled.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	8/1/95	Initial Revision
 *			kcr	10/24/95	fixed bug with selection index of 'SaveBackup'
 *								object
 *			rbb	5/20/99	Added support for deleting "Current & Future"
 *
 ***********************************************************************/
SEC001 static Boolean DeleteRecord (UInt16 recordNum)
{
	Err err = errNone;
	UInt16 ctlIndex;
	UInt16 alertButton = RangeAllButton;
	FormPtr alert;
	MemHandle recordH;
	Boolean archive;
	Boolean exception = false;
	ApptDBRecordType apptRec;
	ApptDBRecordFlags apptFlags = {};
	RepeatInfoType repeatInfo;
	DateRangeType dateRange = dateRangeAll;

	// If we're changing a repeating appointmemt, check if all occurrences
	// are being changed, or if an exception is being created.
	ApptGetRecord (ApptDB, recordNum, &apptRec, &recordH);
	
	if (apptRec.repeat && ApptHasMultipleOccurences(&apptRec))
		{
		alert = FrmInitForm (RangeDialog);
		alertButton = FrmDoDialog (alert);
		FrmDeleteForm (alert);

		// If the alert was canceled don't delete the record.
		if (alertButton == RangeCancelButton)
			{
			dateRange = dateRangeNone;
			}

		else if (alertButton == RangeCurrentButton)
			{
			dateRange = dateRangeCurrent;
			exception = true;
			}
		
		else if (alertButton == RangeFutureButton)
			{
			// When editing the first instance, deleting "this and future"
			// will cause the event to disappear. Modify the dialog result
			// to trigger the confirmation dialog.
			if (DateToDays(apptRec.when->date) != DateToDays(Date))
				{
				dateRange = dateRangeCurrentAndFuture;
				repeatInfo = *(apptRec.repeat);
				}
			else
				{
				dateRange = dateRangeAll;
				}
			}
		}
	
	// Release the appointment record (apptRec is no longer valid)
	MemHandleUnlock (recordH);

	switch (dateRange)
		{
		// ignore dateRangeNone

		case dateRangeCurrent:
			// Add an exception to the current record.
			err = ApptAddException (ApptDB, &recordNum, Date);
			break;
		
		case dateRangeCurrentAndFuture:
			// Clip the meeting's end date to just before the displayed date
			repeatInfo.repeatEndDate = Date;
			DateAdjust (&repeatInfo.repeatEndDate, -1);

			// Fill part of apptRec with the new repeat info and mark the field
			// as modified. Only marked fields will be used by ApptChangeRecord.
			apptRec.repeat = &repeatInfo;
			apptFlags.repeat = true;
			
			err = ApptChangeRecord (ApptDB, &recordNum, &apptRec, apptFlags);
			break;
		
		case dateRangeAll:
			// Display an alert to comfirm the delete operation.
			alert = FrmInitForm (DeleteApptDialog);
		
			ctlIndex = FrmGetObjectIndex (alert, DeleteApptSaveBackup);
			FrmSetControlValue (alert, ctlIndex, SaveBackup);

			alertButton = FrmDoDialog (alert);
			archive = FrmGetControlValue (alert, ctlIndex);
			FrmDeleteForm (alert);

			if (alertButton == DeleteApptCancel)
				{
				dateRange = dateRangeNone;
				}
			else
				{
				SaveBackup = archive;
				
				// Delete or archive the record
				if (archive)
					DmArchiveRecord (ApptDB, recordNum);
				else
					DmDeleteRecord (ApptDB, recordNum);
				DmMoveRecord (ApptDB, recordNum, DmNumRecords (ApptDB));
				}
			break;
		}

	if (dateRange != dateRangeNone)
		{
		if (err)
			{
			FrmAlert (DeviceFullAlert);
			return (false);
			}

		// Db35: alarms removed -- no rescheduling.
		}

	return (dateRange != dateRangeNone);
}


/***********************************************************************
 *
 * FUNCTION:    CreateException
 *
 * DESCRIPTION: This routine creates an exception record.  It does not
 *              add the exception to the exceptions list of the original 
 *              record.
 *
 * PARAMETERS:  newRec - new record
 *              indexP - passed:   index of record being excepted
 *                       returned: index of exception record created
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	11/22/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static Err CreateException (ApptDBRecordPtr newRec, UInt16* indexP)
{
	Err err;
	UInt16 attr;
	
	// Get the secert setting of the record.
	DmRecordInfo (ApptDB, *indexP, &attr, NULL, NULL);	

	err = ApptNewRecord (ApptDB, newRec, indexP);				

	if (! err)
		{
		if (attr & dmRecAttrSecret)
			{
			DmRecordInfo (ApptDB, *indexP, &attr, NULL, NULL);	
			attr |= dmRecAttrSecret;
			DmSetRecordInfo (ApptDB, *indexP, &attr, NULL);
			}
		}
	return (err);
}


/***********************************************************************
 *
 * FUNCTION:    CreateNote
 *
 * DESCRIPTION: This routine adds an empty note to the current record.
 *
 * PARAMETERS:  prompt - if true, ask if an exception should be created.
 *
 * RETURNED:    true if the note was added, false if it was not.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	8/4/95	Initial Revision
 *
 ***********************************************************************/
/* Db35: deliberately UNTAGGED (main segment) - see
 * DetailsDeleteAppointment's own comment; same reasoning. Doesn't call
 * DateDAVGetOne so no same-segment-across-files risk regardless of segment. */
static Boolean CreateNote (Boolean prompt)
{
	Err err;
	FormPtr alert;
	UInt16 alertButton;
	Boolean exception = false;
	Boolean splitEvent = false;
	MemHandle recordH;
	ApptDateTimeType when;
	ApptDBRecordType apptRec;
	ApptDBRecordType newRec;
	ApptDBRecordFlags changedFields;
	
	
	if (CurrentRecord == noRecordSelected) return (false);
	// Db35: CalDAV items are read-only - CurrentRecord is a DAV sentinel,
	// not a real ApptDB index (ApptGetRecord below is undefined/crash on
	// it). Report "already has a note" (true) so the Note-button caller's
	// existing CreateNote(false)-then-FrmGotoForm(NewNoteView) path just
	// works unchanged - NoteViewLoadRecord's own DAV branch supplies the
	// actual (possibly empty) DESCRIPTION text.
	if (IsDayDav (CurrentRecord)) return (true);

	ApptGetRecord (ApptDB, CurrentRecord, &apptRec, &recordH);
	MemHandleUnlock (recordH);

	// If the record already has a note, exit
	if (apptRec.note) return (true);

	// If we're changing a repeating appointment, check if all occurrences
	// are being changed, current & future occurrences are being changed,
	// or if and exception is being created.
	if (apptRec.repeat && prompt)
		{
		alert = FrmInitForm (RangeDialog);
		alertButton = FrmDoDialog (alert);
		FrmDeleteForm (alert);
		
		if (alertButton == RangeCancelButton)
			return (false);
			
		else if (alertButton == RangeCurrentButton)
			exception = true;
			
		else if (alertButton == RangeFutureButton)
			splitEvent = true;
		}

	if (exception)
		{
		// Add an exception to the current record.
		err = ApptAddException (ApptDB, &CurrentRecord, Date);
		if (err) goto Exit;

		// Create a new record on the current day that contains
		// the same description, time, and alarm setting as the 
		// repeating event.
		ApptGetRecord (ApptDB, CurrentRecord, &apptRec, &recordH);

		when.startTime = apptRec.when->startTime;
		when.endTime = apptRec.when->endTime;
		when.date = Date;

		MemSet (&newRec, sizeof (newRec), 0);
		newRec.when = &when;
		newRec.description = apptRec.description;
		newRec.alarm = apptRec.alarm;
		newRec.note = "";
		newRec.endDateOffset = apptRec.endDateOffset;	/* Db35: preserve an
									 * existing span -- this fast path (direct tap on
									 * a Day View row's time column) only adjusts
									 * time, never sets/clears a span itself, but must
									 * not silently drop one that was already there. */

		err = CreateException (&newRec, &CurrentRecord);
		MemHandleUnlock (recordH);
		if (err) goto Exit;
		}
	else
		{
		// Clear all changed fields flags.
		MemSet (&changedFields, sizeof (changedFields), 0);
		
		if (splitEvent)
			{
			// Split off the previous occurrences of the event
			err = SplitRepeatingEvent (&CurrentRecord);
			if (err) goto Exit;

			// Set the new start date for the event
			when.date = Date;
			when.startTime = apptRec.when->startTime;
			when.endTime = apptRec.when->endTime;
			newRec.when = &when;
			changedFields.when = true;
			}
		
		// Add the note to the record.
		newRec.note = "";
		changedFields.note = true;
		err = ApptChangeRecord (ApptDB, &CurrentRecord, &newRec, changedFields);
		if (err) goto Exit;
		}
	
	return (true);
	
Exit:
	FrmAlert (DeviceFullAlert);
	return (false);
}


/***********************************************************************
 *
 * FUNCTION:    DeleteNote
 *
 * DESCRIPTION: This routine deletes the note field from an 
 *              appointment record.
 *
 * PARAMETERS:  exception  - true if a new record, an exception, should be
 *                           created.
 *					 splitEvent - true if only deleting note from current and 
 *									  future occurrences of the specified event.
 *
 * RETURNED:    true if the note was deleted.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	10/13/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static Boolean DeleteNote (Boolean exception, Boolean splitEvent)
{
	Err err;
	MemHandle recordH;
	ApptDBRecordType newRec;
	ApptDBRecordType apptRec;
	ApptDateTimeType when;
	ApptDBRecordFlags changedFields;
	
	
	if (exception)
		{
		// Add an exception to the current record.
		err = ApptAddException (ApptDB, &CurrentRecord, Date);
		if (err) goto Exit;		

		// Create a new record on the current day that contains
		// the same description, time, and alarm settings as the 
		// repeating event, but not the note.
		ApptGetRecord (ApptDB, CurrentRecord, &apptRec, &recordH);

		when.startTime = apptRec.when->startTime;
		when.endTime = apptRec.when->endTime;
		when.date = Date;

		MemSet (&newRec, sizeof (newRec), 0);
		newRec.when = &when;
		newRec.description = apptRec.description;
		newRec.alarm = apptRec.alarm;
		newRec.endDateOffset = apptRec.endDateOffset;	/* Db35: preserve an
									 * existing span -- see MoveEvent's identical fix. */

		err = CreateException (&newRec, &CurrentRecord);
		MemHandleUnlock (recordH);
		if (err) goto Exit;
		}

	else
		{
		// Clear all changed fields flags.
		MemSet (&changedFields, sizeof (changedFields), 0);
		ApptGetRecord (ApptDB, CurrentRecord, &apptRec, &recordH);
		
		if (splitEvent)
			{
			// Split off the previous occurrences of the event
			err = SplitRepeatingEvent (&CurrentRecord);
			if (err) goto Exit;

			// Set the new start date for the event
			when.date = Date;
			when.startTime = apptRec.when->startTime;
			when.endTime = apptRec.when->endTime;
			newRec.when = &when;
			changedFields.when = true;
			}

		// Remove the note from the record.
		newRec.note = NULL;
		changedFields.note = true;

		err = ApptChangeRecord (ApptDB, &CurrentRecord, &newRec, changedFields);
		if (err) goto Exit;
		}
	
	return (true);

Exit:
	FrmAlert (DeviceFullAlert);
	return (false);
}


/***********************************************************************
 *
 * FUNCTION:    SplitRepeatingEvent
 *
 * DESCRIPTION: This routine splits a repeating appointment into two
 *              repeating appointment.  The orginal record record is 
 *              copied and the end date of the new record is set to 
 *              yesterday or the day before the current date, which ever
 *              earlier.
 *
 * PARAMETERS:  indexP  - passed:   index of records split.
 *                        returned: index of records orginal record.
 *
 * RETURNED:    error code
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	4/25/96	Initial Revision
 *
 ***********************************************************************/
SEC001 static Err SplitRepeatingEvent (UInt16* indexP)
	{
	Err					err = 0;
	UInt16 					index;
	UInt32 				uniqueID;
	MemHandle				recordH;
	DateType				endDate;
	DateType				repeatDate;
//	DateTimeType		today;
 	RepeatInfoType		repeat;
	ApptDBRecordType	apptRec;


	// Get yesterday's date or the date of the day before the current date,
	// which ever earlier.
// 	TimSecondsToDateTime (TimGetSeconds(), &today);
//	endDate.year = today.year - firstYear;
//	endDate.month = today.month;
//	endDate.day = today.day;
//	if (DateToInt (endDate) > DateToInt (Date))
	endDate = Date;
	DateAdjust (&endDate, -1);


	ApptGetRecord (ApptDB, *indexP, &apptRec, &recordH);

	// Check for past occurrecnes of the event.
	repeatDate = apptRec.when->date;
	ApptNextRepeat (&apptRec, &repeatDate);
	if (DateToDays (repeatDate) <= DateToDays (endDate))
		{
		// Creating the new record may move a current record, so get its 
		// unique id so we can find it after the record is created.
		DmRecordInfo (ApptDB, *indexP, NULL, &uniqueID, NULL); 

		repeat = *apptRec.repeat;
		repeat.repeatEndDate = endDate;
		apptRec.repeat = &repeat;
		
		err = ApptNewRecord (ApptDB, &apptRec, &index);				
		DmFindRecordByID (ApptDB, uniqueID, indexP);
		}

	MemHandleUnlock (recordH);
	
	return (err);
	}


/***********************************************************************
 *
 * FUNCTION:    MoveEvent
 *
 * DESCRIPTION:  This routine changes the date and / or time of the 
 *               specified event
 *
 * PARAMETERS:  recordNumP - 
 *              startTime  -
 *              startTime  - 
 *					 date       -
 *					 splitEvent -
 *					 moved      - (returned) true if moved
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date			Description
 *			----	----			-----------
 *			art	4/01/96		Initial Revision
 *			gap	9/08/99		Add support for moving only current, current & future,
 *									or all occurrences of the event.
 *			gap	9/15/99		Clean up Current/Future/All handling for weekview.
 *
 ***********************************************************************/
 SEC001 Err MoveEvent (UInt16* recordNumP, TimeType startTime, TimeType endTime,
 		DateType startDate, Boolean timeChangeOnly, Boolean* moved)
	{

	Err					err;
	UInt16				id;
	UInt16				dayOfWeek;
	UInt16				alertButton;
	Int32					adjust;
	FormPtr				alert;
	Boolean				exception 	= false;
	Boolean				splitEvent 	= false;
	Boolean				applyToAll 	= false;
	MemHandle			recordH;
	RepeatInfoType		repeat;
	ApptDBRecordType	newRec;
	ApptDBRecordType	apptRec;
	ApptDateTimeType	when;
	ApptDBRecordFlags	changedFields;
	DateType 			date = startDate;

	ApptGetRecord (ApptDB, *recordNumP, &apptRec, &recordH);
	MemHandleUnlock (recordH);

	*moved = false;
	
	// If we're changing a repeating appointmemt, check if the changes will
	// be applied to:
	//  - all occurrences of the event
	//  - the current and all future occurrences of the event
	//  - only the current event  (create an expection)
	if (apptRec.repeat)
		{
		alert = FrmInitForm (RangeDialog);
		alertButton = FrmDoDialog (alert);
		FrmDeleteForm (alert);

		// If the alert was canceled don't apply any of the changes.
		if (alertButton == RangeCancelButton)
			return (0);

		// If the change is to be applied to the selected occurrence only
		// an exception will be created and the specified date and time   
		// will be applied.
		else if (alertButton == RangeCurrentButton)
			exception = true;
			
		// If the change is to be applied to the current and future
		// events, the current record will need to be split, and the
		// specified date and time will be applied.
		else if (alertButton == RangeFutureButton)
			splitEvent = true;

		// If the change is to be applied to the all occurrences of the event
		// the code below will do one of two things.  If the user only changed 
		// the time of the event, the original start date will be maintained and only
		// the time change will be applied.  If the user changes both the time and date,
		// the specified date and time will be applied.
		else if (alertButton == RangeAllButton)
			{
			applyToAll = true;
			
			if (timeChangeOnly)
				date = apptRec.when->date;
			}
		}

	// Add an exception to the current record,  and create a new record
	// at the new time.
	if (exception)
		{
		err = ApptAddException (ApptDB, recordNumP, Date);
		if (err) goto Exit;
		
		ApptGetRecord (ApptDB, *recordNumP, &apptRec, &recordH);

		when.startTime = startTime;
		when.endTime = endTime;
		when.date = date;

		MemSet (&newRec, sizeof (newRec), 0);
		newRec.description = apptRec.description;
		newRec.note = apptRec.note;
		newRec.when = &when;
		newRec.alarm = apptRec.alarm;
		newRec.endDateOffset = apptRec.endDateOffset;	/* Db35: preserve an
									 * existing span -- see CreateNote's identical fix.
									 * This exception branch is MoveEvent's own (direct
									 * tap on a Day View row's time column). */

		err = CreateException (&newRec, recordNumP);
		MemHandleUnlock (recordH);
		if (err) goto Exit;
		}

	// Change the time of the current record.
	else	
		{
		MemSet (&changedFields, sizeof (changedFields), 0);
		MemSet (&newRec, sizeof (newRec), 0);

		if (apptRec.repeat)
			{
			if (splitEvent)
				{
				err = SplitRepeatingEvent (recordNumP);
				if (err) goto Exit;
				}

			// When changing the entire range of event, reset the start date by calculating
			// the number of days the user moved the event and apply the delta to the event's
			// start date, maintain the duration and clear the exceptions list.
			repeat = *apptRec.repeat;
			newRec.repeat = &repeat;

			// Maintain the duration of the event.
			if ((DateToInt (apptRec.repeat->repeatEndDate) != apptNoEndDate) && !(splitEvent))
				{
				adjust = (Int32) DateToDays (date) -
							(Int32) DateToDays (apptRec.when->date);

				DateAdjust (&repeat.repeatEndDate, adjust);

				changedFields.repeat = true;
				}

			// If the repeat type is weekly and the start date of the event
			// has been changed,  update the 'repeat on' field, which contains
			// the days of the week the event repeats on, such that the 
			// event occurs on the start date.
			if (apptRec.repeat->repeatType == repeatWeekly)
				{
				dayOfWeek = DayOfWeek (apptRec.when->date.month, 
					apptRec.when->date.day,
					apptRec.when->date.year+firstYear);
				repeat.repeatOn &= ~(1 << dayOfWeek);
				
				dayOfWeek = DayOfWeek (date.month, 
					date.day,
					date.year+firstYear);
				repeat.repeatOn |= (1 << dayOfWeek);

				changedFields.repeat = true;
				}

			// If the repeat type is monthly by day, get the day of the month (ex:
			// first Friday) of the start date of the event.
			else if (apptRec.repeat->repeatType == repeatMonthlyByDay)
				{
				repeat.repeatOn = DayOfMonth (date.month, date.day, date.year + firstYear);
				changedFields.repeat = true;

				// If we're in the fourth week, and the fourth week is also the last
				// week,  ask the user which week the event repeats in (fourth or last).
				if ( ((repeat.repeatOn / daysInWeek) == 3) &&
						(date.day + daysInWeek > DaysInMonth (date.month, date.year + firstYear)))
					{
					alert = FrmInitForm (MonthlyRepeatDialog);
					
					// Set the 4th / last push button.
					if (apptRec.repeat->repeatOn > dom4thSat)
						id = MonthlyRepeatLastButton;
					else
						id = MonthlyRepeatFourthButton;
					FrmSetControlGroupSelection (alert, MonthlyRepeatWeekGroup,id);

					alertButton = FrmDoDialog (alert);

					if (FrmGetObjectIndex (alert, MonthlyRepeatLastButton) ==
						 FrmGetControlGroupSelection (alert, MonthlyRepeatWeekGroup))
						repeat.repeatOn += daysInWeek;

					FrmDeleteForm (alert);

					if (alertButton == MonthlyRepeatCancel)
						return (0);
					}
				}

			// If the record is a repeating appointment, and the start date has changed, 
			// then clear the exceptions list.
			if ( !(applyToAll && timeChangeOnly) )
				{
				newRec.exceptions = NULL;
				changedFields.exceptions = true;
				}
			}
			
		when.date = date;
		when.startTime = startTime;
		when.endTime = endTime;
		newRec.when = &when;
		changedFields.when = true;

		err = ApptChangeRecord (ApptDB, recordNumP, &newRec, changedFields);
		if (err) goto Exit;
		}

	// Db35: alarms removed -- no rescheduling.

	*moved = true;
	return (0);
	

Exit:
	FrmAlert (DeviceFullAlert);
	return (err);
	}


/***********************************************************************
 *
 * FUNCTION:    PurgeRecords
 *
 * DESCRIPTION: This routine deletes appointments that are before the 
 *              user specified date.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    true if the current view may need to be redrawn.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	8/1/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static Boolean PurgeRecords (void)
{
	Int32 adjust;
	UInt16 index;
	UInt16 ctlIndex;
	UInt16 numRecord;
	UInt16 buttonHit;
	UInt16 rangeItem;
	ListPtr lst;
	FormPtr alert;
	MemHandle recordH;
	Boolean purge;
	Boolean archive = false;
	DateType purgeDate;
	DateTimeType dateTime;
	ApptDBRecordType apptRec;

	// Display an alert to comfirm the operation.
	alert = FrmInitForm (PurgeDialog);

	ctlIndex = FrmGetObjectIndex (alert, PurgeSaveBackup);
	FrmSetControlValue (alert, ctlIndex, SaveBackup);

	buttonHit = FrmDoDialog (alert);

	archive = FrmGetControlValue (alert, ctlIndex);

	lst = FrmGetObjectPtr (alert, FrmGetObjectIndex (alert, PurgeRangeList));
	rangeItem = LstGetSelection (lst);

	FrmDeleteForm (alert);

	if (buttonHit == PurgeCancel)
		return (false);

	SaveBackup = archive;


	// Compute the purge date.
	TimSecondsToDateTime (TimGetSeconds (), &dateTime);
	
	// One, two, or three weeks
	if (rangeItem < 3)
		adjust = ((rangeItem + 1) * daysInWeek);

	// One month
	else if (dateTime.month > january)
		adjust = DaysInMonth (dateTime.month-1, dateTime.year);
	else
		adjust = DaysInMonth (december, dateTime.year-1);
		
	purgeDate.year = dateTime.year - firstYear;
	purgeDate.month = dateTime.month;
	purgeDate.day = dateTime.day;
	DateAdjust (&purgeDate, -adjust);
	

	// Delete records.
	numRecord = DmNumRecords (ApptDB);
	if (! numRecord) return (false);
	
	for (index = numRecord-1; (Int16) index >= 0; index--)
		{
		recordH = DmGetRecord (ApptDB, index);
		if (recordH == 0) continue;
		
		ApptGetRecord (ApptDB, index, &apptRec, &recordH);
		if (apptRec.repeat)
			purge = (DateToInt(apptRec.repeat->repeatEndDate) <= DateToInt(purgeDate));
		else
			purge = (DateToInt(apptRec.when->date) <= DateToInt(purgeDate));
		MemHandleUnlock (recordH);

		DmReleaseRecord (ApptDB, index, false);
		
		if (purge)
			{
			if (archive)
				DmArchiveRecord (ApptDB, index);
			else
				DmDeleteRecord (ApptDB, index);
				
			// Move deleted record to the end of the index so that the 
			// quick sort routine will work.
			DmMoveRecord (ApptDB, index, numRecord);
			}
		}
		
	// Db35: alarms removed -- no rescheduling.

	return (true);
}


/***********************************************************************
 *
 * FUNCTION:    DatebookDayOfWeek
 *
 * DESCRIPTION: This routine returns the day-of-the-week, adjusted by the 
 *              preference setting that specifies the first day-of-
 *              the-week.  If the date passed is a Tuesday and the 
 *              start day of week is Monday, this routine will return
 *              a value of one.
 *
 * PARAMETERS:	 month - month (1-12)
 *              day   - day (1-31)
 *              year  - year (1904-2031)
 *
 * RETURNED:	 day of the week (0-first, 1-second)
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/27/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static UInt16 DatebookDayOfWeek (UInt16 month, UInt16 day, UInt16 year)
{
	return ((DayOfWeek (month, day, year) - StartDayOfWeek + daysInWeek)
	 % daysInWeek);
}


/***********************************************************************
 *
 * FUNCTION:    SubstituteStr
 *
 * DESCRIPTION: This routine substitutes the occurrence a token, within
 *              a string, with another string.
 *
 * PARAMETERS:  str    - string containing token string
 *              token  - the string to be replaced
 *              sub    - the string to substitute for the token
 *              subLen - length of the substitute string.
 *
 * RETURNED:    pointer to the string
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/6/95	Initial Revision
 *
 ***********************************************************************/
SEC002 static Char* SubstituteStr (Char* str, Char* token, Char* sub, UInt16 subLen)
{
	int charsToMove;
	UInt16 tokenLen;
	UInt16 strLen;
	UInt16 blockSize;
	Char* ptr;
	MemHandle strH;

	// Find the start of the token string, if it doesn't exist, exit.
	ptr = StrStr(str, token);
	if (ptr == NULL) return (str);
	
	tokenLen = StrLen (token);
	charsToMove = subLen - tokenLen;
	
	
	// Resize the string if necessary.
	strH = MemPtrRecoverHandle (str);
	strLen = StrLen (str);
	blockSize = MemHandleSize (strH);
	if (strLen + charsToMove + 1 >= blockSize)
		{
		MemHandleUnlock (strH);
		MemHandleResize (strH, strLen + charsToMove + 1);
		str = MemHandleLock (strH);
		ptr = StrStr (str, token);
		ErrNonFatalDisplayIf(ptr == NULL, "Msg missing token");
		}
	
	// Make room for the substitute string.
	if (charsToMove)
		MemMove (ptr + subLen, ptr + tokenLen, StrLen (ptr + tokenLen)+1);
		
	// Replace the token with the substitute string.
	MemMove (ptr, sub, subLen);
	
	return (str);
}


/***********************************************************************
 *
 * FUNCTION:    SelectFont
 *
 * DESCRIPTION: This routine handles selection of a font in the List 
 *              View. 
 *
 * PARAMETERS:  currFontID - id of current font
 *
 * RETURNED:    id of new font
 *
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	9/10/97	Initial Revision
 *
 ***********************************************************************/
SEC002 static FontID SelectFont (FontID currFontID)
{
	UInt16 formID;
	FontID fontID;
	
	formID = (FrmGetFormId (FrmGetActiveForm ()));

	// Call the OS font selector to get the id of a font.
	fontID = FontSelect (currFontID);

	if (fontID != currFontID)
		FrmUpdateForm (formID, updateFontChanged);

	return (fontID);
}


#pragma mark -
/***********************************************************************
 *
 * FUNCTION:    RepeatSetDateTrigger
 *
 * DESCRIPTION: This routine sets the label of the trigger that displays 
 *              the end date of a repeating appointment. 
 *
 * PARAMETERS:  endDate	- date or -1 if no end date
 *
 * RETURNED:    nothing
 *
 * NOTES:
 *      This routine assumes that the memory allocated for the label of 
 *      the due date trigger is large enough to hold the lagest posible
 *      label.  This label's memory is reserved by initializing the label
 *      in the resource file.     
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/16/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static void RepeatSetDateTrigger (DateType endDate)
{
	Char* label;
	ListPtr lst;
	ControlPtr ctl;

	lst = GetObjectPtr (RepeatEndOnList);
	ctl = GetObjectPtr (RepeatEndOnTrigger);
	label = (Char *)CtlGetLabel (ctl);		// OK to cast; we call CtlSetLabel
	if (DateToInt (endDate) == apptNoEndDate)
		{
		StrCopy (label, LstGetSelectionText (lst, repeatNoEndDateItem));
		LstSetSelection (lst, noEndDateItem);
		}
	else
		{
		// Format the end date into a string.
		EraDateToDOWDMFormat (endDate.month, 
						 		 endDate.day, 
						 		 endDate.year + firstYear, 
						 		 ShortDateFormat, label);

//		DateToAscii (endDate.month, 
//						 endDate.day, 
//						 endDate.year + firstYear, 
//						 LongDateFormat, label);
		LstSetSelection (lst, repeatChooseDateItem);
		}
	CtlSetLabel (ctl, label);
}


/***********************************************************************
 *
 * FUNCTION:    RepeatGetUIValues
 *
 * DESCRIPTION: This routine gets the current repeat settings of the 
 *              ui gadgets in the repeat dialog box
 *
 *
 * PARAMETERS:  frm     - pointer to the repeat dialog
 *              repeatP - RepeatInfoType structure (fill-in by this routine) 
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/10/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static void RepeatGetUIValues (FormPtr frm, RepeatInfoPtr repeatP)
{
	UInt16 freq;
	UInt16 i;
	UInt16 id;
	UInt16 index;
	Char* str;
	DetailsPtr details;

	// Get the block that contains the details of the current record.
	details = RepeatDetailsP;


	// Get the repeat type.
	index = FrmGetControlGroupSelection (frm, RepeatTypeGroup);
	id = FrmGetObjectId (frm, index);
	if (id == RepeatYearly)
		repeatP->repeatType = repeatYearly;
	else if (id <= RepeatWeekly)
		{
		repeatP->repeatType = (RepeatType) (id - RepeatNone);
		}
	else
		{
		index = FrmGetControlGroupSelection (frm, RepeatByGroup);
		id = FrmGetObjectId (frm, index);
		if (id == RepeatByDayPushButon)
			repeatP->repeatType = repeatMonthlyByDay;
		else
			repeatP->repeatType = repeatMonthlyByDate;
		}
	

	// Get the repeat end date.
	repeatP->repeatEndDate = RepeatEndDate;


	// Get the repeat frequency.
	str = FldGetTextPtr (GetObjectPtr (RepeatFrequenceField));
	if (str) freq = StrAToI (str);
	else freq = 0;
	
	if (freq)
		repeatP->repeatFrequency = freq;
	else
		repeatP->repeatFrequency = 1;


	// Get the start day of week.  If the original repeat type, that is the 
	// repeat type when the dialog was first displayed, is weekly then
	// use the start date in the repeat info (the unedit data), otherwise
	// use the current start of week.
	if (repeatP->repeatType == repeatWeekly)
		{
		if (details->repeat.repeatType == repeatWeekly)
			repeatP->repeatStartOfWeek = details->repeat.repeatStartOfWeek;
		else		
			repeatP->repeatStartOfWeek = StartDayOfWeek;
		}

	// For all other repeat types, the repeatStartOfWeek field is meaningless.
	else
		repeatP->repeatStartOfWeek = 0;


	// If the repeat type is weekly, get the day of the week the event
	// repeats on. 
	if (repeatP->repeatType == repeatWeekly)
		{
		repeatP->repeatOn = 0;
		index = FrmGetObjectIndex (frm, RepeatDayOfWeek1PushButton);
		for (i = 0; i < daysInWeek ; i++)
			{
			if (FrmGetControlValue (frm, index +
								((i - RepeatStartOfWeek + daysInWeek) % daysInWeek)))
				repeatP->repeatOn |= (1 << i);
			}
		}

	// If the repeat type is monthly by day, get the day of the month (ex:
	// fisrt Friday) of the start date of the event.
	else if (repeatP->repeatType == repeatMonthlyByDay)
		{
		if (details->repeat.repeatType == repeatMonthlyByDay)
			repeatP->repeatOn = details->repeat.repeatOn;
		else
			repeatP->repeatOn = DayOfMonth (details->when.date.month,
				details->when.date.day,
				details->when.date.year + firstYear);
		}

	// For all other repeat types, the repeatOn field is meaningless.
	else
		{
		repeatP->repeatOn = 0;
		}
}


/***********************************************************************
 *
 * FUNCTION:    RepeatSetUIValues
 *
 * DESCRIPTION: This routine sets the current repeat settings of the 
 *              ui gadgets in the repeat dialog box
 *
 * PARAMETERS:  frm     - pointer to the Repeat Dialog
 *              repeatP - pointer to a RepeatInfoType structure.
 *
 * RETURNED:    nothing
 *
 *	HISTORY:
 *		07/10/95	art	Created by Art Lamb
 *		08/03/99	kwk	Copy label ptrs when shifting day-of-week pushbutton titles,
 *							not just the first byte of each label.
 *
 ***********************************************************************/
SEC001 static void RepeatSetUIValues (FormPtr frm, RepeatInfoPtr repeatP)
{
	UInt16			i;
	UInt16			id;
	UInt16			oldFreq;
	MemHandle		freqH;
	Char*				freqP;
	Boolean			on;
	FieldPtr			fld;

	// Set the selection of the "repeat type" push button group.
	id = repeatP->repeatType + RepeatNone;
	if (repeatP->repeatType > repeatMonthlyByDay)
		id--;
	FrmSetControlGroupSelection (frm, RepeatTypeGroup, id);
	

	// Set the frequency field
	if (repeatP->repeatType != repeatNone)	
		{
		fld = GetObjectPtr (RepeatFrequenceField);
		freqH = FldGetTextHandle (fld);
		if (freqH) 
			{
			freqP = MemHandleLock (freqH);
			oldFreq = StrAToI (freqP);
			}
		else
			{
			freqH = MemHandleNew (maxFrequenceFieldLen);
			freqP = MemHandleLock (freqH);
			oldFreq = 0;
			}

		if (oldFreq != repeatP->repeatFrequency)
			{
			StrIToA (freqP, repeatP->repeatFrequency);
			FldSetTextHandle (fld, freqH);
			if (FrmVisible (FrmGetActiveForm ()))
				{
				FldEraseField (fld);
				FldDrawField (fld);
				}
			}
		MemHandleUnlock (freqH);
		}	


	// Set the selection of the "repeat on" push button groups.
	if (repeatP->repeatType == repeatWeekly)
		{
		// If the appointment has a different start-day-of-week than
		// the dialog-box's current start-day-of-week, rearrange the
		//	labels on the days-of-week push buttons.
		//	Note that this will only handle button-label shifts of one day.
		if (StartDayOfWeek != RepeatStartOfWeek)
			{
			const Char* sundayLabel = CtlGetLabel (GetObjectPtr (RepeatDayOfWeek1PushButton));
			for (id = RepeatDayOfWeek1PushButton; id < RepeatDayOfWeek7PushButton; id++)
				{
				CtlSetLabel(GetObjectPtr(id), CtlGetLabel(GetObjectPtr(id + 1)));
				}
			CtlSetLabel(GetObjectPtr(RepeatDayOfWeek7PushButton), sundayLabel);
			RepeatStartOfWeek = StartDayOfWeek;
			}
		
		// Turn on the push buttons for the days the appointment repeats on.
		for (i = 0; i < daysInWeek; i++)
			{
			on = ((repeatP->repeatOn & (1 << i) ) != 0);
			id = RepeatDayOfWeek1PushButton + 
				((i - RepeatStartOfWeek + daysInWeek) % daysInWeek);
			CtlSetValue (GetObjectPtr (id), on);
			}
		}


	// Set the selection of the "repeat by" push button groups.
	if (repeatP->repeatType == repeatMonthlyByDate)	
		FrmSetControlGroupSelection (frm, RepeatByGroup, RepeatByDatePushButon);
	else
		FrmSetControlGroupSelection (frm, RepeatByGroup, RepeatByDayPushButon);
	

	// Set the "end on" trigger label and popup list selection.
	if (repeatP->repeatType != repeatNone)	
		{
		RepeatSetDateTrigger (repeatP->repeatEndDate);
		}
}


/***********************************************************************
 *
 * FUNCTION:    RepeatDrawDescription
 *
 * DESCRIPTION: This routine draws the text description of the current
 *              repeat type and frequency.
 *
 *              The description is created from a template string.  The 
 *              repeat type and frequency determines which template is 
 *              used.  The template may contain one or more of the 
 *              following token:
 *                   ^d - day name (ex: Monday)
 *							^f - frequency
 *                   ^x - day of the month ordinal number (ex: 1st - 31th)
 *                   ^m - month name (ex: July)
 *                   ^w - week ordinal number (1st, 2nd, 3rd, 4th, or last)
 *
 * PARAMETERS:  frm - pointer to the repeat dialog box
 *
 * RETURNED:    nothing
 *
 *	HISTORY:
 *		07/06/95	art	Created by Art Lam
 *		03/02/99	grant	Only do week ordinal substitution in the repeatMonthlyByDay case
 *		08/04/99	kwk	Use explicit string resources for days/months/years versus
 *							borrowing from system, so we can localize properly.
 *		10/21/99	gap	Remove rectangle drawing from proc.  Now a gadget in form.
 *		11/05/99	gap	Always use full day of week name when repeat.repeatType is repeatMonthlyByDay.
 *
 ***********************************************************************/
/* Db35: relocated sec001->sec002 (1274B) to make room for the Location
 * feature's Day View hooks -- sec001 was down to single-digit bytes free.
 * All 5 call sites are within this same file (sec001), so this is just a
 * normal cross-segment call from their perspective, no other change
 * needed (no separate forward declaration existed to update -- this
 * definition, appearing before every call site, was the only one). */
SEC002 static void RepeatDrawDescription (FormPtr frm)
{
	UInt8 repeatOn;
	UInt16 i;
	UInt16 len;
	UInt16 freq;
	UInt16 dayOfWeek;
	UInt16 templateId;
	UInt16 repeatOnCount = 0;
	Char* descP;
	Char* resP;
	Char* saveResP;
	MemHandle descH;
	MemHandle resH;
	FieldPtr fld;
	DetailsPtr details;
	RepeatInfoType repeat;
	
	// Get the block that contains the details of the current record.
	details = RepeatDetailsP;

	fld = GetObjectPtr (RepeatDescField);
	FldEraseField (fld);
		
	// Get the current setting of the repeat ui gadgets.
	RepeatGetUIValues (frm, &repeat);

	// Determine which template string to use.  The template string is 
	// used to format the description string. Note that we could add
	// a soft constant which tells us whether we need to use different
	// strings for freq == 1 case (depends on language), thus saving space.
	freq = repeat.repeatFrequency;
	switch (repeat.repeatType)
		{
		case repeatNone:
			templateId = repeatNoneStrID;
			break;

		case repeatDaily:
			if (freq == 1)
				// "Every day"
				templateId = everyDayRepeatStrID;
			else
				// "Every [other | 2nd | 3rd...] day"
				templateId = dailyRepeatStrID;
			break;

		case repeatWeekly:
			if (freq == 1)
				// "Every week on [days of week]"
				templateId = everyWeekRepeat1DayStrID;
			else
				templateId = weeklyRepeat1DayStrID;
			
			// Generate offset to appropriate string id,
			// based on # of days that we need to append.
			for (i = 0; i < daysInWeek; i++)
				{
				if (repeat.repeatOn & (1 << i) ) repeatOnCount++;
				}
			templateId += (repeatOnCount - 1);
			break;
			
		case repeatMonthlyByDate: 
			if (freq == 1)
				// "The ^w ^d of every month"
				templateId = everyMonthByDateRepeatStrID;
			else
				templateId = monthtlyByDateRepeatStrID;
			break;

		case repeatMonthlyByDay:
			if (freq == 1)
				templateId = everyMonthByDayRepeatStrID;
			else
				templateId = monthtlyByDayRepeatStrID;
			break;

		case repeatYearly:
			if (freq == 1)
				templateId = everyYearRepeatStrID;
			else
				templateId = yearlyRepeatStrID;
			break;

		default:
			ErrNonFatalDisplay("Unknown repeat type");
			break;
		}

	// Allocate a block to hold the description and copy the template 
	// string into it.
	resH = DmGetResource (strRsc, templateId);
	resP = MemHandleLock (resH);
	descH = MemHandleNew (MemPtrSize(resP));
	descP = MemHandleLock (descH);
	StrCopy (descP, resP);
	MemHandleUnlock (resH);
	

	// Substitute the month name string for the month name token.
	resH = DmGetResource (strRsc, repeatMonthNamesStrID);
	resP = MemHandleLock (resH);
	for (i = 1; i < details->when.date.month; i++)
		resP = StrChr (resP, spaceChr) + 1;
	len = (UInt16) (StrChr (resP, spaceChr) - resP);
	descP = SubstituteStr (descP, monthNameToken, resP, len);
	MemHandleUnlock (resH);


	// Substitute the day name string for the day name token.
	if ( (repeatOnCount == 1)  || (repeat.repeatType == repeatMonthlyByDay) )
		templateId = repeatFullDOWNamesStrID;
	else
		templateId = repeatShortDOWNamesStrID;
	
	resH = DmGetResource (strRsc, templateId);
	resP = MemHandleLock (resH);
	if (repeat.repeatType == repeatWeekly)
		{
		dayOfWeek = repeat.repeatStartOfWeek;
		repeatOn = repeat.repeatOn;
		saveResP = resP;
		while (StrStr (descP, dayNameToken))
			{
			for (i = 0; i < daysInWeek; i++)
				{
				if (repeatOn & (1 << dayOfWeek) )
					{
					repeatOn &= ~(1 << dayOfWeek);
					break;
					}
				dayOfWeek = (dayOfWeek + 1 + daysInWeek) % daysInWeek;
				}
			resP = saveResP;
			for (i = 0; i < dayOfWeek; i++)
				resP = StrChr (resP, spaceChr) + 1;
			
			len = (UInt16) (StrChr (resP, spaceChr) - resP);
			descP = SubstituteStr (descP, dayNameToken, resP, len);
			}
		}
	else
		{
		dayOfWeek = DayOfWeek (details->when.date.month, details->when.date.day, 
			details->when.date.year + firstYear);
		for (i = 0; i < dayOfWeek; i++)
			resP = StrChr (resP, spaceChr) + 1;
		len = (UInt16) (StrChr (resP, spaceChr) - resP);
		descP = SubstituteStr (descP, dayNameToken, resP, len);
		}
	MemHandleUnlock (resH);


	// Substitute the repeat frequency string for the frequency token. Note that
	// we do something special for 2nd (other), since the gender of 'other' changes
	// for some languages, depending on whether the next word is day, month, week,
	// or year.
	if (freq == 2)
		{
		Char otherFreqName[16];
		UInt16 index = (UInt16)repeat.repeatType - (UInt16)repeatNone;
		SysStringByIndex(freqOrdinal2ndStrlID, index, otherFreqName, sizeof(otherFreqName));
		descP = SubstituteStr (descP, frequenceToken, otherFreqName, StrLen(otherFreqName));
		}
	else
		{
		resH = DmGetResource (strRsc, freqOrdinalsStrID);
		resP = MemHandleLock (resH);
		for (i = 1; i < freq; i++)
			resP = StrChr (resP, spaceChr) + 1;
		len = (UInt16) (StrChr (resP, spaceChr) - resP);
		descP = SubstituteStr (descP, frequenceToken, resP, len);
		MemHandleUnlock (resH);
		}


	// Substitute the repeat week string (1st, 2nd, 3rd, 4th, or last)
	// for the week ordinal token.
	if (repeat.repeatType == repeatMonthlyByDay)
		{
		resH = DmGetResource (strRsc, weekOrdinalsStrID);
		resP = MemHandleLock (resH);
		for (i = 0; i < repeat.repeatOn / daysInWeek; i++)
			resP = StrChr (resP, spaceChr) + 1;
		len = (UInt16) (StrChr (resP, spaceChr) - resP);
		descP = SubstituteStr (descP, weekOrdinalToken, resP, len);
		MemHandleUnlock (resH);
		}
	else
		{
		// make sure the week ordinal token really doesn't appear
		ErrNonFatalDisplayIf(StrStr(descP, weekOrdinalToken) != NULL, "week ordinal not substituted");
		}


	// Substitute the repeat date string (1st, 2nd, ..., 31th) for the
	// day ordinal token.
	resH = DmGetResource (strRsc, dayOrdinalsStrID);
	resP = MemHandleLock (resH);
	for (i = 1; i < details->when.date.day; i++)
		resP = StrChr (resP, spaceChr) + 1;
	len = (UInt16) (StrChr (resP, spaceChr) - resP);
	descP = SubstituteStr (descP, dayOrdinalToken, resP, len);
	MemHandleUnlock (resH);
	
	// Draw the description.
	MemHandleUnlock (descH);
	FldFreeMemory (fld);
	FldSetTextHandle (fld, descH);
	FldDrawField (fld);
}


/***********************************************************************
 *
 * FUNCTION:    RepeatSelectEndDate
 *
 * DESCRIPTION: This routine selects the end date of a repeating event.
 *
 * PARAMETERS:  event - pointer to a popup select event
 *
 * RETURNED:    nothing
 *
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/16/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static void RepeatSelectEndDate (EventType* event)
{
	Int16 month, day, year;
	Char* titleP;
	MemHandle titleH;
	DetailsPtr details;

	// "No due date" items selected?
	if (event->data.popSelect.selection == repeatNoEndDateItem)
		{
		EDateSetForever(&RepeatEndDate);
		}

	// "Select date" item selected?
	else if (event->data.popSelect.selection == repeatChooseDateItem)
		{
		details = RepeatDetailsP;

		if ( DateToInt (RepeatEndDate) == apptNoEndDate)
			{
			year = details->when.date.year + firstYear;
			month = details->when.date.month;
			day = details->when.date.day;
			}
		else
			{
			year = RepeatEndDate.year + firstYear;
			month = RepeatEndDate.month;
			day = RepeatEndDate.day;
			}

		titleH = DmGetResource (strRsc, endDateTitleStrID);
		titleP = (Char*) MemHandleLock (titleH);

		if (DatePickSelectDay (selectDayByDay, &month, &day, &year, titleP))
			{
			RepeatEndDate.day = day;
			RepeatEndDate.month = month;
			RepeatEndDate.year = year - firstYear;
			
			// Make sure the end date is not before the start date.
			if (DateToInt(RepeatEndDate) < DateToInt (details->when.date))
				{
				SndPlaySystemSound (sndError);
				EDateSetForever(&RepeatEndDate);
				}
			}

		MemHandleUnlock (titleH);
		}

	RepeatSetDateTrigger (RepeatEndDate);
}


/***********************************************************************
 *
 * FUNCTION:    RepeatChangeRepeatOn
 *
 * DESCRIPTION: This routine is called when one of the weekly "repeat on" 
 *              push button is pushed.  This routine checks 
 *              if all the buttons has been turned off,  if so the day
 *              of the week of the appointment's start date is turn on.
 *
 * PARAMETERS:  event - pointer to and event
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/7/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static void RepeatChangeRepeatOn (EventType* event)
{
#pragma unused (event)

	UInt16 i;
	UInt16 id;
	UInt16 index;
	UInt16 dayOfWeek;
	FormPtr frm;
	Boolean on;
	DetailsPtr details;

	// Get the block that contains the details of the current record.
	details = RepeatDetailsP;

	frm = FrmGetActiveForm ();

	// Check if any of the buttons are on.
	index = FrmGetObjectIndex (frm, RepeatDayOfWeek1PushButton);
	on = false;
	for (i = 0; i < daysInWeek; i++)
		{
		if (FrmGetControlValue (frm, index + i) != 0)
			{
			on = true;
			break;
			}
		}

	// If all the buttons are off, turn on the start date's button.
	if (! on)
		{
		dayOfWeek = DayOfWeek (details->when.date.month,
			details->when.date.day,
			details->when.date.year+firstYear);
		dayOfWeek = (( dayOfWeek  - 
			details->repeat.repeatStartOfWeek + daysInWeek) % daysInWeek);

		id = RepeatDayOfWeek1PushButton + dayOfWeek;
		CtlSetValue (GetObjectPtr (id), true);
		}

	// Update the display of the repeat descrition.
	RepeatDrawDescription (frm);
}


/***********************************************************************
 *
 * FUNCTION:    RepeatChangeType
 *
 * DESCRIPTION: This routine changes the ui gadgets in the repeat dialog
 *              such that they match the newly selected repeat type.  The 
 *              routine is called when one of the repeat type push buttons
 *              are pushed.
 *
 * PARAMETERS:  event - pointer to and event
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	07/07/95	Initial Revision
 *			gap	10/28/99	End on value maintained last selected date
 *								after switching repeat type instead of returning to 
 *								"no end date" also reset RepeatEndDate global such that
 * 							if end date is "no end date" and "choose" is selected from
 *								popup - calendar always defaults to current day.
 *
 ***********************************************************************/
SEC001 static void RepeatChangeType (EventType* event)
{
	UInt16 id;
	UInt16 dayOfWeek;
	FormPtr frm;
	DetailsPtr details;
	RepeatType oldType;
	RepeatType newType;
	RepeatInfoType repeat;
	
	// If the type if monthly default to monthly-by-date.
	newType = (RepeatType) (event->data.ctlSelect.controlID - RepeatNone);
	if (newType > repeatWeekly) newType++;

	oldType = RepeatingEventType;
	if (oldType == newType) return;
	
	frm = FrmGetActiveForm ();
	
	// Get the block that contains the details of the current record.
	details = RepeatDetailsP;

	// Initialize the ui gadgets.
	if (newType == details->repeat.repeatType)
		{
		RepeatSetUIValues (frm, &details->repeat);
		
		// if reselecting current repeat type, reset RepeatEndDate global
		// to current date so if user attemps to "choose" a new day, the
		// default matches date displayed as opposed to last date picked 
		// last in choose form
		RepeatEndDate = details->repeat.repeatEndDate;
		}
	else
		{
		repeat.repeatType = newType;
		
		// when switching to a repeat type different from the current
		// setting, always start user with default end date and frequency
		// settings.
		EDateSetForever(&repeat.repeatEndDate);
		EDateSetForever(&RepeatEndDate);
		repeat.repeatFrequency = defaultRepeatFrequency;
		
		repeat.repeatStartOfWeek = StartDayOfWeek;
		
		if (newType == repeatWeekly)
			{
			dayOfWeek = DayOfWeek (details->when.date.month, 
				details->when.date.day,
				details->when.date.year+firstYear);
			repeat.repeatOn = (1 << dayOfWeek);
			}
		else if (newType == repeatMonthlyByDay)
			{
			repeat.repeatOn = DayOfMonth (details->when.date.month, 
				details->when.date.day,
				details->when.date.year + firstYear);
			}
		else
			{
			repeat.repeatOn = 0;
			}
		RepeatSetUIValues (frm, &repeat);
		}
	

	// Hide the ui gadgets that are unique to the repeat type we are 
	// no longer editing.
	switch (oldType)
		{
		case repeatNone:
			HideObject (frm, RepeatNoRepeatLabel);
			break;

		case repeatDaily:
			HideObject (frm, RepeatDaysLabel);
			break;
		
		case repeatWeekly:
			HideObject (frm, RepeatWeeksLabel);
			for (id = RepeatRepeatOnLabel; id <= RepeatDayOfWeek7PushButton; id++)
				HideObject (frm, id);
			break;

		case repeatMonthlyByDay:
		case repeatMonthlyByDate:
			HideObject (frm, RepeatMonthsLabel);
			for (id = RepeatByLabel; id <= RepeatByDatePushButon; id++)
				HideObject (frm, id);
			break;

		case repeatYearly:
			HideObject (frm, RepeatYearsLabel);
			break;
		}


	// Handle switching to or from "no" repeat.
	if (oldType == repeatNone)
		{
		ShowObject (frm, RepeatEveryLabel);
		ShowObject (frm, RepeatFrequenceField);
		ShowObject (frm, RepeatEndOnLabel);
		ShowObject (frm, RepeatEndOnTrigger);
		}
	else if (newType == repeatNone)
		{
		HideObject (frm, RepeatEveryLabel);
		HideObject (frm, RepeatFrequenceField);
		HideObject (frm, RepeatEndOnLabel);
		HideObject (frm, RepeatEndOnTrigger);
		}



	// Show the ui object that are appropriate for the new repeat
	// type.
	switch (newType)
		{
		case repeatNone:
			ShowObject (frm, RepeatNoRepeatLabel);
			break;

		case repeatDaily:
			ShowObject (frm, RepeatDaysLabel);
			break;

		case repeatWeekly:
			ShowObject (frm, RepeatWeeksLabel);
			ShowObject (frm, RepeatRepeatOnLabel);
			for (id = RepeatRepeatOnLabel; id <= RepeatDayOfWeek7PushButton; id++)
				ShowObject (frm, id);
			break;
			
		case repeatMonthlyByDay: 
		case repeatMonthlyByDate:
			ShowObject (frm, RepeatMonthsLabel);
			ShowObject (frm, RepeatByLabel);
			ShowObject (frm, RepeatByDayPushButon);
			ShowObject (frm, RepeatByDatePushButon);
			break;

		case repeatYearly:
			ShowObject (frm, RepeatYearsLabel);
			break;
		}	

	RepeatingEventType = newType;
	
	// Update the display of the repeat descrition.
	RepeatDrawDescription (frm);
}


/***********************************************************************
 *
 * FUNCTION:    RepeatApply
 *
 * DESCRIPTION: This routine applies the changes made in the Repeat Dialog.
 *              The changes or copied to a block intiialize by the 
 *              Details Dialog,  they a not written to the database until
 *              the Details Dialog is applied.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/24/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static void RepeatApply (void)
{
	DetailsPtr		details;
	RepeatInfoType repeat;


	// Get the block that contains the details of the current record,
	// this block was initialized by the details dialog initializtion
	// routine.
	details = RepeatDetailsP;
	
	RepeatGetUIValues (FrmGetActiveForm (), &repeat);
	details->repeat = repeat;
}


/***********************************************************************
 *
 * FUNCTION:    RepeatDescRectHandler
 *
 * DESCRIPTION: This routine is the event handler for rectangle gadget
 *					 surrounding the repeat description in the "Repeat
 *              Dialog Box".
 *
 *					 Instead of drawing a static rectangle the size of the 
 *					 gadget bounds, I have sized the gadget to be the full area
 *					 of the repeat dialog the description field will still fit
 *					 should the field have to be slightly resized for any reason.
 *					 The bounding rect drawn is calculated from the field's 
 *					 bounding rect.
 *
 * PARAMETERS:  gadgetP	- pointer to the gadget
 *					 cmd		- the event type to be handled
 *					 paramp	- any additional data that is passed to the gadget
 *
 * RETURNED:    true if the event was handled and should not be passed
 *              to a higher level handler.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			gap	10/21/99	Initial Revision
 *
 ***********************************************************************/

static Boolean RepeatDescRectHandler(FormGadgetType *gadgetP, UInt16 cmd, void *paramP)
{
	FieldPtr 		fld;
   RectangleType	r;
	
	switch(cmd)
	{
		case formGadgetEraseCmd:
		case formGadgetDrawCmd:
		
			// get the repeat description field and calculate a bounding box
			fld = GetObjectPtr (RepeatDescField);
			FldGetBounds (fld, &r);
			RctInsetRectangle (&r, -4);
			
			if (cmd == formGadgetDrawCmd)
				WinDrawRectangleFrame (simpleFrame, &r);
			else
				WinEraseRectangle (&r, 0);
			break;
		
		case formGadgetHandleEventCmd:
			return false;
			break;
			
		default:
			break;
	}
	
	return true;
}


/***********************************************************************
 *
 * FUNCTION:    RepeatInit
 *
 * DESCRIPTION: This routine initializes tthe "Repeat Dialog Box".  All
 *              the object in the dialog are initialize, even if they
 *              are not used given the current repeat type.
 *
 * PARAMETERS:  frm - pointer to the repeat dialog box
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/4/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static void RepeatInit (FormPtr frm)
{
	UInt16				id;
	DetailsPtr		details;


	// Get the block that contains the details of the current record,
	// this block was initialized by the details dialog initializtion
	// routine.
	details = RepeatDetailsP;
	

	// Set usable the ui object that are appropriate for the given repeat
	// type.
	switch (details->repeat.repeatType)
		{
		case repeatNone:
			ShowObject (frm, RepeatNoRepeatLabel);
			break;

		case repeatDaily:
			ShowObject (frm, RepeatEveryLabel);
			ShowObject (frm, RepeatFrequenceField);
			ShowObject (frm, RepeatDaysLabel);
			ShowObject (frm, RepeatEndOnLabel);
			ShowObject (frm, RepeatEndOnTrigger);
			break;

		case repeatWeekly:
			ShowObject (frm, RepeatEveryLabel);
			ShowObject (frm, RepeatFrequenceField);
			ShowObject (frm, RepeatWeeksLabel);
			ShowObject (frm, RepeatRepeatOnLabel);
			ShowObject (frm, RepeatEndOnLabel);
			ShowObject (frm, RepeatEndOnTrigger);
			for (id = RepeatDayOfWeek1PushButton; id <= RepeatDayOfWeek7PushButton; id++)
				ShowObject (frm, id);
			break;
			
		case repeatMonthlyByDay: 
		case repeatMonthlyByDate:
			ShowObject (frm, RepeatEveryLabel);
			ShowObject (frm, RepeatFrequenceField);
			ShowObject (frm, RepeatMonthsLabel);
			ShowObject (frm, RepeatByLabel);
			ShowObject (frm, RepeatByDayPushButon);
			ShowObject (frm, RepeatByDatePushButon);
			ShowObject (frm, RepeatEndOnLabel);
			ShowObject (frm, RepeatEndOnTrigger);
			break;

		case repeatYearly:
			ShowObject (frm, RepeatEveryLabel);
			ShowObject (frm, RepeatFrequenceField);
			ShowObject (frm, RepeatYearsLabel);
			ShowObject (frm, RepeatEndOnLabel);
			ShowObject (frm, RepeatEndOnTrigger);
			break;
		}

	FrmSetGadgetHandler(frm, FrmGetObjectIndex(frm, RepeatDescRectGadget), RepeatDescRectHandler);

	RepeatStartOfWeek = sunday;	
	RepeatEndDate = details->repeat.repeatEndDate;
	RepeatingEventType = details->repeat.repeatType;
	
	RepeatSetUIValues (frm, &details->repeat);
}


/***********************************************************************
 *
 * FUNCTION:    RepeatHandleEvent
 *
 * DESCRIPTION: This routine is the event handler for the "Repeat
 *              Dialog Box".
 *
 * PARAMETERS:  event  - a pointer to an EventType structure
 *
 * RETURNED:    true if the event was handled and should not be passed
 *              to a higher level handler.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	5/23/95	Initial Revision
 *			CSS	06/22/99	Standardized keyDownEvent handling
 *								(TxtCharIsHardKey, commandKeyMask, etc.)
 *			gap	10/15/99	Added added handling of frmUpdateEvent
 *
 ***********************************************************************/
SEC001 Boolean RepeatHandleEvent (EventType* event)
{
	FormPtr frm;
	Boolean handled = false;

	if (event->eType == ctlSelectEvent)
		{
		switch (event->data.ctlSelect.controlID)
			{
			case RepeatOkButton:
				RepeatApply ();
				
				// Give control of the details info to the Details form
				DetailsP = RepeatDetailsP;
				RepeatDetailsP = 0;
				
				FrmGotoForm (DetailsDialog);
				handled = true;
				break;

			case RepeatCancelButton:
				// Give control of the details info to the Details form
				DetailsP = RepeatDetailsP;
				RepeatDetailsP = 0;
				
				FrmGotoForm (DetailsDialog);
				handled = true;
				break;				

			case RepeatNone:
			case RepeatDaily:
			case RepeatWeekly:
			case RepeatMonthly:
			case RepeatYearly:
				RepeatChangeType (event);
				handled = true;
				break;				

			case RepeatDayOfWeek1PushButton:
			case RepeatDayOfWeek2PushButton:
			case RepeatDayOfWeek3PushButton:
			case RepeatDayOfWeek4PushButton:
			case RepeatDayOfWeek5PushButton:
			case RepeatDayOfWeek6PushButton:
			case RepeatDayOfWeek7PushButton:
				RepeatChangeRepeatOn (event);
				handled = true;
				break;				

			case RepeatByDayPushButon:
			case RepeatByDatePushButon:
				RepeatDrawDescription (FrmGetActiveForm());
				handled = true;
				break;
			}
		}

	else if	(	(event->eType == keyDownEvent)
				&&	(!TxtCharIsHardKey(	event->data.keyDown.modifiers,
												event->data.keyDown.chr))
				&& (!EvtKeydownIsVirtual(event)))
		{
		WChar chr = event->data.keyDown.chr;
		if (TxtCharIsDigit (chr) || TxtCharIsCntrl (chr))
			{
			FieldPtr fld = GetObjectPtr (RepeatFrequenceField);
			FldHandleEvent (fld, event);
			RepeatDrawDescription (FrmGetActiveForm());
			}
		handled = true;
		}
		
	else if (event->eType == popSelectEvent)
		{
		if (event->data.popSelect.listID == RepeatEndOnList)
			{
			RepeatSelectEndDate (event);			
			handled = true;
			}
		}

	else if (event->eType == frmOpenEvent)
		{
		frm = FrmGetActiveForm ();
		RepeatInit (frm);
		FrmDrawForm (frm);
		FrmSetFocus (frm, FrmGetObjectIndex (frm, RepeatFrequenceField));
		RepeatDrawDescription (frm);
		handled = true;
		}

	else if (event->eType == frmUpdateEvent)
		{
		frm = FrmGetActiveForm ();
		FrmDrawForm (frm);
		handled = true;
		}
	
	else if (event->eType == frmCloseEvent)
		{
		if (RepeatDetailsP)
			{
			MemPtrFree(RepeatDetailsP);
			RepeatDetailsP = 0;
			}
		}

	return (handled);
}




/***********************************************************************
 *
 * FUNCTION:    DetailsSetSpanTriggers
 *
 * DESCRIPTION: Db35: refresh both the Start: and End: trigger labels from
 *              details->when/endDateOffset -- replaces the old separate
 *              DetailsSetTimeTrigger (combined Time: label) and the
 *              date-label half of DetailsSelectDate.
 *
 ***********************************************************************/
SEC001 static void DetailsSetSpanTriggers (DetailsPtr details)
{
	ControlPtr ctl;
	Char *label;
	DateType endDate;

	ctl = GetObjectPtr (DetailsStartSelector);
	label = (Char *) CtlGetLabel (ctl);	// OK to cast; we call CtlSetLabel
	DateSpanFormatLabel (details->when.date, details->when.startTime, label);
	CtlSetLabel (ctl, label);

	endDate = details->when.date;
	DateAdjust (&endDate, details->endDateOffset);
	ctl = GetObjectPtr (DetailsEndSelector);
	label = (Char *) CtlGetLabel (ctl);
	DateSpanFormatLabel (endDate, details->when.endTime, label);
	CtlSetLabel (ctl, label);
}


/***********************************************************************
 *
 * FUNCTION:    DetailsDeleteAppointment
 *
 * DESCRIPTION: This routine deletes an appointment record. It is called 
 *              when the delete button in the details dialog is pressed.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    true if the record was delete or archived.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/20/95	Initial Revision
 *
 ***********************************************************************/
/* Db35: deliberately UNTAGGED (main segment, ~9KB free) - sec001/sec002/
 * sec003 are all at or past their own limits. Safe cross-segment call
 * either way from DetailsHandleEvent (sec001); doesn't call DateDAVGetOne
 * so no same-segment-across-files risk to worry about here regardless of
 * which segment it ends up in. */
static Boolean DetailsDeleteAppointment (void)
{
	UInt16 recordNum;

	recordNum = CurrentRecord;

	// Db35: CalDAV items are read-only - nothing to delete, and recordNum
	// is a DAV sentinel, not a real ApptDB index (ClearEditState/
	// DeleteRecord below are undefined/crash on it).
	if (IsDayDav (recordNum))
		return (false);

	// Clear the edit state, this will delete the current record if is
	// blank.
	if (ClearEditState ())
		return (true);

	// Delete the record,  this routine will display an appropriate 
	// dialog to confirm the action.  If the dialog is canceled 
	// don't update the display.
	if (! DeleteRecord (recordNum))
		return (false);

	return (true);
}


/***********************************************************************
 *
 * FUNCTION:    DetailsGet
 *
 * DESCRIPTION: This routine get the current setting of the details
 *              dialog.  
 *
 * PARAMETERS:  details - pointer to a structure that hold the info about
 *                        the current record.
 *
 * RETURNED:    true if the form info was all valid, otherwise false.
 *
 * NOTE:        Not all the members of the structure passed are filled
 *              in by this routine.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/20/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static Boolean DetailsGet (DetailsPtr details)
{
	ListPtr lst;
	FieldPtr fld;
	ControlPtr ctl;

	// Db35: alarms removed -- no alarm settings to read from the dialog.

	// Get the private setting.
	details->secret = CtlGetValue (GetObjectPtr (DetailsPrivateCheckbox));
	
	return(true);
}


/***********************************************************************
 *
 * FUNCTION:    DetailsApply
 *
 * DESCRIPTION: This routine applies the changes made in the Details Dialog.
 *
 * PARAMETERS:  details     - appointment details.
 *              attachNote  - true if pressing the note button being caused
 *                            the changes to be applied.
 *              updateCodeP - pointer to a code which indicates how the
 *									   appointment was changed,  this code is send
 *									   as the update code in a frmUpdate event.
 *
 * RETURNED:    true if the changes were applied, false if the changes
 *              were not applied.
 *
 *	NOTE:			 If a note is being attached to a repeating event the 
 *              user is prompted to determine if an exception should be
 *              created,  the exception is create be this routine,  the
 *              note is attached elsewhere.
 *
 *					 (GKG 1/27/99) Changing the repeat info of weekly repeating
 *					 appointments is tricky.  There are two ways to change which
 *					 days the appt repeats on - 1. change the date in the details dialog
 *					 and 2. change it directly in the repeat info dialog.  Both
 *					 ways should work now.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	03/10/95	Initial Revision
 *			kcr	10/09/95	added 'private records' alert
 *			gkg	01/27/99	fixed some problems with weekly repeating appointments
 *			gap	08/27/99	Replaced call of ExceptionAlert with call to RangeDialog.
 *			gap	09/14/99	Date change are now applied when changes are made to
 *								current and future occurrences of an event.
 *
 ***********************************************************************/
SEC001 static Boolean DetailsApply (DetailsPtr details, Boolean attachNote,
	UInt16* updateCodeP)
{
	Err					err = 0;
	UInt16				attr;
	UInt32				uid;		/* Db35: this appt's uniqueID, for the Location gadget */
	UInt16				id;
	UInt16				repeatOn;
	UInt16				dayOfWeek;
	UInt16 				updateCode	= 0;
	UInt16				alertButton;
	Int32					adjust;
	Boolean				dirty 		= false;
	Boolean				attrDirty 	= false;
	Boolean				exception 	= false;
	Boolean				pastRepeats	= false;
	Boolean				descDirty	= false;	/* Db35: Title field change -- kept
										 * separate from `dirty` (see below) */
	Char*					newDesc;					/* Db35: Title field's current text */
	FormPtr				alert;
	MemHandle 			recordH;
	ApptDateTimeType	when;
	ApptDBRecordType	apptRec;
	ApptDBRecordType	newRec;
	ApptDBRecordFlags	changedFields;

	// Db35: CalDAV items are read-only - CurrentRecord is a DAV sentinel,
	// not a real ApptDB index, so nothing below (ApptGetRecord/
	// ApptChangeRecord/LocGadgetCommit/...) is safe to run. Report success
	// (nothing needed applying) so the caller just closes the dialog.
	if (IsDayDav (CurrentRecord))
		{
		*updateCodeP = 0;	/* both real call sites always pass a real address */
		return (true);
		}

	// Get the ui setting for the appointment. If the alarm setting contains
	// invalid characters, bail out.
	if (!DetailsGet (details))
		return(false);

	when = details->when;

	// Compare the start date setting in the dialog with the date in the
	// current record.  Update the record if necessary.
	ApptGetRecord (ApptDB, CurrentRecord, &apptRec, &recordH);

	// Db35: has the title been edited? Deliberately NOT folded into `dirty`
	// below -- `dirty` also gates the This/Future/All range-dialog prompt
	// for repeating events, and a title isn't a per-occurrence property in
	// this data model (the inline table-edit path never prompts for this
	// either). Threaded through the exception branch and the final
	// ApptChangeRecord call separately, further down.
	//
	// FldCompactText FIRST -- release any free/gap space and settle the
	// field's text buffer before reading it, same discipline NoteViewSave
	// already uses for the Note field. Without this, FldGetTextPtr can
	// return a not-yet-settled buffer right after the user finishes typing,
	// which can silently read as "unchanged" (title edit lost) and leave
	// the field's internal state inconsistent for the NEXT time this same
	// record's Details is opened (found via human testing: title edit not
	// taking effect, followed by a MemoryMgr Free Handle crash on
	// re-entering Details for the same event).
	{
		FieldPtr titleFld = GetObjectPtr (DetailsTitleField);
		if (FldDirty (titleFld))
			FldCompactText (titleFld);
	}
	newDesc = (Char*) FldGetTextPtr (GetObjectPtr (DetailsTitleField));
	if (! newDesc) newDesc = "";
	descDirty = (StrCompare (newDesc, apptRec.description ? apptRec.description : "") != 0);

	MemSet (&changedFields, sizeof (changedFields), 0);

	// Has the start time of the appointment been changed?
	if (TimeToInt(apptRec.when->startTime) != TimeToInt(details->when.startTime))
		{
		when.date = apptRec.when->date;
		newRec.when = &when;
		changedFields.when = true;
		updateCode |= updateTimeChanged;
		dirty = true;
		}


	// Has the end time of the appointment been changed?
	if (TimeToInt(apptRec.when->endTime) != TimeToInt(details->when.endTime))
		{
		when.date = apptRec.when->date;
		newRec.when = &when;
		changedFields.when = true;
		updateCode |= updateTimeChanged;
		dirty = true;
		}


	// Has the date of the appointment been changed?
	if (DateToInt(Date) != DateToInt(details->when.date))
		{
		when.date = details->when.date;
		newRec.when = &when;
		changedFields.when = true;
		updateCode |= updateDateChanged;
		dirty = true;
		}


	// Db35: has the End: span (days after the start date) been changed?
	if (apptRec.endDateOffset != details->endDateOffset)
		{
		newRec.endDateOffset = details->endDateOffset;
		changedFields.endDate = true;
		updateCode |= updateTimeChanged;
		dirty = true;
		}



	// Db35: alarms removed -- no alarm set/cancel/change handling.



	// Has a non-repeating event been changed to a repeating event?
	if ((! apptRec.repeat) && (details->repeat.repeatType != repeatNone))
		{
		newRec.repeat = &details->repeat;
		changedFields.repeat = true;
		updateCode |= updateRepeatChanged;
		dirty = true;
		}

	// Has a repeating event been changed to a non-repeating event?
	else if ((apptRec.repeat) && (details->repeat.repeatType == repeatNone))
		{
		newRec.repeat = NULL;
		updateCode |= updateRepeatChanged;
		changedFields.repeat = true;
		dirty = true;
		}
	
	// Has any of the repeat info been changed?
	else if (apptRec.repeat &&
					(apptRec.repeat->repeatType != details->repeat.repeatType ||
			   	 DateToInt (apptRec.repeat->repeatEndDate) != DateToInt(details->repeat.repeatEndDate) ||
			   	 apptRec.repeat->repeatFrequency != details->repeat.repeatFrequency ||
					 (details->repeat.repeatType == repeatWeekly &&
			 			apptRec.repeat->repeatOn != details->repeat.repeatOn)))
		{
		newRec.repeat = &details->repeat;
		changedFields.repeat = true;
		updateCode |= updateRepeatChanged;
		dirty = true;
		}
	


	// Get the current setting of the secret checkbox and compare it the
	// the setting of the record.
	DmRecordInfo (ApptDB, CurrentRecord, &attr, &uid, NULL);
	LocGadgetCommit (uid);	/* Db35: persist any Location edit (DateLocation.c) */
	// Db35: force a Day View relayout so DayInjectLocationText re-prefetches
	// gDayLocText -- without this, a Location-only change sets no other
	// updateCode bit, DayViewUpdateDisplay's if/else chain matches nothing,
	// and the "@ location" suffix never appears until some UNRELATED change
	// happens to also touch the day. Reuses updateTimeChanged rather than a
	// new bit (cheaper: no new sec001 code, and its only remaining effect,
	// alarms being removed, is exactly DayViewLayoutDay(true) -- what's needed).
	updateCode |= updateTimeChanged;
	if (((attr & dmRecAttrSecret) == dmRecAttrSecret) != details->secret)
		{
		if (PrivateRecordVisualStatus > showPrivateRecords)
			{
			updateCode |= updateItemHide;
			}
		else if (details->secret)
			FrmAlert (privateRecordInfoAlert);
			
		attrDirty = true;
		}


	// If the record is a repeating appointment and the repeat info
	// has not been modified,  find out if we should create an exception.
	if ( (apptRec.repeat) && 
	     (!changedFields.repeat) && 
	     ((dirty || attrDirty) || (attachNote && (!apptRec.note))) )
		{

		alert = FrmInitForm (RangeDialog);
		alertButton = FrmDoDialog (alert);
		FrmDeleteForm (alert);


		// If the alert was canceled don't apply any of the changes.
		if (alertButton == RangeCancelButton)
			goto cancelExit;

		else if (alertButton == RangeCurrentButton)
			{
			updateCode |= updateException;
			exception = true;

			// If the date of the appointment has not be changed then set the
			// date of the exception to the current date.
			if ( ! (updateCode & updateDateChanged))
				when.date = Date;
			}
		else if (alertButton == RangeFutureButton) 
			// Split the repeating event into two event, one of past occurrences
			// and one for future occurreneces.
			{
			pastRepeats = true;
			if ( ! (updateCode & updateDateChanged))
				when.date = Date;
			newRec.when = &when;
			changedFields.when = true;
			dirty = true;
			}
			 // Currently no special code in needed to handle the case that the 
			 // user has selected RangeAllButton only because pastRepeats is initialized
			 // to false.  If the code changes in the future where this may not be the case,
			 // the following else case will need to be activated.
//		else //  (alertButton == RangeAllButton)
//			{
//			pastRepeats = false;
//			}

		}

	// If the record is a repeating appointment, and the repeat info has been
	// changed, and there are past occurrences of the event then we need
	// to duplicate the event so the past occurrence will continue to exist.
	if (apptRec.repeat && (changedFields.repeat))
		{
		// If any of the repeat info other than the end date has changed
		// then spawn a repeating event for all past occurrences.
		if (apptRec.repeat->repeatType != details->repeat.repeatType ||
			 apptRec.repeat->repeatFrequency != details->repeat.repeatFrequency ||
			 apptRec.repeat->repeatOn != details->repeat.repeatOn)
			{
			pastRepeats = true;

			// If the date of the appointment has not been changed then set the
			// start date of the repeating appointment to the current date.
			if ( ! (updateCode & updateDateChanged))
				{
				updateCode |= updateDateChanged;
				when.date = Date;
				newRec.when = &when;
				changedFields.when = true;
				}
			}
		}


	// If the record is a repeating appointment, and the repeat info has not
	// been changed but the start date has, then we need to adjust the 
	// end date and possibly split the event into two events.
	if ((! exception) && apptRec.repeat && (!changedFields.repeat) &&
		 (updateCode & updateDateChanged))
		{
		// If the event is a repeating event and it has an end date, move 
		// the end date such that the duration of the event is maintained.
		if ((DateToInt (apptRec.repeat->repeatEndDate) != apptNoEndDate))
			{
			adjust = DateToDays (apptRec.repeat->repeatEndDate) -
		         DateToDays (apptRec.when->date);
			details->repeat.repeatEndDate = details->when.date;
			DateAdjust (&details->repeat.repeatEndDate, adjust);
			}

		if (DateToInt (details->repeat.repeatEndDate) != 
			 (DateToInt(apptRec.repeat->repeatEndDate)))
			{
			newRec.repeat = &details->repeat;
			changedFields.repeat = true;
			updateCode |= updateRepeatChanged;
			dirty = true;
			}
		}
		

	// If the record is a repeating appointment, and the start date has, 
	// then delete the exceptions list.
	if ((! exception) && apptRec.repeat && (updateCode & updateDateChanged))
		{
		newRec.exceptions = NULL;
		changedFields.exceptions = true;
		dirty = true;
		}


	// If the repeat type is weekly and the start date of the event
	// has been changed, and the event only repeats on one day per week
	// update the 'repeat on' field, which contains
	// the days of the week the event repeats on, such that the 
	// event occurs on the start date.
	if ((! exception) && (details->repeat.repeatType == repeatWeekly) &&
		 (updateCode & updateDateChanged))
		{
		// does the event only repeat once per week?
		ErrNonFatalDisplayIf(details->repeat.repeatOn == 0, "weekly appt. repeats on no days");
		
		if (OnlyRepeatsOncePerWeek(details->repeat))
			{
			UInt16 newDayOfWeek;
			
			dayOfWeek = DayOfWeek (apptRec.when->date.month, 
				apptRec.when->date.day,
				apptRec.when->date.year + firstYear);
			newDayOfWeek = DayOfWeek (when.date.month, 
				when.date.day,
				when.date.year + firstYear);
			
			if (dayOfWeek != newDayOfWeek)
				{
			//	details->repeat.repeatOn &= ~(1 << dayOfWeek);
			//	details->repeat.repeatOn |= (1 << newDayOfWeek);
				details->repeat.repeatOn = (1 << newDayOfWeek);
								
				changedFields.repeat = true;
				newRec.repeat = &details->repeat;
				}
			}
		}
		
		
	// If the repeat type is monthly by day and the start date of the event
	// has been changed, update the repeat on field which contains
	// the date of the month the event repeating on (ex: first friday).
	if ((! exception) && (details->repeat.repeatType == repeatMonthlyByDay) &&
		 ( (updateCode & updateDateChanged) || (! apptRec.repeat) ))
		{
		repeatOn = DayOfMonth (when.date.month,
			when.date.day,
			when.date.year + firstYear);
			
		// If we're in the fourth week, and the fourth week is also the last
		// week,  ask the user which week the event repeats in (fourth or last).
		if ( ((repeatOn / daysInWeek) == 3) &&
		      (when.date.day + daysInWeek > DaysInMonth (
		      	when.date.month, when.date.year + firstYear)))
			{
			alert = FrmInitForm (MonthlyRepeatDialog);
			
			// Set the 4th / last push button.
			if (apptRec.repeat && 
				 apptRec.repeat->repeatType == repeatMonthlyByDay &&
				 apptRec.repeat->repeatOn > dom4thSat)
				id = MonthlyRepeatLastButton;
			else
				id = MonthlyRepeatFourthButton;
			FrmSetControlGroupSelection (alert, MonthlyRepeatWeekGroup,id);

			alertButton = FrmDoDialog (alert);

			if (FrmGetObjectIndex (alert, MonthlyRepeatLastButton) ==
			    FrmGetControlGroupSelection (alert, MonthlyRepeatWeekGroup))
			   repeatOn += daysInWeek;

			FrmDeleteForm (alert);

			if (alertButton == MonthlyRepeatCancel)
				goto cancelExit;
			}
			
		details->repeat.repeatOn = repeatOn;
		newRec.repeat = &details->repeat;
		changedFields.repeat = true;
		}


	// Unlock the appointment record.
	MemHandleUnlock (recordH);

	// Erase the details dialog before the changes are applied to the 
	// database.  It is necessary to do thing in this order because
	// erasing the details dialog may cause a frmUpdate event to be 
	// sent to list view, and the list view may not be able to redraw
	// properly once the database has been changed.
	FrmEraseForm (FrmGetActiveForm());


	// Write the changes to the database.
	if (exception)
		{
		err = ApptAddException (ApptDB, &CurrentRecord, Date);
		if (! err)
			{
			ApptGetRecord (ApptDB, CurrentRecord, &apptRec, &recordH);
			MemSet (&newRec, sizeof (newRec), 0);
			// Db35: carry the edited title into the split-off exception record too --
			// this used to always copy the OLD description, silently dropping a title
			// edit made in the same Details session as a time/date change.
			newRec.description = descDirty ? newDesc : apptRec.description;
			newRec.note = apptRec.note;
			newRec.when = &when;
			newRec.endDateOffset = details->endDateOffset;	/* Db35: carry the
										 * span into the split-off occurrence too --
										 * same class of bug as the description fix
										 * just above (MemSet'd newRec would silently
										 * drop it otherwise). */
			// Db35: alarms removed.

			err = CreateException (&newRec, &CurrentRecord);
			MemHandleUnlock (recordH);
			}
		}
	else if (dirty || descDirty)
		{
		// Create a copy of the repeating event and set its end date to yesterday.
		if (pastRepeats)
			err = SplitRepeatingEvent (&CurrentRecord);

		if (!err)
			{
			// Db35: fold in the title change here -- deliberately not part of
			// `changedFields`/`newRec` above, so a title-only edit never
			// contributes to the This/Future/All prompt (see descDirty's
			// declaration comment).
			if (descDirty)
				{
				newRec.description = newDesc;
				changedFields.description = true;
				}
			err = ApptChangeRecord (ApptDB, &CurrentRecord, &newRec, changedFields);
			}
		}

	if (err)
		{
		FrmAlert (DeviceFullAlert);
		ClearEditState ();
		updateCode = 0;
		}

	// Save the new secret status if it's been changed.
	else if (attrDirty)
		{
		DmRecordInfo (ApptDB, CurrentRecord, &attr, NULL, NULL);	
		if (details->secret)
			attr |= dmRecAttrSecret;
		else
			attr &= ~dmRecAttrSecret;

		attr |= dmRecAttrDirty;
		DmSetRecordInfo (ApptDB, CurrentRecord, &attr, NULL);
		}

	// If the alarm info has been changed, or if the appointment has 
	// an alarm and the date, time, or repeat info has been changed,
	// reschedule the next alarm.
	// Db35: alarms removed -- no rescheduling.

	//if record has been hidden, make sure to deselect it
	//if (updateCode & updateItemHide)
	//		CurrentRecord = noRecordSelected;

	*updateCodeP = updateCode;
	return (true);
	

	// We're here if the cancel button one of the confirmation dialogs
	// was pressed.	
cancelExit:
	MemHandleUnlock (recordH);
	return (false);	
}



/***********************************************************************
 *
 * FUNCTION:    DetailsExit
 *
 * DESCRIPTION: This routine is called when the details dialog is 
 *              applied or canceled.  It unlocks any memory locked 
 *              and frees any memory allocated by the Details Dialog routine.
 *
 * PARAMETERS:  none
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/21/95	Initial Revision
 *			grant 6/28/99	Removed 'free' parameter - always free if DetailsP is non-NULL.
 *								Removed 'details' parameter - we use DetailsP instead.
 *
 ***********************************************************************/
SEC001 static void DetailsExit(void)
{
	MemHandle rscH;

	// The label of the repeat trigger is locked - unlock it.
	// Note that we can't get the pointer to be unlocked by using
	// CtlGetLabel on the repeat trigger because it may actually be
	// pointing into the middle of the resource.
	rscH = DmGetResource (strRsc, repeatTypesStrID);
	MemHandleUnlock (rscH);
	
	// Free the block that holds the changes to the appointment record.
	if (DetailsP)
		{
		/* Db35: FldSetTextHandle never took ownership of descH, but the field
		 * still holds a reference to it -- freeing a handle a field still
		 * points at crashes on form teardown ("MemoryMgr FreeHandle").
		 * Detach first, same as gDayEditH's cleanup does elsewhere in this
		 * file (DayViewSaveDescription: FldSetTextHandle(fld,NULL) before
		 * the free). */
		if (((DetailsPtr) DetailsP)->descH)
			{
			FldSetTextHandle (GetObjectPtr (DetailsTitleField), NULL);
			MemHandleFree (((DetailsPtr) DetailsP)->descH);
			}
		MemPtrFree (DetailsP);
		DetailsP = 0;
		}

	// Db35: CalDAV items are read-only pseudo-records - ItemSelected must
	// not stay true with CurrentRecord pointing at a DAV sentinel once
	// Details closes, or every one of this app's "is something selected"
	// guards (ClearEditState in Datebook.c, DayViewClearEditState/
	// DayViewRestoreEditState right here - all structured as "if
	// (!ItemSelected) { CurrentRecord = noRecordSelected; return; }
	// ...otherwise touch ApptDB with CurrentRecord unconditionally") skips
	// its own safe early-return and crashes (confirmed real: "DataMgr.c,
	// Index out of range" tapping a DAV row again after closing its
	// Details). CurrentRecord itself is deliberately left alone here (NOT
	// reset to noRecordSelected) - the Note-button path (DetailsHandleEvent)
	// calls DetailsExit BEFORE navigating to NewNoteView, which still needs
	// CurrentRecord to find the right DAV record; ItemSelected=false alone
	// is enough to make every guard above treat it as inert in the
	// meantime, and NoteViewLoadRecordFromDav never looks at ItemSelected
	// at all.
	if (IsDayDav (CurrentRecord))
		ItemSelected = false;
}


/***********************************************************************
 *
 * FUNCTION:    DetailsInit
 *
 * DESCRIPTION: This routine initializes the Details Dialog, and allocates
 *              and initialize a memory block that hold the record
 *              info to be edited by the details dialog.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    details - memory block that hold info to be edited 
 *                        by the details dialog.
 *
 * NOTES:
 *      This routine assumes that the memory allocated for the label of 
 *      the time trigger is large enough to hold the largest posible
 *      label.  This label's memory is reserved by initializing the label
 *      in the resource file.     
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/20/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static DetailsPtr DetailsInit (void)
{
	UInt16 i;
	UInt16 attr;
	UInt32 uid;			/* Db35: this appt's uniqueID, for the Location gadget */
	Char* rscP;
	Char* label;
	Char* textP;
	FormPtr frm;
	ListPtr lst;
	FieldPtr fld;
	ControlPtr ctl;
	DetailsPtr details;
	MemHandle textH;
	MemHandle recordH;
	ApptDBRecordType	apptRec;

	// Allocate and initialize a block to hold a temporary copy of the 
	// info from the appointment record, we don't want to apply any changes 
	// to the record until the "Details Dialog" is confirmed.
	if (! DetailsP)
		{
		DetailsP = MemPtrNew (sizeof (DetailsType));
		details = DetailsP;
		MemSet (details, sizeof (DetailsType), 0);

		// Db35: CurrentRecord is a DAV sentinel, not a real ApptDB index -
		// ApptGetRecord/DmRecordInfo below would be undefined/crash. Populate
		// from CalDAV data instead; the common tail (Title field, Start/End
		// triggers, Repeat label, Private checkbox) below runs unconditionally
		// either way.
		if (IsDayDav (CurrentRecord))
			{
			DetailsInitFromDav (details);
			}
		else
			{
			// Get a pointer to the appointment record.
			ApptGetRecord (ApptDB, CurrentRecord, &apptRec, &recordH);

			DmRecordInfo (ApptDB, CurrentRecord, &attr, &uid, NULL);
			LocGadgetLoad (FrmGetActiveForm (), uid);	/* Db35: seed + display the Location field */
			details->descH = DayCopyDescPlain (CurrentRecord);	/* Db35: staged Title text */

			details->secret = (attr & dmRecAttrSecret) == dmRecAttrSecret;
			details->when = *apptRec.when;
			/* Db35: Date is the currently-VIEWED day, correct to use as the
			 * staged start date for a repeating event's occurrence -- but for a
			 * spanning event, its row can appear on a continuation day that is
			 * NOT a valid start date at all (just the same occurrence bleeding
			 * onto a later day). Two sub-cases:
			 * - non-repeating: only one occurrence ever exists, so its real
			 *   start (apptRec.when->date) is always right when Date differs.
			 * - repeating: Date itself can be a LEGITIMATE occurrence start too
			 *   (this day may show both a continuation row AND a fresh row),
			 *   so Date alone is ambiguous -- trust which ROW was actually
			 *   tapped (gDetailsApptIndex) instead. A continuation row's
			 *   clamped display start won't match the record's real
			 *   per-occurrence start time; walk backward (same bound as
			 *   ApptGetAppointments' own lookback) for the occurrence it
			 *   belongs to. */
			if (apptRec.repeat && apptRec.endDateOffset &&
				gDetailsApptIndex != kDetailsApptUnknown)
				{
				ApptInfoPtr apInfo = MemHandleLock (ApptsH);
				TimeType rowStart = apInfo[gDetailsApptIndex].startTime;
				MemPtrUnlock (apInfo);
				details->when.date = Date;
				if (TimeToInt (rowStart) != TimeToInt (apptRec.when->startTime))
					{
					Int16 back, maxBack = (Int16) apptRec.endDateOffset;
					if (maxBack > 30) maxBack = 30;
					for (back = 1; back <= maxBack; back++)
						{
						DateType cand = Date;
						DateAdjust (&cand, (Int32) -back);
						if (ApptRepeatsOnDate (&apptRec, cand))
							{
							details->when.date = cand;
							break;
							}
						}
					}
				}
			else if (! apptRec.repeat && apptRec.endDateOffset &&
				DateToDays (Date) != DateToDays (apptRec.when->date))
				details->when.date = apptRec.when->date;
			else
				details->when.date = Date;
			gDetailsApptIndex = kDetailsApptUnknown;	/* Db35: one-shot, consumed */
			details->endDateOffset = apptRec.endDateOffset;	/* Db35: multi-day span */
			// Db35: alarms removed.

			if (apptRec.repeat)
				details->repeat = *apptRec.repeat;
			else
				{
				details->repeat.repeatType = repeatNone;
				EDateSetForever(&details->repeat.repeatEndDate);
				}


			// Unlock the appointment record
			MemHandleUnlock (recordH);
			}
		}
	else
		details = DetailsP;

	// Db35: set the Title field's displayed text (unconditionally, like every
	// other control below -- these widgets are freshly created each call).
	fld = GetObjectPtr (DetailsTitleField);
	FldSetTextHandle (fld, details->descH);

	// Set the Start:/End: trigger labels.
	DetailsSetSpanTriggers (details);


	// Db35: alarms removed -- no alarm checkbox / advance UI to initialise.

	// Set the repeat type selector label.  The label will point
	// to a locked resouce string,  we'll need to unlock this 
	// resource when the dialog is dismissed.
	ctl = GetObjectPtr (DetailsRepeatSelector);
	rscP = MemHandleLock (DmGetResource (strRsc, repeatTypesStrID));
	for (i = 0; i < details->repeat.repeatType; i++)
		rscP += StrLen (rscP) + 1;
	CtlSetLabel (ctl, rscP);

	// Db35: WiRSa only reports WHETHER the source VEVENT repeats (a
	// boolean), never its actual pattern (Daily/Weekly/...), so
	// details->repeat.repeatType is unconditionally repeatNone for a DAV
	// record (DetailsInitFromDav) - the label set above would always say
	// "None" even for an event that DOES repeat, which is wrong and was
	// reported as such. Override with a plain "Repeats" label instead of
	// guessing a specific pattern we don't actually know.
	if (DayDavRepeatOverrideLabel ())
		CtlSetLabel (ctl, "Repeats");


	// If the record is mark secret, turn on the secret checkbox.
	ctl = GetObjectPtr (DetailsPrivateCheckbox);
	CtlSetValue (ctl, details->secret);


	return (details);
}

/* Db35: DetailsHandleEvent's shared read-only-edit-attempt gate for a
 * CalDAV item - returns true (and shows altDavReadOnly) for exactly the
 * control taps/keystrokes that would otherwise CHANGE the record:
 *   - ctlSelectEvent on the Start/End/Repeat triggers or the Delete
 *     button (each of which would otherwise open a real sub-dialog or
 *     delete the record).
 *   - ctlSelectEvent on the Private checkbox - by the time the app's own
 *     handler sees this event, the ROM's control manager has ALREADY
 *     flipped the checkbox's on-screen value (that's how ctlSelectEvent
 *     itself gets generated) - reverted here so nothing about a CalDAV
 *     record's displayed state ever actually changes, not just "doesn't
 *     persist."
 *   - keyDownEvent for any printable character or backspace - covers
 *     typing into the Title field (a plain editable FIELD with no
 *     separate fldEnterEvent hook the way Location has, so a keystroke
 *     is the only chokepoint).
 *   - fldEnterEvent on the Location field (which would otherwise call
 *     LocFieldTapped and open its own edit sub-dialog).
 * Returns false (no alert, caller's own logic proceeds normally) for
 * everything else - OK/Cancel/Note taps, and pure navigation/focus
 * events. Deliberately UNTAGGED (main segment, plenty of room) - called
 * from sec001 (DetailsHandleEvent, almost no headroom left), a safe
 * cross-segment call either way. */
static Boolean DayDavBlockEdit (EventType *event)
{
	if (event->eType == ctlSelectEvent)
		{
		switch (event->data.ctlSelect.controlID)
			{
			case DetailsStartSelector:
			case DetailsEndSelector:
			case DetailsRepeatSelector:
			case DetailsDeleteButton:
				FrmAlert (altDavReadOnly);
				return true;

			case DetailsPrivateCheckbox:
				CtlSetValue (GetObjectPtr (DetailsPrivateCheckbox), false);
				FrmAlert (altDavReadOnly);
				return true;
			}
		return false;
		}

	if (event->eType == keyDownEvent)
		{
		if ((event->data.keyDown.chr >= 0x20 && event->data.keyDown.chr < 0x7F) ||
			event->data.keyDown.chr == chrBackspace)
			{
			FrmAlert (altDavReadOnly);
			return true;
			}
		return false;
		}

	if (event->eType == fldEnterEvent && event->data.fldEnter.fieldID == DetailsLocationGadget)
		{
		FrmAlert (altDavReadOnly);
		return true;
		}

	return false;
}


/***********************************************************************
 *
 * FUNCTION:    DetailsHandleEvent
 *
 * DESCRIPTION: This routine is the event handler for the "Details
 *              Dialog Box".
 *
 * PARAMETERS:  event  - a pointer to an EventType structure
 *
 * RETURNED:    true if the event has handled and should not be passed
 *              to a higher level handler.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/20/95	Initial Revision
 *			CSS	06/22/99	Standardized keyDownEvent handling
 *								(TxtCharIsHardKey, commandKeyMask, etc.)
 *			jmp	09/17/99	Use NewNoteView instead of NoteView.
 *
 ***********************************************************************/
SEC001 Boolean DetailsHandleEvent (EventType* event)
{
	static DetailsPtr	details;

	UInt16 updateCode = 0;
	FormPtr frm;
	Boolean handled = false;

	// Db35: CalDAV items are read-only - intercept every control/keystroke
	// that would otherwise actually CHANGE the record (as opposed to just
	// viewing/closing it) BEFORE the real logic below runs, and tell the
	// user why instead of silently discarding the attempt. OK/Cancel/Note
	// are deliberately NOT caught here - viewing is always allowed, only
	// editing is blocked. See DayDavBlockEdit's own comment for the full
	// list and the keyDownEvent case just past this switch.
	if (IsDayDav (CurrentRecord) && DayDavBlockEdit (event))
		return (true);

	if (event->eType == ctlSelectEvent)
		{
		switch (event->data.ctlSelect.controlID)
			{
			case DetailsOkButton:
				if (DetailsApply (details, false, &updateCode))
					{
					DetailsExit();
					FrmReturnToForm (DayView);
					}
				FrmUpdateForm (DayView, updateCode);
				handled = true;
				break;

			case DetailsCancelButton:
				DetailsExit();
				FrmUpdateForm (DayView, updateCode);
				FrmReturnToForm (DayView);
				handled = true;
				break;
				
			case DetailsDeleteButton:
				DetailsExit();
				FrmReturnToForm (DayView);
				if ( DetailsDeleteAppointment ()) 
					updateCode = updateItemDelete;
				FrmUpdateForm (DayView, updateCode);
				handled = true;
				break;
				
			case DetailsNoteButton:
				// Db35: for a CalDAV item, DetailsApply and CreateNote both
				// already no-op safely (see their own DAV guards) and just
				// report success, so this unmodified real-appointment path
				// naturally lands on NewNoteView - which reads the actual
				// (possibly empty) DESCRIPTION text via NoteViewLoadRecord's
				// own DAV branch.
				if (DetailsApply (details, true, &PendingUpdate))
					{
					DetailsExit();
					FrmCloseAllForms ();
					if (CreateNote (false))
						FrmGotoForm (NewNoteView);
					else
						FrmGotoForm (DayView);
					}
				handled = true;
				break;
				
			case DetailsStartSelector:
				if (DateSpanEditStart (&details->when.date, &details->when.startTime))
					{
					// If the event repeats monthly-by-day, update the day of
					// the month it repeats on (same side effect the old
					// DetailsSelectDate had for the start date).
					if (details->repeat.repeatType == repeatMonthlyByDay)
						details->repeat.repeatOn = DayOfMonth (details->when.date.month,
							details->when.date.day, details->when.date.year + firstYear);

					// Db35: the event just became timed (was untimed) but
					// endTime is still the apptNoTime sentinel from before --
					// default it to start+1hr so the End dialog has something
					// sane to show/edit instead of formatting garbage.
					if (TimeToInt (details->when.startTime) != apptNoTime &&
						TimeToInt (details->when.endTime) == apptNoTime)
						{
						details->when.endTime = details->when.startTime;
						if (details->when.endTime.hours < 23)
							details->when.endTime.hours++;
						}

					// Db35: endDateOffset is a RELATIVE day-count (days after
					// start), not an absolute end date -- moving Start
					// naturally carries End along with it, preserving the
					// span's length, with zero extra reconciliation needed.
					DetailsSetSpanTriggers (details);
					}
				handled = true;
				break;

			case DetailsEndSelector:
				if (DateSpanEditEnd (details->when.date, details->when.startTime,
						&details->endDateOffset, &details->when.endTime))
					DetailsSetSpanTriggers (details);
				handled = true;
				break;

			case DetailsRepeatSelector:
				if (DetailsGet (details))
					{
					// Give control of the details info block to the Repeat form
					RepeatDetailsP = DetailsP;
					DetailsP = 0;

					/* Db35: DetailsExit's own field-detach (FldSetTextHandle(fld,NULL)
					 * before freeing descH) is gated on DetailsP being non-NULL, since it
					 * only runs right before MemPtrFree -- but here descH's OWNERSHIP is
					 * being TRANSFERRED to RepeatDetailsP, not freed, so DetailsP is
					 * already 0 and that detach never runs. The Title field is left
					 * pointing at descH when FrmGotoForm below tears this form down,
					 * which crashed ("MemoryMgr.c, Free handle") on returning from the
					 * Repeat dialog -- confirmed via backtrace: the fatal hit inside the
					 * very next DetailsInit's FldSetTextHandle re-attach call. Detach
					 * explicitly here, independent of the free. */
					FldSetTextHandle (GetObjectPtr (DetailsTitleField), NULL);

					DetailsExit();
					FrmGotoForm (RepeatDialog);
					}
				
				handled = true;
				break;
			}
		}


	// Db35: alarms removed -- the Details dialog no longer has a numeric
	// alarm-advance field to route digit keys to.

	// Db35: Location field -- same idiom as Address35's EditForm.c
	// (fldEnterEvent interception on ftAddress/ftEmail/etc rows): this
	// field must never actually enter inline edit mode, so LocFieldTapped
	// always runs the Location edit dialog instead and 'handled' is
	// unconditionally set.
	else if (event->eType == fldEnterEvent &&
				event->data.fldEnter.fieldID == DetailsLocationGadget)
		{
		LocFieldTapped (FrmGetActiveForm ());
		handled = true;
		}

	// Db35: Details became a full-screen form that intercepts focus from the
	// Day View (see the Title-field/tap-to-Details feature), which meant the
	// Day View's own Record menu -- Duplicate Event, Beam Event -- was no
	// longer reachable while editing an event. Own menu bar (mnuDetails,
	// forms.rcp), same command IDs as the Day View's MENU 1000 so
	// DetailsDoCommand can share DuplicateCmd/SendRecordCmd directly.
	else if (event->eType == menuEvent)
		{
		handled = DetailsDoCommand (event->data.menu.itemID);
		}

	else if (event->eType == frmOpenEvent)
		{
		frm = FrmGetActiveForm ();
		details = DetailsInit ();
		FrmDrawForm (frm);
		handled = true;
		}
	
	else if (event->eType == frmCloseEvent)
		{
		if (DetailsP)
			{
			MemPtrFree(DetailsP);
			DetailsP = 0;
			}
		}

	return (handled);
}


#pragma mark ----------------
/***********************************************************************
 *
 * FUNCTION:    NoteViewDrawTitleAndForm
 *
 * DESCRIPTION: This routine draws the form and title of the note view.
 *
 * PARAMETERS:  frm, FormPtr to the form to draw
 *
 * RETURNED:    nothing
 *
 *	HISTORY:
 *		08/03/95	art	Created by Art Lamb.
 *		08/04/99	kwk	Use FntWidthToOffset for text truncation.
 *    09/27/99 jmp   Square off the NoteView title so that it covers up
 *                   the blank Form title used to trigger the menu on taps
 *                   to the title area.  Also, set the NoteView title's color
 *                   to match the standard Form title colors.  Eventually, we
 *                   should add a variant to Forms that allows for NoteView
 *                   titles directly.  This "fixes" bug #21610.
 *		09/29/99	jmp	Fix bug #22413:  Ensure that we peform the font metrics
 *							AFTER we have set the font!
 *    12/02/99 jmp	Fix bug #24377.  Don't call WinScreenUnlock() if WinScreenLock()
 *                   fails.
 *                          
 ***********************************************************************/
 SEC001 static void NoteViewDrawTitleAndForm (FormPtr frm)
 {
 	Coord x;
	Coord maxWidth;
   Coord formWidth;
   RectangleType r;
	FontID curFont;
	Char* desc;
	MemHandle recordH = NULL;
	RectangleType eraseRect, drawRect;
	ApptDBRecordType	apptRec;
	UInt16 descLen, ellipsisWidth;
	Char* linefeedP;
	Int16 descWidth;
	IndexedColorType curForeColor;
	IndexedColorType curBackColor;
	IndexedColorType curTextColor;
	UInt8 * lockedWinP;
	Char davTitle[64];

	// Db35: CalDAV items are read-only - CurrentRecord is a DAV sentinel,
	// not a real ApptDB index (ApptGetRecord below is undefined/crash on
	// it). recordH stays NULL (its unlock further down is already guarded).
	if (IsDayDav (CurrentRecord))
		{
		NoteViewDavTitle (davTitle, sizeof (davTitle));
		desc = davTitle;
		}
	else
		{
		// Get current record and related info.
		//
		ApptGetRecord (ApptDB, CurrentRecord, &apptRec, &recordH);
		desc = apptRec.description;
		}

	// "Lock" the screen so that all drawing occurs offscreen to avoid
	// the anamolies associated with drawing the Form's title then drawing
	// the NoteView title.  We REALLY need to make a variant for doing
	// this in a more official way!
	//
	lockedWinP = WinScreenLock (winLockCopy);

	FrmDrawForm(frm);
	
	// Perform initial set up.
	//
   FrmGetFormBounds(frm, &r);
   formWidth = r.extent.x;
	maxWidth = formWidth - 8;
	
	linefeedP = StrChr (desc, linefeedChr);
	descLen = (linefeedP == NULL ? StrLen (desc) : linefeedP - desc);
	ellipsisWidth = 0;
			
	RctSetRectangle (&eraseRect, 0, 0, formWidth, FntLineHeight()+4);
	RctSetRectangle (&drawRect, 0, 0, formWidth, FntLineHeight()+2);
	
	// Save/Set window colors and font.  Do this after FrmDrawForm() is called
	// because FrmDrawForm() leaves the fore/back colors in a state that we
	// don't want here.
	//
 	curForeColor = WinSetForeColor (UIColorGetTableEntryIndex(UIFormFrame));
 	curBackColor = WinSetBackColor (UIColorGetTableEntryIndex(UIFormFill));
 	curTextColor = WinSetTextColor (UIColorGetTableEntryIndex(UIFormFrame));
	curFont = FntSetFont (noteTitleFont);

	// Erase the Form's title area and draw the NoteView's.
	//
	WinEraseRectangle (&eraseRect, 0);
	WinDrawRectangle (&drawRect, 3);

	if (FntWidthToOffset (desc, descLen, maxWidth, NULL, &descWidth) != descLen)
		{
		ellipsisWidth = FntCharWidth(chrEllipsis);
		descLen = FntWidthToOffset (desc, descLen, maxWidth - ellipsisWidth, NULL, &descWidth);
		}
	
	x = (formWidth - descWidth - ellipsisWidth + 1) / 2;
	
	WinDrawInvertedChars (desc, descLen, x, 1);
	if (ellipsisWidth != 0)
		{
		Char buf[maxCharBytes + sizeOf7BitChar(chrNull)];
		buf[TxtSetNextChar(buf, 0, chrEllipsis)] = chrNull;
		WinDrawInvertedChars (buf, StrLen(buf), x + descWidth, 1);
		}

	// Now that we've drawn everything, blast it all back on the screen at once.
	//
	if (lockedWinP)
		WinScreenUnlock ();

	// Unlock the record that ApptGetRecord() implicitly locked - recordH
	// stays NULL for the CalDAV branch above (nothing to unlock).
	//
	if (recordH) MemHandleUnlock (recordH);
	
   // Restore window colors and font.
   //
   WinSetForeColor (curForeColor);
   WinSetBackColor (curBackColor);
   WinSetTextColor (curTextColor);
	FntSetFont (curFont);
}
 


/***********************************************************************
 *
 * FUNCTION:    NoteViewUpdateScrollBar
 *
 * DESCRIPTION: This routine update the scroll bar.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	07/01/96	Initial Revision
 *			gap	11/02/96	Fix case where field and scroll bars get out of sync
 *
 ***********************************************************************/
SEC001 static void NoteViewUpdateScrollBar (void)
{
	UInt16 scrollPos;
	UInt16 textHeight;
	UInt16 fieldHeight;
	Int16 maxValue;
	FieldPtr fld;
	ScrollBarPtr bar;

	fld = GetObjectPtr (NoteField);
	bar = GetObjectPtr (NoteScrollBar);
	
	FldGetScrollValues (fld, &scrollPos, &textHeight,  &fieldHeight);

	if (textHeight > fieldHeight)
		{
		// On occasion, such as after deleting a multi-line selection of text,
		// the display might be the last few lines of a field followed by some
		// blank lines.  To keep the current position in place and allow the user
		// to "gracefully" scroll out of the blank area, the number of blank lines
		// visible needs to be added to max value.  Otherwise the scroll position
		// may be greater than maxValue, get pinned to maxvalue in SclSetScrollBar
		// resulting in the scroll bar and the display being out of sync.
		maxValue = (textHeight - fieldHeight) + FldGetNumberOfBlankLines (fld);
		}
	else if (scrollPos)
		maxValue = scrollPos;
	else
		maxValue = 0;

	SclSetScrollBar (bar, scrollPos, 0, maxValue, fieldHeight-1);
}


/***********************************************************************
 *
 * FUNCTION:    NoteViewLoadRecord
 *
 * DESCRIPTION: This routine loads the note db field into the note edit 
 *              object.
 *
 * PARAMETERS:  frm - pointer to the Edit View form
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	2/21/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static void NoteViewLoadRecord (void)
{
	FieldPtr fld;
	ApptDBRecordType	apptRec;
	MemHandle recordH;

	// Get a pointer to the note field.
	fld = GetObjectPtr (NoteField);

	// Set the font used in the note field.
	FldSetFont (fld, NoteFont);

	// Db35: CalDAV items are read-only - CurrentRecord is a DAV sentinel,
	// not a real ApptDB index (ApptGetRecord below is undefined/crash on
	// it). Relayed through a sec002 helper - sec001 has almost no headroom
	// left.
	if (IsDayDav (CurrentRecord))
		{
		NoteViewLoadRecordFromDav (fld);
		return;
		}

	ApptGetRecord (ApptDB, CurrentRecord, &apptRec, &recordH);
	ErrFatalDisplayIf (! apptRec.note, "Invalid record");
	MemHandleUnlock (recordH);

	// Db35: edit an OWNED decrypted copy, never the handle ApptGetRecord returned --
	// for an encrypted appointment that's gApptDecH, a single shared/reused scratch
	// buffer (DateDB.c), not real storage.  Editing it in place silently discarded
	// edits (note reads back empty) and eventually crashed "Field.c: Field object
	// overlocked" once the shared buffer was reused while still attached to this
	// field.  Same fix/pattern as gDayEditH (DayViewSaveDescription) for the Title.
	if (gNoteEditH) { MemHandleFree (gNoteEditH); gNoteEditH = NULL; }
	gNoteEditH = DayCopyNotePlain (CurrentRecord);
	if (gNoteEditH)
		FldSetTextHandle (fld, gNoteEditH);
}


/***********************************************************************
 *
 * FUNCTION:    NoteViewSave
 *
 * DESCRIPTION: This routine removed any empty space that the field
 *              routines may a add to the note,  the field object
 *              modifies the note db field in place.  The current
 *              record is also mark dirty.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	2/21/95	Initial Revision
 *
 ***********************************************************************/
SEC002 static void NoteViewSave (void)
{
	FieldPtr fld;
	Boolean empty = false;
	Boolean dirty;
	Char* newNote;

	fld = GetObjectPtr (NoteField);

	// Was the note string modified by the user.
	dirty = FldDirty (fld);
	if (dirty)
		{
		// Release any free space in the note field.
		FldCompactText (fld);
		}

	empty = (FldGetTextLength (fld) == 0);

	// Db35: gNoteEditH is an OWNED decrypted copy (see NoteViewLoadRecord), never the
	// record's real storage -- write it back explicitly via ApptChangeRecord (repacks +
	// re-seals if encrypted), same as DayViewSaveDescription does for gDayEditH. Only
	// needed if the user actually changed the text and it isn't now empty (an empty
	// result is handled below by deleting the note field entirely).
	// Db35: CalDAV items are read-only - never persist any edit made here
	// (CurrentRecord is a DAV sentinel, ApptChangeRecord/DeleteNote below
	// are undefined/crash on it regardless).
	if (dirty && ! empty && ! IsDayDav (CurrentRecord))
		{
		ApptDBRecordType	r;
		ApptDBRecordFlags	changed;

		newNote = (Char*) FldGetTextPtr (fld);
		MemSet (&r, sizeof (r), 0);
		r.note = newNote ? newNote : "";
		MemSet (&changed, sizeof (changed), 0);
		changed.note = true;
		ApptChangeRecord (ApptDB, &CurrentRecord, &r, changed);
		DirtyRecord (ApptDB, CurrentRecord);
		}

	// Clear the handle value in the field, otherwise the handle
	// will be freed while the field still points at it -- this also
	// unlocks the field's own internal lock on the handle.
	FldSetTextHandle (fld, NULL);

	// Db35: detach done above; now free OUR owned copy (see NoteViewLoadRecord).
	if (gNoteEditH)
		{
		MemHandleFree (gNoteEditH);
		gNoteEditH = NULL;
		}

	// Remove the note from the record, if it is empty.
	if (empty && ! IsDayDav (CurrentRecord))
		{
		DeleteNote (false, false);
		}
}


/***********************************************************************
 *
 * FUNCTION:    NoteViewDeleteNote
 *
 * DESCRIPTION: This routine deletes a the note field from an appointment
 *              record.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    true if the note was deleted.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	8/4/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static Boolean NoteViewDeleteNote (void)
{
	FormPtr alert;
	UInt16 alertButton;
	FieldPtr fld;
	Boolean exception = false;
	Boolean splitEvent = false;
	MemHandle recordH;
	ApptDBRecordType apptRec;

	// Db35: CalDAV items are read-only - nothing to delete, and
	// CurrentRecord is a DAV sentinel (ApptGetRecord below is undefined/
	// crash on it). Relayed through a sec002 helper - sec001 has almost no
	// headroom left.
	if (IsDayDav (CurrentRecord))
		return (NoteViewDeleteNoteFromDav ());

	ApptGetRecord (ApptDB, CurrentRecord, &apptRec, &recordH);
	MemHandleUnlock (recordH);

	// If we're changing a repeating appointmemt, check if all occurrences
	// are being changed, or if and exception is being created.
	if (apptRec.repeat)
		{
		alert = FrmInitForm (RangeDialog);
		alertButton = FrmDoDialog (alert);
		FrmDeleteForm (alert);
		
		if (alertButton == RangeCancelButton)
			return (false);
			
		else if (alertButton == RangeCurrentButton)
			exception = true;
			
		else if (alertButton == RangeFutureButton)
			splitEvent = true;
		}

	// Confirm the operation.
	else if (FrmAlert(DeleteNoteAlert) != DeleteNoteYes)
		return (false);


	// Clear the handle value in the field, otherwise the handle
	// will be free when the form is disposed of.  This call also
	// unlocks the handle that contains the note string.
	fld = GetObjectPtr (NoteField);
	FldCompactText (fld);
	FldSetTextHandle (fld, 0);

	// Db35: fld held OUR owned copy (gNoteEditH, see NoteViewLoadRecord) -- detach
	// above already done; free it now (was previously a leak -- the old in-place
	// scheme had nothing separate to free here).
	if (gNoteEditH)
		{
		MemHandleFree (gNoteEditH);
		gNoteEditH = NULL;
		}

	// Remove the note field form the record.
	if (DeleteNote (exception, splitEvent))
		return (true);

	// If we were unable to delete the note, restore the state of the 
	// note view.  Deletes can fails if the appointment repeats and
	// there not enough space to create an exception.
	NoteViewLoadRecord ();
	
	return (false);
}


/***********************************************************************
 *
 * FUNCTION:    NoteViewDoCommand
 *
 * DESCRIPTION: This routine preforms the menu command specified.
 *
 * PARAMETERS:  command  - menu item id
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	3/29/95	Initial Revision
 *			jmp	9/17/99	Eliminate the obsolete goto top/bottom menu items.
 *			jmp	11/04/99	To prevent other sublaunch issues, remind ourselves
 *								that we've sublaunched already into PhoneNumberLookup().
 *
 ***********************************************************************/
SEC001 static Boolean NoteViewDoCommand (UInt16 command)
{
	FieldPtr fld;
	Boolean handled = true;
	
	switch (command)
		{
		case newNoteFontCmd:
			NoteFont = SelectFont (NoteFont);
			break; 
		
		case newNotePhoneLookupCmd:
			fld = GetObjectPtr (NoteField);
			InPhoneLookup = true;
			A35LookupPhone (fld);
			InPhoneLookup = false;
			break;

		default:
			handled = false;
		}	
	return (handled);
}


/***********************************************************************
 *
 * FUNCTION:    NoteViewScroll
 *
 * DESCRIPTION: This routine scrolls the Note View by the specified
 *					 number of lines.
 *
 * PARAMETERS:  linesToScroll - the number of lines to scroll,
 *						positive for down,
 *						negative for up
 *					 updateScrollbar - force a scrollbar update?
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/1/96	Initial Revision
 *			grant 2/2/99	Use NoteViewUpdateScrollBar()
 *
 ***********************************************************************/
SEC001 static void NoteViewScroll (Int16 linesToScroll, Boolean updateScrollbar)
{
	UInt16			blankLines;
	FieldPtr			fld;
	
	fld = GetObjectPtr (NoteField);
	blankLines = FldGetNumberOfBlankLines (fld);

	if (linesToScroll < 0)
		FldScrollField (fld, -linesToScroll, winUp);
	else if (linesToScroll > 0)
		FldScrollField (fld, linesToScroll, winDown);
	
	// If there were blank lines visible at the end of the field
	// then we need to update the scroll bar.
	if (blankLines && linesToScroll < 0 || updateScrollbar)
		{
		NoteViewUpdateScrollBar();
		}
}

/***********************************************************************
 *
 * FUNCTION:    NoteViewPageScroll
 *
 * DESCRIPTION: This routine scrolls the message a page up or down.
 *
 * PARAMETERS:   direction     up or down
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/1/96	Initial Revision
 *			grant 2/9/99	use NoteViewScroll() to do actual scrolling
 *
 ***********************************************************************/
SEC001 static void NoteViewPageScroll (WinDirectionType direction)
{
	UInt16 linesToScroll;
	FieldPtr fld;

	fld = GetObjectPtr (NoteField);
	
	if (FldScrollable (fld, direction))
		{
		linesToScroll = FldGetVisibleLines (fld) - 1;
		
		if (direction == winUp)
			linesToScroll = -linesToScroll;
			
		NoteViewScroll(linesToScroll, true);
		}
}


/***********************************************************************
 *
 * FUNCTION:    NoteViewInit
 *
 * DESCRIPTION: This routine initializes the Note View form.
 *
 * PARAMETERS:  frm - pointer to the Note View form.
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	3/31/95	Initial Revision
 *			jmp	9/8/99	Make this routine more consistent with other
 *								built-in apps that have it.
 *			jmp	9/23/99	Eliminate the hiding of the old font controls now
 *								that we're using a form that doesn't contain them anyway.
 *
 ***********************************************************************/
SEC001 static void NoteViewInit (FormPtr frm)
{
	FieldPtr 		fld;
	FieldAttrType	attr;

	NoteViewLoadRecord ();

	// Have the field send events to maintain the scroll bar.
	fld = GetObjectPtr (NoteField);
	FldGetAttributes (fld, &attr);
	attr.hasScrollBar = true;
	FldSetAttributes (fld, &attr);
}


/***********************************************************************
 *
 * FUNCTION:    NoteViewHandleEvent
 *
 * DESCRIPTION: This routine is the event handler for the "Note View".
 *
 * PARAMETERS:  event  - a pointer to an EventType structure
 *
 * RETURNED:    true if the event has handled and should not be passed
 *              to a higher level handler.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	8/3/95	Initial Revision
 *			CSS	06/22/99	Standardized keyDownEvent handling
 *								(TxtCharIsHardKey, commandKeyMask, etc.)
 *			jmp	9/27/99	Combined NoteViewDrawTitle() & FrmUpdateForm()
 *								into a single routine that is now called
 *								NoteViewDrawTitleAndForm().
 *
 ***********************************************************************/
SEC001 Boolean NoteViewHandleEvent (EventType* event)
{
	UInt16 pos;
	FormPtr frm;
	FieldPtr fld;
	Boolean handled = false;

	if (event->eType == keyDownEvent)
		{
		if (TxtCharIsHardKey(event->data.keyDown.modifiers, event->data.keyDown.chr))
			{
			NoteViewSave ();
			ClearEditState ();
			DateSecondsToDate (TimGetSeconds (), &Date);
			FrmGotoForm (DayView);
			handled = true;
			}
		else if (EvtKeydownIsVirtual(event))
			{
			if (event->data.keyDown.chr == vchrPageUp)
				{
				NoteViewPageScroll (winUp);
				handled = true;
				}
	
			else if (event->data.keyDown.chr == vchrPageDown)
				{
				NoteViewPageScroll (winDown);
				handled = true;
				}
			}
		}

	else if (event->eType == ctlSelectEvent)
		{		
		switch (event->data.ctlSelect.controlID)
			{
			case NoteDoneButton:
				NoteViewSave ();
				FrmGotoForm (DayView);
				handled = true;
				break;

			case NoteDeleteButton:
				if (NoteViewDeleteNote())
					FrmGotoForm (DayView);
				handled = true;
				break;
			}
		}


	else if (event->eType == menuEvent)
		{
		handled = NoteViewDoCommand (event->data.menu.itemID);
		}
		

	else if (event->eType == frmOpenEvent)
		{
		frm = FrmGetActiveForm ();
		NoteViewInit (frm);
		NoteViewDrawTitleAndForm (frm);
		NoteViewUpdateScrollBar ();
		FrmSetFocus (frm, FrmGetObjectIndex (frm, NoteField));
		handled = true;
		}
		

	else if (event->eType == frmGotoEvent)
		{
		frm = FrmGetActiveForm ();
		CurrentRecord = event->data.frmGoto.recordNum;
		SetDateToNextOccurrence (CurrentRecord);
		NoteViewInit (frm);
		fld = GetObjectPtr (NoteField);
		pos = event->data.frmGoto.matchPos;
		FldSetScrollPosition (fld, pos);
		FldSetSelection (fld, pos, pos + event->data.frmGoto.matchLen);
		NoteViewDrawTitleAndForm (frm);
		NoteViewUpdateScrollBar ();
		FrmSetFocus (frm, FrmGetObjectIndex (frm, NoteField));
		handled = true;
		}
		

	else if (event->eType == fldChangedEvent)
		{
		frm = FrmGetActiveForm ();
		NoteViewUpdateScrollBar ();
		handled = true;
		}
		

	else if (event->eType == frmUpdateEvent)
		{
		if (event->data.frmUpdate.updateCode & updateFontChanged)
			{
			fld = GetObjectPtr (NoteField);
			FldSetFont (fld, NoteFont);
			NoteViewUpdateScrollBar ();
			}
		else
			{
			frm = FrmGetActiveForm ();
			NoteViewDrawTitleAndForm (frm);
			}
		handled = true;
		}


	else if (event->eType == frmCloseEvent)
		{
		if ( FldGetTextHandle (GetObjectPtr (NoteField)))
			NoteViewSave ();

		// Db35: same fix as DetailsExit's own (see its comment) - a direct
		// note-icon tap on a CalDAV row jumps straight here (DayOpenDavDetails,
		// toNote=true) without ever going through DetailsExit, so ItemSelected
		// was left true with CurrentRecord still a DAV sentinel on every exit
		// from this form (Done/Delete/hard-key save all reach frmCloseEvent).
		// The next tap on any other Day/Week row then skipped its own
		// ItemSelected guard and called DmRecordInfo with the sentinel value -
		// confirmed real: "DataMgr.c, Index out of range".
		if (IsDayDav (CurrentRecord))
			ItemSelected = false;
		}


	else if (event->eType == sclRepeatEvent)
		{
		NoteViewScroll (event->data.sclRepeat.newValue - 
			event->data.sclRepeat.value, false);
		}

	return (handled);
}


#pragma mark ----------------
/***********************************************************************
 *
 * FUNCTION:    DayViewRestoreEditState
 *
 * DESCRIPTION: This routine restores the edit state of the day view,
 *              if the view is in edit mode. This routine is 
 *              called when the time of an appointment is changed, or when
 *              returning from the details dialog or note view.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/28/95	Initial Revision
 *
 ***********************************************************************/
/* Db35: set by the tblEnterEvent tap-to-Details path (see DayViewHandleEvent)
 * right before FrmPopupForm(DetailsDialog) for an EXISTING event -- that row
 * was never put into inline edit (Details opened directly instead), so the
 * frmUpdateEvent this generates on close must NOT re-grab table focus the way
 * it correctly does for the OLD flow (Details button pressed while a row was
 * already mid inline-edit). One-shot: cleared the instant it's consumed. */
static Boolean gDaySkipRestoreEditState = false;

SEC001 static void DayViewRestoreEditState ()
{
	Int16 row;
	UInt32 uniqueID;
	FormPtr frm;
	TablePtr table;
	FieldPtr fld;

	if (gDaySkipRestoreEditState)
		{
		gDaySkipRestoreEditState = false;
		return;
		}

	// Db35: DetailsExit's own ItemSelected=false reset (for a DAV
	// CurrentRecord) makes this check alone sufficient - no DAV-specific
	// code needed here (was tried, cost more sec001 budget than it was
	// worth once confirmed redundant).
	if ( ! ItemSelected) return;

	// Find the row that the current record is in.  It's posible
	// that the current record is no longer displayable (ex: the record
	// has marked private).
	table = GetObjectPtr (DayTable);
	DmRecordInfo (ApptDB, CurrentRecord, NULL, &uniqueID, NULL);

	if ( ! TblFindRowData (table, uniqueID, &row) || TblRowMasked(table,row))
		{
		ClearEditState ();
		return;
		}

	frm = FrmGetActiveForm ();
	FrmSetFocus (frm, FrmGetObjectIndex (frm, DayTable));
	TblGrabFocus (table, row, descColumn);
	
	// Restore the insertion point position.
	fld = TblGetCurrentField (table);
	FldSetInsPtPosition (fld, DayEditPosition);
	if (DayEditSelectionLength)
		FldSetSelection (fld, DayEditPosition, DayEditPosition + DayEditSelectionLength);

	FldGrabFocus (fld);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewClearEditState
 *
 * DESCRIPTION: This routine clears the edit state of the day view.
 *              It is called whenever a table item is selected.
 *
 *              If the new item selected is in a different row than
 *              the current record the edit state is cleared,  and if 
 *              current record is empty it is deleted.
 *
 * PARAMETERS:  newRow - row number of newly table item
 *
 * RETURNED:    true if the current record is deleted and items
 *              of the table have been move around on the display
 *              as a result.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/28/95	Initial Revision
 *			jmp	9/29/99	Use FrmGetFormPtr() & FrmGetObjectIndex() instead of
 *								GetObjectPtr() because GetObjectPtr() calls FrmGetActiveForm(),
 *								and FrmGetActiveForm() may return a form that isn't the one we
 *								want when other forms are up when we are called.
 *								Fixes bug #22418.
 *
 ***********************************************************************/
SEC001 static Boolean DayViewClearEditState (void)
{
	Int16 row;
	UInt16 numAppts;
	UInt32 uniqueID;
	TablePtr table;
	FormPtr frm;

	// Db35: DetailsExit's own ItemSelected=false reset (for a DAV
	// CurrentRecord) makes this check alone sufficient - see
	// DayViewRestoreEditState's own comment on the same simplification.
	if ( ! ItemSelected)
		{
		CurrentRecord = noRecordSelected;
		return (false);
		}

	frm = FrmGetFormPtr (DayView);
	table = FrmGetObjectPtr (frm, FrmGetObjectIndex (frm, DayTable));
	DmRecordInfo (ApptDB, CurrentRecord, NULL, &uniqueID, NULL);
	ErrFatalDisplayIf ( (! TblFindRowData (table, uniqueID, &row)),
		"Wrong record");

	TblReleaseFocus (table);

	// Db35: the row we just left was temporarily demoted to a plain text item for a clean edit;
	// restore its narrowTextTableItem style so its note/repeat icon draws again on the redraw below.
	DayRestoreEditStyle (table);

	// If a different row has been selected, clear the edit state, this
	// will delete the current record if its empty.
	if (ClearEditState ())
		{
		numAppts = NumAppts;

		// Layout the day again, empty time slots may needed to be add to
		// full-in the gap left by the deleted appointment.
		DayViewLayoutDay (true);
		DayViewLoadTable ();
		TblRedrawTable (table);
		DayViewDrawTimeBars ();

		// If the number of appointments has changed as a result of
		// deleting the currently selected empty appointment, then
		// the item in the table have move around and tblEnter event
		// should be ignored.
		return (numAppts != NumAppts);
		}

	// Db35: for an ENCRYPTED appt, TblReleaseFocus above ran the save-proc, which re-encrypted
	// the edit and freed the decrypted copy -- leaving the inline field (hence the cell) blank.
	// The ROM does not redraw the cell on a normal (non-deleting) edit, so force it: the display
	// load-proc re-runs and re-decrypts the row.  Harmless for a plaintext appt (redraws in
	// place).  Without this the edited row goes blank until the next full reload.
	TblMarkRowInvalid (table, row);
	TblRedrawTable (table);

	return (false);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewFindAppointment
 *
 * DESCRIPTION: Given the database index of a record, this routine 
 *              finds the record in the appointment list and returns
 *              the index of the appointment
 *
 * PARAMETERS:  recordNum - db index of record to fine 
 *              recordNum - 
 *
 * RETURNED:   appointment list index
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/27/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static UInt16 DayViewFindAppointment (UInt16 recordNum)
{
	UInt16 i;
	ApptInfoPtr appts;
	
	appts = MemHandleLock (ApptsH);

	for (i = 0; i < NumAppts; i++)
		{
		if (appts[i].recordNum == recordNum)
			{
			MemPtrUnlock (appts);
			return (i);
			}
		}

	MemPtrUnlock (appts);

	// If we're beyond the maximun number of appointment that can be 
	// shown on a day, then the record passed may not be in the list.
	if (NumApptsOnly >= apptMaxPerDay)
		{
		ClearEditState ();
		return (0);
		}

	
	ErrDisplay ("Record not on day");

	return (0);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewSetTopAppointment
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
SEC001 static void DayViewSetTopAppointment (void)
{
	UInt16 i;
	TimeType time;
	DateTimeType dateTime;
	ApptInfoPtr appts;

	TopVisibleAppt = 0;

	// Db35: if ToDo35 and/or birthday/anniversary rows were injected at the
	// top, keep them visible rather than scrolling down to the current time.
	if (gDayTodoCount > 0 || gDayBdayCount > 0)
		return;

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
	
	appts = MemHandleLock (ApptsH);
	for (i = 0; i < NumAppts; i++)
		{
		if (TimeToInt (appts[i].endTime) < TimeToInt (time))
			TopVisibleAppt = i;
		}

	MemPtrUnlock (appts);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewDrawTime
 *
 * DESCRIPTION: This routine draws the start time of an appointment.  This
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
 *			art	6/29/95	Initial Revision
 *			kcr	11/14/95	Display 'no time' char for untimed events
 *			rbb	6/4/99	Moved bulk of code to DrawTime, for sharing w/Agenda
 *
 ***********************************************************************/
// Db35: the 5x5 square that marks a day-view row as a ToDo item.
SEC001 static void DayDrawTodoBox (Coord x, Coord y)
{
	RectangleType r;
	r.topLeft.x = x;  r.topLeft.y = y;
	r.extent.x = 5;   r.extent.y = 5;
	WinDrawRectangleFrame (rectangleFrame, &r);
}

SEC001 static void DayViewDrawTime (void * table, Int16 row, Int16 column,
	RectanglePtr bounds)
{
#pragma unused (column)

	UInt16 apptIndex;
	UInt16 recordNum;
	TimeType startTime;
	ApptInfoPtr appts;


	// Get the appointment index that corresponds to the table item.
	// The index of the appointment in the appointment list, is stored
	// as the row id.
	apptIndex =TblGetRowID (table, row);

	// Get the start time of the appointment.
	appts = MemHandleLock (ApptsH);
	startTime = appts[apptIndex].startTime;
	recordNum = appts[apptIndex].recordNum;
	MemHandleUnlock (ApptsH);

	// Db35: a timed ToDo shows its start time here (the checkbox square that marks
	// it as a todo is drawn in the description column); an untimed/undated todo
	// shows the checkbox square here instead of a time.
	if (IsDayTodo (recordNum))
		{
		UInt16 slot = (UInt16)(recordNum - kDayTodoBase);
		if (slot < gDayTodoCount && gDayTodoTimed[slot])
			DrawTime (gDayTodoStart[slot], TimeFormat, apptTimeFont, rightAlign, bounds);
		else
			DayDrawTodoBox ((Coord)(bounds->topLeft.x + bounds->extent.x - 13),
								 (Coord)(bounds->topLeft.y + 3));
		return;
		}

	// Db35: a birthday/anniversary row falls through to the normal DrawTime()
	// call below, same as any other untimed appointment -- DrawTime already
	// special-cases apptNoTime and draws the centered symbolNoTime (0x13)
	// diamond, which is exactly the marker the Agenda view already shows for
	// these rows (AgendaApptDrawTime calls the same DrawTime unconditionally).
	// No IsDayBday special case needed here; it's earlier ToDo rows that need
	// their own handling above (checkbox instead of a time/diamond).

	DrawTime (startTime, TimeFormat, apptTimeFont, rightAlign, bounds);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewDrawIcons
 *
 * DESCRIPTION: This routine draws the note, alarm and repeat icons.
 *              It is called by the table object as a callback 
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
 *			art	5/16/96	Initial Revision
 *
 ***********************************************************************/
SEC001 static void DayViewDrawIcons (void * table, Int16 row, Int16 column, 
	RectanglePtr bounds)
{
	Char					chr;
	UInt16					apptIndex;
	UInt16 					recordNum;
	Int16					x, y;
	FontID				curFont;
	MemHandle 			recordH;
	ApptInfoPtr 		appts;
	ApptDBRecordType	apptRec;


	// Get the appointment index that corresponds to the table item.
	// The index of the appointment in the appointment list, is stored
	// as the row id.
	apptIndex =TblGetRowID (table, row);

	// Get the start time of the appointment.
	appts = MemHandleLock (ApptsH);
	recordNum = appts[apptIndex].recordNum;
	MemHandleUnlock (ApptsH);

	// Db35: a ToDo35 row -- this is a customTableItem, so we draw the whole cell
	// ourselves (never touching ApptDB): [timed: checkbox square] description text,
	// with note/repeat icons right-aligned, and a dotted underline.
	if (IsDayTodo (recordNum))
		{
		UInt16	slot = (UInt16)(recordNum - kDayTodoBase);
		char*		d = gDayTodoDesc[slot];
		Int16		tx = bounds->topLeft.x;
		Int16		rightW = 0;
		Int16		lineH;

		curFont = FntSetFont (symbolFont);
		if (slot < gDayTodoCount && gDayTodoNote[slot])   rightW += FntCharWidth (symbolNote) + 1;
		if (slot < gDayTodoCount && gDayTodoRepeat[slot]) rightW += FntCharWidth (symbolRepeat) + 1;
		FntSetFont (curFont);

		// timed todo: the 5x5 todo square at the left, text indented past it
		if (slot < gDayTodoCount && gDayTodoTimed[slot])
			{
			DayDrawTodoBox ((Coord)(bounds->topLeft.x + 1), (Coord)(bounds->topLeft.y + 3));
			tx = (Int16)(bounds->topLeft.x + 9);
			}

		curFont = FntSetFont (ApptDescFont);
		WinDrawTruncChars (d, StrLen (d), tx, bounds->topLeft.y,
			(Coord)(bounds->topLeft.x + bounds->extent.x - rightW - tx));
		lineH = FntLineHeight ();
		FntSetFont (curFont);

		// note / repeat icons, right-aligned
		x = (Int16)(bounds->topLeft.x + bounds->extent.x - rightW);
		y = bounds->topLeft.y;
		curFont = FntSetFont (symbolFont);
		if (slot < gDayTodoCount && gDayTodoNote[slot])
			{ chr = symbolNote;   WinDrawChars (&chr, 1, x, y); x += FntCharWidth (chr) + 1; }
		if (slot < gDayTodoCount && gDayTodoRepeat[slot])
			{ chr = symbolRepeat; WinDrawChars (&chr, 1, x, y); }
		FntSetFont (curFont);

		// dotted underline under the text only -- it stops before the right icons
		// (the note/repeat icons sit on the plain background, per the design).
		y = (Int16)(bounds->topLeft.y + lineH - 1);
		WinDrawGrayLine (bounds->topLeft.x, y,
			(Coord)(bounds->topLeft.x + bounds->extent.x - 1 - (rightW ? rightW + 1 : 0)), y);
		return;
		}

	// Db35: a birthday/anniversary row -- also a customTableItem, drawn in full
	// here (never touching ApptDB). Simpler than the ToDo case: always untimed
	// (no checkbox), no note/repeat icons -- just the "Name - Nth Birthday" text
	// and a dotted underline, same style. The untimed "diamond" marker (0x13)
	// is drawn in the TIME column instead (DayViewDrawTime falls through to the
	// shared DrawTime(), same as the Agenda view) -- not repeated here.
	if (IsDayBday (recordNum))
		{
		UInt16	slot = (UInt16)(recordNum - kDayBdayBase);
		char*		d = (slot < gDayBdayCount) ? gDayBdayLines[slot] : "";
		Int16		lineH;

		curFont = FntSetFont (ApptDescFont);
		WinDrawTruncChars (d, StrLen (d), bounds->topLeft.x, bounds->topLeft.y,
			bounds->extent.x);
		lineH = FntLineHeight ();
		FntSetFont (curFont);

		y = (Int16)(bounds->topLeft.y + lineH - 1);
		WinDrawGrayLine (bounds->topLeft.x, y,
			(Coord)(bounds->topLeft.x + bounds->extent.x - 1), y);
		return;
		}

	// Db35: a CalDAV Sync event row is a textTableItem (multi-line wrap,
	// matching a real appointment) for the TEXT itself, but one with a
	// note and/or repeat reserves icon space (DayDavCalcHeight) and so
	// becomes a narrowTextTableItem, same as a real appointment's own
	// note/repeat icons - this callback DOES fire for that reserved area.
	if (IsDayDav (recordNum))
		{
		UInt16	slot = (UInt16)(recordNum - kDayDavBase);
		Boolean	hasNote, hasRepeat;
		(void) DayDavIconsWidth (slot, &hasNote, &hasRepeat);
		if (hasNote || hasRepeat)
			{
			x = bounds->topLeft.x + bounds->extent.x - TblGetItemInt (table, row, column);
			y = bounds->topLeft.y;
			curFont = FntSetFont (symbolFont);
			if (hasNote)
				{
				chr = symbolNote;
				WinDrawChars (&chr, 1, x, y);
				x += FntCharWidth (chr) + 1;
				}
			if (hasRepeat)
				{
				chr = symbolRepeat;
				WinDrawChars (&chr, 1, x, y);
				}
			FntSetFont (curFont);
			}
		return;
		}

	ApptGetRecord (ApptDB, recordNum, &apptRec, &recordH);

	x = bounds->topLeft.x + bounds->extent.x - TblGetItemInt (table, row, column);
	y = bounds->topLeft.y;
	curFont = FntSetFont (symbolFont);

	// Draw note icon
	if (apptRec.note)
		{
		chr = symbolNote;
		WinDrawChars (&chr, 1, x, y);
		x += FntCharWidth (chr) + 1;
		}

	// Draw repeat icon
	if (apptRec.repeat)
		{
		chr = symbolRepeat;
		WinDrawChars (&chr, 1, x, y);
		x += FntCharWidth (chr) + 1;
		}

	FntSetFont (curFont);

	MemHandleUnlock (recordH);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewCheckForConflicts
 *
 * DESCRIPTION: This routine check the apointment list for conflicts
 *              (overlapping appointmens).
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    the number of columns of time bars necessary to display
 *              the conflicts
 *              
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	5/3/96	Initial Revision
 *
 ***********************************************************************/
SEC001 static UInt16 DayViewCheckForConflicts (void)
{
	UInt16				i;
	UInt16				numColumns;
	UInt16				apptIndex;
	UInt16				width;
	TablePtr			table;
	TimeType			endTime [maxTimeBarColumns];
	ApptInfoPtr		appts;
	RectangleType	tableR;
	
	MemSet (endTime, sizeof (endTime), 0);

	numColumns = 1;

	appts = MemHandleLock (ApptsH);
	for (apptIndex = 0; apptIndex < NumAppts; apptIndex++)
		{
		if (appts[apptIndex].recordNum == emptySlot)
			continue;

		else if (TimeToInt (appts[apptIndex].startTime) == apptNoTime)
			continue;
		
		for (i = 0; i < maxTimeBarColumns; i++)
			{
			if (TimeToInt (appts[apptIndex].startTime) >= TimeToInt (endTime[i]))
				{
				endTime[i] = appts[apptIndex].endTime;
				if (i+1 > numColumns)
					numColumns = i+1;
				break;
				}
			}
		}
	MemPtrUnlock (appts);
	

	// Reserve spase for the time bars.  We will show time bars if the user
	// has requested them or if there are overlapping appointments.
	if (numColumns == 1 && (! ShowTimeBars))
		numColumns = 0;

	if (TimeBarColumns != numColumns)
		{
		TimeBarColumns = numColumns;
		table = GetObjectPtr (DayTable);
		
		// Set the width of the time bar table column.
		TblSetColumnWidth (table, timeBarColumn, numColumns * timeBarWidth);
		
		// Adjust the width of the description column.
		TblGetBounds (table, &tableR);
		width = tableR.extent.x - 
				  (numColumns * timeBarWidth) -
 				  TblGetColumnWidth (table, timeColumn) -
 				  TblGetColumnSpacing (table, timeColumn);
 		TblSetColumnWidth (table, descColumn, width);
 		
 		// Invalid the whole table since the positions of the time and 
 		// description columns has changed.
 		TblMarkTableInvalid (table);
		}

	return (numColumns);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewDrawTimeBars
 *
 * DESCRIPTION: This routine draw the time bars the indicate the durations
 *              of apointments.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *              
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	5/3/96	Initial Revision
 *			rbb	4/9/99	Removed time bar for zero-duration appts
 *			jmp	9/29/99	Use FrmGetFormPtr() & FrmGetObjectIndex() instead of
 *								GetObjectPtr() because GetObjectPtr() calls FrmGetActiveForm(),
 *								and FrmGetActiveForm() may return a form that isn't the one we
 *								want when other forms are up when we are called.
 *								Fixes bug #22481.
 ***********************************************************************/
SEC001 static void DayViewDrawTimeBars (void)
{
	Int16				i, j;
	Int16				apptIndex;
	Int16				numColumns;
	Int16				row;
	Int16				lineHeight;
	Int16				x, y1, y2;
	FontID			curFont;
	TablePtr			table;
	Boolean			drawTop;
	Boolean			drawBottom;
	Int16				endPoint [maxTimeBarColumns];
	TimeType			endTime [maxTimeBarColumns];
	ApptInfoPtr		appts;
	RectangleType	r;
	RectangleType	tableR;
	RectangleType	eraseR;
	FormPtr			frm;

	if (! TimeBarColumns) return;

	MemSet (endTime, sizeof (endTime), 0);
	numColumns = 1;

	frm = FrmGetFormPtr (DayView);
	if (! FrmVisible (frm)) return;
	
	table = FrmGetObjectPtr (frm, FrmGetObjectIndex (frm, DayTable));
	TblGetBounds (table, &tableR);

	for (i = 0; i < maxTimeBarColumns; i++)
		endPoint[i] = tableR.topLeft.y;

 	WinPushDrawState();
	curFont = FntSetFont (apptTimeFont);
	lineHeight = FntLineHeight ();
	FntSetFont (curFont);

	appts = MemHandleLock (ApptsH);
	for (apptIndex = 0; apptIndex < NumAppts; apptIndex++)
		{
		if (appts[apptIndex].recordNum == emptySlot)
			continue;
		else if (IsDayTodo (appts[apptIndex].recordNum))	// Db35: no time bar for todos
			continue;
		else if (TimeToInt (appts[apptIndex].startTime) == apptNoTime)
			continue;

		for (i = 0; i < maxTimeBarColumns; i++)
			{
			if (i == 0)
				WinSetForeColor(UIColorGetTableEntryIndex(UIObjectForeground));
			else
				WinSetForeColor(UIColorGetTableEntryIndex(UIWarning));
			
			if (TimeToInt (appts[apptIndex].startTime) >= TimeToInt (endTime[i]))
				{
				endTime[i] = appts[apptIndex].endTime;
				
				// Find the row that hold the appointment, it may not be
				// visible.
				if (TblFindRowID (table, apptIndex, &row))
					{
					TblGetItemBounds (table, row, descColumn, &r);
					y1 = r.topLeft.y + (lineHeight >> 1);
					drawTop = true;
					}

				// Is the appointment off the top of the display.
				else if (apptIndex < TopVisibleAppt)
					{
					y1 = tableR.topLeft.y;
					drawTop = false;
					}

				// If the appointment is below the bottom of the display we
				// don't draw anything.	
				else 
					break;

				// If the start time matches the end time we don't draw anything
				if ( TimeToInt (appts[apptIndex].startTime) ==
						TimeToInt (appts[apptIndex].endTime) )
					break;				

				// Find the row that contains the end time of the appointment.
				for (j = apptIndex + 1; j < NumAppts; j++)
					{
					// There may be more the one time slot with the time
					// we're searching for, get the last one.
					if (TimeToInt (appts[apptIndex].endTime) <=
						 TimeToInt (appts[j].startTime))
						break;
					}

				// Is the end-time visible.
				if (TblFindRowID (table, j, &row))
					{
					TblGetItemBounds (table, row, descColumn, &r);
					y2 = r.topLeft.y + (lineHeight >> 1);
					drawBottom = true;				
					}

				// Is the end of the appointment off the top of the display, if so
				// don't draw anything.
				else if (j < TopVisibleAppt)
					break;

				else
					{
					y2 = tableR.topLeft.y + tableR.extent.y - 1;
					drawBottom = false;				
					}

				x = tableR.topLeft.x + (i * timeBarWidth);


				// Erase the region between the top of the time bar we're
				// about to draw and the bottom of the previous time bar.
				if (y1 > endPoint[i])
					{
					eraseR.topLeft.x = x;
					eraseR.topLeft.y = endPoint[i];
					eraseR.extent.x = timeBarWidth;
					eraseR.extent.y = y1 - endPoint[i];
					WinEraseRectangle (&eraseR, 0);
					}
				endPoint[i] = y2 + 1;
				

				// Draw the time bar.
				WinEraseLine (x+1, y1+1, x+1, y2-2);

				if (drawTop) y1++;
				if (drawBottom) y2--;

				WinDrawLine (x, y1, x, y2);
				if (drawTop)
					WinDrawLine (x, y1, x + timeBarWidth - 1, y1);
				if (drawBottom)
					WinDrawLine (x, y2, x + timeBarWidth - 1, y2);
				
				if (i+1 > numColumns)
					numColumns = i+1;

				break;
				}
			}
		}
		
	// Erase the regions between the botton of the last time bar in 
	// each column and the bottom of the table.
	for (i = 0; i < numColumns; i++)
		{
		if (tableR.topLeft.y + tableR.extent.y > endPoint[i])
			{
			eraseR.topLeft.x = tableR.topLeft.x + (i * timeBarWidth);
			eraseR.topLeft.y = endPoint[i];
			eraseR.extent.x = timeBarWidth;
			eraseR.extent.y = tableR.topLeft.y + tableR.extent.y - endPoint[i];
			WinEraseRectangle (&eraseR, 0);
			}
		}
	
	MemPtrUnlock (appts);
 	WinPopDrawState();
	}


/***********************************************************************
 *
 * FUNCTION:    DayViewInsertAppointment
 *
 * DESCRIPTION: This routine inserts the record index of a new record 
 *              into the structure that keeps track of the appointment
 *              on the current day.
 *
 * PARAMETERS:  apptIndex - 
 *              recordNum - appointment record index
 *              row       - row in the table object of the new record
 *
 * RETURNED:   nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/28/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static void DayViewInsertAppointment (UInt16 apptIndex, UInt16 recordNum, UInt16 row)
{
	UInt16 i;
	UInt32 uniqueID;
	TablePtr table;
	ApptInfoPtr appts;
	
	appts = MemHandleLock (ApptsH);

	// Adjust all the record index that are greater than the new record.
	// (Db35: leave ToDo35/birthday sentinel rows untouched -- they are not
	// ApptDB records.)
	for (i = 0; i < NumAppts; i++)
		{
		// Db35: "not emptySlot && not any sentinel row" collapses to a single
		// comparison - emptySlot (0xffff) and every sentinel range all sit
		// at/above kDayDavBase, so excluding rn>=kDayDavBase already
		// excludes emptySlot too (sec001 is hard-full; every byte matters).
		if ((appts[i].recordNum < kDayDavBase) &&
			 (appts[i].recordNum >= recordNum))
			appts[i].recordNum++;
		}

	appts[apptIndex].recordNum = recordNum;
//	appts[apptIndex].endTime.hours = appts[apptIndex].startTime.hours + 1;

	MemHandleUnlock (ApptsH);

	// Store the unique id of the record in the row.
	table = GetObjectPtr (DayTable);
	DmRecordInfo (ApptDB, recordNum, NULL, &uniqueID, NULL);
	TblSetRowData (table, row, uniqueID);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewGetDescription
 *
 * DESCRIPTION: This routine returns a pointer to the description field
 *              of a appointment record.  This routine is called by 
 *              the table object as a callback routine when it wants to 
 *              display or edit the appointment's description.
 *
 * PARAMETERS:  table         - pointer to the Day View table (TablePtr)
 *              row           - row in the table
 *              column        - column in the table
 *              editable      - true if the field will be edited by the table
 *              textOffset    - offset within the record of the desc field (returned)
 *              textAllocSize - allocated size the the description field (returned)
 *
 * RETURNED:    handle of the appointment record
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/29/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static Err DayViewGetDescription (void * table, Int16 row, Int16 column,
	Boolean editable, MemHandle * textH, Int16 * textOffset, 
	Int16 * textAllocSize, FieldPtr fld)
{
#pragma unused (column)

	Err error = 0;
	UInt16 apptIndex;
	UInt16 recordNum;
	Char* recordP;
	MemHandle recordH;
	Boolean redraw = false;
	FieldAttrType attr;
	ApptInfoPtr appts;
	ApptDBRecordType apptRec;
	ApptDateTimeType when;
//	UInt16 height;
//	UInt16 iconsWidth;
//	UInt16 tableHeight;
//	UInt16 columnWidth;
//	UInt32 uniqueID;
//	FontID fontID;
//	RectangleType r;
	
	*textH = 0;

	// Get the appoitment that corresponds to the table item.
	// The index of the appointment in the appointment list, is stored
	// as the row id.
	apptIndex = TblGetRowID (table, row);

	// Get the record index of the next appointment,  empty time slots
	// have a minus one in the recordNum field.
	appts = MemHandleLock (ApptsH);
	recordNum = appts[apptIndex].recordNum;
	MemHandleUnlock (ApptsH);

	// Db35: any sentinel row (ToDo35/birthday/CalDAV) - none of these are
	// real ApptDB records. ToDo/birthday are customTableItems (never
	// editable; should not reach here, but guard anyway). CalDAV IS a
	// textTableItem (so the ROM's own Field Manager wraps it to multiple
	// lines, matching a real appointment's own description - the original
	// design this reader was supposed to match) - relayed through a
	// sec002 helper since sec001 has almost no headroom left. One shared
	// membership test (IsDaySentinelRow) instead of two separate ones,
	// distinguishing only inside - cheaper than checking twice.
	if (IsDaySentinelRow (recordNum))
		return IsDayDav (recordNum)
			? DayDavGetDescHandle (row, recordNum, textH, textOffset, textAllocSize)
			: 0;

	if (recordNum == emptySlot)
		{
		// If we're drawing the description, return a null MemHandle.
		if  (! editable)
			return (0);

		// If we have reached the maximum number of displayable event, then
		// exit returning an error.
		if (NumApptsOnly >= apptMaxPerDay)
			return (-1);


		// If we're editing the description, create a new record.
		MemSet (&apptRec, sizeof (apptRec), 0);

		appts = MemHandleLock (ApptsH);
		when.startTime = appts[apptIndex].startTime;

		// If the start time is before 11:00 pm, the end time is one hour 
		// after the start time.
		if (when.startTime.hours < maxHours)
			{
			when.endTime.hours = when.startTime.hours + 1;
			when.endTime.minutes = when.startTime.minutes;
			}
			
		// If the start time is 11:00 pm or later, the end time is 11:55 pm. 
		else
			{
			when.endTime.hours = maxHours;
			when.endTime.minutes = maxMinutes;
			}

			// Don't let the new appointment overlap the next appointment.
		if (((apptIndex+1) < NumAppts) &&
			 (TimeToInt(when.endTime) > TimeToInt(appts[apptIndex+1].startTime)))
			{
			when.endTime = appts[apptIndex+1].startTime;
			appts[apptIndex].endTime = when.endTime;
			}

			// If the end time of the new event is not currently displayed, then
		// don't redraw the time bars.  We need to redraw the day, but we cannot
		// do it here.  We'll redraw the row on the tblSelect event.
		if (((apptIndex+1) == NumAppts) ||
			 (((apptIndex+1) < NumAppts) &&
				(TimeToInt(when.endTime) < TimeToInt(appts[apptIndex+1].startTime))))
			{
			redraw = true;
			TblMarkRowInvalid (table, row);
			}

		// If the description font is a different than the empty appointment font 
		// then we need to redraw the day, but we cannot
		// do it here.  We'll redraw the row on the tblSelect event.
		if (ApptDescFont != apptEmptyDescFont)
			{
			redraw = true;
			TblMarkRowInvalid (table, row);
			}

		MemHandleUnlock (ApptsH);

		when.date = Date;
		apptRec.when = &when;

		// Make sure the record has a description field so that we have
		// something to edit.
		apptRec.description = "";
		/* Db35: alarms removed -- new events never carry a preset alarm. */

		error = ApptNewRecord (ApptDB, &apptRec, &recordNum);

		if (error)
			{
			FrmAlert (DeviceFullAlert);
			return (error);
			}

		DayViewInsertAppointment (apptIndex, recordNum, row);

		// Db35: alarms removed -- new events never carry a preset alarm, so
		// there is no alarm icon to reinitialize or alarm to reschedule here.

		if (! redraw)
			DayViewDrawTimeBars ();
		}


	// Get the offset and length of the description field 
	ApptGetRecord (ApptDB, recordNum, &apptRec, &recordH);
	
	
	if (apptRec.description == NULL)
		{
		ApptDBRecordFlags changedFields;
		
		
		// Add the note to the record.
		MemSet (&changedFields, sizeof (changedFields), 0);
		changedFields.description = true;
		apptRec.description = "";
		error = ApptChangeRecord (ApptDB, &recordNum, &apptRec, changedFields);
		if (error) return error;
		
		ApptGetRecord (ApptDB, recordNum, &apptRec, &recordH);
		}
	
	// Db35: an ENCRYPTED appointment is never edited in place (storage stays ciphertext).
	// The load-proc is called for BOTH drawing (editable == false) and inline editing
	// (editable == true); the ROM caches the returned handle PER ROW for the whole draw, so a
	// DISPLAY call must hand back a stable per-row handle (gDayRowH[]), while the one row being
	// edited uses gDayEditH (re-encrypted by DayViewSaveDescription).  Mirrors ToDo35.
	if (ApptRecEncrypted (ApptDB, recordNum))
		{
		Boolean locked = ! EncKeyPresent ();
		MemHandle h;

		MemHandleUnlock (recordH);				/* release ApptGetRecord's (gApptDecH) lock */

		// --- Locked: masked "******", per-row DISPLAY copy.  A tap on a locked encrypted row
		// bounces to Sec35 (DayLockedBounce in the tap handler) before it can be edited, so we
		// must NOT write attr.editable here: writing editable in the load-proc makes the ROM
		// draw the item selected/inverted (whole-row black bar).  Only autoShift. ---
		if (locked)
			{
			h = DayMaskCopy ();
			if (h == NULL) return (-1);
			*textH = DaySetRowCopy (row, h);
			*textOffset = 0;
			*textAllocSize = 7;
			return (0);
			}

		// --- Unlocked + EDITING: the ONE active decrypted copy (re-encrypted on save). ---
		if (editable)
			{
			if (! (gDayEditH != NULL && gDayEditRow == row))
				{
				if (gDayEditH) { MemHandleFree (gDayEditH); gDayEditH = NULL; }
				gDayEditH = DayCopyDescPlain (recordNum);
				gDayEditRow = row;
				}
			if (gDayEditH != NULL)
				{
				*textH = gDayEditH;
				*textOffset = 0;
				*textAllocSize = (Int16) MemHandleSize (gDayEditH);
				if (fld)
					{
					FldGetAttributes (fld, &attr);
					attr.autoShift = true;
					FldSetAttributes (fld, &attr);
					}
				return (0);
				}
			}
		else
			{
			// --- Unlocked + DISPLAY: per-row decrypted copy. The "@ location" suffix
			// (below, for the non-encrypted path) never used to run for encrypted rows
			// at all -- this branch always returned first -- so an encrypted
			// appointment's location silently never showed no matter what. Apply the
			// same suffix here, built from the just-decrypted plain text. ---
			h = DayCopyDescPlain (recordNum);
			if (h != NULL)
				{
				if (apptIndex < kMaxDayLocRows && gDayLocText[apptIndex][0])
					{
					char*		plain = (char*) MemHandleLock (h);
					MemHandle	sh = DayBuildLocSuffix (plain, gDayLocText[apptIndex]);
					MemHandleUnlock (h);
					if (sh != NULL)
						{
						MemHandleFree (h);
						h = sh;
						}
					}
				*textH = DaySetRowCopy (row, h);
				*textOffset = 0;
				*textAllocSize = (Int16) MemHandleSize (h);
				return (0);
				}
			}

		// Safety net: decryption failed unexpectedly -> masked per-row copy.  (No attr.editable
		// write -- see the locked branch above.)
		h = DayMaskCopy ();
		if (h == NULL) return (-1);
		*textH = DaySetRowCopy (row, h);
		*textOffset = 0;
		*textAllocSize = 7;
		return (0);
		}

	// Db35: append " @ <location>" when DISPLAYING (not editing) a row that
	// has a linked location (gDayLocText, prefetched once per day-load --
	// DayInjectLocationText). Editing shows the plain description only.
	// KNOWN LIMITATION (per user direction, not being actively worked):
	// needs 2 taps on first entry into a row before the suffix clears --
	// see [[app-reference-docs]] / memory for the failed alternate attempts.
	// Do not "fix" this again without being asked.
	if (! editable && apptIndex < kMaxDayLocRows && gDayLocText[apptIndex][0])
		{
		MemHandle	h = DayBuildLocSuffix (apptRec.description, gDayLocText[apptIndex]);
		if (h != NULL)
			{
			*textH = DaySetRowCopy (row, h);
			*textOffset = 0;
			*textAllocSize = (Int16) MemHandleSize (h);
			MemHandleUnlock (recordH);	/* undo ApptGetRecord's lock -- we never
										 * took a 2nd one (no recordP needed here) */
			return (0);
			}
		}

	recordP = MemHandleLock(recordH);
	*textOffset = apptRec.description - recordP;
	*textAllocSize = StrLen (apptRec.description) + 1;  // one for null terminator
	*textH = recordH;
	MemHandleUnlock (recordH);
	MemHandleUnlock (recordH);  // MemHandle was also locked in ApptGetRecord

	// Set the field to support auto-shift.  (Match original: do NOT force editable here --
	// the ROM sets edit-mode editability; writing it in the load-proc made the item draw
	// selected/inverted instead of a clean cursor edit.)
	if (fld)
		{
		FldGetAttributes (fld, &attr);
		attr.autoShift = true;
		FldSetAttributes (fld, &attr);
		}

	return (0);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewSaveDescription
 *
 * DESCRIPTION: This routine saves the description field of a appointment 
 *              to its db record.  This routine is called by the table 
 *              object, as a callback routine, when it wants to save
 *              the description.
 *
 * PARAMETERS:  table  - pointer to the memo Day table (TablePtr)
 *              row    - row of the table to draw
 *              column - column of the table to draw 
 *
 * RETURNED:    true if the table needs to be redrawn
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/29/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static Boolean DayViewSaveDescription (void * table, Int16 row, Int16 column)
{
#pragma unused (column)

	UInt16 recordNum;
	UInt16 apptIndex;
	UInt16 selectStart, selectEnd;
	Boolean dirty;
	FieldPtr fld;
	ApptInfoPtr appts;
	
	
	// Get the appointment that corresponds to the table item passed.
	apptIndex = TblGetRowID (table, row);

	// Get the record index of the appointment,  empty time slots 
	// have a minus one in the recordNum field.
	appts = MemHandleLock (ApptsH);
	recordNum = appts[apptIndex].recordNum;
	MemHandleUnlock (ApptsH);

	ErrFatalDisplayIf ( (recordNum == emptySlot), "Error saving appointment");


	// Db35: if this row was edited through a decrypted COPY (encrypted appt), re-encrypt the
	// copy's text back into storage via ApptChangeRecord (which repacks + re-seals), then
	// detach + free the copy.  Storage therefore never holds a plaintext description.
	if (gDayEditH != NULL && gDayEditRow == row)
		{
		fld = TblGetCurrentField (table);
		dirty = FldDirty (fld);
		if (dirty)
			{
			Char* newDesc = (Char*) FldGetTextPtr (fld);
			ApptDBRecordType r;
			ApptDBRecordFlags changed;
			MemSet (&r, sizeof (r), 0);
			r.description = newDesc ? newDesc : "";
			MemSet (&changed, sizeof (changed), 0);
			changed.description = true;
			ApptChangeRecord (ApptDB, &recordNum, &r, changed);
			DirtyRecord (ApptDB, recordNum);
			}
		FldSetTextHandle (fld, NULL);			/* detach before we free our copy */
		MemHandleFree (gDayEditH);
		gDayEditH = NULL;
		gDayEditRow = -1;
		RecordDirty = dirty;
		return (false);
		}


	// If the description has been modified mark the record dirty,  any
	// change made to the description were written directly to the
	// record by the field object.
	fld = TblGetCurrentField (table);
	dirty = FldDirty (fld);
	if (dirty)
		DirtyRecord (ApptDB, recordNum);


	// Save the dirty state, we'll need it if we auto-delete an empty record.
	RecordDirty = dirty;

	// Check if the top of the description is scroll off the top of the 
	// field, if it is then redraw the field.
	if (FldGetScrollPosition (fld))
		{
		FldSetSelection (fld, 0, 0);
		FldSetScrollPosition (fld, 0);
		DayEditPosition = 0;
		DayEditSelectionLength = 0;
		}

	// Save the insertion point position, and length of the selection.  
	// We'll need the insertion point position an selection length
	// if we put the table back into edit mode.
	else
		{
		DayEditPosition = FldGetInsPtPosition (fld);
		
		FldGetSelection (fld, &selectStart, &selectEnd);
		DayEditSelectionLength = selectEnd - selectStart;
		if (DayEditSelectionLength)
			DayEditPosition = selectStart;
		}

	return (false);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewGetDescriptionHeight
 *
 * DESCRIPTION: This routine returns the height, in pixels, of the 
 *              description field of an appointment record.
 *
 * PARAMETERS:  apptIndex   - index in appointment list
 *              width       - width of the description column
 * 				 maxHeight   - the maximum height of the field
 *              iconsWidthP - space ot reserve for note, alarm and 
 *                            repeat icons
 *              fontIdP     - font id to draw the text with,
 *
 * RETURNED:    height in pixels
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/29/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static Int16 DayViewGetDescriptionHeight (Int16 apptIndex, Int16 width, Int16 maxHeight,
	Int16 * iconsWidthP, FontID * fontIdP)
{
	Int16 height;
	Int16 iconsWidth;
	UInt16 recordNum;
	FontID curFont;
	MemHandle recordH;
	ApptInfoPtr	appts;
	ApptDBRecordType apptRec;
	UInt16 			attr;


	// Get the record index.
	appts = MemHandleLock (ApptsH);
	recordNum = appts[apptIndex].recordNum;
	MemPtrUnlock (appts);

	iconsWidth = 0;

	// Db35: a CalDAV Sync event row - wraps to multiple lines exactly like a
	// real appointment's own description (FldCalcFieldHeight), not a fixed
	// single line, per the original design. Relayed through a sec002
	// helper (DayDavCalcHeight) - sec001 has almost no headroom left.
	if (IsDayDav (recordNum))
		{
		height = DayDavCalcHeight (recordNum, width, maxHeight, &iconsWidth);
		*fontIdP = ApptDescFont;
		}

	// Empty time slot?
	else if (recordNum == emptySlot)
		{
		curFont = FntSetFont (apptTimeFont);
		height = FntLineHeight ();
		FntSetFont (curFont);
		*fontIdP = apptTimeFont;
		}

	// Db35: real (non-sentinel) appointment record - recordNum < kDayDavBase
	// is every sentinel range's own shared lower bound, so this one
	// comparison excludes ALL of them at once (cheaper than the ToDo/Bday
	// check this branch used to need before Dav/emptySlot above already
	// ruled themselves out - the true ToDo/birthday case below then needs
	// NO check at all, being the final implicit else. sec001 is hard-full;
	// every comparison removed matters).
	else if (recordNum < kDayDavBase)
		{
		// Get the appointment record.
		ApptGetRecord (ApptDB, recordNum, &apptRec, &recordH);


		DmRecordInfo (ApptDB, recordNum, &attr, NULL, NULL);
	   if (((attr & dmRecAttrSecret) && PrivateRecordVisualStatus == maskPrivateRecords))
	   	{
	   	//masked
			curFont = FntSetFont (apptTimeFont);
			height = FntLineHeight ();
			*fontIdP = apptTimeFont;
			FntSetFont (curFont);
			}
		else
			{
			//unmasked - relayed through a sec002 helper (does its own font
			// save/restore internally) since sec001 has almost no headroom
			// left; this used to be ~35 lines of real logic inline here.
			height = DayApptCalcUnmaskedHeight (recordNum, apptIndex, &apptRec,
				width, maxHeight, &iconsWidth);
			*fontIdP = ApptDescFont;
			}

		MemHandleUnlock (recordH);
		}

	// Db35: must be a ToDo35/birthday row - every other possibility (Dav,
	// emptySlot, real appointment) was already ruled out above, so no
	// check is needed here at all. Always a single line drawn in the
	// description font.
	else
		{
		curFont = FntSetFont (ApptDescFont);
		height = FntLineHeight ();
		FntSetFont (curFont);
		*fontIdP = ApptDescFont;
		}

	*iconsWidthP = iconsWidth;
	return (height);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewUpdateScrollers
 *
 * DESCRIPTION: This routine draws or erases the Day View scroll arrow
 *              buttons.
 *
 * PARAMETERS:  frm             -  pointer to the to do Day form
 *              bottomAppt      -  record index of the last visible record
 *              lastItemClipped - true if the list is partially off the 
 *                                display
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/28/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static void DayViewUpdateScrollers (FormPtr frm, UInt16 bottomAppt,
	Boolean lastItemClipped)
{
	UInt16 upIndex;
	UInt16 downIndex;
	Boolean scrollableUp;
	Boolean scrollableDown;
	
	// If the first appointment displayed is not the fist appointment
	// of the day, enable the up scroller.
	scrollableUp = (TopVisibleAppt > 0);

	// If the last appointment displayed is not the last appointment
	// of the day or if it partially clipped, enable the down scroller.
	scrollableDown = ( lastItemClipped || (bottomAppt+1 < NumAppts) );

	// Update the scroll button.
	upIndex = FrmGetObjectIndex (frm, DayUpButton);
	downIndex = FrmGetObjectIndex (frm, DayDownButton);
	FrmUpdateScrollers (frm, upIndex, downIndex, scrollableUp, scrollableDown);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewInitRow
 *
 * DESCRIPTION: This routine initializes a row in the Day View table.
 *
 * PARAMETERS:  table      - pointer to the table of appointments
 *              row        - row number (first row is zero)
 *              apptIndex  - index in appointment list
 *              rowHeight  - height of the row in pixels
 *              uniqueID   - unique ID of appointment record
 *              iconsWidth - spase to reserve for note, alarm and repeat
 *                           icons
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/29/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static void DayViewInitRow (TablePtr table, UInt16 row, UInt16 apptIndex, 
	Int16 rowHeight, UInt32 uniqueID, UInt16 iconsWidth, FontID fontID)
{
	UInt16 time;
	UInt16 rn;					/* Db35 */
	Boolean isSentinel;			/* Db35: ToDo or birthday/anniversary row */
	ApptInfoPtr	appts;

	// Make the row usable.
	TblSetRowUsable (table, row, true);

	// Set the height of the row to the height of the description.
	TblSetRowHeight (table, row, rowHeight);

	// Store the record number as the row id.
	TblSetRowID (table, row, apptIndex);

	// Store the start time of the appointment in the table.
	appts = MemHandleLock(ApptsH);
	time = TimeToInt(appts[apptIndex].startTime);
	rn = appts[apptIndex].recordNum;			/* Db35 */
	MemHandleUnlock(ApptsH);
	/* Db35: Dav is deliberately excluded here - unlike ToDo/birthday it IS
	 * a textTableItem (falls into the "!iconsWidth" branch below, same as
	 * a real appointment), so the ROM wraps it to multiple lines. */
	isSentinel = (Boolean) IsDayTodoOrBday (rn);	/* Db35 */
	TblSetItemInt (table, row, timeColumn, time);

	// Store the unique id of the record in the row.
	TblSetRowData (table, row, uniqueID);

	// Set the table item type for the description,  it will differ depending
	// on the presents of a note.
	if (isSentinel)					/* Db35: non-editable, custom-drawn Todo/birthday row */
		{
		TblSetItemStyle (table, row, descColumn, customTableItem);
		TblSetItemInt (table, row, descColumn, 0);
		}
	else if (! iconsWidth)
		{
		TblSetItemStyle (table, row, descColumn, textTableItem);
		TblSetItemInt (table, row, descColumn, 0);
		}
	else
		{
		TblSetItemStyle (table, row, descColumn, narrowTextTableItem);
		TblSetItemInt (table, row, descColumn, iconsWidth);
		}


	// Set the font used to draw the text of the row.
	TblSetItemFont (table, row, descColumn, fontID);

	// Mark the row invalid so that it will draw when we call the 
	// draw routine.
	TblMarkRowInvalid (table, row);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewLoadTable
 *
 * DESCRIPTION: This routine reloads appointment database records into
 *              the Day view.  This routine is called when:
 *              	o A new item is inserted
 *              	o An item is deleted
 *              	o The time of an items is changed
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/29/95	Initial Revision
 *			jmp	9/29/99	Use FrmGetFormPtr() & FrmGetObjectIndex() instead of
 *								GetObjectPtr() because GetObjectPtr() calls FrmGetActiveForm(),
 *								and FrmGetActiveForm() may return a form that isn't the one we
 *								want when other forms are up when we are called.
 *								Fixes bug #22418.
 ***********************************************************************/
SEC001 static void DayViewLoadTable (void)
{
	Int16 apptIndex;
	Int16 row;
	UInt16 numRows;
	UInt16 recordNum;
	UInt16 lastAppt;
	Int16 iconsWidth;
	Int16 lineHeight;
	Int16 dataHeight;
	Int16 tableHeight;
	Int16 columnWidth;
	UInt16 pos, oldPos;
	UInt16 height, oldHeight;
	UInt32	uniqueID;
	FontID fontID;
	FontID currFont;
	FormPtr frm;
	TablePtr table;
	Boolean init;
	Boolean rowUsable;
	Boolean rowsInserted = false;
	Boolean lastItemClipped;
	ApptInfoPtr	appts;
	RectangleType r;
	UInt16 attr;
	Boolean masked;
	Boolean hasLoc;
	UInt16 descHash;

	// Db35: this re-inits every row's item style (DayViewInitRow), so drop any pending
	// "demoted for edit" tracking to avoid restoring a stale/shifted row later.
	gDayEditStyleRow = -1;
	gDayEditStyleIcons = 0;

	appts = MemHandleLock (ApptsH);

	frm = FrmGetFormPtr (DayView);
	
	// Get the height of the table and the width of the description
	// column.
	table = FrmGetObjectPtr (frm, FrmGetObjectIndex (frm, DayTable));
	TblGetBounds (table, &r);
	tableHeight = r.extent.y;
	columnWidth = TblGetColumnWidth (table, descColumn);


	// If we currently have a selected record, make sure that it is not
	// above the first visible record.
	if (CurrentRecord != noRecordSelected)
		{
		apptIndex = DayViewFindAppointment (CurrentRecord);
		if (apptIndex < TopVisibleAppt)
			TopVisibleAppt = apptIndex;
		}

	apptIndex = TopVisibleAppt;
	lastAppt = apptIndex;

	// Load records into the table.
	row = 0;
	dataHeight = 0;
	oldPos = pos = 0;

	while (apptIndex < NumAppts)
		{		
		// Compute the height of the appointment's description.
		height = DayViewGetDescriptionHeight (apptIndex, columnWidth, tableHeight, &iconsWidth, &fontID);

		// Is there enought room for at least one line of the the decription.
		currFont = FntSetFont (fontID);
		lineHeight = FntLineHeight ();
		FntSetFont (currFont);
		if (tableHeight >= dataHeight + lineHeight)
			{
			// Get the height of the current row.
			rowUsable = TblRowUsable (table, row);
			if (rowUsable)
				oldHeight = TblGetRowHeight (table, row);
			else
				oldHeight = 0;


			// Determine if the row needs to be initialized.  We will initialize 
			// the row if: the row is not usable (not displayed),  the unique
			// id of the record does not match the unique id stored in the 
			// row, or if the start time of the appointment does not match the
			// start time stored in the table.
			init = (! rowUsable);
			uniqueID = 0;
			masked = false;
			hasLoc = false;
			descHash = 0;
			recordNum = appts[apptIndex].recordNum;
			if (IsDayTodo (recordNum))		// Db35: ToDo35 row
				{
				uniqueID = gDayTodoUID[recordNum - kDayTodoBase];	// per-item id
				init |= TblGetRowData (table, row) != uniqueID;
				}
			else if (IsDayBday (recordNum))	// Db35: birthday/anniversary row
				{
				uniqueID = gDayBdayUID[recordNum - kDayBdayBase];	// per-item id
				init |= TblGetRowData (table, row) != uniqueID;
				}
			else if (IsDayDav (recordNum))		// Db35: CalDAV Sync event row
				{
				uniqueID = gDayDavUID[recordNum - kDayDavBase];	// per-item id
				init |= TblGetRowData (table, row) != uniqueID;
				}
			else if (recordNum != emptySlot)	// empty time slot?
				{
				MemHandle			descRecH;
				ApptDBRecordType	descRec;

				DmRecordInfo (ApptDB, recordNum, NULL, &uniqueID, NULL);
				init |= TblGetRowData (table, row) != uniqueID;

				//Mask if appropriate
				DmRecordInfo (ApptDB, recordNum, &attr, NULL, NULL);
		   	masked = (((attr & dmRecAttrSecret) && PrivateRecordVisualStatus == maskPrivateRecords));

				// Db35: same identity (uniqueID/startTime/font/iconsWidth all
				// unchanged) does NOT mean the row's "@ location" suffix is still
				// correct -- the location lives in a separate DB this loop never
				// otherwise looks at, so a location-only edit left the row's
				// cached DaySetRowCopy text stale forever (never invalidated,
				// so TblRedrawTable had nothing to redo -- this is why the
				// suffix "never showed up at all" regardless of focus). Same
				// stale-row-cache class of bug as the masked check right above;
				// fixed the identical way -- compare a per-row remembered flag,
				// invalidate on change.
				hasLoc = (apptIndex < kMaxDayLocRows && gDayLocText[apptIndex][0] != 0);

				// Db35: same stale-row-cache class again, this time for the
				// description TEXT itself -- see gDayRowDescHash's declaration
				// comment. A title-only edit (no time/date/repeat/font/icon
				// change) trips none of this loop's other checks, so without
				// this the row's cached text was never marked invalid until
				// some UNRELATED change also happened to touch the row.
				if (ApptGetRecord (ApptDB, recordNum, &descRec, &descRecH) == 0)
					{
					const char *dp = descRec.description;
					if (dp) while (*dp) descHash = (UInt16) (descHash * 33 + (UInt8) *dp++);
					MemHandleUnlock (descRecH);
					}
				}
			else if (! init)
				init |= (TblGetRowData (table, row) != 0);

			if (masked != TblRowMasked(table,row))
				TblMarkRowInvalid (table, row);

			TblSetRowMasked(table,row,masked);

			if (row < kDayMaxRows)
				{
				if (hasLoc != gDayRowHasLoc[row])
					TblMarkRowInvalid (table, row);
				gDayRowHasLoc[row] = hasLoc;

				if (descHash != gDayRowDescHash[row])
					TblMarkRowInvalid (table, row);
				gDayRowDescHash[row] = descHash;
				}
		

			if (! init)
				init = TimeToInt (appts[apptIndex].startTime) != 
					    TblGetItemInt (table, row, timeColumn);
					    
			if (! init)
				init = TblGetItemInt (table, row, descColumn) != iconsWidth;

			if (! init)
				init = TblGetItemFont (table, row, descColumn) != fontID;

			// If the record is not already being displayed in the current 
			// row load the record into the table.
			if (init)
				{
				DayViewInitRow (table, row, apptIndex, height, uniqueID, iconsWidth, fontID);
				}

			// If the height or the position of the item has changed draw the item.
			else 
				{
				TblSetRowID (table, row, apptIndex);
				if (height != oldHeight)
					{
					TblSetRowHeight (table, row, height);
					TblMarkRowInvalid (table, row);
					}
				else if (pos != oldPos)
					{
					TblMarkRowInvalid (table, row);
					}
				}
				
			pos += height;
			oldPos += oldHeight;

			lastAppt = apptIndex;
			apptIndex++;
			row++;
			}
		
		dataHeight += height;

		// Is the table full?
		if (dataHeight >= tableHeight)		
			{
			// If we have a currently selected record, make sure that it is
			// not below the last visible record.  If the currently selected 
			// record is the last visible record, make sure the whole description 
			// is visible.
			if (CurrentRecord == noRecordSelected) break;

			apptIndex = DayViewFindAppointment (CurrentRecord);
			if (apptIndex < lastAppt)
				 break;

			// Last visible?
			else if (apptIndex == lastAppt)
				{
				if ((apptIndex == TopVisibleAppt) || (dataHeight == tableHeight))
					break;
					
				// Remove the top item from the table and reload the table again.
				TopVisibleAppt++;
				apptIndex = TopVisibleAppt;
				}
			// Below last visible.
			else
				TopVisibleAppt = apptIndex;
				
			row = 0;
			dataHeight = 0;
			oldPos = pos = 0;
			}
		}



	// Hide the items that don't have any data.
	numRows = TblGetNumberOfRows (table);
	while (row < numRows)
		{		
		TblSetRowUsable (table, row, false);
		row++;
		}


	// If the table is not full and the first visible record is 
	// not the first record	in the database, displays enough records
	// to fill out the table.
	while (dataHeight < tableHeight)
		{
		apptIndex = TopVisibleAppt;
		if (apptIndex == 0) break;
		apptIndex--;

		height = DayViewGetDescriptionHeight (apptIndex, columnWidth, tableHeight, &iconsWidth, &fontID);
			
		// If adding the item to the table will overflow the height of
		// the table, don't add the item.
		if (dataHeight + height > tableHeight)
			break;
		
		// Insert a row before the first row.
		TblInsertRow (table, 0);

		recordNum = appts[apptIndex].recordNum;
		masked = false;
		if (IsDayTodo (recordNum))		// Db35: ToDo35 row
			uniqueID = gDayTodoUID[recordNum - kDayTodoBase];
		else if (IsDayBday (recordNum))	// Db35: birthday/anniversary row
			uniqueID = gDayBdayUID[recordNum - kDayBdayBase];
		else if (IsDayDav (recordNum))		// Db35: CalDAV Sync event row
			uniqueID = gDayDavUID[recordNum - kDayDavBase];
		else if (recordNum != emptySlot)	// empty time slot?
			{
			DmRecordInfo (ApptDB, recordNum, NULL, &uniqueID, NULL);
			//mask if appropriate
			DmRecordInfo (ApptDB, recordNum, &attr, NULL, NULL);
	   		masked = (((attr & dmRecAttrSecret) && PrivateRecordVisualStatus == maskPrivateRecords));
			}
		else
			uniqueID = 0;
				
		TblSetRowMasked(table,0,masked);


		DayViewInitRow (table, 0, apptIndex, height, uniqueID, iconsWidth, fontID);
		
		TopVisibleAppt = apptIndex;
		
		rowsInserted = true;

		dataHeight += height;
		}
		
	// If rows were inserted to full out the page, invalidate the whole
	// table, it all needs to be redrawn.
	if (rowsInserted)
		TblMarkTableInvalid (table);

	// If the height of the data in the table is greater than the height
	// of the table, then the bottom of the last row is clip and the 
	// table is scrollable.
	lastItemClipped = (dataHeight > tableHeight);

	// Update the scroll arrows.
	DayViewUpdateScrollers (frm, lastAppt, lastItemClipped);

	MemPtrUnlock (appts);
}


/***********************************************************************
 *
 * FUNCTION:    DayInjectTodos
 *
 * DESCRIPTION: Db35 -- scan the ToDo35 database and prepend, to the front of
 *              the (already-built) day appointment list, one untimed sentinel
 *              row for every incomplete ToDo item due on the shown day, plus
 *              (only when the shown day is today) every undated ToDo item.
 *              Called at the tail of DayViewLayoutDay, after ApptsH is built,
 *              unlocked and resized.
 *
 ***********************************************************************/
SEC001 static void DayInjectTodos (void)
{
	DmOpenRef		db;
	DmOpenRef		extDb = NULL;	/* Db35: time/repeat extras (read-only) */
	UInt16			n, i;
	Int32			dayInt;			/* era DateToInt is a 32-bit monotonic key */
	Boolean			isToday;
	DateTimeType	now;
	ApptInfoPtr		appts;

	gDayTodoCount = 0;

	if (! ApptsH) return;
	if (ToDoGetDatabase (&db, dmModeReadOnly) != errNone || ! db)
		return;
	if (TdxGetDatabase (&extDb, dmModeReadOnly) != errNone)	/* time/repeat extras */
		extDb = NULL;

	// Is the shown day today?  Undated ToDos are only listed on today.
	TimSecondsToDateTime (TimGetSeconds (), &now);
	isToday = (Boolean) (now.year == Date.year + firstYear &&
								now.month == Date.month && now.day == Date.day);
	dayInt = DateToInt (Date);

	n = DmNumRecords (db);
	for (i = 0; i < n && gDayTodoCount < kMaxDayTodo; i++)
		{
		MemHandle			h = DmQueryRecord (db, i);
		ToDoDBRecordPtr	rec;
		Int32				dueInt;
		Boolean				match = false;

		if (! h) continue;
		rec = (ToDoDBRecordPtr) MemHandleLock (h);

		if (! (rec->priority & completeFlag))
			{
			dueInt = DateToInt (rec->dueDate);
			if (dueInt == toDoNoDueDate)
				match = isToday;				// undated -> today only
			else if (dueInt == dayInt)
				match = true;					// due exactly on the shown day
			else if (isToday && dueInt < dayInt)
				match = true;					// overdue & incomplete -> pile onto today
			}

		if (match)
			{
			UInt32	uid = 0;
			TdxExt	ext;
			Char*		note;
			DmRecordInfo (db, i, NULL, &uid, NULL);
			// Db35: a ToDo35 record whose spare priority bit (0x40) is set is AES-CTR
			// encrypted ([descLen:2][noteLen:2][encDesc][encNote] after the priority byte).
			// Decrypt the description (nonce uid,0) when the suite is unlocked, else mask.
			if (rec->priority & 0x40)
				{
				const UInt8* b = (const UInt8*) &rec->description;
				UInt16 descLen = (UInt16) (((UInt16) b[0] << 8) | b[1]);
				UInt16 noteLen = (UInt16) (((UInt16) b[2] << 8) | b[3]);
				UInt8 key[ENC_KEY_BYTES];
				if (descLen > 63) descLen = 63;
				if (EncGetKey (key, 1000000000UL))
					{
					MemMove (gDayTodoDesc[gDayTodoCount], b + 4, descLen);
					if (descLen) EncCrypt (key, (UInt8*) gDayTodoDesc[gDayTodoCount], descLen, uid, 0);
					gDayTodoDesc[gDayTodoCount][descLen] = '\0';
					MemSet (key, ENC_KEY_BYTES, 0);
					}
				else
					StrCopy (gDayTodoDesc[gDayTodoCount], "******");
				gDayTodoNote[gDayTodoCount] = (Boolean) (noteLen != 0);
				}
			else
				{
				StrNCopy (gDayTodoDesc[gDayTodoCount], &rec->description, 63);
				gDayTodoDesc[gDayTodoCount][63] = '\0';
				note = &rec->description + StrLen (&rec->description) + 1;
				gDayTodoNote[gDayTodoCount] = (Boolean) (*note != 0);
				}
			gDayTodoRec[gDayTodoCount] = i;
			gDayTodoUID[gDayTodoCount] = 0xE0000000UL | (uid & 0x00FFFFFFUL);
			// Time + repeat come from the extras DB.
			if (extDb && TdxGet (extDb, uid, &ext))
				{
				gDayTodoTimed[gDayTodoCount]  = (Boolean) ((ext.flags & tdxTimed) != 0);
				gDayTodoStart[gDayTodoCount]  = ext.startTime;
				gDayTodoRepeat[gDayTodoCount] = (Boolean) ((ext.flags & tdxHasRepeat) != 0);
				}
			else
				{
				gDayTodoTimed[gDayTodoCount]  = false;
				gDayTodoRepeat[gDayTodoCount] = false;
				}
			gDayTodoCount++;
			}

		MemHandleUnlock (h);
		}

	DmCloseDatabase (db);
	if (extDb) DmCloseDatabase (extDb);

	if (gDayTodoCount == 0)
		return;

	if (MemHandleResize (ApptsH,
			(NumAppts + gDayTodoCount) * sizeof (ApptInfoType)) != errNone)
		return;

	appts = MemHandleLock (ApptsH);

	// Untimed / undated todos: prepend at the very top (in slot order).
	{
	UInt16 nUntimed = 0, j = 0;
	for (i = 0; i < gDayTodoCount; i++) if (! gDayTodoTimed[i]) nUntimed++;
	if (nUntimed)
		{
		MemMove (&appts[nUntimed], &appts[0], NumAppts * sizeof (ApptInfoType));
		for (i = 0; i < gDayTodoCount; i++)
			if (! gDayTodoTimed[i])
				{
				MemSet (&appts[j].startTime, sizeof (TimeType), 0xFF);	// untimed
				MemSet (&appts[j].endTime,   sizeof (TimeType), 0xFF);
				appts[j].recordNum = (UInt16) (kDayTodoBase + i);
				j++;
				}
		NumAppts = (UInt16)(NumAppts + nUntimed);
		}
	}

	// Timed todos: slot each at its start time -- replacing the empty hour slot
	// there if one exists, otherwise inserting before the next timed row.
	for (i = 0; i < gDayTodoCount; i++)
		{
		UInt16 t, pos;
		if (! gDayTodoTimed[i]) continue;
		t = TimeToInt (gDayTodoStart[i]);
		for (pos = 0; pos < NumAppts; pos++)
			{
			UInt16 st = TimeToInt (appts[pos].startTime);
			if (st != (UInt16) apptNoTime && st >= t) break;
			}
		if (pos < NumAppts && appts[pos].recordNum == emptySlot &&
			 TimeToInt (appts[pos].startTime) == t)
			{
			appts[pos].recordNum = (UInt16) (kDayTodoBase + i);	// reuse the empty slot
			}
		else
			{
			MemMove (&appts[pos + 1], &appts[pos],
						(NumAppts - pos) * sizeof (ApptInfoType));
			appts[pos].startTime = gDayTodoStart[i];
			appts[pos].endTime   = gDayTodoStart[i];		// zero-length -> no time bar
			appts[pos].recordNum = (UInt16) (kDayTodoBase + i);
			NumAppts++;
			}
		}

	MemPtrUnlock (appts);
}


/***********************************************************************
 *
 * FUNCTION:    DayInjectBdays
 *
 * DESCRIPTION: Db35 -- prepend, to the very front of the (already-built,
 *              already ToDo-injected) day appointment list, one untimed
 *              sentinel row for every Address35 birthday/anniversary that
 *              falls on the shown day. Called at the tail of
 *              DayViewLayoutDay, after DayInjectTodos.
 *
 ***********************************************************************/
SEC001 static void DayInjectBdays (void)
{
	ApptInfoPtr	appts;
	UInt16		i;

	gDayBdayCount = 0;
	if (! ApptsH) return;

	// Db35: pass the TRUE full year (wide DateType stores year as full-firstYear),
	// so the age/ordinal is correct in any era -- same call the Agenda view makes.
	gDayBdayCount = AddrBdayGet ((UInt16) (Date.year + firstYear), Date.month, Date.day,
		gDayBdayLines, gDayBdayRec, kMaxDayBday);
	if (gDayBdayCount == 0) return;

	if (MemHandleResize (ApptsH,
			(NumAppts + gDayBdayCount) * sizeof (ApptInfoType)) != errNone)
		{
		gDayBdayCount = 0;
		return;
		}

	appts = MemHandleLock (ApptsH);
	MemMove (&appts[gDayBdayCount], &appts[0], NumAppts * sizeof (ApptInfoType));
	for (i = 0; i < gDayBdayCount; i++)
		{
		MemSet (&appts[i].startTime, sizeof (TimeType), 0xFF);	// untimed
		MemSet (&appts[i].endTime,   sizeof (TimeType), 0xFF);
		appts[i].recordNum = (UInt16) (kDayBdayBase + i);
		// 0xD prefix keeps this visually/semantically distinct from ToDo's 0xE-prefixed pseudo-uid.
		gDayBdayUID[i] = 0xD0000000UL | (gDayBdayRec[i] & 0x00FFFFFFUL);
		}
	NumAppts = (UInt16) (NumAppts + gDayBdayCount);
	MemPtrUnlock (appts);
}


/* Db35: sec001 is hard-full (link error: "region sec001res is full"), so
 * this whole app's CalDAV Sync integration lives in sec002 - the
 * multi-line text builder + height calc (called from DayViewGetDescription/
 * DayViewGetDescriptionHeight, both sec001) and the tap-to-info-alert
 * helper (called from both of DateDay.c's own tblSelectEvent/fldEnterEvent
 * tap-handling sites). */

/* Deliberately UNTAGGED (main segment) relay hop - DayBuildLocSuffix lives
 * in DateLocation.c, ALSO tagged SEC002, so calling it directly from
 * another SEC002 function in a DIFFERENT FILE hit the documented
 * same-segment-across-files gotcha: GCC emits a short bsr.w assuming a
 * safe same-segment direct branch, but this cross-compiler's short-branch
 * resolution for that shape is unreliable ("Error: value out of range...
 * too large for field of 2 bytes" at ASSEMBLE time, before the linker
 * even sees both files). Routing through an untagged hop turns it into
 * two ordinary (safe, already-proven) cross-segment indirect calls
 * instead of one unsafe same-segment direct one. Main has ~9KB of
 * headroom, easily enough for this. */
static MemHandle DayBuildLocSuffixHop (const char *desc, const char *locSummary)
{
	return DayBuildLocSuffix (desc, locSummary);
}

/* Builds DayViewGetDescription's entire CalDAV return value (fresh
 * MemHandle copy of gDayDavLines[slot], cached per-row via the existing
 * DaySetRowCopy) - the ROM's own Field Manager (via textTableItem) wraps
 * and draws this to multiple lines itself, same as a real appointment's
 * description; unlike that case there's no encryption/edit-mode to
 * handle, so this is a plain string-to-handle copy (same shape as
 * DayMaskCopy's own "******" handle, just real text). Whole thing lives
 * in sec002, not just the copy - sec001 has almost no headroom left. */
SEC002 static Err DayDavGetDescHandle (Int16 row, UInt16 recordNum,
	MemHandle *textH, Int16 *textOffset, Int16 *textAllocSize)
{
	UInt16		slot = (UInt16)(recordNum - kDayDavBase);
	char*		d = (slot < gDayDavCount) ? gDayDavLines[slot] : "";
	UInt16		len = (UInt16) StrLen (d);
	MemHandle	h = MemHandleNew ((UInt32) len + 1);
	if (h == NULL) return (Err) -1;
	{
	char* p = (char*) MemHandleLock (h);
	MemMove (p, d, len);
	p[len] = 0;
	MemHandleUnlock (h);
	}
	*textH = DaySetRowCopy ((Int16) row, h);
	*textOffset = 0;
	*textAllocSize = (Int16) (len + 1);
	return 0;
}

/* DayViewGetDescriptionHeight's real-appointment "unmasked" case, moved
 * here whole (~35 lines of real logic) - sec001 has almost no headroom
 * left, and unlike DateDAV's own helpers this isn't CalDAV-specific, it's
 * just the biggest safely-extractable chunk left in that function. Does
 * its own font save/restore internally (the caller no longer needs to). */
SEC002 static Int16 DayApptCalcUnmaskedHeight (UInt16 recordNum, Int16 apptIndex,
	ApptDBRecordType *apptRec, Int16 width, Int16 maxHeight, Int16 *iconsWidthOut)
{
	Int16 iconsWidth = 0;
	Int16 height;
	Int16 lineHeight;
	FontID prev = FntSetFont (symbolFont);

	if (apptRec->note)
		iconsWidth += FntCharWidth (symbolNote);
	if (apptRec->repeat)
		{
		if (iconsWidth) iconsWidth++;
		iconsWidth += FntCharWidth (symbolRepeat);
		}

	FntSetFont (ApptDescFont);

	// Db35: a row with a linked location draws "desc @ location" (see
	// DayViewGetDescription), which can wrap to more lines than the
	// plain title alone -- measure the SAME text that gets drawn, or
	// the row is sized too short and the suffix gets clipped. Skip
	// when Sec35-locked: that case draws plain "******" with no
	// suffix, so sizing off the suffixed text here would make the
	// row taller than what's actually shown.
	if (apptIndex < kMaxDayLocRows && gDayLocText[apptIndex][0] &&
		  ! (ApptRecEncrypted (ApptDB, recordNum) && ! EncKeyPresent ()))
		{
		MemHandle	sh = DayBuildLocSuffixHop (apptRec->description, gDayLocText[apptIndex]);
		if (sh != NULL)
			{
			char*	sp = (char*) MemHandleLock (sh);
			height = FldCalcFieldHeight (sp, width - iconsWidth);
			MemHandleUnlock (sh);
			MemHandleFree (sh);
			}
		else
			height = FldCalcFieldHeight (apptRec->description, width - iconsWidth);
		}
	else
		height = FldCalcFieldHeight (apptRec->description, width - iconsWidth);

	lineHeight = FntLineHeight ();
	height = min (height, (maxHeight / lineHeight));
	height *= lineHeight;

	FntSetFont (prev);
	*iconsWidthOut = iconsWidth;
	return height;
}

/* Db35: shared by DayDavCalcHeight/DayViewDrawIcons/DayDavSelectIcons -
 * note icon (if the synced record has one) drawn/reserved first, then
 * repeat icon, exactly the same left-to-right order and 1px gap a real
 * appointment's own note+repeat icon pair uses (DayApptCalcUnmaskedHeight/
 * DayViewDrawIcons's non-DAV path). Must SetFont(symbolFont) itself before
 * calling FntCharWidth - callers may be in any font. Restores the
 * caller's font on return. */
SEC002 static Int16 DayDavIconsWidth (UInt16 slot, Boolean *hasNoteOut, Boolean *hasRepeatOut)
{
	Boolean	note = (slot < gDayDavCount) && gDayDavNote[slot];
	Boolean	repeat = (slot < gDayDavCount) && gDayDavRepeat[slot];
	Int16		w = 0;
	FontID	prev = FntSetFont (symbolFont);

	if (note) w += FntCharWidth (symbolNote);
	if (repeat)
		{
		if (w) w++;
		w += FntCharWidth (symbolRepeat);
		}

	FntSetFont (prev);
	if (hasNoteOut) *hasNoteOut = note;
	if (hasRepeatOut) *hasRepeatOut = repeat;
	return w;
}

/* Db35: same shape as DayApptCalcUnmaskedHeight (icon-width reserve, font
 * save/restore done internally) -- reserves space for whichever of the
 * note/repeat icons this DAV row actually has (DayDavIconsWidth). */
SEC002 static Int16 DayDavCalcHeight (UInt16 recordNum, Int16 width, Int16 maxHeight, Int16 *iconsWidthOut)
{
	UInt16	slot = (UInt16)(recordNum - kDayDavBase);
	char*		d = (slot < gDayDavCount) ? gDayDavLines[slot] : "";
	Int16		iconsWidth = DayDavIconsWidth (slot, NULL, NULL);
	FontID	prev = FntSetFont (ApptDescFont);
	Int16		n, lineHeight;

	n = FldCalcFieldHeight (d, (Int16) (width - iconsWidth));
	lineHeight = FntLineHeight ();
	FntSetFont (prev);

	n = min (n, (maxHeight / lineHeight));
	*iconsWidthOut = iconsWidth;
	return (Int16) (n * lineHeight);
}

/* Db35: same-segment-across-different-files DISP16 gotcha (documented
 * elsewhere in this file - DayBuildLocSuffixHop/EncGetKeyHop/EncCryptHop):
 * the DAV-detail helpers just below are sec002/sec003 (sec001 has no
 * headroom left), and so is DateDAVGetOne (DateDAV.c) - a direct call
 * between them is a same-segment call across FILES, which this compiler's
 * short-branch resolution can't always reach once the segment grows past a
 * threshold (confirmed real: "bsr.w DateDAVGetOne" failed assembly with
 * "value out of range" the first time this was tried). Routing through
 * deliberately UNTAGGED (main-segment) relays turns it into two safe
 * cross-segment indirect calls instead.
 *
 * Two SLIM, purpose-specific hops rather than one that mirrors
 * DateDAVGetOne's full 16-argument signature - sec002/sec003 are both
 * also right at their own limits, so every NULL/0 argument push at the
 * CALL SITE (in a constrained segment) matters; the main segment (a
 * separate, roomier 32KB budget, currently under 24KB used) can afford to
 * absorb that boilerplate instead. */
static Boolean DateDAVGetTitleLocHop (UInt16 year, UInt8 month, UInt8 day, UInt32 uidHash,
	char *title, UInt16 titleCap, char *location, UInt16 locationCap,
	TimeType *startOut, TimeType *endOut, Boolean *isRepeatingOut)
{
	return DateDAVGetOne (year, month, day, uidHash, NULL, 0, title, titleCap,
		location, locationCap, NULL, 0, startOut, endOut, NULL, isRepeatingOut);
}

static Boolean DateDAVGetNotesHop (UInt16 year, UInt8 month, UInt8 day, UInt32 uidHash,
	char *notes, UInt16 notesCap)
{
	return DateDAVGetOne (year, month, day, uidHash, NULL, 0, NULL, 0, NULL, 0,
		notes, notesCap, NULL, NULL, NULL, NULL);
}

static Boolean DateDAVGetTitleHop (UInt16 year, UInt8 month, UInt8 day, UInt32 uidHash,
	char *title, UInt16 titleCap)
{
	return DateDAVGetOne (year, month, day, uidHash, NULL, 0, title, titleCap,
		NULL, 0, NULL, 0, NULL, NULL, NULL, NULL);
}

/* Db35: NoteViewDrawTitleAndForm's CalDAV branch - sec002 (that function's
 * own segment, sec001, has no headroom left). Fetches just the title text
 * (all NoteViewDrawTitleAndForm actually needs from a record) instead of
 * ApptGetRecord, which is undefined/crash on a DAV sentinel CurrentRecord
 * (confirmed real: "DateDB.c, Error Querying record" opening a DAV
 * event's Note view - a SEPARATE crash from the DayViewClearEditState one,
 * same root cause: this function also draws the Note screen's own
 * pseudo-title bar from the record's description, unconditionally). Always
 * NUL-terminates buf even if nothing was found. */
static void NoteViewDavTitle (char *buf, UInt16 bufCap)
{
	buf[0] = '\0';
	DateDAVGetTitleHop (gDavTargetYear, gDavTargetMonth, gDavTargetDay,
		gDavTargetUidHash, buf, bufCap);
}

/* Db35: DetailsInit's CalDAV branch - populates the REAL Event Details
 * dialog (form 1300) straight from DateBook35DAV.pdb via DateDAVGetOne,
 * instead of ApptGetRecord (CurrentRecord is a DAV sentinel here, not a
 * real ApptDB index - touching ApptDB with it is undefined/crashes).
 * Reuses the exact same dialog, fields and code path a real appointment
 * gets - NOT a separate custom form. Editing is prevented at the far end
 * (DetailsApply/DetailsDeleteAppointment/DetailsDoCommand/NoteView* all
 * no-op for a DAV CurrentRecord - see their own guards), not here; every
 * field below is left genuinely interactive so the dialog looks and
 * behaves exactly like the real one.
 *
 * Location is set directly on the field, bypassing LocGadgetLoad's own
 * LinkGet-by-apptUid lookup (meaningless for a record that was never in
 * ApptDB). Repeat is always left at repeatNone - WiRSa only tells us a
 * boolean (did the source VEVENT have an RRULE), not enough to populate
 * a specific repeatType (Daily/Weekly/...); the Day View row's own repeat
 * icon remains the accurate indicator. Private has no CalDAV equivalent,
 * so it stays unchecked.
 *
 * sec003, not sec001 (DetailsInit's own segment) or sec002 - both have no
 * headroom left; uses DateDAVGetTitleLocHop (see its own comment just
 * above) since DateDAVGetOne itself is also sec003. */
SEC003 static void DetailsInitFromDav (DetailsPtr details)
{
	char title[64], location[64];
	TimeType start, end;
	Boolean isRepeating = false;
	UInt16 len;

	title[0] = '\0';
	location[0] = '\0';
	MemSet (&start, sizeof (start), 0xFF);
	MemSet (&end, sizeof (end), 0xFF);

	DateDAVGetTitleLocHop (gDavTargetYear, gDavTargetMonth, gDavTargetDay,
		gDavTargetUidHash, title, sizeof (title), location, sizeof (location),
		&start, &end, &isRepeating);
	gDavTargetIsRepeating = isRepeating;

	len = (UInt16) StrLen (title);
	details->descH = MemHandleNew ((UInt32) len + 1);
	if (details->descH)
		{
		char *p = (char *) MemHandleLock (details->descH);
		MemMove (p, title, len);
		p[len] = 0;
		MemHandleUnlock (details->descH);
		}

	// Db35: the event's OWN actual date, NOT Day view's currently-displayed
	// `Date` global - a Week-view-originated occurrence's date can differ
	// from whatever day Day view happens to be showing (see
	// DetailsOpenForDav's own comment on why this whole function reads
	// gDavTarget* instead of Day-view-specific state).
	details->when.date.year = (Int16) (gDavTargetYear - firstYear);
	details->when.date.month = gDavTargetMonth;
	details->when.date.day = gDavTargetDay;
	details->when.startTime = start;
	details->when.endTime = end;
	details->endDateOffset = 0;
	details->repeat.repeatType = repeatNone;
	EDateSetForever (&details->repeat.repeatEndDate);
	details->secret = false;

	DetailsSetDavLocationField (location);
}

/* Db35: DetailsInitFromDav's Location-field piece, split out to sec002 -
 * sec003 (DetailsInitFromDav's own segment) is out of headroom too. Sets
 * the Location field directly, same as DetailsInitFromDav's own comment
 * explains (bypasses LocGadgetLoad's DB-linking, meaningless here). */
static void DetailsSetDavLocationField (const char *location)
{
	FieldPtr locFld = GetObjectPtr (DetailsLocationGadget);
	UInt16 len = (UInt16) StrLen (location);
	MemHandle h = MemHandleNew ((UInt32) len + 1);
	if (h)
		{
		char *p = (char *) MemHandleLock (h);
		MemMove (p, location, len);
		p[len] = 0;
		MemHandleUnlock (h);
		FldSetTextHandle (locFld, h);
		}
}

/* Db35: DetailsInit's own "should the Repeat trigger say 'Repeats'
 * instead of 'None'" check, split out to sec002 - sec001 (DetailsInit's
 * own segment) has almost no headroom left. False for a real appointment
 * (IsDayDav false). gDavTargetIsRepeating is only meaningful once
 * DetailsInitFromDav has run for the CURRENT dialog session (it always
 * has, by the time this is called - DetailsInit calls DetailsInitFromDav
 * unconditionally before the common tail that reads this). */
static Boolean DayDavRepeatOverrideLabel (void)
{
	if (! IsDayDav (CurrentRecord)) return false;
	return gDavTargetIsRepeating;
}

/* Db35: NoteViewLoadRecord's CalDAV branch - sec002 (uses the hop too, so
 * safe in either segment; placed wherever has room - see
 * NoteViewDeleteNoteFromDav's own comment). Builds gNoteEditH from the
 * synced DESCRIPTION text (may be empty) instead of ApptGetRecord. */
static void NoteViewLoadRecordFromDav (FieldPtr fld)
{
	char notes[kDateDavNotesLen];
	UInt16 len;

	notes[0] = '\0';
	DateDAVGetNotesHop (gDavTargetYear, gDavTargetMonth, gDavTargetDay,
		gDavTargetUidHash, notes, sizeof (notes));

	if (gNoteEditH) { MemHandleFree (gNoteEditH); gNoteEditH = NULL; }
	len = (UInt16) StrLen (notes);
	gNoteEditH = MemHandleNew ((UInt32) len + 1);
	if (gNoteEditH)
		{
		char *p = (char *) MemHandleLock (gNoteEditH);
		MemMove (p, notes, len);
		p[len] = 0;
		MemHandleUnlock (gNoteEditH);
		FldSetTextHandle (fld, gNoteEditH);
		}
}

/* Db35: NoteViewDeleteNote's CalDAV branch - sec002 (not sec003 with its
 * two siblings just above - no ApptDB/DateDAVGetOne call here at all, so
 * no same-segment DISP16 risk either way; placed in whichever segment has
 * room, which shifts as this file's own budget shifts). Nothing to delete;
 * just the same field-handle detach/free housekeeping the real path
 * does. */
static Boolean NoteViewDeleteNoteFromDav (void)
{
	// Db35: no FldCompactText - unlike the real path, this field was never
	// editable, so there's no gap space from user edits to release.
	FieldPtr fld = GetObjectPtr (NoteField);
	FldSetTextHandle (fld, 0);
	if (gNoteEditH)
		{
		MemHandleFree (gNoteEditH);
		gNoteEditH = NULL;
		}
	return (true);
}

/* Db35: shared by every one of DateDay.c's own navigate-to-a-CalDAV-row
 * call sites (tblSelectEvent's DayViewItemSelected, fldEnterEvent's row
 * handler, and DayDavSelectIcons's own icon-tap routing just below) - same
 * invocation shape as an existing real appointment's own tap-to-Details/
 * tap-to-note path (CurrentRecord/ItemSelected/FrmPopupForm or
 * FrmGotoForm), just without gDetailsApptIndex (DetailsInitFromDav never
 * uses it, unlike the real ApptDB path's occurrence-walkback logic).
 * Sets gDavTarget* (Date + this row's own uidHash) so
 * DetailsInitFromDav/NoteViewLoadRecordFromDav/NoteViewDavTitle look up
 * the RIGHT occurrence - not just any DAV sentinel value, an explicit key.
 * gDaySkipRestoreEditState=true: this row was never inline-editing, so
 * DayViewRestoreEditState must not try to DmRecordInfo a DAV sentinel when
 * Details/NoteView closes. toNote selects NewNoteView (tapped the note
 * icon) over the default DetailsDialog. sec002, not sec001 - sec001 has no
 * headroom left, and this consolidates what used to be duplicated inline
 * at multiple call sites. */
SEC002 static void DayOpenDavDetails (UInt16 rn, Boolean toNote)
{
	UInt16 slot = (UInt16) (rn - kDayDavBase);

	// Db35: CalDAV-synced event content (title/location/notes/alias/uid) is
	// Sec35 ciphertext at rest now (apps/caldav_sync/DAVDb.h's own comment) -
	// same "locked -> bounce to Sec35 before opening anything" gate a real
	// encrypted appointment gets from DayLockedBounce (tblEnterEvent, above),
	// just unconditional here rather than gated on ApptDbHasEncrypted - every
	// DAV row is always encrypted, unlike a real appointment which might not
	// be. This is the single choke point every view's own CalDAV-row tap
	// routes through (see this function's own header comment), so gating
	// here alone covers Details AND the note-icon tap (toNote) uniformly.
	if (! EncKeyPresent ())
		{
		DaySwitchToSec35IfInstalled ();
		return;
		}

	gDavTargetYear = (UInt16) (Date.year + firstYear);
	gDavTargetMonth = Date.month;
	gDavTargetDay = Date.day;
	gDavTargetUidHash = (slot < gDayDavCount) ? gDayDavHash[slot] : 0;

	CurrentRecord = rn;
	ItemSelected = true;
	gDaySkipRestoreEditState = true;
	if (toNote)
		FrmGotoForm (NewNoteView);
	else
		FrmPopupForm (DetailsDialog);
}

/* Db35: DayViewSelectIcons's CalDAV branch - same icon-hit-test-then-pen-
 * tracking-then-navigate logic as the real-appointment path in that
 * function, but keyed off gDayDavNote/gDayDavRepeat (via DayDavIconsWidth)
 * instead of apptRec.note/apptRec.repeat, and never touches ApptDB at all
 * (a DAV sentinel recordNum is not a real ApptDB index - see the crash
 * this fixes, noted at the call site). itemR/iconsWidth/x/y are exactly
 * what the caller already computed for the WHOLE icon-column hit-test;
 * this only needs to narrow it down to which specific icon. sec002, not
 * sec001 - sec001 has no headroom left. */
static Boolean DayDavSelectIcons (TablePtr table, Int16 row, UInt16 recordNum,
	RectangleType itemR, UInt16 iconsWidth, Int16 x, Int16 y)
{
	UInt16	slot = (UInt16)(recordNum - kDayDavBase);
	Boolean	hasNote, hasRepeat;
	Boolean	inNoteIcon = false;
	Boolean	penDown = true;
	Boolean	selected = true;
	FontID	curFont;
	RectangleType r;

	(void) DayDavIconsWidth (slot, &hasNote, &hasRepeat);

	r.topLeft.x = itemR.topLeft.x + itemR.extent.x - iconsWidth;
	r.topLeft.y = itemR.topLeft.y;
	r.extent.x = iconsWidth;
	curFont = FntSetFont (ApptDescFont);
	r.extent.y = FntLineHeight ();
	FntSetFont (curFont);

	curFont = FntSetFont (symbolFont);
	if (hasNote)
		{
		r.extent.x = FntCharWidth (symbolNote);
		inNoteIcon = RctPtInRectangle (x, y, &r);
		if (! inNoteIcon)
			r.topLeft.x += r.extent.x + 1;
		}
	if (hasRepeat && ! inNoteIcon)
		r.extent.x = FntCharWidth (symbolRepeat);
	FntSetFont (curFont);

	TblReleaseFocus (table);
	DayViewSelectIconRect (table, row, &r, selected);

	do
		{
		PenGetPoint (&x, &y, &penDown);
		if (RctPtInRectangle (x, y, &r))
			{
			if (! selected)
				{
				selected = true;
				DayViewSelectIconRect (table, row, &r, selected);
				}
			}
		else if (selected)
			{
			selected = false;
			DayViewSelectIconRect (table, row, &r, selected);
			}
		} while (penDown);

	if (selected)
		{
		selected = false;
		DayViewSelectIconRect (table, row, &r, selected);
		}

	DayOpenDavDetails (recordNum, inNoteIcon);
	return true;
}


/***********************************************************************
 *
 * FUNCTION:    DayInjectCalDav
 *
 * DESCRIPTION: Db35 -- prepend/insert, into the (already Todo+Bday-injected)
 *              day appointment list, one sentinel row for every CalDAV Sync
 *              event synced onto the shown day. Unlike DayInjectBdays
 *              (always untimed), a CalDAV event can be timed OR untimed
 *              (an all-day event vs. a real meeting) - so this mirrors
 *              DayInjectTodos's own two-pass structure instead: untimed
 *              events prepend to the very top, timed events get inserted
 *              at (or reuse an empty slot at) their own start hour, same as
 *              a real appointment. Called at the tail of DayViewLayoutDay,
 *              after DayInjectBdays.
 *
 ***********************************************************************/
SEC002 static void DayInjectCalDav (void)
{
	ApptInfoPtr	appts;
	UInt16		i;

	gDayDavCount = 0;
	if (! ApptsH) return;

	// Db35: TRUE full year, same convention as AddrBdayGet's own call above.
	gDayDavCount = DateDAVGet ((UInt16) (Date.year + firstYear), Date.month, Date.day,
		gDayDavLines, gDayDavStart, gDayDavEnd, gDayDavHash, gDayDavRepeat, gDayDavNote, kMaxDayDav);
	if (gDayDavCount == 0) return;

	for (i = 0; i < gDayDavCount; i++)
		{
		gDayDavTimed[i] = (Boolean) (gDayDavStart[i].hours != (UInt8) apptNoTime);
		// 0xC prefix - distinct from Bday's 0xD and ToDo's 0xE pseudo-uid prefixes.
		gDayDavUID[i] = 0xC0000000UL | (gDayDavHash[i] & 0x00FFFFFFUL);
		}

	if (MemHandleResize (ApptsH,
			(NumAppts + gDayDavCount) * sizeof (ApptInfoType)) != errNone)
		{
		gDayDavCount = 0;
		return;
		}

	appts = MemHandleLock (ApptsH);

	// Untimed / all-day DAV events: prepend at the very top (same shape as
	// DayInjectTodos's own untimed pass).
	{
	UInt16 nUntimed = 0, j = 0;
	for (i = 0; i < gDayDavCount; i++) if (! gDayDavTimed[i]) nUntimed++;
	if (nUntimed)
		{
		MemMove (&appts[nUntimed], &appts[0], NumAppts * sizeof (ApptInfoType));
		for (i = 0; i < gDayDavCount; i++)
			if (! gDayDavTimed[i])
				{
				MemSet (&appts[j].startTime, sizeof (TimeType), 0xFF);	// untimed
				MemSet (&appts[j].endTime,   sizeof (TimeType), 0xFF);
				appts[j].recordNum = (UInt16) (kDayDavBase + i);
				j++;
				}
		NumAppts = (UInt16)(NumAppts + nUntimed);
		}
	}

	// Timed DAV events: insert each at its start time, before the next timed
	// row. UNLIKE DayInjectTodos's own timed pass, this deliberately does NOT
	// reuse an existing empty hour-slot's row (which would keep that empty
	// slot's own pre-set end time, not the event's real one) and does NOT
	// collapse to a zero-length span - a synced calendar event has a real
	// utcEnd/localEnd (gDayDavEnd[i]), unlike a ToDo due-time, so it gets a
	// genuine time bar the same way a real appointment would.
	for (i = 0; i < gDayDavCount; i++)
		{
		UInt16 t, pos;
		if (! gDayDavTimed[i]) continue;
		t = TimeToInt (gDayDavStart[i]);
		for (pos = 0; pos < NumAppts; pos++)
			{
			UInt16 st = TimeToInt (appts[pos].startTime);
			if (st != (UInt16) apptNoTime && st >= t) break;
			}
		MemMove (&appts[pos + 1], &appts[pos],
					(NumAppts - pos) * sizeof (ApptInfoType));
		appts[pos].startTime = gDayDavStart[i];
		appts[pos].endTime   = gDayDavEnd[i];
		appts[pos].recordNum = (UInt16) (kDayDavBase + i);
		NumAppts++;
		}

	MemPtrUnlock (appts);
}


/***********************************************************************
 *
 * FUNCTION:    DayInjectLocationText
 *
 * DESCRIPTION: Db35 -- prefetch each row's "@ location" summary text once
 *              per day-load (after DayInjectTodos/DayInjectBdays, so
 *              apptIndex already matches the final row layout). ToDo/
 *              birthday sentinel rows get an empty entry (they're never
 *              real ApptDB records). The actual DB lookup is one
 *              cross-segment call per row (LocSummaryForRecord, locdb.c) --
 *              kept out of sec001 to save code space; this function itself
 *              is just the loop.
 *
 ***********************************************************************/
SEC001 static void DayInjectLocationText (void)
{
	ApptInfoPtr	appts;
	UInt16		i;

	if (! ApptsH) return;

	appts = MemHandleLock (ApptsH);
	for (i = 0; i < NumAppts && i < kMaxDayLocRows; i++)
		{
		// Db35: emptySlot (0xffff) is an empty hour row, not a real ApptDB
		// record -- DmRecordInfo on it is a fatal "index out of range".
		// emptySlot is itself >= kDayDavBase, so this one comparison alone
		// already covers "emptySlot OR any sentinel row" (sec001 is
		// hard-full; every byte matters).
		if (appts[i].recordNum >= kDayDavBase)
			gDayLocText[i][0] = '\0';
		else
			LocSummaryForRecord (ApptDB, appts[i].recordNum, gDayLocText[i], kLocSummaryLen);
		}
	MemPtrUnlock (appts);
}


/***********************************************************************
 *
 * FUNCTION:    DayLaunchTodo
 *
 * DESCRIPTION: Db35 -- switch to ToDo35, positioned on the ToDo record that
 *              backs day-view sentinel row 'slot'.
 *
 ***********************************************************************/
SEC001 static void DayLaunchTodo (UInt16 slot)
{
	DmOpenRef	db;

	if (slot >= gDayTodoCount) return;
	if (ToDoGetDatabase (&db, dmModeReadOnly) != errNone || ! db)
		return;
	LaunchToDoWithRecord (db, gDayTodoRec[slot]);
	DmCloseDatabase (db);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewLayoutDay
 *
 * DESCRIPTION: This routine builds a list of: untimed appointment, 
 *              timed appointment, and empty time slots, for the 
 *              current day (the date store in the global variable Date).
 *
 * PARAMETERS:  retrieve - true if the list if appointment should be 
 *
 * RETURNED:    nothing
 *
 * NOTE:			the global variables ApptsH and NumAppts are set by
 *             this routine.
 *              
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	5/1/96	Initial Revision
 *			rbb	4/9/99	Don't use extra line when end time matches start
 *			rbb	5/18/99	Events ending on the hour caused display to skip an hour
 *			jmp	10/7/99	Replace GetObjectPtr() with FrmGetFormPtr() and 
 *								FrmGetObjectIndex(); fixes bug #22548.
 *
 ***********************************************************************/
SEC001 static void DayViewLayoutDay (Boolean retieve)
{
	Int16				i, j;
	Int16				index;
	Int16				numRows;
	Int16				height;
	Int16				lineHeight;
	Int16				iconsWidth;
	Int16				tableHeight;
	Int16				columnWidth;
	FontID			fontID;
	FontID			currFont;
	TablePtr			table;
	TimeType			next;
	TimeType			endTime;
	Boolean			replace;
	Boolean			addEndTime;
	ApptInfoPtr		appts;
	ApptInfoPtr		apptsOnly;
	RectangleType 	r;
	FormPtr			frm;
	

	// Get a list of: untimed appointment and timed appointment for the
	// current day.
	if (retieve)
		{
		if (ApptsOnlyH) MemHandleFree (ApptsOnlyH);
		ApptGetAppointments (ApptDB, Date, 1, &ApptsOnlyH, &NumApptsOnly);
		}


	// Free the existing list.
	if (ApptsH)
		MemHandleFree (ApptsH);

	// If there are no appointsment on the day, fill in the appointment list
	// with empty time slots.
	if (! ApptsOnlyH)
		{
		NumAppts = DayEndHour - DayStartHour + 1;
		ApptsH = MemHandleNew (NumAppts * sizeof (ApptInfoType));
		appts = MemHandleLock (ApptsH);

		for (i = 0; i < NumAppts; i++)
			{
			appts[i].startTime.hours = DayStartHour + i;				
			appts[i].startTime.minutes = 0;				
			appts[i].endTime.hours = DayStartHour + i + 1;				
			appts[i].endTime.minutes = 0;				
			appts[i].recordNum = emptySlot;	
			}
			
		DayViewCheckForConflicts ();
		MemHandleUnlock (ApptsH);
		DayInjectTodos ();			/* Db35: prepend due/undated ToDo35 rows */
		DayInjectBdays ();			/* Db35: prepend Address35 birthday/anniversary rows */
		DayInjectCalDav ();			/* Db35: prepend/insert synced CalDAV Sync events */
		DayInjectLocationText ();	/* Db35: prefetch "@ location" suffixes */
		return;
		}

	// Merge empty time slots into the appointment list.
	//
	// Allocate space for the maximun number of empty time slots that
	// we may need to add. 
	ApptsH = MemHandleNew ((NumApptsOnly+(hoursPerDay*2)) * sizeof (ApptInfoType));
	appts = MemHandleLock (ApptsH);
	NumAppts = 0;
	index = 0;

	// Add the untimed events, the timed events, and a blank time slot for 
	// the end-time of each timed event.
	apptsOnly = MemHandleLock (ApptsOnlyH);
	for (i = 0; i < NumApptsOnly; i++)
		{
		// Find the correct position at which to insert the current event.
		replace = false;
		for (j = index; j < NumAppts; j++)
			{
			if (appts[j].recordNum == emptySlot)
				{
				// If an empty time slot with the same start-time already exist, then
				// replace it.
				if (TimeToInt (appts[j].startTime) == TimeToInt (apptsOnly[i].startTime))
					{
					replace = true;
					break;
					}

				// If we find an empty time slot that has an start-time before
				// the start-time of the current event and an end-time after the 
				// the start-time of the current event, adjust the end-time of the 
				// empty time such that it is equal to the start of the current event.
			 	if (TimeToInt (appts[j].startTime) < TimeToInt (apptsOnly[i].startTime) &&
			 		 TimeToInt (appts[j].endTime)   > TimeToInt (apptsOnly[i].startTime))
			 		appts[j].endTime = apptsOnly[i].startTime;
			 	}
			
			if (TimeToInt (appts[j].startTime) > TimeToInt (apptsOnly[i].startTime))
				{
				// Make room for the empty time slot we're about to add.
				MemMove (&appts[j+1], &appts[j], (NumAppts-j) * sizeof (ApptInfoType));
				break;
				}
			}

		// Add the event to the list.
		appts[j] = apptsOnly[i];
		index = j + 1;
		if (! replace)
			NumAppts++;
			

		// If the event is a timed event add an empty time slot to display the end-time.
		// If the event has no duration, skip it to avoid displaying the same time twice.
		if ( (TimeToInt (apptsOnly[i].startTime) != apptNoTime)
				&& (TimeToInt (apptsOnly[i].startTime) != TimeToInt (apptsOnly[i].endTime)) )
			{
			// Find the correct position at which to insert the end-time time slot.
			addEndTime = true;
			for (j = index; j < NumAppts; j++)
				{
				// If an event already exist that has a start-time equal to the 
				// end-time of the current event then we don't need to add an 
				//  end-time time slot.
				if (TimeToInt (appts[j].startTime) == TimeToInt (apptsOnly[i].endTime))
					{
					addEndTime = false;
					break;
					}
					
				// We're found the position to insert the empty time slot when we find 
				// an appointment with a start-time greater than the end-time of the 
				// current event.
				if (TimeToInt (appts[j].startTime) > TimeToInt (apptsOnly[i].endTime))
					{
					// Make room for the empty time slot we're about to add.
					MemMove (&appts[j+1], &appts[j], (NumAppts-j) * sizeof (ApptInfoType));
					break;
					}
				}
			
			if (addEndTime)
				{
				// The end time of the empty time slot is the earlier of:
				//		o the start time plus one hour
				//		o 11:55 pm
				//		o the start time of the next event.
				if (apptsOnly[i].endTime.hours < 23)
					{
					endTime.hours = apptsOnly[i].endTime.hours + 1;
					endTime.minutes = 0;
//					endTime.minutes = apptsOnly[i].endTime.minutes;
					}
				else
					{
					endTime.hours = 23;		// max end time is 11:55 pm
					endTime.minutes = 55;
					}
	
				if (j < NumAppts && TimeToInt(endTime) > TimeToInt(appts[j+1].startTime))
					endTime = appts[j+1].startTime;
		
	
				appts[j].recordNum = emptySlot;
				appts[j].startTime = apptsOnly[i].endTime;
				appts[j].endTime = endTime;
				NumAppts++;
				}
			}
		}


	// Reserve spase for the time bars.  We will show time bars if the user
	// has requested them or if there are overlapping appointments.
	DayViewCheckForConflicts ();


	// Determine if that is space to add empty time slot, these time slot are in
	// addition to the empty time slot that represent end times of events.
	frm = FrmGetFormPtr (DayView);
	table = FrmGetObjectPtr (frm, FrmGetObjectIndex (frm, DayTable));
	numRows = TblGetNumberOfRows (table);
	if (( ! CompressDayView) || (numRows > NumAppts))
		{
		TblGetBounds (table, &r);
		tableHeight = r.extent.y;
		height = 0;
		columnWidth = TblGetColumnWidth (table, descColumn);

		currFont = FntSetFont (apptEmptyDescFont);
		lineHeight = FntLineHeight ();
		FntSetFont (currFont);

		if (CompressDayView)
			{
			for (j = 0; j < NumAppts; j++)
				{
				height += DayViewGetDescriptionHeight (j, columnWidth, tableHeight, &iconsWidth, &fontID);
				if (height >= tableHeight)
					break;
				}
			}
		
		// Add empty time slots to the list of appointment until the table is full.
		next.hours = DayStartHour;
		next.minutes = 0;
		i = 0;

		while (( ! CompressDayView) || (height + lineHeight <= tableHeight))
			{
			if ((i < NumAppts) &&
				 (TimeToInt (next) >= TimeToInt (appts[i].startTime)))
				{
				if (TimeToInt (next) <= TimeToInt (appts[i].endTime) &&
					(appts[i].endTime.hours >= DayStartHour))
					{
					next = appts[i].endTime;
					if (next.minutes || (TimeToInt (appts[i].startTime)
													== TimeToInt (appts[i].endTime)))
						{
						next.hours++;
						next.minutes = 0;
						}
					}
				i++;
				}

			// Insert an empty time slot if we're not passed the end of the 
			// day.
			else if ((next.hours < DayEndHour) || 
						((next.hours == DayEndHour) && next.minutes == 0))
				{
				MemMove (&appts[i+1], &appts[i], (NumAppts-i) * sizeof (ApptInfoType));
				NumAppts++;
	
				appts[i].startTime = next;
				appts[i].recordNum = emptySlot;	
	
				// The end time is the beginning of the next hour or the 
				// start time of the next appointment, which ever is earliest.
				next.hours++;
				next.minutes = 0;
				if ( (i+1 < NumAppts) &&
					  (TimeToInt (next) > TimeToInt (appts[i+1].startTime)))
					next = appts[i+1].startTime;
	
				appts[i].endTime = next;
				
				height += DayViewGetDescriptionHeight (i, columnWidth, tableHeight, &iconsWidth, &fontID);
				i++;
				}

			else if (i < NumAppts)
				{
				next.hours++;
				next.minutes = 0;
				}
			
			else
				break;
			}
		}
				
	MemHandleUnlock (ApptsOnlyH);
			
	// Release any unused space in the appointment list;
	MemHandleUnlock (ApptsH);
	MemHandleResize (ApptsH, (NumAppts * sizeof (ApptInfoType)));

	DayInjectTodos ();			/* Db35: prepend due/undated ToDo35 rows */
	DayInjectBdays ();			/* Db35: prepend Address35 birthday/anniversary rows */
	DayInjectCalDav ();			/* Db35: prepend/insert synced CalDAV Sync events */
	DayInjectLocationText ();	/* Db35: prefetch "@ location" suffixes */
}


/***********************************************************************
 *
 * FUNCTION:    DayViewNewAppointment
 *
 * DESCRIPTION: This routine adds a new untimed appointment to the top of
 *              current day.
 *
 * PARAMETERS:  event - pointer to the keyDown event, or NULL
 *
 * RETURNED:    true if the key has handle by this routine
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	7/29/95		Initial Revision
 *
 ***********************************************************************/
SEC001 static Boolean DayViewNewAppointment (EventType* event)
{
	Err error;
	Int16 row;
	Int16 apptIndex;
	UInt16 recordNum;
	Char desc [2];
	FormPtr frm;
	TablePtr table;
	TimeType startTime;
	TimeType endTime;
	Boolean handled = false;
	ApptDBRecordType newAppt;
	ApptDateTimeType when;
	

	TimeToInt (startTime) = apptNoTime;
	TimeToInt (endTime) = apptNoTime;
	*desc = 0;

	// We'll have an event if the appointment is being created as the result
	// writting a character when no appoint is selected.
	if (event)
		{
		if (TxtCharIsDigit (event->data.keyDown.chr))
			{
			EvtAddEventToQueue (event);
			
			startTime.hours = event->data.keyDown.chr - '0';
			startTime.minutes = 0;
			endTime.hours = startTime.hours + 1;
			endTime.minutes = 0;
			if (!GetTime (&startTime, &endTime, setTimeTitleStrID))
				return (true);
			}

		// Convert lower case alpha character to upper-case.
		else
			{
			desc[0] = event->data.keyDown.chr;
			desc[1] = 0;
			if ((UInt8)desc[0] >= 'a' && (UInt8)desc[0] <= 'z')
				desc[0] -= ('a' - 'A');
			}
		handled = true;
		}


	// Limit the number of appointments that can be enter on a day.
	if (NumApptsOnly >= apptMaxPerDay)
		return (true);

	// Create a untimed appointment on the current day.
	MemSet (&newAppt, sizeof (newAppt), 0);
	when.startTime = startTime;
	when.endTime = endTime;
	when.date = Date;
	newAppt.when = &when;
	newAppt.description = desc;
	/* Db35: alarms removed. */
	error = ApptNewRecord (ApptDB, &newAppt, &recordNum);
	
	// If necessary display an alert that indicates that the new record could 
	// not be created.
	if (error)
		{
		FrmAlert (DeviceFullAlert);
		return (true);
		}

	CurrentRecord = recordNum;
	ItemSelected = true;

	frm = FrmGetActiveForm ();
	table = GetObjectPtr (DayTable);
	
	// Reload and redraw the appointment table.
	DayViewLayoutDay (true);
	DayViewLoadTable ();
	TblRedrawTable (table);
	DayViewDrawTimeBars ();


	// Give the focus to the new item.
	apptIndex = DayViewFindAppointment (recordNum);
	TblFindRowID (table, apptIndex, &row);
	FrmSetFocus (frm, FrmGetObjectIndex (frm, DayTable));
	TblGrabFocus (table, row, descColumn);
	FldGrabFocus (TblGetCurrentField (table));
	
	return (handled);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewDeleteAppointment
 *
 * DESCRIPTION: This routine deletes the selected appointment.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/29/95		Initial Revision
 *
 ***********************************************************************/
SEC001 static void DayViewDeleteAppointment (void)
{
	Int16 row;
	Int16 column;
	Int16 apptIndex;
	UInt16 recordNum;
	TablePtr table;
	ApptInfoPtr	appts;

		
	// If appointment is selected, return.
	table = GetObjectPtr (DayTable);
	if (! TblEditing (table))
		return;

	TblGetSelection (table, &row, &column);
	TblReleaseFocus (table);


	// Get the record index.
	apptIndex = TblGetRowID (table, row);
	appts = MemHandleLock (ApptsH);
	recordNum = appts[apptIndex].recordNum;
	MemHandleUnlock (ApptsH);
	
	ErrFatalDisplayIf ((recordNum != CurrentRecord), "Wrong record");

	// Clear the edit state, this will delete the current record if is 
	// blank.
	if (! ClearEditState ())
		{
		// Delete the record,  this routine will display an appropriate 
		// dialog to confirm the action.  If the dialog is canceled 
		// don't update the display.
		if (! DeleteRecord (recordNum))
			return;
		}

	// Layout the day again, empty time slots may needed to be add to 
	// full-in the gap left by the deleted appointment.
	DayViewLayoutDay (true);

	DayViewLoadTable ();
	TblRedrawTable (table);
	DayViewDrawTimeBars ();
}


/***********************************************************************
 *
 * FUNCTION:    DuplicatePromptRepeat
 *
 * DESCRIPTION: Db35: "This Only" / "All" / "Cancel" confirm dialog for
 *              Duplicate Event on a repeating event, message in bold.
 *              Custom FORM (not a native ALERT): an ALERT auto-sizes its
 *              buttons from label text with no width override, and this
 *              dialog needs specific geometry (full app width, bottom-
 *              aligned, buttons evenly spaced 5px from the borders and from
 *              each other) -- see DuplicateRepeatDlg's resource. Same idiom
 *              as ToDo35's identical dialog.
 *
 * RETURNED:    DuplicateThisOccurrence / DuplicateAllOccurrences / DuplicateCancel
 *
 ***********************************************************************/
SEC001 static UInt16 DuplicatePromptRepeat (void)
{
	FormPtr	prev = FrmGetActiveForm ();
	FormPtr	frm = FrmInitForm (DuplicateRepeatDlg);
	UInt16	result = DuplicateCancel;
	Boolean	done = false;
	const Char *msg = "This event repeats. Duplicate only this occurrence "
							"or the complete repeat setup?";

	FrmSetActiveForm (frm);
	FrmDrawForm (frm);

	// Hand-drawn, word-wrapped, bold message -- not a resource LABEL (LABEL
	// objects don't word-wrap).
	{
		const Char *	p = msg;
		Int16				y = 18;
		Int16				lineHeight;
		UInt16			len;
		FontID			oldFont = FntSetFont (boldFont);

		lineHeight = FntLineHeight ();
		while (*p)
			{
			len = FldWordWrap (p, 144);
			WinDrawChars (p, len, 6, y);
			p += len;
			while (*p == spaceChr) p++;	// don't let a leading space start the next line
			y += lineHeight;
			}
		FntSetFont (oldFont);
	}

	while (! done)
		{
		EventType	event;

		EvtGetEvent (&event, evtWaitForever);
		if (event.eType == ctlSelectEvent)
			{
			switch (event.data.ctlSelect.controlID)
				{
				case btnDupThisOnly:
					result = DuplicateThisOccurrence; done = true; break;
				case btnDupAll:
					result = DuplicateAllOccurrences; done = true; break;
				case btnDupCancel:
					result = DuplicateCancel; done = true; break;
				}
			continue;
			}
		if (event.eType == appStopEvent)
			{
			EvtAddEventToQueue (&event);
			done = true;
			continue;
			}
		FrmDispatchEvent (&event);
		}

	FrmEraseForm (frm);
	FrmDeleteForm (frm);
	if (prev) FrmSetActiveForm (prev);
	return result;
}


/***********************************************************************
 *
 * FUNCTION:    DayViewDuplicateEvent
 *
 * DESCRIPTION: Db35: duplicate the selected event (Record menu). If the
 *              event repeats, ask whether to duplicate just this occurrence
 *              (a one-off event on the currently viewed date, repeat NOT
 *              copied) or the complete repeat setup (the duplicate repeats
 *              independently of the original, from the same anchor date).
 *              A non-repeating event is always duplicated as a single event
 *              on its own date -- no prompt needed.
 *
 ***********************************************************************/
/* Db35: core duplicate logic, independent of the Day View table -- shared by
 * DayViewDuplicateEvent (Day View's own Record menu, below) and
 * DetailsDuplicateEvent (the Details dialog's new Record menu -- Details is
 * now a full-screen form that intercepts focus from the Day View, so it
 * needs its own path to this, not just the table's). Operates purely on
 * `recordNum` (a valid ApptDB record) and the global `Date` (used as the
 * one-off duplicate's date, same as before). Returns true if a duplicate was
 * actually created (false on user cancel or DeviceFullAlert). */
SEC001 static Boolean ApptDuplicateEventCore (UInt16 recordNum)
{
	UInt16				attr, newIndex;
	Boolean				hasRepeat;
	UInt16				choice = DuplicateThisOccurrence;
	MemHandle			recordH;
	ApptDBRecordType	apptRec;
	ApptDBRecordType	newRec;
	ApptDateTimeType	when;
	Err					error;

	ApptGetRecord (ApptDB, recordNum, &apptRec, &recordH);

	// Db35: does this event repeat? If so, ask which way to duplicate it.
	hasRepeat = (Boolean) (apptRec.repeat != NULL);
	if (hasRepeat)
		{
		choice = DuplicatePromptRepeat ();
		if (choice == DuplicateCancel)
			{
			MemHandleUnlock (recordH);
			return false;
			}
		}

	MemSet (&when, sizeof (when), 0);
	when.startTime = apptRec.when->startTime;
	when.endTime = apptRec.when->endTime;

	// Db35: zero the whole record first -- `newRec` is a local, and a field
	// left unset here is stack GARBAGE, not a safe default. This is exactly
	// what happened to endDateOffset below before this fix (found via human
	// testing: duplicating a multi-day span event produced the right start
	// date but a garbage end date, spraying bogus continuation rows).
	MemSet (&newRec, sizeof (newRec), 0);
	newRec.when = &when;
	newRec.alarm = NULL;			// Db35: alarms removed -- duplicates never carry one.
	newRec.exceptions = NULL;	// a fresh duplicate starts with no exceptions.
	newRec.description = apptRec.description;
	newRec.note = apptRec.note;
	newRec.endDateOffset = apptRec.endDateOffset;	/* Db35: preserve an
								 * existing multi-day span on the duplicate,
								 * same as every other ApptNewRecord call site
								 * in this file. */

	if (choice == DuplicateAllOccurrences)
		{
		// Keep the same anchor date and repeat setup -- the duplicate repeats
		// independently going forward, in parallel with the original.
		when.date = apptRec.when->date;
		newRec.repeat = apptRec.repeat;
		}
	else
		{
		// One-off duplicate on the currently viewed date (which, for a
		// non-repeating event, is simply that event's own date already).
		when.date = Date;
		newRec.repeat = NULL;
		}

	// Get the secret (Private) setting of the source record, same as
	// CreateException's precedent.
	DmRecordInfo (ApptDB, recordNum, &attr, NULL, NULL);

	error = ApptNewRecord (ApptDB, &newRec, &newIndex);

	MemHandleUnlock (recordH);

	if (error)
		{
		FrmAlert (DeviceFullAlert);
		return false;
		}

	if (attr & dmRecAttrSecret)
		{
		DmRecordInfo (ApptDB, newIndex, &attr, NULL, NULL);
		attr |= dmRecAttrSecret;
		DmSetRecordInfo (ApptDB, newIndex, &attr, NULL);
		}

	return true;
}

SEC001 static void DayViewDuplicateEvent (void)
{
	TablePtr				table;

	table = GetObjectPtr (DayTable);
	if (! TblEditing (table))
		return;
	TblReleaseFocus (table);

	if (! ApptDuplicateEventCore (CurrentRecord))
		return;

	// Layout the day again -- the duplicate may land as a new row today.
	DayViewLayoutDay (true);
	DayViewLoadTable ();
	TblRedrawTable (table);
	DayViewDrawTimeBars ();
}

/* Db35: Details dialog's own "Duplicate Event" (Record menu) -- no table to
 * refresh here (Details is a separate full-screen form covering it); the
 * Day View picks up the new record naturally via its existing
 * FrmUpdateForm/DayViewUpdateDisplay path whenever Details eventually
 * closes (that path unconditionally re-reads the table at its tail even for
 * updateCode==0), same as any other database change made while Details is
 * open. */
SEC001 static void DetailsDuplicateEvent (void)
{
	(void) ApptDuplicateEventCore (CurrentRecord);
}

/* Db35: Details dialog's own Record-menu command dispatch (see
 * DetailsHandleEvent's menuEvent case). Only Duplicate/Beam are new here --
 * Delete and Note are already reachable via Details' own dedicated buttons
 * (DetailsDeleteButton/DetailsNoteButton), so they're deliberately not
 * duplicated into this menu too. Edit-menu system commands (Cut/Copy/Paste/
 * Undo/...) are NOT matched here -- they fall through (handled=false) to the
 * ROM's own default field handling in FrmHandleEvent, same convention as
 * DayViewDoCommand's own trailing "else handled = false" case. */
/* Db35: deliberately UNTAGGED (main segment) - see DetailsDeleteAppointment's
 * own comment; same reasoning (sec001/sec002/sec003 all at their limits).
 * Calls DateSendRecord (DateTransfer.c, sec003) - a safe cross-segment call
 * from main either way. */
static Boolean DetailsDoCommand (UInt16 command)
{
	// Db35: CalDAV items are read-only, and both commands below touch
	// ApptDB with CurrentRecord (a DAV sentinel here - undefined/crash).
	// Both Duplicate (would create a modified copy) and Beam (would send
	// one) are genuine "trying to act on" attempts, same as
	// DayDavBlockEdit's own list - alert instead of a silent no-op.
	if (IsDayDav (CurrentRecord))
		{
		FrmAlert (altDavReadOnly);
		return true;
		}
	if (command == DuplicateCmd)
		{
		DetailsDuplicateEvent ();
		return true;
		}
	if (command == SendRecordCmd)
		{
		DateSendRecord (ApptDB, CurrentRecord);
		return true;
		}
	return false;
}


/***********************************************************************
 *
 * FUNCTION:    DayViewDeleteNote
 *
 * DESCRIPTION: This routine deletes the note attached to the selected 
 *              appointment.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	5/1/95		Initial Revision
 *
 ***********************************************************************/
SEC001 static void DayViewDeleteNote ()
{
	UInt16 i;
	Int16 row;
	Int16 column;
	Int16 height;
	Int16 tableHeight;
	Int16 iconsWidth;
	Int16 newHeight;
	Int16 apptIndex;
	FormPtr alert;
	UInt16 alertButton;
	Int16 columnWidth;
	UInt16 rowsInTable;
	FontID fontID;
	TablePtr table;
	Boolean empty;
	Boolean exception = false;
	Boolean splitEvent = false;
	MemHandle recordH;
	RectangleType r;
	ApptDBRecordType apptRec;
		
	table = GetObjectPtr (DayTable);

	// Check if we are editing an item.
	if (! TblEditing (table))
		return;

	// Check if the record has a note attached.
	ApptGetRecord (ApptDB, CurrentRecord, &apptRec, &recordH);
	empty = ( (! apptRec.note) || (*apptRec.note == 0));
	MemHandleUnlock (recordH);
	if (empty) return;


	// If we're changing a repeating appointmemt, check if all occurrences
	// are being changed, or if and exception is being created.
	if (apptRec.repeat)
		{
		alert = FrmInitForm (RangeDialog);
		alertButton = FrmDoDialog (alert);
		FrmDeleteForm (alert);
		
		if (alertButton == RangeCancelButton)
			return;
			
		else if (alertButton == RangeCurrentButton)
			exception = true;
			
		else if (alertButton == RangeFutureButton)
			splitEvent = true;
		}

	else if (FrmAlert(DeleteNoteAlert) != DeleteNoteYes)
		return;


	// Get the selected row.
	TblGetSelection (table, &row, &column);

	// Release the focus, this will unlock the current record.
	TblReleaseFocus (table);

	// Remove the note field from the database record.
	if (! DeleteNote (exception, splitEvent))
		return;

	// Mark the current row non-usable so the it will redraw.
	TblSetRowUsable (table, row, false);
	
	// Get the current height of the description.
	height = TblGetRowHeight (table, row);

	// Get the new height of the description, the desciption may be short
	// because we can draw in the space vacated by the note indicator.
	apptIndex = TblGetRowID (table, row);
	columnWidth = TblGetColumnWidth (table, descColumn);
	TblGetBounds (table, &r);
	tableHeight = r.extent.y;
	newHeight = DayViewGetDescriptionHeight (apptIndex, columnWidth, tableHeight, &iconsWidth, &fontID);
	
	// If the height of the description has changed, invalid all the row
	// after the current row so that they be drawn.
	if (height != newHeight)
		{
		rowsInTable = TblGetNumberOfRows (table);
		for (i = row+1; i < rowsInTable; i++)
			TblSetRowUsable (table, i, false);
		}
	
	// Creating an exception will move records around, so we need to rebuild
	// the appointment list.
	if (exception)
		DayViewLayoutDay (true);

	DayViewLoadTable ();
	TblRedrawTable (table);
	DayViewDrawTimeBars ();

	DayViewRestoreEditState ();
}


/***********************************************************************
 *
 * FUNCTION:    DayViewSelectTime
 *
 * DESCRIPTION: This routine is called when a time item in the day view
 *              table is selected.  The time picker is displayed, if 
 *              the start or end time of the appointment is changed, 
 *              the day's appointments are resorted and the appointment
 *              table is redrawn.
 *
 * PARAMETERS:  table - table of appointments
 *              row   - row selected
 *              
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/12/95		Initial Revision
 *
 ***********************************************************************/
SEC001 static void DayViewSelectTime (TablePtr table, UInt16 row)
{
	UInt16 apptIndex;
	UInt16 recordNum;
	Boolean sameTime;
	Boolean moved;
	TimeType startTime;
	TimeType endTime;
	ApptInfoPtr appts;

	// Get the record index, start time, and end time.
	apptIndex = TblGetRowID (table, row);
	appts = MemHandleLock (ApptsH);
	startTime = appts[apptIndex].startTime;
	endTime = appts[apptIndex].endTime;
	recordNum = appts[apptIndex].recordNum;
	MemHandleUnlock (ApptsH);

	// Db35: a row showing a CONTINUATION day of a multi-day span has its
	// start/end times CLAMPED for display (see ApptGetAppointments's
	// per-day placement) -- not the record's real values. A quick
	// time-column edit here would apply those clamped values as if they
	// were real, corrupting the span. Redirect to Event Details instead,
	// same "open Details, never inline-edit" idiom the description-column
	// tap already uses for an existing event.
	if (! IsDaySentinelRow (recordNum))
		{
		ApptDBRecordType apptRec;
		MemHandle recordH;
		if (ApptGetRecord (ApptDB, recordNum, &apptRec, &recordH) == 0)
			{
			Boolean continuation = (Boolean) (apptRec.endDateOffset != 0 &&
				DateToInt (Date) != DateToInt (apptRec.when->date));
			MemHandleUnlock (recordH);
			if (continuation)
				{
				TblUnhighlightSelection (table);
				CurrentRecord = recordNum;
				ItemSelected = true;
				gDaySkipRestoreEditState = true;
				FrmPopupForm (DetailsDialog);
				return;
				}
			}
		}

	// Display the time picker, exit if the time picker if not confirmed.
	sameTime = (! GetTime (&startTime, &endTime, setTimeTitleStrID));
	if (sameTime)
		{
		DayViewRestoreEditState ();
		return;
		}
		

	if (MoveEvent (&recordNum, startTime, endTime, Date, true, &moved))
		return;
	
	if (moved)
		{
		CurrentRecord = recordNum;	
		DayViewLayoutDay (true);
		DayViewLoadTable ();
		TblRedrawTable (table);
		DayViewDrawTimeBars ();
		}

	DayViewRestoreEditState ();
}


/***********************************************************************
 *
 * FUNCTION:    DayViewSelectIcons
 *
 * DESCRIPTION: This routine handles the selection of the note, alarm 
 *              and repeat icons.  If determains which icon the pen is
 *              on and tracks the pen until it is released.  If the pen 
 *              is released within the bounds of an icon we nagivate to
 *              apprpperiate form,
 *
 * PARAMETERS:  table	 - pointer to DayView table
 *					 row		 - row in table DayView icon is in
 *					 r			 - DayView's icon's bounds
 *					 selected - whether to draw the icon selected or unselected
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			jmp	10/31/99	Initial Revision
 *			jmp	11/06/99	Oops, was just doing a WinResetClip() instead of
 *								save/restore WinSetClip().
 *
 ***********************************************************************/
SEC001 static void DayViewSelectIconRect(TablePtr table, Int16 row, RectangleType *r, Boolean selected)
{
	RectangleType savedClip;
	
	WinGetClip(&savedClip);
	
	// The table selection code will select or unselect the entire item as
	// the icons are part of a custom item for the Datebook.  So, we restrict
	// what the table-selection code will do by clipping the rectangle to
	// the specific icon we want selected or unselected.
	//
	WinSetClip(r);
	
	// Select or unselect the icon as requested.
	//
	if (selected)
		TblSelectItem(table, row, descColumn);
	else
		TblUnhighlightSelection(table);

	// Restore clipping.
	//
	WinSetClip(&savedClip);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewSelectIcons
 *
 * DESCRIPTION: This routine handles the selection of the note, alarm 
 *              and repeat icons.  If determains which icon the pen is
 *              on and tracks the pen until it is released.  If the pen 
 *              is released within the bounds of an icon we nagivate to
 *              apprpperiate form,
 *
 * PARAMETERS:  event  - pointer to a tblEnter event.
 *
 * RETURNED:    true if an icon is selected.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	5/16/96	Initial Revision
 *			jmp	9/17/99	Use NewNoteView instead of NoteView.
 *			jmp	10/31/99 Replace calls to WinInvertRectangle() with calls to
 *								DayViewSelectIconRect().
 *
 ***********************************************************************/
SEC001 static Boolean DayViewSelectIcons (EventType* event)
{
	UInt16			apptIndex;
	UInt16			row;
	UInt16			recordNum;
	UInt16			iconsWidth;
	Int16				x, y;
	FontID			curFont;
	MemHandle 		recordH;
	TablePtr			table;
	Boolean			penDown = true;
	Boolean			selected = true;
	Boolean			inNoteIcon = false;
	Boolean			inAlarmIcon = false;
	Boolean			inRepeatIcon = false;
	ApptInfoPtr		appts;
	RectangleType	r;
	RectangleType	itemR;
	ApptDBRecordType apptRec;

	if (event->data.tblEnter.column != descColumn)
		return (false);

	// Check if there are any icons.
	table = (TablePtr) event->data.tblEnter.pTable;
	row = event->data.tblEnter.row;
	iconsWidth = TblGetItemInt (table, row, descColumn);
	if (! iconsWidth)
		return (false);
	
	// Check if the pen is within the bounds of the icons.
	TblGetItemBounds (table, row, descColumn, &itemR);

	r.topLeft.x = itemR.topLeft.x + itemR.extent.x - iconsWidth;
	r.topLeft.y = itemR.topLeft.y;
	r.extent.x = iconsWidth;
	curFont = FntSetFont (ApptDescFont);
	r.extent.y = FntLineHeight ();
	FntSetFont (curFont);
	
	x = event->screenX;
	y = event->screenY;
	
	if (RctPtInRectangle (x, y, &r))
		{
		// Get the record index of the selected appointment.
		apptIndex = TblGetRowID (table, row);
		appts = MemHandleLock (ApptsH);
		recordNum = appts[apptIndex].recordNum;
		MemPtrUnlock (appts);

		// Db35: a CalDAV Sync event row is a DAV sentinel, not a real
		// ApptDB index - ApptGetRecord below is undefined/crash on it
		// (confirmed real: "DateDB.c, Error querying record" tapping the
		// repeat icon on a CalDAV row). Relayed through its own sec002
		// helper (mirrors this function's own icon-hit-test + pen-tracking
		// logic, keyed off gDayDavNote/gDayDavRepeat instead of
		// apptRec.note/apptRec.repeat) - sec001 has almost no headroom left.
		if (IsDayDav (recordNum))
			return DayDavSelectIcons (table, row, recordNum, itemR, iconsWidth, x, y);

		// Get the appointment record.
		ApptGetRecord (ApptDB, recordNum, &apptRec, &recordH);

		curFont = FntSetFont (symbolFont);
		
		// Check if the pen is on the note icon.
		if (apptRec.note)
			{
			r.extent.x = FntCharWidth (symbolNote);
			inNoteIcon = RctPtInRectangle (x, y, &r);
			if (! inNoteIcon)
				r.topLeft.x += r.extent.x + 1;
			}

		// Db35: alarms removed -- no alarm icon to hit-test.

		// Check if the pen is on the repeat icon.
		if (apptRec.repeat && (! inNoteIcon) && (! inAlarmIcon) )
			{
			r.extent.x = FntCharWidth (symbolRepeat);
			inRepeatIcon = RctPtInRectangle (x, y, &r);
			if (! inRepeatIcon)
				r.topLeft.x += r.extent.x  + 1;
			}
			
		FntSetFont (curFont);
		MemHandleUnlock (recordH);

		TblReleaseFocus (table);

		// Highlight the icon.
		DayViewSelectIconRect(table, row, &r, selected);

		// Track the pen until it's released.
		do 
			{
			PenGetPoint (&x, &y, &penDown);
	
			if (RctPtInRectangle (x, y, &r))
				{
				if (!selected)
					{
					selected = true;
					DayViewSelectIconRect(table, row, &r, selected);
					}
				}
	
			else if (selected)
				{
				selected = false;
				DayViewSelectIconRect(table, row, &r, selected);
				}
	
			} while (penDown);

		if (selected)
			{
			selected = false;
			DayViewSelectIconRect(table, row, &r, selected);

			CurrentRecord = recordNum;

			if (inNoteIcon)
				FrmGotoForm (NewNoteView);
			else
				FrmPopupForm (DetailsDialog);
			return (true);
			}
		}

	return (false);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewItemSelected
 *
 * DESCRIPTION: This routine is called when an item in Day View table
 *              is selected.
 *
 * PARAMETERS:  event  - a pointer to an EventType structure
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	5/9/95	Initial Revision
 *			jmp	9/17/99	Use NewNoteView instead of NoteView.
 *
 ***********************************************************************/
SEC001 static void DayViewItemSelected (EventType* event)
{
	Err error;
	UInt16 apptIndex;
	UInt16 row;
	UInt16 column;
	UInt16 recordNum;
	UInt32 uniqueID;
	TablePtr table;
	ApptInfoPtr appts;
	ApptDBRecordType newAppt;
	ApptDateTimeType when;
	
	table = event->data.tblSelect.pTable;
	row = event->data.tblSelect.row;
	column = event->data.tblSelect.column;

	// Db35: tapping a ToDo35 row jumps to ToDo35, positioned on that record.
	{
	UInt16 ai = TblGetRowID (table, row);
	ApptInfoPtr ap = MemHandleLock (ApptsH);
	UInt16 rn = ap[ai].recordNum;
	MemPtrUnlock (ap);
	if (IsDayTodo (rn))
		{
		TblUnhighlightSelection (table);
		DayLaunchTodo ((UInt16)(rn - kDayTodoBase));
		return;
		}
	// Db35: tapping a birthday/anniversary row jumps to Address35, positioned
	// on that contact record.
	if (IsDayBday (rn))
		{
		TblUnhighlightSelection (table);
		AddrBdayGoTo (gDayBdayRec[rn - kDayBdayBase]);
		return;
		}
	// Db35: tapping a CalDAV Sync event row opens the REAL Event Details
	// dialog, populated from CalDAV data (see DayOpenDavDetails's own
	// comment).
	if (IsDayDav (rn))
		{
		TblUnhighlightSelection (table);
		DayOpenDavDetails (rn, false);
		return;
		}
	}

	if (TblRowMasked(table,row))
		{
		if (SecVerifyPW (showPrivateRecords) == true)
			{
			PrivateRecordVisualStatus = showPrivateRecords;
			DayViewUpdateDisplay (updateDisplayOptsChanged);
			}
		return;
		}
		
	// Was a time item selected?
	if (column == timeColumn)
		{
		// Unhighlight the time.
		TblUnhighlightSelection (table);
		
		// If an empty time slot has been selected then create a 
		// new appointment.
		uniqueID = TblGetRowData (table, event->data.tblEnter.row);
		if (uniqueID == 0)
			{
			// If we have reached the maximum number of displayable event, then
			// exit.
			if (NumApptsOnly >= apptMaxPerDay)
				return;

			// Get the start time, and end time of the row selected.
			apptIndex = TblGetRowID (table, row);
			appts = MemHandleLock (ApptsH);
		
			// Create a untimed appointment on the current day.
			MemSet (&newAppt, sizeof (newAppt), 0);
			when.startTime = appts[apptIndex].startTime;
			when.endTime = appts[apptIndex].endTime;
			when.date = Date;
			newAppt.when = &when;
			newAppt.description = "";

			/* Db35: alarms removed. */

			error = ApptNewRecord (ApptDB, &newAppt, &recordNum);
			
			if (! error)
				{
				appts[apptIndex].recordNum =  recordNum;
		
				// Store the unique id of the record in the row.
				DmRecordInfo (ApptDB, recordNum, NULL, &uniqueID, NULL);
				TblSetRowData (table, row, uniqueID);

				CurrentRecord = recordNum;
				ItemSelected = true;
				}

			MemHandleUnlock (ApptsH);

			// Display an alert that indicates that the new record could 
			// not be created.
			if (error)
				{
				FrmAlert (DeviceFullAlert);
				return;
				}
			}

		DayViewSelectTime (table, row);
		}


	// Was a description selected?
	else if (column == descColumn)
		{
		// Get the record index of the selected appointment.
		apptIndex = TblGetRowID (table, row);
		appts = MemHandleLock (ApptsH);
		CurrentRecord = appts[apptIndex].recordNum;
		gDetailsApptIndex = apptIndex;	/* Db35: which row -- see kDetailsApptUnknown */
		MemPtrUnlock (appts);
		
		// If the table is in edit mode then the description field
		// was selected, otherwise the note indictor must have
		// been selected.
		if (TblEditing (table))
			{
			ItemSelected = true;


			// If the row will have been visually invalidate if: an empty slot
			// was selected and the alarm preset preference is on, or
			// the end time of the event is not displayed, or the font for 
			// drawing descriptions are a different height than the font for 
			// drawing empty time slots.

			// Db35: selecting a row demotes + redraws its desc/time cells (DayEnterEditStyle),
			// which erases that row's segment of the left time bars (drawn separately, not as a
			// table column).  Refresh them so they aren't left clipped by the row redraw.
			DayViewDrawTimeBars ();

			// If the alarm preset preference is on then selecting an empty
			// appointment will visually invalidate that row.  We need to
			// redraw the table so the the alarm icon can be drawn.
			if (TblRowInvalid (table, row))
				{
				TblReleaseFocus (table);
				DayViewLayoutDay (true);
				DayViewLoadTable ();
				TblRedrawTable (table);
				DayViewDrawTimeBars ();
				DayViewRestoreEditState ();
				}
			}
		else
			FrmGotoForm (NewNoteView);
		}
}


/***********************************************************************
 *
 * FUNCTION:    DayViewResizeDescription
 *
 * DESCRIPTION: This routine is called when the height of appointment's
 *              description is changed as a result of user input.
 *              If the new hieght of the field is shorter,  more items
 *              may need to be added to the bottom of the Day.
 *              
 * PARAMETERS:  event  - a pointer to an EventType structure
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	5/18/95		Initial Revision
 *
 ***********************************************************************/
SEC001 static void DayViewResizeDescription (EventType* event)
{
	Int16 apptIndex;
	Int16 row;
	Int16 currentRow;
	Int16 currentColumn;
	FieldPtr fld;
	TablePtr table;
	RectangleType fieldR;
	
	// Get the current height of the field;
	fld = event->data.fldHeightChanged.pField;
	FldGetBounds (fld, &fieldR);

	// Have the table object resize the field and move the items below
	// the field up or down.
	table = GetObjectPtr (DayTable);
	TblHandleEvent (table, event);
	

	// If the field's height has been expanded , and we don't have items scrolled 
	// off the top of the table, just update the scrollers.

	if (event->data.fldHeightChanged.newHeight >= fieldR.extent.y)
		{
		TopVisibleAppt = TblGetRowID (table, 0);
		}
		

	// Relayout the day to add or remove empty time slots.  We don't need 
	// to rebuild the list of records.
	DayViewLayoutDay (true);
	DayViewLoadTable ();


	// Relaying-out the day migth move the row that has the focus, if so 
	// we need to correct the table's current row.
	apptIndex = DayViewFindAppointment (CurrentRecord);
	TblFindRowID (table, apptIndex, &row);
	TblGetSelection (table, &currentRow, &currentColumn);
	if (row != currentRow)
		// zzz write API routine to do this.
		table->currentRow = row;


	TblRedrawTable (table);		
	DayViewDrawTimeBars ();
}


/***********************************************************************
 *
 * FUNCTION:    DayViewSetTitle
 *
 * DESCRIPTION: Set the Day View form's title, based on the Day and
 *		LongDateFormat global variables.
 *
 * PARAMETERS:  frmP		Pointer to Day View form.
 *
 * RETURNED:    nothing
 *
 * HISTORY:
 *		09/12/99	kwk	Created by Ken Krugler.
 *		10/22/99	kwk	Directly use short date format as index into the
 *							formatting strings.
 *
 ***********************************************************************/
SEC001 static void DayViewSetTitle(FormType* frmP)
{
	Char title[longDateStrLength];
	UInt16 dateFormatIndex;
	MemHandle templateH;
	Char* templateP;

	// We can't use the long date format to guess at the short date
	// format, since there's not a one-to-one mapping set up in the
	// Formats panel. We'll directly use the short date format.
	
	
	// We need to derive the appropriate date template string based on
	// the LongDateFormat global, which is loaded from sys prefs (and
	// thus is set by the user in the Formats panel).
	if (ShortDateFormat > dfYMDWithDashes)
		{
		ErrNonFatalDisplay("Unknown short date format");
		dateFormatIndex = 0;
		}
	else
		{
		dateFormatIndex = (UInt16)ShortDateFormat;
		}
	
	templateH = DmGetResource(strRsc, DayViewFirstTitleTemplateStrID + dateFormatIndex);
	templateP = (Char*)MemHandleLock(templateH);
	/* Db35: show the REAL era year (window year + Hc_Offset), keeping the ROM
	   weekday/layout from the valid window year. See era.h. */
	EraDateTemplateToAscii(templateP, Date.month, Date.day, (Date.year+firstYear), false, title, sizeof(title) - 1);
	MemHandleUnlock(templateH);

	FrmCopyTitle (frmP, title);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewDrawTitle
 *
 * DESCRIPTION: This routine draws the day view title and highlights
 *              the current day's the day-of-week push button. 
 *
 * PARAMETERS:  void
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	8/1/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static void DayViewDrawTitle (void)
{
	UInt16	dayOfWeek;
	FormPtr	frm;

	frm = FrmGetActiveForm ();
	DayViewSetTitle(frm);
	
	// Update the day-of-week push button to highlight the correct day of
	// the week.
	dayOfWeek = DatebookDayOfWeek (Date.month, Date.day, Date.year+firstYear);
	FrmSetControlGroupSelection (frm, DayOfWeekGroup, DayDOW1Button + dayOfWeek);

	TimeDisplayed = false;
}


/***********************************************************************
 *
 * FUNCTION:    DayViewShowTime
 *
 * DESCRIPTION: This routine display the current time in the title of the
 *              day view.
 *
 * PARAMETERS:  frm - pointer to the day view form.
 *
 * RETURNED:    nothing
 *
 * NOTE:        The global variables TimeDisplayed and TimeDisplayTick are
 *					 set by this routine.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	5/14/96	Initial Revision
 *			grant 2/2/99	update TimeDisplayTick (now matches WeekViewShowTime
 *								and MonthViewShowTime)
 *
 ***********************************************************************/
SEC001 static void DayViewShowTime (void)
{
	Char				title[timeStringLength];
	DateTimeType 	dateTime;

	TimSecondsToDateTime (TimGetSeconds (), &dateTime);
	TimeToAscii (dateTime.hour, dateTime.minute, TimeFormat, title);
	FrmCopyTitle (FrmGetActiveForm (), title);
	
	TimeDisplayed = true;
	TimeDisplayTick = TimGetTicks () + timeDisplayTicks;
}


/***********************************************************************
 *
 * FUNCTION:    DayViewHideTime
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
SEC001 static void DayViewHideTime (Boolean hide)
{
	if (TimeDisplayed)
		{
		if (hide || TimeToWait() == 0)
			{
			// If the Day View is the draw window then redraw the title.
			if (WinGetDrawWindow () == FrmGetWindowHandle (FrmGetFormPtr (DayView)))
				DayViewDrawTitle ();
			}
		}
}


/***********************************************************************
 *
 * FUNCTION:    DayViewDrawDate
 *
 * DESCRIPTION: This routine display the date passed.
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
SEC001 static void DayViewDrawDate (DateType date)
{
	TablePtr table;

	// Adjust the current date.
	Date = date;

	DayViewDrawTitle ();
		
	// Get all the appointments and empty time slots on the new day.
	DayViewLayoutDay (true);

	// Determine the first appointment to display.
	DayViewSetTopAppointment ();

 	// Load the appointments and empty time slots into the table object 
	// that will display them.
	DayViewLoadTable ();
	
	// Draw the new day's events.
	table = GetObjectPtr (DayTable);
	TblRedrawTable (table);
	DayViewDrawTimeBars ();
}


/***********************************************************************
 *
 * FUNCTION:    DayViewDayOfWeekSelected
 *
 * DESCRIPTION: This routine is called when of the day-of-week push buttons
 *              is pressed.
 *
 * PARAMETERS:  event  - a pointer to an EventType structure
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/27/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static void DayViewDayOfWeekSelected (EventType* event)
{
	Int16			adjust;
	UInt16		dayOfWeek;
	UInt16		newDayOfWeek;

	// Adjust the current date.
	dayOfWeek = DatebookDayOfWeek (Date.month, Date.day, Date.year+firstYear);
	newDayOfWeek = event->data.ctlSelect.controlID - DayDOW1Button;
	if (dayOfWeek == newDayOfWeek) return;
	
	adjust = newDayOfWeek - dayOfWeek;
	DateAdjust (&Date, adjust);

	DayViewDrawDate (Date);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewGoToDate
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
SEC001 static void DayViewGoToDate (void)
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

	// Display the date picker.
	if (DatePickSelectDay (selectDayByDay, &month, &day, &year, title))
		{
		Date.day = day;
		Date.month = month;
		Date.year = year - firstYear;

		DayViewDrawDate (Date);
		}
		
	MemHandleUnlock (titleH);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewGotoAppointment
 *
 * DESCRIPTION: This routine sets gloabal variables such the Day View
 *              will display the text found by the text search
 *              command.
 *
 * PARAMETERS:  event - frmGotoEvent 
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	8/24/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static void DayViewGotoAppointment (EventType* event)
{
	TopVisibleAppt = 0;
	ItemSelected = true;
	CurrentRecord = event->data.frmGoto.recordNum;
	DayEditPosition = event->data.frmGoto.matchPos;
	DayEditSelectionLength = event->data.frmGoto.matchLen;
	
	SetDateToNextOccurrence (CurrentRecord);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewScroll
 *
 * DESCRIPTION: This routine scrolls the day of of appointments
 *              in the direction specified.
 *
 * PARAMETERS:  direction - up or dowm
 *              wrap      - if true the day is wrap to the first appointment
 *                          if we're at the end of the day.
 *
 * RETURNED:    Db35: true if the view actually scrolled (or wrapped), false
 *              if it was already at that edge and did nothing -- callers
 *              that need to know "is there anything left to scroll to"
 *              (e.g. hardware Up/Down falling through to the previous/next
 *              day) should use THIS, not a separately cached button-enabled
 *              flag: CtlEnabled(DayUpButton/DayDownButton) is only ever as
 *              fresh as the last DayViewUpdateScrollers call, which real-
 *              device testing showed can desync from the live TopVisibleAppt/
 *              NumAppts state this function itself checks (worse right after
 *              ToDo/birthday rows are injected/removed) -- "sometimes works,
 *              sometimes doesn't" depending on exactly when that happened.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	2/21/95	Initial Revision
 *
 ***********************************************************************/
SEC001 static Boolean DayViewScroll (WinDirectionType direction, Boolean wrap)
{
	Int16				row;
	Int16				height;
	UInt16			apptIndex;
	Int16				iconsWidth;
	Int16 			columnWidth;
	Int16 			tableHeight;
	FontID			fontID;
	TablePtr			table;
	RectangleType	r;

	table = GetObjectPtr (DayTable);
	TblReleaseFocus (table);

	// Get the height of the table and the width of the description
	// column.
	TblGetBounds (table, &r);
	tableHeight = r.extent.y;
	height = 0;
	columnWidth = TblGetColumnWidth (table, descColumn);

	apptIndex = TopVisibleAppt;

	// Scroll the table down.
	if (direction == winDown)
		{
		// If we're at the bottom of the day, and page wrapping is allowed,
		// go to the top of the page.
		if (wrap && ( ! CtlEnabled (GetObjectPtr (DayDownButton))))
			TopVisibleAppt = 0;

		else if (TopVisibleAppt+1 >= NumAppts)
			return false;

		else
			{
			row = TblGetLastUsableRow (table);
			apptIndex = TblGetRowID (table, row);

			// If there is only one appointment visible, this is the case
			// when a appointment occupies the whole screeen, and its not
			// the last appointment, move to the next appointment.
			if (row == 0)
				apptIndex++;
			}
		}


	// Scroll the table up.
	else
		{
		if (TopVisibleAppt == 0) return false;

		// Scan the records starting with the first visible record to
		// determine how many record we need to scroll.  Since the
		// heights of the records vary,  we sum the heights of the
		// records until we get a screen full.
		height = TblGetRowHeight (table, 0);
		if (height >= tableHeight)
			height = 0;

		while ( (height < tableHeight) && (apptIndex > 0) )
			{
			height += DayViewGetDescriptionHeight (apptIndex-1, columnWidth, tableHeight, &iconsWidth, &fontID);
			if ((height <= tableHeight) || (apptIndex == TopVisibleAppt))
				apptIndex--;
			}
		}

	TblMarkTableInvalid (table);

	TopVisibleAppt = apptIndex;
	DayViewLoadTable ();

	TblUnhighlightSelection (table);
	TblRedrawTable (table);
	DayViewDrawTimeBars ();
	return true;
}


/***********************************************************************
 *
 * FUNCTION:    DayViewPurge
 *
 * DESCRIPTION: This routine deletes appointment that are before the 
 *              user specified date.
 *
 * PARAMETERS:  nothing
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	8/1/95		Initial Revision
 *
 ***********************************************************************/
SEC001 static void DayViewPurge (void)
{
	TablePtr table;
	Boolean redraw;

	redraw = PurgeRecords ();

	// If the current date is before the purge date redraw the day, we may
	// have deleted some of the appointments being displayed.
	if (redraw)
		{
		DayViewLayoutDay (true);		
		DayViewLoadTable ();	
	
		table = GetObjectPtr (DayTable);
		TblRedrawTable (table);
		DayViewDrawTimeBars ();
		}
}


/***********************************************************************
 *
 * FUNCTION:    DayViewDoCommand
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
 *			art	3/29/95	Initial Revision
 *			jmp	9/17/99	Use NewNoteView instead of NoteView.
*			jmp	11/04/99	To prevent other sublaunch issues, remind ourselves
 *								that we've sublaunched already into PhoneNumberLookup().
 *
 ***********************************************************************/
SEC001 static Boolean DayViewDoCommand (UInt16 command)
{
	Int16			row;
	Int16			column;
	UInt16 		pasteLen;
	MemHandle 	pasteCharsH;
	FontID		fontID;
	FieldPtr 	fld;
	TablePtr 	table;
	Boolean 		handled = true;
	
	// New
	if (command == NewItemCmd)
		{
		DayViewClearEditState ();
		DayViewNewAppointment (NULL);
		if (ItemSelected)
			{
			table = GetObjectPtr (DayTable);
			TblGetSelection (table, &row, &column);
			TblReleaseFocus (table);
			DayViewSelectTime (table, row);
			}
		}


	// Delete
	else if (command == DeleteCmd)
		{
		if (ItemSelected)
			DayViewDeleteAppointment ();
		else
			FrmAlert (SelectItemAlert);
		}


	// Db35: Duplicate Event
	else if (command == DuplicateCmd)
		{
		if (ItemSelected)
			DayViewDuplicateEvent ();
		else
			FrmAlert (SelectItemAlert);
		}


	// Create note
	else if (command == CreateNoteCmd)
		{
		if (ItemSelected)
			{
			TblReleaseFocus (GetObjectPtr (DayTable));
			if (CreateNote (true))
				{
				MenuEraseStatus (NULL);
				FrmGotoForm (NewNoteView);
				}
			else
				DayViewRestoreEditState ();
			}
		else
			FrmAlert (SelectItemAlert);
		}


	// Delete nore
	else if (command == DeleteNoteCmd)
		{
		if (ItemSelected)
			DayViewDeleteNote ();
		else
			FrmAlert (SelectItemAlert);
		}
		

	// Purge
	else if (command == PurgeCmd)
		{
		DayViewClearEditState ();
		DayViewPurge ();
		}
					

	// Send Record
	else if (command == SendRecordCmd)
		{
		if (ItemSelected)
			{
			TblReleaseFocus (GetObjectPtr (DayTable));
			DateSendRecord(ApptDB, CurrentRecord);
			DayViewRestoreEditState ();
			}
		else
			FrmAlert (SelectItemAlert);
		}
					

	// Font Selector
	else if (command == DayFontsCmd)
		{
		TblReleaseFocus (GetObjectPtr (DayTable));
		fontID = SelectFont (ApptDescFont);
		if (fontID != ApptDescFont)
			{
			ApptDescFont = fontID;
			DayViewClearEditState ();
			}
		else
			DayViewRestoreEditState ();
		}

	// Preferences
	else if (command == DayPreferencesCmd)
		{
		DayViewClearEditState ();
		FrmPopupForm (PreferencesDialog);
		}

	// Display Options
	else if (command == DayDisplayOptionsCmd)
		{
		DayViewClearEditState ();
		FrmPopupForm (DisplayDialog);
		}

	// Phone Lookup
	else if (command == DayPhoneLookup)
		{
		fld = GetFocusObjectPtr ();
		if (fld)
			{
			InPhoneLookup = true;
			A35LookupPhone (fld);
			InPhoneLookup = false;
			}
		else
			FrmAlert (SelectItemAlert);
		}

	// Db35: CalDAV Sync - plain switch-to-app bounce (DateDAVGoToSync, main
	// segment - see its own comment; this file/sec001 has no headroom left).
	else if (command == DayCalDavSyncCmd)
		DateDAVGoToSync ();


	// Security
	else if (command == DaySecurityCmd)
		{
		DayViewClearEditState();
		DoSecurity();
		DayViewUpdateDisplay(updateDisplayOptsChanged);
		}

	// About
	else if (command == DayGetInfoCmd)
		{
		DayViewClearEditState ();
		ShowAboutDb35 ();
		}


	// Paste
	else if (command == sysEditMenuPasteCmd)
		{
		fld = GetFocusObjectPtr ();
		if (! fld)
			{
			pasteCharsH = ClipboardGetItem (clipboardText, &pasteLen);
			if (pasteCharsH && pasteLen)
				{
				DayViewNewAppointment (NULL);
				GrfSetState (false, false, false);
				fld = GetFocusObjectPtr ();
				}
			}
		handled = false;
		}

	// Some the the edit menu commands are handled by FrmHandleEvent.
	else
		{
		handled = false;
		}
	
	return (handled);
}


/***********************************************************************
 *
 * FUNCTION:    DayViewUpdateDisplay
 *
 * DESCRIPTION: This routine updates the display of the day View.
 *
 * PARAMETERS:  updateCode - a code that indicated what changes have been
 *                           made to the Day View.
 *                		
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	6/12/95	Initial Revision
*			jmp	11/01/99	Fixed problem on frmRedrawUpdateCode events when
 *								we're still in the edit state but we weren't redrawing
 *								the edit indicator.  Fixes Datebook part of bug #23235.
 *
 ***********************************************************************/
SEC001 static void DayViewUpdateDisplay (UInt16 updateCode)
{
	Int16 apptIndex;
	Int16 row, column;
	DateType next;
	TablePtr table;
	Boolean repeatsAgain;
	MemHandle recordH;
	ApptDBRecordType apptRec;
	
	table = GetObjectPtr (DayTable);

	// Was the UI unable to save an image of the day view when is 
	// obscured part of the day view with another dialog?  If not,
	// we'll handle it here.
	if (updateCode & frmRedrawUpdateCode)
		{
		FrmDrawForm (FrmGetActiveForm ());
		
		// If we're editing, then find out which row is being edited,
		// mark it invalid, and redraw the table.
		if (TblEditing(table))
			{
			TblGetSelection (table, &row, &column);
			TblMarkRowInvalid(table, row);
			TblRedrawTable (table);
			}
			
		DayViewDrawTimeBars ();
		return;
		}
		
	// Was the display options modified (Preferences dialog) or was the 
	// font changed.
	else if (updateCode & updateDisplayOptsChanged)
		{
		DayViewLayoutDay (true);
		DayViewSetTopAppointment ();
		}

	// Was an item deleted or marked sercet.
	else if ( (updateCode & updateItemDelete) ||
				 (updateCode & updateItemHide))
		{
		ClearEditState ();
		DayViewLayoutDay (true);
		}


	// Was the repeat infomation changed?
	else if (updateCode & updateRepeatChanged)
		{
		ApptGetRecord (ApptDB, CurrentRecord, &apptRec, &recordH);
		if (apptRec.repeat)
			{
			next = Date;
			repeatsAgain = ApptNextRepeat (&apptRec, &next);
			if (! repeatsAgain)
				{
				next = apptRec.when->date;
				repeatsAgain = ApptNextRepeat (&apptRec, &next);
				}
			MemHandleUnlock (recordH);

			if (repeatsAgain && (DateToInt (next) != DateToInt (Date)))
				{
				Date = next;
				DayViewDrawDate (Date);
				return;
				}
			else if (! repeatsAgain)
				ClearEditState ();
			}
		else
			{
			next = apptRec.when->date;
			MemHandleUnlock (recordH);
			// If a repeating event was changed to a non-repeating event
			// and the start date was also change then go to the new 
			// start date.
			if (DateToInt (next) != DateToInt (Date))
				{
				Date = next;
				DayViewDrawDate (Date);
				return;
				}
			}
			
		// Redraw the current record if it's still visible so the
		// the repeat indictor is draw or erased
		TblGetSelection (table, &row, &column);
		TblSetRowUsable (table, row, false);

		DayViewLayoutDay (true);
		}


	// Was the date of an appointment changed?
	else if (updateCode & updateDateChanged)
		{
		ApptGetRecord (ApptDB, CurrentRecord, &apptRec, &recordH);
		Date = apptRec.when->date;
		MemHandleUnlock (recordH);
			
		DayViewDrawDate (Date);
		return;
		}


	// Was the time of an appointment or the font changed?
	else if (updateCode & (updateTimeChanged | updateException | updateFontChanged))
		{
		DayViewLayoutDay (true);
		}

	// Db35: alarms removed -- no updateAlarmChanged handling.

	DayViewLoadTable ();
	TblRedrawTable (table);
	DayViewDrawTimeBars ();
}


/***********************************************************************
 *
 * FUNCTION:    DayViewInit
 *
 * DESCRIPTION: This routine initializes the "Day View" of the 
 *              Datebook application.
 *
 * PARAMETERS:  frm - pointer to the day view form.
 *
 * RETURNED:    nothing
 *
 *	HISTORY:
 *		06/12/95	art	Created by Art Lamb.
 *		08/04/99	kwk	Copy day-of-week pushbutton ptrs vs. just first byte
 *							of each label.
 *
 ***********************************************************************/
SEC001 static void DayViewInit (FormPtr frm)
{
	UInt16 month;
	UInt16 day;
	UInt16 year;
	UInt16 dayOfWeek;
	UInt16 id;
	UInt16 row;
	UInt16 rowsInTable;
	TablePtr table;

	// Get the day we're displaying.
	day = Date.day;
	month = Date.month;
	year = Date.year+firstYear;

	DayViewSetTitle(frm);

	// If the start-day-of-week is monday rearrange the labels on the 
	// days-of-week push buttons.
	if (StartDayOfWeek == monday)
		{
		const Char* sundayLabel = CtlGetLabel (GetObjectPtr (DayDOW1Button));
		for (id = DayDOW1Button; id < DayDOW7Button; id++)
			{
			CtlSetLabel (GetObjectPtr (id), CtlGetLabel (GetObjectPtr (id + 1)));
			}
		CtlSetLabel (GetObjectPtr (DayDOW7Button), sundayLabel);
		}

	// Highlight the correct day-of-week push button.
	dayOfWeek = DatebookDayOfWeek (month, day, year);
	FrmSetControlGroupSelection (frm, DayOfWeekGroup, DayDOW1Button + dayOfWeek);

	// Highlight the Day View push button.
	FrmSetControlGroupSelection (frm, DayViewGroup, DayDayViewButton);

	// Initialize the table used to display the day's appointments.
	table = FrmGetObjectPtr (frm, FrmGetObjectIndex (frm, DayTable));

	rowsInTable = TblGetNumberOfRows (table);
	for (row = 0; row < rowsInTable; row++)
		{		
		TblSetItemStyle (table, row, timeBarColumn, customTableItem);
		TblSetItemStyle (table, row, timeColumn, customTableItem);
		TblSetItemStyle (table, row, descColumn, textTableItem);
		TblSetRowUsable (table, row, false);
		}

	TblSetColumnUsable (table, timeBarColumn, true);
	TblSetColumnUsable (table, timeColumn, true);
	TblSetColumnUsable (table, descColumn, true);
	
	TblSetColumnMasked (table, descColumn, true);

	TblSetColumnSpacing (table, timeBarColumn, spaceAfterTimeBarColumn);
	TblSetColumnSpacing (table, timeColumn, spaceAfterTimeColumn);
	
	TblSetColumnEditIndicator (table, timeBarColumn, false);

	// Set the callback routines that will load and save the 
	// description field.
	TblSetLoadDataProcedure (table, descColumn, DayViewGetDescription);
	TblSetSaveDataProcedure (table, descColumn, DayViewSaveDescription);
	
	// Set the callback routine that draws the time field.
	TblSetCustomDrawProcedure (table, timeColumn, DayViewDrawTime);

	// Set the callback routine that draws the note, alarm, and repeat icons.
	TblSetCustomDrawProcedure (table, descColumn, DayViewDrawIcons);

	// By default the list view assume no time bar are displayed.
	TimeBarColumns = 0;

	// Get all the appointments and empty time slots on the current day.
	DayViewLayoutDay (true);


	// If we do not have an appointment selected, then position the table
	// so the the first appointment of the day is visible, unless we're on
	// today, then make sure the next appointment is visible.
	if (! ItemSelected)
		DayViewSetTopAppointment ();

	// Load the appointments and empty time slots into the table object 
	// that will display them.
	if (PendingUpdate && ItemSelected)
		DayViewUpdateDisplay (PendingUpdate);
	else
		DayViewLoadTable ();


	// Initial miscellaneous global variables.
	TimeDisplayed = false;
	PendingUpdate = 0;
}


/***********************************************************************
 *
 * FUNCTION:    DayViewHandleEvent
 *
 * DESCRIPTION: This routine is the event handler for the Day View
 *              of the Datebook application.
 *
 * PARAMETERS:  event  - a pointer to an EventType structure
 *
 * RETURNED:    true if the event was handled and should not be passed
 *              to a higher level handler.
 *
 *	HISTORY:
 *		06/12/95	art	Created by Art Lamb.
 *		11/22/98	kwk	Handle command keys in separate code block so that
 *							TxtCharIsPrint doesn't get called w/virtual chars.
 *		06/22/99	CS		Standardized keyDownEvent handling
 *							(TxtCharIsHardKey, commandKeyMask, etc.)
 *		08/04/99	kwk	Tweak auto-entry generation w/keydown event so that
 *							it works when a FEP is active.
 *		09/23/99 jmp	On frmCloseEvent, don't call FrmSetFocus() with
 *							a NULL frm to prevent debug build from going ErrFatalDisplay
 *							crazy.
 *		11/04/99	jmp	Restored DayView edit state after beaming; fixes bug #23315.
 *
 ***********************************************************************/
SEC001 Boolean DayViewHandleEvent (EventType* event)
{
	Int16 row, column;
	UInt32 uniqueID;
	DateType date;
	FormPtr frm;
	TablePtr table;
	Boolean handled = false;


	if (event->eType == keyDownEvent)
		{
		// Datebook key pressed?
		if (TxtCharIsHardKey(event->data.keyDown.modifiers, event->data.keyDown.chr))
			{
			DayViewClearEditState ();
			DateSecondsToDate (TimGetSeconds (), &date);
			if (DateToInt (date) != DateToInt (Date) ||
				 ((event->data.keyDown.modifiers & poweredOnKeyMask)))
				DayViewDrawDate (date);
			else
				FrmGotoForm (WeekView);
			handled = true;
			}
		else if (EvtKeydownIsVirtual(event))
			{
	
			// Scroll up key pressed?
			if (event->data.keyDown.chr == vchrPageUp)
				{
				UInt16 prevTop;
				DayViewClearEditState ();
				// Db35: page up through the day's earlier items first (same
				// action as the on-screen DayUpButton tap), only falling
				// through to the previous day once nothing actually moved.
				// Belt-and-suspenders on top of DayViewScroll's own return
				// value: also verify TopVisibleAppt genuinely changed --
				// on a real device DOWN was seen to report "scrolled" via
				// its return value while visibly doing nothing (silently
				// swallowing the keypress instead of falling through to the
				// day change), so don't trust the return value alone.
				prevTop = TopVisibleAppt;
				if (! DayViewScroll (winUp, false) || TopVisibleAppt == prevTop)
					{
					DateAdjust (&Date, -1);
					DayViewDrawDate (Date);
					}
				handled = true;
				}

			// Scroll down key pressed?
			else if (event->data.keyDown.chr == vchrPageDown)
				{
				UInt16 prevTop;
				DayViewClearEditState ();
				// Db35: same as vchrPageUp above, mirrored.
				prevTop = TopVisibleAppt;
				if (! DayViewScroll (winDown, false) || TopVisibleAppt == prevTop)
					{
					DateAdjust (&Date, 1);
					DayViewDrawDate (Date);
					}
				handled = true;
				}
	
			// Send data key pressed?
			else if (event->data.keyDown.chr == vchrSendData)
				{
				if (ItemSelected)
					{
					TblReleaseFocus (GetObjectPtr (DayTable));
					DateSendRecord(ApptDB, CurrentRecord);
					DayViewRestoreEditState ();
					}
				else
					FrmAlert (SelectItemAlert);
				handled = true;
				}
			
			else
				{
				handled = false;
				}
			}

		else if (TxtCharIsPrint (event->data.keyDown.chr))
			{
			if (!ItemSelected)
				{
				// If no item is selected and the character is display, 
				// create a new ToDo item.
				if (TxtCharIsDigit (event->data.keyDown.chr))
					{
					handled = DayViewNewAppointment (event);
					}
				else
					{
					// Create a new empty appointment, upper-shift the character,
					// and requeue it. This way it works even if an input method
					// is active.
					DayViewNewAppointment (NULL);
					if (ItemSelected)
						{
						// Db35: plain ASCII uppercase, not TxtGlueUpperChar -- that
						// glue call drags in the ROM's whole international text-glue
						// chain (TextMgr.o/IntlGlue.o/TxtGlue.o, ~7KB) into main just
						// for this one keystroke fold. This suite is English-only by
						// convention elsewhere (see LocLower/DlgLower's identical
						// ASCII-only reasoning), and Graffiti/hardware keydown chars
						// are plain ASCII in practice, so the glue chain buys nothing
						// here -- freed room was needed for the Location feature's
						// full Country list. See [[app-reference-docs]] Datebook35.md.
						if (event->data.keyDown.chr >= 'a' && event->data.keyDown.chr <= 'z')
							event->data.keyDown.chr = (WChar) (event->data.keyDown.chr - 32);
						EvtAddEventToQueue (event);
						handled = true;
						}
					}
				}
/*			else
				{
				// If we get a printable character, then we can assume that any Graffiti
				// auto-shifting is finished, so it's safe to turn it off. This solves the
				// problem of a re-queued keydown event (e.g. when in list view & writing
				// a character -> generate a new entry) not turning off the temp shift state
				// because it wasn't created by Graffiti.

				Boolean capsLock, numLock, autoShifted;
				UInt16 tempShift;
				
				if (GrfGetState (&capsLock, &numLock, &tempShift, &autoShifted) == 0
					 && autoShifted)
					{
					GrfSetState (capsLock, numLock, false);
					}
				}*/
			}
		}


	// If the pen is not in any of the object of the view, take the 
	// view out of edit mode.
	else if (event->eType == penDownEvent)
		{
		frm = FrmGetActiveForm ();
		handled = FrmHandleEvent (frm, event);
		if (! handled)
			{
			DayViewClearEditState ();
			}
		}

	// Check for buttons that take us out of edit mode.
	else if (event->eType == ctlEnterEvent)
		{
		switch (event->data.ctlEnter.controlID)
			{
			case DayNextWeekButton:
			case DayDOW1Button:
			case DayDOW2Button:
			case DayDOW3Button:
			case DayDOW4Button:
			case DayDOW5Button:
			case DayDOW6Button:
			case DayDOW7Button:
			case DayPrevWeekButton:
			case DayDayViewButton:
			case DayWeekViewButton:
			case DayMonthViewButton:
			case DayAgendaViewButton:
			case DayNewButton:
			case DayGoToButton:
			case DayUpButton:
			case DayDownButton:
				DayViewClearEditState ();
				break;
			}
		}


	// Handle the button that was selected.
	else if (event->eType == ctlSelectEvent)
		{
		switch (event->data.ctlSelect.controlID)
			{
			case DayWeekViewButton:
				FrmGotoForm (WeekView);
				handled = true;
				break;

			case DayMonthViewButton:
				FrmGotoForm (MonthView);
				handled = true;
				break;
			
			case DayAgendaViewButton:
				FrmGotoForm (AgendaView);
				handled = true;
				break;

			case DayDetailsButton:
				if (ItemSelected)
					FrmPopupForm (DetailsDialog);
				else
					FrmAlert (SelectItemAlert);
				handled = true;
				break;

			case DayNewButton:
				DayViewNewAppointment (NULL);
				if (ItemSelected)
					{
					table = GetObjectPtr (DayTable);
					TblGetSelection (table, &row, &column);
					TblReleaseFocus (table);
					DayViewSelectTime (table, row);
					}
				break;

			case DayGoToButton:
				DayViewGoToDate ();
				handled = true;
				break;

			case DayDOW1Button:
			case DayDOW2Button:
			case DayDOW3Button:
			case DayDOW4Button:
			case DayDOW5Button:
			case DayDOW6Button:
			case DayDOW7Button:
				DayViewDayOfWeekSelected (event);
				handled = true;
				break;
			}
		}


	// If the pen when down in the details button but did not go up in it,
	// restore the edit state.
	else if (event->eType == ctlExitEvent)
		{
		if (event->data.ctlEnter.controlID == DayDetailsButton)
			DayViewRestoreEditState ();
		}


	// Handle the scrolling controls.
	else if (event->eType == ctlRepeatEvent)
		{
		switch (event->data.ctlRepeat.controlID)
			{
			// Db35: was -daysInWeek/daysInWeek (jump a week, same weekday) --
			// per user request, these now step one day at a time, rolling
			// into the next/previous week the same way DateAdjust already
			// rolls a day-of-week trigger into the next/previous month.
			case DayPrevWeekButton:
				DateAdjust (&Date, -1);
				DayViewDrawDate (Date);
				break;

			case DayNextWeekButton:
				DateAdjust (&Date, 1);
				DayViewDrawDate (Date);
				break;

			case DayUpButton:
				DayViewScroll (winUp, false);
				break;
				
			case DayDownButton:
				DayViewScroll (winDown, false);
				break;
			}
		}


	// Check if we've changed row in the day view table, if so 
	// clear the edit state.
	else if (event->eType == tblEnterEvent)
		{
		table = (TablePtr) event->data.tblEnter.pTable;
		// Db35: if the suite is locked and there are encrypted appointments, a tap must NOT edit
		// a masked record -- hand off to Sec35 to unlock first (it switches back).  Bounce before
		// the ROM enters edit mode on the "******" cell.
		if (DayLockedBounce ())
			{
			handled = true;
			}
		else
			{
			if (ItemSelected)
				{
				DmRecordInfo (ApptDB, CurrentRecord, NULL, &uniqueID, NULL);
				if (TblFindRowData (table, uniqueID, &row))
					{
					if (event->data.tblEnter.row != row)
						handled = DayViewClearEditState ();
					}
				}
			if (event->data.tblEnter.column == timeBarColumn)
				handled = true;
			else if (! handled)
				{
				handled = DayViewSelectIcons (event);
				// Db35: a plain text tap on the description column.
				if (! handled && event->data.tblEnter.column == descColumn)
					{
					UInt16			ai = TblGetRowID (table, event->data.tblEnter.row);
					ApptInfoPtr		ap = MemHandleLock (ApptsH);
					UInt16			rn = ap[ai].recordNum;
					MemPtrUnlock (ap);
					// Db35: ToDo35/birthday sentinel rows are NOT ApptDB record
					// indices (rn = kDayTodoBase/kDayBdayBase + slot) -- must be
					// routed the same way DayViewItemSelected's tblSelectEvent
					// handler already does (DayLaunchTodo / AddrBdayGoTo), NOT
					// treated as an "existing event" below. Missing this check
					// let a tap fall through to CurrentRecord = rn (the sentinel
					// value) followed by FrmPopupForm(DetailsDialog), which
					// crashed ("Error Querying record", ApptGetRecord handed a
					// wildly out-of-range ApptDB index) -- confirmed real for a
					// TIMED ToDo row (DayViewSelectIcons above always returns
					// false for these rows, iconsWidth is unconditionally 0 for
					// customTableItem sentinels, so nothing catches it before
					// this point).
					if (IsDayTodo (rn))
						{
						TblUnhighlightSelection (table);
						DayLaunchTodo ((UInt16)(rn - kDayTodoBase));
						handled = true;
						}
					else if (IsDayBday (rn))
						{
						TblUnhighlightSelection (table);
						AddrBdayGoTo (gDayBdayRec[rn - kDayBdayBase]);
						handled = true;
						}
					else if (IsDayDav (rn))
						{
						// Db35: same shape as the real "EXISTING event" branch
						// right below (open Details instead of inline edit) -
						// see DayOpenDavDetails's own comment.
						TblUnhighlightSelection (table);
						DayOpenDavDetails (rn, false);
						handled = true;
						}
					else if (rn != emptySlot)
						{
						// Db35: an EXISTING event -- open Details instead of inline
						// edit. Sidesteps the whole location-suffix focus/blur
						// problem for this row (it's never put into edit mode at
						// all -- see DatebookRsc.h DetailsTitleField / the "Title
						// field" writeup in Datebook35.md). Empty-slot taps
						// (below) are unchanged -- still create + inline-edit.
						TblUnhighlightSelection (table);
						CurrentRecord = rn;
						gDetailsApptIndex = ai;	/* Db35: which row -- see kDetailsApptUnknown */
						ItemSelected = true;
						gDaySkipRestoreEditState = true;	/* this row was never inline-editing */
						FrmPopupForm (DetailsDialog);
						handled = true;
						}
					else
						{
						// Db35: the ROM is about to grab focus and edit a blank
						// row about to become a new event.  Demote an icon row to
						// a plain text item first so the edit renders as a clean
						// cursor, not a full-cell invert.  (Icon returns on
						// unfocus.)
						DayEnterEditStyle (table, event->data.tblEnter.row);
						TblMarkRowInvalid (table, event->data.tblEnter.row);
						TblRedrawTable (table);
						}
					}
				}
			}
		}
		

	// An item in the table has been selected.
	else if (event->eType == tblSelectEvent)
		{
		DayViewItemSelected (event);
		handled = true;
		}
		

	// Expand or compress the height of the appointments description.
	else if (event->eType == fldHeightChangedEvent)
		{
		DayViewResizeDescription (event);
		handled = true;
		}


	// Add the buttons that we want available on the command bar, based on the current context
	else if (event->eType == menuCmdBarOpenEvent)
		{

		if (ItemSelected) 
			{
			FieldPtr fld;
			UInt16 startPos, endPos;
			
			fld = TblGetCurrentField(GetObjectPtr(DayTable));
			FldGetSelection(fld, &startPos, &endPos);
			
			if (startPos != endPos)  // if there's some highlighted text
				{
				// (Note that we're adding buttons from right to left)
				MenuCmdBarAddButton(menuCmdBarOnLeft, BarPasteBitmap, menuCmdBarResultMenuItem, sysEditMenuPasteCmd, 0);
				MenuCmdBarAddButton(menuCmdBarOnLeft, BarCopyBitmap, menuCmdBarResultMenuItem, sysEditMenuCopyCmd, 0);
				MenuCmdBarAddButton(menuCmdBarOnLeft, BarCutBitmap, menuCmdBarResultMenuItem, sysEditMenuCutCmd, 0);
				MenuCmdBarAddButton(menuCmdBarOnLeft, BarUndoBitmap, menuCmdBarResultMenuItem, sysEditMenuUndoCmd, 0);
				}
			else 	// there's no highlighted text, but an item is chosen
				{
				MenuCmdBarAddButton(menuCmdBarOnLeft, BarDeleteBitmap, menuCmdBarResultMenuItem, DeleteCmd, 0);
				MenuCmdBarAddButton(menuCmdBarOnLeft, BarPasteBitmap, menuCmdBarResultMenuItem, sysEditMenuPasteCmd, 0);
				MenuCmdBarAddButton(menuCmdBarOnLeft, BarSecureBitmap, menuCmdBarResultMenuItem, DaySecurityCmd, 0);
				MenuCmdBarAddButton(menuCmdBarOnLeft, BarBeamBitmap, menuCmdBarResultMenuItem, SendRecordCmd, 0);
				}
			}
		else	// no item is chosen
			{
			MenuCmdBarAddButton(menuCmdBarOnLeft, BarSecureBitmap, menuCmdBarResultMenuItem, DaySecurityCmd, 0);
			}
		
		// tell the field package to not add buttons automatically; we've done it all ourselves.
		event->data.menuCmdBarOpen.preventFieldButtons = true;
		
		// don't set handled to true; this event must fall through to the system.
		}


	else if (event->eType == menuEvent)
		{
		handled = DayViewDoCommand (event->data.menu.itemID);
		}
		

	else if (event->eType == frmOpenEvent)
		{
		frm = FrmGetActiveForm ();
		DayViewInit (frm);
		FrmDrawForm (frm);
		DayViewDrawTimeBars ();
		DayViewRestoreEditState ();
		handled = true;
		}


	else if (event->eType == frmGotoEvent)
		{
		frm = FrmGetActiveForm ();
		DayViewGotoAppointment (event);
		DayViewInit (frm);
		FrmDrawForm (frm);
		DayViewDrawTimeBars ();
		DayViewRestoreEditState ();
		handled = true;
		}


	else if (event->eType == frmUpdateEvent)
		{
		DayViewUpdateDisplay (event->data.frmUpdate.updateCode);
		if (event->data.frmUpdate.updateCode != frmRedrawUpdateCode)
			DayViewRestoreEditState ();
		handled = true;
		}


	else if (event->eType == frmSaveEvent)
		{
		DayViewClearEditState ();
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
				DayViewHideTime (true);
			else
				{
				DayViewShowTime ();
				}
			}
		}


	else if (event->eType == nilEvent)
		{
		DayViewHideTime (false);
		}

	else if (event->eType == frmCloseEvent)
		{
		// If necessary, release the focus before freeing ApptsH.  Releasing the focus causes some
		// data to be saved, and that action depends on ApptsH.  If the data isn't
		// saved now, trying to save it later will access a NULL ApptsH.
		frm = FrmGetActiveForm ();
		if (frm)
			FrmSetFocus(frm, noFocus);

		// Db35: free the per-row decrypted/masked display copies (and any pending edit copy).
		// FrmSetFocus(noFocus) above already flushed an active inline edit through the save-proc.
		DayViewFreeRowCopies ();

		MemHandleFree (ApptsH);
		ApptsH = 0;
		
		// Also free ApptsOnlyH if necessary (was allocated in last call to DayViewLayoutDay)
		if (ApptsOnlyH)
			{
			MemHandleFree(ApptsOnlyH);
			ApptsOnlyH = 0;
			}
		}

	return (handled);
}

