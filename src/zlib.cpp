#include "zlib.hpp"

#include <zlib.h>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace unitron {
namespace {

// zlib counts in uInt; feed larger buffers in pieces of at most this many bytes.
constexpr size_t CHUNK = 1u << 30;

[[noreturn]] void fail(const z_stream& z, const char* what) {
    throw std::runtime_error(std::string("zlib: ") + (z.msg ? z.msg : what));
}

}  // namespace

Bytes zlib_decompress(const uint8_t* data, size_t len) {
    z_stream z{};
    if (inflateInit(&z) != Z_OK) fail(z, "out of memory");
    Bytes out;
    uint8_t buf[16384];
    size_t pos = 0;
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        if (z.avail_in == 0 && pos < len) {
            z.next_in = const_cast<Bytef*>(data + pos);
            z.avail_in = static_cast<uInt>(std::min(len - pos, CHUNK));
            pos += z.avail_in;
        }
        z.next_out = buf;
        z.avail_out = sizeof buf;
        ret = inflate(&z, Z_NO_FLUSH);
        out.insert(out.end(), buf, buf + (sizeof buf - z.avail_out));
        if (ret == Z_NEED_DICT) {
            inflateEnd(&z);
            throw std::runtime_error("zlib: stream needs a preset dictionary");
        }
        if (ret == Z_BUF_ERROR && z.avail_in == 0 && pos >= len) {
            inflateEnd(&z);
            throw std::runtime_error("zlib: incomplete or truncated stream");
        }
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            std::string msg = std::string("zlib: ") + (z.msg ? z.msg : "invalid stream");
            inflateEnd(&z);
            throw std::runtime_error(msg);
        }
    }
    inflateEnd(&z);
    return out;
}

Bytes zlib_decompress(const Bytes& data) { return zlib_decompress(data.data(), data.size()); }

Bytes zlib_compress(const Bytes& data) {
    z_stream z{};
    if (deflateInit(&z, Z_DEFAULT_COMPRESSION) != Z_OK) fail(z, "out of memory");
    Bytes out;
    uint8_t buf[16384];
    size_t pos = 0;
    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        if (z.avail_in == 0 && pos < data.size()) {
            z.next_in = const_cast<Bytef*>(data.data() + pos);
            z.avail_in = static_cast<uInt>(std::min(data.size() - pos, CHUNK));
            pos += z.avail_in;
        }
        z.next_out = buf;
        z.avail_out = sizeof buf;
        ret = deflate(&z, pos >= data.size() ? Z_FINISH : Z_NO_FLUSH);
        out.insert(out.end(), buf, buf + (sizeof buf - z.avail_out));
        if (ret == Z_STREAM_ERROR) {
            deflateEnd(&z);
            throw std::runtime_error("zlib: deflate failed");
        }
    }
    deflateEnd(&z);
    return out;
}

uint32_t crc32(const uint8_t* data, size_t len, uint32_t init) {
    uLong c = init;
    for (size_t i = 0; i < len; i += CHUNK)
        c = ::crc32(c, data + i, static_cast<uInt>(std::min(len - i, CHUNK)));
    return static_cast<uint32_t>(c);
}

uint32_t adler32(const uint8_t* data, size_t len, uint32_t init) {
    uLong a = init;
    for (size_t i = 0; i < len; i += CHUNK)
        a = ::adler32(a, data + i, static_cast<uInt>(std::min(len - i, CHUNK)));
    return static_cast<uint32_t>(a);
}

}  // namespace unitron
