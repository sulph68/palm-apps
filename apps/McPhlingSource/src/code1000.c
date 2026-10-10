///////////////////////////////////////////////////////////////////////////////
//                                                                           //
//        Program Name:  code1000.c                                          //
//              Author:  Mike McCollister (� 2000-2009)                      //
//            Language:  PRC-Tools 2.3                                       //
//                       Palm OS 5 SDK R3                                    //
//                       PilRC 3.2                                           //
//                       HandEra Header Files from SDK 1.04                  // 
//                       palmOne Header Files from SDK version 5.1           //
//                       Sony Header Files from SDK 5.0 version 1.2          //
//               Notes:  Distributed under the GNU GPL.                      //
//         Description:  SysHandleEvent trap handler for McPhling.           //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

///////////////////
// Include Files //
///////////////////

#include <PalmOS.h>
#include <PalmOSGlue.h>

#include <Standalone.h>

#include "common.h"
#include "McPhling.h"
#include "McPhling.rch"

/////////////
// Defines //
/////////////

#define STATIC_MODIFIERS    ( shiftKeyMask      | \
                              capsLockMask      | \
                              numLockMask       | \
                              commandKeyMask    | \
                              optionKeyMask     | \
                              controlKeyMask    | \
                              poweredOnKeyMask  | \
                              appEvtHookKeyMask | \
                              libEvtHookKeyMask | \
                              willSendUpKeyMask   \
                            )
#define DYNAMIC_MODIFIERS   ( autoRepeatKeyMask | \
                              doubleTapKeyMask    \
                            )

////////////////
// Structures //
////////////////

typedef struct
{
   JumpRefPref jumpRef;
   UInt16      cardNo;
   LocalID     dbID;
   Char        name[dmDBNameLength + 1];
   Boolean     bInVfs;
   Int16       vfsVolIndex;
   Char        vfsPath[VFS_MAX_PATH + 1];
} JumpRefPlus;

typedef struct
{
   Boolean        bVgaExists;
   Boolean        bVgaLargeFont;
   FontID         McStdFont;
   FontID         McBoldFont;
   Int16          indexStart;
   Int16          xOffsetText;
   JumpRefPlus    jumpRefPlus[MAX_NUM_ITEMS + 1];
   VFSInfo        vfsInfo;
   McPhlingPrefs *pMpPrefs;
} DrawListData;

///////////////////////////////////////
// Swipe Related Function Prototypes //
///////////////////////////////////////

Boolean HandleSwipe(PointType* start, PointType* end);
static void GetSwipeBounds(McActivation *pMpActivation, Boolean bVgaExists,
                           RectangleType *pSwipeAppsFrom,
                           RectangleType *pSwipeAppsTo,
                           RectangleType *pSwipeBackFrom,
                           RectangleType *pSwipeBackTo,
                           RectangleType *pRectAppsButton);
static void GetSwipeBoundsSony(McActivation *pMpActivation,
                               RectangleType *pSwipeAppsFrom,
                               RectangleType *pSwipeAppsTo,
                               RectangleType *pSwipeBackFrom,
                               RectangleType *pSwipeBackTo);
static void GetSwipeBound(UInt16 location, RectangleType *pBound,
                          RectangleType *pRectApps,
                          RectangleType *pRectMenu,
                          RectangleType *pRectCalc,
                          RectangleType *pRectFind,
                          RectangleType *pRectGraffiti,
                          RectangleType *pRectScreen);
static void DoSwipeStuffWrap(Boolean bUseList, Boolean bVgaExists,
                             AppListPreSelect alps);
void DoSwipeStuff(Boolean bUseList, Boolean bVgaExists, AppListPreSelect alps);
static Int16 MyLstPopupList(ListPtr pList, RectangleType *pRectListAbs);
static void DrawList(UInt16 itemnum, RectanglePtr bounds, Char **unused);
static Int16 JumpRefSortMRUFavTop(JumpRefPlus *a1, JumpRefPlus *a2,
                                  Int32 unused);
static Int16 JumpRefSortMRUFavBottom(JumpRefPlus *a1, JumpRefPlus *a2,
                                     Int32 unused);
static Int16 JumpRefSortAlpha(JumpRefPlus *a1, JumpRefPlus *a2, Int32 unused);
static Int16 JumpRefSortAlphaFavTop(JumpRefPlus *a1, JumpRefPlus *a2,
                                    Int32 unused);
static Int16 JumpRefSortAlphaFavBottom(JumpRefPlus *a1, JumpRefPlus *a2,
                                       Int32 unused);
static Boolean GetIconNameFromCache(UInt16 cardNo, LocalID dbID, UInt32 creator,
                                    Char *pIconChar);
static void myWipe(void);
static void RunApp(UInt32 creator, UInt16 cardNo, LocalID dbID);
Boolean GetLastAppCreatorID(JumpRefPlus jumpRefPlus[], UInt32 *pLastAppCreator,
                            UInt16 *pJumpNum);

// VFS Specific Code
static void VFSFindApps(DrawListData *drawListData);
static void VFSRunApp(VFSInfo *vfsInfo, UInt32 creator, Int16 vfsVolIndex,
                      Char *vfsPath, Boolean bVgaExists);
#ifdef STANDALONE
static void VFSUpdateCache(VFSInfo *vfsInfo);
#endif

// DA Specific Code
static void RunDa(UInt16 cardNo, LocalID dbID, Boolean bVgaExists);

/////////////////////////////////////
// Key Related Function Prototypes //
/////////////////////////////////////

Boolean HandleKeys(struct _KeyDownEventType *keyDown);
static Coord CheckSpecificKey(struct _KeyDownEventType *keyDown);

///////////////
// Functions //
///////////////

#ifndef STANDALONE
#define RESOURCE_ID 1000
HACKMASTER_TRAP(sysTrapSysHandleEvent);
STANDALONE_CODE_RESOURCE_ID(RESOURCE_ID);

Boolean MySysHandleEvent(EventPtr eventP)
{
   UInt32 temp;
   Boolean (*oldTrap)(EventPtr);
   Boolean bDoneDoAction = false;

   // get the address of the old SysHandleEvent() from HackMaster
   FtrGet(APP_ID, RESOURCE_ID, &temp);

   // set the function pointer to the old trap
   oldTrap = (Boolean (*)(EventPtr))temp;

   // only bring up if not in McPhling activation form 
   if(FtrGet(APP_ID, FTR_ACTTEMP, &temp) == ftrErrNoSuchFeature)
   {
      switch(eventP->eType)
      {
         case penUpEvent:
            bDoneDoAction = HandleSwipe(&eventP->data.penUp.start,
                                        &eventP->data.penUp.end);
            break;

         case keyDownEvent:
            bDoneDoAction = HandleKeys(&eventP->data.keyDown);
            break;

         default:
            bDoneDoAction = false;
            break;
      }
   }

   // call old trap (if necessary)
   if(bDoneDoAction)
   {
      // clear this event
      MemSet(eventP, sizeof(EventType), 0);
      eventP->eType = nilEvent;

      return true;
   }
   else
   {
      return oldTrap(eventP);
   }
}
#endif

///////////////////////////////////////
// Swipe Related Function Prototypes //
///////////////////////////////////////

Boolean HandleSwipe(PointType* start, PointType* end)
{
   UInt32 temp;
   Boolean bVgaExists = false;

   // see if vga exists
   if(_TRGVGAFeaturePresent(&temp))
      bVgaExists = true;

   // don't try if in activation form
   if(FtrGet(APP_ID, FTR_ACTTEMP, (UInt32 *)&temp) != ftrErrNoSuchFeature)
      return false;

   if(start->x == KEY_APP_LIST)
   {
      // in this case start->y is the selection option
      myWipe();
      DoSwipeStuffWrap(true, bVgaExists, start->y);
      myWipe();
      return true;
   }
   else if(start->x == KEY_APP_LAST)
   {
      myWipe();
      DoSwipeStuffWrap(false, bVgaExists, alpsNoSelect);
      myWipe();
      return true;
   }
   else
   {
      McActivation mpActivation;

      GetActivation(&mpActivation);
      if(mpActivation.bSwipe)
      {
         RectangleType swipeAppsFrom, swipeAppsTo;
         RectangleType swipeBackFrom, swipeBackTo;
         RectangleType rectAppsButton;
         Boolean bSonySilk = false;
         UInt16 refNum;
         Boolean bGraffitiAreaVisible = true;
         Coord x, y;

         // some Sony specific values
         if(SysLibFind(sonySysLibNameSilk, &refNum) != sysErrLibNotFound)
            bSonySilk = true;

         GetSwipeBounds(&mpActivation, bVgaExists,
                        &swipeAppsFrom, &swipeAppsTo,
                        &swipeBackFrom, &swipeBackTo,
                        &rectAppsButton);

         // adjust if graffiti area is not visible
         WinGetDisplayExtent(&x, &y);
#if 0
{
   Char sTemp[80];
   StrPrintF(sTemp, "%d, %d, %d, %d", x, y, rectAppsButton.topLeft.x,
             rectAppsButton.topLeft.y);
   SysFatalAlert(sTemp);
   StrPrintF(sTemp, "start = (%d, %d), end = (%d, %d)",
             start->x, start->y, end->x, end->y);
   SysFatalAlert(sTemp);
   StrPrintF(sTemp, "af = (%d, %d), (%d, %d)",
             swipeAppsFrom.topLeft.x, swipeAppsFrom.topLeft.y,
             swipeAppsFrom.extent.x, swipeAppsFrom.extent.y);
   SysFatalAlert(sTemp);
   StrPrintF(sTemp, "at = (%d, %d), (%d, %d)",
             swipeAppsTo.topLeft.x, swipeAppsTo.topLeft.y,
             swipeAppsTo.extent.x, swipeAppsTo.extent.y);
   SysFatalAlert(sTemp);
}
#endif
/*
         if((rectAppsButton.topLeft.y > MAX_HEIGHT) && 
            (rectAppsButton.topLeft.x > MAX_WIDTH))
         {
*/
         if((rectAppsButton.topLeft.y < y))
            bGraffitiAreaVisible = false;

         if(bGraffitiAreaVisible)
         {
            if(RctPtInRectangle(start->x, start->y, &swipeAppsFrom))
            {
               if(RctPtInRectangle(end->x, end->y, &swipeAppsTo))
               {
                  myWipe();
                  DoSwipeStuffWrap(true, bVgaExists, alpsNoSelect);
                  myWipe();
                  return true;
               }
            }
            if(RctPtInRectangle(start->x, start->y, &swipeBackFrom))
            {
               if(RctPtInRectangle(end->x, end->y, &swipeBackTo))
               {
                  myWipe();
                  DoSwipeStuffWrap(false, bVgaExists, alpsNoSelect);
                  myWipe();
                  return true;
               }
            }
         }

         // do sony minimized silk buttons
         if(bSonySilk)
         {
            GetSwipeBoundsSony(&mpActivation, &swipeAppsFrom, &swipeAppsTo,
                               &swipeBackFrom, &swipeBackTo);

            if(RctPtInRectangle(start->x, start->y, &swipeAppsFrom))
            {
               if(RctPtInRectangle(end->x, end->y, &swipeAppsTo))
               {
                  myWipe();
                  DoSwipeStuffWrap(true, bVgaExists, alpsNoSelect);
                  myWipe();
                  return true;
               }
            }
            if(RctPtInRectangle(start->x, start->y, &swipeBackFrom))
            {
               if(RctPtInRectangle(end->x, end->y, &swipeBackTo))
               {
                  myWipe();
                  DoSwipeStuffWrap(false, bVgaExists, alpsNoSelect);
                  myWipe();
                  return true;
               }
            }
         }
      }
   }

   return false;
}

static void GetSwipeBounds(McActivation *pMpActivation, Boolean bVgaExists,
                           RectangleType *pSwipeAppsFrom,
                           RectangleType *pSwipeAppsTo,
                           RectangleType *pSwipeBackFrom,
                           RectangleType *pSwipeBackTo,
                           RectangleType *pRectAppsButton)
{
   Boolean bOS35OrGreater;
   UInt16 ROMVerMajor;
   UInt16 ROMVerMinor;
   RectangleType rectApps            = {{  0, 161}, { 24,  32}};
   RectangleType rectMenu            = {{  0, 191}, { 24,  32}};
   RectangleType rectCalc            = {{136, 161}, { 24,  32}};
   RectangleType rectFind            = {{136, 191}, { 24,  32}};
   RectangleType rectGraffiti        = {{ 27, 164}, {106,  56}};
   RectangleType rectGraffitiAlpha   = {{ 27, 164}, { 62,  56}};
   RectangleType rectGraffitiNumeric = {{ 89, 164}, { 44,  56}};
   RectangleType rectScreen          = {{  0,   0}, {MAX_WIDTH, MAX_HEIGHT}};

   // check if PalmOS 3.5 or greater
   GetROMVer(&ROMVerMajor, &ROMVerMinor);
   if(ROMVerMajor < 3 || (ROMVerMajor == 3 && ROMVerMinor < 5))
      bOS35OrGreater = false;
   else
      bOS35OrGreater = true;

   // kludge to get to work on Palm OS 5 to prevent a PACE shadow from not
   // being freeded when using notification callback function
/*
   if(ROMVerMajor >= 5)
      bOS35OrGreater = false;
*/

   if(bOS35OrGreater)
   {
      UInt16 i;
      UInt16 numButtons = 0, numAreas = 0;
      PenBtnInfoType *penBtnInfo;
      SilkscreenAreaType *silkscreenArea;
#ifdef STANDALONE
      UInt32 temp;
#endif

      // get button and silk areas
#ifdef STANDALONE
      if(FtrGet(APP_ID, FTR_PSCOLLGRAFFITI, &temp) == ftrErrNoSuchFeature)
      {
         if(FtrGet(APP_ID, FTR_NUMBUTTONS, &temp) == 0)
         {
            numButtons = temp;
            FtrGet(APP_ID, FTR_BUTTONS, (UInt32 *)&penBtnInfo);
         }
         if(FtrGet(APP_ID, FTR_NUMSILK, &temp) == 0)
         {
            numAreas = temp;
            FtrGet(APP_ID, FTR_SILK, (UInt32 *)&silkscreenArea);
         }
      }
      else
      {
#endif
         penBtnInfo = (PenBtnInfoType *)EvtGetPenBtnList(&numButtons);
         silkscreenArea =
            (SilkscreenAreaType *)EvtGetSilkscreenAreaList(&numAreas);
#ifdef STANDALONE
      }
#endif

      // assign rect values for buttons
      for(i = 0; i < numButtons ; i++)
      {
         // need to take into account multiple copies of rectApps, rectMenu
         // rectCalc and rectFind
         
         switch(penBtnInfo[i].asciiCode)
         {
            case vchrLaunch:
               rectApps = penBtnInfo[i].boundsR;
               break;
            case vchrMenu:
               rectMenu = penBtnInfo[i].boundsR;
               break;
            case vchrCalc:
               rectCalc = penBtnInfo[i].boundsR;
               break;
            case vchrFind:
               rectFind = penBtnInfo[i].boundsR;
               break;
         }
      }

      // assign rect values for silkscreen
      for(i = 0; i < numAreas ; i++)
      {
         switch(silkscreenArea[i].areaType)
         {
            case silkscreenRectGraffiti:
               switch(silkscreenArea[i].index)
               {
                  case alphaGraffitiSilkscreenArea:
                     rectGraffitiAlpha = silkscreenArea[i].bounds;
                     break;
   
                  case numericGraffitiSilkscreenArea:
                     rectGraffitiNumeric = silkscreenArea[i].bounds;
                  break;
               }
               break;
   
            case silkscreenRectScreen:
               rectScreen = silkscreenArea[i].bounds;
               break;
         }
      }

      // set whole graffiti rectangle
      if(rectGraffitiAlpha.topLeft.y < rectGraffitiNumeric.topLeft.y)
      {
         rectGraffiti           = rectGraffitiAlpha;
         rectGraffiti.extent.y += rectGraffitiNumeric.extent.y;
      }
      else if(rectGraffitiAlpha.topLeft.y > rectGraffitiNumeric.topLeft.y)
      {
         rectGraffiti           = rectGraffitiNumeric;
         rectGraffiti.extent.y += rectGraffitiAlpha.extent.y;
      }
      else
      {
         rectGraffiti           = rectGraffitiAlpha;
         rectGraffiti.extent.x += rectGraffitiNumeric.extent.x;
      }

      // fix screen extent for the HandEra 330.
      if(bVgaExists)
      {
         VgaScreenModeType screenMode;
         VgaRotateModeType rotateMode;

         VgaGetScreenMode(&screenMode, &rotateMode);

         if(screenMode == screenMode1To1)
         {
            rectScreen.extent.x = 320; // kludge
            rectScreen.extent.y = 320; // kludge
         }
      }
   }

   // get bounds
   GetSwipeBound(pMpActivation->swipeAppsFrom, pSwipeAppsFrom,
                 &rectApps, &rectMenu, &rectCalc, &rectFind,
                 &rectGraffiti, &rectScreen);
   GetSwipeBound(pMpActivation->swipeAppsTo, pSwipeAppsTo,
                 &rectApps, &rectMenu, &rectCalc, &rectFind,
                 &rectGraffiti, &rectScreen);
   GetSwipeBound(pMpActivation->swipeBackFrom, pSwipeBackFrom,
                 &rectApps, &rectMenu, &rectCalc, &rectFind,
                 &rectGraffiti, &rectScreen);
   GetSwipeBound(pMpActivation->swipeBackTo, pSwipeBackTo,
                 &rectApps, &rectMenu, &rectCalc, &rectFind,
                 &rectGraffiti, &rectScreen);

   *pRectAppsButton = rectApps;
}

static void GetSwipeBoundsSony(McActivation *pMpActivation,
                               RectangleType *pSwipeAppsFrom,
                               RectangleType *pSwipeAppsTo,
                               RectangleType *pSwipeBackFrom,
                               RectangleType *pSwipeBackTo)
{
   RectangleType rectApps            = {{  1, 227}, { 11,  12}};
   RectangleType rectMenu            = {{ 13, 227}, { 11,  12}};
   RectangleType rectCalc            = {{255, 227}, { 11,   0}}; // not used
   RectangleType rectFind            = {{ 25, 227}, { 11,  12}};
   RectangleType rectGraffiti        = {{255, 255}, {  0,   0}}; // not used
   RectangleType rectScreen          = {{  0,   0}, {MAX_WIDTH, 226}};
#if 0
   Coord x, y;

   // tried to get this to work with the Sony UX series devices but it
   // did not work
   WinGetDisplayExtent(&x, &y);
   if(x > MAX_WIDTH)
   {
      rectApps.topLeft.x = 227;
      rectApps.topLeft.y = 147;
      rectMenu.topLeft.x = 226;
      rectMenu.topLeft.y = 134;
   }
#endif

   // get bounds
   GetSwipeBound(pMpActivation->swipeAppsFrom, pSwipeAppsFrom,
                 &rectApps, &rectMenu, &rectCalc, &rectFind,
                 &rectGraffiti, &rectScreen);
   GetSwipeBound(pMpActivation->swipeAppsTo, pSwipeAppsTo,
                 &rectApps, &rectMenu, &rectCalc, &rectFind,
                 &rectGraffiti, &rectScreen);
   GetSwipeBound(pMpActivation->swipeBackFrom, pSwipeBackFrom,
                 &rectApps, &rectMenu, &rectCalc, &rectFind,
                 &rectGraffiti, &rectScreen);
   GetSwipeBound(pMpActivation->swipeBackTo, pSwipeBackTo,
                 &rectApps, &rectMenu, &rectCalc, &rectFind,
                 &rectGraffiti, &rectScreen);
}

static void GetSwipeBound(UInt16 location, RectangleType *pBound,
                          RectangleType *pRectApps,
                          RectangleType *pRectMenu,
                          RectangleType *pRectCalc,
                          RectangleType *pRectFind,
                          RectangleType *pRectGraffiti,
                          RectangleType *pRectScreen)
{
   switch((SwipeToType)location)
   {
      case sttApps:
         *pBound = *pRectApps;
         break;

      case sttMenu:
         *pBound = *pRectMenu;
         break;

      case sttCalc:
         *pBound = *pRectCalc;
         break;

      case sttFind:
         *pBound = *pRectFind;
         break;

      case sttGraffiti:
         *pBound = *pRectGraffiti;
         break;

      case sttScreen:
         *pBound = *pRectScreen;
         break;
   }
}

static void DoSwipeStuffWrap(Boolean bUseList,
                             Boolean bVgaExists __attribute__ ((unused)),
                             AppListPreSelect alps __attribute__ ((unused)))
{
#ifdef STANDALONE
      SysNotifyParamType np;
      Boolean bUseListNotify;

      np.notifyType     = APP_ID;
      np.broadcaster    = APP_ID;
      np.notifyDetailsP = &bUseListNotify;
      np.userDataP      = NULL;
      np.handled        = false;
#endif

   // this is being done so that I am not calling a resource from within
   // a notification with a callback function when running over a
   // arm-native programs such as HotSync

#ifdef STANDALONE
   bUseListNotify = bUseList;
   SysNotifyBroadcast(&np);
#else
   DoSwipeStuff(bUseList, bVgaExists, alpsNoSelect);
#endif
}

void DoSwipeStuff(Boolean bUseList, Boolean bVgaExists, AppListPreSelect alps)
{
   ListPtr pList;
   Int16 first, sel;
   UInt16 i, cardNo, jumpNum;
   LocalID dbID;
   UInt32 temp;
   JumpRefPref *jumpRefs = NULL;
   DmSearchStateType searchState;
   Err err;
   FontID oldFont, McStdFont = stdFont, McBoldFont = boldFont;
   McPhlingPrefs mpPrefs;
   DrawListData *drawListData = NULL;
   Coord width;
   VgaScreenModeType screenMode;
   VgaRotateModeType rotateMode;
   Boolean bVgaLargeFont = false;
   Boolean bInVfs;
   UInt8 numToCheck;
   Char *iconName = NULL;
   Char *nameList[MAX_NUM_ITEMS + 1];
   UInt16 listNumItems = 0;
   Coord listWidth = 0;
   FormPtr pFormList;
   FormPtr pFormSave;
   Boolean bOS35OrGreater;
   UInt16 ROMVerMajor;
   UInt16 ROMVerMinor;
   RectangleType rect;
   Coord screenX, screenY;
   RGBColorType rgbDialogFill, rgbObjectFill;
   UInt32 lastCreatorID;
#ifdef STANDALONE
   Boolean bVFSScanned = false;
#endif

   // get preferences
   GetPrefsAndNewIgnored(&mpPrefs);

   // Clear list if list already up
   if(bUseList)
   {
      if(FtrGet(APP_ID, FTR_DUPLICATE, &temp) == 0)
      {
         EventType event;

         event.eType   = penDownEvent;
         event.penDown = true;
         event.screenX = -1;
         event.screenY = -1;
         EvtAddEventToQueue(&event);

         event.eType   = penUpEvent;
         event.penDown = false;
         event.screenX = -1;
         event.screenY = -1;
         EvtAddEventToQueue(&event);

#ifdef STANDALONE
         // FtrUnregister(APP_ID, FTR_SWIPEWAIT);
#endif
         return;
      }
   }

   // Click to confirm to the user
   if(mpPrefs.bClick)
      CLICK;

   // set aside memory for iconName
   iconName = MemPtrNew(sizeof(Char) * (dmDBNameLength + 1));
   if(iconName == NULL)
      goto cleanup;

   // Create large data structures
   jumpRefs = MemPtrNew((MAX_NUM_ITEMS + 1) * sizeof(JumpRefPref));
   if(jumpRefs == NULL)
      goto cleanup;

   drawListData = MemPtrNew(sizeof(DrawListData));
   if(drawListData == NULL)
      goto cleanup;

   // set name list pointers
   for(i = 0; i <= MAX_NUM_ITEMS; i++)
      nameList[i] = drawListData->jumpRefPlus[i].name;

   // Get the list of cached apps
   GetMRUApps(&mpPrefs, jumpRefs);
   drawListData->pMpPrefs = &mpPrefs;

   // Put jumpRefs into jumpRef
   for(i = 0; i <= MAX_NUM_ITEMS; i++)
   {
      drawListData->jumpRefPlus[i].jumpRef = jumpRefs[i];
      drawListData->jumpRefPlus[i].bInVfs  = false;
   }

   // Setup VFS Structures
   if(mpPrefs.bVFSSupport)
   {
      VFSSetup(&drawListData->vfsInfo);
      VFSFindApps(drawListData);
   }

   // remove any deleted apps and update cardNo and dbID
   numToCheck = (bUseList ? mpPrefs.numItems : 1);
   for(i = 0; i <= mpPrefs.numItems; i++)
   {
      Boolean bGotName;
      UInt32 creator = drawListData->jumpRefPlus[i].jumpRef.creator;
      UInt32 type = sysFileTApplication;

      if(creator == 0)
         break;

      // see if application still exists
      if(drawListData->jumpRefPlus[i].jumpRef.flags & IS_A_DA)
         type = DA_APP_TYPE;
      err = 0;
      err = DmGetNextDatabaseByTypeCreator(true, &searchState, type, creator,
                                           true, &cardNo, &dbID);
      bGotName = GetIconNameFromCache(cardNo, dbID, creator, iconName);

      // see if application is on a VFS volume
      if(err && bGotName)
      {
#ifdef STANDALONE
         if(mpPrefs.bVFSSupport)
         {
            if(bVFSScanned == false)
            {
               VFSUpdateCache(&drawListData->vfsInfo);
               VFSFindApps(drawListData);
               bVFSScanned = true;
            }
         }
#endif
         bInVfs = drawListData->jumpRefPlus[i].bInVfs;
      }
      else
      {
         bInVfs = false;
         drawListData->jumpRefPlus[i].bInVfs = bInVfs;
      }
      
      if(err && !bInVfs)
      {
         // Remove the offending entry
         MemMove(&drawListData->jumpRefPlus[i],
                 &drawListData->jumpRefPlus[i + 1],
                 (mpPrefs.numItems - i) * sizeof(JumpRefPlus));
         MemSet(&drawListData->jumpRefPlus[mpPrefs.numItems],
                sizeof(JumpRefPlus), 0);
         i--;
         continue;
      }
      else
      {
         StrNCopy(drawListData->jumpRefPlus[i].name, iconName, dmDBNameLength);

         if(!bInVfs)
         {
            drawListData->jumpRefPlus[i].cardNo = cardNo;
            drawListData->jumpRefPlus[i].dbID   = dbID;
         }
      }
   }

   // Decide which action to take...
   if(bUseList)
   {
      // check if PalmOS 3.5 or greater
      GetROMVer(&ROMVerMajor, &ROMVerMinor);
      if(ROMVerMajor < 3 || (ROMVerMajor == 3 && ROMVerMinor < 5))
         bOS35OrGreater = false;
      else
         bOS35OrGreater = true;

      // do some VGA stuff
      if(bVgaExists)
      {
         VgaGetScreenMode(&screenMode, &rotateMode);
         VgaSetScreenMode(screenMode1To1, rotateMode);
         if(rotateMode == rotateModeNone || rotateMode == rotateMode180 ||
                                            ROMVerMajor >= 4)
         {
            bVgaLargeFont = true;
            McStdFont  = VgaBaseToVgaFont(stdFont);
            McBoldFont = VgaBaseToVgaFont(boldFont);
         }
      }
      drawListData->bVgaLargeFont = bVgaLargeFont;
      drawListData->bVgaExists    = bVgaExists;
      drawListData->McStdFont     = McStdFont;
      drawListData->McBoldFont    = McBoldFont;

      // determine which is the first application to show
      first = (mpPrefs.bShowCurrentApp ? 0 : 1);

      // get last app creator ID
      // (note jumpNum is not used here and is set later)
      GetLastAppCreatorID(drawListData->jumpRefPlus, &lastCreatorID, &jumpNum);

      // Find the list height and width
      oldFont = FntSetFont(McStdFont);
      for(i = first; i <= mpPrefs.numItems; i++)
      {
         if(!drawListData->jumpRefPlus[i].jumpRef.creator)
            break;

         if(drawListData->jumpRefPlus[i].jumpRef.flags & IS_A_FAV)
         {
            FntSetFont(McBoldFont);
            width = FntCharsWidth(drawListData->jumpRefPlus[i].name,
                                  StrLen(drawListData->jumpRefPlus[i].name))+1;
            FntSetFont(McStdFont);
         }
         else
         {
            width = FntCharsWidth(drawListData->jumpRefPlus[i].name,
                                  StrLen(drawListData->jumpRefPlus[i].name));
         }
         if(width > listWidth)
            listWidth = width;
         listNumItems++;
      }

      // goto cleanup if nothing on the list
      if(!listNumItems)
      {
         SndPlaySystemSound(sndWarning);
         goto cleanup;
      }

      // Sort entries
      switch(mpPrefs.sortType)
      {
         case stMRU:
            break;

         case stMRUFavTop:
            SysInsertionSort(&(drawListData->jumpRefPlus[first]),
                             listNumItems, sizeof(JumpRefPlus),
                             (CmpFuncPtr)JumpRefSortMRUFavTop, 0);
            break;

         case stMRUFavBottom:
            SysInsertionSort(&(drawListData->jumpRefPlus[first]),
                             listNumItems, sizeof(JumpRefPlus),
                             (CmpFuncPtr)JumpRefSortMRUFavBottom, 0);
            break;

         case stAlpha:
            SysInsertionSort(&(drawListData->jumpRefPlus[first]),
                             listNumItems, sizeof(JumpRefPlus),
                             (CmpFuncPtr)JumpRefSortAlpha, 0);
            break;

         case stAlphaFavTop:
            SysInsertionSort(&(drawListData->jumpRefPlus[first]),
                             listNumItems, sizeof(JumpRefPlus),
                             (CmpFuncPtr)JumpRefSortAlphaFavTop, 0);
            break;

         case stAlphaFavBottom:
            SysInsertionSort(&(drawListData->jumpRefPlus[first]),
                             listNumItems, sizeof(JumpRefPlus),
                             (CmpFuncPtr)JumpRefSortAlphaFavBottom, 0);
            break;
      }

      // add padding to the width
      if(mpPrefs.bShowIcons)
         listWidth += (bVgaLargeFont ? 28 : 20); // padding
      else
         listWidth += (bVgaLargeFont ? 1 : 3);  // padding

      // setup data to be passed
      drawListData->indexStart = listNumItems - (first ? 0 : 1);

      // save current form and create new form for list
      pFormSave = FrmGetActiveForm();
      pFormList = FrmNewForm(FrmGetActiveFormID() + 1, "", 0, 0, MAX_WIDTH,
                             MAX_HEIGHT, true, 0, 0, 0); // was false before
                                                         // cmd bar

      if(bVgaExists)
      {
         if(rotateMode == rotateModeNone || rotateMode == rotateMode180)
         {
            VgaFormModify(pFormList, vgaFormModify160To240);
         }
      }

      // change size of form
      WinGetDisplayExtent(&screenX, &screenY);
      rect.extent.x  = listWidth;
      rect.extent.y  = listNumItems * FntCharHeight();

      switch(mpPrefs.appListPosition)
      {
         case alpTopLeft:
            rect.topLeft.x = 1;
            rect.topLeft.y = 1;
            break;

         case alpTopCenter:
            rect.topLeft.x = (screenX - rect.extent.x - 2) / 2;
            rect.topLeft.y = 1;
            break;

         case alpTopRight:
            rect.topLeft.x = screenX - rect.extent.x - 2;
            rect.topLeft.y = 1;
            break;

         case alpBottomCenter:
            rect.topLeft.x = (screenX - rect.extent.x - 2) / 2;
            rect.topLeft.y = screenY - rect.extent.y - 2;
            break;

         case alpBottomRight:
            rect.topLeft.x = screenX - rect.extent.x - 2;
            rect.topLeft.y = screenY - rect.extent.y - 2;
            break;

         case alpCenter:
            rect.topLeft.x = (screenX - rect.extent.x - 2) / 2;
            rect.topLeft.y = (screenY - rect.extent.y - 2) / 2;
            break;

         case alpBottomLeft:
         default:
            rect.topLeft.x = 1;
            rect.topLeft.y = screenY - rect.extent.y - 2;
            break;
      }

      WinSetBounds(FrmGetWindowHandle(pFormList), &rect);
      WinGlueSetFrameType(FrmGetWindowHandle(pFormList), menuFrame);

      FrmSetActiveForm(pFormList);

      // setup list
      err = LstNewList((void **)&pFormList, PopupAppList, 0, 0,
                       listWidth, listNumItems * FntCharHeight(), McStdFont,
                       listNumItems, 0);
      if(err)
         SysFatalAlert("ERROR creating list");

      pList = Id2Ptr(PopupAppList);
      LstSetListChoices(pList, nameList, listNumItems);
      LstSetDrawFunction(pList, (ListDrawDataFuncPtr)DrawList);

      // set list selection (can be removed some day)
      switch(alps)
      {
         case alpsNoSelect:
            LstSetSelection(pList, noListSelection);
            break;

         case alpsTopSelect:
            LstSetSelection(pList, 0);
            break;

         case alpsBottomSelect:
            LstSetSelection(pList, listNumItems - 1);
            break;

         case alpsConfigureSelect:
            if(mpPrefs.bSelectLast)
            {
               for(i = first; i < listNumItems; i++)
               {
                  if(drawListData->jumpRefPlus[i].jumpRef.creator ==
                     lastCreatorID)
                  {
                     LstSetSelection(pList, listNumItems - i - 1 + first);
                     break;
                  }
               }
            }
            else
            {
               LstSetSelection(pList, noListSelection);
            }
            break;
      }

      // draw form and fix colors if necessary
      if(bOS35OrGreater)
      {
#ifdef STANDALONE
         if(FtrGet(APP_ID, FTR_PSCOLLGRAFFITI, &temp) != ftrErrNoSuchFeature)
            CollapseSetUserState(pFormList, false);
#endif

         // make sure colors are correct to prevent blinking
         UIColorGetTableEntryRGB(UIDialogFill,  &rgbDialogFill);
         UIColorGetTableEntryRGB(UIObjectFill,  &rgbObjectFill);
         UIColorSetTableEntry(UIDialogFill,     &rgbObjectFill);

         FrmDrawForm(pFormList);

         // make sure colors are correct to prevent blinking
         UIColorSetTableEntry(UIDialogFill,  &rgbDialogFill);
      }
      else
      {
         RectangleType rect2;

         FrmDrawForm(pFormList);

         // ease window properly (only for OS <= 3.3)
         rect2 = rect;
         rect2.topLeft.x = 0;
         rect2.topLeft.y = 0;
         WinEraseRectangle(&rect2, 0);
      }

      // set text position
      drawListData->xOffsetText =
         (mpPrefs.bShowIcons ? (bVgaLargeFont ? 26 : 17) : 0);

      // Popup the list
      FtrSet(APP_ID, FTR_DRAWLISTDATA, (UInt32)drawListData);
      // save pointer to list
      FtrSet(APP_ID, FTR_DUPLICATE, (UInt32)pList); // just a warning...

#ifdef STANDALONE
      // FtrUnregister(APP_ID, FTR_SWIPEWAIT);
#endif

      sel = MyLstPopupList(pList, &rect);

      FrmEraseForm(pFormList);
      FrmDeleteForm(pFormList);
      FrmSetActiveForm(pFormSave);

      // make saved form active
      if(bVgaExists)
         VgaSetScreenMode(screenMode, rotateMode);
 
      // unregister any features
      FtrUnregister(APP_ID, FTR_DUPLICATE); // OK, safe now.
      FtrUnregister(APP_ID, FTR_DRAWLISTDATA);
      FntSetFont(oldFont);
      if(sel >= 0)
         jumpNum = listNumItems - sel - (first ? 0 : 1);
      else
         goto cleanup;

   }
   else
   {
      UInt32 currCreator;
      UInt16 prefsSize;
      Int16 prefResult;

      // assume jumpNum is 1
      jumpNum = 1;

      // Get the last launched application regardless of whether it was
      // ignored or not
      prefsSize = 0;
      prefResult = PrefGetAppPreferences(APP_ID, APPLAST, NULL, &prefsSize,
                                         true);

      if((prefResult != noPreferenceFound) &&
         (prefsSize == sizeof(currCreator)))
      {
         PrefGetAppPreferences(APP_ID, APPLAST, &currCreator, &prefsSize, true);
         if(drawListData->jumpRefPlus[0].jumpRef.creator != currCreator)
            jumpNum = 0;
      }
   }

   // Jump to the app
   if(!drawListData->jumpRefPlus[jumpNum].jumpRef.creator)
   {
      // Must not be a real app
      SndPlaySystemSound(sndWarning);
      goto cleanup;
   }

   // launch application
   if(drawListData->jumpRefPlus[jumpNum].bInVfs)
      VFSRunApp(&drawListData->vfsInfo,
                drawListData->jumpRefPlus[jumpNum].jumpRef.creator,
                drawListData->jumpRefPlus[jumpNum].vfsVolIndex,
                drawListData->jumpRefPlus[jumpNum].vfsPath,
                bVgaExists);
   else if(drawListData->jumpRefPlus[jumpNum].jumpRef.flags & IS_A_DA)
   { 
      RunDa(drawListData->jumpRefPlus[jumpNum].cardNo,
            drawListData->jumpRefPlus[jumpNum].dbID, bVgaExists);
   }
   else
   {
      RunApp(drawListData->jumpRefPlus[jumpNum].jumpRef.creator,
             drawListData->jumpRefPlus[jumpNum].cardNo, 
             drawListData->jumpRefPlus[jumpNum].dbID);
   }

cleanup:
   if(jumpRefs)
      MemPtrFree(jumpRefs);
   if(drawListData)
      MemPtrFree(drawListData);
   if(iconName)
      MemPtrFree(iconName);
}

Boolean GetLastAppCreatorID(JumpRefPlus jumpRefPlus[], UInt32 *pLastAppCreator,
                            UInt16 *pJumpNum)
{
   UInt16 prefsSize;
   Int16 prefResult;
   Boolean bGotApp = false;

   // assume jumpNum is 1
   *pJumpNum = 1;

   // Get the last launched application regardless of whether it was
   // ignored or not
   prefsSize = 0;
   prefResult = PrefGetAppPreferences(APP_ID, APPLAST, NULL, &prefsSize, true);

   if((prefResult != noPreferenceFound) &&
      (prefsSize == sizeof(*pLastAppCreator)))
   {
      bGotApp = true;

      PrefGetAppPreferences(APP_ID, APPLAST, pLastAppCreator, &prefsSize, true);
      if(jumpRefPlus[0].jumpRef.creator != *pLastAppCreator)
      {
         *pJumpNum = 0;
         *pLastAppCreator = jumpRefPlus[0].jumpRef.creator;
      }
      else
      {
         *pJumpNum = 1;
         *pLastAppCreator = jumpRefPlus[1].jumpRef.creator;
      }
   }

   return bGotApp;
}

static Int16 MyLstPopupList(ListPtr pList, RectangleType *pRectListAbs)
{
   EventType event;
   Int16 newSel;
   Boolean bLoop = true;
   Boolean bDelayedSysHandle = false;
   Int16 sel = noListSelection;
   RectangleType rectScr, rectList;
   PointType screen;
   Boolean bWaitForPenUpEvent = false;
   FormPtr pForm = FrmGetActiveForm();

   FrmGetObjectBounds(pForm, FrmGetObjectIndex(pForm, PopupAppList), &rectList);

   rectScr.topLeft.x = 0;
   rectScr.topLeft.y = 0;
   WinGetDisplayExtent(&rectScr.extent.x, &rectScr.extent.y);
   
   // draw the list
   LstDrawList(pList);

   // event loop
   do
   {
      Boolean bSkipSysHandle = false;
      Boolean bSkipMyHandle  = false;

      EvtGetEvent(&event, evtWaitForever);

      // pre-SysHandleEvent event checking
      switch(event.eType)
      {
         case penDownEvent:
            screen.x = event.screenX;
            screen.y = event.screenY;
            // might cause problem in future OS versions
            WinWindowToDisplayPt(&screen.x, &screen.y); // do version check

            if(RctPtInRectangle(screen.x, screen.y, pRectListAbs))
            {
               if(!RctPtInRectangle(event.screenX, event.screenY, &rectList))
               {
                  bLoop = false;
                  bSkipSysHandle = true;
               }
            }
            else
            {
               // prevent penDown event from doing anything else
               MemSet(&event, sizeof(EventType), 0);
               event.eType = nilEvent;

               // now out of the picture. beep and exit
               bWaitForPenUpEvent = true;
               bLoop = false;
               bSkipSysHandle = true;
            }
            break;

         case keyDownEvent:
            switch(event.data.keyDown.chr)
            {
               case vchrIrReceive:
               case lateWakeupChr:
               case vchrHardPower:
               case vchrPowerOff:
               case vchrAutoOff:
               case vchrSilkResize: // Sony graffiti min-max button
               case PALM_SLIDER_OPEN:
               case PALM_SLIDER_CLOSE:
               case vchrHardCradle:
                  bLoop = false;
                  bDelayedSysHandle = true;
                  break;

/* removed 3/12/2006, to prevent keyboard lock issues on the Treo
               case vchrLock:
               case hsChrKeyboardLock:
                  bLoop = false;
                  bDelayedSysHandle = true;
                  break;
*/

               case vchrJogUp:
               case vchrJogDown:
               case vchrJogPush:
               case vchrJogBack:
                  bSkipSysHandle = true;
                  break;

               default:
                  // prevent all repeats from going through
                  if(CheckSpecificKey(&(event.data.keyDown)))
                  {
                     if(event.data.keyDown.modifiers & autoRepeatKeyMask)
                     {
                        bSkipMyHandle = true;
                        bSkipSysHandle = true;
                     }
                  }

                  // if unknown key, skip system events
                  bSkipSysHandle = true;
                  break;
            }
            break;

         default:
            break;
      }

      // handle all system events
      if((bDelayedSysHandle == false) && (bSkipSysHandle == false))
         if(SysHandleEvent(&event))
            continue;

      if(bSkipMyHandle)
         continue;

      // post-SysHandleEvent event checking
      switch(event.eType)
      {
         case appStopEvent:
            bLoop = false;
            break;

         case lstSelectEvent:
            sel = event.data.lstSelect.selection;
            bLoop = false;
            break;

         case lstEnterEvent:
            LstHandleEvent(pList, &event);
            break;

         case lstExitEvent:
            bLoop = false;
            break;

         case penDownEvent:
            if(RctPtInRectangle(event.screenX, event.screenY, &rectList))
            {
               if(LstHandleEvent(pList, &event) == false)
               {
                  bLoop = false;
               }
            }
            else
            {
               bWaitForPenUpEvent = true;
               bLoop = false;
            }
            break;

         case keyDownEvent:
            newSel = LstGetSelection(pList);

            switch(event.data.keyDown.chr)
            {
               case vchrTrgJogUp:
               case kyoceraJogUp:
               case kyoceraVolumeUp:
               case chrUpArrow:
               case chrLeftArrow:
               case vchrPageUp:
               case vchrPrevField:
               case vchrJogUp:
               case FOSSIL_ROCKER_UP:
               case hsChrVolumeUp:
               case vchrRockerUp:
                  CLICK;
                  newSel--;
                  if(newSel < noListSelection) // no items selected
                     newSel = LstGetNumberOfItems(pList) - 1;
                  LstSetSelection(pList, newSel);
                  break;

               case vchrTrgJogDown:
               case kyoceraJogDown:
               case kyoceraVolumeDown:
               case chrDownArrow:
               case chrRightArrow:
               case vchrPageDown:
               case vchrNextField:
               case vchrJogDown:
               case FOSSIL_ROCKER_DOWN:
               case hsChrVolumeDown:
               case vchrRockerDown:
                  CLICK;
                  newSel++;
                  if(newSel >= LstGetNumberOfItems(pList))
                     newSel = noListSelection;  // no items selected
                  LstSetSelection(pList, newSel);
                  break;

               case vchrTrgJogPush:
               case kyoceraJogPush:
               case kyoceraCallButton:
               case chrLineFeed:
               case 0x1608: // hsChrJogScan
               case vchrRockerCenter:
               case vchrJogPush:
               case chrSpace:
               case chrCarriageReturn:
               case FOSSIL_ROCKER_PUSH:
               case hsChrSide:
                  CLICK;
                  sel = newSel;
                  bLoop = false;
                  break;

              case vchrNavChange:
                  if(event.data.keyDown.keyCode == navChangeSelect)
                  {
                     sel = newSel;
                     bLoop = false;
                  }
                  break;

               case vchrTrgAux:
               case chrEscape:
               case vchrJogBack:
                  bLoop = false;
                  break;

               default:
                  // "Sel.:" -- the user's configured hard key confirms
                  // whatever's currently highlighted, same as tapping it.
                  if(CheckSpecificKey(&(event.data.keyDown)) == KEY_APP_SEL)
                  {
                     CLICK;
                     sel = newSel;
                     bLoop = false;
                  }
                  break;
            }
            break;

         default:
            break;
      }
   }
   while(bLoop);

   if(bWaitForPenUpEvent)
   {
      bLoop = true;

      do
      {
         EvtGetEvent(&event, evtWaitForever);
         if(event.eType == penUpEvent)
            bLoop = false;
/*
         else
            EvtAddEventToQueue(&event);
*/
      } while(bLoop);
   }

   // handle any delayed SysHandleEvents
   if(bDelayedSysHandle)
      SysHandleEvent(&event);

   return sel;
}

static void DrawList(UInt16 itemnum, RectanglePtr bounds,
                     Char **unused __attribute__ ((unused)))
{
   UInt16 cardNo;
   LocalID dbID;
   DmOpenRef dbR = NULL;
   Char *name;
   MemHandle hRes = NULL;
   BitmapPtr pRes;
   Int16 xOffset;
   JumpRefPlus *pCurrentRefPlus;
   JumpRefPref *pCurrentRef;
   UInt16 index;
   Boolean bIsAFav;
   Coord iconWidth;
   Boolean bExpand = false;
   Boolean bInVfs;
   VFSInfo *pVfsInfo;
   UInt32 creator;
   Boolean bIsADa;
   DrawListData *drawListData;
 
   // get drawListData pointer
   if(FtrGet(APP_ID, FTR_DRAWLISTDATA, &creator) == ftrErrNoSuchFeature)
      return;
   drawListData = (DrawListData *)creator;

   index = drawListData->indexStart - itemnum;

   // get cardNo and dbID
   pCurrentRefPlus =  drawListData->jumpRefPlus + index;
   pCurrentRef     = &pCurrentRefPlus->jumpRef;
   cardNo          =  pCurrentRefPlus->cardNo;
   dbID            =  pCurrentRefPlus->dbID;
   name            =  pCurrentRefPlus->name;
   bInVfs          =  pCurrentRefPlus->bInVfs;
   bIsAFav         =  pCurrentRef->flags & IS_A_FAV;
   pVfsInfo        = &drawListData->vfsInfo;
   creator         =  pCurrentRef->creator;
   bIsADa          =  pCurrentRef->flags & IS_A_DA;

   // Open database and read the info
   dbR = DmOpenDatabaseByTypeCreator(DB_TYPE_CACHE, APP_ID, dmModeReadOnly);

   // set the font
   if(bIsAFav)
      FntSetFont(drawListData->McBoldFont);
   else
      FntSetFont(drawListData->McStdFont);

   if(drawListData->pMpPrefs->bShowIcons)
   {
      // get icon and draw
      if(bIsADa == false)
      {
         if(drawListData->bVgaLargeFont)
         {
            // get medium icon
            hRes = DmGet1Resource(creator, MEDIUM_ICON);

            // get small icon if medium does not exist
            if(hRes == NULL)
            {
               bExpand = true;
               hRes = DmGet1Resource(creator, SMALL_ICON);
            }
         }
         else
         {
            // get small icon
            hRes = DmGet1Resource(creator, SMALL_ICON);
         }
      }

      // get DA icon
      if((hRes == NULL) && bIsADa)
      {
         DmOpenRef dbRda = DmOpenDatabaseByTypeCreator(APP_TYPE, APP_ID, 
                                                       dmModeReadOnly);
         if(drawListData->bVgaLargeFont)
         {
            hRes = DmGet1Resource(bitmapRsc, DaIcon+0x1000);
         }
         else
         {
            hRes = DmGet1Resource(bitmapRsc, DaIcon);
         }

         if(dbRda != NULL)
          DmCloseDatabase(dbRda);
      }

      // get default icon if necessary
      if(hRes == NULL)
         hRes = DmGetResource(iconType, defaultAppSmallIconBitmap);

      if(hRes != NULL)
      {
         pRes = (BitmapPtr)MemHandleLock(hRes);
         BmpGlueGetDimensions(pRes, &iconWidth, NULL, NULL);
         if(bExpand)
         {
            xOffset = (15 - iconWidth) / 2;
            VgaWinDrawBitmapExpanded(pRes, bounds->topLeft.x + xOffset,
                                           bounds->topLeft.y + 1);
         }
         else
         {
            xOffset = ((drawListData->bVgaLargeFont ? 23 : 15) - iconWidth) / 2;
            WinDrawBitmap(pRes, bounds->topLeft.x + xOffset,
                                bounds->topLeft.y + 1);
         }
         MemHandleUnlock(hRes);
         DmReleaseResource(hRes);
      }
   }

   // close the database
   if(dbR != NULL)
      DmCloseDatabase(dbR);

   // draw the characters
   if(bInVfs)
   {
      WinSetUnderlineMode(grayUnderline);
      WinDrawChars(name, StrLen(name),
                   bounds->topLeft.x + drawListData->xOffsetText,
                   bounds->topLeft.y);
      WinSetUnderlineMode(noUnderline);
   }
   else
   {
      WinDrawChars(name, StrLen(name),
                   bounds->topLeft.x + drawListData->xOffsetText,
                   bounds->topLeft.y);
   }

   // reset the font
   if(bIsAFav)
      FntSetFont(drawListData->McStdFont);
}

static Int16 JumpRefSortMRUFavTop(JumpRefPlus *a1, JumpRefPlus *a2,
                                  Int32 unused __attribute__ ((unused)))
{
   Int16 result;
   Boolean bA1Fav = a1->jumpRef.flags & IS_A_FAV;
   Boolean bA2Fav = a2->jumpRef.flags & IS_A_FAV;

   if((bA1Fav && bA2Fav) || (!bA1Fav && !bA2Fav))
      result =  0;
   else if(bA1Fav && !bA2Fav)
      result =  1;
   else
      result = -1;
   
   return result;
}

static Int16 JumpRefSortMRUFavBottom(JumpRefPlus *a1, JumpRefPlus *a2,
                                     Int32 unused __attribute__ ((unused)))
{
   Int16 result;
   Boolean bA1Fav = a1->jumpRef.flags & IS_A_FAV;
   Boolean bA2Fav = a2->jumpRef.flags & IS_A_FAV;

   if((bA1Fav && bA2Fav) || (!bA1Fav && !bA2Fav))
      result =  0;
   else if(bA1Fav && !bA2Fav)
      result = -1;
   else
      result =  1;
   
   return result;
}

static Int16 JumpRefSortAlpha(JumpRefPlus *a1, JumpRefPlus *a2,
                              Int32 unused __attribute__ ((unused)))
{
   return StrCaselessCompare(a2->name, a1->name);
}

static Int16 JumpRefSortAlphaFavTop(JumpRefPlus *a1, JumpRefPlus *a2,
                                    Int32 unused __attribute__ ((unused)))
{
   Int16 result;
   Boolean bA1Fav = a1->jumpRef.flags & IS_A_FAV;
   Boolean bA2Fav = a2->jumpRef.flags & IS_A_FAV;

   if((bA1Fav && bA2Fav) || (!bA1Fav && !bA2Fav))
      result = StrCaselessCompare(a2->name, a1->name);
   else if(bA1Fav && !bA2Fav)
      result =  1;
   else
      result = -1;
   
   return result;
}

static Int16 JumpRefSortAlphaFavBottom(JumpRefPlus *a1, JumpRefPlus *a2,
                                       Int32 unused __attribute__ ((unused)))
{
   Int16 result;
   Boolean bA1Fav = a1->jumpRef.flags & IS_A_FAV;
   Boolean bA2Fav = a2->jumpRef.flags & IS_A_FAV;

   if((bA1Fav && bA2Fav) || (!bA1Fav && !bA2Fav))
      result = StrCaselessCompare(a2->name, a1->name);
   else if(bA1Fav && !bA2Fav)
      result = -1;
   else
      result =  1;
   
   return result;
}

static Boolean GetIconNameFromCache(UInt16 cardNo, LocalID dbID, UInt32 creator,
                                    Char *pIconChar)
{
   DmOpenRef dbR;
   MemHandle hRes;
   Boolean bGotName = false;

   // Open database and read the info
   dbR = DmOpenDatabaseByTypeCreator(DB_TYPE_CACHE, APP_ID, dmModeReadOnly);
   if(dbR == NULL)
   {
      bGotName = GetIconName(cardNo, dbID, pIconChar);
      return bGotName;
   }

   hRes = DmGet1Resource(creator, ICON_NAME);
   if(hRes)
   {
      Char *pName = MemHandleLock(hRes);

      StrNCopy(pIconChar, pName, dmDBNameLength);
      MemHandleUnlock(hRes);
      bGotName = true;
   }
   else
   {
      bGotName = GetIconName(cardNo, dbID, pIconChar);
   }

   // close the database
   if(dbR)
      DmCloseDatabase(dbR);

   return bGotName;
}

static void myWipe(void)
{
   EvtFlushPenQueue();
   EvtFlushNextPenStroke();
}

static void RunApp(UInt32 creator, UInt16 cardNo, LocalID dbID)
{
   UInt16 size;
   Int16 prefResult;
   UInt32 currCreator;
   Boolean bRunApp = true;

   size = 0;
   prefResult = PrefGetAppPreferences(APP_ID, APPLAST, NULL, &size, true);

   if((prefResult != noPreferenceFound) && (size == sizeof(currCreator)))
   {
      PrefGetAppPreferences(APP_ID, APPLAST, &currCreator, &size, true);
      if(currCreator == creator)
      {
         bRunApp = false;
      }
   }

   if(bRunApp)
      SysUIAppSwitch(cardNo, dbID, sysAppLaunchCmdNormalLaunch, NULL);
}

/*********************/
/* VFS Specific Code */
/*********************/

static void VFSFindApps(DrawListData *drawListData)
{
   Int16 i, j;
   Err error;
   UInt32 version;
   UInt32 creator;
   DmOpenRef dbR;
   MemHandle hRes;
   FileRef fileRef;

   // Check if VFS features are available
   error = FtrGet(sysFileCVFSMgr, vfsFtrIDVersion, &version);
   if(error)
      return;

   // Open database and read the info
   dbR = DmOpenDatabaseByTypeCreator(DB_TYPE_CACHE, APP_ID, dmModeReadOnly);
   if(dbR == NULL)
      return;

   for(i = 0; i < drawListData->vfsInfo.numVFSVolumes; i++)
   {
      VFSVolInfo *pVfsVol = drawListData->vfsInfo.vfsVol + i;

      for(j = 0; j <= drawListData->pMpPrefs->numItems; j++)
      {
         Err err;
         DmSearchStateType searchState;
         UInt16 cardNo;
         LocalID dbID;
         JumpRefPlus *pJumpRefPlus = drawListData->jumpRefPlus + j;

         creator = pJumpRefPlus->jumpRef.creator;

         if(creator == 0)
            break;

         // do no mark as VFS if application is in RAM
         err = DmGetNextDatabaseByTypeCreator(true, &searchState,
                                              sysFileTApplication, creator,
                                              true, &cardNo, &dbID);
         if(err == errNone)
            continue;

         // find application on VFS device
         hRes = DmGet1Resource(creator, VFS_PATH);
         if(hRes)
         {
            Char *vfsPath = MemHandleLock(hRes);

            error = VFSFileOpen(pVfsVol->volRefNum,
                                vfsPath, vfsModeRead, &fileRef);
            if(error == errNone)
            {
               pJumpRefPlus->bInVfs      = true;
               pJumpRefPlus->vfsVolIndex = i;
               StrNCopy(pJumpRefPlus->vfsPath, vfsPath, VFS_MAX_PATH);
               VFSFileClose(fileRef);
            }

            MemHandleUnlock(hRes);
            DmReleaseResource(hRes);
         }
      }
   }

   // close the database
   if(dbR)
      DmCloseDatabase(dbR);
}

static void VFSRunApp(VFSInfo *vfsInfo, UInt32 creator, Int16 vfsVolIndex,
                      Char *vfsPath, Boolean bVgaExists)
{
   Err err = 0;
   UInt16 cardNo;
   LocalID dbID;
   DmSearchStateType searchState;
   VFSRunningApp *pVra;
   DmOpenRef dbMcPhling;
   FormPtr pNewForm = NULL;
   VgaScreenModeType screenMode;
   VgaRotateModeType rotateMode;
   FormPtr pForm = FrmGetActiveForm();
   MemHandle hRes;

   pVra = MemPtrNew(sizeof(VFSRunningApp));
   if(pVra == NULL)
      return;

   // open up "McPhling" to ensure that wait form resources are available
   dbMcPhling = DmOpenDatabaseByTypeCreator(APP_TYPE, APP_ID, dmModeReadOnly);
   DisplayWaitForm(bVgaExists, &screenMode, &rotateMode, &pNewForm);

   // load "McPhlingVFS" prc from resource to memory
   hRes = DmGet1Resource('iPRC', MCPHLING_VFS_ID);
   DmCreateDatabaseFromImage(MemHandleLock(hRes));
   MemHandleUnlock(hRes);

   // close "McPhling" database
   DmCloseDatabase(dbMcPhling);

   // setup vra structure
   pVra->vfsState  = vsDeleteMe; // assume that VFSImportDatabaseFromFile fails
   pVra->creator   = creator;
   pVra->volRefNum = vfsInfo->vfsVol[vfsVolIndex].volRefNum;
   StrCopy(pVra->path, vfsPath);

   // copy program from VFS to memory
   err = VFSImportDatabaseFromFile(pVra->volRefNum, pVra->path,
                                   &pVra->cardNo, &pVra->dbID);
   if(err == errNone)
   {
      err = DmGetNextDatabaseByTypeCreator(true, &searchState,
                                           sysFileTApplication, APP_ID_VFS,
                                           true, &cardNo, &dbID);
      if(err == errNone)
      {
         pVra->vfsState = vsWaiting;
         SysUIAppSwitch(cardNo, dbID, sysAppLaunchCmdNormalLaunch, NULL);
      }
   }

   // save preference so that "McPhlingVFS" knows what to run
   PrefSetAppPreferences(APP_ID, VFSRUNNINGAPP, 0, pVra, sizeof(*pVra), true);

   RemoveWaitForm(bVgaExists, screenMode, rotateMode, pNewForm, pForm);

   if(pVra)
      MemPtrFree(pVra);
}

#ifdef STANDALONE
static void VFSUpdateCache(VFSInfo *vfsInfo)
{
   Int16 i;
   Err error;
   FileInfoType fileInfo;
   Char *fileName = NULL;
   UInt32 version;
   UInt32 dirIterator;
   FileRef dirRef;
   FileRef fileRef;
   UInt32 type;
   Char vfsPath[VFS_MAX_PATH + 1];
   UInt32 localCreator;
   DmOpenRef dbCache = NULL;

   // Check if VFS features are available
   error = FtrGet(sysFileCVFSMgr, vfsFtrIDVersion, &version);
   if(error)
      return;

   // setup file name memory
   fileName = MemPtrNew(sizeof(Char) * (VFS_MAX_PATH + 1));
   if(fileName == NULL)
      return;

   // open cache for writing
   dbCache = DmOpenDatabaseByTypeCreator(DB_TYPE_CACHE, APP_ID,
                                         dmModeReadWrite);

   if(dbCache == NULL)
      return;

   for(i = 0; i < vfsInfo->numVFSVolumes; i++)
   {
      VFSFileOpen(vfsInfo->vfsVol[i].volRefNum, vfsInfo->vfsVol[i].path,
                  vfsModeRead, &dirRef);
      fileInfo.nameP = fileName; // point to local buffer
      fileInfo.nameBufLen = VFS_MAX_PATH;
      dirIterator = vfsIteratorStart;
      while(dirIterator != vfsIteratorStop)
      {
         // Get the next file
         error = VFSDirEntryEnumerate(dirRef, &dirIterator, &fileInfo);
         if(error == errNone)
         {
            // open the file
            StrCopy(vfsPath, vfsInfo->vfsVol[i].path);
            StrCat(vfsPath, fileInfo.nameP);
            error = VFSFileOpen(vfsInfo->vfsVol[i].volRefNum, vfsPath,
                                vfsModeRead, &fileRef);
            if(error == errNone)
            {
               // get type and creator
               error = VFSFileDBInfo(fileRef, NULL, NULL, NULL, NULL, NULL,
                                     NULL, NULL, NULL, NULL, &type,
                                     &localCreator, NULL);
               if(error == errNone && type == sysFileTApplication)
               {
                  MemHandle hRes = DmGet1Resource(localCreator, VFS_PATH);
                  if(hRes == NULL)
                  { // prevent duplicate resource
                     MemHandle hVFSPath = DmNewResource(dbCache, localCreator,
                                                        VFS_PATH,
                                                        VFS_MAX_PATH + 1);
                     Char *pVFSPath = MemHandleLock(hVFSPath);

                     DmWrite(pVFSPath, 0, vfsPath, VFS_MAX_PATH + 1);

                     // clean up
                     MemHandleUnlock(hVFSPath);
                     DmReleaseResource(hVFSPath);
                  }
                  else
                  {
                     DmReleaseResource(hRes);
                  }
               }
            }

            // close the file
            VFSFileClose(fileRef);
         }
      }
      VFSFileClose(dirRef);
   }

   // clean up
   MemPtrFree(fileName);

   // close cache database
   DmCloseDatabase(dbCache);
}
#endif

/********************/
/* DA Specific Code */
/********************/

static void RunDa(UInt16 cardNo, LocalID dbID, Boolean bVgaExists)
{
   MemHandle h;
   MemPtr p;
   DmOpenRef db;
   FormPtr oldform;
   UInt16 w;
   WinHandle dwin, awin;
   VgaScreenModeType screenMode;
   VgaRotateModeType rotateMode;

   if(dbID == 0) 
      return;

   if(bVgaExists)
   {
      // adjust screen for HandEra 330 if necessary
      VgaGetScreenMode(&screenMode, &rotateMode);

      if(screenMode == screenMode1To1)
         VgaSetScreenMode(screenModeScaleToFit, rotateMode);
   }
      
   // do DA stuff
   db = DmOpenDatabase(cardNo, dbID, dmModeReadOnly);

   h = DmGet1Resource(sysResTAppCode, ainID);
   p = MemHandleLock(h);
   DmReleaseResource(h);

   oldform = FrmGetActiveForm();
   w       = FrmGetActiveFormID();
   awin    = WinGetActiveWindow();
   dwin    = WinGetDrawWindow();

   ((void (*)(void))p)();  /* Call DA code with type = code, id = ainID */

   if(w != 0)
   {
      FrmSetActiveForm(oldform);
   }
   WinSetActiveWindow(awin);
   WinSetDrawWindow(dwin);
   
   MemHandleUnlock(h);
   DmCloseDatabase(db);

   if(bVgaExists)
   {
      // restore screen mode for the HandEra 330 (if necessary)
      if(screenMode == screenMode1To1)
         VgaSetScreenMode(screenMode, rotateMode);
   }
}

/////////////////////////////////////
// Key Related Function Prototypes //
/////////////////////////////////////

Boolean HandleKeys(struct _KeyDownEventType *keyDown)
{
   ListPtr pList;
   Boolean handled = false;
#ifdef STANDALONE
   UInt32 version;

   // check of Treo keyboard is locked
   FtrGet(hsFtrCreator, hsFtrIDVersion, &version);
   if(version >= 0x02000000)
   {
      UInt32 locked;

      HsAttrGet(hsAttrKeyboardLocked, 0, &locked);
      if(locked)
         return false;
   }
#endif

   // kludge for Handspring Treo (prevents it from locking up when autodialing)
   if(keyDown->chr == vchrAutoOff)
      return false;

   // don't try if in activation form
   if(FtrGet(APP_ID, FTR_ACTTEMP, (UInt32 *)&pList) != ftrErrNoSuchFeature)
      return false;

   if(FtrGet(APP_ID, FTR_DUPLICATE, (UInt32 *)&pList) == ftrErrNoSuchFeature)
   {
      PointType start;

      start.x = CheckSpecificKey(keyDown);
      switch(start.x)
      {
         case KEY_APP_LIST:
            handled = true;

            start.y = alpsNoSelect;
            HandleSwipe(&start, &start);
            break;

         case KEY_APP_LAST:
            handled = true;

            start.y = 0;
            HandleSwipe(&start, &start);
            break;

         case KEY_FILTER:
            handled = true;
            break;
      }
   }

   return handled;
}

static Coord CheckSpecificKey(struct _KeyDownEventType *keyDown)
{
   McActivation *pMpActivation = NULL;
   Coord retVal = 0;
   Boolean bWaitForKeyRelease = false;

   pMpActivation = MemPtrNew(sizeof(McActivation));
   if(pMpActivation == NULL)
   {
      BEEP;
      return false;
   }
   GetActivation(pMpActivation);

   if(pMpActivation->bKeyboard)
   {
      WChar   chr       = keyDown->chr;
      UInt16  keyCode   = keyDown->keyCode;
      UInt16  mod       = keyDown->modifiers & ~(poweredOnKeyMask); // filtered
      WChar   aChr      = pMpActivation->keyInfoApps.chr;
      UInt16  aKeyCode  = pMpActivation->keyInfoApps.keyCode;
      UInt16  aMod      = pMpActivation->keyInfoApps.modifiers;
      WChar   bChr      = pMpActivation->keyInfoBack.chr;
      UInt16  bKeyCode  = pMpActivation->keyInfoBack.keyCode;
      UInt16  bMod      = pMpActivation->keyInfoBack.modifiers;
      WChar   selChr     = pMpActivation->keyInfoSel.chr;
      UInt16  selKeyCode = pMpActivation->keyInfoSel.keyCode;
      UInt16  selMod     = pMpActivation->keyInfoSel.modifiers;
      UInt16  sMod      = STATIC_MODIFIERS;

      if(pMpActivation->bKeyboardHoldApps || pMpActivation->bKeyboardHoldBack)
      {
         UInt16  initDelay;
         UInt16  period;
         UInt16  doubleTapDelay;
         Boolean bQueueAhead;
         UInt32  startTicks = TimGetTicks();
         UInt32  nextHoldTime;

         // get key rates
         KeyRates(false, &initDelay, &period, &doubleTapDelay, &bQueueAhead);
         initDelay *= 2; // make two times longer

         // wait for a bit before allowing this to happen again
         if(FtrGet(APP_ID, FTR_NEXTHOLDTIME, &nextHoldTime) == 0)
         {
            if((mod & autoRepeatKeyMask) && (startTicks < nextHoldTime))
            {
               MemPtrFree(pMpActivation);
               return KEY_FILTER;
            }
         }

         if(pMpActivation->bKeyboardHoldApps &&
            (aChr != chrNull) && (chr == aChr) && (keyCode == aKeyCode) &&
            ((mod & sMod) == aMod))
         {
            bWaitForKeyRelease = true;
         }
         if(pMpActivation->bKeyboardHoldBack &&
            (bChr != chrNull) && (chr == bChr) && (keyCode == bKeyCode) &&
                 ((mod & sMod) == bMod))
         { 
            bWaitForKeyRelease = true;
         }

         // wait for key to be released
         if(bWaitForKeyRelease)
         {
            Boolean bKeepGoing = true;
            UInt32  repeatTime = startTicks + initDelay;

            // HsKeyCurrentStateExt() for Handspring
            while(KeyCurrentState() && bKeepGoing)
            {
               if(TimGetTicks() >= repeatTime)
               {
                  bKeepGoing = false;
                  mod |= autoRepeatKeyMask;
                  keyDown->chr = NULL; // clear out and wait for next
                  retVal = KEY_FILTER;
               }
            }

            if(pMpActivation->bKeyboardHoldApps)
               aMod |= autoRepeatKeyMask;
            if(pMpActivation->bKeyboardHoldBack)
               bMod |= autoRepeatKeyMask;
         }

         // set static modifiers
         sMod = STATIC_MODIFIERS | DYNAMIC_MODIFIERS;
      }
      else
      {
         mod &= ~autoRepeatKeyMask;
      }

      if((aChr != chrNull) && (chr == aChr) && (keyCode == aKeyCode) &&
         ((mod & sMod) == aMod))
      {
         retVal = KEY_APP_LIST;
      }
      else if((bChr != chrNull) && (chr == bChr) && (keyCode == bKeyCode) &&
              ((mod & sMod) == bMod))
      {
         retVal = KEY_APP_LAST;

         // prevent this from happening again in the near future
         if(bWaitForKeyRelease)
         {
            FtrSet(APP_ID, FTR_NEXTHOLDTIME,
                   TimGetTicks() + (5 * SysTicksPerSecond()));
         }
      }
      // "Sel.:" -- no Hold support, so compare against STATIC_MODIFIERS
      // directly (not the hold-adjusted sMod mask above) -- but still the
      // poweredOnKeyMask-filtered `mod`, same as the aMod/bMod checks,
      // since a hard app-launch key commonly also wakes/powers on the
      // device (this bit must not be allowed to break the match).
      else if((selChr != chrNull) && (chr == selChr) &&
              (keyCode == selKeyCode) &&
              ((mod & STATIC_MODIFIERS) == selMod))
      {
         retVal = KEY_APP_SEL;
      }
   }

   // clean up
   MemPtrFree(pMpActivation);

   return retVal;
}
