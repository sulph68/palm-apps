#ifndef   __MAIN_H__
#define   __MAIN_H__

#include <PalmOS.h>

#include "MainRsc.h"

#define newMemoEvent		32767
#define memoProcessEvent	32768
#define memoStatusEvent		32769
#define globalPasswordStatusEvent 32770
#define catProcessEvent	32771
#define cardNewFileEvent	32772   /* CryptoNote: "New" from the card editor -> New-file dialog */

/* The category of a record can be changed on
 * the Details form. In that case, we need to
 * notify the Edit form to update its display
 */

#define updateCategory		0x01

/* The Main form may be needed to reload its
 * record list when a record has been removed
 * as a result of a forgotten password.
 */

#define updateTable			0x02

/* These macros break up a Char * to two words.
 * This is needed to be able to pass a ptr around
 * using custom event that only accepts words as
 * their data type.
 */

#define hiBits(ptr)  	((UInt16) ((((Long) ptr) & 0xffff0000) >> 16))
#define lowBits(ptr) 	((UInt16) (((Long) ptr) & 0xffff))
#define hi_low(hi, low)	((Char *) ((((Long) hi) << 16) | ((Long) low)))

#define	USES_ALERT	\
Char * _textP

#define ALERT(format, value)	\
_textP = MemPtrNew(80); \
StrPrintF(_textP, format, value); \
FrmCustomAlert(altDebugAlert, _textP, "", ""); \
MemPtrFree(_textP)

#define MsgBox(textP) \
FrmCustomAlert(altDebugAlert, textP, "", "")

Boolean isEncrypted(UInt16);
FontID SelectFont (FontID);

/* AESCrypt() (the Blowfish() replacement) is declared in AesCrypt.h, which
 * any file calling it should include directly. */
void SetApplicationPrefs(UInt16);
void GetApplicationPrefs(UInt32*);
void SetShowHidden(Boolean show);	/* Prefs > Show Hidden - unsaved, see CryptoDefaults */
Boolean ApplicationHandleEvent(EventPtr);

#endif /* __MAIN_H__ */
