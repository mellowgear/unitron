"""LZSS in the parameters of Haruhiko Okumura's LZSS.C (1989, public domain): N=1024 ring,
F=18 longest match, THRESHOLD=2, zero-filled ring, first write at N-F.

Stream: a flag byte announces 8 items, LSB first: 1 = literal byte, 0 = match of two bytes A, W:
offset = ((W >> 4) << 8) | A, length = (W & 15) + 3. The decoder stops after exactly `out_len`
bytes, even inside a flag group.
"""

N, F, THRESHOLD = 0x400, 18, 2


def decompress(data, out_len, pos=0):
    """Return (output bytes, number of input bytes consumed)."""
    ring = bytearray(N)
    r = N - F
    out = bytearray()
    start = pos
    flags = 0
    while len(out) < out_len:
        flags >>= 1
        if not flags & 0x100:
            flags = data[pos] | 0xFF00
            pos += 1
        if flags & 1:
            c = data[pos]; pos += 1
            out.append(c); ring[r] = c; r = (r + 1) % N
        else:
            a, w = data[pos], data[pos + 1]; pos += 2
            off = ((w >> 4) << 8) | a
            ln = (w & 15) + THRESHOLD + 1
            for k in range(ln):
                c = ring[(off + k) % N]
                out.append(c); ring[r] = c; r = (r + 1) % N
                if len(out) >= out_len:
                    break
    return bytes(out), pos - start


def _match_len(ring, r, raw, i, off, maxlen):
    """Length of the match at ring offset `off`, allowing overlap with the
    bytes being written at r, r+1, ... (they equal raw[i], raw[i+1], ...)."""
    ln = 0
    while ln < maxlen:
        p = (off + ln) % N
        d = (p - r) % N
        c = raw[i + d] if d < ln else ring[p]
        if c != raw[i + ln]:
            break
        ln += 1
    return ln


def compress(raw, tie="first"):
    """Greedy LZSS with the parameters above.

    tie: which offset wins among equally long matches --
      "first"  lowest ring offset        "last"   highest ring offset
      "near"   most recently written     "far"    least recently written
    """
    ring = bytearray(N)
    r = N - F
    out = bytearray()
    i, n = 0, len(raw)
    while i < n:
        flag_pos = len(out)
        out.append(0)
        flags = 0
        for bit in range(8):
            if i >= n:
                break
            maxlen = min(F, n - i)
            best_len, best_off, best_key = 0, 0, None
            for off in range(N):
                if ring[off] != raw[i] and (off - r) % N != 0:
                    continue
                ln = _match_len(ring, r, raw, i, off, maxlen)
                if ln == 0:
                    continue
                age = (r - off) % N   # 1 = just written
                key = {"first": -off, "last": off, "near": -age, "far": age}[tie]
                if ln > best_len or (ln == best_len and key > best_key):
                    best_len, best_off, best_key = ln, off, key
            if best_len <= THRESHOLD:
                flags |= 1 << bit
                c = raw[i]
                out.append(c)
                ring[r] = c; r = (r + 1) % N
                i += 1
            else:
                out.append(best_off & 0xFF)
                out.append(((best_off >> 8) << 4) | (best_len - THRESHOLD - 1))
                for k in range(best_len):
                    ring[r] = raw[i + k]; r = (r + 1) % N
                i += best_len
        out[flag_pos] = flags
    return bytes(out)


def compress_okumura(raw, clamp=True):
    """Haruhiko Okumura's LZSS.C (1989) encoder, binary-tree match finder,
    with N=1024, F=18, THRESHOLD=2 and a zero-filled (not space-filled) buffer.
    Tie-breaking falls out of the tree exactly as in the original program.

    clamp=True is LZSS.C as published: the final match is cut to the bytes left. clamp=False
    lets the last match run past the end (a decoder that stops at its output length doesn't
    notice); some tools did that."""
    NIL = N
    text = bytearray(N + F - 1)
    lson = [NIL] * (N + 1)
    rson = [NIL] * (N + 257)
    dad = [NIL] * (N + 1)
    st = {"pos": 0, "len": 0}

    def insert(r):
        cmp = 1
        p = N + 1 + text[r]
        rson[r] = lson[r] = NIL
        st["len"] = 0
        while True:
            if cmp >= 0:
                if rson[p] != NIL:
                    p = rson[p]
                else:
                    rson[p] = r; dad[r] = p; return
            else:
                if lson[p] != NIL:
                    p = lson[p]
                else:
                    lson[p] = r; dad[r] = p; return
            i = 1
            while i < F:
                cmp = text[r + i] - text[p + i]
                if cmp != 0:
                    break
                i += 1
            if i > st["len"]:
                st["pos"] = p
                st["len"] = i
                if i >= F:
                    break
        dad[r] = dad[p]; lson[r] = lson[p]; rson[r] = rson[p]
        dad[lson[p]] = r; dad[rson[p]] = r
        if rson[dad[p]] == p:
            rson[dad[p]] = r
        else:
            lson[dad[p]] = r
        dad[p] = NIL

    def delete(p):
        if dad[p] == NIL:
            return
        if rson[p] == NIL:
            q = lson[p]
        elif lson[p] == NIL:
            q = rson[p]
        else:
            q = lson[p]
            if rson[q] != NIL:
                while rson[q] != NIL:
                    q = rson[q]
                rson[dad[q]] = lson[q]; dad[lson[q]] = dad[q]
                lson[q] = lson[p]; dad[lson[p]] = q
            rson[q] = rson[p]; dad[rson[p]] = q
        dad[q] = dad[p]
        if rson[dad[p]] == p:
            rson[dad[p]] = q
        else:
            lson[dad[p]] = q
        dad[p] = NIL

    src = iter(raw)
    out = bytearray()
    code = bytearray([0])
    mask = 1
    s, r = 0, N - F
    ln = 0
    for c in src:
        text[r + ln] = c
        ln += 1
        if ln == F:
            break
    if ln == 0:
        return b""
    for i in range(1, F + 1):
        insert(r - i)
    insert(r)
    while ln > 0:
        if clamp and st["len"] > ln:
            st["len"] = ln
        if st["len"] <= THRESHOLD:
            st["len"] = 1
            code[0] |= mask
            code.append(text[r])
        else:
            p = st["pos"]
            code.append(p & 0xFF)
            code.append(((p >> 4) & 0xF0) | (st["len"] - (THRESHOLD + 1)))
        mask = (mask << 1) & 0xFF
        if mask == 0:
            out += code
            code = bytearray([0])
            mask = 1
        last = st["len"]
        i = 0
        while i < last:
            c = next(src, None)
            if c is None:
                break
            delete(s)
            text[s] = c
            if s < F - 1:
                text[s + N] = c
            s = (s + 1) & (N - 1)
            r = (r + 1) & (N - 1)
            insert(r)
            i += 1
        while i < last:
            i += 1
            delete(s)
            s = (s + 1) & (N - 1)
            r = (r + 1) & (N - 1)
            ln -= 1
            if ln:
                insert(r)
    if len(code) > 1:
        out += code
    return bytes(out)
