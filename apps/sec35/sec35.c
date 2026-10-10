/*
 * sec35.c - Sec35 security manager (single pane) for the era suite.
 *
 * Owns the suite passcode + TTL + the auto-lock alarm, and caches the derived
 * key in shared RAM (via enccore) so the apps can encrypt/decrypt in-process.
 * Set/Change/Clear and the wipe path (Forget Password + 10th-wrong-attempt
 * auto-wipe) all re-encrypt/decrypt/remove each of the 4 era apps' own data
 * directly from here -- see the Recrypt-prefixed functions, WipeEncDb and
 * WipeCryptoNoteDb below (Address35, ToDo35, Datebook35, CryptoNote).
 */
#include <PalmOS.h>
#include "enccore.h"

/* ---- main form ---- */
#define frmMain			1000
#define pwSelector		1001		/* "Password:" state box (tap to manage) */
#define btnLock			1004
#define btnHelp			1012
#define lblStatus		1010
#define fldTtl			1006
#define popTtlUnit		1007
#define lstTtlUnit		1008
#define lblCountdown	1013
#define chkLockOnSleep	1014
/* ---- password dialogs (Set = 2 rows, Change = 3 rows + Delete) ---- */
#define frmPwSet		1100
#define frmPwChange		1200
#define fldCurrent		1101
#define fldNew			1102
#define fldConfirm		1103
#define btnPwOk			1104
#define btnPwCancel		1105
#define btnPwDelete		1109
/* ---- unlock dialog (password-only) ---- */
#define frmUnlock		1300
#define frmProgress		1400
#define frmDelay		1500
#define fldUnlockPw		1301
#define btnUnlockOk		1302
#define btnUnlockCancel	1303
#define btnDelayOk		1501
#define mnuMain			1600
#define mniForget		1601
#define mniPrefs		1602
/* ---- preferences (password entry method) ---- */
#define frmPrefs		1800
#define popPwType		1802
#define lstPwType		1803
#define btnPrefsOk		1804
#define btnPrefsCancel	1805
/* ---- numeric keypad (PIN-style entry, used in place of graffiti/keyboard text
 * entry for the password fields above when Password Type = Numeric Keypad) ---- */
#define frmKeypad		1900
#define kpd1			1901
#define kpdDel			1910
#define kpd0			1911
#define kpdOk			1912
#define kpdCancel		1913
/* ---- resume-interrupted-rekey dialog (Old/New password, no Delete) ----
 * Reuses fldCurrent/fldNew (the masking/keypad plumbing dispatches by field ID,
 * not by form) and btnPwCancel (identical dismiss behavior); only the OK button
 * needs its own ID since ResumeOk's logic is different from PwOk's. */
#define frmResume		2100
#define btnResumeOk		2101
/* ---- alerts ---- */
#define altMismatch		2001
#define altEmpty		2002
#define altWrong		2003
#define altHelp			2004
#define altForget		2005
#define altLowMem		2006
#define altIncomplete	2007
#define altResumeDone	2008

#define kSecPrefId		0
#define kSecPrefVer		5
#define kAlarmRef		1UL

/* PW_SET when no password yet (New/Confirm); PW_CHANGE when one exists
 * (Current + New/Confirm, plus a Delete button that clears it); PW_RESUME for the
 * interrupted-CHANGE recovery dialog (Old/New, no Confirm, no Delete). */
enum { PW_SET, PW_CHANGE, PW_RESUME };

/* Password entry method (Preferences > Password Type). Purely a UI input choice --
 * the password itself is always just a string, compared the same way regardless of
 * how it was typed, so a numeric password entered via keypad matches the same digits
 * entered via graffiti/keyboard in Text Entry mode. kPwTypeText/kPwTypeNumeric now
 * come from enccore.h -- shared cross-app (EncGetPasswordEntryType/
 * EncSetPasswordEntryType) so other apps (e.g. CryptoNote) can offer the same
 * masked-entry style without depending on Sec35's own Sec35Pref struct layout. */

typedef struct {
	UInt8	salt[ENC_SALT_BYTES];
	UInt8	verifier[16];		/* CTR(key, kVerifyConst, nonce 0) */
	UInt16	rounds;
	UInt16	ttlValue;
	UInt8	ttlUnit;			/* 0 = minutes, 1 = hours, 2 = days */
	UInt8	passwordSet;
	/* --- v2: wrong-password lockout (appended; old prefs load with these 0) --- */
	UInt8	failCount;			/* consecutive wrong attempts */
	UInt8	reserved;
	UInt32	delayUntil;			/* TimGetSeconds() before which unlocking is blocked */
	/* --- v3: password entry method (appended; old prefs load with this 0 = Text Entry,
	 * today's behavior, unchanged) --- */
	UInt8	pwType;
	UInt8	reserved2[3];
	/* --- v4: interrupted-CHANGE resume checkpoint (appended; old prefs load with
	 * rekeyPending=0 -- nothing pending, matches today's behavior exactly).
	 * Deliberately holds NO key material, only the new (non-secret) salt/rounds a
	 * CHANGE was mid-way through -- a crash before this is written can never lose
	 * data, and the checkpoint itself can't be used to derive any key without the
	 * password being re-entered. See PwOk/ResumeOk. */
	UInt8	rekeyPending;
	UInt8	reserved3[3];
	UInt8	pendingSalt[ENC_SALT_BYTES];
	UInt16	pendingRounds;
	/* --- v5: lock the session immediately when the device sleeps/powers off
	 * (appended; old prefs load with lockOnSleep=0 -- off, matching today's
	 * behavior: rely on the TTL alone, unchanged unless explicitly turned on). */
	UInt8	lockOnSleep;
	UInt8	reserved4[3];
} Sec35Pref;

static Sec35Pref	gPref;
static UInt16		gPwMode;
static UInt32		gUnlockReturn;	/* creator to switch back to after an unlock hand-off, 0 = none */

/* Secondary safety net alongside EnoughMemoryForRekey's pre-flight check: set by any
 * RecryptXxx35 call if it had to skip a record because its temp buffer allocation
 * failed mid-pass anyway (heap got more fragmented than the pre-flight check could
 * predict). PwOk/PwDelete check this after the whole sequence and warn if it fired,
 * since that record is now left mismatched against the session's active key. */
static Boolean		gRekeyIncomplete;

/* Numeric keypad: which password FIELD to fill in when kpdOk is tapped (set right
 * before FrmPopupForm(frmKeypad) by the fldEnterEvent interception below), and the
 * digit buffer being built up while the keypad is on screen. */
static UInt16		gKeypadTargetField;
static char			gKeypadBuf[32];
static UInt16		gKeypadLen;

/* Password masking: the real (unmasked) text for each of the 4 password fields lives
 * ONLY here -- the visible FIELD always shows a masked ('*') representation, revealing
 * just-typed characters in Text Entry mode for kRevealTicks before masking them too
 * (like a modern phone keyboard). Everything that needs the actual password (Verify/
 * DoSetPassword/...) reads these via FieldText(), not the field's own displayed text. */
#define kPwBufLen 33
static char			gPwCurrent[kPwBufLen];
static char			gPwNew[kPwBufLen];
static char			gPwConfirm[kPwBufLen];
static char			gPwUnlock[kPwBufLen];

/* Which field (if any) is currently showing its last character in clear, and when to
 * mask it -- checked from PilotMain's event loop (a bounded EvtGetEvent timeout, not
 * polling) so the reveal expires even if the user doesn't type anything else. */
static UInt16		gRevealField;
static UInt32		gRevealUntilTicks;

/* Countdown label ("Locking in HH:MM"/"MM:SS") auto-refresh, same bounded-
 * EvtGetEvent-timeout idiom as gRevealUntilTicks above -- armed by RefreshMain
 * whenever there's a live countdown to show, cleared when there isn't
 * (locked / no password), checked from PilotMain's event loop. */
static UInt32		gCountdownRefreshUntilTicks;
#define kCountdownRefreshSecs	30

/* file-scope (single-seg is fine, but keep tables out of function bodies) */
static const UInt8 kVerifyConst[16] =
	{ 'S','e','c','3','5','-','v','e','r','i','f','y','!',0,1,2 };
static const char* const gUnitNames[3] = { "Minutes", "Hours", "Days" };
static const char* const gPwTypeNames[2] = { "Text Entry", "Numeric Keypad" };

/* Phone-style keypad layout: index = digit value (0-9), kKeyIds = that digit's
 * BUTTON resource ID, kKeyLetters = the (purely decorative) letters drawn under it,
 * hand-drawn since a BUTTON label can't hold a second line. */
static const UInt16 kKeyIds[10]     = { kpd0, kpd1, kpd1+1, kpd1+2, kpd1+3, kpd1+4, kpd1+5, kpd1+6, kpd1+7, kpd1+8 };
static const char* const kKeyLetters[10] =
	{ "", "", "ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ" };

/* ------------------------------------------------------------------ */

static void* GetObj (UInt16 id)
{
	FormType* frm = FrmGetActiveForm ();
	UInt16 idx = FrmGetObjectIndex (frm, id);
	if (idx == frmInvalidObjectId) return NULL;	/* id not in the CURRENT active form */
	return FrmGetObjectPtr (frm, idx);
}

/* Replace a field's displayed content with `text`, editing the field's OWN existing
 * handle in place (FldDelete + FldInsert) rather than swapping in a brand new handle
 * (FldSetTextHandle). The masking feature below used to free + reallocate a fresh
 * handle on every keystroke, which triggered a rare "MemoryMgr.c: non-word-aligned
 * handle" crash under memory pressure (many X-Master hacks active) -- editing in
 * place matches how the Field Manager already handles ordinary typing internally
 * (no repeated alloc/free churn), so it doesn't hit that edge case. */
static void FldSetContent (FieldType* fld, const char* text)
{
	UInt16 curLen = FldGetTextLength (fld);
	UInt16 newLen = (UInt16) StrLen (text);

	if (curLen > 0)
		{
		FldSetInsPtPosition (fld, curLen);
		FldDelete (fld, 0, curLen);
		}
	if (newLen > 0)
		{
		FldSetInsPtPosition (fld, 0);
		FldInsert (fld, text, newLen);
		}
}

static UInt32 TtlSeconds (void);

static void LoadPrefs (void)
{
	UInt16 sz = sizeof (gPref);

	/* zero first so a shorter (v1) stored blob leaves the appended lockout fields
	 * at 0; the password/ttl fields sit at unchanged offsets -> forward compatible. */
	MemSet (&gPref, sizeof (gPref), 0);
	gPref.ttlValue = 5;
	gPref.ttlUnit  = 0;				/* 5 minutes */
	PrefGetAppPreferences (ENC_CREATOR, kSecPrefId, &gPref, &sz, true);
	/* (re)assert the suite-wide "encryption active" signal for the era apps. */
	EncSetEncryptionActive ((Boolean) gPref.passwordSet);
	/* (re)assert the shared password-entry-method preference for the era apps
	 * (e.g. CryptoNote) -- keeps it in sync even if it was never explicitly
	 * changed this launch (covers the very first launch after upgrading too,
	 * since gPref.pwType defaults to 0 = kPwTypeText either way). */
	EncSetPasswordEntryType (gPref.pwType);
	/* (re)assert the shared auto-lock TTL -- every app's EncKeyPresent call
	 * reads this to self-check whether the cached key has actually expired,
	 * see enccore.h. Must be current even if the user never touches the TTL
	 * setting this launch (covers upgrading from a build that never published
	 * it at all, in which case EncGetTtlSeconds' 300s default was being used
	 * for everyone regardless of the REAL configured value until now). */
	EncSetTtlSeconds (TtlSeconds ());
	/* (re)assert "lock on power-off" -- every app's sleep-notify callback
	 * (EncRegisterSleepWipe) reads this to decide whether to actually wipe. */
	EncSetLockOnSleep ((Boolean) gPref.lockOnSleep);
}

static void SavePrefs (void)
{
	PrefSetAppPreferences (ENC_CREATOR, kSecPrefId, kSecPrefVer,
		&gPref, sizeof (gPref), true);
}

static UInt32 TtlSeconds (void)
{
	UInt32 mult = (gPref.ttlUnit == 0) ? 60UL
				 : (gPref.ttlUnit == 1) ? 3600UL : 86400UL;
	UInt32 v = gPref.ttlValue ? gPref.ttlValue : 1;
	return v * mult;
}

static Boolean GetMyDb (UInt16* card, LocalID* db)
{
	DmSearchStateType st;
	return (Boolean)(DmGetNextDatabaseByTypeCreator (true, &st,
		sysFileTApplication, ENC_CREATOR, true, card, db) == errNone);
}

static void ArmAlarm (void)
{
	UInt16 card; LocalID db;
	if (GetMyDb (&card, &db))
		AlmSetAlarm (card, db, kAlarmRef, TimGetSeconds () + TtlSeconds (), true);
}

static void CancelAlarm (void)
{
	UInt16 card; LocalID db;
	if (GetMyDb (&card, &db))
		AlmSetAlarm (card, db, kAlarmRef, 0, true);		/* 0 cancels */
}

/* ---- passcode operations ---- */

static Boolean VerifyPassword (const char* pw)
{
	UInt8	key[ENC_KEY_BYTES], ver[16];
	Boolean	ok;

	if (! gPref.passwordSet) return false;
	EncDeriveKey (pw, gPref.salt, gPref.rounds, key);
	MemMove (ver, kVerifyConst, 16);
	EncCrypt (key, ver, 16, 0, 0);
	ok = (Boolean)(MemCmp (ver, gPref.verifier, 16) == 0);
	if (ok) { EncCacheKey (key); ArmAlarm (); }		/* a correct entry unlocks */
	MemSet (key, ENC_KEY_BYTES, 0);
	MemSet (ver, 16, 0);
	return ok;
}

static void DoSetPassword (const char* pw)
{
	UInt8 key[ENC_KEY_BYTES], ver[16];

	EncRandomBytes (gPref.salt, ENC_SALT_BYTES);
	gPref.rounds = EncCalibrateRounds (800);		/* ~0.8 s of stretching */
	EncDeriveKey (pw, gPref.salt, gPref.rounds, key);
	MemMove (ver, kVerifyConst, 16);
	EncCrypt (key, ver, 16, 0, 0);
	MemMove (gPref.verifier, ver, 16);
	gPref.passwordSet = true;
	SavePrefs ();
	EncSetEncryptionActive (true);	/* encryption is now active suite-wide */
	EncCacheKey (key);				/* unlock now */
	ArmAlarm ();
	MemSet (key, ENC_KEY_BYTES, 0);
	MemSet (ver, 16, 0);
}

static void DoClearPassword (void)
{
	/* Callers (PwDelete) DECRYPT all data first, so removing the passcode leaves
	 * readable plaintext rather than orphaned ciphertext. */
	gPref.passwordSet = false;
	MemSet (gPref.salt, ENC_SALT_BYTES, 0);
	MemSet (gPref.verifier, 16, 0);
	SavePrefs ();
	EncSetEncryptionActive (false);	/* no passcode -> encryption inactive */
	EncWipeKey ();
	CancelAlarm ();
}

/* ---- rekey on password change --------------------------------------- *
 * Changing the passcode derives a NEW key (new salt), so every record already
 * encrypted under the old key must be re-encrypted, else it is orphaned. We do
 * it here in Sec35's foreground (its enccore is in main) rather than sublaunching
 * each app - their crypto lives in a secondary segment a no-globals sublaunch
 * can't load. Format + policy MUST match AddressDB35.c: misc bit 0x01, header 10,
 * A35FieldEncrypted, nonce = (recordUniqueID, fieldIndex). ToDo35, Datebook35 and
 * CryptoNote are wired the same way below (each app's own on-disk format). */
#define kBigTtl			1000000000UL
#define kNoLimit		0xFFFF		/* RecryptXxx35's `limit` param: process every record */

static Boolean A35IsEncField (UInt8 t)
{
	switch (t) {
	case 7:  case 8:  case 9:  case 10: case 11:	/* phone email IM URL address */
	case 15: case 16:								/* note custom */
	case 17: case 18: case 19: case 20:				/* city state zip country */
	case 21: case 22: case 23: case 25:				/* social gender pronouns other */
		return true;
	default:
		return false;
	}
}

/* ---- progress bar, drawn INTO a real modal form (frmProgress) so the window
 * manager saves the pixels behind it and restores them on erase -> no leftover
 * artifacts. The loops below are tight synchronous loops with no event pump, so we
 * init/draw/erase the form by hand; coords are form-relative (156-wide form). ---- */
/* Ticks of the last EvtResetAutoOffTimer() call, reset at the top of every
 * ProgressBegin so the first ProgressBar of a fresh rekey/wipe pass always
 * fires immediately rather than possibly waiting up to the throttle interval
 * left over from an earlier, unrelated pass. */
static UInt32 gProgLastAutoOffTick;

static void ProgressBar (UInt16 done, UInt16 total)
{
	RectangleType	bar, fill;
	Int16			w = total ? (Int16) ((UInt32) 142 * done / total) : 0;

	/* Address35's own A35ProgressBar (AddrCard.c) had a real device confirm a
	 * large multi-record operation with no auto-off-timer reset can reach the
	 * system's sleep timeout mid-pass and fatal-exception - same shared-key
	 * suite, same class of long synchronous DmWrite loop, so this pass is
	 * equally exposed even though it's called once per RECORD (not once per
	 * ~256 bytes like the vCard stream reader) - a rekey over a large DB can
	 * still be many seconds. Matches Address35's final, confirmed-working
	 * shape exactly: throttled to the system's 30s minimum sleep timeout / 2
	 * (once per 15s, not on every call - the trap itself, not just checking
	 * the clock, has real cost multiplied across hundreds of records), and a
	 * plain unconditional event drain (EvtGetEvent(0), never FrmDispatchEvent
	 * or special-cased on appStopEvent - Address35 tried special-casing
	 * appStopEvent here first and it was a real regression, reverted after
	 * comparing against CryptoNote's own equivalent progress callback, which
	 * doesn't do that either). This function has ZERO interaction with the
	 * rekey/crash-safe-resume checkpoint logic in RecryptAddress35 and
	 * friends (DmWrite/gRekeyIncomplete/the salt-checkpoint commit sequence)
	 * - it only reads local ticks and drains the event queue without
	 * dispatching anything, so it can't re-enter any of that code mid-record. */
	{
		UInt32 now = TimGetTicks ();
		if (now - gProgLastAutoOffTick >= (UInt32) SysTicksPerSecond () * 15) {
			EvtResetAutoOffTimer ();
			gProgLastAutoOffTick = now;
		}
	}
	{
		EventType ev;
		while (EvtGetEvent (&ev, 0), ev.eType != nilEvent) { /* drain, don't dispatch */ }
	}

	bar.topLeft.x = 6; bar.topLeft.y = 30; bar.extent.x = 144; bar.extent.y = 10;
	WinDrawRectangleFrame (simpleFrame, &bar);
	if (w > 0) {
		fill.topLeft.x = 7; fill.topLeft.y = 31; fill.extent.x = w; fill.extent.y = 8;
		WinDrawRectangle (&fill, 0);
	}
}

/* Stand up the progress form (title in the title bar; optional centered body line
 * above the bar) and return it; *prevOut = the form to restore. FrmInitForm on a
 * MODAL form saves the bits behind it, FrmEraseForm restores them (no artifacts). */
static FormPtr ProgressBegin (const char* title, const char* body, FormPtr* prevOut)
{
	FormPtr prev = FrmGetActiveForm ();
	FormPtr prog = FrmInitForm (frmProgress);

	gProgLastAutoOffTick = 0;		/* forces an immediate first reset in ProgressBar */
	FrmSetActiveForm (prog);
	FrmSetTitle (prog, (Char*) title);
	FrmDrawForm (prog);
	if (body && body[0]) {
		FontID old = FntSetFont (stdFont);
		Int16  w   = FntCharsWidth (body, (Int16) StrLen (body));
		Int16  x   = (Int16) ((156 - w) / 2);
		if (x < 2) x = 2;
		WinDrawChars (body, (Int16) StrLen (body), x, 16);
		FntSetFont (old);
	}
	ProgressBar (0, 1);
	*prevOut = prev;
	return prog;
}

static void ProgressEnd (FormPtr prog, FormPtr prev)
{
	FrmEraseForm (prog);				/* restores the pixels behind -> no artifacts */
	FrmSetActiveForm (prev);
	FrmDeleteForm (prog);
}

/* ---- memory-pressure pre-flight check ------------------------------------------
 * Each RecryptXxx35 CHANGE/SET/CLEAR record operation below needs a temporary
 * scratch buffer roughly the record's own size (see their tmp/out MemPtrNew calls).
 * Under memory pressure (many X-Master hacks resident -> less free dynamic heap,
 * more fragmentation) that allocation can fail partway through a rekey pass; the
 * per-record loops already skip a record they can't allocate for rather than
 * corrupting it, but skipping still leaves a MIXED-KEY database (some records under
 * the old key, some under the new) once the session's cached key has moved on to
 * the new one -- exactly the state that produced "hangs"/heap corruption/unreadable
 * data reported earlier. Refuse to even START the pass if there isn't a solid
 * safety margin, so this can never happen in the first place. */

/* Largest single record size across every record in `type`/`creator`'s DB (0 if the
 * DB doesn't exist -- nothing to rekey there, doesn't gate the check). */
static UInt32 DbMaxRecordBytes (UInt32 type, UInt32 creator)
{
	DmOpenRef	db = DmOpenDatabaseByTypeCreator (type, creator, dmModeReadOnly);
	UInt32		maxSize = 0;
	UInt16		i, n;

	if (! db) return 0;
	n = DmNumRecords (db);
	for (i = 0; i < n; i++)
		{
		MemHandle h = DmQueryRecord (db, i);
		if (h)
			{
			UInt32 sz = (UInt32) MemHandleSize (h);
			if (sz > maxSize) maxSize = sz;
			}
		}
	DmCloseDatabase (db);
	return maxSize;
}

/* The largest single free CONTIGUOUS chunk on the dynamic heap -- what actually
 * bounds whether ONE MemPtrNew(size) call can succeed (total free bytes can be much
 * larger than this if the heap is fragmented). */
static UInt32 DynamicHeapLargestFreeChunk (void)
{
	UInt16 i, n = MemNumHeaps (0);
	for (i = 0; i < n; i++)
		{
		UInt16 id = MemHeapID (0, i);
		if (MemHeapDynamic (id))
			{
			UInt32 freeBytes = 0, maxChunk = 0;
			MemHeapFreeBytes (id, &freeBytes, &maxChunk);
			return maxChunk;
			}
		}
	return 0;
}

/* Is there enough headroom to safely rekey every app's data? Compares against the
 * SINGLE LARGEST record across all four apps (records are rekeyed one at a time,
 * each needing its own temp buffer, freed before the next) plus a flat safety
 * margin for everything else competing for the same heap (form loading, key
 * derivation, the password field masking, ...). */
static Boolean EnoughMemoryForRekey (void)
{
	UInt32 need = DbMaxRecordBytes ('DATA', 'Ad35');
	UInt32 t;
	t = DbMaxRecordBytes ('DATA', 'Td35'); if (t > need) need = t;
	t = DbMaxRecordBytes ('DATA', 'Db35'); if (t > need) need = t;
	t = DbMaxRecordBytes ('Data', 'CrNt'); if (t > need) need = t;
	return (Boolean) (DynamicHeapLargestFreeChunk () > need + 4096UL);
}

/* Re-crypt every applicable Address35 record. Three modes:
 *   oldK, newK : re-encrypt old->new         (password CHANGE) - acts on encrypted recs
 *   NULL, newK : encrypt plaintext -> new     (password SET)    - acts on plaintext recs
 *   oldK, NULL : decrypt + clear the enc bit   (password CLEAR)  - acts on encrypted recs
 * Nonce per field = (recordUniqueID, fieldIndex). Progress shown in frmProgress. */
/* `limit` bounds how many records (from index 0) to process -- pass 0xFFFF for "all
 * of them" (the normal forward/backward pass). Returns the index reached: n (all
 * done) if it completed, or the specific record's index if it had to stop there
 * (gRekeyIncomplete) -- callers use that as `limit` for a RANGE-BOUNDED rollback
 * pass, so a rollback only ever touches records the FAILED pass actually reached
 * (touching anything past that would corrupt records that were never converted in
 * the first place -- see the caller in PwOk). */
static UInt16 RecryptAddress35 (const UInt8* oldK, const UInt8* newK, UInt16 limit)
{
	DmOpenRef	db;
	UInt16		i, n, changed = 0;
	FormPtr		prev, prog;
	const char*	title = ! newK ? "Removing encryption"
					  : ! oldK ? "Encrypting" : "Re-encrypting";

	db = DmOpenDatabaseByTypeCreator ('DATA', 'Ad35', dmModeReadWrite);
	if (! db) return 0;
	n = DmNumRecords (db);
	if (limit < n) n = limit;

	prog = ProgressBegin (title, NULL, &prev);

	for (i = 0; i < n; i++)
		{
		MemHandle	h = DmGetRecord (db, i);
		UInt8*		rec;
		Boolean		enc, doit;

		ProgressBar ((UInt16) (i + 1), n);
		if (! h) continue;
		rec = (UInt8*) MemHandleLock (h);
		enc  = (Boolean) (rec[1] & 0x01);			/* a35MiscEncrypted */
		/* CHANGE/CLEAR act on encrypted records; SET acts on plaintext ones. */
		doit = oldK ? enc : (Boolean) (newK && ! enc);
		if (doit)
			{
			Int32	size = MemHandleSize (h);
			UInt8*	tmp  = (UInt8*) MemPtrNew (size);
			if (tmp)
				{
				UInt32 uid = 0;
				UInt16 count, f;
				Int32  off = 10;					/* a35HdrSize */
				Boolean valid = true;
				Int32  chkOff; UInt16 cf;
				DmRecordInfo (db, i, NULL, &uid, NULL);
				MemMove (tmp, rec, size);
				count = (UInt16) (((UInt16) tmp[2] << 8) | tmp[3]);
				/* Hardening: validate every field's [type][?][len:2] header + payload
				 * fits within `size` BEFORE touching anything. A garbage/mismatched
				 * length here (this hand-duplicated format drifting out of sync with
				 * Address35's own code -- the same class of bug the endDate fix
				 * addressed for Datebook35) would otherwise silently overflow `tmp`
				 * and corrupt the heap, potentially trashing OTHER apps' records
				 * processed later in the same rekey pass (ToDo35/Datebook35/CryptoNote
				 * run after this). Malformed records are left untouched instead. */
				chkOff = off;
				for (cf = 0; cf < count; cf++)
					{
					UInt16 ln;
					if (chkOff + 4 > size) { valid = false; break; }
					ln = (UInt16) (((UInt16) tmp[chkOff + 2] << 8) | tmp[chkOff + 3]);
					if (chkOff + 4 + ln > size) { valid = false; break; }
					chkOff += 4 + ln;
					}
				if (valid)
					{
					for (f = 0; f < count; f++)
						{
						UInt8  t  = tmp[off];
						UInt16 ln = (UInt16) (((UInt16) tmp[off + 2] << 8) | tmp[off + 3]);
						if (ln && A35IsEncField (t))
							{
							if (oldK) EncCrypt (oldK, tmp + off + 4, ln, uid, f);	/* -> plaintext */
							if (newK) EncCrypt (newK, tmp + off + 4, ln, uid, f);	/* -> cipher */
							}
						off += 4 + ln;
						}
					if (newK) tmp[1] |= 0x01;			/* now encrypted */
					else      tmp[1] &= (UInt8) ~0x01;	/* now plaintext */
					DmWrite (rec, 0, tmp, size);
					changed++;
					}
				MemPtrFree (tmp);
				}
			else
				gRekeyIncomplete = true;	/* allocation failed despite the pre-flight check */
			}
		MemHandleUnlock (h);
		DmReleaseRecord (db, i, doit);
		/* Stop at the FIRST failure rather than continuing through the rest of the
		 * database -- bounds how many records can ever end up mismatched against
		 * the session's active key to the smallest number possible (ideally none,
		 * given the pre-flight check, but heap fragmentation is still a heuristic). */
		if (gRekeyIncomplete) break;
		}
	(void) changed;

	ProgressEnd (prog, prev);
	DmCloseDatabase (db);
	return i;		/* n if it got through everything, else the record it stopped at */
}

/* Re-crypt every applicable ToDo35 record ('DATA'/'Td35'). Same 3 modes as
 * RecryptAddress35. ToDo's sensitive text = description + note; AES-CTR can emit 0x00
 * (which would break the record's inline NUL-terminated strings), so an ENCRYPTED
 * record stores that text length-prefixed:
 *   plaintext:  [dueDate:4][priority:1] description\0 note\0
 *   encrypted:  [dueDate:4][priority|0x40] [descLen:2][noteLen:2][encDesc][encNote]
 * So SET grows / CLEAR shrinks the record; CHANGE re-keys the payload in place. Marker
 * = priority bit 0x40. Nonce (uid,0)=desc,(uid,1)=note. Offsets MUST match ToDoDB.c
 * (era-wide DateType = 4 bytes -> priority at 4, description at 5). */
#define TD_PRI_OFF	4
#define TD_DESC_OFF	5
#define TD_ENC		0x40

/* `limit`/return value: see RecryptAddress35's comment. */
static UInt16 RecryptToDo35 (const UInt8* oldK, const UInt8* newK, UInt16 limit)
{
	DmOpenRef	db;
	UInt16		i, n, changed = 0;
	FormPtr		prev, prog;
	const char*	title = ! newK ? "Removing encryption"
					  : ! oldK ? "Encrypting" : "Re-encrypting";

	db = DmOpenDatabaseByTypeCreator ('DATA', 'Td35', dmModeReadWrite);
	if (! db) return 0;
	n = DmNumRecords (db);
	if (limit < n) n = limit;

	prog = ProgressBegin (title, NULL, &prev);

	for (i = 0; i < n; i++)
		{
		MemHandle	h = DmGetRecord (db, i);
		UInt8*		rec;
		Boolean		enc, doit;
		UInt32		uid = 0;
		Int32		recSize;

		ProgressBar ((UInt16) (i + 1), n);
		if (! h) continue;
		rec = (UInt8*) MemHandleLock (h);
		enc  = (Boolean) (rec[TD_PRI_OFF] & TD_ENC);
		doit = oldK ? enc : (Boolean) (newK && ! enc);
		recSize = MemHandleSize (h);
		DmRecordInfo (db, i, NULL, &uid, NULL);

		if (doit && oldK && newK)		/* CHANGE: re-key payload in place (same size) */
			{
			UInt8* tmp = (UInt8*) MemPtrNew (recSize);
			if (tmp) {
				UInt16 dLen = (UInt16)(((UInt16) rec[TD_DESC_OFF] << 8) | rec[TD_DESC_OFF+1]);
				UInt16 nLen = (UInt16)(((UInt16) rec[TD_DESC_OFF+2] << 8) | rec[TD_DESC_OFF+3]);
				/* Hardening: same class of check as Address35/Datebook35 -- a garbage
				 * dLen/nLen would otherwise silently overflow `tmp` and corrupt the
				 * heap for records processed later in the same rekey pass. */
				if ((Int32) (TD_DESC_OFF + 4 + dLen + nLen) <= recSize) {
					UInt8* d  = tmp + TD_DESC_OFF + 4;
					UInt8* nt = d + dLen;
					MemMove (tmp, rec, recSize);
					if (dLen) { EncCrypt (oldK, d, dLen, uid, 0); EncCrypt (newK, d, dLen, uid, 0); }
					if (nLen) { EncCrypt (oldK, nt, nLen, uid, 1); EncCrypt (newK, nt, nLen, uid, 1); }
					DmWrite (rec, 0, tmp, recSize);
					changed++;
				}
				MemPtrFree (tmp);
			}
			else
				gRekeyIncomplete = true;	/* allocation failed despite the pre-flight check */
			MemHandleUnlock (h);
			DmReleaseRecord (db, i, true);
			}
		else if (doit && newK)			/* SET: plaintext -> encrypted (grow by ~2) */
			{
			UInt16 dLen = (UInt16) StrLen ((char*)(rec + TD_DESC_OFF));
			UInt16 nOff = (UInt16)(TD_DESC_OFF + dLen + 1);
			UInt16 nLen = (nOff < recSize) ? (UInt16) StrLen ((char*)(rec + nOff)) : 0;
			Int32  outSize = TD_DESC_OFF + 4 + dLen + nLen;
			UInt8* out = (UInt8*) MemPtrNew (outSize);
			if (out) {
				MemMove (out, rec, TD_DESC_OFF);
				out[TD_PRI_OFF] |= TD_ENC;
				out[TD_DESC_OFF]   = (UInt8)(dLen >> 8); out[TD_DESC_OFF+1] = (UInt8) dLen;
				out[TD_DESC_OFF+2] = (UInt8)(nLen >> 8); out[TD_DESC_OFF+3] = (UInt8) nLen;
				MemMove (out + TD_DESC_OFF + 4, rec + TD_DESC_OFF, dLen);
				MemMove (out + TD_DESC_OFF + 4 + dLen, rec + nOff, nLen);
				if (dLen) EncCrypt (newK, out + TD_DESC_OFF + 4, dLen, uid, 0);
				if (nLen) EncCrypt (newK, out + TD_DESC_OFF + 4 + dLen, nLen, uid, 1);
				MemHandleUnlock (h);
				h = DmResizeRecord (db, i, outSize);
				if (h) { rec = MemHandleLock (h); DmWrite (rec, 0, out, outSize); MemHandleUnlock (h); }
				MemPtrFree (out);
				changed++;
				DmReleaseRecord (db, i, true);
			} else { gRekeyIncomplete = true; MemHandleUnlock (h); DmReleaseRecord (db, i, false); }
			}
		else if (doit)					/* CLEAR: encrypted -> plaintext (shrink) */
			{
			UInt16 dLen = (UInt16)(((UInt16) rec[TD_DESC_OFF] << 8) | rec[TD_DESC_OFF+1]);
			UInt16 nLen = (UInt16)(((UInt16) rec[TD_DESC_OFF+2] << 8) | rec[TD_DESC_OFF+3]);
			/* Hardening: dLen/nLen also bound a READ from `rec` below (not just the
			 * `out` allocation, which is self-consistently sized from them) -- a
			 * garbage length would over-read past `rec`'s real end. Same fix as the
			 * CHANGE branch above. */
			if ((Int32) (TD_DESC_OFF + 4 + dLen + nLen) <= recSize) {
			Int32  outSize = TD_DESC_OFF + dLen + 1 + nLen + 1;
			UInt8* out = (UInt8*) MemPtrNew (outSize);
			if (out) {
				MemMove (out, rec, TD_DESC_OFF);
				out[TD_PRI_OFF] &= (UInt8) ~TD_ENC;
				MemMove (out + TD_DESC_OFF, rec + TD_DESC_OFF + 4, dLen);
				out[TD_DESC_OFF + dLen] = 0;
				MemMove (out + TD_DESC_OFF + dLen + 1, rec + TD_DESC_OFF + 4 + dLen, nLen);
				out[TD_DESC_OFF + dLen + 1 + nLen] = 0;
				if (dLen) EncCrypt (oldK, out + TD_DESC_OFF, dLen, uid, 0);
				if (nLen) EncCrypt (oldK, out + TD_DESC_OFF + dLen + 1, nLen, uid, 1);
				MemHandleUnlock (h);
				h = DmResizeRecord (db, i, outSize);
				if (h) { rec = MemHandleLock (h); DmWrite (rec, 0, out, outSize); MemHandleUnlock (h); }
				MemPtrFree (out);
				changed++;
				DmReleaseRecord (db, i, true);
			} else { gRekeyIncomplete = true; MemHandleUnlock (h); DmReleaseRecord (db, i, false); }
			} else { MemHandleUnlock (h); DmReleaseRecord (db, i, false); }
			}
		else							/* record not in scope */
			{
			MemHandleUnlock (h);
			DmReleaseRecord (db, i, false);
			}
		/* Stop at the FIRST failure -- see RecryptAddress35's identical check. */
		if (gRekeyIncomplete) break;
		}
	(void) changed;

	ProgressEnd (prog, prev);
	DmCloseDatabase (db);
	return i;
}

/* Re-crypt every applicable Datebook35 record ('DATA'/'Db35').  Same 3 modes as the others.  A
 * packed appt is [ApptDateTimeType when : 8][ApptDBRecordFlags flags : 2][endDate?][alarm?]
 * [repeat?][exceptions?] followed by the sensitive text (description + note):
 *   plaintext:  ...prefix... [description\0 if flags.desc][note\0 if flags.note]
 *   encrypted:  ...prefix, flags.enc set... [descLen:2 BE][noteLen:2 BE][encDesc][encNote]
 * so SET grows / CLEAR shrinks / CHANGE re-keys in place.  Unlike ToDo the prefix is VARIABLE, so it
 * is computed (ApptPrefix) from the flag bits.  Struct sizes are m68k-verified (when 8, flags 2,
 * endDateOffset 2 (1 value byte + 1 pad byte), AlarmInfoType 2, RepeatInfoType 10, DateType 4).
 * flags byte 0 (offset 8) bits: when 0x80, alarm 0x40, repeat 0x20, note 0x10, exceptions 0x08,
 * description 0x04, ENCRYPTED 0x02, endDate 0x01 (added for the multi-day span feature -- the
 * 8th/last-declared bit in ApptDBRecordFlags, DateDB.h).  Nonce (uid,0)=desc, (uid,1)=note.  MUST
 * match apps/datebook35/Src/DateDB.c (ApptPrefixSize/ApptRecrypt) -- endDate checked FIRST, same
 * order as there, though only the total byte count actually matters here. */
#define DB_FLAGS_OFF	8
#define DB_F_ENDDATE	0x01
#define DB_F_ALARM		0x40
#define DB_F_REPEAT		0x20
#define DB_F_NOTE		0x10
#define DB_F_EXCEPT		0x08
#define DB_F_DESC		0x04
#define DB_ENC			0x02
#define DB_SZ_ENDDATE	2
#define DB_SZ_ALARM		2
#define DB_SZ_REPEAT	10
#define DB_SZ_DATE		4

static UInt32 ApptPrefix (const UInt8* rec)
{
	UInt32	o = 10;						/* when(8) + flags(2) */
	UInt8	f = rec[DB_FLAGS_OFF];
	if (f & DB_F_ENDDATE) o += DB_SZ_ENDDATE;
	if (f & DB_F_ALARM)  o += DB_SZ_ALARM;
	if (f & DB_F_REPEAT) o += DB_SZ_REPEAT;
	if (f & DB_F_EXCEPT)
		{
		UInt16 nExc = (UInt16)(((UInt16) rec[o] << 8) | rec[o + 1]);
		o += 2 + (UInt32) nExc * DB_SZ_DATE;
		}
	return o;
}

/* `limit`/return value: see RecryptAddress35's comment. */
static UInt16 RecryptDatebook35 (const UInt8* oldK, const UInt8* newK, UInt16 limit)
{
	DmOpenRef	db;
	UInt16		i, n, changed = 0;
	FormPtr		prev, prog;
	const char*	title = ! newK ? "Removing encryption"
					  : ! oldK ? "Encrypting" : "Re-encrypting";

	db = DmOpenDatabaseByTypeCreator ('DATA', 'Db35', dmModeReadWrite);
	if (! db) return 0;
	n = DmNumRecords (db);
	if (limit < n) n = limit;

	prog = ProgressBegin (title, NULL, &prev);

	for (i = 0; i < n; i++)
		{
		MemHandle	h = DmGetRecord (db, i);
		UInt8*		rec;
		Boolean		enc, doit, hasD, hasN;
		UInt32		uid = 0, prefix;
		Int32		recSize;
		UInt8		flags;

		ProgressBar ((UInt16) (i + 1), n);
		if (! h) continue;
		rec = (UInt8*) MemHandleLock (h);
		enc  = (Boolean) (rec[DB_FLAGS_OFF] & DB_ENC);
		doit = oldK ? enc : (Boolean) (newK && ! enc);
		recSize = MemHandleSize (h);
		DmRecordInfo (db, i, NULL, &uid, NULL);
		prefix = ApptPrefix (rec);
		flags  = rec[DB_FLAGS_OFF];
		hasD   = (Boolean) (flags & DB_F_DESC);
		hasN   = (Boolean) (flags & DB_F_NOTE);

		if (doit && oldK && newK)		/* CHANGE: re-key payload in place (same size) */
			{
			UInt8* tmp = (UInt8*) MemPtrNew (recSize);
			if (tmp) {
				UInt16 dLen = (UInt16)(((UInt16) rec[prefix] << 8) | rec[prefix + 1]);
				UInt16 nLen = (UInt16)(((UInt16) rec[prefix + 2] << 8) | rec[prefix + 3]);
				/* Hardening: same class of check as Address35/ToDo35 -- a garbage
				 * dLen/nLen (e.g. from a future format field this hand-duplicated
				 * prefix calc hasn't been updated for, same as the endDate gap this
				 * fixed) would otherwise silently overflow `tmp` and corrupt the
				 * heap for CryptoNote's records, processed right after this. */
				if ((Int32) (prefix + 4 + dLen + nLen) <= recSize) {
					UInt8* d  = tmp + prefix + 4;
					UInt8* nt = d + dLen;
					MemMove (tmp, rec, recSize);
					if (dLen) { EncCrypt (oldK, d, dLen, uid, 0); EncCrypt (newK, d, dLen, uid, 0); }
					if (nLen) { EncCrypt (oldK, nt, nLen, uid, 1); EncCrypt (newK, nt, nLen, uid, 1); }
					DmWrite (rec, 0, tmp, recSize);
					changed++;
				}
				MemPtrFree (tmp);
			}
			else
				gRekeyIncomplete = true;	/* allocation failed despite the pre-flight check */
			MemHandleUnlock (h);
			DmReleaseRecord (db, i, true);
			}
		else if (doit && newK)			/* SET: plaintext -> encrypted (grow) */
			{
			const char* desc = hasD ? (const char*)(rec + prefix) : "";
			UInt16 dLen = (UInt16) StrLen (desc);
			const char* note = hasN ? (const char*)(rec + prefix + (hasD ? dLen + 1 : 0)) : "";
			UInt16 nLen = (UInt16) StrLen (note);
			Int32  outSize = (Int32) prefix + 4 + dLen + nLen;
			UInt8* out = (UInt8*) MemPtrNew (outSize);
			if (out) {
				MemMove (out, rec, prefix);
				out[DB_FLAGS_OFF] |= DB_ENC;
				out[prefix]     = (UInt8)(dLen >> 8); out[prefix + 1] = (UInt8) dLen;
				out[prefix + 2] = (UInt8)(nLen >> 8); out[prefix + 3] = (UInt8) nLen;
				MemMove (out + prefix + 4, desc, dLen);
				MemMove (out + prefix + 4 + dLen, note, nLen);
				if (dLen) EncCrypt (newK, out + prefix + 4, dLen, uid, 0);
				if (nLen) EncCrypt (newK, out + prefix + 4 + dLen, nLen, uid, 1);
				MemHandleUnlock (h);
				h = DmResizeRecord (db, i, outSize);
				if (h) { rec = MemHandleLock (h); DmWrite (rec, 0, out, outSize); MemHandleUnlock (h); }
				MemPtrFree (out);
				changed++;
				DmReleaseRecord (db, i, true);
			} else { gRekeyIncomplete = true; MemHandleUnlock (h); DmReleaseRecord (db, i, false); }
			}
		else if (doit)					/* CLEAR: encrypted -> plaintext (shrink) */
			{
			UInt16 dLen = (UInt16)(((UInt16) rec[prefix] << 8) | rec[prefix + 1]);
			UInt16 nLen = (UInt16)(((UInt16) rec[prefix + 2] << 8) | rec[prefix + 3]);
			/* Hardening: dLen/nLen also bound a READ from `rec` below (not just the
			 * `out` allocation, which is self-consistently sized from them) -- a
			 * garbage length would over-read past `rec`'s real end. Same fix as the
			 * CHANGE branch above. */
			if ((Int32) (prefix + 4 + dLen + nLen) <= recSize) {
			Int32  outSize = (Int32) prefix + (hasD ? dLen + 1 : 0) + (hasN ? nLen + 1 : 0);
			UInt8* out = (UInt8*) MemPtrNew (outSize);
			if (out) {
				UInt32 o = prefix;
				MemMove (out, rec, prefix);
				out[DB_FLAGS_OFF] &= (UInt8) ~DB_ENC;
				if (hasD) {
					MemMove (out + o, rec + prefix + 4, dLen);
					if (dLen) EncCrypt (oldK, out + o, dLen, uid, 0);
					out[o + dLen] = 0; o += dLen + 1;
				}
				if (hasN) {
					MemMove (out + o, rec + prefix + 4 + dLen, nLen);
					if (nLen) EncCrypt (oldK, out + o, nLen, uid, 1);
					out[o + nLen] = 0; o += nLen + 1;
				}
				MemHandleUnlock (h);
				h = DmResizeRecord (db, i, outSize);
				if (h) { rec = MemHandleLock (h); DmWrite (rec, 0, out, outSize); MemHandleUnlock (h); }
				MemPtrFree (out);
				changed++;
				DmReleaseRecord (db, i, true);
			} else { gRekeyIncomplete = true; MemHandleUnlock (h); DmReleaseRecord (db, i, false); }
			} else { MemHandleUnlock (h); DmReleaseRecord (db, i, false); }
			}
		else							/* record not in scope */
			{
			MemHandleUnlock (h);
			DmReleaseRecord (db, i, false);
			}
		/* Stop at the FIRST failure -- see RecryptAddress35's identical check. */
		if (gRekeyIncomplete) break;
		}
	(void) changed;

	ProgressEnd (prog, prev);
	DmCloseDatabase (db);
	return i;
}

/* Re-crypt every applicable CryptoNote record ('Data'/'CrNt' -- CryptoNote's
 * own DB type is mixed-case 'Data', NOT 'DATA' like the other 3 apps; FourCC
 * types are case-sensitive, so using 'DATA' here made DmOpenDatabaseByTypeCreator
 * silently find nothing -- RecryptCryptoNote/WipeCryptoNoteDb were both no-ops
 * the whole time, only ever "working" because CryptoNote's OWN launch-time
 * CryptoLaunchMaintenance independently re-seals things when the app is next
 * opened, masking the failure in earlier testing. Confirmed against
 * apps/cryptonote/Database.h's `#define cryptoPadType 'Data'`.). Same 3 modes as
 * RecryptAddress35/ToDo35/Datebook35 above. CryptoNote's own on-disk format
 * (apps/cryptonote/CryptoSec35.h) is much simpler than those three: one leading
 * marker byte -- 0x80 = kCryptoFmtMarker (record has been migrated to this
 * format at all), +0x01 = kCryptoFmtWrappedBit (the rest of the record is
 * EncCrypt ciphertext of the original record blob rather than the plain blob
 * itself) -- followed by either the plain blob or its ciphertext. So this
 * re-crypts the whole post-marker blob as ONE field (nonce field index 0),
 * not per-field like the other three. A record with NO marker byte at all is
 * CryptoNote's own pre-Sec35-retrofit legacy format; SET upgrades those too
 * (adds the marker + wraps) -- same outcome CryptoNote's own CryptoSealRecord
 * produces at its own next launch, just done here immediately instead of
 * waiting for the user to reopen CryptoNote. Deliberately does NOT touch
 * CryptoNote's SD-card file encryption (a completely separate VFS-based
 * feature, opt-in per file, never routed through this device DB at all --
 * out of scope for suite-wide rekey/wipe by design). */
/* `limit`/return value: see RecryptAddress35's comment. */
static UInt16 RecryptCryptoNote (const UInt8* oldK, const UInt8* newK, UInt16 limit)
{
	DmOpenRef	db;
	UInt16		i, n, changed = 0;
	FormPtr		prev, prog;
	const char*	title = ! newK ? "Removing encryption"
					  : ! oldK ? "Encrypting" : "Re-encrypting";

	db = DmOpenDatabaseByTypeCreator ('Data', 'CrNt', dmModeReadWrite);
	if (! db) return 0;
	n = DmNumRecords (db);
	if (limit < n) n = limit;

	prog = ProgressBegin (title, NULL, &prev);

	for (i = 0; i < n; i++)
		{
		MemHandle	h = DmGetRecord (db, i);
		UInt8*		rec;
		Boolean		hasMarker, wrapped, doit;
		UInt32		uid = 0;

		ProgressBar ((UInt16) (i + 1), n);
		if (! h) continue;
		rec = (UInt8*) MemHandleLock (h);
		hasMarker = (Boolean) ((rec[0] & 0x80) != 0);
		wrapped   = (Boolean) (hasMarker && (rec[0] & 0x01));
		/* CHANGE/CLEAR act on wrapped records; SET acts on everything NOT
		 * already wrapped (marked-but-plain, or pre-migration legacy). */
		doit = oldK ? wrapped : (Boolean) (newK && ! wrapped);
		DmRecordInfo (db, i, NULL, &uid, NULL);

		if (doit && oldK && newK)		/* CHANGE: re-key in place (same size) */
			{
			UInt32	size = (UInt32) MemHandleSize (h) - 1;
			UInt8*	tmp  = (UInt8*) MemPtrNew (size);
			if (tmp) {
				MemMove (tmp, rec + 1, size);
				EncCrypt (oldK, tmp, size, uid, 0);
				EncCrypt (newK, tmp, size, uid, 0);
				DmWrite (rec, 1, tmp, size);
				MemPtrFree (tmp);
				changed++;
			}
			else
				gRekeyIncomplete = true;	/* allocation failed despite the pre-flight check */
			MemHandleUnlock (h);
			DmReleaseRecord (db, i, true);
			}
		else if (doit && newK)			/* SET: plaintext (marked or legacy) -> wrapped */
			{
			/* Same size when already marked (ciphertext length == plaintext
			 * length); grows by 1 byte when adding the marker for the first
			 * time -- DmResizeRecord to the SAME size is a safe no-op, so no
			 * separate case is needed for the two. */
			UInt32	plainSize = hasMarker ? (UInt32) MemHandleSize (h) - 1
										  : (UInt32) MemHandleSize (h);
			UInt8*	out = (UInt8*) MemPtrNew (plainSize + 1);
			if (out) {
				out[0] = 0x81;		/* kCryptoFmtMarker | kCryptoFmtWrappedBit */
				MemMove (out + 1, hasMarker ? rec + 1 : rec, plainSize);
				EncCrypt (newK, out + 1, plainSize, uid, 0);
				MemHandleUnlock (h);
				h = DmResizeRecord (db, i, plainSize + 1);
				if (h) { rec = (UInt8*) MemHandleLock (h); DmWrite (rec, 0, out, plainSize + 1); MemHandleUnlock (h); }
				MemPtrFree (out);
				changed++;
				DmReleaseRecord (db, i, true);
			} else { gRekeyIncomplete = true; MemHandleUnlock (h); DmReleaseRecord (db, i, false); }
			}
		else if (doit)					/* CLEAR: wrapped -> plaintext (marker stays, same size) */
			{
			UInt32	size = (UInt32) MemHandleSize (h) - 1;
			UInt8*	tmp  = (UInt8*) MemPtrNew (size);
			if (tmp) {
				UInt8 plainMarker = 0x80;	/* kCryptoFmtMarker, wrapped bit cleared */
				MemMove (tmp, rec + 1, size);
				EncCrypt (oldK, tmp, size, uid, 0);
				DmWrite (rec, 1, tmp, size);
				DmWrite (rec, 0, &plainMarker, 1);
				MemPtrFree (tmp);
				changed++;
			}
			else
				gRekeyIncomplete = true;	/* allocation failed despite the pre-flight check */
			MemHandleUnlock (h);
			DmReleaseRecord (db, i, true);
			}
		else							/* record not in scope */
			{
			MemHandleUnlock (h);
			DmReleaseRecord (db, i, false);
			}
		/* Stop at the FIRST failure -- see RecryptAddress35's identical check. */
		if (gRekeyIncomplete) break;
		}
	(void) changed;

	ProgressEnd (prog, prev);
	DmCloseDatabase (db);
	return i;
}

/* ---- resume-after-interrupted-CHANGE repair pass --------------------------------
 * Unlike RecryptXxx35 above (which blindly trusts the enc bit + assumes the WHOLE
 * db is on one key generation), these can't assume that -- a CHANGE cut off by a
 * reset/power-loss can leave some records under oldK and some already under newK,
 * all still just flagged "encrypted", with no bit that says which. Each Recrypt*
 * CHANGE branch above already builds a record into a scratch buffer and commits it
 * with exactly ONE DmWrite, so a given record is entirely under oldK or entirely
 * under newK, never a mix -- these decide by TRIAL: decrypt a scratch copy of the
 * record's sensitive bytes under newK first; if every checked field/blob looks like
 * plausible text (PlausibleText), the record is already on the new key, done. Else
 * try oldK the same way; if THAT looks plausible, the record was still on the old
 * key when the pass was cut off -- finish converting it now and write back. If
 * NEITHER key produces plausible content, the record is left completely untouched
 * and counted as unrecoverable: guessing would risk destroying intact ciphertext
 * under a key that just hasn't been identified yet (see the caller, ResumeOk). */

/* Wrong-key AES-CTR output is uniformly random bytes -- requiring 90% of the
 * checked prefix (up to the first NUL, or the whole field if none) to be
 * printable/common-whitespace has a negligible false-accept rate: real text
 * clears it easily, random bytes essentially never do. */
static Boolean PlausibleText (const UInt8* buf, UInt16 len)
{
	UInt16 i, printable = 0;

	if (len == 0) return true;
	for (i = 0; i < len; i++)
		{
		UInt8 c = buf[i];
		if (c == 0) break;
		if ((c >= 0x20 && c <= 0x7E) || c == '\t' || c == '\n' || c == '\r') printable++;
		}
	return (Boolean) (i == 0 || (printable * 10 >= i * 9));
}

/* Trial-decrypt Address35's encrypted fields under `key` into scratch copies (never
 * touching `tmp`) and report whether ALL of them look plausible. `count`/`tmp` as
 * already validated (structurally sound) by the caller. */
static Boolean A35TryKey (const UInt8* tmp, Int32 size, UInt16 count, const UInt8* key, UInt32 uid)
{
	Int32	off = 10;
	UInt16	f;

	for (f = 0; f < count; f++)
		{
		UInt8	t  = tmp[off];
		UInt16	ln = (UInt16) (((UInt16) tmp[off + 2] << 8) | tmp[off + 3]);
		if (ln && A35IsEncField (t))
			{
			UInt8* scratch = (UInt8*) MemPtrNew (ln);
			Boolean ok;
			if (! scratch) return false;		/* treat an allocation failure as "not proven" */
			MemMove (scratch, tmp + off + 4, ln);
			EncCrypt (key, scratch, ln, uid, f);
			ok = PlausibleText (scratch, ln);
			MemPtrFree (scratch);
			if (! ok) return false;
			}
		off += 4 + ln;
		}
	(void) size;
	return true;
}

static UInt16 RepairAddress35 (const UInt8* oldK, const UInt8* newK)
{
	DmOpenRef	db;
	UInt16		i, n, unrecoverable = 0;
	FormPtr		prev, prog;

	db = DmOpenDatabaseByTypeCreator ('DATA', 'Ad35', dmModeReadWrite);
	if (! db) return 0;
	n = DmNumRecords (db);
	prog = ProgressBegin ("Resuming re-key", NULL, &prev);

	for (i = 0; i < n; i++)
		{
		MemHandle	h = DmGetRecord (db, i);
		UInt8*		rec;
		Boolean		enc;

		ProgressBar ((UInt16) (i + 1), n);
		if (! h) continue;
		rec = (UInt8*) MemHandleLock (h);
		enc = (Boolean) (rec[1] & 0x01);

		if (enc)
			{
			Int32	size = MemHandleSize (h);
			UInt8*	tmp  = (UInt8*) MemPtrNew (size);
			if (tmp)
				{
				UInt32	uid = 0;
				UInt16	count;
				Int32	chkOff; UInt16 cf;
				Boolean structValid = true;

				DmRecordInfo (db, i, NULL, &uid, NULL);
				MemMove (tmp, rec, size);
				count = (UInt16) (((UInt16) tmp[2] << 8) | tmp[3]);

				/* Same structural bound-check as RecryptAddress35 before trusting any
				 * field offset -- headers (type/length) are never encrypted, so this
				 * passes regardless of which key is right; it only rules out garbage. */
				chkOff = 10;
				for (cf = 0; cf < count; cf++)
					{
					UInt16 ln;
					if (chkOff + 4 > size) { structValid = false; break; }
					ln = (UInt16) (((UInt16) tmp[chkOff + 2] << 8) | tmp[chkOff + 3]);
					if (chkOff + 4 + ln > size) { structValid = false; break; }
					chkOff += 4 + ln;
					}

				if (structValid)
					{
					if (! A35TryKey (tmp, size, count, newK, uid))
						{
						if (A35TryKey (tmp, size, count, oldK, uid))
							{
							/* still on the old key -- finish converting it */
							Int32	off = 10;
							UInt16	f;
							for (f = 0; f < count; f++)
								{
								UInt8	t  = tmp[off];
								UInt16	ln = (UInt16) (((UInt16) tmp[off + 2] << 8) | tmp[off + 3]);
								if (ln && A35IsEncField (t))
									{
									EncCrypt (oldK, tmp + off + 4, ln, uid, f);
									EncCrypt (newK, tmp + off + 4, ln, uid, f);
									}
								off += 4 + ln;
								}
							DmWrite (rec, 0, tmp, size);
							}
						else
							unrecoverable++;	/* neither key produces plausible content */
						}
					/* else: already on newK, nothing to do */
					}
				MemPtrFree (tmp);
				}
			}
		MemHandleUnlock (h);
		DmReleaseRecord (db, i, false);
		}

	ProgressEnd (prog, prev);
	DmCloseDatabase (db);
	return unrecoverable;
}

/* Same trial-then-finish idea for ToDo35's desc+note pair. */
static UInt16 RepairToDo35 (const UInt8* oldK, const UInt8* newK)
{
	DmOpenRef	db;
	UInt16		i, n, unrecoverable = 0;
	FormPtr		prev, prog;

	db = DmOpenDatabaseByTypeCreator ('DATA', 'Td35', dmModeReadWrite);
	if (! db) return 0;
	n = DmNumRecords (db);
	prog = ProgressBegin ("Resuming re-key", NULL, &prev);

	for (i = 0; i < n; i++)
		{
		MemHandle	h = DmGetRecord (db, i);
		UInt8*		rec;
		Boolean		enc;
		Int32		recSize;

		ProgressBar ((UInt16) (i + 1), n);
		if (! h) continue;
		rec = (UInt8*) MemHandleLock (h);
		enc = (Boolean) (rec[TD_PRI_OFF] & TD_ENC);
		recSize = MemHandleSize (h);

		if (enc)
			{
			UInt16	dLen = (UInt16)(((UInt16) rec[TD_DESC_OFF] << 8) | rec[TD_DESC_OFF+1]);
			UInt16	nLen = (UInt16)(((UInt16) rec[TD_DESC_OFF+2] << 8) | rec[TD_DESC_OFF+3]);
			if ((Int32) (TD_DESC_OFF + 4 + dLen + nLen) <= recSize)
				{
				UInt32	uid = 0;
				UInt8*	d  = rec + TD_DESC_OFF + 4;
				UInt8*	nt = d + dLen;
				UInt8*	scratch;
				Boolean	okNew, okOld;

				DmRecordInfo (db, i, NULL, &uid, NULL);

				scratch = (UInt8*) MemPtrNew ((UInt32) dLen + nLen);
				if (scratch)
					{
					MemMove (scratch, d, dLen);
					MemMove (scratch + dLen, nt, nLen);
					if (dLen) EncCrypt (newK, scratch, dLen, uid, 0);
					if (nLen) EncCrypt (newK, scratch + dLen, nLen, uid, 1);
					okNew = (Boolean) (PlausibleText (scratch, dLen) && PlausibleText (scratch + dLen, nLen));
					if (! okNew)
						{
						MemMove (scratch, d, dLen);
						MemMove (scratch + dLen, nt, nLen);
						if (dLen) EncCrypt (oldK, scratch, dLen, uid, 0);
						if (nLen) EncCrypt (oldK, scratch + dLen, nLen, uid, 1);
						okOld = (Boolean) (PlausibleText (scratch, dLen) && PlausibleText (scratch + dLen, nLen));
						if (okOld)
							{
							UInt8* tmp = (UInt8*) MemPtrNew (recSize);
							if (tmp)
								{
								UInt8* td  = tmp + TD_DESC_OFF + 4;
								UInt8* tnt = td + dLen;
								MemMove (tmp, rec, recSize);
								if (dLen) { EncCrypt (oldK, td, dLen, uid, 0); EncCrypt (newK, td, dLen, uid, 0); }
								if (nLen) { EncCrypt (oldK, tnt, nLen, uid, 1); EncCrypt (newK, tnt, nLen, uid, 1); }
								DmWrite (rec, 0, tmp, recSize);
								MemPtrFree (tmp);
								}
							}
						else
							unrecoverable++;
						}
					MemPtrFree (scratch);
					}
				}
			}
		MemHandleUnlock (h);
		DmReleaseRecord (db, i, false);
		}

	ProgressEnd (prog, prev);
	DmCloseDatabase (db);
	return unrecoverable;
}

/* Same trial-then-finish idea for Datebook35's desc+note pair (variable prefix). */
static UInt16 RepairDatebook35 (const UInt8* oldK, const UInt8* newK)
{
	DmOpenRef	db;
	UInt16		i, n, unrecoverable = 0;
	FormPtr		prev, prog;

	db = DmOpenDatabaseByTypeCreator ('DATA', 'Db35', dmModeReadWrite);
	if (! db) return 0;
	n = DmNumRecords (db);
	prog = ProgressBegin ("Resuming re-key", NULL, &prev);

	for (i = 0; i < n; i++)
		{
		MemHandle	h = DmGetRecord (db, i);
		UInt8*		rec;
		Boolean		enc;
		Int32		recSize;
		UInt32		prefix;

		ProgressBar ((UInt16) (i + 1), n);
		if (! h) continue;
		rec = (UInt8*) MemHandleLock (h);
		enc = (Boolean) (rec[DB_FLAGS_OFF] & DB_ENC);
		recSize = MemHandleSize (h);
		prefix = ApptPrefix (rec);

		if (enc)
			{
			UInt16	dLen = (UInt16)(((UInt16) rec[prefix] << 8) | rec[prefix + 1]);
			UInt16	nLen = (UInt16)(((UInt16) rec[prefix + 2] << 8) | rec[prefix + 3]);
			if ((Int32) (prefix + 4 + dLen + nLen) <= recSize)
				{
				UInt32	uid = 0;
				UInt8*	d  = rec + prefix + 4;
				UInt8*	nt = d + dLen;
				UInt8*	scratch;
				Boolean	okNew, okOld;

				DmRecordInfo (db, i, NULL, &uid, NULL);

				scratch = (UInt8*) MemPtrNew ((UInt32) dLen + nLen);
				if (scratch)
					{
					MemMove (scratch, d, dLen);
					MemMove (scratch + dLen, nt, nLen);
					if (dLen) EncCrypt (newK, scratch, dLen, uid, 0);
					if (nLen) EncCrypt (newK, scratch + dLen, nLen, uid, 1);
					okNew = (Boolean) (PlausibleText (scratch, dLen) && PlausibleText (scratch + dLen, nLen));
					if (! okNew)
						{
						MemMove (scratch, d, dLen);
						MemMove (scratch + dLen, nt, nLen);
						if (dLen) EncCrypt (oldK, scratch, dLen, uid, 0);
						if (nLen) EncCrypt (oldK, scratch + dLen, nLen, uid, 1);
						okOld = (Boolean) (PlausibleText (scratch, dLen) && PlausibleText (scratch + dLen, nLen));
						if (okOld)
							{
							UInt8* tmp = (UInt8*) MemPtrNew (recSize);
							if (tmp)
								{
								UInt8* td  = tmp + prefix + 4;
								UInt8* tnt = td + dLen;
								MemMove (tmp, rec, recSize);
								if (dLen) { EncCrypt (oldK, td, dLen, uid, 0); EncCrypt (newK, td, dLen, uid, 0); }
								if (nLen) { EncCrypt (oldK, tnt, nLen, uid, 1); EncCrypt (newK, tnt, nLen, uid, 1); }
								DmWrite (rec, 0, tmp, recSize);
								MemPtrFree (tmp);
								}
							}
						else
							unrecoverable++;
						}
					MemPtrFree (scratch);
					}
				}
			}
		MemHandleUnlock (h);
		DmReleaseRecord (db, i, false);
		}

	ProgressEnd (prog, prev);
	DmCloseDatabase (db);
	return unrecoverable;
}

/* Same trial-then-finish idea for CryptoNote's single whole-blob field. */
static UInt16 RepairCryptoNote (const UInt8* oldK, const UInt8* newK)
{
	DmOpenRef	db;
	UInt16		i, n, unrecoverable = 0;
	FormPtr		prev, prog;

	db = DmOpenDatabaseByTypeCreator ('Data', 'CrNt', dmModeReadWrite);
	if (! db) return 0;
	n = DmNumRecords (db);
	prog = ProgressBegin ("Resuming re-key", NULL, &prev);

	for (i = 0; i < n; i++)
		{
		MemHandle	h = DmGetRecord (db, i);
		UInt8*		rec;
		Boolean		wrapped;
		UInt32		uid = 0;

		ProgressBar ((UInt16) (i + 1), n);
		if (! h) continue;
		rec = (UInt8*) MemHandleLock (h);
		wrapped = (Boolean) (((rec[0] & 0x80) != 0) && (rec[0] & 0x01));

		if (wrapped)
			{
			UInt32	size = (UInt32) MemHandleSize (h) - 1;
			UInt8*	scratch = (UInt8*) MemPtrNew (size);
			DmRecordInfo (db, i, NULL, &uid, NULL);
			if (scratch)
				{
				Boolean okNew, okOld;
				MemMove (scratch, rec + 1, size);
				EncCrypt (newK, scratch, size, uid, 0);
				okNew = PlausibleText (scratch, (UInt16) size);
				if (! okNew)
					{
					MemMove (scratch, rec + 1, size);
					EncCrypt (oldK, scratch, size, uid, 0);
					okOld = PlausibleText (scratch, (UInt16) size);
					if (okOld)
						{
						UInt8* tmp = (UInt8*) MemPtrNew (size);
						if (tmp)
							{
							MemMove (tmp, rec + 1, size);
							EncCrypt (oldK, tmp, size, uid, 0);
							EncCrypt (newK, tmp, size, uid, 0);
							DmWrite (rec, 1, tmp, size);
							MemPtrFree (tmp);
							}
						}
					else
						unrecoverable++;
					}
				MemPtrFree (scratch);
				}
			}
		MemHandleUnlock (h);
		DmReleaseRecord (db, i, false);
		}

	ProgressEnd (prog, prev);
	DmCloseDatabase (db);
	return unrecoverable;
}

/* ---- security wipe: delete every ENCRYPTED record across the era apps, then
 * revert to no-password. Used by "Forget Password" and the 10th-wrong-attempt
 * auto-wipe. Covers all three era apps that store encrypted records: Address35
 * ('DATA'/'Ad35'), ToDo35 ('DATA'/'Td35') and Datebook35 ('DATA'/'Db35').
 * Records are removed (not decrypted) because without the passcode they can't be
 * read. Shows the "Security Wipe" progress, which auto-dismisses when done. ---- */
/* Delete every record whose marker byte (off & mask) is set from one DB. */
static void WipeEncDb (UInt32 type, UInt32 creator, UInt16 off, UInt8 mask)
{
	DmOpenRef	db = DmOpenDatabaseByTypeCreator (type, creator, dmModeReadWrite);
	FormPtr		prev, prog;
	UInt16		i, n;

	if (! db) return;
	n = DmNumRecords (db);
	prog = ProgressBegin ("Security Wipe", "Removing all encrypted records", &prev);
	for (i = n; i > 0; ) {				/* top-down so removals don't shift lower i */
		MemHandle	h;
		Boolean		enc = false;

		i--;
		ProgressBar ((UInt16) (n - i), n ? n : 1);
		h = DmQueryRecord (db, i);
		if (h) {
			const UInt8* rec = (const UInt8*) MemHandleLock (h);
			enc = (Boolean) (rec[off] & mask);
			MemHandleUnlock (h);
		}
		if (enc) DmRemoveRecord (db, i);
	}
	ProgressEnd (prog, prev);
	DmCloseDatabase (db);
}

/* CryptoNote-specific: unlike the other 3 apps' single-bit "encrypted" flag,
 * CryptoNote's marker byte packs TWO bits -- 0x80 (migrated at all) and 0x01
 * (actually wrapped). A marked-but-plain record (0x80 set, 0x01 clear) must
 * NOT be wiped, but WipeEncDb's plain bitmask-truthy check (rec[off] & mask)
 * can't tell that apart from a wrapped one (0x81) -- 0x80 alone is already
 * truthy against either an 0x01 or an 0x81 mask. Needs its own exact-match
 * check instead of reusing WipeEncDb. Only ever opens the CryptoPad DEVICE
 * database by type+creator ('Data'/'CrNt' -- mixed-case type, see
 * RecryptCryptoNote's comment) -- never touches CryptoNote's SD-card
 * file encryption (a separate VFS feature, not a DmOpenDatabaseByTypeCreator
 * database at all), so a connected card's encrypted files are structurally
 * untouched by this, same as by WipeEncDb for the other 3 apps. */
static void WipeCryptoNoteDb (void)
{
	DmOpenRef	db = DmOpenDatabaseByTypeCreator ('Data', 'CrNt', dmModeReadWrite);
	FormPtr		prev, prog;
	UInt16		i, n;

	if (! db) return;
	n = DmNumRecords (db);
	prog = ProgressBegin ("Security Wipe", "Removing all encrypted records", &prev);
	for (i = n; i > 0; ) {				/* top-down so removals don't shift lower i */
		MemHandle	h;
		Boolean		wrapped = false;

		i--;
		ProgressBar ((UInt16) (n - i), n ? n : 1);
		h = DmQueryRecord (db, i);
		if (h) {
			const UInt8* rec = (const UInt8*) MemHandleLock (h);
			wrapped = (Boolean) ((rec[0] & 0x81) == 0x81);
			MemHandleUnlock (h);
		}
		if (wrapped) DmRemoveRecord (db, i);
	}
	ProgressEnd (prog, prev);
	DmCloseDatabase (db);
}

static void WipeEncryptedRecords (void)
{
	WipeEncDb ('DATA', 'Ad35', 1, 0x01);				/* Address35: misc bit 0x01 */
	WipeEncDb ('DATA', 'Td35', TD_PRI_OFF, TD_ENC);		/* ToDo35: priority bit 0x40  */
	WipeEncDb ('DATA', 'Db35', DB_FLAGS_OFF, DB_ENC);	/* Datebook35: flags.encrypted 0x02 */
	WipeCryptoNoteDb ();								/* CrNt: own function, see comment above */
}

/* Full "forget password" / lockout wipe: delete encrypted data + revert to
 * unassigned + clear the lockout counters. */
static void SecurityWipe (void)
{
	WipeEncryptedRecords ();
	gPref.passwordSet = false;
	MemSet (gPref.salt, ENC_SALT_BYTES, 0);
	MemSet (gPref.verifier, 16, 0);
	gPref.failCount  = 0;
	gPref.delayUntil = 0;
	SavePrefs ();
	EncSetEncryptionActive (false);	/* wiped -> encryption inactive */
	EncWipeKey ();
	CancelAlarm ();
}

/* ---- wrong-password lockout ---------------------------------------------- *
 * Escalating delay after 3 free tries; the 10th wrong attempt wipes + resets. */

/* Delay (seconds) imposed AFTER the Nth consecutive wrong attempt. 1-3: none. */
static UInt32 DelayForAttempt (UInt16 n)
{
	switch (n) {
	case 4:  return 60UL;			/* 1 minute  */
	case 5:  return 300UL;			/* 5 minutes */
	case 6:  return 900UL;			/* 15 minutes */
	case 7:  return 3600UL;			/* 1 hour    */
	case 8:  return 10800UL;		/* 3 hours   */
	case 9:  return 28800UL;		/* 8 hours   */
	default: return 0;				/* 1-3: none; 10+ handled by the wipe path */
	}
}

/* "HH:MM:SS" (zero-padded; hours clamped to 99). out >= 9 bytes. */
static void FormatHMS (UInt32 secs, char* out)
{
	UInt16 h = (UInt16) (secs / 3600UL);
	UInt16 m = (UInt16) ((secs % 3600UL) / 60UL);
	UInt16 s = (UInt16) (secs % 60UL);

	if (h > 99) h = 99;
	out[0] = (char) ('0' + (h / 10) % 10);	out[1] = (char) ('0' + h % 10);	out[2] = ':';
	out[3] = (char) ('0' + (m / 10) % 10);	out[4] = (char) ('0' + m % 10);	out[5] = ':';
	out[6] = (char) ('0' + (s / 10) % 10);	out[7] = (char) ('0' + s % 10);	out[8] = 0;
}

/* Modal countdown dialog. title/line1 are set here; the [HH:MM:SS] line is redrawn
 * every second until the user taps OK or (if a delay was active) it hits 0 and
 * auto-dismisses. attempt>0 -> "Attempt N. Delay HH:MM:SS."; else just the clock. */
static void ShowDelayDialog (const char* title, const char* line1,
                             UInt16 attempt, UInt32 untilSecs)
{
	FormPtr	prev = FrmGetActiveForm ();
	FormPtr	frm  = FrmInitForm (frmDelay);
	Boolean	autoDismiss = (Boolean) (untilSecs > TimGetSeconds ());
	FontID	old;

	FrmSetActiveForm (frm);
	FrmSetTitle (frm, (Char*) title);
	FrmDrawForm (frm);
	old = FntSetFont (boldFont);			/* headline (e.g. "Incorrect password.") bold */
	WinDrawChars (line1, (Int16) StrLen (line1), 8, 16);
	FntSetFont (old);

	for (;;) {
		EventType		e;
		UInt32			now = TimGetSeconds ();
		UInt32			rem = (untilSecs > now) ? (untilSecs - now) : 0;
		char			line2[48], hms[12];
		RectangleType	r;

		FormatHMS (rem, hms);
		if (attempt) {
			StrCopy (line2, "Attempt ");
			StrIToA (line2 + StrLen (line2), attempt);
			StrCat (line2, ". Delay ");
			StrCat (line2, hms);
			StrCat (line2, ".");
		} else {
			StrCopy (line2, hms);
		}
		r.topLeft.x = 6; r.topLeft.y = 28; r.extent.x = 148; r.extent.y = 12;
		WinEraseRectangle (&r, 0);
		old = FntSetFont (stdFont);
		WinDrawChars (line2, (Int16) StrLen (line2), 8, 30);
		FntSetFont (old);

		if (autoDismiss && rem == 0) break;			/* delay elapsed -> dismiss */

		EvtGetEvent (&e, SysTicksPerSecond ());		/* ~1s tick for the countdown */
		if (e.eType == appStopEvent) { EvtAddEventToQueue (&e); break; }
		if (! SysHandleEvent (&e))
			FrmDispatchEvent (&e);
		if (e.eType == ctlSelectEvent && e.data.ctlSelect.controlID == btnDelayOk)
			break;
	}
	FrmEraseForm (frm);
	FrmSetActiveForm (prev);
	FrmDeleteForm (frm);
}

/* Before a password attempt: if a lockout delay is active, show the block dialog
 * and refuse (return false). */
static Boolean AttemptAllowed (void)
{
	if (gPref.delayUntil && TimGetSeconds () < gPref.delayUntil) {
		ShowDelayDialog ("Unlock Security Delay", "Delay active.", 0, gPref.delayUntil);
		return false;
	}
	return true;
}

/* A correct password clears the failure counter + any delay. */
static void OnCorrectPassword (void)
{
	if (gPref.failCount || gPref.delayUntil) {
		gPref.failCount  = 0;
		gPref.delayUntil = 0;
		SavePrefs ();
	}
}

/* A wrong password escalates: count it, set the next delay, and show the
 * "Security35" alert with the attempt number + countdown. The 10th wipes + resets. */
static void OnWrongPassword (void)
{
	UInt32 delay;

	gPref.failCount++;
	if (gPref.failCount >= 10) {
		SecurityWipe ();				/* delete encrypted data + unassign password */
		SysReset ();					/* soft reset (does not return) */
		return;
	}
	delay = DelayForAttempt (gPref.failCount);
	gPref.delayUntil = delay ? (TimGetSeconds () + delay) : 0;
	SavePrefs ();
	ShowDelayDialog ("Security35", "Incorrect password.", gPref.failCount, gPref.delayUntil);
}

/* ---- main form ---- */

/* "Locking in HH:MM" (>=1hr remaining) / "Locking in MM:SS" (<1hr), blank
 * when locked or no password set. Re-arms gCountdownRefreshUntilTicks so
 * PilotMain's event loop calls this again in ~kCountdownRefreshSecs even if
 * nothing else happens -- a live countdown, not a static snapshot. Minute/
 * second resolution only (no need for the sub-second precision EncGetKey's
 * own TTL check uses internally). */
static void RefreshCountdown (void)
{
	Int32	remain = gPref.passwordSet ? EncSecondsRemaining (TtlSeconds ()) : -1;
	Char	buf[32];

	if (remain < 0)
		{
		FrmCopyLabel (FrmGetActiveForm (), lblCountdown, "");
		gCountdownRefreshUntilTicks = 0;
		return;
		}
	if (remain >= 3600)
		StrPrintF (buf, "Locking in %ld:%02ld", remain / 3600, (remain / 60) % 60);
	else
		StrPrintF (buf, "Locking in %ld:%02ld", remain / 60, remain % 60);
	FrmCopyLabel (FrmGetActiveForm (), lblCountdown, buf);
	/* Next wake-up: whichever is SOONER, the normal refresh cadence or the
	 * exact moment this TTL actually expires -- so the Session status/
	 * Lock-Unlock button (refreshed alongside this, see RefreshMain) flips
	 * to Locked right when it truly expires instead of lagging up to
	 * kCountdownRefreshSecs behind the real deadline. */
	{
	UInt32 untilExpiry = (UInt32) remain + 1;		/* +1: land just past expiry, not exactly at it */
	UInt32 next = (untilExpiry < kCountdownRefreshSecs) ? untilExpiry : kCountdownRefreshSecs;
	gCountdownRefreshUntilTicks = TimGetTicks () + SysTicksPerSecond () * next;
	}
}

static void RefreshMain (void)
{
	FormType* frm = FrmGetActiveForm ();
	Boolean   set = (Boolean) gPref.passwordSet;

	/* the "Password:" selector shows assignment state, ROM Security style. */
	CtlSetLabel (GetObj (pwSelector), set ? "Assigned" : "-Unassigned-");
	/* the bottom button LOCKS when unlocked and UNLOCKS (prompts) when locked. */
	CtlSetLabel (GetObj (btnLock), (set && ! EncKeyPresent ()) ? "Unlock" : "Lock");
	FrmCopyLabel (frm, lblStatus,
		! set ? "No password" : (EncKeyPresent () ? "Unlocked" : "Locked"));
	RefreshCountdown ();
}

#define kTtlFieldW	20		/* fldTtl width from the resource */

/* Set the auto-lock value, visually CENTERED in its field. Palm single-line
 * fields don't implement centerAlign (it renders as left; only left/right work),
 * so we emulate centering by left-padding the number with spaces computed from
 * the stdFont metrics. The field's own underline still spans the full slot, and
 * StrAToI on read skips the leading spaces. Applied on every display -> always
 * centered. */
static void SetTtlField (UInt16 v)
{
	FieldPtr	fld = GetObj (fldTtl);
	MemHandle	h;
	char*		p;
	char		num[8], buf[14];
	UInt16		i = 0;
	Int16		tw, sw, n;
	FontID		old;

	StrIToA (num, v);
	old = FntSetFont (stdFont);
	tw = FntCharsWidth (num, StrLen (num));
	sw = FntCharWidth (' ');
	FntSetFont (old);
	n = (sw > 0 && tw < kTtlFieldW) ? (Int16)(((kTtlFieldW - tw) / 2) / sw) : 0;
	while (i < (UInt16) n && i < sizeof (buf) - 6) buf[i++] = ' ';
	StrCopy (&buf[i], num);

	FldSetTextHandle (fld, NULL);		/* release any prior handle */
	h = MemHandleNew (StrLen (buf) + 1);
	if (! h) return;
	p = MemHandleLock (h);
	StrCopy (p, buf);
	MemHandleUnlock (h);
	FldSetTextHandle (fld, h);
}

static void InitMainForm (void)
{
	SetTtlField (gPref.ttlValue);
	CtlSetLabel (GetObj (popTtlUnit), gUnitNames[gPref.ttlUnit <= 2 ? gPref.ttlUnit : 0]);
	CtlSetValue (GetObj (chkLockOnSleep), gPref.lockOnSleep ? 1 : 0);
	RefreshMain ();
}

/* Any change to the auto-lock timeout WIPES the cached key, so the passcode is
 * re-prompted on next access - a retimed window must not silently inherit the
 * current unlocked session. Also republishes the new TTL for every app's
 * EncKeyPresent self-check (see enccore.h) -- must happen for EVERY real
 * change, not just ones made through this exact dialog. */
static void TtlSettingChanged (void)
{
	EncWipeKey ();
	CancelAlarm ();
	EncSetTtlSeconds (TtlSeconds ());
}

static void SaveTtlFromUI (void)
{
	Char* p = FldGetTextPtr (GetObj (fldTtl));
	Int16 v;
	if (p) while (*p == ' ') p++;		/* skip the centering pad (StrAToI won't) */
	v = p ? StrAToI (p) : 0;
	if (v < 1)    v = 1;
	if (v > 9999) v = 9999;
	/* Unchanged: leave the session exactly as it was. Deliberately does NOT
	 * re-arm the alarm here (a bug this comment replaces) -- doing so on every
	 * close of this screen reset the auto-lock deadline to "now + ttl" even
	 * though nothing was touched, silently extending the session just from
	 * opening and closing Settings. If the key is still genuinely valid, its
	 * alarm is already correctly scheduled from the real unlock time and
	 * doesn't need touching; if it already expired, EncKeyPresent's own TTL
	 * self-check (enccore.h) catches that on the next access regardless of
	 * the alarm's state. */
	if ((UInt16) v == gPref.ttlValue)
		return;
	gPref.ttlValue = (UInt16) v;
	SavePrefs ();
	TtlSettingChanged ();						/* timeout changed -> lock */
}

static Boolean MainFormHandleEvent (EventType* e)
{
	Boolean handled = false;

	switch (e->eType)
		{
		case frmOpenEvent:
			InitMainForm ();
			FrmDrawForm (FrmGetActiveForm ());
			/* A CHANGE was cut short by a reset last session -- resolve that BEFORE
			 * anything else (including the unlock-handoff prompt below: unlocking
			 * under the stale old-or-mixed state doesn't make sense until this
			 * completes or the user chooses to leave it pending by cancelling). */
			if (gPref.rekeyPending)
				{ gPwMode = PW_RESUME; FrmPopupForm (frmResume); }
			/* launched to unlock for another app: go straight to the prompt */
			else if (gUnlockReturn && gPref.passwordSet && ! EncKeyPresent ())
				FrmPopupForm (frmUnlock);
			handled = true;
			break;

		case frmUpdateEvent:
			RefreshMain ();
			FrmDrawForm (FrmGetActiveForm ());
			handled = true;
			break;

		case ctlSelectEvent:
			switch (e->data.ctlSelect.controlID)
				{
				case pwSelector:
					/* tapping the password box: set if none, else manage it */
					if (gPref.passwordSet)
						{ gPwMode = PW_CHANGE; FrmPopupForm (frmPwChange); }
					else
						{ gPwMode = PW_SET;    FrmPopupForm (frmPwSet); }
					handled = true;
					break;
				case btnLock:
					if (gPref.passwordSet && ! EncKeyPresent ())
						{				/* Unlock -> dedicated password prompt */
						FrmPopupForm (frmUnlock);
						}
					else				/* Lock -> drop the session key now */
						{
						EncWipeKey ();
						CancelAlarm ();
						RefreshMain ();
						FrmDrawForm (FrmGetActiveForm ());
						}
					handled = true;
					break;
				case btnHelp:
					FrmCustomAlert (altHelp, "", "", "");
					handled = true;
					break;
				case chkLockOnSleep:
					gPref.lockOnSleep = (UInt8) e->data.ctlSelect.on;
					SavePrefs ();
					EncSetLockOnSleep ((Boolean) gPref.lockOnSleep);
					handled = true;
					break;
				}
			break;

		case popSelectEvent:
			if (e->data.popSelect.listID == lstTtlUnit)
				{
				UInt8 newUnit = (UInt8) e->data.popSelect.selection;
				CtlSetLabel (GetObj (popTtlUnit), gUnitNames[newUnit <= 2 ? newUnit : 0]);
				if (newUnit != gPref.ttlUnit)		/* unit changed -> lock */
					{
					gPref.ttlUnit = newUnit;
					SavePrefs ();
					TtlSettingChanged ();
					RefreshMain ();
					FrmDrawForm (FrmGetActiveForm ());
					}
				handled = true;
				}
			break;

		case menuEvent:
			if (e->data.menu.itemID == mniPrefs)
				{
				FrmPopupForm (frmPrefs);
				handled = true;
				}
			else if (e->data.menu.itemID == mniForget)
				{
				/* Forget Password: confirm, then wipe all encrypted data and revert
				 * to unassigned. Button 1 = "Yes! I really forgot!". */
				if (FrmAlert (altForget) == 1)
					{
					SecurityWipe ();
					RefreshMain ();
					FrmDrawForm (FrmGetActiveForm ());
					}
				handled = true;
				}
			break;

		case frmCloseEvent:
			SaveTtlFromUI ();
			break;

		default:
			break;
		}
	return handled;
}

/* ---- password dialog ---- */

/* The real (unmasked) buffer backing password field `id` -- see the masking comment
 * above gPwCurrent. NULL if `id` isn't one of the 4 password fields. */
static char* RealPwBuf (UInt16 id)
{
	if (id == fldCurrent)  return gPwCurrent;
	if (id == fldNew)      return gPwNew;
	if (id == fldConfirm)  return gPwConfirm;
	if (id == fldUnlockPw) return gPwUnlock;
	return NULL;
}

static Char* FieldText (UInt16 id)
{
	char* p = RealPwBuf (id);
	return p ? p : (Char*) "";
}

#define kRevealTicks	1	/* seconds a just-typed character stays in clear */

/* Redraw field `id`'s VISIBLE text from its real buffer, masked with '*' -- the last
 * character stays in clear if revealLast (Text Entry: just-typed char), matching a
 * modern phone keyboard. Does not touch the real buffer. */
static void RedrawMaskedField (UInt16 id, Boolean revealLast)
{
	char*	real = RealPwBuf (id);
	UInt16	len, i;
	Char	disp[kPwBufLen];

	if (! real) return;
	len = (UInt16) StrLen (real);
	for (i = 0; i < len; i++)
		disp[i] = (Char) ((revealLast && i == len - 1) ? real[i] : '*');
	disp[len] = 0;
	{
	FieldType* fld = (FieldType*) GetObj (id);
	if (! fld) return;		/* id isn't in the current active form -- nothing to redraw */
	FldSetContent (fld, disp);
	FldSetInsPtPosition (fld, len);
	FldDrawField (fld);
	}
}

/* Mask whatever field is currently showing a revealed character -- called from
 * PilotMain's event loop once its reveal window has elapsed. */
static void MaskPendingReveal (void)
{
	if (gRevealField)
		RedrawMaskedField (gRevealField, false);
	gRevealField = 0;
	gRevealUntilTicks = 0;
}

/* Text Entry mode keystroke handling for a password field: printable characters are
 * appended (masked display, last char revealed briefly) and Backspace removes one --
 * no mid-string editing, same simple append/remove model as the numeric keypad.
 * Returns true if the key was consumed (the Field Manager's own default handling,
 * which would otherwise show the RAW typed text, must not also run). */
static Boolean PwFieldKeyDown (UInt16 fieldId, EventType* e)
{
	char*	real = RealPwBuf (fieldId);
	UInt16	len;
	WChar	c;

	if (! real) return false;
	len = (UInt16) StrLen (real);
	c = e->data.keyDown.chr;

	if (c == chrBackspace)
		{
		if (len > 0) real[len - 1] = 0;
		gRevealField = 0; gRevealUntilTicks = 0;
		RedrawMaskedField (fieldId, false);
		return true;
		}
	if (TxtCharIsPrint (c) && len < kPwBufLen - 1)
		{
		real[len] = (char) c;
		real[len + 1] = 0;
		RedrawMaskedField (fieldId, true);
		gRevealField = fieldId;
		gRevealUntilTicks = TimGetTicks () + (SysTicksPerSecond () * kRevealTicks);
		return true;
		}
	return false;
}

/* Close the dialog and SYNCHRONOUSLY redraw the main form (refreshing its
 * labels AND wiping any progress-bar pixels left on the display window - a posted
 * frmUpdateEvent redraws too late / incompletely). */
static void PwDone (void)
{
	/* A pending reveal-then-mask timer (see PwFieldKeyDown) must not outlive the
	 * dialog it belongs to -- the main loop's nilEvent timeout fires it against
	 * whatever form is THEN active, which after this call is no longer the one
	 * that owns the field id it's holding. GetObj/RedrawMaskedField now guard
	 * against that too, but clearing here stops the dangling timer at the source. */
	gRevealField = 0; gRevealUntilTicks = 0;
	FrmReturnToForm (0);
	RefreshMain ();
	FrmDrawForm (FrmGetActiveForm ());
}

static void PwOk (void)
{
	Char*		np = FieldText (fldNew);
	Char*		cp = FieldText (fldConfirm);
	Boolean		change = (Boolean) (gPwMode == PW_CHANGE);
	Boolean		haveOld = false;
	UInt8		kold[ENC_KEY_BYTES], knew[ENC_KEY_BYTES];
	UInt8		newSalt[ENC_SALT_BYTES];
	UInt16		newRounds = 0;
	Sec35Pref	prevPref;	/* snapshot for rollback -- see the gRekeyIncomplete check below */

	if (change) {
		/* verifying the current passcode re-derives the SAME key (stored salt) and
		 * caches it -> this IS the unlock. */
		if (! AttemptAllowed ()) return;		/* lockout delay active */
		if (! VerifyPassword (FieldText (fldCurrent)))
			{ OnWrongPassword (); return; }
		OnCorrectPassword ();
		if (StrLen (np) == 0)		/* current only, no new -> just unlock (no rekey) */
			{ PwDone (); return; }
		haveOld = EncGetKey (kold, kBigTtl);	/* grab OLD key before it's replaced */
	}
	if (StrLen (np) == 0)
		{ FrmCustomAlert (altEmpty, "", "", ""); return; }
	if (StrCompare (np, cp) != 0)
		{ FrmCustomAlert (altMismatch, "", "", ""); return; }
	/* Refuse to even start re-encrypting under memory pressure -- see
	 * EnoughMemoryForRekey's comment. Checked here, before anything changes, so a
	 * "no" leaves the password and all data completely untouched. */
	if (! EnoughMemoryForRekey ())
		{ FrmAlert (altLowMem); return; }

	prevPref = gPref;		/* keep a copy to restore from if the rekey pass fails
							 * partway, OR if the device resets mid-pass -- see below */

	if (change && haveOld)
		{
		/* CHANGE: derive the new key WITHOUT committing it yet. Committing the new
		 * salt/verifier here (as SET below still does) would destroy the only way
		 * to ever re-derive kold from the old password the instant this runs --
		 * if a reset then cuts the re-encrypt pass short, whatever's still under
		 * kold becomes permanently unrecoverable even with the correct old
		 * password, because the salt it needs is already gone. Instead: persist
		 * ONLY the new (non-secret) salt/rounds as a "rekey pending" checkpoint --
		 * no key material -- so that if a reset does cut this short, Sec35 can
		 * detect it next launch and prompt to resume (ResumeOk), re-deriving both
		 * keys from the still-intact old salt + the checkpointed new salt plus the
		 * two passwords re-entered then. The REAL salt/verifier (what actually
		 * unlocks going forward) is committed further down, only once every app's
		 * re-encrypt pass has fully succeeded. */
		EncRandomBytes (newSalt, ENC_SALT_BYTES);
		newRounds = EncCalibrateRounds (800);		/* ~0.8 s of stretching */
		EncDeriveKey (np, newSalt, newRounds, knew);

		gPref.rekeyPending = true;
		MemMove (gPref.pendingSalt, newSalt, ENC_SALT_BYTES);
		gPref.pendingRounds = newRounds;
		SavePrefs ();

		EncCacheKey (knew);		/* work unlocked under the new key during the pass */
		ArmAlarm ();
		}
	else
		DoSetPassword (np);					/* SET: new salt/key, caches Knew, commits immediately -- see PwOk's comment above for why CHANGE can't do the same */

	gRekeyIncomplete = false;
	{
	UInt16 stopA = 0, stopT = 0, stopD = 0, stopC = 0;		/* how far each pass got --
					 0 = never attempted (an earlier app already failed); exact
					 range a rollback must cover, see below */
	if (EncGetKey (knew, kBigTtl)) {
		/* Stop touching FURTHER apps' data the moment one record anywhere fails --
		 * each RecryptXxx35 call already stops at its OWN first failure (see their
		 * "if (gRekeyIncomplete) break"), this just keeps that from cascading into
		 * the next app's database too. */
		if (change) { if (haveOld) {
		                             stopA = RecryptAddress35 (kold, knew, kNoLimit);	/* re-encrypt */
		                             if (! gRekeyIncomplete) stopT = RecryptToDo35 (kold, knew, kNoLimit);
		                             if (! gRekeyIncomplete) stopD = RecryptDatebook35 (kold, knew, kNoLimit);
		                             if (! gRekeyIncomplete) stopC = RecryptCryptoNote (kold, knew, kNoLimit); } }
		else        {
		              RecryptAddress35 (NULL, knew, kNoLimit);				/* SET: encrypt all */
		              if (! gRekeyIncomplete) RecryptToDo35 (NULL, knew, kNoLimit);
		              if (! gRekeyIncomplete) RecryptDatebook35 (NULL, knew, kNoLimit);
		              if (! gRekeyIncomplete) RecryptCryptoNote (NULL, knew, kNoLimit); }
	}

	if (gRekeyIncomplete)
		{
		if (change && haveOld)
			{
			/* Roll back: convert whatever DID get switched to knew back to kold --
			 * ONLY the exact range [0, stopX) each pass actually reached (0 for an
			 * app never even attempted). Touching anything past that would mean
			 * decrypting-with-the-wrong-key a record the forward pass never
			 * touched in the first place, silently corrupting it -- the range
			 * bound is exactly what makes this safe. Then restore the pre-change
			 * prefs (prevPref, snapshotted before the checkpoint above was written
			 * -- this also clears rekeyPending, since a rollback that completes
			 * HERE, synchronously, needs no resume) so the password that ACTUALLY
			 * unlocks everything again is the one the user already knows. A
			 * partial CHANGE must never leave some records under a key nothing
			 * refers to any more (kold/knew are stack locals, gone the moment this
			 * function returns). */
			RecryptAddress35 (knew, kold, stopA);
			RecryptToDo35 (knew, kold, stopT);
			RecryptDatebook35 (knew, kold, stopD);
			RecryptCryptoNote (knew, kold, stopC);
			gPref = prevPref;
			SavePrefs ();
			EncCacheKey (kold);		/* stay unlocked under the ORIGINAL password */
			ArmAlarm ();
			}
		/* SET (no old key) can't roll back per-record -- there's nothing to revert
		 * TO, a skipped record there is simply still plaintext (SET only ever acts
		 * on plaintext records to begin with, so "untouched" == "not yet encrypted",
		 * not a mismatch against any key). */
		FrmAlert (altIncomplete);
		}
	else if (change && haveOld)
		{
		/* Full success: NOW it's safe to commit the real salt/verifier/rounds --
		 * every app's data is confirmed re-encrypted under knew, so the old salt
		 * is no longer needed for anything. Clears the checkpoint too. */
		UInt8 ver[16];
		MemMove (gPref.salt, newSalt, ENC_SALT_BYTES);
		gPref.rounds = newRounds;
		MemMove (ver, kVerifyConst, 16);
		EncCrypt (knew, ver, 16, 0, 0);
		MemMove (gPref.verifier, ver, 16);
		gPref.rekeyPending = false;
		MemSet (gPref.pendingSalt, ENC_SALT_BYTES, 0);
		gPref.pendingRounds = 0;
		SavePrefs ();
		MemSet (ver, 16, 0);
		}
	}

	MemSet (kold, ENC_KEY_BYTES, 0);
	MemSet (knew, ENC_KEY_BYTES, 0);
	MemSet (newSalt, ENC_SALT_BYTES, 0);

	/* A password CHANGE just re-keyed every app's on-disk data from the old key to
	 * the new one (DoSetPassword left the session UNLOCKED, caching the new key).
	 * Force the session back to LOCKED rather than continuing unlocked with it --
	 * switching straight to another suite app while unlocked right after a change
	 * crashed (reported: stale in-RAM state in that app, sized/keyed around
	 * whatever was true before the rekey, is no longer consistent with what's now
	 * on disk / cached). Locking here means every app has to re-derive its state
	 * from a clean unlock (re-entering the NEW password), same as any other cold
	 * start -- no path where a just-changed key is used before everything that
	 * depends on it has had a chance to catch up. SET (first-time password, no old
	 * ciphertext to be inconsistent with) is unaffected. A rolled-back CHANGE
	 * (gRekeyIncomplete above) is ALSO fine to lock here -- EncCacheKey (kold) just
	 * ran, so re-unlocking uses the (correctly restored) original password. */
	if (change)
		{
		EncWipeKey ();
		CancelAlarm ();
		}

	PwDone ();							/* closes dialog + redraws main (clears progress) */
}

/* OK on the Resume Re-key dialog (see frmResume / MainFormHandleEvent's
 * gPref.rekeyPending check): finishes a password CHANGE that a reset cut off
 * partway through. The OLD password is checked the normal way -- VerifyPassword
 * against the still-active old verifier, which PwOk never overwrote until full
 * success, so this works exactly like any other unlock. The NEW password can't be
 * verified the same way (no verifier for it was ever committed -- that's the
 * whole point of deferring it); it's proven by the repair pass itself, which
 * trial-decrypts every previously-encrypted record and only accepts a key when
 * PlausibleText recognizes the result. If what's typed here doesn't match what
 * was actually in progress before the reset, records still on the old key get
 * correctly finished under THIS (now authoritative) password, while records that
 * had already reached the old in-progress key fail both trials and are reported
 * unrecoverable -- nothing is silently corrupted either way, see Repair*35. */
static void ResumeOk (void)
{
	Char*	op    = FieldText (fldCurrent);
	Char*	npass = FieldText (fldNew);
	UInt8	kold[ENC_KEY_BYTES], knew[ENC_KEY_BYTES], ver[16];
	UInt16	unrec = 0;
	Char	msg[64];

	if (! AttemptAllowed ()) return;		/* lockout delay active */
	if (! VerifyPassword (op))
		{ OnWrongPassword (); return; }
	OnCorrectPassword ();
	if (StrLen (npass) == 0)
		{ FrmCustomAlert (altEmpty, "", "", ""); return; }
	if (! EnoughMemoryForRekey ())
		{ FrmAlert (altLowMem); return; }

	EncGetKey (kold, kBigTtl);		/* VerifyPassword just cached it */
	EncDeriveKey (npass, gPref.pendingSalt, gPref.pendingRounds, knew);

	unrec += RepairAddress35 (kold, knew);
	unrec += RepairToDo35 (kold, knew);
	unrec += RepairDatebook35 (kold, knew);
	unrec += RepairCryptoNote (kold, knew);

	/* Commit: this reaches the same final state PwOk's own success path would
	 * have, using the checkpointed (non-secret) salt/rounds from before the
	 * interruption. */
	MemMove (gPref.salt, gPref.pendingSalt, ENC_SALT_BYTES);
	gPref.rounds = gPref.pendingRounds;
	MemMove (ver, kVerifyConst, 16);
	EncCrypt (knew, ver, 16, 0, 0);
	MemMove (gPref.verifier, ver, 16);
	gPref.rekeyPending = false;
	MemSet (gPref.pendingSalt, ENC_SALT_BYTES, 0);
	gPref.pendingRounds = 0;
	SavePrefs ();

	MemSet (kold, ENC_KEY_BYTES, 0);
	MemSet (knew, ENC_KEY_BYTES, 0);
	MemSet (ver, 16, 0);

	EncWipeKey ();			/* same "come back locked" policy as a normal CHANGE */
	CancelAlarm ();

	if (unrec == 0)
		FrmCustomAlert (altResumeDone, "All records recovered.", "", "");
	else
		{
		StrPrintF (msg, "%d record(s) could not be recovered and were left untouched.", unrec);
		FrmCustomAlert (altResumeDone, msg, "", "");
		}

	PwDone ();							/* closes dialog + redraws main (clears progress) */
}

/* Delete button (PW_CHANGE only): verify current, DECRYPT all data (so it survives
 * losing the passcode), then clear the password. */
static void PwDelete (void)
{
	UInt8	key[ENC_KEY_BYTES];
	Boolean	have;

	if (! AttemptAllowed ()) return;		/* lockout delay active */
	if (! VerifyPassword (FieldText (fldCurrent)))
		{ OnWrongPassword (); return; }
	OnCorrectPassword ();
	/* See PwOk's identical check -- decrypting everything is the same kind of
	 * per-record rekey pass, same risk under memory pressure. */
	if (! EnoughMemoryForRekey ())
		{ FrmAlert (altLowMem); return; }
	have = EncGetKey (key, kBigTtl);		/* VerifyPassword just cached it */
	gRekeyIncomplete = false;
	if (have) {
		RecryptAddress35 (key, NULL, kNoLimit);		/* decrypt -> plaintext, clear enc bit */
		if (! gRekeyIncomplete) RecryptToDo35 (key, NULL, kNoLimit);
		if (! gRekeyIncomplete) RecryptDatebook35 (key, NULL, kNoLimit);
		if (! gRekeyIncomplete) RecryptCryptoNote (key, NULL, kNoLimit);
		MemSet (key, ENC_KEY_BYTES, 0);
	}
	if (gRekeyIncomplete)
		{
		/* Some records got decrypted before memory ran out, but not all -- do NOT
		 * clear the password now. That would leave the still-encrypted remainder
		 * PERMANENTLY unreadable (nothing would exist any more to derive their key
		 * from -- unlike PwOk's CHANGE rollback, there's no "old key" to fall back
		 * to here, this key IS the only one). The session stays unlocked under the
		 * SAME password, still valid for whatever's left encrypted; retry once
		 * more memory is free to finish clearing it. */
		FrmAlert (altIncomplete);
		PwDone ();
		return;
		}
	DoClearPassword ();
	PwDone ();							/* closes dialog + redraws main (clears progress) */
}

static Boolean PwDialogHandleEvent (EventType* e)
{
	Boolean handled = false;

	switch (e->eType)
		{
		case frmOpenEvent:
			FrmDrawForm (FrmGetActiveForm ());
			/* Fresh dialog -- don't inherit stale password text from a previous
			 * session (SET only ever shows New/Confirm; CHANGE also has Current). */
			gPwCurrent[0] = gPwNew[0] = gPwConfirm[0] = 0;
			gRevealField = 0; gRevealUntilTicks = 0;
			/* FrmSetFocus sets focus PROGRAMMATICALLY and does not raise fldEnterEvent,
			 * so it would bypass the keypad interception below -- skip the auto-focus
			 * in Numeric Keypad mode; the user must tap a field (which does raise
			 * fldEnterEvent) to bring up the keypad. */
			if (gPref.pwType != kPwTypeNumeric)
				FrmSetFocus (FrmGetActiveForm (), FrmGetObjectIndex (FrmGetActiveForm (),
					(gPwMode == PW_CHANGE || gPwMode == PW_RESUME) ? fldCurrent : fldNew));
			handled = true;
			break;

		case ctlSelectEvent:
			if (e->data.ctlSelect.controlID == btnPwOk)          { PwOk (); handled = true; }
			else if (e->data.ctlSelect.controlID == btnResumeOk) { ResumeOk (); handled = true; }
			else if (e->data.ctlSelect.controlID == btnPwDelete) { PwDelete (); handled = true; }
			else if (e->data.ctlSelect.controlID == btnPwCancel)
				{
				gRevealField = 0; gRevealUntilTicks = 0;	/* see PwDone's comment */
				FrmReturnToForm (0);
				handled = true;
				}
			break;

		/* Numeric Keypad mode: a tap into any password field pops the keypad instead
		 * of letting it take normal graffiti/keyboard focus. Text Entry mode (the
		 * default) falls through unhandled -- fields behave exactly as before. */
		case fldEnterEvent:
			if (gPref.pwType == kPwTypeNumeric)
				{
				UInt16 id = e->data.fldEnter.fieldID;
				if (id == fldCurrent || id == fldNew || id == fldConfirm)
					{
					gKeypadTargetField = id;
					FrmPopupForm (frmKeypad);
					handled = true;
					}
				}
			break;

		/* Text Entry mode: intercept keystrokes on the focused password field so the
		 * real text never sits in the field itself (masked display instead -- see
		 * PwFieldKeyDown/gPwCurrent). Numeric Keypad mode never gives these fields
		 * focus to type into (see fldEnterEvent above), so this never fires then. */
		case keyDownEvent:
			{
			FormType* frm = FrmGetActiveForm ();
			UInt16 focus = FrmGetFocus (frm);
			if (focus != noFocus)
				{
				UInt16 id = FrmGetObjectId (frm, focus);
				if (id == fldCurrent || id == fldNew || id == fldConfirm)
					handled = PwFieldKeyDown (id, e);
				}
			}
			break;

		default:
			break;
		}
	return handled;
}

/* Switch back to the app that handed off to us for unlocking (by creator). It
 * relaunches, its lock gate now passes (the key is cached), and it shows its data. */
static void ReturnToUnlockCaller (void)
{
	DmSearchStateType st;
	UInt16 card; LocalID db;
	UInt32 creator = gUnlockReturn;

	gRevealField = 0; gRevealUntilTicks = 0;	/* see PwDone's comment */
	gUnlockReturn = 0;
	if (DmGetNextDatabaseByTypeCreator (true, &st, sysFileTApplication, creator,
			true, &card, &db) == errNone)
		SysUIAppSwitch (card, db, sysAppLaunchCmdNormalLaunch, NULL);
}

/* ---- unlock dialog (password-only) ---- */

/* Shared by btnUnlockOk (Text Entry: user taps OK) and the numeric keypad's OK
 * (Numeric Keypad: entering the PIN and tapping the keypad's OK submits immediately,
 * no second tap on this dialog's own OK needed -- see KeypadFormHandleEvent). */
static void UnlockAttempt (void)
{
	if (! AttemptAllowed ())		/* lockout delay active -> block dialog */
		return;
	if (VerifyPassword (FieldText (fldUnlockPw)))	/* caches key + arms alarm */
		{
		OnCorrectPassword ();
		if (gUnlockReturn)			/* hand-off: return to the launching app */
			{ ReturnToUnlockCaller (); }
		else
			PwDone ();
		}
	else
		OnWrongPassword ();
}

static Boolean UnlockDialogHandleEvent (EventType* e)
{
	Boolean handled = false;

	switch (e->eType)
		{
		case frmOpenEvent:
			FrmDrawForm (FrmGetActiveForm ());
			gPwUnlock[0] = 0;			/* fresh dialog -- don't inherit stale text */
			gRevealField = 0; gRevealUntilTicks = 0;
			/* Numeric Keypad mode: go straight to the keypad -- no need to tap the
			 * field first (that used to take an extra tap: one to bring up the
			 * keypad, one more on this dialog's own OK after). Text Entry mode is
			 * unchanged (auto-focus the field for graffiti/keyboard entry). */
			if (gPref.pwType == kPwTypeNumeric)
				{
				gKeypadTargetField = fldUnlockPw;
				FrmPopupForm (frmKeypad);
				}
			else
				FrmSetFocus (FrmGetActiveForm (),
					FrmGetObjectIndex (FrmGetActiveForm (), fldUnlockPw));
			handled = true;
			break;

		case ctlSelectEvent:
			if (e->data.ctlSelect.controlID == btnUnlockOk)
				{ UnlockAttempt (); handled = true; }
			else if (e->data.ctlSelect.controlID == btnUnlockCancel)
				{
				gRevealField = 0; gRevealUntilTicks = 0;	/* see PwDone's comment */
				FrmReturnToForm (0);
				if (gUnlockReturn) ReturnToUnlockCaller ();	/* cancelled: still go back (masked) */
				handled = true;
				}
			break;

		/* Safety net: reachable if the keypad was cancelled and the user then taps
		 * the (still-empty, still-unfocused) field directly instead of retapping
		 * the Password: selector. */
		case fldEnterEvent:
			if (gPref.pwType == kPwTypeNumeric && e->data.fldEnter.fieldID == fldUnlockPw)
				{
				gKeypadTargetField = fldUnlockPw;
				FrmPopupForm (frmKeypad);
				handled = true;
				}
			break;

		/* Text Entry mode: see PwDialogHandleEvent's identical case. */
		case keyDownEvent:
			{
			FormType* frm = FrmGetActiveForm ();
			UInt16 focus = FrmGetFocus (frm);
			if (focus != noFocus && FrmGetObjectId (frm, focus) == fldUnlockPw)
				handled = PwFieldKeyDown (fldUnlockPw, e);
			}
			break;

		default:
			break;
		}
	return handled;
}

/* ---- preferences (password entry method) ---- */

static Boolean PrefsFormHandleEvent (EventType* e)
{
	Boolean handled = false;

	switch (e->eType)
		{
		case frmOpenEvent:
			{
			FormType* frm = FrmGetActiveForm ();
			LstSetSelection (GetObj (lstPwType), gPref.pwType);
			CtlSetLabel (GetObj (popPwType), (Char*) gPwTypeNames[gPref.pwType]);
			FrmDrawForm (frm);
			handled = true;
			}
			break;

		case popSelectEvent:
			if (e->data.popSelect.listID == lstPwType)
				{
				CtlSetLabel (GetObj (popPwType),
					(Char*) gPwTypeNames[e->data.popSelect.selection]);
				handled = true;
				}
			break;

		case ctlSelectEvent:
			if (e->data.ctlSelect.controlID == btnPrefsOk)
				{
				UInt8 newType = (UInt8) LstGetSelection (GetObj (lstPwType));
				if (newType != gPref.pwType)
					{
					gPref.pwType = newType;
					SavePrefs ();
					EncSetPasswordEntryType (newType);	/* keep the shared cross-app copy in sync */
					}
				FrmReturnToForm (0);
				handled = true;
				}
			else if (e->data.ctlSelect.controlID == btnPrefsCancel)
				{
				FrmReturnToForm (0);
				handled = true;
				}
			break;

		default:
			break;
		}
	return handled;
}

/* ---- numeric keypad (PIN-style password entry) ---- */

#define kKeypadStarsY	15
#define kKeypadStarsH	12
#define kKeyNone		(-1)		/* KeypadHitTest: no key under the point */
#define kKeyDelSlot		10			/* KeypadHitTest/gKeyPressed: the Del key (not in kKeyIds[]) */
/* FrameType is (cornerDiam << 8) | (shadowWidth << 2) | width (see FrameBitsType,
 * Window.h) -- 3px rounded corners, simple 1px line, no shadow. */
#define kKeyFrame		((FrameType) ((3 << 8) | 1))

/* Which key (if any) is currently pen-down/inverted -- 0-9 = digit, kKeyDelSlot = Del. */
static Int16	gKeyPressed = kKeyNone;

/* Erase + redraw the row of asterisks (one per digit typed so far), centered --
 * hand-drawn since a resource LABEL can't stay centered as its length changes. Kept
 * deliberately short (kKeypadStarsH) to leave more room for the digit grid below. */
static void KeypadDrawStars (void)
{
	RectangleType	r;
	Char			buf[32];
	UInt16			i;
	Int16			w, x;
	FontID			old;

	r.topLeft.x = 2; r.topLeft.y = kKeypadStarsY; r.extent.x = 152; r.extent.y = kKeypadStarsH;
	WinEraseRectangle (&r, 0);

	for (i = 0; i < gKeypadLen; i++) buf[i] = '*';
	buf[i] = 0;

	old = FntSetFont (boldFont);
	w = FntCharsWidth (buf, (Int16) StrLen (buf));
	x = (Int16) ((156 - w) / 2);
	if (x < 2) x = 2;
	WinDrawChars (buf, (Int16) StrLen (buf), x, kKeypadStarsY);
	FntSetFont (old);
}

/* Draw one key's border + digit + (if any) phone-style letters, both lines INSIDE the
 * key's own bounds -- id is either a kKeyIds[] digit gadget or kpdDel. */
static void KeypadDrawKey (FormType* frm, UInt16 id, const char* label, const char* letters)
{
	RectangleType	r;
	FontID			old;
	Int16			w, x, y, boldH;

	FrmGetObjectBounds (frm, FrmGetObjectIndex (frm, id), &r);
	WinDrawRectangleFrame (kKeyFrame, &r);

	old = FntSetFont (boldFont);
	boldH = FntLineHeight ();
	w = FntCharsWidth (label, (Int16) StrLen (label));
	x = (Int16) (r.topLeft.x + (r.extent.x - w) / 2);
	y = (Int16) (r.topLeft.y + 1);
	WinDrawChars (label, (Int16) StrLen (label), x, y);

	if (letters && letters[0])
		{
		FntSetFont (stdFont);
		w = FntCharsWidth (letters, (Int16) StrLen (letters));
		x = (Int16) (r.topLeft.x + (r.extent.x - w) / 2);
		y = (Int16) (y + boldH - 2);		/* 2px closer to the digit above */
		WinDrawChars (letters, (Int16) StrLen (letters), x, y);
		}
	FntSetFont (old);
}

static void KeypadDrawKeys (FormType* frm)
{
	UInt16 d;
	Char digitStr[2];

	digitStr[1] = 0;
	for (d = 0; d < 10; d++)
		{
		digitStr[0] = (Char) ('0' + d);
		KeypadDrawKey (frm, kKeyIds[d], digitStr, kKeyLetters[d]);
		}
	KeypadDrawKey (frm, kpdDel, "Del", NULL);
}

/* Which key (0-9 digit, kKeyDelSlot, or kKeyNone) is under (x,y) -- form-relative
 * screen coords, same as event->screenX/screenY. */
static Int16 KeypadHitTest (FormType* frm, Int16 x, Int16 y)
{
	UInt16			d;
	RectangleType	r;

	for (d = 0; d < 10; d++)
		{
		FrmGetObjectBounds (frm, FrmGetObjectIndex (frm, kKeyIds[d]), &r);
		if (RctPtInRectangle (x, y, &r)) return (Int16) d;
		}
	FrmGetObjectBounds (frm, FrmGetObjectIndex (frm, kpdDel), &r);
	if (RctPtInRectangle (x, y, &r)) return kKeyDelSlot;
	return kKeyNone;
}

static void KeypadInvertKey (FormType* frm, Int16 key)
{
	RectangleType	r;
	UInt16			id = (key == kKeyDelSlot) ? kpdDel : kKeyIds[key];

	FrmGetObjectBounds (frm, FrmGetObjectIndex (frm, id), &r);
	WinInvertRectangle (&r, 3);		/* match the key's 3px rounded corners */
}

static void KeypadAppend (char c)
{
	if (gKeypadLen < sizeof (gKeypadBuf) - 1)
		{
		gKeypadBuf[gKeypadLen++] = c;
		gKeypadBuf[gKeypadLen] = 0;
		KeypadDrawStars ();
		}
}

static void KeypadBackspace (void)
{
	if (gKeypadLen > 0)
		{
		gKeypadBuf[--gKeypadLen] = 0;
		KeypadDrawStars ();
		}
}

/* Don't leave a plaintext PIN sitting in RAM once the keypad is done with it. */
static void KeypadClearBuf (void)
{
	MemSet (gKeypadBuf, sizeof (gKeypadBuf), 0);
	gKeypadLen = 0;
}

static Boolean KeypadFormHandleEvent (EventType* e)
{
	Boolean handled = false;

	switch (e->eType)
		{
		case frmOpenEvent:
			{
			FormType* frm = FrmGetActiveForm ();
			KeypadClearBuf ();
			gKeyPressed = kKeyNone;
			FrmDrawForm (frm);
			KeypadDrawKeys (frm);
			KeypadDrawStars ();
			handled = true;
			}
			break;

		/* The 10 digit/Del keys are GADGETs (need 2 lines drawn inside their own
		 * border -- see the .rcp comment on frmKeypad), so they don't generate
		 * ctlSelectEvent; tap detection + press feedback are hand-tracked here. */
		case penDownEvent:
			{
			FormType* frm = FrmGetActiveForm ();
			Int16 key = KeypadHitTest (frm, e->screenX, e->screenY);
			if (key != kKeyNone)
				{
				gKeyPressed = key;
				KeypadInvertKey (frm, key);
				/* Digit/Del keys are GADGETs (see the comment above), so they
				 * never reach the Control Manager's own automatic tap click --
				 * play it by hand here so this pad sounds like every other
				 * button in the app (SndPlaySystemSound already self-gates on
				 * the System Sound preference, same as every other beep call
				 * in this suite - no manual pref check needed). */
				SndPlaySystemSound (sndClick);
				handled = true;
				}
			}
			break;

		case penUpEvent:
			if (gKeyPressed != kKeyNone)
				{
				FormType* frm = FrmGetActiveForm ();
				Int16 key = KeypadHitTest (frm, e->screenX, e->screenY);
				KeypadInvertKey (frm, gKeyPressed);	/* un-invert (2nd invert restores) */
				if (key == gKeyPressed)
					{
					if (key == kKeyDelSlot) KeypadBackspace ();
					else                     KeypadAppend ((char) ('0' + key));
					}
				gKeyPressed = kKeyNone;
				handled = true;
				}
			break;

		case ctlSelectEvent:
			if (e->data.ctlSelect.controlID == kpdOk)
				{
				UInt16	targetField = gKeypadTargetField;
				char*	real = RealPwBuf (targetField);
				if (real) StrNCopy (real, gKeypadBuf, kPwBufLen);	/* size = TOTAL dst size (strlcat-style) */
				FrmReturnToForm (0);			/* back to the Pw/Unlock dialog */
				/* Masked, same as Text Entry mode -- the keypad's own star row
				 * already gave real-time feedback while typing the PIN, so no
				 * "reveal last digit" here, straight to fully masked. */
				RedrawMaskedField (targetField, false);
				KeypadClearBuf ();
				/* Unlock dialog only: submit immediately, no second tap on that
				 * dialog's own OK button needed (Set/Change Password still need a
				 * separate OK -- New/Confirm/Current are multiple fields). */
				if (targetField == fldUnlockPw)
					UnlockAttempt ();
				handled = true;
				}
			else if (e->data.ctlSelect.controlID == kpdCancel)
				{
				FrmReturnToForm (0);
				KeypadClearBuf ();
				handled = true;
				}
			break;

		default:
			break;
		}
	return handled;
}

/* ---- app plumbing ---- */

static Boolean AppHandleEvent (EventType* e)
{
	UInt16		formId;
	FormType*	frm;

	if (e->eType == frmLoadEvent)
		{
		formId = e->data.frmLoad.formID;
		frm = FrmInitForm (formId);
		FrmSetActiveForm (frm);
		if (formId == frmMain)         FrmSetEventHandler (frm, MainFormHandleEvent);
		else if (formId == frmUnlock)  FrmSetEventHandler (frm, UnlockDialogHandleEvent);
		else if (formId == frmPrefs)   FrmSetEventHandler (frm, PrefsFormHandleEvent);
		else if (formId == frmKeypad)  FrmSetEventHandler (frm, KeypadFormHandleEvent);
		else                           FrmSetEventHandler (frm, PwDialogHandleEvent);
		return true;
		}
	return false;
}

UInt32 PilotMain (UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
	EventType e;

	if (cmd == sysAppLaunchCmdAlarmTriggered)
		{
		/* background, no-globals sublaunch: auto-lock -> drop the cached key */
		EncWipeKey ();
		return 0;
		}

	if (cmd == sysAppLaunchCmdNormalLaunch || cmd == kSecLaunchUnlock)
		{
		LoadPrefs ();
		EncRegisterSleepWipe ();		/* see enccore.h -- only meaningful while resident */
		gUnlockReturn = 0;
		if (cmd == kSecLaunchUnlock)
			{								/* hand-off: who to return to (via Feature) */
			UInt32 ret = 0;
			if (FtrGet (ENC_CREATOR, kSecReturnFtr, &ret) == errNone)
				{
				gUnlockReturn = ret;
				FtrUnregister (ENC_CREATOR, kSecReturnFtr);
				}
			}
		FrmGotoForm (frmMain);			/* frmOpen auto-pops the unlock dialog if handing off */
		do {
			UInt16 err;
			UInt32 timeout = evtWaitForever;
			/* Password masking: while a field has a just-typed character showing in
			 * clear (gRevealField/gRevealUntilTicks, see PwFieldKeyDown), wake up
			 * exactly when its reveal window ends even if no other event arrives, so
			 * it always gets masked on schedule rather than only on the next keystroke. */
			if (gRevealUntilTicks)
				{
				UInt32 now = TimGetTicks ();
				timeout = (gRevealUntilTicks > now) ? (gRevealUntilTicks - now) : 0;
				}
			/* Countdown label ("Locking in HH:MM"/"MM:SS"): wake up on its own
			 * schedule too (see RefreshCountdown), taking whichever of the two
			 * deadlines is sooner. */
			if (gCountdownRefreshUntilTicks)
				{
				UInt32 now = TimGetTicks ();
				UInt32 cdTimeout = (gCountdownRefreshUntilTicks > now)
					? (gCountdownRefreshUntilTicks - now) : 0;
				if (cdTimeout < timeout) timeout = cdTimeout;
				}
			EvtGetEvent (&e, timeout);
			if (e.eType == nilEvent)
				{
				if (gRevealUntilTicks && TimGetTicks () >= gRevealUntilTicks)
					MaskPendingReveal ();
				/* Only touch frmMain's objects while it's actually the active
				 * form -- RefreshCountdown/RefreshMain assume that context.
				 * Full RefreshMain, not just RefreshCountdown: once the TTL
				 * actually elapses (countdown reaches zero) the Session
				 * status/Lock-Unlock button need to flip too, not just the
				 * countdown label going blank. */
				if (gCountdownRefreshUntilTicks && TimGetTicks () >= gCountdownRefreshUntilTicks
						&& FrmGetActiveFormID () == frmMain)
					RefreshMain ();
				}
			if (! SysHandleEvent (&e))
				if (! MenuHandleEvent (0, &e, &err))	/* Options > Forget Password */
					if (! AppHandleEvent (&e))
						FrmDispatchEvent (&e);
		} while (e.eType != appStopEvent);
		EncUnregisterSleepWipe ();
		FrmCloseAllForms ();
		}
	return 0;
}
