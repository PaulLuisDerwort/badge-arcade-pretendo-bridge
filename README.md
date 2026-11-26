# Badge Arcade Pretendo Bridge

Play Nintendo Badge Arcade (EUR, update 1.3.1) offline with the original crane games, even while your 3DS runs in **Pretendo mode** through Nimbus.

This is based on [badge-arcade-offline v0.5.4](https://github.com/GrouchoGlasses42/badge-arcade-offline) by GrouchoGlasses42 (MIT). That patch starts the game without servers and without the NNID check. The bridge adds **a single hook** (see `bridge.patch`):

| Address (1.3.1) | Original | Bridge |
|---|---|---|
| `0x2CD6CC` (1.3.0: `0x2CD3A0`) | Checks that the account ID stored in the save matches the active account | Accepts the save under any account (NNID or PNID) |

The next time the game saves, it writes the active account's ID as usual. Every other byte of the IPS is identical to the official v0.5.4 release; the unmodified build was reproduced byte for byte to confirm this.

## Install directly on the 3DS

**Back up first:** Save the Badge Arcade extdata with Checkpoint and dump your HOME Menu badges with Simple Badge Injector.

### Option 1: FBI (recommended)

1. Open **FBI** → **Remote Install** → **Scan QR Code** and scan `fbi-qr.png`. FBI installs the *Pretendo Bridge Installer* app from the latest release.
2. Open **Pretendo Bridge Installer** from the HOME Menu. It checks Luma3DS, the game and its update version, and any patch already on the SD card.
3. Press **A** (badges go to the HOME Menu) or **Y** (badges stay in the game). The app writes the patch and turns on Luma's *Enable game patching* if it is off (after backing up `luma/config.ini` to `luma/config.ini.pretendo-bridge.bak`). If a restart is needed, press **SELECT**.
4. Launch Badge Arcade. *Reading latest data* can take a while on an Old 3DS.

**X** in the app removes the bridge again. The app has no network access; both patch variants are built into it.

### Beta: switch catalog modes on the console (v1.1.0 beta)

Scan `fbi-qr-beta.png` in FBI instead to try the newest test build (it always points to the `beta` release). In the app, pick the **Mode** with LEFT/RIGHT (L/R jumps 10 weeks), the **Badges** variant on the second row, and press **A**:

| Mode | Machines | What the app needs |
|---|---|---|
| Vanilla | 384 from the installed update | nothing, installs instantly |
| Expanded | 398 + 8 bonus layouts | your installed game (read directly from the console) |
| Week | every machine of one archived SpotPass week | your installed game, the week's pack and the badge catalog |

The app builds Expanded and weeks on the console from your own game (badge-arcade-offline's native builder, ported to the 3DS) and swaps them into `luma/titles/0004000000153600/`. Building takes a few minutes; switching back to Vanilla is instant.

- **Weeks and badge catalog:** put SpotPass packs (`data_v131-…-EU.enc`, `.boss`, or decrypted `.sarc`) and the full catalog `allbadge_v131…` in `SD:/3ds/pretendo-bridge/spotpass/`, then press **Y** to rescan. Encrypted packs are decrypted on the console with its own SpotPass key; no boot9 dump is needed. If your console still has SpotPass data from back then, its last week and catalog show up without any files. This project does not include or download any SpotPass data.
- **Log:** every message, with error codes and the step that failed, goes to `SD:/3ds/pretendo-bridge/log.txt`.
- **If the app can't read the game's files** (it uses the installed titles' RomFS), export `base-pc.sarc`, `pc.sarc` and `Schedule.xml` as in badge-arcade-offline's README (step 3) to `SD:/3ds/pretendo-bridge/input/`.

### Option 2: Universal-Updater

1. **Universal-Updater** → Settings → *Select UniStore* → **+** → **Scan QR code** → scan `unistore-qr.png`.
2. Open the *Badge Arcade Pretendo Bridge* store and choose **Install: badges go to HOME Menu** or **Install: badges stay in game**.
3. Luma3DS: **Enable game patching** must be on (it usually already is if you use Nimbus/Pretendo).
4. Launch Badge Arcade.

### Option 3: by hand

Copy `ips/<variant>/code.ips` to `SD:/luma/titles/0004000000153600/code.ips`.

## Variants

- **home-menu:** Badges you win go to the HOME Menu badge box. This can replace custom (GYTB) badges.
- **game-only:** Badges stay in the game; the HOME Menu is not touched.

## Switching between Nintendo and Pretendo

- **Save data:** Thanks to the bridge, it is valid in both modes.
- **HOME Menu badges:** The HOME Menu itself ties the badge box to the active account. After every switch in Nimbus, run the NNID/PNID fix option in **Simple Badge Injector** **before** you launch Badge Arcade. You may have to rearrange placed badges afterwards.

## Limitations

- **EUR, update 1.3.1 only.** USA and JPN are not supported.
- The extra hook has been confirmed working on one console so far. The original project's test suite runs without the game code (61 passed, 442 skipped).
- An existing Reclawed patch gets replaced. This patch does not read badges that Reclawed stored only inside the game.
- To uninstall: **X** in the installer app, *Uninstall* in the store, or delete `code.ips`.

## Build it yourself

```sh
git clone https://github.com/GrouchoGlasses42/badge-arcade-offline
cd badge-arcade-offline && git apply ../bridge.patch
python3 src/badge_patch.py --game-version 1.3.1 --home-menu --package ../home-menu.zip
python3 src/badge_patch.py --game-version 1.3.1 --package ../game-only.zip
```

Requires Python 3.11+ and clang with ARM support.

### The installer app

The app lives in `installer/` (C, libctru, console UI). GitHub Actions builds it in the `devkitpro/devkitarm` image on every push (`.github/workflows/installer.yml`). When the version in `installer/Makefile` has no release yet, the workflow publishes one (tag `v<version>`) with the `.cia` and `.3dsx`. To build locally with devkitARM, makerom and bannertool on the PATH:

```sh
bash .github/scripts/build_installer.sh   # -> dist/pretendo-bridge-installer.{cia,3dsx}
```

## License and notes

MIT, see `LICENSE` (the original license of badge-arcade-offline). The installer's `luma_config`, `luma_system` and `fileops` sources, its CIA spec (`app.rsf`) and the pinned makerom/bannertool versions come from [Badge Arcade Reclawed](https://github.com/giunzione/badge-arcade-reclawed) (MIT, see `installer/LICENSE-reclawed`). Contains no Nintendo code and no game data. Unofficial fan project, not affiliated with Nintendo or Pretendo Network. The bridge was created with AI assistance. Use at your own risk and always keep backups.
