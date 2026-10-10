/*
 * vcard.h - vCard 3.0/4.0 <-> Address35 record conversion.
 *
 * Import parses vCard 3.0 (RFC 2426) and 4.0 (RFC 6350), tolerant of 2.1-isms.
 * Export writes vCard 4.0. The PARSER (vcard.c) lives in the MAIN segment: the
 * beam RECEIVE/import path is a no-globals sublaunch and must reach this code
 * (see [[beam-receive-sublaunch-segment]]). The EMITTER (vcardw.c) is only
 * reached from the foreground SEND/export path, so it lives in a secondary
 * segment (SEC002) instead.
 *
 * Field map (see apps/address35/Address35.md section 8):
 *   N/FN/ORG/TITLE/NICKNAME, TEL(+TYPE), EMAIL, ADR(+TYPE), BDAY, ANNIVERSARY,
 *   X-SOCIALPROFILE/IMPP, URL, CATEGORIES(->tags), RELATED(+TYPE), NOTE, GENDER,
 *   X-PRONOUNS; unknown properties are preserved as a35fCustom passthrough.
 */
#ifndef VCARD_H
#define VCARD_H

#include <PalmOS.h>
#include "AddressDB35.h"

#ifndef VCARD_SECTION
#define VCARD_SECTION
#endif
#ifndef SEC001				/* host regression harness has no a35_seg.h */
#define SEC001
#endif
#ifndef SEC002
#define SEC002
#endif

/* Scratch arena the parser decodes field values into. One import at a time; the
 * produced A35Record's field .value pointers reference this arena, so it must
 * outlive the record until it is packed (A35NewRecord/A35ChangeRecord). */
#define vcArenaLen		4096

typedef struct {
	char	buf[vcArenaLen];
	UInt16	used;
} VCardArena;

#ifdef __cplusplus
extern "C" {
#endif

/* Serialize record r as a vCard 4.0 into buf (capacity bufLen, NUL-terminated).
 * dbP resolves tag names + relation target display names. Returns the string
 * length written, or 0 on overflow/error. Emitter lives in vcardw.c / SEC002. */
UInt16 A35RecordToVCard(DmOpenRef dbP, const A35Record *r, char *buf, UInt16 bufLen)
	SEC002;

/* Parse ONE vCard from text[0..len) into *r, decoding values into *arena.
 * Returns true if a BEGIN:VCARD..END:VCARD with at least a name/org was parsed.
 * dbP is used to add any CATEGORIES to the tag dictionary. The record's field
 * values point into *arena (keep it alive until the record is packed).
 * len is UInt32 (not UInt16): a real-world vCard with an embedded PHOTO can
 * easily exceed 64KB, and this was a real bug (files got silently truncated
 * before their END:VCARD, so import failed) - see [[strncat-strlcat-semantics-gotcha]]
 * sibling gotcha, same "narrow int truncates real-world data" family. */
Boolean A35VCardToRecord(DmOpenRef dbP, const char *text, UInt32 len,
	A35Record *r, VCardArena *arena) VCARD_SECTION;

/* Length of the first vCard (BEGIN..END inclusive) starting at text; 0 if none.
 * Lets a caller walk a multi-person .vcf file one card at a time. Used only by
 * A35CardSplit (needs raw byte ranges to copy verbatim) and the beam-merge path
 * in Transfer.c (parses an already-in-memory DmRecord, not an SD-card file). */
UInt32 A35VCardSpan(const char *text, UInt32 len) VCARD_SECTION;

/* ---- streaming reader (SD-card files) ----------------------------------- *
 * Reads a VFS file through a small fixed-size rolling buffer instead of
 * loading the whole file into one MemHandle - a real-world vCard with an
 * embedded PHOTO can be 50-100KB+, and a single big allocation/VFSFileRead
 * was observed to silently fail on real (memory-constrained) hardware even
 * though the exact same file parsed fine on an unlimited-RAM host. PHOTO/LOGO/
 * SOUND/KEY values are discarded as they stream past, never buffered - callers
 * that need the vCard's RAW bytes (A35CardSplit, writing a verbatim copy) must
 * use A35VCardSpan/A35VCardToRecord on a fully-read buffer instead.
 * `fileRef` is a VFSMgr FileRef (== UInt32) stored untyped so this header never
 * needs <VFSMgr.h> - pulling that into some callers' include context triggers a
 * trap-macro parse error (same reason CardIO.h keeps it .c-only). */
/* progress callback: barWidthPx is already computed (0..VC_PROGRESS_WIDTH) so
 * the caller (which may live in a SECONDARY segment) never needs to do 32-bit
 * multiply/divide itself - that crashes outside main (see the family's
 * multiseg-libgcc gotcha). All the arithmetic stays in vcard.c/main. */
#define VC_PROGRESS_WIDTH	142		/* matches AddrCard.c's A35ProgressBar bar fill cap */
typedef void (*VCardProgressFn)(void *ctx, Int16 barWidthPx);

typedef struct {
	UInt32	fileRef;
	UInt32	fileSize;
	UInt32	readTotal;
	VCardProgressFn progressFn;
	void	*progressCtx;
	char	buf[256];
	UInt16	len;			/* valid bytes currently in buf */
	UInt16	pos;			/* next unread index in buf */
	Boolean	eof;
	Boolean	hasPeek;
	char	peekCh;
} VCardStream;

/* Open path on volRef for streaming read. */
Err A35VCardOpenStream(VCardStream *s, UInt16 volRef, const char *path) VCARD_SECTION;
void A35VCardCloseStream(VCardStream *s) VCARD_SECTION;

/* Optional: get a barWidthPx callback as bytes are read, e.g. to drive a
 * "Reading..." busy dialog on large (photo-heavy) files. Set before the first
 * A35VCardStreamNext call; NULL (the default after Open) disables it. */
void A35VCardSetProgress(VCardStream *s, VCardProgressFn fn, void *ctx) VCARD_SECTION;

/* Parse the NEXT vCard from the stream into *r (decoding values into *arena),
 * skipping any bytes before its BEGIN:VCARD. Returns false once the stream is
 * exhausted (no more vCards) - callers loop this to walk a multi-person file
 * one card at a time, same as the old CardReadFile+A35VCardSpan loop but
 * without ever holding the whole file in memory. */
Boolean A35VCardStreamNext(DmOpenRef dbP, VCardStream *s, A35Record *r,
	VCardArena *arena) VCARD_SECTION;

/* Extract a display name ("First Last" or Company, "(Unnamed)" as a last
 * resort) from an ALREADY-parsed record - the non-parsing half of what used
 * to be AddrCard.c's A35VCardName, split out so streaming callers (which
 * already have a parsed A35Record from A35VCardStreamNext) don't need to
 * re-parse text to get a name. */
void A35VCardNameFromRecord(const A35Record *r, char *out, UInt16 outLen) VCARD_SECTION;

#ifdef __cplusplus
}
#endif

#endif /* VCARD_H */
