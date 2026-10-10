#include "choose.h"
#include <WinGlue.h>

/*
	Selective restore: which files of a backup go back onto the device.

	Everything shown comes from the backup files' own headers (ops.c, backupScan), so any backup works, old or new.
	Files are grouped by owner. The owner of a database is the application (or hack, or panel) in the same backup
	that has the same creator ID; data of the ROM's own applications and of the system is recognised from a short
	table of well-known creators.
*/

#define MAX_GROUPS				96

#define FORM_GROUPS				1100
#define LIST_GROUPS				1101
#define TRIGGER_PRESET			1102
#define LIST_PRESET				1103
#define BUTTON_RESTORE			1104
#define BUTTON_FILES			1105
#define BUTTON_CANCEL			1106
#define FIELD_SUMMARY			1107

#define FORM_FILES				1110
#define LIST_FILES				1111
#define BUTTON_FILES_DONE		1112

#define ALERT_ERROR				1000
#define ALERT_CONFIRM			1002

#define SCROLL_ARROW_W			9		//the list draws its scroll arrows over the right edge of the rows

#define KEY_OTHER				0x00000001UL
#define KEY_LIBS				0x00000002UL
#define KEY_SHLIBS				0x00000003UL

static const char mStrOther[] = "Other data";
static const char mStrLibs[] = "Drivers & extensions";
static const char mStrShLibs[] = "Shared libraries";
static const char mStrSystem[] = "System settings";
static const char mStrSpace[] = " ";
static const char mStrNothingChosen[] = "Nothing is chosen";
static const char mStrPickGroup[] = "Tap a line first, then Files";

struct KnownCreator {
	UInt32 creator;
	UInt8 cls;
	const char *title;
};

//creators whose owner is in ROM, so no application file comes with them in a backup
static const struct KnownCreator mKnown[] = {
	{'addr', GROUP_CLASS_BUILTIN, "Address (built-in)"},
	{'date', GROUP_CLASS_BUILTIN, "Date Book (built-in)"},
	{'memo', GROUP_CLASS_BUILTIN, "Memo Pad (built-in)"},
	{'todo', GROUP_CLASS_BUILTIN, "To Do (built-in)"},
	{'mail', GROUP_CLASS_BUILTIN, "Mail (built-in)"},
	{'npad', GROUP_CLASS_BUILTIN, "Note Pad (built-in)"},
	{'exps', GROUP_CLASS_BUILTIN, "Expense (built-in)"},
	{'psys', GROUP_CLASS_SYSTEM, mStrSystem},
	{'netl', GROUP_CLASS_SYSTEM, "System: network"},
	{'netw', GROUP_CLASS_SYSTEM, "System: network"},
	{'modm', GROUP_CLASS_SYSTEM, "System: connections"},
	{'graf', GROUP_CLASS_SYSTEM, "System: Graffiti"},
	{'lnch', GROUP_CLASS_SYSTEM, "System: Applications"},
	{'sync', GROUP_CLASS_SYSTEM, "System: HotSync"},
};

static UInt8 choosePrvKindOf(const struct BackupItem *it)
{
	if (backupItemIsPrefsDb(it))
		return ITEM_KIND_PREFS;
	
	if (it->isRes) switch (it->type) {
		case 'appl':	return ITEM_KIND_APP;
		case 'HACK':	return ITEM_KIND_HACK;
		case 'panl':	return ITEM_KIND_PANEL;
		case 'libr':	return ITEM_KIND_SHLIB;
		case 'libf':
		case 'libs':
		case 'extn':
		case 'extx':
		case 'ovly':	return ITEM_KIND_LIBRARY;
	}
	return ITEM_KIND_DATA;
}

static UInt16 choosePrvFindGroup(const struct ChooseGroup *groups, UInt16 numGroups, UInt32 key)
{
	UInt16 i;
	
	for (i = 0; i < numGroups; i++) {
		if (groups[i].key == key)
			return i;
	}
	return 0xffff;
}

static UInt16 choosePrvAddGroup(struct ChooseGroup *groups, UInt16 *numGroupsP, UInt16 maxGroups, UInt32 key, UInt8 cls, const char *title)
{
	UInt16 idx = choosePrvFindGroup(groups, *numGroupsP, key);
	
	if (idx != 0xffff)
		return idx;
	
	if (*numGroupsP >= maxGroups) {		//no room: everything else lands in the last group (not expected in practice)
		
		idx = maxGroups - 1;
		groups[idx].key = KEY_OTHER;
		groups[idx].cls = GROUP_CLASS_OTHER;
		StrCopy(groups[idx].title, mStrOther);
		return idx;
	}
	
	idx = (*numGroupsP)++;
	MemSet(&groups[idx], sizeof(groups[idx]), 0);
	groups[idx].key = key;
	groups[idx].cls = cls;
	StrNCopy(groups[idx].title, title, dmDBNameLength - 1);
	return idx;
}

void chooseRecount(const struct BackupItem *items, UInt16 num, struct ChooseGroup *groups, UInt16 numGroups)
{
	UInt16 i;
	
	for (i = 0; i < numGroups; i++) {
		groups[i].count = 0;
		groups[i].numSel = 0;
		groups[i].size = 0;
	}
	for (i = 0; i < num; i++) {
		struct ChooseGroup *g = &groups[items[i].group];
		
		g->count++;
		g->size += items[i].size;
		if (items[i].selected)
			g->numSel++;
	}
}

UInt16 chooseBuildGroups(struct BackupItem *items, UInt16 num, struct ChooseGroup *groups, UInt16 maxGroups)
{
	UInt16 numGroups = 0, i, j, k;
	
	for (i = 0; i < num; i++)
		items[i].kind = choosePrvKindOf(&items[i]);
	
	//first the owners: every application, hack and panel starts a group named after it
	for (k = ITEM_KIND_APP; k <= ITEM_KIND_PANEL; k++) {
		for (i = 0; i < num; i++) {
			
			UInt8 cls;
			
			if (items[i].kind != k)
				continue;
			
			if (k == ITEM_KIND_HACK || items[i].creator == 'ExtM' /* X-Master: its data is the list of active hacks */)
				cls = GROUP_CLASS_HACK;
			else if (k == ITEM_KIND_PANEL)
				cls = GROUP_CLASS_PANEL;
			else
				cls = GROUP_CLASS_APP;
			
			(void)choosePrvAddGroup(groups, &numGroups, maxGroups, items[i].creator, cls, items[i].name);
		}
	}
	
	//then place every file
	for (i = 0; i < num; i++) {
		
		UInt16 g;
		
		if (items[i].kind == ITEM_KIND_LIBRARY)		//never under an application, whatever its creator: these are system code
			g = choosePrvAddGroup(groups, &numGroups, maxGroups, KEY_LIBS, GROUP_CLASS_LIBS, mStrLibs);
		else if (items[i].kind == ITEM_KIND_SHLIB)
			g = choosePrvAddGroup(groups, &numGroups, maxGroups, KEY_SHLIBS, GROUP_CLASS_SHLIB, mStrShLibs);
		else if (items[i].kind == ITEM_KIND_PREFS)
			g = choosePrvAddGroup(groups, &numGroups, maxGroups, 'psys', GROUP_CLASS_SYSTEM, mStrSystem);
		else {
			
			g = choosePrvFindGroup(groups, numGroups, items[i].creator);
			if (g == 0xffff) {
				
				for (j = 0; j < sizeof(mKnown) / sizeof(*mKnown) && mKnown[j].creator != items[i].creator; j++);
				
				if (j < sizeof(mKnown) / sizeof(*mKnown))
					g = choosePrvAddGroup(groups, &numGroups, maxGroups, items[i].creator, mKnown[j].cls, mKnown[j].title);
				else
					g = choosePrvAddGroup(groups, &numGroups, maxGroups, KEY_OTHER, GROUP_CLASS_OTHER, mStrOther);
			}
		}
		items[i].group = (UInt8)g;
	}
	
	//order the groups: by class, then by name. Insertion sort - there are only a few dozen
	for (i = 1; i < numGroups; i++) {
		
		struct ChooseGroup tmp = groups[i];
		
		for (j = i; j && (groups[j - 1].cls > tmp.cls || (groups[j - 1].cls == tmp.cls && StrCaselessCompare(groups[j - 1].title, tmp.title) > 0)); j--)
			groups[j] = groups[j - 1];
		groups[j] = tmp;
	}
	
	//the sort moved groups around: point the items at them again
	for (i = 0; i < num; i++) {
		
		UInt32 key;
		
		if (items[i].kind == ITEM_KIND_LIBRARY)
			key = KEY_LIBS;
		else if (items[i].kind == ITEM_KIND_SHLIB)
			key = KEY_SHLIBS;
		else if (items[i].kind == ITEM_KIND_PREFS)
			key = 'psys';
		else {
			key = items[i].creator;
			if (choosePrvFindGroup(groups, numGroups, key) == 0xffff)
				key = KEY_OTHER;
		}
		j = choosePrvFindGroup(groups, numGroups, key);
		items[i].group = (UInt8)(j == 0xffff ? numGroups - 1 : j);
	}
	
	chooseRecount(items, num, groups, numGroups);
	
	return numGroups;
}

void chooseApplyPreset(struct BackupItem *items, UInt16 num, struct ChooseGroup *groups, UInt16 numGroups, UInt8 preset)
{
	UInt16 i;
	
	for (i = 0; i < num; i++) {
		
		UInt8 cls = groups[items[i].group].cls;
		Boolean userStuff = (cls == GROUP_CLASS_APP || cls == GROUP_CLASS_BUILTIN || cls == GROUP_CLASS_OTHER || cls == GROUP_CLASS_SHLIB);
		
		switch (preset) {
			case PRESET_EVERYTHING:
				items[i].selected = true;
				break;
			
			case PRESET_APPS_AND_DATA:	//what is safe to move to another device: no system settings, drivers, hacks or panels
										//(shared libraries are included: applications need them)
				items[i].selected = userStuff;
				break;
			
			case PRESET_DATA_ONLY:		//as above, without the programs
				items[i].selected = userStuff && items[i].kind == ITEM_KIND_DATA;
				break;
			
			default:
				items[i].selected = false;
				break;
		}
	}
	chooseRecount(items, num, groups, numGroups);
}


//---- user interface ----

static struct BackupItem *mItems;
static UInt16 mNumItems;
static struct ChooseGroup *mGroups;
static UInt16 mNumGroups;
static UInt16 mCurGroup;			//whose files the file list shows
static char mSummary[48];
static Int16 mPenX;					//where the pen went down (the list does not tell us)

static void* choosePrvObj(FormPtr fp, UInt16 id)
{
	return FrmGetObjectPtr(fp, FrmGetObjectIndex(fp, id));
}

static void choosePrvSizeStr(char *dst, UInt32 size)
{
	StrIToA(dst, (size + 1023) / 1024);
	StrCat(dst, "K");
}

//a check box: empty, crossed (all), or with a bar (some)
static void choosePrvDrawBox(Int16 x, Int16 y, UInt16 numSel, UInt16 count)
{
	RectangleType r;
	
	RctSetRectangle(&r, x + 1, y + 2, 7, 7);
	WinEraseRectangle(&r, 0);
	WinDrawRectangleFrame(simpleFrame, &r);
	
	if (numSel && numSel == count) {
		WinDrawLine(x + 2, y + 3, x + 6, y + 7);
		WinDrawLine(x + 6, y + 3, x + 2, y + 7);
	}
	else if (numSel) {
		WinDrawLine(x + 2, y + 5, x + 6, y + 5);
		WinDrawLine(x + 2, y + 6, x + 6, y + 6);
	}
}

static void choosePrvDrawRow(RectangleType *bounds, UInt16 numSel, UInt16 count, const char *name, const char *right)
{
	Int16 rightW = FntCharsWidth(right, StrLen(right));
	Int16 x = bounds->topLeft.x, y = bounds->topLeft.y;
	
	choosePrvDrawBox(x, y, numSel, count);
	WinGlueDrawTruncChars(name, StrLen(name), x + 12, y, bounds->extent.x - 12 - rightW - 4 - SCROLL_ARROW_W);
	WinDrawChars(right, StrLen(right), x + bounds->extent.x - rightW - 1 - SCROLL_ARROW_W, y);
}

static void choosePrvGroupDraw(Int16 itemNum, RectangleType *bounds, Char **itemsText)
{
	const struct ChooseGroup *g;
	char right[24];
	
	if (itemNum < 0 || itemNum >= (Int16)mNumGroups)
		return;
	g = &mGroups[itemNum];
	
	StrIToA(right, g->count);
	StrCat(right, "  ");
	choosePrvSizeStr(right + StrLen(right), g->size);
	
	choosePrvDrawRow(bounds, g->numSel, g->count, g->title, right);
}

//the n-th item of the current group
static struct BackupItem* choosePrvGroupItem(Int16 n)
{
	UInt16 i;
	
	for (i = 0; i < mNumItems; i++) {
		if (mItems[i].group == mCurGroup && !n--)
			return &mItems[i];
	}
	return NULL;
}

static void choosePrvFileDraw(Int16 itemNum, RectangleType *bounds, Char **itemsText)
{
	static const char *const kinds[] = {"Data", "App", "Hack", "Panel", "Driver", "Prefs", "Lib"};
	const struct BackupItem *it = choosePrvGroupItem(itemNum);
	char right[24];
	
	if (!it)
		return;
	
	StrCopy(right, kinds[it->kind]);
	StrCat(right, "  ");
	choosePrvSizeStr(right + StrLen(right), it->size);
	
	choosePrvDrawRow(bounds, it->selected ? 1 : 0, 1, it->name, right);
}

static void choosePrvUpdateSummary(FormPtr fp, Boolean draw)
{
	UInt16 i, numSel = 0;
	UInt32 size = 0;
	FieldPtr fi = choosePrvObj(fp, FIELD_SUMMARY);
	
	for (i = 0; i < mNumItems; i++) {
		if (mItems[i].selected) {
			numSel++;
			size += mItems[i].size;
		}
	}
	
	StrIToA(mSummary, numSel);
	StrCat(mSummary, " of ");
	StrIToA(mSummary + StrLen(mSummary), mNumItems);
	StrCat(mSummary, " files, ");
	choosePrvSizeStr(mSummary + StrLen(mSummary), size);
	
	FldSetTextPtr(fi, mSummary);
	if (draw)
		FldDrawField(fi);
}

static Boolean choosePrvFilesHandler(EventType *e)
{
	if (e->eType == lstSelectEvent && e->data.lstSelect.listID == LIST_FILES) {
		
		struct BackupItem *it = choosePrvGroupItem(e->data.lstSelect.selection);
		
		if (it) {
			it->selected = !it->selected;
			LstSetSelection(e->data.lstSelect.pList, noListSelection);
			LstDrawList(e->data.lstSelect.pList);
		}
		return true;
	}
	return false;
}

static void choosePrvFilesDialog(UInt16 group)
{
	FormPtr fp = FrmInitForm(FORM_FILES);
	ListPtr list = choosePrvObj(fp, LIST_FILES);
	
	mCurGroup = group;
	FrmSetTitle(fp, mGroups[group].title);
	LstSetDrawFunction(list, choosePrvFileDraw);
	LstSetListChoices(list, NULL, mGroups[group].count);
	LstSetSelection(list, noListSelection);
	FrmSetEventHandler(fp, choosePrvFilesHandler);
	(void)FrmDoDialog(fp);
	FrmDeleteForm(fp);
	
	chooseRecount(mItems, mNumItems, mGroups, mNumGroups);
}

static Boolean choosePrvGroupsHandler(EventType *e)
{
	FormPtr fp = FrmGetActiveForm();
	ListPtr list;
	
	switch (e->eType) {
		
		case penDownEvent:
			mPenX = e->screenX;
			break;
		
		case lstSelectEvent:
			if (e->data.lstSelect.listID == LIST_GROUPS) {
				
				Int16 sel = e->data.lstSelect.selection;
				
				//a tap on the check box (or anywhere left of the name) ticks or unticks the whole group;
				//a tap on the name just picks the line, for the Files button
				if (sel >= 0 && sel < (Int16)mNumGroups && mPenX < 20) {
					
					Boolean on = (mGroups[sel].numSel != mGroups[sel].count);	//some or none -> all, all -> none
					UInt16 i;
					
					for (i = 0; i < mNumItems; i++) {
						if (mItems[i].group == sel)
							mItems[i].selected = on;
					}
					chooseRecount(mItems, mNumItems, mGroups, mNumGroups);
					LstDrawList(e->data.lstSelect.pList);
					choosePrvUpdateSummary(fp, true);
				}
				return true;
			}
			break;
		
		case popSelectEvent:
			if (e->data.popSelect.listID == LIST_PRESET) {
				
				list = choosePrvObj(fp, LIST_GROUPS);
				chooseApplyPreset(mItems, mNumItems, mGroups, mNumGroups, (UInt8)e->data.popSelect.selection);
				LstDrawList(list);
				choosePrvUpdateSummary(fp, true);
				//let the system set the trigger's label
			}
			break;
		
		case ctlSelectEvent:
			if (e->data.ctlSelect.controlID == BUTTON_FILES) {
				
				Int16 sel;
				
				list = choosePrvObj(fp, LIST_GROUPS);
				sel = LstGetSelection(list);
				if (sel == noListSelection || sel >= (Int16)mNumGroups)
					FrmCustomAlert(ALERT_ERROR, mStrPickGroup, mStrSpace, mStrSpace);
				else {
					choosePrvFilesDialog(sel);
					LstDrawList(list);
					choosePrvUpdateSummary(fp, true);
				}
				return true;
			}
			break;
		
		default:
			break;
	}
	return false;
}

//says what is about to happen, in plain words, and asks
static Boolean choosePrvConfirm(void)
{
	static const char sAsk[] = "Restore ";
	static const char sWhole[] = "?\n\nSystem settings will be REPLACED by the ones in the backup.\n\nThe device then resets.";
	static const char sMerge[] = "?\n\nSettings of the chosen apps come back too. This device's own system settings are kept.\n\nThe device then resets.";
	Boolean wholePrefs = false;
	UInt16 i;
	
	for (i = 0; i < mNumItems; i++) {
		if (mItems[i].selected && backupItemIsPrefsDb(&mItems[i]))
			wholePrefs = true;
	}
	
	return FrmCustomAlert(ALERT_CONFIRM, sAsk, mSummary, wholePrefs ? sWhole : sMerge) == 0;
}

Boolean chooseRun(struct BackupItem *items, UInt16 num, const char *bkpName)
{
	Boolean go = false;
	FormPtr fp;
	ListPtr list;
	UInt16 button, i, numSel;
	
	mGroups = MemPtrNew((UInt32)MAX_GROUPS * sizeof(struct ChooseGroup));
	if (!mGroups)
		return false;
	
	mItems = items;
	mNumItems = num;
	mNumGroups = chooseBuildGroups(items, num, mGroups, MAX_GROUPS);
	chooseApplyPreset(items, num, mGroups, mNumGroups, PRESET_APPS_AND_DATA);
	
	fp = FrmInitForm(FORM_GROUPS);
	list = choosePrvObj(fp, LIST_GROUPS);
	LstSetDrawFunction(list, choosePrvGroupDraw);
	LstSetListChoices(list, NULL, mNumGroups);
	LstSetSelection(list, noListSelection);
	LstSetSelection(choosePrvObj(fp, LIST_PRESET), PRESET_APPS_AND_DATA);
	CtlSetLabel(choosePrvObj(fp, TRIGGER_PRESET), LstGetSelectionText(choosePrvObj(fp, LIST_PRESET), PRESET_APPS_AND_DATA));
	FrmSetEventHandler(fp, choosePrvGroupsHandler);
	choosePrvUpdateSummary(fp, false);
	
	while (1) {
		
		button = FrmDoDialog(fp);
		if (button != BUTTON_RESTORE)
			break;
		
		for (i = 0, numSel = 0; i < num; i++)
			numSel += items[i].selected ? 1 : 0;
		
		if (!numSel)
			FrmCustomAlert(ALERT_ERROR, mStrNothingChosen, mStrSpace, mStrSpace);
		else if (choosePrvConfirm()) {
			go = true;
			break;
		}
	}
	
	FldSetTextPtr(choosePrvObj(fp, FIELD_SUMMARY), NULL);
	FrmDeleteForm(fp);
	MemPtrFree(mGroups);
	mGroups = NULL;
	
	return go;
}
