# Building the apps

How to build the `.prc` files in `apps/`. Everything here is built with the **prc-tools** m68k cross compiler for Palm OS; the host can be any Linux system that runs it. To put the result on a Palm see [INSTALL.md](INSTALL.md).

## The tools
| Tool | Used for |
|---|---|
| `m68k-palmos-gcc` (prc-tools, **gcc 2.95.3**) | Compiler and linker. It is an old compiler: C89, declarations at the top of a block. |
| `m68k-palmos-multigen` | Makes the segment glue and linker script for apps with several code segments. |
| `build-prc` | Packs the linked program and the resources into a `.prc`. |
| `pilrc` (3.2.90) | Compiles the `.rcp` resource files (forms, menus, alerts) to `.bin`. |
| `make`, `bash`, `python3` | The Makefiles and build scripts; Python only for the helper tools in some apps. |
| **Palm OS 4 SDK** headers and libraries | `sdk-4/include` and `sdk-4/lib/m68k-palmos-coff` (for `-lPalmOSGlue`). |

All the builds compile for **Palm OS 4** with `-palmos4` and need the Palm OS SDK headers and libraries. They are not part of this repository.

**Getting the SDK.** Clone the community **palm-os-sdk** collection, which holds the Palm OS 4 SDK as its `sdk-4` folder:

```
git clone https://github.com/jichu4n/palm-os-sdk.git
```

Put it anywhere you like; call that folder `<SDK>` below (so the headers are in `<SDK>/sdk-4/include` and the glue library in `<SDK>/sdk-4/lib/m68k-palmos-coff`). You can also use the SDK's original download from Palm/PalmSource if you have it, as long as it has the same layout.

**Telling the builds where it is.** The Makefiles and build scripts of the apps name the SDK folder near their top (a line starting `SDK`, `GLUE` or `INCLUDES`, with the path `$(HOME)/palmos_sdk/palm-os-sdk`, or `$HOME/palmos_sdk/palm-os-sdk` in the scripts). Either:
- clone the SDK to `$HOME/palmos_sdk/palm-os-sdk`, so those lines work unchanged, or
- change that path to your `<SDK>` in all of them with one command, run from the `apps/` folder (replace `/path/to/palm-os-sdk` with your folder):

```
grep -rlE 'palmos_sdk/palm-os-sdk' --include=Makefile --include='*.sh' . \
  | xargs sed -i -E 's#\$\(?HOME\)?/palmos_sdk/palm-os-sdk#/path/to/palm-os-sdk#g'
```

The shared source in `apps/lib/` is picked up with `-I../lib`, so build from inside each app's own folder and keep the folder layout as it is.

The tools must be on your `PATH`: check with

```
m68k-palmos-gcc --version     # 2.95.3
build-prc --version
pilrc                         # prints its usage
```

## Building one app
Go into the app's folder and run its build. The result is the `.prc` in that folder.

| App | Build | Output |
|---|---|---|
| Address35 | `make` | `Address35_v1.prc` |
| BitWarden | `make` | `Bitwarden.prc` |
| Bkp1 | `make` | `Bkp1.prc` |
| CalDAV Sync | `make` | `caldav_sync.prc` |
| CryptoNote | `make` | `CryptoNote_v1.prc` |
| lazyFTP | `make` | `LazyFTP_v1.prc` |
| Meshtastic | `make` | `meshtastic.prc` |
| WiRSaControl | `make` | `wirsacontrol.prc` |
| SASD driver | `make` (also `make LOG=1` for `SASD_log.prc`) | `SASD.prc` |
| Datebook35 | `./build.sh` | `Datebook35.prc` |
| ToDo35 | `./build.sh` | `ToDo35.prc` |
| Sec35 | `./build_sec35.sh` | `Sec35.prc` |
| EraNTP | `./build_eraNTP.sh` | `EraNTP.prc` |
| McPhling | `make out/McPhling4.prc` | `out/McPhling4.prc` |
| X-Master, X-Y2K35 (in `../hackmaster/`) | `make` | `X-Y2K35.prc` (X-Master-Light is supplied as a `.prc`) |

Example:

```
cd lazyFTP   # from the apps/ folder
make
```

Notes:
- **Datebook35 uses `build.sh`.** Its `Makefile` is an early sketch and does not build the full app.
- **McPhling:** build only the Palm OS 4 target (`make out/McPhling4.prc`). A plain `make` also tries targets that are not supported. If you change the source and an old `McPhling4.prc` is still there, delete it first.
- **SASD:** `make clean` between variants (`LOG=1`, `PAIR=0`, `EXTRA=...`).
- **Make does not notice header changes in every app.** After editing a shared header in `apps/lib/`, delete the app's object files (`rm -f *.o` or `rm -f build/*.o`) and build again. The build scripts do this for you.
- **A clean rebuild:** `make clean` where the Makefile has it, otherwise remove the build directory or the `*.o` and `*.bin` files.

## Building everything
There is no top-level build. Build the **support files first** and then each app:

```
cd ../hackmaster && make
cd ../apps/sec35        && ./build_sec35.sh
cd ../todo35            && ./build.sh
cd ../datebook35        && ./build.sh
cd ../address35         && make
cd ../cryptonote        && make
cd ../lazyFTP           && make
cd ../Bkp1              && make
cd ../bitwarden         && make
cd ../caldav_sync       && make
cd ../meshtastic_client && make
cd ../WiRSaControl      && make
cd ../eraNTP            && ./build_eraNTP.sh
cd ../SASD              && make
```

## How an app is put together
Knowing this helps when a build fails.

1. **Shared code** in `apps/lib/` (dates, encryption, the relay link, pickers) is compiled into each app that uses it.
2. **Segments.** A Palm OS code segment is limited to 32 KB, so the larger apps are split into several (`sec001`, `sec002`, ...) listed in the app's `.def` file (`multiple code { ... }`). Which function goes in which segment is set by an attribute in the source, using the macros in the app's `*_seg.h` header, which is force-included on the compile line (`-include xx_seg.h`). `m68k-palmos-multigen` turns the `.def` into glue code and a linker script.
3. **Resources.** The `.rcp` files are compiled by `pilrc` to `.bin` files (forms, menus, bitmaps, strings).
4. **Link and pack.** The objects are linked (with the glue and `-lPalmOSGlue`) and `build-prc -o <name>.prc <app>.def <program> *.bin` packs them with the resources. The `.def` file names the app and its creator ID.

## Common problems
| Problem | Cause and fix |
|---|---|
| `m68k-palmos-gcc: command not found` | The prc-tools are not on your `PATH`. |
| Missing `PalmOS.h` or other Palm headers | The build cannot find the SDK. Check that `<SDK>/sdk-4/include/PalmOS.h` exists and that the `SDK`/`INCLUDES` path in the Makefile or script points to your `<SDK>` (see above). |
| `undefined reference` to `Fld...`/`Win...` glue functions | The link is missing `-lPalmOSGlue` or its library path. |
| The link says a segment is too large (over 32 KB) | The code in that segment grew. Move functions to another segment by changing their section macro, or to the main segment if it has room. Each app's own document ("Segmentation") shows how full each segment is. |
| Calls jump to a wild address on the device but the build works | `-palmos4` was left out of the compile flags. It must be on every compile line. |
| `pilrc` errors on a form | A resource constant is missing or a limit was passed (pilrc 3.2 caps a table at three columns; some forms widen it in code). |
| The app runs in the emulator but not on a device, or the reverse | Build with the same `-palmos4` flag and test on the Palm OS 4.1 ROM first. |

## Where the details are
The app-level documents in `apps/` describe each app's own build in depth: `Datebook35.md` and `ToDo35.md` (build, segmentation and the era headers), `SASD_driver.md`, `Bkp1_improved.md`, `McPhling4.md`, `CryptoNote.md`, `lazyFTP.md`, `Bitwarden.md`, `CalDAVSync.md`, `meshtastic_client.md` and `WiRSaControl.md`.
