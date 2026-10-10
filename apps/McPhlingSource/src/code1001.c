///////////////////////////////////////////////////////////////////////////////
//                                                                           //
//        Program Name:  code1001.c                                          //
//              Author:  Mike McCollister (© 2000-2009)                      //
//     Original Author:  Mitch Blevins <mblevin@debian.org> (© 1997-2000)    //
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
//         Description:  AppLaunch trap handler for McPhling.                //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

///////////////////
// Include Files //
///////////////////

#include <PalmOS.h>
#include <Extensions/ExpansionMgr/VFSMgr.h>

#include <Standalone.h>

#include "common.h"
#include "McPhling.h"
#include "McPhling.rch"

/////////////////////////
// Function Prototypes //
/////////////////////////

void UpdateJumpRef(UInt16 cardNo, LocalID dbID);
static void CleanupVFSStuff(void);
static void UpdateCache(Boolean bShowIcons, UInt16 cardNo, LocalID dbID,
                        UInt32 creator);
void UpdateVFSCache(UInt16 cardNo, LocalID dbID,
                    SysAppLaunchCmdCardType *pCardLaunchCmd);

///////////////
// Functions //
///////////////

#ifndef STANDALONE
#define RESOURCE_ID 1001
HACKMASTER_TRAP(sysTrapSysAppLaunch);
STANDALONE_CODE_RESOURCE_ID(RESOURCE_ID);

Err MySysAppLaunch(UInt16 cardNo, LocalID dbID, UInt16 launchFlags, UInt16 cmd,
                   MemPtr cmdPBP, UInt32* resultP)
{
   // This is the code that will be called whenever ANY app calls
   // EvtSysAppLaunch and this hack is enabled in HackMaster.
   // HackMaster is not smart about the content of this file; it will simply
   // jump to the first executable statement in this file. If you need helper
   // functions, prototype them above, and define them AFTER the trap patch.

   UInt32 temp;
   Err (*oldTrap)(UInt16, LocalID, UInt16, UInt16, MemPtr, UInt32*);

   FtrGet(APP_ID, RESOURCE_ID, &temp);
   oldTrap = (Err(*)(UInt16, LocalID, UInt16, UInt16, MemPtr, UInt32*))temp;

   if(cmd == sysAppLaunchCmdCardLaunch)
      UpdateVFSCache(cardNo, dbID, (SysAppLaunchCmdCardType *)cmdPBP);
   if(launchFlags & sysAppLaunchFlagUIApp)
      UpdateJumpRef(cardNo, dbID);

   return oldTrap(cardNo, dbID, launchFlags, cmd, cmdPBP, resultP);
}
#endif

void UpdateJumpRef(UInt16 cardNo, LocalID dbID)
{
   UInt32 dbType;
   UInt32 *ignoredApps = NULL;
   UInt32 dbCreator;
   UInt16 prefsSize, numIgnored;
   JumpRefPref *jumpRefs = NULL;
   JumpRefPref currentAppRef;
   Int16 i, j;
   McPhlingPrefs mpPrefs;
   Int16 prefResult;
   Boolean bWasInList = false;
   UInt16 attr;

   // Find the creatorID, type and name
   if(DmDatabaseInfo(cardNo, dbID, NULL, &attr, NULL, NULL, NULL,
                     NULL, NULL, NULL, NULL, &dbType, &dbCreator))
      goto allDone;

   // Ignore it if it is not an application
   if(dbType != sysFileTApplication) 
      goto allDone;

   // Ignore if it is hidden
   if(attr & dmHdrAttrHidden)
      goto allDone;

   // skip if "McPhlingVFS" applet
   if(dbCreator == APP_ID_VFS)
      goto allDone;

   // save preference so that McPhling knows which application was run last
   // regardless of whether it is ignored or not
   PrefSetAppPreferences(APP_ID, APPLAST, 0, &dbCreator, sizeof(dbCreator),
                         true);

   // cleanup any VFS stuff such as "McPhlingVFS"
   CleanupVFSStuff();

   // get preferences
   GetPrefsAndNewIgnored(&mpPrefs);

   // initialize currentAppRef
   MemSet(&currentAppRef, sizeof(JumpRefPref), 0);
   currentAppRef.creator = dbCreator;

   // Fetch the ignored apps
   prefsSize = 0;
   prefResult = PrefGetAppPreferences(APP_ID, IGNOREPREFS, NULL,
                                      &prefsSize, true);
   if((prefResult == noPreferenceFound) || (prefsSize == 0)) 
      goto doneIgnoring;

   // Create large data structures
   ignoredApps = MemPtrNew(prefsSize);
   if(!ignoredApps)
      goto doneIgnoring;

   prefResult = PrefGetAppPreferences(APP_ID, IGNOREPREFS, ignoredApps,
                                      &prefsSize, true);

   // Check if the currently launched app is to be ignored
   numIgnored = prefsSize / sizeof(UInt32);
   for(i = 0; i < (Int16)numIgnored; i++)
   {
      if(ignoredApps[i] == dbCreator)
      {
         currentAppRef.flags = IS_IGNORED;
         if(mpPrefs.bShowCurrentApp)
            goto allDone;
         else
            goto doneIgnoring;
      }
   }

doneIgnoring:

   // update icon cache
   if((currentAppRef.flags & IS_IGNORED) == 0)
      UpdateCache(mpPrefs.bShowIcons, cardNo, dbID, dbCreator);

   // Create large data structures
   jumpRefs = MemPtrNew((MAX_NUM_ITEMS + 1) * sizeof(JumpRefPref));
   
   // Get the list of application
   GetMRUApps(&mpPrefs, jumpRefs);

   // Find the last cached jumpref
   for(i = 0; i <= mpPrefs.numItems; i++)
   {
      if(!jumpRefs[i].creator)
      {
         break;  // current app not found in the list, exit the loop
      }
      if(jumpRefs[i].creator == currentAppRef.creator)
      {
         // Already on the list
         bWasInList = true;
         currentAppRef = jumpRefs[i];
         break;  // found current app in the list, exit the loop
      }
   }

   // Make sure a favorite is not pushed off the list
   if(i > mpPrefs.numItems)
   {
      i = mpPrefs.numItems;
      for(j = mpPrefs.numItems; j >= 0; j--)
      {
         if(jumpRefs[j].flags & IS_A_FAV)
         {
            i = j - 1;
         }
         else
            break;
      }
   }

   // don't insert if no room (all favorites)
   //if(i < 0 && !bWasInList) {
   if(i < 0)
   {
      goto allDone;
   }

   if((jumpRefs[0].flags & IS_IGNORED) == 0)
   {  // if not ignored move up
      for(j = i; j > 0; j--)
      {
         jumpRefs[j] = jumpRefs[j - 1];
      }
   }
   else if(bWasInList && (i != 0))
   { // if ignored and was in list move down
      for(j = i; j < mpPrefs.numItems; j++)
      {
         jumpRefs[j] = jumpRefs[j + 1];
      }
      jumpRefs[j].creator = 0;
      jumpRefs[j].flags   = 0x00;
   }

   jumpRefs[0] = currentAppRef;

   // Save new preferences
   PrefSetAppPreferences(APP_ID, REFPREFS, 0, jumpRefs,
                         (mpPrefs.numItems + 1) * sizeof(JumpRefPref), true);

allDone:

   // clean up memory
   if(ignoredApps)
      MemPtrFree(ignoredApps);
   if(jumpRefs)
      MemPtrFree(jumpRefs);
}

static void CleanupVFSStuff(void)
{
   VFSRunningApp *pVra = NULL;
   UInt16 vraSize;
   Int16 prefResult;

   pVra = MemPtrNew(sizeof(VFSRunningApp));
   if(pVra == NULL)
      return;

   vraSize = 0;
   prefResult = PrefGetAppPreferences(APP_ID, VFSRUNNINGAPP, pVra, &vraSize,
                                      true);
   if((prefResult != noPreferenceFound) && (vraSize == sizeof(*pVra)))
   {
      PrefGetAppPreferences(APP_ID, VFSRUNNINGAPP, pVra, &vraSize, true);

      if((pVra->vfsState == vsDeleteMe))
      {
         UInt16 cardNo;
         LocalID dbID;
         DmSearchStateType searchState;
         Err err;

         // delete "McPhlingVFS"
         err = DmGetNextDatabaseByTypeCreator(true, &searchState,
                                              sysFileTApplication, APP_ID_VFS,
                                              true, &cardNo, &dbID);
         if(err == errNone)
            DmDeleteDatabase(cardNo, dbID);

         // delete preference
         PrefSetAppPreferences(APP_ID, VFSRUNNINGAPP, 0, NULL, 0, true);
      }
   }

   if(pVra)
      MemPtrFree(pVra);
}

static void UpdateCache(Boolean bShowIcons, UInt16 cardNo, LocalID dbID,
                        UInt32 creator)
{
   DmOpenRef dbCache;
   DmOpenRef dbApp;
   MemHandle hIcon, hIcon2, hIconName, hVersion;
   BitmapPtr pIcon, pIcon2;
   UInt32 iconHandleSize;
   Char *pIconName;
   Char *iconName = NULL, *pVersion = NULL;

   // create icon name array
   iconName = MemPtrNew(sizeof(Char) * (dmDBNameLength + 1));
   if(iconName == NULL)
      return;

   // open cache database
   dbCache = DmOpenDatabaseByTypeCreator(DB_TYPE_CACHE, APP_ID,
                                         dmModeReadWrite);
   if(dbCache == NULL)
   {
      if(DmCreateDatabase(0, DB_NAME_CACHE, APP_ID, DB_TYPE_CACHE, true) == 0)
      {
         dbCache = DmOpenDatabaseByTypeCreator(DB_TYPE_CACHE, APP_ID,
                                               dmModeReadWrite);

         // put version into resource
         hVersion = DmNewResource(dbCache, verRsc, appVersionID,
                                  DB_VER_LEN_CACHE);
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
      }
   }

   // remove resource so that they can be replaced
   if(dbCache)
   {
      UInt16 index;
      MemHandle hRes;

      hRes = DmGet1Resource(creator, SMALL_ICON);
      if(hRes)
      {
         index = DmFindResource(dbCache, SMALL_ICON, creator, hRes);
         DmRemoveResource(dbCache, index);
      }

      hRes = DmGet1Resource(creator, MEDIUM_ICON);
      if(hRes)
      {
         index = DmFindResource(dbCache, MEDIUM_ICON, creator, hRes);
         DmRemoveResource(dbCache, index);
      }

      hRes = DmGet1Resource(creator, ICON_NAME);
      if(hRes)
      {
         index = DmFindResource(dbCache, ICON_NAME, creator, hRes);
         DmRemoveResource(dbCache, index);
      }
   }

   // open application database
   dbApp = DmOpenDatabase(cardNo, dbID, dmModeReadOnly);

   if(bShowIcons)
   {
      // get small icon
      hIcon = DmGet1Resource(iconType, SMALL_ICON);
      if(hIcon)
      {
         pIcon = MemHandleLock(hIcon);
         iconHandleSize = MemHandleSize(hIcon);

         hIcon2 = DmNewResource(dbCache, creator, SMALL_ICON, iconHandleSize);
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

         hIcon2 = DmNewResource(dbCache, creator, MEDIUM_ICON, iconHandleSize);
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
   hIconName = DmNewResource(dbCache, creator, ICON_NAME, dmDBNameLength + 1);
   pIconName = MemHandleLock(hIconName);
   GetIconName(cardNo, dbID, iconName);
   DmWrite(pIconName, 0, iconName, dmDBNameLength + 1);
   MemPtrFree(iconName);

   // clean up
   MemHandleUnlock(hIconName);
   DmReleaseResource(hIconName);

   // close cache database
   DmCloseDatabase(dbCache);
   DmCloseDatabase(dbApp);
}

void UpdateVFSCache(UInt16 cardNo, LocalID dbID,
                    SysAppLaunchCmdCardType *pCardLaunchCmd)
{
   DmOpenRef dbCache;
   MemHandle hVFSName, hVersion;
   Char *pVFSName = NULL;
   Char *pVersion = NULL;
   UInt32 type;
   UInt32 creator;

   // don't do anything if pCardLaunchCmd is NULL
   if(pCardLaunchCmd == NULL)
      return;

   // only valid initial error type allowed
   if(pCardLaunchCmd->err != expErrUnsupportedOperation)
      return;

   // Find the type and creator
   if(DmDatabaseInfo(cardNo, dbID, NULL, NULL, NULL, NULL, NULL,
                     NULL, NULL, NULL, NULL, &type, &creator))
      return;

   // Ignore it if it is not an application
   if(type != sysFileTApplication)
      return;

   // open cache database
   dbCache = DmOpenDatabaseByTypeCreator(DB_TYPE_CACHE, APP_ID,
                                         dmModeReadWrite);
   if(dbCache == NULL)
   {
      if(DmCreateDatabase(0, DB_NAME_CACHE, APP_ID, DB_TYPE_CACHE, true) == 0)
      {
         dbCache = DmOpenDatabaseByTypeCreator(DB_TYPE_CACHE, APP_ID,
                                               dmModeReadWrite);

         // put version into resource
         hVersion = DmNewResource(dbCache, verRsc, appVersionID,
                                  DB_VER_LEN_CACHE);
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
      }
   }

   // remove resource so that they can be replaced
   if(dbCache)
   {
      UInt16 index;
      MemHandle hRes;

      hRes = DmGet1Resource(creator, VFS_PATH);
      if(hRes)
      {
         index = DmFindResource(dbCache, VFS_PATH, creator, hRes);
         DmRemoveResource(dbCache, index);
      }
   }

   // write VFS Path
   hVFSName = DmNewResource(dbCache, creator, VFS_PATH, VFS_MAX_PATH + 1);
   pVFSName = MemHandleLock(hVFSName);
   // for some reason the address to pCardLaunchCmd->path is
   // in the stack range
   DmWrite(pVFSName, 0, pCardLaunchCmd->path, VFS_MAX_PATH + 1);

   // clean up
   MemHandleUnlock(hVFSName);
   DmReleaseResource(hVFSName);

   // close cache database
   if(dbCache)
      DmCloseDatabase(dbCache);
}
