# EraNTP — Single-Screen NTP Client (reference & rebuild guide)

A standalone utility that queries an NTP server over UDP, sets the device clock, and
cooperates with the HackMaster Y2K35 era plugin's safe rebase-past-2031 mechanism.
Creator `Entp`, single file (`EraNTP.c`), single segment, single form, no locale
variants — `.def` = `application { "EraNTP" Entp backup }`. Unlike the four PIM apps
(ToDo35/Datebook35/Address35/CryptoNote), EraNTP owns no database and needs no
multi-segment layout — the whole app comfortably fits in one code resource. See design
memory [[erantp-app]].

## 1. What EraNTP is

One screen: a Server field (default `pool.ntp.org`), a "Set device clock" checkbox, three
always-live comparison rows (**Device** / **Era** / **NTP**, each one `LABEL` re-drawn in
place roughly once a second), a scrollable action-trace log, and **Sync Now**/**Done**
buttons. A `Network` menu (`Connect`/`Disconnect`) lets the link warm up ahead of time on
real hardware. No periodic/alarm sync — explicit "Sync Now" only. No HotSync-backed prefs;
the Server text and checkbox state live in the OS's **unsaved** prefs DB (`A35Defaults`
precedent, nothing here worth backing up).

## 2. Era-plugin cooperation (the actual point of this app)

- If the NTP-retrieved real year is **> 2031**, the clock is **always** re-based via
  `datelib`'s year-type match (`dl_reBaseOffset`/`dl_dateToSeconds` — the exact same math
  the era plugin's own control panel uses) **regardless of whether the plugin is
  installed**, so the device is left in a safe, plausible state either way.
- If the plugin **is** active (`HcCheck() == HC_OK`), the true date is **additionally**
  published to its Feature Manager/pref slots (`FtrSet` + `PrefSetAppPreferences`, same
  `Y2K35_CREATOR`/`Y2K35_FTR_*`/`Y2K35_PREF_ID` constants as `../lib/y2k35.h`) so the
  plugin's own panel echoes the real date and every era-aware app in the suite sees it. If
  the plugin isn't active, this step is silently skipped — the rebase-only clock set already
  stands on its own.
- "Set device clock" **unchecked** = dry run: query + log the comparison only, no
  `TimSetSeconds`, no publish.
- NTP is UTC; the retrieved time is converted to the device's own configured local time
  (`TimUTCToTimeZone` with `prefTimeZone`/`prefDaylightSavingAdjustment` — same prefs the
  Date & Time panel itself uses) before any rebase math runs.

## 3. Sync state machine — why it's not one blocking call

Palm OS 4.x/68K has no user-level threads, so "run the NTP query without freezing the UI"
means driving a resumable state machine one small step per event-loop tick
(`SyncTick`/`SyncPhase`: `kSyncOpening` → `kSyncResolving` → `kSyncSending` →
`kSyncWaiting`), not one long synchronous call:

- **Opening**: `SysLibFind("Net.lib")` + `NetLibOpen` + `NetLibSocketOpen` +
  **explicit `NetLibSocketBind`** to an OS-chosen local port before sending anything — an
  unbound UDP socket was found unreliable for receiving replies on some real-hardware
  network drivers (SLIP among them); see
  [[palmos-netlib-udp-client-working-pattern]]. Deliberately **no** `NetLibSocketConnect`
  either — a "connected" UDP socket filters incoming datagrams by strict peer match, and on
  some real-hardware/NAT paths a genuine reply's apparent source can differ just enough to
  be silently dropped; sends pass the destination directly instead (unconnected
  send-to/receive-from).
- **Resolving**: if the Server field parses as a raw dotted-quad IPv4 (`ParseIPv4`), DNS is
  skipped entirely. Otherwise one `NetLibGetHostByName` attempt per tick (retried up to
  `dnsMaxAttempts=3`) — this is the **one genuinely blocking call** in the whole flow
  (no non-blocking form exists on this API), bounded to `dnsTimeoutTicks=5s` per attempt so
  a queued Stop tap is still picked up promptly once it returns.
- **Sending**: builds a 48-byte SNTP client packet (`LI=0,VN=3,Mode=3` — first byte `0x1B`)
  with a fresh random transmit timestamp (`gOrigin`), and `NetLibSend`s it.
- **Waiting**: `NetLibSelect(timeout=0)` polled once per tick (never a blocking receive) —
  this is the phase that used to dominate the wait before being made pollable, and is what
  actually keeps the Device/Era clock rows ticking live and the Stop button responsive
  while a sync is in flight. Each datagram is checked (`ReplyProblem`, since 2026-11-17):
  at least 48 bytes, from the server's address, mode 4 (server), leap indicator not 3
  (unsynchronized), stratum 1-15 (0 = kiss-o'-death), the originate timestamp equal to our
  `gOrigin`, a non-zero transmit timestamp. A failing one is logged ("Ignored reply: <why>")
  and the wait goes on; with no good reply the timeout/retry/give-up logic runs as before
  and the last reason ends up in the FAILED line. Before that check any 48-byte datagram,
  from anywhere, set the clock.

`EventLoop` picks the `EvtGetEvent` timeout dynamically: `evtWaitForever` once the app has
navigated away from `frmMain`, `syncPollTicks` (~200ms) while a sync is active, or
`tickTimeout` (~1s) otherwise — the same bounded-`nilEvent` idiom Sec35 uses for its own
countdown label, reused here to drive both the live clock rows and `SyncTick`.

## 4. Reading the reply — real bugs found and fixed live

`SyncFinish(true)` decodes the 48-byte reply and runs the exact rebase/log/set/publish tail
described in §2.

**Eras and 64 bits (2026-11-17).** NTP's transmit seconds are 32 bits since 1900 and wrap
every 136 years (first on 7 Feb 2036); the era is not sent. `EraOf` picks it: with the plugin
active (`HC_OK`), the era that puts the time nearest the plugin's real date (good to the plugin's
own limit, year 9999, as long as the Palm's real date is within ~68 years); without it, any time
before 2026 counts as the next era (good until 2162 - past 2031 the device clock is re-based anyway
and nothing on the Palm knows the true year). From there everything is 64-bit: time zone plus
daylight saving added in minutes (what `TimUTCToTimeZone` adds) and `dl_secondsToDate`, so
nothing goes through Palm's 32-bit time calls, which stop in Feb 2040. Host-tested (2026..9999,
the Palm's date off by up to 40 days, with and without the plugin) and emulator-tested against
a fake server sending a 2050 time and against pool.ntp.org.

Two real, non-obvious bugs were found and fixed during this build:

- **`DateTimeType.year` is already absolute** — unlike the *packed* 7-bit `DateType.year`
  (years-since-1904) used elsewhere in this suite's date pickers, `DateTimeType` (what
  `TimSecondsToDateTime` fills in) uses plain, already-absolute `Int16` fields. Writing
  `dt.year + 1904` here (as if it were the packed type) produced a garbage year like 3930
  for what should have been 2026. **Two different date-typedef conventions in the same
  SDK family, both named similarly — worth checking explicitly for any future direct
  `TimSecondsToDateTime`/`DateTimeType` use anywhere in this suite.**
- **`FrmCopyLabel` buffer-overflow crash (the big one)** — PalmOS `LABEL` resources
  allocate a buffer sized to the string literal **given in the `.rcp` at build time**;
  `FrmCopyLabel`'s own doc comment says the new label "must not exceed the size of the
  label defined in the resource," but that's easy to miss since the API signature
  (`const Char *`) gives no compile-time hint. The `.rcp` originally declared short
  placeholder prefixes (`"Device:"`, `"Era:"`, `"NTP:"`); the code later wrote much longer
  formatted strings into them ("Device: Aug 19 2026 14:32:07") via `FrmCopyLabel` —
  silent heap corruption, manifesting as an address-error crash reading from ~0x9 on the
  **third** such call, not the first (the first two overflows corrupted adjacent memory
  without immediately crashing, making this genuinely hard to bisect from the crash site
  alone). Root-caused via debug `WinDrawChars` breadcrumbs plus POSE64's `dialog`/
  `backtrace`/`regs` while `blocked_on_ui`. **Fixed by padding the three LABELs' initial
  `.rcp` text to 30 spaces** — see `EraNTP.rcp`'s `LABEL "                              "`
  lines — the same idiom already used elsewhere in this suite (Sec35's
  `lblCountdown`/`lblSession`, CryptoNote's padded blank label ID 1013) but easy to
  overlook on a first pass. **This exact bug class recurred independently in lazyFTP this
  session** (a different app, same root cause) — see [[palmos-frmcopylabel-buffer-size-gotcha]]
  for the reusable writeup and a second real incident's specifics.
- `StrNPrintF` isn't linkable in this SDK/glue configuration (undefined reference at link
  time — not present in sdk-4's headers at all). Swapped for plain `StrPrintF` throughout;
  every call site here has a generously fixed-size buffer, so dropping the explicit bound
  isn't a real overflow risk in practice.

## 5. UI & resources

`EraNTP.rcp` (pilrc), one `FORM ID 1000` (`frmMain`, full-screen, `MENUID 1600`: Network with
Connect /N and Disconnect /D, and the standard Edit menu for the Server field). No
separate resource header — IDs are `#define`d at the top of `EraNTP.c` (Sec35's
convention). Server field + checkbox at the top; three padded comparison `LABEL`s (§4);
a `HASSCROLLBAR` log `FIELD` + matching `SCROLLBAR` (dragging generates `sclRepeatEvent`,
never routed to the field automatically — same as CryptoNote's/Address35's own
field+scrollbar pairs, handled explicitly in `MainFormHandleEvent`); Done/Sync Now buttons
shifted left of the graffiti indicator at `(150,150)`.

**Log field idiom**: rebuilt wholesale on every `LogAppend` call (a handful of KB total, only
on an explicit Sync Now tap — no incremental-edit complexity needed) via a fresh
`MemHandleNew`+`FldSetTextHandle`, freeing the field's previous handle after the swap.
`LogClear()` (called at the start of every `SyncStart`) frees the handle, resets the
scrollbar to 0/0/0, and calls `FldRecalculateField` so a repeated Sync Now shows only the
current attempt's trace, never an accumulating history.

## 6. Manual Network > Connect/Disconnect

Net.lib's open count is refcounted, so `SyncStart`/`SyncFinish`'s own self-contained
per-sync Open/Close pairs don't fight a manual Connect held open here — Connect just lets a
slow real-hardware link (routing/DHCP still settling right after `NetLibOpen`) finish
settling *before* a time-sensitive NTP exchange starts, instead of racing it.
`AppendNetworkInfo` reports own-IP + DNS by probing the known interface creators
(**SLIP first, then PPP**) directly via `NetLibIFSettingGet`, rather than trusting
`netSettingDefaultIFCreator` — on at least one real setup that setting came back all-zero,
which silently resolved to the loopback interface (127.0.0.1) instead of erroring, masking
the real SLIP address. `Disconnect` drains the **entire** open count with
`NetLibClose(refnum, true)` in a loop (a single close only decrements the count and defers
teardown to a lazy "close-wait" state that can leave Network prefs showing "Connected" for a
while — confirmed on a real device) and force-completes any lingering close-wait via
`NetLibFinishCloseWait`.

## 7. POSE64 environment caveat (not an app bug)

POSE64 continuously re-syncs its emulated RTC to the **host machine's** wall-clock time in
the background, silently overriding a `TimSetSeconds()` call within about a second of it
being called — the Device row visibly "snaps back." Real hardware doesn't do this; verified
functionally correct via the actual NTP round-trip + rebase math running (log trace + the
brief correct value before POSE64's own re-sync stomps it), not via the post-sync Device row
remaining changed in the emulator specifically.

## 8. Not yet real-NTP-tested

The real-year->2031-without-active-plugin explicit log branch ("Real year N exceeds 2031 and
no active era plugin found... Rebasing device clock to best-matched year M") is impractical
to trigger against a genuine NTP server (which always returns the true current year) —
verified by code review only, reusing the exact same `dl_reBaseOffset`/`dl_dateToSeconds`
functions already unit-tested elsewhere in this suite (`hackmaster/test/pose_test.py`, 80/80 passing —
see [[hackmaster-y2035-project]]), so confidence is high without a live real-2031+ exercise.

## 9. File map

```
apps/eraNTP/
  EraNTP.c              app core - single file, single segment: state machine, UI, network,
                         era-plugin publish, unsaved-prefs load/store
  EraNTP.def             application Entp + backup (no multiple-code segments)
  EraNTP.rcp              single FORM 1000 + MENU 1600 (Network > Connect/Disconnect, Edit)
  build_eraNTP.sh        build script (single-file build, no multigen/section glue needed)
  icons/ mkicon_eranTP.py  app icon assets + generator
apps/lib/  (shared)      hackcheck.h (HcCheck/Hc_CurrentRealDate), y2k35.h (Y2K35_* feature/
                         pref IDs), datelib.h (dl_reBaseOffset/dl_dateToSeconds - the exact
                         rebase math the era plugin's own control panel uses)
```

No database of its own — reads/writes only the era plugin's Feature Manager slots and its
own unsaved app-prefs blob (`appCreator='Entp'`, id 0).

## 10. UI round (2026-11-18)

- **Main:** Done (1,147,36,12) and Sync Now (41,147,102,12) on the ROM bottom row (y=147 from x=1, 4 px apart); Sync Now takes the rest of the row up to the Graffiti indicator.
- **Graffiti indicator:** its middle lines up with the buttons' middle (y = button y + 1 for 12 px buttons). Full-screen forms put it 4 px from the screen's right edge (x=148), dialogs 5 px from the frame like the buttons (x=143); it keeps 4 px clear of anything to its right and at least 4 px from the item on its left. The glyph is 8 px wide in every state.
