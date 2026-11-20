#include "Main.h"

/* AES_SECTION (sec001) -- main is at its 32KB cap and this file grew past
 * fitting there once masking + the numeric keypad were added. Same pattern
 * as CryptoSec35.h's CryptoLockedBounce: a cross-file entry point tagged
 * into a secondary segment, which works fine for both the ordinary calls
 * (Main.c, MainForm.c) and the FrmSetEventHandler function-pointer registration
 * as long as the tag is on this forward declaration too. */
Boolean PasswordFormHandleEvent(EventPtr eventP) AES_SECTION;

Boolean ValidatePassword(Char *) AES_SECTION;

/* Password masking + its reveal timer: PwMask.h. */
#include "PwMask.h"

/* Numeric keypad popup (frmKeypadForm) -- see PasswordForm.c. */
Boolean KeypadFormHandleEvent(EventPtr eventP) AES_SECTION;