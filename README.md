# Badge Arcade Pretendo-Bridge

Nintendo Badge Arcade (EUR, Update 1.3.1) offline spielen, mit den originalen Kranspielen, auch wenn der 3DS über Nimbus im **Pretendo-Modus** läuft.

Basis ist [badge-arcade-offline v0.5.4](https://github.com/GrouchoGlasses42/badge-arcade-offline) von GrouchoGlasses42 (MIT). Dieser Patch startet das Spiel ohne Server und ohne NNID-Abfrage. Die Bridge ergänzt **einen einzigen Hook** (siehe `bridge.patch`):

| Adresse (1.3.1) | Original | Bridge |
|---|---|---|
| `0x2CD6CC` (1.3.0: `0x2CD3A0`) | Prüft, ob die Konto-ID im Spielstand zum aktiven Konto passt | Akzeptiert den Spielstand unter jedem Konto (NNID oder PNID) |

Beim nächsten Speichern schreibt das Spiel wie gewohnt die ID des aktiven Kontos. Alle anderen Bytes des IPS sind identisch mit dem offiziellen v0.5.4-Release; der unveränderte Build wurde dafür byte-genau reproduziert.

## Installation direkt auf dem 3DS

1. **Backup:** Extdata von Badge Arcade mit Checkpoint sichern, HOME-Badges mit Simple Badge Injector dumpen.
2. **Universal-Updater** → Einstellungen → *Select UniStore* → **+** → **QR-Code scannen** → `unistore-qr.png` scannen.
3. Store *Badge Arcade Pretendo-Bridge* öffnen → **Installieren: Badges ins HOME-Menue** (oder *nur im Spiel*).
4. Luma3DS: **Enable game patching** muss an sein (für Nimbus/Pretendo ist es das in der Regel schon).
5. Badge Arcade starten. *Reading latest data* kann auf einem Old 3DS lange dauern.

Ohne Universal-Updater: `ips/<variante>/code.ips` nach `SD:/luma/titles/0004000000153600/code.ips` kopieren.

## Varianten

- **home-menu:** Gewonnene Badges landen in der HOME-Badgebox. Kann eigene (GYTB-)Badges ersetzen.
- **game-only:** Badges bleiben im Spiel, das HOME-Menü wird nicht angefasst.

## Wechsel zwischen Nintendo und Pretendo

- **Spielstand:** Dank Bridge in beiden Modi gültig.
- **HOME-Badges:** Das HOME-Menü selbst bindet die Badgebox an das aktive Konto. Nach jedem Nimbus-Wechsel in **Simple Badge Injector** die Option zum Korrigieren der NNID/PNID ausführen, **bevor** du Badge Arcade startest. Platzierte Badges musst du danach eventuell neu anordnen.

## Grenzen

- Nur **EUR, Update 1.3.1**. USA/JPN werden nicht unterstützt.
- Der zusätzliche Hook ist **nicht auf echter Hardware getestet**. Die Testsuite des Original-Projekts läuft ohne Spielcode (61 bestanden, 442 übersprungen).
- Ein vorhandener Reclawed-Patch wird ersetzt. Badges, die Reclawed nur im Spiel gespeichert hat, liest dieser Patch nicht.
- Deinstallieren: Store → *Deinstallieren* oder `code.ips` löschen.

## Selbst bauen

```sh
git clone https://github.com/GrouchoGlasses42/badge-arcade-offline
cd badge-arcade-offline && git apply ../bridge.patch
python3 src/badge_patch.py --game-version 1.3.1 --home-menu --package ../home-menu.zip
python3 src/badge_patch.py --game-version 1.3.1 --package ../game-only.zip
```

Benötigt Python 3.11+ und clang mit ARM-Unterstützung.

## Lizenz und Hinweise

MIT, siehe `LICENSE` (Original-Lizenz von badge-arcade-offline). Enthält keinen Nintendo-Code und keine Spieldaten. Inoffizielles Fanprojekt, nicht mit Nintendo oder Pretendo verbunden. Die Bridge wurde mit KI-Unterstützung erstellt. Nutzung auf eigenes Risiko, immer mit Backup.
