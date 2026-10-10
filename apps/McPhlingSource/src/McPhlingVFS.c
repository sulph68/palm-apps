///////////////////////////////////////////////////////////////////////////////
//                                                                           //
//        Program Name:  McPhlingVFS.c                                       //
//              Author:  Mike McCollister (© 2000-2009)                      //
//            Language:  PRC-Tools 2.3                                       //
//                       Palm OS 5 SDK R3                                    //
//                       PilRC 3.2                                           //
//                       HandEra Header Files from SDK 1.04                  // 
//                       palmOne Header Files from SDK version 5.1           //
//                       Sony Header Files from SDK 5.0 version 1.2          //
//               Notes:  Distributed under the GNU GPL.                      //
//         Description:  VFR Mini-App for using with "McPhling".             //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

///////////////////
// Include Files //
///////////////////

#include <PalmOS.h>
#include <Extensions/ExpansionMgr/VFSMgr.h>

#include "McPhling.h"

///////////////////
// Main Function //
///////////////////

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP __attribute__ ((unused)),
                 UInt16 launchFlags __attribute__ ((unused)))
{
   Err err;
   VFSRunningApp vra;
   UInt16 size;
   Int16 prefResult;

   if(cmd == sysAppLaunchCmdNormalLaunch)
   {
      SysAppLaunchCmdCardType sysAppLaunchCmdCardType;
      UInt32 result;
      
      size = 0;
      prefResult = PrefGetAppPreferences(APP_ID, VFSRUNNINGAPP, &vra, &size,
                                         true);
      if((prefResult != noPreferenceFound) && (size == sizeof(vra)))
      {
         PrefGetAppPreferences(APP_ID, VFSRUNNINGAPP, &vra, &size, true); 
         // get ready to launch application
         sysAppLaunchCmdCardType.err = expErrUnsupportedOperation;
         sysAppLaunchCmdCardType.volRefNum = vra.volRefNum;
         sysAppLaunchCmdCardType.path = vra.path;
         sysAppLaunchCmdCardType.startFlags = sysAppLaunchStartFlagNoUISwitch;

         // set vfsState
         vra.vfsState = vsRunning;
         PrefSetAppPreferences(APP_ID, VFSRUNNINGAPP, 0, &vra, sizeof(vra),
                               true);

         // now try to launch for memory-card aware applications
         err = SysAppLaunch(vra.cardNo, vra.dbID,
                            sysAppLaunchFlagNewGlobals |
                            sysAppLaunchFlagUIApp |
                            sysAppLaunchFlagNewStack,
                            sysAppLaunchCmdCardLaunch,
                            &sysAppLaunchCmdCardType, &result);

         // launch a non-memory card aware application
         if(sysAppLaunchCmdCardType.err != errNone)
         {
            err = SysAppLaunch(vra.cardNo, vra.dbID,
                               sysAppLaunchFlagNewGlobals |
                               sysAppLaunchFlagUIApp |
                               sysAppLaunchFlagNewStack,
                               sysAppLaunchCmdNormalLaunch,
                               0, &result);
         }

         // delete application
         DmDeleteDatabase(vra.cardNo, vra.dbID);

         // now check if state has changed
         size = 0;
         prefResult = PrefGetAppPreferences(APP_ID, VFSRUNNINGAPP, &vra, &size,
                                            true);
         if((prefResult != noPreferenceFound) && (size == sizeof(vra)))
         {
            PrefGetAppPreferences(APP_ID, VFSRUNNINGAPP, &vra, &size, true);
            if(vra.vfsState == vsRunning)
            {
               vra.vfsState = vsDeleteMe;
               PrefSetAppPreferences(APP_ID, VFSRUNNINGAPP, 0, &vra,
                                     sizeof(vra), true);
            }
         }
      }
   }
   else if(cmd == sysAppLaunchCmdSystemReset)
   {
      UInt16 cardNo;
      LocalID dbID;
      DmSearchStateType searchState;

      // delete anything that needs to be deleted

      size = 0;
      prefResult = PrefGetAppPreferences(APP_ID, VFSRUNNINGAPP, &vra, &size,
                                         true);
      if((prefResult != noPreferenceFound) && (size == sizeof(vra)))
      {
         PrefGetAppPreferences(APP_ID, VFSRUNNINGAPP, &vra, &size, true);

         // delete application
         err = DmGetNextDatabaseByTypeCreator(true, &searchState,
                                              sysFileTApplication, vra.creator,
                                              true, &cardNo, &dbID);
         if(err == errNone)
            DmDeleteDatabase(cardNo, dbID);

         // change preference to make sure that "McPhlingVFS" gets deleted
         vra.vfsState = vsDeleteMe;
         PrefSetAppPreferences(APP_ID, VFSRUNNINGAPP, 0, &vra, sizeof(vra),
                               true);
      }
   }

   return 0;
}
