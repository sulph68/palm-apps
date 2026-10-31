/*
 * WiRSaControl.h - shared declarations for WiRSaControl ("WiRSaCtl").
 */
#ifndef WIRSACONTROL_H
#define WIRSACONTROL_H

#include <PalmOS.h>
#include "WiRSaControlRsc.h"
#include "wirsactl.h"

#define appCreator			'WrSa'
#define appPrefID			0
#define appPrefVersion		3

/* Mode letters in list/string-table order (frmModes LIST, strMode*). */
#define kModeLetters		"MHDSABT"
#define kNumModes			7

/* frmUpdateEvent codes for frmMain: talk to WiRSa again, or (after a
 * mode was started from frmModes) show gPrefs.lastMode as last known. */
#define updRefresh			0x0100
#define updLastKnown		0x0101

typedef struct {
	UInt16	version;
	UInt32	baud;			/* rate WiRSa last answered at */
	UInt8	lastMode;		/* mode letter last started from here, or 0 */
	UInt8	reserved;
	UInt32	menuBaud;		/* WiRSa's menu rate, from its last PONG (0 = unknown) */
	UInt32	preferredBaud;	/* Options > Preferences: rate this Palm asks WiRSa's menu for (9600 = don't ask) */
	UInt32	speedSkip;		/* preferredBaud that failed or gained nothing - don't ask again (0 = none) */
} WCPrefs;

/* STATUS reply */
typedef struct {
	Boolean	valid;
	UInt8	mode;
	UInt8	wifi;			/* 0 not set up, 1 connecting, 2 connected, 3 gave up */
	char	ssid[33];
	Int8	rssi;
	char	ip[16];
	UInt8	sd;				/* 0 unknown, 1 present, 2 none */
	UInt8	ftp;			/* 0 stopped, 1 started */
	UInt8	ftpClients;
	UInt32	uptime;
	UInt8	battery;		/* 0 none/not reported, 1 present */
	UInt16	battMv;
	UInt8	battPct;
	char	sdText[16];		/* "used/total" in SASD's short units, "" = not reported */
	Boolean	diskKnown;		/* WiRSa sent the SASD disk setting */
	UInt8	sasdDisk;		/* the SASD disk setting (0 auto, 1 SD card, 2 virtual) */
} WCStatus;

/* A password field whose real text is kept here, shown as '*'s. */
typedef struct {
	UInt16	fieldID;
	Boolean	show;
	char	real[65];
} WCMasked;

extern WCPrefs gPrefs;
extern WCStatus gStatus;

/* WiRSaControl.c */
void PrefsSave(void);
Boolean SweepOK(void);								/* may WCtlBegin try every rate? */
FieldPtr GetField(UInt16 id);
void SetFieldPtr(UInt16 id, char *text);			/* non-editable; text must stay valid */
void SetFieldEdit(UInt16 id, const char *text);		/* editable; copies */
void GetFieldEdit(UInt16 id, char *out, UInt16 cap);
void BusyShow(const char *msg);
void BusyHide(void);
void ShowResult(UInt8 r);							/* error alert for a WCtl result */
Int16 ModeIndex(UInt8 letter);						/* -1 if unknown */
UInt8 StatusFetch(void);							/* port must be open; fills gStatus */
void MaskRedraw(WCMasked *m);
Boolean MaskKey(WCMasked *m, EventPtr e);			/* true if consumed */
Boolean EditMenu(UInt16 itemID, WCMasked *masks, UInt16 nMasks);

/* Modes.c / Wifi.c / Setup.c */
Boolean ModesHandleEvent(EventPtr e);
Boolean WifiHandleEvent(EventPtr e);
Boolean SetupOpenFromMain(void);					/* fetch settings, then pop up frmSetup */
Boolean SetupHandleEvent(EventPtr e);
Boolean FtpHandleEvent(EventPtr e);
Boolean ApHandleEvent(EventPtr e);
Boolean SlipHandleEvent(EventPtr e);
Boolean TimersHandleEvent(EventPtr e);
Boolean PrefsHandleEvent(EventPtr e);

/* Log.c */
Boolean SysOpenFromMain(void);						/* fetch WiRSa's system page, then pop up frmLog showing it */
Boolean LogOpenFromMain(void);						/* fetch WiRSa's session log, then pop up frmLog */
Boolean LogHandleEvent(EventPtr e);

#endif
