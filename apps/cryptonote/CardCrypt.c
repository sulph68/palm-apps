/*
 * CardCrypt.c - see CardCrypt.h. Serialises CryptoPad's AES model to a file.
 *
 * These functions call AESCryptEx (which lives in a secondary code segment) and
 * so must only ever run from the foreground app (globals loaded). Card
 * encryption is a foreground-only, user-driven action, so that always holds.
 */
#include "CardCrypt.h"
#include "AesCrypt.h"
#include "Database.h"     /* globalPasswordP */
#include "MainRsc.h"      /* frmCardPassword, fldCardPassword, btn... */

Boolean CardCryptIsEncrypted(const UInt8 *data, UInt32 size) AES_SECTION;
Boolean CardCryptIsEncrypted(const UInt8 *data, UInt32 size)
{
	return (size >= cardCryptHdrLen &&
	        MemCmp((void *)data, cardCryptMagic, cardCryptMagicLen) == 0);
}

Err CardCryptEncrypt(const char *plaintext, UInt32 len, const char *password,
                     MemHandle *outH, UInt32 *outSize) AES_SECTION;
Err CardCryptEncrypt(const char *plaintext, UInt32 len, const char *password,
                     MemHandle *outH, UInt32 *outSize)
{
	UInt16   pwLen  = StrLen(password);
	UInt16   rounds = CalibrateKdfRounds();
	UInt8    pwIV[AES_IV_SIZE], bodyIV[AES_IV_SIZE], salt[AES_SALT_SIZE];
	UInt32   total  = (UInt32) cardCryptHdrLen + pwLen + len;
	MemHandle h;
	UInt8   *p;
	Char    *tmp;

	*outH = NULL; *outSize = 0;

	GenerateRandomIV(pwIV);
	GenerateRandomIV(bodyIV);
	GenerateRandomIV(salt);

	h = MemHandleNew(total ? total : 1);
	if (!h) return memErrNotEnoughSpace;
	p = MemHandleLock(h);

	MemMove(p, cardCryptMagic, cardCryptMagicLen);
	p[6] = 1;
	MemMove(p + 7,  pwIV,   AES_IV_SIZE);
	MemMove(p + 23, bodyIV, AES_IV_SIZE);
	MemMove(p + 39, salt,   AES_SALT_SIZE);
	MemMove(p + 55, &rounds, sizeof(rounds));
	MemMove(p + 57, &pwLen,  sizeof(pwLen));

	/* password verifier: encrypt the password under its own derived key */
	tmp = MemPtrNew(pwLen + 1);
	AESCryptEx((Char *) password, tmp, (Char *) password, globalPasswordP,
	           pwIV, salt, rounds, pwLen, AES_ENCRYPT);
	MemMove(p + cardCryptHdrLen, tmp, pwLen);
	MemPtrFree(tmp);

	/* body */
	if (len) {
		tmp = MemPtrNew(len + 1);
		AESCryptEx((Char *) plaintext, tmp, (Char *) password, globalPasswordP,
		           bodyIV, salt, rounds, (UInt16) len, AES_ENCRYPT);
		MemMove(p + cardCryptHdrLen + pwLen, tmp, len);
		MemPtrFree(tmp);
	}

	MemHandleUnlock(h);
	*outH = h;
	*outSize = total;
	return errNone;
}

Int16 CardCryptDecrypt(const UInt8 *data, UInt32 size, const char *password,
                       MemHandle *outPlainH, UInt32 *outLen) AES_SECTION;
Int16 CardCryptDecrypt(const UInt8 *data, UInt32 size, const char *password,
                       MemHandle *outPlainH, UInt32 *outLen)
{
	UInt16   pwLen, rounds;
	UInt8    pwIV[AES_IV_SIZE], bodyIV[AES_IV_SIZE], salt[AES_SALT_SIZE];
	UInt32   bodyLen;
	Char    *verifier;
	MemHandle h;
	Char    *pt;
	Boolean  ok;

	*outPlainH = NULL; *outLen = 0;

	if (!CardCryptIsEncrypted(data, size)) return cardCryptNotEncrypted;

	MemMove(pwIV,   (void *)(data + 7),  AES_IV_SIZE);
	MemMove(bodyIV, (void *)(data + 23), AES_IV_SIZE);
	MemMove(salt,   (void *)(data + 39), AES_SALT_SIZE);
	MemMove(&rounds, (void *)(data + 55), sizeof(rounds));
	MemMove(&pwLen,  (void *)(data + 57), sizeof(pwLen));

	if ((UInt32) cardCryptHdrLen + pwLen > size) return cardCryptCorrupt;

	/* verify the password: decrypt the verifier and compare to the password */
	verifier = MemPtrNew(pwLen + 1);
	AESCryptEx((Char *)(data + cardCryptHdrLen), verifier, (Char *) password,
	           globalPasswordP, pwIV, salt, rounds, pwLen, AES_DECRYPT);
	ok = (StrLen(password) == pwLen && StrNCompare(verifier, password, pwLen) == 0);
	MemPtrFree(verifier);
	if (!ok) return cardCryptBadPassword;

	bodyLen = size - cardCryptHdrLen - pwLen;
	h = MemHandleNew(bodyLen + 1);
	if (!h) return cardCryptCorrupt;
	pt = MemHandleLock(h);
	if (bodyLen)
		AESCryptEx((Char *)(data + cardCryptHdrLen + pwLen), pt, (Char *) password,
		           globalPasswordP, bodyIV, salt, rounds, (UInt16) bodyLen, AES_DECRYPT);
	pt[bodyLen] = 0;
	MemHandleUnlock(h);

	*outPlainH = h;
	*outLen = bodyLen;
	return cardCryptOK;
}

/* The dialog frame (width 2) is drawn a few px OUTSIDE the content rect, so the
 * saved/restored area must be bigger than the frame or a border residue is left
 * behind (e.g. after a wrong-password decrypt). Save a margin around the frame. */
#define cardBusyX     25       /* content rect */
#define cardBusyY     70
#define cardBusyMarg  4        /* extra saved margin for the frame               */
#define cardBusySaveX (cardBusyX - cardBusyMarg)
#define cardBusySaveY (cardBusyY - cardBusyMarg)

WinHandle CardBusyShow(const char *msg) AES_SECTION;
WinHandle CardBusyShow(const char *msg)
{
	RectangleType r, sr;
	WinHandle saved, prevDraw;
	Err err;
	Int16 w, len = StrLen(msg);

	/* Coordinates here are screen-absolute by design (centered on a real
	 * 160x160 screen) -- but WinDrawXxx/WinSaveBits map them through the
	 * CURRENT draw window, not the physical screen. If the caller runs
	 * right after a modal FrmDoDialog (e.g. CardPromptPassword) closes,
	 * the draw window can still be that now-deleted dialog's window
	 * instead of the display window, so these "centered" coordinates end
	 * up offset by wherever that dialog happened to be -- e.g. the
	 * password dialog sits low on screen, so a naive draw lands mostly
	 * below the visible area. Force the real display window for the
	 * duration so this always centers on the actual screen. */
	prevDraw = WinGetDrawWindow();
	WinSetDrawWindow(WinGetDisplayWindow());

	r.topLeft.x = cardBusyX; r.topLeft.y = cardBusyY;
	r.extent.x = 110;        r.extent.y = 24;

	sr.topLeft.x = cardBusySaveX; sr.topLeft.y = cardBusySaveY;
	sr.extent.x = r.extent.x + 2 * cardBusyMarg;
	sr.extent.y = r.extent.y + 2 * cardBusyMarg;

	saved = WinSaveBits(&sr, &err);
	WinEraseRectangle(&r, 3);                /* rounded-corner erase to match frame */
	WinDrawRectangleFrame(dialogFrame, &r);
	FntSetFont(boldFont);
	w = FntCharsWidth((Char *) msg, len);
	WinDrawChars((Char *) msg, len, r.topLeft.x + (r.extent.x - w) / 2,
	             r.topLeft.y + 7);

	WinSetDrawWindow(prevDraw);
	return saved;
}

void CardBusyHide(WinHandle saved) AES_SECTION;
void CardBusyHide(WinHandle saved)
{
	if (saved) {
		WinHandle prevDraw = WinGetDrawWindow();
		WinSetDrawWindow(WinGetDisplayWindow());
		WinRestoreBits(saved, cardBusySaveX, cardBusySaveY);
		WinSetDrawWindow(prevDraw);
	}
}

/* Two prior attempts at auto-focus here both failed: (1) setting focus
 * before FrmDoDialog -- clobbered by FrmDoDialog's own internal
 * FrmSetActiveForm/FrmDrawForm; (2) attaching a custom event handler via
 * FrmSetEventHandler that grabs focus on frmOpenEvent, mirroring the
 * (working) DB-record password dialogs -- still didn't focus, meaning
 * FrmDoDialog does not reliably dispatch frmOpenEvent through a custom
 * handler the way FrmPopupForm-based forms do (undocumented/version-
 * specific behavior, not worth chasing further). Dropping FrmDoDialog
 * entirely and driving our own minimal modal event loop instead removes
 * the dependency on any of that: we control exactly when the form is
 * drawn and focus is grabbed, with no assumptions about what FrmDoDialog
 * does internally. */
Boolean CardPromptPassword(char *out, UInt16 outSize) AES_SECTION;
Boolean CardPromptPassword(char *out, UInt16 outSize)
{
	FormPtr   frmP;
	FormPtr   prevFrmP;
	FieldPtr  fldP;
	UInt16    fieldIndex;
	UInt16    btn = 0;
	EventType event;
	Boolean   done = false;

	out[0] = 0;
	prevFrmP = FrmGetActiveForm();
	frmP = FrmInitForm(frmCardPassword);
	fieldIndex = FrmGetObjectIndex(frmP, fldCardPassword);
	fldP = FrmGetObjectPtr(frmP, fieldIndex);

	FrmSetActiveForm(frmP);
	FrmDrawForm(frmP);
	FrmSetFocus(frmP, fieldIndex);
	FldGrabFocus(fldP);

	while (!done) {
		EvtGetEvent(&event, evtWaitForever);
		if (event.eType == appStopEvent) {
			EvtAddEventToQueue(&event);	/* let the app-level loop quit too */
			btn = btnCardPasswordCancel;
			break;
		}
		if (SysHandleEvent(&event)) continue;
		if (MenuHandleEvent(0, &event, NULL)) continue;

		if (event.eType == ctlSelectEvent) {
			if (event.data.ctlSelect.controlID == btnCardPasswordOK) {
				btn = btnCardPasswordOK;
				done = true;
			} else if (event.data.ctlSelect.controlID == btnCardPasswordCancel) {
				btn = btnCardPasswordCancel;
				done = true;
			}
		}

		FrmHandleEvent(frmP, &event);
	}

	if (btn == btnCardPasswordOK) {
		Char *txt = FldGetTextPtr(fldP);
		if (txt) { StrNCopy(out, txt, outSize - 1); out[outSize - 1] = 0; }
	}

	/* frmCardPassword is FRAME MODAL and doesn't cover the whole screen, so
	 * FrmDrawForm(frmP) above auto-saved the bits behind it (bitsBehindForm).
	 * FrmDoDialog normally consumes that save via FrmEraseForm before
	 * FrmDeleteForm; our hand-rolled loop must call it explicitly too, or
	 * the saved handle just leaks and the pixels under the dialog are never
	 * restored -- left as visible corruption in whatever the caller draws
	 * next, confined to the dialog's rect (y 84-158, the screen's lower
	 * half). Confirmed: only noticeable on a LARGE memo, because a large
	 * memo's text actually fills that region; a small memo's text doesn't
	 * reach down there so the same leftover pixels go unseen. */
	FrmEraseForm(frmP);
	FrmDeleteForm(frmP);

	/* FrmDoDialog also restores the caller's underlying form as active
	 * internally -- our own hand-rolled loop must do this explicitly, or
	 * the underlying form (e.g. the card editor) is left unresponsive to
	 * any further taps once this dialog is gone. Confirmed: "Encrypt
	 * Memo" on a card file -- which, unlike the open/decrypt call site,
	 * does NOT navigate to a different form right after this returns --
	 * appeared to completely hang (no tap responded, "Encrypting..."
	 * never showed) until the OS's own app-switch forced a redraw on
	 * exit. The open/decrypt call site never showed this symptom only
	 * because its own subsequent FrmGotoForm happened to paper over it. */
	if (prevFrmP) {
		FrmSetActiveForm(prevFrmP);
		FrmDrawForm(prevFrmP);
	}

	return (btn == btnCardPasswordOK && out[0] != 0);
}
