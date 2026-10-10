/*
 * todoext.h - Td35 per-ToDo "extras": start/end time + repeat recurrence.
 *
 * These attributes live in a SEPARATE database ("ToDo35ExtDB", creator 'Td35',
 * type 'Tdx3'), one record per ToDo that has a time and/or a repeat, keyed by
 * the ToDo record's Data-Manager unique ID (stable across sort + HotSync).
 *
 * Rationale: the ROM ToDoDBRecord ({dueDate, priority, description\0, note\0})
 * is left EXACTLY as-is, so in-place note editing, long notes, sort order and
 * HotSync are all unaffected -- nothing is appended to the record.  ToDo35 owns
 * the extras DB; Datebook35 reads it (read-only) to slot timed todos.
 *
 * Both apps compile this file; multi-segment apps (Datebook35) build with
 * -DCATSYNC_MULTISEG so it lands in "sec001" (only ROM traps are reached here).
 */
#ifndef TODOEXT_H
#define TODOEXT_H

#include <PalmOS.h>

/* Segment placement: the includer may force a segment via -DTODOEXT_SEG_NAME=secNNN
 * (ToDo35 uses sec002); Datebook35 builds multi-seg (CATSYNC_MULTISEG) -> sec001;
 * otherwise the code lands in main. */
#if defined(TODOEXT_SEG_NAME)
#define TDX_STR2(x)	#x
#define TDX_STR(x)	TDX_STR2(x)
#define TDX_SEC	__attribute__ ((section (TDX_STR(TODOEXT_SEG_NAME))))
#elif defined(CATSYNC_MULTISEG)
#define TDX_SEC	__attribute__ ((section ("sec001")))
#else
#define TDX_SEC
#endif

/* Repeat frequency -- numeric values match Datebook35's repeatTypes enum. */
#define tdxRepeatNone			0
#define tdxRepeatDaily			1
#define tdxRepeatWeekly			2
#define tdxRepeatMonthlyByDay	3
#define tdxRepeatMonthlyByDate	4
#define tdxRepeatYearly			5

/* ext.flags bits */
#define tdxTimed				0x01	/* startTime/endTime valid            */
#define tdxHasRepeat			0x02	/* repeat valid                       */

#define tdxNoTime				0xff	/* TimeType.hours sentinel = untimed  */

typedef struct {
	UInt8			repeatType;			/* tdxRepeat*                         */
	UInt8			frequency;			/* every N occurrences (>= 1)         */
	DateType		endDate;			/* 0xFF.. = forever (see TdxForever)  */
	UInt8			onMask;				/* weekly: day bitfield (bit0 = Sun);
										 * monthlyByDay: DayOfMonthType        */
	UInt8			startOfWeek;		/* weekly only                        */
	UInt8			reserved1;
	UInt8			reserved2;
} TdxRepeat;

typedef struct {
	UInt8			flags;				/* tdxTimed | tdxHasRepeat            */
	UInt8			reserved;
	TimeType		startTime;			/* valid iff (flags & tdxTimed)       */
	TimeType		endTime;
	TdxRepeat		repeat;				/* valid iff (flags & tdxHasRepeat)   */
} TdxExt;

/* "forever" end date: all-ones DateType.  Use these to set/test it. */
#define TdxSetForever(dp)	MemSet ((dp), sizeof (DateType), 0xFF)
#define TdxIsForever(d)		(*(const UInt8*)&(d) == 0xFF)

/* API.  An app opens the extras DB once (StartApplication) and reuses the ref;
 * pass dmModeReadWrite for the owning app, dmModeReadOnly for a reader. */
Err     TdxGetDatabase (DmOpenRef* dbPP, UInt16 mode) TDX_SEC;
Boolean TdxGet (DmOpenRef db, UInt32 uid, TdxExt* out) TDX_SEC;	/* false + zeroed if none */
Err     TdxSet (DmOpenRef db, UInt32 uid, const TdxExt* ext) TDX_SEC;	/* flags==0 -> delete */
Err     TdxDelete (DmOpenRef db, UInt32 uid) TDX_SEC;

#endif /* TODOEXT_H */
