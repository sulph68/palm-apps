# Installing the apps on the Palm

The apps are **`.prc` files**. Any way of getting a `.prc` onto the Palm works: the Palm does not care how it arrived. This guide uses the simplest one that needs no computer on the Palm side, the **HotSync mode of the WiRSa**, and then lists the others.

## What to install
Each app's file is in its own folder under `apps/` (build instructions are in [BUILD.md](BUILD.md)).

| App | File | Needs |
|---|---|---|
| X-Master | `hackmaster/X-Master-Light.prc` | Nothing. A manager for hacks; install it first. |
| X-Y2K35 (era plugin) | `hackmaster/X-Y2K35.prc` | X-Master. Tick it on in X-Master. |
| Sec35 (shared password) | `sec35/Sec35.prc` | Nothing. |
| Datebook35 | `datebook35/Datebook35.prc` | X-Y2K35 enabled. Install ToDo35 and Address35 too. |
| ToDo35 | `todo35/ToDo35.prc` | X-Y2K35 enabled. |
| Address35 | `address35/Address35_v1.prc` | Sec35. |
| CryptoNote | `cryptonote/CryptoNote_v1.prc` | Sec35. |
| EraNTP | `eraNTP/EraNTP.prc` | X-Y2K35, a network link. |
| lazyFTP | `lazyFTP/LazyFTP_v1.prc` | Sec35 (saved passwords), a network link. |
| BitWarden | `bitwarden/Bitwarden.prc` | Sec35, a WiRSa. |
| CalDAV Sync | `caldav_sync/caldav_sync.prc` | Sec35, a WiRSa. Datebook35 and Address35 show what it syncs. |
| Meshtastic | `meshtastic_client/meshtastic.prc` | A WiRSa and a Meshtastic node. |
| WiRSaControl | `WiRSaControl/wirsacontrol.prc` | A WiRSa. |
| Bkp1 | `Bkp1/Bkp1.prc` | A card (SD, or the WiRSa's through SASD). |
| McPhling | `McPhlingSource/out/McPhling4.prc` | X-Master (it is a hack). |
| SASD driver set | `SASD_Install/PALM/PROGRAMS/SASD/*.prc` | Install together. See [SASD](user_guides/SASD.md). |

Install the support files first (X-Master, X-Y2K35, Sec35), then the apps. The apps themselves are described in the [user guides](user_guides/README.md).

## Install with the WiRSa's HotSync mode
The WiRSa acts as the HotSync cradle. It installs every `.prc` and `.pdb` it finds in two folders of a card, with no computer and no HotSync software.

**You need:** the WiRSa, the serial cable to the Palm, and a card: a micro SD card, or the WiRSa's built-in **virtual card** (12 MB, name `ESP32`). The Palm needs no card.

### 1. Put the files on the card
The folders on the card are:

| Folder | Use |
|---|---|
| `/PALM/PROGRAMS/INSTALL/` | Ordinary apps. They are installed again every time they are found and then **moved** to `INSTALLED/`, so they are not reinstalled by the next sync. |
| `/PALM/PROGRAMS/SASD/` | The SASD driver set. Installed once; files already on the Palm are skipped. The Palm resets after a driver is installed. |
| `/PALM/PROGRAMS/INSTALLED/` | Where finished installs land (created for you). |

The WiRSa creates these folders itself the first time it starts HotSync, if they are missing. Put the files there by one of these ways:

- **SD card in a computer:** copy the `.prc` files into `/PALM/PROGRAMS/INSTALL/` (create the folders if needed). Put the card back in the WiRSa.
- **FTP to the WiRSa (SD card or virtual card):** on the WiRSa choose **FTP Server > Start** and connect an FTP client to its IP address (see the [WiRSa guide](user_guides/WiRSa.md)). The root has two folders, **SDCard** and **ESP32** (the virtual card). Upload into `/ESP32/PALM/PROGRAMS/INSTALL/` for the virtual card, or `/SDCard/PALM/PROGRAMS/INSTALL/` for the SD card. The FTP server can stay running during HotSync, but do not upload while a sync is in progress.
- **From another Palm or the same one:** with the card visible through SASD, a file manager such as Mgr1 can copy files into the folder.

### 2. Choose the card
The WiRSa installs from the card named by its **Disk** setting: **Auto** (the SD card if there is one, otherwise the virtual card), **SD card** or **Virtual**. Set it in WiRSaControl > Setup > Disk, if you use both cards.

### 3. HotSync
1. On the WiRSa choose **HotSync** from the main menu (or press its **BOOT** button, or type `H` on the serial console). The screen says `State: Waiting`.
2. On the Palm press the HotSync button, or open the HotSync application and tap the HotSync icon (set to *Local*). Use the serial cable to the WiRSa.
3. The WiRSa shows `Connected`, then `Syncing` with each file name, then `Completed`. If a file in the `SASD` folder was installed it says `Init Complete`, and the Palm **resets itself**: this is normal.
4. Press **BACK** on the WiRSa to leave. It also leaves by itself after the quiet time (45 s if nothing starts, 15 s after a finished sync).

The apps are now in the Palm's launcher. An app that needs another (see the table) will tell you what is missing when you open it.

## Other ways to get a `.prc` onto the Palm
All of these end in the same place; use whichever you have.

| Way | How |
|---|---|
| **A computer's HotSync** | Add the `.prc` in the desktop's Install Tool (or HotSync Manager) and HotSync. |
| **Network HotSync through the WiRSa** | The Palm HotSyncs to a computer over the WiRSa's SLIP gateway. See *Network HotSync* in the [WiRSa guide](user_guides/WiRSa.md). |
| **The WiRSa's FTP server and lazyFTP** | Start the WiRSa's FTP server, connect lazyFTP to it (over SLIP), tap a `.prc` twice and choose **Download**: it is installed at once. [lazyFTP guide](user_guides/LazyFTP.md). |
| **Another FTP server and lazyFTP** | Same, from any FTP server the Palm can reach. |
| **A card and a file manager** | With the card visible (a card in the Palm, or SASD), open the `.prc` in a file manager such as Mgr1 and copy it to the Palm. |
| **Beaming** | Send the app from another Palm over infrared. |
| **A Palm emulator** | `install file.prc` in POSE, for testing. |

## After installing
- **X-Master:** open it, tick **X-Y2K35**, and any other hacks you want.
- **Sec35:** open it and set the password once ([Getting started](user_guides/Getting_started.md)).
- **WiRSaControl:** open it with the WiRSa connected; it finds the WiRSa by itself.

## If an install fails
| Symptom | Try |
|---|---|
| The WiRSa says "no card" for HotSync | Put a card in, or set Disk to Virtual (WiRSaControl > Setup). |
| Nothing installs | The files must be in `INSTALL` or `SASD` of the card the Disk setting picks; names starting with a dot are ignored. |
| A new version does not replace the old one | Files in `INSTALL` are reinstalled over the old copy every time. The `SASD` folder skips files already on the Palm, so delete the old driver on the Palm first. A hack that is switched on in X-Master may refuse to be replaced: switch it off first. |
| An app says a plugin is not enabled | Open X-Master and tick X-Y2K35. |
