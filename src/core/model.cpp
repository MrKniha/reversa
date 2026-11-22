#include "core/model.hpp"

#include <algorithm>
#include <cstring>

#include "core/sha256.hpp"
#include "core/text.hpp"

namespace rv {

std::string hex(uint64_t v) { return format_hex(v); }

std::string Diagnostic::friendly() const {
    if (!location) return message;
    return message + " (" + (where == "address" ? "adresa " : "offset v súbore ") + hex(*location) + ")";
}

void Binary::reindex() const {
    if (indexed_count_ == sections.size() && indexed_data_ == (sections.empty() ? nullptr : sections.data())) return;
    index_.clear();
    for (const auto& s : sections)
        if (s.size > 0) index_.push_back(&s);
    std::stable_sort(index_.begin(), index_.end(), [](auto a, auto b) { return a->addr < b->addr; });
    starts_.clear();
    for (auto s : index_) starts_.push_back(s->addr);
    indexed_count_ = sections.size();
    indexed_data_ = sections.empty() ? nullptr : sections.data();
}

const Section* Binary::section_at(Addr addr) const {
    reindex();
    auto it = std::upper_bound(starts_.begin(), starts_.end(), addr);
    long i = long(it - starts_.begin()) - 1;
    // sections may overlap (e.g. ELF .tbss), so look back a little
    for (long j = i; j >= 0 && j > i - 4; j--) {
        const Section* s = index_[j];
        if (s->addr <= addr && addr < s->end()) return s;
    }
    return nullptr;
}

bool Binary::is_code(Addr addr) const {
    auto s = section_at(addr);
    return s && s->executable;
}

std::optional<uint64_t> Binary::addr_to_offset(Addr addr) const {
    auto s = section_at(addr);
    if (!s || addr - s->addr >= s->file_size) return std::nullopt;
    return s->offset + (addr - s->addr);
}

std::optional<Addr> Binary::offset_to_addr(uint64_t off) const {
    reindex();
    for (auto s : index_)
        if (s->offset <= off && off < s->offset + s->file_size) return s->addr + (off - s->offset);
    return std::nullopt;
}

Bytes Binary::read(Addr addr, size_t size) const {
    Bytes out;
    const Bytes& d = bytes();
    while (size > 0) {
        auto s = section_at(addr);
        if (!s) break;
        uint64_t rel = addr - s->addr;
        uint64_t chunk = std::min<uint64_t>(size, s->size - rel);
        if (rel < s->file_size) {
            uint64_t n = std::min<uint64_t>(chunk, s->file_size - rel);
            uint64_t start = s->offset + rel;
            uint64_t avail = start < d.size() ? std::min<uint64_t>(n, d.size() - start) : 0;
            out.insert(out.end(), d.begin() + start, d.begin() + start + avail);
            if (avail < n) break;  // truncated file
            if (n < chunk) out.insert(out.end(), chunk - n, 0);
        } else {
            out.insert(out.end(), chunk, 0);
        }
        addr += chunk;
        size -= chunk;
        if (chunk == 0) break;
    }
    return out;
}

std::optional<uint64_t> Binary::read_uint(Addr addr, size_t size) const {
    Bytes b = read(addr, size);
    if (b.size() != size || size > 8) return std::nullopt;
    uint64_t v = 0;
    for (size_t i = 0; i < size; i++) v = (v << 8) | b[endian == "little" ? size - 1 - i : i];
    return v;
}

std::optional<int64_t> Binary::read_int(Addr addr, size_t size) const {
    auto v = read_uint(addr, size);
    if (!v) return std::nullopt;
    if (size < 8 && (*v >> (size * 8 - 1)) & 1) return int64_t(*v | (~uint64_t(0) << (size * 8)));
    return int64_t(*v);
}

std::optional<std::string> Binary::read_cstring(Addr addr, size_t limit) const {
    Bytes b = read(addr, limit);
    auto end = std::find(b.begin(), b.end(), 0);
    if (end == b.end() || end == b.begin()) return std::nullopt;
    return decode_text(std::string_view(reinterpret_cast<const char*>(b.data()), end - b.begin()));
}

const std::string& Binary::sha256() const {
    if (sha_.empty()) sha_ = sha256_hex(bytes());
    return sha_;
}

void Binary::warn(const std::string& message, std::optional<uint64_t> location, const std::string& detail,
                  const std::string& severity) {
    diagnostics.push_back({severity, message, location, "offset", detail});
}

}  // namespace rv
