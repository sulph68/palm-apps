#include "VfsExportImport.h"
#include "ExgDB.h"

struct ProgressState {
	//two plain members rather than an anonymous union: gcc 2.95 (prc-tools) has no anonymous unions in C
	VFSImportProcPtr importProcP;
	VFSExportProcPtr exportProcP;
	void *cbkData;
	FileRef file;
	UInt32 totalSize;	//may be an estimate
	UInt32 doneSize;
};


static Boolean prvImportExgDeleteCbk(const char *nameP, UInt16 version, UInt16 cardNo, LocalID dbID, void *userDataP)
{
	return true;
}

static Err prvImportExgReadCbk(void *dataP, UInt32 *sizeP, void *userDataP)
{
	struct ProgressState *ps = (struct ProgressState*)userDataP;
	UInt32 done;
	Err e;
	
	e = VFSFileRead(ps->file, *sizeP, dataP, sizeP);
	done = ps->doneSize += *sizeP;
	
	if (e != errNone && e != vfsErrFileEOF)
		return e;
	
	if (ps->importProcP)
		return ps->importProcP(ps->totalSize, done, ps->cbkData);
	
	return errNone;
}

Err MyVFSImportDatabaseFromFileCustom(UInt16 volRefNum, const Char *pathNameP, UInt16 *cardNoP, LocalID *dbIDP, VFSImportProcPtr importProcP, void *userDataP)
{
	struct ProgressState ps;
	Boolean needReset;
	LocalID LID;
	Err e;
	
	ps.importProcP = importProcP;
	ps.cbkData = userDataP;
	ps.doneSize = 0;
		
	e = VFSFileOpen(volRefNum, pathNameP, vfsModeRead, &ps.file);
	if (e != errNone)
		goto out;
	
	e = VFSFileSize(ps.file, &ps.totalSize);
	if (e != errNone)
		goto out_close;
		
	e = MyExgDBRead(prvImportExgReadCbk, prvImportExgDeleteCbk, &ps, &LID, 0, &needReset, true);

	if (dbIDP)
		*dbIDP = LID;
	if (cardNoP)
		*cardNoP = 0;

out_close:
	VFSFileClose(ps.file);

out:
	return e;
}

static Err prvExportExgCbk(const void *dataP, UInt32 *sizeP, void *userDataP)
{
	struct ProgressState *ps = (struct ProgressState*)userDataP;
	UInt32 done;
	Err e;
	
	e = VFSFileWrite(ps->file, *sizeP, dataP, sizeP);
	done = ps->doneSize += *sizeP;
	
	if (e != errNone)
		return e;
	
	if (done > ps->totalSize)	//avoid showing more bytes written than predicted
		done = ps->totalSize;
	
	if (ps->exportProcP)
		e = ps->exportProcP(ps->totalSize, done, ps->cbkData);
	
	return e;
}

Err MyVFSExportDatabaseToFileCustom(UInt16 volRefNum, const Char *pathNameP, UInt16 cardNo, LocalID dbID, VFSExportProcPtr exportProcP, void *userDataP)
{
	struct ProgressState ps;
	Err e;

	e = DmDatabaseSize(cardNo, dbID, NULL, &ps.totalSize, NULL);
	if (e != errNone)
		goto out;

	//make sure we do not overwrite a file	
	e = VFSFileCreate(volRefNum, pathNameP);
	if (e != errNone)
		goto out;
	
	e = VFSFileOpen(volRefNum, pathNameP, vfsModeReadWrite, &ps.file);
	if (e != errNone)
		goto out_delete;
	
	ps.exportProcP = exportProcP;
	ps.cbkData = userDataP;
	ps.doneSize = 0;
	
	e = MyExgDBWrite(prvExportExgCbk, &ps, NULL, dbID, cardNo);
	

out_close:
	VFSFileClose(ps.file);
	
out_delete:
	if (e != errNone)
		(void)VFSFileDelete(volRefNum, pathNameP);
	
out:
	return e;
}

