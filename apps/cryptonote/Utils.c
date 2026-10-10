#include "Utils.h"

MemPtr GetActiveFormObjectPtr(UInt16 objectID)
{
	FormPtr formP;

	formP = FrmGetActiveForm();
	return FrmGetObjectPtr(formP, FrmGetObjectIndex(formP, objectID));
}

MemPtr GetFocusObjectPtr(void)
{
	FormPtr formP;
	UInt16 objectIndex;

	formP = FrmGetActiveForm();
	objectIndex = FrmGetFocus(formP);

	if (objectIndex == noFocus) return NULL;
	else return FrmGetObjectPtr(formP, objectIndex);

}

/* ROM StrLen returns Int16 (SYS_TRAP sysTrapStrLen) -- signed, max 32767.
 * Any string past that (a card file or a multi-part-import-sized memo body,
 * both routinely up to ~60000 bytes in this app) makes it wrap negative,
 * then sign-extend to a huge garbage value wherever the result gets
 * widened to UInt32 -- the actual cause of "Import Failed" on a 33KB file
 * (and the same latent bug on save/reopen of any record that large). Plain
 * byte-scanning loop, no 16-bit ceiling. */
UInt32 SafeStrLen(const Char *s)
{
	UInt32 n = 0;
	if (!s) return 0;
	while (s[n]) n++;
	return n;
}
