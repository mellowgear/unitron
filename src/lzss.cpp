#include "lzss.hpp"

#include <stdexcept>
#include <vector>

namespace unitron {
namespace lzss {

Bytes decompress(const Bytes& data, int64_t out_len, int64_t pos, int64_t* used) {
    const int64_t n = static_cast<int64_t>(data.size());
    auto at = [&](int64_t i) -> unsigned {  // Python bytes indexing
        if (i < -n || i >= n) throw std::out_of_range("input ends inside the LZSS stream");
        return data[static_cast<size_t>(i < 0 ? i + n : i)];
    };
    uint8_t ring[N] = {};
    int r = N - F;
    Bytes out;
    const int64_t start = pos;
    unsigned flags = 0;
    while (static_cast<int64_t>(out.size()) < out_len) {
        flags >>= 1;
        if (!(flags & 0x100)) {
            flags = at(pos) | 0xFF00;
            pos += 1;
        }
        if (flags & 1) {
            unsigned c = at(pos);
            pos += 1;
            out.push_back(static_cast<uint8_t>(c));
            ring[r] = static_cast<uint8_t>(c);
            r = (r + 1) % N;
        } else {
            unsigned a = at(pos), w = at(pos + 1);
            pos += 2;
            unsigned off = ((w >> 4) << 8) | a;
            int ln = static_cast<int>(w & 15) + THRESHOLD + 1;
            for (int k = 0; k < ln; k++) {
                uint8_t c = ring[(off + k) % N];
                out.push_back(c);
                ring[r] = c;
                r = (r + 1) % N;
                if (static_cast<int64_t>(out.size()) >= out_len) break;
            }
        }
    }
    if (used) *used = pos - start;
    return out;
}

namespace {

// Length of the match at ring offset off, allowing overlap with the bytes being written at
// r, r+1, ... (they equal raw[i], raw[i+1], ...).
int match_len(const uint8_t* ring, int r, const Bytes& raw, size_t i, int off, int maxlen) {
    int ln = 0;
    while (ln < maxlen) {
        int p = (off + ln) % N;
        int d = ((p - r) % N + N) % N;
        uint8_t c = d < ln ? raw[i + d] : ring[p];
        if (c != raw[i + ln]) break;
        ln++;
    }
    return ln;
}

}  // namespace

Bytes compress(const Bytes& raw, const std::string& tie) {
    int mode;
    if (tie == "first") mode = 0;
    else if (tie == "last") mode = 1;
    else if (tie == "near") mode = 2;
    else if (tie == "far") mode = 3;
    else throw std::invalid_argument("tie must be first, last, near or far");
    uint8_t ring[N] = {};
    int r = N - F;
    Bytes out;
    size_t i = 0, n = raw.size();
    while (i < n) {
        size_t flag_pos = out.size();
        out.push_back(0);
        unsigned flags = 0;
        for (int bit = 0; bit < 8; bit++) {
            if (i >= n) break;
            int maxlen = static_cast<int>(n - i < static_cast<size_t>(F) ? n - i : F);
            int best_len = 0, best_off = 0, best_key = 0;
            for (int off = 0; off < N; off++) {
                if (ring[off] != raw[i] && ((off - r) % N + N) % N != 0) continue;
                int ln = match_len(ring, r, raw, i, off, maxlen);
                if (ln == 0) continue;
                int age = ((r - off) % N + N) % N;  // 1 = just written
                int key = mode == 0 ? -off : mode == 1 ? off : mode == 2 ? -age : age;
                if (ln > best_len || (ln == best_len && key > best_key)) {
                    best_len = ln;
                    best_off = off;
                    best_key = key;
                }
            }
            if (best_len <= THRESHOLD) {
                flags |= 1u << bit;
                uint8_t c = raw[i];
                out.push_back(c);
                ring[r] = c;
                r = (r + 1) % N;
                i += 1;
            } else {
                out.push_back(static_cast<uint8_t>(best_off & 0xFF));
                out.push_back(static_cast<uint8_t>(((best_off >> 8) << 4) | (best_len - THRESHOLD - 1)));
                for (int k = 0; k < best_len; k++) {
                    ring[r] = raw[i + k];
                    r = (r + 1) % N;
                }
                i += best_len;
            }
        }
        out[flag_pos] = static_cast<uint8_t>(flags);
    }
    return out;
}

namespace {

struct Okumura {
    static constexpr int NIL = N;
    uint8_t text[N + F - 1] = {};
    int lson[N + 1], rson[N + 257], dad[N + 1];
    int match_pos = 0, match_len = 0;

    Okumura() {
        for (int& v : lson) v = NIL;
        for (int& v : rson) v = NIL;
        for (int& v : dad) v = NIL;
    }

    void insert(int r) {
        int cmp = 1;
        int p = N + 1 + text[r];
        rson[r] = lson[r] = NIL;
        match_len = 0;
        for (;;) {
            if (cmp >= 0) {
                if (rson[p] != NIL) {
                    p = rson[p];
                } else {
                    rson[p] = r;
                    dad[r] = p;
                    return;
                }
            } else {
                if (lson[p] != NIL) {
                    p = lson[p];
                } else {
                    lson[p] = r;
                    dad[r] = p;
                    return;
                }
            }
            int i = 1;
            while (i < F) {
                cmp = int(text[r + i]) - int(text[p + i]);
                if (cmp != 0) break;
                i++;
            }
            if (i > match_len) {
                match_pos = p;
                match_len = i;
                if (i >= F) break;
            }
        }
        dad[r] = dad[p];
        lson[r] = lson[p];
        rson[r] = rson[p];
        dad[lson[p]] = r;
        dad[rson[p]] = r;
        if (rson[dad[p]] == p) rson[dad[p]] = r;
        else lson[dad[p]] = r;
        dad[p] = NIL;
    }

    void remove(int p) {
        if (dad[p] == NIL) return;
        int q;
        if (rson[p] == NIL) {
            q = lson[p];
        } else if (lson[p] == NIL) {
            q = rson[p];
        } else {
            q = lson[p];
            if (rson[q] != NIL) {
                while (rson[q] != NIL) q = rson[q];
                rson[dad[q]] = lson[q];
                dad[lson[q]] = dad[q];
                lson[q] = lson[p];
                dad[lson[p]] = q;
            }
            rson[q] = rson[p];
            dad[rson[p]] = q;
        }
        dad[q] = dad[p];
        if (rson[dad[p]] == p) rson[dad[p]] = q;
        else lson[dad[p]] = q;
        dad[p] = NIL;
    }
};

}  // namespace

Bytes compress_okumura(const Bytes& raw, bool clamp) {
    std::vector<Okumura> holder(1);  // the trees are ~10 KiB: keep them off the stack
    Okumura& t = holder[0];
    size_t src = 0;
    const size_t n = raw.size();
    Bytes out;
    Bytes code = {0};
    unsigned mask = 1;
    int s = 0, r = N - F;
    int ln = 0;
    while (src < n) {
        t.text[r + ln] = raw[src++];
        ln++;
        if (ln == F) break;
    }
    if (ln == 0) return Bytes();
    for (int i = 1; i <= F; i++) t.insert(r - i);
    t.insert(r);
    while (ln > 0) {
        if (clamp && t.match_len > ln) t.match_len = ln;
        if (t.match_len <= THRESHOLD) {
            t.match_len = 1;
            code[0] |= static_cast<uint8_t>(mask);
            code.push_back(t.text[r]);
        } else {
            int p = t.match_pos;
            code.push_back(static_cast<uint8_t>(p & 0xFF));
            code.push_back(static_cast<uint8_t>(((p >> 4) & 0xF0) | (t.match_len - (THRESHOLD + 1))));
        }
        mask = (mask << 1) & 0xFF;
        if (mask == 0) {
            out.insert(out.end(), code.begin(), code.end());
            code.assign(1, 0);
            mask = 1;
        }
        int last = t.match_len;
        int i = 0;
        while (i < last) {
            if (src >= n) break;
            uint8_t c = raw[src++];
            t.remove(s);
            t.text[s] = c;
            if (s < F - 1) t.text[s + N] = c;
            s = (s + 1) & (N - 1);
            r = (r + 1) & (N - 1);
            t.insert(r);
            i++;
        }
        while (i < last) {
            i++;
            t.remove(s);
            s = (s + 1) & (N - 1);
            r = (r + 1) & (N - 1);
            ln -= 1;
            if (ln) t.insert(r);  // ln can go negative with clamp=false; nonzero still inserts
        }
    }
    if (code.size() > 1) out.insert(out.end(), code.begin(), code.end());
    return out;
}

}  // namespace lzss
}  // namespace unitron
