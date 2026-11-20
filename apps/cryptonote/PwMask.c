/*
 * PwMask.c - see PwMask.h. Ported from PasswordForm.c's original masking
 * (itself a copy of sec35.c's RedrawMaskedField/PwFieldKeyDown), generalised
 * to any field and with BitWarden's "Show" toggle.
 */
#include "PwMask.h"

#define kPwRevealSecs	1

UInt32         gPwRevealUntilTicks;
static char    gPwReal[kPwMaskMax + 1];
static UInt16  gPwFieldId;
static Boolean gPwShowAll;

static FieldPtr PwField (void) UI2_SECTION;
static FieldPtr PwField (void)
{
	FormPtr	frm = FrmGetActiveForm ();
	UInt16	idx;

	if (! frm || ! gPwFieldId) return NULL;
	idx = FrmGetObjectIndex (frm, gPwFieldId);
	if (idx == frmInvalidObjectId) return NULL;
	return (FieldPtr) FrmGetObjectPtr (frm, idx);
}

/* Redraw the field from gPwReal: '*'s, the last character in clear if
 * revealLast, everything in clear while "Show" is checked. The field's own
 * handle is edited in place (FldDelete + FldInsert), as Sec35 does, to avoid
 * a rare "non-word-aligned handle" crash from handle churn. */
static void PwRedraw (Boolean revealLast) UI2_SECTION;
static void PwRedraw (Boolean revealLast)
{
	FieldPtr	fld = PwField ();
	UInt16		len = (UInt16) StrLen (gPwReal), i, cur;
	Char		disp[kPwMaskMax + 1];

	if (! fld) return;
	for (i = 0; i < len; i++)
		disp[i] = (Char) ((gPwShowAll || (revealLast && i == len - 1)) ? gPwReal[i] : '*');
	disp[len] = 0;
	cur = FldGetTextLength (fld);
	if (cur > 0)
		{
		FldSetInsPtPosition (fld, cur);
		FldDelete (fld, 0, cur);
		}
	FldInsert (fld, disp, len);
	FldSetInsPtPosition (fld, len);
	FldDrawField (fld);
	MemSet (disp, sizeof (disp), 0);
}

void PwMaskBegin (UInt16 fieldId, UInt16 showId)
{
	FormPtr frm = FrmGetActiveForm ();

	MemSet (gPwReal, sizeof (gPwReal), 0);
	gPwFieldId = fieldId;
	gPwShowAll = false;
	gPwRevealUntilTicks = 0;
	PwRedraw (false);					/* empty field (a retry clears it) */
	if (frm && showId)
		{
		UInt16 idx = FrmGetObjectIndex (frm, showId);
		if (idx != frmInvalidObjectId)
			CtlSetValue ((ControlPtr) FrmGetObjectPtr (frm, idx), 0);
		}
}

Boolean PwMaskKeyDown (EventType* e)
{
	FormPtr	frm = FrmGetActiveForm ();
	UInt16	focus, len = (UInt16) StrLen (gPwReal);
	WChar	c = e->data.keyDown.chr;

	if (! frm || ! gPwFieldId) return false;
	focus = FrmGetFocus (frm);
	if (focus == noFocus || FrmGetObjectId (frm, focus) != gPwFieldId) return false;

	if (c == chrBackspace)
		{
		if (len > 0) gPwReal[len - 1] = 0;
		gPwRevealUntilTicks = 0;
		PwRedraw (false);
		return true;
		}
	if (TxtCharIsPrint (c) && len < kPwMaskMax)
		{
		gPwReal[len] = (char) c;
		gPwReal[len + 1] = 0;
		PwRedraw (true);
		gPwRevealUntilTicks = TimGetTicks () + (SysTicksPerSecond () * kPwRevealSecs);
		return true;
		}
	return false;
}

Boolean PwMaskKeyboard (EventType* e)
{
	FormPtr		frm = FrmGetActiveForm ();
	FieldPtr	fld;
	UInt16		focus, cur;
	WChar		c;
	Char*		t;

	if (e->eType != keyDownEvent || ! gPwFieldId || ! frm) return false;
	c = e->data.keyDown.chr;
	if (! (e->data.keyDown.modifiers & commandKeyMask)) return false;
	if (c != vchrKeyboard && c != vchrKeyboardAlpha && c != vchrKeyboardNumeric) return false;
	focus = FrmGetFocus (frm);
	if (focus == noFocus || FrmGetObjectId (frm, focus) != gPwFieldId) return false;
	fld = PwField ();
	if (! fld) return false;

	/* the keyboard works on the field's text: give it the real password
	 * (it shows in clear while the keyboard is up, as with "Show") */
	cur = FldGetTextLength (fld);
	if (cur > 0) FldDelete (fld, 0, cur);
	FldInsert (fld, gPwReal, (UInt16) StrLen (gPwReal));
	SysKeyboardDialog (c == vchrKeyboardNumeric ? kbdNumbersAndPunc :
		(c == vchrKeyboardAlpha ? kbdAlpha : kbdDefault));

	t = FldGetTextPtr (fld);
	StrNCopy (gPwReal, t ? t : "", kPwMaskMax);
	gPwReal[kPwMaskMax] = 0;
	gPwRevealUntilTicks = 0;
	PwRedraw (false);					/* back to '*'s (the field text is replaced) */
	return true;
}

void PwMaskShow (Boolean on)
{
	gPwShowAll = on;
	gPwRevealUntilTicks = 0;
	PwRedraw (false);
}

void PwMaskPendingReveal (void)
{
	if (gPwRevealUntilTicks)
		PwRedraw (false);
	gPwRevealUntilTicks = 0;
}

void PwMaskSet (const char* s)
{
	StrNCopy (gPwReal, s, kPwMaskMax);
	gPwReal[kPwMaskMax] = 0;
	gPwRevealUntilTicks = 0;
	PwRedraw (false);
}

Char* PwMaskText (void)
{
	return gPwReal;
}

void PwMaskEnd (void)
{
	FieldPtr fld = PwField ();

	if (fld)
		{
		MemHandle	h;
		UInt16		cur = FldGetTextLength (fld);
		if (cur > 0) FldDelete (fld, 0, cur);	/* field now empty ... */
		h = FldGetTextHandle (fld);				/* ... then zero its buffer */
		if (h)
			{
			MemSet (MemHandleLock (h), (Int32) MemHandleSize (h), 0);
			MemHandleUnlock (h);
			}
		}
	MemSet (gPwReal, sizeof (gPwReal), 0);
	gPwFieldId = 0;
	gPwShowAll = false;
	gPwRevealUntilTicks = 0;
}

void PwFreeWiped (Char** pp)
{
	if (*pp)
		{
		MemSet (*pp, (Int32) MemPtrSize (*pp), 0);
		MemPtrFree (*pp);
		}
	*pp = NULL;
}
