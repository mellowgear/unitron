# Legacy Python unitron (frozen reference)

The first unitron, in Python. The C++ binary in the repository root replaced it and matches it byte
for byte; its behavior is frozen in `tests/vectors/` and `tests/golden/` (commit cfaed04). Nothing
builds with it any more. It is kept for reference and will be deleted once nothing needs it.

- `unitron/`: the package; `pyproject.toml` installs it (`pip install -e legacy/python`, pillow
  for gfx).
- `tests/`: its pytest suite: `python -I -m pytest legacy/python/tests` from the repository root.
  The ROM, sample and decomp tests skip themselves when those files are absent.
- `tools/freeze_vectors.py`: wrote the C++ test vectors (run once, before the port). It runs the
  package with `python -I -m unitron`, so it needs the package pip-installed.
- `tools/convert_decomp.py`: the one-off bridge from the the reference ROM decomp's old dialect to asm900.
- `tools/gen_hw_inc.py`: wrote the package's own copy of `ngpc_hw.inc`. The C++ build generates
  `data/ngpc_hw.inc` from `data/hwnames.tsv` instead.
