#include <PalmOS.h>
#include <VfsMgr.h>
#include <WinGlue.h>
#include "ops.h"
#include "choose.h"

//this nonsense is needed to avoid strings ending up in ".data" somehow


static const char mStrTaskFailedSuccessfully[] = "Task faled successfully";
static const char mStrComeBackWithCard[] = "Come back when you have an expansion card inserted";
static const char mStrSpaceSlashSpace[] = " / ";
static const char mStrAnErrorOccured[] = "An error occured: ";
static const char mStrNeedSelection[] = "You need to select a backup to do this";
static const char mStrRestoring[] = "Restoring...";
static const char mStrReadingBackup[] = "Reading backup...";
static const char mStrBackingUp[] = "Backing up...";
static const char mStrDeleting[] = "Deleting...";
static const char mStrNothing[] = "";
static const char mStrNeedVfs[] = "Expansion card support is needed for this application to work";
static const char mStrError0x[] = "Error 0x";
static const char mStrOccured[] = " occured";
static const char mStrCardNo[] = "Card #";
static const char mStrAbout[] = "Bkp1 - the PalmOS 1.0 compatible backup tool\n\n(c) 2022\npalmpowerups.com";
static const char mStrSpace[] = " ";
static const char mStrNotRestored[] = " database(s) could not be restored and were skipped:";
static const char mStrAndMore[] = "\n(and more)";
static const char mStrNewline[] = "\n";
static const char mStrColonSpace[] = ": ";






#define MAX_VOLUMES							10

#define EVENT_TYPE_VOLUMES_CHANGED			(firstUserEvent + 0)
#define ERR_USER_CANCELLED					(0xffff)

struct BackupEntry {
	struct BackupEntry *next;
	char name[1];	//really variable length (was "name[]", which gcc 2.95 rejects); allocations add StrLen + 1
};

struct VolumeEntry {
	UInt16 vrn;
	char name[16];
};


//restore failures are only noted while a restore runs, and reported once at its end
#define MAX_RESTORE_FAILS_LISTED			4

struct RestoreFail {
	char name[dmDBNameLength];
	Err e;
};

static struct RestoreFail mRestoreFails[MAX_RESTORE_FAILS_LISTED];
static UInt16 mNumRestoreFails;		//how many happened (may be more than we have room to list)

static UInt8 mNumVolumes, mCurVolume;
static struct VolumeEntry mVolumes[MAX_VOLUMES];
struct BackupEntry *mBackups;


static void uiPrvErrAlert(Err e)
{
	if (e == errNone)
		SysFatalAlert(mStrTaskFailedSuccessfully);
	else {
	
		UInt8 specifier = e & 0xff;
		MemHandle mh;
		char x[9];
		
		if (specifier) {
		
			mh = DmGetResource('err!', 0x1000 + (e >> 8));
			if (mh) {
				
				char *res = MemHandleLock(mh), *end = res + MemHandleSize(mh), *cls = res;
				
				while (res < end && specifier--)
					res += StrLen(res) + 1;
				
				if (res < end) {
					
					FrmCustomAlert(1000, mStrAnErrorOccured, cls, res);
					MemHandleUnlock(mh);
					return;
				}
				
				MemHandleUnlock(mh);
			}
		}
		
		StrIToH(x, e);
		FrmCustomAlert(1000, mStrError0x, x + 4, mStrOccured);
	}
}

static void* uiPrvFrmGetObjectPtrFromId(FormPtr fp, UInt16 id)
{
	return FrmGetObjectPtr(fp, FrmGetObjectIndex(fp, id));
}

static FieldPtr uiPrvSetTextField(FormPtr fp, UInt16 fldIdx, const char *strIn, UInt16 len)
{
	FieldPtr fi = uiPrvFrmGetObjectPtrFromId(fp, fldIdx);
	MemHandle mh;
	
	mh = FldGetTextHandle(fi);
	FldSetTextHandle(fi, NULL);
	if (mh)
		MemHandleFree(mh);
	
	mh = MemHandleNew(len + 1);
	if (mh) {
		char *dst = MemHandleLock(mh);
		
		MemMove(dst, strIn, len);
		dst[len] = 0;
		
		MemHandleUnlock(mh);
		FldSetTextHandle(fi, mh);
		FldDrawField(fi);
	}
	
	return fi;
}

static const char* uiPrvGetFieldText(FormPtr fp, UInt16 fldIdx)
{
	const char *ret = FldGetTextPtr(uiPrvFrmGetObjectPtrFromId(fp, fldIdx));
	
	if (!ret)
		ret = mStrNothing;
	
	return ret;
}

static UInt16 backupsPrvCount(const struct BackupEntry *list)
{
	const struct BackupEntry *t;
	UInt16 count = 0;
	
	for (t = list, count = 0; t; t = t->next, count++);
	
	return count;
}

static struct BackupEntry* backupsPrvSort(struct BackupEntry *list)
{
	struct BackupEntry *t, *p, *ret = NULL;
	UInt16 count = 0;

	count = backupsPrvCount(list);
	
	//if zero or one, we're done
	if (count < 2)
		return list;
	
	//get middle of list
	for (t = list, count /= 2; count; p = t, t = t->next, count--);
	
	//terminate first list
	p->next = NULL;
	
	//sort each half
	list = backupsPrvSort(list);
	t = backupsPrvSort(t);
	
	//merge
	ret = NULL;
	p = NULL;
	while (list || t) {
		
		struct BackupEntry *lesser;
		
		if (!t || (list && StrCaselessCompare(list->name, t->name) <= 0)) {
			
			lesser = list;
			list = list->next;
		}
		else {
			
			lesser = t;
			t = t->next;
		}
		
		
		lesser->next = NULL;
		if (p)
			p->next = lesser;
		else
			ret = lesser;
		p = lesser;
	}
	
	return ret;
}

static void backupsPrvFree(struct BackupEntry *list)
{
	struct BackupEntry *cur, *next;
	
	for (cur = list; cur; cur = next) {
		
		next = cur->next;
		MemPtrFree(cur);
	}
}

static Err backupsPrvEnumerateEnumF(void *userData, const char *name)
{
	struct BackupEntry **listP = userData, *t;
	
	t = MemPtrNew(sizeof(struct BackupEntry) + StrLen(name) + 1);
	if (!t)
		return memErrNotEnoughSpace;
	
	t->next = *listP;
	StrCopy(t->name, name);
	*listP = t;
	
	return errNone;
}

static struct BackupEntry* backupsPrvEnumerate(UInt16 vrn)
{
	struct BackupEntry *list = NULL;
	Err e;
	
	e = backupEnumerate(vrn, backupsPrvEnumerateEnumF, &list);
	if (e != errNone) {
	
		backupsPrvFree(list);
		return NULL;
	}
	
	return backupsPrvSort(list);
}

static void uiPrvObjectHide(FormPtr fp, UInt16 objId)
{
	UInt16 idx = FrmGetObjectIndex(fp, objId);
	Boolean needWorkaroundBitmapHiding;
	UInt32 osVer;
	void *ptr;
	
	/*
		PalmOS 3.1 and below cannot properly hide form bitmaps since they test for "visible" bit but never set it
		This patch sets visible and usable bits before hiding it to force an erase as expected, and clears them after.
		FrmShowObject() works as expected since when visible is clear, it will draw the object
	*/
	
	needWorkaroundBitmapHiding = FrmGetObjectType(fp, idx) == frmBitmapObj && (errNone != FtrGet(sysFtrCreator, sysFtrNumROMVersion, &osVer) || osVer < sysMakeROMVersion(3, 5, 0, sysROMStageRelease, 0));
	
	if (needWorkaroundBitmapHiding) {
		ptr = FrmGetObjectPtr(fp, idx);
		(*(UInt8*)ptr) |= 0xa0;
	}
	
	FrmHideObject(fp, idx);
	
	if (needWorkaroundBitmapHiding)
		(*(UInt8*)ptr) &=~ 0xa0;
}

static void uiPrvObjectShow(FormPtr fp, UInt16 objId)
{
	FrmShowObject(fp, FrmGetObjectIndex(fp, objId));
}

static void uiRefresh(void)
{
	FormPtr fp = FrmGetFormPtr(1000);
	ListPtr list = uiPrvFrmGetObjectPtrFromId(fp, 1000);

	LstSetSelection(list, noListSelection);
	LstSetListChoices(list, NULL, backupsPrvCount(mBackups));
	
	FrmDrawForm(fp);
}

static void uiPrvBackupListAndUiRefresh(void)
{
	backupsPrvFree(mBackups);
	mBackups = backupsPrvEnumerate(mVolumes[mCurVolume].vrn);
	
	uiRefresh();
}

static Boolean uiPrvVolumesChanged(void)
{
	UInt16 curVrn =  mVolumes[mCurVolume].vrn, vrn, idx = 0;
	Boolean redrawNeeded = false, curFound = false;
	UInt32 dummy;
	
	mCurVolume = 0;
	for (dummy = vfsIteratorStart; dummy != vfsIteratorStop && errNone == VFSVolumeEnumerate(&vrn, &dummy); idx++) {
		
		mVolumes[idx].vrn = vrn;

		if (curVrn == vrn) {
			redrawNeeded = true;
			curFound = true; 
			mCurVolume = idx;
		}
		
		if (errNone != VFSVolumeGetLabel(vrn, mVolumes[idx].name, sizeof(mVolumes[idx].name)) || !mVolumes[idx].name[0]) {
			
			StrCopy(mVolumes[idx].name, mStrCardNo);
			mVolumes[idx].name[5] = idx + '0';
		}
	}
	mNumVolumes = idx;

	if (!curFound)
		mCurVolume = 0;
	
	if (!idx) {
		EventType e;
		
		FrmCustomAlert(1000, mStrComeBackWithCard, mStrSpace, mStrSpace);
		MemSet(&e, sizeof(e), 0);
		e.eType = appStopEvent;
		EvtAddEventToQueue(&e);
		
		return false;
	}
	
	LstSetListChoices(uiPrvFrmGetObjectPtrFromId(FrmGetFormPtr(1000), 3100), NULL, idx);

	return redrawNeeded;
}

static struct BackupEntry* uiPrvGetCurEntry(void)
{
	Int16 sel = LstGetSelection(uiPrvFrmGetObjectPtrFromId(FrmGetFormPtr(1000), 1000));
	struct BackupEntry *t;
	
	if (sel == noListSelection) {
	
		FrmCustomAlert(1000, mStrNeedSelection, mStrSpace, mStrSpace);
		return NULL;
	}
	
	for (t = mBackups; t; t = t->next) {
		
		if (!sel--)
			return t;
	}
	
	return NULL;
}

static void* uiProgressStart(const char *title)
{
	FormPtr fp = FrmInitForm(1001), old = FrmGetActiveForm();
	
	FrmSetTitle(fp, (char*)title);
	FrmSetActiveForm(fp);
	FrmDrawForm(fp);
	
	return old;
}

static void uiProgressEnd(void *state)
{
	FormPtr fp = FrmGetActiveForm(), old = state;
	
	FrmEraseForm(fp);
	FrmDeleteForm(fp);
	FrmSetActiveForm(old);
	FrmDrawForm(old);
}

static void uiPrvProportionToString(char *dst, UInt32 top, UInt32 bottom)
{
	StrIToA(dst, top);
	StrCat(dst, mStrSpaceSlashSpace);
	StrIToA(dst + StrLen(dst), bottom);
}

static void uiPrvSetTextIfChanged(FormPtr fp, UInt16 idx, const char *txt)
{
	//redraw costs if done a lot, so we check first...
	
	if (StrCompare(uiPrvGetFieldText(fp, idx), txt))
		uiPrvSetTextField(fp, idx, txt, StrLen(txt));
}

static void uiProgressUpdate(void *state, const char *stateName, UInt16 fileIdx, UInt16 fileNum, UInt32 size, UInt32 sizeTotal, UInt32 permille)		//only growth of progress is supported
{
	FormPtr fp = FrmGetActiveForm();
	RectangleType r;
	char x[24];
	
	
	uiPrvSetTextIfChanged(fp, 1000, stateName);
		
	uiPrvProportionToString(x,  size, sizeTotal);
	uiPrvSetTextIfChanged(fp, 1001, x);

	uiPrvProportionToString(x, fileIdx + 1, fileNum);
	uiPrvSetTextIfChanged(fp, 1002, x);
	
	//FrmUpdateForm(1001, frmRedrawUpdateCode);
	
	RctSetRectangle(&r, 3, 66, 150, 14);
	WinDrawRectangleFrame(simpleFrame, &r);
	RctSetRectangle(&r, 4, 67, (148ULL * permille) / 1000000, 12);
	WinDrawRectangle(&r, 0);
	
	(void)EvtResetAutoOffTimer();
}

static Err uiPrvOpProgressF(void *userData, enum BackupOpType op, UInt16 idx, UInt16 of, UInt32 size, UInt32 sizeOf, const char *name)
{
	UInt32 permille;
	EventType e;
	Err err;
	
	EvtGetEvent(&e, 0);
	
	if (!SysHandleEvent(&e) && !MenuHandleEvent(NULL, &e, &err) && !FrmHandleEvent(FrmGetActiveForm(), &e) && e.eType == ctlSelectEvent && e.data.ctlSelect.controlID == 8000)
		return ERR_USER_CANCELLED;
	
	if (op == BackupCopying)
		permille = (1000000ULL * (size + 1) + (sizeOf + 1) / 2) / (sizeOf + 1);
	else if (op == BackupDeleting)
		permille = (1000000ULL * (idx + 1) + (of + 1) / 2) / (of + 1);
	else
		return errNone;
	
	uiProgressUpdate(userData, name, idx, of, size, sizeOf, permille);
	
	return errNone;
}

static Err uiPrvOpBackupErrHandlerF(void *data, const char *name, Err e)
{
	//no errors to ignoe on backup
	
	return e;
}


//appends a short name for the error (e.g. "vfsErrBadData", from the same 'err!' tables uiPrvErrAlert uses) or "0x2A0C"
static void uiPrvAppendErrName(char *dst, Err e)
{
	UInt8 specifier = e & 0xff;
	MemHandle mh;
	char x[9];
	
	if (specifier && (mh = DmGetResource('err!', 0x1000 + (e >> 8))) != NULL) {
		
		char *res = MemHandleLock(mh), *end = res + MemHandleSize(mh), *cls = res;
		
		while (res < end && specifier--)
			res += StrLen(res) + 1;
		
		if (res < end) {
			
			StrCat(dst, cls);
			StrCat(dst, res);
			MemHandleUnlock(mh);
			return;
		}
		MemHandleUnlock(mh);
	}
	
	StrIToH(x, e);
	StrCat(dst, "0x");
	StrCat(dst, x + 4);
}

//one alert listing what a restore could not bring back
static void uiPrvRestoreReport(void)
{
	UInt16 i, listed = mNumRestoreFails < MAX_RESTORE_FAILS_LISTED ? mNumRestoreFails : MAX_RESTORE_FAILS_LISTED;
	char count[maxStrIToALen];
	char *text;
	
	if (!mNumRestoreFails)
		return;
	
	text = MemPtrNew((UInt32)listed * (1 + dmDBNameLength + 2 + 40) + sizeof(mStrAndMore));	//newline, name, ": ", error name
	if (!text)
		return;
	
	text[0] = 0;
	for (i = 0; i < listed; i++) {
		
		StrCat(text, mStrNewline);
		StrCat(text, mRestoreFails[i].name);
		StrCat(text, mStrColonSpace);
		uiPrvAppendErrName(text, mRestoreFails[i].e);
	}
	
	if (mNumRestoreFails > listed)
		StrCat(text, mStrAndMore);
	
	StrIToA(count, mNumRestoreFails);
	FrmCustomAlert(1000, count, mStrNotRestored, text);	//(all three strings are non-empty, as FrmCustomAlert needs)
	
	MemPtrFree(text);
	mNumRestoreFails = 0;
}

static Err uiPrvOpRestoreErrHandlerF(void *data, const char *name, Err e)
{
	if (!name) {	//restore is done: report now, before the reset prompt
		
		uiPrvRestoreReport();
		return errNone;
	}
	
	if (e == dmErrDatabaseProtected)
		e = errNone;
	
	if (e != errNone) {		//only note it - do not stop the restore to ask
		
		if (mNumRestoreFails < MAX_RESTORE_FAILS_LISTED) {
			
			StrNCopy(mRestoreFails[mNumRestoreFails].name, name, dmDBNameLength - 1);
			mRestoreFails[mNumRestoreFails].name[dmDBNameLength - 1] = 0;
			mRestoreFails[mNumRestoreFails].e = e;
		}
		mNumRestoreFails++;
	}
	
	return errNone;	//we always try to continue
}

static void uiPrvOpBackup(void)
{
	char name[longDateStrLength + timeStringLength + 2];
	DateTimeType dt;
	void *state;
	Err e;
	

	TimSecondsToDateTime(TimGetSeconds(), &dt);
	DateToAscii(dt.month, dt.day, dt.year, dfMDYLongWithComma, name);
	StrCat(name, mStrSpace);
	TimeToAscii(dt.hour, dt.minute, tfDotAMPM, name + StrLen(name));
	
	state = uiProgressStart(mStrBackingUp);
	e = backupDo(mVolumes[mCurVolume].vrn, name, uiPrvOpProgressF, uiPrvOpBackupErrHandlerF, state);

	if (e != errNone && e != ERR_USER_CANCELLED)
		uiPrvErrAlert(e);
	
	uiProgressEnd(state);
	
	uiPrvBackupListAndUiRefresh();
}

static void uiPrvOpDelete(void)
{
	struct BackupEntry *bkp = uiPrvGetCurEntry();
	void *state;
	Err e;
	
	if (!bkp)
		return;
	
	state = uiProgressStart(mStrDeleting);
	e = backupDelete(mVolumes[mCurVolume].vrn, bkp->name, uiPrvOpProgressF, state);
	uiProgressEnd(state);

	if (e != errNone && e != ERR_USER_CANCELLED)
		uiPrvErrAlert(e);
	
	uiPrvBackupListAndUiRefresh();
}

static void uiPrvOpRestore(void)
{
	struct BackupEntry *bkp = uiPrvGetCurEntry();
	void *state;
	Err e;
	
	if (!bkp)
		return;
	
	//everything (as always), or let the user choose which files go back
	switch (FrmAlert(1003)) {
		
		case 0:		//everything
			mNumRestoreFails = 0;
			state = uiProgressStart(mStrRestoring);
			e = backupRestore(mVolumes[mCurVolume].vrn, bkp->name, uiPrvOpProgressF, uiPrvOpRestoreErrHandlerF, state);
			uiProgressEnd(state);
			break;
		
		case 1: {	//choose
			
			struct BackupItem *items;
			UInt16 num;
			
			state = uiProgressStart(mStrReadingBackup);
			e = backupScan(mVolumes[mCurVolume].vrn, bkp->name, &items, &num, uiPrvOpProgressF, state);
			uiProgressEnd(state);
			if (e != errNone)
				break;
			
			if (num && chooseRun(items, num, bkp->name)) {
				
				mNumRestoreFails = 0;
				state = uiProgressStart(mStrRestoring);
				e = backupRestoreSelected(mVolumes[mCurVolume].vrn, bkp->name, items, num, uiPrvOpProgressF, uiPrvOpRestoreErrHandlerF, state);
				uiProgressEnd(state);
			}
			if (items)
				MemPtrFree(items);
			break;
		}
		
		default:	//cancel
			return;
	}

	//we only get here if the restore stopped early (a completed one resets the device)
	if (e != errNone && e != ERR_USER_CANCELLED)
		uiPrvErrAlert(e);
	uiPrvRestoreReport();	//whatever was skipped before it stopped
}

static void uiPrvOpRename(void)
{
	struct BackupEntry *bkp = uiPrvGetCurEntry();
	UInt16 button;
	MemHandle mh;
	FieldPtr fi;
	FormPtr fp;
	Err e;
	
	if (!bkp)
		return;
	
	mh = MemHandleNew(StrLen(bkp->name) + 1);
	if (!mh)
		return;
	
	StrCopy(MemHandleLock(mh), bkp->name);
	MemHandleUnlock(mh);
	
	fp = FrmInitForm(1002);
	fi = FrmGetObjectPtr(fp, FrmGetObjectIndex(fp, 1000));
	FldSetTextHandle(fi, mh);
	button = FrmDoDialog(fp);
	FldSetTextHandle(fi, NULL);
	FrmDeleteForm(fp);
	
	e = (button == 2000) ? backupRename(mVolumes[mCurVolume].vrn, bkp->name, MemHandleLock(mh)) : errNone;
	MemHandleUnlock(mh);
	MemHandleFree(mh);
	
	if (e != errNone)
		uiPrvErrAlert(e);
}

static Boolean uiPrvMainFormEventHandler(EventType *e)
{
	struct BackupEntry *t;

	if (e->eType == popSelectEvent) {
		
		UInt8 newVolumeIdx = e->data.popSelect.selection;
		
		CtlSetLabel(e->data.popSelect.controlP, mVolumes[newVolumeIdx].name);
		
		if (newVolumeIdx != mCurVolume) {
			mCurVolume = newVolumeIdx;
			uiPrvBackupListAndUiRefresh();
		}
		
		return true;
	}
	else if (e->eType == ctlSelectEvent) switch (e->data.ctlSelect.controlID) {
		case 8000:
			uiPrvOpBackup();
			break;
		
		case 8001:
			uiPrvOpDelete();
			break;
		
		case 8002:
			uiPrvOpRestore();
			break;
	}
	else if (e->eType == menuEvent)  switch (e->data.menu.itemID) {
		
		case 1000:
			uiPrvOpBackup();
			break;
		
		case 1001:
			uiPrvOpDelete();
			break;
		
		case 1002:
			uiPrvOpRestore();
			break;
		
		case 1003:
			uiPrvOpRename();
			uiPrvBackupListAndUiRefresh();
			break;
		
		case 9000:
			FrmCustomAlert(1001, mStrAbout, mStrSpace, mStrSpace);
			break;
	}
	else if (e->eType == EVENT_TYPE_VOLUMES_CHANGED) {
		
		if (uiPrvVolumesChanged())
			uiPrvBackupListAndUiRefresh();
	}

	return false;
}


static void uiPrvBackupListDrawFunc(Int16 itemNum, RectangleType *bounds, Char **itemsText)
{
	//slow, but saves memory
	const struct BackupEntry *t = mBackups;
	
	if (itemNum < 0)
		return;
	
	while (itemNum-- && t)
		t = t->next;
	
	WinGlueDrawTruncChars(t->name, StrLen(t->name), bounds->topLeft.x, bounds->topLeft.y, bounds->extent.x);
}


static void uiPrvVolumeListDrawFunc(Int16 itemNum, RectangleType *bounds, Char **itemsText)
{
	const char *name = mVolumes[itemNum].name;
	Int16 x = bounds->topLeft.x;
	
	MemHandle mh = DmGetResource('Tbmp', 1001);
	WinDrawBitmap(MemHandleLock(mh), x, bounds->topLeft.y + 1);
	MemHandleUnlock(mh);
	DmReleaseResource(mh);
	x += 7;

	WinDrawChars(name, StrLen(name), x, bounds->topLeft.y);
}

static void uiLoop(void)
{
	EventType e;
	FormPtr fp;
	Err err;
	
	while (1) {
		EvtGetEvent(&e, -1);
		
		if (SysHandleEvent(&e))
			continue;
		
		if (MenuHandleEvent(NULL, &e, &err))
			continue;
		
		switch (e.eType) {
			case appStopEvent:
				FrmCloseAllForms();
				return;
			
			case frmLoadEvent:
				fp = FrmInitForm(e.data.frmLoad.formID);
				FrmSetActiveForm(fp);
				
				switch (e.data.frmLoad.formID) {
					case 1000:
						LstSetDrawFunction(uiPrvFrmGetObjectPtrFromId(fp, 1000), uiPrvBackupListDrawFunc);
						LstSetDrawFunction(uiPrvFrmGetObjectPtrFromId(fp, 3100), uiPrvVolumeListDrawFunc);
						FrmSetEventHandler(fp, uiPrvMainFormEventHandler);
						uiPrvVolumesChanged();
						CtlSetLabel(uiPrvFrmGetObjectPtrFromId(fp, 3000), mVolumes[0].name);
						uiPrvBackupListAndUiRefresh();
						break;
				}
				break;
			
			case frmOpenEvent:
				FrmDrawForm(FrmGetActiveForm());
				break;
		}
		
		FrmDispatchEvent(&e);
	}
}

static Err uiPrvVolumesChangedNotif(SysNotifyParamType *notifyParamsP)
{
	EventType e;
	
	MemSet(&e, sizeof(e), 0);
	e.eType = EVENT_TYPE_VOLUMES_CHANGED;
	
	EvtAddEventToQueue(&e);
	
	return errNone;
}

#ifdef __GNUC__

//prc-tools build: its own startup code has already done SysAppStartup and calls PilotMain
#define BKP1_ENTRY_BEGIN	UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags) { {
#define BKP1_ENTRY_CMD		cmd
#define BKP1_ENTRY_END		}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags);

#else

//CodeWarrior build: this IS the startup code
#define BKP1_ENTRY_BEGIN	UInt32 __Startup__(void) { void *prevGlobalsP, *globalsP; SysAppInfoPtr appInfoP; if (SysAppStartup(&appInfoP, &prevGlobalsP, &globalsP) == errNone) {
#define BKP1_ENTRY_CMD		appInfoP->cmd
#define BKP1_ENTRY_END		SysAppExit(appInfoP, prevGlobalsP, globalsP); }

UInt32 __Startup__(void);

#endif

BKP1_ENTRY_BEGIN
		
		if (BKP1_ENTRY_CMD == sysAppLaunchCmdNormalLaunch) {
		
			UInt16 cardNo;
			UInt32 dummy;
			LocalID LID;
			
			SysCurAppDatabase(&cardNo, &LID);
			
#ifdef BKP1_FAKE_VFS	//test build: the "card" is a backup embedded in this PRC (test/fakevfs.c)
			if (0)
#else
			if (errNone != FtrGet(sysFileCVFSMgr, vfsFtrIDVersion, &dummy) || !dummy)
#endif
				FrmCustomAlert(1000, mStrNeedVfs, mStrSpace, mStrSpace);
			else {

				//subscribe to volume changes, if notif mgr exists
				if (errNone == FtrGet(sysFtrCreator, sysFtrNumNotifyMgrVersion, &dummy) && dummy) {
					
					(void)SysNotifyRegister(cardNo, LID, sysNotifyVolumeMountedEvent, uiPrvVolumesChangedNotif, sysNotifyNormalPriority, NULL);
					(void)SysNotifyRegister(cardNo, LID, sysNotifyVolumeUnmountedEvent, uiPrvVolumesChangedNotif, sysNotifyNormalPriority, NULL);
				}
				
				FrmGotoForm(1000);
				uiLoop();

				//unsubscribe to volume changes, if notif mgr exists
				if (errNone == FtrGet(sysFtrCreator, sysFtrNumNotifyMgrVersion, &dummy) && dummy) {
					
					(void)SysNotifyUnregister(cardNo, LID, sysNotifyVolumeMountedEvent, sysNotifyNormalPriority);
					(void)SysNotifyUnregister(cardNo, LID, sysNotifyVolumeUnmountedEvent, sysNotifyNormalPriority);
				}
				
				backupsPrvFree(mBackups);
			}
		}
			
	BKP1_ENTRY_END
	
	return 0;
}





