/*
 * lz_seg.h - lazyFTP multi-segment scaffolding (force-included into every TU).
 *
 * A function's segment must be known at BOTH its definition and every forward
 * declaration/call site (multiseg-forward-decl-gotcha), so these tags belong
 * on prototypes in headers too, not just definitions.
 *
 * Segment map:
 *   main   : PilotMain, launch dispatch, StartApplication/StopApplication,
 *            EventLoop only - kept deliberately thin. Reserved for a future
 *            cross-app custom launch code (see project memory
 *            lazyftp-project.md) even though nothing calls into it yet.
 *   sec001 : app/UI - form event handlers, dialogs, menu handling.
 *   sec002 : database - VFS-free ExgDBRead/ExgDBWrite, Sec35 password
 *            encrypt/decrypt wiring (enccore).
 *   sec003 : graphics - hand-drawn directory listing rows, scrollbar sync.
 *   sec004 : communication - FTP transport + control/PASV/LIST protocol.
 *   sec005 : reserved, unused - future BearSSL/FTPS integration would live
 *            here, kept separate from sec004 given its likely size.
 *
 * CRITICAL reminders (see the family's memories):
 *  - Build with -palmos4; without it main->secNNN calls become near bsr.w
 *    (DISP16) -> wild jump / Fatal Alert (palmos4-flag-multiseg-farcall).
 *  - libgcc (32/64-bit division) and const-data tables are only reliably
 *    reached from main (multiseg-libgcc-gotcha).
 *  - A function-local `static const` in a non-main-segment function is
 *    accessed A5-relative and breaks once the segment grows
 *    (multiseg-local-static-const) - hoist to file scope instead.
 */
#ifndef LZ_SEG_H
#define LZ_SEG_H

#define SEC001 __attribute__ ((section ("sec001")))
#define SEC002 __attribute__ ((section ("sec002")))
#define SEC003 __attribute__ ((section ("sec003")))
#define SEC004 __attribute__ ((section ("sec004")))

#define ENCCORE_SECTION  SEC002
#define AES_SECTION      SEC002

#endif
