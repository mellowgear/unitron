"""Assembler: directives, labels, expressions, includes and error messages."""
from helpers import asm

HEAD = "$maximum\n\tmodule\tt\nc\tsection\tcode abs=0x200000\n"


def ok(body, size=None, **kw):
    a, img = asm(HEAD + body + "\n\tend\n", origin=0x200000, size=size, **kw)
    assert a.d.errors == [], a.d.errors
    return img, a


def errors(body, head=HEAD):
    a, img = asm(head + body + "\n\tend\n", origin=0x200000)
    assert img is None
    return a.d.errors


def test_data_directives():
    img, _ = ok('\tdb\t1,0x10,-1,255,"AB\\n",\'C\'\n\tdw\t0x1234,\'AX\'\n\tdp\t0x123456\n'
                "\tdl\t0x12345678,-1\n\tdfb\t3,0xEE\n\tdfw\t2,0x5883\n\tdsb\t2\n\tdb\t0y101,0y11,010,0\n")
    assert img.hex(" ") == ("01 10 ff ff 41 42 0a 43 34 12 58 41 56 34 12 78 56 34 12 ff ff ff ff "
                            "ee ee ee 83 58 83 58 ff ff 05 03 08 00")


def test_labels_and_expressions():
    img, a = ok("Start:\n\tdw\tLater-Start,(1+2)*3,1+2*3,1<<4|1,6&3^1,0x100>>4,-2%3,~0&0xFF\n"
                "Later\tdb\t$&0xFF\nRoutine.loop:\tjr\tRoutine.loop\n"
                "COUNT\tequ\tLater+0x50\n\tdl\tCOUNT\n")
    assert img[:16].hex(" ") == "10 00 09 00 07 00 11 00 03 00 10 00 fe ff ff 00"
    assert img[16] == 0x10
    assert img[17:19] == bytes([0x68, 0xFE])
    assert int.from_bytes(img[19:23], "little") == 0x200010 + 0x50
    assert a.symbols()["Routine.loop"] == 0x200011


def test_org_align_and_sections():
    img, _ = ok("\tdb\t1\n\talign\t4\n\tdb\t2\n\torg\t0x200010\n\tdb\t3\n"
                "d\tsection\tromdata abs=0x200020\n\tdb\t4\n", size=0x21)
    assert img[0] == 1 and img[4] == 2 and img[0x10] == 3 and img[0x20] == 4
    assert img[1:4] == b"\xff\xff\xff"


def test_instructions_and_symbols():
    img, _ = ok("VEC\tequ\t0x6FCC\nX\tequ\t0x6F82\n\tld\t(VEC),XHL\n\tldb\t(X),0x10\n"
                "\tld\tA,(XIZ+TASK)\n\tjp\tXHL\n\tcall\tC,XIX+2\n\tlda\tXHL,XHL+DE\nTASK\tequ\t8\n")
    assert img.hex(" ") == ("f1 cc 6f 63 f1 82 6f 00 10 8e 08 21 b3 d8 bc 02 e7 f3 07 ec e8 33")


def test_include():
    img, _ = ok('$include "data.asm"\n\tdb\tVAL\n', files={"data.asm": "VAL\tequ\t7\n\tdb\t1\n"})
    assert img == b"\x01\x07"


def test_undefined_and_multi():
    e = errors("\tld\tA,(nowhere)\nfoo:\nfoo:\n")
    assert any("E221" in x and "nowhere" in x and "t.asm:4" in x for x in e)
    assert any("E203" in x and "foo" in x for x in e)


def test_overlap_names_both_lines():
    e = errors("\tdb\t1,2,3,4\nd\tsection\tromdata abs=0x200002\n\tdb\t9\n")
    msg = [x for x in e if "E217" in x]
    assert msg and "t.asm:6" in msg[0] and "t.asm:4" in msg[0]


def test_no_section_and_mismatch():
    e = errors("\tnop\n", head="$maximum\n")
    assert any("E215" in x for x in e)
    e = errors("\tld\t(XHL),(XDE)\n")
    assert any("E230" in x for x in e)
    e = errors("\tadd\tA,(XHL),1\n")
    assert any("legal forms" in x for x in e)
    e = errors("\tdb\t256\n")
    assert any("does not fit" in x for x in e)
    e = errors("\tjr\tfar\n\tdfb\t200,0\nfar:\n")
    assert any("E227" in x for x in e)


def test_relocatable_widths():
    # a medium data symbol is addressed with 16 bits, a large one with 24, whatever its value
    a, img = asm("$maximum\nm\tsection\tdata medium\nv\tdsb\t1\nl\tsection\tdata large\nvl\tdsb\t1\n"
                 "c\tsection\tcode abs=0x200000\n\tld\tA,(v)\n\tld\tA,(vl)\n\tend\n",
                 origin=0x200000, placements={"m": 0x40, "l": 0x80})
    assert a.d.errors == []
    assert img.hex(" ") == "c1 40 00 21 c2 80 00 00 21"


def test_numbers_asm900():
    assert any("0y" in x for x in errors("\tdb\t0b101\n"))       # asm900 rejects 0b (E202)
    assert any("-H" in x for x in errors("\tdb\t12H\n"))
    from unitron.asm import expr as X
    X.INTEL_SUFFIX = True
    try:
        assert X.parse_number("0FFH") == 255 and X.parse_number("101B") == 5
    finally:
        X.INTEL_SUFFIX = False


def test_width_operator():
    # `:n` forces an encoding at least n bits wide (asm900 grammar rule 78)
    img, _ = ok("\tld\tA,(0x62AA:24)\n\tld\tC,(XHL+0xF:16)\n\tld\tBC,2:16\n\tcp\tE,0:8\n"
                "\tld\tA,2:3\n\tld\tA,5\n\tld\tA,(XHL-2:16)\n\tjp\t0x1234:24\n\tdb\t0x1FF:8\n")
    assert img.hex(" ") == ("c2 aa 62 00 21 c3 ed 0f 00 23 31 02 00 cd cf 00 c9 aa 21 05 "
                            "c3 ed fe ff 21 1b 34 12 00 ff")


def test_reserved_and_primes():
    e = errors("small:\n")
    assert any("reserved" in x for x in e)
    img, _ = ok("\tex\tF,F'\n\tld\tWA',BC\n")
    assert img[0] == 0x16
