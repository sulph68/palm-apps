/*
 * a35lookup.c - see a35lookup.h. Caller side of Phone Lookup.
 *
 * Every function here is in A35LK_SECTION (main unless the app overrides it),
 * and only calls OS traps - no same-file calls into other segments.
 */
#include <PalmOS.h>
#include "a35lookup.h"
#include "enccore.h"	/* ENC_CREATOR, kSecReturnFtr, kSecLaunchUnlock (constants only) */

static Boolean A35IsWordChar(Char c) A35LK_SECTION;
static void A35SwitchToSec35(void) A35LK_SECTION;

/* Characters that end a lookup word, as the user would see it. */
A35LK_SECTION static Boolean A35IsWordChar(Char c)
{
	return (Boolean) (c != ' ' && c != '\t' && c != '\n' && c != ',' &&
		c != ';' && c != ':' && c != '(' && c != ')');
}

/* Hand off to Sec35's unlock screen; Sec35 switches back to this app once
 * unlocked (the same hand-off every suite app's lock gate uses). */
A35LK_SECTION static void A35SwitchToSec35(void)
{
	DmSearchStateType st;
	UInt16 card, myCard;
	LocalID dbID, myID;
	UInt32 myCreator = 0;

	if (DmGetNextDatabaseByTypeCreator(true, &st, sysFileTApplication, ENC_CREATOR,
			true, &card, &dbID) != errNone)
		return;
	if (SysCurAppDatabase(&myCard, &myID) != errNone ||
		DmDatabaseInfo(myCard, myID, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
			NULL, NULL, NULL, &myCreator) != errNone)
		return;
	FtrSet(ENC_CREATOR, kSecReturnFtr, myCreator);
	SysUIAppSwitch(card, dbID, kSecLaunchUnlock, NULL);
}

A35LK_SECTION void A35LookupPhone(FieldType *fld)
{
	DmSearchStateType st;
	UInt16 card, start, end, i, n;
	LocalID dbID;
	AddrLookupParamsType params;
	UInt32 result = 0;
	Char *text;
	Err err;
	FieldAttrType attr;
	FormPtr frm;

	if (DmGetNextDatabaseByTypeCreator(true, &st, sysFileTApplication, a35LookupCreator,
			true, &card, &dbID) != errNone) {
		PhoneNumberLookup(fld);		/* no Address35: the ROM Address Book lookup */
		return;
	}

	/* What to look up: the selection, else the word just before the cursor. */
	FldGetSelection(fld, &start, &end);
	text = FldGetTextPtr(fld);
	if (start == end) {
		end = FldGetInsPtPosition(fld);
		start = end;
		while (text && start > 0 && A35IsWordChar(text[start - 1]))
			start--;
	}

	MemSet(&params, sizeof(params), 0);
	params.title = "Phone Number Lookup";
	params.pasteButtonText = "Paste";
	n = (UInt16) (end - start);
	if (n > addrLookupStringLength - 1)
		n = addrLookupStringLength - 1;
	for (i = 0; i < n && text; i++)
		params.lookupString[i] = text[start + i];
	params.field1 = addrLookupSortField;
	params.field2 = addrLookupListPhone;
	params.field2Optional = false;
	params.userShouldInteract = true;
	params.formatStringP = "^listname - ^listphone";

	/* Cursor off for the duration: a blink during Address35's sublaunch would
	 * save the pixels under it in a window owned by that sublaunch, which the
	 * OS frees on return - the next blink here would then free it again
	 * ("MemoryMgr.c, Invalid chunk ptr"). */
	InsPtEnable(false);
	frm = FrmGetActiveForm();
	err = SysAppLaunch(card, dbID, sysAppLaunchFlagNewGlobals, sysAppLaunchCmdLookup,
			&params, &result);
	/* Address35 leaves re-activating our form to us, so that whatever the Form
	 * Manager allocates for it (the field's insertion point) is ours. */
	if (frm) FrmSetActiveForm(frm);
	FldGetAttributes(fld, &attr);
	if (err != errNone) {
		if (attr.hasFocus) InsPtEnable(true);
		return;
	}

	if (params.uniqueID == a35LookupLockedID && params.resultStringH == NULL) {
		A35SwitchToSec35();
		return;
	}
	if (params.resultStringH) {
		Char *s = (Char *) MemHandleLock(params.resultStringH);
		FldSetSelection(fld, start, end);	/* replace the looked-up word (or insert at the cursor) */
		FldInsert(fld, s, StrLen(s));
		MemHandleUnlock(params.resultStringH);
		MemHandleFree(params.resultStringH);
	}
	if (attr.hasFocus)
		InsPtEnable(true);
}
