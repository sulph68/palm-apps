# WirSaControl — controlling WiRSa from the Palm (design spec)

Status: **built and in use (2026-10-16)**: tested on a real Palm and WiRSa, and in POSE against a
Python stand-in; firmware compiled in all three configurations (§6). Firmware work is on the WiRSa
`improved` branch only; `main` is untouched. Line references are to `improved` as of 2026-10-15 and
may have moved a little since.

## 1. Goals and agreed decisions

- WiRSa can be built **screenless** (no OLED, no buttons) and driven entirely from the Palm.
- There is **one serial port**: the Palm line carries the menu, the control link and every mode's
  data. The USB console is for debug only, so its `+++` exits don't count.
- A new Palm app, **WirSaControl.prc**, shows status, sets up WiFi and settings, and starts modes.
- **CalDAV Sync and Bitwarden select their WiRSa mode themselves** when their new
  "Screenless Mode:" preference is on (default off = today's behaviour).
- How each mode ends:

| Mode | How it ends |
|---|---|
| Config, WiFi, Utilities, Status, FTP | Never leave the main menu: they are control commands |
| HotSync | Exit Timers (§2.7): back to the main menu if no HotSync starts within 45 s of entering, or 15 s after a sync (both settable); EXIT command or the BOOT button while still waiting |
| CalDAV / Bitwarden | Exit Timers (same defaults); EXIT command once the Palm app has closed the session |
| SLIP | SLIP timeout: back to the main menu after 1-5 min (default 5) with no traffic; or reset WiRSa |
| SASD | SASD timeout: back to the main menu after 3, 5, 10 or 15 min (default 3) with nothing heard from the Palm; or reset WiRSa. There is still no in-band exit: the Palm's SASD driver holds the serial port, so WiRSaCtl cannot send one |

- After a WiRSa reset during SASD, the user switches the Palm off and on: the SASD driver only
  re-checks the card on wake (`Slot.c:437–465`), then closes the port when the ping fails.
- Menu letter keys stay usable on the Palm line, protected by a **burst guard** (§2.5).
- Only password-carrying commands are encrypted (reusing `relaysec`).
- **Factory Reset always needs confirmation**, in every build and on every path (§2.6).

## 2. Firmware

### 2.1 Build defines (`globals.h`)

| Define | Default | Effect |
|---|---|---|
| `WIRSA_FEATURE_PALMCTL` | 1 | Control link, burst guard, mode exit hooks |
| `WIRSA_SCREENLESS` | 0 | Sets `WIRSA_HAS_DISPLAY 0` and `WIRSA_HAS_BUTTONS 0` |
| `WIRSA_FEATURE_BOOT_HOTSYNC` | 1 | BOOT button starts HotSync (§2.9) |
| `WIRSA_FEATURE_DUAL_BAUD` | 1 | Menu at 9600 after boot, SPEED, second receiver (§2.10). Needs PALMCTL. 0 = menu at the configured baud, as before |

- `WIRSA_HAS_DISPLAY 0`: `display` becomes a `NullDisplay` class (derives from `Print`, `write()`
  returns 1) with no-op versions of the 17 methods used (`print`, `setCursor`, `drawLine`,
  `fillRect`, `setTextColor`, `display`, `write`, `clearDisplay`, `setRotation`, `setTextWrap`,
  `setTextSize`, `setFont`, `drawRect`, `println`, `drawPixel`, `cp437`, `begin`). The six files that
  declare `extern Adafruit_SSD1306 display;` themselves (`display_menu`, `wifi_setup`, `slip_mode`,
  `network`, `sasd`, `diagnostics`) use the declaration in `globals.h` instead. `setup()` skips
  `display.begin()`, its `for(;;)` lock-up and the 2 s splash. The SSD1306/GFX libraries drop out
  of the link.
- `WIRSA_HAS_BUTTONS 0`: `readSwitches()` sets all four `BTN*` to false. Required: SW1–SW4
  (GPIO 36–39 on the ESP32-S3) are plain `INPUT`s relying on the button board's pull-ups, so
  without it they float and read as random presses.
- With `WIRSA_FEATURE_PALMCTL 0` and `WIRSA_SCREENLESS 0`, the firmware must behave exactly as today.

### 2.2 Screenless replacements for button-only waits

| Place | Today | Screenless |
|---|---|---|
| `waitForWiFiConnection()` (`network.cpp:384`) | "ENTER: Retry / BACK: Exit" loop | Return false after the first grace window |
| `charEntryPrompt()` (`display_menu.cpp:704`) | BACK cancels | ESC (0x1B) cancels too (all builds) |
| Information screen (`diagnostics.cpp:197`) | BACK or ESC | Unchanged (ESC already works) |
| Mode loops (BACK exits) | BACK | Never fires; exits per §1 table |

Control-link mode entry never calls the blocking prompt at all (§2.4, ENTER_MODE checks first).

### 2.3 Control frame format and 2.4 Commands

**The control link's frame format, every command, its request and reply bodies, and the rules for what each mode accepts are defined in one place, in the WiRSa repository: `docs/protocol/Protocol-wire.md`**
(section 7 for the link, section 12 for the commands). This document does not repeat them: change a command in the firmware (`palm_ctl.cpp`), in the Palm code (`apps/lib/wirsactl.c`) and in that document together.

What belongs to this design rather than to the wire:

- **Rates.** The main menu listens at its session rate, `menuBaud()` (9600 after boot, then SPEED, §2.10); CalDAV, BitWarden and HotSync idle at 9600.
- **Waits on the Palm.** `WIFI_SCAN` is given 20 s, `WIFI_SET` 40 s.
- **Order.** `SECRETS_SET` is sent before `CFG_SET`.
- **Card removal.** The Refresh and Find buttons show a pulled card as none and a card put back as present; the firmware checks the card at the main menu only.
- **Encryption.** The Palm sends the key-exchange frames first, behind the lead byte; WiRSa makes its key pair lazily at the first key exchange in the main menu and clears the session after the one encrypted command.

### 2.5 Burst guard (Palm line only)

Applies to single-letter **menu choices** read from `PhysicalSerial` in: `mainLoop` (418),
`baudLoop` (555), `orientationLoop` (612), `settingsLoop` (818), `ftpServerLoop` (943),
`diagnosticsLoop` (`diagnostics.cpp:276`), `wifiSetupLoop` (`wifi_setup.cpp:242`).

- A key counts only if the line was quiet for ≥100 ms before it, and in the next 100 ms nothing
  but CR/LF arrives (so a terminal that sends `D\r` still works).
- Exception: `S` followed by SLIP END (0xC0) counts, and the packet is left on the line. A Palm
  network login script sends `S` to start the SLIP Gateway and the Palm's SLIP traffic can follow
  inside the window; SLIP mode doesn't drain at entry, so it gets the packet.
- Otherwise the key is dropped and the line drained until 100 ms of quiet.
- SYN (0x16) skips the guard and starts a control frame.
- **Not** applied to text entry, so pasting works: `charEntryPrompt` (776), `wifiPasswordLoop`
  (`wifi_setup.cpp:481`), the Information screen's ESC (`diagnostics.cpp:219`).
- The USB console is not guarded.
- Implemented once, as a key-reading helper in `serial_io.cpp` that returns the key and its source.

### 2.6 Factory Reset confirmation (all builds)

- Serial `F` in Config: prints "Factory reset? Press Y to confirm"; the next guarded key must be
  `Y`/`y`, anything else cancels.
- Buttons: a confirm screen, "ENTER: confirm / BACK: cancel".
- Control link: FACTORY_RESET with the `"RESET"` body; WirSaControl shows a Palm confirmation alert
  first.
- Reboot: the serial `R` stays unconfirmed; WiRSaCtl's Reboot WiRSa... asks first (no shortcut), since it cuts off a running HotSync, SASD card or SLIP link.

### 2.7 Mode exit hooks

Each hook peeks at the next byte at a frame boundary; if it is SYN, it reads a control frame
instead of a mode frame.

| Mode | Where | Accepts |
|---|---|---|
| CalDAV | `caldavRelayLoop()` before the `cddvWifiReady` early return (`caldav_relay.cpp:3003`) | PING, STATUS; EXIT only while at `CDDV_BAUD_IDLE` (no negotiated session) → reply, `exitCalDavRelayMode()`, `msgFlag = true`, `mainMenu(false)` |
| Bitwarden | `bitwardenRelayLoop()`, same place (`bwWifiReady` check) | Same, with `BW_BAUD_IDLE` |
| HotSync | `hotsyncLoop()` in `HS_WAITING`, before `hsPollForWake()` (its scanner swallows bytes until 0xBE EF ED, `hotsync.cpp:347`) | PING, STATUS; EXIT → reply, `hsExit()` |
| SLIP | — | No hook; the SLIP timeout (§2.7 end) or a reset |
| SASD | — | No hook; the SASD timeout (§2.7 end) or a reset |

Every relay mode (CalDAV, Bitwarden, Mesh) passes its highest Palm->WiRSa frame type (`RelayMode::Spec.maxPalmType`, Protocol-wire.md 5.6 and 6.2); the hook first drops any byte
that can't start one of the mode's frames (type 1..max, with or without 0x80, or 0x70-0x74).
Without it, junk (e.g. a PING sent at the wrong rate) is taken as a frame header and the mode's
3 s byte timeout swallows the real request behind it (found on hardware). EXIT is accepted at
the idle rate within 2%, since `baudRate()` returns the rate the UART actually achieved.

Other requests in a mode get `ERR BUSY` with the mode code, except ENTER_MODE for the mode itself
(just `ok`).

**Exit Timers (2026-10-16, `mode_timer.h`)** - HotSync, CalDAV Relay and Bitwarden Relay go back to
the main menu by themselves, however they were started: no session within the *unused* time of
entering (default 45 s), or the *after* time once a session ended (default 15 s). A session starts
with the mode's own protocol (control-link PINGs don't count) and ends at its BYE, at the 15 s
silence that already drops a relay back to 9600, or when a HotSync finishes; a new session cancels
the countdown. Both are seconds, 0 = never (HotSync then listens for the next HotSync), set in
Config > Exit Timers or WiRSaCtl Setup > Exit Timers... (CFG_GET/CFG_SET carry them in the optional
tail). EEPROM 440 (marker 0xA5) / 441 / 442. This replaced an after-session flag on
ENTER_MODE: on hardware the Palm's own EXIT after a long Bitwarden sync was lost 2 times out of 3.
The relay apps still send a flags byte (ignored) and still EXIT as a fallback.

**SLIP timeout (2026-10-16)** - SLIP Gateway goes back to the main menu (gateway stopped, as with
BACK) after 1, 2, 3 or 5 minutes (default 5) with no traffic either way: bytes from the Palm, or
frames WiRSa sends to it. Counted from entry, so a Palm that never connects also times out. Set in
Config > Exit Timers (third prompt) or WiRSaCtl's Exit Timers form ("SLIP Timeout:" popup, hidden
when the firmware doesn't send it). EEPROM 443; the third byte of the CFG tail.

**SASD timeout (2026-10-28)** - SASD goes back to the main menu (as with BACK) after 3, 5, 10 or 15 minutes (default 3; anything else in
the EEPROM reads as 3) with no byte from the Palm and no operation under way. Counted from entry, so a Palm that never connects also times
out. Measured first on hardware: after one card access the line was totally silent for 11+ minutes, with the Palm awake and idle and then
switched off: no polling and no noise bytes, so silence is a reliable signal. A Palm that finds the card gone on its next access gives the
no-card alert, and SASD is started again from WiRSa's menu or from WiRSaCtl. Set in Config > Exit Timers (fourth prompt) or WiRSaCtl's Exit
Timers form ("SASD Timeout:" popup under the SLIP one, hidden when the firmware doesn't send it). EEPROM 447; the last byte of the CFG tail
(after the deep sleep delay).

**The mode's own reader must never see a control frame** (found on hardware, 2026-10-16): each
loop checks the hook once at the top, then reads its own frames. If a second control frame was
already queued (PINGs pile up while CalDAV/Bitwarden generate their key right after a reset), the
mode's reader took its 0x16 as a frame type, read a bogus length, and its 3 s byte timeout then
swallowed every later PING - the Palm reported "didn't switch" while WiRSa sat at "Waiting". Fix:
CalDAV and Bitwarden return before their reader, and HotSync before its WAKE scanner, whenever the
next byte is 0x16; the hook takes it on the next pass.

### 2.8 Screen build with the control link (both at once)

`WIRSA_FEATURE_PALMCTL` does not depend on `WIRSA_SCREENLESS`: the normal screen build has the
OLED, the buttons and the control link together. The firmware handles one input at a time, so
buttons and frames never run at the same moment; ENTER_MODE calls the same `enterXMode()` the
buttons do, so the OLED shows the mode as usual. For this to work in every menu state:

- **Any menu screen** (main, Config and its submenus, Utilities) answers a control frame, not
  just the main menu: the SYN check lives in the shared key-reading helper (§2.5) that every
  menu loop uses. PING/STATUS/CFG_GET are answered in place; commands that change something
  (ENTER_MODE, CFG_SET, SECRETS_SET, WIFI_SET, FTP) first return to the main menu
  (`msgFlag = true; mainMenu(false)`).
- **Text entry** (`charEntryPrompt`, `wifiPasswordLoop`): a SYN byte cancels the prompt (as ESC
  does) and discards the half-typed value, so the frame's bytes are never typed into the field.
  The rest of that frame arrives as a burst and the menu's burst guard drains it; that one
  request goes unanswered, and the Palm's next request is handled normally.
- **OLED refresh**: after a command that changes settings or WiFi, redraw the current screen;
  PING/STATUS don't redraw.
- BACK on the device during a Palm-driven session behaves as today (leaves the mode).

### 2.9 BOOT button = HotSync (`boot_button.cpp`, 2026-10-16)

For a Palm that has lost its apps (hard reset) on a screenless WiRSa: press WiRSa's **BOOT**
button, then tap HotSync on the Palm; HotSync Install puts WiRSaCtl and the rest back from the SD
card.

- `bootButtonPoll()` runs once per `loop()` pass and never blocks. A short press (50 ms - 3 s) in
  any menu screen starts HotSync through `startHotsyncFromMenu()`, the same function as the H key
  (SD check, then HotSync). Another press while HotSync is still waiting leaves it. Other modes
  ignore the button.
- Status LED (`LED_PIN`, active-low, otherwise the WiFi indicator): 3 slow blinks (1 s on, 1 s
  off) = HotSync started or left; one 2 s blink = not started (no disk for the Disk setting). Then back to the WiFi
  state.
- Press it only after WiRSa has started: held during reset, GPIO0 selects the ESP32's download
  mode.

### 2.10 Menu at 9600, Palm-chosen speed (`WIRSA_FEATURE_DUAL_BAUD`, 2026-10-18)

Problem: Palm A runs 115200, Palm B only 57600, one WiRSa. With the menu at the configured rate
(115200), B could never reach the menu, WiRSaCtl or Screenless Mode.

- **Session rate:** the menu runs at 9600 after every boot (`homeBaud()`; the configured rate if
  that is lower). `menuBaud()` is the menu's current rate; nothing is saved. `setup()`, every mode
  exit (HotSync, SASD, CalDAV, Bitwarden), the Config baud screen and CFG_SET reopen the line at
  `menuBaud()`. SLIP Gateway starts at it.
- **SPEED (0x4C):** WiRSaCtl asks for its Palm's Pref Baud; the menu moves there (any standard
  rate, `setSpeed()`/`setMenuRate()`).
- **Configured baud = maximum** for the modes' own negotiation only (HotSync, SASD, CalDAV,
  Bitwarden still start at 9600 and agree on the lower of the Palm's rate and it). Changing it
  doesn't move the menu (`menuBaudReset()`). Config shows it as "Baud Rate (Max)" / "MAX BAUD
  RATE"; WiRSaCtl Setup as "Max baud:".
- **Second receiver:** while the menu runs faster than 9600 and WiRSa is in its menus, UART1
  listens receive-only on the Palm RX pin (GPIO16, no TX pin: UART1's default TX on the S3 is
  GPIO17, the Palm TX line) at 9600 (`listenSync()`, from `palmCtlPoll()` each `loop()` pass). A
  control frame with a good checksum, or a single printable key passing the burst guard, decoded
  there moves the menu to 9600 (`heardOnListener()`), and the frame or key is then handled as
  usual. Off at 9600 and in every mode. When the main receiver handles a good frame, the second
  receiver's copy (garbled) is discarded.
- **Only SPEED moves the menu up.** A first version also listened at the maximum while the menu
  was at 9600 (so a login script or terminal at the maximum worked after a reboot). On hardware a
  57600 Palm's Find sweep sent a 115200 PING that decoded cleanly there, the menu went to 115200,
  and that Palm couldn't follow. Removed: right after a boot only 9600 works for senders that
  don't negotiate.
- **Display:** the header's top-right rate shows `menuBaud()`; the main menu is redrawn when the
  rate changes (inside `setMenuRate()`). A try at deferring that repaint until the line was quiet
  (2026-10-19) sent the text menu out on the Palm line while WiRSaCtl was switching rate; Find
  WiRSa and dial-up SLIP became unreliable on hardware, so it was reverted. Utilities → Information: "Baud: 115200 now 57600" while the menu runs at a
  different rate from the maximum.
- Cost: about 1.3 KB of flash. Burst guard factored into `guardKey()` for both receivers.

### 2.11 Rates, boot flush and script speed changes (2026-10-24, `improved`)

- **The seven Palm rates** 9600, 14400, 19200, 28800, 38400, 57600 and 115200 are the only ones WiRSa offers (Max baud in
  Config and WiRSaCtl, `bauds[]`, the second receiver and the autobaud detector). The wire numbering of the rate
  (CFG_GET `baudIdx`, SPEED) keeps the old values 4 to 8 for 9600, 19200, 38400, 57600 and 115200 and adds 9 = 14400 and
  10 = 28800, so older apps and firmware still work. WiRSa's saved baud format has its own mark (`0xBA` seven rates;
  `0xB9` the older five are mapped on read). BitWarden Sync, CalDAV Sync, WiRSaControl and the Meshtastic client use
  the same list (their prefs were versioned and migrate the old index).
- **Boot flush:** after every reset or wake WiRSa discards what arrives on the Palm line for 1.5 s before it listens
  (`setup()`), so line noise from a Palm that is still starting cannot select a mode.
- **Autobaud at the main menu:** a login script that sends `SETBAUD`, waits for the menu's `BR` request and sends the
  rate (for example `BR38400`) moves the menu to that rate at any menu speed; the detector measures the 'S' of
  `SETBAUD` by pin interrupt (edge times), tolerates the Palm UART's irregular bit timing at 38400 and 28800, and
  the BR line confirms it. When the script ends (10 s of silence) the menu goes back to the speed it had. Spec
  and log lines: `~/WiRSa/WiRSa/docs/README-Palm.md` ("Setting the speed from a login script").

## 3. Palm side

### 3.1 `apps/lib/wirsactl.c` / `.h` (built)

Built on `relayproto` (`RpOpen`, `RpSetBaud`, `RpFlushInput`, `RpSendFrame`, `RpRecvFrame`),
`relaywire` and `relaysec`. `relayproto` gained two switches, both off by default so CalDAV Sync and
Bitwarden are unchanged: `gRpLeadByte` (send it before each frame; on receive, skip everything up to
it) and `gRpAllowPlain` (plaintext non-KEX frames allowed while the link is not secured). It also
records `gRpOpenErr`, SrmOpen's result.

```c
UInt8 WCtlBegin(Boolean sweep); /* PING at the last rate, 9600, the menu rate; then WCtlFind if sweep */
UInt8 WCtlBeginFind(void);  /* WCtlBegin with the sweep (Options > Find WiRSa) */
void  WCtlEnd(void);        /* close the port, switches off */
UInt8 WCtlFind(void);       /* PING at 9600, 115200, 57600, 38400, 28800, 19200, 14400 */
void  WCtlSetBaud(UInt32 baud);
UInt8 WCtlRequest(UInt8 type, const UInt8 *body, UInt16 len,
                  UInt8 *reply, UInt16 cap, UInt16 *replyLen, UInt32 timeoutTicks);
UInt8 WCtlSecure(void);     /* RpSecure, for WIFI_SET / SECRETS_SET */
void  WCtlUnsecure(void);
UInt8 WCtlSetSpeed(UInt32 want, UInt32 *got); /* SPEED, then follow and PING; on failure back to 9600 */
UInt8 WCtlEnsureMode(UInt8 mode, UInt32 menuBaud); /* relay apps' Screenless Mode, §3.3 */
UInt8 WCtlLeaveMode(UInt32 menuBaud);
```

`WCtlBegin(sweep)` first tries the rates WiRSa can be listening at: the last one, 9600 (the modes)
and WiRSa's menu rate (from its last PONG, kept in the app's prefs). It then tries every rate (9600, 115200, 57600, 38400, 28800, 19200, 14400) only
if `sweep`: the app passes true when WiRSa's menu rate isn't known yet or WiRSa should be in its
menu (where the burst guard drops the junk), and false after it started a mode, since a PING at
the wrong rate reaches a running mode as junk. Options > Find WiRSa always sweeps
(`WCtlBeginFind`). Found on hardware: without the sweep, a fresh install whose WiRSa menu ran
above 9600 was never found (each 9600 PING arrived as ~20 junk bytes and was dropped).

Results: `wctlOk`, `wctlNoReply`, `wctlPortBusy`, `wctlErrReply` (with `gWCtlErrCode`/`gWCtlErrMsg`),
`wctlBadReply`, `wctlSecFailed`, `wctlModeNotUp` (`WCtlEnsureMode` only). A PONG only counts if its nonce matches. PING waits 0.5 s plus the
time for 64 bytes at the rate. The port is never held between operations, so the SASD driver,
HotSync and the relay apps can open it.

### 3.2 wirsacontrol.prc (built: `apps/WiRSaControl/`)

Launcher name "WiRSaCtl" (title and About: "WiRSa-Palm Control"), creator `'WrSa'`, single code segment (~17 KB). Layout is in
`WiRSaControl.rcp` (hand-editable, labels colon-aligned with `RIGHT@`; IDs in `WiRSaControlRsc.h`); mode names, mode guidance and the
SASD/SLIP confirmations are STRINGTABLEs there. Launcher icons: `icons/wirsacontrol_large.png`
(22×22) and `wirsacontrol_small.png` (15×9), converted to 1-bit `.bmp` for the build. Build output
goes to `build/`.

- **Main** (full screen): Mode (normal font), WiRSa (baud, firmware, "no screen"), WiFi, IP, the disk
  row, FTP, and a 3-line guidance area (strMainNotes) when WiRSa can't be reached. Buttons Refresh / WiFi / Modes / Setup.
  Options menu: Find WiRSa, Reboot WiRSa... and Factory Reset... (each with a confirmation alert), Format SD Card...
  (`Format.c`: frmFormat via FrmDoDialog, the "Copy the SASD install set" box ticked by default; FORMAT_SD `0x4F`,
  Protocol-wire.md §12, waits up to 240 s; SD card only; the virtual card is never formatted on request, anywhere), Preferences...;
  Help menu: System (the same form, titled WiRSa System, showing the SYSINFO text; Refresh only), Show Logs (frmLog: WiRSa's session log, newest at the bottom, Refresh / Clear, Clear asks first), About.
  - `frmLog` serves both Help > System and Help > Show Logs (`gShowSys`). The text block shown in
    its field (`gLogH`) is already gone once the form is deleted (the field frees it), so the next open must not
    free it again: `SysOpenFromMain` / `LogOpenFromMain` just reset `gLogH` (opening either page a
    second time crashed with "Invalid chunk ptr" before 2026-11-15). The Help > System menu case
    also `return true`s now instead of falling out of the switch.
  - The disk row follows the Disk setting (`MainFillSd`, `gSdVirtual`; the label is two objects,
    `lblMainSd` "SD:" and `lblMainVsd` "V. SD:", one shown at a time by `MainSdLabel`, because a
    right-aligned label cannot change its text and keep its alignment). STATUS `sd` (0 unknown, 1 card,
    2 none, 3 virtual) and `sasdDisk`: SD card setting: used/total or "Not present" or "Unknown";
    Virtual: `V. SD:` with the virtual card's used/total, "not formatted", or "Not present"; Auto: "Card
    <used/total>", "Virt <used/total>", "Not found" or "Unknown". Firmware without `sasdDisk` gets the
    old text ("Present ..." / "Not present" / "Unknown"). The text is used/total, not free/total.
  - The main screen asks WiRSa again every 15 s on its own (only while it is the active form; no
    busy box, never the full sweep). When WiRSa can't be reached the mode reads
    "<last mode> (last known)", or "Not found" if WiRSa was never found; the guidance says why
    (SASD/SLIP leave by themselves after their quiet timeout or need a reset, port in use, or no answer - asking again every 15 s).
- **Modes**: "Select Mode:" popup (Main Menu, HotSync, SASD SD Card, SLIP Gateway, CalDAV Relay,
  Bitwarden Relay) preselected to the current mode, 3-line guidance under it; "FTP Server:"
  Start | Stop push buttons acting at once, with Started/Stopped under them; Start / Cancel.
  Start on SASD or SLIP asks to continue first. "Main Menu" + Start sends EXIT; another mode
  while one is running sends EXIT, waits 0.5 s, then ENTER. Start always ends back on the main
  screen. If WiRSa confirms - or the reply is lost after the request went out (it most likely
  switched at once) - the main screen shows "<mode> (last known)" ("(last)" if that doesn't fit)
  without asking WiRSa (the mode is still starting up); the 15 s refresh then shows the live mode.
  A refusal (e.g. no WiFi) or not reaching WiRSa shows an alert, then the main screen.
- **WiFi**: Scan → list (up to 16); tapping one fills SSID; masked password (real text kept
  aside, `*` shown with the character just typed in clear for a second; "Show password" reveals; append/backspace and Paste only); Connect.
  Every masked field (WiFi, FTP, AP) works the same (`MaskKey`/`MaskShow`/`MaskEnd`): replaced field text is wiped before it is freed,
  and on leaving the form (OK, Cancel, Connect, or the app quitting) the field's text is wiped, since FrmReturnToForm frees it unwiped.
- **Setup** (frmSetup): rows at y 18/31/44/57: "Max baud:" popup (9600, 14400, 19200, 28800, 38400, 57600, 115200; the configured baud, the maximum
  for syncs), "Log level:" (Off / Warn / Info / Verbose; on the wire 0 off, 1 info, 2 warn, 3 verbose,
  so an older app's on/off byte still works), "Orientation:" (hidden on a screenless WiRSa), "Disk:"
  (Auto / SD card / Virtual; the CFG `sasdDisk` byte, used by SASD and HotSync; hidden if the firmware
  does not send it), "Sleep:" (deep sleep delay) and "Wifi Pulse:" at y 83 (Always On / 15s / 30s /
  1min / Never; the CFG `wifiPulse` byte, 0..4 in the list order: how the blue LED shows that the WiFi or BLE is up, see
  README-Palm.md "Status LED"; hidden if the firmware does not send it). On a screenless WiRSa the Disk row and the buttons below move up one row
  (13 px) into Orientation's place (`FrmSetObjectPosition` in `SetupHandleEvent`). Under them (y 99 and 114) a 2x2
  grid of 72x12 buttons (4 px between columns, 3 px between rows): "FTP Setup..." (frmFtp: user, pass),
  "AP Setup..." (frmAp: SSID, pass), "SLIP Address..." (frmSlip: four dotted fields, validated),
  "Exit Timers..." (frmTimers). frmFtp/frmAp are 156x80 at y 78, identical except for labels and the
  password limit (32 / 64 characters); each has "Blank password = keep current", an unlabelled Show checkbox right of the
  password (no room for the word) and its own OK/Cancel.
  The password is kept masked in `gMasks[]` (one handler, `CredHandleEvent`, with the form index) until
  Setup's OK sends it by SECRETS_SET; Cancel in a sub-form restores the previous masked text (the copy, `gMaskBackup`, is wiped
  when the sub-form closes). The
  user/SSID text is edited into `gCfg` on the sub-form's OK. Everything is sent with Setup's OK
  (SECRETS_SET first, then one CFG_SET). The layout was checked in POSE64 (Palm III ROM) with a test
  build that skipped the CFG_GET (canned settings), for both the screen and the screenless case.
- **Exit Timers** (from Setup): "Unused mode:" and "After session:" in seconds (0-255, 0 = never),
  "SLIP Timeout:" popup (1/2/3/5 min) and "SASD Timeout:" popup (3/5/10/15 min). The SLIP row is hidden, and the whole form refused, when
  WiRSa's firmware doesn't send those values; the SASD row is hidden when it doesn't send that one. Saved with Setup's OK (one CFG_SET).
- **Preferences** (Options → Preferences...): "Pref Baud:" popup (9600, 14400, 19200, 28800, 38400,
  57600, 115200; default 9600 = don't ask), stored in this app's prefs (version 3), so each Palm keeps
  its own. When a refresh finds WiRSa's menu at 9600 and the firmware has the SPEED flag,
  `SpeedUp()` (`WiRSaControl.c`, from `MainRefresh`) sends SPEED, follows and confirms with a PING
  (`WCtlSetSpeed`). If the confirm fails, it PINGs at 9600 (WiRSa's second receiver brings its menu
  back), then tries the next lower rates (57600, 38400, 28800, 19200, 14400) until one holds. A rate that failed
  (or gained nothing) is remembered for this run only (`gSpeedBad`, RAM): only lower rates are tried until Pref Baud
  changes, Find WiRSa, or the app is started again. (Until 2026-11-17 the failed rate was saved in the prefs as
  `speedSkip` and never stepped down, so a Pref Baud of 115200 that the line could not carry left the menu at 9600 for
  good; the field is now `unused` and cleared at launch.)
  With SPEED firmware, changing Setup's Max baud no longer makes the app assume the menu moved.
  Limit: lowering Pref Baud doesn't move a menu that already runs faster than 9600.
- Tried and reverted (2026-10-19): following Pref Baud whenever the rate differs, and a final
  9600 PING in Find WiRSa. Together with the deferred repaint (§2.10) they made Find and dial-up
  SLIP unreliable on hardware.
- Modal forms sit at the bottom of the screen (y + height = 158), labels colon-aligned.
- Tested in POSE against a Python stand-in (`fake_wctl.py`, scratchpad): every screen, mode
  start/exit, SASD confirm and assumed state, encrypted WiFi and secrets, settings, reboot,
  factory reset, not found.

### 3.3 CalDAV Sync and Bitwarden: "Screenless Mode:"

Built 2026-10-16 (details in `CalDAVSync.md` §10, `Bitwarden.md` §9): `wirsactl.c` gained
`WCtlEnsureMode(mode, menuBaud)` (ping at 9600 and the menu rate, EXIT any other mode, ENTER with a
flags byte the firmware now ignores, then poll at 9600 each second, up to 20 s, until the mode
itself answers) and `WCtlLeaveMode(menuBaud)` (EXIT after the session, retried for about 12 s; a
fallback now that the firmware's Exit Timers bring it back anyway). `WCTL_SECTION` lets an app place
it in a segment (Bitwarden: SEC001). Sync All enters once per batch. If WiRSa can't be reached, or
the mode never comes up, the app says so and skips the sync; cancelling shows no alert.

The original plan, kept for reference (built as planned except where noted in the app docs):

- New preference, default off; saved-prefs version bump with migration to off.
- Preferences form becomes full screen with border: `AT (2 2 156 156) FRAME MODAL`, like
  `frmSlotEdit`/`frmHelp`/`frmTwoFactorKeypad`.
  - Bitwarden: existing rows keep their y; new row y≈122 `LABEL "Screenless Mode:"` + `CHECKBOX`;
    OK/Cancel at y=138.
  - CalDAV: Baud row stays at 107; new row y≈123; OK at y=138.
- When on, before the first HELLO: `WCtlPing` at the app's Baud preference, then 9600.
  - Main-menu PONG → `WCtlEnterMode('A' or 'B')`; on `ERR` show it and stop.
  - PONG with the right mode, or no answer (already in mode, or old firmware) → carry on as today.
- After the final BYE (CalDAV: once, after Sync All), and on a failed sync (best effort):
  `WCtlExitMode()`.
- CalDAV's Google sign-in path (`OAuthRelay.c`) gets the same enter/exit.
- Code space: CalDAV main `.text` 11,448 bytes (room); Bitwarden main `.text` 32,668 bytes (full) —
  Bitwarden's additions go in `sec001` (9,376 bytes used). Recheck after a fresh build.

## 4. Regression plan

1. Build matrix: {screen, screenless} × {PALMCTL on, off}. Screen + PALMCTL off = today's behaviour.
2. Each mode on the device, entered by button, by letter and by ENTER_MODE; exited by BACK, `+++`
   (USB), EXIT command, and reset:
   HotSync (+15 s auto-exit), SASD (+ Palm off/on after reset), SLIP connect/disconnect,
   CalDAV single sync and Sync All (15 s window unchanged), Google sign-in,
   Bitwarden sync with 2FA, FTP running alongside each mode.
3. Burst guard: typed letters work in a Palm terminal (with and without CR); SASD wake pings at the
   main menu start nothing; pasting into SSID/password works; `F` then anything but `Y` cancels.
4. CalDAV Sync / Bitwarden with Screenless Mode off against `main` firmware: unchanged.
   With it on against `main` firmware: syncs as before after the ping timeout.
5. Flash use before/after (budget ~675 KB free); screenless should shrink.

## 5. Open items

- ~~Regression matrix (§4)~~ - tested on hardware by the user (2026-10-19): no major issues.
- Time `RelayProto::secureBegin()` key generation in the main menu (first WIFI_SET): it runs
  inside the first KEX_REQ; the Palm waits up to 180 s for KEX_PUB.
- Launcher category for WiRSaCtl (deferred).
- Seen while doing this, not changed: `baudLoop()` (`display_menu.cpp`) restarts the UART with
  `PhysicalSerial.begin(bauds[serialSpeed], WIRSA_SERIAL_FORMAT)` - no `RXD2, TXD2`, unlike every
  other restart in the firmware. CFG_SET uses the explicit pins.

## 6. As built (WiRSa `improved`, committed)

Firmware:
- `include/palm_ctl.h`, `src/modules/palm_ctl.cpp`: `menuKey()` (burst guard + control frames +
  ENTER_MODE handing its letter to the main menu) and `palmCtlModeHook()`; all commands of §2.4.
- `relay_proto`: `setControlLink()` (lead byte on send, plaintext non-KEX allowed while not
  secured), `isSecure()`, and `readFrameRest()` (readFrame after a caller-checked type byte).
  Off by default; the relay modes never turn it on.
- Seven menu loops read keys through `menuKey()` (main, Baud, Orientation, Config, FTP Server,
  Utilities, WiFi Setup). Factory Reset asks first (buttons: ENTER/BACK; typed: `Y`, 15 s).
- Hooks: CalDAV and Bitwarden (EXIT only at the idle rate), HotSync while waiting.
- `charEntryPrompt`: ESC and SYN cancel. `wifiPasswordLoop`: SYN cancels like ESC.
- `WIRSA_SCREENLESS`: `include/null_display.h`, `WirsaDisplay` typedef in `globals.h`,
  `readSwitches()` all false, no splash, `waitForWiFiConnection()` returns instead of prompting.
- `network.cpp`: `wifiStateCode()` for STATUS.
- Later (2026-10-16): relay junk filter and EXIT rate tolerance (§2.7), ENTER replies at once,
  SD probe after STATUS, control frames never reach a mode's reader, BOOT button (§2.9), Exit
  Timers and SLIP timeout (§2.7).
- 2026-10-18: menu at 9600 after boot, SPEED, second receiver, header shows the current rate
  (§2.10); WiRSaCtl Preferences → Pref Baud (§3.2).

Compile check (scratch copies, PlatformIO 6.1.19, espressif32 7.0.1, `esp32-s3-devkitc-1`,
`min_spiffs.csv`) - all succeed with no warnings from these files:

| Build | Flash | RAM |
|---|---|---|
| `improved` HEAD (before) | 1,287,149 | 102,300 |
| Screen, PALMCTL on (default) | 1,292,545 (+5,396) | 103,972 |
| Screen, PALMCTL off | 1,287,885 (+736) | — |
| Screenless | 1,276,501 (−10,648) | 103,900 |

## 7. UI round (2026-11-18)

- **Main:** Refresh / WiFi / Modes / Setup packed from x=1, 4 px apart.
- **Modes, WiFi Setup, WiRSa Setup, Log** (full height): standard 37x12 buttons 5 px above the bottom; Log text ends 8 px above them; Setup sub-buttons 12 px, 4 px apart. Setup rows stay 13 apart so the screenless shift (-13) and the hidden older-firmware rows still line up.
- **FTP / AP / SLIP / Exit Timers / Preferences:** first row's text 7 px under the title like the row gaps, buttons 7-9 px under the last row, bottom-anchored.
- **Graffiti indicator:** its middle lines up with the buttons' middle (y = button y + 1 for 12 px buttons). Full-screen forms put it 4 px from the screen's right edge (x=148), dialogs 5 px from the frame like the buttons (x=143); it keeps 4 px clear of anything to its right and at least 4 px from the item on its left. The glyph is 8 px wide in every state.
- **Menu:** Options > Preferences gets /R.
