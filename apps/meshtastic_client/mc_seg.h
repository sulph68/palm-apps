/*
 * mc_seg.h - the Meshtastic client's second code segment (force-included into every file, as
 * CalDAV Sync's cddv_seg.h is; read that file for the gotchas). The main segment is full at 32 KB, so
 * the WiRSa link code goes to sec001: Link.c, the shared relay framing helpers and the crypto.
 *
 * Rules that keep it working (same toolchain as CalDAV Sync):
 *  - every function of a file that is moved is tagged, here and on its prototype;
 *  - no 32-bit multiply/divide, no function-local or const-table data in sec001 code: those reach
 *    libgcc and the data segment only from main (Data.c has the helpers: SecTicks, SecToStamp,
 *    BaudOf, LatLon);
 *  - build with -palmos4 (far calls between segments).
 */
#ifndef MC_SEG_H
#define MC_SEG_H

#define MC_SEC			__attribute__ ((section ("sec001")))
#define MC_UI			__attribute__ ((section ("sec002")))	/* the screens: lists, chat, node card */
#define RP_SECTION		MC_SEC
#define AES_SECTION		MC_SEC

#endif
