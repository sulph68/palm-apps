/*
 * ConnectForm.h - the 3-slot connect settings dialog. Gathers connection
 * parameters via the UI; the caller (MainForm.c) is the one that actually
 * calls FtpConnect and refreshes the listing, matching the existing
 * DoConnectTemp split (query params -> connect -> refresh).
 */
#ifndef CONNECTFORM_H
#define CONNECTFORM_H

#include "LazyFTP.h"
#include "FtpProtocol.h"

/* Shows the modal connect dialog. Returns true (with *outParams filled) if
 * the user tapped Connect, false if Cancel. */
Boolean ConnectFormRun(FtpConnParams *outParams) SEC001;

#endif
