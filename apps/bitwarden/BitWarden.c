/*
 * BitWarden.c - app entry point. Same PilotMain/EventLoop/
 * ApplicationHandleEvent(frmLoadEvent) shape as every other app in this
 * suite (Address35's own Address35.c is the closest reference). v1 only
 * needs sysAppLaunchCmdNormalLaunch - no beaming, no global Find, no
 * sublaunch receive paths (nothing here is meant to run without globals).
 */
#include "BitWarden.h"
#include "enccore.h"

/* Not an SDK call - every app in this suite defines its own copy of this
 * exact helper (see Address35.c's own RomVersionCompatible). */
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

/* About is a plain ALERT (altAbout), shown via FrmAlert from MainForm.c's
 * menu handler - no separate About FORM needed for an app this small. */
static Boolean ApplicationHandleEvent(EventPtr e)
{
	FormPtr frm;
	UInt16 formID;

	if (e->eType != frmLoadEvent) return false;

	formID = e->data.frmLoad.formID;
	frm = FrmInitForm(formID);
	FrmSetActiveForm(frm);

	switch (formID) {
	case frmMain:
		FrmSetEventHandler(frm, MainFormHandleEvent);
		break;
	case frmRecordLogin:
	case frmRecordNote:
	case frmRecordCard:
	case frmRecordIdentity:
		FrmSetEventHandler(frm, RecordFormHandleEvent);
		break;
	case frmPrefs:
		FrmSetEventHandler(frm, PrefsFormHandleEvent);
		break;
	}
	return true;
}

/* Real-device feedback: the sysNotifySleepNotifyEvent-based wipe (below)
 * did not reliably fire - after an auto-off sleep+wake, the cached master
 * password was still usable, syncing without a re-prompt. Rather than trust
 * a notification we can't verify fires on every device/ROM, detect sleep
 * directly: EvtGetEvent(evtWaitForever) blocks for the ENTIRE sleep
 * duration (no events are delivered while the device is off), so a large
 * jump in wall-clock time between two consecutive events is a reliable,
 * hardware-independent sleep signal regardless of whether any OS
 * notification fired. 15s threshold matches this project's own established
 * "not too eager, not too slow" convention (see Serial.c's own auto-off
 * timer reset throttle) - long enough that normal reading/thinking pauses
 * between taps don't false-trigger, short enough to catch a real sleep
 * (which requires at least the device's own auto-off timeout to elapse)
 * quickly. Compared against gLastActiveTime (BwTouchActivity, BitWarden.h)
 * rather than a purely local variable - see that comment for why: without
 * it, this false-triggered on every normal unlock and every sync. */
#define bwSleepGapSeconds	15

static UInt32 gLastActiveTime = 0;

void BwTouchActivity(void)
{
	gLastActiveTime = TimGetSeconds();
}

void BwKeepDeviceAwake(void)
{
	/* REVERTED from EvtSetAutoOffTimer(SetAtLeast, bwAutoOffFloorSeconds) -
	 * real-device testing showed that version causing the device to sleep
	 * SOONER, not later (mid-PBKDF2-wait, well before any prior build ever
	 * had trouble there), losing the cached master password before the 2FA
	 * prompt could even be answered - the opposite of the intended effect,
	 * for reasons this project has no way to verify further (no documented
	 * local precedent for EvtSetAutoOffTimer at all, unlike
	 * EvtResetAutoOffTimer, which is already proven working in this exact
	 * spot - see PollKeepAlive - and in Address35/Sec35 elsewhere in the
	 * suite for the identical class of problem). Back to the proven call,
	 * just centralized here so BwStoreAddItem's own (newly added) keep-
	 * alive coverage shares the same function as Serial.c's. */
	EvtResetAutoOffTimer();
}

static void EventLoop(void)
{
	EventType event;
	UInt16 error;

	do {
		/* Normally evtWaitForever (matches the sleep-detection heuristic
		 * right below, which relies on EvtGetEvent blocking for the WHOLE
		 * sleep duration with nothing in between) - but frmMain's own
		 * debounced search (MainForm.c) needs a periodic nilEvent to check
		 * whether its 1-second timer has elapsed, and evtWaitForever means
		 * nilEvent never fires at all until some REAL event arrives,
		 * defeating the whole point of "wait for a pause in typing".
		 * Narrowly scoped to exactly when it's needed (frmMain active AND
		 * a search is actually pending, or frmRecordLogin active with a
		 * live TOTP countdown - RecordHasTotpActive) so this doesn't
		 * affect the sleep-detection gap-heuristic, battery, or
		 * responsiveness anywhere else in the app. */
		Int32 timeout = evtWaitForever;
		if (FrmGetFormId(FrmGetActiveForm()) == frmMain && MainHasPendingSearch())
			timeout = SysTicksPerSecond() / 5;	/* 200ms - responsive enough that the 1s debounce doesn't feel late */
		else if (RecordHasTotpActive())
			timeout = SysTicksPerSecond();	/* 1s - matches the countdown's own display granularity */
		EvtGetEvent(&event, timeout);
		{
			UInt32 now = TimGetSeconds();
			if (now < gLastActiveTime || (now - gLastActiveTime) > bwSleepGapSeconds) {
				/* BwWipeCachedPassword() deliberately NOT called here any
				 * more (2026-08 behavior change) - the cached master
				 * password now persists across sleep/app-exit, governed
				 * only by its own TTL (BwGetTtlSeconds, "Lock After") and
				 * by Sec35's own key wrapping it - see Store.c's own
				 * comment on BwGetCachedPassword/BwHasCachedPassword.
				 * BwRecordDataWipe still runs: an open record's DECRYPTED
				 * FIELDS on screen are a display-hygiene concern
				 * independent of the password cache question. */
				BwRecordDataWipe();
				/* A record left open across the gap still has its real,
				 * already-decrypted fields sitting on screen (see
				 * BwRecordDataWipe's own comment) - wiping the buffer alone
				 * doesn't repaint those pixels, so force back to the list,
				 * which redraws everything masked via MainDrawRow's own
				 * EncKeyPresent check. Safe to call FrmGotoForm here (unlike
				 * from the async sleep-notify callback below) - this runs in
				 * EventLoop's own normal, synchronous dispatch context. */
				switch (FrmGetFormId(FrmGetActiveForm())) {
				case frmRecordLogin:
				case frmRecordNote:
				case frmRecordCard:
				case frmRecordIdentity:
					FrmGotoForm(frmMain);
					break;
				default:
					break;
				}
			}
		}
		BwTouchActivity();
		/* Real-device report (2026-09): "the timeout should occur from
		 * last use" - the local-vault/Sec35/cached-password TTLs
		 * ("Lock After") only ever got renewed by an explicit BwUnlock or
		 * during a sync (BwTouchSessionKeys, Sync.c) - plain browsing
		 * (opening records, scrolling, filtering) never touched them at
		 * all, so the vault could lock mid-session purely because enough
		 * wall-clock time had passed since the LAST unlock/sync, even with
		 * the user actively using the app the whole time. Gated on a REAL
		 * event (not nilEvent, which fires periodically on its own for the
		 * debounced-search/TOTP-countdown timers above and isn't user
		 * activity) so genuine inactivity still locks normally -
		 * self-throttled inside BwTouchSessionKeys itself (~20s), so
		 * calling this on every keystroke/tap costs nothing extra. */
		if (event.eType != nilEvent) BwTouchSessionKeys();
		if (!SysHandleEvent(&event))
			if (!MenuHandleEvent(0, &event, &error))
				if (!ApplicationHandleEvent(&event))
					FrmDispatchEvent(&event);
	} while (event.eType != appStopEvent);
}

/* Wipes an open record's decrypted-fields-on-screen buffer on sleep -
 * independent of EncRegisterSleepWipe's own callback, which only wipes the
 * shared Sec35 key itself. Same SysNotifyProcPtr shape as enccore.c's
 * EncSleepNotifyCallback. Used to also wipe the cached master password
 * here (BwWipeCachedPassword) - deliberately removed (2026-08 behavior
 * change): the cache now persists across sleep/app-exit on purpose, see
 * Store.c's own comment on BwGetCachedPassword/BwHasCachedPassword. */
static Err BwSleepNotifyCallback(SysNotifyParamType *notifyParamsP)
{
	(void) notifyParamsP;
	BwRecordDataWipe();	/* defense in depth - plain MemSet, safe in any callback context */
	return errNone;
}

static Err StartApplication(void)
{
	UInt16 cardNo;
	LocalID dbID;

	if (!RomVersionCompatible(sysMakeROMVersion(4, 0, 0, sysROMStageRelease, 0)))
		return sysErrRomIncompatible;
	BwStoreInit();
	BwTouchActivity();	/* baseline - without this the first EventLoop iteration sees a bogus gap from 0 */
	EncRegisterSleepWipe();	/* Sec35 "Lock on Power-Off" for the shared key itself */
	if (SysCurAppDatabase(&cardNo, &dbID) == errNone)
		SysNotifyRegister(cardNo, dbID, sysNotifySleepNotifyEvent,
			BwSleepNotifyCallback, sysNotifyNormalPriority, NULL);
	return errNone;
}

static void StopApplication(void)
{
	UInt16 cardNo;
	LocalID dbID;

	if (SysCurAppDatabase(&cardNo, &dbID) == errNone)
		SysNotifyUnregister(cardNo, dbID, sysNotifySleepNotifyEvent, sysNotifyNormalPriority);
	EncUnregisterSleepWipe();
	/* BwWipeCachedPassword() deliberately NOT called here any more
	 * (2026-08 behavior change) - the cache is meant to survive app exit
	 * now, governed only by its own TTL + Sec35's key. See Store.c's own
	 * comment on BwGetCachedPassword/BwHasCachedPassword. */
	FrmCloseAllForms();
	BwStoreClose();
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
	Err err;
	(void) cmdPBP;
	(void) launchFlags;

	if (cmd == sysAppLaunchCmdNormalLaunch) {
		err = StartApplication();
		if (err != errNone) return err;

		/* Sec35-mandatory gate - BitWarden refuses to run at all without it
		 * (user-confirmed design, unlike CryptoNote/Datebook35's own
		 * optional integration). Real-device report (2026-09): this used to
		 * ALSO hard-refuse whenever BwSec35PasswordSet() (EncEncryptionActive)
		 * was false - but that's a Feature Manager flag Sec35 itself only
		 * (re)asserts from its own persisted prefs when SEC35 ITSELF actually
		 * launches (sec35.c's own LoadPrefs). If Sec35 hasn't been opened
		 * yet this boot session, that flag reads "false" even when a real,
		 * previously-configured password is sitting in Sec35's own
		 * persistent prefs - BitWarden wrongly showed "not installed or no
		 * password has been set" and refused to launch instead of bouncing
		 * over, exactly the report here. Fixed: only "Sec35 has no database
		 * at all" (BwSec35Installed - a straightforward, session-independent
		 * existence check) is a hard refuse now. Everything else - locked,
		 * or simply not yet active this session - is handled uniformly by
		 * the EncKeyPresent() bounce below; Sec35 itself decides once
		 * launched whether that means "unlock" or "set up a password for
		 * the first time", not BitWarden. */
		if (!BwSec35Installed()) {
			FrmCustomAlert(altErr,
				"Sec35 is not installed. Launch will not proceed as items will not be encrypted at rest.",
				NULL, NULL);
			StopApplication();
			return 0;
		}
		if (!EncKeyPresent()) {
			/* Bounce straight to Sec35's unlock UI before showing any of
			 * our own UI - Sec35 switches back here via a normal re-launch
			 * on success, which re-runs this same check and falls through. */
			BwSwitchToSec35();
			StopApplication();
			return 0;
		}

		FrmGotoForm(frmMain);
		EventLoop();
		StopApplication();
	}
	return 0;
}
