#ifndef _MY_VFS_EXPORT_IMPORT_H_
#define _MY_VFS_EXPORT_IMPORT_H_


#include <PalmOS.h>
#include <VfsMgr.h>


Err MyVFSImportDatabaseFromFileCustom(UInt16 volRefNum, const Char *pathNameP, UInt16 *cardNoP, LocalID *dbIDP, VFSImportProcPtr importProcP, void *userDataP);
Err MyVFSExportDatabaseToFileCustom(UInt16 volRefNum, const Char *pathNameP, UInt16 cardNo, LocalID dbID, VFSExportProcPtr exportProcP, void *userDataP);


#endif
