# SASD

SASD lets the Palm use a card that sits in the **WiRSa** (its SD card, or its built-in 12 MB virtual card) as if it were in the Palm's own expansion slot. Bkp1, CryptoNote, Address35 and any file manager then see a card called `ESP32` (virtual) or the SD card's own name. SASD is a driver: it has no screen of its own. It is Dmitry's SASD project, with changes that make it work over slow and noisy links.

## 1. Where to put the files

The install set is the folder `SASD_Install` in the repository. Its contents must end up at **the root of the card** the WiRSa serves, so that the full path of each file is:

```
/PALM/PROGRAMS/SASD/ExpansionMgr.prc
/PALM/PROGRAMS/SASD/SlimFat.prc
/PALM/PROGRAMS/SASD/RetroFix.prc
/PALM/PROGRAMS/SASD/SASD.prc
/PALM/PROGRAMS/SASD/SASDprefs.prc
/PALM/PROGRAMS/SASD/Mgr1.prc
/PALM/PROGRAMS/SASD/Bkp1.prc
/PALM/PROGRAMS/SASD/wirsacontrol.prc
```

In other words, copy the whole `PALM` folder from `SASD_Install` to the top of the card, not the `SASD` folder on its own.

| Card | How to copy |
|---|---|
| SD card | Put it in a computer and copy `PALM` to the card's top folder. |
| Virtual card | Start the WiRSa's **FTP Server**, log in (default user `admin`), open the **ESP32** root and upload to `/PALM/PROGRAMS/SASD/` (create the folders if they are not there). |

Any other app you want installed goes in `/PALM/PROGRAMS/INSTALL/`. It is installed once and then moved to `/PALM/PROGRAMS/INSTALLED/`.

## 2. Install on the Palm

1. On the WiRSa choose **HotSync** from the main menu ([WiRSa](WiRSa.md)).
2. On the Palm, start **HotSync** (the cradle button). The WiRSa installs everything in `/PALM/PROGRAMS/SASD/` (files already on the Palm are skipped) and then the files in `INSTALL`. The Palm resets when it has finished.
3. Open **WiRSaCtl** ([WiRSaControl](WiRSaControl.md)) once, it was installed too.

To replace the driver later: delete `SASD` on the Palm, soft-reset, install the new `SASD.prc`, soft-reset.

## 3. Use

1. On the WiRSa choose **SAS[D] SDCard** (or use WiRSaCtl > Modes). The WiRSa serves the card named in WiRSaCtl > Setup > **Disk**: *SD card*, *Virtual* or *Auto* (the SD card if there is one, else the virtual card).
2. On the Palm the card appears at once in Bkp1, CryptoNote and the others.
3. To leave SASD, press the WiRSa's Back button, or reset it. After that the Palm may keep listing the card until its next access fails. This is normal.

SASD ends by itself after the quiet time set in WiRSaCtl > Setup > Exit Timers (**SASD Timeout**).
