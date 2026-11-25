#include "hash.hpp"


namespace unitron {
namespace {

inline uint32_t rotl(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

// Message padding shared by both: 0x80, zeros, 64-bit big-endian bit length; 64-byte blocks.
template <typename F>
void for_each_block(const uint8_t* data, size_t len, F block) {
    size_t full = len / 64;
    for (size_t i = 0; i < full; i++) block(data + 64 * i);
    uint8_t tail[128] = {};
    size_t rem = len - 64 * full;
    for (size_t i = 0; i < rem; i++) tail[i] = data[64 * full + i];
    tail[rem] = 0x80;
    size_t tlen = rem + 1 + 8 <= 64 ? 64 : 128;
    uint64_t bits = static_cast<uint64_t>(len) * 8;
    for (int i = 0; i < 8; i++) tail[tlen - 1 - i] = static_cast<uint8_t>(bits >> (8 * i));
    block(tail);
    if (tlen == 128) block(tail + 64);
}

inline uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

std::string hex_words(const uint32_t* h, int n) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < n; i++)
        for (int k = 28; k >= 0; k -= 4) s += digits[(h[i] >> k) & 15];
    return s;
}

}  // namespace

std::string sha1_hex(const uint8_t* data, size_t len) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    for_each_block(data, len, [&](const uint8_t* b) {
        uint32_t w[80];
        for (int t = 0; t < 16; t++) w[t] = be32(b + 4 * t);
        for (int t = 16; t < 80; t++) w[t] = rotl(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
        for (int t = 0; t < 80; t++) {
            uint32_t f, k;
            if (t < 20) { f = (bb & c) | (~bb & d); k = 0x5A827999; }
            else if (t < 40) { f = bb ^ c ^ d; k = 0x6ED9EBA1; }
            else if (t < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = bb ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t tmp = rotl(a, 5) + f + e + k + w[t];
            e = d; d = c; c = rotl(bb, 30); bb = a; a = tmp;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
    });
    return hex_words(h, 5);
}

std::string sha256_hex(const uint8_t* data, size_t len) {
    static const uint32_t K[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    for_each_block(data, len, [&](const uint8_t* b) {
        uint32_t w[64];
        for (int t = 0; t < 16; t++) w[t] = be32(b + 4 * t);
        for (int t = 16; t < 64; t++) {
            uint32_t s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
            uint32_t s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
            w[t] = w[t - 16] + s0 + w[t - 7] + s1;
        }
        uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int t = 0; t < 64; t++) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K[t] + w[t];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t mj = (a & bb) ^ (a & c) ^ (bb & c);
            uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = bb; bb = a; a = t1 + t2;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    });
    return hex_words(h, 8);
}

std::string sha1_hex(const Bytes& data) { return sha1_hex(data.data(), data.size()); }
std::string sha256_hex(const Bytes& data) { return sha256_hex(data.data(), data.size()); }

}  // namespace unitron
