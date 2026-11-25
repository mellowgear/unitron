"""Command-line smoke tests."""
import subprocess
import sys
from pathlib import Path

# Run the package from legacy/python without needing it pip-installed.
PKG = str(Path(__file__).resolve().parents[1])
BOOT = f"import sys, runpy; sys.path.insert(0, {PKG!r}); runpy.run_module('unitron', run_name='__main__')"


def run(*args, cwd=None):
    return subprocess.run([sys.executable, "-I", "-c", BOOT, *map(str, args)], capture_output=True,
                          text=True, cwd=cwd)


def test_asm_mac_lz_bin2db_gfx(tmp_path):
    src = tmp_path / "a.asm"
    src.write_text("$maximum\n#define V 7\nc\tsection\tcode abs=0x200000\n\tld\tA,V\n\tld\t(XHL),A\n\tend\n")
    r = run("asm", src, "-o", tmp_path / "a.bin")
    assert r.returncode == 0, r.stderr
    assert (tmp_path / "a.bin").read_bytes() == bytes.fromhex("2107b341")
    r = run("mac", src)
    assert "\tld\tA,7" in r.stdout
    bad = tmp_path / "b.asm"
    bad.write_text("$maximum\nc\tsection\tcode abs=0x200000\n\tld\tA,(nowhere)\n\tend\n")
    r = run("asm", bad, "-o", tmp_path / "b.bin")
    assert r.returncode == 1 and "b.asm:3: error E221" in r.stderr and not (tmp_path / "b.bin").exists()
    raw = tmp_path / "raw.bin"
    raw.write_bytes(bytes(range(64)) * 4)
    assert run("lz", "c", raw, tmp_path / "raw.lz").returncode == 0
    assert run("lz", "d", tmp_path / "raw.lz", tmp_path / "raw.out", "--length", 256).returncode == 0
    assert (tmp_path / "raw.out").read_bytes() == raw.read_bytes()
    r = run("bin2db", raw, "Blob")
    assert r.stdout.startswith("; generated") and "Blob:" in r.stdout
    assert run("gfx", "topng", raw, tmp_path / "t.png").returncode == 0
    assert run("gfx", "frompng", tmp_path / "t.png", tmp_path / "t.bin", "--count", 16).returncode == 0
    assert (tmp_path / "t.bin").read_bytes() == raw.read_bytes()
