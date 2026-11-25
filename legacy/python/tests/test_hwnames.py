"""Checks for the cited NGPC hardware-name table and its generated include file."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))

import gen_hw_inc  # noqa: E402
from unitron.ngpc import hwnames  # noqa: E402

NAME_RE = re.compile(r"^[A-Z][A-Z0-9_]*$")
KINDS = {"io", "sysram", "vector", "k2ge", "mem"}


def all_names():
    return [e[1] for e in hwnames.NAMES] + [b[1] for b in hwnames.BITS]


def test_names_unique():
    names = all_names()
    assert len(names) == len(set(names))


def test_addresses_unique():
    # unique across all kinds, so also per kind
    addrs = [e[0] for e in hwnames.NAMES]
    assert len(addrs) == len(set(addrs))


def test_sorted_by_address():
    addrs = [e[0] for e in hwnames.NAMES]
    assert addrs == sorted(addrs)


def test_entry_shape_and_citation():
    for addr, name, size, kind, cite, official in hwnames.NAMES:
        assert kind in KINDS, name
        assert size >= 1, name
        assert cite.startswith(("IO900H.H l.", "SNK ")), name
        assert official.strip(), name
        if kind == "io":
            assert addr < 0x80 and size in (1, 2) and official == name
    for reg, name, bit, cite, official in hwnames.BITS:
        assert 0 <= bit <= 7, name
        assert cite.startswith("SNK "), name
        assert official.strip(), name


def test_name_spelling():
    for name in all_names():
        assert NAME_RE.match(name), name


def test_official_normalizes_to_name():
    # D5: where the name is the official spelling, NAME == official.upper() with "." -> "_"
    for _addr, name, _size, kind, _cite, official in hwnames.NAMES:
        if kind in ("io", "sysram") or (kind == "k2ge" and " " not in official):
            assert name == official.upper().replace(".", "_"), name


def test_bits_point_at_named_registers():
    regs = hwnames.by_addr()
    for reg, name, _bit, _cite, _off in hwnames.BITS:
        assert reg in regs, name


def test_vectors_subset():
    assert hwnames.VECTORS and all(e[3] == "vector" for e in hwnames.VECTORS)
    for e in hwnames.VECTORS:
        assert 0x6FB8 <= e[0] <= 0x6FFC and e[0] % 4 == 0 and e[2] == 4
    assert 0x6FEC not in {e[0] for e in hwnames.VECTORS}  # SNK: reserved


def test_helpers_round_trip():
    a, n = hwnames.by_addr(), hwnames.by_name()
    assert len(a) == len(n) == len(hwnames.NAMES)
    assert all(a[v] == k for k, v in n.items())
    assert hwnames.by_addr("io")[0x20] == "TRUN"


def test_spot_checks():
    n = hwnames.by_name()
    assert n["TRUN"] == 0x20
    assert n["WDCR"] == 0x6F
    assert n["DMA0V"] == 0x7C
    assert n["WBA_H"] == 0x8002
    assert n["S1SO_H"] == 0x8032
    assert n["VEC_VBLANK"] == 0x6FCC
    assert n["BG"] == 0x8118
    assert n["SYS_LEVER"] == 0x6F82
    sizes = {e[1]: e[2] for e in hwnames.NAMES}
    assert sizes["ADREG0"] == 2 and sizes["TREG4"] == 2 and sizes["BATTERY_VOLTAGE"] == 2
    assert hwnames.bits_by_name()["SYS_LEVER_OPTION"] == (0x6F82, 6)


def test_inc_up_to_date():
    assert gen_hw_inc.OUT.read_bytes() == gen_hw_inc.render().encode("utf-8")


def test_inc_lines_parse():
    seen = {}
    for line in gen_hw_inc.render().splitlines():
        if not line or line.startswith(";"):
            continue
        m = re.match(r"^([A-Z][A-Z0-9_]*)\tequ\t(0x[0-9A-F]+|\d)\t; \S", line)
        assert m, line
        seen[m.group(1)] = int(m.group(2), 0)
    assert len(seen) == len(all_names())
    assert all(seen[name] == addr for name, addr in hwnames.by_name().items())
