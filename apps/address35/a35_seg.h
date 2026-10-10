/*
 * a35_seg.h - Address35 multi-segment scaffolding (force-included into every TU).
 *
 * The ROM Address app is ~16K LOC; compiled it far exceeds the 32KB PalmOS code
 * resource limit, so Address35 is split into main + sec00x via multigen. As with
 * the rest of the family, a function's segment must be known at BOTH its
 * definition and every forward declaration / call site, so these tags are applied
 * on the prototypes in the headers (marked "Ad35 seg:").
 *
 * Segment map:
 *   main   : AddressDB35 (record pack/unpack, CRUD, sort, AppInfo/tags) + the
 *            vCard RECEIVE/import path (no-globals sublaunch rule) + PilotMain.
 *   sec001 : foreground-only view/edit UI.
 *   sec002 : vCard SEND/export, SD-card browse, beam-inbox merge, Phone Lookup.
 *   sec003 : bundled crypto (enccore/aes) + record decrypt/seal.
 *
 * CRITICAL reminders (see the family's memories):
 *  - Build with -palmos4 (plain `make`); without it main->secNNN calls in new
 *    object files become near bsr.w (DISP16) -> wild jump / Fatal Alert.
 *  - libgcc (32/64-bit division) and const-data tables are only reliably reached
 *    from main; datelib/edate (DATELIB_SECTION) therefore live in main.
 *  - Any code a no-globals sublaunch reaches (vCard beam receive) MUST be in main.
 */
#ifndef A35_SEG_H
#define A35_SEG_H

#define SEC001 __attribute__ ((section ("sec001")))
#define SEC002 __attribute__ ((section ("sec002")))
#define SEC003 __attribute__ ((section ("sec003")))
#define SEC_RECV	/* vCard RECEIVE path: MUST be in main (no-globals sublaunch) */

/* Bundled crypto (apps/lib/enccore.c + aes.c) -> sec003, to keep it off main's
 * 32KB code resource. Address35 only calls division-free EncGetKey/EncCrypt, so
 * the libgcc-only-in-main rule (multiseg-libgcc memory) isn't tripped. These must
 * be defined before sects.h/enccore.h/aes.h (a35_seg.h is force-included first). */
#define AES_SECTION      SEC003
#define ENCCORE_SECTION  SEC003

/* apps/lib (datelib/edate/datepick/hackcheck/era) tag their functions with
 * DATELIB_SECTION and MUST live in main. */
#define DATELIB_SECTION

#endif /* A35_SEG_H */
