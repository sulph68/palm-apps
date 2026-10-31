# Meshtastic Client (`meshtastic.prc`)

A Palm OS client for a Meshtastic node. The Palm cannot do Bluetooth LE or Meshtastic's protobuf
protocol, so **WiRSa's Mesh Relay mode** holds the Bluetooth link to a paired node, keeps the node
list and the messages, and serves the Palm over the serial line. This app is the Palm side: chats
on the node's channels, direct messages, the node list, the node's own settings, and the WiRSa
side's pairing and background-check setup.

Source: `apps/meshtastic_client/`. Firmware side and the frame list: `~/WiRSa/WiRSa/docs/Mesh-Palm.md`.
All names, numbers and messages below are examples.

---

## 1. What you need

- A WiRSa (ESP32-S3 build) with the Mesh Relay mode (`WIRSA_FEATURE_MESH`, on by default in the
  `improved` build). Main-menu entry **`Mesh[T]astic`**.
- A Meshtastic node in Bluetooth range, with Bluetooth on. A node that shows a PIN on its screen, a
  fixed PIN (123456 unless changed) or no PIN at all all work.
- A Palm with a 160x160 screen (tested on Palm OS 4.1). The X-Master / X-Y2K35 plugin is optional (see §8).
- `meshtastic.prc` installed (HotSync, or the SASD / virtual card install folder).

The Palm links to WiRSa with the serial cable (or the Palm's serial port wired to WiRSa), the same
line WiRSaControl, BitWarden Sync and CalDAV Sync use.

## 2. First use

1. **Preferences** (Settings menu): pick this Palm's **Baud** (57600 is a good start; the list is 9600,
   14400, 19200, 28800, 38400, 57600, 115200) and tick **Screenless Mode:** if you want the app to
   find WiRSa and start Mesh mode itself. Without it, start Mesh mode from WiRSa's menu first.
2. **Node > Connect.** The app finds WiRSa, secures the link (about half a second) and agrees the speed.
   The screen stays usable from then on: everything after that is fetched in the background and the
   status line says **Updating...** until it has settled.
3. **Settings > Pair Device...** (first time only): **Scan**, pick your node in the list, **Pair**. If the
   node asks for a PIN, a keypad opens ("PIN shown on the node"). The pairing is kept by WiRSa; it
   reconnects to the node by itself from then on.
4. Open **Channels** on the main screen and tap a channel to chat.

## 3. The main screen

Top right is a pop-up with three views: **Channels**, **Direct Msg**, **Nodes**. The title is the paired
node's name. Under it, the status line and the node's battery (a bar and a percentage).

Status line:

| Text | Meaning |
|---|---|
| Not connected | no session with WiRSa (use Node > Connect) |
| No node paired | WiRSa has no node paired (Settings > Pair Device) |
| Updating... | linked; settings, channels, nodes or messages are still arriving |
| Linked 10:21 pm | linked; time of the last update |
| Disconnected | WiRSa lost the Bluetooth link to the node (it retries by itself; the line returns to Linked when it is back) |
| Link failed | the node refused or the pairing is broken |
| (an error text) | the last request failed; for example "WiRSa stopped answering during the sync" |

**Channels** lists the node's channels in slot order with the slot number in front:

```
0: Primary           - (2)   10:21 pm
1: Family            - (1)   9:58 pm
2: Hikers
```

A row with unread messages is bold all through (name, `- (n)` count, time). The time is the clock
today, `Yest.`, the weekday within a week, else the date in your Date preference.

**Direct Msg** lists direct-message conversations, newest first. **Nodes** lists the nodes the node
has heard: favourites first, then by name, with when each was heard. Type in **Find:** to narrow the
list (name or short name); **Clear** empties it. Tap a row to open it.

## 4. Chatting

The message area shows the messages with a heading for each day (`Sat Oct 24, 2026`). Each message
starts with the sender's short name in bold, then the text, with the time at the right end of the last line. Your
own messages start `Me:`. A node that has not told its name yet shows the last four characters of its id
(`e536:`).

Marks before the time on messages you sent: `?` still going out, `!` failed, nothing once it is
acknowledged.

- **Write** in the three-line field at the bottom; the counter under the box shows `045/233` (a message
  can be 233 bytes). **Send** sends it.
- **Scroll** with the scrollbar, or the hardware page buttons (and a 5-way rocker, thumb wheel or jog dial).
- **Tap** a message to select it, tap another to extend to a range, a third tap starts over. **Edit > Copy**
  copies the selection; **Chat > Select all** selects everything.
- **Double-tap** a message (or Chat > Message details) for the whole message in a bordered form: who, where,
  when, its status and, for a failed one, the reason in words.
- **Pop-up at the top right:** *Main Menu* first (back to the main screen), then the channels in slot order
  (`0: Primary`, `1: Family`...) or the 8 latest direct messages. The list is only as long as it needs to be.

Chat menu:

- **Chat:** Node info, Reply privately (a direct message to whoever spoke last), Select all, Message details,
  Clear messages.
- **Send:** Alert (bell) sends `:bell:` (with your text after it, if you typed some); Request node info, Request
  position, Request telemetry, Traceroute; Share position... and Waypoint... (degrees such as 1.3521 and
  103.8198). Replies come back as messages from the other node.

## 5. Nodes

A node's card (tap it in Nodes, or Chat > Node info): long name, short name, id (`!a1b2c3d4`), last heard,
SNR and hops, battery, hardware, role, firmware (your own node), uptime, channel and air utilisation, the
number of direct messages, and the position. The **Favourite** box sets the node's own favourite flag.
**DM** starts a direct message. Tapping the *Chats* row or the position row acts on them.

## 6. The node's settings (Node > Settings...)

A card with one row per part: **Owner**, **LoRa**, **Role**, **Bluetooth**, **Channels** and **Node clock**. A
tap on a row opens its form and changes a copy; **Save** sends the changed parts to the node in one go. **Reboot
Node** restarts it. **Node clock** shows the node's time against the Palm's and sets the node's clock from the Palm.
The page reads the node's settings first, which can take 10 to 40 seconds on a node that has just connected: a
dialog says so and can be cancelled.

## 7. Menus

Main screen:

| Menu | Items |
|---|---|
| **Node** | Sync Now, Clear Messages..., Settings..., (line), Connect, Disconnect |
| **Settings** | Pair Device..., (line), Preferences... |
| **Edit** | the standard Palm edit items |
| **About** | Meshtastic Client |

- **Sync Now** asks WiRSa to take what the node holds right now and then updates the Palm.
- **Clear Messages...** deletes on the Palm: all, or those older than 1 day, 3 days, 1 week or 1 month. It says how many
  would go and asks first. WiRSa keeps its own copy (clear that from Pair Device's menu).
- **Pair Device...** (a bordered form): *Paired:* node name, the scan list, **Scan**, **Pair**, **Unpair**, **Mode:**
  *Session* (Mesh mode ends with the app) or *Background* (WiRSa checks the node by itself every *Bg Chk Every:* 1, 5,
  15, 30 or 60 minutes and hangs up between checks, so messages are waiting when the app opens). *Bg Chk Every*
  only matters in Background mode, and only while no Palm app is open. The form's menu clears WiRSa's own message store.

## 8. Dates and clocks

- **Message and node times** come from the node (through WiRSa) and do not depend on the Palm's date.
- **Today, Yest., the weekday/date choice, Linked hh:mm, the time on messages you send, and Clear Messages by age** use
  the **Palm's own date**. WiRSa's clock is not used (it is not reliable).
- Dates are drawn with the era-aware formatters in `apps/lib/era.c`, in your Palm's Date preferences, so years
  beyond 2031 print correctly. With the **X-Master / X-Y2K35** plugin installed and *enabled* the real year comes from
  the plugin; if the plugin is missing or switched off the clock is used as the Palm reports it (correct to 2040).
  Messages stored while the plugin was off keep the stamps they got then.

Tested in the emulator at Oct 25, 2100 with the real X-Master and X-Y2K35 (enabled and disabled), and with the
clock at its real date.

## 9. What the app keeps, and how it updates

- A copy of the channel names, node settings, nodes, conversations and messages is saved in the app's preferences
  (creator `McHt`, version 5) every time something new arrives, and read back at launch, so the lists are there
  before you connect. A WiRSa restart does **not** clear it: the app fetches again from the start and skips the
  messages it already has (same sender, time and text).
- Limits: 32 nodes, 16 conversations, 200 messages (the oldest go first, 12000 bytes of text), 8 channels, 400 lines
  per chat.
- **Nothing blocks the screen.** After Connect, requests are sent and the answers are read in the event loop, a few
  frames at a time, never while the pen is down. The app asks again every 3 seconds for a short while until the channel
  count has stayed the same three times, because the node hands its channels to WiRSa slowly. WiRSa tells the app about
  news (a message, a delivery, a link change) itself, so nothing is polled.
- Only Connect (finding WiRSa and starting Mesh mode), Settings, Pair/Scan and Clear WiRSa's store wait for an answer,
  each with a progress dialog that can be cancelled.

## 10. Troubleshooting

| Symptom | Check |
|---|---|
| "WiRSa did not answer" on Connect | cable and rate; try Preferences > Baud 57600 or 19200; Options (WiRSaControl) > Find WiRSa |
| Status stays "Updating..." | the node may still be sending its list to WiRSa; wait a minute, or use Node > Sync Now |
| "Disconnected" | WiRSa lost the node (out of range, off, or restarting); it reconnects by itself |
| Messages missing | Node > Sync Now; in Background mode the node is only reached every few minutes |
| A message shows `!` | double-tap it for the reason (for example no route, or no public key for a direct message: send Request node info first) |
| Fewer channels than the node has | give it a minute; Settings > Channels shows the node's own list |
| Failed frames at 38400 | the Palm's 38400 serial timing is irregular on some units; use 57600 or 19200 |

## 11. Build notes

- Build with `make` in `apps/meshtastic_client/`; after a change to a header or a segment tag use `make clean && make`,
  the Makefile has no header dependencies.
- Two code segments: the main one is full at 32 KB, so `Link.c`, the framing helpers and the crypto are in `sec001`
  (`MC_SEC`) and the screens (`Views.c`, `Chat.c`, `Node.c`, `Relay.c`) in `sec002` (`MC_UI`). No const tables and no
  32-bit multiply or divide in secondary segments (`mc_seg.h`). `Data.c` (main) has the helpers they need.
- Files: `MeshChat.c` (event loop, status line, preferences), `Link.c` (the WiRSa session: open, sync, settings, cache),
  `Data.c` (tables, dates, labels), `Views.c` (main lists), `Chat.c` (conversation), `Node.c` (settings pages),
  `Relay.c` (Pair Device), shared `../lib/relayproto.c`, `relaysec.c`, `relaywire.c`, `wirsactl.c`, `era.c`, `datelib.c`.
- Layout rules: buttons 4 px from the bottom, left and right; no italics.
- Testing without hardware: the Palm OS emulator with a fake WiRSa speaking the Mesh frames (see `Mesh-Palm.md` for the
  frames). The emulator's serial line sometimes stalls a sync; the real line does not.
