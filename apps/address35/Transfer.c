/*
 * Transfer.c - Address35 vCard beam send + receive (see Transfer.h).
 */
#include "Transfer.h"
#include "AddressDB35.h"
#include "vcard.h"

#define a35VcardMax		4096		/* per-record vCard build buffer */

/* ------------------------------------------------------------------- send */

/* push the whole buffer through the exg socket */
SEC001 static Err ExgSendAll(ExgSocketPtr s, const char *buf, UInt16 len)
{
	UInt16 sent = 0;
	Err err = 0;
	while (sent < len && !err) {
		UInt32 n = ExgSend(s, buf + sent, (UInt32) (len - sent), &err);
		if (n == 0) break;
		sent = (UInt16) (sent + n);
	}
	return err;
}

void A35BeamRecord(DmOpenRef db, UInt16 index)
{
	A35Record rec;
	MemHandle h;
	char *vc;
	UInt16 vclen;
	Err err;
	ExgSocketType sock;
	char desc[64], fname[80];

	if (A35GetRecord(db, index, &rec, &h) != errNone)	/* invalid index -> err */
		return;
	vc = (char *) MemPtrNew(a35VcardMax);
	if (vc == NULL) { A35ReleaseRecord(h); return; }
	vclen = A35RecordToVCard(db, &rec, vc, a35VcardMax);
	A35RecordDisplayName(db, index, desc, sizeof(desc));
	A35ReleaseRecord(h);
	if (vclen == 0) { MemPtrFree(vc); return; }

	if (!desc[0]) StrCopy(desc, "Contact");
	StrCopy(fname, desc);
	StrCat(fname, ".");
	StrCat(fname, a35VcardExt);

	MemSet(&sock, sizeof(sock), 0);
	sock.description = desc;
	sock.name = fname;
	sock.length = vclen;
	sock.target = a35Creator;
	sock.type = (char *) a35VcardMime;

	err = ExgPut(&sock);
	if (!err) {
		err = ExgSendAll(&sock, vc, vclen);
		ExgDisconnect(&sock, err);
	}
	MemPtrFree(vc);
}

void A35BeamCategory(DmOpenRef db, UInt16 category)
{
	ExgSocketType sock;
	Err err;
	UInt16 index = 0;
	char desc[dmCategoryLength + 4], fname[dmCategoryLength + 8];
	char *vc;
	Boolean any = false;

	/* need at least one queryable record in the category */
	index = 0;
	while (DmSeekRecordInCategory(db, &index, 0, dmSeekForward, category) == errNone) {
		if (DmQueryRecord(db, index) != NULL) { any = true; break; }
		index++;
	}
	if (!any) return;

	if (category == dmAllCategories) StrCopy(desc, "All");
	else CategoryGetName(db, category, desc);
	if (!desc[0]) StrCopy(desc, "Contacts");
	StrCopy(fname, desc);
	StrCat(fname, ".");
	StrCat(fname, a35VcardExt);

	MemSet(&sock, sizeof(sock), 0);
	sock.description = desc;
	sock.name = fname;
	sock.length = 0;
	sock.target = a35Creator;
	sock.type = (char *) a35VcardMime;

	err = ExgPut(&sock);
	if (err) return;

	vc = (char *) MemPtrNew(a35VcardMax);
	if (vc == NULL) { ExgDisconnect(&sock, exgMemError); return; }

	index = 0;
	while (!err &&
		DmSeekRecordInCategory(db, &index, 0, dmSeekForward, category) == errNone) {
		A35Record rec;
		MemHandle h;
		if (A35GetRecord(db, index, &rec, &h) == errNone) {
			UInt16 vclen = A35RecordToVCard(db, &rec, vc, a35VcardMax);
			A35ReleaseRecord(h);
			if (vclen) err = ExgSendAll(&sock, vc, vclen);
		}
		index++;
	}
	MemPtrFree(vc);
	ExgDisconnect(&sock, err);
}

/* ---------------------------------------------------------------- receive */

void A35ExgRegister(void)
{
	ExgRegisterData(a35Creator, exgRegExtensionID, a35VcardExt);
	ExgRegisterData(a35Creator, exgRegTypeID, a35VcardMime);
}

/* Beam ACCEPT dialog (sysAppLaunchCmdExgAskUser): let the Exchange Manager ask the user
 * whether to accept AND which category to file the incoming contact(s) under (the picker
 * lets them create a new one).  We stash the chosen category in the socket's appData; the
 * receive handler carries it into the inbox and the merge files every contact there.  Runs
 * as a sublaunch -> MAIN, and touches no app globals (dbP is passed in).  Mirrors ToDo35. */
SEC_RECV Err A35AcceptBeamDialog(DmOpenRef dbP, ExgAskParamPtr askInfoP)
{
	ExgDialogInfoType exgInfo;
	Err err;
	Boolean result;

	exgInfo.categoryIndex = dmUnfiledCategory;		/* default if the dialog has no picker */
	exgInfo.db = dbP;
	result = ExgDoDialog(askInfoP->socketP, &exgInfo, &err);
	if (!err && result) {
		askInfoP->result = exgAskOk;
		askInfoP->socketP->appData = exgInfo.categoryIndex;	/* -> inbox -> merge */
	} else {
		askInfoP->result = exgAskCancel;
	}
	return err;
}

#define a35BeamRecMax	60000UL		/* one inbox record, safely under the ~64KB chunk limit */
#define a35BeamCarry	16			/* tail kept while skipping, so END: can span reads */

/* Offset just past the newline ending an "END:" line in b[0..n) (first match, or the
 * last one if `last`), or 0 if none has fully arrived yet. Compared char-by-char, not
 * against a string literal: literals are globals, and this runs in the no-globals
 * beam sublaunch. A vCard's only line-start "END:" is its END:VCARD. */
static UInt32 A35CardEndAt(const char *b, UInt32 n, Boolean last)
{
	UInt32 i, j, hit = 0;
	for (i = 0; i + 4 <= n; i++) {
		if ((i == 0 || b[i - 1] == '\n') && (b[i] | 0x20) == 'e' &&
			(b[i + 1] | 0x20) == 'n' && (b[i + 2] | 0x20) == 'd' && b[i + 3] == ':') {
			for (j = i + 4; j < n && b[j] != '\n'; j++)
				;
			if (j == n) break;
			hit = j + 1;
			if (!last) break;
			i = j;
		}
	}
	return hit;
}

/* Beam receive. Runs as a NO-GLOBALS sublaunch (app not running) where globals, the
 * session key, the goto/display path AND the vCard parser's static tables are all
 * off-limits. So we do the absolute minimum: drain the socket into a buffer and store
 * those RAW bytes, unparsed, in the Address35DBPlain inbox. Pure Exg + Mem + DB I/O;
 * no globals, no parsing. A35MergePlainDB parses them on the next full launch. `pdb`
 * is the already-open inbox.
 *
 * A record can't exceed ~64KB, so a large beam (e.g. a whole category) is stored as
 * several records of at most a35BeamRecMax, each cut at a vCard boundary. A single
 * card bigger than that (a huge embedded PHOTO) keeps its first a35BeamRecMax bytes
 * and drops the rest: the parser tolerates a missing END:VCARD and discards PHOTO
 * data anyway, so only fields placed after the photo are lost. */
Err A35ReceiveData(DmOpenRef pdb, ExgSocketPtr sock)
{
	Err err;
	UInt32 cap = 4096, total = 0, got, e;
	UInt16 cat = (UInt16) (sock->appData & dmRecAttrCategoryMask);	/* accept-dialog pick */
	Boolean skip = false;			/* discarding the tail of one over-size card */
	MemHandle bufH;
	char *buf;

	err = ExgAccept(sock);
	if (err) return err;

	bufH = MemHandleNew(cap);
	if (bufH == NULL) { ExgDisconnect(sock, exgMemError); return exgMemError; }
	buf = (char *) MemHandleLock(bufH);

	for (;;) {
		if (total >= cap && cap < a35BeamRecMax) {		/* grow */
			UInt32 nc = (cap * 2 > a35BeamRecMax) ? a35BeamRecMax : cap * 2;
			MemHandleUnlock(bufH);
			if (MemHandleResize(bufH, nc) == errNone) cap = nc;
			buf = (char *) MemHandleLock(bufH);
		}
		if (total >= cap) {			/* full (at the max, or heap too tight to grow) */
			e = A35CardEndAt(buf, total, true);
			if (e) {
				A35InboxAppendRaw(pdb, buf, e, cat);	/* whole cards; keep the partial tail */
			} else {
				A35InboxAppendRaw(pdb, buf, total, cat);	/* over-size card's head */
				skip = true;
				e = total - a35BeamCarry;
			}
			MemMove(buf, buf + e, (Int32) (total - e));
			total -= e;
		}
		got = ExgReceive(sock, buf + total, cap - total, &err);
		if (err || got == 0) break;
		total += got;
		if (skip) {		/* scan only the new bytes, keeping just a short tail between reads */
			e = A35CardEndAt(buf, total, false);
			if (e) skip = false;						/* over-size card ended */
			else if (total > a35BeamCarry) e = total - a35BeamCarry;
			MemMove(buf, buf + e, (Int32) (total - e));
			total -= e;
		}
	}

	/* Every beamed contact is filed into `cat` at merge time. Do NOT parse here. */
	if (!skip) A35InboxAppendRaw(pdb, buf, total, cat);

	MemHandleUnlock(bufH);
	MemHandleFree(bufH);
	ExgDisconnect(sock, err);
	return err;
}

/* Merge the inbox: parse each raw record's vCard(s) and add them to mainDB, then
 * discard the inbox. Runs on a full launch (globals present) so the parser + record
 * builder are safe. Sealing is left to A35SealAllPlaintext (called right after). */
SEC002 void A35MergePlainDB(DmOpenRef mainDB)
{
	LocalID id = DmFindDatabase(0, a35PlainDBName);
	DmOpenRef pdb;
	VCardArena *arena;

	if (id == 0 || mainDB == NULL)
		return;								/* nothing beamed in */
	pdb = DmOpenDatabase(0, id, dmModeReadOnly);
	if (pdb == NULL)
		return;

	arena = (VCardArena *) MemPtrNew(sizeof(VCardArena));
	if (arena != NULL) {
		UInt16 i, n = DmNumRecords(pdb);
		for (i = 0; i < n; i++) {
			MemHandle h = DmQueryRecord(pdb, i);
			if (h != NULL) {
				const char *raw = (const char *) MemHandleLock(h);
				UInt32 len = MemHandleSize(h), off = 0, span;
				UInt16 icat = 0;
				DmRecordInfo(pdb, i, &icat, NULL, NULL);	/* receive-time category pick */
				icat &= dmRecAttrCategoryMask;
				while (off < len) {			/* a record may hold several vCards */
					A35Record r;
					UInt16 idx;
					span = A35VCardSpan(raw + off, len - off);
					if (span == 0) break;
					if (A35VCardToRecord(mainDB, raw + off, span, &r, arena) &&
						A35RecordHasData(&r)) {
						A35NewRecord(mainDB, &r, &idx);
						/* File the beamed contact into the category the receiver chose. */
						if (icat) {
							UInt16 attr = 0;
							DmRecordInfo(mainDB, idx, &attr, NULL, NULL);
							attr = (UInt16)((attr & ~dmRecAttrCategoryMask) | icat);
							DmSetRecordInfo(mainDB, idx, &attr, NULL);
						}
					}
					off += span;
				}
				MemHandleUnlock(h);
			}
		}
		MemPtrFree(arena);
	}
	DmCloseDatabase(pdb);
	DmDeleteDatabase(0, id);				/* consumed -> discard the inbox */
}
