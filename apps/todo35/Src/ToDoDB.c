/******************************************************************************
 *
 * Copyright (c) 1995-1999 Palm Computing, Inc. or its subsidiaries.
 * All rights reserved.
 *
 * File: ToDoDB.c
 *
 * Description:
 *		To Do Manager routines
 *
 * History:
 *   	1/20/95  rsf - Created
 *		10/4/99	jmp - Added ToDoGetDatabase() routine.
 *		10/4/99	rbb - Moved SetDBBackupBit from ToDo.c (used by
 *							ToDoGetDatabase) and renamed as ToDoSetDBBackupBit
 *
 *****************************************************************************/

// Set this to get to private database defines
#define __TODOMGR_PRIVATE__

#include <PalmOS.h>

#include "ToDo.h"
#include "ToDoDB.h"
#include "enccore.h"			/* Sec35 shared-key AES-128-CTR (EncGetKey/EncCrypt) */

// Export error checking routines
void ECToDoDBValidate(DmOpenRef dbP);

/* ---- Td35 at-rest encryption of description + note --------------------- *
 * Sensitive text = the description + note strings; dueDate/priority/category stay
 * clear (sort/list/filter read them). Because AES-CTR output can contain 0x00 - which
 * would break the record's inline NUL-terminated strings - an ENCRYPTED record uses a
 * length-prefixed layout for that region instead of the two \0-terminated strings:
 *   plaintext:  [dueDate][priority][description\0][note\0]
 *   encrypted:  [dueDate][priority|encryptedFlag][descLen:2 BE][noteLen:2 BE]
 *               [encDesc descLen][encNote noteLen]
 * The sort comparator only reads dueDate+priority+category (verified), so it is
 * unaffected. EVERY description/note READ of a possibly-encrypted record must go via
 * ToDoGetDecrypted; Find skips encrypted records. Nonce = (uniqueID, 0)=desc, 1=note.
 * These touch sec003 (crypto) -> FOREGROUND (full-launch) only. */

#define kTdEncTtl	1000000000UL		/* read TTL; Sec35's alarm enforces the lock */

static UInt16 tdRdU16(const UInt8 *p) { return (UInt16)(((UInt16)p[0] << 8) | p[1]); }
static void   tdWrU16(UInt8 *p, UInt16 v) { p[0] = (UInt8)(v >> 8); p[1] = (UInt8)v; }

/* offsets within the record of the priority byte and the description string */
#define kTdPriOff	((UInt16)((UInt8 *)&((ToDoDBRecordPtr)0)->priority - (UInt8 *)0))
#define kTdDescOff	((UInt16)((UInt8 *)&((ToDoDBRecordPtr)0)->description - (UInt8 *)0))

/* True if ANY record is marked encrypted (raw priority-byte scan; no key, no decrypt
 * -> sublaunch-safe, MAIN). */
Boolean ToDoDbHasEncrypted(DmOpenRef dbP)
{
	UInt16 i, n = DmNumRecords(dbP);

	for (i = 0; i < n; i++) {
		MemHandle h = DmQueryRecord(dbP, i);
		if (h != NULL) {
			Boolean enc = (Boolean)(((const UInt8 *)MemHandleLock(h))[kTdPriOff]
				& encryptedFlag);
			MemHandleUnlock(h);
			if (enc)
				return true;
		}
	}
	return false;
}

/* Convert one record between plaintext and the length-prefixed encrypted layout, IN
 * PLACE. (AES-CTR output can contain 0x00, which would truncate the inline
 * NUL-terminated strings, so an encrypted record stores desc/note as
 * [descLen:2][noteLen:2][encDesc][encNote] after dueDate+priority. The sort comparator
 * reads only dueDate/priority/category, so it is unaffected.) The crypto toggle itself
 * must NOT mark the record dirty for HotSync (only real user edits should), so the
 * pre-existing dirty bit is preserved. No-op if already in the target state or locked.
 * Model: the app decrypts ALL records on open (unlocked) and re-encrypts on close, so
 * records are plaintext only while the app is foreground; the rest of the app reads
 * them normally. Nonce (uid,0)=desc, (uid,1)=note. sec003 (foreground). */
static void TdRecrypt(DmOpenRef dbP, UInt16 index, Boolean toEnc) ENCCORE_SECTION;
static void TdRecrypt(DmOpenRef dbP, UInt16 index, Boolean toEnc)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt32 uid = 0;
	MemHandle h;
	UInt8 *rec, *out = NULL;
	Int32 recSize, outSize = 0;
	UInt16 descLen = 0, noteLen = 0, attr = 0;
	Boolean wasDirty;

	if (!EncGetKey(key, kTdEncTtl))
		return;
	h = DmGetRecord(dbP, index);
	if (h == NULL) { MemSet(key, ENC_KEY_BYTES, 0); return; }
	rec = (UInt8 *) MemHandleLock(h);
	if ((((rec[kTdPriOff] & encryptedFlag) != 0)) == toEnc) {	/* already in target state */
		MemHandleUnlock(h); DmReleaseRecord(dbP, index, false);
		MemSet(key, ENC_KEY_BYTES, 0); return;
	}
	DmRecordInfo(dbP, index, &attr, &uid, NULL);
	wasDirty = (Boolean)((attr & dmRecAttrDirty) != 0);
	recSize = MemHandleSize(h);

	if (toEnc) {						/* plaintext -> encrypted */
		UInt16 noteOff;
		descLen = (UInt16) StrLen((const char *)(rec + kTdDescOff));
		noteOff = (UInt16)(kTdDescOff + descLen + 1);
		noteLen = (noteOff < recSize) ? (UInt16) StrLen((const char *)(rec + noteOff)) : 0;
		outSize = kTdDescOff + 4 + descLen + noteLen;
		out = (UInt8 *) MemPtrNew(outSize);
		if (out != NULL) {
			MemMove(out, rec, kTdDescOff);				/* dueDate + priority */
			out[kTdPriOff] |= encryptedFlag;
			tdWrU16(out + kTdDescOff, descLen);
			tdWrU16(out + kTdDescOff + 2, noteLen);
			MemMove(out + kTdDescOff + 4, rec + kTdDescOff, descLen);
			MemMove(out + kTdDescOff + 4 + descLen, rec + noteOff, noteLen);
			if (descLen) EncCrypt(key, out + kTdDescOff + 4, descLen, uid, 0);
			if (noteLen) EncCrypt(key, out + kTdDescOff + 4 + descLen, noteLen, uid, 1);
		}
	} else {							/* encrypted -> plaintext */
		descLen = tdRdU16(rec + kTdDescOff);
		noteLen = tdRdU16(rec + kTdDescOff + 2);
		outSize = kTdDescOff + descLen + 1 + noteLen + 1;
		out = (UInt8 *) MemPtrNew(outSize);
		if (out != NULL) {
			MemMove(out, rec, kTdDescOff);
			out[kTdPriOff] &= (UInt8) ~encryptedFlag;
			MemMove(out + kTdDescOff, rec + kTdDescOff + 4, descLen);
			out[kTdDescOff + descLen] = 0;
			MemMove(out + kTdDescOff + descLen + 1, rec + kTdDescOff + 4 + descLen, noteLen);
			out[kTdDescOff + descLen + 1 + noteLen] = 0;
			if (descLen) EncCrypt(key, out + kTdDescOff, descLen, uid, 0);
			if (noteLen) EncCrypt(key, out + kTdDescOff + descLen + 1, noteLen, uid, 1);
		}
	}
	MemHandleUnlock(h);
	if (out != NULL) {
		h = DmResizeRecord(dbP, index, outSize);		/* may move; relock via new h */
		if (h != NULL) {
			rec = (UInt8 *) MemHandleLock(h);
			DmWrite(rec, 0, out, outSize);
			MemHandleUnlock(h);
		}
		MemPtrFree(out);
	}
	DmReleaseRecord(dbP, index, true);
	if (!wasDirty) {					/* the crypto toggle must not queue a HotSync */
		DmRecordInfo(dbP, index, &attr, NULL, NULL);
		attr = (UInt16)(attr & ~dmRecAttrDirty);
		DmSetRecordInfo(dbP, index, &attr, NULL);
	}
	MemSet(key, ENC_KEY_BYTES, 0);
}

/* Encrypt every plaintext record (call on close / when locking). No-op locked. */
void ToDoSealAllPlaintext(DmOpenRef dbP)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt16 i, n;

	if (!EncGetKey(key, kTdEncTtl)) return;
	MemSet(key, ENC_KEY_BYTES, 0);
	n = DmNumRecords(dbP);
	for (i = 0; i < n; i++) {
		MemHandle h = DmQueryRecord(dbP, i);
		if (h != NULL) {
			Boolean enc = (Boolean)(((const UInt8 *)MemHandleLock(h))[kTdPriOff] & encryptedFlag);
			MemHandleUnlock(h);
			if (!enc) TdRecrypt(dbP, i, true);
		}
	}
}

/* Decrypt every encrypted record into the standard plaintext layout (call on open,
 * once unlocked). No-op locked. The app then reads/edits records normally. */
void ToDoDecryptAll(DmOpenRef dbP)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt16 i, n;

	if (!EncGetKey(key, kTdEncTtl)) return;
	MemSet(key, ENC_KEY_BYTES, 0);
	n = DmNumRecords(dbP);
	for (i = 0; i < n; i++) {
		MemHandle h = DmQueryRecord(dbP, i);
		if (h != NULL) {
			Boolean enc = (Boolean)(((const UInt8 *)MemHandleLock(h))[kTdPriOff] & encryptedFlag);
			MemHandleUnlock(h);
			if (enc) TdRecrypt(dbP, i, false);
		}
	}
}

/* ---- per-record helpers for the at-rest (copy-edit) model ---------------- *
 * Like Address35, the storage record is ALWAYS ciphertext when a passcode is set; it is
 * NEVER decrypted in place.  Reads go through ToDoGetInlineCopy (a throwaway DECRYPTED
 * copy in the ORIGINAL inline [dueDate][pri][desc\0][note\0] layout, so all the existing
 * ->description / GetToDoNotePtr / height / title code works unchanged); edits hand the
 * ROM a decrypted field copy and re-encrypt it into storage on save.  So a lock or crash
 * never exposes plaintext on disk.  These crypto helpers are foreground-only (sec003). */

/* Seal a plaintext record in place (new records, or records made while locked). No-op if
 * already encrypted or locked. */
void ToDoEncryptOne(DmOpenRef dbP, UInt16 index) { TdRecrypt(dbP, index, true); }

/* Is the record empty (no description AND no note)?  Layout-aware, NO decryption/key
 * needed -> MAIN.  (For an encrypted record, emptiness is the two length prefixes = 0.) */
Boolean ToDoRecordIsEmpty(DmOpenRef dbP, UInt16 index)
{
	MemHandle h = DmQueryRecord(dbP, index);
	const UInt8 *rec;
	Boolean empty;

	if (h == NULL) return false;
	rec = (const UInt8 *) MemHandleLock(h);
	if (rec[kTdPriOff] & encryptedFlag) {
		empty = (Boolean)(tdRdU16(rec + kTdDescOff) == 0 && tdRdU16(rec + kTdDescOff + 2) == 0);
	} else {
		const char *d = (const char *)(rec + kTdDescOff);
		empty = (Boolean)(*d == 0 && *(d + StrLen(d) + 1) == 0);
	}
	MemHandleUnlock(h);
	return empty;
}

/* Does the record have a non-empty note?  Layout-aware, NO key -> MAIN. */
Boolean ToDoRecordHasNote(DmOpenRef dbP, UInt16 index)
{
	MemHandle h = DmQueryRecord(dbP, index);
	const UInt8 *rec;
	Boolean has;

	if (h == NULL) return false;
	rec = (const UInt8 *) MemHandleLock(h);
	if (rec[kTdPriOff] & encryptedFlag) {
		has = (Boolean)(tdRdU16(rec + kTdDescOff + 2) != 0);
	} else {
		const char *d = (const char *)(rec + kTdDescOff);
		has = (Boolean)(*(d + StrLen(d) + 1) != 0);
	}
	MemHandleUnlock(h);
	return has;
}

/* THE READ CHOKEPOINT.  Return a locked handle holding a DECRYPTED copy of the record in
 * the ORIGINAL inline layout [dueDate][priority(enc bit cleared)][desc\0][note\0].  When
 * the record is encrypted but Sec35 is LOCKED, the description is masked with "******" and
 * the note blanked, so the UI shows stars rather than ciphertext.  A plaintext record is
 * copied verbatim.  Caller must ToDoReleaseInlineCopy the handle.  sec003 (foreground). */
ToDoDBRecordPtr ToDoGetInlineCopy(DmOpenRef dbP, UInt16 index, MemHandle *outH) ENCCORE_SECTION;
ToDoDBRecordPtr ToDoGetInlineCopy(DmOpenRef dbP, UInt16 index, MemHandle *outH)
{
	MemHandle h, copy = NULL;
	const UInt8 *rec;
	UInt8 *dst = NULL;
	Int32 recSize;
	UInt16 descLen, noteLen;

	*outH = NULL;
	h = DmQueryRecord(dbP, index);
	if (h == NULL) return NULL;
	rec = (const UInt8 *) MemHandleLock(h);
	recSize = MemHandleSize(h);

	if ((rec[kTdPriOff] & encryptedFlag) == 0) {		/* plaintext: copy verbatim */
		copy = MemHandleNew(recSize);
		if (copy != NULL) {
			dst = (UInt8 *) MemHandleLock(copy);
			MemMove(dst, rec, recSize);
		}
	} else {
		UInt8 key[ENC_KEY_BYTES];
		UInt32 uid = 0;
		Boolean haveKey = EncGetKey(key, kTdEncTtl);

		descLen = tdRdU16(rec + kTdDescOff);
		noteLen = tdRdU16(rec + kTdDescOff + 2);

		if (!haveKey) {									/* LOCKED: mask desc, blank note */
			UInt16 stars = 6;
			copy = MemHandleNew(kTdDescOff + stars + 2);
			if (copy != NULL) {
				dst = (UInt8 *) MemHandleLock(copy);
				MemMove(dst, rec, kTdDescOff);
				dst[kTdPriOff] &= (UInt8) ~encryptedFlag;
				MemSet(dst + kTdDescOff, stars, '*');
				dst[kTdDescOff + stars] = 0;
				dst[kTdDescOff + stars + 1] = 0;
			}
		} else {										/* unlocked: decrypt into inline */
			DmRecordInfo(dbP, index, NULL, &uid, NULL);
			copy = MemHandleNew(kTdDescOff + descLen + 1 + noteLen + 1);
			if (copy != NULL) {
				dst = (UInt8 *) MemHandleLock(copy);
				MemMove(dst, rec, kTdDescOff);
				dst[kTdPriOff] &= (UInt8) ~encryptedFlag;
				MemMove(dst + kTdDescOff, rec + kTdDescOff + 4, descLen);
				dst[kTdDescOff + descLen] = 0;
				MemMove(dst + kTdDescOff + descLen + 1, rec + kTdDescOff + 4 + descLen, noteLen);
				dst[kTdDescOff + descLen + 1 + noteLen] = 0;
				if (descLen) EncCrypt(key, dst + kTdDescOff, descLen, uid, 0);
				if (noteLen) EncCrypt(key, dst + kTdDescOff + descLen + 1, noteLen, uid, 1);
			}
			MemSet(key, ENC_KEY_BYTES, 0);
		}
	}

	MemHandleUnlock(h);
	if (copy == NULL) return NULL;
	*outH = copy;
	return (ToDoDBRecordPtr) dst;
}

void ToDoReleaseInlineCopy(MemHandle h)
{
	if (h != NULL) { MemHandleUnlock(h); MemHandleFree(h); }
}

/* Return a NEW handle with the decrypted DESCRIPTION (field 0) resp. NOTE (field 1) string,
 * for the ROM to edit / for per-row list drawing.  Only the requested field is decrypted
 * (cheap enough to run per row per redraw).  Caller MemHandleFree's it.  NULL on OOM /
 * locked-with-key-missing.  sec003. */
static MemHandle TdCopyField(DmOpenRef dbP, UInt16 index, UInt16 field) ENCCORE_SECTION;
static MemHandle TdCopyField(DmOpenRef dbP, UInt16 index, UInt16 field)
{
	MemHandle h, out = NULL;
	const UInt8 *rec;
	char *dst;

	h = DmQueryRecord(dbP, index);
	if (h == NULL) return NULL;
	rec = (const UInt8 *) MemHandleLock(h);

	if ((rec[kTdPriOff] & encryptedFlag) == 0) {		/* plaintext inline: copy field */
		const char *d = (const char *)(rec + kTdDescOff);
		const char *s = (field == 0) ? d : (d + StrLen(d) + 1);
		UInt16 ln = (UInt16) StrLen(s);
		out = MemHandleNew(ln + 1);
		if (out != NULL) {
			dst = (char *) MemHandleLock(out);
			MemMove(dst, s, ln + 1);
			MemHandleUnlock(out);
		}
	} else {											/* encrypted: decrypt just this field */
		UInt8 key[ENC_KEY_BYTES];
		UInt32 uid = 0;
		if (EncGetKey(key, kTdEncTtl)) {
			UInt16 descLen = tdRdU16(rec + kTdDescOff);
			UInt16 noteLen = tdRdU16(rec + kTdDescOff + 2);
			UInt16 ln = (field == 0) ? descLen : noteLen;
			const UInt8 *s = rec + kTdDescOff + 4 + ((field == 0) ? 0 : descLen);
			DmRecordInfo(dbP, index, NULL, &uid, NULL);
			out = MemHandleNew(ln + 1);
			if (out != NULL) {
				dst = (char *) MemHandleLock(out);
				MemMove(dst, s, ln);
				if (ln) EncCrypt(key, (UInt8 *) dst, ln, uid, field);
				dst[ln] = 0;
				MemHandleUnlock(out);
			}
		}
		MemSet(key, ENC_KEY_BYTES, 0);
	}

	MemHandleUnlock(h);
	return out;
}

MemHandle ToDoCopyDescPlain(DmOpenRef dbP, UInt16 index) ENCCORE_SECTION;
MemHandle ToDoCopyDescPlain(DmOpenRef dbP, UInt16 index) { return TdCopyField(dbP, index, 0); }

MemHandle ToDoCopyNotePlain(DmOpenRef dbP, UInt16 index) ENCCORE_SECTION;
MemHandle ToDoCopyNotePlain(DmOpenRef dbP, UInt16 index) { return TdCopyField(dbP, index, 1); }

/* Build the encrypted record [dueDate][pri|enc][descLen:2][noteLen:2][encDesc][encNote]
 * from plaintext desc + note and write it to storage.  Shared by the desc/note savers.
 * Real user edit -> the record is left dirty.  No-op if locked. sec003. */
static void TdWriteEncrypted(DmOpenRef dbP, UInt16 index,
	const char *desc, UInt16 descLen, const char *note, UInt16 noteLen) ENCCORE_SECTION;
static void TdWriteEncrypted(DmOpenRef dbP, UInt16 index,
	const char *desc, UInt16 descLen, const char *note, UInt16 noteLen)
{
	UInt8 key[ENC_KEY_BYTES];
	UInt32 uid = 0;
	MemHandle h;
	UInt8 *out;
	Int32 outSize = kTdDescOff + 4 + descLen + noteLen;

	if (!EncGetKey(key, kTdEncTtl)) return;
	DmRecordInfo(dbP, index, NULL, &uid, NULL);
	h = DmGetRecord(dbP, index);
	if (h == NULL) { MemSet(key, ENC_KEY_BYTES, 0); return; }
	out = (UInt8 *) MemPtrNew(outSize);
	if (out != NULL) {
		UInt8 *rec = (UInt8 *) MemHandleLock(h);
		MemMove(out, rec, kTdDescOff);				/* dueDate + priority */
		MemHandleUnlock(h);
		out[kTdPriOff] |= encryptedFlag;
		tdWrU16(out + kTdDescOff, descLen);
		tdWrU16(out + kTdDescOff + 2, noteLen);
		MemMove(out + kTdDescOff + 4, desc, descLen);
		MemMove(out + kTdDescOff + 4 + descLen, note, noteLen);
		if (descLen) EncCrypt(key, out + kTdDescOff + 4, descLen, uid, 0);
		if (noteLen) EncCrypt(key, out + kTdDescOff + 4 + descLen, noteLen, uid, 1);
	}
	MemSet(key, ENC_KEY_BYTES, 0);
	if (out != NULL) {
		h = DmResizeRecord(dbP, index, outSize);
		if (h != NULL) {
			UInt8 *p = (UInt8 *) MemHandleLock(h);
			DmWrite(p, 0, out, outSize);
			MemHandleUnlock(h);
		}
		MemPtrFree(out);
	}
	DmReleaseRecord(dbP, index, true);				/* user edit -> dirty */
}

/* Re-encrypt storage after the list field edited the DESCRIPTION (keeps the note); after
 * the note field edited the NOTE (keeps the description).  Each reads the OTHER field
 * from the current (decrypted) inline copy, so a plaintext record is converted too. */
void ToDoSetDescEncrypted(DmOpenRef dbP, UInt16 index, const char *newDesc) ENCCORE_SECTION;
void ToDoSetDescEncrypted(DmOpenRef dbP, UInt16 index, const char *newDesc)
{
	MemHandle srcH;
	ToDoDBRecordPtr rec = ToDoGetInlineCopy(dbP, index, &srcH);
	if (rec != NULL) {
		const char *oldDesc = &rec->description;
		const char *oldNote = oldDesc + StrLen(oldDesc) + 1;
		TdWriteEncrypted(dbP, index, newDesc, (UInt16) StrLen(newDesc),
			oldNote, (UInt16) StrLen(oldNote));
		ToDoReleaseInlineCopy(srcH);
	}
}

void ToDoSetNoteEncrypted(DmOpenRef dbP, UInt16 index, const char *newNote) ENCCORE_SECTION;
void ToDoSetNoteEncrypted(DmOpenRef dbP, UInt16 index, const char *newNote)
{
	MemHandle srcH;
	ToDoDBRecordPtr rec = ToDoGetInlineCopy(dbP, index, &srcH);
	if (rec != NULL) {
		const char *oldDesc = &rec->description;
		TdWriteEncrypted(dbP, index, oldDesc, (UInt16) StrLen(oldDesc),
			newNote, (UInt16) StrLen(newNote));
		ToDoReleaseInlineCopy(srcH);
	}
}

/* ---- transient plaintext inbox for beamed vCal (ToDoDBPlain) ------------ *
 * Beam receive is a no-globals sublaunch: parsing vCal there (static tables, no A5)
 * corrupts the heap (same trap as Address35). So the sublaunch drops the RAW beam
 * bytes here, unparsed; the next full launch parses + imports them (ToDoMergePlainDB
 * in ToDoTransfer.c) and deletes this. Pure DB I/O -> MAIN, globals-free. Distinct
 * type 'Td3P' so global Find never scans it. */
#define ToDoPlainDBName		"ToDoDBPlain"
#define ToDoPlainDBType		'Td3P'

Err ToDoOpenPlainDB (DmOpenRef *dbPP, Boolean create)
{
	LocalID id = DmFindDatabase (0, ToDoPlainDBName);

	*dbPP = NULL;
	if (id == 0) {
		if (!create)
			return dmErrCantFind;
		if (DmCreateDatabase (0, ToDoPlainDBName, 'Td35', ToDoPlainDBType, false) != errNone)
			return DmGetLastErr ();
		id = DmFindDatabase (0, ToDoPlainDBName);
		if (id == 0)
			return dmErrCantFind;
	}
	*dbPP = DmOpenDatabase (0, id, dmModeReadWrite);
	return *dbPP ? errNone : DmGetLastErr ();
}

void ToDoInboxAppendRaw (DmOpenRef pdb, const void *bytes, UInt16 len, UInt16 category)
{
	UInt16 idx = dmMaxRecordIndex;
	MemHandle h;

	if (pdb == NULL || len == 0)
		return;
	h = DmNewRecord (pdb, &idx, len);
	if (h != NULL) {
		void *p = MemHandleLock (h);
		DmWrite (p, 0, bytes, len);
		MemHandleUnlock (h);
		DmReleaseRecord (pdb, idx, true);
		/* Td35: carry the receive-time category choice in the inbox record's own category
		 * attr, so the merge can hand it to the parser (which files the item there). */
		if (category) {
			UInt16 attr = 0;
			DmRecordInfo (pdb, idx, &attr, NULL, NULL);
			attr = (UInt16)((attr & ~dmRecAttrCategoryMask) | (category & dmRecAttrCategoryMask));
			DmSetRecordInfo (pdb, idx, &attr, NULL);
		}
	}
}


/************************************************************
 * Private routines used only in this module
 *************************************************************/


/************************************************************
 *
 *  FUNCTION: ECToDoDBValidate
 *
 *  DESCRIPTION: This routine validates the integrity of the to do
 *		datebase.
 *
 *  PARAMETERS: database pointer
 *
 *  RETURNS: nothing
 *
 *  CREATED: 6/22/95 
 *
 *  BY: Art Lamb
 *
 *************************************************************/
#if ERROR_CHECK_LEVEL == ERROR_CHECK_FULL

#define maxDescLen	256
#define maxNoteLen	4000

void ECToDoDBValidate (DmOpenRef dbP)
{
	UInt16 i;
	UInt16 size;
	UInt16 cLen;
	UInt16 recSize;
	UInt16 descLen;
	UInt16 noteLen;
	UInt16 numRecord;
	UInt16 priority;
	Char * c;
	Char * note;
	MemHandle recH;
	ToDoDBRecordPtr rec;
	
	numRecord = DmNumRecords (dbP);
	for (i = 0 ; i < numRecord; i++)
		{
		recH = DmQueryRecord (dbP, i);
		if (! recH) continue;

		rec = MemHandleLock (recH);

		priority = rec->priority & priorityOnly;
		ErrFatalDisplayIf (priority > toDoMaxPriority,  "DB integrity error");

		// Td35: an ENCRYPTED record uses the length-prefixed layout
		// [dueDate][pri|enc][descLen:2][noteLen:2][encDesc][encNote], not the plaintext
		// inline [desc\0][note\0] -- so its size and byte scan differ.  Validate that shape.
		if (rec->priority & encryptedFlag)
			{
			const UInt8 *b = (const UInt8 *) rec;
			descLen = tdRdU16 (b + kTdDescOff);
			noteLen = tdRdU16 (b + kTdDescOff + 2);
			size = kTdDescOff + 4 + descLen + noteLen;
			recSize = MemPtrSize (rec);
			ErrFatalDisplayIf ((recSize != size), "DB integrity error");
			MemPtrUnlock (rec);
			continue;
			}

		descLen = StrLen (&rec->description);
		ErrFatalDisplayIf (descLen > maxDescLen, "DB integrity error");

		note = &rec->description + descLen + 1;
		noteLen = StrLen (note);
		ErrFatalDisplayIf (descLen > maxNoteLen, "DB integrity error");

		// Validate the record size.
		size = sizeof (DateType) + 1;
		c = &rec->description;
		cLen = StrLen(c) + 1;
		size += cLen;
		c += cLen;
		size += StrLen(c) + 1;
		recSize = MemPtrSize (rec);
		ErrFatalDisplayIf ( (recSize != size), "DB integrity error");

		MemPtrUnlock (rec);
		}
}
#endif

/************************************************************
 *
 *  FUNCTION: DateTypeCmp
 *
 *  DESCRIPTION: Compare two dates
 *
 *  PARAMETERS: 
 *
 *  RETURNS: 
 *
 *  CREATED: 1/20/95 
 *
 *  BY: Roger Flores
 *
 *************************************************************/
static Int16 DateTypeCmp(DateType d1, DateType d2)
{
	Int16 result;
	
	result = d1.year - d2.year;
	if (result != 0)
		{
		if (DateToInt(d1) == 0xffff)
			return 1;
		if (DateToInt(d2) == 0xffff)
			return -1;
		return result;
		}
	
	result = d1.month - d2.month;
	if (result != 0)
		return result;
	
	result = d1.day - d2.day;

	return result;

}


/************************************************************
 *
 *  FUNCTION: CategoryCompare
 *
 *  DESCRIPTION: Compare two categories
 *
 *  PARAMETERS:  attr1, attr2 - record attributes, which contain 
 *						  the category. 
 *					  appInfoH - MemHandle of the applications category info
 *
 *  RETURNS: 0 if they match, non-zero if not
 *				 + if s1 > s2
 *				 - if s1 < s2
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	8/5/96	Initial Revision
 *
 *************************************************************/
static Int16 CategoryCompare (UInt8 attr1, UInt8 attr2, MemHandle appInfoH)
{
	Int16 result;
	UInt8 category1;
	UInt8 category2;
	ToDoAppInfoPtr appInfoP;
	
	
	category1 = attr1 & dmRecAttrCategoryMask;
	category2 = attr2 & dmRecAttrCategoryMask;

	result = category1 - category2;
	if (result != 0)
		{
		if (category1 == dmUnfiledCategory)
			return (1);
		else if (category2 == dmUnfiledCategory)
			return (-1);
		
		appInfoP = MemHandleLock (appInfoH);
		result = StrCompare (appInfoP->categoryLabels[category1],
									appInfoP->categoryLabels[category2]);

		MemPtrUnlock (appInfoP);
		return result;
		}
	
	return result;
}


/************************************************************
 *
 *  FUNCTION: ToDoAppInfoInit
 *
 *  DESCRIPTION: Create an app info chunk if missing.  Set
 *		the strings to a default.
 *
 *  PARAMETERS: database pointer
 *
 *  RETURNS: 0 if successful, errorcode if not
 *
 *  CREATED: 1/20/95 
 *
 *  BY: Roger Flores
 *
 *************************************************************/
Err	ToDoAppInfoInit(DmOpenRef dbP)
{
	UInt16 cardNo;
	UInt16 wordValue;
	MemHandle h;
	LocalID dbID;
	LocalID appInfoID;
	ToDoAppInfoPtr	nilP = 0;
	ToDoAppInfoPtr appInfoP;
	
	if (DmOpenDatabaseInfo(dbP, &dbID, NULL, NULL, &cardNo, NULL))
		return dmErrInvalidParam;

	if (DmDatabaseInfo(cardNo, dbID, NULL, NULL, NULL, NULL, NULL, NULL, 
		 NULL, &appInfoID, NULL, NULL, NULL))
		return dmErrInvalidParam;
	
	if (appInfoID == NULL)
		{
		h = DmNewHandle(dbP, sizeof (ToDoAppInfoType));
		if (! h) return dmErrMemError;

		appInfoID = MemHandleToLocalID (h);
		DmSetDatabaseInfo(cardNo, dbID, NULL, NULL, NULL, NULL, NULL, NULL, 
			NULL, &appInfoID, NULL, NULL, NULL);
		}
	
	appInfoP = MemLocalIDToLockedPtr(appInfoID, cardNo);

	// Clear the app info block.
	DmSet (appInfoP, 0, sizeof(ToDoAppInfoType), 0);

	// Initialize the categories.
	CategoryInitialize ((AppInfoPtr) appInfoP, LocalizedAppInfoStr);

	// I don't know what this field is used for.
	wordValue = 0xFFFF;
	DmWrite (appInfoP, (UInt32)&nilP->dirtyAppInfo, &wordValue,
		sizeof(appInfoP->dirtyAppInfo));

	// Initialize the sort order.
	DmSet (appInfoP, (UInt32)&nilP->sortOrder, sizeof(appInfoP->sortOrder), 
		soPriorityDueDate);

	MemPtrUnlock (appInfoP);

	return 0;
}


/************************************************************
 *
 *  FUNCTION: ToDoGetAppInfo
 *
 *  DESCRIPTION: Get the app info chunk 
 *
 *  PARAMETERS: database pointer
 *
 *  RETURNS: MemHandle to the to do application info block (ToDoAppInfoType)
 *
 *  CREATED: 5/12/95 
 *
 *  BY: Art Lamb
 *
 *************************************************************/
MemHandle ToDoGetAppInfo (DmOpenRef dbP)
{
	Err error;
	UInt16 cardNo;
	LocalID dbID;
	LocalID appInfoID;
	
	error = DmOpenDatabaseInfo (dbP, &dbID, NULL, NULL, &cardNo, NULL);
	ErrFatalDisplayIf (error,  "Get getting to do app info block");

	error = DmDatabaseInfo (cardNo, dbID, NULL, NULL, NULL, NULL, NULL, 
			NULL, NULL, &appInfoID, NULL, NULL, NULL);
	ErrFatalDisplayIf (error,  "Get getting to do app info block");

	return ((MemHandle) MemLocalIDToGlobal (appInfoID, cardNo));
}


/************************************************************
 *
 *  FUNCTION: ToDoGetSortOrder
 *
 *  DESCRIPTION: This routine gets the sort order value from the 
 *               'to do' application info block.
 *
 *  PARAMETERS: database pointer
 *
 *  RETURNS:    true - if the 'to do' record are sorted by priority, 
 *              false - if the records are sorted by due date.
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	5/12/95	Initial Revision
 *       art	3/22/96	Rename routine and added more sort orders
 *
 *************************************************************/
UInt8 ToDoGetSortOrder (DmOpenRef dbP)
{
	UInt8 sortOrder;
	ToDoAppInfoPtr appInfoP;
			
	appInfoP = MemHandleLock (ToDoGetAppInfo (dbP));
	sortOrder = appInfoP->sortOrder;
	MemPtrUnlock (appInfoP);	

	return (sortOrder);
}


/************************************************************
 *
 *  FUNCTION: ToDoCompareRecords
 *
 *  DESCRIPTION: Compare two records.
 *
 *  PARAMETERS: database record 1
 *				database record 2
 *
 *  RETURNS: -n if record one is less (n != 0)
 *			  n if record two is less
 *
 *  CREATED: 1/23/95 
 *
 *  BY: Roger Flores
 *
 *	COMMENTS:	Compare the two records key by key until
 *	there is a difference.  Return -n if r1 is less or n if r2
 *	is less.  A zero is never returned because if two records
 *	seem identical then their unique IDs are compared!
 *
 * This function accepts record data chunk pointers to avoid
 * requiring that the record be within the database.  This is
 * important when adding records to a database.  This prevents
 * determining if a record is a deleted record (which are kept
 * at the end of the database and should be considered "greater").
 * The caller should test for deleted records before calling this
 * function!
 *
 *************************************************************/
 
static Int16 ToDoCompareRecords(ToDoDBRecordPtr r1, 
	ToDoDBRecordPtr r2, Int16 sortOrder, 
	SortRecordInfoPtr info1, SortRecordInfoPtr info2,
	MemHandle appInfoH)
{
	Int16 result;

	// Sort by priority, due date, and category.
	if (sortOrder == soPriorityDueDate)
		{
		result = (r1->priority & priorityOnly) - (r2->priority & priorityOnly);
		if (result == 0)
			{
			result = DateTypeCmp (r1->dueDate, r2->dueDate);
			if (result == 0)
				{
				result = CategoryCompare (info1->attributes, info2->attributes, appInfoH);
				}
			}
		}

	// Sort by due date, priority, and category.
	else if (sortOrder == soDueDatePriority)
		{
		result = DateTypeCmp(r1->dueDate, r2->dueDate);
		if (result == 0)
			{
			result = (r1->priority & priorityOnly) - (r2->priority & priorityOnly);
			if (result == 0)
				{
				result = CategoryCompare (info1->attributes, info2->attributes, appInfoH);
				}
			}
		}
	
	// Sort by category, priority, due date
	else if (sortOrder == soCategoryPriority)
		{
		result = CategoryCompare (info1->attributes, info2->attributes, appInfoH);
		if (result == 0)
			{
			result = (r1->priority & priorityOnly) - (r2->priority & priorityOnly);
			if (result == 0)
				{
				result = DateTypeCmp (r1->dueDate, r2->dueDate);
				}
			}
		}

	// Sort by category, due date, priority
	else if (sortOrder == soCategoryDueDate)
		{
		result = CategoryCompare (info1->attributes, info2->attributes, appInfoH);
		if (result == 0)
			{
			result = DateTypeCmp (r1->dueDate, r2->dueDate);
			if (result == 0)
				{
				result = (r1->priority & priorityOnly) - (r2->priority & priorityOnly);
				}
			}
		}
	
	return result;
}


/************************************************************
 *
 *  FUNCTION: ToDoSize
 *
 *  DESCRIPTION: Return the size of a ToDoDBRecordType
 *
 *  PARAMETERS: database record
 *
 *  RETURNS: the size in bytes
 *
 *  CREATED: 1/10/95 
 *
 *  BY: Roger Flores
 *
 *************************************************************/
static UInt16 ToDoSize(ToDoDBRecordPtr r)
{
	UInt16 size;
	char *c;
	int cLen;
	
	c = &r->description;
	cLen = StrLen(c) + 1;
	size = sizeof (DateType) + 1 + cLen;
	c += cLen;
	
	size += StrLen(c) + 1;

	return size;
}


/************************************************************
 *
 *  FUNCTION: ToDoFindSortPosition
 *
 *  DESCRIPTION: Return where a record is or should be.
 *		Useful to find a record or find where to insert a record.
 *
 *  PARAMETERS: database record (not deleted!)
 *
 *  RETURNS: the size in bytes
 *
 *  CREATED: 1/11/95 
 *
 *  BY: Roger Flores
 *
 *************************************************************/
static UInt16 ToDoFindSortPosition(DmOpenRef dbP, ToDoDBRecord *newRecord, 
		SortRecordInfoPtr newRecordInfo)
{
	int sortOrder;
	
	sortOrder = ToDoGetSortOrder (dbP);
		
	return (DmFindSortPosition (dbP, newRecord, newRecordInfo, 
		(DmComparF *)ToDoCompareRecords, sortOrder));
}


/************************************************************
 *
 *  FUNCTION: ToDoNewRecord
 *
 *  DESCRIPTION: Create a new record in sorted position
 *
 *  PARAMETERS: database pointer
 *				database record
 *
 *  RETURNS: ##0 if successful, errorcode if not
 *
 *  CREATED: 1/10/95 
 *
 *  BY: Roger Flores
 *
 *************************************************************/
Err ToDoNewRecord(DmOpenRef dbP, ToDoItemPtr item, UInt16 category, UInt16 *index)
{
	Err 						err;
	UInt16					size;
	Char						zero=0;
	UInt16					attr;
	UInt16 					newIndex;
	UInt32					offset;
	ToDoDBRecordPtr		nilP=0;
	ToDoDBRecordPtr		recordP;
	MemHandle 				recordH;
	SortRecordInfoType	sortInfo;

	// Compute the size of the new to do record.
	size = sizeDBRecord;
	if (item->description)
		size += StrLen (item->description);
	if (item->note)
		size += StrLen (item->note);
		

	//  Allocate a chunk in the database for the new record.
	recordH = (MemHandle)DmNewHandle(dbP, size);
	if (recordH == NULL)
		return dmErrMemError;

	// Pack the the data into the new record.
	recordP = MemHandleLock (recordH);
	DmWrite(recordP, (UInt32)&nilP->dueDate, &item->dueDate, sizeof(nilP->dueDate));
	DmWrite(recordP, (UInt32)&nilP->priority, &item->priority, sizeof(nilP->priority));

	offset = (UInt32)&nilP->description;
	if (item->description)
		{
		DmStrCopy(recordP, offset, item->description);
		offset += StrLen (item->description) + 1;
		}
	else
		{
		DmWrite(recordP, offset, &zero, 1);
		offset++;
		}
	
	if (item->note) 
		DmStrCopy(recordP, offset, item->note);
	else
		DmWrite(recordP, offset, &zero, 1);
	
	
	sortInfo.attributes = category;
	sortInfo.uniqueID[0] = 0;
	sortInfo.uniqueID[1] = 0;
	sortInfo.uniqueID[2] = 0;
	
	// Determine the sort position of the new record.
	newIndex = ToDoFindSortPosition (dbP, recordP, &sortInfo);

	MemPtrUnlock (recordP);

	// Insert the record.
	err = DmAttachRecord(dbP, &newIndex, recordH, 0);
	if (err) 
		MemHandleFree(recordH);
	else
		{
		*index = newIndex;

		// Set the category.
		DmRecordInfo (dbP, newIndex, &attr, NULL, NULL);
		attr &= ~dmRecAttrCategoryMask;
		attr |= category;
		DmSetRecordInfo (dbP, newIndex, &attr, NULL);
		}

	return err;
}


/***********************************************************************
 *
 * FUNCTION:    ToDoInsertNewRecord
 *
 * DESCRIPTION: This routine creates a new record and inserts it after
 *              the specified position.  The new record is assigned the 
 *              same priority and due date as the record it is 
 *              inserted after.
 *
 * PARAMETERS:  database pointer
 *              database record
 *
 * RETURNED:    nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			art	5/1/95	Initial Revision
 *
 ***********************************************************************/
Err ToDoInsertNewRecord (DmOpenRef dbP, UInt16 * index)
{
	UInt8					priority;
	ToDoDBRecordPtr 	rec;
	ToDoDBRecordPtr 	newRec;
	MemHandle 		 	recH;
	MemHandle			 	newRecH;
	Err				 	err;
	UInt16 				size;
	ToDoDBRecordPtr	nilP=0;
	UInt16				zero=0;
	UInt16 				newIndex;
	UInt16				category;
	UInt16				attr;


	// Make a new chunk
	size = sizeof (ToDoDBRecord) + 1;
	newRecH = DmNewHandle (dbP, size);
	if (! newRecH) return dmErrMemError;


	// Get the record that the new record will be inserted after.
	recH = DmQueryRecord (dbP, *index);
	ErrFatalDisplayIf (!recH, "Error inserting new to do record");
	rec = MemHandleLock (recH);

	// Get the category of the current record.
	DmRecordInfo (dbP, *index, &attr, NULL, NULL);
	category = (attr & dmRecAttrCategoryMask);
		
	// Set the priority and the due date of the new record to the same
	// values as the record we're inserting after.  This will insure
	// that the records are in the proper sort order.
	newRec = (ToDoDBRecord *) MemHandleLock (newRecH);
	DmWrite(newRec, (UInt32)&nilP->dueDate, &rec->dueDate, sizeof(rec->dueDate));

	priority = rec->priority & priorityOnly;
	DmWrite(newRec, (UInt32)&nilP->priority, &priority, sizeof(rec->priority));
	
	// Description is 1 byte, plus add extra byte for note field
	DmWrite(newRec, (UInt32)&nilP->description, &zero, 2);

	MemPtrUnlock (rec);

	// Attach in place
	newIndex = *index + 1;
	err = DmAttachRecord(dbP, &newIndex, newRecH, 0);
	if (err) 
		{
		MemHandleFree(newRecH);
		}
	else
		{
		MemPtrUnlock (newRec);

		// Set the category of the new record.
		DmRecordInfo (dbP, newIndex, &attr, NULL, NULL);
		attr |= category;
		DmSetRecordInfo (dbP, newIndex, &attr, NULL);

		*index = newIndex;
		}

#if ERROR_CHECK_LEVEL == ERROR_CHECK_FULL
	ECToDoDBValidate (dbP);
#endif
	
	return err;
}


/************************************************************
 *
 *  FUNCTION: ToDoChangeSortOrder
 *
 *  DESCRIPTION: Change the ToDo Database's sort order
 *
 *  PARAMETERS: database pointer
 *				TRUE if sort by company
 *
 *  RETURNS: nothing
 *
 *  CREATED: 1/17/95 
 *
 *  BY: Roger Flores
 *
 *************************************************************/
Err ToDoChangeSortOrder(DmOpenRef dbP, Boolean sortOrder)
{
	ToDoAppInfoPtr appInfoP;
	ToDoAppInfoPtr	nilP = 0;
	UInt16			dirtyAppInfo;


	appInfoP = MemHandleLock (ToDoGetAppInfo (dbP));

	if (appInfoP->sortOrder != sortOrder)
		{
		dirtyAppInfo = appInfoP->dirtyAppInfo | toDoSortByPriorityDirty;
		DmWrite(appInfoP, (UInt32)&nilP->dirtyAppInfo, &dirtyAppInfo, sizeof(appInfoP->dirtyAppInfo)); 
		DmWrite(appInfoP, (UInt32)&nilP->sortOrder, &sortOrder, sizeof(appInfoP->sortOrder));
		
		DmInsertionSort(dbP, (DmComparF *) &ToDoCompareRecords, (Int16) sortOrder);
		}
		
	MemPtrUnlock (appInfoP);	

	return 0;
}


/************************************************************
 *
 *  FUNCTION: ToDoSort
 *
 *  DESCRIPTION: Sort the appointment database.
 *
 *  PARAMETERS: database record
 *
 *  RETURNS: nothing
 *
 *  CREATED: 10/17/95 
 *
 *  BY: Art Lamb
 *
 *************************************************************/
void ToDoSort (DmOpenRef dbP)
{
	int sortOrder;
	
	sortOrder = ToDoGetSortOrder (dbP);
	DmInsertionSort(dbP, (DmComparF *) &ToDoCompareRecords, (Int16) sortOrder);
}


/************************************************************
 *
 *  FUNCTION: ToDoChangeRecord
 *
 *  DESCRIPTION: Change a record in the ToDo Database
 *
 *  PARAMETERS: database pointer
 *					 database index
 *					 database record
 *					 changed fields
 *
 *  RETURNS: ##0 if successful, errorcode if not
 *
 *  CREATED: 1/14/95 
 *
 *  BY: Roger Flores
 *
 *	COMMENTS:	Records are not stored with extra padding - they
 *	are always resized to their exact storage space.  This avoids
 *	a database compression issue.  The code works as follows:
 *	
 *
 *************************************************************/
Err ToDoChangeRecord(DmOpenRef dbP, UInt16 *index, 
	ToDoRecordFieldType changedField, void * data)
{
	Err 						err;
	Int16 					cLen;
	UInt16					attr;
	UInt16 					curSize;
	UInt16 					newSize;
	UInt16 					newIndex;
	UInt8						priority;
	UInt32					offset;
	Char * 					c;
	MemHandle 				recordH=0;
	ToDoDBRecord			temp;
	ToDoDBRecordPtr 		src;
	ToDoDBRecordPtr 		nilP = 0;
	SortRecordInfoType	sortInfo;
	
	// Get the record which we are going to change
	recordH = DmQueryRecord(dbP, *index);
	src = MemHandleLock (recordH);
	

	// If the  record is being change such that its sort position will 
	// change,  move the record to its new sort position.
	if ( (changedField == toDoPriority) || 
		  (changedField == toDoDueDate)  ||
		  (changedField == toDoCategory) )
		{
		MemSet (&sortInfo, sizeof (sortInfo), 0);
		DmRecordInfo (dbP, *index, &attr, NULL, NULL);
		sortInfo.attributes = attr;

		if (changedField == toDoPriority)
			{
			temp.priority = *(UInt16 *) data;
			temp.dueDate = src->dueDate;
			sortInfo.attributes = attr;
			}
		else if (changedField == toDoDueDate) 
			{
			temp.priority = src->priority;
			temp.dueDate = *((DatePtr)data);
			sortInfo.attributes = attr;
			}
		else
			{
			temp.priority = src->priority;
			temp.dueDate = src->dueDate;
			sortInfo.attributes = *(UInt16 *) data;			
			}

		newIndex = ToDoFindSortPosition (dbP, &temp, &sortInfo);
		DmMoveRecord (dbP, *index, newIndex);
		if (newIndex > *index) newIndex--;
		*index = newIndex;
		}


	if (changedField == toDoPriority)
		{
		/* Td35: keep BOTH high bits (complete + encrypted); dropping encryptedFlag here
		 * would leave the ciphertext body but a "plaintext" marker -> garbage on read. */
		priority = *(UInt16 *) data | (src->priority & (completeFlag | encryptedFlag));
		DmWrite (src, (UInt32)&nilP->priority, &priority, sizeof(src->priority));
		goto exit;
		}

	if (changedField == toDoComplete)
		{
		/* Td35: priorityOnly (0x3f) masks out encryptedFlag (0x40) -- OR it back in so
		 * checking an item off doesn't clear the at-rest encryption marker. */
		priority = (*(UInt16 *) data << 7) |  (src->priority & (priorityOnly | encryptedFlag));
		DmWrite (src, (UInt32)&nilP->priority, &priority, sizeof(src->priority));
		goto exit;
		}

	if (changedField == toDoDueDate)
		{ 
		DmWrite (src, (UInt32)&nilP->dueDate, data, sizeof(src->dueDate));
		goto exit;
		}
		
	if (changedField == toDoCategory)
		{
		attr = (attr & ~dmRecAttrCategoryMask) | *(UInt16 *) data;
		DmSetRecordInfo (dbP, newIndex, &attr, NULL);
		goto exit;
		}


	// Calculate the size of the changed record. First,
	// find the size of the data used from the old record.
	newSize = sizeof (DateType) + 1;
	newSize += StrLen((Char *) data) + 1;
	c = &src->description;
	cLen = StrLen(c) + 1;
	if (changedField != toDoDescription)
		newSize += cLen;
	if (changedField != toDoNote)
		{
		c += cLen;
		newSize += StrLen(c) + 1;
		}
		

	// Change the description field.
	if (changedField == toDoDescription)
		{
		// If the new description is longer, expand the record.
		curSize = MemPtrSize (src);
		if (newSize > curSize)
			{
			MemPtrUnlock (src);
			err = MemHandleResize (recordH, newSize);
			if (err) return (err);
			src = MemHandleLock (recordH);
			}
		
		// Move the note field.
		offset = sizeof(DateType) + 1 + StrLen (data) + 1;
		c = &src->description;
		c += StrLen(c) + 1;
		DmWrite (src, offset, c, StrLen(c) + 1);
		
		
		// Write the new description field.
		offset = sizeof(DateType) + 1;
		DmStrCopy (src, offset, data);

		// If the new description is shorter, shrink the record.
		if (newSize < curSize)
			MemPtrResize (recordH, newSize);
		goto exit;
		}


	// Change the note field
	if (changedField == toDoNote)
		{
		offset = sizeof(DateType) + 1 + StrLen((Char *)&src->description) + 1;
		MemPtrUnlock (src);
		err = MemHandleResize (recordH, newSize);
		if (err) return (err);

		src = MemHandleLock (recordH);
		DmStrCopy (src, offset, data);
		goto exit;
		}

exit:
	MemPtrUnlock (src);

#if ERROR_CHECK_LEVEL == ERROR_CHECK_FULL
	ECToDoDBValidate (dbP);
#endif

	return 0;
}


/***********************************************************************
 *
 * FUNCTION:     ToDoSetDBBackupBit
 *
 * DESCRIPTION:  This routine sets the backup bit on the given database.
 *					  This is to aid syncs with non Palm software.
 *					  If no DB is given, open the app's default database and set
 *					  the backup bit on it.
 *
 * PARAMETERS:   dbP -	the database to set backup bit,
 *								can be NULL to indicate app's default database
 *
 * RETURNED:     nothing
 *
 * REVISION HISTORY:
 *			Name	Date		Description
 *			----	----		-----------
 *			grant	4/1/99	Initial Revision
 *
 ***********************************************************************/
void ToDoSetDBBackupBit(DmOpenRef dbP)
{
	DmOpenRef localDBP;
	LocalID dbID;
	UInt16 cardNo;
	UInt16 attributes;

	// Open database if necessary. If it doesn't exist, simply exit (don't create it).
	if (dbP == NULL)
		{
		localDBP = DmOpenDatabaseByTypeCreator (toDoDBType, toDo35Creator, dmModeReadWrite);
		if (localDBP == NULL)  return;
		}
	else
		{
		localDBP = dbP;
		}
	
	// now set the backup bit on localDBP
	DmOpenDatabaseInfo(localDBP, &dbID, NULL, NULL, &cardNo, NULL);
	DmDatabaseInfo(cardNo, dbID, NULL, &attributes, NULL, NULL,
				NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	attributes |= dmHdrAttrBackup;
	DmSetDatabaseInfo(cardNo, dbID, NULL, &attributes, NULL, NULL,
				NULL, NULL, NULL, NULL, NULL, NULL, NULL);
	
	// close database if necessary
   if (dbP == NULL) 
   	{
   	DmCloseDatabase(localDBP);
      }
}


/***********************************************************************
 *
 * FUNCTION:     ToDoGetDatabase
 *
 * DESCRIPTION:  Get the application's database.  Open the database if it
 * exists, create it if neccessary.
 *
 * PARAMETERS:   *dbPP - pointer to a database ref (DmOpenRef) to be set
 *					  mode - how to open the database (dmModeReadWrite)
 *
 * RETURNED:     Err - zero if no error, else the error
 *
 * REVISION HISTORY:
 *			Name		Date		Description
 *			----		----		-----------
 *			jmp		10/04/99	Initial Revision
 *
 ***********************************************************************/
Err ToDoGetDatabase (DmOpenRef *dbPP, UInt16 mode)
{
	Err error = 0;
	DmOpenRef dbP;
	UInt16 cardNo;
	LocalID dbID;

	*dbPP = NULL;
  
  // Find the application's data file.  If it doesn't exist create it.
	dbP = DmOpenDatabaseByTypeCreator (toDoDBType, toDo35Creator, mode);
	if (!dbP)
		{
		error = DmCreateDatabase (0, toDoDBName, toDo35Creator, toDoDBType, false);
		if (error)
			return error;
		
		dbP = DmOpenDatabaseByTypeCreator(toDoDBType, toDo35Creator, mode);
		if (!dbP)
			return (1);

		// Set the backup bit.  This is to aid syncs with non Palm software.
		ToDoSetDBBackupBit(dbP);
		
		error = ToDoAppInfoInit (dbP);
      if (error) 
      	{
			DmOpenDatabaseInfo(dbP, &dbID, NULL, NULL, &cardNo, NULL);
      	DmCloseDatabase(dbP);
      	DmDeleteDatabase(cardNo, dbID);
         return error;
         }
		}
	
	*dbPP = dbP;
	return 0;
}

