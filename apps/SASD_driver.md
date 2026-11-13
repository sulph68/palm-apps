# SASD slot driver — changes for slow and noisy serial links

The source, the tools and the builds are in the `SASD/` folder next to this file; the paths below (`Src/`, `firmware/`, `tools/`, `diag/`, `SASD.prc`) are relative to it. The install set is `SASD_Install/`.

**Credit and thanks.** SASD is [dmitry.gr's project](https://dmitry.gr/?r=05.Projects&proj=32.%20SASD): the Palm-side slot driver (`serialSD`), the reader firmware and the
Bkp1/Mgr1 tools. His source is in `sasd/serialSD/` and everything here is built on it. It is released under the GPL v3 (`LICENSE.txt` in this folder), and so are these changes.

This folder is the **driver with the changes below**, its build, the tools that tested it, and a patch that makes the WiRSa print what it sees. `SASD.prc` here is also the
driver in `apps/SASD_Install/PALM/PROGRAMS/SASD/` (the install set); the stock CodeWarrior build it replaced is kept as `SASD_stock.prc` in this folder.

## Why

SASD works over a direct cable. Over a wireless TTL link (NOWttl: two ESP32-C3 units that copy the waveform of one UART to the other by radio) the stock driver could not
get a backup through: Bkp1 stopped with `vfsErrFileNotFound` or `expErrCardBadSector`, and the Palm stopped with a fatal alert (`err4a`) or asked to format the card at the mount. The other WiRSa modes
(CalDAV, Bitwarden, Mesh, SLIP) run over the same link: their protocols resynchronise on a checksum and wait for the reader for seconds, not 210 ms.

Measured over NOWttl with the stock driver (WiRSa on 115200, `pre` 30 ms): half of the frames the WiRSa received were repeats of a request it had already answered (the Palm had not accepted the answer): the ack of a WRITE request was not accepted 84% of the time,
the ack of a data frame 82%, a 530-byte data reply 21%; the Palm's serial port reported software overruns; and a damaged packet stalled the WiRSa's receiver for up to 46 s.
The causes were found one at a time, with the packet log (`SASD_log.prc`) on the Palm and a diagnostic build of the WiRSa firmware, in a series of runs on 2026-11-08 and 09.

## What changed

All of it is in `Src/tol.inc`, `Src/Slot.c` and the two shared layers `firmware/slk.c` and `firmware/frag.c`, behind `SASD_TOL` (always set by the Makefile). `dg...` names are from the log's origin.

| # | Change | Why (measured) |
|---|---|---|
| 1 | **The wait for an ack follows the link.** Twice the slowest of the last 16 accepted acks, never below the stock 21 ticks (210 ms), never above 100 ticks (1 s); a send that got nothing doubles the next wait. Forgotten at every connect. | Over NOWttl an ack takes 170 to 430 ms (median 290 ms) from the end of the Palm's send; the stock 210 ms made 80% of the first sends of a write "fail", and the Palm's resends added traffic. Ack wait 210 ms: 94 of 292 exchanges succeeded on the first send, 10 failed; 600 ms: 256 of 257; 1.2 s: 256 of 256. On a direct cable acks take 5 to 10 ticks, so the wait stays at 210 ms. |
| 2 | **Bulk receive.** Whatever the serial manager holds is read in one call into a 256 byte FIFO (`slotPrvSerRxHard`), and the serial queue is 2080 bytes instead of 560 (the stock size if memory is short). | The stock driver reads one byte per call (`SerClearErr` + `SerReceive`). Noise on the link arrives as hundreds of junk bytes (up to 255 in a single wait), which overflowed the queue: 22 software overruns in one run, and the real ack lost behind the junk. With bulk receive: none. Of 64 exchanges, stock receive failed 8 of 68, bulk receive 0 of 64, on the same link in the same run. |
| 3 | **A locked packet gets time.** Once a header is found, the end time is moved to at least 120 ms from then. | A precaution: an ack whose header arrives at the end of the wait is not abandoned half way. The measurements do not show that it mattered. |
| 4 | **A header whose first byte is missing is accepted.** `EF ED`, the addressing, the length, the tid and a checksum that works out with `BE` in front are as good as a header. | Failed 530-byte replies showed `ef ed 08 08` as the first bytes on all four sends of an exchange: the `BE` was lost, so the whole frame was skipped as junk. (Not needed in the later runs; it costs nothing and cannot match by accident: 24 bits and an 8 bit checksum.) |
| 5 | **A zero-length receive is not a failed receive** (`slkRx`). | `CMD_RESET_STATE` has no reply buffer; the stock `slkRx` took the empty read for a timeout, so a reset never succeeded: 4 sends and about 1 s every time. |
| 6 | **Errors instead of fatal alerts.** A failed READ request returns `sysErrParamErr` after a `CMD_RESET_STATE` (the stock driver calls `SysFatalAlert("err4a")`, which needs a reset of the Palm). | The mount reads sector 0; on a bad link the Palm stopped. Now VFS reports "format?" or an error and the call can be tried again. |
| 7 | **Request and data back to back** (`fragSendPair`, `slotPrvPktExchangePair`). A WRITE request and its first XFER, or a READ request and its CTS, are sent one after the other; the reply that counts is the second. A sector then costs one round trip instead of two. Used only when the reader sets `SD_FLAG_PIPE` (0x2000 in its INFO reply) **and** the link is slow (acks of 12 ticks or more); stopped for 30 s after a pair that needed a resend; if a pair fails, the sector is done the stock way. | NOWttl, one request at a time: 380 writes in 260 s. With pairs: 383 writes in 180 s. On a later run with a faster link (an exchange averaged 128 ms against 310) pairs and single requests had the same cycle (347 and 354 ms). **On a direct cable** the data frame behind the request was corrupted (11 CRC errors in the first 13 s of a run, while the Palm still held the slow readings of the previous run): probably the reader's ack overlapping the incoming data. That is why pairs are limited to slow links, why the readings are forgotten at every connect, and why a pair that needs a resend stops them. |
| 8 | **The packet log** (`make LOG=1`, `SASD_log.prc`). Per exchange: send, end of send, first byte of the answer (10 ms ticks), serial line errors, junk skipped, CRC result, what the ack matcher decided; counters per command and per ack time. Uploaded to a reader that sets `SD_FLAG_DIAG` (0x1000) as `CMD_DIAG` (0x70) packets: every 128 exchanges, when the ring is three quarters full, every 60 s, and after a failed transfer. Never during the mount and **never from sleep** (it hung the Palm). | Without it none of the above would have been found. In the default build (`SASD.prc`) the state block is a few hundred bytes and nothing is logged or uploaded, whatever the reader says. |
| 9 | **No wait without an end** (2026-11-09, after a long NOWttl backup stopped dead). The stock transmit loop repeats `SerSend` for ever if it returns an error with nothing sent; now it clears the line error, waits a tick, and gives up after 0.6 s (the exchange then times out and is sent again). The extension of the receive time after a header lock (+120 ms each time) is capped at 120 ms past the original end time, so a line that keeps producing header-like junk cannot hold `slkRx` for ever. | Not shown to be the cause of the failed run (its cause is still open); these were the places where the driver could wait with no limit. |
| 10 | **Breadcrumbs** (log build only). Before each step (exchange start/end, every send, every receive wait, sector read/write, upload, sleep, wake) the driver writes the step into a 32 entry ring in a `crmb` resource of its own database (`SASD_log.prc` carries it zeroed), so it survives a reset. At the first start after a reset the entries go into the log as events `0x80|step` (and a marker, type `0xFF`) and leave with the first upload; `tools/dgdecode.py` prints them as "What the driver did last before the reset". | A Palm that hangs inside a call leaves no log; the steps before the hang are now read after the reset. Mount again with the capture running. The step list is in `tol.inc` (`CR_*`). |
| 11 | **Transmitter check** (2026-11-10). After every send the driver waits (`SerSendWait`) for the Serial Manager's queue to empty, in the time the bytes need plus 120 ms. If it does not, it logs `TXFIX` (log build), flushes and closes and reopens the port at the current rate; the exchange goes on as a lost packet and its resend goes out. | Long NOWttl backups at 57600 stopped with the Palm's log showing four sends per request and nothing on the line or at the reader, while the reopen done by the wake handshake always made the link work again. Not shown to be the cause: the check is there to detect it and to recover. |
| 12 | **The serial port is let go when the card is unmounted** (2026-11-11). The driver listens for the Expansion Manager's "volume unmounted" notification (OS 4). When it arrives and no volume of this slot is still mounted, a procedure alarm (the same kind of context that opens the port) closes the port and clears the card flags, so WirsaCtl and the WiRSa menu can use the one serial port again, and the next wake sees a card that came back and mounts it. A removal the driver starts itself (wake handshake) is left alone. Opening the port when it is already open closes the first open (one open at a time). The log build records the notification (`0x37`) and the `SerClose` result (`0x36`, 8 = someone else holds the port too). | The Palm kept the port after the card was unmounted (it released it only on sleep), so the WiRSa's main menu could not be reached from the Palm. Checked on a device with the log build: after the unmount the WiRSa goes back to its menu and the serial port connects normally. |

Not changed: the protocol on the wire for a reader that does not know the flags; the V1 (PalmOS 1.x) serial path; the slot API.

### Build changes (the compiler is old)

`m68k-palmos-gcc` 2.95.3 cannot build the source as released (it was written for a newer compiler): `gccisms/stdbool.h` (the SDK's `PalmTypes.h` and the compiler's `stdbool.h` clash),
`firmware/divhelp.c` (32 bit multiply and divide for the 68000, the prebuilt `_udivsi3.o` is for another toolchain), a plain linker script (no forward `ADDR()`), no `-ffunction-sections`, the dispatch table as top-level
`asm` (inside a function the compiler puts a `link` in front and every offset in the table moves by 4), and `-DsysFtrNumProcessorSuperVZ`. The result is 13.4 KB (stock gcc build 8 KB, CodeWarrior 6.9 KB).

## Reader side

The WiRSa firmware has the matching changes (branch `sasd_nowttl`, `Firmware/include/sasd_rx.h`, `README-Palm.md` "Over a wireless serial link"):
- a packet with no byte for 100 ms is dropped, and the bytes of any dropped packet except the first are looked at again for a real packet inside (a 478 byte packet cut short held the receiver for 46 s);
- back to 9600 when bytes keep arriving for 8 s and none makes a packet (a Palm that starts over talks 9600; the reader stayed at the negotiated rate);
- `SASD_FLAG_PIPE` in the INFO reply.

`diag/wirsa_diag_firmware.patch` (applies on top of that branch, `git apply -p1`; build with `-DWIRSA_SASD_DIAG`) adds what was used to measure: one line per request with microsecond times (`TL`), CRC errors,
stalled packets, UART errors, and the Palm's packet log printed as `DIAG` lines. Lines are held in PSRAM and printed on USB only when the Palm has been quiet for 1.5 s (USB traffic coincided with bursts of
spikes on the TX line). The board never deep-sleeps in that build.

## Results (2026-11-08/09, NOWttl `pre` 30 ms, 115200, one Palm, Bkp1 backup of the same databases)

| Driver, link | Backup |
|---|---|
| stock, NOWttl | fails (`vfsErrFileNotFound`); 49% of the frames repeats; format prompt at the mount at 57600 |
| ack wait 900 ms, bulk receive, one request at a time, NOWttl | completes, 380 writes + 34 reads, 260 s; 826 of 828 exchanges on the first send; WiRSa: 22 repeats of 902 frames |
| the same with pairs, NOWttl | completes, 383 writes + 34 reads, 180 s; 378 of 380; WiRSa: 1 repeat of 869 |
| final (this folder), NOWttl | completes, 385 writes, 177 s; WiRSa: 4 repeats of 882 frames, 0 CRC errors, 1 receive timeout |
| final, direct cable | completes, 384 writes, 72 s (17 pauses of Bkp1's own add up to 29 s of it); WiRSa: 0 repeats of 886 frames, 1 CRC error; per-exchange gaps identical to the earlier direct run (12 and 20 ms medians). Stock driver on the direct cable (2026-11-08 14:08): 5958 writes in 802 s |

Not measured: the stock and the final driver back to back on the same day on a direct cable, a real SASD hardware reader, a Palm other than the one used, PalmOS before 3.5.
A sector write over NOWttl still takes 0.35 to 0.65 s against 0.14 s on a direct cable: an exchange takes about 130 to 310 ms depending on the run, and the WiRSa answers in about 1 ms, so the rest is in the link.

## Using it

- `SASD.prc` replaces the installed driver (the same database name, `SASD`, creator `PpSS`): delete the old `SASD` on the Palm, soft-reset, install, soft-reset. To go back, install
  `SASD_stock.prc` the same way (it is the build that was in the install set before).
- With a WiRSa from the `sasd_nowttl` branch the improvements apply on both links. With any other reader the driver behaves like the stock one except for 1 to 6.
- `make` (needs `prc-tools-remix` and `pilrc`): `SASD.prc`; `make LOG=1`: `SASD_log.prc`; `make PAIR=0` forces pairs (testing; the emulator has no slow link). Run `make clean` between variants.

## Installing a new driver without a hard reset (`DrvSwap/`)

A slot driver is in use from the moment the Palm starts, so HotSync (or the install from the card) cannot replace it: the install fails (in the emulator: "ROM import error … hard reset recommended"). What
Bkp1's restore does to a database it cannot delete is the way round it, and `DrvSwap.prc` does only that: it renames the installed `SASD` to `~D-<n>`, sets its type and creator to `~~~~` (so it is no longer a slot driver)
and marks it recyclable. The running driver keeps running, the card stays mounted, nothing else changes. Then the new `SASD.prc` installs under the free name, and the soft reset loads it and deletes the renamed one.

1. Install `DrvSwap.prc` once (it is an ordinary application). Run it before every driver update; it says "The running SASD driver was renamed" (or that none is installed, or that the database called SASD is not a slot driver and was left alone).
2. Install the new `SASD.prc` / `SASD_log.prc` (HotSync, or the install from the card).
3. Soft-reset the Palm. No hard reset, no restore of the data.

Run it only **before** installing the new driver: it renames whatever slot driver is called SASD. Build: `DrvSwap/build_drvswap.sh`. Checked in POSE (Palm III 4.1): install over the running driver fails; after DrvSwap it succeeds; after the
soft reset the new driver runs (its log arrives at the reader) and a full backup completes. Not tried yet on a real Palm.

## Tests and tools (`tools/`)

- `run_test.sh DRIVER.prc [stand-in options]` starts POSE with the SASD stack and `sasd_fake.py` (a PC stand-in for the reader that can drop, delay, split and junk its acks, drop the first byte, flood, advertise
  the flags and keep the uploaded log), runs a full Bkp1 backup, and decodes the log with `dgdecode.py`; `fat16.py` lists and checksums the card image. Examples:
  `run_test.sh SASD.prc --drop 0.1 --junk 0.1 --flood 0.1` (completes, card clean); `--dropfirst 0.3` (headers recovered); `PAIR=0` build with `--pipe`.
- `diag_start.sh` / `diag_stop.sh` / `wirsa_log.py` / `wirsa_keys.py`: capture a run on the bench (WiRSa console log with `D` typed once, NOWttl tap and unit counters, or `NOTAP=1`); `diag_probe.py` talks SASD
  to a reader from a PC. They assume the NOWttl tools in `~/NOWttl/tools` and write to `~/NOWttl/reports` (`REPORTS`, `WIRSA_PORT` override).
- `dgdecode.py FILE [--events N]` decodes the Palm's log from a hex file or a WiRSa log.
- Host test of the reader's receiver: `WiRSa/Firmware/test_host/sasd_rx_test.cpp`.

## Open

- What puts periodic junk (`04 04 04…`, `80 80 80…`, `00 00 00…`, up to hundreds of bytes) on the Palm's receive line, and why an exchange takes 130 to 310 ms: for the NOWttl side. The driver copes with both; it does not remove them.
- The first PING after a connect over NOWttl is often lost (4 sends). The driver recovers on the next connect attempt.
- A write cache or a window of several sectors would cut the round trips further; not done: it changes when an error is reported.
