#ifndef CDDV_OAUTHRELAY_H
#define CDDV_OAUTHRELAY_H

#include "CalDAVSync.h"

/* Runs the Google sign-in flow for this slot: sends OAUTH_START to WiRSa,
 * displays the returned IP+PIN+script instructions, waits for OAUTH_TOKEN
 * (or BACK/cancel), stores the result into slot via CDDVEncryptSlotSecret.
 * Returns true if a token was obtained and saved. */
Boolean OAuthRelayRunGoogleSignIn(CDDVSlot *slot, UInt8 slotIndex) SEC004;

#endif
