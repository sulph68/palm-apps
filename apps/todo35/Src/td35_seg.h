/* td35_seg.h - ToDo35 segment scaffolding.
 *
 * The core app + era lib fit the MAIN code segment, but adding the vCal beaming
 * (ToDoTransfer.c, ~5KB) pushes past the 32KB code-resource limit. So the whole
 * of ToDoTransfer.c is placed in a SECOND segment (sec002) via TD35_XFER, and
 * multigen builds the cross-segment glue. ToDoTransfer.c has NO libgcc/const-data
 * calls (verified), so it is safe outside main -- unlike datelib/edate, which
 * MUST stay in main (DATELIB_SECTION empty). See multiseg-libgcc-gotcha and
 * multiseg-forward-decl-gotcha: TD35_XFER is applied to every ToDoTransfer.c
 * function AND to the three exported prototypes in ToDo.h.
 */
#ifndef TD35_SEG_H
#define TD35_SEG_H
#define DATELIB_SECTION

/* sec002 = the second code segment. Secondary segments are ONLY loaded when the
 * app is launched WITH a globals world (crt0 calls _GccRelocateData only if
 * sysAppLaunchFlagNewGlobals is set). A beam arriving while the app is not running
 * is a no-globals sublaunch (sysAppLaunchCmdExgReceiveData) -- so any code it needs
 * MUST live in the MAIN segment, or the call jumps into an unloaded segment and
 * faults. Therefore:
 *   TD35_XFER  -> sec002  : foreground-only code (beam SEND, and UI moved here to
 *                            make room in main). Never reached from a no-globals
 *                            sublaunch (Find/ExgReceiveData/ExgAskUser).
 *   TD35_RECV  -> main    : the beam RECEIVE/import path (reached during the
 *                            no-globals ExgReceiveData sublaunch). Empty = main.
 * See note-overlock-column-hack / multiseg-libgcc-gotcha memories. */
#define TD35_XFER __attribute__ ((section ("sec002")))
#define TD35_RECV

/* Phone Lookup caller helper (lib/a35lookup.c): foreground UI only, with the
 * note view that calls it - sec002, main has ~1 KB left. */
#define A35LK_SECTION TD35_XFER

/* sec003 = bundled Sec35 crypto (lib/enccore.c + aes.c) + the decrypt/seal record
 * helpers. Kept off main's 32KB cap. Only reached from FULL (globals) launches: the
 * no-globals beam-receive sublaunch stores raw bytes and never touches crypto (see
 * the Address35DBPlain / ToDoDBPlain inbox pattern). ToDo35 calls only division-free
 * EncGetKey/EncCrypt, so the libgcc-only-in-main rule isn't tripped. These must be
 * defined before sects.h/enccore.h/aes.h (td35_seg.h is force-included first). */
#define SEC003          __attribute__ ((section ("sec003")))
#define AES_SECTION     SEC003
#define ENCCORE_SECTION SEC003
#endif
