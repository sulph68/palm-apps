///////////////////////////////////////////////////////////////////////////////
//                                                                           //
//        Program Name:  McPhling.c                                          //
//              Author:  Mike McCollister (© 2000-2009)                      //
//            Language:  PRC-Tools 2.3                                       //
//                       Palm OS 5 SDK R3                                    //
//                       PilRC 3.2                                           //
//                       HandEra Header Files from SDK 1.04                  // 
//                       palmOne Header Files from SDK version 5.1           //
//                       Sony Header Files from SDK 5.0 version 1.2          //
//               Notes:  Distributed under the GNU GPL.                      //
//         Description:  Main application file for Palm OS 5+ version of     //
//                       "McPhling".                                         //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

///////////////////
// Include Files //
///////////////////

#include <PalmOS.h>

#include "common.h"
#include "endianUtils.h"
#include "McPhling.h"
#include "McPhling.rch"

/////////////
// Defines //
/////////////

#define ALARM_REF		1

//////////////////////
// Global Variables //
//////////////////////

//////////////////////////////////
// External Function Prototypes //
//////////////////////////////////

extern Boolean PrefsEventHandler(EventPtr event);
extern Err HandleSwipe(PointType* start, PointType* end);
extern void DoSwipeStuff(Boolean bUseList, Boolean bVgaExists,
                         AppListPreSelect alps);
extern void UpdateJumpRef(UInt16 cardNo, LocalID dbID);
extern void UpdateVFSCache(UInt16 cardNo, LocalID dbID,
                           SysAppLaunchCmdCardType *pCardLaunchCmd);
extern Boolean HandleKeys(struct _KeyDownEventType *keyDown);

/////////////////////////
// Function Prototypes //
/////////////////////////

// Common Palm Functions
static UInt16 CheckOsVersion(Boolean bDisplayAlert);
static UInt16 StartApplication(void);
static void EventLoop(void);
static Boolean AppHandleEvent(EventPtr event);
Err MySysNotifyEventDequeuedEvent(SysNotifyParamType *np);
static DmResID CreateCmdBarBitmapsDB(void);
static void DeleteCmdBarBitmapsDB(void);

///////////////////
// Main Function //
///////////////////

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP,
                 UInt16 launchFlags __attribute__ ((unused)))
{
   UInt16 error;
   DmSearchStateType searchState;
   UInt16 cardNo, myCardNo;
   LocalID dbID, myDbID;
   McPhlingPrefs mpPrefs;
   McActivation MpActivation;

   if(cmd == sysAppLaunchCmdNotify)
   { // make first to improve performance
      SysNotifyParamType *np = (SysNotifyParamType *)cmdPBP;

      switch(np->notifyType)
      {
         case sysNotifyEventDequeuedEvent:
            // this is here in preparation for Palm OS 6 which will not allow
            // the user of notifications with function pointers
            MySysNotifyEventDequeuedEvent(np);
            break;

         case sysNotifyAppLaunchingEvent:
            {
               SysNotifyAppLaunchOrQuitType *alp =
                  (SysNotifyAppLaunchOrQuitType *)np->notifyDetailsP;

               UpdateJumpRef(alp->cardNo, alp->dbID);
            }
            break;

         case sysNotifySyncStartEvent:
            // turn off all notifications (makes HotSync faster)
            EnableOS5(false);

            // enable sysNotifySyncFinishEvent (to turn back on notifications)
            SysCurAppDatabase(&myCardNo, &myDbID);
            SysNotifyRegister(myCardNo, myDbID, sysNotifySyncFinishEvent,
                              NULL, sysNotifyNormalPriority, NULL);

            // put HotSync in the MRU apps list (if not ignored)
            error = DmGetNextDatabaseByTypeCreator(true, &searchState,
                                                   sysFileTApplication,
                                                   sysFileCSync,
                                                   true, &cardNo, &dbID);
            if(error == errNone)
               UpdateJumpRef(cardNo, dbID);
            break;

         case sysNotifySyncFinishEvent:
            // turn on all notifications
            EnableOS5(true);

            // disable sysNotifySyncFinishEvent (to turn back on notifications)
            SysCurAppDatabase(&myCardNo, &myDbID);
            SysNotifyUnregister(myCardNo, myDbID, sysNotifySyncFinishEvent,
                                NULL);
            break;

         case sysNotifyDeleteProtectedEvent:
            {
               SysNotifyDBInfoType *dbi =
                  (SysNotifyDBInfoType *)np->notifyDetailsP;

               // if "McPhling" is being deleted, then unlock it
               if(dbi->creator == APP_ID)
               {
                  EnableOS5(false);
                  np->handled = true;
               }
            }
            break;

         case sysNotifyMenuCmdBarOpenEvent:
            GetActivation(&MpActivation);

            if(MpActivation.bCommandBar)
            {
               DmResID cbb = CreateCmdBarBitmapsDB();
               if(cbb != NULL)
                  MenuCmdBarAddButton(menuCmdBarOnLeft, cbb,
                                      menuCmdBarResultNotify, APP_ID,
                                      CMD_BAR_TEXT);
            }
            break;

         case APP_ID:
            {
               Boolean *bp = (Boolean *)np->notifyDetailsP;
               Boolean bUseList;

               if(bp)
                  bUseList = *bp;
               else
                  bUseList = true;

               DoSwipeStuff(bUseList, false, alpsConfigureSelect);
               np->handled = true;
            }
            break;

         default:
            break;
      }
   }
   else if(cmd == sysAppLaunchCmdNormalLaunch)
   {
      error = CheckOsVersion(true);
      if(error)
      {
         if(GetROMVer(NULL, NULL) == 1)
            AppLaunchWithCommand(sysFileCDefaultApp,
                                 sysAppLaunchCmdNormalLaunch, NULL);
         return error;
      }

      StartApplication();

      EventLoop();
   }
   else if(cmd == sysAppLaunchCmdCardLaunch)
   {
      SysAppLaunchCmdCardType *lc = (SysAppLaunchCmdCardType *)cmdPBP;

      lc->err = errNone;

      error = CheckOsVersion(true);
      if(error)
      {
         if(GetROMVer(NULL, NULL) == 1)
            AppLaunchWithCommand(sysFileCDefaultApp,
                                 sysAppLaunchCmdNormalLaunch, NULL);
         return error;
      }

      FrmAlert(MemoryCardAlert);
   }
   else if(cmd == sysAppLaunchCmdSyncNotify)
   {
      if(CheckOsVersion(false) == 0)
      {
         GetPrefsAndNewIgnored(&mpPrefs);
         EnableOS5(mpPrefs.bEnabled);
      }
   }
   else if(cmd == sysAppLaunchCmdSystemReset)
   {
      if(CheckOsVersion(false) == 0)
      {
         GetPrefsAndNewIgnored(&mpPrefs);
            if(mpPrefs.bEnabled)
            {
               EnableOS5(mpPrefs.bEnabled);
            }
      }
   }

   return 0;
}

Err MySysNotifyEventDequeuedEvent(SysNotifyParamType *np)
{
   EventPtr ep = (EventPtr)np->notifyDetailsP;

   if(ep->eType == ByteSwap16(penUpEvent))
   {
      struct _PenUpEventType *pup = &(ep->data.penUp);
      PointType startPt, endPt;

      startPt.x = ByteSwap16(pup->start.x);
      startPt.y = ByteSwap16(pup->start.y);
      endPt.x   = ByteSwap16(pup->end.x);
      endPt.y   = ByteSwap16(pup->end.y);
 
      np->handled = HandleSwipe(&startPt, &endPt);
      if(np->handled)
      {
         MemSet(ep, sizeof(EventType), 0);
         ep->eType = ByteSwap16(nilEvent);
      }
   }
   else if(ep->eType == ByteSwap16(keyDownEvent))
   {
      struct _KeyDownEventType  kd;
      struct _KeyDownEventType *kdp = &(ep->data.keyDown);
               
      kd.chr       = ByteSwap16(kdp->chr);
      kd.keyCode   = ByteSwap16(kdp->keyCode);
      kd.modifiers = ByteSwap16(kdp->modifiers);

      np->handled = HandleKeys(&kd);
      if(np->handled)
      {
         MemSet(ep, sizeof(EventType), 0);
         ep->eType = ByteSwap16(nilEvent);
      }
   }
   else if(ep->eType == ByteSwap16(menuEvent))
   {
      DeleteCmdBarBitmapsDB();
   }
   else
   {
      np->handled = false;
   }

   return 0;
}

///////////////////////////
// Common Palm Functions //
///////////////////////////

static UInt16 CheckOsVersion(Boolean bDisplayAlert)
{
   UInt16 error = 0;
   UInt16 ROMVerMajor = GetROMVer(NULL, NULL);

   // check if PalmOS 5.0
   if(ROMVerMajor != 5)
   {
      if(bDisplayAlert)
         FrmAlert(VersionAlert);
      error = sysErrRomIncompatible;
   }

   return error;
}

static UInt16 StartApplication(void)
{
   FrmGotoForm(PrefsForm);

   return 0;
}

static void EventLoop(void)
{
   short err;
   EventType event;

   do
   {
      // wait for event or timeout after a very long time
      EvtGetEvent(&event, evtWaitForever);

      // get DIA updates sent to hidden form if necessary
      if((event.eType == winEnterEvent) && FrmGetActiveForm())
      {
         EventType newEvent;
	
         MemSet(&newEvent, sizeof(EventType), 0);
         newEvent.eType = winDisplayChangedEvent;
         EvtAddUniqueEventToQueue(&newEvent, 0, true);
      }

      // handle any system events
      if(SysHandleEvent(&event))
         continue;

      // handle any menu events (before I get messages)
      if(MenuHandleEvent(0, &event, &err))
         continue;

      // handle application events
      if(AppHandleEvent(&event))
         continue;

      // dispatch event to appropriate form
      FrmDispatchEvent(&event);

   } while(event.eType != appStopEvent);

   FrmCloseAllForms();
}

static Boolean AppHandleEvent(EventPtr event)
{
   Int32 formID;
   FormPtr pForm;

   // load appropriate form
   if(event->eType == frmLoadEvent)
   {
      formID = event->data.frmLoad.formID;
      pForm = FrmInitForm(formID);
      FrmSetActiveForm(pForm);
      switch(formID)
      {
         case PrefsForm:
            FrmSetEventHandler(pForm, PrefsEventHandler);
            break;
         default:
            ErrNonFatalDisplayIf(true, "Invalid Form Load Event");
            return true;
            break;

      }
   }

   return false;
}

static DmResID CreateCmdBarBitmapsDB(void)
{
   DmResID newCBB;
   DmOpenRef dbCmdBarBmp;
   Boolean bPositionFound = false;
   Err err;

   // delete Cmd Bar bitmaps DB
   DeleteCmdBarBitmapsDB();

   // Search for an unused resource for the bitmap
   for(newCBB = CmdBarAppsBitmap - 1000; newCBB < CmdBarAppsBitmap; newCBB++)
   {
      MemHandle hBitmap;

      hBitmap = DmGetResource(bitmapRsc, newCBB);
      if(hBitmap == NULL)
      {
         bPositionFound = true;
         break;
      }
   }
           
   if(bPositionFound)
   {
      err = DmCreateDatabase(0, DB_NAME_CMDBAR, APP_ID, DB_TYPE_CMDBAR, true);
      if(err == errNone)
      {
         MemHandle hOrigCmdBarBmp;
         MemHandle hCmdBarBmp;

         dbCmdBarBmp = DmOpenDatabaseByTypeCreator(DB_TYPE_CMDBAR, APP_ID,
                                                   dmModeReadWrite);
         hOrigCmdBarBmp = DmGetResource(bitmapRsc, CmdBarAppsBitmap);
         hCmdBarBmp = DmNewResource(dbCmdBarBmp, bitmapRsc, newCBB,
                                    MemHandleSize(hOrigCmdBarBmp));
         DmWrite(MemHandleLock(hCmdBarBmp), 0, MemHandleLock(hOrigCmdBarBmp),
                 MemHandleSize(hOrigCmdBarBmp));
         MemHandleUnlock(hCmdBarBmp);
         MemHandleUnlock(hOrigCmdBarBmp);
         DmReleaseResource(hCmdBarBmp);
         DmCloseDatabase(dbCmdBarBmp);
      }

      // open up Cmd Bar Bitmaps database
      dbCmdBarBmp = DmOpenDatabaseByTypeCreator(DB_TYPE_CMDBAR, APP_ID,
                                                dmModeReadOnly);
      FtrSet(APP_ID, FTR_CMDBMPDBID, (UInt32)dbCmdBarBmp);
   }
   else
   {
      newCBB = NULL;
   }

   return newCBB;
}

static void DeleteCmdBarBitmapsDB(void)
{
   LocalID dbCmdBarDB;
   DmOpenRef dbCmdBarBmp;
   Err err;

   // delete Cmd Bar bitmap database
   dbCmdBarDB = DmFindDatabase(0, DB_NAME_CMDBAR);
   if(dbCmdBarDB)
   {
      DmDeleteDatabase(0, dbCmdBarDB);
   }

   // close Cmd Bar bitmap database if it is still open
   err = DmCreateDatabase(0, DB_NAME_CMDBAR, APP_ID, DB_TYPE_CMDBAR, true);
   if(err == dmErrAlreadyExists)
   {
      if(FtrGet(APP_ID, FTR_CMDBMPDBID, (UInt32 *)&dbCmdBarBmp) == 0)
      {
         DmCloseDatabase(dbCmdBarBmp);
         FtrUnregister(APP_ID, FTR_CMDBMPDBID);
      }
   }

   // delete Cmd Bar bitmap database if still exists
   dbCmdBarDB = DmFindDatabase(0, DB_NAME_CMDBAR);
   if(dbCmdBarDB)
   {
      DmDeleteDatabase(0, dbCmdBarDB);
   }
}
