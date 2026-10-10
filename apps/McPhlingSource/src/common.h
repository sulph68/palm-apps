///////////////////////////////////////////////////////////////////////////////
//                                                                           //
//        Program Name:  common.h                                            //
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
//         Description:  Common code header file for McPhling.               //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#ifndef COMMON_H
#define	COMMON_H 1

///////////////////
// Include Files //
///////////////////

#include <PalmOS.h>
#include <Extensions/ExpansionMgr/VFSMgr.h>

#include "McPhling.h"

// This build targets PalmOS 4.1 only -- no HandEra/Sony/palmOne hardware
// support is needed, and none of those vendor SDKs (nor the two newer
// PalmOSGlue functions below) are available in this toolchain. See
// common.c's LIBCOMMON_13 block for the actual stub bodies -- IMPORTANT:
// they must be real (non-static) functions living in common.c, NOT
// `static` bodies inlined into this header. code1000.c and code2000.c are
// HackMaster "raw code resource" files (STANDALONE_CODE_RESOURCE_ID /
// HACKMASTER_TRAP) where HackMaster jumps directly to the FIRST compiled
// function in the resource -- a `static` stub defined in a header
// included near the top of those files would land before the real entry
// point (MySysHandleEvent / PrefsEventHandler) in the compiled object,
// silently hijacking the entry point (this was a real bug: the McPhling
// Prefs screen came up with the right resource loaded but stayed blank,
// because HackMaster was jumping into a stub instead of PrefsEventHandler).
// Keeping these as ordinary extern functions in the separately-compiled
// common.c avoids that entirely -- code1000.c/code2000.c only get a
// harmless external reference, not inlined code, from including this
// header.
typedef UInt16 VgaScreenModeType;
typedef UInt16 VgaRotateModeType;
#define screenModeScaleToFit 0

Boolean _TRGVGAFeaturePresent(UInt32 *versionP);
FontID  VgaBaseToVgaFont(FontID fontID);
void    VgaGetScreenMode(VgaScreenModeType *pScreenMode, VgaRotateModeType *pRotateMode);
void    VgaSetScreenMode(VgaScreenModeType screenMode, VgaRotateModeType rotateMode);
void    VgaFormModify(FormPtr formP, UInt16 modifyType);
void    VgaWinDrawBitmapExpanded(BitmapPtr bitmapP, Coord x, Coord y);

// WinGlueSetFrameType (PalmOSGlue) is newer than this toolchain's
// libPalmOSGlue.a -- it only ever sets a popup window's cosmetic border
// style, so a no-op just means that one popup keeps its default frame
// instead of the intended menuFrame style.
void WinGlueSetFrameType(WinHandle winH, UInt16 frameType);

// LstGlueGetItemsText (PalmOSGlue) is also newer than this toolchain's
// libPalmOSGlue.a, but IS load-bearing (populates the "keyboard back"
// list from the apps list) -- not dead code, so it gets a real
// implementation in common.c (ListType's itemsText field is directly
// accessible in this SDK via ALLOW_ACCESS_TO_INTERNALS_OF_LISTS, on by
// default per BuildDefaults.h -- exactly what the real glue function
// wraps for later SDKs where ListType became opaque).
Char **LstGlueGetItemsText(const ListType *listP);

// Same reasoning as above for GetCollapseGraffiti's OS5 "collapsible
// Graffiti input area" detection (PalmSource PenInputMgr) and Sony CLIE
// SilkLib detection -- neither SDK is available in this toolchain. The
// pin*/sysFtr*/grfFtr* values below are the real constants (copied from
// the OS5 SDK's PenInputMgr.h/Graffiti.h/SystemMgr.h), so the FtrGet
// calls that use them behave exactly as they would on a real PalmOS 4.1
// device: the feature is correctly reported as absent. sonySysLibNameSilk
// and the two SilkLib stub calls are never reached in that case (SysLibFind
// always fails first) -- their bodies are unreachable no-ops.
#ifndef pinCreator
#define pinCreator                     'pins'
#define pinFtrAPIVersion               1
#define pinAPIVersion1_0               0x01000000
#define pinAPIVersion1_1               0x01103000
#endif
#ifndef sysFtrNumInputAreaFlags
#define sysFtrNumInputAreaFlags        26
#define grfFtrInputAreaFlagCollapsible 0x00000004
#endif
#define sonySysLibNameSilk             "Silk.lib"

Err  SilkLibOpen(UInt16 refNum);
void SilkLibEnableResize(UInt16 refNum);

// Same reasoning again for CollapseSetUserState's OS5 Pen Input Manager
// calls -- only reached when GetCollapseGraffiti() above already reported
// bPS true, which (per the note there) never happens on our target. Real
// enum values from the OS5 SDK's PenInputMgr.h, stub function bodies
// (never invoked).
#ifndef pinInputTriggerEnabled
#define pinInputTriggerEnabled 0
#endif
#ifndef pinInputAreaUser
#define pinInputAreaUser 5
#endif
#ifndef frmDIAPolicyCustom
#define frmDIAPolicyCustom 1
#endif

UInt16 PINGetInputTriggerState(void);
Err    PINSetInputTriggerState(UInt16 state);
Err    PINSetInputAreaState(UInt16 state);
Err    WinSetConstraintsSize(WinHandle winH, Coord minH, Coord prefH,
          Coord maxH, Coord minW, Coord prefW, Coord maxW);
Err    FrmSetDIAPolicyAttr(FormPtr formP, UInt16 diaPolicy);

// This SDK's NotifyMgr.h (PalmOS 4 era) predates these two OS5-only
// notification events -- same literal FourCharCode values as the real
// OS5 SDK's Core/System/NotifyMgr.h, just not present in this toolchain.
// The registrations that use them are OS5-specific graffiti/silk UI
// bookkeeping (see EnableOS5 in common.c) -- inert on our PalmOS 4.1
// target since EnableOS5 is only ever invoked once ROM version is
// confirmed >= 5.
#ifndef sysNotifyEventDequeuedEvent
#define sysNotifyEventDequeuedEvent  'hede'
#endif
#ifndef sysNotifyAppLaunchingEvent
#define sysNotifyAppLaunchingEvent   'hapl'
#endif

/////////////
// Defines //
/////////////

#define BEEP			SndPlaySystemSound(sndInfo)
#define CLICK			SndPlaySystemSound(sndClick)
#define DEBUG_BEEP		BEEP

#ifndef SetBits
#define SetBits(b, len)         (((1U << ((len) - 1)) - 1U +  \
                                  (1U << ((len) - 1))) << (b))
#endif

// pinMaxConstraintSize is set to the max value of a Coord such that it can
// grow as big as the hardware supports
#ifndef pinMaxConstraintSize
#define pinMaxConstraintSize 	SetBits(0, (sizeof(Coord) * 8) - 1)
#endif

/////////////////////////
// Function Prototypes //
/////////////////////////

void GetActivation(McActivation *pMpActivation);
Boolean GetIconName(UInt16 cardNo, LocalID dbID, Char *pIconChar);
void GetMRUApps(McPhlingPrefs *mpPrefs, JumpRefPref *jumpRefs);
void GetPrefsAndNewIgnored(McPhlingPrefs *pMpPrefs);
void *Id2Ptr(UInt16 wID);
void LstSetBestWidth(UInt16 listID);
void DisplayWaitForm(Boolean bVgaExists, VgaScreenModeType *pScreenMode,
                     VgaRotateModeType *pRotateMode, FormPtr *pNewForm);
void RemoveWaitForm(Boolean bVgaExists, VgaScreenModeType screenMode,
                    VgaRotateModeType rotateMode, FormPtr pNewForm,
                    FormPtr pForm);
UInt16 GetROMVer(UInt16 *ROMVerMajor, UInt16 *ROMVerMinor);
void VFSSetup(VFSInfo *vfsInfo);
void EnableOS5(Boolean bEnabled);
Boolean GetCollapseGraffiti(Boolean *pbPS, Boolean *pbSony);
void CollapseSetUserState(FormPtr pForm, Boolean bForce);

#endif
