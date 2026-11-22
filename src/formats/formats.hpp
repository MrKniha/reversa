// Format loaders. Malformed content never throws past load_bytes(): it degrades to a raw view
// with diagnostics (graceful degradation).
#pragma once
#include <map>
#include <optional>
#include <string>

#include "core/model.hpp"

namespace rv {

bool is_elf(const Bytes& d);
bool is_pe(const Bytes& d);
Binary load_elf(const std::string& path, std::shared_ptr<const Bytes> data);  // throws ParseError
Binary load_pe(const std::string& path, std::shared_ptr<const Bytes> data);   // throws ParseError

extern const std::vector<std::string> ARCHES;
int arch_bits(const std::string& arch);
// Prologue-count arch guess for raw data; returns best arch and per-arch scores.
std::string guess_arch(const Bytes& d, std::map<std::string, int>* scores = nullptr);
Binary load_raw(const std::string& path, std::shared_ptr<const Bytes> data, std::optional<std::string> arch,
                Addr base = 0, const std::string& endian = "little");

std::string detect_format(const Bytes& d);  // ELF | PE | MACHO | ARCHIVE | RAW

struct LoadOptions {
    std::optional<std::string> arch;
    std::optional<Addr> base;
    bool force_raw = false;
};
Binary load_bytes(std::shared_ptr<const Bytes> data, const std::string& path = "<memory>", const LoadOptions& o = {});
Binary load_file(const std::string& path, const LoadOptions& o = {});  // throws LoadError

}  // namespace rv
