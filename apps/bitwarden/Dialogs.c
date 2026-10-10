/*
 * Dialogs.c - transient modal popups (master password entry, TOFU trust
 * prompt, sync progress). Same hand-rolled blocking event loop as
 * lazyFTP's PromptForm.c (FrmDoDialog doesn't reliably reach a custom
 * handler's frmOpenEvent on this SDK/glue - proven finding, reused fix).
 */
#include "BitWarden.h"

/* Forward decl - BwTwoFactorPromptRun (below) uses the numeric keypad
 * popup, whose own definition lives further down this file next to its
 * helper functions (KeypadDrawKey/KeypadHitTest). bwKeypadMaxLen matches
 * fldTwoFactorCode's own MAXCHARS 32 (BitWardenEn.rcp). */
#define bwKeypadMaxLen	32
static Boolean BwTwoFactorKeypadRun(char *outCode, UInt16 outLen) SEC001;

/* Same in-place field-content idiom as lazyFTP's PromptForm.c
 * SetFieldContent - a field straight from its resource has no text handle
 * yet, and FldInsert silently no-ops without one. Cursor-based (FldDelete/
 * FldInsert) - safe and REQUIRED for a field the user is actively editing
 * (this file's own password-entry fields go through PwRedraw on every
 * keystroke while genuinely focused; PrefsForm.c's own separate copy of
 * this same idiom has the same requirement).
 *
 * CRASH FIX (2026-08): a prior version of this function replaced the
 * cursor-based calls with a direct MemHandleFree+MemHandleNew rewrite,
 * to fix the 2FA keypad's own UNFOCUSED fields (see
 * SetUnfocusedFieldContent below for why those need different handling).
 * That rewrite was applied to THIS function - the one PwRedraw also
 * calls on every keystroke of the actively-focused master-password field
 * - and caused a real-device Fatal Exception: yanking a field's text
 * handle out from under an in-progress Field Manager edit session (intercepted
 * keystrokes, Graffiti/undo state) isn't a supported operation, unlike
 * FldDelete/FldInsert which the Field Manager expects and coordinates
 * with internally. Reverted to cursor-based for every focused-field use;
 * SetUnfocusedFieldContent is the ONLY place that still needs the direct
 * rewrite, and it's never called on a field that has ever had focus. */
static void SetFieldContent(FieldType *fld, const char *text)
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
	if (curLen > 0) { FldSetInsPtPosition(fld, curLen); FldDelete(fld, 0, curLen); }
	if (newLen > 0) { FldSetInsPtPosition(fld, 0); FldInsert(fld, text, newLen); }
}

/* Direct-handle-rewrite variant, ONLY for fields that are known to never
 * have had focus - the 2FA numeric-keypad flow deliberately skips
 * FrmSetFocus/FldGrabFocus entirely (the keypad popup, not the field,
 * owns input - see BwTwoFactorPromptRun's own comment), and on such a
 * field FldSetInsPtPosition/FldDelete were observed (POSE64) to silently
 * no-op, leaving FldInsert appending at a stale end-of-text position
 * instead of replacing content. Never call this on a field the user can
 * type into. */
static void SetUnfocusedFieldContent(FieldType *fld, const char *text)
{
	UInt16 newLen = (UInt16) StrLen(text);
	MemHandle h = FldGetTextHandle(fld);
	char *p;

	if (h) {
		FldSetTextHandle(fld, NULL);
		MemHandleFree(h);
	}
	h = MemHandleNew((UInt32) newLen + 1);
	if (!h) return;
	p = (char *) MemHandleLock(h);
	MemMove(p, text, newLen);
	p[newLen] = '\0';
	MemHandleUnlock(h);
	FldSetTextHandle(fld, h);
	FldRecalculateField(fld, false);
}

/* Password masking, modeled directly on sec35.c's own RedrawMaskedField/
 * PwFieldKeyDown/PwDone (proven, and specifically crash-fixed once already
 * - see project memory "Sec35 Text Entry reveal-timer crash" - a dangling
 * reveal-then-mask timer firing against whatever form became active AFTER
 * the password dialog closed). The real value lives ONLY in gPwReal, never
 * in the FIELD's own displayed text (which shows '*'s, plus the
 * just-typed character briefly in clear like a phone keyboard) - reading
 * FldGetTextPtr for the real password would be wrong once masking is on.
 * "Show" is a persistent full-reveal toggle layered on top of that,
 * per explicit design instruction - lazyFTP's own Connect dialog has the
 * same kind of checkbox (ctlConnShow) as further precedent. */
#define bwPwBufLen 128
static char gPwReal[bwPwBufLen];
static Boolean gPwRevealActive;	/* just-typed last char is showing in clear */
static UInt32 gPwRevealUntilTicks;
static Boolean gPwShowAll;			/* "Show" checkbox - full reveal */
#define bwPwRevealTicks	1			/* seconds a just-typed character stays in clear, matches sec35's own kRevealTicks */

static void PwRedraw(FieldType *fld)
{
	char disp[bwPwBufLen];
	UInt16 len = (UInt16) StrLen(gPwReal), i;
	for (i = 0; i < len; i++)
		disp[i] = (char) ((gPwShowAll || (gPwRevealActive && i == len - 1)) ? gPwReal[i] : '*');
	disp[len] = 0;
	SetFieldContent(fld, disp);
	FldSetInsPtPosition(fld, len);
	FldDrawField(fld);
}

/* Returns true if the key was consumed (the Field Manager's own default
 * handling, which would otherwise show the RAW typed text, must not also
 * run) - same append/backspace-only model as sec35's own PwFieldKeyDown. */
static Boolean PwKeyDown(FieldType *fld, EventType *e)
{
	WChar c = e->data.keyDown.chr;
	UInt16 len = (UInt16) StrLen(gPwReal);

	if (c == chrBackspace) {
		if (len > 0) gPwReal[len - 1] = 0;
		gPwRevealActive = false;
		PwRedraw(fld);
		return true;
	}
	if (TxtCharIsPrint(c) && len < bwPwBufLen - 1) {
		gPwReal[len] = (char) c;
		gPwReal[len + 1] = 0;
		gPwRevealActive = true;
		gPwRevealUntilTicks = TimGetTicks() + SysTicksPerSecond() * bwPwRevealTicks;
		PwRedraw(fld);
		return true;
	}
	return false;
}

Boolean BwUnlockPromptRun(char *outPassword, UInt16 outLen)
{
	FormPtr frm, prevFrm;
	FieldPtr fld;
	UInt16 fieldIndex;
	EventType event;
	Boolean done = false;
	UInt16 btn = 0;

	gPwReal[0] = 0;
	gPwRevealActive = false;
	gPwRevealUntilTicks = 0;
	gPwShowAll = false;

	prevFrm = FrmGetActiveForm();
	frm = FrmInitForm(frmUnlock);
	if (!frm) return false;

	FrmSetActiveForm(frm);
	fieldIndex = FrmGetObjectIndex(frm, fldUnlockPassword);
	fld = (FieldPtr) FrmGetObjectPtr(frm, fieldIndex);
	SetFieldContent(fld, "");
	CtlSetValue((ControlType *) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, chkUnlockShow)), false);

	FrmDrawForm(frm);
	FrmSetFocus(frm, fieldIndex);
	FldGrabFocus(fld);

	while (!done) {
		/* Bounded wait (not polling) only while a reveal is actually
		 * pending, so its timer expires even with no further input -
		 * same idiom as sec35's own PilotMain event loop, just scoped to
		 * this one dialog's own loop instead of the app-wide one. */
		EvtGetEvent(&event, gPwRevealActive ? (SysTicksPerSecond() / 4) : evtWaitForever);
		/* This loop is its own separate EvtGetEvent source, outside
		 * EventLoop's own top-level one - without this, typing a real
		 * password (which legitimately takes real seconds) would look like
		 * a sleep gap the moment this dialog closes and control returns to
		 * EventLoop (see BitWarden.h's BwTouchActivity comment). */
		BwTouchActivity();

		if (event.eType == nilEvent) {
			if (gPwRevealActive && TimGetTicks() >= gPwRevealUntilTicks) {
				gPwRevealActive = false;
				PwRedraw(fld);
			}
			continue;
		}
		if (event.eType == appStopEvent) { EvtAddEventToQueue(&event); btn = btnUnlockCancel; break; }
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, NULL)) continue;

		if (event.eType == keyDownEvent && event.data.keyDown.chr == chrLineFeed) {
			btn = btnUnlockOk; done = true;
		} else if (event.eType == keyDownEvent && FrmGetFocus(frm) == fieldIndex) {
			if (PwKeyDown(fld, &event)) continue;	/* consumed - do not also run FrmHandleEvent's default field handling */
		} else if (event.eType == ctlSelectEvent) {
			if (event.data.ctlSelect.controlID == btnUnlockOk) { btn = btnUnlockOk; done = true; }
			else if (event.data.ctlSelect.controlID == btnUnlockCancel) { btn = btnUnlockCancel; done = true; }
			else if (event.data.ctlSelect.controlID == chkUnlockShow) {
				gPwShowAll = event.data.ctlSelect.on;
				PwRedraw(fld);
			}
		}
		FrmHandleEvent(frm, &event);
	}

	if (btn == btnUnlockOk) {
		StrNCopy(outPassword, gPwReal, outLen - 1);
		outPassword[outLen - 1] = '\0';
	}

	/* Clear the field's own text handle, and - critically, per sec35's own
	 * documented fix - the reveal timer state, before tearing the form
	 * down: a pending reveal outliving this dialog would otherwise fire
	 * against whatever form is active next. */
	SetFieldContent(fld, "");
	MemSet(gPwReal, sizeof(gPwReal), 0);
	gPwRevealActive = false;
	gPwRevealUntilTicks = 0;
	gPwShowAll = false;

	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prevFrm) { FrmSetActiveForm(prevFrm); FrmDrawForm(prevFrm); }
	return btn == btnUnlockOk;
}

/* Vaultwarden 2FA challenge (PROTOCOL.md's TWO_FACTOR_REQUIRED), run from
 * Sync.c's own receive loop mid-sync - same blocking-dialog shape as
 * BwUnlockPromptRun above, but far simpler: the code isn't masked (no
 * PwKeyDown/PwRedraw needed - a 2FA code isn't as sensitive as a password,
 * and Show/Hide has no purpose here), so this is just a plain EDITABLE
 * field PalmOS's own default Field Manager handling already covers via the
 * closing FrmHandleEvent call. providerLabel is only read here (not
 * copied) - safe because this call is fully synchronous/blocking, so the
 * caller's own buffer (Sync.c's local, decoded straight off the wire) stays
 * valid for this whole function's duration. */
Boolean BwTwoFactorPromptRun(const char *providerLabel, char *outCode, UInt16 outLen)
{
	FormPtr frm, prevFrm;
	FieldPtr labelFld, codeFld;
	UInt16 codeFieldIndex;
	EventType event;
	Boolean done = false;
	UInt16 btn = 0;

	prevFrm = FrmGetActiveForm();
	frm = FrmInitForm(frmTwoFactor);
	if (!frm) return false;

	FrmSetActiveForm(frm);
	labelFld = (FieldPtr) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, fldTwoFactorLabel));
	FldSetTextPtr(labelFld, (char *) (providerLabel && providerLabel[0] ? providerLabel : "Two-factor code"));
	FldRecalculateField(labelFld, false);	/* MULTIPLELINES field - see SetWrappedText's own comment on why this is needed */

	codeFieldIndex = FrmGetObjectIndex(frm, fldTwoFactorCode);
	codeFld = (FieldPtr) FrmGetObjectPtr(frm, codeFieldIndex);
	SetFieldContent(codeFld, "");

	FrmDrawForm(frm);

	/* Options > Security > 2FA - Numeric Keypad mode: launch the keypad
	 * right away instead of taking Graffiti/keyboard input directly
	 * (mirrors Sec35's own "auto-focus is skipped in Numeric Keypad mode"
	 * gotcha - see BwTwoFactorKeypadRun's own comment - so the keypad
	 * popup, not the field itself, owns the first tap/keystroke). Tapping
	 * the field again later (below, in the main loop) re-opens it, same
	 * as Sec35's own re-tap-to-reopen behavior. */
	if (BwGetTwoFactorEntryMode() == bwTwoFactorEntryNumeric) {
		char code[bwKeypadMaxLen + 1];
		Boolean keypadOk = BwTwoFactorKeypadRun(code, sizeof(code));
		/* codeFld never has focus in numeric mode - see SetUnfocusedFieldContent's
		 * own comment on why the plain SetFieldContent above can't be used here. */
		if (keypadOk) { SetUnfocusedFieldContent(codeFld, code); FldDrawField(codeFld); }
		MemSet(code, sizeof(code), 0);
		/* OK on the keypad submits the whole 2FA prompt immediately - no
		 * need for a second tap on this form's own OK button (the user's
		 * own ask: numeric entry should be a single confirm, not two). A
		 * keypad Cancel does NOT submit - falls through to the loop below
		 * so the user can still retry via the (now still-empty) field. */
		if (keypadOk) { btn = btnTwoFactorOk; done = true; }
	} else {
		FrmSetFocus(frm, codeFieldIndex);
		FldGrabFocus(codeFld);
	}

	while (!done) {
		EvtGetEvent(&event, evtWaitForever);
		BwTouchActivity();	/* own separate loop - see BwUnlockPromptRun's own comment on this */

		if (event.eType == appStopEvent) { EvtAddEventToQueue(&event); btn = btnTwoFactorCancel; break; }
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, NULL)) continue;

		if (event.eType == keyDownEvent && event.data.keyDown.chr == chrLineFeed) {
			btn = btnTwoFactorOk; done = true;
		} else if (event.eType == ctlSelectEvent) {
			if (event.data.ctlSelect.controlID == btnTwoFactorOk) { btn = btnTwoFactorOk; done = true; }
			else if (event.data.ctlSelect.controlID == btnTwoFactorCancel) { btn = btnTwoFactorCancel; done = true; }
		} else if (event.eType == penDownEvent && BwGetTwoFactorEntryMode() == bwTwoFactorEntryNumeric) {
			RectangleType r;
			FrmGetObjectBounds(frm, codeFieldIndex, &r);
			if (event.screenX >= r.topLeft.x && event.screenX < r.topLeft.x + r.extent.x
					&& event.screenY >= r.topLeft.y && event.screenY < r.topLeft.y + r.extent.y) {
				char code[bwKeypadMaxLen + 1];
				Boolean keypadOk = BwTwoFactorKeypadRun(code, sizeof(code));
				if (keypadOk) { SetUnfocusedFieldContent(codeFld, code); FldDrawField(codeFld); }
				MemSet(code, sizeof(code), 0);
				if (keypadOk) { btn = btnTwoFactorOk; done = true; }
				continue;	/* re-tapping the (non-editable-focus) field shouldn't also fall through to FrmHandleEvent */
			}
		}
		FrmHandleEvent(frm, &event);
	}

	if (btn == btnTwoFactorOk) {
		char *text = FldGetTextPtr(codeFld);
		StrNCopy(outCode, text ? text : "", outLen - 1);
		outCode[outLen - 1] = '\0';
	}

	SetFieldContent(codeFld, "");
	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prevFrm) { FrmSetActiveForm(prevFrm); FrmDrawForm(prevFrm); }
	return btn == btnTwoFactorOk;
}

/* Both static, not stack-local - FldSetTextPtr keeps pointing at these for
 * as long as the field is on screen, and gTrustCertText in particular is
 * sized to comfortably fit future growth (subject/issuer CN once
 * NetworkClientSecure exposes them - see PROTOCOL.md's TRUST_RESULT note)
 * without touching the .rcp again. Using FldSetTextPtr (an externally-
 * owned buffer) rather than FrmCopyLabel is what actually fixes the real
 * bug found on-device: FrmCopyLabel writes into the compiled resource's
 * OWN fixed buffer, sized from the .rcp's placeholder text - the real
 * fingerprint string here is 39 characters and the old placeholder was
 * only 24, a real overflow that plausibly corrupted the form (explaining
 * the missing buttons) rather than just looking wrong. */
static char gTrustCertText[256];

/* Shared by BwTrustPromptRun and BwFingerprintViewRun - no forced line
 * breaks (tested both '\n' and chrCarriageReturn embedded in a
 * MULTIPLELINES field's text on real rendering and BOTH showed as a
 * literal missing-glyph box instead of a line break, so this field
 * apparently only wraps by width, no embedded-break support to rely on).
 * Space-separated 2-byte groups let its own natural word-wrap lay this out
 * across as many lines as the field width needs, with the field's own
 * scrollbar handling overflow either way. */
/* SEC001 for main-segment budget, together with UpdateFieldScrollBar and
 * BwFingerprintViewRun below (their only caller in THIS direction) - all
 * three tagged as one self-contained group so calls between them are
 * SEC001->SEC001 same-file (safe). BwTrustPromptRun (main, elsewhere in
 * this file) also calls these same two helpers - main->SEC001 same-file is
 * the other safe direction, so that's fine too. Only ROM traps in either
 * body, no call into any UNTAGGED same-file function - the one truly
 * unsafe direction. Low-frequency (Preferences' "Show" button, or the
 * initial trust prompt - not the per-item sync hot path some other
 * SEC001-related bugs this session came from). */
static void FormatFingerprintText(const UInt8 fingerprint[32], char *out, UInt16 outCap) SEC001;
static void UpdateFieldScrollBar(FieldType *fld, ScrollBarType *sb) SEC001;

static void FormatFingerprintText(const UInt8 fingerprint[32], char *out, UInt16 outCap)
{
	char *p = out;
	UInt8 i;
	(void) outCap; /* 256-byte gTrustCertText is generously sized for this - see its own comment */
	p += StrPrintF(p, "SHA-256 Fingerprint: ");
	/* Hex digits written directly: Palm OS StrPrintF ignores the field
	 * width in "%02X" and printed each byte as four digits ("0001"). */
	for (i = 0; i < 32; i++) {
		UInt8 hi = (UInt8) (fingerprint[i] >> 4), lo = (UInt8) (fingerprint[i] & 0x0F);
		*p++ = (char) (hi < 10 ? '0' + hi : 'A' + hi - 10);
		*p++ = (char) (lo < 10 ? '0' + lo : 'A' + lo - 10);
		if (i != 31 && (i % 2) == 1) *p++ = ' ';
	}
	*p = 0;
}

/* Generic FIELD+SCROLLBAR wiring shared by any full-screen text-display
 * dialog in this app (frmTrust's cert data, frmFingerprint) - standard
 * FldGetScrollValues/SclSetScrollBar pairing (PalmOS's own idiom for this,
 * e.g. Memo Pad), not anything BitWarden-specific. */
static void UpdateFieldScrollBar(FieldType *fld, ScrollBarType *sb)
{
	UInt16 scrollPos, textHeight, fieldHeight;
	Int16 max;

	FldGetScrollValues(fld, &scrollPos, &textHeight, &fieldHeight);
	max = (Int16) ((textHeight > fieldHeight) ? (textHeight - fieldHeight) : 0);
	SclSetScrollBar(sb, (Int16) scrollPos, 0, max, (Int16) (fieldHeight ? fieldHeight : 1));
}

Boolean BwTrustPromptRun(const UInt8 fingerprint[32])
{
	FormPtr frm, prevFrm;
	FieldType *certFld;
	ScrollBarType *sb;
	EventType event;
	Boolean done = false;
	UInt16 btn = 0;

	FormatFingerprintText(fingerprint, gTrustCertText, sizeof(gTrustCertText));

	prevFrm = FrmGetActiveForm();
	frm = FrmInitForm(frmTrust);
	if (!frm) return false;

	FrmSetActiveForm(frm);
	{
		FieldType *qFld = (FieldType *) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, fldTrustQuestion));
		FldSetTextPtr(qFld, (char *) "Trust this server's certificate?");
		/* FldSetTextPtr alone doesn't trigger a word-wrap recalculation -
		 * without this, MULTIPLELINES text just runs off the field's right
		 * edge on one line instead of wrapping (same call already used
		 * correctly for the password mask in RecordForm.c/Dialogs.c's own
		 * ApplyPasswordDisplay/PwRedraw - missed here on the first pass). */
		FldRecalculateField(qFld, false);
	}
	certFld = (FieldType *) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, fldTrustCertData));
	sb = (ScrollBarType *) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, sclTrust));
	FldSetTextPtr(certFld, gTrustCertText);
	FldRecalculateField(certFld, false);

	FrmDrawForm(frm);
	UpdateFieldScrollBar(certFld, sb);

	while (!done) {
		EvtGetEvent(&event, evtWaitForever);
		BwTouchActivity();	/* own separate loop - see BwUnlockPromptRun's own comment on this */
		if (event.eType == appStopEvent) { EvtAddEventToQueue(&event); btn = btnTrustNo; break; }
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, NULL)) continue;

		if (event.eType == ctlSelectEvent) {
			if (event.data.ctlSelect.controlID == btnTrustYes) { btn = btnTrustYes; done = true; }
			else if (event.data.ctlSelect.controlID == btnTrustNo) { btn = btnTrustNo; done = true; }
		} else if (event.eType == sclRepeatEvent) {
			Int16 delta = (Int16) (event.data.sclRepeat.newValue - event.data.sclRepeat.value);
			FldScrollField(certFld, (UInt16) (delta < 0 ? -delta : delta), (delta > 0) ? winDown : winUp);
			UpdateFieldScrollBar(certFld, sb);
		} else if (event.eType == keyDownEvent &&
				   (event.data.keyDown.chr == vchrPageUp || event.data.keyDown.chr == vchrPageDown)) {
			WinDirectionType dir = (event.data.keyDown.chr == vchrPageDown) ? winDown : winUp;
			if (FldScrollable(certFld, dir)) {
				FldScrollField(certFld, FldGetVisibleLines(certFld), dir);
				UpdateFieldScrollBar(certFld, sb);
			}
		}
		FrmHandleEvent(frm, &event);
	}

	MemSet(gTrustCertText, sizeof(gTrustCertText), 0);
	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prevFrm) { FrmSetActiveForm(prevFrm); FrmDrawForm(prevFrm); }
	return btn == btnTrustYes;
}

/* Read-only variant of the same text-display shape, reached from
 * Preferences' "Show" button once a fingerprint is already trusted - just
 * displays it with an OK to dismiss, no Yes/No decision to make. */
/* SEC001 - see FormatFingerprintText's own comment on why this and its two
 * helpers are tagged as one group. Declaration in BitWarden.h tagged to
 * match (required, not optional - see that file's own note on the same
 * mistake found on BwSerialSendFrame). */
void BwFingerprintViewRun(const UInt8 fingerprint[32]) SEC001;

void BwFingerprintViewRun(const UInt8 fingerprint[32])
{
	FormPtr frm, prevFrm;
	FieldType *fld;
	ScrollBarType *sb;
	EventType event;
	Boolean done = false;
	static char certText[256];

	FormatFingerprintText(fingerprint, certText, sizeof(certText));

	prevFrm = FrmGetActiveForm();
	frm = FrmInitForm(frmFingerprint);
	if (!frm) return;

	FrmSetActiveForm(frm);
	fld = (FieldType *) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, fldFingerprintData));
	sb = (ScrollBarType *) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, sclFingerprint));
	FldSetTextPtr(fld, certText);
	FldRecalculateField(fld, false);

	FrmDrawForm(frm);
	UpdateFieldScrollBar(fld, sb);

	while (!done) {
		EvtGetEvent(&event, evtWaitForever);
		BwTouchActivity();	/* own separate loop - see BwUnlockPromptRun's own comment on this */
		if (event.eType == appStopEvent) { EvtAddEventToQueue(&event); break; }
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, NULL)) continue;

		if (event.eType == ctlSelectEvent && event.data.ctlSelect.controlID == btnFingerprintOk) {
			done = true;
		} else if (event.eType == sclRepeatEvent) {
			Int16 delta = (Int16) (event.data.sclRepeat.newValue - event.data.sclRepeat.value);
			FldScrollField(fld, (UInt16) (delta < 0 ? -delta : delta), (delta > 0) ? winDown : winUp);
			UpdateFieldScrollBar(fld, sb);
		} else if (event.eType == keyDownEvent &&
				   (event.data.keyDown.chr == vchrPageUp || event.data.keyDown.chr == vchrPageDown)) {
			WinDirectionType dir = (event.data.keyDown.chr == vchrPageDown) ? winDown : winUp;
			if (FldScrollable(fld, dir)) {
				FldScrollField(fld, FldGetVisibleLines(fld), dir);
				UpdateFieldScrollBar(fld, sb);
			}
		}
		FrmHandleEvent(frm, &event);
	}

	MemSet(certText, sizeof(certText), 0);
	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prevFrm) { FrmSetActiveForm(prevFrm); FrmDrawForm(prevFrm); }
}

/* ------------------------------------------------------------------ *
 *  sync progress - simple centered status form (same "centered modal"
 *  shape as lazyFTP's own transfer-progress form, not bottom-aligned -
 *  progress/busy indicators are the one documented exception to this
 *  suite's bottom-aligned-dialog rule). Status text is the first line;
 *  a filled-rectangle progress bar (same plain hand-drawn shape as
 *  Sec35's own ProgressBar - no GADGET/SLIDER resource needed) appears
 *  underneath once item streaming actually starts (BwProgressBar).
 *
 * NOTE: despite this form itself doing no event pumping between steps,
 * long waits are NOT actually safe from the device's auto-off timer on
 * that basis alone - a single blocking Serial Manager call can run for
 * many seconds with zero events dispatched, which is exactly what caused
 * a real screen-off-mid-sync bug on real hardware. That's fixed at the
 * transport layer instead (Serial.c's ReadExact polls in short slices and
 * calls EvtResetAutoOffTimer between them, same proven pattern as
 * Address35/Sec35's own progress-bar auto-off fixes) - this file doesn't
 * need to know about it, but the OLD version of this comment's "blocking
 * calls are fine, nothing to reset" reasoning was the actual bug, so it's
 * corrected here too rather than left to mislead the next reader. */
static FormPtr gProgressFrm = NULL;
static FormPtr gProgressPrevFrm = NULL;
static Boolean gProgressBarShown = false;
static Boolean gProgressCancelled = false;

/* Y positions moved up 8px from their original 20/38 (real-device feedback:
 * too much dead space below the bar) - form height (BitWardenEn.rcp's own
 * frmSyncProgress) trimmed to match. */
#define bwProgressBarX		6
#define bwProgressBarY		30
#define bwProgressBarW		144
#define bwProgressBarH		10
#define bwProgressCycleItems	20

void BwProgressShow(const char *title)
{
	gProgressPrevFrm = FrmGetActiveForm();
	gProgressFrm = FrmInitForm(frmSyncProgress);
	if (!gProgressFrm) return;
	gProgressBarShown = false;
	gProgressCancelled = false;
	FrmSetActiveForm(gProgressFrm);
	FrmDrawForm(gProgressFrm);
	BwProgressSetStatus(title);
}

/* Real-device request: a long sync (100+s PBKDF2, a stuck login retry, or
 * just a large vault) had no way to back out of short of pulling the
 * battery - a Cancel button on frmSyncProgress, same shape as lazyFTP's own
 * proven ctlXferCancel/LzTPProgressCancelled (BusyDialog.c). Detects a tap
 * against the button's own bounds directly rather than dispatching the
 * penUpEvent through FrmHandleEvent - lazyFTP's own comment on
 * LzTPProgressBar explains why: feeding arbitrary events (including
 * appStopEvent) into a form handler from inside a tight synchronous drain
 * loop like this one crashed on app-close there. Called once per drained
 * penUpEvent from Serial.c's own PollKeepAlive - that already runs roughly
 * once a second during EVERY blocking wait in BwSyncRun (connect, login,
 * each item, a 2FA wait), not just while items are actively streaming, so
 * this one hook point covers the whole sync rather than needing a second,
 * competing event-drain loop anywhere else. No-op if the progress form
 * isn't showing or a cancel was already noted. Tagged SEC001 purely for
 * main-segment budget - both only touch this file's own static state or
 * ROM traps, no same-file call, so this is safe regardless of caller
 * (Serial.c/Sync.c, both cross-file calls into this segment either way).
 *
 * Real-device report (2026-09): a sync occasionally aborted itself with no
 * one having tapped Cancel - WiRSa's own log showed the Palm actively
 * sending BW_BYE mid-item-ack-wait. Root cause: this only ever checked
 * where a penUpEvent LANDED, not where the matching penDownEvent had
 * STARTED - a stray touch anywhere that happened to end inside the Cancel
 * button's small (40x14) rectangle counted as a tap, even a drag/scroll
 * gesture merely passing through on its way elsewhere. Real PalmOS controls
 * don't have this problem (the Control Manager only fires a control's
 * action when BOTH the down and up land on that same control) - bypassing
 * that machinery (see this function's own note on why: dispatching through
 * FrmHandleEvent from this drain loop crashed lazyFTP's own equivalent)
 * lost that safety net, so it's reimplemented here explicitly. */
static Boolean BwProgressPenDownInBounds(Int16 x, Int16 y) SEC001;
void BwProgressNotePenDown(Int16 x, Int16 y) SEC001;
void BwProgressNotePenUp(Int16 x, Int16 y) SEC001;
Boolean BwProgressCancelled(void) SEC001;

static Boolean gProgressPenDownInCancel = false;

static Boolean BwProgressPenDownInBounds(Int16 x, Int16 y)
{
	RectangleType r;
	UInt16 idx;
	if (!gProgressFrm) return false;
	idx = FrmGetObjectIndex(gProgressFrm, btnSyncCancel);
	if (idx == frmInvalidObjectId) return false;
	FrmGetObjectBounds(gProgressFrm, idx, &r);
	return RctPtInRectangle(x, y, &r);
}

void BwProgressNotePenDown(Int16 x, Int16 y)
{
	gProgressPenDownInCancel = (!gProgressCancelled && BwProgressPenDownInBounds(x, y));
}

void BwProgressNotePenUp(Int16 x, Int16 y)
{
	Boolean downWasIn = gProgressPenDownInCancel;
	gProgressPenDownInCancel = false;
	if (!downWasIn || gProgressCancelled) return;
	if (BwProgressPenDownInBounds(x, y)) gProgressCancelled = true;
}

Boolean BwProgressCancelled(void)
{
	return gProgressCancelled;
}

void BwProgressSetStatus(const char *status)
{
	RectangleType r;
	if (!gProgressFrm) return;
	r.topLeft.x = 6; r.topLeft.y = 12;
	r.extent.x = 148; r.extent.y = 16;
	WinEraseRectangle(&r, 0);
	FntSetFont(stdFont);
	WinDrawChars(status, StrLen(status), 8, 14);
}

/* `done` = items received so far. There's no true total to compute a real
 * percentage against - PROTOCOL.md's SYNC_DONE only reports the final
 * count once everything has already streamed. The original design filled
 * toward a 20-item soft cap and then held at full width - real-device
 * feedback: for any sync bigger than ~20 items (this suite's own test
 * vault has 300+) the bar looked finished/stuck for the rest of the sync,
 * even though the status text above it (Sync.c's own "Syncing... N items")
 * kept counting up correctly the whole time. Fixed as a sawtooth instead:
 * fills 0->full every bwProgressCycleItems items, then snaps back to 0 and
 * fills again, repeating for as long as the sync runs - an honest "still
 * working" signal instead of a bar that silently stops meaning anything
 * partway through. Call with done=0 once to reveal the bar frame right as
 * streaming starts. */
/* Real-device report (2026-09): a Fatal Exception happened right as this
 * function was called for the SECOND time ever with real streaming data
 * (the first call, revealing the bar after item #1, apparently succeeded -
 * WiRSa's own log shows item #1's ack completing fine; item #2's frame was
 * sent but the Palm never acked it, consistent with a crash somewhere in
 * this exact window). Reverted the SEC001 tag as the most surgical
 * rollback of the newest untested-with-real-data change, mirroring the
 * same reasoning already applied once this session to PollKeepAlive/
 * ReadExact (Serial.c) - budget recovered from BwProgressHide instead
 * (called once per sync, not on this same per-item hot path). */
void BwProgressBar(UInt16 done)
{
	RectangleType bar, fill;
	Int16 w;
	UInt16 phase;
	if (!gProgressFrm) return;

	if (!gProgressBarShown) {
		bar.topLeft.x = bwProgressBarX; bar.topLeft.y = bwProgressBarY;
		bar.extent.x = bwProgressBarW; bar.extent.y = bwProgressBarH;
		WinDrawRectangleFrame(simpleFrame, &bar);
		gProgressBarShown = true;
	}

	phase = done % bwProgressCycleItems;
	w = (Int16) ((bwProgressBarW - 2) * phase / bwProgressCycleItems);

	/* Erase the whole interior first, then redraw just the new (possibly
	 * much smaller, right after a sawtooth reset) fill width - erasing
	 * unconditionally is what makes the reset-to-0 snap visually correct
	 * instead of leaving the previous, larger fill behind. */
	fill.topLeft.x = bwProgressBarX + 1; fill.topLeft.y = bwProgressBarY + 1;
	fill.extent.x = bwProgressBarW - 2; fill.extent.y = bwProgressBarH - 2;
	WinEraseRectangle(&fill, 0);

	if (w > 0) {
		fill.extent.x = w;
		WinDrawRectangle(&fill, 0);
	}
}

/* SEC001 for main-segment budget, recovered here instead of on
 * BwProgressBar (see that function's own note) - called once per sync
 * (success or failure), not from the tight per-item loop, so far lower
 * exposure if there's any marginal issue with this multi-segment scheme
 * under real-device conditions POSE64 doesn't reproduce. Only touches this
 * file's own static state and ROM traps, no same-file call, only ever
 * called cross-file (Sync.c) - declaration in BitWarden.h tagged to match. */
void BwProgressHide(void) SEC001;

void BwProgressHide(void)
{
	if (!gProgressFrm) return;
	FrmEraseForm(gProgressFrm);
	FrmDeleteForm(gProgressFrm);
	gProgressFrm = NULL;
	gProgressBarShown = false;
	if (gProgressPrevFrm) { FrmSetActiveForm(gProgressPrevFrm); FrmDrawForm(gProgressPrevFrm); }
}

/* ------------------------------------------------------------------ *
 *  Options > Security > 2FA - entry-mode preference + numeric keypad,
 *  same shape as Sec35's own frmPrefs entry-mode picker / frmKeypad
 *  (sec35.c) - see BitWardenRsc.h's own comment on both forms.
 * ------------------------------------------------------------------ */

SEC001 Boolean BwTwoFactorPrefsPromptRun(void)
{
	FormPtr frm, prevFrm;
	ListType *lst;
	ControlType *trigger;
	EventType event;
	Boolean done = false;
	UInt16 btn = 0;
	UInt8 mode = BwGetTwoFactorEntryMode();

	prevFrm = FrmGetActiveForm();
	frm = FrmInitForm(frmTwoFactorPrefs);
	if (!frm) return false;
	FrmSetActiveForm(frm);

	lst = (ListType *) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, lstTwoFactorPrefsType));
	trigger = (ControlType *) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, popTwoFactorPrefsType));
	LstSetSelection(lst, (Int16) mode);
	CtlSetLabel(trigger, LstGetSelectionText(lst, (Int16) mode));

	FrmDrawForm(frm);

	while (!done) {
		EvtGetEvent(&event, evtWaitForever);
		BwTouchActivity();

		if (event.eType == appStopEvent) { EvtAddEventToQueue(&event); btn = btnTwoFactorPrefsCancel; break; }
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, NULL)) continue;

		if (event.eType == ctlSelectEvent) {
			if (event.data.ctlSelect.controlID == btnTwoFactorPrefsOk) { btn = btnTwoFactorPrefsOk; done = true; }
			else if (event.data.ctlSelect.controlID == btnTwoFactorPrefsCancel) { btn = btnTwoFactorPrefsCancel; done = true; }
		}
		FrmHandleEvent(frm, &event);
	}

	if (btn == btnTwoFactorPrefsOk) {
		Int16 sel = LstGetSelection(lst);
		if (sel >= 0) BwSetTwoFactorEntryMode((UInt8) sel);
	}

	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prevFrm) { FrmSetActiveForm(prevFrm); FrmDrawForm(prevFrm); }
	return btn == btnTwoFactorPrefsOk;
}

/* ---- numeric keypad -----------------------------------------------
 * Mirrors Sec35's own frmKeypad (sec35.c: KeypadDrawKey/KeypadHitTest/
 * KeypadInvertKey) but without the phone-letter decorations - not useful
 * for a TOTP/backup code. GADGETs don't fire ctlSelectEvent, so hit
 * detection is fully manual: penDownEvent inverts the tapped key,
 * penUpEvent re-checks the SAME key is still under the pen (a drag-off
 * cancels that tap, matching every other PalmOS button's own behavior)
 * and applies it. */
/* bwKeypadMaxLen lives at the top of this file (BwTwoFactorPromptRun needs it too). */
#define bwKeypadDigitCount	10
static const UInt16 kKeypadDigitIds[bwKeypadDigitCount] = {
	gadTwoFactorKey0, gadTwoFactorKey1, gadTwoFactorKey2, gadTwoFactorKey3, gadTwoFactorKey4,
	gadTwoFactorKey5, gadTwoFactorKey6, gadTwoFactorKey7, gadTwoFactorKey8, gadTwoFactorKey9
};

static void KeypadDrawKey(FormPtr frm, UInt16 gadgetId, const char *label, Boolean inverted) SEC001;

static void KeypadDrawKey(FormPtr frm, UInt16 gadgetId, const char *label, Boolean inverted)
{
	UInt16 idx = FrmGetObjectIndex(frm, gadgetId);
	RectangleType r;
	FontID prevFont;
	Coord w, h, x, y;

	FrmGetObjectBounds(frm, idx, &r);
	WinEraseRectangle(&r, 3);
	WinDrawRectangleFrame(((3 << 8) | 1), &r);	/* 3px rounded corner, 1px line - same as Sec35's own KeypadDrawKey */

	prevFont = FntSetFont(boldFont);
	w = FntCharsWidth((Char *) label, (Int16) StrLen(label));
	h = FntLineHeight();
	x = (Coord) (r.topLeft.x + (r.extent.x - w) / 2);
	y = (Coord) (r.topLeft.y + (r.extent.y - h) / 2);
	if (inverted) {
		RectangleType inv = r;
		inv.topLeft.x += 1; inv.topLeft.y += 1;
		inv.extent.x -= 2; inv.extent.y -= 2;
		WinInvertRectangle(&inv, 2);
	}
	WinDrawChars((Char *) label, (Int16) StrLen(label), x, y);
	FntSetFont(prevFont);
}

/* Returns 0..9 for a digit key, -1 for Del, -2 for no hit. */
static Int16 KeypadHitTest(FormPtr frm, Coord x, Coord y) SEC001;

static Int16 KeypadHitTest(FormPtr frm, Coord x, Coord y)
{
	UInt16 i;
	RectangleType r;
	for (i = 0; i < bwKeypadDigitCount; i++) {
		FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, kKeypadDigitIds[i]), &r);
		if (x >= r.topLeft.x && x < r.topLeft.x + r.extent.x
				&& y >= r.topLeft.y && y < r.topLeft.y + r.extent.y)
			return (Int16) i;
	}
	FrmGetObjectBounds(frm, FrmGetObjectIndex(frm, gadTwoFactorKeyDel), &r);
	if (x >= r.topLeft.x && x < r.topLeft.x + r.extent.x
			&& y >= r.topLeft.y && y < r.topLeft.y + r.extent.y)
		return -1;
	return -2;
}

static void KeypadRedrawActive(FormPtr frm, Int16 key, Boolean inverted) SEC001;

static void KeypadRedrawActive(FormPtr frm, Int16 key, Boolean inverted)
{
	if (key >= 0) {
		char label[2]; label[0] = (char) ('0' + key); label[1] = 0;
		KeypadDrawKey(frm, kKeypadDigitIds[key], label, inverted);
	} else if (key == -1) {
		KeypadDrawKey(frm, gadTwoFactorKeyDel, "Del", inverted);
	}
}

/* Draws the keypad's own live digit-echo directly (not via the FIELD
 * object's usual left-aligned text APIs) so it can be centered - the
 * user's own ask. The fldTwoFactorKeypadDisplay resource object is still
 * used, just for its bounds; SetFieldContent/FldDrawField (left-aligned
 * only) are no longer used here at all. */
static void KeypadDrawEcho(FormPtr frm, const char *text) SEC001;

static void KeypadDrawEcho(FormPtr frm, const char *text)
{
	UInt16 idx = FrmGetObjectIndex(frm, fldTwoFactorKeypadDisplay);
	RectangleType r;
	FontID prevFont;
	Coord w, h, x, y;

	FrmGetObjectBounds(frm, idx, &r);
	WinEraseRectangle(&r, 0);
	prevFont = FntSetFont(boldFont);
	w = FntCharsWidth((Char *) text, (Int16) StrLen(text));
	h = FntLineHeight();
	x = (Coord) (r.topLeft.x + (r.extent.x - w) / 2);
	y = (Coord) (r.topLeft.y + (r.extent.y - h) / 2);
	WinDrawChars((Char *) text, (Int16) StrLen(text), x, y);
	FntSetFont(prevFont);
}

/* The code itself is NOT masked here either - same reasoning as
 * BwTwoFactorPromptRun's own comment (a 2FA code isn't as sensitive as a
 * password). buf/outCode are wiped either way before returning, matching
 * this app's own "don't linger" convention for anything credential-
 * adjacent even when not strictly secret. */
static Boolean BwTwoFactorKeypadRun(char *outCode, UInt16 outLen)
{
	FormPtr frm, prevFrm;
	EventType event;
	char buf[bwKeypadMaxLen + 1];
	UInt16 len = 0;
	Boolean done = false;
	UInt16 btn = 0;
	Int16 activeKey = -2;

	prevFrm = FrmGetActiveForm();
	frm = FrmInitForm(frmTwoFactorKeypad);
	if (!frm) return false;
	FrmSetActiveForm(frm);

	MemSet(buf, sizeof(buf), 0);

	FrmDrawForm(frm);
	KeypadDrawEcho(frm, buf);
	{
		UInt16 i;
		for (i = 0; i < bwKeypadDigitCount; i++) {
			char label[2]; label[0] = (char) ('0' + i); label[1] = 0;
			KeypadDrawKey(frm, kKeypadDigitIds[i], label, false);
		}
		KeypadDrawKey(frm, gadTwoFactorKeyDel, "Del", false);
	}

	while (!done) {
		EvtGetEvent(&event, evtWaitForever);
		BwTouchActivity();

		if (event.eType == appStopEvent) { EvtAddEventToQueue(&event); btn = btnTwoFactorKeypadCancel; break; }
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, NULL)) continue;

		if (event.eType == penDownEvent) {
			Int16 hit = KeypadHitTest(frm, event.screenX, event.screenY);
			if (hit != -2) {
				activeKey = hit;
				KeypadRedrawActive(frm, activeKey, true);
				/* Digit/Del keys are GADGETs, so they never reach the Control
				 * Manager's own automatic tap click - play it by hand, same
				 * fix/placement as Sec35's own keypad (sec35.c). */
				SndPlaySystemSound(sndClick);
			}
		} else if (event.eType == penMoveEvent && activeKey != -2) {
			/* Dragging off the key cancels it, matching a normal button's
			 * own behavior - re-hit-test against the CURRENT pen position. */
			Int16 stillOver = KeypadHitTest(frm, event.screenX, event.screenY);
			if (stillOver != activeKey) { KeypadRedrawActive(frm, activeKey, false); activeKey = -2; }
		} else if (event.eType == penUpEvent) {
			if (activeKey != -2) {
				KeypadRedrawActive(frm, activeKey, false);
				if (activeKey >= 0) {
					if (len < bwKeypadMaxLen) {
						buf[len++] = (char) ('0' + activeKey);
						buf[len] = 0;
						KeypadDrawEcho(frm, buf);
					}
				} else {	/* Del */
					if (len > 0) {
						buf[--len] = 0;
						KeypadDrawEcho(frm, buf);
					}
				}
				activeKey = -2;
			}
		} else if (event.eType == ctlSelectEvent) {
			if (event.data.ctlSelect.controlID == btnTwoFactorKeypadOk) { btn = btnTwoFactorKeypadOk; done = true; }
			else if (event.data.ctlSelect.controlID == btnTwoFactorKeypadCancel) { btn = btnTwoFactorKeypadCancel; done = true; }
		}
		FrmHandleEvent(frm, &event);
	}

	if (btn == btnTwoFactorKeypadOk) {
		StrNCopy(outCode, buf, outLen - 1);
		outCode[outLen - 1] = 0;
	}
	MemSet(buf, sizeof(buf), 0);

	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prevFrm) { FrmSetActiveForm(prevFrm); FrmDrawForm(prevFrm); }
	return btn == btnTwoFactorKeypadOk;
}
