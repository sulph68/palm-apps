/*
 * db35_seg.h - Db35 multi-segment scaffolding (NOT in the original Datebook).
 *
 * The original Datebook is ~88KB of code, over the 32KB PalmOS code-resource
 * limit, so it must be split into several 'code' segments (as the original
 * multi-segment app was). prc-tools implements this by having the compiler
 * emit cross-segment calls indirectly through code-pointers kept in globals;
 * for that the compiler must KNOW each function's segment, via a section
 * attribute visible both at the definition and at every call site.
 *
 * SEC001..SEC003 tag the functions that live outside the main code segment.
 * This header is force-included (gcc -include) into every translation unit so
 * the macros are always defined. The per-function tags are applied in the
 * segment source files and their headers, each marked with a "Db35 seg:" note.
 *
 * Segment map:
 *   main   : Datebook, DateGlobals, DateDB, + the beam RECEIVE path (SEC_RECV)
 *   sec001 : DateDay
 *   sec002 : DateWeek, ToDoDB, DatePref, DateDisplay (foreground-only dialogs)
 *   sec003 : DateAgenda, DateMonth, DateTransfer (beam SEND/export path)
 *
 * CRITICAL (beam receive): a beam arriving while Datebook is not the running app
 * is a no-globals sublaunch (sysAppLaunchCmdExgReceiveData). Secondary segments
 * are ONLY loaded when the app is launched WITH a globals world (crt0 runs the
 * segment relocation only if sysAppLaunchFlagNewGlobals is set -- see the crt0
 * disassembly), so any code the receive path touches MUST be in the MAIN segment
 * or the call jumps into an unloaded segment (Fatal Exception on the receiver).
 * The receive/import half of DateTransfer.c is therefore tagged SEC_RECV (main);
 * only the foreground SEND/export half stays in sec003. To make room in main,
 * the Preferences and Display-Options dialogs moved to sec002.
 */
#ifndef DB35_SEG_H
#define DB35_SEG_H

#define SEC001 __attribute__ ((section ("sec001")))
#define SEC002 __attribute__ ((section ("sec002")))
#define SEC003 __attribute__ ((section ("sec003")))
#define SEC_RECV	/* beam RECEIVE path: MUST be in main (no-globals sublaunch) */

/* Db35: at-rest encryption (Sec35 shared key). The crypto core (lib/enccore.c + aes.c)
 * and the Datebook encrypt/decrypt/mask helpers live in sec003 (FOREGROUND only -- never
 * reached from a no-globals sublaunch: Find skips encrypted records, beam-receive stores
 * raw). Main is already at the 32KB cap, so nothing crypto goes in main. Division-free:
 * Datebook calls only EncGetKey/EncCrypt (AES has no libgcc division). */
#define AES_SECTION		SEC003
#define ENCCORE_SECTION	SEC003

/* Shared apps/lib files (datelib/datepick/hackcheck/era/edate) tag their
 * functions with DATELIB_SECTION. They MUST live in the MAIN segment: datelib's
 * 64-bit math and edate's constant divisions pull in libgcc helpers (__divsi3
 * etc.) and const-data tables that are only reliably reachable from main (a
 * non-main segment mis-resolves them -> garbage/wild jump). See the
 * multiseg-libgcc-gotcha memory. */
#define DATELIB_SECTION

#endif
