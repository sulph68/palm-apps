#include "VfsExportImport.h"
#include <PalmOS.h>
#include <VfsMgr.h>
#include "ops.h"


static const char mStrLaunchDbName[] = "psysLaunchDB";
static const char mStrBkpPath[] = "/PALM/PROGRAMS/BKP1";	//trailing slash NOT allowed
static const char mStrReset[] = "Restore completed. Device must now reset";
static const char mStrSpace[] = " ";
static const char mStrSlash[] = "/";


struct BackupProgressState {
	
	BackupProgressF progressF;
	void *progressD;
	UInt16 fileIdx, numFiles;
	UInt32 startSizeDone, thisFileSize, totalSize;
	const char *name;
};


//we generate only short names to save card space wasted on dir entries otherwise
//we only back up RAM card 0 becase, well, because....

static Err backupPrvCreateDirs(UInt16 vrn)
{
	char *path = MemPtrNew(sizeof(mStrBkpPath) + 1);
	Err e = errNone;
	UInt16 i;
	
	if (!path)
		return memErrNotEnoughSpace;
	
	for (i = 0; i < sizeof(mStrBkpPath); i++) {
		
		if (i && (mStrBkpPath[i] == '/' || mStrBkpPath[i] == 0)) {
			path[i] = 0;
			
			(void)VFSDirCreate(vrn, path);
		}
		path[i] = mStrBkpPath[i];
	}
	
	MemPtrFree(path);
	
	return e;
}

//assumes some pre-setup
static void backupPrvFormPath(char *filePathPart, UInt16 idx, Boolean isResDB)
{
	StrIToH(filePathPart, idx);
	*(filePathPart += 8) = '.';
	filePathPart += 2;
	if (isResDB) {
		*filePathPart++ = 'R';
		*filePathPart++ = 'C';
	}
	else {
		*filePathPart++ = 'D';
		*filePathPart++ = 'B';
	}
}

static char* backupPrvAllocPath(char **filePathPartP, const char *dir)
{
	char *filePathPart, *filePath = MemPtrNew(sizeof(mStrBkpPath) + 1 +  StrLen(dir) + 1 + 8 + 1 + 3 + 1);	//slash, name, dot, ext, nul
	UInt16 len;
	
	if (!filePath)
		return NULL;
	
	StrCopy(filePath, mStrBkpPath);
	StrCat(filePath, mStrSlash);
	StrCat(filePath, dir);
	if (!(len = StrLen(filePath)) || filePath[len - 1] != '/')
		filePath[len++] = '/';
	filePathPart = filePath + len;
	filePathPart[9] = 'P';
	filePathPart[12] = 0;
	
	*filePathPartP = filePathPart;
	return filePath;
}

static Err backupPrvExportImportProc(UInt32 totalBytes, UInt32 offset, void *userDataP)
{
	struct BackupProgressState *state = (struct BackupProgressState*)userDataP;
	
	if (offset == 0 || offset == totalBytes)		//we sometimes get called with zero or full size - ignore that, do not waste time on updating the UI yet again
		return errNone;
	
	else {
		
		UInt32 doneSoFar = (unsigned long long)offset * state->thisFileSize / totalBytes;	//scale

		state->progressF(state->progressD, BackupCopying, state->fileIdx, state->numFiles, state->startSizeDone + doneSoFar, state->totalSize, state->name);
	}
	
	return 0;
}

Err backupDo(UInt16 vrn, const char *bkpName, BackupProgressF progressF, BackupFailHandlerF errF, void *progressD)		//path is dir we'll create
{
	UInt16 i, numFiles, numRamFiles = 0, idxRamFile = 0, selfCard;
	UInt32 sizeTotal = 0, sizeDone = 0, now;
	struct BackupProgressState exportState;
	char *filePath, *filePathPart;
	char name[dmDBNameLength];
	LocalID LID, selfLID;
	Err e;
	
	SysCurAppDatabase(&selfCard, &selfLID);
	
	e = backupPrvCreateDirs(vrn);
	if (e != errNone)
		goto out;
	
	filePath = backupPrvAllocPath(&filePathPart, bkpName);
	if (!filePath) {
		e = memErrNotEnoughSpace;
		goto out;
	}
	
	numFiles = DmNumDatabases(0) - 1 /* skip self */;
	
	//tell caller
	e = progressF(progressD, BackupStarting, 0, 0, 0, 0, NULL);
	if (e != errNone)
		goto out_free;
	

	//enumerate them first so we can give proper reports
	for (i = 0; i < numFiles; i++) {
		
		LID = DmGetDatabase(0, i);
		
		if (MemLocalIDKind(LID) != memIDHandle)
			continue;
		
		if (selfCard == 0 && selfLID == LID)	//skip self
			continue;
		
		//PalmOs below 4.1 does not like LaunchDB touched
		if (errNone == DmDatabaseInfo(0, LID, name, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL) && !StrCompare(name, mStrLaunchDbName))
			continue;
		
		numRamFiles++;
		e = DmDatabaseSize(0, LID, NULL, &now, NULL);
		if (e != errNone)
			goto out_free;
		
		sizeTotal += now;
	}
	
	//create dir in preparation for backup
	filePathPart[-1] = 0;	//remove slash and terminate for now
	e = VFSDirCreate(vrn, filePath);
	if (e != errNone)
		goto out_free;
	filePathPart[-1] = '/';	//restore slash

	
	exportState.progressF = progressF;
	exportState.progressD = progressD;
	exportState.numFiles = numRamFiles;
	exportState.totalSize = sizeTotal;
		
	//now copy
	for (i = 0; i < numFiles; i++) {
		
		UInt16 attr;
		
		LID = DmGetDatabase(0, i);
		
		if (MemLocalIDKind(LID) != memIDHandle)
			continue;
		
		if (selfCard == 0 && selfLID == LID)	//skip self
			continue;
		
		e = DmDatabaseInfo(0, LID, name, &attr, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
		if (e != errNone)
			break;
		
		//PalmOs below 4.1 does not like LaunchDB touched
		if (!StrCompare(name, mStrLaunchDbName))
			continue;
		
		//form name
		backupPrvFormPath(filePathPart, idxRamFile, !!(attr & dmHdrAttrResDB));
		
		e = progressF(progressD, BackupCopying, idxRamFile, numRamFiles, sizeDone, sizeTotal, name);
		if (e != errNone)
			break;
		
		e = DmDatabaseSize(0, LID, NULL, &now, NULL);
		if (e != errNone)
			goto out_free;
		
		exportState.fileIdx = idxRamFile;
		exportState.startSizeDone = sizeDone;
		exportState.thisFileSize = now;
		exportState.name = name;
		e = MyVFSExportDatabaseToFileCustom(vrn, filePath, 0, LID, backupPrvExportImportProc, &exportState);
		idxRamFile++;
		if (e != errNone)
			e = errF(progressD, name, e);
		if (e != errNone)
			break;
		
		//report
		e = progressF(progressD, BackupCopying, idxRamFile, numRamFiles, sizeDone += now, sizeTotal, name);
		if (e != errNone)
			break;
		
	}
	
	if (i != numFiles) {	//we had an error delete up & ivcluding to index i
		
		UInt16 j;
		
		for (j = 0; j <= i; j++) {
			
			//brute force the sucker
			backupPrvFormPath(filePathPart, j, true);
			(void)VFSFileDelete(vrn, filePath);
			
			backupPrvFormPath(filePathPart, j, false);
			(void)VFSFileDelete(vrn, filePath);
		}
		
		filePathPart[-1] = 0;	//remove slash
		(void)VFSFileDelete(vrn, filePath);
	}
	

	(void)progressF(progressD, BackupDone, 0, 0, 0, 0, NULL);

out_free:
	MemPtrFree(filePath);

out:
	return e;
}


static Err backupPrvOpenSpecificFile(UInt16 vrn, char *filePath, char *filePathPart, UInt16 idx, Boolean res, FileRef *fP)
{
	backupPrvFormPath(filePathPart, idx, res);
	return VFSFileOpen(vrn, filePath, vfsModeRead, fP);
}

static Err backupPrvOpenIndexedFile(UInt16 vrn, char *filePath, char *filePathPart, UInt16 idx, FileRef *fP)
{
	Err e;
	
	e = backupPrvOpenSpecificFile(vrn, filePath, filePathPart, idx, true, fP);
	if (e == vfsErrFileNotFound)
		e = backupPrvOpenSpecificFile(vrn, filePath, filePathPart, idx, false, fP);
	
	return e;
}

//restores file number idx of a backup. "name" (dmDBNameLength bytes) gets the database's name, or the file's name
//("0000002A.PDB") if we could not get that far. *sizeP gets the file's size (0 if unknown). progressF is told the
//name before the copy starts; if it returns an error (user cancelled) that is returned with *cancelledP set.
static Err backupPrvRestoreFile(UInt16 vrn, char *filePath, char *filePathPart, UInt16 idx, UInt16 progressIdx, char *name, UInt32 *sizeP, struct BackupProgressState *importState, Boolean *cancelledP)
{
	UInt32 now = 0;
	UInt16 cardNo;
	LocalID LID;
	FileRef f;
	Err e;
	
	*cancelledP = false;
	
	e = backupPrvOpenIndexedFile(vrn, filePath, filePathPart, idx, &f);
	StrCopy(name, filePathPart);	//until we know the database's name, report the file's name ("0000002A.PDB")
	if (e == errNone) {
		
		char dbName[dmDBNameLength];
		
		e = VFSFileSize(f, &now);
		if (e != errNone)
			now = 0;
		else {
			
			//All we need here is the database's name, for the progress display: it is the first 32 bytes of the
			//file. Do not use VFSFileDBInfo() for this: it rejects (vfsErrBadData) files that are perfectly good
			//and that our own importer handles - seen with a third-party app whose header dates are all zero.
			UInt32 got = 0;
			UInt16 j;
			
			e = VFSFileRead(f, dmDBNameLength, dbName, &got);
			if (e == vfsErrFileEOF)
				e = errNone;
			for (j = 0; j < dmDBNameLength && dbName[j]; j++);
			if (e == errNone && (got != dmDBNameLength || !j || j == dmDBNameLength))
				e = dmErrCorruptDatabase;	//too short to be a database, or no (terminated) name
		}
		
		(void)VFSFileClose(f);
		
		if (e == errNone) {
			
			StrCopy(name, dbName);
			
			e = importState->progressF(importState->progressD, BackupCopying, progressIdx, importState->numFiles, importState->startSizeDone, importState->totalSize, name);
			if (e != errNone) {		//user cancelled
				*cancelledP = true;
				*sizeP = now;
				return e;
			}
			
			importState->fileIdx = progressIdx;
			importState->thisFileSize = now;
			importState->name = name;
			e = MyVFSImportDatabaseFromFileCustom(vrn, filePath, &cardNo, &LID, backupPrvExportImportProc, importState);
			if (e == dmErrAlreadyExists) {
			
				e = DmDeleteDatabase(cardNo, LID);
	
				if (e != errNone) {
					
					UInt32 typeAndCrid = '~~~~';
					UInt16 attrs, x = 0;
					char tmpName[16];
					
					//get a name
					do {
						tmpName[0] = '~';
						tmpName[1] = 'D';
						tmpName[2] = '-';
						StrIToA(tmpName + 3, x++);
						
					} while (DmFindDatabase(cardNo, tmpName));
					
					if (errNone == DmDatabaseInfo(cardNo, LID, NULL, &attrs, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL)) {
						attrs |= dmHdrAttrRecyclable;
						DmSetDatabaseInfo(cardNo, LID, tmpName, &attrs, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &typeAndCrid, &typeAndCrid);
					}
				}
				e = MyVFSImportDatabaseFromFileCustom(vrn, filePath, &cardNo, &LID, backupPrvExportImportProc, importState);
			}
		}
	}
	
	*sizeP = now;
	return e;
}

Err backupRestore(UInt16 vrn, const char *bkpName, BackupProgressF progressF, BackupFailHandlerF errF, void *progressD)
{
	UInt32 totalSize = 0, doneSize = 0, now;
	struct BackupProgressState importState;
	char *filePath, *filePathPart;
	UInt16 numFiles = 0, i, numSkipped = 0, failsInARow = 0;
	FileRef f;
	Err e;
	
	
	filePath = backupPrvAllocPath(&filePathPart, bkpName);
	if (!filePath) {
		e = memErrNotEnoughSpace;
		goto out;
	}
	
	e = progressF(progressD, BackupStarting, 0, 0, 0, 0, NULL);
	if (e != errNone)
		goto out_free;
	
	//count files
	while (1) {
		
		e = backupPrvOpenIndexedFile(vrn, filePath, filePathPart, numFiles, &f);
		if (e == vfsErrFileNotFound) {
			e = errNone;
			break;
		}
		else if (e != errNone)
			goto out_free;
		
		if (errNone == VFSFileSize(f, &now))	//a file we cannot size is still counted - it gets reported and skipped below
			totalSize += now;
		numFiles++;
		
		(void)VFSFileClose(f);
	}
	
	importState.progressF = progressF;
	importState.progressD = progressD;
	importState.numFiles = numFiles;
	importState.totalSize = totalSize;
		
	//restore. A file that cannot be read or imported is noted (errF - which must not stop to ask the user) and
	//skipped, so that one bad file does not cost the rest of the restore. The failures are reported once, after
	//the last file and before the reset (errF with a NULL name). Three failures in a row means the card itself
	//is in trouble: give up.
	for (i = 0; i < numFiles; i++) {
	
		char name[dmDBNameLength];
		Boolean cancelled;

		importState.startSizeDone = doneSize;
		e = backupPrvRestoreFile(vrn, filePath, filePathPart, i, i, name, &now, &importState, &cancelled);
		if (cancelled)
			goto out_free;
		
		if (e == errNone)
			failsInARow = 0;
		else {
			
			Err failedWith = e;
			
			e = errF(progressD, name, e);	//notes it; errNone = carry on (also for errors it chooses to ignore)
			if (e != errNone)
				goto out_free;
			
			if (failedWith != dmErrDatabaseProtected) {
				
				numSkipped++;
				if (++failsInARow >= 3) {
					e = failedWith;
					goto out_free;
				}
			}
		}
		
		doneSize += now;
		
		e = progressF(progressD, BackupCopying, i, numFiles, doneSize, totalSize, name);
		if (e != errNone)
			goto out_free;
	}
	
	(void)progressF(progressD, BackupDone, 0, 0, 0, 0, NULL);
	
	if (numSkipped)
		(void)errF(progressD, NULL, errNone);	//NULL name = "restore finished, report what was skipped now"
	
	FrmCustomAlert(1001, mStrReset, mStrSpace, mStrSpace);
	SysReset();

out_free:
	MemPtrFree(filePath);
	
out:
	return e;

close_file_and_free:
	VFSFileClose(f);
	goto out_free;
}

#define PREFS_DB_TYPE_SAVED		'sprf'
#define PREFS_DB_TYPE_UNSAVED	'pref'
#define PREFS_DB_CREATOR		'psys'
#define MAX_SCAN_FILES			1000
#define MAX_PREF_RESOURCES		2000
#define MAX_PREF_SIZE			0xF000UL

Boolean backupItemIsPrefsDb(const struct BackupItem *item)
{
	return item->isRes && item->creator == PREFS_DB_CREATOR && (item->type == PREFS_DB_TYPE_SAVED || item->type == PREFS_DB_TYPE_UNSAVED);
}

Err backupScan(UInt16 vrn, const char *bkpName, struct BackupItem **itemsP, UInt16 *numP, BackupProgressF progressF, void *progressD)
{
	struct BackupItem *items = NULL, *it;
	char *filePath, *filePathPart;
	UInt16 num = 0, cap = 0;
	UInt8 hdr[78];
	UInt32 got;
	FileRef f;
	Err e;
	
	*itemsP = NULL;
	*numP = 0;
	
	filePath = backupPrvAllocPath(&filePathPart, bkpName);
	if (!filePath)
		return memErrNotEnoughSpace;
	
	e = progressF(progressD, BackupStarting, 0, 0, 0, 0, NULL);
	
	while (e == errNone && num < MAX_SCAN_FILES) {
		
		e = backupPrvOpenIndexedFile(vrn, filePath, filePathPart, num, &f);
		if (e == vfsErrFileNotFound) {	//no more files
			e = errNone;
			break;
		}
		if (e != errNone)
			break;
		
		if (num == cap) {	//grow the array
			
			struct BackupItem *bigger = MemPtrNew((UInt32)(cap + 32) * sizeof(struct BackupItem));
			
			if (!bigger) {
				(void)VFSFileClose(f);
				e = memErrNotEnoughSpace;
				break;
			}
			if (items) {
				MemMove(bigger, items, (UInt32)cap * sizeof(struct BackupItem));
				MemPtrFree(items);
			}
			items = bigger;
			cap += 32;
		}
		
		it = &items[num];
		MemSet(it, sizeof(*it), 0);
		it->fileIdx = num;
		it->selected = true;
		(void)VFSFileSize(f, &it->size);
		
		got = 0;
		e = VFSFileRead(f, sizeof(hdr), hdr, &got);
		if (e == vfsErrFileEOF)
			e = errNone;
		(void)VFSFileClose(f);
		if (e != errNone)
			break;
		
		if (got == sizeof(hdr) && hdr[0] && !hdr[dmDBNameLength - 1]) {
			
			StrCopy(it->name, (char*)hdr);
			it->isRes = hdr[33] & dmHdrAttrResDB;
			MemMove(&it->type, hdr + 60, 4);
			MemMove(&it->creator, hdr + 64, 4);
			it->numEntries = ((UInt16)hdr[76] << 8) | hdr[77];
		}
		else {	//not a database we understand - still list it (under its file name) so nothing is hidden
			
			StrCopy(it->name, filePathPart);
			it->isRes = (filePathPart[10] == 'R');
		}
		num++;
		
		e = progressF(progressD, BackupDeleting /* = count-based bar */, num, num + 1, 0, 0, it->name);
	}
	
	MemPtrFree(filePath);
	
	if (e != errNone) {
		if (items)
			MemPtrFree(items);
		return e;
	}
	
	*itemsP = items;
	*numP = num;
	return errNone;
}

static Boolean backupPrvCreatorSelected(const struct BackupItem *items, UInt16 num, UInt32 creator)
{
	UInt16 i;
	
	for (i = 0; i < num; i++) {
		if (items[i].selected && items[i].creator == creator)
			return true;
	}
	return false;
}

//copies, from a preference database file in the backup, the entries that belong to selected creators into this
//device's own preferences. An entry is a resource: type = the owner's creator, id = the preference id, contents =
//version (2 bytes) + data - exactly what PrefSetAppPreferences() stores, so we hand each one to it.
//The system's own entries (creator 'psys') are never touched here.
static Err backupPrvMergePrefs(UInt16 vrn, char *filePath, char *filePathPart, const struct BackupItem *prefsItem, const struct BackupItem *items, UInt16 num)
{
	Boolean saved = (prefsItem->type == PREFS_DB_TYPE_SAVED);
	UInt32 fileSize, got, ofst, nextOfst, creator, size;
	UInt16 n = prefsItem->numEntries, i, id;
	UInt8 *table = NULL, *buf, *ent;
	FileRef f;
	Err e;
	
	if (!n)
		return errNone;
	if (n > MAX_PREF_RESOURCES)
		return dmErrCorruptDatabase;
	
	e = backupPrvOpenSpecificFile(vrn, filePath, filePathPart, prefsItem->fileIdx, true, &f);
	if (e != errNone)
		return e;
	
	e = VFSFileSize(f, &fileSize);
	if (e != errNone)
		goto out_close;
	
	table = MemPtrNew((UInt32)n * 10);
	if (!table) {
		e = memErrNotEnoughSpace;
		goto out_close;
	}
	
	e = VFSFileSeek(f, vfsOriginBeginning, 78);
	if (e != errNone && e != vfsErrFileEOF)
		goto out_close;
	e = VFSFileRead(f, (UInt32)n * 10, table, &got);
	if (e == vfsErrFileEOF)
		e = errNone;
	if (e == errNone && got != (UInt32)n * 10)
		e = dmErrCorruptDatabase;
	if (e != errNone)
		goto out_close;
	
	for (i = 0; i < n; i++) {
		
		ent = table + (UInt32)i * 10;
		MemMove(&creator, ent, 4);
		id = ((UInt16)ent[4] << 8) | ent[5];
		MemMove(&ofst, ent + 6, 4);
		if (i + 1 < n)
			MemMove(&nextOfst, ent + 16, 4);
		else
			nextOfst = fileSize;
		
		if (creator == PREFS_DB_CREATOR || !backupPrvCreatorSelected(items, num, creator))
			continue;
		
		if (nextOfst <= ofst || nextOfst > fileSize) {
			e = dmErrCorruptDatabase;
			break;
		}
		size = nextOfst - ofst;
		if (size < 2 || size > MAX_PREF_SIZE)	//no room for the version word, or absurd: leave it
			continue;
		
		buf = MemPtrNew(size);
		if (!buf) {
			e = memErrNotEnoughSpace;
			break;
		}
		
		e = VFSFileSeek(f, vfsOriginBeginning, ofst);
		if (e == vfsErrFileEOF)
			e = errNone;
		if (e == errNone) {
			e = VFSFileRead(f, size, buf, &got);
			if (e == vfsErrFileEOF)
				e = errNone;
			if (e == errNone && got != size)
				e = dmErrCorruptDatabase;
		}
		if (e == errNone)
			PrefSetAppPreferences(creator, id, (Int16)(((UInt16)buf[0] << 8) | buf[1]), buf + 2, (UInt16)(size - 2), saved);
		
		MemPtrFree(buf);
		if (e != errNone)
			break;
	}

out_close:
	if (table)
		MemPtrFree(table);
	(void)VFSFileClose(f);
	return e;
}

Err backupRestoreSelected(UInt16 vrn, const char *bkpName, struct BackupItem *items, UInt16 num, BackupProgressF progressF, BackupFailHandlerF errF, void *progressD)
{
	UInt32 totalSize = 0, doneSize = 0, now;
	struct BackupProgressState importState;
	UInt16 numSel = 0, i, done = 0, numSkipped = 0, failsInARow = 0;
	char *filePath, *filePathPart;
	char name[dmDBNameLength];
	Boolean cancelled;
	Err e;
	
	filePath = backupPrvAllocPath(&filePathPart, bkpName);
	if (!filePath)
		return memErrNotEnoughSpace;
	
	e = progressF(progressD, BackupStarting, 0, 0, 0, 0, NULL);
	if (e != errNone)
		goto out_free;
	
	for (i = 0; i < num; i++) {
		if (items[i].selected) {
			numSel++;
			totalSize += items[i].size;
		}
	}
	
	importState.progressF = progressF;
	importState.progressD = progressD;
	importState.numFiles = numSel;
	importState.totalSize = totalSize;
	
	//same rules as backupRestore(): note a failure, skip the file, carry on; three in a row = give up
	for (i = 0; i < num; i++) {
		
		Err failedWith;
		
		if (items[i].selected) {
			
			importState.startSizeDone = doneSize;
			e = backupPrvRestoreFile(vrn, filePath, filePathPart, items[i].fileIdx, done, name, &now, &importState, &cancelled);
			if (cancelled)
				goto out_free;
			if (!now)
				now = items[i].size;
		}
		else if (backupItemIsPrefsDb(&items[i])) {	//not restored whole: bring over the chosen apps' own settings
			
			now = 0;
			StrCopy(name, items[i].name);
			e = backupPrvMergePrefs(vrn, filePath, filePathPart, &items[i], items, num);
		}
		else
			continue;
		
		if (e == errNone)
			failsInARow = 0;
		else {
			
			failedWith = e;
			
			e = errF(progressD, name, e);
			if (e != errNone)
				goto out_free;
			
			if (failedWith != dmErrDatabaseProtected) {
				
				numSkipped++;
				if (++failsInARow >= 3) {
					e = failedWith;
					goto out_free;
				}
			}
		}
		
		if (items[i].selected) {
			
			doneSize += now;
			done++;
			e = progressF(progressD, BackupCopying, done - 1, numSel, doneSize, totalSize, name);
			if (e != errNone)
				goto out_free;
		}
	}
	
	(void)progressF(progressD, BackupDone, 0, 0, 0, 0, NULL);
	
	if (numSkipped)
		(void)errF(progressD, NULL, errNone);
	
	FrmCustomAlert(1001, mStrReset, mStrSpace, mStrSpace);
	SysReset();

out_free:
	MemPtrFree(filePath);
	return e;
}

Err backupDelete(UInt16 vrn, const char *bkpName, BackupProgressF progressF, void *progressD)
{
	char *fullPath = MemPtrNew(sizeof(mStrBkpPath) + 1 + StrLen(bkpName) + 255 + 1 + 1);
	FileInfoType fit;
	FileRef f;
	UInt16 i;
	Err e;
	
	if (!fullPath) {
	
		e = memErrNotEnoughSpace;
		goto out;
	}
	
	e = progressF(progressD, BackupStarting, 0, 0, 0, 0, NULL);
	if (e != errNone)
		goto out_free;

	StrCopy(fullPath, mStrBkpPath);
	StrCat(fullPath, mStrSlash);
	StrCat(fullPath, bkpName);
	if (!(i = StrLen(fullPath)) || fullPath[i - 1] != '/') {
		fullPath[i] = '/';
		i++;
	}
	fit.nameP = fullPath + i;
	fit.nameBufLen = 255;
	
	fit.nameP[-1] = 0;	//remove slash for now
	e = VFSFileOpen(vrn, fullPath, vfsModeRead, &f);
	if (e != errNone)
		goto out_free;
	fit.nameP[-1] = '/';	//restore slash
	
	while (1) {
		UInt32 iter = vfsIteratorStart;	//as we delete, iterator becomes invalid, so we restart each time
		
		e = VFSDirEntryEnumerate(f, &iter, &fit);
		if (e == expErrEnumerationEmpty) {
			e = errNone;
			break;
		}
		if (e != errNone)
			goto close_file_and_free;
		
		e = VFSFileDelete(vrn, fullPath);
		if (e != errNone)
			goto close_file_and_free;
	}
	
	(void)VFSFileClose(f);
	fit.nameP[-1] = 0;	//remove slash
	e = VFSFileDelete(vrn, fullPath);
	
	(void)progressF(progressD, BackupDone, 0, 0, 0, 0, NULL);
	
out_free:
	MemPtrFree(fullPath);

out:
	return e;

close_file_and_free:
	VFSFileClose(f);
	goto out_free;
}

Err backupRename(UInt16 vrn, const char *bkpName, const char *newName)
{
	char *fullPath = MemPtrNew(sizeof(mStrBkpPath) + 1 + StrLen(bkpName) + 255 + 1 + 1);
	UInt16 L;
	Err e;
	
	if (!fullPath)
		return memErrNotEnoughSpace;
	
	StrCopy(fullPath, mStrBkpPath);
	StrCat(fullPath, mStrSlash);
	StrCat(fullPath, bkpName);
	L = StrLen(fullPath);
	
	while (L && fullPath[L - 1] == '/')
		fullPath[--L] = 0;
	
	e = VFSFileRename(vrn, fullPath, newName);
	
	MemPtrFree(fullPath);
	
	return e;
}

Err backupEnumerate(UInt16 vrn, BackupEnumF enumF, void *enumD)
{
	UInt32 iter = vfsIteratorStart;
	FileInfoType fit;
	FileRef f, setF;
	Err e;
	
	//ignore the error here, but create the dir so the user knows about it...
	(void)backupPrvCreateDirs(vrn);
	
	fit.nameP = MemPtrNew((fit.nameBufLen = 255) + 1);
	if (!fit.nameP) {
	
		e = memErrNotEnoughSpace;
		goto out;
	}
	
	e = VFSFileOpen(vrn, mStrBkpPath, vfsModeRead, &f);
	if (e != errNone) {
		if (e == vfsErrFileNotFound)	//no backup dir == no backups (but not an error)
			e = errNone;
		goto out_free;
	}
	
	do {
		
		e = VFSDirEntryEnumerate(f, &iter, &fit);
		if (e == expErrEnumerationEmpty) {
		
			e = errNone;
			break;
		}
	
		e = enumF(enumD, fit.nameP);
		if (e != errNone)
			break;
	
	} while (iter != vfsIteratorStop);
	
	(void)VFSFileClose(f);

out_free:
	MemPtrFree(fit.nameP);
	
out:
	return e;
}

	






























