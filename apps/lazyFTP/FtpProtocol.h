/*
 * FtpProtocol.h - FTP control/data connection state and protocol calls.
 * PASV-only (see project memory lazyftp-project.md - active/PORT mode is
 * impractical on single-tasking PalmOS, the same conclusion axxpac_ftp_src
 * independently reached). Blocking calls throughout - callers wrap these in
 * LzTPBusyBegin/End or LzTPProgressBegin/Bar/End (BusyDialog.c) and reset
 * EvtResetAutoOffTimer themselves during any long-running one.
 *
 * Transport is a thin boundary (FtpTransportSend/Receive/Open/Close) over
 * plain NetLib TCP today - deliberately kept as the ONLY place that touches
 * raw sockets, so a future FTPS/BearSSL integration only changes these
 * functions' internals, not the protocol code that calls them.
 */
#ifndef FTPPROTOCOL_H
#define FTPPROTOCOL_H

#include "LazyFTP.h"
#include <NetMgr.h>

#define ftpPathLen    256
#define ftpReplyLen   256
#define ftpNameLen     64

/* Reserved, unused in v1 - see the FTPS-extensibility plan in project
 * memory. Kept in the connection-slot struct now so adding real modes later
 * doesn't need a prefs-version migration. */
typedef enum {
	kFtpSecNone = 0,
	kFtpSecExplicitTLS,
	kFtpSecImplicitTLS
} FtpSecMode;

typedef struct {
	char       server[ftpNameLen];
	UInt16     port;
	char       user[ftpNameLen];
	char       pass[ftpNameLen];		/* plaintext once decrypted for use */
	FtpSecMode secMode;					/* unused v1, always kFtpSecNone */
	/* Extra connect attempts (beyond the first) if the initial one fails
	 * with a TRANSPORT-level error (socket/DNS failure, or the greeting
	 * banner never arriving) - NOT retried on a real auth rejection (see
	 * FtpConnect's own comment). From the Connect dialog's "Retry:" popup
	 * (1/2/3, ConnectForm.c) - real-hardware report this session: some
	 * servers intermittently fail with "Connection lost" right after the
	 * TCP connect succeeds (banner never arrives, PC-to-same-server never
	 * has this problem) - a classic NAT/relay-race shape, not a server
	 * issue, matching this file's own already-documented DNS-retry lesson
	 * ("the first lookup right after NetLibOpen can spuriously fail while
	 * the link is still settling"). */
	UInt8      retryCount;
} FtpConnParams;

typedef struct {
	Boolean is_dir;
	char    name[ftpNameLen];
	UInt32  size;
} FtpDirEntry;

/* ---- connection lifecycle -------------------------------------------- */
Boolean FtpIsConnected(void) SEC004;
Boolean FtpConnect(const FtpConnParams *p, char *errMsg, UInt16 errMsgLen) SEC004;

/* Two distinct disconnect actions (user-requested split, this session):
 * FtpDisconnectServer closes just the FTP session (QUIT + close control
 * socket) and leaves the underlying network connection alone - lets the
 * user drop a server session without paying the cost of re-establishing
 * the SLIP/PPP link itself for whatever comes next. FtpDisconnectNetwork
 * is the stronger action - closes the server session too (if still open)
 * AND tears down the actual network connection. There's no third
 * "network gone but still connected to a server" state. */
void    FtpDisconnectServer(void) SEC004;
void    FtpDisconnectNetwork(void) SEC004;

const char *FtpCurPath(void) SEC004;
const char *FtpCurServer(void) SEC004;
UInt16      FtpCurPort(void) SEC004;

/* Builds an absolute path (out, at most outLen-1 chars + NUL) from a
 * directory (in FtpCurPath()'s own format - root is "/", no trailing slash
 * otherwise) and a bare name - see the .c file for why this exists (Cut/
 * Paste's RNFR/RNTO arguments). Returns `out`. */
char *FtpJoinPath(const char *dir, const char *name, char *out, UInt16 outLen) SEC004;

/* Called by FtpCmd's own silent-auto-reconnect path (see project memory:
 * PalmOS/the underlying driver can drop an idle SLIP link on its own) the
 * moment a reconnect attempt actually starts, so a caller showing a busy
 * dialog around whatever FTP call triggered it can swap the on-screen text
 * to something honest ("Reconnecting...") instead of it staying frozen on
 * stale text for the whole attempt. Pass NULL to clear. Registering a hook
 * is optional - nothing calls this by default, existing callers are
 * unaffected either way. */
typedef void (*FtpStatusProc)(const char *status);
void FtpSetStatusHook(FtpStatusProc proc) SEC004;

/* Called by FtpConnect's own retry loop (FtpConnParams.retryCount - see its
 * own comment) with a ready-to-display title string, both for the "Retry N
 * in Ns..." countdown between attempts and to switch back to "Connecting..."
 * right before each retry actually fires. Deliberately a SEPARATE hook from
 * FtpStatusProc/gFtpStatusHook above, not a reuse of it - that one was
 * hardened (LzTPBusyStatusHook) to stop drawing any text at all and just
 * tear the busy overlay down, after a real crash where it collided with
 * PalmOS's own network redial UI taking over the screen mid NetLib-call
 * failure. This hook only ever fires from a plain SysTaskDelay pause
 * BETWEEN attempts - no NetLib call is in flight at that instant, so it
 * can't hit that same collision; reusing the text-suppressing hook here
 * would just make the countdown never actually show up. Pass NULL to
 * clear. Optional opt-in, same shape/rationale as the other hooks here. */
typedef void (*FtpConnRetryProc)(const char *title);
void FtpSetConnRetryHook(FtpConnRetryProc proc) SEC004;

/* Called periodically during FtpStoreDatabase/FtpRetrieveDatabase with
 * cumulative bytes transferred so far and the expected total (0 if unknown
 * - caller should treat that as "can't compute a percentage", not "0%"),
 * so a caller can drive a real progress bar instead of a plain "Uploading/
 * Downloading..." busy dialog. Pass NULL to clear. Optional opt-in, same
 * shape/rationale as FtpSetStatusHook - existing callers unaffected. */
typedef void (*FtpProgressProc)(UInt32 done, UInt32 total);
void FtpSetProgressHook(FtpProgressProc proc) SEC004;

/* Called once when a transfer moves into a distinct sub-phase the caller
 * should relabel its dialog for - today, only the upload path's post-STOR
 * CRC-verify re-download ("CRC Checking"), which otherwise silently reuses
 * the same progress hook as the upload itself and looks like a stall/reset.
 * Pass NULL to clear. Optional opt-in, same shape/rationale as the other
 * hooks above - existing callers unaffected. */
typedef void (*FtpPhaseProc)(const char *phase);
void FtpSetPhaseHook(FtpPhaseProc proc) SEC004;

/* Polled between chunks during a transfer (upload send, download receive,
 * and the CRC-verify re-download) so a caller can offer the user a way to
 * abort mid-transfer. Return true to abort. Pass NULL to clear (default -
 * no cancellation support unless a caller opts in). A cancel is reported to
 * the transfer's own caller as a distinct failure from a real transport
 * error or (upload-only) a CRC mismatch - see FtpStoreDatabase's own
 * comment on how it tells the three apart. */
typedef Boolean (*FtpCancelQueryProc)(void);
void FtpSetCancelQuery(FtpCancelQueryProc proc) SEC004;

/* Sends a no-op control-channel command (expects a 200 reply). Used to keep
 * the FTP control connection - and, on this project's SLIP/WiRSa transport,
 * the underlying idle-timed link - alive across a long CRC-verify pass so
 * the directory-listing refresh right after doesn't have to pay for a
 * reconnect. Returns false only on a transport-level failure (mirrors
 * FtpCmd's own true/false contract - see its comment); a caller doesn't
 * need to treat that as fatal, just informational. */
Boolean FtpNoop(void) SEC004;

/* ---- directory operations -----------------------------------------------
 * Results land directly in Listing.c's own storage (small directories: an
 * in-RAM buffer; large ones: the on-device LazyFTPDBCache database) rather
 * than a caller-supplied buffer - see Listing.h's ingest/cache API. If a
 * fresh, matching cache already exists for the current directory, FtpList
 * skips the network fetch entirely. outTruncated is set true on the rare
 * failure to even write one page's worth of data (heap/storage exhausted) -
 * reported honestly rather than silently dropped. Pass NULL if the caller
 * doesn't care. */
Boolean FtpList(Boolean *outTruncated, char *errMsg, UInt16 errMsgLen) SEC004;
Boolean FtpChangeDir(const char *name, char *errMsg, UInt16 errMsgLen) SEC004;
Boolean FtpParentDir(char *errMsg, UInt16 errMsgLen) SEC004;
Boolean FtpMakeDir(const char *name, char *errMsg, UInt16 errMsgLen) SEC004;
Boolean FtpDelete(const char *name, Boolean isDir, char *errMsg, UInt16 errMsgLen) SEC004;
Boolean FtpRename(const char *oldName, const char *newName, char *errMsg, UInt16 errMsgLen) SEC004;

/* Deletes the CURRENT directory (FtpCurPath()) and everything inside it,
 * recursively - the Folder > Del Folder counterpart to New Folder's own
 * "acts on the directory I'm in" convention. Leaves the connection
 * positioned in the PARENT directory afterward (success or failure -
 * always tries to navigate back up). *outTruncated is set true if some
 * subfolder had more than the per-level entry cap and was only partially
 * processed (still attempts deletion of what it DID enumerate; never
 * silently drops entries without saying so). Fails outright (no changes
 * attempted) if already at the root directory. */
Boolean FtpDeleteCurrentFolderRecursive(Boolean *outTruncated, char *errMsg, UInt16 errMsgLen) SEC004;

/* ---- file transfer -----------------------------------------------------
 * Callback-driven, one call per chunk, matching ExgDBReadProcPtr/
 * ExgDBWriteProcPtr's own shape so Download.c/Upload.c can bridge directly
 * to ExgDBRead/ExgDBWrite without an intermediate buffer. Return false from
 * the callback to abort the transfer early (e.g. an ExgDBRead/Write error). */
typedef Boolean (*FtpRecvProc)(const void *data, UInt32 len, void *userData);
typedef Boolean (*FtpSendProc)(void *buf, UInt32 *len, void *userData);

Boolean FtpRetrieve(const char *name, FtpRecvProc recvProc, void *userData,
	char *errMsg, UInt16 errMsgLen) SEC004;
Boolean FtpStore(const char *name, FtpSendProc sendProc, void *userData,
	char *errMsg, UInt16 errMsgLen) SEC004;

/* Downloads a remote file and installs it as a real on-device database
 * (PRC/PDB) - the Download counterpart to FtpStoreDatabase. Same "can't
 * reuse the pull/push-mismatched high-level callback" reasoning as that
 * function - see its own comment. */
Boolean FtpRetrieveDatabase(const char *name, Boolean *outNeedsReset,
	char *errMsg, UInt16 errMsgLen) SEC004;

/* Uploads an installed database's raw on-disk bytes as one FTP file - the
 * Upload counterpart to the download side's text/PRC handling. Cannot reuse
 * FtpStore's own sendProc (a PULL callback - FtpStore calls it to ask for
 * the next chunk) because ROM's ExgDBWrite is a PUSH callback (it hands you
 * chunks as it produces them, on its own schedule) - opposite calling
 * directions with no real concurrency in this single-threaded app to bridge
 * them, so this does its own PASV+STOR/close/reply sequence (same shape as
 * FtpStore's) around a direct ExgDBWrite call instead. */
Boolean FtpStoreDatabase(const char *remoteName, const char *dbName,
	LocalID dbID, UInt16 cardNo, char *errMsg, UInt16 errMsgLen) SEC004;

#endif
