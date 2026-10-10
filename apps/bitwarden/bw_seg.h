/*
 * bw_seg.h - BitWarden multi-segment scaffolding (force-included into every
 * TU via -include, same convention as Address35's own a35_seg.h).
 *
 * BitWarden was single-segment through v1 (see the Makefile's own prior
 * comment) - this app's own main code resource finally hit the 32KB cap
 * after the 2026-08 round (debounced search, toast dialogs, Security > 2FA
 * numeric keypad, RAM-only cached-password rewrite). Rather than cut a
 * feature, the numeric keypad (Dialogs.c) - the single largest addition,
 * and one only ever reached in the foreground via an explicit user
 * action, never from a sublaunch or any startup path - moves to sec001.
 *
 * Sync.c's HandleSyncStatusFrame (BW_SYNC_STATUS handling, added 2026-09)
 * also moved to sec001 the same way - the main segment had zero headroom
 * left by that point, confirmed by a real build failure from adding even
 * this small a function inline.
 *
 * CRITICAL reminders (see the family's own memories on this exact class of
 * bug, hit repeatedly across Address35/lazyFTP):
 *  - Build with -palmos4 (plain `make`); without it, main->sec001 calls in
 *    new object files become near bsr.w (DISP16) -> wild jump / Fatal Alert.
 *  - A function's segment must be known at BOTH its definition and every
 *    forward declaration/prototype that calls it across the main/sec001
 *    boundary - tag both or the call jumps to garbage.
 *  - libgcc (32/64-bit division) is only reliably reachable from main -
 *    nothing sec001-tagged here does division, so this doesn't apply, but
 *    keep it in mind before moving anything else across in the future.
 */
#ifndef BW_SEG_H
#define BW_SEG_H

#define SEC001 __attribute__ ((section ("sec001")))

/* Shared relay framing (../lib/relayproto.c RpSendFrame + relaywire.c):
 * same sec001 placement as the former Serial.c/Wire.c. */
#define RP_SECTION SEC001

/* Bundled crypto (apps/lib/enccore.c + aes.c) stays in main - BitWarden's
 * own EncCrypt/EncCryptFast call sites are all over the main-segment sync/
 * store hot paths, and enccore.h's own sects.h default (empty) already
 * matches that with zero extra plumbing - no override needed here. */

#endif
