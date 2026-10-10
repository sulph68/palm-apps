/*
 * Address35.c - application entry, event loop, start/stop, preferences.
 *
 * A modern, era-aware replacement for the ROM Address app. Own DB (Address35DB),
 * own TLV record format (AddressDB35). See apps/address35/Address35.md.
 *
 * Everything here is in the MAIN segment.
 */
#include <PalmOS.h>
#include "Main.h"
#include "enccore.h"		/* EncGetKey: is the suite unlocked? */
#include "hackcheck.h"
#include "catsync.h"		/* keep categories in sync with the other apps */
#include "Transfer.h"		/* beam / vCard receive */
#include "vcard.h"		/* VCardArena for the Card-browse globals */

/* ---- globals ----------------------------------------------------------- */
DmOpenRef	gAddrDB = NULL;
UInt16		gCurrentCategory = dmAllCategories;
Boolean		gShowAllCategories = true;
FontID		gListFont = stdFont;
UInt16		gCurrentTagFilter = a35NoTag;
UInt16		gCurrentRecord = a35NoRecord;
UInt16		gTopVisibleRecord = 0;
UInt16		gEditIndex = a35NoRecord;
Int16		gEditFocusOrigin = -1;
char		gCategoryName[dmCategoryLength];
UInt32		gMeUniqueID = 0;		/* the "Me" record (0 = none); see MainForm/RecordForm */

/* Card-browse: when gViewingCard, RecordForm shows gCardRecord (a read-only vCard
 * off the SD card, field values in gCardArena) with an "Import" button instead of
 * a DB record. See AddrCard.c + MainForm's Browse Card menu. */
Boolean		gViewingCard = false;
A35Record	gCardRecord;
VCardArena	gCardArena;
Boolean		gShowHiddenFiles = false;	/* Prefs > Display > Show Hidden */

/* StartApplication already ran A35MergePlainDB+A35SealAllPlaintext once (both
 * real, whole-database scans) before frmMain's very first frmOpenEvent fires --
 * that event's own handler re-ran the identical pair unconditionally, so every
 * launch paid for the scan twice. This flags "already covered by
 * StartApplication" so MainForm.c's frmOpenEvent can skip its own copy exactly
 * once, on that first open, while still re-running the pair on every
 * SUBSEQUENT return to frmMain (a beam really can arrive, or Sec35 really can
 * get unlocked, while the app was showing some other form). */
Boolean		gMainDbFreshFromLaunch = true;

/* ---- ROM check --------------------------------------------------------- */
static Boolean RomVersionCompatible(UInt32 requiredVersion)
{
	UInt32 romVersion;

	FtrGet(sysFtrCreator, sysFtrNumROMVersion, &romVersion);
	if (romVersion < requiredVersion) {
		FrmAlert(altRomIncompatible);
		return false;
	}
	return true;
}

/* ---- preferences ------------------------------------------------------- */
static void LoadPrefs(void)
{
	A35Prefs prefs;
	UInt16 size = sizeof(prefs);
	Int16 result;

	result = PrefGetAppPreferences(a35PrefCreator, a35PrefID, &prefs, &size, true);
	if (result != noPreferenceFound && size >= sizeof(prefs) &&
		prefs.version == a35PrefVersion) {
		gCurrentCategory = prefs.currentCategory;
		gShowAllCategories = (Boolean) prefs.showAllCategories;
		gListFont = (FontID) prefs.listFont;
		gTopVisibleRecord = prefs.topVisibleRecord;
		gCurrentTagFilter = prefs.currentTagFilter;
		gMeUniqueID = prefs.meUniqueID;
	} else {
		gCurrentCategory = dmAllCategories;
		gShowAllCategories = true;
		gListFont = stdFont;
		gTopVisibleRecord = 0;
		gCurrentTagFilter = a35NoTag;
		gMeUniqueID = 0;
	}
}

static void SavePrefs(void)
{
	A35Prefs prefs;

	prefs.version = a35PrefVersion;
	prefs.currentCategory = gCurrentCategory;
	prefs.showAllCategories = (UInt8) gShowAllCategories;
	prefs.listFont = (UInt8) gListFont;
	prefs.topVisibleRecord = gTopVisibleRecord;
	prefs.currentTagFilter = gCurrentTagFilter;
	prefs.meUniqueID = gMeUniqueID;
	PrefSetAppPreferences(a35PrefCreator, a35PrefID, a35PrefVersion, &prefs,
		sizeof(prefs), true);
}

/* ---- "last used" defaults (UNSAVED prefs; not backed up on HotSync) ------- */
typedef struct { UInt16 version; Int16 lastCountry; Int16 lastDial;
	UInt8 showHiddenFiles; } A35Defaults;

static void A35LoadDefaults(A35Defaults *d)
{
	UInt16 size = sizeof(*d);
	Int16 r = PrefGetAppPreferences(a35PrefCreator, a35UnsavedPrefID, d, &size,
		false);
	if (r == noPreferenceFound || size < sizeof(*d) || d->version != a35DefaultsVer) {
		d->version = a35DefaultsVer;
		d->lastCountry = 0;
		d->lastDial = 0;
		d->showHiddenFiles = false;	/* Show Hidden starts unchecked */
	}
}

static void A35StoreDefaults(const A35Defaults *d)
{
	PrefSetAppPreferences(a35PrefCreator, a35UnsavedPrefID, a35DefaultsVer,
		(void *) d, sizeof(*d), false);	/* saved=false -> unsaved prefs DB */
}

Int16 A35GetLastCountry(void) { A35Defaults d; A35LoadDefaults(&d); return d.lastCountry; }
Int16 A35GetLastDial(void)    { A35Defaults d; A35LoadDefaults(&d); return d.lastDial; }

void A35SetLastCountry(Int16 idx)
{
	A35Defaults d;
	A35LoadDefaults(&d);
	d.lastCountry = idx;
	A35StoreDefaults(&d);
}

void A35SetLastDial(Int16 idx)
{
	A35Defaults d;
	A35LoadDefaults(&d);
	d.lastDial = idx;
	A35StoreDefaults(&d);
}

void A35SetShowHiddenFiles(Boolean show)
{
	A35Defaults d;
	A35LoadDefaults(&d);
	d.showHiddenFiles = (UInt8) show;
	A35StoreDefaults(&d);
	gShowHiddenFiles = show;
}

/* If record `index` is the Me card, forget it. Call BEFORE deleting a record so
 * the Me pointer never dangles. (MeIndexForCat already fails safe if it does,
 * but this keeps gMeUniqueID honest and avoids any stale-id surprises.) */
void A35ForgetMeIfDeleted(UInt16 index)
{
	UInt32 uid = 0;

	if (index == a35NoRecord || gMeUniqueID == 0)
		return;
	if (DmRecordInfo(gAddrDB, index, NULL, &uid, NULL) == errNone &&
		uid == gMeUniqueID)
		gMeUniqueID = 0;
}

/* The launch-time lock gate lives in sec001 (foreground UI segment) to keep main
 * under the 32KB cap; it's only reached from a full (globals) launch. */
static Boolean Sec35Installed(void) SEC001;
static void SwitchToSec35(void) SEC001;
/* A35LaunchGate is declared in Main.h (public; used by MainForm too). */

/* Sec35 (the decryption authority, creator 'Sec3') installed? */
static Boolean Sec35Installed(void)
{
	DmSearchStateType st;
	UInt16 card;
	LocalID dbID;

	return (Boolean) (DmGetNextDatabaseByTypeCreator(true, &st,
		sysFileTApplication, 'Sec3', true, &card, &dbID) == errNone);
}

/* Hand off to Sec35 to unlock: it pops its unlock dialog directly and, once
 * unlocked, switches back to us (a35Creator). We reopen with the key available. */
static void SwitchToSec35(void)
{
	DmSearchStateType st;
	UInt16 card;
	LocalID dbID;

	if (DmGetNextDatabaseByTypeCreator(true, &st, sysFileTApplication, 'Sec3',
			true, &card, &dbID) != errNone)
		return;
	/* pass the return-to creator via a Feature (no cross-app param chunk to free) */
	FtrSet(ENC_CREATOR, kSecReturnFtr, a35Creator);
	SysUIAppSwitch(card, dbID, kSecLaunchUnlock, NULL);
}

/* Lock gate: an encrypted address book needs the Sec35 session key. Returns true
 * to proceed. No Sec35 -> "cannot decrypt" + exit (DESIGN.md §6). Locked -> hand
 * off to Sec35 to unlock; the user reopens Address35 with the key available. */
Boolean A35LaunchGate(void)
{
	UInt8 key[ENC_KEY_BYTES];
	Boolean active;

	/* Encryption is in play if any record is already encrypted OR a passcode is set
	 * with records present (so plaintext records - beam-received / created while
	 * locked - are prompted for + sealed on the next unlock, not left in the clear).
	 * An empty book, or no passcode, opens freely. */
	active = A35DbHasEncrypted(gAddrDB) ||
		(EncEncryptionActive() && DmNumRecords(gAddrDB) > 0);
	if (!active)
		return true;
	if (!Sec35Installed()) {
		FrmAlert(altNoSec35);
		return false;
	}
	if (!EncGetKey(key, 1000000000UL)) {		/* locked -> hand off to Sec35 to unlock */
		SwitchToSec35();
		return false;
	}
	MemSet(key, ENC_KEY_BYTES, 0);
	return true;
}

/* ---- start / stop ------------------------------------------------------ */
static Err StartApplication(void)
{
	Err err;

	if (!RomVersionCompatible(sysMakeROMVersion(4, 0, 0, sysROMStageRelease, 0)))
		return sysErrRomIncompatible;

	err = A35GetDatabase(&gAddrDB, dmModeReadWrite);
	if (err != errNone || gAddrDB == NULL)
		return err ? err : dmErrCantOpen;

	/* Launch gate intentionally DISABLED (user pref, 2026-07): the app opens even when
	 * encrypted+locked and shows masked (******) records; the per-action gates in MainForm
	 * (A35LaunchGate on New/Edit/tap) prompt for the password only when an encrypted record
	 * is actually accessed.  Uncomment to force an unlock-at-launch bounce instead. */
	(void) A35LaunchGate;
	/* if (!A35LaunchGate()) {
		DmCloseDatabase(gAddrDB);
		gAddrDB = NULL;
		return dmErrCantOpen;
	} */

	LoadPrefs();
	{ A35Defaults d; A35LoadDefaults(&d); gShowHiddenFiles = (Boolean) d.showHiddenFiles; }
	EncRegisterSleepWipe();	/* Sec35 "Lock on Power-Off" -- see enccore.h */

	/* Soft hack gate: birthdays store fine without the plugin, but age /
	 * "today" / the Datebook35 Agenda need the real date. Warn once, continue. */
	if (HcCheck() != HC_OK)
		FrmAlert(altHackOff);

	CatSync();		/* union category names across the era apps + ROM apps */
	A35ExgRegister();	/* receive beamed vCards / .vcf files */

	/* Fold any beam/vCard-received records (parked plaintext in Address35DBPlain by
	 * the receive sublaunch) into the real DB, then discard the inbox. */
	A35MergePlainDB(gAddrDB);

	/* Seal any records that are still plaintext (just-merged, or created while
	 * locked): now that we have globals + the crypto segment, encrypt them so an
	 * unencrypted record never lingers past the next unlock. No-op if not unlocked. */
	A35SealAllPlaintext(gAddrDB);
	return errNone;
}

static void StopApplication(void)
{
	FrmCloseAllForms();
	SavePrefs();
	EncUnregisterSleepWipe();
	if (gAddrDB != NULL)
		DmCloseDatabase(gAddrDB);
}

/* ---- About ------------------------------------------------------------- */
static Boolean AboutFormHandleEvent(EventPtr eventP)
{
	Boolean handled = false;

	if (eventP->eType == frmOpenEvent) {
		FrmDrawForm(FrmGetActiveForm());
		handled = true;
	} else if (eventP->eType == ctlSelectEvent) {
		FrmReturnToForm(0);
		handled = true;
	}
	return handled;
}

/* ---- form dispatch ----------------------------------------------------- */
static Boolean ApplicationHandleEvent(EventPtr eventP)
{
	FormPtr formP;
	UInt16 formID;

	if (eventP->eType != frmLoadEvent)
		return false;

	formID = eventP->data.frmLoad.formID;
	formP = FrmInitForm(formID);
	FrmSetActiveForm(formP);

	switch (formID) {
	case frmMain:
		FrmSetEventHandler(formP, MainFormHandleEvent);
		break;
	case frmEdit:
		FrmSetEventHandler(formP, EditFormHandleEvent);
		break;
	case frmRecord:
		FrmSetEventHandler(formP, RecordFormHandleEvent);
		break;
	case frmPrefs:
		FrmSetEventHandler(formP, PrefsFormHandleEvent);
		break;
	case frmAbout:
		FrmSetEventHandler(formP, AboutFormHandleEvent);
		break;
	}
	return true;
}

static void EventLoop(void)
{
	EventType event;
	UInt16 error;

	do {
		/* Bounded wait (not polling): only actually shorter than forever while
		 * the Find filter's debounce timer is armed (MainFindPendingTimeout),
		 * so this wakes up exactly when the pending filter pass is due even if
		 * no other event arrives -- see MainForm.c's gFindPendingUntilTicks. */
		EvtGetEvent(&event, MainFindPendingTimeout());
		if (event.eType == nilEvent)
			MainFindFirePending();
		if (!SysHandleEvent(&event))
			if (!MenuHandleEvent(0, &event, &error))
				if (!ApplicationHandleEvent(&event))
					FrmDispatchEvent(&event);
	} while (event.eType != appStopEvent);
}

/* ---- global Find (sysAppLaunchCmdFind / GoTo) -------------------------- *
 * Runs as a sublaunch (may have no globals), so it opens the DB itself and
 * never touches gAddrDB. Matches any text field; skips packed fields.         */
static Boolean SearchDB(FindParamsPtr fp, DmOpenRef db, UInt16 card, LocalID dbID)
{
	UInt16 num = DmNumRecords(db), i, pos, fi;

	if (FindDrawHeader(fp, "Address35"))
		return true;
	for (i = fp->recordNum; i < num; i++) {
		A35Record rec;
		MemHandle h;
		Boolean match = false;

		/* Find runs as a no-globals sublaunch (sec003 unloaded) -> RAW read, and
		 * only search CLEAR fields (encrypted values are unreadable ciphertext). */
		if (A35GetRecordRaw(db, i, &rec, &h) != errNone)
			continue;
		for (fi = 0; fi < rec.fieldCount; fi++) {
			UInt8 t = rec.fields[fi].type;
			if (t == a35fRelation || t == a35fTag || rec.fields[fi].len == 0 ||
				A35FieldEncrypted(t))
				continue;
			if (FindStrInStr((Char *) rec.fields[fi].value, fp->strToFind, &pos)) {
				match = true;
				break;
			}
		}
		MemHandleUnlock(h);
		if (!match)
			continue;
		if (FindSaveMatch(fp, i, 0, 0, 0, card, dbID)) {
			fp->recordNum = i;
			return true;			/* match list full -> resume here next time */
		}
		{
			RectangleType r;
			char nm[72];

			FindGetLineBounds(fp, &r);
			A35RecordPickName(db, i, nm, sizeof(nm));
			WinDrawChars(nm, (Int16) StrLen(nm),
				(Coord) (r.topLeft.x + 1), r.topLeft.y);
			fp->lineNumber++;
		}
	}
	fp->recordNum = 0;
	return false;
}

static void SearchFind(FindParamsPtr fp)
{
	DmSearchStateType st;
	UInt16 card;
	LocalID dbID;
	Boolean done = false;
	Err err;

	err = DmGetNextDatabaseByTypeCreator(true, &st, a35DBType, a35Creator,
		true, &card, &dbID);
	while (err == errNone && dbID != 0 && !done) {
		DmOpenRef db = DmOpenDatabase(card, dbID, dmModeReadOnly);
		if (db != NULL) {
			done = SearchDB(fp, db, card, dbID);
			DmCloseDatabase(db);
		}
		if (done)
			break;
		err = DmGetNextDatabaseByTypeCreator(false, &st, a35DBType, a35Creator,
			true, &card, &dbID);
	}
}

static void GoToRecord(GoToParamsPtr gp)
{
	if (gAddrDB == NULL)			/* mis-delivered goto with no open DB -> no crash */
		return;
	gEditIndex = gp->recordNum;
	gCurrentRecord = gp->recordNum;
	gShowAllCategories = true;
	gCurrentCategory = dmAllCategories;
	FrmGotoForm(frmRecord);
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
	Err err;
	
	if (cmd == sysAppLaunchCmdNormalLaunch) {
		err = StartApplication();
		if (err != errNone)
			return err;
		FrmGotoForm(frmMain);
		EventLoop();
		StopApplication();
	} else if (cmd == sysAppLaunchCmdFind) {
		SearchFind((FindParamsPtr) cmdPBP);
	} else if (cmd == sysAppLaunchCmdGoTo) {
		if (launchFlags & sysAppLaunchFlagNewGlobals) {	/* launched for this */
			err = StartApplication();
			if (err != errNone)
				return err;
			GoToRecord((GoToParamsPtr) cmdPBP);
			EventLoop();
			StopApplication();
		} else {					/* already running */
			GoToRecord((GoToParamsPtr) cmdPBP);
		}
	} else if (cmd == sysAppLaunchCmdLookup) {
		/* Phone Lookup from Datebook35/ToDo35 (../lib/a35lookup.c). Needs
		 * globals - multi-segment code - so only the NewGlobals form. */
		if (launchFlags & sysAppLaunchFlagNewGlobals)
			return A35PhoneLookup((AddrLookupParamsPtr) cmdPBP);
	} else if (cmd == sysAppLaunchCmdExgAskUser) {
		/* Beam ACCEPT + category picker.  Can arrive as a no-globals sublaunch, so open our
		 * own DB ref unless we're a subcall of the running app (then gAddrDB is valid). */
		DmOpenRef dbP;
		Boolean sub = (launchFlags & sysAppLaunchFlagSubCall) != 0;
		if (!sub) {
			if (A35GetDatabase(&dbP, dmModeReadWrite) != errNone)
				dbP = NULL;
		} else
			dbP = gAddrDB;
		if (dbP != NULL) {
			A35AcceptBeamDialog(dbP, (ExgAskParamPtr) cmdPBP);
			if (!sub)
				DmCloseDatabase(dbP);
		}
	} else if (cmd == sysAppLaunchCmdExgReceiveData) {
		/* Beamed/SD vCard(s). This can arrive as a no-globals sublaunch (app not
		 * running), where gAddrDB, the session key and the goto-display path are all
		 * off-limits (they fault). So - whether or not we're running - we drop the
		 * records, as plaintext, into the Address35DBPlain inbox and touch NOTHING
		 * else. The next full launch / list reload merges + seals them. */
		DmOpenRef pdb;
		if (A35OpenPlainDB(&pdb, true) == errNone) {
			A35ReceiveData(pdb, (ExgSocketPtr) cmdPBP);
			DmCloseDatabase(pdb);
		}
	}
	return 0;
}
