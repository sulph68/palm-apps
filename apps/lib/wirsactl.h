/*
 * wirsactl.h - Palm side of the WiRSa control link (spec: apps/WiRSaControl.md).
 *
 * Every frame on the line is 0x16 + a relayproto frame (relayproto.h), sent
 * in plaintext except the password commands, which run after WCtlSecure().
 * Requests are 0x40-0x4F; each reply is the request type + 0x20, or
 * wctlTypeErr (code u8, message str).
 *
 * Usage: WCtlBegin() before talking (opens the port at gWCtlBaud, or finds
 * WiRSa if that fails), WCtlEnd() after - the port is never held between
 * operations, so the SASD driver, HotSync and the relay apps can open it.
 * Main segment only (blocking reads; see relayproto.h).
 */
#ifndef WIRSACTL_H
#define WIRSACTL_H

#include <PalmOS.h>

/* Segment for everything in wirsactl.c (empty = main). The tag must be on
 * every prototype here - callers pick near vs far calls from it. */
#ifndef WCTL_SECTION
#define WCTL_SECTION
#endif

#define wctlLeadByte		0x16

#define wctlTypePing		0x40
#define wctlTypeStatus		0x41
#define wctlTypeWifiScan	0x42
#define wctlTypeWifiSet		0x43	/* encrypted */
#define wctlTypeCfgGet		0x44
#define wctlTypeCfgSet		0x45
#define wctlTypeSecrets		0x46	/* encrypted */
#define wctlTypeFtp			0x47
#define wctlTypeEnter		0x48
#define wctlTypeExit		0x49
#define wctlTypeReboot		0x4A
#define wctlTypeFactory		0x4B
#define wctlTypeSys			0x4E	/* system page text, a page at a time: op u8 (unused), offset u32 - same reply as Log */
#define wctlTypeLog			0x4D	/* session log, a page at a time: op u8 (0 read, 1 clear), offset u32 */
#define wctlTypeSpeed		0x4C	/* move WiRSa's menu to a faster rate (firmware with wctlFlagSpeed) */
#define wctlTypeErr			0x5F
#define wctlReplyOf(t)		((UInt8) ((t) + 0x20))

/* wctlTypeErr codes */
#define wctlErrNoWifi		1
#define wctlErrNoSd			2
#define wctlErrNotBuilt		3
#define wctlErrNotAMode		4
#define wctlErrBusy			5
#define wctlErrBadRequest	6
#define wctlErrFailed		7

/* PONG flags */
#define wctlFlagScreenless	0x0001
#define wctlFlagFtp			0x0002
#define wctlFlagHotSync		0x0004
#define wctlFlagSasd		0x0008
#define wctlFlagSlip		0x0010
#define wctlFlagBitwarden	0x0020
#define wctlFlagCalDav		0x0040
#define wctlFlagSpeed		0x0080	/* SPEED supported: menu starts at 9600 after boot */
#define wctlFlagMesh		0x0100	/* Mesh Relay (Meshtastic) built in */

/* Mode letters (PONG/STATUS mode, ENTER body): 'M' main menu (or any menu
 * screen), 'H' HotSync, 'D' SASD, 'S' SLIP, 'A' CalDAV, 'B' Bitwarden, 'T' Mesh Relay (Meshtastic). */

/* Results of every WCtl call */
#define wctlOk				0
#define wctlNoReply			1	/* nothing usable came back */
#define wctlPortBusy		2	/* serial port held by something else */
#define wctlErrReply		3	/* WiRSa answered wctlTypeErr - see gWCtlErrCode/Msg */
#define wctlBadReply		4	/* reply too short or wrong */
#define wctlSecFailed		5	/* key exchange failed */
#define wctlModeNotUp		6	/* WCtlEnsureMode: WiRSa answered, but the mode never did */

typedef struct {
	UInt8	proto;
	char	fw[24];
	UInt16	flags;
	UInt8	mode;
	UInt32	menuBaud;
} WCtlInfo;

/* Called with the rate before every PING (0 = none): lets an app show what is being tried. */
extern void (*gWCtlProgress)(UInt32 baud);
extern UInt32 gWCtlBaud;			/* rate WiRSa last answered at */
extern UInt32 gWCtlMenuBaud;		/* WiRSa's menu rate, from its last PONG (0 = unknown) */
extern WCtlInfo gWCtlInfo;			/* from the last successful PING */
extern UInt8 gWCtlErrCode;
extern char gWCtlErrMsg[48];

/* Open the port and make sure WiRSa answers: PING at gWCtlBaud, then at
 * 9600 (the modes' rate), then at gWCtlMenuBaud, then - only if sweep -
 * every rate (WCtlFind). A PING at the wrong rate reaches a running mode as
 * junk, so callers sweep only when WiRSa should be in its menu (where the
 * burst guard drops junk) or its menu rate isn't known yet. Leaves the port
 * open on wctlOk only. */
UInt8 WCtlBegin(Boolean sweep) WCTL_SECTION;
/* WCtlBegin with the full sweep (WCtlFind) - Options > Find WiRSa. */
UInt8 WCtlBeginFind(void) WCTL_SECTION;
void WCtlEnd(void) WCTL_SECTION;
/* Try every rate (9600 first, then 115200 down to 14400) until WiRSa answers
 * a PING; port must already be open. Only on the user's request ("Find
 * WiRSa"): it sends junk to a WiRSa that sits in a mode. */
UInt8 WCtlFind(void) WCTL_SECTION;
/* Switch the open port (and gWCtlBaud) to a new rate. */
void WCtlSetBaud(UInt32 baud) WCTL_SECTION;

/* One request/reply. Frames of other types (stale replies) are skipped
 * until the reply arrives or timeoutTicks pass. */
UInt8 WCtlRequest(UInt8 type, const UInt8 *body, UInt16 bodyLen,
	UInt8 *reply, UInt16 replyCap, UInt16 *replyLen, UInt32 timeoutTicks) WCTL_SECTION;

/* Key exchange for the password commands; WCtlUnsecure afterwards (WiRSa
 * drops its side after each encrypted command). */
UInt8 WCtlSecure(void) WCTL_SECTION;
void WCtlUnsecure(void) WCTL_SECTION;

/* SPEED: ask WiRSa to move its menu to want for this WiRSa power-on (any
 * standard rate; WiRSa's maximum only caps its modes). Port open, after a PING with WiRSa in its menu. On
 * wctlOk the port runs at the new rate, WiRSa answered a PING there, and
 * *got is that rate. If that PING fails, the port goes back to 9600 and
 * PINGs once there - WiRSa still listens at 9600 and moves its menu back -
 * and wctlNoReply comes back. */
UInt8 WCtlSetSpeed(UInt32 want, UInt32 *got) WCTL_SECTION;

/* Relay apps' "Screenless Mode" (CalDAV Sync, Bitwarden). menuBaud: the
 * app's Baud setting, which matches WiRSa's menu rate.
 * WCtlEnsureMode: make WiRSa run mode ('A' CalDAV, 'B' Bitwarden) - leave
 * any other mode, start this one, then wait (up to 20 s) until the mode
 * itself answers. Port closed afterwards. wctlErrReply = WiRSa refused (the
 * caller shows gWCtlErrMsg and stops); wctlModeNotUp = WiRSa answered but
 * the mode never came up (the caller says so, skips the session, and still
 * calls WCtlLeaveMode); wctlNoReply / wctlPortBusy = WiRSa couldn't be
 * reached at any rate (the caller says so and skips the session - with
 * Screenless Mode on, syncing blind would only hang).
 * ENTER carries a flags byte that current firmware ignores: WiRSa's own
 * Exit Timers bring it back to its menu. WCtlLeaveMode stays as the fallback:
 * WCtlLeaveMode: after the session's BYE, send WiRSa back to its main menu
 * (retries for ~12 s while the mode finishes). Call it only when
 * WCtlEnsureMode returned wctlOk - otherwise the control link isn't there
 * and it would just wait. */
UInt8 WCtlEnsureMode(UInt8 mode, UInt32 menuBaud) WCTL_SECTION;
UInt8 WCtlLeaveMode(UInt32 menuBaud) WCTL_SECTION;

#endif
