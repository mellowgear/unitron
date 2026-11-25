"""Macro preprocessor subset: #define/#include/#if, ?macro with labels, comments."""
import tempfile
from pathlib import Path

import pytest

from unitron import mpp
from helpers import asm

TASK_WAIT = """; task_wait: expands to "R: ld XWA,R / ld (XIZ+8),XWA / cpb (XIZ+12),0 / ret NZ"
?macro task_wait
labels resume
?resume:
\tld\tXWA,?resume
\tld\t(XIZ+8),XWA
\tcpb\t(XIZ+12),0
\tret\tNZ
?endm
"""


def pp(text, files=None, **kw):
    with tempfile.TemporaryDirectory() as d:
        for n, t in (files or {}).items():
            (Path(d) / n).write_text(t)
        p = Path(d) / "t.mac"
        p.write_text(text)
        return [t for t, _ in mpp.preprocess(p, [d], **kw)]


def test_macro_labels():
    out = pp(TASK_WAIT + "\ttask_wait\nhere:\ttask_wait\n")
    body = [x for x in out if x.strip() and not x.startswith(";")]
    assert body[0] == "resume000:" and body[1] == "\tld\tXWA,resume000"
    assert "here:" in body and "resume001:" in body


def test_macro_params():
    out = pp("?macro put a,b\n\tld\t?a,?b\n?endm\n\tput A,5 ; c\n\tput (XHL),B\n")
    assert out == ["\tld\tA,5", "\tld\t(XHL),B"]


def test_define_include_if():
    out = pp('#include "c.inc"\n#define TWICE(x) ((x)*2)\n#if N > 1 && defined(N)\n\tdb\tTWICE(N)\n#else\n\tdb\t0\n#endif\n'
             "\tdb\t1 // dropped\n/* block\n comment */\tdb\t2 ; kept\n\tex\tF,F' ; it's fine\n",
             files={"c.inc": "#define N 3\n"})
    out = [x for x in out if x.strip()]
    assert out == ["\tdb\t((3)*2)", "\tdb\t1 ", "\tdb\t2 ; kept", "\tex\tF,F' ; it's fine"]


def test_unsupported():
    with pytest.raises(mpp.MppError):
        pp("?repeat(3)\nx\n?endr\n")


def test_assembles_through_mpp():
    a, img = asm("$maximum\n" + TASK_WAIT + "c\tsection\tcode abs=0x200000\n\ttask_wait\n\tend\n",
                 origin=0x200000)
    assert a.d.errors == []
    assert img.hex(" ") == "40 00 00 20 00 be 08 60 8e 0c 3f 00 b0 fe"
