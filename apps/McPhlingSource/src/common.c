///////////////////////////////////////////////////////////////////////////////
//                                                                           //
//        Program Name:  common.c                                            //
//              Author:  Mike McCollister (� 2000-2009)                      //
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
//         Description:  Common code for McPhling.                           //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

///////////////////
// Include Files //
///////////////////

#include <PalmOS.h>

#include "common.h"
#include "McPhling.rch"

///////////////
// Functions //
///////////////

#ifdef LIBCOMMON_00
void GetActivation(McActivation *pMpActivation)
{
   UInt16 mpActivationSize = 0;
   Int16 prefResult;
   UInt16 prefSize = sizeof(McActivation);

   prefResult = PrefGetAppPreferences(APP_ID, ACTIVATION, NULL,
		                      &mpActivationSize, true);
   if((prefResult == noPreferenceFound) || (mpActivationSize != prefSize))
   {
      pMpActivation->bSwipe                = true;
      pMpActivation->swipeAppsFrom         = sftMenu;
      pMpActivation->swipeAppsTo           = sftApps;
      pMpActivation->swipeBackFrom         = sttApps;
      pMpActivation->swipeBackTo           = sttGraffiti;
      pMpActivation->bKeyboard             = false;
      pMpActivation->bKeyboardHoldApps     = false;
      pMpActivation->bKeyboardHoldBack     = false;
      pMpActivation->bCustomDetails        = false;
      pMpActivation->bCommandBar           = false;
      pMpActivation->unused03              = false;
      pMpActivation->unused04              = false;
      pMpActivation->unused05              = false;
      pMpActivation->unused06              = false;
      pMpActivation->unused07              = false;
      pMpActivation->unused08              = false;
      pMpActivation->unused09              = false;
      pMpActivation->unused10              = false;
      pMpActivation->unused11              = false;
      pMpActivation->unused12              = false;
      pMpActivation->unused13              = false;
      pMpActivation->PreSelect             = alpsBottomSelect;
      pMpActivation->keyInfoApps.bCustom   = false;
      pMpActivation->keyInfoApps.chr       = vchrPageUp;
      pMpActivation->keyInfoApps.keyCode   = 0x0000;
      pMpActivation->keyInfoApps.modifiers = commandKeyMask;
      pMpActivation->keyInfoBack.bCustom   = false;
      pMpActivation->keyInfoBack.chr       = vchrPageDown;
      pMpActivation->keyInfoBack.keyCode   = 0x0000;
      pMpActivation->keyInfoBack.modifiers = commandKeyMask;
      pMpActivation->keyInfoSel.bCustom    = false;
      pMpActivation->keyInfoSel.chr        = chrNull; // disabled by default
      pMpActivation->keyInfoSel.keyCode    = 0x0000;
      pMpActivation->keyInfoSel.modifiers  = 0x0000;
      PrefSetAppPreferences(APP_ID, ACTIVATION, 0, pMpActivation, prefSize,
                            true);
   }
   else
      PrefGetAppPreferences(APP_ID, ACTIVATION, pMpActivation,
		            &mpActivationSize, true);
}
#endif

#ifdef LIBCOMMON_01
Boolean GetIconName(UInt16 cardNo, LocalID dbID, Char *pIconChar)
{
   Char *pChar;
   DmOpenRef dbR  = NULL;
   MemHandle hRes = NULL;
   Boolean bGotName = false;

   if(dbID)
      dbR = DmOpenDatabase(cardNo, dbID, dmModeReadOnly);
   else
   {
      pIconChar[0] = '\0'; // just in case
      return false;
   }

   if(dbR)
      hRes = DmGet1Resource(ainRsc, ainID);

   if(hRes)
   {
      pChar = (Char *)MemHandleLock(hRes);

      // only change to icon name if tAIN is available
      if(pChar)
         StrNCopy(pIconChar, pChar, dmDBNameLength);

      MemHandleUnlock(hRes);
      DmReleaseResource(hRes);

      bGotName = true;
   }
   else
   {  // get the application name
      DmDatabaseInfo(cardNo, dbID, pIconChar, NULL,
                     NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                     NULL, NULL);

      bGotName = true;
   }

   DmCloseDatabase(dbR);

   return bGotName;
}
#endif

#ifdef LIBCOMMON_02
void GetMRUApps(McPhlingPrefs *mpPrefs, JumpRefPref *jumpRefs)
{
   UInt16 prefsSize;
   Int16 prefResult;

   prefsSize = (mpPrefs->numItems + 1) * sizeof(JumpRefPref);

   MemSet(jumpRefs, prefsSize, 0);
   prefResult = PrefGetAppPreferences(APP_ID, REFPREFS, jumpRefs, &prefsSize,
                                      true);
   if((prefResult == noPreferenceFound) ||
      (prefsSize < (mpPrefs->numItems + 1) * sizeof(JumpRefPref)))
   {
         MemSet(jumpRefs, (mpPrefs->numItems + 1) * sizeof(JumpRefPref), 0);
   }
}
#endif

#ifdef LIBCOMMON_03
void GetPrefsAndNewIgnored(McPhlingPrefs *pMpPrefs)
{
   UInt16 mpPrefsSize = 0;
   Int16 prefResult;
   UInt32 *ignoredapps = NULL;
   UInt16 ignPrefsSize = 0;
   UInt16 ignNumApps = 1; // this must match the number of apps below
   UInt16 desiredSize = sizeof(McPhlingPrefs);

   prefResult = PrefGetAppPreferences(APP_ID, PREFS, NULL, &mpPrefsSize, true);
   if((prefResult == noPreferenceFound) || (mpPrefsSize != desiredSize))
   {
      PrefGetAppPreferences(APP_ID, PREFS, pMpPrefs, &mpPrefsSize, true);

      if(pMpPrefs->prefVersion == 0x02)
      {
         pMpPrefs->prefVersion     = 0x03;
         pMpPrefs->appListPosition = alpTopLeft;
      }
      else
      {
         pMpPrefs->prefVersion     = 0x03;
         pMpPrefs->numItems        = DEFAULT_NUM_ITEMS;
         pMpPrefs->bShowIcons      = true;
         pMpPrefs->bClick          = true;
         pMpPrefs->bShowCurrentApp = false;
         pMpPrefs->bVFSSupport     = false;
         pMpPrefs->bEnabled        = false;
         pMpPrefs->bSelectLast     = false; // only OS5+ version
         pMpPrefs->sortType        = stMRU;
         pMpPrefs->appListPosition = alpTopLeft;
         PrefSetAppPreferences(APP_ID, PREFS, 0, pMpPrefs, desiredSize, true);

         desiredSize = ignNumApps * sizeof(UInt32);
         prefResult = PrefGetAppPreferences(APP_ID, IGNOREPREFS, NULL,
                                            &ignPrefsSize, true);
         if((prefResult == noPreferenceFound) || (ignPrefsSize != desiredSize))
         {
            UInt16 i = 0;

            ignPrefsSize = desiredSize;
            ignoredapps = MemPtrNew(ignPrefsSize);
            ignoredapps[i++] = 'sync'; // HotSync
            PrefSetAppPreferences(APP_ID, IGNOREPREFS, 0, ignoredapps,
                                  ignPrefsSize, true);
         }
      }
   }
   else
   {
      PrefGetAppPreferences(APP_ID, PREFS, pMpPrefs, &mpPrefsSize, true);

      // adjust preferences due to change
      if(pMpPrefs->prefVersion == 0x01)
      {
         pMpPrefs->prefVersion     = 0x03;
         pMpPrefs->bShowCurrentApp = false;
         pMpPrefs->bEnabled        = false;
         pMpPrefs->sortType        = stMRU;
         pMpPrefs->appListPosition = alpTopLeft;
         if(pMpPrefs->numItems > MAX_NUM_ITEMS)
            pMpPrefs->numItems = MAX_NUM_ITEMS;
      }
   }

   if(ignoredapps)
      MemPtrFree(ignoredapps);
}
#endif


#ifdef LIBCOMMON_04
void *Id2Ptr(UInt16 wID)
{
   FormPtr pForm = FrmGetActiveForm();

   return(FrmGetObjectPtr(pForm, FrmGetObjectIndex(pForm, wID)));
}
#endif

#ifdef LIBCOMMON_05
void LstSetBestWidth(UInt16 listID)
{
   Int16 i;
   FormPtr pForm = FrmGetActiveForm();
   ListType *pList = Id2Ptr(listID);
   Int16 numItems = LstGetNumberOfItems(pList);
   UInt16 listIndex = FrmGetObjectIndex(pForm, listID);
   UInt16 width, maxWidth = 0;
   RectangleType listBounds;
   FontID oldFont, myStdFont, mySymbolFont;
   UInt32 version;
   Boolean bVgaExists = false;

   // see if vga exists
   if(_TRGVGAFeaturePresent(&version))
      bVgaExists = true;

   if(bVgaExists)
      myStdFont = VgaBaseToVgaFont(stdFont);
   else
      myStdFont = stdFont;
   oldFont = FntSetFont(myStdFont);

   maxWidth = 0;
   for(i = 0; i < numItems; i++)
   {
      width = StrLen(LstGetSelectionText(pList, i));
      width = FntCharsWidth(LstGetSelectionText(pList, i), width);
      if(width > maxWidth)
          maxWidth = width;
   }

   FntSetFont(oldFont);

   // add width to account for up/down arrows
   if(numItems > LstGetVisibleItems(pList))
   {
      Char downArrow = symbolSmallDownArrow;

      if(bVgaExists)
         mySymbolFont = VgaBaseToVgaFont(symbolFont);
      else
         mySymbolFont = symbolFont;

      oldFont = FntSetFont(mySymbolFont);
      maxWidth += FntCharsWidth(&downArrow, 1);
      maxWidth -= LIST_PADDING;

      FntSetFont(oldFont);
   }

   FrmGetObjectBounds(pForm, listIndex, &listBounds);
   listBounds.extent.x = maxWidth + LIST_PADDING;
   FrmSetObjectBounds(pForm, listIndex, &listBounds);
}
#endif

#ifdef LIBCOMMON_06
void DisplayWaitForm(Boolean bVgaExists, VgaScreenModeType *pScreenMode,
                     VgaRotateModeType *pRotateMode, FormPtr *pNewForm)
{
   *pNewForm = FrmInitForm(PleaseWaitForm);

   if(*pNewForm)
   {
      if(bVgaExists)
      {
         VgaGetScreenMode(pScreenMode, pRotateMode);
         VgaSetScreenMode(screenModeScaleToFit, *pRotateMode);
      }
      FrmSetActiveForm(*pNewForm);
      CollapseSetUserState(*pNewForm, false);
      FrmDrawForm(*pNewForm);
   }
}
#endif

#ifdef LIBCOMMON_07
void RemoveWaitForm(Boolean bVgaExists, VgaScreenModeType screenMode,
                    VgaRotateModeType rotateMode, FormPtr pNewForm,
                    FormPtr pForm)
{
   if(pNewForm)
   {
      FrmEraseForm(pNewForm);
      FrmDeleteForm(pNewForm);
      if(bVgaExists)
         VgaSetScreenMode(screenMode, rotateMode);
   }

   if(pForm)
      FrmSetActiveForm(pForm);
}
#endif

#ifdef LIBCOMMON_08
UInt16 GetROMVer(UInt16 *ROMVerMajor, UInt16 *ROMVerMinor)
{
   UInt32 dwROMVer;

   FtrGet(sysFtrCreator, sysFtrNumROMVersion, &dwROMVer);

   if(ROMVerMajor != NULL)
      *ROMVerMajor = (UInt16)sysGetROMVerMajor(dwROMVer);
   if(ROMVerMinor != NULL)
      *ROMVerMinor = (UInt16)sysGetROMVerMinor(dwROMVer);

   return (UInt16)sysGetROMVerMajor(dwROMVer);
}
#endif

#ifdef LIBCOMMON_09
void VFSSetup(VFSInfo *vfsInfo)
{
   Err error;
   UInt32 version;
   Int16 i;
   UInt16 volRefNum;
   UInt32 volIterator = vfsIteratorStart;
   UInt16 bufLen = VFS_MAX_PATH;

   // Assume no VFS Volumes available
   vfsInfo->numVFSVolumes = 0;

   // Check if VFS features are available
   error = FtrGet(sysFileCVFSMgr, vfsFtrIDVersion, &version);
   if(error)
      return;

   // get volume reference number
   for(i = 0; i < VFS_MAX_VOLUMES; i++)
   {
      error = VFSVolumeEnumerate(&volRefNum, &volIterator);
      if(!error)
      {
         vfsInfo->vfsVol[i].volRefNum = volRefNum;
         vfsInfo->numVFSVolumes++;
      }
   }

   // exit if no VFS volumes are available
   if(vfsInfo->numVFSVolumes == 0)
      return;

   // get default directory
   for(i = 0; i < vfsInfo->numVFSVolumes; i++)
   {
      UInt16 lastChar;

      VFSGetDefaultDirectory(vfsInfo->vfsVol[i].volRefNum, ".prc",
                             vfsInfo->vfsVol[i].path, &bufLen);

      // this is done so that it will work with the Tapwave devices
      lastChar = StrLen(vfsInfo->vfsVol[i].path) - 1;
      if(vfsInfo->vfsVol[i].path[lastChar] == '/')
         vfsInfo->vfsVol[i].path[lastChar] = '\0';
   }
}
#endif

#ifdef LIBCOMMON_10
extern Err MySysNotifyEventDequeuedEvent(SysNotifyParamType *np);

void EnableOS5(Boolean bEnabled)
{
   UInt16 cardNo;
   LocalID dbID;
   MemHandle hCode = DmGetResource(sysResTAppCode, 0x0001);
   UInt32 temp;
   Boolean bNotificationsOn = false;

   if(FtrGet(APP_ID, FTR_NOTIFICATIONSON, &temp) != ftrErrNoSuchFeature)
      bNotificationsOn = true;

   SysCurAppDatabase(&cardNo, &dbID);

   if(bEnabled)
   {
      if(bNotificationsOn == false)
      {
         UInt16 numButtons, numAreas;
         const PenBtnInfoType *pPenBtnInfo = EvtGetPenBtnList(&numButtons);
         const SilkscreenAreaType *pSilkscreenArea =
                                       EvtGetSilkscreenAreaList(&numAreas);

         FtrSet(APP_ID, FTR_NOTIFICATIONSON, temp);

         // lock myself
         if(hCode)
         {
            MemHandleLock(hCode);
            DmDatabaseProtect(cardNo, dbID, true);
            DmReleaseResource(hCode);
         }

         // enable notifications
         SysNotifyRegister(cardNo, dbID, sysNotifyEventDequeuedEvent,
                           &MySysNotifyEventDequeuedEvent,
                           sysNotifyNormalPriority, NULL);
         SysNotifyRegister(cardNo, dbID, sysNotifyAppLaunchingEvent,
                           NULL, sysNotifyNormalPriority, NULL);
         SysNotifyRegister(cardNo, dbID, sysNotifySyncStartEvent,
                           NULL, sysNotifyNormalPriority, NULL);
         SysNotifyRegister(cardNo, dbID, sysNotifyDeleteProtectedEvent,
                           NULL, sysNotifyNormalPriority, NULL);
         SysNotifyRegister(cardNo, dbID, sysNotifyMenuCmdBarOpenEvent,
                           NULL, sysNotifyNormalPriority, NULL);
         SysNotifyRegister(cardNo, dbID, APP_ID,
                           NULL, sysNotifyNormalPriority, NULL);

         // save button and silk locations
         FtrSet(APP_ID, FTR_NUMBUTTONS, numButtons);
         FtrPtrNew(APP_ID, FTR_BUTTONS, numButtons * sizeof(PenBtnInfoType),
                   (void **)&pPenBtnInfo);
         FtrSet(APP_ID, FTR_NUMSILK, numAreas);
         FtrPtrNew(APP_ID, FTR_SILK, numAreas * sizeof(SilkscreenAreaType),
                   (void **)&pSilkscreenArea);

         // save collapsible graffiti state
#ifdef STANDALONE
         if(GetCollapseGraffiti(NULL, NULL))
            FtrSet(APP_ID, FTR_PSCOLLGRAFFITI, NULL);
         else
#endif
            FtrUnregister(APP_ID, FTR_PSCOLLGRAFFITI);
      }
   }
   else
   {
      if(bNotificationsOn)
      {
         FtrUnregister(APP_ID, FTR_NOTIFICATIONSON);
         FtrUnregister(APP_ID, FTR_CMDBMPDBID);

         // unlock myself
         if(hCode)
         {
            DmDatabaseProtect(cardNo, dbID, false);
            MemHandleUnlock(hCode);
            DmReleaseResource(hCode);
         }
      }

      // still disable notifications just in case
      SysNotifyUnregister(cardNo, dbID, sysNotifyEventDequeuedEvent,
                          sysNotifyNormalPriority);
      SysNotifyUnregister(cardNo, dbID, sysNotifyAppLaunchingEvent,
                          sysNotifyNormalPriority);
      SysNotifyUnregister(cardNo, dbID, sysNotifySyncStartEvent,
                          sysNotifyNormalPriority);
      SysNotifyUnregister(cardNo, dbID, sysNotifyDeleteProtectedEvent,
                          sysNotifyNormalPriority);
      SysNotifyUnregister(cardNo, dbID, sysNotifyMenuCmdBarOpenEvent,
                          sysNotifyNormalPriority);
      SysNotifyUnregister(cardNo, dbID, APP_ID,
                          sysNotifyNormalPriority);

      // free button and silk locations
      if(FtrGet(APP_ID, FTR_NUMBUTTONS, &temp) == 0)
         FtrUnregister(APP_ID, FTR_NUMBUTTONS);
      if(FtrGet(APP_ID, FTR_BUTTONS, &temp) == 0)
         FtrPtrFree(APP_ID, FTR_BUTTONS);
      if(FtrGet(APP_ID, FTR_NUMSILK, &temp) == 0)
         FtrUnregister(APP_ID, FTR_NUMSILK);
      if(FtrGet(APP_ID, FTR_SILK, &temp) == 0)
         FtrPtrFree(APP_ID, FTR_SILK);
   }
}
#endif

#ifdef LIBCOMMON_11
Boolean GetCollapseGraffiti(Boolean *pbPS, Boolean *pbSony)
{
   UInt32 pinMgrVersion;
   Boolean bPS   = false;
   Boolean bSony = false;
   Err error;
   UInt16 refNum;

   // PalmSource detection
   error = FtrGet(pinCreator, pinFtrAPIVersion, &pinMgrVersion);
   if(error == errNone)
   {
      if(pinMgrVersion == pinAPIVersion1_0)
      {
         // make sure that it works with the Gramin iQue 3600
         bPS = true;
      }
      else if(pinMgrVersion >= pinAPIVersion1_1)
      {
         UInt32 flags;

         error = FtrGet(sysFtrCreator, sysFtrNumInputAreaFlags, &flags);
         if((error == errNone) && (flags & grfFtrInputAreaFlagCollapsible))
         {
            bPS = true;
         }
      }
   }

   // Sony detection
   if(SysLibFind(sonySysLibNameSilk, &refNum) != sysErrLibNotFound)
   {
      if(SilkLibOpen(refNum) == errNone)
      {
         SilkLibEnableResize(refNum);
         bSony = true;
      }
   }

   // return values
   if(pbPS)
      *pbPS = bPS;
   if(pbSony)
      *pbSony = bSony;

   return (bPS || bSony);
}
#endif

#ifdef LIBCOMMON_12
void CollapseSetUserState(FormPtr pForm, Boolean bForce)
{
   WinHandle hWin = WinGetWindowHandle(pForm);
   Boolean bPS;

   GetCollapseGraffiti(&bPS, NULL);

   if(bPS)
   {
      RectangleType frmBounds;
      Coord x, y; 

      FrmGetFormBounds(pForm, &frmBounds);
      y = frmBounds.extent.y;
      x = frmBounds.extent.x;

      if(bForce || (PINGetInputTriggerState() == pinInputTriggerEnabled))
      {
         WinSetConstraintsSize(hWin, y, pinMaxConstraintSize,
                               pinMaxConstraintSize, x, x,
                               pinMaxConstraintSize);
         FrmSetDIAPolicyAttr(pForm, frmDIAPolicyCustom);
         PINSetInputTriggerState(pinInputTriggerEnabled);
         PINSetInputAreaState(pinInputAreaUser);
      }
   }
}
#endif

// PalmOS 4.1-only build stand-ins for HandEra/Sony/palmOne vendor SDK
// calls and two newer PalmOSGlue functions -- see common.h for the full
// explanation of why these MUST be real functions here (not static
// bodies in common.h). Every one of these except LstGlueGetItemsText is
// guarded at every call site by a runtime feature-presence check that
// always correctly reports "absent" on our target, so the bodies are
// unreachable no-ops; LstGlueGetItemsText is load-bearing and has a real
// implementation.
#ifdef LIBCOMMON_13
Boolean _TRGVGAFeaturePresent(UInt32 *versionP)
{
   #pragma unused (versionP)
   return false;
}

FontID VgaBaseToVgaFont(FontID fontID)
{
   return fontID;
}

void VgaGetScreenMode(VgaScreenModeType *pScreenMode, VgaRotateModeType *pRotateMode)
{
   #pragma unused (pScreenMode, pRotateMode)
}

void VgaSetScreenMode(VgaScreenModeType screenMode, VgaRotateModeType rotateMode)
{
   #pragma unused (screenMode, rotateMode)
}

void VgaFormModify(FormPtr formP, UInt16 modifyType)
{
   #pragma unused (formP, modifyType)
}

void VgaWinDrawBitmapExpanded(BitmapPtr bitmapP, Coord x, Coord y)
{
   #pragma unused (bitmapP, x, y)
}

void WinGlueSetFrameType(WinHandle winH, UInt16 frameType)
{
   #pragma unused (winH, frameType)
}

Char **LstGlueGetItemsText(const ListType *listP)
{
   return listP->itemsText;
}

Err SilkLibOpen(UInt16 refNum)
{
   #pragma unused (refNum)
   return sysErrParamErr;
}

void SilkLibEnableResize(UInt16 refNum)
{
   #pragma unused (refNum)
}

UInt16 PINGetInputTriggerState(void)
{
   return pinInputTriggerEnabled;
}

Err PINSetInputTriggerState(UInt16 state)
{
   #pragma unused (state)
   return errNone;
}

Err PINSetInputAreaState(UInt16 state)
{
   #pragma unused (state)
   return errNone;
}

Err WinSetConstraintsSize(WinHandle winH, Coord minH, Coord prefH,
   Coord maxH, Coord minW, Coord prefW, Coord maxW)
{
   #pragma unused (winH, minH, prefH, maxH, minW, prefW, maxW)
   return errNone;
}

Err FrmSetDIAPolicyAttr(FormPtr formP, UInt16 diaPolicy)
{
   #pragma unused (formP, diaPolicy)
   return errNone;
}
#endif
