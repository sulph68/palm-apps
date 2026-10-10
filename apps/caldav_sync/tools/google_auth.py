#!/usr/bin/env python3
"""One-time Google Calendar sign-in helper for CalDAV Sync's CalDAV Relay.

Run exactly as shown on the Palm's screen after tapping "Sign in with
Google": python3 google_auth.py --ip <ip> --pin <pin>

Does a standard OAuth 2.0 "installed app" flow (PKCE, loopback redirect,
no embedded browser) against Google, then POSTs the resulting refresh
token to WiRSa's transient, PIN-secured local listener, which relays it to
the Palm over serial. Pure standard library - no pip install needed.
"""
import argparse
import base64
import hashlib
import http.server
import json
import secrets
import sys
import urllib.error
import urllib.parse
import urllib.request
import webbrowser

# ---------------------------------------------------------------------------
# One-time setup: paste your own Google Cloud OAuth 2.0 Client ID and
# Client Secret here (Cloud Console > APIs & Services > Credentials >
# Create Credentials > OAuth client ID > Application type: "Desktop app").
# Google's own docs say the secret is optional for this client type, but
# in practice its token endpoint rejects requests without one
# ("client_secret is missing") - confirmed against a real client, so both
# are required here.
GOOGLE_CLIENT_ID = ""
GOOGLE_CLIENT_SECRET = ""
# ---------------------------------------------------------------------------

GOOGLE_AUTH_ENDPOINT = "https://accounts.google.com/o/oauth2/v2/auth"
GOOGLE_TOKEN_ENDPOINT = "https://oauth2.googleapis.com/token"
GOOGLE_SCOPE = "https://www.googleapis.com/auth/calendar.readonly"
WIRSA_OAUTH_PORT_DEFAULT = 8090
BROWSER_FLOW_TIMEOUT_SECONDS = 300  # matches WiRSa's own OAuth listener timeout


def print_production_reminder():
    print("=" * 70)
    print("Reminder: your Google Cloud OAuth client's Consent Screen must be")
    print("set to 'In production' (not 'Testing'), or the refresh token this")
    print("script obtains will silently stop working after 7 days. This is a")
    print("one-time Google Cloud Console setting, separate from formal app")
    print("verification.")
    print("=" * 70)
    print()


def b64url(raw_bytes):
    return base64.urlsafe_b64encode(raw_bytes).rstrip(b"=").decode("ascii")


class _CallbackHandler(http.server.BaseHTTPRequestHandler):
    result = {}

    def do_GET(self):
        params = urllib.parse.parse_qs(urllib.parse.urlparse(self.path).query)
        _CallbackHandler.result["code"] = params.get("code", [None])[0]
        _CallbackHandler.result["state"] = params.get("state", [None])[0]
        _CallbackHandler.result["error"] = params.get("error", [None])[0]
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.end_headers()
        if _CallbackHandler.result.get("code"):
            self.wfile.write(b"<html><body>Signed in - you can close this tab and return to the Palm.</body></html>")
        else:
            self.wfile.write(b"<html><body>Sign-in failed or was cancelled - you can close this tab.</body></html>")

    def log_message(self, fmt, *args):
        pass  # quiet - the default would print an HTTP access log line per request


def run_oauth_flow():
    code_verifier = b64url(secrets.token_bytes(64))
    code_challenge = b64url(hashlib.sha256(code_verifier.encode("ascii")).digest())
    state = secrets.token_urlsafe(16)

    # Port 0 - let the OS pick a free loopback port. Google's own docs say
    # this is fine for a "Desktop app" client: only the http://127.0.0.1
    # scheme+host is registered, not a specific port.
    httpd = http.server.HTTPServer(("127.0.0.1", 0), _CallbackHandler)
    redirect_uri = "http://127.0.0.1:%d/" % httpd.server_address[1]

    auth_params = {
        "client_id": GOOGLE_CLIENT_ID,
        "redirect_uri": redirect_uri,
        "response_type": "code",
        "scope": GOOGLE_SCOPE,
        "access_type": "offline",
        # Forces a fresh refresh_token even if this app was already
        # authorized before - Google only issues one on first consent
        # otherwise, which would silently break a re-run of this script.
        "prompt": "consent",
        "code_challenge": code_challenge,
        "code_challenge_method": "S256",
        "state": state,
    }
    auth_url = GOOGLE_AUTH_ENDPOINT + "?" + urllib.parse.urlencode(auth_params)

    print("Opening your browser to sign in to Google...")
    print("If it doesn't open automatically, visit:\n  %s\n" % auth_url)
    webbrowser.open(auth_url)

    httpd.timeout = BROWSER_FLOW_TIMEOUT_SECONDS
    print("Waiting for you to complete sign-in in the browser (up to %d minutes)..."
          % (BROWSER_FLOW_TIMEOUT_SECONDS // 60))
    httpd.handle_request()
    httpd.server_close()

    result = _CallbackHandler.result
    if result.get("error"):
        sys.exit("Google sign-in failed: %s" % result["error"])
    if not result.get("code"):
        sys.exit("Timed out waiting for the browser sign-in. Try again.")
    if result.get("state") != state:
        sys.exit("State mismatch on the OAuth callback - aborting for safety. Try again.")

    print("Exchanging the authorization code for a refresh token...")
    token_params = {
        "client_id": GOOGLE_CLIENT_ID,
        "client_secret": GOOGLE_CLIENT_SECRET,
        "code": result["code"],
        "code_verifier": code_verifier,
        "grant_type": "authorization_code",
        "redirect_uri": redirect_uri,
    }
    req = urllib.request.Request(
        GOOGLE_TOKEN_ENDPOINT,
        data=urllib.parse.urlencode(token_params).encode("ascii"),
        headers={"Content-Type": "application/x-www-form-urlencoded"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            token_doc = json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        sys.exit("Google token exchange failed (HTTP %d): %s" % (e.code, e.read().decode("utf-8", "replace")))

    refresh_token = token_doc.get("refresh_token")
    if not refresh_token:
        sys.exit(
            "Google did not return a refresh_token. If you've signed in with this\n"
            "app before, revoke its access at https://myaccount.google.com/permissions\n"
            "and try again - Google only issues a refresh_token on first consent."
        )
    return refresh_token


def send_to_wirsa(ip, port, pin, refresh_token):
    body = json.dumps({"pin": pin, "refresh_token": refresh_token}).encode("utf-8")
    req = urllib.request.Request(
        "http://%s:%d/" % (ip, port),
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            resp.read()
    except urllib.error.HTTPError as e:
        sys.exit("WiRSa rejected the token (HTTP %d): %s" % (e.code, e.read().decode("utf-8", "replace")))
    except urllib.error.URLError as e:
        sys.exit(
            "Could not reach WiRSa at %s:%d (%s). Check the IP/PIN shown on the\n"
            "Palm are still current - each sign-in attempt gets a fresh PIN and\n"
            "listener, which stops after 5 minutes." % (ip, port, e.reason)
        )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ip", required=True, help="WiRSa's IP address, shown on the Palm")
    parser.add_argument("--pin", required=True, help="The 6-digit PIN shown on the Palm")
    parser.add_argument("--port", type=int, default=WIRSA_OAUTH_PORT_DEFAULT,
                         help="WiRSa's OAuth listener port (default: %(default)s)")
    args = parser.parse_args()

    print_production_reminder()

    if not GOOGLE_CLIENT_ID or not GOOGLE_CLIENT_SECRET:
        sys.exit(
            "GOOGLE_CLIENT_ID and/or GOOGLE_CLIENT_SECRET is not set. Open this\n"
            "script and paste in your own Google Cloud OAuth 2.0 Client ID and\n"
            "Client Secret (Cloud Console > APIs & Services > Credentials >\n"
            "Create Credentials > OAuth client ID > Desktop app)."
        )

    refresh_token = run_oauth_flow()
    print("Sending the refresh token to WiRSa at %s:%d..." % (args.ip, args.port))
    send_to_wirsa(args.ip, args.port, args.pin, refresh_token)
    print("Done - the Palm should now show the calendar as signed in.")


if __name__ == "__main__":
    main()
