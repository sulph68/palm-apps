/*
 * Data.c - the tables the screens read (nodes, conversations, messages), the time helpers and the
 * Palm-preference date and time formats. Link.c fills the tables from WiRSa.
 */
#include "MeshChat.h"
#include "era.h"

#define DAY			1440L			/* minutes in a day */

typedef struct { Int32 year; Int16 month, day, hour, minute, weekday; } Civil;

Node gNodes[MAX_NODE];		UInt16 gNNodes;
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

/* "5m", "3h", "12d": how long ago. */
void FmtAgo(Stamp t, char *out)
{
	Int32 age = NowUtc() - t;

	out[0] = '\0';
	if (age < 1) age = 1;
	if (age < 60) { PutNum(out, age); StrCat(out, "m"); }
	else if (age < 2 * DAY) { PutNum(out, age / 60); StrCat(out, "h"); }
	else { PutNum(out, age / DAY); StrCat(out, "d"); }
}

/* ---- conversations and messages ------------------------------------------------------ */

void ConvName(UInt16 c, char *out)
{
	const Conv *v = &gConv[c];
	out[0] = v->isChan ? '#' : '>';
	out[1] = ' ';
	if (v->isChan)
		StrCopy(out + 2, gNodeCfg.chan[v->index].name);
	else
		StrNCopy(out + 2, gNodes[v->index].longName, 30);
	out[32] = '\0';
}

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

/* The direct-message conversation with a node; made if asked and there is room. */
UInt16 FindDm(UInt16 node, Boolean create)
{
	UInt16 i;

	for (i = 0; i < gNConv; i++)
		if (! gConv[i].isChan && gConv[i].index == node) return i;
	if (! create || gNConv >= MAX_CONV) return 0xFFFF;
	MemSet(&gConv[gNConv], sizeof(Conv), 0);
	gConv[gNConv].index = (UInt8) node;
	return gNConv++;
}

/* The conversation for a channel; made if asked and there is room. */
UInt16 FindChan(UInt8 index, Boolean create)
{
	UInt16 i;

	for (i = 0; i < gNConv; i++)
		if (gConv[i].isChan && gConv[i].index == index) return i;
	if (! create || gNConv >= MAX_CONV) return 0xFFFF;
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
Msg *AddMsg(UInt16 conv, Boolean out, UInt8 from, const char *text, Stamp when)
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

/* The reason a message failed, in words. 1 to 39 are the node's Routing.Error codes; 240 and up are WiRSa's and the Palm's. */
const char *ErrWords(UInt8 code)
{
	switch (code) {
	case 1:		return "The mesh has no route to that node.";
	case 2:		return "The next node answered with a refusal.";
	case 3:		return "No answer came in time.";
	case 4:		return "The node has no radio interface for it.";
	case 5:		return "It was sent several times and never acknowledged.";
	case 6:		return "The node has no such channel.";
	case 7:		return "The message is too large for the radio.";
	case 8:		return "The other node did not respond.";
	case 9:		return "The radio's duty cycle limit is reached. Wait a while and send again.";
	case 32:	return "The node rejected the request.";
	case 33:	return "The node says this is not authorised.";
	case 34:	return "Encrypting with the other node's key failed.";
	case 35:	return "The other node does not know this node's public key.";
	case 36: case 37:	return "The node rejected the admin session.";
	case 38:	return "Sending too fast. Wait a moment and send again.";
	case 39:	return "The node has no public key for the recipient, so it cannot encrypt a direct message. Send Request node info from the Send menu, then send again.";
	case 240:	return "WiRSa could not write the message to the node.";
	case 241:	return "WiRSa is not linked to the node.";
	case 242:	return "The Palm is not connected to WiRSa.";
	case 243:	return "It is too long, or WiRSa had no room for it.";
	}
	return "The node reported an error.";
}

const char *StatusWords(UInt8 st)
{
	switch (st) {
	case 0:		return "Sending to the node";
	case 1:		return "Sent by the node, no acknowledgement yet";
	case 2:		return "Delivered (acknowledged)";
	case 3:		return "Failed";
	case 4:		return "Sent, acknowledgement not known";
	}
	return "Unknown";
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

UInt16 CountOlder(UInt16 days)
{
	UInt16 i, n = 0;
	Stamp limit = NowUtc() - days * DAY;

	for (i = 0; i < gNMsg; i++)
		if (days == 0 || gMsg[i].when < limit) n++;
	return n;
}

void ClearOlder(UInt16 days)
{
	UInt16 i, k = 0;
	Stamp limit = NowUtc() - days * DAY;

	for (i = 0; i < gNMsg; i++)
		if (! (days == 0 || gMsg[i].when < limit)) {
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
#define prefsVersion	2		/* 1 listed five rates (9600 19200 38400 57600 115200) */

void PrefsLoad(void)
{
	Prefs p;
	UInt16 size = sizeof(Prefs);

	UInt16 ver = PrefGetAppPreferences(prefsCreator, 0, &p, &size, true);

	if (size != sizeof(Prefs))
		return;
	if (ver == prefsVersion && p.baud < 7)
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
	UInt16 i;

	gNNodes = 0; gNConv = 0; gNMsg = 0; gPoolUsed = 0;
	MemSet(gNodes, sizeof(gNodes), 0);
	MemSet(gConv, sizeof(gConv), 0);
	MemSet(&gRelay, sizeof(gRelay), 0);
	gRelay.mode = 0; gRelay.every = 1;
	gMyBatt = -1;
	gPrefs.baud = 0; gPrefs.autoMode = true;
	PrefsLoad();

	/* The node's own settings (Setup pages): shown from here until the node can be asked for them. */
	StrCopy(gNodeCfg.chan[0].name, "Primary");
	gNodeCfg.region = 0; gNodeCfg.preset = 0; gNodeCfg.hop = 3; gNodeCfg.txPower = 0;
	gNodeCfg.role = 0; gNodeCfg.btMode = 0;
	StrCopy(gNodeCfg.pin, "123456");
	for (i = 0; i < MAX_CHAN; i++) gNodeCfg.chan[i].role = 0;
	CacheLoad();					/* what the last run knew: a launch then fetches only what is new */
}
