/*
 * AddrCard.c - Address35 SD-card vCard Import/Export (see AddrCard.h).
 */
#include "AddrCard.h"
#include "CardIO.h"
#include "vcard.h"
#include "AddressRsc.h"		/* frmCardProgress */
#include "Main.h"			/* gShowHiddenFiles */

#define a35ExportBufLen		4096	/* per-contact vCard build buffer */
/* files scanned per Import run - matches MainForm.c's kCardMax (same
 * CardListDir silent-truncation risk for a large folder of split .vcf
 * files); heap-allocated (MemHandleNew), so raising this only costs memory
 * during the Import call itself, not a permanent reservation. */
#define a35MaxImportFiles	256

static Boolean EndsWithVcf(const char *name) SEC001;
static void A35CardSanitize(const char *s, char *out, UInt16 outLen) SEC001;
static void A35CardLeafFromRecord(const A35Record *r, char *out, UInt16 outLen) SEC001;
static Boolean A35CardUniquePath(UInt16 volRef, const char *dir, const char *leaf, char *path) SEC001;
static Err A35CardExportOne(DmOpenRef db, UInt16 volRef, UInt16 index, char *buf) SEC001;

/* Duplicate-filename prompt (frmDupFile), shown from A35CardUniquePath when a
 * write collides with an existing file. "Apply to all" remembers the choice
 * for the REST of the current Split/Export run (gDupApplyAll/gDupChoice,
 * reset at the top of A35CardSplitFile/A35CardExport - each is its own
 * top-level operation). Cancel stops the whole run, not just this one file
 * (matches the Finder-style "Stop"/"Replace"/"Keep Both" convention this
 * dialog is modeled on) - gDupCancelled is checked by both callers' loops. */
enum { a35DupReplace = 0, a35DupKeepBoth = 1, a35DupCancel = 2 };
static Boolean gDupApplyAll;
static UInt8   gDupChoice;
static Boolean gDupCancelled;

static UInt8 A35CardDupPrompt(void) SEC001;
static UInt8 A35CardDupPrompt(void)
{
	FormPtr frm, prev;
	UInt16 cid = btnDupCancel;
	Boolean done = false, applyAll;
	ControlType *cb;

	prev = FrmGetActiveForm();
	frm = FrmInitForm(frmDupFile);
	if (frm == NULL)
		return a35DupCancel;		/* can't show the prompt - safest is to stop */
	FrmSetActiveForm(frm);
	FrmDrawForm(frm);
	cb = (ControlType *) FrmGetObjectPtr(frm, FrmGetObjectIndex(frm, cbDupApplyAll));

	while (!done) {
		EventType ev;
		EvtGetEvent(&ev, evtWaitForever);
		if (ev.eType == appStopEvent) {
			EvtAddEventToQueue(&ev);
			cid = btnDupCancel;
			done = true;
			continue;
		}
		if (ev.eType == ctlSelectEvent) {
			UInt16 id = ev.data.ctlSelect.controlID;
			if (id == btnDupReplace || id == btnDupKeepBoth || id == btnDupCancel) {
				cid = id;
				done = true;
				continue;
			}
		}
		if (SysHandleEvent(&ev)) continue;
		FrmDispatchEvent(&ev);
	}

	applyAll = (Boolean) CtlGetValue(cb);
	/* FrmEraseForm BEFORE FrmDeleteForm - matches every other custom dialog in
	 * this app (EditDialogs.c) and A35ProgressEnd's own convention. Missing
	 * this left the dialog's drawn pixels on screen after teardown (visible
	 * as leftover artifacts, on EVERY exit path including Cancel), and since
	 * nothing had restored the screen, the very next A35ProgressBegin drew
	 * "Processing vCard..." on top of that leftover mess instead of a clean
	 * background. */
	FrmEraseForm(frm);
	FrmDeleteForm(frm);
	if (prev) FrmSetActiveForm(prev);

	if (cid == btnDupReplace) {
		if (applyAll) { gDupApplyAll = true; gDupChoice = a35DupReplace; }
		return a35DupReplace;
	}
	if (cid == btnDupKeepBoth) {
		if (applyAll) { gDupApplyAll = true; gDupChoice = a35DupKeepBoth; }
		return a35DupKeepBoth;
	}
	return a35DupCancel;
}

/* ---- lightweight busy "toast" for operations with no real progress metric
 * (a directory scan, a single-file preview read) - text only, no bar, sized
 * to fit the message. Screen-direct draw (WinSaveBits/WinDrawXxx/
 * WinRestoreBits), matching CryptoNote's CardBusyShow/CardBusyHide exactly
 * (see CardCrypt.c): no form resource, forces the real display window so it
 * centers correctly even right after some other dialog's draw-window context
 * was still active, and needs no window-manager form teardown to dismiss. */
#define a35BusyPadX   10
#define a35BusyH      24
#define a35BusyMarg   4			/* extra saved margin for the frame */

static WinHandle     gBusySaved;
static RectangleType gBusySaveR;

void A35BusyBegin(const char *title)
{
	RectangleType r;
	WinHandle prevDraw;
	Err err;
	Int16 w, len = (Int16) StrLen(title);

	prevDraw = WinGetDrawWindow();
	WinSetDrawWindow(WinGetDisplayWindow());

	FntSetFont(boldFont);
	w = FntCharsWidth((Char *) title, len);
	r.extent.x = (Int16) (w + 2 * a35BusyPadX);
	r.extent.y = a35BusyH;
	r.topLeft.x = (Int16) ((160 - r.extent.x) / 2);
	r.topLeft.y = (Int16) ((160 - r.extent.y) / 2);

	gBusySaveR.topLeft.x = (Int16) (r.topLeft.x - a35BusyMarg);
	gBusySaveR.topLeft.y = (Int16) (r.topLeft.y - a35BusyMarg);
	gBusySaveR.extent.x = (Int16) (r.extent.x + 2 * a35BusyMarg);
	gBusySaveR.extent.y = (Int16) (r.extent.y + 2 * a35BusyMarg);

	gBusySaved = WinSaveBits(&gBusySaveR, &err);
	WinEraseRectangle(&r, 3);			/* rounded-corner erase to match frame */
	WinDrawRectangleFrame(dialogFrame, &r);
	WinDrawChars((Char *) title, len, r.topLeft.x + a35BusyPadX, r.topLeft.y + 6);

	WinSetDrawWindow(prevDraw);
}

void A35BusyEnd(void)
{
	if (gBusySaved) {
		WinHandle prevDraw = WinGetDrawWindow();
		WinSetDrawWindow(WinGetDisplayWindow());
		WinRestoreBits(gBusySaved, gBusySaveR.topLeft.x, gBusySaveR.topLeft.y);
		WinSetDrawWindow(prevDraw);
		gBusySaved = NULL;
	}
}

/* ---- progress dialog for operations that DO have a real metric (splitting
 * or importing multiple vCards) - a real MODAL form so the window manager
 * saves/restores the pixels behind it, same idiom as Sec35's own "Working"
 * progress dialog (sec35.c ProgressBegin/ProgressBar/ProgressEnd): hand-drawn
 * bar, no event pump (callers run a tight synchronous loop). Bar spans nearly
 * the full 156px-wide form. The bar-width MATH lives in vcard.c/main (see
 * VCardProgressFn) - this side only ever draws an already-computed width, so
 * no 32-bit divide runs here in sec001 (multiseg-libgcc-gotcha). */
static FormPtr gProgForm;
static FormPtr gProgPrevForm;
static Int16   gProgLastW = -1;
static UInt32  gProgLastAutoOffTick;

void A35ProgressBegin(const char *title)
{
	RectangleType bar;

	gProgPrevForm = FrmGetActiveForm();
	gProgForm = FrmInitForm(frmCardProgress);
	gProgLastW = -1;
	gProgLastAutoOffTick = 0;		/* forces an immediate first reset below */
	/* FrmInitForm can return NULL under real memory pressure (a real device,
	 * not POSE64, running low - this app's own 4KB arena + 4KB export buffer
	 * per split/import call adds to that pressure). Every other caller in
	 * this pair (A35ProgressBar/A35ProgressEnd) already null-checks gProgForm
	 * and no-ops gracefully, but this function used to dereference it
	 * unconditionally (FrmSetTitle/FrmSetActiveForm/FrmDrawForm on a NULL
	 * FormPtr) - an immediate crash right at the start of a split/import
	 * under exactly the low-memory conditions most likely to trigger it.
	 * Degrade gracefully instead: skip the dialog, let the operation itself
	 * still run (it doesn't depend on the UI succeeding). */
	if (gProgForm == NULL)
		return;
	FrmSetTitle(gProgForm, (char *) title);
	FrmSetActiveForm(gProgForm);
	FrmDrawForm(gProgForm);
	bar.topLeft.x = 6; bar.topLeft.y = 28; bar.extent.x = 144; bar.extent.y = 10;
	WinDrawRectangleFrame(simpleFrame, &bar);
}

void A35ProgressBar(void *ctx, Int16 barWidthPx)
{
	RectangleType fill;
	(void) ctx;

	/* Confirmed the actual cause (2026-08-16): a large split/import runs long
	 * enough (real per-item file I/O, 80+ contacts) to reach the system
	 * auto-off timeout mid-operation with an open file handle and a write in
	 * flight - user reproduced/fixed it by extending the device's auto-off
	 * timeout, isolating this exactly. Same idiom as CryptoNote's own
	 * ProgressCallback ("Reset the auto-off timer to make sure we don't fall
	 * asleep in the middle of the update"): reset unconditionally, before the
	 * early-return below, so it fires on every ~256-byte stream refill (the
	 * most frequent available heartbeat) rather than only on visible
	 * pixel-width changes.
	 *
	 * REVERTED (2026-08-16): a prior version of this drain also detected
	 * appStopEvent specifically and EvtAddEventToQueue'd it back (+ set an
	 * abort flag the split/import loops checked), on the theory that a plain
	 * discard would starve StopApplication of ever seeing the quit event.
	 * That introduced a NEW real-device "fatal exception on exiting after an
	 * import" regression that didn't exist before it was added. Checked
	 * CryptoNote's own equivalent (MainForm.c ProgressCallback/
	 * ProgressHandleEvent, the exact pattern this was modeled on) side by
	 * side: CryptoNote's progress-callback drain does NOT special-case
	 * appStopEvent at all - EvtGetEvent(0), return event.eType, nothing else -
	 * only its MODAL DIALOG loops (waiting on user input, nothing in flight)
	 * requeue+break on appStopEvent, matching this app's own EditDialogs.c
	 * convention. A progress callback runs during active, synchronous I/O,
	 * not while idle waiting for input, so CryptoNote deliberately does NOT
	 * try to abort there - it just keeps going and drops the event, and the
	 * user's next Applications-button press (after the operation naturally
	 * finishes) is what actually triggers the quit. Reverted to match that
	 * exact proven behavior instead of inventing a new one.
	 *
	 * THROTTLED (2026-08-16): this callback fires on every ~256-byte stream
	 * refill - for an 80-contact/51KB split that's on the order of 100-200
	 * calls, each one calling EvtResetAutoOffTimer(). User's hypothesis after
	 * the appStopEvent revert still didn't fix a real-device "exit crashes
	 * after a split+view, but every other in-app action afterward is fine":
	 * calling this SysTrap so often (not just draining events - the actual
	 * auto-off-timer reset itself) is the suspect, not anything about the
	 * split/view/DB code, since nothing else in the app is affected. The
	 * system's minimum sleep timeout is 30s (user-confirmed), so resetting
	 * every 15s keeps a comfortable 2x safety margin under that floor while
	 * cutting the SysTrap call count from 100-200/split down to a handful -
	 * still draining events (the cheap, non-trapping part) on every callback
	 * as before. */
	{
		UInt32 now = TimGetTicks();
		if (now - gProgLastAutoOffTick >= (UInt32) SysTicksPerSecond() * 15) {
			EvtResetAutoOffTimer();
			gProgLastAutoOffTick = now;
		}
	}
	{
		EventType ev;
		while (EvtGetEvent(&ev, 0), ev.eType != nilEvent) { /* drain, don't dispatch */ }
	}

	if (gProgForm == NULL || barWidthPx == gProgLastW)
		return;
	gProgLastW = barWidthPx;
	fill.topLeft.x = 7; fill.topLeft.y = 29;
	fill.extent.x = barWidthPx; fill.extent.y = 8;
	WinDrawRectangle(&fill, 0);
}

void A35ProgressEnd(void)
{
	if (gProgForm == NULL)
		return;
	FrmEraseForm(gProgForm);		/* restores the pixels behind -> no artifacts */
	if (gProgPrevForm) FrmSetActiveForm(gProgPrevForm);
	FrmDeleteForm(gProgForm);
	gProgForm = NULL;
}

/* case-insensitive ".vcf" suffix test */
static Boolean EndsWithVcf(const char *name)
{
	UInt16 n = (UInt16) StrLen(name);
	if (n < 4) return false;
	name += n - 4;
	return (Boolean) ((name[0] == '.') &&
		(name[1] == 'v' || name[1] == 'V') &&
		(name[2] == 'c' || name[2] == 'C') &&
		(name[3] == 'f' || name[3] == 'F'));
}

/* sanitize s -> out: lowercase, keep alnum/_/- , space->_ (>=1 char) */
static void A35CardSanitize(const char *s, char *out, UInt16 outLen)
{
	UInt16 o = 0, i;
	for (i = 0; s[i] && o < (UInt16) (outLen - 1); i++) {
		char c = s[i];
		if (c == ' ') c = '_';
		else if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
		if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
			c == '_' || c == '-')
			out[o++] = c;
	}
	out[o] = '\0';
	if (!out[0]) StrCopy(out, "contact");
}

/* build a safe "firstname_lastname" (or company/nickname) leaf from a record */
static void A35CardLeafFromRecord(const A35Record *r, char *out, UInt16 outLen)
{
	const char *first = A35GetStr(r, a35fFirst);
	const char *last = A35GetStr(r, a35fLast);
	const char *comp = A35GetStr(r, a35fCompany);
	char nm[96];

	nm[0] = '\0';
	if (first[0] || last[0]) {
		StrNCopy(nm, first, sizeof(nm) - 1); nm[sizeof(nm) - 1] = '\0';
		if (first[0] && last[0]) StrNCat(nm, "_", sizeof(nm));
		StrNCat(nm, last, sizeof(nm));
	} else if (comp[0]) {
		StrNCopy(nm, comp, sizeof(nm) - 1); nm[sizeof(nm) - 1] = '\0';
	} else {
		StrNCopy(nm, A35GetStr(r, a35fNickname), sizeof(nm) - 1);
		nm[sizeof(nm) - 1] = '\0';
	}
	A35CardSanitize(nm, out, outLen);
}

/* <dir>/<leaf>[_N].vcf, prompting on a real collision. Returns false if the
 * user cancelled (path is left pointing at the colliding name - caller must
 * not write to it); true otherwise (path is ready to write to). */
static Boolean A35CardUniquePath(UInt16 volRef, const char *dir, const char *leaf,
	char *path)
{
	char cand[cardMaxName];
	char base[cardMaxName];
	UInt16 n;

	/* leaf can be up to cardMaxName-1 (63) chars - A35CardLeafFromRecord's own
	 * cap - but StrPrintF is UNBOUNDED (no size arg, unlike snprintf), so
	 * appending "_NN.vcf" onto an already-near-max leaf overflows this
	 * cardMaxName-byte cand[]: a real, confirmed stack overflow, and it gets
	 * WORSE the more duplicate leaf names already exist on the card (re-
	 * splitting/re-exporting the same group repeatedly pushes N higher each
	 * time, needing the same amount of headroom every time) - can even
	 * overflow on the very first candidate (N=1) if the leaf alone is long
	 * enough. Truncate the leaf here so leaf + "_99.vcf" + NUL always fits. */
	StrNCopy(base, leaf, sizeof(base) - 8);	/* room for "_99.vcf\0" = 8 */
	base[sizeof(base) - 8] = '\0';

	StrPrintF(cand, "%s.vcf", base);
	CardJoinPath(path, dir, cand);
	if (!CardExists(volRef, path))
		return true;					/* no collision - the common case */

	{
		UInt8 choice = gDupApplyAll ? gDupChoice : A35CardDupPrompt();

		if (choice == a35DupCancel) {
			gDupCancelled = true;
			return false;
		}
		if (choice == a35DupReplace)
			return true;			/* path already points at the exact collision - overwrite it */
	}

	/* Keep Both: find the next free "_N" suffix */
	for (n = 2; n <= 99; n++) {
		StrPrintF(cand, "%s_%d.vcf", base, (int) n);
		CardJoinPath(path, dir, cand);
		if (!CardExists(volRef, path)) return true;
	}
	/* give up de-duping after 99: overwrite the "_99" name */
	return true;
}

Err A35CardInit(UInt16 volRef)
{
	Err err;
	err = CardEnsureDir(volRef, a35CardRoot);
	if (err) return err;
	CardEnsureDir(volRef, a35CardImportDir);
	CardEnsureDir(volRef, a35CardExportDir);
	return errNone;
}

static Err A35CardExportOne(DmOpenRef db, UInt16 volRef, UInt16 index, char *buf)
{
	A35Record rec;
	MemHandle h;
	UInt16 len;
	char leaf[cardMaxName], path[cardMaxPath];

	if (A35GetRecord(db, index, &rec, &h) != errNone)
		return dmErrIndexOutOfRange;
	len = A35RecordToVCard(db, &rec, buf, a35ExportBufLen);
	A35CardLeafFromRecord(&rec, leaf, sizeof(leaf));
	A35ReleaseRecord(h);
	if (len == 0)
		return errNone;						/* empty -> skip, not an error */
	if (!A35CardUniquePath(volRef, a35CardExportDir, leaf, path))
		return dmErrCantOpen;			/* user cancelled - caller checks gDupCancelled */
	return CardWriteFile(volRef, path, buf, len);
}

Err A35CardExport(DmOpenRef db, UInt16 volRef, UInt16 scope,
	UInt16 category, UInt16 tagId, UInt16 recIndex, UInt16 *countP)
{
	char *buf;
	UInt16 count = 0;
	Err err;

	*countP = 0;
	err = A35CardInit(volRef);
	if (err) return err;

	buf = (char *) MemPtrNew(a35ExportBufLen);
	if (buf == NULL) return memErrNotEnoughSpace;

	gDupApplyAll = false;			/* fresh duplicate-choice state for this run */
	gDupCancelled = false;

	/* Text-only busy toast (auto-dismisses via A35BusyEnd below, no user
	 * interaction needed) - matches Import's own "check if there is one"
	 * parity ask, and this app's own documented split between the two busy
	 * idioms: A35Busy* is for operations with no REAL progress metric
	 * (A35ProgressBegin's own comment, right above it in this file), which
	 * is what Export is - unlike Import/Split, it never streams a single
	 * large file through VCardStream (the only place this app currently
	 * computes a proportional bar width, deliberately kept in vcard.c's
	 * main segment - see multiseg-libgcc-gotcha - to avoid a 32-bit divide
	 * running from this file's own SEC001 tag), it writes one small file
	 * per DB record. */
	A35BusyBegin("Exporting...");

	if (scope == a35ExpRecord) {
		if (A35CardExportOne(db, volRef, recIndex, buf) == errNone) count++;
	} else {
		UInt16 total = DmNumRecords(db), i;
		for (i = 0; i < total; i++) {
			Boolean match = false;
			if (scope == a35ExpAll) {
				match = true;
			} else if (scope == a35ExpCategory) {
				UInt16 attr;
				if (DmRecordInfo(db, i, &attr, NULL, NULL) == errNone &&
					(UInt16) (attr & dmRecAttrCategoryMask) == category)
					match = true;
			} else if (scope == a35ExpTagged) {
				A35Record rec;
				MemHandle h;
				if (A35GetRecord(db, i, &rec, &h) == errNone) {
					match = A35RecordHasTag(&rec, tagId);
					A35ReleaseRecord(h);
				}
			}
			if (match && A35CardExportOne(db, volRef, i, buf) == errNone)
				count++;
			if (gDupCancelled)
				break;			/* Cancel stops the whole export run */
		}
	}

	A35BusyEnd();

	MemPtrFree(buf);
	*countP = count;
	return errNone;
}

Err A35CardImport(DmOpenRef db, UInt16 volRef, UInt16 *countP)
{
	CardEntry *entries;
	MemHandle eh;
	VCardArena *arena;
	UInt16 n = 0, i, count = 0;
	Err err;

	*countP = 0;
	eh = MemHandleNew((UInt32) sizeof(CardEntry) * a35MaxImportFiles);
	if (eh == NULL) return memErrNotEnoughSpace;
	entries = (CardEntry *) MemHandleLock(eh);

	err = CardListDir(volRef, a35CardImportDir, entries, a35MaxImportFiles, &n, NULL);
	if (err != errNone) {
		MemHandleUnlock(eh); MemHandleFree(eh);
		return err;
	}

	arena = (VCardArena *) MemPtrNew(sizeof(VCardArena));
	if (arena == NULL) {
		MemHandleUnlock(eh); MemHandleFree(eh);
		return memErrNotEnoughSpace;
	}

	if (n > 0) A35ProgressBegin("Importing...");
	for (i = 0; i < n; i++) {
		char path[cardMaxPath];
		VCardStream stream;

		if (entries[i].isDir || !EndsWithVcf(entries[i].name))
			continue;
		if (!gShowHiddenFiles && entries[i].name[0] == '.')
			continue;			/* AppleDouble "._foo.vcf" resource fork, not a real vCard */
		CardJoinPath(path, a35CardImportDir, entries[i].name);
		/* streamed (not read whole-file): a real vCard with an embedded PHOTO
		 * can be 50-100KB+, and a single big allocation was observed to fail
		 * on real hardware even though it parses fine with unlimited host RAM */
		if (A35VCardOpenStream(&stream, volRef, path) == errNone) {
			A35Record r;
			UInt16 idx;
			A35VCardSetProgress(&stream, A35ProgressBar, NULL);
			while (A35VCardStreamNext(db, &stream, &r, arena)) {
				if (A35RecordHasData(&r)) {
					if (A35NewRecord(db, &r, &idx) == errNone) {
						A35EncryptRecordInPlace(db, idx);	/* seal (foreground import) */
						count++;
					}
				}
			}
			A35VCardCloseStream(&stream);
		}
	}
	if (n > 0) A35ProgressEnd();

	MemPtrFree(arena);
	MemHandleUnlock(eh);
	MemHandleFree(eh);
	*countP = count;
	return errNone;
}

/* ----------------------------------------------------------- Card browse */

/* How many vCards are packed into the file at path (0 on read failure or an
 * empty file). STREAMED (A35VCardOpenStream/StreamNext, 256-byte rolling
 * buffer) - NOT a whole-file CardReadFile: a group vCard can hold 50-80+
 * contacts (50KB+), and a single big allocation was confirmed to fail on
 * real memory-constrained hardware (2026-08-16: a 21-contact/7KB file split
 * fine, an 80-contact/51KB file did not - textbook large-allocation failure,
 * same class of bug the Import path's own streaming reader already avoids -
 * this function just wasn't using it before). */
UInt16 A35CardCountAt(UInt16 volRef, DmOpenRef db, const char *path)
{
	VCardStream stream;
	VCardArena *arena;
	UInt16 n = 0;

	if (A35VCardOpenStream(&stream, volRef, path) != errNone)
		return 0;
	arena = (VCardArena *) MemPtrNew(sizeof(VCardArena));
	if (arena == NULL) { A35VCardCloseStream(&stream); return 0; }
	{
		A35Record r;
		while (A35VCardStreamNext(db, &stream, &r, arena))
			if (A35RecordHasData(&r)) n++;
	}
	MemPtrFree(arena);
	A35VCardCloseStream(&stream);
	return n;
}

/* Split the (already-confirmed) multi-person file at path into one
 * "firstname_lastname[_N].vcf" per person, written into dir (the file's own
 * folder), then delete the original. Returns the number of people written (0
 * on failure, or if path turned out to hold only one person - left untouched
 * in that case). Caller-driven (a user tap + confirm), not an automatic scan -
 * see [[address35-vcard-macos-contacts-compat]]-adjacent project memory for
 * why the old auto-split-on-browse behavior was removed.
 *
 * STREAMED, same reasoning as A35CardCountAt above: each person is parsed one
 * at a time (A35VCardStreamNext, bounded 256-byte buffer) and RE-SERIALIZED
 * via A35RecordToVCard (the same bounded a35ExportBufLen=4096 buffer regular
 * contact export already uses) instead of copying raw byte ranges out of a
 * whole-file buffer - this also means a photo/unsupported-field-bearing
 * source card comes out the other side already normalized the same way any
 * other Address35 export is, rather than needing separate handling.
 *
 * `total16` is the caller's ALREADY-COMPUTED count (from its own pre-split
 * A35CardCountAt confirm-prompt check) - this function used to redundantly
 * recount internally, meaning every split opened+fully-streamed the same
 * card file TWICE before ever writing anything (once for the caller's
 * confirm prompt, once again here). Real SD-card VFS drivers can be flaky
 * about rapid repeated open/close of the same path on actual hardware
 * (unlike a simulator); passing the count in cuts that to one open here
 * instead of two, regardless of whether that flakiness is confirmed as a
 * cause of anything - it's a real, unconditional I/O reduction either way. */
UInt16 A35CardSplitFile(UInt16 volRef, DmOpenRef db, const char *dir, const char *path,
	UInt16 total16)
{
	VCardStream stream;
	VCardArena *arena;
	char *ebuf;
	UInt16 written = 0;

	if (total16 <= 1) return 0;			/* nothing to do */

	gDupApplyAll = false;			/* fresh duplicate-choice state for this run */
	gDupCancelled = false;

	if (A35VCardOpenStream(&stream, volRef, path) != errNone)
		return 0;
	arena = (VCardArena *) MemPtrNew(sizeof(VCardArena));
	if (arena == NULL) { A35VCardCloseStream(&stream); return 0; }
	ebuf = (char *) MemPtrNew(a35ExportBufLen);
	if (ebuf == NULL) { MemPtrFree(arena); A35VCardCloseStream(&stream); return 0; }
	{
		/* r is heap-allocated, not a stack local: A35VCardStreamNext (the
		 * parser, its own sizable locals) is immediately followed here by
		 * A35RecordToVCard (the exporter, ~900B of ITS OWN locals) every
		 * iteration - a combination no other caller in this app exercises
		 * (Import never re-exports; Export never re-parses), so this stays
		 * off the stack rather than adding another ~400B on top of an
		 * already-deep event-handler call chain on real, stack-constrained
		 * hardware. */
		A35Record *rp = (A35Record *) MemPtrNew(sizeof(A35Record));
		if (rp == NULL) {
			MemPtrFree(ebuf); MemPtrFree(arena); A35VCardCloseStream(&stream);
			return 0;
		}

		A35VCardSetProgress(&stream, A35ProgressBar, NULL);	/* byte-based, computed in vcard.c/main */
		while (A35VCardStreamNext(db, &stream, rp, arena)) {
			char leaf[cardMaxName], npath[cardMaxPath];
			UInt16 len;

			if (!A35RecordHasData(rp))
				continue;			/* empty/garbage sub-card: skip, doesn't count */
			len = A35RecordToVCard(db, rp, ebuf, a35ExportBufLen);
			if (len == 0)
				continue;			/* re-serialize overflowed the buffer: skip, doesn't count */
			A35CardLeafFromRecord(rp, leaf, sizeof(leaf));
			if (!A35CardUniquePath(volRef, dir, leaf, npath))
				break;			/* user cancelled the duplicate-filename prompt */
			/* CardWriteFile's result was never checked here before - a silent
			 * write failure still let `written` reach total16 (the loop ran
			 * regardless), which fired the "delete the original" gate below
			 * even though nothing new actually landed on the card. Stop on
			 * the first failure instead of pretending it succeeded. */
			if (CardWriteFile(volRef, npath, ebuf, len) != errNone)
				break;
			written++;
		}
		MemPtrFree(rp);
	}
	MemPtrFree(ebuf);
	MemPtrFree(arena);
	A35VCardCloseStream(&stream);
	/* Only drop the original once EVERY person was actually written back out -
	 * a partial split (written < total16) leaves the original untouched
	 * instead of silently losing whoever wasn't captured while ALSO leaving a
	 * partial set of new files behind. A re-tap just retries. */
	if (written == total16)
		CardDeleteFile(volRef, path);		/* drop the original multi-person file */
	return written;
}

Err A35CardBrowseList(UInt16 volRef, DmOpenRef db, A35CardItem *items,
	UInt16 max, UInt16 *countP)
{
	CardEntry *entries;
	MemHandle eh;
	VCardArena *arena;
	UInt16 n = 0, i, out = 0;
	Err err;

	*countP = 0;
	eh = MemHandleNew((UInt32) sizeof(CardEntry) * a35MaxImportFiles);
	if (eh == NULL) return memErrNotEnoughSpace;
	entries = (CardEntry *) MemHandleLock(eh);

	err = CardListDir(volRef, a35CardRoot, entries, a35MaxImportFiles, &n, NULL);
	if (err != errNone) { MemHandleUnlock(eh); MemHandleFree(eh); return err; }

	arena = (VCardArena *) MemPtrNew(sizeof(VCardArena));
	if (arena == NULL) { MemHandleUnlock(eh); MemHandleFree(eh); return memErrNotEnoughSpace; }

	for (i = 0; i < n && out < max; i++) {
		char path[cardMaxPath];
		VCardStream stream;

		if (entries[i].isDir || !EndsWithVcf(entries[i].name))
			continue;						/* Import/ & Export/ are dirs -> skipped */
		CardJoinPath(path, a35CardRoot, entries[i].name);
		if (A35VCardOpenStream(&stream, volRef, path) == errNone) {
			A35Record r;
			if (A35VCardStreamNext(db, &stream, &r, arena)) {
				A35VCardNameFromRecord(&r, items[out].name, sizeof(items[out].name));
				StrNCopy(items[out].file, entries[i].name, cardMaxName - 1);
				items[out].file[cardMaxName - 1] = '\0';
				out++;
			}
			A35VCardCloseStream(&stream);
		}
	}

	MemPtrFree(arena);
	MemHandleUnlock(eh);
	MemHandleFree(eh);
	*countP = out;
	return errNone;
}

/* Caller owns the "Reading..." indicator (MainCardTap shows it continuously
 * across the count-check AND this read, so there's no gap where a long file
 * feels hung - see gCardDirValid's neighbor comments in MainForm.c). */
Err A35CardReadContact(UInt16 volRef, DmOpenRef db, const char *path,
	A35Record *r, VCardArena *arena)
{
	VCardStream stream;
	Boolean ok;

	if (A35VCardOpenStream(&stream, volRef, path) != errNone)
		return dmErrCantFind;
	/* NB: r's field values point into *arena, which the caller keeps alive. */
	ok = A35VCardStreamNext(db, &stream, r, arena);
	A35VCardCloseStream(&stream);
	return ok ? errNone : dmErrCantFind;
}
