# Bkp1 improved — everything changed from the original 1.2

> **Credit and thanks.** Bkp1 was written by Dmitry as part of his SASD project, and the
> original source comes from his release:
> <https://dmitry.gr/?r=05.Projects&proj=32.%20SASD#_TOC_99cac7b4168b96e0ddda01bf7bdfdd7e>.
> Thank you, Dmitry, for releasing the original source — that is what made it possible to find
> these faults and improve on it. Everything below builds on his work.

**Bkp1** is a third-party SD-card backup/restore tool for Palm OS ("the PalmOS 1.0 compatible
backup tool", © 2022 palmpowerups.com, creator `PpB1`). This document lists every change made to
it in `hackmaster_y2035/apps/Bkp1/` on 2026-10-10, measured against the pristine source in the
SASD release (`SASD_release/software/Bkp1/`). It is the single reference for this copy of Bkp1.

**What it does.** Bkp1 backs up every RAM database on card 0 (except itself and `psysLaunchDB`)
to `/PALM/PROGRAMS/BKP1/<backup name>/` on an expansion card, one file per database
(`<index>.PDB` / `.PRC`, 8-hex-digit index, standard PDB/PRC format), and restores a set followed
by a reset. Paths below are relative to `hackmaster_y2035/apps/Bkp1/`.

**Files.**

```
Src/bkp1.c              UI (backup list, progress form), startup, end-of-restore report
Src/ops.c               backup / restore / delete / rename / enumerate over VFS; scan + selective restore
Src/choose.c, choose.h  selective restore: grouping by owner, presets, the chooser screens (§4)
Src/VfsExportImport.c   one database <-> one file (thin wrapper, progress callbacks)
Src/ExgDB.c             the PDB/PRC writer (MyExgDBWrite) and reader (MyExgDBRead) - "from RetroFix"
Src/ExgDB.c.orig, ops.c.orig   the 1.2 sources of the two files changed most
Rsc/res.rcp             forms, alerts, error-name tables;  Rsc/version.rcp  version (gcc build)
compat/VfsMgr.h         header-name shim (sources say VfsMgr.h, SDK file is VFSMgr.h)
Makefile, Bkp1.def      prc-tools build (§5);  bkp1.mcp = the original CodeWarrior project
Bkp1.prc                current build (1.3);  Bkp1_orig.prc = the original 1.2 binary
test/                   reader/writer test and the embedded-backup build, both for POSE64 (§7)
tools/                  host scripts that inspect a backup folder (§7)
```

| | Original | Improved |
|---|---|---|
| Version | 1.2 | **1.3** |
| File | `Bkp1_orig.prc`, 16,917 bytes | `Bkp1.prc`, 26,600 bytes |
| Built with | CodeWarrior (`bkp1.mcp`) | prc-tools (`make`) |
| Code size | 13,010 bytes | 21,656 bytes (one segment, limit 32,768) |

Backup, Delete and Rename behave as before, and **the backup file format is unchanged**:
backups made by either version restore with either, except that the original crashes or stops
on the cases in §1 and §3.

Size of the change (lines added / removed per original file): `bkp1.c` +162/−16, `ops.c`
+425/−63, `ops.h` +38/−1, `ExgDB.c` +27/−7, `VfsExportImport.c` +3/−4, `res.rcp` +41/−0.
New files: `Src/choose.c` (550 lines), `Src/choose.h`, `Makefile`, `Bkp1.def`,
`Rsc/version.rcp`, `compat/VfsMgr.h`, `test/*`, `tools/*`.

---

## 1. Restore no longer crashes on deleted or archived records

**Symptom in 1.2:** a restore died partway with `MemoryMgr.c, Line:4365, NULL handle`. Whether it
happened depended only on what was in the backup.

**Cause:** `MyExgDBRead` (`Src/ExgDB.c`) mishandled records an app had deleted but that were
still listed in the database, waiting for a HotSync that never comes for apps without a conduit:
- a **stub** (`DmDeleteRecord`): a list entry with no data;
- an **archived record** (`DmArchiveRecord`, the "Save archive copy on PC" tick): data kept,
  deleted flag set.

Two faults: (a) a record with data was created one step late while a stub was appended at once,
so records came back in the wrong order; (b) data was matched to records by counting and fetched
with `DmGetRecord`, which returns nothing for any record carrying the deleted flag — and that
NULL went straight to `MemHandleLock`.

Reproduced in POSE64 with the original code: stubs at the end restore, but the last live record
moves behind them; a stub ahead of a live record, or an archived record that is not the last
record with data, gives exactly that fatal alert. A successful restore with 1.2 can also leave a
database in the state that crashes the next restore.

**Why the HackMaster Y2K35 suite makes it likely:** Datebook35, ToDo35 and CryptoNote delete the
standard way (stub or archived record, moved to the end), but they have no HotSync conduit, so
nothing ever purges those entries and they accumulate. (Address35 uses `DmRemoveRecord`.) It has
nothing to do with Sec35 encryption (a restore copies bytes) or with X-Master / X-Y2K35 (an
enabled hack's database is protected; Bkp1 renames what it cannot delete).

**Change (`Src/ExgDB.c`, record databases only):**
- new locals `prevIdx` (the pending record's position in the file's list) and `recCursor`;
- the pending record is created with `DmNewRecord` **at `prevIdx`**, and the last record is
  attached with `DmAttachRecord` at `prevIdx`, instead of always at the end — the restored order
  now equals the backed-up order;
- the data pass walks the records with `DmRecordInfo`, skips entries with no data, and locks each
  chunk by its LocalID (`MemLocalIDToLockedPtr`) instead of `DmGetRecord`;
- every chunk is unlocked after it is filled (`MemPtrUnlock`); 1.2 left them all locked until the
  reset;
- a short read inside a chunk now leaves through the normal cleanup (`break`) instead of jumping
  past it.

Resource databases and the file format are unchanged, so **old backups need no conversion**: the
files were always correct, only the reader was wrong. `tools/scan_bkp1.py <backup folder>` lists
the databases that would crash 1.2. Original kept as `Src/ExgDB.c.orig`.

## 2. One bad file no longer abandons the restore; failures are reported once, at the end

**In 1.2:** an error at a file's open / size / header step stopped the whole restore — no reset
prompt, later databases not restored — and an import error stopped to show an alert each time.

Seen on real hardware: `vfsErrBadData` near the end of an old backup, twice at the same file.

**Change (`Src/ops.c` `backupRestore`, `Src/bkp1.c`):**
- a failing file is **noted and skipped**; nothing is shown while the restore runs;
- after the last file, **one alert** lists what was skipped — count, then up to 4 lines of
  "name: error" (the database name, or the file name such as `0000002A.PDB` when its header
  could not be read; error names from the app's own `err!` tables), then "(and more)" — followed
  by the usual "Restore completed. Device must now reset";
- three failures in a row are treated as a card problem: the restore stops and shows that error
  plus the list. Cancel still stops it.
- In the file-counting pass, a file whose size cannot be read is still counted.

New in `bkp1.c`: `struct RestoreFail`, `mRestoreFails[]`, `mNumRestoreFails`,
`MAX_RESTORE_FAILS_LISTED`, `uiPrvAppendErrName`, `uiPrvRestoreReport`; the restore fail handler
`uiPrvOpRestoreErrHandlerF` now records instead of alerting, and reports when called with
`name == NULL`. `ops.h` documents that contract on `BackupFailHandlerF`.

## 3. Restore no longer rejects a good file because of `VFSFileDBInfo`

**In 1.2:** restore called `VFSFileDBInfo` on each file, only to get the database name for the
progress screen. On real hardware that call returned `vfsErrBadData` for a sound file
(AsciiChart, whose header dates are all zero — the presumed reason; not confirmed by
disassembly), which stopped the restore.

**Change (`Src/ops.c`):** the name is read from the first 32 bytes of the file with
`VFSFileRead`; a file too short to be a database, or with no terminated name, is reported as
`dmErrCorruptDatabase`. Validation is otherwise left to the importer.

## 4. Selective restore — choose what goes back, and see what belongs to what (new in 1.3)

**Restore now asks:** *Everything* / *Choose...* / *Cancel* (alert 1003). "Everything" is the
old behaviour.

**Choose... (new `Src/choose.c`, `Src/choose.h`; forms 1100 and 1110 in `Rsc/res.rcp`):**
- **Scan** — `backupScan` (`ops.c`) reads each file's 78-byte header (name, type, creator,
  resource/record, entry count) and its size into a `struct BackupItem` array. Everything comes
  from the files themselves, so **every existing backup works**; no listing file is written.
- **Grouping** — `chooseBuildGroups`: a file's owner is the application, hack or panel in the
  same backup with the same **creator ID**. Data of ROM applications and of the system is
  recognised from a short table (`mKnown`). Shared libraries (`libr`), drivers and extensions
  (`libf` / `libs` / `extn` / `extx`), and the two system preference databases each get their own
  group. Anything left over goes to "Other data". X-Master (`ExtM`) is classed with the hacks,
  because its data is the list of active hacks.
- **Group screen** — one line per owner: a tri-state box (none / some / all), the name, the file
  count and size, and a summary line "N of M files, size". Tap the **box** to tick or untick the
  whole group; tap the **name** to select the line, then **Files...** to see that group's files.
- **Files screen** — each file with its own box, its kind (App / Data / Hack / Panel / Driver /
  Prefs / Lib) and size.
- **Presets** ("Tick:" popup, `chooseApplyPreset`): *Everything*; **Apps and data** (the default
  — applications and their data, built-in apps' data, other data, shared libraries; no hacks,
  panels, drivers, extensions or system settings); *Data only*; *Nothing*.
- **Confirmation** (alert 1002) states how many files and what happens to settings, then the
  restore runs (`backupRestoreSelected`) with the same skip-and-report rules as §2 and resets.

**Preferences are filtered per app (`backupPrvMergePrefs`, `ops.c`).** "Saved Preferences" and
"Unsaved Preferences" each hold every app's settings plus the system's own, one resource per
setting, tagged with its owner's creator ID.
- If one of them is **ticked**, it is replaced whole, as before.
- If it is **not ticked**, every entry in the backup's copy whose owner has at least one ticked
  file is copied onto the device with `PrefSetAppPreferences` (resource contents = 2-byte
  version + data, exactly what that call stores). Entries of unticked owners, and all system
  (`psys`) entries, are left as they are on the device.

So an app comes back with its settings while device-level settings stay untouched. This matters
for Sec35: its passcode data lives in Saved Preferences under Sec35's own ID, so **encrypted
records only unlock after a restore if Sec35 is ticked too** (both "Everything" and "Apps and
data" include it; "Data only" does not).

Note for the HackMaster Y2K35 suite: its apps need X-Master and X-Y2K35, which "Apps and data"
leaves out. Tick those groups as well, or install them separately.

Other code for this in `ops.c`: the per-file work of a restore was pulled out of `backupRestore`
into `backupPrvRestoreFile`, shared by both restore paths; `backupItemIsPrefsDb`;
`backupPrvCreatorSelected`. `ops.h` gained an include guard, `struct BackupItem` and the three
new prototypes. `bkp1.c` `uiPrvOpRestore` drives the new flow.

## 5. Build: prc-tools instead of CodeWarrior

Needed because CodeWarrior is not available here. None of this changes behaviour, and the
sources still compile with CodeWarrior.
- **`Makefile`, `Bkp1.def`** — `make` builds `Bkp1.prc` with `m68k-palmos-gcc -palmos4 -Os`,
  linking `libPalmOSGlue` (for `WinGlueDrawTruncChars`).
- **`Src/bkp1.c` entry point** — `PilotMain` under `__GNUC__` (prc-tools' startup code has already
  called `SysAppStartup`); the original hand-written `__Startup__` otherwise. Done with three
  `BKP1_ENTRY_*` macros around the unchanged body.
- **`Src/bkp1.c`** — `char name[]` → `char name[1]` in `struct BackupEntry` (gcc 2.95 rejects the
  open-ended form; allocations already add `StrLen + 1`).
- **`Src/VfsExportImport.c`** — the anonymous union in `struct ProgressState` became two plain
  members (no anonymous unions in gcc 2.95 C).
- **`compat/VfsMgr.h`** — the sources include `<VfsMgr.h>`; the SDK file is `VFSMgr.h`.
- **`Rsc/version.rcp`** — the version resource (CodeWarrior took it from the project settings).
- Resource differences in the PRC: `tver` is ID 1 instead of 1000, and prc-tools adds a `pref`
  resource. The original forms, alerts, icons and error tables are byte-identical.

## 6. Resources added (`Rsc/res.rcp`)

| ID | What |
|---|---|
| Alert 1002 | confirmation with *Restore* / *Back* |
| Alert 1003 | *Everything* / *Choose...* / *Cancel* |
| Form 1100 | "Choose what to restore": group list, summary field, "Tick:" preset popup, *Restore* / *Files...* / *Cancel* |
| Form 1110 | one group's files, *Done* |

## 7. Test and support files added

- **`test/exgtest.c` + `build.sh` + `run_pose.py`** — drives the PDB/PRC writer and reader through
  a memory buffer in POSE64 (no card needed): 34 record-database cases mixing live, deleted and
  archived records, with and without an AppInfo block, plus 7 resource-database cases.
  `test/build.sh [dir with ExgDB.c]` builds **ExgTest.prc**; `test/run_pose.py <prc>` runs it.
  Each launch builds a database from a pattern (A = live, N = stub, R = archived), exports it
  with `MyExgDBWrite` into memory, re-imports it with `MyExgDBRead`, and compares record count,
  order (unique IDs), attributes, data and the AppInfo block. With the original reader, plain
  databases pass, stubs at the end fail the order check, and a stub before a live record or an
  archived record (`ANA`, `ARA`, `RAA`) crashes.
- **`test/fakevfs.c`, `fakevfs.h`, `build_fake.sh`** — a test-only build (`-DBKP1_FAKE_VFS`) of the
  real app whose card calls read a backup embedded in the PRC, so the real screens and the real
  restore run in POSE64. The only trace in the shipped sources is one `#ifdef` in `bkp1.c`
  (skips the "expansion card support needed" check) and one in `compat/VfsMgr.h`.
- **`tools/check_bkp1.py`** — host script: lists and validates every file of a backup folder.
- **`tools/scan_bkp1.py`** — host script: finds the databases that crash the original 1.2.
- **Kept originals:** `Bkp1_orig.prc`, `Src/ExgDB.c.orig`, `Src/ops.c.orig` (the latter two are
  the 1.2 sources of the files changed most).

## 8. What was verified, and how

| Change | Verified by |
|---|---|
| §1 reader fix | POSE64, real Data Manager: all 41 cases pass; the original reproduces the wrong order and the exact fatal alert |
| §1–§3 on real data | a real 71-file backup restored through the app's own code in POSE64: 0 failures, device boots |
| §3 name read | the file that failed on hardware (AsciiChart) restores in POSE64 |
| §4 chooser | POSE64 with the embedded real backup: grouping (0 files without an owner), presets, group and per-file ticking, Cancel |
| §4 selective restore | "Apps and data" (48 of 71): resets, boots; apps, data and their settings present (Sec35, Datebook35, Address35, CryptoNote entries); hacks, drivers, panels and device-level settings (AfterBurner, battery panel, driver speed) absent |
| §4 "Everything" | 71 of 71 restored, resets, boots |
| Real hardware | restore of a previously crashing backup completed (user, 2026-10-10); after the 1.3 build the user reported it "seems to work well" - which flows were run on the device is not recorded |

Not exercised anywhere: the three-failures-in-a-row stop (§2), and a report with more than four
skipped files.

## 9. Known limits and open points

- **Dependencies between apps** are not detected (only "same creator" ownership is). Shared
  libraries are included in "Apps and data" for that reason.
- **Data whose app is neither in the backup nor a known ROM app** appears under "Other data".
- **Restoring to a different device:** Sec35's key includes the device serial, so the same
  passcode is rejected there, and the 10th wrong attempt wipes the encrypted records.
- **A boot loop seen on hardware** after restoring an old full backup onto a freshly reset
  device (false "erase all data?" prompt, reset on any hardware button) could not be reproduced
  in POSE64 and was not root-caused. The same backup boots in the emulator. The working
  assumption is device-level settings in the old preference databases; restoring with "Apps and
  data" avoids replacing them.
- **Sec35's auto-lock alarm** is registered against its own database, which a restore replaces;
  if it fired before the final reset the OS would launch a stale database ID. Not seen.
- **The suite's apps never purge deleted or archived entries** (no HotSync conduit). Harmless
  with the fixed reader; they take space indefinitely.
- `Src/choose.c` and `Src/choose.h` use Unix line endings; the original sources use Windows ones.
