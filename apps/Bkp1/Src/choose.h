#ifndef _CHOOSE_H_
#define _CHOOSE_H_

#include <PalmOS.h>
#include "ops.h"

//what kind of thing a backup file is (from its database type)
#define ITEM_KIND_DATA			0
#define ITEM_KIND_APP			1
#define ITEM_KIND_HACK			2
#define ITEM_KIND_PANEL			3
#define ITEM_KIND_LIBRARY		4	//drivers and system extensions
#define ITEM_KIND_PREFS			5	//the two system preference databases
#define ITEM_KIND_SHLIB			6	//a shared library applications call (e.g. MathLib)

//what a group of files is. Also the order the groups are listed in
#define GROUP_CLASS_APP			0	//an application and the data that carries its creator
#define GROUP_CLASS_BUILTIN		1	//data of an application that lives in ROM
#define GROUP_CLASS_OTHER		2	//data whose owner we cannot name
#define GROUP_CLASS_SHLIB		3	//shared libraries: plain code that applications need
#define GROUP_CLASS_HACK		4	//HackMaster hacks, and the hack manager with its list of active hacks
#define GROUP_CLASS_PANEL		5
#define GROUP_CLASS_LIBS		6	//drivers and system extensions
#define GROUP_CLASS_SYSTEM		7

#define PRESET_EVERYTHING		0
#define PRESET_APPS_AND_DATA	1
#define PRESET_DATA_ONLY		2
#define PRESET_NOTHING			3

struct ChooseGroup {
	char title[dmDBNameLength];
	UInt32 key;					//creator, or a made-up value for the catch-all groups
	UInt32 size;				//of all its files
	UInt16 count, numSel;
	UInt8 cls;
};

//---- the logic (no user interface in these; test/ drives them directly) ----

//sorts the items into groups by owner. Sets each item's kind and group. Returns the number of groups
//(groups[] must have room for maxGroups; the last one is used as a catch-all if there are too many)
UInt16 chooseBuildGroups(struct BackupItem *items, UInt16 num, struct ChooseGroup *groups, UInt16 maxGroups);

//(re)selects items according to a preset, and recounts the groups
void chooseApplyPreset(struct BackupItem *items, UInt16 num, struct ChooseGroup *groups, UInt16 numGroups, UInt8 preset);

//recounts groups' numSel after items were (de)selected
void chooseRecount(const struct BackupItem *items, UInt16 num, struct ChooseGroup *groups, UInt16 numGroups);

//---- the user interface ----

//lets the user pick. Returns true to go ahead with what is now selected in items[], false if cancelled
Boolean chooseRun(struct BackupItem *items, UInt16 num, const char *bkpName);

#endif
