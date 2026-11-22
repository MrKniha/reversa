// Format-independent description of a loaded binary. Every loader (ELF, PE, raw)
// produces a Binary; the analysis layer only talks to this model.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rv {

using Addr = uint64_t;
using Bytes = std::vector<uint8_t>;

struct Section {
    std::string name;
    Addr addr = 0;
    uint64_t size = 0;
    uint64_t offset = 0;
    uint64_t file_size = 0;
    bool readable = true;
    bool writable = false;
    bool executable = false;

    Addr end() const { return addr + size; }
    std::string kind() const { return executable ? "code" : (file_size == 0 ? "bss" : "data"); }
    std::string perms() const {
        return std::string(readable ? "r" : "-") + (writable ? "w" : "-") + (executable ? "x" : "-");
    }
};

struct Symbol {
    std::string name;
    Addr addr = 0;
    uint64_t size = 0;
    std::string kind = "func";  // func | object | unknown
    std::string source;         // symtab | dynsym | coff | export | heuristic
};

struct Import {
    std::string name;
    std::string library;
    Addr slot = 0;                // GOT/IAT slot filled by the OS loader
    std::optional<Addr> thunk;    // PLT / jump stub
    std::optional<uint32_t> ordinal;
};

struct Export {
    std::string name;
    Addr addr = 0;
    std::optional<uint32_t> ordinal;
    std::string forwarder;
};

struct Relocation {
    Addr addr = 0;
    std::string type;
    std::string symbol;
    int64_t addend = 0;
};

// A problem found while loading or analysing. Never fatal by itself.
struct Diagnostic {
    std::string severity;  // info | warning | error
    std::string message;
    std::optional<uint64_t> location;
    std::string where = "offset";  // offset | address
    std::string detail;

    std::string friendly() const;
};

struct Segment {
    Addr vaddr = 0;
    uint64_t memsz = 0, offset = 0, filesz = 0;
    std::string perms;
};

class Binary {
public:
    std::string path;
    std::shared_ptr<const Bytes> data;
    std::string format;  // ELF | PE | RAW
    std::string arch;    // x86 | x86-64 | arm | arm64
    int bits = 64;
    std::string endian = "little";
    std::optional<Addr> entry;
    Addr base = 0;
    std::vector<Section> sections;
    std::vector<Symbol> symbols;
    std::vector<Import> imports;
    std::vector<Export> exports;
    std::vector<Relocation> relocations;
    std::vector<std::string> libraries;
    std::vector<Diagnostic> diagnostics;
    std::string compiler = "unknown";
    std::string os;
    std::vector<Addr> extra_function_starts;  // e.g. PE .pdata
    std::map<std::string, std::string> meta;   // human readable facts (elf_type, subsystem, ...)
    std::vector<Segment> segments;

    const Bytes& bytes() const { return *data; }
    size_t file_size() const { return data ? data->size() : 0; }

    const Section* section_at(Addr addr) const;
    bool is_mapped(Addr addr) const { return section_at(addr) != nullptr; }
    bool is_code(Addr addr) const;
    std::optional<uint64_t> addr_to_offset(Addr addr) const;
    std::optional<Addr> offset_to_addr(uint64_t off) const;
    // Reads the loaded image; bss reads as zero, stops at the first unmapped address.
    Bytes read(Addr addr, size_t size) const;
    std::optional<uint64_t> read_uint(Addr addr, size_t size) const;
    std::optional<int64_t> read_int(Addr addr, size_t size) const;
    std::optional<std::string> read_cstring(Addr addr, size_t limit = 4096) const;
    int ptr_size() const { return bits / 8; }
    const std::string& sha256() const;
    void warn(const std::string& message, std::optional<uint64_t> location = std::nullopt,
              const std::string& detail = "", const std::string& severity = "warning");

private:
    void reindex() const;
    mutable std::vector<const Section*> index_;
    mutable std::vector<Addr> starts_;
    mutable size_t indexed_count_ = SIZE_MAX;
    mutable const Section* indexed_data_ = nullptr;
    mutable std::string sha_;
};

std::string hex(uint64_t v);  // "0x401000"

}  // namespace rv
