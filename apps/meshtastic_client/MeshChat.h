/*
 * MeshChat.h - the Meshtastic client for the Palm. The screens show what WiRSa's Mesh mode holds
 * (Link.c: nodes, channels, messages, sending, pairing); WiRSa talks to the Meshtastic node over
 * Bluetooth. Wire format: WiRSa's docs/Mesh-Palm.md. Pages that change the node's own settings
 * (Setup: owner, LoRa, device, channels, clock) are still mock-ups.
 *
 * Times are UTC minutes since 1970 in an Int32 (a Stamp): all 32-bit maths, right to the year 5970,
 * so dates stay correct beyond 2031 (and beyond 2106, where a 32-bit rx_time in seconds would stop).
 */
#ifndef MESHCHAT_H
#define MESHCHAT_H

#include <PalmOS.h>
#include "datelib.h"
#include "hackcheck.h"
#include "MeshChatRsc.h"

#define MAX_CONV		16
#define MAX_MSG			200
#define POOL_SIZE		12000		/* the texts of all messages, packed: short ones cost only their length */
#define MAX_NODE		32
#define MAX_SCAN		8
#define MAX_CHAN		8
#define TXT_MAX			234		/* 233 bytes of text (the Meshtastic limit) + NUL */
#define updRefresh		0x0100		/* FrmUpdateForm code: refill and redraw */

typedef Int32 Stamp;			/* minutes since 1970-01-01 UTC */

typedef struct {
	UInt32	num;			/* the node's number on the mesh */
	char	longName[40];
	char	shortName[8];
	char	id[12];
	Stamp	heard;
	Int8	snr;
	UInt8	hops;
	Int8	batt;			/* percent, -1 unknown */
	char	pos[24];		/* "lat, lon", empty = no position */
	UInt8	hwNum, roleNum;
	UInt32	uptime;			/* seconds, 0 = unknown */
	UInt16	util, air;		/* channel utilisation and air time, percent x10 */
	Boolean	fav;			/* a favourite on the node itself */
} Node;

typedef struct {
	Boolean	isChan;
	UInt8	index;			/* channel number, or node number for a direct message */
	UInt16	unread;
	Stamp	last;			/* newest message, 0 = none */
} Conv;

typedef struct {
	UInt8	conv;
	Boolean	out;			/* sent by this Palm */
	UInt8	from;			/* node number of the sender (incoming) */
	UInt8	status;			/* outgoing: 0 sending, 1 sent, 2 sent and acked, 3 failed, 4 sent (ack unknown), 5 unknown */
	UInt8	err;			/* with status 3: a Meshtastic Routing.Error from the node, or 240 and up: see ErrWords */
	Stamp	when;
	UInt32	seq;			/* WiRSa's number for it (incoming), 0 = none yet */
	UInt32	localId;		/* this Palm's number for a message it sent */
	UInt16	off;			/* the text: MsgText(m), in gPool */
} Msg;

typedef struct {
	UInt8	addr[6];
	UInt8	type;
	Int8	dbm;
	char	name[24];
} ScanHit;

typedef struct {
	char	longName[40];
	char	shortName[5];
	UInt8	region, preset, hop;
	UInt8	txPower;
	UInt8	role, btMode;
	char	pin[7];
	struct { char name[12]; UInt8 role; char key[46]; } chan[MAX_CHAN];
	Boolean	dirty;
} NodeCfg;

typedef struct {
	char	node[40];
	UInt8	mode;			/* 0 session (Mesh mode ends with the app), 1 background */
	UInt8	every;			/* index into 1, 5, 15, 30, 60 minutes */
} RelayCfg;

typedef struct {
	UInt8	baud;			/* index into 9600..115200 */
	Boolean	autoMode;
} Prefs;

extern Node gNodes[MAX_NODE];	extern UInt16 gNNodes;
extern Conv gConv[MAX_CONV];	extern UInt16 gNConv;	extern UInt16 gOrder[MAX_CONV];	extern UInt16 gNOrder;
extern Msg gMsg[MAX_MSG];		extern UInt16 gNMsg;
extern char gPool[POOL_SIZE];	extern UInt16 gPoolUsed;
#define MsgText(m)		(gPool + (m)->off)
extern NodeCfg gNodeCfg;
extern RelayCfg gRelay;
extern Prefs gPrefs;
extern Stamp gLastSync;
extern Int8 gMyBatt;				/* the node's battery, percent, -1 unknown */
extern ScanHit gScan[MAX_SCAN];	extern UInt16 gNScan;
extern UInt16 gCurConv;			/* conversation shown by frmChat */
extern UInt16 gCurNode;			/* node picked in frmNodes / frmScan, shown by frmNode */

/* Data.c */
void	DataInit(void);
void	PrefsLoad(void);
void	PrefsSave(void);
Stamp	NowUtc(void);
Int32	DayOf(Stamp t);
UInt32	StampToSec(Stamp t);
extern Boolean gNodeClockKnown;
extern Int32 gNodeSkew;
void	NodeClockSet(UInt32 nodeSec);
void	NodeClockText(char *node, char *diff);
void	FmtClock(Stamp t, char *out);
void	FmtDay(Stamp t, char *out);
void	FmtDateTime(Stamp t, char *out);
void	FmtStamp(Stamp t, char *out);
void	FmtStampShort(Stamp t, char *out);
void	FmtListTime(Stamp t, char *out);
void	FmtAgo(Stamp t, char *out);
void	ConvName(UInt16 c, char *out);
void	ConvLabel(UInt16 c, char *out);
void	SortConv(void);
UInt16	TotalUnread(void);
UInt16	FindDm(UInt16 node, Boolean create);
UInt16	FindChan(UInt8 index, Boolean create);
MC_SEC UInt16	NodeByNum(UInt32 num, Boolean create);
Msg		*AddMsg(UInt16 conv, Boolean out, UInt8 from, const char *text, Stamp when);
UInt32	SecTicks(UInt16 sec);
Stamp	SecToStamp(UInt32 utcSec);
UInt32	BaudOf(UInt8 idx);
void	LatLon(char *out, Int32 lat, Int32 lon);
void	FmtUptime(UInt32 sec, char *out);
void	FmtPct10(UInt16 v, char *out);
const char *HwName(UInt8 n);
const char *RoleName(UInt8 n);
const char *RegionName(UInt8 n);
const char *PresetName(UInt8 n);
const char *DevRoleName(UInt8 n);
const char *BtName(UInt8 n);
void	NodeClockShort(char *out);
UInt16	CountOlder(UInt16 days);
void	ClearOlder(UInt16 days);
UInt16	TextBytes(const char *s);
void	PutNum(char *out, Int32 v);
char	*StrEnd(char *s);
const char *ErrWords(UInt8 code);
const char *StatusWords(UInt8 st);
Boolean	ParseDeg(const char *s, Int32 *e7);
void	DropConv(UInt16 conv);

/* Link.c: WiRSa */
extern UInt8 gLinkState;		/* 0 no node paired, 1 down, 2 ready, 3 failed */
extern Boolean gWirsaUp;
extern char gLinkText[48];
extern Boolean gWantNodes, gWantChans;
extern Boolean gCfgLoaded;
extern UInt8 gFillTries;
extern UInt8 gCfgChans, gCfgStable;
extern UInt32 RpPending(void) MC_SEC;
extern UInt32 gCursor, gLastPollUtc, gWirsaUtc;
extern UInt16 gWirsaStored, gWirsaNodes;
extern char gFirmware[16];
MC_SEC Boolean	LinkOpen(void);
MC_SEC void	LinkClose(void);
MC_SEC Boolean	LinkSync(void);
MC_SEC Boolean	LinkSyncStart(void);
MC_SEC Boolean	LinkCfgStart(void);
MC_SEC Boolean	LinkAsyncCheck(void);
extern Boolean gSyncPending, gCfgPending, gNewsAgain;
MC_SEC UInt16	LinkPump(void);
MC_SEC void	CacheSave(void);
MC_SEC void	CacheLoad(void);
MC_SEC Boolean	LinkSend(UInt16 conv, const char *text);
MC_SEC Boolean	LinkScan(void);
MC_SEC Boolean	LinkPair(const ScanHit *h);
MC_SEC Boolean	LinkUnpair(void);
MC_SEC Boolean	LinkRelayGet(void);
MC_SEC Boolean	LinkRelaySet(UInt8 mode, UInt8 every);
MC_SEC Boolean	LinkCfgGet(void);
MC_SEC Boolean	LinkCfgSet(UInt16 mask, UInt16 chanMask);
MC_SEC Boolean	LinkAct(UInt8 action);
MC_SEC Boolean	LinkFav(UInt32 num, Boolean on);
MC_SEC Boolean	LinkClearWirsa(void);
MC_SEC Boolean	LinkReconnectNode(void);
MC_SEC Boolean	LinkNodeNow(void);
MC_SEC Boolean	LinkReq(UInt16 conv, UInt8 kind, Int32 lat, Int32 lon, const char *name);

/* MeshChat.c */
FieldPtr GetField(UInt16 id);
void	SetFieldPtr(UInt16 id, char *text);
void	SetFieldEdit(UInt16 id, const char *text);
void	GetFieldEdit(UInt16 id, char *out, UInt16 cap);
void	SetPopup(UInt16 popID, UInt16 lstID, Int16 sel, char *label, UInt16 cap);
Int16	GetListSel(UInt16 lstID);
Boolean	EditMenu(UInt16 id);
Boolean	NumPad(char *buf, UInt16 maxLen, const char *title);
void	Info(const char *a, const char *b, const char *c);
Boolean	Ask(const char *a, const char *b, const char *c);
void	McPoll(void);
Boolean	McCancelled(void);
void	BusyCancel(const char *msg);
void	Busy(const char *msg, UInt16 ticks);
void	BusyDone(void);
Boolean	PrefsHandleEvent(EventPtr e);
void	DoSync(void);
void	OpenSetup(void);
void	DoConnect(void);
void	DoDisconnect(void);
void	StatusLine(char *out, Int16 px);
Boolean	BattGadget(FormGadgetType *g, UInt16 cmd, void *param);

/* Views.c: the main lists and the node card; Chat.c: one conversation */
extern UInt8 gCat;				/* 0 channels, 1 direct messages, 2 nodes */
extern UInt16 gNodeView[MAX_NODE];	extern UInt16 gNNodeView;
extern Boolean gNodesRefresh;
MC_UI Boolean	LineGadget(FormGadgetType *g, UInt16 cmd, void *param);
MC_UI Boolean	MainHandleEvent(EventPtr e);
MC_UI Boolean	NodeHandleEvent(EventPtr e);
MC_UI void	MainFindPoll(void);
MC_UI Boolean	ChatHandleEvent(EventPtr e);
MC_UI void	ChatKeyPoll(void);
MC_UI Boolean	MsgHandleEvent(EventPtr e);
MC_UI Boolean	PosHandleEvent(EventPtr e);

/* Node.c: the node's own settings */
MC_SEC Boolean	SetupHandleEvent(EventPtr e);
MC_SEC Boolean	OwnerHandleEvent(EventPtr e);
MC_SEC Boolean	LoraHandleEvent(EventPtr e);
MC_SEC Boolean	DeviceHandleEvent(EventPtr e);
MC_SEC Boolean	ChListHandleEvent(EventPtr e);
MC_SEC Boolean	ChEditHandleEvent(EventPtr e);
MC_SEC Boolean	ActionsHandleEvent(EventPtr e);

/* Relay.c: WiRSa-side relay settings, pairing, clearing stored messages */
Boolean	RelayHandleEvent(EventPtr e);
Boolean	ClearHandleEvent(EventPtr e);

#endif
