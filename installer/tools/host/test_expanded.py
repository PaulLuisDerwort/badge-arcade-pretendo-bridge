"""Compare the native Expanded build (installer/source/builder) with
badge-arcade-offline's Python expand_catalog() on synthetic, game-free data.

Run from a badge-arcade-offline checkout whose src/weekly_native/ holds this
repository's builder sources and host.cpp (see .github/scripts/test_builder.sh):

    PYTHONPATH=src:tests python -m pytest -q <this file>
"""
from __future__ import annotations

import struct
import subprocess
import xml.etree.ElementTree as ET

import pytest

from badge_catalog import decompress_yaz0, encode_yaz0_literals, expand_catalog, read_sarc
from test_badge_spotpass import sarc, synthetic_assets
from weekly_builder import build_host


@pytest.fixture(scope="session")
def host(tmp_path_factory):
    path = tmp_path_factory.mktemp("expanded") / "nba-host"
    build_host(path)
    return path


def historical_catalog():
    catalog = synthetic_assets()
    prize = bytearray(decompress_yaz0(catalog["pc/rt/Pr/PrizeTest.prb.szs"]))
    struct.pack_into("<I", prize, 0x3C, 333)
    catalog["pc/rt/Pr/OnlyInAllbadge.prb.szs"] = encode_yaz0_literals(prize)
    raw = bytearray(decompress_yaz0(catalog["pc/ci/CraneInstanceTest.cib.szs"]))
    raw[0x30:0x60] = b"HistoricalMachine".ljust(48, b"\0")
    raw[0x1100:0x1130] = b"OnlyInAllbadge".ljust(48, b"\0")
    struct.pack_into("<I", raw, 0xE8, 1)
    raw[0x1880:0x18B0] = b"UnneededPlayfieldObstacle".ljust(48, b"\0")
    catalog["pc/ci/HistoricalMachine.cib.szs"] = encode_yaz0_literals(raw)
    return catalog


def schedule_xml():
    root = ET.Element("FileItems")
    items = ET.SubElement(root, "Items")
    for key, value, kind in (("DefaultStageName000", "Old", "DefaultStage"),
                             ("BonusStageName", "CraneInstanceTest", "BonusStage"),
                             ("SomethingElse", "Kept", "Other")):
        node = ET.SubElement(items, "FileItem")
        for tag, text in (("Key", key), ("Value", value), ("RegexSetName", kind),
                          ("DateStartText", "20220527"), ("DateExpireText", "20220603")):
            ET.SubElement(node, tag).text = text
    ET.SubElement(root, "ItemsCount").text = str(len(items))
    return ET.tostring(root)


def registry(xml: bytes) -> dict[str, list[str]]:
    root = ET.fromstring(xml)
    return {node.tag: [child.get("name") for child in node] for node in root}


def items(xml: bytes) -> list[tuple]:
    root = ET.fromstring(xml)
    return [tuple(item.findtext(tag) for tag in ("Key", "Value", "RegexSetName", "DateStartText", "DateExpireText"))
            for item in root.find("Items")]


@pytest.mark.parametrize("with_allbadge", [False, True])
def test_native_expanded_matches_python(host, tmp_path, with_allbadge):
    base = sarc(synthetic_assets())
    update = sarc(synthetic_assets())
    schedule = schedule_xml()
    allbadge = sarc(historical_catalog()) if with_allbadge else None
    for name, data in (("base", base), ("update", update), ("schedule", schedule)):
        (tmp_path / name).write_bytes(data)
    args = [str(host), "build", tmp_path / "base", tmp_path / "update", tmp_path / "schedule",
            tmp_path / "out", "--expanded", "--synthetic"]
    if allbadge is not None:
        (tmp_path / "allbadge").write_bytes(allbadge)
        args += ["--allbadge", tmp_path / "allbadge"]
    result = subprocess.run(list(map(str, args)), capture_output=True, text=True)
    assert result.returncode == 0, result.stderr

    expected_sarc, expected_schedule, _ = expand_catalog(update, schedule, base, allbadge)
    expected = read_sarc(expected_sarc)
    actual = read_sarc((tmp_path / "out/romfs/region_EUR/common/sharc/pc.sarc").read_bytes())
    assert set(actual) == set(expected)
    for path in expected:
        if path == "pc/PrizeCollection.xml":
            assert registry(actual[path]) == registry(expected[path])
        elif path.endswith(".szs"):
            assert decompress_yaz0(actual[path]) == decompress_yaz0(expected[path]), path
        else:
            assert actual[path] == expected[path], path
    native_schedule = (tmp_path / "out/romfs/region_common/Schedule.xml").read_bytes()
    assert items(native_schedule) == items(expected_schedule)
    assert native_schedule == (tmp_path / "out/romfs/region_EUR/common/Schedule.xml").read_bytes()
