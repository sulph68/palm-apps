#include "Main.h"
#include "CardIO.h"      /* cardMaxPath / cardMaxName for the card-edit bridge */

Boolean EditFormHandleEvent(EventPtr eventP);

void EncryptRecord(UInt16, Char *);

#define cardMaxPassword 64

/* Card-file edit bridge. MainForm sets these before FrmGotoForm(frmEditForm) to
 * edit a text file on the SD card instead of a DB record. While gCardFileOpen is
 * true the EditForm loads/saves the file via CardIO/CardCrypt and hides the
 * DB-only controls (category popup, Details). Cleared when the file closes.
 *
 * gCardEditPath is always the PLAINTEXT (base) path (never ends in ".enc"); the
 * encrypted sibling is gCardEditPath + ".enc". gCardEncrypted selects which one
 * is written on save (the other is deleted). gCardEditName is the display title.
 * gCardPreloadH (if set) is the plaintext the editor should load - MainForm
 * fills it (reading, and decrypting when needed) so the editor never has to. */
extern Boolean   gCardFileOpen;
extern UInt16    gCardEditVol;
extern char      gCardEditPath[cardMaxPath];      /* plaintext/base path            */
extern char      gCardEditName[cardMaxName];      /* display leaf (with .enc if enc)*/
extern Boolean   gCardEncrypted;                  /* save as encrypted container    */
extern Boolean   gCardWasEncrypted;               /* file was encrypted when opened */
extern Boolean   gCardDeletePlain;                /* Encrypt File: delete plaintext */
extern char      gCardPassword[cardMaxPassword];  /* per-file password ("" = none)  */
extern MemHandle gCardPreloadH;                   /* plaintext to load (NULL = disk)*/
extern UInt32    gCardPreloadLen;
