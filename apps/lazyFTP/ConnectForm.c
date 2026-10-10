/*
 * ConnectForm.c - the 3-slot connect settings dialog (popup trigger swaps
 * between saved Server/Port/Username/Password sets). Password field is
 * always-masked (shadow real-value buffer + '*' display) - the core
 * technique (in-place FldDelete+FldInsert, not a fresh handle per
 * keystroke) matches Sec35's own FldSetContent, which fixed a real rare
 * "non-word-aligned handle" crash under memory pressure from the naive
 * free+realloc approach - read as reference here. The reveal-then-hide
 * masking (RedrawMaskedPass/PassFieldKeyDown/MaskPendingReveal below) ports
 * Sec35's own proven pattern (sec35.c: RedrawMaskedField/PwFieldKeyDown/
 * MaskPendingReveal) verbatim rather than reinventing it - Sec35's version
 * exists because a dangling reveal-then-mask timer once fired against
 * whichever form happened to be active after ITS dialog closed; that can't
 * happen here structurally, since lazyFTP's reveal timeout is only ever
 * watched by this dialog's OWN hand-rolled loop (see ConnectFormRun), which
 * exits exactly when the dialog does - there is no persistent app-level
 * loop left running afterward the way Sec35 has one. gRevealPending is
 * still cleared defensively at every point gFrm stops being valid, matching
 * Sec35's belt-and-suspenders convention anyway.
 */
#include "ConnectForm.h"
#include "LzTPSec35.h"
#include "PrefsForm.h"	/* gUiPrefs.retryCount - see ConnectFormRun's own comment */

/* Fixed placeholder shown (not a real masked length) when a slot HAS a
 * saved password but Sec35 is locked right now so it can't be decrypted -
 * distinguishes "a password is saved, you just can't see/use it yet" from
 * "no password saved" (both used to render as a blank field - the real
 * source of the "confusing" login reports, see ConnectFormRun's Connect-tap
 * handling below, which is the other half of this same root cause). */
#define lzPassPlaceholder "*****"
#define lzRevealTicks 1		/* seconds a just-typed character stays in clear, matching Sec35's kRevealTicks */

static LzTPConnPrefs gPrefs;
static UInt8         gActiveSlot;
/* Retry popup's current choice (1/2/3) - tracked explicitly here and kept
 * in sync via ConnectFormHandleEvent's own popSelectEvent case, same
 * established pattern as gActiveSlot for the Profile popup, rather than
 * trusting a deferred LstGetSelection() read at Connect-tap time (real
 * user report: "your retry count is wrong" - couldn't confirm a bug in the
 * actual retry-loop math, which a POSE64 test already showed doing exactly
 * 1=2 total tries as intended, but this deferred-read path had no proven
 * precedent in this file, unlike gActiveSlot's explicit tracking, so
 * hardening it removes that uncertainty either way). */
static UInt8          gRetryCount;
static char          gPassPlain[lzPassMaxLen + 1];
static Boolean       gPassDirty;		/* true once the user has typed/cleared it this session */
/* true while the field is showing lzPassPlaceholder rather than a real
 * (possibly empty) masked value - see LoadSlotIntoUI. */
static Boolean       gPassPlaceholderShown;
/* Reveal-then-hide state for the just-typed character, mirroring Sec35's
 * gRevealField/gRevealUntilTicks (see file header) - a plain Boolean is
 * enough here since this dialog only ever has the one password field. */
static Boolean       gRevealPending;
static UInt32        gRevealUntilTicks;
/* "Show" checkbox state - true shows gPassPlain in the clear (no masking,
 * no reveal-timer needed since everything's already visible), matching the
 * user's real-world need to actually verify what's saved after repeated
 * "Connection lost" reports traced to (at least partly) the control
 * connection's own read-timeout bug (see FtpProtocol.c's FtpPollReceive) -
 * this doesn't fix a protocol bug, but lets the user directly confirm
 * whether a bad password is ALSO part of what they're seeing. */
static Boolean       gPassShowPlain;
/* Explicit form pointer, NOT FrmGetActiveForm() - only reliable from inside
 * the dialog's own hand-rolled event loop in ConnectFormRun (see that
 * function's header comment for why it's hand-rolled rather than
 * FrmDoDialog-based in the first place). */
static FormPtr        gFrm;

static void *GetObj(UInt16 id)
{
	UInt16 idx;
	if (!gFrm) return NULL;
	idx = FrmGetObjectIndex(gFrm, id);
	if (idx == frmInvalidObjectId) return NULL;	/* defensive, matches Sec35's own GetObj - see file header */
	return FrmGetObjectPtr(gFrm, idx);
}

/* In-place field text replace (FldDelete+FldInsert) - see file header.
 * Real bug found+fixed this session: a field straight from its resource
 * (never typed into, never previously populated) has NO text handle at
 * all, and FldInsert silently no-ops on such a field instead of allocating
 * one itself - so loading saved prefs into a freshly-opened Connect dialog
 * left every field blank, even though the underlying save/load round-trip
 * was correct (confirmed via diagnostic). Typing always worked because
 * PalmOS's own keystroke handling allocates the handle on the first
 * character. Fix: give the field a fresh (empty) handle ONCE via
 * FldSetTextHandle if it doesn't have one yet, matching Address35's
 * EditSetRowValue precedent for populating a field's initial content -
 * then fall through to the normal in-place edit for everything after,
 * which is what keeps the per-keystroke path (RedrawMaskedPass) off the
 * repeated free+realloc pattern that caused Sec35's real crash (see file
 * header) - this only allocates once per field, not once per keystroke. */
static void LzTPFldSetContent(FieldType *fld, const char *text)
{
	UInt16 curLen;
	UInt16 newLen = (UInt16) StrLen(text);

	if (!FldGetTextHandle(fld)) {
		MemHandle h = MemHandleNew(1);
		if (h) {
			char *p = (char *) MemHandleLock(h);
			p[0] = '\0';
			MemHandleUnlock(h);
			FldSetTextHandle(fld, h);
		}
	}

	curLen = FldGetTextLength(fld);
	if (curLen > 0) {
		FldSetInsPtPosition(fld, curLen);
		FldDelete(fld, 0, curLen);
	}
	if (newLen > 0) {
		FldSetInsPtPosition(fld, 0);
		FldInsert(fld, text, newLen);
	}
}

/* revealLast: show the just-typed character in clear (Sec35's RedrawMaskedField
 * pattern - see file header). Does not touch gPassPlain. Ignored entirely
 * while the "Show" checkbox (gPassShowPlain) is on - the whole point of that
 * checkbox is showing everything, not just the last character. */
static void RedrawMaskedPass(Boolean revealLast)
{
	char disp[lzPassMaxLen + 1];
	UInt16 len = (UInt16) StrLen(gPassPlain), i;

	if (gPassShowPlain) {
		LzTPFldSetContent((FieldType *) GetObj(fldConnPass), gPassPlain);
		return;
	}

	for (i = 0; i < len; i++)
		disp[i] = (char) ((revealLast && i == len - 1) ? gPassPlain[i] : '*');
	disp[len] = '\0';
	LzTPFldSetContent((FieldType *) GetObj(fldConnPass), disp);
}

/* Masks whatever character is currently showing in clear - called once the
 * reveal window elapses (ConnectFormRun's own bounded EvtGetEvent timeout),
 * matching Sec35's identical MaskPendingReveal. */
static void MaskPendingReveal(void)
{
	if (gRevealPending) RedrawMaskedPass(false);
	gRevealPending = false;
	gRevealUntilTicks = 0;
}

static Boolean PassFieldKeyDown(EventType *e)
{
	UInt16 len;
	WChar c = e->data.keyDown.chr;

	if (gPassPlaceholderShown) {
		/* First keystroke while showing the "saved but locked" placeholder
		 * (see LoadSlotIntoUI) replaces it outright - there's no real
		 * plaintext behind lzPassPlaceholder to edit character-by-character. */
		gPassPlaceholderShown = false;
		gPassPlain[0] = '\0';
	}
	len = (UInt16) StrLen(gPassPlain);

	if (c == chrBackspace) {
		if (len > 0) gPassPlain[len - 1] = '\0';
		gPassDirty = true;
		gRevealPending = false;
		gRevealUntilTicks = 0;
		RedrawMaskedPass(false);
		return true;
	}
	if (TxtCharIsPrint(c) && len < lzPassMaxLen) {
		gPassPlain[len] = (char) c;
		gPassPlain[len + 1] = '\0';
		gPassDirty = true;
		RedrawMaskedPass(true);
		gRevealPending = true;
		gRevealUntilTicks = TimGetTicks() + (SysTicksPerSecond() * lzRevealTicks);
		return true;
	}
	return false;
}

/* Save the currently-shown fields back into gPrefs.slots[slot]. */
static void SaveUIIntoSlot(UInt8 slot)
{
	LzTPSlot *s = &gPrefs.slots[slot];
	char portStr[8];
	Boolean anon = (Boolean) CtlGetValue((ControlType *) GetObj(ctlConnAnon));

	StrNCopy(s->server, FldGetTextPtr((FieldType *) GetObj(fldConnServer)) ?
		FldGetTextPtr((FieldType *) GetObj(fldConnServer)) : "", sizeof(s->server) - 1);
	s->server[sizeof(s->server) - 1] = '\0';

	{
		char *p = FldGetTextPtr((FieldType *) GetObj(fldConnPort));
		s->port = (UInt16) (p && p[0] ? StrAToI(p) : 21);
	}
	(void) portStr;

	if (anon) {
		/* Anonymous checkbox is an explicit, unambiguous override - the
		 * Username/Password fields are inert while it's checked (see
		 * ConnectFormHandleEvent's keyDownEvent case), but force-clear here
		 * too rather than trusting field content, matching this suite's
		 * usual belt-and-suspenders convention for anything that becomes an
		 * actual saved credential. */
		s->user[0] = '\0';
		gPassPlain[0] = '\0';
		gPassPlaceholderShown = false;
		gPassDirty = true;
	} else {
		StrNCopy(s->user, FldGetTextPtr((FieldType *) GetObj(fldConnUser)) ?
			FldGetTextPtr((FieldType *) GetObj(fldConnUser)) : "", sizeof(s->user) - 1);
		s->user[sizeof(s->user) - 1] = '\0';
	}

	if (gPassDirty) {
		/* Only touch the saved password if the user actually edited it this
		 * session (or the anonymous checkbox just cleared it) - encrypts+
		 * saves if Sec35 is unlocked, otherwise leaves the slot's
		 * previously-saved (possibly still-encrypted) password untouched,
		 * matching the agreed "typing works without unlocking, just can't
		 * be saved yet" behavior. */
		LzTPEncryptSlotPassword(s, slot, gPassPlain);
	}
}

/* Populate the fields from gPrefs.slots[slot]. */
static void LoadSlotIntoUI(UInt8 slot)
{
	LzTPSlot *s = &gPrefs.slots[slot];
	char portStr[8];
	/* static, NOT stack-local - CtlSetLabel keeps a POINTER to what's
	 * passed, not a copy (same documented gotcha as Address35's
	 * CategorySetTriggerLabel: address35-category-and-pose64 memory,
	 * "stack buffer -> blank label"). A stack-local buffer here goes out
	 * of scope the moment this function returns, leaving the control's
	 * label pointer dangling - reliably reads back blank rather than
	 * garbage, which is exactly the symptom seen live in POSE64. */
	static char popupLabel[2];

	LzTPFldSetContent((FieldType *) GetObj(fldConnServer), s->server);
	StrIToA(portStr, s->port ? s->port : 21);
	LzTPFldSetContent((FieldType *) GetObj(fldConnPort), portStr);
	LzTPFldSetContent((FieldType *) GetObj(fldConnUser), s->user);

	gPassPlain[0] = '\0';
	gPassDirty = false;
	gPassPlaceholderShown = false;
	/* "Show" defaults off on every (re)load - don't carry a revealed state
	 * across slot switches, so switching to a DIFFERENT slot never shows
	 * that slot's password just because the checkbox was left on. */
	gPassShowPlain = false;
	/* A pending reveal must not outlive the slot it belongs to (same reason
	 * as Sec35's PwDone clearing gRevealField/gRevealUntilTicks - see file
	 * header) - switching slots mid-reveal is the one way this dialog's own
	 * gFrm-scoped state could otherwise show a stale character. */
	gRevealPending = false;
	gRevealUntilTicks = 0;

	if (s->passLen > 0) {
		if (LzTPDecryptSlotPassword(s, slot, gPassPlain, sizeof(gPassPlain))) {
			RedrawMaskedPass(false);
		} else {
			/* Saved password exists but Sec35 is locked right now - a fixed
			 * placeholder (not blank) tells the user a password WILL be
			 * used, rather than leaving them unable to tell "no password
			 * saved" apart from "one's saved but hidden" (see file header -
			 * this is also half of the "connect fails 2-3 times" root
			 * cause: the other half is ConnectFormRun's Connect-tap check). */
			gPassPlaceholderShown = true;
			LzTPFldSetContent((FieldType *) GetObj(fldConnPass), lzPassPlaceholder);
		}
	} else {
		RedrawMaskedPass(false);	/* genuinely no saved password - truly blank */
	}

	CtlSetValue((ControlType *) GetObj(ctlConnAnon),
		(Int16) (s->user[0] == '\0' && s->passLen == 0));
	CtlSetValue((ControlType *) GetObj(ctlConnShow), false);

	popupLabel[0] = (char) ('1' + slot);
	popupLabel[1] = '\0';
	{
		ControlType *trigger = (ControlType *) GetObj(popConnSlot);
		CtlSetLabel(trigger, popupLabel);
		/* CtlSetLabel doesn't auto-redraw the way FldInsert does for a
		 * field already on-screen - the trigger's OLD (resource-default
		 * blank) label kept showing until an explicit CtlDrawControl. */
		CtlDrawControl(trigger);
	}
	LstSetSelection((ListType *) GetObj(lstConnSlot), (Int16) slot);
}

SEC001 Boolean ConnectFormHandleEvent(EventPtr eventP)
{
	Boolean handled = false;

	switch (eventP->eType) {
	case keyDownEvent:
		{
			UInt16 focus = FrmGetFocus(gFrm);
			if ((focus == FrmGetObjectIndex(gFrm, fldConnUser) ||
					focus == FrmGetObjectIndex(gFrm, fldConnPass)) &&
					CtlGetValue((ControlType *) GetObj(ctlConnAnon))) {
				/* Anonymous checked - Username/Password are inert (see
				 * SaveUIIntoSlot's matching force-clear). */
				handled = true;
				break;
			}
			if (focus == FrmGetObjectIndex(gFrm, fldConnPass)) {
				handled = PassFieldKeyDown(eventP);
			}
		}
		break;

	case ctlSelectEvent:
		if (eventP->data.ctlSelect.controlID == ctlConnAnon) {
			/* Checking it now visually clears Username/Password right away,
			 * rather than only at Save time, so the "these fields are
			 * inert" state is never ambiguous on screen. */
			if (CtlGetValue((ControlType *) GetObj(ctlConnAnon))) {
				LzTPFldSetContent((FieldType *) GetObj(fldConnUser), "");
				gPassPlain[0] = '\0';
				gPassPlaceholderShown = false;
				gRevealPending = false;
				gRevealUntilTicks = 0;
				RedrawMaskedPass(false);
			}
		} else if (eventP->data.ctlSelect.controlID == ctlConnShow) {
			Boolean checked = (Boolean) CtlGetValue((ControlType *) GetObj(ctlConnShow));

			if (checked && gPassPlaceholderShown) {
				/* Nothing real to show yet - the field is only showing the
				 * "saved but locked" placeholder (see LoadSlotIntoUI). Try
				 * decrypting once more (Sec35 may have been unlocked since
				 * the dialog opened) before bouncing - same recovery as the
				 * Connect-tap check in ConnectFormRun below. */
				LzTPSlot *s = &gPrefs.slots[gActiveSlot];
				if (LzTPDecryptSlotPassword(s, gActiveSlot, gPassPlain, sizeof(gPassPlain))) {
					gPassPlaceholderShown = false;
				} else if (LzTPSec35Installed()) {
					/* Sec35-suite convention (CryptoLockedBounce and every
					 * other "35" app's identical pattern): bounce to Sec35's
					 * own unlock UI and flip back, instead of an alert the
					 * user has to dismiss and then separately go unlock
					 * manually. The pending app-switch surfaces as an
					 * appStopEvent in ConnectFormRun's own loop below, which
					 * already tears the dialog down cleanly on that event. */
					CtlSetValue((ControlType *) GetObj(ctlConnShow), false);
					LzTPSwitchToSec35();
					return true;
				} else {
					FrmCustomAlert(altErr2,
						"This profile's saved password can't be shown - Sec35 is not installed",
						"Type the password instead.", NULL);
					CtlSetValue((ControlType *) GetObj(ctlConnShow), false);
					checked = false;
				}
			}

			gPassShowPlain = checked;
			/* A stale reveal-last-char timer is redundant (RedrawMaskedPass
			 * already shows everything while gPassShowPlain is set) but
			 * clear it anyway rather than leave it ticking pointlessly. */
			gRevealPending = false;
			gRevealUntilTicks = 0;
			RedrawMaskedPass(false);
		}
		break;

	case popSelectEvent:
		if (eventP->data.popSelect.listID == lstConnSlot) {
			UInt8 newSlot = (UInt8) eventP->data.popSelect.selection;
			if (newSlot != gActiveSlot && newSlot < lzMaxSlots) {
				SaveUIIntoSlot(gActiveSlot);
				gActiveSlot = newSlot;
				LoadSlotIntoUI(gActiveSlot);
			}
		} else if (eventP->data.popSelect.listID == lstConnRetry) {
			/* Tracked explicitly the instant it changes - see gRetryCount's
			 * own comment at the top of this file for why, instead of a
			 * deferred LstGetSelection() read at Connect-tap time. */
			UInt8 newRetry = (UInt8) (eventP->data.popSelect.selection + 1);
			if (newRetry >= 1 && newRetry <= 3) gRetryCount = newRetry;
		}
		break;

	default:
		break;
	}
	return handled;
}

/* Real bug found+fixed this session: this dialog used to be driven by
 * FrmDoDialog + a FrmSetEventHandler-attached frmOpenEvent handler that
 * populated the fields (the normal pattern, and what every other dialog in
 * this suite does) - but the fields NEVER actually got populated on first
 * open (confirmed empty via ReControl "ui" introspection AND a screenshot,
 * not just a redraw glitch; confirmed NOT about missing text handles or
 * FrmDrawForm ordering either - even a raw literal FldInsert("TEST") call
 * silently failed the same way). Root cause: this codebase has ALREADY hit
 * and documented this exact FrmDoDialog quirk elsewhere (CryptoNote's
 * CardCrypt.c CardPromptPassword, lib/datepick.c, CryptoNote MainForm.c) -
 * FrmDoDialog does not reliably dispatch frmOpenEvent through a custom
 * handler (undocumented/version-specific SDK/glue behavior). Fix: drop
 * FrmDoDialog entirely and drive our own minimal modal event loop instead,
 * matching CardPromptPassword's exact proven pattern - populate the fields
 * directly, inline, no frmOpenEvent dependency at all. */
SEC001 Boolean ConnectFormRun(FtpConnParams *outParams)
{
	FormPtr frm;
	FormPtr prevFrm;
	UInt16 fieldIndex;
	EventType event;
	Boolean done = false;
	UInt16 btn = 0;
	/* static, not stack-local - CtlSetLabel keeps a POINTER not a copy, same
	 * gotcha as LoadSlotIntoUI's own popupLabel (see its comment). */
	static char retryPopupLabel[2];

	LzTPLoadPrefs(&gPrefs);

	prevFrm = FrmGetActiveForm();
	frm = FrmInitForm(frmConnect);
	if (!frm) return false;
	gFrm = frm;

	gActiveSlot = gPrefs.activeSlot;
	if (gActiveSlot >= lzMaxSlots) gActiveSlot = 0;

	FrmSetActiveForm(frm);
	LoadSlotIntoUI(gActiveSlot);

	/* Real-hardware bug found this session: a screen-artifact report traced
	 * to this exact block - it used to run HERE, before FrmDrawForm(frm)
	 * below. frmConnect is a non-fullscreen MODAL frame form, and
	 * FrmDrawForm does an implicit "capture what's behind me" step as part
	 * of painting the form (matching this file's own existing comment on
	 * FrmEraseForm: "FrmDrawForm above auto-saved the bits behind it").
	 * Calling CtlDrawControl on this popup trigger BEFORE that capture ran
	 * meant the capture included THIS premature draw instead of frmMain's
	 * real pixels underneath - so when the dialog later closed, it restored
	 * the corrupted snapshot (the trigger's own rendered image) instead of
	 * the real background, leaving it stuck on screen permanently. Only
	 * showed up on the auto-launched first dialog specifically because
	 * that's the case where frmMain's true background under this exact
	 * screen position hadn't been drawn over by anything else since - once
	 * corrupted, later manual Connect opens don't re-corrupt anything
	 * further, so only the first one was ever visibly wrong. Fixed by
	 * moving this whole block to AFTER FrmDrawForm(frm) instead - by then
	 * the form's own background-capture has already happened, so nothing
	 * this code draws can be mistaken for "the background" anymore. */
	FrmDrawForm(frm);

	/* "Retry:" popup - a global UI preference (gUiPrefs.retryCount), not
	 * per-slot, so it's set up once here rather than inside LoadSlotIntoUI
	 * (which would needlessly re-run this identically on every slot
	 * switch). See PrefsForm.h's own comment on retryCount for why it's
	 * global rather than saved per connection slot. gRetryCount (not
	 * gUiPrefs.retryCount directly) is what the rest of this dialog reads/
	 * updates from here on - see its own comment at the top of this file. */
	gRetryCount = gUiPrefs.retryCount;
	retryPopupLabel[0] = (char) ('0' + gRetryCount);
	retryPopupLabel[1] = '\0';
	CtlSetLabel((ControlType *) GetObj(popConnRetry), retryPopupLabel);
	CtlDrawControl((ControlType *) GetObj(popConnRetry));
	LstSetSelection((ListType *) GetObj(lstConnRetry), (Int16) (gRetryCount - 1));

	fieldIndex = FrmGetObjectIndex(frm, fldConnServer);
	FrmSetFocus(frm, fieldIndex);
	FldGrabFocus((FieldType *) FrmGetObjectPtr(frm, fieldIndex));

	while (!done) {
		/* Bounded timeout while a just-typed character is showing in clear
		 * (gRevealPending/gRevealUntilTicks) - matches Sec35's identical
		 * PilotMain event-loop idiom (see file header) so the reveal always
		 * expires on schedule, not just on the next keystroke. */
		UInt32 timeout = evtWaitForever;
		if (gRevealPending) {
			UInt32 now = TimGetTicks();
			timeout = (gRevealUntilTicks > now) ? (gRevealUntilTicks - now) : 0;
		}
		EvtGetEvent(&event, timeout);

		if (event.eType == nilEvent) {
			if (gRevealPending && TimGetTicks() >= gRevealUntilTicks)
				MaskPendingReveal();
			continue;
		}
		if (event.eType == appStopEvent) {
			EvtAddEventToQueue(&event);
			btn = btnConnCancel;
			break;
		}
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, NULL)) continue;
		if (ConnectFormHandleEvent(&event)) continue;

		if (event.eType == ctlSelectEvent) {
			if (event.data.ctlSelect.controlID == btnConnConnect) {
				Boolean anon = (Boolean) CtlGetValue((ControlType *) GetObj(ctlConnAnon));
				Boolean blocked = false;

				if (!anon && gPassPlaceholderShown && !gPassDirty) {
					/* Connect was about to fire with an empty password even
					 * though this slot HAS one saved, just because Sec35
					 * happens to be locked right now - the exact "confusing,
					 * fails 2-3 times" symptom reported (a blank password
					 * sent to a server that requires one just reads as a
					 * generic auth failure, with nothing telling the user
					 * why). Try once more (Sec35 may have been unlocked
					 * since this dialog opened) before bouncing. */
					LzTPSlot *s = &gPrefs.slots[gActiveSlot];
					if (LzTPDecryptSlotPassword(s, gActiveSlot, gPassPlain, sizeof(gPassPlain))) {
						gPassPlaceholderShown = false;
						RedrawMaskedPass(false);
					} else if (LzTPSec35Installed()) {
						/* Sec35-suite convention (CryptoLockedBounce and
						 * every other "35" app's identical pattern): bounce
						 * to Sec35's own unlock UI and flip back, instead of
						 * an alert. Treated like Cancel here (no saved
						 * server/user/port from this in-progress dialog
						 * survives - same tradeoff every other bounce in
						 * this suite already makes); the pending app-switch
						 * also surfaces as an appStopEvent at the top of
						 * this same loop, which handles it identically. */
						LzTPSwitchToSec35();
						btn = btnConnCancel;
						blocked = true;
						done = true;
					} else {
						FrmCustomAlert(altErr2,
							"This profile has a saved password, but Sec35 is not installed so it can't be used",
							"Type the password instead.", NULL);
						blocked = true;
					}
				}

				if (!blocked) {
					btn = btnConnConnect;
					done = true;
				}
			} else if (event.data.ctlSelect.controlID == btnConnCancel) {
				btn = btnConnCancel;
				done = true;
			}
		}

		FrmHandleEvent(frm, &event);
	}

	/* A pending reveal must not outlive this dialog (see file header for why
	 * that can't actually happen structurally here, and LoadSlotIntoUI's
	 * matching comment) - cleared anyway as cheap, defensive insurance. */
	gRevealPending = false;
	gRevealUntilTicks = 0;

	if (btn == btnConnConnect) {
		SaveUIIntoSlot(gActiveSlot);
		gPrefs.activeSlot = gActiveSlot;
		LzTPSavePrefs(&gPrefs);

		/* gRetryCount -> gUiPrefs (global, not per-slot - see its own
		 * comment in PrefsForm.h) -> outParams, committed here same as the
		 * slot fields just above, not on every popup tap. */
		gUiPrefs.retryCount = gRetryCount;
		LzTPSaveUiPrefs();

		MemSet(outParams, sizeof(*outParams), 0);
		StrCopy(outParams->server, gPrefs.slots[gActiveSlot].server);
		outParams->port = gPrefs.slots[gActiveSlot].port ? gPrefs.slots[gActiveSlot].port : 21;
		StrCopy(outParams->user, gPrefs.slots[gActiveSlot].user);
		StrCopy(outParams->pass, gPassPlain);	/* freshly known plaintext - no decrypt round-trip needed */
		outParams->secMode = kFtpSecNone;
		outParams->retryCount = gUiPrefs.retryCount;
	}

	/* frmConnect is not full-screen (AT 2 20 156 136) so FrmDrawForm above
	 * auto-saved the bits behind it - FrmEraseForm restores them before
	 * FrmDeleteForm, same as CardPromptPassword's identical situation. */
	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	gFrm = NULL;

	/* FrmDoDialog also restores the caller's underlying form as active
	 * internally - our hand-rolled loop must do this explicitly, or
	 * frmMain is left unresponsive/in a stale state once this dialog is
	 * gone (real bug found+fixed this session, matching CardCrypt.c's own
	 * documented CardPromptPassword gotcha exactly: "Connect" appearing to
	 * do nothing on a real device, and a real address-error crash in
	 * POSE64 immediately after - both traced to this missing restore). */
	if (prevFrm) {
		FrmSetActiveForm(prevFrm);
		FrmDrawForm(prevFrm);
	}

	MemSet(gPassPlain, sizeof(gPassPlain), 0);
	return (Boolean) (btn == btnConnConnect);
}
