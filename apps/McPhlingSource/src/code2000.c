///////////////////////////////////////////////////////////////////////////////
//                                                                           //
//        Program Name:  code2000.c                                          //
//              Author:  Mike McCollister (� 2000-2009)                      //
//     Original Author:  Mitch Blevins <mblevin@debian.org> (� 1997-2000)    //
//            Language:  PRC-Tools 2.3                                       //
//                       Palm OS 5 SDK R3                                    //
//                       PilRC 3.2                                           //
//                       HandEra Header Files from SDK 1.04                  //
//                       palmOne Header Files from SDK version 5.1           //
//                       Sony Header Files from SDK 5.0 version 1.2          //
//               Notes:  Distributed under the GNU GPL.                      //
//                       Based on LinkHistoryHack by Ben Darnell             //
//                       <bgdarnel@unity.ncsu.edu> which was based on        //
//                       SampleHack by Roger Chaplin                         //
//                       <foursquaredev@att.net>.                            //
//         Description:  McPhling Preferences Panel                          //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

///////////////////
// Include Files //
///////////////////

#include <PalmOS.h>
#include <PalmOSGlue.h>
#include <Extensions/ExpansionMgr/VFSMgr.h>

#include <Standalone.h>

#include "common.h"
#include "McPhling.h"
#include "McPhling.rch"

/////////////
// Defines //
/////////////

#define CUSTOM_POSITION          1

////////////////
// Structures //
////////////////

typedef struct
{
   Char name[dmDBNameLength + 1];
   UInt32 creator;
   UInt16 flags;
   UInt16 vfsVolRefNum;
   Char *vfsPath;
} McPhlingAppList;

typedef struct
{
   KeyInfoType kit;
   Boolean     bCustomDetails;
} CustomData;

/////////////////////////
// Function Prototypes //
/////////////////////////

// Preference Form Functions
static void PrefsInit(FormPtr pForm);
static void PrefsOK(FormPtr pForm);
static void PrefsListDrawFunc(UInt16 index, RectanglePtr bounds, Char **data);
static void DoListSelect(Int16 selection);
static void ScrollList(FormPtr pForm, WinDirectionType direction);
static void AddFavorite(McPhlingAppList *pAppList, JumpRefPref *favApps,
                        McPhlingPrefs *pMpPrefs);
static void ChangeNumItems(FormPtr pForm, UInt16 keyDown);
static void PrefsUpdateForm(FormPtr pForm);
static void TruncateString(Char *pStr, Int16 width);
static void SizeForm(FormPtr pForm);
static void ResizeObject(FormPtr pForm, UInt16 objectID,
                         Coord xMove, Coord yMove,
                         Coord xExp, Coord yExp, Boolean bDraw);
#ifdef STANDALONE
static void SetObjectHeight(FormPtr pForm, UInt16 objectID,
                            Coord yExp, Boolean bDraw);
#endif
static void DisplayString(Char *str, MemHandle hField, UInt16 fieldID,
                          Boolean bRefresh);
static void CreateAppCache(Boolean bShowIcons, Boolean bVFSSupport,
                           JumpRefPref *pAppList, UInt8 numItems,
                           McPhlingAppList *pMcAppList, UInt16 numApps,
                           UInt16 vfsAppCount);
static Boolean CreateDataBaseList(Boolean bVFSSupport, UInt16 *pAppCount,
                                  UInt16 *pVfsAppCount,
                                  McPhlingAppList **pMcAppList,
                                  Char **pVFSPathList);
static void UpdateDatabaseList(FormPtr pForm);
static Boolean IsAppInRam(UInt32 creator);
static Int16 DatabaseSortAlpha(McPhlingAppList *a1, McPhlingAppList *a2,
                               Int32 unused);
#ifdef STANDALONE
static Err MySysNotifyDisplayChangeEvent(SysNotifyParamType *np);
#endif

// Generic Functions
static void DisplaySimpleForm(UInt16 rscID);
#ifdef STANDALONE
static Err SendMe(Char *prefix);
static Err SendDatabase(UInt16 cardNo, LocalID dbID, Char *nameP,
                        Char *descriptionP);
static Err WriteDBData(const void *dataP, UInt32 *sizeP, void *userDataP);
#endif

// Activation Form Functions
static UInt16 ActivationDoDialog(FormPtr pCurrentForm);
static Boolean ActivationEventHandler(EventPtr event);
static void ActivationInit(FormPtr pForm);
static void ActivationOK(void);
static void ActivationGetData(McActivation *pMpActivation,
                              Boolean bGetCustomApps, Boolean bGetCustomBack);
static void ActivationMakeDefault(FormPtr pForm);
static void ActivationUpdateForm(FormPtr pForm, McActivation *pMpActivation);
static UInt16 ConvertButtonListToKey(KeyInfoType *keyInfo, UInt16 sel,
                                     Boolean bReverseLookup);
static UInt16 ConvertSelListToKey(KeyInfoType *keyInfo, UInt16 sel,
                                  Boolean bReverseLookup);

// Custom Button Form Functions
static void CustomButtonDoDialog(FormPtr pCurrentForm, CustomData *pCD);
static Boolean CustomButtonEventHandler(EventPtr event);
static void CustomButtonInit(FormPtr pForm, CustomData *pCD);
static void CustomButtonDetails(KeyInfoType *pKit);
static void CustomButtonChange(KeyInfoType *pKit);
static Boolean CustomButtonFiltered(KeyInfoType *pKit);

// VFS Specific Code
static UInt16 VFSCountApps(VFSInfo *vfsInfo);
static void VFSGetDatabaseList(McPhlingAppList **pMcAppList,
                               Char *pVFSPathList, VFSInfo *vfsInfo,
                               UInt16 first);

///////////////////////////////
// Preference Form Functions //
///////////////////////////////

#ifndef STANDALONE
#define RESOURCE_ID 2000
STANDALONE_CODE_RESOURCE_ID(RESOURCE_ID);
#endif

Boolean PrefsEventHandler(EventPtr event)
{
   // This is the code that will be called whenever the user taps this 
   // hack's "+" button in the HackMaster list. HackMaster is not smart about
   // the content of this file; it will simply jump to the first executable
   // statement in this file. If you need helper functions, prototype them
   // above, and define them AFTER the trap patch.

   FormPtr pForm = FrmGetActiveForm();
   Boolean handled = true;
   UInt32 emptyCreator;
   McPhlingAppList *pAppList;
   Char *pVFSPathList;
   MemHandle hNumItems;
   RectangleType *pOldWinRect = NULL;
   UInt32 version;
   McActivation *pMpActivation;

   switch(event->eType)
   {
      case frmOpenEvent:
         hNumItems = MemHandleNew(NUM_CHARS_NUM_ITEMS);
         FtrSet(APP_ID, FTR_FIELDNUMITEMS, (UInt32)hNumItems);
         PrefsInit(pForm);
         FrmDrawForm(pForm);
         PrefsUpdateForm(pForm);
         break;
      
      case displayExtentChangedEvent:
      case winDisplayChangedEvent:
         SizeForm(pForm);
         break;

      case frmUpdateEvent:
         FrmDrawForm(pForm);
         PrefsUpdateForm(pForm);
         break;

      case ctlSelectEvent:
         switch(event->data.ctlEnter.controlID)
         {
#ifdef STANDALONE
            case PrefsEnabled:
               EnableOS5(CtlGetValue(Id2Ptr(PrefsEnabled)));
               break;

            case PrefsShowIcons:
            case PrefsClick:
            case PrefsShowCurrentApp:
               PrefsOK(pForm);
               break;
#endif

            case PrefsVFSSupport:
#ifndef STANDALONE
               if(CtlGetValue(Id2Ptr(PrefsVFSSupport)))
                  FrmAlert(VFSAlert);
#endif
               UpdateDatabaseList(pForm);
#ifdef STANDALONE
               PrefsOK(pForm);
#endif
               break;

            case PrefsCacheButton:
               if(!FrmAlert(CacheAlert))
                  PrefSetAppPreferences(APP_ID, REFPREFS, 0, &emptyCreator,
                                        0, true);
               break;

#ifndef STANDALONE
            case OKButton:
               PrefsOK(pForm);
               FrmGotoForm(HackMasterForm);  // return to HackMaster control
               break;

            case CancelButton:
               FrmGotoForm(HackMasterForm);  // return to HackMaster control
               break;
#endif

            case PrefsActivationButton:
               // bring up activation form
               if(ActivationDoDialog(pForm) == OKButton)
               {
#ifdef STANDALONE
                  PrefsOK(pForm);
#endif
               }
               break;

            default:
               handled = false;
               break;
         }
         break;

      case ctlRepeatEvent:
         switch(event->data.ctlEnter.controlID)
         {
            case PrefsNumItemsUp:
               ChangeNumItems(pForm, pageUpChr);
#ifdef STANDALONE
               PrefsOK(pForm);
#endif
               break;

            case PrefsNumItemsDown:
               ChangeNumItems(pForm, pageDownChr);
#ifdef STANDALONE
               PrefsOK(pForm);
#endif
               break;

            default:
               handled = false;
               break;
         }
         handled = false;
         break;

#ifdef STANDALONE
      case menuEvent:
         switch(event->data.menu.itemID)
         {
            case menuitemID_beam:
               SendMe(exgBeamPrefix);
               break;

            case menuitemID_send:
               SendMe(exgSendBeamPrefix);
               break;

            case menuitemID_info:
               FrmHelp(Help);
               break;

            case menuitemID_about:
               DisplaySimpleForm(AboutForm);
               break;

            default:
               handled = false;
               break;
         }
         break;

      case popSelectEvent:
         PrefsOK(pForm);
         handled = false;
         break;
#endif

      case lstSelectEvent:
         DoListSelect(event->data.lstSelect.selection);
#ifdef STANDALONE
         PrefsOK(pForm);
#endif
         break;

      case keyDownEvent:
         switch(event->data.keyDown.chr)
         {
            case pageUpChr:
               ScrollList(pForm, winUp);
               break;

            case pageDownChr:
               ScrollList(pForm, winDown);
               break;

#ifndef STANDALONE
            case chrCarriageReturn:
               PrefsOK(pForm);
               FrmGotoForm(HackMasterForm);  // return to HackMaster control
               break;
#endif

#ifdef STANDALONE
            case vchrSendData:
               SendMe(exgBeamPrefix);
               break;
#endif

            default:
               handled = false;
               break;
         }
         break;
      
#ifdef STANDALONE
      case menuCmdBarOpenEvent:
         MenuCmdBarAddButton(menuCmdBarOnRight, BarBeamBitmap,
                             menuCmdBarResultMenuItem, menuitemID_beam, 0);
         MenuCmdBarAddButton(menuCmdBarOnRight, BarInfoBitmap,
                             menuCmdBarResultMenuItem, menuitemID_info, 0);

         handled = false; // don't set handled to true; this event must fall
                          //   through to the system.
         break;
#endif

      case fldEnterEvent:
         // prevent fields from being entered
         break;

      case frmCloseEvent:
#ifdef STANDALONE
         PrefsOK(pForm);
#endif

         FrmEraseForm(pForm);
         FrmDeleteForm(pForm);

         if(FtrGet(APP_ID, FTR_PAPPLIST, (UInt32 *)&pAppList) == 0)
            if(pAppList)
               MemPtrFree(pAppList);
         FtrUnregister(APP_ID, FTR_PAPPLIST);
         if(FtrGet(APP_ID, FTR_PVFSPATHLIST, (UInt32 *)&pVFSPathList) == 0)
            if(pVFSPathList)
               MemPtrFree(pVFSPathList);
         FtrUnregister(APP_ID, FTR_PVFSPATHLIST);
         FtrUnregister(APP_ID, FTR_APPCOUNT);
         FtrUnregister(APP_ID, FTR_VFSAPPCOUNT);
         FtrUnregister(APP_ID, FTR_NEWNUMITEMS);
         if(FtrGet(APP_ID, FTR_OLDWINRECT, (UInt32 *)&pOldWinRect) == 0)
            if(pOldWinRect)
               MemPtrFree(pOldWinRect);
         FtrUnregister(APP_ID, FTR_OLDWINRECT);
         if(_TRGVGAFeaturePresent(&version))
         {
            UInt32 temp;
            VgaScreenModeType screenMode;
            VgaRotateModeType rotateMode;

            VgaGetScreenMode(&screenMode, &rotateMode);

            if(FtrGet(APP_ID, FTR_VGASCREENMODE, &temp) == 0)
               screenMode = (VgaScreenModeType)temp;
            FtrUnregister(APP_ID, FTR_VGASCREENMODE);

            VgaSetScreenMode(screenMode, rotateMode);
         }
         if(FtrGet(APP_ID, FTR_ACTIVATION, (UInt32 *)&pMpActivation) == 0)
            if(pMpActivation)
               MemPtrFree(pMpActivation);
         FtrUnregister(APP_ID, FTR_ACTIVATION);

         /* not needed since hNumItems is automatically freed
         if(FtrGet(APP_ID, FTR_FIELDNUMITEMS, (UInt32 *)&hNumItems) == 0)
            MemHandleFree(hNumItems);
         */
         FtrUnregister(APP_ID, FTR_FIELDNUMITEMS);
         break;

      default:
         handled = false;
         break;
   }
   
   return handled;
}

static void PrefsInit(FormPtr pForm)
{
   McPhlingAppList *pAppList = NULL;
   Char *pVFSPathList = NULL;
   UInt16 appCount = 0;
   UInt16 vfsAppCount = 0;
   ListPtr pList;
   UInt16 i, j;
   UInt16 ignPrefsSize, favPrefsSize, ignNumApps;
   UInt32 *ignoredApps;
   Int16 prefResult;
   McPhlingPrefs mpPrefs;
   JumpRefPref favApps[MAX_NUM_ITEMS + 1];
   UInt32 version;
   Err error;
   VgaScreenModeType screenMode;
   VgaRotateModeType rotateMode;
   Boolean bGotDatabaseList = false;
   McActivation *pMpActivation = NULL;
   FormPtr pNewForm = NULL;
   Boolean bVgaExists = _TRGVGAFeaturePresent(&version);
   Boolean bCollapsibleGraffiti = false;
#ifdef STANDALONE
   Boolean bPS, bSony;
#endif

#ifdef STANDALONE
   bCollapsibleGraffiti = GetCollapseGraffiti(&bPS, &bSony);

   if(bCollapsibleGraffiti)
   {
      UInt16 cardNo;
      LocalID dbID;

      SysCurAppDatabase(&cardNo, &dbID);
      if(bPS)
      {
         SysNotifyRegister(cardNo, dbID, sysNotifyDisplayResizedEvent,
                           &MySysNotifyDisplayChangeEvent,
                           sysNotifyNormalPriority, NULL);
      }
      else if(bSony)
      {
         SysNotifyRegister(cardNo, dbID, sysNotifyDisplayChangeEvent,
                           &MySysNotifyDisplayChangeEvent,
                           sysNotifyNormalPriority, NULL);
      }
   }
   CollapseSetUserState(pForm, true);
#endif

   if(bVgaExists)
   {
      VgaGetScreenMode(&screenMode, &rotateMode);
      FtrSet(APP_ID, FTR_VGASCREENMODE, (UInt32)screenMode);

      VgaSetScreenMode(screenMode1To1, rotateMode);
      VgaFormModify(pForm, vgaFormModify160To240);
   } 

   if(bVgaExists || bCollapsibleGraffiti)
   {
      RectangleType *pOldWinRect = NULL;

      pOldWinRect = MemPtrNew(sizeof(RectangleType));
      if(pOldWinRect)
      {
#ifndef STANDALONE
         pOldWinRect->topLeft.x = pOldWinRect->topLeft.y = 2;
         pOldWinRect->extent.x  = pOldWinRect->extent.y  = 236;
#else
         pOldWinRect->topLeft.x = pOldWinRect->topLeft.y = 0;
         pOldWinRect->extent.x  = pOldWinRect->extent.y  = 160; // was 240;
#endif
         FtrSet(APP_ID, FTR_OLDWINRECT, (UInt32)pOldWinRect);
      }

      // size the form for the HandEra 330 and PalmSource Collapsible Graffiti
      SizeForm(pForm);
   } 

   // get PERFSPREFS and set checkboxes
   GetPrefsAndNewIgnored(&mpPrefs);
#ifdef STANDALONE
   CtlSetValue(Id2Ptr(PrefsEnabled),        mpPrefs.bEnabled);
   EnableOS5(mpPrefs.bEnabled);
#endif
   CtlSetValue(Id2Ptr(PrefsShowIcons),      mpPrefs.bShowIcons);
   CtlSetValue(Id2Ptr(PrefsClick),          mpPrefs.bClick);
   CtlSetValue(Id2Ptr(PrefsShowCurrentApp), mpPrefs.bShowCurrentApp);
   CtlSetValue(Id2Ptr(PrefsSelectLast),     mpPrefs.bSelectLast);

   error = FtrGet(sysFileCVFSMgr, vfsFtrIDVersion, &version);
   if(error == errNone)
   {
      FrmShowObject(pForm,
                    FrmGetObjectIndex(pForm, PrefsVFSSupport));
      CtlSetValue(Id2Ptr(PrefsVFSSupport), mpPrefs.bVFSSupport);
   }

   // set sort type
   LstSetSelection(Id2Ptr(PrefsListSort), mpPrefs.sortType);
   CtlSetLabel(Id2Ptr(PrefsPopupTriggerSort),
               LstGetSelectionText(Id2Ptr(PrefsListSort), mpPrefs.sortType));
   
   // set list position
   LstSetSelection(Id2Ptr(PrefsListAppListPos), mpPrefs.appListPosition);
   CtlSetLabel(Id2Ptr(PrefsPopupTriggerAppListPos),
               LstGetSelectionText(Id2Ptr(PrefsListAppListPos),
                                   mpPrefs.appListPosition));
   
   // put up "Please wait" form
#ifdef STANDALONE
   if(mpPrefs.bVFSSupport)
      DisplayWaitForm(bVgaExists, &screenMode, &rotateMode, &pNewForm);
#else
   DisplayWaitForm(bVgaExists, &screenMode, &rotateMode, &pNewForm);
#endif

   // Get the favorites
   GetMRUApps(&mpPrefs, favApps);

   // adjust numItems due to show current app
   if(mpPrefs.bShowCurrentApp)
      mpPrefs.numItems++;

   // set number of items
   FtrSet(APP_ID, FTR_NEWNUMITEMS, (UInt32)mpPrefs.numItems);

   // Get the excluded app list
   ignoredApps  = 0;
   ignPrefsSize = 0;
   favPrefsSize = 0;
   prefResult = PrefGetAppPreferences(APP_ID, IGNOREPREFS, NULL, &ignPrefsSize,
                                      true);
   if((prefResult == noPreferenceFound) || (ignPrefsSize == 0))
   {
      ignNumApps = 0;
   }
   else
   {
      ignNumApps = ignPrefsSize / sizeof(UInt32);
      ignoredApps = MemPtrNew(ignPrefsSize);
      if(ignoredApps)
      {
         MemSet(ignoredApps, ignPrefsSize, 0);
         prefResult = PrefGetAppPreferences(APP_ID, IGNOREPREFS, ignoredApps,
                                            &ignPrefsSize, true);
         if((prefResult == noPreferenceFound) || (ignPrefsSize == 0))
         {
               ignNumApps = 0;
               MemPtrFree(ignoredApps);
               ignoredApps = 0;
         }
      }
      else
      {
         ignoredApps = 0;
         ignNumApps = 0;
      }
   }

   // Get every stinking little application
   bGotDatabaseList = CreateDataBaseList(mpPrefs.bVFSSupport, &appCount,
                                         &vfsAppCount, &pAppList,
                                         &pVFSPathList);

   // remove "Please wait" form
#ifdef STANDALONE
   if(mpPrefs.bVFSSupport)
      RemoveWaitForm(bVgaExists, screenMode, rotateMode, pNewForm, pForm);
#else
   RemoveWaitForm(bVgaExists, screenMode, rotateMode, pNewForm, pForm);
#endif

   // fill database list
   if(bGotDatabaseList)
   {
      // Publish the database list handle
      FtrSet(APP_ID, FTR_PAPPLIST, (UInt32)pAppList);
      FtrSet(APP_ID, FTR_PVFSPATHLIST, (UInt32)pVFSPathList);
      FtrSet(APP_ID, FTR_APPCOUNT, (UInt32)appCount);
      FtrSet(APP_ID, FTR_VFSAPPCOUNT, (UInt32)vfsAppCount);
      // Cheat and use version to signify an excluded application
      for(i = 0; i < appCount; i++)
      {
         for(j = 0; j < ignNumApps; j++)
         {
            if(pAppList[i].creator == ignoredApps[j])
            {
               pAppList[i].flags |= IS_IGNORED;
            }
         }
         for(j = 0; j <= mpPrefs.numItems; j++)
         {
            if(pAppList[i].creator == favApps[j].creator)
            {
               if(favApps[j].flags & IS_A_FAV)
               {
                  pAppList[i].flags |= IS_A_FAV;
               }
            }
         }

         // DAs are marked as ignored unless marked as favorite by default
         if(pAppList[i].flags == IS_A_DA)
         {
            pAppList[i].flags = IS_A_DA | IS_IGNORED;
         }
      }

      pList = FrmGetObjectPtr(pForm, FrmGetObjectIndex(pForm, AppList));
      LstSetListChoices(pList, NULL, appCount);
      LstSetDrawFunction(pList, (ListDrawDataFuncPtr)PrefsListDrawFunc);
      LstSetSelection(pList, -1);
   }
   else
   {
      FtrUnregister(APP_ID, FTR_PAPPLIST);
      FtrUnregister(APP_ID, FTR_PVFSPATHLIST);
      FtrUnregister(APP_ID, FTR_APPCOUNT);
   }

   // Set list width for sort list
   LstSetBestWidth(PrefsListSort);

   // Set list width for app list position
   LstSetBestWidth(PrefsListAppListPos);

   // get activation data
   pMpActivation = MemPtrNew(sizeof(McActivation));
   GetActivation(pMpActivation);
   FtrSet(APP_ID, FTR_ACTIVATION, (UInt32)pMpActivation);

   // Cleanup
   if(ignoredApps)
      MemPtrFree(ignoredApps);
}

static void PrefsOK(FormPtr pForm __attribute__ ((unused)))
{
   McPhlingAppList *pAppList;
   UInt32 appCount;
   UInt16 vfsAppCount;
   UInt16 i, j, k, m;
   UInt32 *ignoredApps;
   UInt32 emptyCreator;
   McPhlingPrefs mpPrefs;
   UInt32 NewNumItems32;
   JumpRefPref favApps[MAX_NUM_ITEMS + 1];
   UInt8 maxFavorites;
   UInt32 version;
   Boolean bVgaExists = false;
   McActivation *pMpActivation = NULL;
   UInt32 temp;
#ifdef STANDALONE
   Boolean bPS, bSony;
#endif
#ifndef STANDALONE
   FormPtr pNewForm = NULL;
   VgaScreenModeType screenMode;
   VgaRotateModeType rotateMode;
#endif

#ifdef STANDALONE
   // close any collapsible notifications
   if(GetCollapseGraffiti(&bPS, &bSony))
   {
      UInt16 cardNo;
      LocalID dbID;

      SysCurAppDatabase(&cardNo, &dbID);
      if(bPS)
      {
         SysNotifyUnregister(cardNo, dbID, sysNotifyDisplayResizedEvent, NULL);
      }
      else if(bSony)
      {
         SysNotifyUnregister(cardNo, dbID, sysNotifyDisplayChangeEvent, NULL);
      }
   }
#endif

   // see if vga exists
   if(_TRGVGAFeaturePresent(&version))
      bVgaExists = true;

   // Get the applist
   if(FtrGet(APP_ID, FTR_PAPPLIST, (UInt32 *)&pAppList))
      return;
   if(FtrGet(APP_ID, FTR_APPCOUNT, &appCount))
      return;
   if(FtrGet(APP_ID, FTR_NEWNUMITEMS, &NewNumItems32))
      return;
   if(FtrGet(APP_ID, FTR_VFSAPPCOUNT, &temp))
      return;
   vfsAppCount = (UInt16)temp;

   // get activation data pointer
   if(FtrGet(APP_ID, FTR_ACTIVATION, (UInt32 *)&pMpActivation))
      return;

   // save the activation data
   PrefSetAppPreferences(APP_ID, ACTIVATION, 0, pMpActivation,
		         sizeof(McActivation), true);

   // get the preferences
   GetPrefsAndNewIgnored(&mpPrefs);
   
   // Get the cache list
   GetMRUApps(&mpPrefs, favApps);

   // set the preferences
   mpPrefs.numItems        = (UInt8)NewNumItems32;
#ifdef STANDALONE
   mpPrefs.bEnabled        = CtlGetValue(Id2Ptr(PrefsEnabled));
   EnableOS5(mpPrefs.bEnabled);
#endif
   mpPrefs.bShowIcons      = CtlGetValue(Id2Ptr(PrefsShowIcons));
   mpPrefs.bClick          = CtlGetValue(Id2Ptr(PrefsClick));
   mpPrefs.bShowCurrentApp = CtlGetValue(Id2Ptr(PrefsShowCurrentApp));
   mpPrefs.bSelectLast     = CtlGetValue(Id2Ptr(PrefsSelectLast));
   mpPrefs.bVFSSupport     = CtlGetValue(Id2Ptr(PrefsVFSSupport));
   mpPrefs.sortType        = LstGetSelection(Id2Ptr(PrefsListSort));
   mpPrefs.appListPosition = LstGetSelection(Id2Ptr(PrefsListAppListPos));

   // put up "Please wait" form
#ifndef STANDALONE
   DisplayWaitForm(bVgaExists, &screenMode, &rotateMode, &pNewForm);
#endif

   // adjust numItems due to show current app
   if(mpPrefs.bShowCurrentApp)
      mpPrefs.numItems--;

   // Set the ignored apps
   j = 0;
   ignoredApps = 0;
   for(i = 0; i < appCount; i++)
   {
      if(pAppList[i].flags & IS_IGNORED)
      {
         j++; // Just counting...
      }
   }
   if(!j)
   { // Nothing to do...
      emptyCreator = 0;
      PrefSetAppPreferences(APP_ID, IGNOREPREFS, 0, &emptyCreator, 0, true);
   }
   else
   {
      ignoredApps = MemPtrNew(j * sizeof(UInt32));
      if(ignoredApps)
      {
         MemSet(ignoredApps, j * sizeof(UInt32), 0);
         j = 0;
         for(i = 0; i < appCount; i++)
         {
            if(pAppList[i].flags & IS_IGNORED)
            {
               ignoredApps[j] = pAppList[i].creator;
               for(k = 0; k <= mpPrefs.numItems; k++)
               {
                  if((favApps[k].creator) &&
                     (favApps[k].creator == ignoredApps[j]))
                  {
                        // Remove from cache list
                        for(m = k + 1; m <= mpPrefs.numItems; m++)
                        {
                           favApps[m - 1] = favApps[m];
                        }
                        MemSet(&favApps[mpPrefs.numItems],
                               sizeof(JumpRefPref), 0);
                  }
               }
               j++;
            }
         }
      }
      else
      {
         FrmCustomAlert(OutOfMemory, "1", NULL, NULL);
      }
      PrefSetAppPreferences(APP_ID, IGNOREPREFS, 0, ignoredApps,
                            j * sizeof(UInt32), true);
   }

   // Set the favorites
   for(k = 0; k <= mpPrefs.numItems; k++)
   {
      favApps[k].flags = 0;  // clear all
   }
   maxFavorites = mpPrefs.numItems + (mpPrefs.bShowCurrentApp ? 1U : 0U);
   for(i = 0; i < appCount; i++)
   {
      if(pAppList[i].flags & IS_A_FAV)
      {
         AddFavorite(&pAppList[i], &favApps[0], &mpPrefs);
      }
   }
   PrefSetAppPreferences(APP_ID, REFPREFS, 0, favApps,
                         (mpPrefs.numItems + 1) * sizeof(JumpRefPref), true);

   // save the preferences
   PrefSetAppPreferences(APP_ID, PREFS, 0, &mpPrefs, sizeof(McPhlingPrefs),
                         true);
   
   // create application cache
   CreateAppCache(mpPrefs.bShowIcons, mpPrefs.bVFSSupport, favApps,
                  mpPrefs.numItems + 1, pAppList, appCount, vfsAppCount);

   // Cleanup
   if(ignoredApps)
      MemPtrFree(ignoredApps);

   // remove "Please wait" form
#ifndef STANDALONE
   RemoveWaitForm(bVgaExists, screenMode, rotateMode, pNewForm, pForm);
#endif
}

static void AddFavorite(McPhlingAppList *pAppList, JumpRefPref *favApps,
                        McPhlingPrefs *pMpPrefs)
{
   Int16 i, k;
   Boolean gotIt = false;
   UInt8 numItems = pMpPrefs->numItems;
   UInt32 creator = pAppList->creator;

   k = 0;
   for(i = 0; i <= numItems; i++)
   {
      // favorite get first vacant slot or slot with its creator ID
      if((!favApps[i].creator) || (favApps[i].creator == creator))
      {
         k = i;
         gotIt = true;
         break;
      }
   }

   if(!gotIt)
   {
      for(i = numItems; i >= 0; i--)
      {
         if(!(favApps[i].flags & IS_A_FAV))
         {
            k = i;
            gotIt = true;
            break;
         }
      }
   }

   if(gotIt)
   {
      favApps[k].creator = creator;
      favApps[k].flags   = pAppList->flags;
   }
}

static void PrefsListDrawFunc(UInt16 index, RectanglePtr bounds,
                              Char **data __attribute__ ((unused)))
{
   McPhlingAppList *pAppList;
   MemHandle symbolH;
   DmResID symbol;
   UInt32 version;
   Boolean bVgaExists = false;
   Coord textOffset;
   FontID font = stdFont;
   FontID oldFont;
   Char name[dmDBNameLength + 1];

   // see if vga exists
   if(_TRGVGAFeaturePresent(&version))
      bVgaExists = true;

   // Get the applist
   if(FtrGet(APP_ID, FTR_PAPPLIST, (UInt32*)&pAppList))
   {
      // WinDrawChars("Error", 5, bounds->topLeft.x, bounds->topLeft.y);
      return;
   }

   // Draw the application name
   symbol = 0;
   if(pAppList[index].flags & IS_A_DA)
   {
      if(pAppList[index].flags & IS_IGNORED)
         symbol = DaBitmap;
      if(pAppList[index].flags & IS_A_FAV)
         symbol = DaSelectBitmap;
   }
   else
   {
      if(pAppList[index].flags & IS_IGNORED)
         symbol = IgnoreBitmap;
      if(pAppList[index].flags & IS_A_FAV)
         symbol = FavoriteBitmap;
   }

   if(symbol)
   {
      symbolH = DmGetResource(bitmapRsc, symbol + (bVgaExists ? 0x1000 : 0));
      WinDrawBitmap((BitmapPtr)MemHandleLock(symbolH), bounds->topLeft.x - 2,
                    bounds->topLeft.y);
      MemHandleUnlock(symbolH);
      DmReleaseResource(symbolH);
   }
   textOffset = (bVgaExists ? 22 : 13);

   if(bVgaExists)
      font = VgaBaseToVgaFont(font);
   oldFont = FntSetFont(font);

   StrCopy(name, pAppList[index].name);
   TruncateString(name, bounds->extent.x - textOffset - 1);
   TruncateString(name, bounds->extent.x - textOffset - 1);
   if(pAppList[index].flags & IS_A_VFS)
   {
      WinSetUnderlineMode(grayUnderline);
      WinDrawChars(name, StrLen(name),
                   bounds->topLeft.x + textOffset, bounds->topLeft.y);
      WinSetUnderlineMode(noUnderline);
   }
   else
   {
      WinDrawChars(name, StrLen(name),
                   bounds->topLeft.x + textOffset, bounds->topLeft.y);
   }

   FntSetFont(oldFont);
}

static void DoListSelect(Int16 selection)
{
   FormPtr pForm;
   ListPtr pList;
   McPhlingAppList *pAppList;
   UInt8 NewNumItems, numFavorites = 0;
   UInt32 NewNumItems32, i, appCount;
   Boolean bTooManyFavorites = false;
   UInt16 flags;

   if(selection < 0)
      return;
   if(FtrGet(APP_ID, FTR_PAPPLIST, (UInt32*)&pAppList))
      return;
   if(FtrGet(APP_ID, FTR_APPCOUNT, &appCount))
      return;
   if(FtrGet(APP_ID, FTR_NEWNUMITEMS, &NewNumItems32))
      return;
   NewNumItems = (UInt8)NewNumItems32;

   flags = pAppList[selection].flags;

   if((flags & (IS_A_VFS ^ 0xFFFF)) == 0)
   {
      pAppList[selection].flags = (flags & IS_A_VFS) | IS_IGNORED;
   }
   else if(flags & IS_IGNORED)
   {
      for(i = 0; i < appCount; i++)
      {
         if(pAppList[i].flags & IS_A_FAV)
         {
            numFavorites++;
         }
      }
      if(numFavorites >= NewNumItems)
      {
         pAppList[selection].flags &= IS_A_VFS;
         bTooManyFavorites = true;
      }
      else
      {
         pAppList[selection].flags = (flags & (IS_A_VFS | IS_A_DA)) | IS_A_FAV;
      }
   }
   else if(pAppList[selection].flags & IS_A_DA)
   {
      pAppList[selection].flags = (flags & (IS_A_VFS | IS_A_DA)) | IS_IGNORED;
   }
   else
   {
      pAppList[selection].flags &= IS_A_VFS;
   }

   pForm = FrmGetActiveForm();
   pList = FrmGetObjectPtr(pForm, FrmGetObjectIndex(pForm, AppList));
   LstEraseList(pList);
   LstSetSelection(pList, -1);
   LstDrawList(pList);

   if(bTooManyFavorites)
   {
      Char sTooManyFavs[4];

      FrmCustomAlert(TooManyFavorites, StrIToA(sTooManyFavs, NewNumItems),
                     NULL, NULL);
   }
}

static void ScrollList(FormPtr pForm, WinDirectionType direction)
{
   ListPtr pList = FrmGetObjectPtr(pForm, FrmGetObjectIndex(pForm, AppList));

   LstScrollList(pList, direction, LstGetVisibleItems(pList) - 1);
}

static void ChangeNumItems(FormPtr pForm, UInt16 keyDown)
{
   UInt8 NewNumItems;
   UInt32 NewNumItems32;

   FtrGet(APP_ID, FTR_NEWNUMITEMS, &NewNumItems32);
   NewNumItems = (UInt8)NewNumItems32;

   switch(keyDown) {
      case pageUpChr:
         if(NewNumItems < MAX_NUM_ITEMS)
            NewNumItems++;
         break;

      case pageDownChr:
         if(NewNumItems > MIN_NUM_ITEMS)
            NewNumItems--;
         break;
   }

   FtrSet(APP_ID, FTR_NEWNUMITEMS, (UInt32)NewNumItems);
   PrefsUpdateForm(pForm);
}

static void PrefsUpdateForm(FormPtr pForm)
{
   UInt16 wUp      = FrmGetObjectIndex(pForm, PrefsNumItemsUp);
   UInt16 wDown    = FrmGetObjectIndex(pForm, PrefsNumItemsDown);
   Char sNumber[NUM_CHARS_NUM_ITEMS];
   UInt32 NewNumItems32;
   MemHandle hNumItems;

   FtrGet(APP_ID, FTR_NEWNUMITEMS, &NewNumItems32);
   FtrGet(APP_ID, FTR_FIELDNUMITEMS, (UInt32 *)&hNumItems);

   // update num items field
   StrIToA(sNumber, NewNumItems32);
   DisplayString(sNumber, hNumItems, PrefsFieldNumItems, true);

   FrmUpdateScrollers(pForm, wUp, wDown, NewNumItems32 < MAX_NUM_ITEMS,
                      NewNumItems32 > MIN_NUM_ITEMS);
}

static void TruncateString(Char *pStr, Int16 width)
{
   UInt16 strLength = StrLen(pStr);
   Int16 strWidth = width;
   Boolean truncated;
   Char ChrEllipsis;

   // Get the max number of chars that can be displayed
   FntCharsInWidth(pStr, &strWidth, &strLength, &truncated);

   // Truncate the title and add trailing ellipsis.
   ChrHorizEllipsis(&ChrEllipsis);
   if(strLength < StrLen(pStr))
   {
      pStr[strLength - 1] = ChrEllipsis; // ellipsis
      pStr[strLength]     = '\0'; // truncate
   }
}

static void SizeForm(FormPtr pForm)
{
   RectangleType winRect;
   Coord xDiff, yDiff;
   Boolean bDraw;
   UInt32 version;
   Boolean bVgaExists = _TRGVGAFeaturePresent(&version);
   RectangleType *pOldWinRect = NULL;
   Boolean bCollapsibleGraffiti = false;

   // see if PalmSource collapsible graffiti exists
#ifdef STANDALONE
   bCollapsibleGraffiti = GetCollapseGraffiti(NULL, NULL);
#endif

   // see if vga or PalmSource collapsible graffiti exists
   if(!bVgaExists && !bCollapsibleGraffiti)
      return;

   if(FtrGet(APP_ID, FTR_OLDWINRECT, (UInt32 *)&pOldWinRect) != 0)
      return;

   bDraw = FrmVisible(pForm);

   // set new window bounds
   winRect.topLeft.x = pOldWinRect->topLeft.x;
   winRect.topLeft.y = pOldWinRect->topLeft.y;
   WinGetDisplayExtent(&winRect.extent.x, &winRect.extent.y);

   winRect.extent.x -= winRect.topLeft.x * 2;
   winRect.extent.y -= winRect.topLeft.y * 2;
   xDiff = winRect.extent.x - pOldWinRect->extent.x;
   yDiff = winRect.extent.y - pOldWinRect->extent.y;
   *pOldWinRect = winRect;

   WinSetWindowBounds(FrmGetWindowHandle(pForm), &winRect);

   if((xDiff == 0) && (yDiff == 0))
      return;

#ifdef STANDALONE
   if(yDiff > 0)
   {
      Int16 FntHeight = FntCharHeight();

      ResizeObject(pForm, AppList,
                   0, 0, 0, (yDiff / FntHeight) * FntHeight, false);
   }
   else if(yDiff < 0)
   {
      SetObjectHeight(pForm, AppList, APPLIST_HEIGHT_NOMINAL, false);
   }
#else
   ResizeObject(pForm, AppList, 0, 0, 0, yDiff, false);
#endif
   ResizeObject(pForm, AppList, 0, 0, xDiff, 0, false);
#ifdef STANDALONE
   ResizeObject(pForm, PrefsEnabled, xDiff, 0, 0, 0, false);
#endif
   ResizeObject(pForm, ListNumLabel, xDiff, 0, 0, 0, false);
   ResizeObject(pForm, NumberBox, xDiff, 0, 0, 0, false);
   ResizeObject(pForm, PrefsShowIcons, xDiff, 0, 0, 0, false);
   ResizeObject(pForm, PrefsFieldNumItems, xDiff, 0, 0, 0, false);
   ResizeObject(pForm, PrefsNumItemsUp, xDiff, 0, 0, 0, false);
   ResizeObject(pForm, PrefsNumItemsDown, xDiff, 0, 0, 0, false);
   ResizeObject(pForm, PrefsClick, xDiff, 0, 0, 0, false);
   ResizeObject(pForm, PrefsShowCurrentApp, xDiff, 0, 0, 0, false);
   ResizeObject(pForm, PrefsSelectLast, xDiff, 0, 0, 0, false);
   ResizeObject(pForm, PrefsVFSSupport, xDiff, 0, 0, 0, false);
   ResizeObject(pForm, PrefsLabelSort, 0, yDiff, 0, 0, false);
   ResizeObject(pForm, PrefsPopupTriggerSort, 0, yDiff, 0, 0, false);
   ResizeObject(pForm, PrefsListSort, 0, yDiff, 0, 0, false);
   ResizeObject(pForm, PrefsLabelAppListPos, 0, yDiff, 0, 0, false);
   ResizeObject(pForm, PrefsPopupTriggerAppListPos, 0, yDiff, 0, 0, false);
   ResizeObject(pForm, PrefsListAppListPos, 0, yDiff, 0, 0, false);
#ifndef STANDALONE
   ResizeObject(pForm, OKButton, 0, yDiff, 0, 0, false);
   ResizeObject(pForm, CancelButton, 0, yDiff, 0, 0, false);
#endif
   ResizeObject(pForm, PrefsCacheButton, 0, yDiff, 0, 0, false);
   ResizeObject(pForm, PrefsActivationButton, 0, yDiff, 0, 0, false);

   if(bDraw)
   {
      ListPtr pList = FrmGetObjectPtr(pForm, FrmGetObjectIndex(pForm, AppList));
      Int16 pos = LstGlueGetTopItem(pList);
      Int16 numVisible = LstGetVisibleItems(pList);
      Int16 numItems = LstGetNumberOfItems(pList);
      RectangleType rect;

      // adjust position of application list
      if(pos + numVisible >= numItems)
         pos = numItems - numVisible;

      if(pos < 0)
         pos = 0;
      
      LstSetTopItem(pList, pos);

      // erase the contents of the window
      FrmGetFormBounds(pForm, &rect);
      WinEraseRectangle(&rect, 0);

      // draw the form
      FrmDrawForm(pForm);
   }
}

static void ResizeObject(FormPtr pForm, UInt16 objectID,
                         Coord xMove, Coord yMove,
                         Coord xExp, Coord yExp, Boolean bDraw)
{
   RectangleType r;
   UInt16 objectIndex;
    
   if((xMove == 0) && (yMove == 0) && (xExp == 0) && (yExp == 0))
      return;
    
   objectIndex = FrmGetObjectIndex(pForm, objectID);

   FrmGetObjectBounds(pForm, objectIndex, &r);
   if(bDraw)
      FrmHideObject(pForm, objectIndex);
   r.topLeft.x += xMove;
   r.topLeft.y += yMove;
   r.extent.x  += xExp;
   r.extent.y  += yExp;
   FrmSetObjectBounds(pForm, objectIndex, &r);
    
   if(FrmGetObjectType(pForm, objectIndex) == frmGraffitiStateObj)
      GsiSetLocation(r.topLeft.x, r.topLeft.y);
    
   if(bDraw)
      FrmShowObject(pForm, objectIndex);
}

#ifdef STANDALONE
static void SetObjectHeight(FormPtr pForm, UInt16 objectID,
                            Coord yExp, Boolean bDraw)
{
   RectangleType r;
   UInt16 objectIndex;

   objectIndex = FrmGetObjectIndex(pForm, objectID);
    
   FrmGetObjectBounds(pForm, objectIndex, &r);
   if(bDraw)
      FrmHideObject(pForm, objectIndex);
   r.extent.y = yExp;
   FrmSetObjectBounds(pForm, objectIndex, &r);
    
   if(bDraw)
      FrmShowObject(pForm, objectIndex);
}
#endif

static void DisplayString(Char *str, MemHandle hField, UInt16 fieldID,
                          Boolean bRefresh)
{
   FieldPtr pField = Id2Ptr(fieldID);

   StrCopy(MemHandleLock(hField), str);
   MemHandleUnlock(hField);
   FldSetTextHandle(pField, hField);

   if(bRefresh)
      FldDrawField(pField);
}

static void CreateAppCache(Boolean bShowIcons, Boolean bVFSSupport,
                           JumpRefPref *pAppList, UInt8 numItems,
                           McPhlingAppList *pMcAppList, UInt16 appCount,
                           UInt16 vfsAppCount)
{
   UInt16 cardNo = 0;
   UInt16 i, j;
   LocalID dbID;
   DmOpenRef dbCache = NULL;
   MemHandle hIcon, hIcon2, hIconName, hVersion, hVFSPath;
   BitmapPtr pIcon, pIcon2;
   UInt32 iconHandleSize;
   Char *pIconName, *pVersion, *pVFSPath;

   // delete cache
   dbID = DmFindDatabase(cardNo, DB_NAME_CACHE);
   if(dbID)
      DmDeleteDatabase(cardNo, dbID);

   // create new cache
   if(DmCreateDatabase(0, DB_NAME_CACHE, APP_ID, DB_TYPE_CACHE, true) == 0)
      dbCache = DmOpenDatabaseByTypeCreator(DB_TYPE_CACHE, APP_ID,
                                            dmModeReadWrite);

   if(dbCache == NULL)
      return;

   // put version into resource
   hVersion = DmNewResource(dbCache, verRsc, appVersionID, DB_VER_LEN_CACHE);
   if(hVersion)
   {
      pVersion = MemHandleLock(hVersion);
      if(pVersion)
      {
         DmWrite(pVersion, 0, DB_VER_CACHE, DB_VER_LEN_CACHE);
         MemHandleUnlock(hVersion);
      }
      DmReleaseResource(hVersion);
   }

   // put in icons and names
   for(i = 0; i < numItems; i++)
   {
      Err err;
      UInt32 creator = pAppList[i].creator;
      DmOpenRef dbApp;

      // skip if no creator
      if(creator == NULL)
         continue;

      dbApp = DmOpenDatabaseByTypeCreator(sysFileTApplication, creator,
                                          dmModeReadOnly);
      if(dbApp)
      {
         err = DmOpenDatabaseInfo(dbApp, &dbID, NULL, NULL, &cardNo, NULL);
      
         if(err == errNone)
         {
            if(bShowIcons)
            {
               // get small icon
               hIcon = DmGet1Resource(iconType, SMALL_ICON);
               if(hIcon)
               {
                  pIcon = MemHandleLock(hIcon);
                  iconHandleSize = MemHandleSize(hIcon);

                  hIcon2 = DmNewResource(dbCache, creator, SMALL_ICON,
                                         iconHandleSize);
                  pIcon2 = MemHandleLock(hIcon2);
                  DmWrite(pIcon2, 0, pIcon, iconHandleSize);

                  // clean up
                  MemHandleUnlock(hIcon);
                  MemHandleUnlock(hIcon2);
                  DmReleaseResource(hIcon);
                  DmReleaseResource(hIcon2);
               }

               // get medium icon
               hIcon = DmGet1Resource(iconType, MEDIUM_ICON);
               if(hIcon)
               {
                  pIcon = MemHandleLock(hIcon);
                  iconHandleSize = MemHandleSize(hIcon);
         
                  hIcon2 = DmNewResource(dbCache, creator, MEDIUM_ICON,
                                         iconHandleSize);
                  pIcon2 = MemHandleLock(hIcon2);
                  DmWrite(pIcon2, 0, pIcon, iconHandleSize);

                  // clean up
                  MemHandleUnlock(hIcon);
                  MemHandleUnlock(hIcon2);
                  DmReleaseResource(hIcon);
                  DmReleaseResource(hIcon2);
               }
            }

            // get icon name
            hIconName = DmNewResource(dbCache, creator, ICON_NAME,
                                      dmDBNameLength + 1);
            pIconName = MemHandleLock(hIconName);
            for(j = 0; j < appCount; j++)
            {
               if(pMcAppList[j].creator == creator)
               {
                  DmWrite(pIconName, 0, pMcAppList[j].name, dmDBNameLength + 1);
                  break;
               }
            }

            // clean up
            MemHandleUnlock(hIconName);
            DmReleaseResource(hIconName);
         }
         DmCloseDatabase(dbApp);
      }
      else if(bVFSSupport)
      {
         UInt32 type;
         UInt32 localCreator;

         for(j = 0; j < appCount; j++)
         {
            // assuming a VFS at this point
            if((pMcAppList[j].creator == creator) && vfsAppCount)
            {
               FileRef fileRef;
               Err err;
   
               VFSFileOpen(pMcAppList[j].vfsVolRefNum, pMcAppList[j].vfsPath,
                           vfsModeRead, &fileRef);
               err = VFSFileDBInfo(fileRef, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, &type,
                                   &localCreator, NULL);
               if(err == errNone && type == sysFileTApplication &&
                  localCreator == creator)
               {

                  if(bShowIcons)
                  {
                     // get small icon
                     err = VFSFileDBGetResource(fileRef, iconType, SMALL_ICON,
                                                &hIcon);
                     if(hIcon)
                     {
                        pIcon = MemHandleLock(hIcon);
                        iconHandleSize = MemHandleSize(hIcon);

                        hIcon2 = DmNewResource(dbCache, creator, SMALL_ICON,
                                               iconHandleSize);
                        pIcon2 = MemHandleLock(hIcon2);
                        DmWrite(pIcon2, 0, pIcon, iconHandleSize);

                        // clean up
                        MemHandleUnlock(hIcon);
                        MemHandleUnlock(hIcon2);
                        DmReleaseResource(hIcon);
                        DmReleaseResource(hIcon2);
                     }

                     // get medium icon
                     err = VFSFileDBGetResource(fileRef, iconType, MEDIUM_ICON,
                                                &hIcon);
                     if(hIcon)
                     {
                        pIcon = MemHandleLock(hIcon);
                        iconHandleSize = MemHandleSize(hIcon);

                        hIcon2 = DmNewResource(dbCache, creator, MEDIUM_ICON,
                                               iconHandleSize);
                        pIcon2 = MemHandleLock(hIcon2);
                        DmWrite(pIcon2, 0, pIcon, iconHandleSize);

                        // clean up
                        MemHandleUnlock(hIcon);
                        MemHandleUnlock(hIcon2);
                        DmReleaseResource(hIcon);
                        DmReleaseResource(hIcon2);
                     }
                  }
               }

               // close the file
               VFSFileClose(fileRef);

               // get icon name
               hIconName = DmNewResource(dbCache, creator, ICON_NAME,
                                         dmDBNameLength + 1);
               pIconName = MemHandleLock(hIconName);
               DmWrite(pIconName, 0, pMcAppList[j].name, dmDBNameLength + 1);
               MemHandleUnlock(hIconName);
               DmReleaseResource(hIconName);

               break;
            }
         }
      }
   }

   // put in VFS paths
   if(bVFSSupport)
   {
      for(i = 0; i < appCount; i++)
      {
         UInt32 creator = pMcAppList[i].creator;

         // skip if no creator
         if(creator == NULL)
            continue;

         // skip if not VFS
         if((pMcAppList[i].flags & IS_A_VFS) == 0)
            continue;

         hVFSPath = DmNewResource(dbCache, creator, VFS_PATH, VFS_MAX_PATH + 1);
         pVFSPath = MemHandleLock(hVFSPath);
         DmWrite(pVFSPath, 0, pMcAppList[i].vfsPath, VFS_MAX_PATH + 1);

         // clean up
         MemHandleUnlock(hVFSPath);
         DmReleaseResource(hVFSPath);
      }
   }

   // close cache database
   DmCloseDatabase(dbCache);
}

static Boolean CreateDataBaseList(Boolean bVFSSupport, UInt16 *pAppCount,
                                  UInt16 *pVfsAppCount,
                                  McPhlingAppList **pMcAppList,
                                  Char **pVFSPathList)
{
   SysDBListItemType *pAppList, *pDaList;
   MemHandle hAppList, hDaList;
   UInt16 i, r;
   VFSInfo vfsInfo;
   UInt16 ramAppCount = 0;
   UInt16 ramDaCount  = 0;
   UInt16 vfsAppCount = 0;
   UInt16 totalAppCount;
   UInt32 freeRAM;
   Char sFreeRAM[12];

   // get applications that reside in ram
   SysCreateDataBaseList(sysFileTApplication, 0, &ramAppCount, &hAppList,
                         true);

   // get DAs that reside in ram
   SysCreateDataBaseList(DA_APP_TYPE, 0, &ramDaCount, &hDaList, true);

   // count number of applications on external cards
   if(bVFSSupport)
   {
      VFSSetup(&vfsInfo);
      vfsAppCount = VFSCountApps(&vfsInfo);
   }

   // create array for vfs paths
   *pVFSPathList = MemPtrNew(vfsAppCount * ((VFS_MAX_PATH + 1) * sizeof(Char)));
   if(*pVFSPathList == NULL && (vfsAppCount != 0))
   {
      MemCardInfo(0, NULL, NULL, NULL, NULL, NULL, NULL, &freeRAM);
      FrmCustomAlert(OutOfMemoryForVFSList, "2",
                     StrIToA(sFreeRAM, freeRAM / 1024), NULL);

      vfsAppCount = 0;
   }

   // create total count and allocate memory for pMcAppList
   totalAppCount  = ramAppCount + ramDaCount + vfsAppCount;
   *pMcAppList = MemPtrNew(totalAppCount * sizeof(McPhlingAppList));
   if(*pMcAppList == NULL)
   {
      MemHandleFree(hAppList);

      if(*pVFSPathList)
         MemPtrFree(*pVFSPathList);

      MemCardInfo(0, NULL, NULL, NULL, NULL, NULL, NULL, &freeRAM);
      FrmCustomAlert(OutOfMemory, "3", StrIToA(sFreeRAM, freeRAM / 1024), NULL);

      totalAppCount = 0;
      return (Boolean)totalAppCount;
   }

   // put application data from ram into pMcAppList
   if(ramAppCount)
   {
      pAppList = MemHandleLock(hAppList);
      
      //for(i = 0; i < ramAppCount; i++) {
      i = 0;
      for(r = 0; r < ramAppCount; r++)
      {
         UInt16 attr;

         // filter out hidden apps
         DmDatabaseInfo(pAppList[r].cardNo, pAppList[r].dbID, NULL, &attr,
                        NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
         if(attr & dmHdrAttrHidden)
            continue;

         StrCopy((*pMcAppList)[i].name, pAppList[r].name);
         (*pMcAppList)[i].creator = pAppList[r].creator;
         (*pMcAppList)[i].flags = 0;
         i++; // count only non-hidden apps
      }

      MemHandleUnlock(hAppList);
      MemHandleFree(hAppList);

      totalAppCount -= (r - i); // adjust for hidden apps
      ramAppCount = i; // update ramAppCount since hidden ones are now filtered
   }

   // put desk accessory data from ram into pMcAppList
   if(ramDaCount)
   {
      pDaList = MemHandleLock(hDaList);

      for(i = 0; i < ramDaCount; i++)
      {
         StrCopy((*pMcAppList)[ramAppCount + i].name, pDaList[i].name);
         (*pMcAppList)[ramAppCount + i].creator = pDaList[i].creator;
         (*pMcAppList)[ramAppCount + i].flags = IS_A_DA;
      }

      MemHandleUnlock(hDaList);
      MemHandleFree(hDaList);
   }
   // put application data from cards into pMcAppList
   if(bVFSSupport && (vfsAppCount != 0))
      VFSGetDatabaseList(pMcAppList, *pVFSPathList, &vfsInfo,
                         ramAppCount + ramDaCount);

   // sort list if necessary
   if((ramDaCount + vfsAppCount) != 0)
      SysInsertionSort(*pMcAppList, totalAppCount, sizeof(McPhlingAppList),
                       (CmpFuncPtr)DatabaseSortAlpha, 0);

   // set pAppCount
   *pAppCount = totalAppCount;

   // set pVfsAppCount
   *pVfsAppCount = vfsAppCount;

   // got it all
   return (Boolean)totalAppCount;
}

static void UpdateDatabaseList(FormPtr pForm)
{
   UInt16 i, j, ignoredCount, favoriteCount;
   Boolean bGotDatabaseList = false;
   Boolean bVFSSupport = CtlGetValue(Id2Ptr(PrefsVFSSupport));
   UInt32 temp;
   Boolean bVgaExists = false;
   McPhlingAppList *pAppList = NULL;
   Char *pVFSPathList = NULL;
   UInt16 appCount, vfsAppCount;
   UInt32 *ignoredApps  = NULL;
   UInt32 *favoriteApps = NULL;
   FormPtr pNewForm = NULL;
   VgaScreenModeType screenMode;
   VgaRotateModeType rotateMode;

   // see if VGA screen exists
   if(_TRGVGAFeaturePresent(&temp))
      bVgaExists = true;

   // Get the applist, vfs path list and appcount
   if(FtrGet(APP_ID, FTR_PAPPLIST, (UInt32 *)&pAppList))
      return;
   if(FtrGet(APP_ID, FTR_PVFSPATHLIST, (UInt32 *)&pVFSPathList))
      return;
   if(FtrGet(APP_ID, FTR_APPCOUNT, &temp))
      return;
   appCount = (UInt16)temp;
   if(FtrGet(APP_ID, FTR_VFSAPPCOUNT, &temp))
      return;
   vfsAppCount = (UInt16)temp;

   // put up "Please wait" form
#ifdef STANDALONE
   if(bVFSSupport)
      DisplayWaitForm(bVgaExists, &screenMode, &rotateMode, &pNewForm);
#else
   DisplayWaitForm(bVgaExists, &screenMode, &rotateMode, &pNewForm);
#endif

   // get the ignored and favorite apps from the list
   ignoredCount  = 0;
   favoriteCount = 0;
   for(i = 0; i < appCount; i++)
   {
      if(pAppList[i].flags & IS_IGNORED)
      {
         ignoredCount++; // Just counting...
      }
      if(pAppList[i].flags & IS_A_FAV)
      {
         favoriteCount++; // Just counting...
      }
   }
   ignoredApps  = MemPtrNew(ignoredCount  * sizeof(UInt32));
   favoriteApps = MemPtrNew(favoriteCount * sizeof(UInt32));

   ignoredCount  = 0;
   favoriteCount = 0;
   for(i = 0; i < appCount; i++)
   {
      if(pAppList[i].flags & IS_IGNORED)
      {
         ignoredApps[ignoredCount] = pAppList[i].creator;
         ignoredCount++;
      }
      if(pAppList[i].flags & IS_A_FAV)
      {
         favoriteApps[favoriteCount] = pAppList[i].creator;
         favoriteCount++;
      }
   }

   // remove old features and free pAppList
   FtrUnregister(APP_ID, FTR_PAPPLIST);
   FtrUnregister(APP_ID, FTR_PVFSPATHLIST);
   FtrUnregister(APP_ID, FTR_APPCOUNT);
   FtrUnregister(APP_ID, FTR_VFSAPPCOUNT);
   if(pAppList)
      MemPtrFree(pAppList);
   if(pVFSPathList)
      MemPtrFree(pVFSPathList);

   // Get every stinking little application
   bGotDatabaseList = CreateDataBaseList(bVFSSupport, &appCount, &vfsAppCount,
                                         &pAppList, &pVFSPathList);

   // remove "Please wait" form
#ifdef STANDALONE
   if(bVFSSupport)
      RemoveWaitForm(bVgaExists, screenMode, rotateMode, pNewForm, pForm);
#else
   RemoveWaitForm(bVgaExists, screenMode, rotateMode, pNewForm, pForm);
#endif

   // fill database list
   if(bGotDatabaseList)
   {
      ListPtr pList;

      // Publish the database list handle
      FtrSet(APP_ID, FTR_PAPPLIST, (UInt32)pAppList);
      FtrSet(APP_ID, FTR_PVFSPATHLIST, (UInt32)pVFSPathList);
      FtrSet(APP_ID, FTR_APPCOUNT, (UInt32)appCount);
      FtrSet(APP_ID, FTR_VFSAPPCOUNT, (UInt32)vfsAppCount);

      // Cheat and use version to signify an excluded application
      for(i = 0; i < appCount; i++)
      {
         for(j = 0; j < ignoredCount; j++)
         {
            if(pAppList[i].creator == ignoredApps[j])
            {
               pAppList[i].flags |= IS_IGNORED;
            }
         }
         for(j = 0; j < favoriteCount; j++)
         {
            if(pAppList[i].creator == favoriteApps[j])
            {
               pAppList[i].flags |= IS_A_FAV;
            }
         }

         // DAs are marked as ignored unless marked as favorite by default
         if(pAppList[i].flags == IS_A_DA)
         {
            pAppList[i].flags = IS_A_DA | IS_IGNORED;
         }
      }

      pList = FrmGetObjectPtr(pForm, FrmGetObjectIndex(pForm, AppList));
      LstSetListChoices(pList, NULL, appCount);
      LstSetDrawFunction(pList, (ListDrawDataFuncPtr)PrefsListDrawFunc);
      LstSetTopItem(pList, 0);

      LstDrawList(pList);
   }
   else
   {
      FtrUnregister(APP_ID, FTR_PAPPLIST);
      FtrUnregister(APP_ID, FTR_PVFSPATHLIST);
      FtrUnregister(APP_ID, FTR_APPCOUNT);
      FtrUnregister(APP_ID, FTR_VFSAPPCOUNT);
   }

   // clean up
   if(ignoredApps)
      MemPtrFree(ignoredApps);
   if(favoriteApps)
      MemPtrFree(favoriteApps);
}

static Boolean IsAppInRam(UInt32 creator)
{
   DmOpenRef dbApp;
   Boolean bIsInRam = false;

   dbApp = DmOpenDatabaseByTypeCreator(sysFileTApplication, creator,
                                       dmModeReadOnly);

   if(dbApp)
   {
      DmCloseDatabase(dbApp);
      bIsInRam = true;
   }

   return bIsInRam;
} 

static Int16 DatabaseSortAlpha(McPhlingAppList *a1, McPhlingAppList *a2,
                               Int32 unused __attribute__ ((unused)))
{
   return StrCaselessCompare(a1->name, a2->name);
}

#ifdef STANDALONE
static Err MySysNotifyDisplayChangeEvent(SysNotifyParamType *np
                                         __attribute__ ((unused)))
{
   EventType newEvent;
	
   MemSet(&newEvent, sizeof(EventType), 0);

   newEvent.eType = winDisplayChangedEvent;
	
   EvtAddUniqueEventToQueue(&newEvent, 0, true);
	
   return 0;
}
#endif

///////////////////////
// Generic Functions //
///////////////////////

static void DisplaySimpleForm(UInt16 rscID)
{
   FormPtr pForm = FrmGetActiveForm();
   FormPtr pNewForm = FrmInitForm(rscID);
   VgaScreenModeType screenMode;
   VgaRotateModeType rotateMode;
   UInt32 version;
   Boolean bVgaExists = false;

   // see if vga exists
   if(_TRGVGAFeaturePresent(&version))
      bVgaExists = true;

   if(bVgaExists)
   {
      VgaGetScreenMode(&screenMode, &rotateMode);
      VgaSetScreenMode(screenModeScaleToFit, rotateMode);
   }

   // show the about form
   FrmDoDialog(pNewForm);

   if(bVgaExists)
      VgaSetScreenMode(screenMode, rotateMode);

   FrmDeleteForm(pNewForm);
   FrmSetActiveForm(pForm);
}

#if STANDALONE
static Err SendMe(Char *prefix)
{
   UInt16 cardNo;
   LocalID dbID;
   Err err;
   Char dbName[dmDBNameLength];
   Char *dbNamePRC = NULL;

   dbNamePRC = MemPtrNew(sizeof(Char) * (StrLen(prefix) + dmDBNameLength + 4));

   err = SysCurAppDatabase(&cardNo, &dbID);
   err = DmDatabaseInfo(cardNo, dbID, dbName, NULL, NULL, NULL, NULL, NULL,
                        NULL, NULL, NULL, NULL, NULL);
   StrPrintF(dbNamePRC, "%s%s.prc", prefix, dbName);
   err = SendDatabase(cardNo, dbID, dbNamePRC, dbName);

   // clean up
   if(dbNamePRC)
      MemPtrFree(dbNamePRC);

   return err;
}

static Err SendDatabase(UInt16 cardNo, LocalID dbID, Char *nameP,
                        Char *descriptionP)
{
   ExgSocketType exgSocket;
   Err err;

   // Create exgSocket structure
   MemSet(&exgSocket, sizeof(exgSocket), 0);
   exgSocket.description = descriptionP;
   exgSocket.name = nameP;

   // Start and exchange put operation
   err = ExgPut(&exgSocket);
   if(!err)
   {
      // This function converts a palm database into its external (public)
      // format. The first parameter is a callback that will be passed parts of
      // the database to send or write.
      err = ExgDBWrite(WriteDBData, &exgSocket, NULL, dbID, cardNo);
      // Disconnect Exg and pass error
      err = ExgDisconnect(&exgSocket, err);
   }

   return err;
}

static Err WriteDBData(const void *dataP, UInt32 *sizeP, void *userDataP)
{
   Err err;

   // Try to send as many bytes as were requested by the caller
   *sizeP = ExgSend((ExgSocketPtr)userDataP, (void*)dataP, *sizeP, &err);

   return err;
}
#endif

///////////////////////////////
// Activation Form Functions //
///////////////////////////////

static UInt16 ActivationDoDialog(FormPtr pCurrentForm)
{
   FormPtr pForm;
   McActivation *pMpActivation, *pMpActivationTemp;
   UInt16 retVal;

   // get activation data and setup temp activation data
   FtrGet(APP_ID, FTR_ACTIVATION, (UInt32 *)&pMpActivation);

   pMpActivationTemp = MemPtrNew(sizeof(McActivation));
   *pMpActivationTemp = *pMpActivation;
   FtrSet(APP_ID, FTR_ACTTEMP, (UInt32)pMpActivationTemp);

   // init form
   pForm = FrmInitForm(ActForm);

   // set new form
   FrmSetActiveForm(pForm);

   // set alarm form event handler
   FrmSetEventHandler(pForm, ActivationEventHandler);

   // initialize and draw the form
   ActivationInit(pForm);
   FrmDrawForm(pForm);

   // now wait for button to be pressed
   retVal = FrmDoDialog(pForm);

   // now clean up form
   FrmEraseForm(pForm);
   FrmDeleteForm(pForm);
   FrmSetActiveForm(pCurrentForm);

   // clean up activation stuff
   FtrUnregister(APP_ID, FTR_ACTTEMP);
   MemPtrFree(pMpActivationTemp);

   // return value
   return retVal;
}

Boolean ActivationEventHandler(EventPtr event)
{
   FormPtr pForm = FrmGetActiveForm();
   Boolean handled = true;
   McActivation *pMpActivationTemp;

   // get global activation data
   FtrGet(APP_ID, FTR_ACTTEMP, (UInt32 *)&pMpActivationTemp);

   switch(event->eType)
   {
/* Not used
      case frmOpenEvent:
         ActivationInit(pForm);
         FrmDrawForm(pForm);
         break;
      
*/
      case frmUpdateEvent:
         FrmDrawForm(pForm);
         break;

      case ctlSelectEvent:
         switch(event->data.ctlEnter.controlID)
         {
            case ActCheckboxSwipeActivation:
            case ActCheckboxKeyboardActivation:
               ActivationGetData(pMpActivationTemp, true, true);
               ActivationUpdateForm(pForm, pMpActivationTemp);
               break;

            case OKButton:
               ActivationOK();
               handled = false;
               break;

            case CancelButton:
               handled = false;
               break;

            case ActDefaultButton:
               if(!FrmAlert(DefaultAlert))
	          ActivationMakeDefault(pForm);
               break;

            default:
               handled = false;
               break;
         }
         break;

      case popSelectEvent:
         switch(event->data.popSelect.controlID)
         {
            CustomData cd;

            case ActPopupTriggerKeyboardApps:
               ActivationGetData(pMpActivationTemp, false, true);
               if(event->data.popSelect.selection == CUSTOM_POSITION)
               {
                  cd.kit            = pMpActivationTemp->keyInfoApps;
                  cd.bCustomDetails = pMpActivationTemp->bCustomDetails;

                  CustomButtonDoDialog(pForm, &cd);

                  pMpActivationTemp->keyInfoApps    = cd.kit;
                  pMpActivationTemp->bCustomDetails = cd.bCustomDetails;

                  ActivationUpdateForm(pForm, pMpActivationTemp);
               }
               else
               {
                  pMpActivationTemp->keyInfoApps.bCustom = false;
                  handled = false;
               }
               break;

            case ActPopupTriggerKeyboardBack:
               ActivationGetData(pMpActivationTemp, true, false);
               if(event->data.popSelect.selection == CUSTOM_POSITION)
               {
                  cd.kit            = pMpActivationTemp->keyInfoBack;
                  cd.bCustomDetails = pMpActivationTemp->bCustomDetails;

                  CustomButtonDoDialog(pForm, &cd);

                  pMpActivationTemp->keyInfoBack    = cd.kit;
                  pMpActivationTemp->bCustomDetails = cd.bCustomDetails;

                  ActivationUpdateForm(pForm, pMpActivationTemp);
               }
               else
               {
                  pMpActivationTemp->keyInfoBack.bCustom = false;
                  handled = false;
               }
               break;

            default:
               handled = false;
               break;
         }
         break;

      case frmCloseEvent:
         FrmEraseForm(pForm);
         FrmDeleteForm(pForm);
         break;

      default:
         handled = false;
         break;
   }
   
   return handled;
}

static void ActivationInit(FormPtr pForm)
{
   McActivation *pMpActivationTemp = NULL;
   UInt32 version;
   VgaRotateModeType rotateMode;
   ListPtr pList = Id2Ptr(ActListKeyboardApps);

   if(_TRGVGAFeaturePresent(&version))
   {
      VgaGetScreenMode(NULL, &rotateMode);
      VgaSetScreenMode(screenMode1To1, rotateMode);
      VgaFormModify(pForm, vgaFormModify160To240);
   }

   // show the command bar option if OS 5
#ifdef STANDALONE
   FrmShowObject(pForm, FrmGetObjectIndex(pForm, ActCheckboxCmdBarActivation));
#endif

   // copy the apps list to the back list (saves memory)
   LstSetListChoices(Id2Ptr(ActListKeyboardBack), LstGlueGetItemsText(pList),
                     LstGetNumberOfItems(pList));

   // Set list widths
   LstSetBestWidth(ActListAppsSwipeFrom);
   LstSetBestWidth(ActListAppsSwipeTo);
   LstSetBestWidth(ActListBackSwipeFrom);
   LstSetBestWidth(ActListBackSwipeTo);
   LstSetBestWidth(ActListKeyboardApps);
   LstSetBestWidth(ActListKeyboardBack);

   // get activation pointer information
   FtrGet(APP_ID, FTR_ACTTEMP, (UInt32 *)&pMpActivationTemp);

   // update form
   ActivationUpdateForm(pForm, pMpActivationTemp);
}

static void ActivationOK(void)
{
   McActivation *pMpActivation, *pMpActivationTemp;

   // get activation pointer information
   FtrGet(APP_ID, FTR_ACTIVATION, (UInt32 *)&pMpActivation);
   FtrGet(APP_ID, FTR_ACTTEMP, (UInt32 *)&pMpActivationTemp);

   // get data from form
   ActivationGetData(pMpActivationTemp, true, true);

   // copy data to final destination
   *pMpActivation = *pMpActivationTemp;
}

static void ActivationGetData(McActivation *pMpActivation,
                              Boolean bGetCustomApps, Boolean bGetCustomBack)
{
   // get the preferences
   pMpActivation->bSwipe        =
      CtlGetValue(Id2Ptr(ActCheckboxSwipeActivation));
   pMpActivation->swipeAppsFrom = LstGetSelection(Id2Ptr(ActListAppsSwipeFrom));
   pMpActivation->swipeAppsTo   = LstGetSelection(Id2Ptr(ActListAppsSwipeTo));
   pMpActivation->swipeBackFrom = LstGetSelection(Id2Ptr(ActListBackSwipeFrom));
   pMpActivation->swipeBackTo   = LstGetSelection(Id2Ptr(ActListBackSwipeTo));
   pMpActivation->bKeyboard     =
      CtlGetValue(Id2Ptr(ActCheckboxKeyboardActivation));
   pMpActivation->bKeyboardHoldApps =
      CtlGetValue(Id2Ptr(ActCheckboxKeyboardHoldApps));
   pMpActivation->bKeyboardHoldBack =
      CtlGetValue(Id2Ptr(ActCheckboxKeyboardHoldBack));
   // pMpActivation->bCustomDetails = false; // already set
   pMpActivation->bCommandBar    =
      CtlGetValue(Id2Ptr(ActCheckboxCmdBarActivation));
   pMpActivation->unused03       = false;
   pMpActivation->unused04       = false;
   pMpActivation->unused05       = false;
   pMpActivation->unused06       = false;
   pMpActivation->unused07       = false;
   pMpActivation->unused08       = false;
   pMpActivation->unused09       = false;
   pMpActivation->unused10       = false;
   pMpActivation->unused11       = false;
   pMpActivation->unused12       = false;
   pMpActivation->unused13       = false;
   if(bGetCustomApps)
      ConvertButtonListToKey(&(pMpActivation->keyInfoApps),
                             LstGetSelection(Id2Ptr(ActListKeyboardApps)),
                             false);
   if(bGetCustomBack)
      ConvertButtonListToKey(&(pMpActivation->keyInfoBack),
                             LstGetSelection(Id2Ptr(ActListKeyboardBack)),
                             false);
   // "Sel.:" has no Custom option, so (unlike Apps:/Back: above) it's
   // always safe to read unconditionally here.
   ConvertSelListToKey(&(pMpActivation->keyInfoSel),
                       LstGetSelection(Id2Ptr(ActListKeyboardSel)),
                       false);
}

static void ActivationMakeDefault(FormPtr pForm)
{
   McActivation *pMpActivationTemp, mpActivationSave;

   // get saved activation
   GetActivation(&mpActivationSave);

   // get pointer to activation temp
   FtrGet(APP_ID, FTR_ACTTEMP, (UInt32 *)&pMpActivationTemp);

   // delete preference
   PrefSetAppPreferences(APP_ID, ACTIVATION, 0, NULL, 0, true);

   // get activation stuff and set form values
   GetActivation(pMpActivationTemp);

   // update form
   ActivationUpdateForm(pForm, pMpActivationTemp);

   // save the activation data
   PrefSetAppPreferences(APP_ID, ACTIVATION, 0, &mpActivationSave,
		         sizeof(McActivation), true);
}

static void ActivationUpdateForm(FormPtr pForm, McActivation *pMpActivation)
{
   ListPtr pList;
   UInt16 sel;
   Int16 visibleItems;
 
   // set swipe activation boolean
   CtlSetValue(Id2Ptr(ActCheckboxSwipeActivation), pMpActivation->bSwipe);

   // swipe labels
   if(pMpActivation->bSwipe)
   {
      FrmShowObject(pForm, FrmGetObjectIndex(pForm, ActLabelSwipeApps));
      FrmShowObject(pForm, FrmGetObjectIndex(pForm, ActLabelSwipeBack));
   }
   else
   {
      FrmHideObject(pForm, FrmGetObjectIndex(pForm, ActLabelSwipeApps));
      FrmHideObject(pForm, FrmGetObjectIndex(pForm, ActLabelSwipeBack));
   }

   // swipe "apps from" activation
   pList = Id2Ptr(ActListAppsSwipeFrom);
   LstSetSelection(pList, pMpActivation->swipeAppsFrom);
   CtlSetLabel(Id2Ptr(ActPopupTriggerAppsSwipeFrom),
               LstGetSelectionText(pList, pMpActivation->swipeAppsFrom));
   if(pMpActivation->bSwipe)
   {
      FrmShowObject(pForm, FrmGetObjectIndex(pForm, ActLabelAppsSwipeFrom));
      FrmShowObject(pForm, FrmGetObjectIndex(pForm,
			                     ActPopupTriggerAppsSwipeFrom));
   }
   else
   {
      FrmHideObject(pForm, FrmGetObjectIndex(pForm, ActLabelAppsSwipeFrom));
      FrmHideObject(pForm, FrmGetObjectIndex(pForm,
			                     ActPopupTriggerAppsSwipeFrom));
   }
   
   // swipe "apps to" activation
   pList = Id2Ptr(ActListAppsSwipeTo);
   LstSetSelection(pList, pMpActivation->swipeAppsTo);
   CtlSetLabel(Id2Ptr(ActPopupTriggerAppsSwipeTo),
               LstGetSelectionText(pList, pMpActivation->swipeAppsTo));
   if(pMpActivation->bSwipe)
   {
      FrmShowObject(pForm, FrmGetObjectIndex(pForm, ActLabelAppsSwipeTo));
      FrmShowObject(pForm, FrmGetObjectIndex(pForm,
			                     ActPopupTriggerAppsSwipeTo));
   }
   else
   {
      FrmHideObject(pForm, FrmGetObjectIndex(pForm, ActLabelAppsSwipeTo));
      FrmHideObject(pForm, FrmGetObjectIndex(pForm,
			                     ActPopupTriggerAppsSwipeTo));
   }
   
   // swipe "back from" activation
   pList = Id2Ptr(ActListBackSwipeFrom);
   LstSetSelection(pList, pMpActivation->swipeBackFrom);
   CtlSetLabel(Id2Ptr(ActPopupTriggerBackSwipeFrom),
               LstGetSelectionText(pList, pMpActivation->swipeBackFrom));
   if(pMpActivation->bSwipe)
   {
      FrmShowObject(pForm, FrmGetObjectIndex(pForm, ActLabelBackSwipeFrom));
      FrmShowObject(pForm, FrmGetObjectIndex(pForm,
			                     ActPopupTriggerBackSwipeFrom));
   }
   else
   {
      FrmHideObject(pForm, FrmGetObjectIndex(pForm, ActLabelBackSwipeFrom));
      FrmHideObject(pForm, FrmGetObjectIndex(pForm,
			                     ActPopupTriggerBackSwipeFrom));
   }
   
   // swipe "back to" activation
   pList = Id2Ptr(ActListBackSwipeTo);
   LstSetSelection(pList, pMpActivation->swipeBackTo);
   CtlSetLabel(Id2Ptr(ActPopupTriggerBackSwipeTo),
               LstGetSelectionText(pList, pMpActivation->swipeBackTo));
   if(pMpActivation->bSwipe)
   {
      FrmShowObject(pForm, FrmGetObjectIndex(pForm, ActLabelBackSwipeTo));
      FrmShowObject(pForm, FrmGetObjectIndex(pForm,
			                     ActPopupTriggerBackSwipeTo));
   }
   else
   {
      FrmHideObject(pForm, FrmGetObjectIndex(pForm, ActLabelBackSwipeTo));
      FrmHideObject(pForm, FrmGetObjectIndex(pForm,
			                     ActPopupTriggerBackSwipeTo));
   }
   
   // set command bar activation boolean
   CtlSetValue(Id2Ptr(ActCheckboxCmdBarActivation),
               pMpActivation->bCommandBar);

   // set keyboard activation boolean
   CtlSetValue(Id2Ptr(ActCheckboxKeyboardActivation), pMpActivation->bKeyboard);

   // set keyboard apps hold information
   CtlSetValue(Id2Ptr(ActCheckboxKeyboardHoldApps),
                      pMpActivation->bKeyboardHoldApps);
   if(pMpActivation->bKeyboard)
   {
      FrmShowObject(pForm, FrmGetObjectIndex(pForm,
                                             ActCheckboxKeyboardHoldApps));
   }
   else
   {
      FrmHideObject(pForm, FrmGetObjectIndex(pForm,
                                             ActCheckboxKeyboardHoldApps));
   }

   // set keyboard apps hold information
   CtlSetValue(Id2Ptr(ActCheckboxKeyboardHoldBack),
                      pMpActivation->bKeyboardHoldBack);
   if(pMpActivation->bKeyboard)
   {
      FrmShowObject(pForm, FrmGetObjectIndex(pForm,
                                             ActCheckboxKeyboardHoldBack));
   }
   else
   {
      FrmHideObject(pForm, FrmGetObjectIndex(pForm,
                                             ActCheckboxKeyboardHoldBack));
   }

   // set keyboard activation for application list
   sel = ConvertButtonListToKey(&(pMpActivation->keyInfoApps), 0, true);
   pList = Id2Ptr(ActListKeyboardApps);
   LstSetSelection(pList, sel);
   visibleItems = LstGetVisibleItems(pList);
   LstSetTopItem(pList, (sel / visibleItems) * visibleItems);
   CtlSetLabel(Id2Ptr(ActPopupTriggerKeyboardApps),
               LstGetSelectionText(pList, sel));
   if(pMpActivation->bKeyboard)
   {
      FrmShowObject(pForm, FrmGetObjectIndex(pForm, ActLabelKeyboardApps));
      FrmShowObject(pForm, FrmGetObjectIndex(pForm,
                                             ActPopupTriggerKeyboardApps));
      FrmShowObject(pForm, FrmGetObjectIndex(pForm, ActLabelKeyboardHold));
   }
   else
   {
      FrmHideObject(pForm, FrmGetObjectIndex(pForm, ActLabelKeyboardApps));
      FrmHideObject(pForm, FrmGetObjectIndex(pForm,
                                             ActPopupTriggerKeyboardApps));
      FrmHideObject(pForm, FrmGetObjectIndex(pForm, ActLabelKeyboardHold));
   }
   
   // set keyboard activation for back
   sel = ConvertButtonListToKey(&(pMpActivation->keyInfoBack), 0, true);
   pList = Id2Ptr(ActListKeyboardBack);
   LstSetSelection(pList, sel);
   visibleItems = LstGetVisibleItems(pList);
   LstSetTopItem(pList, (sel / visibleItems) * visibleItems);
   CtlSetLabel(Id2Ptr(ActPopupTriggerKeyboardBack),
               LstGetSelectionText(pList, sel));
   if(pMpActivation->bKeyboard)
   {
      FrmShowObject(pForm, FrmGetObjectIndex(pForm, ActLabelKeyboardBack));
      FrmShowObject(pForm, FrmGetObjectIndex(pForm,
                                             ActPopupTriggerKeyboardBack));
   }
   else
   {
      FrmHideObject(pForm, FrmGetObjectIndex(pForm, ActLabelKeyboardBack));
      FrmHideObject(pForm, FrmGetObjectIndex(pForm,
                                             ActPopupTriggerKeyboardBack));
   }

   // set keyboard activation for select-highlighted-item
   sel = ConvertSelListToKey(&(pMpActivation->keyInfoSel), 0, true);
   pList = Id2Ptr(ActListKeyboardSel);
   LstSetSelection(pList, sel);
   visibleItems = LstGetVisibleItems(pList);
   LstSetTopItem(pList, (sel / visibleItems) * visibleItems);
   CtlSetLabel(Id2Ptr(ActPopupTriggerKeyboardSel),
               LstGetSelectionText(pList, sel));
   if(pMpActivation->bKeyboard)
   {
      FrmShowObject(pForm, FrmGetObjectIndex(pForm, ActLabelKeyboardSel));
      FrmShowObject(pForm, FrmGetObjectIndex(pForm,
                                             ActPopupTriggerKeyboardSel));
   }
   else
   {
      FrmHideObject(pForm, FrmGetObjectIndex(pForm, ActLabelKeyboardSel));
      FrmHideObject(pForm, FrmGetObjectIndex(pForm,
                                             ActPopupTriggerKeyboardSel));
   }
}

static UInt16 ConvertButtonListToKey(KeyInfoType *keyInfo, UInt16 sel,
                                     Boolean bReverseLookup)
{
   KeyInfoType charArray[] =
      {{false, chrNull,
               chrNull,
               chrNull},
       // custom (CUSTOM_POSITION)
       {true,  chrNull,
               chrNull,
               chrNull},
       // Silk: Left Top (Apps)
       {false, vchrLaunch,
               chrNull,
               commandKeyMask},
       // Silk: Left Bottom (Menu)
       {false, vchrMenu,
               chrNull,
               commandKeyMask},
       // Silk: Right Top (Favorite)
       {false, vchrCalc,
               chrNull,
               commandKeyMask},
       // Silk: Right Bottom (Find)
       {false, vchrFind,
               chrNull,
               commandKeyMask},
       // Silk: Alpha Keyboard
       {false, vchrKeyboardAlpha,
               chrNull,
               commandKeyMask},
       // Silk: Numeric Keyboard
       {false, vchrKeyboardNumeric,
               chrNull,
               commandKeyMask},
       // Page Up
       {false, vchrPageUp,
               chrNull,
               commandKeyMask},
       // Page Down
       {false, vchrPageDown,
               chrNull,
               commandKeyMask},
       // Hard Key #1
       {false, vchrHard1,
               vchrHard1,
               willSendUpKeyMask | commandKeyMask},
       // Hard Key #2
       {false, vchrHard2,
               vchrHard2,
               willSendUpKeyMask | commandKeyMask},
       // Hard Key #3
       {false, vchrHard3,
               vchrHard3,
               willSendUpKeyMask | commandKeyMask},
       // Hard Key #4
       {false, vchrHard4,
               vchrHard4,
               willSendUpKeyMask | commandKeyMask},
       // Legacy: Hard Key #1
       {false, vchrHard1,
               chrNull,
               commandKeyMask},
       // Legacy: Hard Key #2
       {false, vchrHard2,
               chrNull,
               commandKeyMask},
       // Legacy: Hard Key #3
       {false, vchrHard3,
               chrNull,
               commandKeyMask},
       // Legacy: Hard Key #4
       {false, vchrHard4,
               chrNull,
               commandKeyMask},
       // Fossil: Rocker Up
       {false, FOSSIL_ROCKER_UP,
               chrNull,
               commandKeyMask},
       // Fossil: Rocker Down
       {false, FOSSIL_ROCKER_DOWN,
               chrNull,
               commandKeyMask},
       // Fossil: Rocker Push
       {false, FOSSIL_ROCKER_PUSH,
               chrNull,
               commandKeyMask},
       // HandEra: Jog Up
       {false, vchrTrgJogUp,
               chrNull,
               commandKeyMask},
       // HandEra: Jog Down
       {false, vchrTrgJogDown,
               chrNull,
               commandKeyMask},
       // HandEra: Jog Push
       {false, vchrTrgJogPush,
               chrNull,
               commandKeyMask},
       // HandEra: Auxiliary
       {false, vchrTrgAux,
               chrNull,
               commandKeyMask},
       // Keyboard: Fn+Up
       {false, vchrPageUp,
               chrNull,
               optionKeyMask | commandKeyMask},
       // Keyboard: Fn+Down
       {false, vchrPageDown,
               chrNull,
               optionKeyMask | commandKeyMask},
       // Keyboard: Fn+Space
       {false, chrSpace,
               chrNull,
               optionKeyMask},
       // Kyocera: Jog Up
       {false, kyoceraJogUp,
               kyoceraJogUp,
               commandKeyMask},
       // Kyocera: Jog Down
       {false, kyoceraJogDown,
               kyoceraJogDown,
               commandKeyMask},
       // Kyocera: Jog Push
       {false, kyoceraJogPush,
               kyoceraJogPush,
               commandKeyMask},
       // Kyocera: Volume Up
       {false, kyoceraVolumeUp,
               chrNull,
               commandKeyMask},
       // Kyocera: Volume Down
       {false, kyoceraVolumeDown,
               chrNull,
               commandKeyMask},
       // Kyocera: OK Button
       {false, kyoceraOKButton,
               chrNull,
               commandKeyMask},
       // Legacy: 5-way Up
       {false, vchrPageUp,
               navBitUp    | navChangeUp,
               commandKeyMask},
       // Legacy: 5-way Down
       {false, vchrPageDown,
               navBitDown  | navChangeDown,
               commandKeyMask},
       // Legacy: 5-way Left
       {false, vchrNavChange,
               navBitLeft  | navChangeLeft,
               commandKeyMask},
       // Legacy: 5-way Right
       {false, vchrNavChange,
               navBitRight | navChangeRight,
               commandKeyMask},
       // Palm: Voice Memo
       {false, vchrHard5,
               chrNull,
               commandKeyMask},
       // Palm: Slik Clock
       {false, vchrSilkClock,
               chrNull,
               commandKeyMask},
       // Palm: Slik Brightness
       {false, vchrPopupBrightness,
               chrNull,
               commandKeyMask},
       // Palm: 5-way Up
       {false, vchrPageUp,
               vchrPageUp,
               willSendUpKeyMask | commandKeyMask},
       // Palm: 5-way Down
       {false, vchrPageDown,
               vchrPageDown,
               willSendUpKeyMask | commandKeyMask},
       // Palm: 5-way Left
       {false, vchrRockerLeft,
               vchrRockerLeft,
               willSendUpKeyMask | commandKeyMask},
       // Palm: 5-way Right
       {false, vchrRockerRight,
               vchrRockerRight,
               willSendUpKeyMask | commandKeyMask},
       // Sony: ALT key
       {false, vchrHWKeyboardPush,
               SONY_HW_KEY_MASK | SONY_ALT_KEY_MASK,
               commandKeyMask},
       // Sony: CTRL key
       {false, vchrHWKeyboardPush,
               SONY_HW_KEY_MASK | SONY_CONTROL_KEY_MASK,
               commandKeyMask},
       // Sony: Jog Up
       {false, vchrJogUp,
               chrNull,
               commandKeyMask},
       // Sony: Jog Down
       {false, vchrJogDown,
               chrNull,
               commandKeyMask},
       // Sony: Jog Push
       {false, vchrJogPush,
               chrNull,
               commandKeyMask},
       // Sony: Jog Back
       {false, vchrJogBack,
               chrNull,
               commandKeyMask},
       // Tapwave: Left Trigger
       {false, 0x1B04,
               chrNull,
               willSendUpKeyMask | commandKeyMask},
       // Tapwave: Right Trigger
       {false, 0x1B05,
               chrNull,
               willSendUpKeyMask | commandKeyMask},
       // Tapwave: Function Key
       {false, vchrThumbWheelBack,
               chrNull,
               willSendUpKeyMask | commandKeyMask},
       // Tapwave: Home Key
       {false, vchrLaunch,
               chrNull,
               willSendUpKeyMask | commandKeyMask},
       // Tapwave: Blue
       {false, 0x1B06,
               chrNull,
               willSendUpKeyMask | commandKeyMask},
       // Tapwave: Yellow
       {false, 0x1B07,
               chrNull,
               willSendUpKeyMask | commandKeyMask},
       // Tapwave: Green
       {false, 0x1B08,
               chrNull,
               willSendUpKeyMask | commandKeyMask},
       // Tapwave: Red
       {false, 0x1B09,
               chrNull,
               willSendUpKeyMask | commandKeyMask},
       // Tapwave: Bluetooth
       {false, 0x1B10,
               chrNull,
               commandKeyMask},
       // Tapwave: Joy Up
       {false, vchrRockerUp,
               chrNull,
               willSendUpKeyMask | commandKeyMask},
       // Tapwave: Joy Down
       {false, vchrRockerDown,
               chrNull,
               willSendUpKeyMask | commandKeyMask},
       // Tapwave: Joy Left
       {false, vchrRockerLeft,
               chrNull,
               willSendUpKeyMask | commandKeyMask},
       // Tapwave: Joy Right
       {false, vchrRockerRight,
               chrNull,
               willSendUpKeyMask | commandKeyMask},
       // Handspring Treo: Option+Up
       {false, chrLeftArrow,
               chrNull,
               optionKeyMask},
       // Handspring Treo: Option+Down
       {false, chrRightArrow,
               chrNull,
               optionKeyMask},
       // Handspring Treo: Rocker Up
       {false, vchrPageUp,
               chrNull,
               commandKeyMask | shiftKeyMask},
       // Handspring Treo: Rocker Down
       {false, vchrPageDown,
               chrNull,
               commandKeyMask | shiftKeyMask},
       // Handspring Treo: Rocker Push
       {false, 0x1608 /* hsChrJogScan */,
               chrNull,
               commandKeyMask},
       // Palm Treo: Side Button
       {false, hsChrSide,
               keySide,
               willSendUpKeyMask | commandKeyMask},
       // Palm Treo: Volume Up
       {false, hsChrVolumeUp,
               keyVolumeUp,
               willSendUpKeyMask | commandKeyMask},
       // Palm Treo: Volume Down
       {false, hsChrVolumeDown,
               keyVolumeDown,
               willSendUpKeyMask | commandKeyMask},
      };
   UInt16 i, N = sizeof(charArray) / sizeof(charArray[0]);
   UInt16 returnVal = 0;

   if(bReverseLookup)
   {
      for(i = 0; i < N; i++)
      {
         KeyInfoType *cai = &charArray[i];
         if((charArray[i].bCustom == false         ) &&
            ( keyInfo->chr        == cai->chr      ) &&
            ( keyInfo->keyCode    == cai->keyCode  ) &&
            (keyInfo->modifiers   == cai->modifiers))
         {
            keyInfo->bCustom = false;
            returnVal = i;
            break;
         }
      }
      if(i == N)
      {
         keyInfo->bCustom = true;
         returnVal = CUSTOM_POSITION;
      }
   }
   else
   {
      if(keyInfo->bCustom == false) // only modify if false
      {  // only modify if false
         keyInfo->bCustom   = false;
         keyInfo->chr       = charArray[sel].chr;
         keyInfo->keyCode   = charArray[sel].keyCode;
         keyInfo->modifiers = charArray[sel].modifiers;
      }
   }

   return returnVal;
}

// "Sel.:" only ever offers Disabled + the 4 dedicated hardware app-launch
// buttons (a strict subset of ConvertButtonListToKey's own table, entries
// 0 and 10-13) -- same lookup/store idiom, just its own small table since
// the list positions here don't line up with the big Apps:/Back: list.
static UInt16 ConvertSelListToKey(KeyInfoType *keyInfo, UInt16 sel,
                                  Boolean bReverseLookup)
{
   // Target is OS 4.1 on Palm IIIx/IIIxe only -- confirmed live (via the
   // Apps:/Back: "Custom..." key capture dialog) that this ROM's hard
   // app-launch buttons don't duplicate the vchr into keyCode and don't
   // set willSendUpKeyMask (the "Legacy" shape in the full Apps:/Back:
   // table) -- use that shape directly rather than offering both.
   KeyInfoType charArray[] =
      {// Disabled
       {false, chrNull,
               chrNull,
               chrNull},
       // Hard Key #1
       {false, vchrHard1,
               chrNull,
               commandKeyMask},
       // Hard Key #2
       {false, vchrHard2,
               chrNull,
               commandKeyMask},
       // Hard Key #3
       {false, vchrHard3,
               chrNull,
               commandKeyMask},
       // Hard Key #4
       {false, vchrHard4,
               chrNull,
               commandKeyMask},
      };
   UInt16 i, N = sizeof(charArray) / sizeof(charArray[0]);
   UInt16 returnVal = 0; // falls back to "Disabled" if no match found

   if(bReverseLookup)
   {
      for(i = 0; i < N; i++)
      {
         KeyInfoType *cai = &charArray[i];
         if((keyInfo->chr      == cai->chr      ) &&
            (keyInfo->keyCode  == cai->keyCode  ) &&
            (keyInfo->modifiers == cai->modifiers))
         {
            returnVal = i;
            break;
         }
      }
   }
   else
   {
      keyInfo->bCustom   = false;
      keyInfo->chr       = charArray[sel].chr;
      keyInfo->keyCode   = charArray[sel].keyCode;
      keyInfo->modifiers = charArray[sel].modifiers;
   }

   return returnVal;
}

/////////////////////////////
// Custom Button Functions //
/////////////////////////////

static void CustomButtonDoDialog(FormPtr pCurrentForm, CustomData *pCD)
{
   FormPtr pForm;
   UInt16 retVal;
   CustomData cd;

   // set feature for transferring data
   FtrSet(APP_ID, FTR_CUSTOMDATA, (UInt32)&cd);

   // init form
   pForm = FrmInitForm(CustomButtonForm);

   // set new form
   FrmSetActiveForm(pForm);

   // set alarm form event handler
   FrmSetEventHandler(pForm, CustomButtonEventHandler);

   // initialize and draw the form
   CustomButtonInit(pForm, pCD);
   FrmDrawForm(pForm);

   // now wait for alarm form to exit
   retVal = FrmDoDialog(pForm);
   if(retVal == OKButton)
      *pCD = cd;

   // now clean up form
   FrmEraseForm(pForm);
   FrmDeleteForm(pForm);
   FrmSetActiveForm(pCurrentForm);
   FtrUnregister(APP_ID, FTR_CUSTOMDATA);
}

Boolean CustomButtonEventHandler(EventPtr event)
{
   FormPtr pForm = FrmGetActiveForm();
   Boolean handled = true;

   switch(event->eType)
   {
/* Not used
      case frmOpenEvent:
         ActivationInit(pForm);
         FrmDrawForm(pForm);
         break;
*/
      
      case frmUpdateEvent:
         FrmDrawForm(pForm);
         break;

      case ctlSelectEvent:
         switch(event->data.ctlEnter.controlID)
         {
            case CancelButton:
               break;
         }
         handled = false;
         break;

      case keyDownEvent:
         switch(event->data.keyDown.chr)
         {
            case lateWakeupChr:
            case vchrHardPower:
            case vchrPowerOff:
            case vchrAutoOff:
               BEEP;
               CtlHitControl(Id2Ptr(CancelButton));
               handled = false;
               break;

            case vchrResetAutoOff:
               handled = false;
               break;

            // don't allow these keys
            case vchrHardCradle:
            case vchrRockerCenter:
            case vchrHardRockerCenter:
               BEEP;
               break;

            case vchrNavChange:
               // don't allow for 5-way select (legacy)
               if(event->data.keyDown.keyCode ==
                  (navBitSelect | navChangeSelect))
               {
                  BEEP;
                  handled = true;
                  break;
               }
               else if(event->data.keyDown.keyCode == navChangeSelect)
               {
                  handled = true;
                  break;
               }
            // no break if not 5-way select

            default:
#ifdef STANDALONE2
/*
               // removed to allow for characters to activate "McPhling"
               if(TxtGlueCharIsVirtual(event->data.keyDown.modifiers,
                                       event->data.keyDown.chr) == false)
               {
                  BEEP;
                  handled = false;
               }
               else
*/
#endif
               {
                  KeyInfoType kit;

                  // mask out willSendUpKeyMask
                  // event->data.keyDown.modifiers &= ~willSendUpKeyMask;

                  kit.chr       = event->data.keyDown.chr;
                  kit.keyCode   = event->data.keyDown.keyCode;
                  kit.modifiers = event->data.keyDown.modifiers;
                  if(CustomButtonFiltered(&kit) == false)
                  {
                     CustomData *pCD = NULL;

                     CLICK;
                     FtrGet(APP_ID, FTR_CUSTOMDATA, (UInt32 *)&pCD);
                     pCD->kit.bCustom    = true;
                     pCD->kit.chr        = event->data.keyDown.chr;
                     pCD->kit.keyCode    = event->data.keyDown.keyCode;
                     pCD->kit.modifiers  = event->data.keyDown.modifiers;
                     pCD->bCustomDetails = false;
                     CustomButtonChange(&pCD->kit);
                     if(CtlGetValue(Id2Ptr(CustomButtonDetailsCheckbox)))
                     {
                        pCD->bCustomDetails = true;
                        CustomButtonDetails(&pCD->kit);
                     }
                     CtlHitControl(Id2Ptr(OKButton));
                  }
               }
               break;
         }
         break;

      case frmCloseEvent:
         FrmEraseForm(pForm);
         FrmDeleteForm(pForm);
         break;

      default:
         handled = false;
         break;
   }
   
   return handled;
}

static void CustomButtonInit(FormPtr pForm, CustomData *pCD)
{
   UInt32 version;
   VgaRotateModeType rotateMode;

   // set details checkbox
   CtlSetValue(Id2Ptr(CustomButtonDetailsCheckbox), pCD->bCustomDetails);

   // set HandEra VGA modes
   if(_TRGVGAFeaturePresent(&version))
   {
      VgaGetScreenMode(NULL, &rotateMode);
      VgaSetScreenMode(screenMode1To1, rotateMode);
      VgaFormModify(pForm, vgaFormModify160To240);
   }
}

static void CustomButtonDetails(KeyInfoType *pKit)
{
   Char sChr[10];
   Char sKeyCode[10];
   Char sModifiers[10];

   StrPrintF(sChr,       "0x%04x", pKit->chr);
   StrPrintF(sKeyCode,   "0x%04x", pKit->keyCode);
   StrPrintF(sModifiers, "0x%04x", pKit->modifiers);
   FrmCustomAlert(CustomButtonDetailsAlert, sChr, sKeyCode, sModifiers);
}

static void CustomButtonChange(KeyInfoType *pKit)
{
   WChar  chr = pKit->chr;
   UInt16 key = pKit->keyCode;
   UInt16 mod = pKit->modifiers;
   
   // Handspring Treo 600 Rocker Up
   if((chr == vchrRockerUp) &&
      (key == vchrRockerUp) &&
      (mod == (willSendUpKeyMask | commandKeyMask)))
   {
         pKit->chr       = vchrPageUp;
         pKit->keyCode   = vchrPageUp;
         // don't change // pKit->modifiers = mod;
   }

   // Handspring Treo 600 Rocker Down
   if((chr == vchrRockerDown) &&
      (key == vchrRockerDown) &&
      (mod == (willSendUpKeyMask | commandKeyMask)))
   {
         pKit->chr       = vchrPageDown;
         pKit->keyCode   = vchrPageDown;
         // don't change // pKit->modifiers = mod;
   }

   // Handspring Treo 600 Opt Rocker Up // ??? make more general
   if((chr == vchrRockerUp) &&
      (key == vchrRockerUp) &&
      (mod == (willSendUpKeyMask | optionKeyMask | commandKeyMask)))
   {
         pKit->chr       = vchrPageUp;
         pKit->keyCode   = vchrPageUp;
         // don't change // pKit->modifiers = mod;
   }

   // Handspring Treo 600 Opt Rocker Down // ??? make more general
   if((chr == vchrRockerDown) &&
      (key == vchrRockerDown) &&
      (mod == (willSendUpKeyMask | optionKeyMask | commandKeyMask)))
   {
         pKit->chr       = vchrPageDown;
         pKit->keyCode   = vchrPageDown;
         // don't change // pKit->modifiers = mod;
   }
}

static Boolean CustomButtonFiltered(KeyInfoType *pKit)
{
   WChar   chr       = pKit->chr;
   UInt16  key       = pKit->keyCode;
   UInt16  mod       = pKit->modifiers;
   Boolean bFiltered = false;
   
   // Filter out Sony CTRL and ALT keys
   if(mod & commandKeyMask)
   {
      if(chr == vchrHWKeyboardRelease)
      {
         bFiltered = true;
      }
      if(chr == vchrHWKeyboardPush)
      {
         if((key & SONY_HW_KEY_MASK) && (key & SONY_ALL_SHIFT_KEY_MASKS))
         {
            if(!(key & ~(SONY_HW_KEY_MASK | SONY_ALL_SHIFT_KEY_MASKS)))
            {
               bFiltered = true;
            }
         }
      }
   }

   // Filter out Handspring Treo 600 Option Key
   if(chr == hsChrModifierKey)
   {
      if(key == keyLeftAlt)
      {
         // 0x0818 = willSendUpKeyMask | optionKeyMask | commandKeyMask
         // 0x0858 = autoRepeatKeyMask | 0x0818
         // if((mod == 0x0818) || (mod == 0x0858)) {
            bFiltered = true;
         // }
      }
   }

   return bFiltered;
}

///////////////////////
// VFS Specific Code //
///////////////////////

static UInt16 VFSCountApps(VFSInfo *vfsInfo)
{
   Int16 i;
   UInt32 version;
   UInt16 vfsAppCount = 0;
   Err error;
   FileInfoType fileInfo;
   Char *fileName = NULL;
   Char *vfsPath = NULL;
   UInt32 dirIterator;
   FileRef dirRef;
   FileRef fileRef;
   UInt32 type;

   // Check if VFS features are available
   error = FtrGet(sysFileCVFSMgr, vfsFtrIDVersion, &version);
   if(error)
      return vfsAppCount;

   // setup file name and path memory
   fileName = MemPtrNew(sizeof(Char) * (VFS_MAX_PATH + 1));
   if(fileName == NULL)
      return vfsAppCount;

   vfsPath = MemPtrNew(sizeof(Char) * (VFS_MAX_PATH + 1));
   if(vfsPath == NULL)
   {
      MemPtrFree(fileName);
      return vfsAppCount;
   }

   // count applications
   for(i = 0; i < vfsInfo->numVFSVolumes; i++)
   {
      error = VFSFileOpen(vfsInfo->vfsVol[i].volRefNum, vfsInfo->vfsVol[i].path,
                          vfsModeRead, &dirRef);
      if(error == errNone)
      {
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
               StrCat(vfsPath, "/");
               StrCat(vfsPath, fileInfo.nameP);
               error = VFSFileOpen(vfsInfo->vfsVol[i].volRefNum, vfsPath,
                                   vfsModeRead, &fileRef);
               if(error == errNone)
               {
                  UInt32 creator;
               
                  // get type and creator
                  error = VFSFileDBInfo(fileRef, NULL, NULL, NULL, NULL, NULL,
                                        NULL, NULL, NULL, NULL, &type, &creator,
                                        NULL);
                  if(error == errNone && type == sysFileTApplication)
                  {
                     if(IsAppInRam(creator) == false)
                        vfsAppCount++;
                  }
               }

               // close the file
               VFSFileClose(fileRef);
            }
         }
         VFSFileClose(dirRef);
      }
   }

   // clean up
   MemPtrFree(fileName);
   MemPtrFree(vfsPath);

   return vfsAppCount;
}

static void VFSGetDatabaseList(McPhlingAppList **pMcAppList,
                               Char *pVFSPathList, VFSInfo *vfsInfo,
                               UInt16 first)
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
   Char *pIconName;
   Char iconName[dmDBNameLength + 1];
   MemHandle hres = NULL;
   Char vfsPath[VFS_MAX_PATH + 1];
   UInt16 count = first;
   UInt32 localCreator;
   UInt16 vfsPathIndex = 0;

   // Check if VFS features are available
   error = FtrGet(sysFileCVFSMgr, vfsFtrIDVersion, &version);
   if(error)
      return;

   // setup file name memory
   fileName = MemPtrNew(sizeof(Char) * (VFS_MAX_PATH + 1));
   if(fileName == NULL)
      return;

   // get names and icons
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
            StrCat(vfsPath, "/");
            StrCat(vfsPath, fileInfo.nameP);
            error = VFSFileOpen(vfsInfo->vfsVol[i].volRefNum, vfsPath,
                                vfsModeRead, &fileRef);
            if(error == errNone)
            {
               // get icon name
               error = VFSFileDBInfo(fileRef, iconName,
                                     NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                     NULL, &type, &localCreator, NULL);
               if(error == errNone && type == sysFileTApplication)
               {
                  if(IsAppInRam(localCreator) == false)
                  {
                     error = VFSFileDBGetResource(fileRef, ainRsc, ainID,
                                                  &hres);
                     if(error == errNone)
                     {
                        pIconName = (Char *)MemHandleLock(hres);
                        StrCopy((*pMcAppList)[count].name, pIconName);

                        MemHandleUnlock(hres);
                        DmReleaseResource(hres);
                     }
                     else
                     {
                        StrCopy((*pMcAppList)[count].name, iconName);
                     }
                     (*pMcAppList)[count].creator      = localCreator;
                     (*pMcAppList)[count].flags        = IS_A_VFS;
                     (*pMcAppList)[count].vfsVolRefNum =
                        vfsInfo->vfsVol[i].volRefNum;
                     (*pMcAppList)[count].vfsPath =
                        pVFSPathList + (vfsPathIndex * (VFS_MAX_PATH + 1));
                     vfsPathIndex++;
                     StrCopy((*pMcAppList)[count].vfsPath, vfsPath);
                     count++;
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
}
