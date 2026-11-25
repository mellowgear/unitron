"""The one-off migration bridge: the decomp's src/ (old dialect) -> asm900 under a temp dir ->
unitron -> the original ROM. Skipped unless the decomp and the ROM are both present."""
import hashlib
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
DECOMP = ROOT / "decomp/decomp"
ROM = ROOT / "roms/rom1/rom1.ngp"
TOOL = Path(__file__).resolve().parents[1] / "tools/convert_decomp.py"


@pytest.mark.skipif(not (DECOMP / "src/main.s").is_file() or not ROM.is_file(), reason="decomp or ROM absent")
def test_convert_decomp(tmp_path):
    r = subprocess.run([sys.executable, "-I", str(TOOL), str(DECOMP), str(ROM), str(tmp_path / "out")],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout + r.stderr
    assert "byte-identical" in r.stdout
    assert hashlib.sha1(ROM.read_bytes()).hexdigest() in r.stdout
