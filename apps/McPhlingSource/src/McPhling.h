///////////////////////////////////////////////////////////////////////////////
//                                                                           //
//        Program Name:  McPhling.h                                          //
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
//         Description:  McPhling Header File                                //
//                                                                           //
///////////////////////////////////////////////////////////////////////////////

#ifndef MCPHLING_H
#define	MCPHLING_H 1
 
///////////////////
// Include Files //
///////////////////

#include <limits.h>

/////////////
// Defines //
/////////////

// The Hack's creator ID -- make sure you reserve one at the PalmOS.com
#define APP_ID              'McPg'
#define APP_ID_VFS          'McPv'
#ifndef STANDALONE
#define APP_TYPE            'HACK'
#else
#define APP_TYPE            sysFileTApplication
#endif
#define DA_APP_TYPE         'DAcc'

// IDs of the different preference types
#define REFPREFS             0
#define IGNOREPREFS          1
#define PREFS                2
#define VFSRUNNINGAPP        3
#define ACTIVATION           5
#define APPLAST              6

#define DEFAULT_NUM_ITEMS    10
#define MAX_NUM_ITEMS        14
#define MIN_NUM_ITEMS        2

#define IS_A_FAV             0x0001
#define IS_IGNORED           0x0002
#define IS_A_VFS             0x0004
#define IS_A_DA              0x0008

#define LIST_PADDING         3

#define VFS_MAX_VOLUMES      2
#define VFS_MAX_PATH         255

// icon cache DB
#define DB_TYPE_CACHE        'Cach'
#define DB_NAME_CACHE        "McPhlingCache"
#define DB_VER_CACHE         "3.0"
#define DB_VER_LEN_CACHE     4
#define ICON_NAME            ainID // this is 1000
#define SMALL_ICON           1001
#define MEDIUM_ICON          2001
#define VFS_PATH             (ICON_NAME + 2000)

// Cmd Bar Bitmaps DB
#define DB_TYPE_CMDBAR      'CMBM'
#define DB_NAME_CMDBAR	    "McPhlingCmdBarBitmaps"
#define CMD_BAR_TEXT        "McPhling"

// features
#define FTR_PAPPLIST         0x115c // 4444
#define FTR_APPCOUNT         0x115d // 4445
#define FTR_DUPLICATE        0x115e // 4446
#define FTR_NEWNUMITEMS      0x115f // 4447
#define FTR_OLDWINRECT       0x1160 // 4448
#define FTR_FIELDNUMITEMS    0x1161 // 4449
#define FTR_VGASCREENMODE    0x1162 // 4450
#define FTR_VGAROTATEMODE    0x1163 // 4451 (not used any more)
#define FTR_DRAWLISTDATA     0x1164 // 4452
#define FTR_PVFSPATHLIST     0x1165 // 4453
#define FTR_ACTIVATION       0x1166 // 4454
#define FTR_ACTTEMP          0x1167 // 4455
#define FTR_CUSTOMDATA       0x1168 // 4456
#define FTR_VFSAPPCOUNT      0x1169 // 4457
#define FTR_NOTIFICATIONSON  0x116A // 4458
#define FTR_BUTTONS          0x116B // 4469
#define FTR_NUMBUTTONS       0x116C // 4470
#define FTR_SILK             0x116D // 4471
#define FTR_NUMSILK          0x116E // 4472
#define FTR_NEXTHOLDTIME     0x116F // 4473
#define FTR_PSCOLLGRAFFITI   0x1170 // 4474
#define FTR_CMDBMPDBID       0x1171 // 4475

// key event to swipe defines
#define KEY_APP_LIST         (SHRT_MAX - 0)
#define KEY_APP_LAST         (SHRT_MAX - 1)
#define KEY_FILTER           (SHRT_MAX - 2)
// "Sel.:" -- while the app-list popup is open, this key launches whichever
// item is currently highlighted (same as tapping it), so a hard button can
// confirm a selection without touching the screen.
#define KEY_APP_SEL           (SHRT_MAX - 3)

// various form IDs
#define HackMasterForm       9000

// special key defines
#define SamsungRecord            0x1552
#define kyoceraCallButton        0x030C
#define kyoceraJogUp             0x030F
#define kyoceraJogDown           0x0310
#define kyoceraOKButton          0x0310
#define kyoceraJogPush           0x0311
#define kyoceraVolumeUp          0x0312
#define kyoceraVolumeDown        0x0313
#define SONY_HW_KEY_MASK         0x8000
#define SONY_FN_KEY_MASK         0x1000
#define SONY_CONTROL_KEY_MASK    0x2000
#define SONY_ALT_KEY_MASK        0x4000
#define SONY_SHIFT_KEY_MASK      0x0400
#define SONY_ALL_SHIFT_KEY_MASKS ( SONY_SHIFT_KEY_MASK   | \
                                   SONY_ALT_KEY_MASK     | \
                                   SONY_CONTROL_KEY_MASK | \
                                   SONY_FN_KEY_MASK        \
                                 )
#define FOSSIL_ROCKER_UP	0x012E
#define FOSSIL_ROCKER_DOWN	0x012F
#define FOSSIL_ROCKER_PUSH	0x0130
#define PALM_SLIDER_CLOSE       (vchrPalmMin + 4)
#define PALM_SLIDER_OPEN        (vchrPalmMin + 5)

// This build targets PalmOS 4.1 only -- HandEra <HandEra/TrgChars.h>,
// palmOne <palmOne/palmOne_68K.h>/<palmOne/68k/Hs.h>, and Sony
// <Sony/System/SonyChars.h>/<Sony/System/SonySystemResources.h> aren't
// available in this toolchain (and aren't needed: no HandEra/Sony/palmOne
// hardware to support). Every use of the constants below is guarded by a
// runtime check for the corresponding vendor feature/device, which will
// correctly never succeed on our target -- so real vendor key-code values
// don't matter here, only that each constant exists and is distinct.
// Where the real value IS documented in the OS5 SDK actually present in
// this toolchain (sdk-5r4) -- true standard virtual chars that just
// predate our SDK-4 baseline, not vendor-proprietary ones -- use that
// real value instead of a sentinel, so the code keeps working correctly
// if this ROM target is ever bumped to a real OS5 device.
#ifndef willSendUpKeyMask
#define willSendUpKeyMask     0x0800   // real (SDK-5 Chars.h), just newer than SDK-4
#endif
#ifndef vchrRockerUp
#define vchrRockerUp          0x0132   // real (SDK-5 Chars.h)
#define vchrRockerDown        0x0133   // real
#define vchrRockerLeft        0x0134   // real
#define vchrRockerRight       0x0135   // real
#define vchrRockerCenter      0x0136   // real
#define vchrHardRockerCenter  0x013D   // real
#endif
#ifndef vchrResetAutoOff
#define vchrResetAutoOff      0x012D   // real
#endif
#ifndef vchrThumbWheelBack
#define vchrThumbWheelBack    0x0131   // real
#endif
#ifndef vchrHard5
#define vchrHard5             0x0214   // real
#endif
#ifndef winDisplayChangedEvent
#define winDisplayChangedEvent 0x4101  // real
#endif

// Genuinely vendor-proprietary below -- sentinel values, high enough
// (0xF100+) to never collide with any real PalmOS virtual key/event code.
#ifndef vchrTrgJogUp
#define vchrTrgJogUp           0xF100
#define vchrTrgJogDown         0xF101
#define vchrTrgJogPush         0xF102
#define vchrTrgAux             0xF103
#endif
#ifndef hsChrVolumeUp
#define hsChrVolumeUp          0xF104
#define hsChrVolumeDown        0xF105
#define hsChrSide              0xF106
#define hsChrModifierKey       0xF107
#define keyVolumeUp            0xF108
#define keyVolumeDown          0xF109
#define keySide                0xF10A
#define keyLeftAlt             0xF10B
#endif
#ifndef vchrJogUp
#define vchrJogUp              0xF10C
#define vchrJogDown            0xF10D
#define vchrJogPush            0xF10E
#define vchrJogBack            0xF10F
#endif
#ifndef vchrSilkResize
#define vchrSilkResize         0xF110
#define vchrSilkClock          0xF111
#endif
#ifndef vchrHWKeyboardPush
#define vchrHWKeyboardPush     0xF112
#define vchrHWKeyboardRelease  0xF113
#endif
#ifndef vchrPopupBrightness
#define vchrPopupBrightness    0xF114
#endif
#ifndef vchrNavChange
#define vchrNavChange          0xF115
#define navChangeUp            0xF116
#define navChangeDown          0xF117
#define navChangeLeft          0xF118
#define navChangeRight         0xF119
#define navChangeSelect        0xF11A
#define navBitUp               0x0001
#define navBitDown             0x0002
#define navBitLeft             0x0004
#define navBitRight            0x0008
#define navBitSelect           0x0010
#endif
#ifndef displayExtentChangedEvent
#define displayExtentChangedEvent 0xF11B
#endif

// HandEra Vga screen-mode constants (VgaScreenModeType/VgaRotateModeType
// themselves are stubbed in common.h) -- same reasoning, never reached
// since bVgaExists is always false on our target.
#ifndef screenMode1To1
#define screenMode1To1         0xF120
#define rotateModeNone         0xF121
#define rotateMode180          0xF122
#define vgaFormModify160To240  0xF123
#endif

//////////////////
// Enumerations //
//////////////////

typedef enum
{
   stMRU = 0,
   stMRUFavTop,
   stMRUFavBottom,
   stAlpha,
   stAlphaFavTop,
   stAlphaFavBottom
} SortType;

typedef enum
{
   alpsNoSelect = 0,
   alpsTopSelect,
   alpsBottomSelect,
   alpsConfigureSelect
} AppListPreSelect;

typedef enum
{
   alpTopLeft = 0,
   alpTopCenter,
   alpTopRight,
   alpCenter,
   alpBottomLeft,
   alpBottomCenter,
   alpBottomRight
} AppListPosition;

typedef enum
{
   sftApps = 0,
   sftMenu,
   sftCalc,
   sftFind
} SwipeFromType;

typedef enum
{
   sttApps = 0,
   sttMenu,
   sttCalc,
   sttFind,
   sttGraffiti,
   sttScreen
} SwipeToType;

typedef enum
{
   vsDeleteMe = 0,
   vsWaiting,
   vsRunning
} VFSState;

////////////////
// Structures //
////////////////

typedef struct
{
   UInt32 creator;
   Coord  width; // no longer used
   UInt16 flags;
} JumpRefPref;

typedef struct
{
   UInt8            prefVersion;
   UInt8            numItems;
   Boolean          bShowIcons      : 1;
   Boolean          bClick          : 1;
   Boolean          bShowCurrentApp : 1;
   Boolean          bVFSSupport     : 1;
   Boolean          bEnabled        : 1;
   Boolean          bSelectLast     : 1;
   SortType         sortType;
   AppListPosition  appListPosition;
} McPhlingPrefs;

typedef struct
{
   UInt16 volRefNum;
   Char   path[VFS_MAX_PATH + 1];
} VFSVolInfo;

typedef struct
{
   VFSState vfsState;
   UInt32   creator;
   UInt16   cardNo;
   LocalID  dbID;
   UInt16   volRefNum;
   Char     path[VFS_MAX_PATH + 1];
} VFSRunningApp;

typedef struct
{
   Int16      numVFSVolumes;
   VFSVolInfo vfsVol[VFS_MAX_VOLUMES];
} VFSInfo;

typedef struct
{
   Boolean bCustom;
   WChar   chr;
   UInt16  keyCode;
   UInt16  modifiers;
} KeyInfoType;

typedef struct
{
   Boolean          bSwipe            : 1;
   SwipeFromType    swipeAppsFrom;
   SwipeToType      swipeAppsTo;
   SwipeFromType    swipeBackFrom;
   SwipeToType      swipeBackTo;
   Boolean          bKeyboard         : 1;
   Boolean          bKeyboardHoldApps : 1;
   Boolean          bKeyboardHoldBack : 1;
   Boolean          bCustomDetails    : 1;
   Boolean          bCommandBar       : 1;
   Boolean          unused03          : 1;  // not used
   Boolean          unused04          : 1;  // not used
   Boolean          unused05          : 1;  // not used
   Boolean          unused06          : 1;  // not used
   Boolean          unused07          : 1;  // not used
   Boolean          unused08          : 1;  // not used
   Boolean          unused09          : 1;  // not used
   Boolean          unused10          : 1;  // not used
   Boolean          unused11          : 1;  // not used
   Boolean          unused12          : 1;  // not used
   Boolean          unused13          : 1;  // not used
   AppListPreSelect PreSelect;              // not used
   KeyInfoType      keyInfoApps;
   KeyInfoType      keyInfoBack;
   KeyInfoType      keyInfoSel;
} McActivation;

////////////////////////////////////////////////////////////
// notes on launching "McPhling" from another application //
////////////////////////////////////////////////////////////

/*
To bring up the apps list do this:

{
   EventType event;
   PointType start;

   start.x = KEY_APP_LIST;
   start.y = alpsNoSelect;

   MemSet(&event, sizeof(EventType), 0);
   event.eType            = penUpEvent;
   event.data.penUp.start = start;
   event.data.penUp.end   = start;
   event.penDown          = false;
   event.tapCount         = 1;
   event.screenX          = start.x;
   event.screenY          = start.y;

   EvtAddEventToQueue(&event);
   EvtProcessSoftKeyStroke(&start, &start); // only do if you want it to be
                                            // compatible with McPhling
                                            // 4.21 or earlier
}

To go back to the last app, do this:

{
   EventType event;
   PointType start;

   start.x = KEY_APP_LAST;
   start.y = 0;

   MemSet(&event, sizeof(EventType), 0);
   event.eType            = penUpEvent;
   event.data.penUp.start = start;
   event.data.penUp.end   = start;
   event.penDown          = false;
   event.tapCount         = 1;
   event.screenX          = start.x;
   event.screenY          = start.y;

   EvtAddEventToQueue(&event);
   EvtProcessSoftKeyStroke(&start, &start); // only do if you want it to be
                                            // compatible with McPhling
                                            // 4.21 or earlier
}

Where:

#define KEY_APP_LIST         (SHRT_MAX - 0)
#define KEY_APP_LAST         (SHRT_MAX - 1)

typedef enum
{
   alpsNoSelect = 0,
   alpsTopSelect,
   alpsBottomSelect
} AppListPreSelect;
*/

#endif
