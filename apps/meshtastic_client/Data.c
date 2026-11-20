/*
 * Data.c - the tables the screens read (nodes, conversations, messages), the time helpers and the
 * Palm-preference date and time formats. Link.c fills the tables from WiRSa.
 */
#include "MeshChat.h"
#include "era.h"

#define DAY			1440L			/* minutes in a day */

typedef struct { Int32 year; Int16 month, day, hour, minute, weekday; } Civil;

Node *gNodes;			UInt16 gNNodes, gMaxNode;
NodeFull gCard, gSelf;
Conv gConv[MAX_CONV];		UInt16 gNConv;		UInt16 gOrder[MAX_CONV];	UInt16 gNOrder;
Msg gMsg[MAX_MSG];			UInt16 gNMsg;
char gPool[POOL_SIZE];		UInt16 gPoolUsed;
NodeCfg gNodeCfg;
RelayCfg gRelay;
Prefs gPrefs;
Stamp gLastSync;
Int8 gMyBatt;
ScanHit gScan[MAX_SCAN];
UInt16 gNScan;
UInt16 gCurConv;
UInt16 gCurNode;


/* ---- small string helpers ------------------------------------------------------------- */

char *StrEnd(char *s)
{
	while (*s) s++;
	return s;
}

void PutNum(char *out, Int32 v)
{
	StrIToA(StrEnd(out), v);
}

/* A message's size on the air: Latin-1 letters above 0x7F take two bytes in UTF-8. */
UInt16 TextBytes(const char *s)
{
	UInt16 n = 0;
	for (; *s; s++)
		n += ((UInt8) *s >= 0x80) ? 2 : 1;
	return n;
}

/* ---- time ------------------------------------------------------------------------------- */
/* A Stamp is minutes since 1970-01-01 UTC in an Int32: good to the year 5970, all 32-bit maths (the
 * 64-bit libgcc helpers misbehave under this compiler - see edate.c). A node's rx_time (seconds)
 * is divided by 60 on the way in. */

/* Days since 1970-01-01 for a civil date, and back (Howard Hinnant's algorithms, 32-bit). */
static Int32 DaysFromCivil(Int32 y, Int32 m, Int32 d)
{
	Int32 era, yoe, doy, doe;

	if (m <= 2) y -= 1;
	era = y / 400;
	yoe = y - era * 400;
	doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097L + doe - 719468L;
}

static void CivilFromDays(Int32 z, Int32 *yy, Int32 *mm, Int32 *dd)
{
	Int32 era, doe, yoe, y, doy, mp;

	z += 719468L;
	era = z / 146097L;
	doe = z - era * 146097L;
	yoe = (doe - doe / 1460 + doe / 36524L - doe / 146096L) / 365;
	y = yoe + era * 400;
	doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	mp = (5 * doy + 2) / 153;
	*dd = doy - (153 * mp + 2) / 5 + 1;
	*mm = mp < 10 ? mp + 3 : mp - 9;
	*yy = y + (*mm <= 2 ? 1 : 0);
}

static Int32 MonthDays(Int32 y, Int32 m)
{
	static const Int8 dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
	return dim[(m + 11) % 12];
}

/* Palm time zone and daylight saving, in minutes east of UTC (0 on a ROM that has neither). */
static Int32 TzMin(void)
{
	UInt32 ver = 0;
	Int16 tz, dst;

	FtrGet(sysFtrCreator, sysFtrNumROMVersion, &ver);
	if (ver < 0x03500000L)
		return 0;
	tz = (Int16) PrefGetPreference(prefTimeZone);
	dst = (Int16) PrefGetPreference(prefDaylightSavingAdjustment);
	return (Int32) tz + (Int32) dst;
}

/* Now, as UTC minutes. The Palm clock is local time; with the Y2K35 hack the year is the real
 * one (Hc_CurrentRealDate), so the clock can be beyond 2031. */
/* The node's clock against this Palm's, kept in minutes from the node's own time (0 seconds = it has none). */
Boolean gNodeClockKnown;
Int32 gNodeSkew;

void NodeClockSet(UInt32 nodeSec)
{
	if (nodeSec < 1700000000UL) { gNodeClockKnown = false; return; }
	gNodeSkew = (Int32) (nodeSec / 60UL) - (Int32) NowUtc();
	gNodeClockKnown = true;
}

void NodeClockText(char *node, char *diff)
{
	Int32 s = gNodeSkew < 0 ? -gNodeSkew : gNodeSkew;

	if (! gNodeClockKnown) {
		StrCopy(node, "not set (no valid time)");
		StrCopy(diff, "-");
		return;
	}
	FmtDateTime(NowUtc() + gNodeSkew, node);
	if (s == 0) { StrCopy(diff, "in step"); return; }
	diff[0] = '\0';
	PutNum(diff, s);
	StrCat(diff, gNodeSkew > 0 ? " min ahead" : " min behind");
}

UInt32 StampToSec(Stamp t) { return (UInt32) t * 60UL; }

Stamp NowUtc(void)
{
	DlDateTime d;
	Int32 year, day;
	static UInt32 sTick;
	static Stamp sNow;
	static Boolean sHave;
	UInt32 tk = TimGetTicks();

	if (sHave && (Int32) (tk - sTick) >= 0 && tk - sTick < SysTicksPerSecond()) return sNow;	/* drawing a list asks for it often */
	if (HcCheck() == HC_OK) {
		Hc_CurrentRealDate(&d);					/* the plugin is installed and on: the real year */
	} else {							/* not installed, or off: the clock as the Palm has it, whatever offset was left behind */
		DateTimeType dt;

		TimSecondsToDateTime(TimGetSeconds(), &dt);
		d.year = dt.year; d.month = dt.month; d.day = dt.day; d.hour = dt.hour; d.minute = dt.minute;
	}
	year = d.year;
	day = d.day;
	if (day > MonthDays(year, d.month)) day = MonthDays(year, d.month);
	sNow = DaysFromCivil(year, d.month, day) * DAY + (Int32) d.hour * 60 + d.minute - TzMin();
	sTick = tk; sHave = true;
	return sNow;
}

static void Local(Stamp utc, Civil *c)
{
	Int32 lm = utc + TzMin(), days, y, m, d;

	if (lm < 0) lm = 0;
	days = lm / DAY;
	CivilFromDays(days, &y, &m, &d);
	c->year = y;
	c->month = (Int16) m;
	c->day = (Int16) d;
	c->hour = (Int16) ((lm % DAY) / 60);
	c->minute = (Int16) (lm % 60);
	c->weekday = (Int16) ((days + 4) % 7);		/* 1970-01-01 was a Thursday; 0 = Sunday */
}

static Int32 DayNo(Stamp utc)
{
	Int32 lm = utc + TzMin();
	return lm < 0 ? 0 : lm / DAY;
}

/* The day a time falls on (local), for telling when a new date heading starts. */
Int32 DayOf(Stamp t) { return DayNo(t); }

/* Dates and times follow the Palm's own Preferences: Time (12 or 24 hour, the separator), Date (the
 * short numeric form) and Long date. The ROM formatters stop at 2031, so the era-aware ones from
 * ../lib/era.c do the dates (they take the real year); times do not depend on the year. Buffers
 * for these must hold 32 characters. */

static DateFormatType ShortFmt(void) { return (DateFormatType) PrefGetPreference(prefDateFormat); }
static DateFormatType LongFmt(void) { return (DateFormatType) PrefGetPreference(prefLongDateFormat); }

void FmtClock(Stamp t, char *out)
{
	Civil d;
	Local(t, &d);
	TimeToAscii((UInt8) d.hour, (UInt8) d.minute, (TimeFormatType) PrefGetPreference(prefTimeFormat), out);
}

/* The long date with its weekday, in the user's long format: "Sat Oct 24, 2026". */
void FmtDay(Stamp t, char *out)
{
	Civil d;
	Local(t, &d);
	EraDateToDOWDMFormat((UInt8) d.month, (UInt8) d.day, (UInt16) d.year, LongFmt(), out);
}

/* The long date and the clock, no weekday: "Oct 24, 2026 12:25 pm". */
void FmtStamp(Stamp t, char *out)
{
	Civil d;
	char c[16];

	Local(t, &d);
	EraDateToAscii((UInt8) d.month, (UInt8) d.day, (UInt16) d.year, LongFmt(), out);
	FmtClock(t, c);
	StrCat(out, " ");
	StrCat(out, c);
}

/* The short numeric date and the clock, in the user's formats: "10/24/2026 12:25 pm". */
void FmtStampShort(Stamp t, char *out)
{
	Civil d;
	char c[16];

	Local(t, &d);
	EraDateToAscii((UInt8) d.month, (UInt8) d.day, (UInt16) d.year, ShortFmt(), out);
	FmtClock(t, c);
	StrCat(out, " ");
	StrCat(out, c);
}

void FmtDateTime(Stamp t, char *out)
{
	FmtStamp(t, out);
}

/* The short time in the conversation list: the clock today, "Yest.", the weekday within a week,
 * else the date in the user's short format. */
void FmtListTime(Stamp t, char *out)
{
	Civil d;
	Int32 age = DayNo(NowUtc()) - DayNo(t);

	out[0] = '\0';
	if (t == 0) return;
	if (age <= 0) { FmtClock(t, out); return; }
	if (age == 1) { StrCopy(out, "Yest."); return; }
	Local(t, &d);
	if (age < 7)
		EraDateTemplateToAscii("^1r", (UInt8) d.month, (UInt8) d.day, (UInt16) d.year, false, out, 16);
	else
		EraDateToAscii((UInt8) d.month, (UInt8) d.day, (UInt16) d.year, ShortFmt(), out);
}

/* ---- conversations and messages ------------------------------------------------------ */

/* The conversation's name for a list or title: no marker, a channel without a name gets its slot. */
void ConvLabel(UInt16 c, char *out)
{
	const Conv *v = &gConv[c];

	if (v->isChan) {
		if (gNodeCfg.chan[v->index].name[0]) {
			StrNCopy(out, gNodeCfg.chan[v->index].name, 31);
			out[31] = '\0';
		} else if (v->index == 0) {
			StrCopy(out, "Primary");
		} else {
			StrCopy(out, "Channel ");
			PutNum(out, v->index);
		}
	} else {
		StrNCopy(out, gNodes[v->index].longName, 31);
		out[31] = '\0';
	}
}

UInt16 TotalUnread(void)
{
	UInt16 i, n = 0;
	for (i = 0; i < gNConv; i++) n += gConv[i].unread;
	return n;
}

/* Newest conversation first. Channels always show; a direct message only once it has a message, so one that was
 * opened and abandoned leaves nothing behind. gNOrder rows are listed. */
void SortConv(void)
{
	UInt16 i, j, t;

	gNOrder = 0;
	for (i = 0; i < gNConv; i++)
		if (gConv[i].isChan || gConv[i].last != 0) gOrder[gNOrder++] = i;
	for (i = 1; i < gNOrder; i++) {
		t = gOrder[i];
		for (j = i; j > 0 && gConv[gOrder[j - 1]].last < gConv[t].last; j--)
			gOrder[j] = gOrder[j - 1];
		gOrder[j] = t;
	}
}

/* The table of conversations is full: the direct message that was quiet the longest goes, with the Palm's copy of its messages (WiRSa
 * still has them), and its place is used again. One being read, or with unread messages, goes last. 0xFFFF if every one is a channel. */
static UInt16 EvictDm(void)
{
	UInt16 i, best = 0xFFFF;

	for (i = 0; i < gNConv; i++) {
		if (gConv[i].isChan || i == gCurConv) continue;
		if (best == 0xFFFF || (gConv[i].unread == 0) > (gConv[best].unread == 0) ||
			((gConv[i].unread == 0) == (gConv[best].unread == 0) && gConv[i].last < gConv[best].last)) best = i;
	}
	if (best == 0xFFFF) return 0xFFFF;
	DropConv(best);
	MemSet(&gConv[best], sizeof(Conv), 0);
	return best;
}

/* The direct-message conversation with a node; made if asked and there is room. */
UInt16 FindDm(UInt16 node, Boolean create)
{
	UInt16 i;

	for (i = 0; i < gNConv; i++)
		if (! gConv[i].isChan && gConv[i].index == node) return i;
	if (! create) return 0xFFFF;
	if (gNConv >= MAX_CONV) {
		i = EvictDm();
		if (i == 0xFFFF) return 0xFFFF;
		gConv[i].index = node;
		return i;
	}
	MemSet(&gConv[gNConv], sizeof(Conv), 0);
	gConv[gNConv].index = node;
	return gNConv++;
}

/* The conversation for a channel; made if asked and there is room. */
UInt16 FindChan(UInt8 index, Boolean create)
{
	UInt16 i;

	for (i = 0; i < gNConv; i++)
		if (gConv[i].isChan && gConv[i].index == index) return i;
	if (! create) return 0xFFFF;
	if (gNConv >= MAX_CONV) {
		i = EvictDm();
		if (i == 0xFFFF) return 0xFFFF;
		gConv[i].isChan = true; gConv[i].index = index;
		SortConv();
		return i;
	}
	MemSet(&gConv[gNConv], sizeof(Conv), 0);
	gConv[gNConv].isChan = true;
	gConv[gNConv].index = index;
	gNConv++;
	SortConv();
	return (UInt16) (gNConv - 1);
}

/* Packs the texts again, in message order, after messages went. */
static void Repack(void)
{
	UInt16 i, used = 0, len;

	for (i = 0; i < gNMsg; i++) {
		len = (UInt16) (StrLen(MsgText(&gMsg[i])) + 1);
		if (gMsg[i].off != used) MemMove(gPool + used, MsgText(&gMsg[i]), len);
		gMsg[i].off = used;
		used = (UInt16) (used + len);
	}
	gPoolUsed = used;
}

/* Add a message; the caller fills in seq, localId and status. The oldest ones go when the table or the text pool is full. */
Msg *AddMsg(UInt16 conv, Boolean out, UInt16 from, const char *text, Stamp when)
{
	Msg *m;
	char *p;
	UInt16 len = (UInt16) StrLen(text);

	if (len > TXT_MAX - 1) len = TXT_MAX - 1;
	while (gNMsg > 0 && (gNMsg >= MAX_MSG || gPoolUsed + len + 1 > POOL_SIZE)) {
		MemMove(&gMsg[0], &gMsg[1], (UInt32) (gNMsg - 1) * sizeof(Msg));
		gNMsg--;
		Repack();
	}
	m = &gMsg[gNMsg++];
	MemSet(m, sizeof(Msg), 0);
	m->conv = (UInt8) conv;
	m->out = out;
	m->from = from;
	m->when = when;
	m->off = gPoolUsed;
	MemMove(gPool + gPoolUsed, text, len);
	gPool[gPoolUsed + len] = '\0';
	gPoolUsed = (UInt16) (gPoolUsed + len + 1);
	for (p = MsgText(m); *p; p++) if (*p == 7) *p = '*';		/* the alert bell has no glyph in the Palm fonts */
	if (when > gConv[conv].last) gConv[conv].last = when;
	return m;
}

/* Removes the messages of one conversation (the Palm's copy only). */
void DropConv(UInt16 conv)
{
	UInt16 i, k = 0;

	for (i = 0; i < gNMsg; i++) {
		if (gMsg[i].conv == conv) continue;
		if (k != i) MemMove(&gMsg[k], &gMsg[i], sizeof(Msg));
		k++;
	}
	gNMsg = k;
	Repack();
	gConv[conv].last = 0;
	gConv[conv].unread = 0;
}

/* Why a message failed: the title and the meaning are the wording of the Meshtastic Android app's "Delivery Errors"; the line
 * "what to do" is its advice, or ours where the app gives none. 1 to 39 are the node's Routing.Error codes; 240 and up are WiRSa's
 * and the Palm's. */
const char *ErrTitle(UInt8 code)
{
	switch (code) {
	case 1:		return "No route";
	case 2: case 3: case 5:	return "Failed to deliver to mesh";
	case 4:		return "No radio interface";
	case 6:		return "Channel/key mismatch";
	case 7:		return "Message is too large to send";
	case 8:		return "No app response";
	case 9:		return "Duty cycle limit";
	case 32:	return "Invalid request";
	case 33:	return "Not authorized";
	case 34:	return "Could not send encrypted message";
	case 35:	return "Recipient needs your key";
	case 36:	return "Admin session expired";
	case 37:	return "Admin key not authorized";
	case 38:	return "Rate limited";
	case 39:	return "Recipient key unavailable";
	case 240:	return "Not written to the node";
	case 241:	return "Not linked to the node";
	case 242:	return "Not connected to WiRSa";
	case 243:	return "Message too long";
	}
	return "Error";
}

const char *ErrMeaning(UInt8 code)
{
	switch (code) {
	case 1:		return "No path to the destination. The recipient may be offline or out of range.";
	case 2: case 3: case 5:	return "Retries exhausted. This covers a relay refusing (NAK), a timeout, or running out of retransmits.";
	case 4:		return "No radio available to send.";
	case 6:		return "The channel or PSK doesn't match. For a direct message, the other node may also lack your public key.";
	case 7:		return "The message exceeds the maximum payload size.";
	case 8:		return "The app or plugin didn't respond.";
	case 9:		return "The regional airtime limit was reached.";
	case 32:	return "The request was malformed.";
	case 33:	return "The destination refused the request.";
	case 34:	return "Encryption failed.";
	case 35:	return "Direct message encryption couldn't complete because the other node lacks your public key.";
	case 36:	return "A remote-admin session timed out.";
	case 37:	return "The target doesn't accept your admin key.";
	case 38:	return "The mesh is throttling you.";
	case 39:	return "You don't have the recipient's public key.";
	case 240:	return "WiRSa could not write the message to the node.";
	case 241:	return "WiRSa has no link to the node.";
	case 242:	return "This Palm has no link to WiRSa.";
	case 243:	return "The message is too long, or WiRSa had no room for it.";
	}
	return "The node reported an error.";
}

const char *ErrAction(UInt8 code)
{
	switch (code) {
	case 1:		return "Try again later, or move closer.";
	case 2: case 3: case 5:	return "Send it again in a moment.";
	case 4:		return "Check the radio connection.";
	case 6:		return "Confirm both nodes share the channel and key. For a direct message, open its Send menu, choose Request node info, then send again.";
	case 7:		return "Shorten it.";
	case 8:		return "Retry, or check the destination module.";
	case 9:		return "Wait for the window to reset.";
	case 32:	return "Retry; update or restart the app if it persists.";
	case 33:	return "Check the channel and keys.";
	case 34:	return "Verify both nodes exchanged keys and run compatible firmware.";
	case 35:	return "Exchange node info (Send menu > Request node info), then send again.";
	case 36:	return "Reopen the remote node's settings.";
	case 37:	return "Verify the key matches on both nodes.";
	case 38:	return "Wait before sending.";
	case 39:	return "Wait for their node info, or ask them to broadcast it (Send menu > Request node info).";
	case 240:	return "Send it again. If it keeps failing, connect to the node again.";
	case 241:	return "Wait for the node link to come up (see the status line), then send again.";
	case 242:	return "Connect first, then send again.";
	case 243:	return "Shorten it, then send again.";
	}
	return "Send it again.";
}

const char *StatusWords(const Msg *m)
{
	switch (m->status) {
	case 0:		return "Sending";
	case 1: case 5:	return "Sent, waiting for confirmation";
	case 2:		return gConv[m->conv].isChan ? "Delivered to mesh" : "Delivered to recipient";
	case 3:		return "Error";
	case 4:		return "Relayed, not confirmed by recipient";
	case 6:		return "Exchanging keys, then it is sent again";
	}
	return "Unknown";
}

Int16 NameWidth(const char *s, Int16 len)
{
	Int16 i = 0, j, w = 0;
	FontID f = FntGetFont();

	while (i < len) {
		if ((UInt8) s[i] == NAME_EMOJI) {
			FntSetFont(symbolFont);
			w = (Int16) (w + FntCharsWidth(s + i, 1));
			FntSetFont(f);
			i++;
			continue;
		}
		for (j = i; j < len && (UInt8) s[j] != NAME_EMOJI; j++) ;
		w = (Int16) (w + FntCharsWidth(s + i, (Int16) (j - i)));
		i = j;
	}
	return w;
}

void DrawName(const char *s, Int16 len, Coord x, Coord y, Int16 maxW)
{
	Int16 i = 0, j, used = 0, w;
	FontID f = FntGetFont();

	while (i < len && used < maxW) {
		if ((UInt8) s[i] == NAME_EMOJI) {
			FntSetFont(symbolFont);
			w = FntCharsWidth(s + i, 1);
			if (used + w <= maxW) WinDrawChars(s + i, 1, (Coord) (x + used), (Coord) (y + 1));	/* the symbol sits a pixel lower */
			FntSetFont(f);
			used = (Int16) (used + w);
			i++;
			continue;
		}
		for (j = i; j < len && (UInt8) s[j] != NAME_EMOJI; j++) ;
		w = FntCharsWidth(s + i, (Int16) (j - i));
		if (used + w <= maxW) WinDrawChars(s + i, (Int16) (j - i), (Coord) (x + used), y);
		else { WinDrawTruncChars(s + i, (Int16) (j - i), (Coord) (x + used), y, (Int16) (maxW - used)); break; }
		used = (Int16) (used + w);
		i = j;
	}
	FntSetFont(f);
}

void FlatName(char *s)
{
	for (; *s; s++) if ((UInt8) *s == NAME_EMOJI) *s = '*';
}

void StripName(char *s)
{
	char *d = s;

	for (; *s; s++) if ((UInt8) *s != NAME_EMOJI) *d++ = *s;
	*d = '\0';
}

/* A small key beside a node's name: its public key is known. */
void DrawKeyIcon(Coord x, Coord y)
{
	MemHandle h = DmGetResource('Tbmp', bmpKey);

	if (! h) return;
	WinDrawBitmap((BitmapPtr) MemHandleLock(h), x, y);
	MemHandleUnlock(h);
	DmReleaseResource(h);
}

/* "-1.3521" or "103.8198" to degrees x 1e7. At most 3 digits before the point, 7 after. False if it is not a number. */
Boolean ParseDeg(const char *s, Int32 *e7)
{
	Int32 whole = 0, frac = 0, scale = 1000000;
	Boolean neg = false, any = false, dot = false;

	while (*s == ' ') s++;
	if (*s == '-') { neg = true; s++; }
	else if (*s == '+') s++;
	for (; *s; s++) {
		if (*s >= '0' && *s <= '9') {
			any = true;
			if (! dot) { whole = whole * 10 + (*s - '0'); if (whole > 999) return false; }
			else if (scale) { frac += (*s - '0') * scale; scale /= 10; }
		} else if ((*s == '.' || *s == ',') && ! dot) dot = true;
		else if (*s == ' ' && s[1] == '\0') ;
		else return false;
	}
	if (! any) return false;
	*e7 = whole * 10000000L + frac;
	if (neg) *e7 = -*e7;
	return true;
}

/* Helpers for the link code (Link.c is in the second segment, which must not multiply, divide or read
 * const tables itself - see mc_seg.h). */
UInt32 SecTicks(UInt16 sec)
{
	return (UInt32) sec * SysTicksPerSecond();
}

Stamp SecToStamp(UInt32 utcSec)
{
	return (Stamp) (utcSec / 60);
}

static const UInt32 kRates[7] = { 9600, 14400, 19200, 28800, 38400, 57600, 115200 };

UInt32 BaudOf(UInt8 idx)
{
	return kRates[idx % 7];
}

/* "51.5007, -0.1246" from degrees times 1e7. */
void LatLon(char *out, Int32 lat, Int32 lon)
{
	Int32 v[2];
	UInt16 k;

	v[0] = lat; v[1] = lon;
	out[0] = '\0';
	for (k = 0; k < 2; k++) {
		UInt32 a = (UInt32) (v[k] < 0 ? -v[k] : v[k]);
		char *p;

		if (k) StrCat(out, ", ");
		p = StrEnd(out);
		if (v[k] < 0) *p++ = '-';
		*p = '\0';
		PutNum(out, (Int32) (a / 10000000UL));
		StrCat(out, ".");
		p = StrEnd(out);
		StrPrintF(p, "%04ld", (Int32) ((a % 10000000UL) / 1000UL));
	}
}

/* "6d 3h", "9h 20m", "12m": an uptime in seconds ("-" when unknown). */
void FmtUptime(UInt32 sec, char *out)
{
	UInt32 m = sec / 60;

	out[0] = '\0';
	if (sec == 0) { StrCopy(out, "-"); return; }
	if (m >= 1440) { PutNum(out, (Int32) (m / 1440)); StrCat(out, "d "); PutNum(out, (Int32) ((m % 1440) / 60)); StrCat(out, "h"); }
	else if (m >= 60) { PutNum(out, (Int32) (m / 60)); StrCat(out, "h "); PutNum(out, (Int32) (m % 60)); StrCat(out, "m"); }
	else { PutNum(out, (Int32) m); StrCat(out, "m"); }
}

/* "3.8%" from a percentage times ten ("-" when zero). */
void FmtPct10(UInt16 v, char *out)
{
	out[0] = '\0';
	if (v == 0) { StrCopy(out, "-"); return; }
	PutNum(out, v / 10);
	StrCat(out, ".");
	PutNum(out, v % 10);
	StrCat(out, "%");
}

/* ---- hardware and role names ---------------------------------------------------------------- */

const char *HwName(UInt8 n)
{
	static char other[16];

	switch (n) {
	case 4: return "T-Beam";
	case 7: return "T-Echo";
	case 9: return "RAK4631";
	case 31: return "Station G2";
	case 43: return "Heltec V3";
	case 48: return "Heltec Wireless Tracker";
	case 50: return "T-Deck";
	case 69: return "Heltec T114";
	case 71: return "T1000-E";
	case 0: return "unknown";
	}
	StrCopy(other, "model ");
	PutNum(other, n);
	return other;
}

const char *RoleName(UInt8 n)
{
	static const char *k[13] = { "Client", "Client Mute", "Router", "Router Client", "Repeater", "Tracker", "Sensor",
		"TAK", "Client Hidden", "Lost and Found", "TAK Tracker", "Router Late", "Client Base" };

	return n < 13 ? k[n] : "unknown";
}

/* The names the Settings card shows (the same orders as the lists of the edit forms). */
const char *RegionName(UInt8 n)
{
	static const char *k[14] = { "US", "EU 433", "EU 868", "CN", "JP", "ANZ", "KR", "TW", "RU", "IN", "NZ 865", "TH",
		"UA 868", "SG 923" };

	return n < 14 ? k[n] : "?";
}

const char *PresetName(UInt8 n)
{
	static const char *k[8] = { "Long Fast", "Long Slow", "Long Mod", "Medium Fast", "Medium Slow", "Short Fast",
		"Short Slow", "Short Turbo" };

	return n < 8 ? k[n] : "?";
}

const char *DevRoleName(UInt8 n)
{
	static const char *k[9] = { "Client", "Client Mute", "Router", "Tracker", "Sensor", "TAK", "Client Hidden",
		"Lost and Found", "Client Base" };

	return n < 9 ? k[n] : "?";
}

const char *BtName(UInt8 n)
{
	static const char *k[3] = { "Random PIN", "Fixed PIN", "No PIN" };

	return n < 3 ? k[n] : "?";
}

/* The node's time of day, and how far it is from this Palm's, for one line. */
void NodeClockShort(char *out)
{
	if (! gNodeClockKnown) { StrCopy(out, "not known"); return; }
	FmtClock(NowUtc() + gNodeSkew, out);
	if (gNodeSkew != 0) {
		StrCat(out, ", ");
		PutNum(out, gNodeSkew < 0 ? -gNodeSkew : gNodeSkew);
		StrCat(out, "m off");
	}
}

/* ---- clearing ---------------------------------------------------------------------------- */

UInt16 gClearConv = 0xFFFF;

static Boolean ClearsIt(UInt16 i, UInt16 days, Stamp limit)
{
	if (gClearConv != 0xFFFF && gMsg[i].conv != gClearConv) return false;
	return days == 0 || gMsg[i].when < limit;
}

UInt16 CountOlder(UInt16 days)
{
	UInt16 i, n = 0;
	Stamp limit = NowUtc() - days * DAY;

	for (i = 0; i < gNMsg; i++)
		if (ClearsIt(i, days, limit)) n++;
	return n;
}

void ClearOlder(UInt16 days)
{
	UInt16 i, k = 0;
	Stamp limit = NowUtc() - days * DAY;

	for (i = 0; i < gNMsg; i++)
		if (! ClearsIt(i, days, limit)) {
			if (k != i) MemMove(&gMsg[k], &gMsg[i], sizeof(Msg));
			k++;
		}
	gNMsg = k;
	Repack();
	for (i = 0; i < gNConv; i++) {
		UInt16 j;
		gConv[i].last = 0;
		for (j = 0; j < gNMsg; j++)
			if (gMsg[j].conv == i && gMsg[j].when > gConv[i].last) gConv[i].last = gMsg[j].when;
		if (gConv[i].last == 0) gConv[i].unread = 0;
	}
	SortConv();
}

/* ---- start ------------------------------------------------------------------------------------ */

/* The app's own settings survive a restart (Palm's saved preferences). */
#define prefsCreator	'McHt'
#define prefsVersion	3		/* 1 listed five rates (9600 19200 38400 57600 115200); 2 had no SD History */

void PrefsLoad(void)
{
	Prefs p;
	UInt16 size = sizeof(Prefs);
	UInt16 ver;

	MemSet(&p, sizeof(p), 0);
	ver = PrefGetAppPreferences(prefsCreator, 0, &p, &size, true);

	if (size != sizeof(Prefs) && size != 2)			/* 2 bytes: baud and autoMode, before SD History */
		return;
	if ((ver == prefsVersion || ver == 2) && p.baud < 7)
		gPrefs = p;
	else if (ver == 1 && p.baud < 5) {
		gPrefs = p;
		switch (p.baud) {		/* the five-rate numbering became the seven-rate one */
		case 1:  gPrefs.baud = 2; break;
		case 2:  gPrefs.baud = 4; break;
		case 3:  gPrefs.baud = 5; break;
		case 4:  gPrefs.baud = 6; break;
		default: gPrefs.baud = 0; break;
		}
	}
}

void PrefsSave(void)
{
	PrefSetAppPreferences(prefsCreator, 0, prefsVersion, &gPrefs, sizeof(Prefs), true);
}

void DataInit(void)
{
	UInt32 id;

	gNNodes = 0; gNConv = 0; gNMsg = 0; gPoolUsed = 0;
	if (! gNodes) {
		gMaxNode = MAX_NODE;
		gNodes = (Node *) MemPtrNew((UInt32) gMaxNode * sizeof(Node));
		if (! gNodes) { gMaxNode = 32; gNodes = (Node *) MemPtrNew((UInt32) gMaxNode * sizeof(Node)); }	/* a Palm short of memory: fewer nodes */
		if (! gNodes) { gMaxNode = 0; ErrDisplay("Out of memory"); }
	}
	if (gNodes) MemSet(gNodes, (UInt32) gMaxNode * sizeof(Node), 0);
	MemSet(&gRelay, sizeof(gRelay), 0);
	gRelay.mode = 0; gRelay.every = 1; gRelay.sdFlush = 4;
	gPrefs.baud = 0; gPrefs.autoMode = true; gPrefs.sdHist = false;
	PrefsLoad();

	NodeDataClear();
	gNodeId = 0;
	id = CacheLastNode();			/* what the last run knew of the node shown last: a launch then fetches only what is new */
	if (id) CacheLoadNode(id);
}
