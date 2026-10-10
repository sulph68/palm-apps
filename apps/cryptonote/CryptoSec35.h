/* CryptoSec35.h - Sec35 shared-key encryption chokepoint for CryptoNote.
 *
 * Wraps the ENTIRE existing PackedRecordType blob as an outer layer, on top
 * of whatever CryptoNote's own inner format already is (title = first line
 * of body, body optionally "Encrypt Memo"-password-protected exactly as
 * before -- that mechanism is untouched, it just runs after the unwrap now).
 * See the CryptoNote encryption plan for the full design writeup.
 */
#ifndef __CRYPTOSEC35_H__
#define __CRYPTOSEC35_H__

#include "sects.h"
#include "Database.h"
#include "enccore.h"

/* Every CryptoPad DB record's leading byte, once migrated to this feature's
 * format. Values are >=0x80 so they can never collide with an OLD-format
 * record's own byte[0] (that's CryptoNote's pre-existing `flag` field,
 * always exactly 0 or 1 -- recDecrypted/recEncrypted) -- this is how
 * CryptoRecordWrapped/CryptoGetRecord tell old-format (no leading byte,
 * unpack raw) from new-format (leading byte present, strip 1 byte first)
 * without needing a separate DB attribute bit. */
#define kCryptoFmtMarker		0x80	/* set => new-format record (leading byte present) */
#define kCryptoFmtWrappedBit	0x01	/* combined with the marker => Sec35-wrapped */

/* Decrypt-or-mask chokepoint. Reads record `index` of `dbRef` (the
 * CryptoPad DB, dbCryptoPadRef) and unpacks it into *recordP exactly the
 * way UnpackRecord always has -- recordP->passwordP/textP point (zero-copy,
 * same as UnpackRecord always aliases) into a handle this function keeps
 * LOCKED across the return: the storage record itself for an old-format
 * or unwrapped record, or the reused decrypt scratch handle for a wrapped
 * one. Valid ONLY until the next CryptoGetRecord call (which unlocks it
 * first thing) -- same discipline as Datebook35's ApptGetRecord/gApptDecH.
 * Use it before calling CryptoGetRecord again for anything, including a
 * DIFFERENT record. If the record is Sec35-wrapped and locked, returns a
 * masked result instead (passwordP="", textP="******", string literals --
 * nothing stays locked for that case) -- title (the body's first line) is
 * masked too, since it lives inside the wrap. Returns false only on a
 * genuine read failure (bad index). */
Boolean CryptoGetRecord (DmOpenRef dbRef, UInt16 index, RecordPtr recordP) AES_SECTION;

/* Cheap list-row peek: title (first line only, truncated into outTitle/
 * outTitleSize) + the layer-2 "Encrypt Memo" flag, WITHOUT decrypting a
 * wrapped record's whole body (AES-CTR lets a bounded prefix decrypt
 * stand in for the real title safely). Use ONLY for list-row display --
 * anything that needs the actual body still goes through CryptoGetRecord.
 * See the .c file for why this exists (was a real "list looks hung after
 * importing/viewing a large memo" bug, from decrypting tens of KB twice
 * per visible row just to show one line of text). */
Boolean CryptoGetRecordTitle (DmOpenRef dbRef, UInt16 index, Char* outTitle, UInt16 outTitleSize, Boolean* outEncrypted) AES_SECTION;

/* Even cheaper than CryptoGetRecordTitle: just the layer-2 "Encrypt Memo"
 * flag, no title at all -- for callers deciding whether to prompt for
 * that password before navigating to open a record (e.g. a list-row
 * tap). Never decrypts more than a 54-byte header. Reads as false while
 * locked (matches isEncrypted()'s own convention) -- callers that care
 * about the locked case call CryptoLockedBounce first regardless. */
Boolean CryptoGetRecordFlag (DmOpenRef dbRef, UInt16 index) AES_SECTION;

/* True if record `index` is at or beyond the safe-to-edit size threshold
 * (99% of the ~60000-byte ceiling where a real "DmWriteCheck failed"
 * fatal error was confirmed on a record that size). Safe to VIEW regardless
 * -- callers should only refuse further EDITS of such a record (see
 * CryptoEditApplyGuard below) and tell the user to create a new memo
 * instead. Cheap: peeks only the record's header (textLen), never decrypts
 * the body even for a wrapped record. */
Boolean CryptoRecordTooLargeToEdit (DmOpenRef dbRef, UInt16 index) AES_SECTION;

/* EditForm.c's too-large-to-edit guard, kept here (not EditForm.c) purely
 * to stay out of the critically tight main segment -- EditForm.c's own
 * functions aren't AES_SECTION-tagged. CryptoEditApplyGuard: call once
 * right after a CryptoPad record's text is loaded into fieldP; checks
 * CryptoRecordTooLargeToEdit and sets the field's editable attribute
 * accordingly (read-only when too large -- blocks paste/cut/drag, not
 * just typing), returning the same Boolean so the caller can remember it
 * for CryptoEditGuardBlockKey. CryptoEditClearGuard: call for a record
 * that's never subject to the guard (MemoPad DB, a blank new record) to
 * force the field back to editable, since the field object is reused
 * across record-switch reloads and a previous too-large record's
 * read-only lock would otherwise stick. CryptoEditGuardBlockKey: call from
 * a keyDownEvent handler for any regular (non-hardkey, non-virtual)
 * keystroke; shows altRecordTooLargeToEdit once per open (via *warnedP)
 * and returns true (caller should consume/mark the event handled) only
 * when tooLarge is true. */
Boolean CryptoEditApplyGuard (DmOpenRef dbRef, UInt16 index, FieldPtr fieldP) AES_SECTION;
void CryptoEditClearGuard (FieldPtr fieldP) AES_SECTION;
Boolean CryptoEditGuardBlockKey (Boolean tooLarge, Boolean *warnedP) AES_SECTION;

/* True if record `index`'s on-disk leading byte marks it Sec35-wrapped.
 * Cheap (no decrypt), safe to call while locked. */
Boolean CryptoRecordWrapped (DmOpenRef dbRef, UInt16 index) AES_SECTION;

/* Call AFTER a record has been freshly written in PLAIN, old-format bytes
 * (e.g. right after PackRecord/EncryptRecord -- neither of those need any
 * changes, they still just write the original format) to seal it: prepends
 * the leading byte and, if Sec35 has an active passcode, encrypts the whole
 * thing in place. If no passcode is active, the record still gets the new
 * leading byte (unwrapped marker) so future reads never have to re-guess
 * old-vs-new format -- but stays fully plaintext, matching "everything
 * plain" mode. */
void CryptoSealRecord (DmOpenRef dbRef, UInt16 index) AES_SECTION;

/* Lock gate: if record `index` is wrapped and EncKeyPresent() is false,
 * bounce to Sec35's unlock UI (if installed) and return true (caller must
 * abort whatever it was about to do). Mirrors Datebook35's DayLockedBounce
 * verbatim, including the Sec35Installed() check -- a device without Sec35
 * installed at all still returns true (blocks) but never attempts the
 * SysUIAppSwitch. */
Boolean CryptoLockedBounce (DmOpenRef dbRef, UInt16 index) AES_SECTION;

/* Phase 5a: same bounce mechanism, but gates SD-card file crypto
 * operations generally (not a specific device record) -- if Sec35 has an
 * active passcode and is currently locked, bounce and return true.
 * Deliberately does NOT mix the Sec35 key into card files' own AES key
 * (would risk permanent data loss on removable media if the Sec35
 * password later changes/is forgotten) -- see the .c file's comment.
 * Call this before CardPromptPassword at every CardCryptEncrypt/
 * CardCryptDecrypt call site. */
Boolean CryptoCardLockedBounce (void) AES_SECTION;

/* One-time, silent format migration: prepend a clear (unwrapped, or wrapped
 * if a passcode is currently active) leading byte to every CryptoPad DB
 * record that doesn't have the new format yet (pre-existing records from
 * before this feature). No user-visible prompt -- pure format upgrade, no
 * content/meaning change from the user's point of view. Safe to call on
 * every launch (a no-op once everything's migrated). */
void CryptoMigrateRecordFormat (DmOpenRef dbRef) AES_SECTION;

/* ------------------------------------------------------------------ *
 *  Edit-session helpers -- kept here (not in EditForm.c/main, which is
 *  critically tight on code space) for the SAME reason Datebook35 keeps
 *  its own Location-field logic out of DateDay.c: a thin caller, all the
 *  real logic in a segment with room. See EditForm.c's
 *  EditRetrieveCryptoPadData/EditSaveCryptoPadData for how these are used.
 * ------------------------------------------------------------------ */

/* Call from EditRetrieveCryptoPadData. If record `index` is Sec35-wrapped,
 * decrypts it into a dedicated scratch handle and calls FldSetText on it
 * (the field edits that copy instead of live DB storage) and returns true
 * -- caller should do nothing more. If NOT wrapped, returns false --
 * caller should proceed with its own existing in-place-edit logic
 * unchanged. */
Boolean CryptoEditRetrieveWrapped (DmOpenRef dbRef, UInt16 index, FieldPtr fieldP) AES_SECTION;

/* Call from EditSaveCryptoPadData. If the CURRENT edit session was a
 * wrapped one (per CryptoEditRetrieveWrapped's last call), rebuilds the DB
 * record via PackRecord (reading the CURRENT text straight from its own
 * gCryptoEditH, NOT from the passed-in `textP` -- that pointer, captured
 * by the caller via FldGetTextPtr before FldCompactText runs, can be
 * stale/relocated by the time this is called; see the .c file), re-seals
 * it via CryptoSealRecord, and returns true -- caller should do nothing
 * more (except its own layer-2
 * EncryptRecord call, which still applies to both cases and is the
 * caller's own responsibility). If the session was NOT wrapped, returns
 * false -- caller should proceed with its own existing in-place length-fixup
 * logic unchanged (note: the caller must STILL call CryptoSealRecord itself
 * after that, in case a passcode is active for a record that started
 * unwrapped -- this function only handles the wrapped-session case's own
 * seal). */
Boolean CryptoEditSaveWrapped (DmOpenRef dbRef, UInt16 index, Char* textP) AES_SECTION;

/* Decrypt-or-unpack record `index`'s text into a FRESH, OWNED, unlocked
 * MemHandle containing just the NUL-terminated text (title+body, same as
 * record.textP) -- for callers (e.g. Beam.c's MemoSendRecord) that need a
 * plain text buffer to hand to something generic that doesn't know about
 * PackedRecordType/Sec35-wrapping at all. Caller locks/unlocks/frees *outH
 * itself. Returns false (leaves *outH untouched) on failure. */
Boolean CryptoCopyTextToHandle (DmOpenRef dbRef, UInt16 index, MemHandle* outH) AES_SECTION;

/* Call once at app launch (from Main.c's StartApplication -- kept to a
 * single call there, all the real logic lives here since main is
 * critically tight on code space). Silently migrates/seals every existing
 * CryptoPad DB record per CryptoMigrateRecordFormat, then checks the
 * MemoPad DB: if it has any records, confirms with the user
 * (altMigrateMemoPadAlert) and, if confirmed, migrates each into a new
 * Sec35-wrapped CryptoPad record and deletes the MemoPad original. Runs
 * (and re-prompts if MemoPad records still exist) on every launch -- no
 * one-time flag, so a MemoPad record that reappears later (HotSync,
 * another app) gets caught on the next launch too. */
void CryptoLaunchMaintenance (void) AES_SECTION;

/* Creates a brand-new, empty record directly in `dbRef` (CryptoPad's own
 * DB) with category `category`, and seals it (adds the marker, wraps if a
 * passcode is active) same as any other fresh write -- used by
 * Database.c's CreateRecord ("New"), kept here since main is critically
 * tight on code space. *indexOut receives the new record's index. Returns
 * false (leaves *indexOut untouched) only if DmNewRecord itself fails
 * (out of storage) -- caller shows its own DeviceFullAlert. */
Boolean CryptoCreateRecord (DmOpenRef dbRef, UInt16* indexOut, UInt16 category) AES_SECTION;

/* Real logic behind Database.c's CompareCryptoPadRecords (kept here, not
 * there, since that entry point must stay in main -- see its own comment).
 * rec1/rec2 are raw locked record-storage pointers (NOT RecordType) exactly
 * as the DM sort routine hands them over. Handles all 3 on-disk formats:
 * legacy (no marker), marked-but-plain (unpacks and sorts correctly), and
 * truly wrapped (ciphertext -- falls back to a bounded raw-byte compare,
 * see the .c file for why unbounded StrCompare was unsafe there). */
Int16 CryptoCompareRecords (const UInt8* rec1, const UInt8* rec2, Int16 sortOrder) AES_SECTION;

/* Phase 5b: Import from Card. Splits `text` (a card file's decrypted, NUL-
 * terminated content) into one or more new CryptoPad DB records under
 * `category`, sealing each one exactly like any other fresh write
 * (CryptoSealRecord -- wraps if a Sec35 passcode is active, plain
 * otherwise). Parts are capped at ~60000 bytes each (matches
 * MainFormOpenCardFile's own ceiling), split at the nearest newline at or
 * before the cap. Every part gets an explicit injected title line
 * ("<baseName>" for a single part, "<baseName> 00N" for a multi-part
 * import) -- deliberately NOT relying on the file's own first line, which
 * may be blank and would otherwise show up as an apparently-empty list
 * row. Writes one part at a time (encode+seal+release before
 * starting the next) to stay within the Palm IIIx RAM budget for large
 * files. Returns the number of records actually created (may be less than
 * the expected part count if MemPtrNew/DmNewRecord fails partway --
 * caller should compare against the expected count and warn if short). */
UInt16 CryptoImportText (const char* text, const char* baseName, UInt16 category) AES_SECTION;

/* Cheap pre-check for the confirm dialog above: how many parts a
 * `textLen`-byte import will produce, without doing any of the actual
 * work (no split-point search, no writes). Kept here (not a raw formula
 * in EditForm.c) purely so the per-part size cap stays defined once, in
 * sec001, next to CryptoImportText itself. */
UInt16 CryptoImportPartCount (UInt32 textLen) AES_SECTION;

/* Confirm/import/report-result for a card file too large to open in the
 * editor field (>60000 bytes) -- an alternative to the plain "too large"
 * refusal, since CryptoImportText doesn't touch the Field Manager and
 * doesn't share that size ceiling. Takes ownership of plainH (frees it on
 * every exit path). plainH must NOT be NUL-terminated yet. */
void CryptoImportFromCardBuffer (MemHandle plainH, UInt32 plainLen, const char* baseName, UInt16 category) AES_SECTION;

/* Reads and imports a PLAIN card file ONE PART AT A TIME, directly from
 * VFS -- never holds the whole file in one buffer, regardless of the
 * file's total size (bounded only by one part's worth, <=60000 bytes, at
 * a time). Use for a card file too large for CardReadFile's own single
 * MemHandleNew to succeed (confirmed failing with memErrNotEnoughSpace
 * on a real device for a ~98KB file). Does NOT handle an encrypted
 * source file -- caller must confirm it's plain first. Returns the
 * number of records actually created. */
UInt16 CryptoImportCardFile (UInt16 volRef, const char* path, const char* baseName, UInt16 category) AES_SECTION;

/* Confirm/import/report-result wrapper around CryptoImportCardFile,
 * mirroring CryptoImportFromCardBuffer's UX but sourcing directly from
 * the file instead of a pre-loaded buffer. */
void CryptoImportFromCardFile (UInt16 volRef, const char* path, const char* baseName, UInt16 category) AES_SECTION;

/* Same as CryptoImportCardFile, but for an ENCRYPTED card file -- reads
 * and imports the DECRYPTED body one part at a time directly from VFS
 * (AES-CTR's random-access property makes this safe; see the .c file's
 * comment for the full explanation). Verifies `password` against the
 * file's own embedded verifier first; returns a sentinel
 * (0xFFFF, see the .c file) if it doesn't match, distinct from a
 * genuine 0-created failure. Does NOT need CryptoCardLockedBounce
 * called first -- caller (MainFormOpenCardFile) already does that
 * before prompting for the password at all. */
UInt16 CryptoImportEncryptedCardFile (UInt16 volRef, const char* path, const char* password,
                                        const char* baseName, UInt16 category) AES_SECTION;

/* Confirm/import/report-result wrapper around CryptoImportEncryptedCardFile,
 * mirroring CryptoImportFromCardFile's UX for the encrypted case. */
void CryptoImportFromEncryptedCardFile (UInt16 volRef, const char* path, const char* password,
                                          const char* baseName, UInt16 category) AES_SECTION;

/* Single-record encrypt/decrypt busy indicator: use CardBusyShow/
 * CardBusyHide (CardCrypt.h) directly at every call site -- standardized
 * on the SD-card path's existing lightweight WinSaveBits-based indicator
 * instead of a separate CryptoSec35-specific mechanism (an earlier
 * FORM-based version with an animated bar was replaced after user
 * feedback preferred the calmer card style everywhere). Do NOT wrap the
 * EXISTING bulk EncryptCategory/DecryptCategory loops with it -- those
 * already show their own frmProgressForm gauge. */

#endif /* __CRYPTOSEC35_H__ */
