#include <PalmOS.h>
/* Straight after PalmOS.h, nothing else in between -- matches CardIO.c's
 * own (the ONLY other file using raw VFS calls) include order exactly.
 * VFSMgr.h hits a parse error if anything else (CardIO.h itself
 * deliberately excludes it for this reason, per its own header comment)
 * gets included first. Needed for CryptoImportCardFile's direct VFS
 * calls. */
#include <VFSMgr.h>
#include "CryptoSec35.h"
#include "MainRsc.h"	/* altMigrateMemoPadAlert */
#include "CardIO.h"	/* cardMaxName -- CryptoImportText title buffer sizing */
#include "CardCrypt.h"	/* CardBusyShow/CardBusyHide -- shared busy-dialog style */
#include "Utils.h"	/* SafeStrLen -- ROM StrLen's Int16 return wraps past 32767 bytes */
#include "AesCrypt.h"	/* AESCryptEx/AesCtrAdvanceIV -- streaming decrypt for large encrypted card files */
#include "Database.h"	/* globalPasswordP */

/* Read-time TTL: effectively "as long as Sec35 says the key is valid" --
 * Sec35's own alarm enforces the real lock. Matches Datebook35's
 * apptEncTtl / Address35's a35EncTtl convention exactly. */
#define kCryptoEncTtl	1000000000UL

/* kCryptoRecordCeiling is the reference "this is where PalmOS DmWriteCheck
 * failures start happening" limit -- a record imported right up against
 * 60000 bytes (an earlier, less conservative import cap) could be
 * IMPORTED fine, but FAILED TO EDIT afterward with a genuine "DmWriteCheck
 * failed" fatal error, confirmed on real hardware. The Field Manager
 * needs headroom beyond a record's raw content size while it's being
 * actively edited (undo/gap-buffer state, resize-in-place growth, etc.)
 * that isn't accounted for by the record's own textLen alone.
 * kCryptoImportMaxPart (used by CryptoImportText/CryptoImportCardFile/
 * CryptoImportEncryptedCardFile below) keeps a 5% safety margin below
 * this reference so a freshly-imported part can always still be opened
 * and edited afterward without hitting this wall. kCryptoRecordEditWarnLen
 * (used by CryptoRecordTooLargeToEdit, the "is this record still safely
 * editable" chokepoint called from MainFormEnterTableItem) is a tighter
 * 1% margin -- a late warning for a record that's already grown large by
 * some OTHER means (not a fresh import), not a size this app chose. */
#define kCryptoRecordCeiling      60000UL
#define kCryptoImportMaxPart      ((UInt32) (kCryptoRecordCeiling * 95UL / 100UL))   /* 5% margin, ~57000 */
#define kCryptoRecordEditWarnLen  ((UInt32) (kCryptoRecordCeiling * 99UL / 100UL))   /* 1% margin, ~59400 */

/* Read chokepoint decrypt buffer: CryptoGetRecord decrypts a wrapped record
 * into this single reused handle and hands back a RecordType unpacked from
 * it. Does NOT touch the storage handle's own lock -- CryptoGetRecord owns
 * that lock itself (see the chunk-over-lock-leak precedent in Datebook35's
 * ApptGetDecrypted). Foreground, one-record-at-a-time. */
static MemHandle gCryptoDecH = NULL;

/* UnpackRecord (Database.c) is zero-copy: recordP->textP/passwordP end up
 * pointing DIRECTLY into whatever buffer was unpacked (the storage handle
 * for old-format/unwrapped records, gCryptoDecH for wrapped ones) -- never
 * a fresh copy. That buffer MUST stay locked for as long as the caller
 * might still read recordP->textP, or PalmOS is free to relocate it on
 * the next allocation (e.g. CryptoEditRetrieveWrapped's own MemHandleNew
 * right after calling CryptoGetRecord), silently turning textP into a
 * dangling pointer -- a real "address error" crash hit during testing.
 * gCryptoLastLockedH tracks whichever handle the MOST RECENT
 * CryptoGetRecord call left locked for this reason; it's only safe to
 * unlock at the START of the NEXT call (once the caller must be done
 * with the previous result), never before returning. */
static MemHandle gCryptoLastLockedH = NULL;

/* ------------------------------------------------------------------ *
 *  Sec35 bounce gate (mirrors Datebook35's Sec35Installed/SwitchToSec35/
 *  DayLockedBounce verbatim -- same enccore.h constants, same mechanism).
 * ------------------------------------------------------------------ */
static Boolean CryptoSec35Installed (void)
{
	DmSearchStateType	st;
	UInt16				card;
	LocalID				dbID;
	return (Boolean) (DmGetNextDatabaseByTypeCreator (true, &st, sysFileTApplication,
		ENC_CREATOR, true, &card, &dbID) == errNone);
}

static void CryptoSwitchToSec35 (void)
{
	DmSearchStateType	st;
	UInt16				card;
	LocalID				dbID;
	if (DmGetNextDatabaseByTypeCreator (true, &st, sysFileTApplication, ENC_CREATOR, true,
			&card, &dbID) != errNone)
		return;
	FtrSet (ENC_CREATOR, kSecReturnFtr, cryptoPadCreator);		/* return-to creator */
	SysUIAppSwitch (card, dbID, kSecLaunchUnlock, NULL);
}

Boolean CryptoRecordWrapped (DmOpenRef dbRef, UInt16 index)
{
	MemHandle	h = DmQueryRecord (dbRef, index);
	UInt8		marker;
	if (! h) return false;
	marker = ((const UInt8 *) MemHandleLock (h))[0];
	MemHandleUnlock (h);
	return (Boolean) ((marker & kCryptoFmtMarker) && (marker & kCryptoFmtWrappedBit));
}

/* True if record `index` has been migrated to the NEW on-disk format AT
 * ALL (leading marker byte present) -- true for BOTH the "marked but
 * plain" and the "wrapped" cases, false only for legacy pre-migration
 * records. Distinct from CryptoRecordWrapped (marker AND wrapped bit):
 * this answers "does this record's storage have the +1 byte offset
 * shift", not "is this record currently encrypted" -- needed below,
 * because a marked-but-plain record's raw bytes are NOT a
 * PackedRecordType starting at offset 0 (the old in-place-edit path's
 * assumption) -- reading it that way misreads the marker byte as
 * record.flag and shifts every field by one byte, corrupting textLen/
 * passwdLen/IVs and pointing textP at garbage. Caught via a real crash
 * ("address error... reading from a memory location") on first device
 * test after CryptoLaunchMaintenance had migrated every existing record
 * to at least marked-but-plain. */
static Boolean CryptoRecordMigrated (DmOpenRef dbRef, UInt16 index)
{
	MemHandle	h = DmQueryRecord (dbRef, index);
	UInt8		marker;
	if (! h) return false;
	marker = ((const UInt8 *) MemHandleLock (h))[0];
	MemHandleUnlock (h);
	return (Boolean) ((marker & kCryptoFmtMarker) != 0);
}

Boolean CryptoLockedBounce (DmOpenRef dbRef, UInt16 index)
{
	if (! EncKeyPresent () && CryptoRecordWrapped (dbRef, index))
		{
		if (CryptoSec35Installed ())
			CryptoSwitchToSec35 ();
		return true;
		}
	return false;
}

/* Phase 5a gate for SD-card file crypto (CardCryptEncrypt/CardCryptDecrypt
 * call sites in MainForm.c/EditForm.c): if Sec35 has an active passcode
 * but is currently LOCKED, bounce to Sec35's unlock UI and return true
 * (caller must abort). Deliberately does NOT fold the Sec35 session key
 * into the card file's own AES key derivation the way device records get
 * wrapped -- that would make every card file encrypted while a Sec35
 * passcode is active PERMANENTLY undecryptable the moment that passcode
 * later changes or is forgotten, since Sec35 has no way to reach/rekey
 * files on removable media (unlike device DB records, which
 * RecryptCryptoNote can always walk). This is a UI-level "you need Sec35
 * unlocked to touch encrypted card files" gate only -- the file's actual
 * key stays derived purely from its own per-file password + device
 * secret, exactly as before Sec35 existed. User-approved design choice
 * (asked directly given the data-loss stakes, chose gate-only over the
 * original plan's "fold the key in" wording). Returns false (nothing to
 * block) if Sec35 has no active passcode at all, or is already
 * unlocked. */
Boolean CryptoCardLockedBounce (void)
{
	if (EncEncryptionActive () && ! EncKeyPresent ())
		{
		if (CryptoSec35Installed ())
			CryptoSwitchToSec35 ();
		return true;
		}
	return false;
}

/* ------------------------------------------------------------------ *
 *  Read chokepoint
 * ------------------------------------------------------------------ */
Boolean CryptoGetRecord (DmOpenRef dbRef, UInt16 index, RecordPtr recordP)
{
	MemHandle		h = DmQueryRecord (dbRef, index);
	const UInt8*	raw;
	UInt8			marker;

	if (! h) return false;

	/* The PREVIOUS call's recordP->textP (if any) aliased into whichever
	 * handle we left locked below -- safe to drop now, the caller must be
	 * done with that result before calling CryptoGetRecord again. */
	if (gCryptoLastLockedH) { MemHandleUnlock (gCryptoLastLockedH); gCryptoLastLockedH = NULL; }

	raw = (const UInt8 *) MemHandleLock (h);
	marker = raw[0];

	if (! (marker & kCryptoFmtMarker))
		{
		/* Old format (pre-dates this feature): no leading byte, the whole
		 * blob is already the plain PackedRecordType -- unpack directly.
		 * recordP aliases into h -- stays locked, see gCryptoLastLockedH. */
		UnpackRecord (recordP, (PackedRecordPtr) raw);
		gCryptoLastLockedH = h;
		return true;
		}

	if (! (marker & kCryptoFmtWrappedBit))
		{
		/* New format, unwrapped (migrated, or written while no passcode was
		 * active): skip the 1 leading byte, rest is plain. Same
		 * stays-locked deal as above. */
		UnpackRecord (recordP, (PackedRecordPtr) (raw + 1));
		gCryptoLastLockedH = h;
		return true;
		}

	/* Wrapped. */
	{
	UInt32		size = (UInt32) MemHandleSize (h) - 1;
	UInt8		key[ENC_KEY_BYTES];
	Boolean		haveKey = EncGetKey (key, kCryptoEncTtl);

	if (! haveKey)
		{
		/* Locked: mask. Title (the body's first line) is masked along with
		 * the body since both live inside the wrap -- exactly what was
		 * asked for. Returns string literals (ROM-resident, never
		 * relocated) -- nothing needs to stay locked for this case. */
		MemHandleUnlock (h);
		recordP->flag      = recDecrypted;
		recordP->passwdLen = 0;
		recordP->textLen   = 6;
		recordP->passwordP = "";
		recordP->textP     = "******";
		return true;
		}

	if (gCryptoDecH != NULL && (UInt32) MemHandleSize (gCryptoDecH) < size)
		{
		MemHandleFree (gCryptoDecH);
		gCryptoDecH = NULL;
		}
	if (gCryptoDecH == NULL)
		gCryptoDecH = MemHandleNew (size);
	if (gCryptoDecH == NULL)
		{
		MemHandleUnlock (h);
		MemSet (key, ENC_KEY_BYTES, 0);
		return false;
		}

	{
	UInt8*	dec = (UInt8 *) MemHandleLock (gCryptoDecH);
	UInt32	uid = 0;
	MemMove (dec, raw + 1, size);
	MemHandleUnlock (h);					/* done with storage -- gCryptoDecH holds the copy now */

	DmRecordInfo (dbRef, index, NULL, &uid, NULL);
	EncCrypt (key, dec, size, uid, 0);
	MemSet (key, ENC_KEY_BYTES, 0);

	UnpackRecord (recordP, (PackedRecordPtr) dec);
	/* recordP aliases into gCryptoDecH -- stays locked, see
	 * gCryptoLastLockedH (NOT unlocked here, unlike before this fix). */
	gCryptoLastLockedH = gCryptoDecH;
	return true;
	}
	}
}

/* Fixed on-disk header size (flag+passwdLen+textLen+pwIV+bodyIV+kdfSalt+
 * kdfRounds), computed the same way PackRecord's own nLength is -- sum of
 * individual field sizes, NOT sizeof(PackedRecordType) (which could
 * differ due to struct padding the on-disk layout doesn't have). */
#define kCryptoRecHeaderLen \
	(sizeof(((PackedRecordPtr)0)->flag) + sizeof(((PackedRecordPtr)0)->passwdLen) + \
	 sizeof(((PackedRecordPtr)0)->textLen) + sizeof(((PackedRecordPtr)0)->pwIV) + \
	 sizeof(((PackedRecordPtr)0)->bodyIV) + sizeof(((PackedRecordPtr)0)->kdfSalt) + \
	 sizeof(((PackedRecordPtr)0)->kdfRounds))

#define kCryptoTitlePeekCap 80	/* max title bytes decrypted for a LIST ROW peek */

static void CryptoTitleFromText (const Char* text, Char* outTitle, UInt16 outSize) AES_SECTION;
static void CryptoTitleFromText (const Char* text, Char* outTitle, UInt16 outSize)
{
	const Char*	nl = StrChr ((Char*) text, linefeedChr);
	UInt16		len;
	if (nl)
		{
		len = (UInt16) (nl - text);
		if (len > outSize - 1) len = outSize - 1;
		}
	else
		{
		/* No newline: bound-scan for NUL ourselves instead of StrLen (which
		 * wraps past 32767 bytes -- a real risk here, this text can be a
		 * whole ~60000-byte record body with no newline at all). We only
		 * need up to outSize-1 chars anyway. */
		for (len = 0; len < outSize - 1 && text[len]; len++) ;
		}
	MemMove (outTitle, text, len);
	outTitle[len] = 0;
}

/* List-row peek: title text (first line, truncated to outTitleSize) +
 * the layer-2 "Encrypt Memo" flag, WITHOUT decrypting a wrapped record's
 * entire body. CryptoGetRecord's full-record decrypt (AES-CTR over the
 * whole blob, up to tens of KB for a multi-part-import-sized record) was
 * being called TWICE per visible list row (once by the list draw itself,
 * once more by isEncrypted()'s own separate CryptoGetRecord call) just to
 * show one line of title and a key-icon flag -- the actual cause of the
 * list appearing to hang after importing or viewing a large memo. AES-CTR
 * is a stream cipher: the keystream for byte i never depends on bytes >
 * i, so decrypting only a small PREFIX (header + password field + a
 * bounded title cap) yields byte-identical output to decrypting the
 * whole thing, at a fraction of the cost. Use this ONLY for list-row
 * display; anything that needs the real body (opening a memo, beaming,
 * etc.) still goes through CryptoGetRecord. */
Boolean CryptoGetRecordTitle (DmOpenRef dbRef, UInt16 index, Char* outTitle, UInt16 outTitleSize, Boolean* outEncrypted)
{
	MemHandle		h = DmQueryRecord (dbRef, index);
	const UInt8*	raw;
	UInt8			marker;

	if (outTitleSize) outTitle[0] = 0;
	*outEncrypted = false;
	if (! h || ! outTitleSize) return false;

	raw = (const UInt8 *) MemHandleLock (h);
	marker = raw[0];

	if (! (marker & kCryptoFmtMarker))
		{
		RecordType rec;
		UnpackRecord (&rec, (PackedRecordPtr) raw);
		CryptoTitleFromText (rec.textP, outTitle, outTitleSize);
		*outEncrypted = (Boolean) (rec.flag == recEncrypted);
		MemHandleUnlock (h);
		return true;
		}

	if (! (marker & kCryptoFmtWrappedBit))
		{
		RecordType rec;
		UnpackRecord (&rec, (PackedRecordPtr) (raw + 1));
		CryptoTitleFromText (rec.textP, outTitle, outTitleSize);
		*outEncrypted = (Boolean) (rec.flag == recEncrypted);
		MemHandleUnlock (h);
		return true;
		}

	/* Wrapped: bounded-prefix decrypt only. */
	{
	UInt32	fullSize = (UInt32) MemHandleSize (h) - 1;
	UInt8	key[ENC_KEY_BYTES];
	Boolean	haveKey = EncGetKey (key, kCryptoEncTtl);
	UInt8	hdr[kCryptoRecHeaderLen];
	UInt32	uid = 0;
	UInt16	hdrLen = (UInt16) ((fullSize < kCryptoRecHeaderLen) ? fullSize : kCryptoRecHeaderLen);
	UInt32	need;
	UInt8*	buf;

	if (! haveKey)
		{
		MemHandleUnlock (h);
		StrNCopy (outTitle, "******", outTitleSize - 1);
		outTitle[outTitleSize - 1] = 0;
		return true;
		}

	DmRecordInfo (dbRef, index, NULL, &uid, NULL);

	/* Stage 1: header only, to learn passwdLen (byte offset 1) so we know
	 * where the text field actually starts. */
	MemMove (hdr, raw + 1, hdrLen);
	EncCrypt (key, hdr, hdrLen, uid, 0);
	*outEncrypted = (Boolean) (hdr[0] == recEncrypted);

	/* Stage 2: re-decrypt from byte 0 up through header+password+a bounded
	 * title cap -- EncCrypt always reinitializes the CTR counter from the
	 * same nonce, so this yields identical bytes for the overlapping
	 * header region, just a fresh, larger prefix. */
	need = kCryptoRecHeaderLen + (UInt8) hdr[1] + 1 + kCryptoTitlePeekCap;
	if (need > fullSize) need = fullSize;

	buf = (UInt8 *) MemPtrNew (need);
	if (! buf) { MemHandleUnlock (h); MemSet (key, ENC_KEY_BYTES, 0); return true; }
	MemMove (buf, raw + 1, need);
	MemHandleUnlock (h);

	EncCrypt (key, buf, need, uid, 0);
	MemSet (key, ENC_KEY_BYTES, 0);

	{
	UInt32 textOff = kCryptoRecHeaderLen + (UInt8) hdr[1] + 1;
	if (textOff < need)
		CryptoTitleFromText ((Char*) (buf + textOff), outTitle, outTitleSize);
	}
	MemPtrFree (buf);
	return true;
	}
}

/* Cheapest possible peek at just the layer-2 "Encrypt Memo" flag, for
 * callers that only need to decide whether to prompt for that password
 * before navigating -- e.g. MainFormEnterTableItem on a list-row tap, or
 * EditForm.c's Find-result navigation. isEncrypted() (Main.c) does a FULL
 * CryptoGetRecord decrypt for this same one-byte answer; calling that
 * synchronously on a row tap, with no busy indicator, meant a real
 * invisible hang before "Opening..." even appeared for a large wrapped
 * memo (the tap-time decrypt happened silently, THEN CryptoEditRetrieveWrapped
 * decrypted the SAME record again for the actual open). This decrypts at
 * most a 54-byte header -- never the body -- when wrapped. Locked-and-
 * can't-decrypt reads as false, matching isEncrypted()'s own convention;
 * callers that care about the locked case call CryptoLockedBounce first
 * anyway (which unconditionally blocks before this would ever run). */
Boolean CryptoGetRecordFlag (DmOpenRef dbRef, UInt16 index)
{
	MemHandle		h = DmQueryRecord (dbRef, index);
	const UInt8*	raw;
	UInt8			marker;
	Boolean			result = false;

	if (! h) return false;
	raw = (const UInt8 *) MemHandleLock (h);
	marker = raw[0];

	if (! (marker & kCryptoFmtMarker))
		result = (Boolean) (raw[0] == recEncrypted);
	else if (! (marker & kCryptoFmtWrappedBit))
		result = (Boolean) (raw[1] == recEncrypted);
	else
		{
		UInt32	fullSize = (UInt32) MemHandleSize (h) - 1;
		UInt8	key[ENC_KEY_BYTES];
		if (EncGetKey (key, kCryptoEncTtl))
			{
			UInt8	hdr[kCryptoRecHeaderLen];
			UInt32	uid = 0;
			UInt16	hdrLen = (UInt16) ((fullSize < kCryptoRecHeaderLen) ? fullSize : kCryptoRecHeaderLen);
			DmRecordInfo (dbRef, index, NULL, &uid, NULL);
			MemMove (hdr, raw + 1, hdrLen);
			EncCrypt (key, hdr, hdrLen, uid, 0);
			MemSet (key, ENC_KEY_BYTES, 0);
			result = (Boolean) (hdr[0] == recEncrypted);
			}
		}

	MemHandleUnlock (h);
	return result;
}

/* True if record `index`'s content is at or beyond kCryptoRecordEditWarnLen
 * (99% of kCryptoRecordCeiling) -- callers should refuse to open it for
 * editing and direct the user to create a new memo instead, rather than
 * risk a genuine "DmWriteCheck failed" fatal error (confirmed real for a
 * record sized right up against the ceiling). Cheap: peeks only the
 * record's fixed header (same mechanism as CryptoGetRecordFlag) to read
 * textLen -- never decrypts the body, even for a wrapped record. Reads
 * as false while locked (can't tell -- matches CryptoGetRecordFlag's own
 * convention; CryptoLockedBounce already blocks that case first anyway). */
Boolean CryptoRecordTooLargeToEdit (DmOpenRef dbRef, UInt16 index)
{
	MemHandle		h = DmQueryRecord (dbRef, index);
	const UInt8*	raw;
	UInt8			marker;
	UInt16			textLen = 0;

	if (! h) return false;
	raw = (const UInt8 *) MemHandleLock (h);
	marker = raw[0];

	if (! (marker & kCryptoFmtMarker))
		MemMove (&textLen, raw + 2, sizeof (textLen));		/* flag(1)+passwdLen(1) */
	else if (! (marker & kCryptoFmtWrappedBit))
		MemMove (&textLen, raw + 3, sizeof (textLen));		/* +1 for the marker byte */
	else
		{
		UInt32	fullSize = (UInt32) MemHandleSize (h) - 1;
		UInt8	key[ENC_KEY_BYTES];
		if (EncGetKey (key, kCryptoEncTtl))
			{
			UInt8	hdr[kCryptoRecHeaderLen];
			UInt32	uid = 0;
			UInt16	hdrLen = (UInt16) ((fullSize < kCryptoRecHeaderLen) ? fullSize : kCryptoRecHeaderLen);
			DmRecordInfo (dbRef, index, NULL, &uid, NULL);
			MemMove (hdr, raw + 1, hdrLen);
			EncCrypt (key, hdr, hdrLen, uid, 0);
			MemSet (key, ENC_KEY_BYTES, 0);
			if (hdrLen >= 4) MemMove (&textLen, hdr + 2, sizeof (textLen));
			}
		}

	MemHandleUnlock (h);
	return (Boolean) ((UInt32) textLen >= kCryptoRecordEditWarnLen);
}

/* See CryptoSec35.h for the full contract of these three -- kept here
 * (sec001) instead of EditForm.c purely to stay off the critically tight
 * main segment. */
Boolean CryptoEditApplyGuard (DmOpenRef dbRef, UInt16 index, FieldPtr fieldP)
{
	Boolean tooLarge = CryptoRecordTooLargeToEdit (dbRef, index);
	FieldAttrType attr;

	FldGetAttributes (fieldP, &attr);
	attr.editable = (Boolean) !tooLarge;
	FldSetAttributes (fieldP, &attr);

	return tooLarge;
}

void CryptoEditClearGuard (FieldPtr fieldP)
{
	FieldAttrType attr;

	FldGetAttributes (fieldP, &attr);
	attr.editable = true;
	FldSetAttributes (fieldP, &attr);
}

Boolean CryptoEditGuardBlockKey (Boolean tooLarge, Boolean *warnedP)
{
	if (! tooLarge) return false;

	if (! *warnedP) {
		FrmAlert (altRecordTooLargeToEdit);
		*warnedP = true;
	}
	return true;
}

/* ------------------------------------------------------------------ *
 *  Write-side seal (call after a fresh plain write) + migration
 * ------------------------------------------------------------------ */
void CryptoSealRecord (DmOpenRef dbRef, UInt16 index)
{
	MemHandle	h = DmQueryRecord (dbRef, index);
	UInt32		plainSize;
	UInt8*		rec;
	MemHandle	tmpH;
	UInt8*		tmp;

	if (! h) return;
	plainSize = (UInt32) MemHandleSize (h);
	rec = (UInt8 *) MemHandleLock (h);

	/* MemHandleNew (movable/relocatable), NOT MemPtrNew (a fixed,
	 * non-relocatable chunk) -- this buffer needs to hold the WHOLE
	 * plaintext body (e.g. after a multi-part SD-card import), and
	 * MemPtrNew's non-movable allocation was failing outright for
	 * anything past roughly 20-30KB on a real/emulated device's
	 * fragmented dynamic heap (a movable handle can be satisfied by
	 * compacting existing allocations; a fixed chunk can't). This was
	 * the actual cause of "Import Failed" for a 33KB file -- every
	 * import part runs through this same seal step. */
	tmpH = MemHandleNew (plainSize + 1);
	if (tmpH == NULL)
		{
		MemHandleUnlock (h);
		return;
		}
	tmp = (UInt8 *) MemHandleLock (tmpH);

	if (EncEncryptionActive ())
		{
		UInt8	key[ENC_KEY_BYTES];
		if (EncGetKey (key, kCryptoEncTtl))
			{
			UInt32		uid = 0;
			/* This is the actual AES-CTR pass over the whole (possibly
			 * large, e.g. after a multi-part import) plaintext body -- the
			 * "Done" button's own hang, previously uncovered by any busy
			 * indicator (EditSaveCryptoPadData's "Encrypting..." only
			 * wrapped the OPTIONAL layer-2 EncryptRecord step, not this
			 * unconditional outer-wrap seal). */
			WinHandle	busy = CardBusyShow ("Encrypting...");
			DmRecordInfo (dbRef, index, NULL, &uid, NULL);
			tmp[0] = kCryptoFmtMarker | kCryptoFmtWrappedBit;
			MemMove (tmp + 1, rec, plainSize);
			MemHandleUnlock (h);				/* done with storage before EncCrypt */
			EncCrypt (key, tmp + 1, plainSize, uid, 0);
			MemSet (key, ENC_KEY_BYTES, 0);

			h = DmResizeRecord (dbRef, index, plainSize + 1);
			if (h != NULL)
				{
				rec = (UInt8 *) MemHandleLock (h);
				DmWrite (rec, 0, tmp, plainSize + 1);
				MemHandleUnlock (h);
				}
			MemHandleUnlock (tmpH);
			MemHandleFree (tmpH);
			CardBusyHide (busy);
			return;
			}
		/* Key unexpectedly unavailable even though a passcode is active --
		 * fall through and store unwrapped rather than lose the edit. */
		}

	/* No passcode active: still upgrade to the new leading-byte format
	 * (unwrapped marker) so future reads never have to re-guess old-vs-new
	 * format, but the record stays fully plaintext. */
	tmp[0] = kCryptoFmtMarker;
	MemMove (tmp + 1, rec, plainSize);
	MemHandleUnlock (h);

	h = DmResizeRecord (dbRef, index, plainSize + 1);
	if (h != NULL)
		{
		rec = (UInt8 *) MemHandleLock (h);
		DmWrite (rec, 0, tmp, plainSize + 1);
		MemHandleUnlock (h);
		}
	MemHandleUnlock (tmpH);
	MemHandleFree (tmpH);
}

/* Take an ALREADY-migrated (leading marker byte present) but still-plain
 * record and wrap it in place: encrypt the bytes after the existing marker
 * and flip its wrapped bit. Different from CryptoSealRecord, which assumes
 * NO marker byte exists yet and prepends a fresh one -- calling
 * CryptoSealRecord on an already-marked record would double-prepend and
 * corrupt the format. Used for the "a passcode was activated after this
 * record was already migrated-but-left-plain" case. No-op if the key isn't
 * available (shouldn't happen given the caller only calls this when
 * EncEncryptionActive(), but defensive). */
static void CryptoWrapInPlace (DmOpenRef dbRef, UInt16 index) AES_SECTION;
static void CryptoWrapInPlace (DmOpenRef dbRef, UInt16 index)
{
	MemHandle	h;
	UInt8		key[ENC_KEY_BYTES];
	UInt32		uid = 0;
	UInt32		size;
	UInt8*		p;

	if (! EncGetKey (key, kCryptoEncTtl)) return;

	h = DmGetRecord (dbRef, index);
	if (! h) { MemSet (key, ENC_KEY_BYTES, 0); return; }

	size = (UInt32) MemHandleSize (h) - 1;		/* content after the marker byte */
	p = (UInt8 *) MemHandleLock (h);
	DmRecordInfo (dbRef, index, NULL, &uid, NULL);

	{
	/* MemHandleNew, not MemPtrNew -- see CryptoSealRecord's comment on the
	 * same swap; this buffer can be just as large for a big migrated
	 * record. */
	MemHandle	tmpH = MemHandleNew (size);
	if (tmpH) {
		UInt8*	tmp = (UInt8 *) MemHandleLock (tmpH);
		MemMove (tmp, p + 1, size);
		EncCrypt (key, tmp, size, uid, 0);
		DmWrite (p, 1, tmp, size);
		MemHandleUnlock (tmpH);
		MemHandleFree (tmpH);
		}
	}
	MemSet (key, ENC_KEY_BYTES, 0);

	{
	UInt8	newMarker = kCryptoFmtMarker | kCryptoFmtWrappedBit;
	DmWrite (p, 0, &newMarker, 1);
	}

	MemHandleUnlock (h);
	DmReleaseRecord (dbRef, index, true);
}

void CryptoMigrateRecordFormat (DmOpenRef dbRef)
{
	UInt16	i, n = DmNumRecords (dbRef);
	Boolean	active = EncEncryptionActive ();

	for (i = 0; i < n; i++)
		{
		MemHandle	h = DmQueryRecord (dbRef, i);
		UInt8		marker;
		Boolean		hasMarker, isWrapped;
		if (! h) continue;
		marker = ((const UInt8 *) MemHandleLock (h))[0];
		MemHandleUnlock (h);

		hasMarker = (Boolean) ((marker & kCryptoFmtMarker) != 0);
		isWrapped = (Boolean) (hasMarker && (marker & kCryptoFmtWrappedBit));

		if (! hasMarker)
			/* Old format (pre-dates this feature): add the marker, and
			 * wrap immediately too if a passcode is already active. */
			CryptoSealRecord (dbRef, i);
		else if (! isWrapped && active)
			/* Already migrated, still plain, but a passcode is active NOW
			 * -- wrap in place (marker byte already exists). */
			CryptoWrapInPlace (dbRef, i);
		/* else: already wrapped, or plain-and-no-passcode -- nothing to do. */
		}
}

/* ------------------------------------------------------------------ *
 *  Edit-session helpers (see CryptoSec35.h) -- kept here, not in
 *  EditForm.c/main, which is critically tight on code space.
 * ------------------------------------------------------------------ */
static MemHandle	gCryptoEditH = NULL;
static Boolean		gCryptoEditMigrated = false;

Boolean CryptoEditRetrieveWrapped (DmOpenRef dbRef, UInt16 index, FieldPtr fieldP)
{
	RecordType	record;
	UInt16		nLength;
	char*		p;

	/* Free any leftover scratch handle from a previous edit session that
	 * ended without a real save (e.g. the "empty record deleted" path in
	 * EditForm.c's EditFormSaveData never calls EditSaveCryptoPadData). */
	if (gCryptoEditH) { MemHandleFree (gCryptoEditH); gCryptoEditH = NULL; }

	/* CryptoRecordMigrated (marker present, wrapped or not), NOT
	 * CryptoRecordWrapped -- see CryptoRecordMigrated's comment. Any
	 * migrated record's raw bytes are offset by the leading marker byte,
	 * whether or not it's actually encrypted, so both cases need the
	 * scratch-copy edit path below, not the caller's raw in-place one. */
	gCryptoEditMigrated = CryptoRecordMigrated (dbRef, index);
	if (! gCryptoEditMigrated) return false;

	/* A wrapped record's decrypt (CryptoGetRecord, full-body AES-CTR) can
	 * take a noticeable moment for a large memo -- cover it with the same
	 * busy indicator used everywhere else. Cheap marker-byte check, no
	 * decrypt of its own. */
	{
	Boolean		wrapped = CryptoRecordWrapped (dbRef, index);
	WinHandle	busy = wrapped ? CardBusyShow ("Opening...") : NULL;
	Boolean		ok = CryptoGetRecord (dbRef, index, &record);
	CardBusyHide (busy);
	if (! ok) return true;	/* nothing sane to do either way */
	}

	nLength = (UInt16) (SafeStrLen (record.textP) + 1);
	gCryptoEditH = MemHandleNew (nLength);
	if (! gCryptoEditH) return true;

	p = (char*) MemHandleLock (gCryptoEditH);
	StrCopy (p, record.textP);
	MemHandleUnlock (gCryptoEditH);

	/* Db35-suite-style Sec35 wrap: CryptoGetRecord (above) leaves whichever
	 * handle backs record.textP LOCKED (see its own comment -- needed so
	 * that pointer stays valid across the call boundary, fixing an earlier
	 * dangling-pointer bug). We've now safely copied everything out of it
	 * into gCryptoEditH, so release it explicitly right here instead of
	 * waiting for the NEXT CryptoGetRecord call to clean it up. Without
	 * this, the record's own storage handle stays locked for the entire
	 * edit session -- and if the user deletes the record from the Edit
	 * form (DetailsFormDeleteCurrentRecord, which never calls
	 * CryptoGetRecord itself) before anything else happens to release it,
	 * DmRemoveRecord/DmDeleteRecord try to free a chunk that's still
	 * locked -- a real "MemoryMgr.c, Free handle" SysFatalAlert, hit
	 * during testing on freshly-created, non-corrupted records (not just
	 * ones with stale pre-fix data). */
	if (gCryptoLastLockedH) { MemHandleUnlock (gCryptoLastLockedH); gCryptoLastLockedH = NULL; }

	FldSetText (fieldP, gCryptoEditH, 0, nLength);
	return true;
}

Boolean CryptoEditSaveWrapped (DmOpenRef dbRef, UInt16 index, Char* textP)
{
	MemHandle	handle;
	RecordType	newRecord;
	Char*		p;

	if (! gCryptoEditMigrated) return false;

	/* Db35-suite-style Sec35 wrap: deliberately IGNORES the `textP`
	 * parameter and reads straight from gCryptoEditH (locked here) --
	 * `textP` was captured by EditForm.c's EditSaveData via
	 * FldGetTextPtr BEFORE FldCompactText runs, and FldCompactText can
	 * MemHandleResize (and relocate) THIS EXACT handle, leaving `textP`
	 * dangling/stale by the time this function is called. The old raw
	 * in-place path only ever used `textP` for StrLen() (tolerant of a
	 * stale-but-not-yet-reused pointer in practice); this path uses the
	 * CONTENT directly to rebuild the record, which is not tolerant --
	 * this was the "content disappears, empty record after Done" bug hit
	 * during testing (no Sec35 passcode -> every record is
	 * marked-but-plain -> CryptoRecordMigrated is true for all of them ->
	 * this exact path runs on every single save). gCryptoEditH itself is
	 * unaffected by FldSetTextHandle(fieldP, 0) (that only detaches the
	 * FIELD from the handle, doesn't touch the handle's own content), so
	 * locking it fresh here is always safe and always current -- the
	 * Field Manager has been editing it directly in place since
	 * CryptoEditRetrieveWrapped handed it over via FldSetText. */
	if (! gCryptoEditH) return true;	/* nothing sane to save */
	p = (Char*) MemHandleLock (gCryptoEditH);

	newRecord.flag      = recDecrypted;
	newRecord.passwdLen = 0;
	newRecord.textLen   = (UInt16) SafeStrLen (p);
	newRecord.passwordP = "";
	newRecord.textP     = p;
	MemSet (newRecord.pwIV, sizeof (newRecord.pwIV), 0);
	MemSet (newRecord.bodyIV, sizeof (newRecord.bodyIV), 0);
	MemSet (newRecord.kdfSalt, sizeof (newRecord.kdfSalt), 0);
	newRecord.kdfRounds = 0;

	handle = DmGetRecord (dbRef, index);
	PackRecord (&newRecord, handle);
	DmReleaseRecord (dbRef, index, true);

	MemHandleUnlock (gCryptoEditH);
	MemHandleFree (gCryptoEditH);
	gCryptoEditH = NULL;

	/* Deliberately NOT sealing here -- the caller (EditSaveCryptoPadData)
	 * still needs to run its own layer-2 EncryptRecord step (if the user
	 * chose "Encrypt Memo") against these freshly-written PLAIN bytes
	 * before the outer Sec35 wrap goes on; sealing here first would leave
	 * EncryptRecord trying to read ciphertext as if it were the plain
	 * inner format. The caller calls CryptoSealRecord itself, once, after
	 * EncryptRecord -- same as the not-wrapped path already does. */
	return true;
}

Boolean CryptoCopyTextToHandle (DmOpenRef dbRef, UInt16 index, MemHandle* outH)
{
	RecordType	record;
	UInt16		len;
	char*		p;

	if (! CryptoGetRecord (dbRef, index, &record)) return false;

	len = (UInt16) (SafeStrLen (record.textP) + 1);
	*outH = MemHandleNew (len);
	if (! *outH) return false;

	p = (char*) MemHandleLock (*outH);
	StrCopy (p, record.textP);
	MemHandleUnlock (*outH);
	return true;
}

/* ------------------------------------------------------------------ *
 *  Launch-time maintenance (see CryptoSec35.h)
 * ------------------------------------------------------------------ */
void CryptoLaunchMaintenance (void)
{
	UInt16	n = 0;

	CryptoMigrateRecordFormat (dbCryptoPadRef);

	/* Db35-suite-style Sec35 wrap: a MemoPad record marked dmRecAttrDelete
	 * (deleted from the ROM Memo Pad app -- with or without "save archive
	 * copy", which additionally sets dmRecAttrDirty) stays PHYSICALLY
	 * present with fully readable content until the next HotSync purges
	 * it -- that's exactly why DmQueryRecord/DmNumRecords below can see it
	 * at all. But it's no longer "there" from the user's point of view;
	 * counting or importing it would resurrect a note the user just
	 * deleted. Skip any record with dmRecAttrDelete set, both when
	 * counting for the migrate prompt and when actually importing --
	 * caught via a real repro: delete a memo in Memo Pad with "save
	 * backup" checked, then CryptoNote kept re-detecting and re-offering
	 * to import it every launch.
	 *
	 * Also skip any record still in the Unfiled category -- an unfiled
	 * memo has no category to carry over into CryptoPad and migrating it
	 * silently would be surprising (nothing distinguishes "the user hasn't
	 * filed this yet" from "this doesn't need filing"); leave it in Memo
	 * Pad, same as a deleted-pending-sync record, so the user can file it
	 * first if they want it migrated. */
	{
	UInt16	total = DmNumRecords (dbMemoPadRef);
	UInt16	i;
	for (i = 0; i < total; i++)
		{
		UInt16	attr;
		if (DmRecordInfo (dbMemoPadRef, i, &attr, NULL, NULL) != errNone) continue;
		if (attr & dmRecAttrDelete) continue;
		if ((attr & dmRecAttrCategoryMask) == dmUnfiledCategory) continue;
		n++;
		}
	}
	if (n == 0) return;

	{
	Char	countStr[6];
	StrIToA (countStr, n);
	if (FrmCustomAlert (altMigrateMemoPadAlert, countStr, "", "") != 0)
		return;		/* Cancel -- leave MemoPad records untouched, re-prompt next launch */
	}

	{
	UInt16	i = 0;
	while (i < DmNumRecords (dbMemoPadRef))
		{
		MemHandle	srcH;
		const char*	srcText;
		UInt16		attr;
		UInt16		newIndex = dmMaxRecordIndex;
		MemHandle	newH;
		RecordType	newRecord;

		if (DmRecordInfo (dbMemoPadRef, i, &attr, NULL, NULL) != errNone) { i++; continue; }
		if (attr & dmRecAttrDelete) { i++; continue; }		/* pending-sync delete -- leave it, move past it */
		if ((attr & dmRecAttrCategoryMask) == dmUnfiledCategory) { i++; continue; }	/* leave unfiled memos in Memo Pad */

		srcH = DmQueryRecord (dbMemoPadRef, i);
		if (! srcH) { i++; continue; }
		srcText = (const char*) MemHandleLock (srcH);

		newH = DmNewRecord (dbCryptoPadRef, &newIndex, sizeof (PackedRecordType));
		if (! newH) { MemHandleUnlock (srcH); i++; continue; }

		newRecord.flag      = recDecrypted;
		newRecord.passwdLen = 0;
		newRecord.textLen   = (UInt16) SafeStrLen (srcText);
		newRecord.passwordP = "";
		newRecord.textP     = (char*) srcText;
		MemSet (newRecord.pwIV, sizeof (newRecord.pwIV), 0);
		MemSet (newRecord.bodyIV, sizeof (newRecord.bodyIV), 0);
		MemSet (newRecord.kdfSalt, sizeof (newRecord.kdfSalt), 0);
		newRecord.kdfRounds = 0;

		PackRecord (&newRecord, newH);
		MemHandleUnlock (srcH);

		DmSetRecordInfo (dbCryptoPadRef, newIndex, &attr, NULL);
		DmReleaseRecord (dbCryptoPadRef, newIndex, true);

		CryptoSealRecord (dbCryptoPadRef, newIndex);

		DmRemoveRecord (dbMemoPadRef, i);
		/* don't advance i -- the next record shifted down into this slot */
		}
	}
}

/* ------------------------------------------------------------------ *
 *  Single-record encrypt/decrypt busy indicator: standardized on
 *  CardBusyShow/CardBusyHide (CardCrypt.c), the SAME lightweight
 *  WinSaveBits-based indicator the SD-card encrypt/decrypt paths always
 *  used. An earlier version of this (a FORM-based modal with an animated
 *  progress bar, frmCryptoBusy) was replaced after user feedback that the
 *  card version's calmer, less distracting style should be used
 *  everywhere instead -- see EditForm.c's/PasswordForm.c's/MainForm.c's
 *  own CardBusyShow/CardBusyHide call sites (search for "Encrypting..."/
 *  "Decrypting..."). No CryptoSec35-specific busy-dialog code needed
 *  anymore; this comment is left as a pointer for anyone looking for it.
 * ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ *
 *  New-record creation (see CryptoSec35.h) -- moved out of Database.c's
 *  CreateRecord, which lives in main and overflowed once this grew to
 *  include CryptoSealRecord + the PackRecord construction.
 * ------------------------------------------------------------------ */
Boolean CryptoCreateRecord (DmOpenRef dbRef, UInt16* indexOut, UInt16 category)
{
	MemHandle	handle;
	RecordType	newRecord;
	UInt16		recAttributes = dmUnfiledCategory;

	*indexOut = dmMaxRecordIndex;
	handle = DmNewRecord (dbRef, indexOut, sizeof (PackedRecordType));
	if (! handle) return false;

	newRecord.flag      = recDecrypted;
	newRecord.passwdLen = 0;
	newRecord.textLen   = 0;
	newRecord.passwordP = "";
	newRecord.textP     = "";
	MemSet (newRecord.pwIV, sizeof (newRecord.pwIV), 0);
	MemSet (newRecord.bodyIV, sizeof (newRecord.bodyIV), 0);
	MemSet (newRecord.kdfSalt, sizeof (newRecord.kdfSalt), 0);
	newRecord.kdfRounds = 0;

	PackRecord (&newRecord, handle);

	DmRecordInfo (dbRef, *indexOut, &recAttributes, NULL, NULL);
	recAttributes &= ~dmRecAttrCategoryMask;
	if (category != dmAllCategories)
		recAttributes |= category;
	DmSetRecordInfo (dbRef, *indexOut, &recAttributes, NULL);

	DmReleaseRecord (dbRef, *indexOut, true);

	CryptoSealRecord (dbRef, *indexOut);
	return true;
}

/* ------------------------------------------------------------------ *
 *  Sort comparator logic (see CryptoSec35.h + Database.c's thin
 *  CompareCryptoPadRecords, which must stay in main).
 * ------------------------------------------------------------------ */
Int16 CryptoCompareRecords (const UInt8* raw1, const UInt8* raw2, Int16 sortOrder)
{
	RecordType record1, record2;
	Boolean hasMarker1 = (Boolean) ((raw1[0] & kCryptoFmtMarker) != 0);
	Boolean hasMarker2 = (Boolean) ((raw2[0] & kCryptoFmtMarker) != 0);
	Boolean wrapped1 = (Boolean) (hasMarker1 && (raw1[0] & kCryptoFmtWrappedBit));
	Boolean wrapped2 = (Boolean) (hasMarker2 && (raw2[0] & kCryptoFmtWrappedBit));

	/* No DmOpenRef/index/uid available here (the real DmComparF signature
	 * carries a SortRecordInfoPtr per record, which DOES include a
	 * uniqueID, but reconstructing the Sec35 nonce from that
	 * untested/unverified would risk silently decrypting to garbage -- a
	 * worse failure than an imperfect sort order). ONLY a truly wrapped
	 * record's bytes past the marker are ciphertext -- a marked-but-plain
	 * record (marker set, wrapped bit clear -- what virtually EVERY
	 * CryptoPad record looks like once CryptoMigrateRecordFormat has run
	 * and no Sec35 passcode is active) is real plaintext 1 byte later and
	 * unpacks/sorts correctly same as a legacy no-marker record; only
	 * genuinely wrapped records fall back. KNOWN LIMITATION, flagged for
	 * follow-up: proper nonce-aware sorting of wrapped records needs the
	 * full DmComparF signature threaded through the ROM sort call site. */
	if (wrapped1 || wrapped2) {
		/* Ciphertext bytes have no reason to contain a NUL within the
		 * record's actual allocated size -- StrCompare's unbounded
		 * NUL-terminator scan used to walk arbitrarily far past the
		 * record's real size into unrelated heap memory, a real crash hit
		 * during testing (right after an import that sealed new records
		 * while a passcode was active, then something sorted the list).
		 * Compare a small FIXED number of leading bytes instead -- safely
		 * within bounds of even the smallest possible record
		 * (sizeof(PackedRecordType) + 1 marker byte is always > 32). */
		UInt16 i;
		for (i = 0; i < 32; i++) {
			UInt8 b1 = raw1[i];
			UInt8 b2 = raw2[i];
			if (b1 != b2) return (Int16) ((Int16) b1 - (Int16) b2);
		}
		return 0;
	}

	UnpackRecord (&record1, (PackedRecordPtr) (hasMarker1 ? raw1 + 1 : raw1));
	UnpackRecord (&record2, (PackedRecordPtr) (hasMarker2 ? raw2 + 1 : raw2));

	if (sortOrder == soAlphabetic)
		return StrCaselessCompare (record1.textP, record2.textP);
	return 0;
}

/* ------------------------------------------------------------------ *
 *  Phase 5b: Import from Card (see CryptoSec35.h)
 * ------------------------------------------------------------------ */

/* Best split point for a chunk starting at `start`: the nearest newline at
 * or before start+kCryptoImportMaxPart (readable break), or a hard cut at
 * exactly start+kCryptoImportMaxPart if no newline appears in range (one
 * very long line). */
static UInt32 CryptoImportFindSplit (const char* text, UInt32 start, UInt32 total) AES_SECTION;
static UInt32 CryptoImportFindSplit (const char* text, UInt32 start, UInt32 total)
{
	UInt32 limit = start + kCryptoImportMaxPart;
	UInt32 i;
	if (limit >= total) return total;
	for (i = limit; i > start; i--)
		if (text[i - 1] == '\n') return i;
	return limit;
}

UInt16 CryptoImportPartCount (UInt32 textLen)
{
	UInt16 parts = (UInt16) ((textLen + kCryptoImportMaxPart - 1) / kCryptoImportMaxPart);
	return parts ? parts : 1;
}

/* Shared by CryptoImportText (whole-buffer case) and CryptoImportCardFile
 * (streaming case): builds ONE part's DB record from a chunk of plain
 * text already in memory (bounded to <=kCryptoImportMaxPart by both
 * callers), injects the title line, seals it. Always injects the card
 * file's own name as an explicit title line (even for a single-part
 * import) -- MemoPad/CryptoNote both use the first line as the displayed
 * "memo name", and a file whose own first line is blank or missing would
 * otherwise show up as an apparently-empty list row, easily mistaken for
 * the list not having loaded. Multi-part gets "<base> 00N" (per the
 * user's own spec); single-part gets just "<base>", no suffix. Returns
 * false (nothing created) only on an allocation/DmNewRecord failure. */
static Boolean CryptoImportOnePart (const char* chunk, UInt32 chunkLen, const char* baseName,
                                      UInt16 partNum, Boolean multiPart, UInt16 category) AES_SECTION;
static Boolean CryptoImportOnePart (const char* chunk, UInt32 chunkLen, const char* baseName,
                                      UInt16 partNum, Boolean multiPart, UInt16 category)
{
	UInt16		index;
	MemHandle	handle;
	RecordType	newRecord;
	MemHandle	bodyH;
	Char*		body;
	UInt32		bodyTotal;

	{
	Char	title[cardMaxName + 8];
	UInt32	titleLen;
	StrNCopy (title, baseName, sizeof (title) - 6);
	title[sizeof (title) - 6] = 0;
	titleLen = StrLen (title);
	if (multiPart)
		{
		title[titleLen++] = ' ';
		title[titleLen++] = (Char) ('0' + (partNum / 100) % 10);
		title[titleLen++] = (Char) ('0' + (partNum / 10) % 10);
		title[titleLen++] = (Char) ('0' + partNum % 10);
		title[titleLen] = 0;
		}

	bodyTotal = titleLen + 1 + chunkLen;
	/* MemHandleNew, not MemPtrNew -- see CryptoSealRecord's comment on the
	 * same swap. This is the exact buffer that was failing to allocate
	 * for a 33KB single-part import ("Import Failed" with no memo created
	 * at all) before that fix. */
	bodyH = MemHandleNew (bodyTotal + 1);
	if (! bodyH) return false;
	body = (Char*) MemHandleLock (bodyH);
	MemMove (body, title, titleLen);
	body[titleLen] = '\n';
	MemMove (body + titleLen + 1, chunk, chunkLen);
	body[bodyTotal] = 0;
	}

	index = dmMaxRecordIndex;
	handle = DmNewRecord (dbCryptoPadRef, &index, sizeof (PackedRecordType));
	if (! handle) { MemHandleUnlock (bodyH); MemHandleFree (bodyH); return false; }

	newRecord.flag      = recDecrypted;
	newRecord.passwdLen = 0;
	newRecord.textLen   = (UInt16) bodyTotal;	/* already known -- no need to re-scan */
	newRecord.passwordP = "";
	newRecord.textP     = body;
	MemSet (newRecord.pwIV, sizeof (newRecord.pwIV), 0);
	MemSet (newRecord.bodyIV, sizeof (newRecord.bodyIV), 0);
	MemSet (newRecord.kdfSalt, sizeof (newRecord.kdfSalt), 0);
	newRecord.kdfRounds = 0;

	PackRecord (&newRecord, handle);
	MemHandleUnlock (bodyH);
	MemHandleFree (bodyH);

	{
	UInt16 attr = dmUnfiledCategory;
	DmRecordInfo (dbCryptoPadRef, index, &attr, NULL, NULL);
	attr &= ~dmRecAttrCategoryMask;
	if (category != dmAllCategories) attr |= category;
	DmSetRecordInfo (dbCryptoPadRef, index, &attr, NULL);
	}

	DmReleaseRecord (dbCryptoPadRef, index, true);

	/* Db35-suite-style Sec35 wrap: same as every other fresh write --
	 * stays plain (marked) if no passcode is active, wraps if one is. */
	CryptoSealRecord (dbCryptoPadRef, index);
	return true;
}

UInt16 CryptoImportText (const char* text, const char* baseName, UInt16 category)
{
	/* SafeStrLen, NOT StrLen -- StrLen returns Int16 (ROM trap, max 32767)
	 * and this app allows imports up to ~60000 bytes; StrLen's result
	 * sign-extends to a huge garbage UInt32 past 32767, corrupting every
	 * downstream size computation in this function. THE actual cause of
	 * "Import Failed" on anything over ~32KB (confirmed: 33KB file failed
	 * even with plenty of free/contiguous heap memory available). */
	UInt32	total = SafeStrLen (text);
	UInt16	parts = CryptoImportPartCount (total);
	Boolean	multiPart = (parts > 1);
	UInt32	start = 0;
	UInt16	partNum = 0;
	UInt16	created = 0;

	/* Loop on ACTUAL progress (start < total), not the estimated `parts`
	 * count -- CryptoImportFindSplit can legitimately land a part short
	 * of the full kCryptoImportMaxPart cap (preferring a nearby newline),
	 * which means MORE parts than the byte-count estimate can genuinely
	 * be needed. Looping on the estimate instead would silently drop
	 * whatever trailing content didn't fit in the estimated part count --
	 * a real latent bug, just never hit by anything tested so far (the
	 * only real multi-part-eligible test was blocked on a separate bug
	 * until CryptoImportCardFile below). */
	while (start < total)
		{
		UInt32 end = multiPart ? CryptoImportFindSplit (text, start, total) : total;
		UInt32 chunkLen = end - start;

		partNum++;
		if (! CryptoImportOnePart (text + start, chunkLen, baseName, partNum, multiPart, category))
			break;
		created++;
		start = end;
		}

	return created;
}

/* Streaming counterpart to CryptoImportText, for a card file too large to
 * read into one buffer (CardReadFile's single MemHandleNew failing
 * outright with memErrNotEnoughSpace on a real device for a ~98KB file --
 * confirmed, not theoretical). Reads and imports ONE PART AT A TIME
 * directly from the VFS file, each part bounded to <=kCryptoImportMaxPart
 * bytes -- the same size class already proven to work (33KB succeeded
 * once the StrLen bug was fixed) -- so peak memory usage never exceeds
 * one part's worth of content, regardless of the source file's total
 * size. Does NOT handle an encrypted source file (CardCryptDecrypt is a
 * separate, still-whole-buffer operation) -- caller must only use this
 * for a file already confirmed plain. */
UInt16 CryptoImportCardFile (UInt16 volRef, const char* path, const char* baseName, UInt16 category)
{
	FileRef  fileRef;
	UInt32   totalSize = 0;
	UInt32   filePos = 0;
	UInt16   parts;
	Boolean  multiPart;
	UInt16   partNum = 0;
	UInt16   created = 0;

	if (VFSFileOpen (volRef, path, vfsModeRead, &fileRef) != errNone)
		return 0;
	VFSFileSize (fileRef, &totalSize);
	parts = CryptoImportPartCount (totalSize);
	multiPart = (parts > 1);

	while (filePos < totalSize)
		{
		UInt32     want = totalSize - filePos;
		MemHandle  chunkH;
		UInt8*     buf;
		UInt32     gotTotal = 0;

		if (want > kCryptoImportMaxPart) want = kCryptoImportMaxPart;

		chunkH = MemHandleNew (want);
		if (! chunkH) break;
		buf = (UInt8 *) MemHandleLock (chunkH);

		VFSFileSeek (fileRef, vfsOriginBeginning, filePos);
		/* Same read-loop discipline as CardIO.c's CardReadFile -- some VFS
		 * drivers cap how much a single VFSFileRead call actually returns. */
		while (gotTotal < want)
			{
			UInt32 nread = 0;
			Err rerr = VFSFileRead (fileRef, want - gotTotal, buf + gotTotal, &nread);
			gotTotal += nread;
			if (nread == 0) break;
			if (rerr != errNone && rerr != vfsErrFileEOF) break;
			}

		{
		UInt32  useLen = gotTotal;
		Boolean isLastPart = (filePos + gotTotal >= totalSize);

		/* Prefer a newline near the end of what we just read over a hard
		 * cut, same readability preference as CryptoImportFindSplit --
		 * just searching a buffer we already hold instead of a full
		 * in-memory copy of the whole file. */
		if (! isLastPart && multiPart)
			{
			UInt32 i;
			for (i = gotTotal; i > 0; i--)
				if (buf[i - 1] == '\n') { useLen = i; break; }
			}

		partNum++;
		if (useLen > 0 && CryptoImportOnePart ((const char *) buf, useLen, baseName, partNum, multiPart, category))
			created++;

		MemHandleUnlock (chunkH);
		MemHandleFree (chunkH);

		if (useLen == 0) break;	/* no progress -- avoid spinning forever */
		filePos += useLen;
		}
		}

	VFSFileClose (fileRef);
	return created;
}

/* Sentinel return from CryptoImportEncryptedCardFile for "the password
 * didn't verify" -- distinct from a genuine 0-created import failure
 * (file corrupt, allocation failure, etc.) so the caller can show
 * altCardBadPassword instead of a generic "Import Failed". No real
 * import can ever legitimately produce this many parts. */
#define kCryptoImportBadPassword 0xFFFFU

/* Streaming decrypt-and-import for an ENCRYPTED card file too large to
 * read/decrypt into one buffer (CardCryptDecrypt itself needs the whole
 * ciphertext in memory at once, same class of memErrNotEnoughSpace
 * failure CryptoImportCardFile above fixes for the plain case). Verifies
 * the password against the small header-embedded verifier first (no
 * body read needed for that), then reads+decrypts+imports the BODY one
 * part at a time directly from VFS. Returns kCryptoImportBadPassword if
 * the password didn't verify (checked BEFORE any part is written).
 *
 * AES-CTR is a stream cipher: the keystream for byte i never depends on
 * bytes after i, so an arbitrary sub-range of the body can be decrypted
 * independently, as long as the decrypt starts at a 16-byte-aligned
 * offset from the body's own start (AesCtrAdvanceIV computes the correct
 * starting counter for that offset). Since a part's actual boundary
 * (found by the same newline-preferring search CryptoImportCardFile
 * uses) can land anywhere, each read/decrypt rounds its OWN start down
 * to the nearest 16-byte boundary and discards the few leading bytes
 * that decrypts before the real part start -- standard random-access CTR
 * technique, not a shortcut. */
UInt16 CryptoImportEncryptedCardFile (UInt16 volRef, const char* path, const char* password,
                                        const char* baseName, UInt16 category)
{
	FileRef  fileRef;
	UInt8    pwIV[AES_IV_SIZE], bodyIV[AES_IV_SIZE], salt[AES_SALT_SIZE];
	UInt16   pwLen, rounds;
	UInt32   fileTotalSize = 0;
	UInt32   bodyOffset, bodyLen;
	UInt32   bodyPos = 0;
	UInt16   parts;
	Boolean  multiPart;
	UInt16   partNum = 0;
	UInt16   created = 0;

	if (VFSFileOpen (volRef, path, vfsModeRead, &fileRef) != errNone)
		return 0;
	VFSFileSize (fileRef, &fileTotalSize);

	/* Fixed-size header only -- IVs, salt, KDF rounds, password-verifier
	 * length. Same layout CardCryptDecrypt reads, just via VFS directly
	 * instead of from an already-in-memory whole-file buffer. */
	{
	UInt8  hdr[cardCryptHdrLen];
	UInt32 got = 0;
	while (got < cardCryptHdrLen)
		{
		UInt32 nread = 0;
		Err rerr = VFSFileRead (fileRef, cardCryptHdrLen - got, hdr + got, &nread);
		got += nread;
		if (nread == 0) break;
		if (rerr != errNone && rerr != vfsErrFileEOF) break;
		}
	if (got < cardCryptHdrLen || MemCmp (hdr, cardCryptMagic, cardCryptMagicLen) != 0)
		{ VFSFileClose (fileRef); return 0; }

	MemMove (pwIV,   hdr + 7,  AES_IV_SIZE);
	MemMove (bodyIV, hdr + 23, AES_IV_SIZE);
	MemMove (salt,   hdr + 39, AES_SALT_SIZE);
	MemMove (&rounds, hdr + 55, sizeof (rounds));
	MemMove (&pwLen,  hdr + 57, sizeof (pwLen));
	}

	if ((UInt32) cardCryptHdrLen + pwLen > fileTotalSize)
		{ VFSFileClose (fileRef); return 0; }

	/* Password verifier: small (pwLen bytes), decrypt and compare -- same
	 * check CardCryptDecrypt does, just reading only the verifier field
	 * instead of the whole file. */
	{
	Char*   verifier = (Char*) MemPtrNew (pwLen + 1);
	UInt32  got = 0;
	Boolean ok;
	if (! verifier) { VFSFileClose (fileRef); return 0; }
	while (got < pwLen)
		{
		UInt32 nread = 0;
		Err rerr = VFSFileRead (fileRef, pwLen - got, verifier + got, &nread);
		got += nread;
		if (nread == 0) break;
		if (rerr != errNone && rerr != vfsErrFileEOF) break;
		}
	AESCryptEx (verifier, verifier, (Char*) password, globalPasswordP, pwIV, salt, rounds, pwLen, AES_DECRYPT);
	ok = (StrLen (password) == pwLen && StrNCompare (verifier, password, pwLen) == 0);
	MemPtrFree (verifier);
	if (! ok) { VFSFileClose (fileRef); return kCryptoImportBadPassword; }
	}

	bodyOffset = (UInt32) cardCryptHdrLen + pwLen;
	bodyLen = fileTotalSize - bodyOffset;
	parts = CryptoImportPartCount (bodyLen);
	multiPart = (parts > 1);

	while (bodyPos < bodyLen)
		{
		UInt32     alignedStart = (bodyPos / 16) * 16;
		UInt32     discard = bodyPos - alignedStart;
		UInt32     want = bodyLen - alignedStart;
		MemHandle  chunkH;
		UInt8*     buf;
		UInt32     gotTotal = 0;

		/* +16 headroom covers the up-to-15 discarded leading bytes from
		 * rounding the start down to a block boundary, so the window
		 * still yields a full kCryptoImportMaxPart's worth of usable
		 * plaintext even after discarding. AESCryptEx's own nLength is
		 * UInt16, so this must never exceed 65535 -- kCryptoImportMaxPart
		 * (60000) + 16 comfortably doesn't. */
		if (want > kCryptoImportMaxPart + 16) want = kCryptoImportMaxPart + 16;

		/* +1 for AESCryptEx's own trailing NUL write (output[nLength]),
		 * same as CardCryptEncrypt/CardCryptDecrypt's own buffers. */
		chunkH = MemHandleNew (want + 1);
		if (! chunkH) break;
		buf = (UInt8 *) MemHandleLock (chunkH);

		VFSFileSeek (fileRef, vfsOriginBeginning, bodyOffset + alignedStart);
		while (gotTotal < want)
			{
			UInt32 nread = 0;
			Err rerr = VFSFileRead (fileRef, want - gotTotal, buf + gotTotal, &nread);
			gotTotal += nread;
			if (nread == 0) break;
			if (rerr != errNone && rerr != vfsErrFileEOF) break;
			}

		if (gotTotal > 0)
			{
			UInt8 ivCopy[AES_IV_SIZE];
			MemMove (ivCopy, bodyIV, AES_IV_SIZE);
			AesCtrAdvanceIV (ivCopy, alignedStart / 16);
			AESCryptEx ((Char*) buf, (Char*) buf, (Char*) password, globalPasswordP,
			            ivCopy, salt, rounds, (UInt16) gotTotal, AES_DECRYPT);
			}

		{
		UInt8*  plain = buf + discard;
		UInt32  plainLen = (gotTotal > discard) ? gotTotal - discard : 0;
		UInt32  useLen = plainLen;
		Boolean isLastPart = (alignedStart + gotTotal >= bodyLen);

		if (! isLastPart && multiPart)
			{
			UInt32 i;
			for (i = plainLen; i > 0; i--)
				if (plain[i - 1] == '\n') { useLen = i; break; }
			}

		partNum++;
		if (useLen > 0 && CryptoImportOnePart ((const char *) plain, useLen, baseName, partNum, multiPart, category))
			created++;

		MemHandleUnlock (chunkH);
		MemHandleFree (chunkH);

		if (useLen == 0) break;	/* no progress -- avoid spinning forever */
		bodyPos += useLen;
		}
		}

	VFSFileClose (fileRef);
	MemSet (bodyIV, AES_IV_SIZE, 0);
	return created;
}

/* Confirm/import/report-result wrapper around CryptoImportEncryptedCardFile,
 * mirroring CryptoImportFromCardFile's UX for the encrypted case. Caller
 * (MainFormOpenCardFile) has already gated on Sec35 lock + prompted for
 * the card file's OWN password before calling this. */
void CryptoImportFromEncryptedCardFile (UInt16 volRef, const char* path, const char* password,
                                          const char* baseName, UInt16 category)
{
	/* Part-count estimate from ciphertext size -- close enough for the
	 * confirm dialog (off by the small fixed header + verifier overhead,
	 * negligible against the 60000-byte part threshold). */
	UInt32 fileSize = 0;
	UInt16 parts;
	Char   countStr[6];
	UInt16 created;

	{
	FileRef fileRef;
	if (VFSFileOpen (volRef, path, vfsModeRead, &fileRef) == errNone)
		{
		VFSFileSize (fileRef, &fileSize);
		VFSFileClose (fileRef);
		}
	}

	parts = CryptoImportPartCount (fileSize);
	StrIToA (countStr, parts);
	if (FrmCustomAlert (altCardTooLarge, countStr, "", "") != 0)
		return;			/* Cancel */

	{
	WinHandle busy = CardBusyShow ("Importing...");
	created = CryptoImportEncryptedCardFile (volRef, path, password, baseName, category);
	CardBusyHide (busy);
	}

	if (created == kCryptoImportBadPassword)
		{
		FrmAlert (altCardBadPassword);
		return;
		}

	StrIToA (countStr, created);
	if (created == parts)
		FrmCustomAlert (altCardImportComplete, countStr, "", "");
	else
		FrmCustomAlert (altCardImportFailed, countStr, "", "");
}

/* Confirm/import/report-result for a card file too large to open in the
 * editor field (>60000 bytes) -- offered as an alternative to the plain
 * "too large" refusal, since CryptoImportText never touches the Field
 * Manager and doesn't share that size ceiling. Takes ownership of plainH
 * (frees it on every exit path: cancel, resize failure, or completion).
 * `plainH` must NOT be NUL-terminated yet -- this function resizes it by
 * one byte and does that itself. */
void CryptoImportFromCardBuffer (MemHandle plainH, UInt32 plainLen, const char* baseName, UInt16 category)
{
	UInt16 parts = CryptoImportPartCount (plainLen);
	Char   countStr[6];
	Char  *plainText;
	UInt16 created;

	StrIToA (countStr, parts);
	if (FrmCustomAlert (altCardTooLarge, countStr, "", "") != 0)
		{
		MemHandleFree (plainH);
		return;			/* Cancel */
		}

	/* CardReadFile/CardCryptDecrypt buffers are exactly plainLen bytes, no
	 * NUL guaranteed -- CryptoImportText needs a NUL-terminated string. */
	if (MemHandleResize (plainH, plainLen + 1) != errNone)
		{
		MemHandleFree (plainH);
		return;
		}
	plainText = (Char *) MemHandleLock (plainH);
	plainText[plainLen] = 0;

	{
	WinHandle busy = CardBusyShow ("Importing...");
	created = CryptoImportText (plainText, baseName, category);
	CardBusyHide (busy);
	}

	MemHandleUnlock (plainH);
	MemHandleFree (plainH);

	StrIToA (countStr, created);
	if (created == parts)
		FrmCustomAlert (altCardImportComplete, countStr, "", "");
	else
		FrmCustomAlert (altCardImportFailed, countStr, "", "");
}

/* Streaming counterpart to CryptoImportFromCardBuffer, for a PLAIN card
 * file too large to read into one buffer at all -- confirm/import/
 * report-result using CryptoImportCardFile (reads and imports one part
 * at a time directly from the file, never the whole file in memory) --
 * used when the caller couldn't even attempt a whole-file CardReadFile
 * (that call itself failing with memErrNotEnoughSpace on a real device
 * for a ~98KB file is what this exists to avoid entirely). */
void CryptoImportFromCardFile (UInt16 volRef, const char* path, const char* baseName, UInt16 category)
{
	UInt32 totalSize = 0;
	UInt16 parts;
	Char   countStr[6];
	UInt16 created;

	{
	FileRef fileRef;
	if (VFSFileOpen (volRef, path, vfsModeRead, &fileRef) == errNone)
		{
		VFSFileSize (fileRef, &totalSize);
		VFSFileClose (fileRef);
		}
	}

	parts = CryptoImportPartCount (totalSize);
	StrIToA (countStr, parts);
	if (FrmCustomAlert (altCardTooLarge, countStr, "", "") != 0)
		return;			/* Cancel */

	{
	WinHandle busy = CardBusyShow ("Importing...");
	created = CryptoImportCardFile (volRef, path, baseName, category);
	CardBusyHide (busy);
	}

	StrIToA (countStr, created);
	if (created == parts)
		FrmCustomAlert (altCardImportComplete, countStr, "", "");
	else
		FrmCustomAlert (altCardImportFailed, countStr, "", "");
}
