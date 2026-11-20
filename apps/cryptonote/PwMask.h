/*
 * PwMask.h - masked password entry shared by every CryptoNote password field
 * (note password, the global-password dialogs, the SD-card file password).
 *
 * Same behaviour as Sec35 and BitWarden: the field only ever shows '*', except
 * the character just typed, which stays in clear for a second (so the user
 * can see what was entered); a "Show" checkbox reveals the whole password.
 * The real text lives only in this module's buffer, which is wiped when the
 * entry ends. One password field is active at a time.
 */
#ifndef PWMASK_H
#define PWMASK_H

#include <PalmOS.h>
#include "sects.h"

#define kPwMaskMax	56			/* longest password (the fields' MAXCHARS) */

/* 0 = no reveal pending; else the tick at which the just-typed character
 * masks. An event loop sizes its EvtGetEvent timeout from it and calls
 * PwMaskPendingReveal on nilEvent once it has passed. */
extern UInt32 gPwRevealUntilTicks;

/* Start a fresh entry on fieldId (active form); showId = its "Show" checkbox
 * (0 if none), which is unchecked. */
void    PwMaskBegin (UInt16 fieldId, UInt16 showId) UI2_SECTION;
/* A keyDownEvent: true if consumed (the field had focus). */
Boolean PwMaskKeyDown (EventType* e) UI2_SECTION;
/* The on-screen keyboard ("abc"/"123" in the Graffiti area) edits a field's
 * text directly, without key events, so it bypassed the masking. Called from
 * the event loop before SysHandleEvent: if a masked field has the focus, it
 * opens the keyboard on the real text and takes the result back. */
Boolean PwMaskKeyboard (EventType* e) UI2_SECTION;
/* The "Show" checkbox changed. */
void    PwMaskShow (Boolean on) UI2_SECTION;
/* Mask the briefly revealed character once its time is up. */
void    PwMaskPendingReveal (void) UI2_SECTION;
/* Replace the entry (the numeric keypad's result) and redraw. */
void    PwMaskSet (const char* s) UI2_SECTION;
/* The typed password (empty string if none). */
Char*   PwMaskText (void) UI2_SECTION;
/* End the entry: wipe the buffer and the field's own text, stop the timer.
 * Call on every exit path (OK, Cancel, form close). */
void    PwMaskEnd (void) UI2_SECTION;

/* Zero and free a MemPtrNew'd string, and NULL the pointer. */
void    PwFreeWiped (Char** pp) UI2_SECTION;

#endif
