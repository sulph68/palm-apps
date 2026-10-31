#ifndef _OPS_H_
#define _OPS_H_

#include <PalmOS.h>


enum BackupOpType {
	BackupStarting,
	BackupCopying,
	BackupDeleting,
	BackupDone,
};

//called when one database fails. Return errNone to carry on with the next one, anything else to stop.
//on restore it must not block (the failure is only to be noted), and it is called once more at the end
//with name == NULL when there were failures: that is the moment to report them to the user.
typedef Err (*BackupFailHandlerF)(void *userData, const char *name, Err e);
typedef Err (*BackupProgressF)(void *userData, enum BackupOpType op, UInt16 idx, UInt16 of, UInt32 size, UInt32 sizeOf, const char *name);
typedef Err (*BackupEnumF)(void *userData, const char *name);

//operation on a backup set
Err backupDo(UInt16 vrn, const char *bkpName, BackupProgressF progressF, BackupFailHandlerF errF, void *progressD);		//path is dir we'll create
Err backupRestore(UInt16 vrn, const char *bkpName, BackupProgressF progressF, BackupFailHandlerF errF, void *progressD);
Err backupDelete(UInt16 vrn, const char *bkpName, BackupProgressF progressF, void *progressD);
Err backupRename(UInt16 vrn, const char *bkpName, const char *newName);


//---- selective restore ----

//one file of a backup, as read from its header by backupScan()
struct BackupItem {
	char name[dmDBNameLength];
	UInt32 type, creator;
	UInt32 size;				//of the file
	UInt16 fileIdx;				//its number in the backup (the file's name)
	UInt16 numEntries;			//records or resources
	UInt8 isRes;				//resource database (.PRC)
	UInt8 selected;				//restore it?
	UInt8 group;				//for the chooser (see choose.c)
	UInt8 kind;					//for the chooser (see choose.c)
};

//reads the header of every file in a backup. *itemsP is a MemPtrNew'd array (caller frees), all "selected".
//works on any backup, old or new: everything comes from the files themselves
Err backupScan(UInt16 vrn, const char *bkpName, struct BackupItem **itemsP, UInt16 *numP, BackupProgressF progressF, void *progressD);

//restores the selected items, then resets the device.
//The two system preference databases ("Saved Preferences", "Unsaved Preferences") hold every app's settings as
//well as the system's own. If one of them is not selected, the settings in it that belong to a selected item's
//creator are copied in individually instead (system entries on the device are left alone).
Err backupRestoreSelected(UInt16 vrn, const char *bkpName, struct BackupItem *items, UInt16 num, BackupProgressF progressF, BackupFailHandlerF errF, void *progressD);

//is this item one of the two system preference databases?
Boolean backupItemIsPrefsDb(const struct BackupItem *item);


//enumeration of sets (do not call other backsup funcs in callback. error returned from callback terminates enumeration and is returned)
//a set made with a device name set is reported as "Device/date", one made without as "date"
Err backupEnumerate(UInt16 vrn, BackupEnumF enumF, void *enumD);
//the device folders on the volume (same callback rules)
Err backupEnumerateDevices(UInt16 vrn, BackupEnumF enumF, void *enumD);

//name of the database holding this device's Bkp1 settings; backups leave it out, so a restore never changes it.
//A #define, not a shared const: each .c keeps its own copy, since one file cannot reach another's code-resident
//constants (no relocation of code resources)
#define BKP_SETTINGS_DB_NAME	"Bkp1 Settings"

#endif
