/*
 * Main.h - Address35 shared globals and cross-form prototypes.
 */
#ifndef ADDRESS35_MAIN_H
#define ADDRESS35_MAIN_H

#include <PalmOS.h>
#include "AddressRsc.h"
#include "AddressDB35.h"
#include "Transfer.h"
#include "vcard.h"

#define a35PrefCreator		'Ad35'
#define a35PrefID			0
#define a35PrefVersion		2			/* v2: adds meUniqueID */
/* transient "last used" defaults live in the UNSAVED prefs DB (not HotSync-backed) */
#define a35UnsavedPrefID	1
#define a35DavMePrefID	4		/* UInt32[8]: hash of the "Me" contact's UID per CardDAV slot, 0 = none */
#define a35LookupPrefID	3		/* UInt8: Phone Lookup source, 0 = Local, 1..8 = CardDAV slot */
#define a35DavPrefID		2		/* UInt8: CardDAV slot 1..8 shown at launch, 0 = Local */
#define a35DefaultsVer		2			/* v2: adds showHiddenFiles */
/* last-used country index (address) + dial-code index (phone); 0 = none */
Int16	A35GetLastCountry(void);
void	A35SetLastCountry(Int16 idx);
Int16	A35GetLastDial(void);
void	A35SetLastDial(Int16 idx);
/* Prefs > Display > Show Hidden - unsaved (not HotSync-backed), default unchecked */
void	A35SetShowHiddenFiles(Boolean show);

#define a35NoRecord			0xffff
#define a35NoTag			0			/* current tag filter: 0 = none         */

/* ---- application globals (defined in Address35.c) ---------------------- */
extern DmOpenRef	gAddrDB;
extern UInt16		gCurrentCategory;	/* dmAllCategories / a category index    */
extern Boolean		gShowAllCategories;
extern FontID		gListFont;		/* list-view font (Options > Font)          */
extern UInt16		gCurrentTagFilter;	/* tag id to filter by, or a35NoTag      */
extern UInt16		gCurrentRecord;		/* selected record index, or a35NoRecord */
extern UInt16		gTopVisibleRecord;	/* first record shown in the list        */
extern UInt16		gEditIndex;			/* record open in the editor, a35NoRecord=new */
extern Int16		gEditFocusOrigin;	/* record field tapped in the view (-1=none):
										 * the editor scrolls to + focuses it        */
extern char			gCategoryName[dmCategoryLength];
extern UInt32		gMeUniqueID;		/* uniqueID of the "Me" record, 0 = none  */
void			A35ForgetMeIfDeleted(UInt16 index);	/* clear Me before deleting a record */
extern Boolean		gViewingCard;		/* RecordForm shows gCardRecord (card vCard) */
extern Boolean		gMainDbFreshFromLaunch;	/* StartApplication just merged+sealed --
											 * skip MainForm's frmOpenEvent doing it
											 * again on the very first open */
extern A35Record	gCardRecord;
extern VCardArena	gCardArena;
extern char			gDavTitle[36];
extern Boolean		gShowHiddenFiles;	/* Prefs > Display > Show Hidden; default true */

/* saved on exit */
typedef struct {
	UInt16	version;
	UInt16	currentCategory;
	UInt8	showAllCategories;
	UInt8	listFont;			/* was sortByCompany (sort now lives in AppInfo) */
	UInt16	topVisibleRecord;
	UInt16	currentTagFilter;
	UInt32	meUniqueID;			/* v2: the "Me" record, 0 = none */
} A35Prefs;

/* ---- Address35.c ------------------------------------------------------- */
/* Lock gate: true if OK to view/edit contacts. If the book is encrypted and
 * Sec35 is locked, hands off to Sec35 to unlock (returns false); if Sec35 is
 * missing, alerts and returns false. Call before opening a record/editor. */
Boolean	A35LaunchGate(void) SEC001;
/* Phone Lookup (Lookup.c): sysAppLaunchCmdLookup handler, NewGlobals sublaunch
 * only. Sets params->uniqueID = a35LookupLockedID when Sec35 must unlock first. */
UInt32	A35PhoneLookup(AddrLookupParamsPtr params) SEC002;

/* ---- MainForm.c -------------------------------------------------------- */
Boolean	MainFormHandleEvent(EventPtr eventP);

/* Debounced Find-filter timer -- called from Address35.c's EventLoop so a fast
 * typist doesn't pay for a decrypt-heavy filter pass on every keystroke. See
 * MainForm.c's gFindPendingUntilTicks comment. */
UInt32	MainFindPendingTimeout(void);
void	MainFindFirePending(void);
void	MainInvalidateCardDir(void);	/* force a re-scan next time the card browser opens */

/* ---- EditForm.c -------------------------------------------------------- */
Boolean	EditFormHandleEvent(EventPtr eventP);

/* ---- RecordForm.c ------------------------------------------------------ */
Boolean	RecordFormHandleEvent(EventPtr eventP) SEC001;	/* RecordForm.c is in sec001 */

/* ---- EditDialogs.c (Ad35 seg: sec001 = foreground-only dialogs) --------- */
Int16	A35PickList(const char *title, const char *const *items, UInt16 count,
			Int16 initial) SEC001;
/* *labelP in/out = a35elXxx email label (Home/Work/iCloud/Other) */
Boolean	EditEmailDialog(char *value, UInt16 maxLen, UInt8 *labelP) SEC001;
const char *EditEmailLabelName(UInt8 sub) SEC001;	/* email label name */
Boolean	EditSocialDialog(char *value, UInt16 maxLen, UInt8 *subP) SEC001;
/* value = the bare phone number (no dial code); *dialP = country index (0=None) */
Boolean	EditPhoneDialog(char *value, UInt16 maxLen, UInt8 *dialP) SEC001;
Boolean	EditAddressDialog(char *value, UInt16 maxLen) SEC001;
/* Full-screen note editor; *noteH (may be NULL) in/out, replaced on Done. */
Boolean	EditNoteDialog(MemHandle *noteH) SEC001;
/* Phone dial-code (data + searchable picker) - Ad35 seg: sec002 (sec001 is full) */
const char *A35DialCode(Int16 idx) SEC002;
Int16	A35DialIndexOf(const char *code) SEC002;
Int16	A35DialPickList(Int16 initial) SEC002;
Boolean	EditDateDialog(char *value, UInt16 maxLen, Boolean isAnniv,
			UInt8 *typeP, const char *title) SEC001;
/* value in/out = target record's unique id (decimal string); *relTypeP = a35relXxx.
 * selfUid (0 = none) is excluded from the contact picker. */
Boolean	EditRelationDialog(char *value, UInt16 maxLen, UInt8 *relTypeP, UInt32 selfUid) SEC001;
const char *EditRelTypeName(UInt8 t) SEC001;		/* relation type label */
const char *EditAnnivTypeName(UInt8 t) SEC001;		/* anniversary Type label */
const char *EditSocialNetName(UInt8 sub) SEC001;	/* social network label */
#define a35AnnivOther	4	/* index of "Other" in kAnnivType; custom name lives
				 * in the value after the 8-char "YYYYMMDD" date  */

/* ---- PrefsForm.c ------------------------------------------------------- */
Boolean	PrefsFormHandleEvent(EventPtr eventP) SEC001;	/* foreground-only */

#endif /* ADDRESS35_MAIN_H */
