// Bounds-checked little/big endian reader shared by all format parsers.
#pragma once
#include <cstdint>
#include <string>

#include "core/errors.hpp"
#include "core/model.hpp"
#include "core/text.hpp"

namespace rv {

class Reader {
public:
    Reader(const Bytes& data, bool big_endian = false) : d_(data), big_(big_endian) {}

    void need(uint64_t off, uint64_t n, const char* what = "structure extends past the end of the file") const {
        if (off > d_.size() || n > d_.size() - off) throw ParseError(what, off);
    }
    uint64_t uint(uint64_t off, int n) const {
        need(off, n);
        uint64_t v = 0;
        for (int i = 0; i < n; i++) {
            uint64_t b = d_[off + (big_ ? i : n - 1 - i)];
            v = (v << 8) | b;
        }
        return v;
    }
    uint8_t u8(uint64_t o) const { return uint8_t(uint(o, 1)); }
    uint16_t u16(uint64_t o) const { return uint16_t(uint(o, 2)); }
    uint32_t u32(uint64_t o) const { return uint32_t(uint(o, 4)); }
    uint64_t u64(uint64_t o) const { return uint(o, 8); }
    int16_t i16(uint64_t o) const { return int16_t(u16(o)); }
    int32_t i32(uint64_t o) const { return int32_t(u32(o)); }
    int64_t i64(uint64_t o) const { return int64_t(u64(o)); }
    std::string raw(uint64_t o, uint64_t n) const {
        need(o, n, "block extends past the end of the file");
        return std::string(reinterpret_cast<const char*>(d_.data()) + o, n);
    }
    std::string cstr(uint64_t o, uint64_t limit = 1024) const {
        if (o >= d_.size()) throw ParseError("string outside the file", o);
        uint64_t end = o;
        while (end < d_.size() && end < o + limit && d_[end]) end++;
        return utf8_lossy(std::string_view(reinterpret_cast<const char*>(d_.data()) + o, end - o));
    }
    size_t size() const { return d_.size(); }

private:
    const Bytes& d_;
    bool big_;
};

}  // namespace rv
