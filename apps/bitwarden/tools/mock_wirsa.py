#!/usr/bin/env python3
"""Mock WiRSa - stands in for the real ESP32 firmware during Palm-side
development (see PROTOCOL.md and ../../../.claude/plans - actually see
PROTOCOL.md, the wire spec both this script and bitwarden_relay.cpp
implement). Speaks the exact same framed serial protocol over a POSE64
serial-PTY bridge, and does the SAME real HTTPS/OAuth/PBKDF2/AES-256-CBC
flow against a real Vaultwarden server that the WiRSa firmware does - this
is a genuine second implementation of PROTOCOL.md + the Bitwarden crypto,
not a stub, so it doubles as cross-validation of the WiRSa C++ port (which
can't be compiled in this sandbox - see bitwarden_relay.cpp's own header
comment).

Usage:
  mock_wirsa.py --pty /dev/pts/10 --creds ../credentials
  mock_wirsa.py --self-test --creds ../credentials
    (does the full Vaultwarden login+sync+decrypt over real HTTPS and
    prints decrypted item names to stdout - no serial/Palm involved, for
    verifying the crypto+API logic in isolation)

The credentials file (never hardcoded, never logged) is 3 lines:
  <server URL>
  <email>
  <master password>
"""

import argparse
import base64
import hashlib
import hmac
import socket
import ssl
import struct
import sys
import time
import urllib.parse

import requests
import serial
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

requests.packages.urllib3.disable_warnings(requests.packages.urllib3.exceptions.InsecureRequestWarning)

# ============================================================================
# Wire protocol (PROTOCOL.md)
# ============================================================================

BW_HELLO, BW_HELLO_ACK = 0x01, 0x02
BW_TRUST, BW_TRUST_RESULT = 0x03, 0x04
BW_SYNC, BW_FOLDER, BW_ORG, BW_SYNC_ITEM = 0x05, 0x0C, 0x0D, 0x06
BW_ITEM_ACK = 0x0E  # Palm -> WiRSa, empty body - see PROTOCOL.md's flow-control note
BW_SYNC_DONE, BW_SYNC_ERROR = 0x07, 0x08
BW_KEX, BW_KEX_ACK = 0x09, 0x0A
BW_BYE = 0x0B
BW_TWO_FACTOR_REQUIRED, BW_TWO_FACTOR_SUBMIT = 0x0F, 0x10
BW_TWO_FACTOR_TIMEOUT = 180.0  # seconds - a human checking their phone/email, not WiRSa's own processing (matches bitwarden_relay.cpp's own BW_TWO_FACTOR_TIMEOUT_MS)

ERR_DNS_FAIL, ERR_CONNECT_FAIL, ERR_TLS_FAIL = 1, 2, 3
ERR_FINGERPRINT_MISMATCH, ERR_AUTH_FAILED, ERR_TWO_FACTOR_REQUIRED = 4, 5, 6
ERR_SYNC_FETCH_FAILED, ERR_DECRYPT_FAILED, ERR_MALFORMED_KDF = 7, 8, 9

ERR_NAMES = {
    ERR_DNS_FAIL: "dns_fail", ERR_CONNECT_FAIL: "connect_fail", ERR_TLS_FAIL: "tls_fail",
    ERR_FINGERPRINT_MISMATCH: "fingerprint_mismatch", ERR_AUTH_FAILED: "auth_failed",
    ERR_TWO_FACTOR_REQUIRED: "two_factor_required", ERR_SYNC_FETCH_FAILED: "sync_fetch_failed",
    ERR_DECRYPT_FAILED: "decrypt_failed", ERR_MALFORMED_KDF: "malformed_kdf",
}


class FrameWriter:
    def __init__(self):
        self.buf = bytearray()

    def u8(self, v):
        self.buf.append(v & 0xFF)

    def u16(self, v):
        self.buf += struct.pack(">H", v)

    def u32(self, v):
        self.buf += struct.pack(">I", v)

    def raw(self, b):
        self.buf += b

    def string(self, s):
        b = s.encode("utf-8") if isinstance(s, str) else s
        self.u16(len(b))
        self.buf += b


class FrameReader:
    def __init__(self, body):
        self.body = body
        self.pos = 0

    def u8(self):
        v = self.body[self.pos]
        self.pos += 1
        return v

    def u16(self):
        v = struct.unpack_from(">H", self.body, self.pos)[0]
        self.pos += 2
        return v

    def u32(self):
        v = struct.unpack_from(">I", self.body, self.pos)[0]
        self.pos += 4
        return v

    def raw(self, n):
        v = self.body[self.pos:self.pos + n]
        self.pos += n
        return v

    def string(self):
        n = self.u16()
        v = self.body[self.pos:self.pos + n]
        self.pos += n
        return v.decode("utf-8", errors="replace")


def read_frame(ser, idle_timeout=60.0, byte_timeout=3.0):
    ser.timeout = idle_timeout
    t = ser.read(1)
    if not t:
        return None, None
    ser.timeout = byte_timeout
    hdr = ser.read(2)
    if len(hdr) != 2:
        return None, None
    (length,) = struct.unpack(">H", hdr)
    body = ser.read(length)
    if len(body) != length:
        return None, None
    chk = ser.read(1)
    if len(chk) != 1:
        return None, None
    calc = t[0] ^ hdr[0] ^ hdr[1]
    for b in body:
        calc ^= b
    if calc != chk[0]:
        print(f"BW mock: checksum mismatch on frame type=0x{t[0]:02X}", file=sys.stderr)
        return None, None
    return t[0], body


def write_frame(ser, msg_type, body: bytes):
    hdr = bytes([msg_type]) + struct.pack(">H", len(body))
    chk = 0
    for b in hdr + body:
        chk ^= b
    ser.write(hdr + body + bytes([chk]))
    ser.flush()


def write_frame_acked(ser, msg_type, body: bytes) -> bool:
    """FOLDER/ORG/SYNC_ITEM flow control - mirrors bitwarden_relay.cpp's
    bwWriteFrameAcked. Blocks for the Palm's ITEM_ACK before returning, so a
    burst of these can't outrun the Palm's own per-record DmWrite/EncCrypt
    work and overrun its small serial receive buffer (the real-device bug
    this ack exists to fix - see PROTOCOL.md)."""
    write_frame(ser, msg_type, body)
    ack_type, _ = read_frame(ser)
    return ack_type == BW_ITEM_ACK


# ============================================================================
# Bitwarden crypto - mirrors bitwarden_relay.cpp's own functions 1:1 so the
# two implementations can be diffed against each other by name.
# ============================================================================

def bw_pbkdf2_sha256(password: bytes, salt: bytes, iterations: int) -> bytes:
    return hashlib.pbkdf2_hmac("sha256", password, salt, iterations, dklen=32)


def bw_hmac_sha256(key: bytes, msg: bytes) -> bytes:
    return hmac.new(key, msg, hashlib.sha256).digest()


def bw_hkdf_expand_sha256(prk: bytes, info: bytes) -> bytes:
    # RFC 5869 HKDF-Expand only (PRK given directly), single 32-byte block.
    return bw_hmac_sha256(prk, info + b"\x01")


def bw_derive_master_key(password: str, email: str, iterations: int) -> bytes:
    return bw_pbkdf2_sha256(password.encode("utf-8"), email.lower().encode("utf-8"), iterations)


def bw_derive_master_password_hash(master_key: bytes, password: str) -> str:
    h = bw_pbkdf2_sha256(master_key, password.encode("utf-8"), 1)
    return base64.b64encode(h).decode("ascii")


def bw_derive_stretched_keys(master_key: bytes):
    return bw_hkdf_expand_sha256(master_key, b"enc"), bw_hkdf_expand_sha256(master_key, b"mac")


def bw_aes_cbc_decrypt(key: bytes, iv: bytes, ct: bytes) -> bytes:
    dec = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
    pt = dec.update(ct) + dec.finalize()
    pad = pt[-1]
    if pad == 0 or pad > 16 or pad > len(pt):
        raise ValueError("bad PKCS7 padding")
    if pt[-pad:] != bytes([pad]) * pad:
        raise ValueError("bad PKCS7 padding")
    return pt[:-pad]


def bw_decrypt_enc_string_bytes(enc_string: str, enc_key: bytes, mac_key: bytes) -> bytes:
    """EncString format: '{encType}.{b64 iv}|{b64 ct}|{b64 mac}'. encType 2 =
    AES-256-CBC+HMAC-SHA256, the only type a personal vault uses. Returns
    raw plaintext bytes - most fields are UTF-8 text (see
    bw_decrypt_enc_string below) but the account "Key" and any per-item
    wrapped cipher.key are raw binary key material, not text, and must not
    be UTF-8-decoded (doing so was a real bug caught while running this
    self-test: Python's utf-8 decode silently replaces invalid byte
    sequences, corrupting random key bytes; the WiRSa C++ side doesn't have
    this problem since its String is just a byte container)."""
    if not enc_string or "." not in enc_string:
        raise ValueError("not an EncString")
    enc_type, rest = enc_string.split(".", 1)
    if enc_type != "2":
        raise ValueError(f"unsupported EncString type {enc_type}")
    iv_b64, ct_b64, mac_b64 = rest.split("|")
    iv = base64.b64decode(iv_b64)
    ct = base64.b64decode(ct_b64)
    mac = base64.b64decode(mac_b64)
    calc = bw_hmac_sha256(mac_key, iv + ct)
    if not hmac.compare_digest(calc, mac):
        raise ValueError("HMAC verification failed")
    return bw_aes_cbc_decrypt(enc_key, iv, ct)


def bw_decrypt_enc_string(enc_string: str, enc_key: bytes, mac_key: bytes) -> str:
    """Text-field variant of bw_decrypt_enc_string_bytes - use this for
    name/username/password/uri/notes/totp/folder-name, never for key
    material."""
    return bw_decrypt_enc_string_bytes(enc_string, enc_key, mac_key).decode("utf-8", errors="replace")


def bw_url_to_host_port(url: str):
    p = urllib.parse.urlparse(url if "://" in url else "https://" + url)
    host = p.hostname
    port = p.port or 443
    return host, port


def bw_get_cert_fingerprint(host: str, port: int, timeout=8.0) -> bytes:
    """TOFU discovery: open a TLS connection accepting any certificate, read
    what was actually presented, and hash it - mirrors bitwarden_relay.cpp's
    bwHandleTrust exactly (setInsecure()+connect()+getFingerprintSHA256())."""
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE
    with socket.create_connection((host, port), timeout=timeout) as sock:
        with ctx.wrap_socket(sock, server_hostname=host) as tls:
            der = tls.getpeercert(binary_form=True)
    return hashlib.sha256(der).digest()


# ============================================================================
# Vaultwarden HTTP calls
# ============================================================================

class BwError(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code
        self.msg = msg


class BwTwoFactorRequired(BwError):
    """Raised by bw_login when Vaultwarden's 400 response has
    TwoFactorProviders/TwoFactorProviders2 - either the account has 2FA
    enabled, or (same response shape - see PROTOCOL.md) the code just
    submitted was wrong. provider/label are whatever _pick_two_factor_provider
    chose; bw_do_sync's own retry loop uses them."""
    def __init__(self, provider, label):
        super().__init__(ERR_TWO_FACTOR_REQUIRED, "two-factor authentication required")
        self.provider = provider
        self.label = label


def _pick_two_factor_provider(d):
    """Mirrors bitwarden_relay.cpp's own bwPickTwoFactorProvider exactly:
    prefers Authenticator app (0), then Email (1), then whatever the server
    listed first - v1 has no "pick your 2FA method" UI on either side."""
    providers2 = d.get("TwoFactorProviders2")
    if providers2:
        if "0" in providers2:
            provider = 0
        elif "1" in providers2:
            provider = 1
        else:
            provider = int(next(iter(providers2.keys())))
        if provider == 1:
            email_hint = (providers2.get("1") or {}).get("Email", "")
            label = f"Email code sent to {email_hint}" if email_hint else "Email code"
        elif provider == 0:
            label = "Authenticator app code"
        else:
            label = f"Two-factor code (provider {provider})"
        return provider, label
    providers = d.get("TwoFactorProviders") or []
    provider = 0 if 0 in providers else (providers[0] if providers else 0)
    label = {0: "Authenticator app code", 1: "Email code"}.get(provider, f"Two-factor code (provider {provider})")
    return provider, label


def bw_prelogin(base_url, email):
    r = requests.post(base_url + "/identity/accounts/prelogin", json={"email": email}, verify=False, timeout=15)
    if r.status_code != 200:
        raise BwError(ERR_MALFORMED_KDF, f"prelogin HTTP {r.status_code}")
    d = r.json()
    return {
        "type": d.get("kdf", 0),
        "iterations": d.get("kdfIterations", 600000),
        "memory": d.get("kdfMemory", 0),
        "parallelism": d.get("kdfParallelism", 0),
    }


def bw_login(base_url, email, master_password_hash_b64, two_factor_token=None, two_factor_provider=None):
    """Returns (access_token, key_enc_string, new_two_factor_token) - the
    third element is Bitwarden's own "remember this device" token if the
    server included one (only happens when twoFactorRemember=true was
    honored - i.e. an actual 2FA factor, remembered or fresh, was used),
    empty string otherwise. Mirrors bitwarden_relay.cpp's own bwLogin."""
    form = {
        "grant_type": "password",
        "username": email,
        "password": master_password_hash_b64,
        "scope": "api offline_access",
        "client_id": "connector",
        "deviceType": "21",
        "deviceName": "WiRSa-mock",
        "deviceIdentifier": "mock-wirsa-relay-py",
    }
    if two_factor_token:
        form["twoFactorToken"] = two_factor_token
        form["twoFactorProvider"] = str(two_factor_provider)
        form["twoFactorRemember"] = "true"
    r = requests.post(base_url + "/identity/connect/token",
                       data=form,
                       headers={"Device-Type": "0"},
                       verify=False, timeout=15)
    if r.status_code == 400:
        d = r.json()
        if "TwoFactorProviders" in d or "TwoFactorProviders2" in d:
            provider, label = _pick_two_factor_provider(d)
            raise BwTwoFactorRequired(provider, label)
        raise BwError(ERR_AUTH_FAILED,
                      "incorrect two-factor code" if two_factor_token else "invalid email or master password")
    if r.status_code != 200:
        raise BwError(ERR_AUTH_FAILED, f"token HTTP {r.status_code}: {r.text[:200]}")
    d = r.json()
    token = d.get("access_token")
    key_enc = d.get("Key")
    new_remember_token = d.get("TwoFactorToken") or ""
    if not token or not key_enc:
        raise BwError(ERR_AUTH_FAILED, "token response missing access_token/Key")
    return token, key_enc, new_remember_token


def bw_sync(base_url, access_token):
    r = requests.get(base_url + "/api/sync", params={"excludeDomains": "true"},
                      headers={"Authorization": f"Bearer {access_token}"},
                      verify=False, timeout=30)
    if r.status_code != 200:
        raise BwError(ERR_SYNC_FETCH_FAILED, f"sync HTTP {r.status_code}")
    return r.json()


def bw_do_sync(server_url, email, master_password, pinned_fp: bytes, item_cb, folder_cb, org_cb,
                two_factor_cb=None, remembered_token=None):
    """Runs the full login+sync+decrypt flow. Calls folder_cb(index, name)
    and org_cb(index, name) for each folder/organization (before any
    items), then item_cb(dict) for each item. Raises BwError on any
    failure. Mirrors bwHandleSync's flow exactly. Returns
    (item_count, folder_count, org_count, new_remember_token) - the last
    element is Bitwarden's own updated "remember this device" token (empty
    string = no change, caller should leave whatever it already has cached
    untouched) - see PROTOCOL.md's own SYNC_DONE note.

    two_factor_cb(provider: int, label: str) -> Optional[str] is called if
    the account has 2FA enabled (or a submitted code was wrong - same
    response shape, see PROTOCOL.md); should return the code the user typed,
    or None to cancel. Retried up to 3 times same as bitwarden_relay.cpp's
    own kMaxTwoFactorAttempts. If 2FA is required and two_factor_cb is None
    (self-test path with no interactive Palm/operator), this fails fast with
    a clear BwError instead of hanging.

    remembered_token, if given, is tried on the FIRST login attempt
    (provider 5 = Bitwarden's own Remember pseudo-provider) before ever
    falling back to an interactive two_factor_cb prompt - if it's still
    valid, this succeeds with no prompt at all, same as bitwarden_relay.cpp's
    own bwHandleSync."""
    host, port = bw_url_to_host_port(server_url)
    base_url = f"https://{host}:{port}" if port != 443 else f"https://{host}"

    actual_fp = bw_get_cert_fingerprint(host, port)
    if actual_fp != pinned_fp:
        raise BwError(ERR_FINGERPRINT_MISMATCH, "certificate fingerprint does not match the pinned value")

    kdf = bw_prelogin(base_url, email)
    if kdf["type"] != 0:
        raise BwError(ERR_MALFORMED_KDF, "this account uses Argon2id - not yet supported by this relay")

    master_key = bw_derive_master_key(master_password, email, kdf["iterations"])
    master_password_hash = bw_derive_master_password_hash(master_key, master_password)
    stretched_enc, stretched_mac = bw_derive_stretched_keys(master_key)

    two_factor_token, two_factor_provider = None, None
    if remembered_token:
        two_factor_token, two_factor_provider = remembered_token, 5  # 5 = Remember
    new_remember_token = ""
    for attempt in range(4):  # 1 initial attempt + up to 3 retries, matches bitwarden_relay.cpp
        try:
            access_token, key_enc_string, new_remember_token = bw_login(
                base_url, email, master_password_hash, two_factor_token, two_factor_provider)
            break
        except BwTwoFactorRequired as e:
            if two_factor_cb is None:
                raise BwError(ERR_TWO_FACTOR_REQUIRED,
                               "this account requires two-factor authentication - no two_factor_cb given")
            if attempt >= 3:
                raise BwError(ERR_AUTH_FAILED, "too many incorrect two-factor codes")
            code = two_factor_cb(e.provider, e.label)
            if not code:
                raise BwError(ERR_AUTH_FAILED, "two-factor entry cancelled")
            two_factor_token, two_factor_provider = code, e.provider

    user_key_raw = bw_decrypt_enc_string_bytes(key_enc_string, stretched_enc, stretched_mac)
    if len(user_key_raw) < 64:
        raise BwError(ERR_DECRYPT_FAILED, "failed to unwrap the account key (likely wrong master password)")
    user_enc_key, user_mac_key = user_key_raw[:32], user_key_raw[32:64]

    data = bw_sync(base_url, access_token)

    folders = data.get("folders", [])
    folder_index_by_id = {}
    for idx, f in enumerate(folders):
        try:
            name = bw_decrypt_enc_string(f.get("name", ""), user_enc_key, user_mac_key)
        except Exception:
            name = "(undecryptable)"
        folder_index_by_id[f.get("id")] = idx
        folder_cb(idx, name)

    # Organizations - NOT end-to-end encrypted in Bitwarden's own model
    # (server-managed membership/metadata), relayed as plain text, same as
    # bitwarden_relay.cpp's own bwSyncAndStream.
    orgs = data.get("profile", {}).get("organizations", [])
    org_index_by_id = {}
    for idx, o in enumerate(orgs):
        org_index_by_id[o.get("id")] = idx
        org_cb(idx, o.get("name", "(unnamed)"))

    item_count = 0
    for c in data.get("ciphers", []):
        if c.get("deletedDate"):
            continue
        item_enc_key, item_mac_key = user_enc_key, user_mac_key
        if c.get("key"):
            try:
                raw = bw_decrypt_enc_string_bytes(c["key"], user_enc_key, user_mac_key)
                if len(raw) >= 64:
                    item_enc_key, item_mac_key = raw[:32], raw[32:64]
            except Exception:
                pass

        def dec(field):
            try:
                return bw_decrypt_enc_string(field, item_enc_key, item_mac_key) if field else ""
            except Exception:
                return ""

        # Dedicated per-field wire slots per itemType - mirrors
        # bitwarden_relay.cpp's own exact field set (see PROTOCOL.md's
        # SYNC_ITEM section). Every real Bitwarden Card/Identity field gets
        # its own slot now, not crammed into 4 generic login-shaped ones.
        item = {
            "guid": c.get("id", ""),
            "type": c.get("type", 1),
            "folderIndex": folder_index_by_id.get(c.get("folderId"), 0xFFFF),
            "orgIndex": org_index_by_id.get(c.get("organizationId"), 0xFFFF),
            "name": dec(c.get("name", "")),
            "notes": dec(c.get("notes", "")),
        }
        if c.get("login"):
            login = c["login"]
            uri = ""
            for u in (login.get("uris") or []):
                if u.get("uri"):
                    uri = dec(u["uri"])
                    break
            item.update({
                "username": dec(login.get("username", "")),
                "password": dec(login.get("password", "")),
                "uri": uri,
                "totp": dec(login.get("totp", "")),
            })
        elif c.get("card"):
            card = c["card"]
            item.update({
                "cardholderName": dec(card.get("cardholderName", "")),
                "brand": dec(card.get("brand", "")),
                "number": dec(card.get("number", "")),
                "expMonth": dec(card.get("expMonth", "")),
                "expYear": dec(card.get("expYear", "")),
                "code": dec(card.get("code", "")),
            })
        elif c.get("identity"):
            identity = c["identity"]
            item.update({
                "title": dec(identity.get("title", "")),
                "firstName": dec(identity.get("firstName", "")),
                "middleName": dec(identity.get("middleName", "")),
                "lastName": dec(identity.get("lastName", "")),
                "username": dec(identity.get("username", "")),
                "company": dec(identity.get("company", "")),
                "ssn": dec(identity.get("ssn", "")),
                "passportNumber": dec(identity.get("passportNumber", "")),
                "licenseNumber": dec(identity.get("licenseNumber", "")),
                "email": dec(identity.get("email", "")),
                "phone": dec(identity.get("phone", "")),
                "address1": dec(identity.get("address1", "")),
                "address2": dec(identity.get("address2", "")),
                "address3": dec(identity.get("address3", "")),
                "city": dec(identity.get("city", "")),
                "state": dec(identity.get("state", "")),
                "postalCode": dec(identity.get("postalCode", "")),
                "country": dec(identity.get("country", "")),
            })
        item_cb(item)
        item_count += 1

    return item_count, len(folders), len(orgs), new_remember_token


# ============================================================================
# Serial-driven mock (real usage: stands in for WiRSa on the POSE64 PTY)
# ============================================================================

def load_credentials(path):
    with open(path) as f:
        lines = [l.rstrip("\n") for l in f]
    return lines[0].strip(), lines[1].strip(), lines[2]


def run_serial_mock(pty_path, creds_path):
    server_url, email, master_password = load_credentials(creds_path)
    print(f"BW mock: loaded credentials from {creds_path} (server/email present, password not printed)")
    ser = serial.Serial(pty_path, baudrate=57600)
    print(f"BW mock: listening on {pty_path}")

    while True:
        msg_type, body = read_frame(ser)
        if msg_type is None:
            continue

        if msg_type == BW_HELLO:
            w = FrameWriter()
            w.u8(1)     # protocol version
            w.u8(0x00)  # capFlags: KEX not implemented in this mock either (Phase 1)
            w.string("mock-wirsa/1")
            write_frame(ser, BW_HELLO_ACK, bytes(w.buf))
            print("BW mock: HELLO -> HELLO_ACK")

        elif msg_type == BW_TRUST:
            r = FrameReader(body)
            url = r.string()
            host, port = bw_url_to_host_port(url)
            w = FrameWriter()
            try:
                fp = bw_get_cert_fingerprint(host, port)
                w.u8(0)
                w.raw(fp)
                w.string("")  # subjectCN - left empty, matches bitwarden_relay.cpp
                w.string("")  # issuerCN
                print(f"BW mock: TRUST {host}:{port} -> fingerprint {fp.hex()}")
            except Exception as e:
                w.u8(2)  # connect_fail
                w.raw(b"\x00" * 32)
                w.string(""); w.string("")
                print(f"BW mock: TRUST failed: {e}")
            write_frame(ser, BW_TRUST_RESULT, bytes(w.buf))

        elif msg_type == BW_SYNC:
            r = FrameReader(body)
            url = r.string()
            pinned_fp = r.raw(32)
            email_req = r.string()
            master_password_req = r.string()
            _kdf_type = r.u8(); _kdf_iters = r.u32(); _kdf_mem = r.u32(); _kdf_par = r.u8()
            remembered_token_req = r.string()
            print(f"BW mock: SYNC requested for {url} / {email_req} (password not logged)"
                  + (" [has remembered 2FA token]" if remembered_token_req else ""))

            def folder_cb(idx, name):
                w = FrameWriter()
                w.u16(idx)
                w.string(name)
                if not write_frame_acked(ser, BW_FOLDER, bytes(w.buf)):
                    raise BwError(ERR_SYNC_FETCH_FAILED, "Palm lost mid-sync (folder ack)")

            def org_cb(idx, name):
                w = FrameWriter()
                w.u16(idx)
                w.string(name)
                if not write_frame_acked(ser, BW_ORG, bytes(w.buf)):
                    raise BwError(ERR_SYNC_FETCH_FAILED, "Palm lost mid-sync (org ack)")

            def item_cb(item):
                w = FrameWriter()
                w.string(item["guid"])
                w.u8(item["type"])
                w.u16(item["folderIndex"])
                w.u16(item["orgIndex"])
                w.string(item["name"])
                w.string(item["notes"])
                # Type-specific tail - see PROTOCOL.md's own SYNC_ITEM
                # section; order must match Sync.c's own reader exactly.
                if item["type"] == 3:		# card
                    for k in ("cardholderName", "brand", "number", "expMonth", "expYear", "code"):
                        w.string(item.get(k, ""))
                elif item["type"] == 4:	# identity
                    for k in ("title", "firstName", "middleName", "lastName", "username", "company",
                              "ssn", "passportNumber", "licenseNumber", "email", "phone",
                              "address1", "address2", "address3", "city", "state", "postalCode", "country"):
                        w.string(item.get(k, ""))
                elif item["type"] != 2:	# login (secure note has no tail)
                    for k in ("username", "password", "uri", "totp"):
                        w.string(item.get(k, ""))
                if not write_frame_acked(ser, BW_SYNC_ITEM, bytes(w.buf)):
                    raise BwError(ERR_SYNC_FETCH_FAILED, "Palm lost mid-sync (item ack)")
                print(f"BW mock: item '{item['name']}' type={item['type']} sent")

            def two_factor_cb(provider, label):
                w = FrameWriter()
                w.u8(provider)
                w.string(label)
                write_frame(ser, BW_TWO_FACTOR_REQUIRED, bytes(w.buf))
                print(f"BW mock: two-factor required (provider {provider}: {label}) - waiting up to {BW_TWO_FACTOR_TIMEOUT:.0f}s for the Palm")
                rx_type, rx_body = read_frame(ser, idle_timeout=BW_TWO_FACTOR_TIMEOUT)
                if rx_type is None:
                    print("BW mock: timed out waiting for two-factor code")
                    return None
                if rx_type == BW_BYE:
                    print("BW mock: two-factor entry cancelled by Palm")
                    return None
                if rx_type != BW_TWO_FACTOR_SUBMIT:
                    print(f"BW mock: unexpected frame 0x{rx_type:02X} while waiting for two-factor code")
                    return None
                code = FrameReader(rx_body).string()
                print(f"BW mock: two-factor code received ({len(code)} chars)")
                return code

            try:
                item_count, folder_count, org_count, new_remember_token = bw_do_sync(
                    url, email_req, master_password_req, pinned_fp, item_cb, folder_cb, org_cb, two_factor_cb,
                    remembered_token=remembered_token_req or None)
                w = FrameWriter()
                w.u16(item_count); w.u16(folder_count); w.u16(org_count)
                w.string(new_remember_token)
                write_frame(ser, BW_SYNC_DONE, bytes(w.buf))
                print(f"BW mock: SYNC complete - {item_count} item(s), {folder_count} folder(s), {org_count} org(s)"
                      + (" [2FA remember token renewed]" if new_remember_token else ""))
            except BwError as e:
                w = FrameWriter()
                w.u8(e.code); w.string(e.msg)
                write_frame(ser, BW_SYNC_ERROR, bytes(w.buf))
                print(f"BW mock: SYNC failed [{ERR_NAMES.get(e.code, e.code)}]: {e.msg}")
            except Exception as e:
                w = FrameWriter()
                w.u8(ERR_SYNC_FETCH_FAILED); w.string(str(e))
                write_frame(ser, BW_SYNC_ERROR, bytes(w.buf))
                print(f"BW mock: SYNC failed [unexpected]: {e}")

        elif msg_type == BW_BYE:
            print("BW mock: BYE received")

        elif msg_type == BW_KEX:
            print("BW mock: KEX received but not implemented in this mock either - ignored")

        else:
            print(f"BW mock: unknown frame type 0x{msg_type:02X} ignored")


def run_self_test(creds_path):
    """Full login+sync+decrypt over real HTTPS, no serial/Palm involved -
    proves the crypto+API logic against the real test account in isolation."""
    server_url, email, master_password = load_credentials(creds_path)
    host, port = bw_url_to_host_port(server_url)
    print(f"Self-test: fetching TOFU fingerprint for {host}:{port} ...")
    fp = bw_get_cert_fingerprint(host, port)
    print(f"Self-test: fingerprint = {fp.hex()}")

    def two_factor_cb(provider, label):
        # No serial/Palm involved here - prompt on the terminal instead, so
        # this path can still exercise the real 2FA round trip manually.
        print(f"Self-test: two-factor required ({label}, provider {provider})")
        try:
            return input("  enter code (blank to cancel): ").strip() or None
        except EOFError:
            return None

    folders = []
    orgs = []
    items = []
    t0 = time.time()
    item_count, folder_count, org_count, new_remember_token = bw_do_sync(
        server_url, email, master_password, fp,
        item_cb=lambda item: items.append(item),
        folder_cb=lambda idx, name: folders.append((idx, name)),
        org_cb=lambda idx, name: orgs.append((idx, name)),
        two_factor_cb=two_factor_cb,
    )
    dt = time.time() - t0
    print(f"Self-test: SYNC complete in {dt:.2f}s - {item_count} item(s), {folder_count} folder(s), {org_count} org(s)"
          + (f" [got 2FA remember token, {len(new_remember_token)} chars - not persisted, self-test only]" if new_remember_token else ""))
    for idx, name in folders:
        print(f"  folder[{idx}] = {name!r}")
    for idx, name in orgs:
        print(f"  org[{idx}] = {name!r}")
    for item in items:
        extra = {k: v for k, v in item.items()
                 if k not in ("guid", "type", "folderIndex", "orgIndex", "name", "notes") and v}
        print(f"  item: name={item['name']!r} type={item['type']} "
              f"folderIndex={item['folderIndex']} orgIndex={item['orgIndex']} "
              f"populated_fields={sorted(extra.keys())}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--pty", help="Path to the POSE64 serial PTY, e.g. /dev/pts/10")
    ap.add_argument("--creds", default="../credentials", help="Path to the credentials file")
    ap.add_argument("--self-test", action="store_true", help="Run the crypto+API flow directly, no serial")
    args = ap.parse_args()

    if args.self_test:
        run_self_test(args.creds)
    else:
        if not args.pty:
            ap.error("--pty is required unless --self-test is given")
        run_serial_mock(args.pty, args.creds)


if __name__ == "__main__":
    main()
