"""Build the bridge IPS for every catalog mode and HOME variant.

Run with badge-arcade-offline's src/ on PYTHONPATH, after applying
bridge.patch to that checkout (see .github/scripts/build_mode_ips.sh):

    PYTHONPATH=<checkout>/src python3 tools/build_mode_ips.py --out ips

Writes, without any game data:
    ips/home-menu/code.ips        Vanilla, badges go to the HOME Menu
    ips/game-only/code.ips        Vanilla, badges stay in the game
    ips/expanded/<variant>.ips    Expanded (needs the generated RomFS)
    ips/weekly/<variant>.ips      Custom Weekly (needs the generated RomFS)

The records follow badge-arcade-offline exactly:
    Vanilla  = build_vanilla_records()
    Expanded = Vanilla without VANILLA_STAGE_OFFSETS, plus CATALOG_RECORDS
               (src/badge_catalog.py, write_personal_package)
    Weekly   = weekly_records() (src/weekly_builder.py)
"""
from __future__ import annotations

import argparse
from pathlib import Path

from badge_patch import build_vanilla_records
from badge_versions import CATALOG_RECORDS, UPDATE, VANILLA_STAGE_OFFSETS
from patch_format import encode_ips
from weekly_builder import weekly_records


def mode_records(mode: str, home_menu: bool) -> list[tuple[int, bytes]]:
    if mode == "vanilla":
        records, _ = build_vanilla_records(UPDATE, home_menu=home_menu)
        return records
    if mode == "expanded":
        records, _ = build_vanilla_records(UPDATE, home_menu=home_menu)
        kept = [(offset, data) for offset, data in records if offset not in VANILLA_STAGE_OFFSETS]
        return sorted([*kept, *CATALOG_RECORDS])
    if mode == "weekly":
        return weekly_records(home_menu)
    raise ValueError(mode)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    for variant, home in (("home-menu", True), ("game-only", False)):
        for mode in ("vanilla", "expanded", "weekly"):
            ips = encode_ips(mode_records(mode, home), UPDATE.size)
            path = (args.out / variant / "code.ips" if mode == "vanilla"
                    else args.out / mode / f"{variant}.ips")
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(ips)
            print(f"{path}: {len(ips)} bytes")


if __name__ == "__main__":
    main()
