#!/usr/bin/env python3
"""mock_wirsa_relay.py - throwaway test tool, NOT part of the shipped app
(the Makefile's OBJS list never references it - Python, not compiled).

Stands in for WiRSa's real CalDAV Relay mode (not built yet, Phase B) so
CalDAV Sync's own Serial.c/CDDVWire.c/SyncEngine.c can be exercised end to
end against a controlled, scriptable fake peer before real firmware exists.
Speaks the exact same wire framing CDDVSerialSendFrame/RecvFrame use:
    type(1 byte) + bodyLen(uint16 BE) + body + checksum(1 byte XOR)
and the same SYNC_REQUEST/EVENT_ITEM/ITEM_ACK/SYNC_DONE/SYNC_ERROR body
formats SyncEngine.c encodes/decodes (see that file - strings are
uint16-BE-length-prefixed raw bytes, no NUL; u32 fields are big-endian).

Usage: python3 mock_wirsa_relay.py /dev/pts/N
(get the PTY path from POSE64's own `info` ReControl command once a
serial:pty transport is configured - see pose64's docs/recontrol-protocol.md
and this project's own emulator/README.md.)
"""
import sys
import os
import select
import struct
import termios
import time

CDDV_HELLO = 0x01
CDDV_HELLO_ACK = 0x02
CDDV_SYNC_REQUEST = 0x03
CDDV_EVENT_ITEM = 0x04
CDDV_ITEM_ACK = 0x05
CDDV_SYNC_DONE = 0x06
CDDV_SYNC_ERROR = 0x07
CDDV_OAUTH_START = 0x08
CDDV_OAUTH_INFO = 0x09
CDDV_OAUTH_TOKEN = 0x0A
CDDV_BYE = 0x0B


def checksum(type_byte, body):
    c = type_byte ^ ((len(body) >> 8) & 0xFF) ^ (len(body) & 0xFF)
    for b in body:
        c ^= b
    return c


def send_frame(fd, type_byte, body=b""):
    hdr = bytes([type_byte, (len(body) >> 8) & 0xFF, len(body) & 0xFF])
    chk = bytes([checksum(type_byte, body)])
    os.write(fd, hdr + body + chk)


def read_exact(fd, n, timeout=15.0):
    # os.read() has no timeout of its own - a plain read-loop blocks inside
    # the syscall forever if the peer never sends, so the deadline has to be
    # enforced via select() around each read, not just checked between reads.
    buf = b""
    deadline = time.time() + timeout
    while len(buf) < n:
        remaining = deadline - time.time()
        if remaining <= 0:
            raise TimeoutError(f"timed out waiting for {n} bytes (got {len(buf)}: {buf!r})")
        r, _, _ = select.select([fd], [], [], remaining)
        if not r:
            raise TimeoutError(f"timed out waiting for {n} bytes (got {len(buf)}: {buf!r})")
        chunk = os.read(fd, n - len(buf))
        buf += chunk
    return buf


def recv_frame(fd, timeout=15.0):
    hdr = read_exact(fd, 3, timeout)
    type_byte = hdr[0]
    body_len = (hdr[1] << 8) | hdr[2]
    body = read_exact(fd, body_len, timeout) if body_len else b""
    rx_chk = read_exact(fd, 1, timeout)[0]
    if checksum(type_byte, body) != rx_chk:
        raise ValueError(f"checksum mismatch: type=0x{type_byte:02X} len={body_len}")
    return type_byte, body


def w_str(parts, s):
    b = s.encode("latin-1")
    parts.append(struct.pack(">H", len(b)))
    parts.append(b)


def r_str(body, pos):
    n = struct.unpack_from(">H", body, pos)[0]
    pos += 2
    s = body[pos:pos + n].decode("latin-1")
    pos += n
    return s, pos


def parse_sync_request(body):
    pos = 0
    server_type = body[pos]; pos += 1
    server, pos = r_str(body, pos)
    path, pos = r_str(body, pos)
    uuid, pos = r_str(body, pos)
    user, pos = r_str(body, pos)
    secret, pos = r_str(body, pos)
    past_win = body[pos]; pos += 1
    future_win = body[pos]; pos += 1
    return {
        "serverType": server_type, "server": server, "path": path, "uuid": uuid,
        "user": user, "secret": secret, "pastWindow": past_win, "futureWindow": future_win,
    }


def build_event_item(uid, title, location, utc_start, utc_end, all_day,
                      description="", is_repeating=False):
    # description/is_repeating appended at the end (not interleaved) -
    # matches WiRSa's caldav_relay.cpp cddvSendEventItem's own write order.
    parts = []
    w_str(parts, uid)
    w_str(parts, title)
    w_str(parts, location)
    parts.append(struct.pack(">I", utc_start))
    parts.append(struct.pack(">I", utc_end))
    parts.append(bytes([1 if all_day else 0]))
    w_str(parts, description)
    parts.append(bytes([1 if is_repeating else 0]))
    return b"".join(parts)


def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)
    pty_path = sys.argv[1]

    print(f"[mock {time.time():.2f}] opening {pty_path}")
    fd = os.open(pty_path, os.O_RDWR | os.O_NOCTTY)
    # Raw mode: a fresh pty slave defaults to canonical line discipline,
    # which withholds our newline-free binary frames from read() until a
    # line terminator or full-buffer flush - never, for this protocol.
    attrs = termios.tcgetattr(fd)
    attrs[0] = 0  # iflag
    attrs[1] = 0  # oflag
    attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag
    attrs[3] = 0  # lflag (no ICANON, no ECHO)
    attrs[6][termios.VMIN] = 1
    attrs[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attrs)
    # Discard anything queued from a previous, unread test run - a pty
    # buffers writes regardless of whether a reader is attached, so a
    # stale frame from an earlier attempt would otherwise misalign this one.
    termios.tcflush(fd, termios.TCIFLUSH)

    print("[mock] waiting for SYNC_REQUEST...")
    type_byte, body = recv_frame(fd, timeout=60.0)
    if type_byte != CDDV_SYNC_REQUEST:
        print(f"[mock {time.time():.2f}] unexpected frame type 0x{type_byte:02X}, expected SYNC_REQUEST")
        sys.exit(1)
    req = parse_sync_request(body)
    print(f"[mock {time.time():.2f}] SYNC_REQUEST: {req}")

    now = int(time.time()) + 2082844800  # Unix epoch -> Palm epoch (1904)
    test_events = [
        build_event_item("mock-uid-1@test", "Mock Timed Meeting", "Conference Room", now + 3600, now + 7200, False,
                          description="Weekly team sync", is_repeating=False),
        build_event_item("mock-uid-2@test", "Mock All-Day Event", "", now + 86400, now + 172800, True),
        build_event_item("mock-uid-3@test", "Mock Recurring Instance 1", "Zoom", now + 604800, now + 606600, False,
                          description="Daily standup - keep it under 15 minutes.", is_repeating=True),
    ]

    for i, ev_body in enumerate(test_events):
        print(f"[mock {time.time():.2f}] sending EVENT_ITEM {i + 1}/{len(test_events)}")
        send_frame(fd, CDDV_EVENT_ITEM, ev_body)
        ack_type, ack_body = recv_frame(fd, timeout=60.0)
        if ack_type != CDDV_ITEM_ACK:
            print(f"[mock {time.time():.2f}] expected ITEM_ACK, got 0x{ack_type:02X} - aborting")
            sys.exit(1)
        print(f"[mock {time.time():.2f}] got ITEM_ACK {i + 1}/{len(test_events)}")

    print("[mock] sending SYNC_DONE")
    send_frame(fd, CDDV_SYNC_DONE)

    try:
        type_byte, _ = recv_frame(fd, timeout=5.0)
        print(f"[mock {time.time():.2f}] final frame from Palm: 0x{type_byte:02X} (expected BYE=0x{CDDV_BYE:02X})")
    except TimeoutError:
        print("[mock] no BYE received within 5s (Palm may have already closed) - not fatal")

    os.close(fd)
    print("[mock] done")


if __name__ == "__main__":
    main()
