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
    if (!data || data->empty()) throw LoadError("The file is empty; there is nothing to analyse.");
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
        std::string where = off ? " at offset " + hex(*off) : "";
        raw.warn("This file looks like " + fmt + " but is damaged" + where + "; loaded it as raw data.", off,
                 detail, "error");
        return raw;
    }
    Binary raw = load_raw(path, data, o.arch, o.base.value_or(0));
    if (fmt == "MACHO")
        raw.warn("Mach-O (macOS) is not supported yet (planned for the full version); showing raw data.", 0);
    else if (fmt == "ARCHIVE")
        raw.warn("ZIP/APK/DEX is not supported yet (planned for the full version); showing raw data.", 0);
    return raw;
}

Binary load_file(const std::string& path, const LoadOptions& o) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path p = path_from_utf8(path);
    if (!fs::exists(p, ec)) throw LoadError("File '" + path + "' does not exist.");
    if (fs::is_directory(p, ec)) throw LoadError("'" + path + "' is a folder, not a file.");
    auto size = fs::file_size(p, ec);
    if (ec) throw LoadError("Cannot read the file '" + path + "'.", ec.message());
    if (size > MAX_SIZE)
        throw LoadError("The file has " + std::to_string(size / (1024 * 1024)) + " MB; the MVP supports at most 512 MB.");
    std::ifstream f(p, std::ios::binary);
    if (!f) throw LoadError("Cannot read the file '" + path + "'.");
    auto data = std::make_shared<Bytes>(size);
    f.read(reinterpret_cast<char*>(data->data()), std::streamsize(size));
    if (!f) throw LoadError("Cannot read the file '" + path + "'.");
    return load_bytes(data, path_to_utf8(fs::absolute(p, ec)), o);
}

}  // namespace rv
