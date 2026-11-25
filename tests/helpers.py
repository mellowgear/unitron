import tempfile
from pathlib import Path

from unitron.asm.assembler import Assembler, build_image


def asm(text, files=None, origin=None, size=None, **kw):
    """Assemble text (plus extra files {name: text}) -> (assembler, image or None)."""
    with tempfile.TemporaryDirectory() as d:
        for n, t in (files or {}).items():
            (Path(d) / n).write_text(t)
        p = Path(d) / "t.asm"
        p.write_text(text)
        a = Assembler(include_dirs=[d])
        a.add_module(p)
        chunks = a.assemble(**kw)
        if a.d.errors:
            return a, None
        img, _, _ = build_image(chunks, origin, size)
        return a, img
