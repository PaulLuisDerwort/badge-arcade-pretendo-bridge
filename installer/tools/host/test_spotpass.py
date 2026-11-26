"""Test installer/source/spotpass.cpp on a PC against badge-arcade-offline's
SpotPass fixtures (all-zero test key; see spotpass_host.cpp).

    SPOTPASS_HOST=<built spotpass-host> PYTHONPATH=src:tests python -m pytest -q <this file>
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess

import pytest

from badge_spotpass import EUR_TITLE_ID
from test_badge_spotpass import encrypted_container, synthetic_week

HOST = os.environ.get("SPOTPASS_HOST", "spotpass-host")


def run(pack: Path, cache: Path):
    return subprocess.run([HOST, str(pack), str(cache)], capture_output=True, text=True)


@pytest.mark.parametrize("payload", [
    synthetic_week(),
    b"SARC" + bytes(range(256)) * 1500,  # several 64 KiB chunks, unaligned end
], ids=["week", "multi-chunk"])
def test_decrypts_like_badge_arcade_offline(tmp_path, payload):
    pack = tmp_path / "data_v131-2022-05-27-01-40-EU.enc"
    pack.write_bytes(encrypted_container(payload))
    result = run(pack, tmp_path / "cache")
    assert result.returncode == 0, result.stderr
    out = Path(result.stdout.strip())
    assert out.read_bytes() == payload
    # A second call reuses the decrypted copy.
    again = run(pack, tmp_path / "cache")
    assert again.returncode == 0 and again.stdout.strip() == str(out)


def test_archive_dec_file_is_unpacked(tmp_path):
    """archive.org's .dec: the decrypted body (0x26E header bytes) before the SARC."""
    payload = b"SARC" + bytes(range(256)) * 300
    pack = tmp_path / "data_v131-2022-02-11-01-40-EU.dec"
    pack.write_bytes(bytes(0x26E) + payload)
    result = run(pack, tmp_path / "cache")
    assert result.returncode == 0, result.stderr
    out = Path(result.stdout.strip())
    assert out != pack and out.read_bytes() == payload
    again = run(pack, tmp_path / "cache")
    assert again.returncode == 0 and again.stdout.strip() == str(out)


def test_plain_sarc_is_used_directly(tmp_path):
    pack = tmp_path / "week.sarc"
    pack.write_bytes(synthetic_week())
    result = run(pack, tmp_path / "cache")
    assert result.returncode == 0 and result.stdout.strip() == str(pack)
    assert not (tmp_path / "cache").exists()


@pytest.mark.parametrize("damage,message", [
    ("payload", "payload hash"),
    ("header", "key check"),
    ("program", "not for EUR"),
    ("length", "container"),
])
def test_rejects_damaged_or_foreign_packs(tmp_path, damage, message):
    payload = synthetic_week()
    data = bytearray(encrypted_container(payload, program=0x1234 if damage == "program" else EUR_TITLE_ID))
    if damage == "payload":
        data[-1] ^= 1
    elif damage == "header":
        data[0x28 + 3] ^= 1
    elif damage == "length":
        data = data[:-1]
    pack = tmp_path / "bad.boss"
    pack.write_bytes(bytes(data))
    result = run(pack, tmp_path / "cache")
    assert result.returncode == 1
    assert message in result.stderr
    assert not list((tmp_path / "cache").glob("*.sarc")) if (tmp_path / "cache").exists() else True
