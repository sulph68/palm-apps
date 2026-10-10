/*
 * PrefsForm.h - Options > Preferences dialog + the UI-preference values it
 * edits. Stored as "unsaved" app preferences (PrefGetAppPreferences/
 * PrefSetAppPreferences with saved=false) - deliberately NOT the same
 * saved=true slot LzTPConnPrefs uses (see LzTPSec35.h): these are minor,
 * device-local UI settings, not worth HotSync-backing-up.
 */
#ifndef PREFSFORM_H
#define PREFSFORM_H

#include "LazyFTP.h"

/* Bumped to 3 - disconnectOnClose was renamed to disconnectNetworkOnExit
 * with a NARROWER meaning (see its own comment below): app exit now always
 * closes the server session regardless of this checkbox, which only used
 * to gate that same close - an old saved=true blob for this bit would
 * silently mean something different than what the user actually set it to
 * mean, so force a reset to the new default (off) rather than misapply a
 * stale value under new semantics. Byte layout is unchanged from version 2
 * (still just a trailing Boolean), but the MEANING changed, which is
 * exactly the same class of reason version 2 itself bumped for.
 *
 * Bumped to 4 - added retryCount (Connect dialog's new "Retry:" popup).
 * This blob is "unsaved" app prefs (see file header) so a version bump just
 * resets these few UI settings to defaults, unlike LzTPConnPrefs's saved=true
 * server slots - safe/cheap to bump whenever a field is added here. */
#define lzUiPrefsVersion 4

/* Large-directory cache staleness window (see Listing.h's
 * gListCacheTtlSecs) - index into the popup's 4 fixed choices. */
typedef enum {
	lzTtl30Sec = 0,
	lzTtl1Min,
	lzTtl2Min,
	lzTtl3Min
} LzTtlChoice;

typedef struct {
	UInt16  version;
	UInt8   cacheTtl;			/* LzTtlChoice */
	/* previewTextFiles removed - was stored ahead of the download/preview
	 * flow existing, but that flow (built since) never actually gated
	 * anything on it - text-file preview always just happens now, making
	 * the checkbox genuinely redundant, not a real setting. */
	/* StopApplication ALWAYS closes the server session on exit now (an
	 * open FTP control connection has no reason to survive the app that
	 * owns it) - this checkbox controls only the STRONGER action, whether
	 * the underlying network/SLIP connection also gets torn down, or is
	 * left up for whatever's next (another app, or relaunching this one
	 * without paying to re-dial). Off by default: leaving the network up
	 * across an app close is the more useful default on real hardware
	 * where establishing the SLIP link itself is the expensive part. */
	Boolean disconnectNetworkOnExit;
	/* Connect dialog's "Retry:" popup (1/2/3, defaults to 1) - number of
	 * EXTRA connect attempts beyond the first if the initial one fails with
	 * a transport-level error (not a real auth rejection - see FtpConnect's
	 * own comment). Global rather than per-slot: it's a connection-quality/
	 * link-behavior setting, not per-server data, and the version-bump-on-
	 * change cost of this "unsaved" blob is cheap/safe (see lzUiPrefsVersion
	 * above), unlike the saved=true slot prefs a per-slot field would have
	 * needed. */
	UInt8   retryCount;
} LzTPUiPrefs;

extern LzTPUiPrefs gUiPrefs;

/* Loads gUiPrefs from unsaved app prefs (defaults if none/stale-version:
 * cacheTtl=30s, disconnectNetworkOnExit=false, matching the spec's stated
 * defaults) and applies cacheTtl to gListCacheTtlSecs - call once at
 * StartApplication. */
void LzTPLoadUiPrefs(void) SEC001;
void LzTPSaveUiPrefs(void) SEC001;

/* Runs the modal Preferences dialog (3/4-height, bottom-aligned). Saves
 * gUiPrefs (and re-applies cacheTtl to gListCacheTtlSecs) on OK; leaves
 * everything untouched on Cancel. */
void PrefsFormRun(void) SEC001;

#endif
