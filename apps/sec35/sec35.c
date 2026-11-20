/*
 * sec35.c - Sec35 security manager (single pane) for the era suite.
 *
 * Owns the suite passcode + TTL + the auto-lock alarm, and caches the derived
 * key in shared RAM (via enccore) so the apps can encrypt/decrypt in-process.
 * Set/Change/Remove, the wipe path (Forget Password + 10th-wrong-attempt
 * auto-wipe) and the one-time key scheme 1 -> 2 conversion never touch another
 * app's database: Sec35 sublaunches each suite app with kSecLaunchRekey and the
 * app re-encrypts its own stores (see RunRekey, enccore.h, encrekey.h).
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
#define altNotFinished	2009
#define altNewMismatch	2010
#define altRemoveConfirm	2011

#define kSecPrefId		0
#define kSecPrefVer		6
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
	/* --- v6: key scheme 2 (per-store keys, see enccore.h). Old prefs load with
	 * these 0: verifier made with the Sec35 key itself, data not yet converted. */
	UInt8	verScheme;			/* <2: verifier = CTR(SecKey); 2: CTR(S35v store key) */
	UInt8	convertDone;		/* every app converted its data to scheme 2           */
	UInt8	pendOp;				/* kRekeyChange / kRekeyConvert not yet finished, 0 = none */
	UInt8	reserved5;
	UInt32	pendSerial;			/* opSerial of that operation (apps resume by it)     */
	UInt16	pendDoneMask;		/* bit i = kRekeyApps[i] finished pendSerial          */
	UInt16	reserved6;
	UInt8	pendVerifier[16];	/* scheme-2 check value of the NEW password of a pending
								 * change (proves the password typed when resuming)   */
} Sec35Pref;

static Sec35Pref	gPref;
static UInt16		gPwMode;
static UInt32		gUnlockReturn;	/* creator to switch back to after an unlock hand-off, 0 = none */


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

/* The typed passwords are plaintext: wipe the whole buffers (not just the
 * first byte) when a dialog opens or closes, on the hand-back to an app, and
 * at exit, so a password never lingers in the dynamic heap after use. The
 * keypad's digit buffer too. */
static void WipePwBufs (void)
{
	MemSet (gPwCurrent, sizeof (gPwCurrent), 0);
	MemSet (gPwNew, sizeof (gPwNew), 0);
	MemSet (gPwConfirm, sizeof (gPwConfirm), 0);
	MemSet (gPwUnlock, sizeof (gPwUnlock), 0);
	MemSet (gKeypadBuf, sizeof (gKeypadBuf), 0);
	gKeypadLen = 0;
}

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
static void ReturnToUnlockCaller (void);

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

/* The password check value. Scheme 2 encrypts the constant under the S35v
 * store key, so it shares no keystream with any app's data; scheme <2 is the
 * old form (CTR under the Sec35 key itself, nonce 0,0), still accepted so an
 * existing password keeps working and is upgraded on the first unlock. */
static void MakeVerifier (const UInt8 key[ENC_KEY_BYTES], UInt8 scheme, UInt8 out[16])
{
	UInt8 k[ENC_KEY_BYTES];

	MemMove (out, kVerifyConst, 16);
	if (scheme >= 2)
		{
		EncDeriveStoreKey (key, ENC_STORE_SEC35V, k);
		EncCrypt (k, out, 16, 0, 0);
		MemSet (k, ENC_KEY_BYTES, 0);
		}
	else
		EncCrypt (key, out, 16, 0, 0);
}

static Boolean VerifyPassword (const char* pw)
{
	UInt8	key[ENC_KEY_BYTES], ver[16];
	Boolean	ok;

	if (! gPref.passwordSet) return false;
	EncDeriveKey (pw, gPref.salt, gPref.rounds, key);
	MakeVerifier (key, gPref.verScheme, ver);
	ok = (Boolean)(MemCmp (ver, gPref.verifier, 16) == 0);
	if (ok)
		{
		if (gPref.verScheme < 2)			/* upgrade the check value in place */
			{
			MakeVerifier (key, 2, ver);
			MemMove (gPref.verifier, ver, 16);
			gPref.verScheme = 2;
			SavePrefs ();
			}
		EncCacheKey (key); ArmAlarm ();		/* a correct entry unlocks */
		}
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
	MakeVerifier (key, 2, ver);
	MemMove (gPref.verifier, ver, 16);
	gPref.verScheme = 2;
	gPref.convertDone = true;		/* nothing was encrypted before: nothing to convert */
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
	/* Callers (PwDelete) have every app decrypt its data first, so removing the
	 * passcode leaves readable plaintext rather than orphaned ciphertext. */
	gPref.passwordSet = false;
	MemSet (gPref.salt, ENC_SALT_BYTES, 0);
	MemSet (gPref.verifier, 16, 0);
	gPref.verScheme = 2;
	gPref.convertDone = true;
	SavePrefs ();
	EncSetEncryptionActive (false);	/* no passcode -> encryption inactive */
	EncWipeKey ();
	CancelAlarm ();
}

/* ---- rekey: Sec35 asks each app to re-encrypt its own data ------------- *
 * The order matters only for the progress display. The CalDAV events and
 * contacts databases belong to CalDAV Sync (Datebook35 and Address35 only read
 * them). BitWarden's vault has its own key; only its cached master password
 * is under a Sec35 store key. */
#define kBigTtl			1000000000UL

typedef struct { UInt32 creator; const char* name; } RekeyApp;
#define kRekeyAppCount	7
static const RekeyApp kRekeyApps[kRekeyAppCount] = {
	{ 'Ad35', "Address35" },
	{ 'Td35', "ToDo35" },
	{ 'Db35', "Datebook35" },
	{ 'CrNt', "CryptoNote" },
	{ 'CDDV', "CalDAV Sync" },
	{ 'LzTP', "lazyFTP" },
	{ 'BWrd', "BitWarden" }
};

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
	 * rekey/crash-safe-resume logic (RunRekey and the apps' handlers)
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

	bar.topLeft.x = 6; bar.topLeft.y = 34; bar.extent.x = 144; bar.extent.y = 10;
	WinDrawRectangleFrame (simpleFrame, &bar);
	if (w > 0) {
		fill.topLeft.x = 7; fill.topLeft.y = 35; fill.extent.x = w; fill.extent.y = 8;
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
		WinDrawChars (body, (Int16) StrLen (body), x, 17);	/* 7 px gaps: title, body, bar, bottom */
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

/* ---- memory-pressure pre-flight check ----------------------------------- *
 * Each app's handler runs as a sublaunch with its own globals (up to ~32 KB
 * for the larger apps) and one record buffer at a time; it checks its own
 * records and stops cleanly with kRekeyResultLowMem if an allocation fails.
 * This only refuses to START when the heap is clearly too small, so a "no"
 * leaves the password and all data untouched. */
#define kRekeyHeapNeed	(40UL * 1024UL)

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

static Boolean EnoughMemoryForRekey (void)
{
	return (Boolean) (DynamicHeapLargestFreeChunk () > kRekeyHeapNeed);
}

/* A fresh, non-zero operation serial. */
static UInt32 NewOpSerial (void)
{
	UInt32 v = 0;
	while (v == 0) EncRandomBytes ((UInt8*) &v, sizeof (v));
	return v;
}

/* Sublaunch every installed suite app with kSecLaunchRekey. With trackDone,
 * apps already finished for this serial (gPref.pendDoneMask) are skipped and
 * each finished app is recorded at once, so a reset part-way resumes with the
 * rest (and each app resumes inside its own data by the same serial). Stops at
 * the first app that does not finish; *failIdx = that app. An installed app
 * that does not answer is an old version: it must be updated first. */
static UInt16 RunRekey (UInt16 op, const UInt8* oldK, const UInt8* newK,
                        UInt32 serial, Boolean trackDone, UInt16* failIdx)
{
	static const char* const titles[6] = { "", "Encrypting", "Re-encrypting",
		"Removing encryption", "Security Wipe", "Updating encryption" };
	UInt16	i;

	for (i = 0; i < kRekeyAppCount; i++)
		{
		DmSearchStateType	st;
		UInt16				card, res;
		LocalID				db;
		UInt32				ret = 0;
		Err					err;
		FormPtr				prog, prev;
		EncRekeyParam		prm;

		if (trackDone && (gPref.pendDoneMask & (1 << i))) continue;
		if (DmGetNextDatabaseByTypeCreator (true, &st, sysFileTApplication,
				kRekeyApps[i].creator, true, &card, &db) == errNone)
			{
			MemSet (&prm, sizeof (prm), 0);
			prm.op = op;
			prm.opSerial = serial;
			if (oldK) MemMove (prm.oldKey, oldK, ENC_KEY_BYTES);
			if (newK) MemMove (prm.newKey, newK, ENC_KEY_BYTES);
			prm.progress.topLeft.x = 6;  prm.progress.topLeft.y = 30;	/* form-relative, */
			prm.progress.extent.x = 144; prm.progress.extent.y = 10;	/* as ProgressBar */

			prog = ProgressBegin (titles[op <= 5 ? op : 0], kRekeyApps[i].name, &prev);
			err = SysAppLaunch (card, db, sysAppLaunchFlagNewGlobals,
				kSecLaunchRekey, (MemPtr) &prm, &ret);
			ProgressEnd (prog, prev);

			res = err ? kRekeyResultFailed : prm.result;
			MemSet (&prm, sizeof (prm), 0);			/* the keys */
			if (res != kRekeyResultDone && op == kRekeyWipe)
				{									/* best effort: wipe the others too */
				if (failIdx) *failIdx = i;
				continue;
				}
			if (res != kRekeyResultDone)
				{
				if (failIdx) *failIdx = i;
				return res;
				}
			}
		if (trackDone)
			{
			gPref.pendDoneMask |= (UInt16) (1 << i);
			SavePrefs ();
			}
		}
	return kRekeyResultDone;
}

/* Tell the user which app did not finish, and why. */
static void ShowNotFinished (UInt16 res, UInt16 failIdx, const char* next)
{
	const char* why = (res == kRekeyResultLowMem) ? "not enough memory"
					: (res == kRekeyResultNone)   ? "it needs to be updated"
					: "an error";
	FrmCustomAlert (altNotFinished, kRekeyApps[failIdx < kRekeyAppCount ? failIdx : 0].name,
		why, next);
}

/* Scheme 1 -> 2: every app converts its data under the CURRENT key (cached).
 * Runs after the first unlock with this version, again whenever an app asks
 * for it (kSecConvertFtr: e.g. a database restored from an old backup), and
 * resumes by serial after a reset. Returns true when everything is done. */
static Boolean RunConvert (void)
{
	UInt8	key[ENC_KEY_BYTES];
	UInt16	res, failIdx = 0;

	if (! EncGetKey (key, kBigTtl)) return false;
	if (gPref.pendOp != kRekeyConvert)
		{
		gPref.pendOp = kRekeyConvert;
		gPref.pendSerial = NewOpSerial ();
		gPref.pendDoneMask = 0;
		SavePrefs ();
		}
	res = RunRekey (kRekeyConvert, key, key, gPref.pendSerial, true, &failIdx);
	MemSet (key, ENC_KEY_BYTES, 0);
	if (res != kRekeyResultDone)
		{
		ShowNotFinished (res, failIdx, "It will continue the next time you unlock.");
		return false;
		}
	gPref.pendOp = 0;
	gPref.pendSerial = 0;
	gPref.pendDoneMask = 0;
	gPref.convertDone = true;
	SavePrefs ();
	FtrUnregister (ENC_CREATOR, kSecConvertFtr);
	return true;
}

/* Run the conversion if it is still owed or an app asked for it. False when
 * one was needed and did not finish (it has said why): a caller that handed
 * off from an app then stays here instead of returning to an app that would
 * only bounce back. */
static Boolean ConvertIfNeeded (void)
{
	UInt32 v;
	Boolean asked = (Boolean) (FtrGet (ENC_CREATOR, kSecConvertFtr, &v) == errNone);

	if (! gPref.passwordSet || gPref.rekeyPending) return true;
	if (! gPref.convertDone || asked || gPref.pendOp == kRekeyConvert)
		{
		if (! EnoughMemoryForRekey ()) { FrmAlert (altLowMem); return false; }
		if (asked && gPref.convertDone && gPref.pendOp != kRekeyConvert)
			{	/* a fresh pass: already-converted databases are skipped by the apps */
			gPref.pendOp = kRekeyConvert;
			gPref.pendSerial = NewOpSerial ();
			gPref.pendDoneMask = 0;
			SavePrefs ();
			}
		return RunConvert ();
		}
	return true;
}

/* A password change or removal must not start while the conversion is half
 * done: a database part way through has records under two keys and no marker
 * yet, and the change would read them all with one. Finish it first. */
static Boolean ConversionSettled (void)
{
	if (gPref.convertDone && gPref.pendOp != kRekeyConvert) return true;
	ConvertIfNeeded ();
	return (Boolean) (gPref.convertDone && gPref.pendOp != kRekeyConvert);
}

/* Full "forget password" / lockout wipe: every app deletes its encrypted data
 * (no key needed), then revert to unassigned + clear the lockout counters.
 * Best effort across apps: one that fails does not stop the others. */
static void SecurityWipe (void)
{
	UInt16 failIdx = kRekeyAppCount;

	RunRekey (kRekeyWipe, NULL, NULL, NewOpSerial (), false, &failIdx);
	gPref.passwordSet = false;
	MemSet (gPref.salt, ENC_SALT_BYTES, 0);
	MemSet (gPref.verifier, 16, 0);
	gPref.verScheme = 2;
	gPref.convertDone = true;
	gPref.pendOp = 0; gPref.pendSerial = 0; gPref.pendDoneMask = 0;
	gPref.rekeyPending = false;
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
	WinDrawChars (line1, (Int16) StrLen (line1), 8, 20);	/* first row y=20, like the other dialogs */
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
		r.topLeft.x = 6; r.topLeft.y = 30; r.extent.x = 148; r.extent.y = 12;
		WinEraseRectangle (&r, 0);
		old = FntSetFont (stdFont);
		WinDrawChars (line2, (Int16) StrLen (line2), 8, 32);
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
			/* handed off while already unlocked: an app found a database still in
			 * key scheme 1 (kSecConvertFtr) -- convert, then go straight back */
			else if (gUnlockReturn && gPref.passwordSet)
				{ if (ConvertIfNeeded ()) ReturnToUnlockCaller (); else gUnlockReturn = 0; }
			/* opened directly while unlocked with a conversion still owed */
			else if (gPref.passwordSet && EncKeyPresent ())
				ConvertIfNeeded ();
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

/* The on-screen keyboard ("abc"/"123" in the Graffiti area) edits the focused
 * field's text directly, without key events, so PwFieldKeyDown never saw it and
 * the real buffer stayed empty. Called from the event loop before SysHandleEvent:
 * on a password field, open the keyboard on the real text ourselves (it shows in
 * clear while the keyboard is up) and take the result back, masked again. */
static Boolean PwKeyboard (EventType* e)
{
	FormType*	frm = FrmGetActiveForm ();
	FieldType*	fld;
	UInt16		focus, id;
	char*		real;
	Char*		t;
	WChar		c;

	if (e->eType != keyDownEvent || ! frm) return false;
	c = e->data.keyDown.chr;
	if (! (e->data.keyDown.modifiers & commandKeyMask)) return false;
	if (c != vchrKeyboard && c != vchrKeyboardAlpha && c != vchrKeyboardNumeric) return false;
	focus = FrmGetFocus (frm);
	if (focus == noFocus) return false;
	id = FrmGetObjectId (frm, focus);
	real = RealPwBuf (id);
	if (! real) return false;
	fld = (FieldType*) FrmGetObjectPtr (frm, focus);

	FldSetContent (fld, real);
	SysKeyboardDialog (c == vchrKeyboardNumeric ? kbdNumbersAndPunc :
		(c == vchrKeyboardAlpha ? kbdAlpha : kbdDefault));
	t = FldGetTextPtr (fld);
	StrNCopy (real, t ? t : "", kPwBufLen - 1);
	real[kPwBufLen - 1] = 0;
	gRevealField = 0; gRevealUntilTicks = 0;
	RedrawMaskedField (id, false);
	return true;
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
	WipePwBufs ();
	FrmReturnToForm (0);
	RefreshMain ();
	FrmDrawForm (FrmGetActiveForm ());
}

static void PwOk (void)
{
	Char*		np = FieldText (fldNew);
	Char*		cp = FieldText (fldConfirm);
	Boolean		change = (Boolean) (gPwMode == PW_CHANGE);
	UInt8		kold[ENC_KEY_BYTES], knew[ENC_KEY_BYTES], ver[16];
	UInt8		newSalt[ENC_SALT_BYTES];
	UInt16		newRounds = 0, res, failIdx = 0;

	if (change) {
		/* verifying the current passcode re-derives the SAME key (stored salt) and
		 * caches it -> this IS the unlock. */
		if (! AttemptAllowed ()) return;		/* lockout delay active */
		if (! VerifyPassword (FieldText (fldCurrent)))
			{ OnWrongPassword (); return; }
		OnCorrectPassword ();
		if (StrLen (np) == 0)		/* current only, no new -> just unlock (no rekey) */
			{ PwDone (); ConvertIfNeeded (); return; }
		if (! ConversionSettled ()) return;	/* it said why */
	}
	if (StrLen (np) == 0)
		{ FrmCustomAlert (altEmpty, "", "", ""); return; }
	if (StrCompare (np, cp) != 0)
		{ FrmCustomAlert (altMismatch, "", "", ""); return; }
	/* Refuse to even start under memory pressure -- checked before anything
	 * changes, so a "no" leaves the password and all data untouched. */
	if (! EnoughMemoryForRekey ())
		{ FrmAlert (altLowMem); return; }

	if (! change)
		{
		/* SET: commit at once (nothing was encrypted), then every app encrypts
		 * its plaintext. An app that does not finish is harmless: its records
		 * are still plaintext, and the apps seal plaintext themselves while
		 * unlocked. */
		DoSetPassword (np);
		EncGetKey (knew, kBigTtl);
		res = RunRekey (kRekeySet, NULL, knew, NewOpSerial (), false, &failIdx);
		MemSet (knew, ENC_KEY_BYTES, 0);
		if (res != kRekeyResultDone)
			ShowNotFinished (res, failIdx, "Its records stay unencrypted until it finishes.");
		PwDone ();
		return;
		}

	/* CHANGE. The old salt/verifier stay committed until EVERY app has finished
	 * (they are the only way to re-derive the old key from the old password).
	 * Persist only the new (non-secret) salt/rounds, the new password's check
	 * value and the operation serial as a "rekey pending" checkpoint, so a reset
	 * or an app that runs out of memory can be resumed (ResumeOk) with the two
	 * passwords re-entered. No key material is stored. */
	EncGetKey (kold, kBigTtl);					/* VerifyPassword just cached it */
	EncRandomBytes (newSalt, ENC_SALT_BYTES);
	newRounds = EncCalibrateRounds (800);		/* ~0.8 s of stretching */
	EncDeriveKey (np, newSalt, newRounds, knew);

	gPref.rekeyPending = true;
	MemMove (gPref.pendingSalt, newSalt, ENC_SALT_BYTES);
	gPref.pendingRounds = newRounds;
	MakeVerifier (knew, 2, ver);
	MemMove (gPref.pendVerifier, ver, 16);
	gPref.pendOp = kRekeyChange;
	gPref.pendSerial = NewOpSerial ();
	gPref.pendDoneMask = 0;
	SavePrefs ();

	res = RunRekey (kRekeyChange, kold, knew, gPref.pendSerial, true, &failIdx);
	if (res == kRekeyResultDone)
		{
		/* Commit the new password. */
		MemMove (gPref.salt, newSalt, ENC_SALT_BYTES);
		gPref.rounds = newRounds;
		MemMove (gPref.verifier, ver, 16);
		gPref.verScheme = 2;
		gPref.convertDone = true;		/* a change converts scheme 1 data too */
		gPref.rekeyPending = false;
		MemSet (gPref.pendingSalt, ENC_SALT_BYTES, 0);
		gPref.pendingRounds = 0;
		MemSet (gPref.pendVerifier, 16, 0);
		gPref.pendOp = 0; gPref.pendSerial = 0; gPref.pendDoneMask = 0;
		SavePrefs ();
		}
	else
		ShowNotFinished (res, failIdx,
			"Open Sec35 and enter the old and new password to finish.");

	MemSet (kold, ENC_KEY_BYTES, 0);
	MemSet (knew, ENC_KEY_BYTES, 0);
	MemSet (ver, 16, 0);
	MemSet (newSalt, ENC_SALT_BYTES, 0);

	/* Come back LOCKED after a change (finished or not): switching straight to
	 * another suite app while unlocked right after a change crashed (stale
	 * in-RAM state in that app). Every app re-derives its state from a clean
	 * unlock with the password that is now valid. */
	EncWipeKey ();
	CancelAlarm ();
	PwDone ();
}

/* OK on the Resume Re-key dialog (see frmResume / MainFormHandleEvent's
 * gPref.rekeyPending check): finishes a password CHANGE that a reset or an app
 * that ran out of memory cut short. The old password is checked against the
 * still-committed old verifier, the new one against the pending change's own
 * check value; then the apps not yet finished carry on from where they were
 * (same operation serial). */
static void ResumeOk (void)
{
	Char*	op    = FieldText (fldCurrent);
	Char*	npass = FieldText (fldNew);
	UInt8	kold[ENC_KEY_BYTES], knew[ENC_KEY_BYTES], ver[16];
	UInt16	res, failIdx = 0;
	Boolean	newOk;

	if (! AttemptAllowed ()) return;		/* lockout delay active */
	if (! VerifyPassword (op))
		{ OnWrongPassword (); return; }
	OnCorrectPassword ();
	if (StrLen (npass) == 0)
		{ FrmCustomAlert (altEmpty, "", "", ""); return; }

	EncGetKey (kold, kBigTtl);		/* VerifyPassword just cached it */
	EncDeriveKey (npass, gPref.pendingSalt, gPref.pendingRounds, knew);
	MakeVerifier (knew, 2, ver);
	newOk = (Boolean) (MemCmp (ver, gPref.pendVerifier, 16) == 0);
	if (! newOk)
		{
		MemSet (kold, ENC_KEY_BYTES, 0); MemSet (knew, ENC_KEY_BYTES, 0); MemSet (ver, 16, 0);
		EncWipeKey (); CancelAlarm ();
		FrmAlert (altNewMismatch);
		return;
		}
	if (! EnoughMemoryForRekey ())
		{
		MemSet (kold, ENC_KEY_BYTES, 0); MemSet (knew, ENC_KEY_BYTES, 0); MemSet (ver, 16, 0);
		FrmAlert (altLowMem);
		return;
		}

	res = RunRekey (kRekeyChange, kold, knew, gPref.pendSerial, true, &failIdx);
	if (res == kRekeyResultDone)
		{
		MemMove (gPref.salt, gPref.pendingSalt, ENC_SALT_BYTES);
		gPref.rounds = gPref.pendingRounds;
		MemMove (gPref.verifier, ver, 16);
		gPref.verScheme = 2;
		gPref.convertDone = true;
		gPref.rekeyPending = false;
		MemSet (gPref.pendingSalt, ENC_SALT_BYTES, 0);
		gPref.pendingRounds = 0;
		MemSet (gPref.pendVerifier, 16, 0);
		gPref.pendOp = 0; gPref.pendSerial = 0; gPref.pendDoneMask = 0;
		SavePrefs ();
		FrmCustomAlert (altResumeDone, "All apps finished.", "", "");
		}
	else
		ShowNotFinished (res, failIdx, "Try again after fixing that.");

	MemSet (kold, ENC_KEY_BYTES, 0);
	MemSet (knew, ENC_KEY_BYTES, 0);
	MemSet (ver, 16, 0);

	EncWipeKey ();			/* same "come back locked" policy as a normal CHANGE */
	CancelAlarm ();
	PwDone ();
}

/* Delete button (PW_CHANGE only): verify current, have every app DECRYPT its
 * data (so it survives losing the passcode; saved passwords in lazyFTP and
 * CalDAV Sync are cleared, there is nowhere safe to keep them), then clear the
 * password. Removal is safe to repeat: records already plaintext are skipped. */
static void PwDelete (void)
{
	UInt8	key[ENC_KEY_BYTES];
	UInt16	res, failIdx = 0;

	if (! AttemptAllowed ()) return;		/* lockout delay active */
	if (! VerifyPassword (FieldText (fldCurrent)))
		{ OnWrongPassword (); return; }
	OnCorrectPassword ();
	if (FrmAlert (altRemoveConfirm) != 1)	/* button 1 = Remove */
		return;
	if (! ConversionSettled ()) return;		/* it said why */
	if (! EnoughMemoryForRekey ())
		{ FrmAlert (altLowMem); return; }
	EncGetKey (key, kBigTtl);				/* VerifyPassword just cached it */
	res = RunRekey (kRekeyRemove, key, NULL, NewOpSerial (), false, &failIdx);
	MemSet (key, ENC_KEY_BYTES, 0);
	if (res != kRekeyResultDone)
		{
		/* Some data decrypted, some not: keep the password (the remainder is
		 * still readable with it); repeating the removal finishes it. */
		ShowNotFinished (res, failIdx, "The password is kept. Remove it again to finish.");
		PwDone ();
		return;
		}
	gPref.pendOp = 0; gPref.pendSerial = 0; gPref.pendDoneMask = 0;
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
			WipePwBufs ();				/* fresh dialog: nothing left from before */
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
				WipePwBufs ();
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
	WipePwBufs ();
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
			{
			if (ConvertIfNeeded ()) ReturnToUnlockCaller ();
			else { gUnlockReturn = 0; PwDone (); }
			}
		else
			{ PwDone (); ConvertIfNeeded (); }
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
			WipePwBufs ();				/* fresh dialog -- don't inherit stale text */
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
				WipePwBufs ();
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

/* "Lock on Power-Off" for whichever app is running when the device sleeps.
 * Sec35 registers for the sleep notification by LAUNCH CODE (no callback), so
 * the OS launches Sec35 itself (no globals) on every sleep, from any app --
 * the launcher, the ROM apps, or a suite app that never registered its own
 * callback (lazyFTP, CalDAV Sync). Registered on every launch, after a soft
 * reset (the registration does not survive one) and after a HotSync; never
 * unregistered (the OS drops it when Sec35 is deleted). A duplicate
 * registration is refused harmlessly. No globals are used on this path. */
static void RegisterSleepLock (void)
{
	UInt16	card;
	LocalID	db;

	if (GetMyDb (&card, &db))
		SysNotifyRegister (card, db, sysNotifySleepNotifyEvent, NULL,
			sysNotifyNormalPriority, NULL);
}

UInt32 PilotMain (UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
	EventType e;

	if (cmd == sysAppLaunchCmdNotify)
		{
		/* sleep notification (see RegisterSleepLock): drop the key if the user
		 * turned "Lock on Power-Off" on. The setting is in the unsaved prefs,
		 * readable without globals. */
		SysNotifyParamType* np = (SysNotifyParamType*) cmdPBP;
		if (np && np->notifyType == sysNotifySleepNotifyEvent && EncGetLockOnSleep ())
			EncWipeKey ();
		return 0;
		}

	if (cmd == sysAppLaunchCmdSystemReset || cmd == sysAppLaunchCmdSyncNotify)
		{
		/* No globals here: read Sec35's own saved settings into a local and
		 * re-assert "a passcode is set" (the Feature does not survive a soft
		 * reset; the unsaved pref does, this re-syncs both with the truth). */
		Sec35Pref	p;
		UInt16		sz = sizeof (p);
		MemSet (&p, sizeof (p), 0);
		PrefGetAppPreferences (ENC_CREATOR, kSecPrefId, &p, &sz, true);
		EncSetEncryptionActive ((Boolean) p.passwordSet);
		MemSet (&p, sizeof (p), 0);
		RegisterSleepLock ();
		return 0;
		}

	if (cmd == sysAppLaunchCmdAlarmTriggered)
		{
		/* background, no-globals sublaunch: auto-lock -> drop the cached key */
		EncWipeKey ();
		return 0;
		}

	if (cmd == sysAppLaunchCmdNormalLaunch || cmd == kSecLaunchUnlock)
		{
		LoadPrefs ();
		RegisterSleepLock ();			/* Lock on Power-Off, any app (see above) */
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
			/* Keystroke / tap timing and position in the password dialogs feed
			 * the randomness pool (salts, serials -- see EncAddEntropy). The
			 * character itself is not added, only when and where. */
			if (e.eType == keyDownEvent || e.eType == penDownEvent)
				{
				UInt16 f = FrmGetActiveFormID ();
				if (f == frmPwSet || f == frmPwChange || f == frmUnlock
						|| f == frmResume || f == frmKeypad)
					{
					struct { UInt32 t; Int16 x, y; } ev;
					ev.t = TimGetTicks ();
					ev.x = e.screenX; ev.y = e.screenY;
					EncAddEntropy (&ev, sizeof (ev));
					}
				}
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
			if (! PwKeyboard (&e) && ! SysHandleEvent (&e))
				if (! MenuHandleEvent (0, &e, &err))	/* Options > Forget Password */
					if (! AppHandleEvent (&e))
						FrmDispatchEvent (&e);
		} while (e.eType != appStopEvent);
		WipePwBufs ();
		FrmCloseAllForms ();
		}
	return 0;
}
