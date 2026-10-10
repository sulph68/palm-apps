/* ExgTest - drives Bkp1's MyExgDBWrite/MyExgDBRead through a memory buffer (no card needed)
 * against the real Data Manager. Each launch runs the next case (counter in unsaved prefs):
 * build a record DB from a pattern, export, import, compare. A=live, N=deleted stub,
 * R=archived (deleted flag, data kept). Case = pattern*2 + (1 if the DB has an AppInfo block). */
#include <PalmOS.h>
#include "ExgDB.h"

#define CRID 'ExgT'
#ifndef START
#define START 0
#endif
#define BUFSZ 24000UL
static const char *const kPat[] = { "AAA", "AAN", "AANN", "ANA", "NAA", "AANA", "ARA", "AAR", "RAA",
	"N", "NN", "A", "NA", "AN", "ANNARNA", "RNRA", "AARN" };
#define NPAT (sizeof(kPat) / sizeof(kPat[0]))
#define MAXREC 8

static UInt8 *gBuf; static UInt32 gLen, gPos;

static Err wr(const void *p, UInt32 *sz, void *u)
{
	if (gLen + *sz > BUFSZ) return memErrNotEnoughSpace;
	MemMove(gBuf + gLen, p, *sz); gLen += *sz; return errNone;
}
static Err rd(void *p, UInt32 *sz, void *u)
{
	UInt32 n = gLen - gPos; if (n > *sz) n = *sz;
	MemMove(p, gBuf + gPos, n); gPos += n; *sz = n; return errNone;
}
static UInt32 recSize(UInt16 i) { return 20 + 700UL * i; }

static void runCase(UInt16 c, char *msg)
{
	const char *pat = kPat[c / 2]; Boolean ai = c & 1;
	UInt16 n = StrLen(pat), i, attr[MAXREC], a; UInt32 uid[MAXREC], u, j; LocalID id, chunk;
	DmOpenRef db; Err e; Boolean reset; UInt8 *p; MemHandle h;
	static const char name[] = "ExgTestDB";

	if ((id = DmFindDatabase(0, name)) != 0) DmDeleteDatabase(0, id);
	DmCreateDatabase(0, name, CRID, 'DATA', false);
	id = DmFindDatabase(0, name); db = DmOpenDatabase(0, id, dmModeReadWrite);
	if (ai) {
		h = DmNewHandle(db, 300); chunk = MemHandleToLocalID(h);
		p = MemHandleLock(h); DmSet(p, 0, 300, 'i'); MemHandleUnlock(h);
		DmSetDatabaseInfo(0, id, 0, 0, 0, 0, 0, 0, 0, &chunk, 0, 0, 0);
	}
	for (i = 0; i < n; i++) {
		UInt16 at = dmMaxRecordIndex;
		h = DmNewRecord(db, &at, recSize(i));
		p = MemHandleLock(h); DmSet(p, 0, recSize(i), (UInt8) ('a' + i)); MemHandleUnlock(h);
		DmReleaseRecord(db, at, true);
	}
	for (i = 0; i < n; i++) {
		if (pat[i] == 'N') DmDeleteRecord(db, i);
		if (pat[i] == 'R') DmArchiveRecord(db, i);
		DmRecordInfo(db, i, &attr[i], &uid[i], 0);
	}
	DmCloseDatabase(db);

	gLen = gPos = 0;
	e = MyExgDBWrite(wr, 0, 0, id, 0);
	if (e) { StrPrintF(msg, "c%d %s ai%d FAIL export err %x", c, pat, ai, e); return; }
	e = MyExgDBRead(rd, 0, 0, &id, 0, &reset, true);
	if (e) { StrPrintF(msg, "c%d %s ai%d FAIL import err %x", c, pat, ai, e); return; }

	db = DmOpenDatabase(0, id, dmModeReadOnly);
	if (DmNumRecords(db) != n) { StrPrintF(msg, "c%d %s ai%d FAIL count %d", c, pat, ai, DmNumRecords(db)); goto out; }
	for (i = 0; i < n; i++) {
		DmRecordInfo(db, i, &a, &u, &chunk);
		if (u != uid[i]) { StrPrintF(msg, "c%d %s ai%d FAIL rec %d uid %lx want %lx (order)", c, pat, ai, i, u, uid[i]); goto out; }
		if ((a & ~dmRecAttrBusy) != (attr[i] & ~dmRecAttrBusy)) { StrPrintF(msg, "c%d %s ai%d FAIL rec %d attr %x want %x", c, pat, ai, i, a, attr[i]); goto out; }
		if ((pat[i] == 'N') != (chunk == 0)) { StrPrintF(msg, "c%d %s ai%d FAIL rec %d data presence", c, pat, ai, i); goto out; }
		if (chunk) {
			p = MemLocalIDToLockedPtr(chunk, 0);
			if (MemPtrSize(p) != recSize(i)) { StrPrintF(msg, "c%d %s ai%d FAIL rec %d size %ld", c, pat, ai, i, MemPtrSize(p)); MemPtrUnlock(p); goto out; }
			for (j = 0; j < recSize(i); j++) if (p[j] != (UInt8) ('a' + i)) break;
			MemPtrUnlock(p);
			if (j != recSize(i)) { StrPrintF(msg, "c%d %s ai%d FAIL rec %d byte %ld", c, pat, ai, i, j); goto out; }
		}
	}
	DmDatabaseInfo(0, id, 0, 0, 0, 0, 0, 0, 0, &chunk, 0, 0, 0);
	if (ai != (chunk != 0)) { StrPrintF(msg, "c%d %s ai%d FAIL appinfo presence", c, pat, ai); goto out; }
	if (ai) {
		p = MemLocalIDToLockedPtr(chunk, 0);
		for (j = 0; j < 300 && p[j] == 'i'; j++) ;
		if (MemPtrSize(p) != 300 || j != 300) { StrPrintF(msg, "c%d %s ai%d FAIL appinfo data", c, pat, ai); MemPtrUnlock(p); goto out; }
		MemPtrUnlock(p);
	}
	StrPrintF(msg, "c%d %s ai%d OK", c, pat, ai);
out:
	DmCloseDatabase(db);
}

/* Resource database round trip (most files in a backup are PRCs): n resources of mixed sizes. */
static void runRes(UInt16 n, char *msg)
{
	static const UInt16 kSz[] = { 10, 2000, 1, 700, 3000, 1024, 1025 };
	static const char name[] = "ExgTestRes";
	LocalID id, chunk; DmOpenRef db; MemHandle h; UInt8 *p; UInt16 i, rid; UInt32 typ, j; Err e; Boolean reset;

	if ((id = DmFindDatabase(0, name)) != 0) DmDeleteDatabase(0, id);
	DmCreateDatabase(0, name, CRID, 'rsrc', true);
	id = DmFindDatabase(0, name); db = DmOpenDatabase(0, id, dmModeReadWrite);
	for (i = 0; i < n; i++) {
		h = DmNewResource(db, 'tstA' + i, 100 + i, kSz[i]);
		p = MemHandleLock(h); DmSet(p, 0, kSz[i], (UInt8) ('A' + i)); MemHandleUnlock(h); DmReleaseResource(h);
	}
	DmCloseDatabase(db);
	gLen = gPos = 0;
	e = MyExgDBWrite(wr, 0, 0, id, 0);
	if (e) { StrPrintF(msg, "res%d FAIL export err %x", n, e); return; }
	e = MyExgDBRead(rd, 0, 0, &id, 0, &reset, true);
	if (e) { StrPrintF(msg, "res%d FAIL import err %x", n, e); return; }
	db = DmOpenDatabase(0, id, dmModeReadOnly);
	if (DmNumResources(db) != n) { StrPrintF(msg, "res%d FAIL count %d", n, DmNumResources(db)); goto out; }
	for (i = 0; i < n; i++) {
		DmResourceInfo(db, i, &typ, &rid, &chunk);
		p = MemLocalIDToLockedPtr(chunk, 0);
		for (j = 0; j < kSz[i] && p[j] == (UInt8) ('A' + i); j++) ;
		if (typ != 'tstA' + i || rid != 100 + i || MemPtrSize(p) != kSz[i] || j != kSz[i]) {
			StrPrintF(msg, "res%d FAIL res %d id %d size %ld byte %ld", n, i, rid, MemPtrSize(p), j); MemPtrUnlock(p); goto out; }
		MemPtrUnlock(p);
		/* the import must not leave chunks locked */
		if (MemHandleLockCount(MemPtrRecoverHandle(p))) { StrPrintF(msg, "res%d FAIL res %d left locked", n, i); goto out; }
	}
	StrPrintF(msg, "res%d OK", n);
out:
	DmCloseDatabase(db);
}

UInt32 PilotMain(UInt16 cmd, MemPtr pb, UInt16 f)
{
	if (cmd == sysAppLaunchCmdNormalLaunch) {
		char msg[96]; UInt16 c = START, sz = sizeof(c), next;
		PrefGetAppPreferences(CRID, 1, &c, &sz, false);
		if (c >= NPAT * 2 + 7) StrCopy(msg, "ALL DONE");
		else {
			next = c + 1; PrefSetAppPreferences(CRID, 1, 1, &next, sizeof(next), false);
			gBuf = MemPtrNew(BUFSZ);
			if (c >= NPAT * 2) runRes(c - NPAT * 2 + 1, msg);	/* 1..7 resources */
			else runCase(c, msg);
			MemPtrFree(gBuf);
		}
		FrmCustomAlert(1000, msg, "", "");
	}
	return 0;
}
