/*
 * locdb.h - Db35 event "Location" storage: two small databases, both owned
 * by Datebook35 (creator 'Db35'), structurally cloning todoext.c's
 * extras-DB pattern (see todoext.h).
 *
 * DB 1 "AppointmentLocationDB" (type 'Dlk1'): one record per appointment
 * that HAS a location, keyed by the appointment's Data-Manager unique ID
 * (stable across sort + HotSync) -- exactly like ToDo35ExtDB is keyed by a
 * ToDo's uniqueID. The appointment's own packed record format
 * (ApptDBRecordType/ApptPack/ApptUnpack in DateDB.c) is left untouched.
 *
 * DB 2 "LocationsDB" (type 'Dlc1'): a flat, NOT-foreign-keyed list of
 * previously-used locations ("Previous Loc" picker history). A link
 * record's locDbRecUID names which LocationsDB record (if any) it came
 * from -- this explicit link is what lets "edit a picked-from-history
 * location" update that history entry in place, without any fuzzy
 * string-match dedup.
 *
 * A location picked from Address35 is NEVER written to LocationsDB and
 * NEVER written back to Address35 -- the link record carries a full
 * standalone snapshot (fromAddress35=1, locDbRecUID=0).
 */
#ifndef LOCDB_H
#define LOCDB_H

#include <PalmOS.h>

/* Segment placement: the includer may force a segment via
 * -DLOCDB_SEG_NAME=secNNN (Datebook35 uses sec002, mirroring todoext.c's
 * own -DTODOEXT_SEG_NAME=sec002 override); otherwise the code lands in main. */
#if defined(LOCDB_SEG_NAME)
#define LOC_STR2(x)	#x
#define LOC_STR(x)	LOC_STR2(x)
#define LOC_SEC	__attribute__ ((section (LOC_STR(LOCDB_SEG_NAME))))
#else
#define LOC_SEC
#endif

#define kLocNameLen		40
#define kLocLine1Len	60
#define kLocLine2Len	60
#define kLocAptLen		20
#define kLocCityLen		40
#define kLocStateLen	20
#define kLocZipLen		16
#define kLocCountryLen	20

/* Summary buffer size (Details preview + Day View suffix). Per user request
 * LocSummary now concatenates EVERY filled field, not just Name/City/Zip --
 * sized for the theoretical max (all 8 fields at their own max length + 7
 * ", " separators is ~282 bytes; 256 is a comfortable, deliberately-round
 * budget -- Day View's own row-height cap (DayViewGetDescriptionHeight)
 * clips visually well before this length would ever matter in practice). */
#define kLocSummaryLen	256

typedef struct {
	char	name[kLocNameLen];			/* "Vista" */
	char	line1[kLocLine1Len];
	char	line2[kLocLine2Len];
	char	apt[kLocAptLen];
	char	city[kLocCityLen];
	char	state[kLocStateLen];
	char	zip[kLocZipLen];
	char	country[kLocCountryLen];
} DlxLocation;

/* Per-appointment link record. A location is considered "unset" iff every
 * DlxLocation field is empty -- LinkSet treats that as a delete request. */
typedef struct {
	UInt32			apptUid;		/* foreign key: ApptDB record's uniqueID */
	UInt8			fromAddress35;	/* 1 = sourced from an Address35 contact;
									 * never written to LocationsDB, never
									 * written back to Address35            */
	UInt8			reserved;
	UInt32			locDbRecUID;	/* 0 if none; else the LocationsDB record
									 * this was picked from / saved to      */
	DlxLocation		loc;			/* full snapshot, always inline         */
} DlxLink;

/* DB 1: "AppointmentLocationDB", type 'Dlk1' creator 'Db35'. An app opens
 * it once and reuses the ref; pass dmModeReadWrite for the owner (Datebook35
 * itself), dmModeReadOnly for a reader. */
Err     LinkGetDatabase (DmOpenRef* dbPP, UInt16 mode) LOC_SEC;
Boolean LinkGet (DmOpenRef db, UInt32 apptUid, DlxLink* out) LOC_SEC;		/* false + zeroed if none */
Err     LinkSet (DmOpenRef db, UInt32 apptUid, const DlxLink* link) LOC_SEC;	/* loc all-blank -> delete */
Err     LinkDelete (DmOpenRef db, UInt32 apptUid) LOC_SEC;

/* Convenience: recordNum (an already-open apptDb's record index) -> its
 * uniqueID -> LinkGet -> LocSummary, all in one cross-segment call. outText
 * is always written (empty string if the record has no linked location or
 * any lookup step fails) -- for Datebook35's Day View prefetch, so the
 * caller's loop body is a single call, not open+find+get+format inline. */
void    LocSummaryForRecord (DmOpenRef apptDb, UInt16 recordNum, char* outText, UInt16 outLen) LOC_SEC;

/* DB 2: "LocationsDB" (the reusable "Previous Loc" history), type 'Dlc1'
 * creator 'Db35'. Records are addressed by their own Data-Manager
 * uniqueID (returned by LocDbAdd), NOT by index -- indices shift on
 * insert/delete, uniqueIDs don't. */
Err     LocDbGetDatabase (DmOpenRef* dbPP, UInt16 mode) LOC_SEC;
UInt32  LocDbAdd (DmOpenRef db, const DlxLocation* loc) LOC_SEC;			/* returns 0 on failure */
Boolean LocDbUpdate (DmOpenRef db, UInt32 recUID, const DlxLocation* loc) LOC_SEC;
Boolean LocDbGet (DmOpenRef db, UInt32 recUID, DlxLocation* out) LOC_SEC;
Err     LocDbDelete (DmOpenRef db, UInt32 recUID) LOC_SEC;
UInt16  LocDbCount (DmOpenRef db) LOC_SEC;
/* Enumerate for a picker list: idx is a 0..LocDbCount()-1 position (NOT
 * stable across adds/deletes mid-enumeration -- finish one pass before
 * mutating). Returns false past the end. */
Boolean LocDbEnumerate (DmOpenRef db, UInt16 idx, UInt32* recUID, DlxLocation* out) LOC_SEC;

/* One-line summary shared by the Details 2-line preview and the Day View
 * "@ ..." suffix, so the two stay consistent by construction.
 * Default format: "Name, City Zip" -- gracefully drops parts that are
 * blank (e.g. just "Name" if City/Zip are both empty). */
void    LocSummary (const DlxLocation* loc, char* out, UInt16 outLen) LOC_SEC;

/* True iff every field of 'loc' is empty (used to detect "location cleared"). */
Boolean LocIsBlank (const DlxLocation* loc) LOC_SEC;

#endif /* LOCDB_H */
