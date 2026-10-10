/*
 * cddv_seg.h - CalDAV Sync multi-segment scaffolding (force-included into
 * every TU). Mirrors lazyFTP's own lz_seg.h exactly - same toolchain, same
 * gotchas, same reasoning; read that file's header comment for the full
 * explanation if any of this is unfamiliar.
 *
 * A function's segment must be known at BOTH its definition and every
 * forward declaration/call site (multiseg-forward-decl-gotcha), so these
 * tags belong on prototypes in headers too, not just definitions.
 *
 * Segment map:
 *   main   : PilotMain, launch dispatch, StartApplication/StopApplication,
 *            EventLoop only - kept deliberately thin, matching lazyFTP's
 *            own convention.
 *   sec001 : app/UI - MainForm/SlotEditForm/PrefsForm/LongTextDialog/
 *            HelpForm/SyncProgress event handlers, dialogs, menu handling.
 *   sec002 : prefs database - CDDVPrefs.c (CalDAVRelayDB, own DB not Saved
 *            Prefs) + CDDVSec35.c (Sec35 password/token encrypt/decrypt
 *            wiring, enccore) - same pairing lazyFTP's sec002 uses.
 *   sec003 : DateBook35DAV.pdb read/write - DAVDb.c (record CRUD, full-
 *            rebuild-then-atomic-rename sync, Recompute Times).
 *   sec004 : communication - ../lib/relayproto.c/relaywire.c/relaysec.c
 *            (shared wire framing + link encryption, RP_SECTION below) +
 *            Serial.c (CDDVSerialPoll) + SyncEngine.c (per-slot sync
 *            orchestration) + OAuthRelay.c (Google sign-in flow) - mirrors
 *            lazyFTP's sec004 FTP-transport convention.
 *
 * CRITICAL reminders (see the family's memories - identical toolchain):
 *  - Build with -palmos4; without it main->secNNN calls become near bsr.w
 *    (DISP16) -> wild jump / Fatal Alert (palmos4-flag-multiseg-farcall).
 *  - libgcc (32/64-bit division) and const-data tables are only reliably
 *    reached from main (multiseg-libgcc-gotcha).
 *  - A function-local `static const` in a non-main-segment function is
 *    accessed A5-relative and breaks once the segment grows
 *    (multiseg-local-static-const) - hoist to file scope instead.
 *  - A SEC00x function calling back into an untagged function in the SAME
 *    .c file crashes/misbehaves - inline it instead of leaving it untagged
 *    (multiseg-sec001-calls-back-into-main-samefile-gotcha).
 *  - Pointing a MenuItemType.itemStr (or similar) at a string literal living
 *    inside a SEC00x function's own code can dangle once that segment isn't
 *    resident - copy into a static data buffer instead
 *    (multiseg-sec001-stringliteral-itemstr-dangling-pointer).
 */
#ifndef CDDV_SEG_H
#define CDDV_SEG_H

#define SEC001 __attribute__ ((section ("sec001")))
#define SEC002 __attribute__ ((section ("sec002")))
#define SEC003 __attribute__ ((section ("sec003")))
#define SEC004 __attribute__ ((section ("sec004")))

/* Shared relay framing (../lib/relayproto.c RpSendFrame + relaywire.c):
 * same sec004 placement as the former Serial.c/CDDVWire.c. */
#define RP_SECTION       SEC004

#define ENCCORE_SECTION  SEC002
#define AES_SECTION      SEC002

#endif
