/*
 * dateprobe.c - DateProbe: a verification harness app for the HackMaster Y2K35
 * plugin. It calls the OS date/time conversion traps on the current clock and
 * writes the results into a read-only text field, so an automated tester can
 * read every value straight from ReControl `ui` (no OCR).
 *
 * Shows: raw TimGetSeconds; TimSecondsToDateTime (Int16-year DateTimeType);
 * DateSecondsToDate (7-bit DateType year); DayOfWeek; and the hack's published
 * offset Feature (FtrGet creator 'Y235'), which reads err until the real plugin
 * publishes it. Build: normal Palm `appl` (creator DPrb).
 */
#include <PalmOS.h>
#include "datelib.h"

#define formMain     1000
#define fldDump      1001
#define btnRefresh   1002
#define HACK_CREATOR 'Y235'
#define FTR_OFFSET   0          /* feature num the plugin will publish offset in */

static char gBuf[420];

static char gDet[400];

/* One self-test line into `out`: real date -> based/offset, weekday & round-trip
 * checks, all on-device via datelib. Returns 1 if both checks pass. */
static int TLine(char *out, dl_i32 y, dl_i16 mo, dl_i16 d, dl_i16 h, dl_i16 mi, dl_i16 s)
{
    dl_i32     off   = dl_reBaseOffset(y);
    dl_i32     based = y - off;
    dl_i64     m     = dl_dateToSeconds(y, mo, d, h, mi, s);
    DlDateTime rd;
    int        wd, rt;
    char       line[64];

    dl_secondsToDate(m, &rd);
    wd = (dl_dayOfWeek(based, mo, d) == dl_dayOfWeek(y, mo, d));
    rt = (rd.year == y && rd.month == mo && rd.day == d &&
          rd.hour == h && rd.minute == mi && rd.second == s);
    StrPrintF(line, "%ld-%02d-%02d>b%ld o%ld %s %s\n",
              (long) y, (int) mo, (int) d, (long) based, (long) off,
              wd ? "wd+" : "WD!", rt ? "rt+" : "RT!");
    StrCat(out, line);
    return (wd && rt) ? 1 : 0;
}

/* Enumerate every database on card 0; list those whose NAME contains a '^'
 * (the HotSync-garbled symptom) or whose creator is a Datebook/ToDo one. Writes
 * "name|CCCC|attr" lines so an automated tester reads them straight from `ui`. */
static void DbScan(char *out)
{
    UInt16 n = DmNumDatabases(0), i;
    out[0] = '\0';
    for (i = 0; i < n; i++) {
        LocalID id = DmGetDatabase(0, i);
        char    name[36];
        UInt32  creator = 0, type = 0;
        UInt16  attr = 0;
        char   *p;
        Boolean hasCaret = false;
        if (id == 0) continue;
        name[0] = '\0';
        DmDatabaseInfo(0, id, name, &attr, NULL, NULL, NULL, NULL, NULL,
                       NULL, NULL, &type, &creator);
        for (p = name; *p; p++) if (*p == '^') { hasCaret = true; break; }
        if (hasCaret ||
            creator == 'Db35' || creator == 'Td35' || creator == 'date') {
            char line[64], cc[5];
            cc[0] = (char)(creator >> 24); cc[1] = (char)(creator >> 16);
            cc[2] = (char)(creator >> 8);  cc[3] = (char) creator; cc[4] = 0;
            StrPrintF(line, "%s|%s|%x\n", name, cc, (unsigned) attr);
            if (StrLen(out) + StrLen(line) < 380) StrCat(out, line);
        }
    }
}

static void BuildDump(void)
{
    UInt32       now = TimGetSeconds();
    DateTimeType dt;
    char         line[96];
    int          pass = 0, total = 0;

    gDet[0] = '\0';
    total++; pass += TLine(gDet, 2000, 2, 29, 12,  0,  0); /* in-range /400 leap, Feb29 */
    total++; pass += TLine(gDet, 2048, 2, 29,  6, 30,  0); /* future leap, Feb29        */
    total++; pass += TLine(gDet, 2050, 7,  1, 14, 30, 15); /* >2031, >2^32 master       */
    total++; pass += TLine(gDet, 2100, 6, 14,  9,  5, 45); /* non-leap century          */
    total++; pass += TLine(gDet, 2104, 2, 29, 18,  0,  0); /* leap after anomaly, Feb29 */
    total++; pass += TLine(gDet, 2300, 1,  1,  0,  0,  1); /* non-leap century          */
    total++; pass += TLine(gDet, 2400, 2, 29, 23, 59, 59); /* /400 leap, Feb29          */
    total++; pass += TLine(gDet, 6000, 3, 15, 10, 20, 30); /* far future, big master    */

    TimSecondsToDateTime(now, &dt);
    gBuf[0] = '\0';
    StrPrintF(line, "ST %d/%d %s\n", pass, total, (pass == total) ? "PASS" : "FAIL");
    StrCat(gBuf, line);
    StrPrintF(line, "clk %d-%02d-%02d %02d:%02d:%02d\n",
              dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
    StrCat(gBuf, line);
    /* DIAG: which 'Y235' features exist while the hack is enabled? */
    {
        UInt32 v;
        StrPrintF(line, "ftr: 1000e=%d 0e=%d 1e=%d 2e=%d\n",
                  (int) FtrGet('Y235', 1000, &v), (int) FtrGet('Y235', 0, &v),
                  (int) FtrGet('Y235', 1, &v),    (int) FtrGet('Y235', 2, &v));
        StrCat(gBuf, line);
    }
    DbScan(gDet);              /* DB names: flag '^' + Datebook/ToDo creators */
    StrCat(gBuf, gDet);
}

static void RefreshField(FormPtr frm)
{
    FieldPtr fld = (FieldPtr) FrmGetObjectPtr(frm,
                       FrmGetObjectIndex(frm, fldDump));
    BuildDump();
    FldSetTextPtr(fld, gBuf);
    FldRecalculateField(fld, true);
    FldEraseField(fld);
    FldDrawField(fld);
}

static Boolean MainFormHandleEvent(EventPtr ev)
{
    FormPtr frm = FrmGetActiveForm();

    switch (ev->eType) {
    case frmOpenEvent:
        FrmDrawForm(frm);
        RefreshField(frm);
        return true;
    case ctlSelectEvent:
        if (ev->data.ctlSelect.controlID == btnRefresh) {
            RefreshField(frm);
            return true;
        }
        break;
    case frmCloseEvent: {
        /* text points at our static buffer, not an owned handle; detach it */
        FieldPtr fld = (FieldPtr) FrmGetObjectPtr(frm,
                           FrmGetObjectIndex(frm, fldDump));
        FldSetTextPtr(fld, NULL);
        break;
    }
    default:
        break;
    }
    return false;
}

static Boolean AppHandleEvent(EventPtr ev)
{
    if (ev->eType == frmLoadEvent) {
        UInt16  id  = ev->data.frmLoad.formID;
        FormPtr frm = FrmInitForm(id);
        FrmSetActiveForm(frm);
        FrmSetEventHandler(frm, MainFormHandleEvent);
        return true;
    }
    return false;
}

UInt32 PilotMain(UInt16 cmd, MemPtr cmdPBP, UInt16 launchFlags)
{
    EventType ev;
    Err       err;

    if (cmd == sysAppLaunchCmdNormalLaunch) {
        FrmGotoForm(formMain);
        do {
            EvtGetEvent(&ev, evtWaitForever);
            if (!SysHandleEvent(&ev))
                if (!MenuHandleEvent(0, &ev, &err))
                    if (!AppHandleEvent(&ev))
                        FrmDispatchEvent(&ev);
        } while (ev.eType != appStopEvent);
        FrmCloseAllForms();
    }
    return 0;
}
