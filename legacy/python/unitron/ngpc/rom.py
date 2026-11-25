"""NGPC cartridge image: address mapping and the 0x40-byte header (SNK SysPro l.174-186)."""
import struct

CART_BASE = 0x200000
CART_HI_BASE = 0x800000   # second flash chip on 4 MB cartridges


class Rom:
    def __init__(self, data):
        self.data = bytes(data)

    def addr_to_off(self, a):
        if CART_BASE <= a < CART_BASE + min(len(self.data), 0x200000):
            return a - CART_BASE
        if len(self.data) > 0x200000 and CART_HI_BASE <= a < CART_HI_BASE + len(self.data) - 0x200000:
            return a - CART_HI_BASE + 0x200000
        return None

    def __call__(self, a):
        o = self.addr_to_off(a)
        return None if o is None else self.data[o]

    def contains(self, a):
        return self.addr_to_off(a) is not None

    def header(self):
        d = self.data
        return {
            "license": d[0:28].decode("ascii", "replace"),
            "entry": struct.unpack_from("<I", d, 0x1C)[0],
            "game_id": struct.unpack_from("<H", d, 0x20)[0],
            "version": d[0x22],
            "system": "color" if d[0x23] == 0x10 else ("mono" if d[0x23] == 0 else f"0x{d[0x23]:02X}"),
            "title": d[0x24:0x30].decode("ascii", "replace").rstrip("\0 "),
            "size": len(d),
        }
