/*
 * AddressDB35.h - Address35 database layer (own DB, own record format).
 *
 * Address35 replaces the ROM Address app with a self-describing TLV record so it
 * can hold multiple values per field (phones/emails/addresses/IM), relationships,
 * tags, and an era-independent birthday - all extensible without breaking the
 * format. See apps/address35/Address35.md for the full design.
 *
 * DB: type 'DATA', creator 'Ad35', name "Address35DB". Its own database - never
 * the ROM 'addr'/AddressDB.
 *
 * PERFORMANCE (a first-class requirement, see performance-old-hardware memory):
 *  - The sort-key value offsets (last/first/company) live in the fixed record
 *    header, so the sort comparator reads them WITHOUT unpacking the TLV blob.
 *  - TLV multi-byte fields are read/written BYTE-WISE big-endian: a UInt16 length
 *    can land on an odd address and a UInt16* load there faults on a 68000.
 *  - Callers operate one record at a time (never all-contacts-in-RAM); unpack is
 *    a zero-copy view into the locked packed record.
 */
#ifndef ADDRESSDB35_H
#define ADDRESSDB35_H

#include <PalmOS.h>

#ifndef DATELIB_SECTION
#define DATELIB_SECTION
#endif
#ifndef SEC001				/* host regression harness has no a35_seg.h */
#define SEC001
#endif
#ifndef SEC002
#define SEC002
#endif
#ifndef SEC003
#define SEC003
#endif

/* ---- identity ---------------------------------------------------------- */
#define a35DBType			'DATA'
#define a35Creator			'Ad35'
#define a35DBName			"Address35DB"
/* Transient plaintext inbox for beamed/vCard-received records. The receive
 * sublaunch (no globals, no crypto, no goto) drops records here; the next full
 * launch (or list reload) merges them into a35DBName and deletes this. Distinct
 * type so global Find (which scans 'DATA'/'Ad35') never sees the inbox. */
#define a35PlainDBName		"Address35DBPlain"
#define a35PlainDBType		'Ad3P'
#define a35RecVersion		1

/* ---- limits ------------------------------------------------------------ */
#define a35MaxFields		48			/* fields per contact (unpacked)          */
#define a35MaxTags			32			/* distinct tags in the AppInfo dictionary*/
#define a35TagNameLen		16			/* incl. NUL                              */
#define a35MaxValueLen		2048		/* per-field value cap (note may be big)  */

/* ---- field types (FieldEntry.type) ------------------------------------- */
enum {
	a35fNone = 0,
	a35fLast,			/* family/last name  (sort key)                      */
	a35fFirst,			/* given/first name  (sort key)                      */
	a35fMiddle,
	a35fNickname,
	a35fCompany,		/* organization      (sort key)                      */
	a35fTitle,
	a35fPhone,			/* label = a35plXxx                                  */
	a35fEmail,
	a35fIM,				/* label = a35imXxx                                  */
	a35fURL,
	a35fAddress,		/* value = a sub-TLV of address lines (see .c)       */
	a35fBirthday,		/* value = "YYYYMMDD" string (year 0000 = unknown)   */
	a35fRelation,		/* value = packed A35Relation                        */
	a35fTag,			/* value = UInt16 tagId (big-endian)                 */
	a35fNote,
	a35fCustom,			/* value = "label\0text"                             */
	/* --- appended for edit-form v2 (keep order; values are on disk) --- */
	a35fCity,
	a35fState,
	a35fZip,
	a35fCountry,
	a35fSocial,			/* label = a35socXxx (network); value = handle/URL   */
	a35fGender,			/* value = text (Female/Male/...)                    */
	a35fPronouns,		/* value = text (she/her, ...)                       */
	a35fAnniversary,	/* value = "YYYYMMDD" string; label = anniv type     */
	a35fOther,			/* generic free-text field                           */
	a35fPrefix,			/* name prefix (Mr./Mrs./Dr./...)                    */
	a35fSuffix,			/* name suffix (Jr./Sr./III/...)                     */
	a35fTypeCount
};

/* ---- record.misc flags ------------------------------------------------- */
#define a35MiscEncrypted	0x01	/* payload field values are AES-CTR encrypted */

/* ---- label byte: low bits = subtype, high bit = "primary/displayed" ---- */
#define a35PrimaryFlag		0x80
#define a35LabelMask		0x7f

/* phone labels (a35fPhone) */
enum { a35plMobile = 0, a35plHome, a35plWork, a35plFax, a35plMain,
       a35plPager, a35plOther };
/* social networks (a35fSocial label) - maps to vCard X-SOCIALPROFILE service.
 * Order matches the Social dialog dropdown. a35socOther => value is
 * "PlatformName: handle" (the custom platform name is kept in the value). */
enum { a35socInstagram = 0, a35socFacebook, a35socWhatsApp, a35socX,
       a35socTelegram, a35socSignal, a35socOther };
/* address labels (a35fAddress) */
enum { a35alHome = 0, a35alWork, a35alOther };
/* email labels (a35fEmail) */
enum { a35elHome = 0, a35elWork, a35elICloud, a35elOther };
/* relation types (a35fRelation) */
enum { a35relSpouse = 0, a35relPartner, a35relChild, a35relParent,
       a35relSibling, a35relAssistant, a35relManager, a35relFriend,
       a35relOther };

/* ---- surname terminology preference ------------------------------------ */
enum { a35termSurname = 0, a35termLastName, a35termFamilyName };

/* ---- sort-order keys (Preferences: Primary + Secondary) ----------------- *
 * Values index the fixed record-header sort-key offsets, so the comparator
 * needs no unpack. Order matches the Preferences popup list. */
enum { a35sortLast = 0, a35sortFirst, a35sortCompany };

/* ---- structured field values ------------------------------------------- */
/* Birthday/Anniversary: era-independent, FULL 4-digit year (any year, no 2031
 * cap); month/day-only when !hasYear. On disk it is a fixed 8-char string
 * "YYYYMMDD" (year "0000" == unknown), so Datebook35/ToDo35 can read it too. */
typedef struct {
	UInt8	flags;			/* bit0 = hasYear                                */
	UInt8	month;			/* 1..12                                         */
	UInt8	day;			/* 1..31                                         */
	Int16	year;			/* full year, valid iff (flags & a35bdHasYear)   */
} A35Birthday;
#define a35bdHasYear		0x01
#define a35BirthdayStrLen	8		/* "YYYYMMDD"                            */

/* Relation: link to another record by its stable DmRecordInfo unique id. */
typedef struct {
	UInt32	targetUniqueID;
	UInt8	relType;		/* a35relXxx                                     */
} A35Relation;
#define a35RelationPackLen	5

/* ---- unpacked (in-RAM) record: a zero-copy view or an editor's build ---- */
typedef struct {
	UInt8			type;
	UInt8			label;
	UInt16			len;
	const UInt8 *	value;	/* into a locked packed record, or an editor buffer */
} A35Field;

typedef struct {
	UInt8		version;
	UInt8		misc;			/* reserved flags                            */
	UInt16		fieldCount;
	A35Field	fields[a35MaxFields];
} A35Record;

/* ---- AppInfo: standard category block + Address35 extensions ------------ */
/* The first four members MUST match the standard AppInfoType so the ROM
 * Category* routines operate on this pointer unchanged. */
typedef struct {
	UInt16	renamedCategories;
	char	categoryLabels[dmRecNumCategories][dmCategoryLength];
	UInt8	categoryUniqIDs[dmRecNumCategories];
	UInt8	lastUniqID;
	/* --- Address35 extensions --- */
	UInt8	surnameTerm;	/* a35termXxx                                    */
	UInt8	primarySort;	/* a35sortXxx - primary sort key (was sortByCompany) */
	UInt8	secondarySort;	/* a35sortXxx - secondary sort key (was reserved)    */
	UInt16	tagCount;		/* number of defined tags (ids 1..tagCount)      */
	char	tagNames[a35MaxTags][a35TagNameLen];
} A35AppInfoType;
typedef A35AppInfoType *A35AppInfoPtr;

#ifdef __cplusplus
extern "C" {
#endif

/* ---- database open / appinfo ------------------------------------------- */
Err		A35GetDatabase(DmOpenRef *dbPP, UInt16 mode);
Err		A35AppInfoInit(DmOpenRef dbP);
A35AppInfoPtr A35AppInfoGetPtr(DmOpenRef dbP);

/* ---- transient plaintext inbox (beam receive) -------------------------- *
 * Both MAIN + globals/crypto-free, so the no-globals receive sublaunch can call
 * A35OpenPlainDB safely. */
/* Open (create if missing && create) the Address35DBPlain inbox. */
Err		A35OpenPlainDB(DmOpenRef *dbPP, Boolean create);
/* Append raw received bytes (unparsed vCard text) as one inbox record. Pure DB I/O
 * (no parser/sort/AppInfo/globals) -> safe in the no-globals receive sublaunch. */
void	A35InboxAppendRaw(DmOpenRef pdb, const void *bytes, UInt32 len, UInt16 category);
/* If the inbox exists, PARSE each raw record's vCard(s) and append them into mainDB,
 * then delete the inbox. Uses the vCard parser -> call only from a full launch
 * (globals). Records are sealed afterwards by A35SealAllPlaintext when unlocked. */
void	A35MergePlainDB(DmOpenRef mainDB) SEC002;	/* full launch only (needs globals), off main for space */

/* ---- record CRUD ------------------------------------------------------- */
/* A35GetRecord returns a zero-copy VIEW: out->fields[i].value point into the
 * record locked as *recordH. The caller MUST MemHandleUnlock(*recordH) when done.
 */
/* A35GetRecord: the returned view's field values are DECRYPTED (if the record is
 * encrypted and Sec35 is unlocked). *recordH is an opaque handle - release it with
 * A35ReleaseRecord (NOT MemHandleUnlock). FOREGROUND ONLY: it lives in the crypto
 * code segment (sec003), which no-globals sublaunches (Find/beam-receive) don't
 * load - those must use A35GetRecordRaw instead. */
Err		A35GetRecord(DmOpenRef dbP, UInt16 index, A35Record *out, MemHandle *recordH) SEC003;
void	A35ReleaseRecord(MemHandle recordH) SEC003;
/* Raw, NON-decrypting zero-copy view (CLEAR fields only: name/company/tag/
 * relation/birthday). Safe in a no-globals sublaunch. Release: MemHandleUnlock. */
Err		A35GetRecordRaw(DmOpenRef dbP, UInt16 index, A35Record *out, MemHandle *recordH);
Boolean	A35FieldEncrypted(UInt8 type);	/* is this field type's value encrypted? */
Boolean	A35DbHasEncrypted(DmOpenRef dbP);	/* any record marked encrypted? (raw scan) */
/* Seal a just-written record's payload in place, keyed by its uniqueID. FOREGROUND
 * ONLY (touches sec003); no-op when Sec35 is locked. Call after A35NewRecord. */
void	A35EncryptRecordInPlace(DmOpenRef dbP, UInt16 index) SEC003;
/* Seal every still-plaintext record (created while locked / beam-received). No-op
 * when locked. Call on launch once unlocked so plaintext never lingers. sec003. */
void	A35SealAllPlaintext(DmOpenRef dbP) SEC003;
Err		A35NewRecord(DmOpenRef dbP, A35Record *r, UInt16 *index);
/* Foreground-only (edit save; not on the no-globals receive path -> sec001). */
Err		A35ChangeRecord(DmOpenRef dbP, UInt16 *index, A35Record *r) SEC001;
/* Duplicate record srcIndex: a decrypted copy inserted in sorted position with the
 * same category, re-sealed under the NEW record's uniqueID (the payload nonce is
 * keyed to the uid, so a raw byte copy would decrypt to garbage). Foreground only
 * (touches sec003). *newIndex = the copy's index. */
Err		A35DuplicateRecord(DmOpenRef dbP, UInt16 srcIndex, UInt16 *newIndex) SEC003;
Boolean	A35RecordHasData(const A35Record *r);

/* ---- unpacked record building / access --------------------------------- */
void			A35RecordInit(A35Record *r);
/* Append a field; value is referenced (not copied) - keep it alive until pack. */
Boolean			A35AddField(A35Record *r, UInt8 type, UInt8 label,
					const void *value, UInt16 len);
Boolean			A35AddStr(A35Record *r, UInt8 type, UInt8 label, const char *s);
const A35Field *A35FindField(const A35Record *r, UInt8 type, UInt16 occurrence);
const char *	A35GetStr(const A35Record *r, UInt8 type);	/* first, "" if none */

/* ---- relations --------------------------------------------------------- */
void	A35PackRelation(const A35Relation *rel, UInt8 *out);	/* out >= 5 bytes */
Boolean	A35UnpackRelation(const UInt8 *p, UInt16 len, A35Relation *out);
Boolean	A35FindByUniqueID(DmOpenRef dbP, UInt32 uid, UInt16 *indexP);
void	A35RecordDisplayName(DmOpenRef dbP, UInt16 index, char *out, UInt16 outLen);
void	A35RecordPickName(DmOpenRef dbP, UInt16 index, char *out, UInt16 outLen);
UInt8	A35ReciprocalRelType(UInt8 t);	/* type the OTHER record carries back */
/* foreground-only (reads decrypted via A35GetRecord) -> sec003, to keep main
 * under the 32KB cap. Not reached by any no-globals sublaunch. */
Err	A35AddReciprocalRelation(DmOpenRef dbP, UInt32 targetUid, UInt32 backUid,
			UInt8 recipType) SEC003;	/* add backUid link to targetUid if absent */

/* ---- birthday / anniversary (portable "YYYYMMDD" string) --------------- */
Boolean	A35GetBirthday(const A35Record *r, A35Birthday *out);	/* false if none */
/* Parse a "YYYYMMDD" string into *out (false if malformed). year 0 => !hasYear. */
Boolean	A35ParseBirthday(const char *ymd, A35Birthday *out);
/* Serialize *bd to a fixed "YYYYMMDD"\0 string (out must hold >= 9 bytes). */
void	A35FormatBirthday(const A35Birthday *bd, char *out);
/* Human-readable form of a "YYYYMMDD" string, e.g. "Jun 15, 1990" (or "Jun 15"
 * when the year is 0000), honoring the system date-format order. out >= 24. */
void	A35BirthdayDisplay(const char *ymd, char *out);

/* ---- sort -------------------------------------------------------------- */
/* Store the primary/secondary sort keys (a35sortXxx) in the AppInfo and re-sort.
 * Foreground-only (Preferences) -> sec001. */
Err		A35ChangeSortOrder(DmOpenRef dbP, UInt8 primary, UInt8 secondary) SEC001;
/* Read the current sort keys (defaults last/first). Foreground-only -> sec001. */
void	A35GetSortOrder(DmOpenRef dbP, UInt8 *primary, UInt8 *secondary) SEC001;
/* Sort position for a freshly packed record (insert without full re-sort). */
UInt16	A35FindSortPosition(DmOpenRef dbP, void *packedRecordP);

/* ---- tag dictionary (AppInfo) ------------------------------------------ */
UInt16		A35TagFind(DmOpenRef dbP, const char *name);	/* id, 0 if absent   */
UInt16		A35TagAdd(DmOpenRef dbP, const char *name);		/* find-or-add id    */
/* Copy tag <tagId>'s name into out (>= a35TagNameLen); false if id invalid. */
Boolean		A35TagName(DmOpenRef dbP, UInt16 tagId, char *out);
Boolean		A35RecordHasTag(const A35Record *r, UInt16 tagId);

/* ---- preferences in AppInfo -------------------------------------------- */
UInt8	A35GetSurnameTerm(DmOpenRef dbP);
void	A35SetSurnameTerm(DmOpenRef dbP, UInt8 term);
/* Display string for a surname term: "Surname" / "Last Name" / "Family Name". */
const char *A35SurnameTermStr(UInt8 term);

#ifdef __cplusplus
}
#endif

#endif /* ADDRESSDB35_H */
