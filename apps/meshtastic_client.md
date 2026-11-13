# Meshtastic Client (`meshtastic.prc`)

A Palm OS client for Meshtastic nodes (up to four). The Palm cannot do Bluetooth LE or Meshtastic's protobuf
protocol, so **WiRSa's Mesh Relay mode** holds the Bluetooth link to the shown node, keeps the node
list and the messages, and serves the Palm over the serial line. This app is the Palm side: chats
on the node's channels, direct messages, the node list, the node's own settings, and the WiRSa
side's pairing and background-check setup.

Source: `apps/meshtastic_client/`. The wire (every frame and its body) is specified in `~/WiRSa/WiRSa/docs/protocol/Protocol-wire.md`, section 9; firmware behaviour is in `~/WiRSa/WiRSa/docs/Mesh-Palm.md`.
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
   find WiRSa and start Mesh mode itself. Without it, start Mesh mode from WiRSa's menu first. **SD History:**
   is described in section 9; leave it unticked unless you want WiRSa's SD card to keep a copy of everything.
2. **Connect** (the button at the bottom left of the main screen, or Node > Connect). A box ("Starting Meshtastic Mode...")
   finds WiRSa, secures the link (about half a second) and agrees the speed. A **Connecting** form then holds the
   screen while WiRSa connects to the node and the node hands over its settings, channels and nodes (30 seconds or
   more the first time). It turns to **Connected** and closes; the status line then says **Connected @ 10:21 pm**.
   **Cancel** on the form hangs up (you are not connected). Nodes and Setup say "The node is still connecting" if
   asked before the node is loaded (after the form timed out at 90 seconds). Picking another paired node in Pair
   Device opens the same form. The button now says **Disconnect**.
3. **Settings > Device...** (first time only; the form is titled "Device"): **Scan**, pick your node in the list,
   **Pair**. The box says pairing and loading the node takes up to a minute (WiRSa reads the node's whole configuration first). If the node asks for a PIN, a keypad opens ("PIN shown on the node");
   a node that still has WiRSa's bond from before asks for none. The pairing is kept by WiRSa; it reconnects to the node by itself from then on. A node paired later is added (up to four, section 9) and starts in Session mode.
4. Open **Channels** on the main screen and tap a channel to chat.

## 3. The main screen

Top right is a pop-up with three views: **Channels**, **Direct Msg**, **Nodes**. The title is the shown
node's name. Under it, the status line and, at its right, the node's battery (a bar and a percentage). A small
**note** symbol shows beside the battery (left of the bar, or at the right end when there is no battery to show)
while any channel or direct conversation has unread messages.

At the bottom of the Channels and Direct Msg views: **Connect** (it says **Disconnect** while connected) at the left
and **Sync Now** at the right ("Updating from the node..."). Nodes has the Find row there instead: `Find: [____] [Clr] [Nodes]`.
**Nodes** asks the node for its whole node list again ("Updating nodes list..."; on a big mesh it can take up to 40 seconds, and can be cancelled).
The status line and the battery group are laid out so the time and the unread note never overlap.

Status line:

| Text | Meaning |
|---|---|
| Not connected | no session with WiRSa (use Connect) |
| No node paired | WiRSa has no node paired (Settings > Device) |
| Initialising... | connected to WiRSa, which has not got the node yet (right after Connect, or after a wake) |
| Fetching messages... | a fetch is running |
| Reading settings... | the node's settings are being read |
| Updating... | linked; settings, channels or nodes are still arriving |
| Connected @ 10:21 pm | linked; time of the last update |
| Disconnected | WiRSa lost the Bluetooth link to the node after it had been up (it retries by itself; the line returns to Connected when it is back) |
| WiRSa ended Mesh mode. | WiRSa left the mode (Exit Timers, its Back button) and told the app: the app is disconnected; Connect starts it again |
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
list (name or short name); **Clr** empties it. Tap a row to open it. In both lists a column at the left holds the **key
icon** (below) for nodes whose public key is known, so names line up. A node's emoji are drawn as the Symbol font's
character 0x10 (a dot); the Palm fonts have no emoji.

The Nodes list is asked for from the node when it is first shown and then at most every 5 minutes (or with the **Nodes** button).
It holds up to 500 nodes.

## 4. Chatting

The message area shows the messages with a heading for each day (`Sat Oct 24, 2026`). Each message
starts with the sender's short name in bold, then the text, with the time at the right end of the last line. Only the messages
you sent start `Me:`; a message from your other node shows that node's short name. A node that has not told its name yet shows the last four characters of its id
(`e536:`); WiRSa asks such nodes for their names in the background and the name appears after the next node refresh.

### Symbols used for messages

Marks before the time on messages you sent (incoming messages have none):

| Mark | Character | Status | Detail form says |
|---|---|---|---|
| `...` | 0x18, bold | sending: queued, or being written to the node | Sending |
| `?` | bold | sent, no confirmation yet | Sent, waiting for confirmation |
| `~` | bold | a direct message only a relay confirmed | Relayed, not confirmed by recipient |
| tick | Symbol font 0x16 | delivered: the recipient acked a direct message, or the mesh acked a channel message | Delivered to recipient / Delivered to mesh |
| key-exchange mark | 0x14, normal font | the message hit a missing-key error; keys are being exchanged and it is sent once more | Exchanging keys, then it is sent again |
| `!` | bold | failed | Error (with Meaning and What to do) |

Other symbols: the **note** beside the battery (Symbol font 0x0B) = unread messages; the small **key icon** (CryptoNote's key bitmap) in
the Direct Msg and Nodes lists (left of the name) and after the short name on a node's card = that node's public key is known, so a
direct message to it can be encrypted; the dot (Symbol font 0x10) = an emoji in a name; `*` stands for it in titles and the details
text, which cannot change font. A relay's ack never undoes a delivered tick. The recipient's key arrives with its node info; the app asks for
it quietly when you open a chat with a node that has none, and when a direct message fails for a key reason WiRSa exchanges node info and sends the text again once.

- **Write** in the three-line field at the bottom; the counter under the box shows `045/233` (a message
  can be 233 bytes). **Send** sends it.
- **Scroll** with the scrollbar, or the hardware page buttons (and a 5-way rocker, thumb wheel or jog dial).
- **Tap** a message to select it, tap another to extend to a range, a third tap starts over. **Edit > Copy**
  copies the selection; **Chat > Select all** selects everything.
- **Double-tap** a message (or Chat > Message details) for the whole message in a bordered form: who, where, when and its
  status in a fixed block, a line 2 px under it, then (scrolling) for a failed message **Error**, **Meaning** and **What to do**
  (the titles and wording of the Meshtastic Android app's delivery errors, listed in the firmware's `docs/Mesh-Palm.md`), and the text.
- **Long threads** are laid out from the newest message backwards: only the latest 6 are wrapped when a chat opens (more, when they are short
  and do not fill the screen twice), and scrolling to the top lays out 12 more and keeps your place.
- **Pop-up at the top right:** *Main Menu* first (back to the main screen), then the channels in slot order
  (`0: Primary`, `1: Family`...) or the 8 latest direct messages. The list is only as long as it needs to be. Picking another conversation keeps the screen and changes its title, list and lines
  (a text typed but not sent is cleared); only *Main Menu* leaves the form.

Chat menu:

- **Chat:** Node info, Reply privately (a direct message to whoever spoke last), Select all, Message details,
  Clear messages. *Clear messages* opens the same box as the main screen's: delete **all** of this conversation's messages
  or those **older than** 1 day, 3 days, 1 week or 1 month, with the count shown before it asks. Only this conversation
  on this Palm; WiRSa keeps its copy.
- **Send:** Alert (bell) sends `:bell:` (with your text after it, if you typed some); Request node info, Request
  position, Request telemetry, Traceroute; Share position... and Waypoint... (degrees such as 1.3521 and
  103.8198). Replies come back as messages from the other node.

## 5. Nodes

A node's card (tap it in Nodes, or Chat > Node info): long name, short name (with the key icon when its public key is known), id (`!a1b2c3d4`), last heard,
SNR and hops, battery, hardware, role, firmware (your own node), uptime, channel and air utilisation, the
number of direct messages, and the position. The list keeps only a short entry for each node; the card asks WiRSa for the rest when it opens. The **Favourite** box sets the node's own favourite flag: WiRSa sends it to the node and the box goes back if the node does not take it.
**DM** starts a direct message. Tapping the *Chats* row or the position row acts on them.

## 6. The node's settings (Node > Settings...)

A card with one row per part: **Owner**, **LoRa**, **Role**, **Bluetooth**, **Channels**, **MQTT** and **Node clock**. A
tap on a row opens its form and changes a copy; **Save** sends the changed parts to the node in one go. **Reboot
Node** restarts it. **Node clock** shows the node's time against the Palm's and sets the node's clock from the Palm.
The page reads the node's settings first, which can take 10 to 40 seconds on a node that has just connected: a
dialog ("Getting settings from the node...") says so and can be cancelled.

- **Channels:** the primary channel's name can be empty (the list shows "(default)"); an empty key means the default key `AQ==`. A row of the
  channel form has **MQTT Up:** and **Dn:** check boxes (uplink / downlink to the MQTT server) beside **New key**. Leaving a form with changes asks "Discard changes?".
- **MQTT** (below Channels): enabled, address, username, password, root topic, encryption, JSON, TLS, proxy to client and map reporting. OK asks
  "Send config and reboot now?": a node turns its Bluetooth off when it gets MQTT settings and restarts about 7 seconds later, so they are sent on their own.
  The app says "Sent. Node restarting. It will reconnect shortly..." and the status line reads Initialising... until the link is back.

## 7. Menus

Main screen:

| Menu | Items |
|---|---|
| **Node** | Sync Now, Clear Messages..., Settings..., (line), Connect, Disconnect |
| **Settings** | Device..., (line), Preferences... |
| **Edit** | the standard Palm edit items |
| **About** | Meshtastic Client |

- **Sync Now** asks WiRSa to take what the node holds right now and then updates the Palm.
- **Clear Messages...** deletes on the Palm, in every conversation: all, or those older than 1 day, 3 days, 1 week or
  1 month. It says how many would go and asks first. WiRSa keeps its own copy (clear that from the Device form's menu).
  A conversation's own Chat > Clear messages uses the same box for that conversation alone.
- **Device...** (a bordered form titled "Device"): *Paired:* a pop-up of the nodes (section 9), the scan list, **Scan**, **Pair**, **Unpair**, then three rows for the node shown:
  **Mode:** *Session* (Mesh mode ends with the app) or *Background* (WiRSa checks the node by itself and hangs up between checks, so messages are waiting when the app opens; only one node can be Background, marked `[B]`),
  **Bg Chk Every:** 1, 5, 15, 30 or 60 minutes (only matters in Background mode, and only while no Palm app is open) and **SD Flush:** (section 10). All three belong to the shown node and change with it.
  The form's menu clears WiRSa's own message store.

## 8. Dates and clocks

- **Message and node times** come from the node (through WiRSa) and do not depend on the Palm's date.
- **Today, Yest., the weekday/date choice, Connected @ hh:mm, the time on messages you send, and Clear Messages by age** use
  the **Palm's own date**. WiRSa's clock is not used (it is not reliable).
- Dates are drawn with the era-aware formatters in `apps/lib/era.c`, in your Palm's Date preferences, so years
  beyond 2031 print correctly. With the **X-Master / X-Y2K35** plugin installed and *enabled* the real year comes from
  the plugin; if the plugin is missing or switched off the clock is used as the Palm reports it (correct to 2040).
  Messages stored while the plugin was off keep the stamps they got then.

Tested in the emulator at Oct 25, 2100 with the real X-Master and X-Y2K35 (enabled and disabled), and with the
clock at its real date.

## 9. What the app keeps, and how it updates

- **One database per node.** A copy of the channel names, node settings, nodes, conversations and messages is saved in a database
  of its own for each node (`MeshChat-<node id>`, 8 hex digits) once when a fetch has finished, and read back at launch (the node shown last) or when you pick the node, so the lists
  are there before you connect and are refreshed once connected. The app's preferences hold only its settings (baud, auto mode, SD History). A WiRSa restart does **not** clear it: the app fetches again from the start and skips the
  messages it already has (the same sender and packet id; a copy saved by an older version has no id and is matched by sender, minute and
  text). Two identical texts sent within a minute are both kept.
- **Several nodes.** WiRSa keeps up to 4 nodes (paired, or archived). *Settings > Device...*: the **Paired:** list shows them: `Name [B]`
  is the one Background mode works for, `Name (archived)` a node kept for its messages. Picking one saves the data on screen and shows that
  node's database at once. Connecting later goes to the node you picked (the app tells WiRSa right after it connects). When WiRSa is connected it follows at once: it saves the texts of the node it had, empties its store, loads the new node's folder on the card
  (`/Meshtastic/<node id>/`) and connects. An archived node has no link, so only reading works. **Pair** adds a node (a node paired before gets its
  database and folder back); **Unpair** asks: *Keep* turns the node into an archive (its messages stay on the Palm and on WiRSa's card),
  *Delete* removes the node, its database and its card folder. With no WiRSa connected the list still shows every node this Palm holds data for.
- Limits, for each node: 500 nodes (a 40-byte entry each, in memory allocated at start; 32 on a Palm short of memory), 32 conversations, 200 messages (the oldest go
  first, 12000 bytes of text), 8 channels, 400 lines per chat. When the 32 conversations are full the direct message that has been quiet the longest
  (read ones first, never the open one) loses its Palm copy and its place is reused; WiRSa keeps the messages.
- **Staying connected:** while linked and idle the app sends WiRSa a small "I am still here" frame every 5 seconds and answers WiRSa's "are you still
  there?" wherever it is waiting; the Exit Timers only end Session mode when both go unanswered. The app tells WiRSa when a dialog is open (HOLD), and when WiRSa
  leaves the mode it says so (BYE) and the app shows Disconnected.
- **Nothing blocks the screen.** After Connect, requests are sent and the answers are read in the event loop, a few
  frames at a time, never while the pen is down. The app asks again every 3 seconds for a short while until the channel
  count has stayed the same three times, because the node hands its channels to WiRSa slowly. WiRSa tells the app about
  news (a message, a delivery, a link change) itself, so nothing is polled.
- Only Connect (finding WiRSa and starting Mesh mode), Settings, Pair/Scan and Clear WiRSa's store wait for an answer,
  each with a progress dialog that can be cancelled; the Nodes list's own refresh and Sync Now do the same. A request sent while a fetch is running waits for that fetch to end
  first (WiRSa takes a frame in the middle of a fetch for a missing acknowledgement).
- While a menu is pulled down the app draws nothing over it: the status line, battery, note and lists catch up when the
  menu has gone. The app follows the menu from the pen and key events (Palm OS has no call that says a menu is open), so
  after a menu is dismissed without a choice a waiting update can show up to a few seconds late.
- **SD History** (Preferences, off by default): after each fetch the app tells WiRSa what it has saved. With History off,
  WiRSa then empties the copy of the texts it keeps on its SD card (it only ever holds texts no Palm has yet); with it
  on, the card keeps everything as a history (1 to 2 MB). Either way the Palm skips texts it already has.

## 10. Background mode, sleep and the SD card

Set under **Settings > Device...**: *Mode: Background* with *Bg Chk Every: 1, 5, 15, 30 or 60 minutes*, for one node. Closing the app
takes WiRSa back to its main menu (to the Background node, if you were looking at another), and WiRSa then checks that node by itself at its interval and keeps the texts for the
next time you connect. While you use the app on another node, no check runs.

- With WiRSa's **Sleep** setting (WiRSaControl Setup) not Off, it sleeps when the main menu has had no Palm traffic for
  that time, and wakes by a timer for each check. A check takes about 10 s and needs only Bluetooth.
- To wake it from the Palm: launch the app and **Connect**, or use WiRSaControl's Find. The LED comes on for a real wake
  (a timer wake leaves it off). The Palm line is not listened to during a check: if nothing answers, try again.
- **After a reboot** WiRSa waits 1 minute before its first background check, so menus and the Palm are not interrupted.
- Texts wait in WiRSa's memory, in a small ring in RTC memory while it sleeps (lost if you press its reset button), and on
  its SD card in `/Meshtastic/<node id>/` (one folder per node). Without a card, only 4 KB of texts survive a long sleep and the oldest go first.
- **SD Flush:** (same form, kept per node) when the ring is written to the card during sleep checks: *Every time*, *3 cycles*, *5 cycles*,
  *10 cycles* (a cycle is one check) or *When full* (the default, when the 4 KB are used). Only a ring that holds texts is
  written; each write wakes the card, so *When full* uses the least power and *Every time* loses nothing to a reset.
- The first fetch after a wake can show **Initialising...** and no battery for a few seconds: WiRSa fetches the node list after
  you connect.

## 11. Troubleshooting

| Symptom | Check |
|---|---|
| "WiRSa did not answer" on Connect | cable and rate; try Preferences > Baud 57600 or 19200; Options (WiRSaControl) > Find WiRSa |
| Status stays "Updating..." | the node may still be sending its list to WiRSa; wait a minute, or use Node > Sync Now |
| Pairing takes a minute | normal: WiRSa reads the node's whole configuration before it says paired; the box says so and can be cancelled |
| "Initialising..." stays | WiRSa cannot reach the node (out of range, off, or another phone holds its Bluetooth connection) |
| "Disconnected" | WiRSa lost the node (out of range, off, or restarting); it reconnects by itself |
| Messages missing | Sync Now (the button or Node > Sync Now); in Background mode the node is only reached at the chosen interval, and the texts of a sleep are kept only while WiRSa has its SD card or has not been reset |
| WiRSa does not wake from the Palm | try Find or Connect again (it does not listen during a background check, about 10 s); the LED shows a real wake |
| A message shows `!` | double-tap it: Error, Meaning and What to do. A missing public key is tried once by itself (the key-exchange mark) before it fails |
| The Nodes list is short or empty | press **Nodes** (it can take up to 40 seconds) or Sync Now; the list is fetched at most every 5 minutes otherwise |
| WiRSa went back to its menu while the app was open | the app and WiRSa lost each other for longer than the Exit Timers (Config > Exit Timers); the app now says "WiRSa ended Mesh mode." and Connect starts it again |
| Fewer channels than the node has | give it a minute; Settings > Channels shows the node's own list |
| Failed frames at 38400 | the Palm's 38400 serial timing is irregular on some units; use 57600 or 19200 |

## 12. Build notes

- Build with `make` in `apps/meshtastic_client/`; after a change to a header or a segment tag use `make clean && make`,
  the Makefile has no header dependencies.
- The `HELLO` offers a confirmed baud switch (`mcHelloOffer`, `Link.c`): a WiRSa that supports it answers at 9600 and waits, the app sends `HELLO` again with `mcHelloConfirm` and then
  moves, and WiRSa moves when it gets that. The reply is read with `RpRecvFrameHunt` (noise in front of it is skipped; built in with `-DRP_WITH_HUNT`), a reply that does not come back readable is
  answered with a new `HELLO` (up to `mcHelloTries`, 3). The junk skip of CalDAV Sync and Bitwarden (`gRpSkipJunkMax`) is not used here: with it the node list fetch ended in a disconnect (cause not found). An older WiRSa replies without
  `mcCapHelloConfirm` and moves at once; the app then moves at once too (WiRSa's Protocol-wire.md 5.1).
- Two code segments: the main one is full at 32 KB, so `Link.c`, the framing helpers and the crypto are in `sec001`
  (`MC_SEC`) and the screens (`Views.c`, `Chat.c`, `Node.c`, `Relay.c`) in `sec002` (`MC_UI`). No const tables and no
  32-bit multiply or divide in secondary segments (`mc_seg.h`). `Data.c` (main) has the helpers they need.
- Files: `MeshChat.c` (event loop, status line, preferences), `Link.c` (the WiRSa session: open, sync, settings), `NodeDb.c` (one database per node, `sec001`),
  `Data.c` (tables, dates, labels), `Views.c` (main lists), `Chat.c` (conversation), `Node.c` (settings pages),
  `Relay.c` (Pair Device), shared `../lib/relayproto.c`, `relaysec.c`, `relaywire.c`, `wirsactl.c`, `era.c`, `datelib.c`.
- Layout rules: buttons 4 px from the bottom, left and right; no italics. The message list gadget is at y=30, 92 px tall: eight 11 px
  lines and a strip below them that the screen scroll must not move (it is erased after every redraw).
- Node storage (`Node` 40 bytes x 500, `NodeFull` for the open card and this node), `LinkNodeGet()` (frame NODE_GET, 0x49), NODES frames (0x58),
  `LinkKeepAlive()` (5 s PING), `LinkAskKey()`, BYE handling in `LinkPump`, `Req` and `SyncFrame`. The globals' region is full at about 32 KB, which is why
  the node table is allocated at start. Message marks and fonts: `Mark()` / `MarkFont()` in `Chat.c`; emoji and keys: `DrawName()`, `DrawKeyIcon()` in `Data.c`
  (bitmap `icons/key.bmp`, id `bmpKey`).
- Frame HIST (0x4D) (`LinkHistSend()`) goes out after the cache is saved when no fetch is pending; `FinishSync()` lets a running
  fetch end before `Req()` sends anything else.
- Testing without hardware: the Palm OS emulator with a fake WiRSa speaking the Mesh frames (see `Protocol-wire.md` section 9 for the
  frames). The emulator's serial line sometimes stalls a sync; the real line does not.
