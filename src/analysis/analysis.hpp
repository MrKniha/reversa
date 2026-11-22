// Auto-analysis results and pipeline: import thunks -> function discovery (recursive descent,
// as in angr CFGFast / Ghidra "Disassemble Entry Points") -> main -> strings -> xrefs.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/model.hpp"
#include "disasm/disasm.hpp"

namespace rv {

struct Succ {
    Addr to;
    std::string kind;  // true | false | jump | fall
};

struct Block {
    Addr start = 0, end = 0;
    std::vector<Addr> insns;
    std::vector<Succ> succs;
};

struct Function {
    Addr addr = 0;
    std::string auto_name;
    std::string kind = "normal";  // normal | import | thunk
    std::map<Addr, Block> blocks;
    std::set<Addr> callees;
    std::set<Addr> tail_calls;
    uint64_t size = 0;
    size_t insn_count = 0;
    bool noreturn = false;
    std::string library;
    Addr end() const { return addr + size; }
};

struct Xref {
    Addr frm, to;
    std::string kind;  // call | jump | read | write | addr | data
    std::string label() const;
    bool operator<(const Xref& o) const { return std::tie(frm, to, kind) < std::tie(o.frm, o.to, o.kind); }
    bool operator==(const Xref& o) const { return frm == o.frm && to == o.to && kind == o.kind; }
};

// Cross-reference database, split like radare2's axt/axf: indexed by target and by source.
class XrefDB {
public:
    void add(Addr frm, Addr to, const std::string& kind);
    std::vector<Xref> to(Addr addr, const std::vector<std::string>& kinds = {}) const;  // sorted by (frm, kind)
    std::vector<Xref> frm(Addr addr) const;                                             // sorted by (to, kind)
    size_t count_to(Addr addr) const;
    size_t size() const { return total_; }

private:
    std::unordered_map<Addr, std::set<Xref>> to_, from_;
    size_t total_ = 0;
};

struct String {
    Addr addr;
    std::string text;      // UTF-8
    std::string encoding;  // ascii | utf-16le
    uint64_t length;       // bytes in file
    Addr end() const { return addr + length; }
};

using Progress = std::function<void(double, const std::string&)>;

class Analysis {
public:
    explicit Analysis(const Binary& b);
    const Binary& binary;
    Disassembler disasm;
    std::unordered_map<Addr, const Insn*> insns;  // instructions that belong to functions
    std::map<Addr, Function> functions;
    std::vector<Addr> function_order;  // discovery order
    XrefDB xrefs;
    std::vector<String> strings;
    std::map<Addr, size_t> string_at;  // addr -> index into strings
    std::map<Addr, std::string> auto_names;
    std::map<Addr, std::string> slot_imports;  // GOT/IAT slot -> import name
    std::map<std::string, Addr> import_addr;   // import name -> its function node
    std::map<Addr, Addr> thunk_target;         // thunk -> function it jumps to
    std::optional<Addr> main;
    double duration = 0;
    bool done = false;

    const Function* function_containing(Addr addr) const;
    const Function* function(Addr addr) const;
    std::vector<Addr> callers(Addr addr, bool through_thunks = true) const;
    std::vector<Insn> decode_at(Addr addr, size_t count = 1) const;
    const Insn* insn(Addr addr) const;
    const String* string(Addr addr) const;

    // internal (used by the pipeline)
    const Insn* insn_at(Addr addr);
    std::unordered_map<Addr, Insn> dcache;
    std::vector<Addr>* imports_thunks = nullptr;
    std::map<Addr, std::optional<Addr>> import_thunk_updates;  // import slot -> thunk
};

std::unique_ptr<Analysis> analyze(Binary& binary, Progress progress = nullptr, const std::atomic<bool>* cancel = nullptr,
                                  std::function<void(const Function&)> on_function = nullptr);

extern const std::set<std::string> NORETURN;

std::vector<String> find_strings(Binary& binary, size_t min_len = 4, bool include_code = false);

// --- graphs ---
struct CfgEdge {
    Addr from, to;
    std::string kind;
    bool back = false;
};
struct Cfg {
    Addr entry = 0;
    std::vector<Addr> nodes;
    std::vector<CfgEdge> edges;
    std::map<Addr, std::vector<Addr>> preds;
    std::map<Addr, std::set<Addr>> dom;
    std::map<Addr, std::vector<Addr>> loops;  // header -> sorted body
};
std::map<Addr, std::set<Addr>> dominators(const std::vector<Addr>& nodes, Addr entry,
                                          const std::map<Addr, std::vector<Addr>>& preds);
std::vector<Addr> reachable(const Function& fn);
Cfg build_cfg(const Function& fn);
struct CallGraph {
    std::vector<Addr> nodes;
    std::vector<std::pair<Addr, Addr>> edges;
};
CallGraph call_graph(const std::map<Addr, Function>& functions, std::optional<Addr> root = std::nullopt, int depth = 0);

// --- search ---
constexpr size_t MAX_HITS = 2000;
std::vector<Addr> search_bytes(const Binary& b, const std::string& pattern);  // "48 8b ?? 05"
std::vector<std::pair<Addr, std::string>> search_text(const std::vector<std::pair<Addr, std::string>>& items,
                                                      const std::string& q, bool regex = false);

}  // namespace rv
