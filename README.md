# Badge Arcade Pretendo Bridge

Play Nintendo Badge Arcade (EUR, update 1.3.1) offline with the original crane games, even while your 3DS runs in **Pretendo mode** through Nimbus.

This is based on [badge-arcade-offline v0.5.4](https://github.com/GrouchoGlasses42/badge-arcade-offline) by GrouchoGlasses42 (MIT). That patch starts the game without servers and without the NNID check. The bridge adds **a single hook** (see `bridge.patch`):

| Address (1.3.1) | Original | Bridge |
|---|---|---|
| `0x2CD6CC` (1.3.0: `0x2CD3A0`) | Checks that the account ID stored in the save matches the active account | Accepts the save under any account (NNID or PNID) |

The next time the game saves, it writes the active account's ID as usual. Every other byte of the IPS is identical to the official v0.5.4 release; the unmodified build was reproduced byte for byte to confirm this.

## Install directly on the 3DS

1. **Back up:** Save the Badge Arcade extdata with Checkpoint and dump your HOME Menu badges with Simple Badge Injector.
2. **Universal-Updater** → Settings → *Select UniStore* → **+** → **Scan QR code** → scan `unistore-qr.png`.
3. Open the *Badge Arcade Pretendo-Bridge* store and choose **Installieren: Badges ins HOME-Menue** (badges go to the HOME Menu) or **Installieren: Badges nur im Spiel** (badges stay in the game).
4. Luma3DS: **Enable game patching** must be on (it usually already is if you use Nimbus/Pretendo).
5. Launch Badge Arcade. *Reading latest data* can take a while on an Old 3DS.

Without Universal-Updater: copy `ips/<variant>/code.ips` to `SD:/luma/titles/0004000000153600/code.ips`.

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
- To uninstall: store → *Deinstallieren*, or delete `code.ips`.

## Build it yourself

```sh
git clone https://github.com/GrouchoGlasses42/badge-arcade-offline
cd badge-arcade-offline && git apply ../bridge.patch
python3 src/badge_patch.py --game-version 1.3.1 --home-menu --package ../home-menu.zip
python3 src/badge_patch.py --game-version 1.3.1 --package ../game-only.zip
```

Requires Python 3.11+ and clang with ARM support.

## License and notes

MIT, see `LICENSE` (the original license of badge-arcade-offline). Contains no Nintendo code and no game data. Unofficial fan project, not affiliated with Nintendo or Pretendo Network. The bridge was created with AI assistance. Use at your own risk and always keep backups.
