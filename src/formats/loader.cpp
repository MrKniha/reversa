// Format detection and the single entry point used by the rest of REVERSA.
#include <filesystem>
#include <fstream>

#include "core/errors.hpp"
#include "core/text.hpp"
#include "formats/formats.hpp"

namespace rv {

constexpr uint64_t MAX_SIZE = 512ull * 1024 * 1024;

std::string detect_format(const Bytes& d) {
    if (is_elf(d)) return "ELF";
    if (is_pe(d)) return "PE";
    if (d.size() >= 4) {
        uint32_t m = uint32_t(d[0]) << 24 | uint32_t(d[1]) << 16 | uint32_t(d[2]) << 8 | d[3];
        if (m == 0xfeedface || m == 0xcefaedfe || m == 0xfeedfacf || m == 0xcffaedfe || m == 0xcafebabe) return "MACHO";
        if (d[0] == 'd' && d[1] == 'e' && d[2] == 'x' && d[3] == '\n') return "ARCHIVE";
    }
    if (d.size() >= 2 && d[0] == 'P' && d[1] == 'K') return "ARCHIVE";
    return "RAW";
}

Binary load_bytes(std::shared_ptr<const Bytes> data, const std::string& path, const LoadOptions& o) {
    if (!data || data->empty()) throw LoadError("Súbor je prázdny – nie je čo analyzovať.");
    std::string fmt = o.force_raw ? "RAW" : detect_format(*data);
    if (fmt == "ELF" || fmt == "PE") {
        std::optional<uint64_t> off;
        std::string detail;
        try {
            Binary b = fmt == "ELF" ? load_elf(path, data) : load_pe(path, data);
            if (o.arch) { b.arch = *o.arch; b.bits = arch_bits(*o.arch); }
            return b;
        } catch (const ParseError& e) {
            off = e.offset;
            detail = std::string("ParseError: ") + e.what();
        } catch (const std::exception& e) {
            detail = e.what();
        }
        Binary raw = load_raw(path, data, o.arch, o.base.value_or(0));
        std::string where = off ? " na offsete " + hex(*off) : "";
        raw.warn("Tento súbor vyzerá ako " + fmt + ", ale je poškodený" + where + " – načítal som ho ako surové dáta.", off,
                 detail, "error");
        return raw;
    }
    Binary raw = load_raw(path, data, o.arch, o.base.value_or(0));
    if (fmt == "MACHO")
        raw.warn("Mach-O (macOS) zatiaľ nie je podporovaný (plánované v plnej verzii) – zobrazujem surové dáta.", 0);
    else if (fmt == "ARCHIVE")
        raw.warn("ZIP/APK/DEX zatiaľ nie je podporovaný (plánované v plnej verzii) – zobrazujem surové dáta.", 0);
    return raw;
}

Binary load_file(const std::string& path, const LoadOptions& o) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p = path_from_utf8(path);
    if (!fs::exists(p, ec)) throw LoadError("Súbor '" + path + "' neexistuje.");
    if (fs::is_directory(p, ec)) throw LoadError("'" + path + "' je priečinok, nie súbor.");
    auto size = fs::file_size(p, ec);
    if (ec) throw LoadError("Súbor '" + path + "' sa nedá prečítať.", ec.message());
    if (size > MAX_SIZE)
        throw LoadError("Súbor má " + std::to_string(size / (1024 * 1024)) + " MB; MVP podporuje najviac 512 MB.");
    std::ifstream f(p, std::ios::binary);
    if (!f) throw LoadError("Súbor '" + path + "' sa nedá prečítať.");
    auto data = std::make_shared<Bytes>(size);
    f.read(reinterpret_cast<char*>(data->data()), std::streamsize(size));
    if (!f) throw LoadError("Súbor '" + path + "' sa nedá prečítať.");
    return load_bytes(data, path_to_utf8(fs::absolute(p, ec)), o);
}

}  // namespace rv
