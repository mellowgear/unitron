"""The big one: a whole NGPC ROM -> asm900 source (unitron's disassembler + db fallback) -> asm ->
the same bytes. Needs a ROM the user owns: $UNITRON_ROM, else the the reference ROM path below; skipped
when absent. The ROM and the source made from it stay in a temporary directory."""
import hashlib
import os
from pathlib import Path

import pytest

from unitron.cli_more import roundtrip

DEFAULT = Path(__file__).resolve().parents[4] / "roms/rom1/rom1.ngp"
ROM1_SHA1 = "ff74d5877bf58164419710888923e46b9cfb415e"
ROM = Path(os.environ.get("UNITRON_ROM", DEFAULT))


@pytest.mark.skipif(not ROM.is_file(), reason="no ROM (set UNITRON_ROM)")
def test_rom_roundtrip():
    data = ROM.read_bytes()
    ok, st, errors, img = roundtrip(ROM)
    assert errors == []
    assert ok, "assembled image differs from the ROM"
    assert hashlib.sha1(img).hexdigest() == hashlib.sha1(data).hexdigest()
    if hashlib.sha1(data).hexdigest() == ROM1_SHA1:
        assert st["instructions"] > 29000
        # the plan's 45 leftovers (39 24-bit sound-RAM addresses + 6 others) all have a `:n` spelling
        assert st["db_fallback"] == 0
        assert st["width_marked"] == 45
