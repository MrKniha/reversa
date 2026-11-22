#include <algorithm>
#include <chrono>
#include <deque>

#include "analysis/analysis.hpp"
#include "core/errors.hpp"
#include "core/text.hpp"

namespace rv {

const std::set<std::string> NORETURN = {
    "exit", "_exit", "_Exit", "abort", "__stack_chk_fail", "__assert_fail", "__libc_start_main", "ExitProcess",
    "ExitThread", "err", "errx", "verr", "verrx", "__cxa_throw", "_assert", "__chk_fail", "longjmp", "siglongjmp",
    "pthread_exit", "quick_exit", "__fortify_fail"};

constexpr size_t MAX_FUNC_INSNS = 60000;
constexpr size_t DECODE_WINDOW = 512;

static bool is_terminator(Flow f) {
    return f == Flow::Jmp || f == Flow::IJmp || f == Flow::CJmp || f == Flow::Ret || f == Flow::Trap;
}

Analysis::Analysis(const Binary& b)
    : binary(b), disasm(b.arch, b.endian, [&b](Addr a) { return b.is_mapped(a); }) {}

const Function* Analysis::function(Addr addr) const {
    auto it = functions.find(addr);
    return it == functions.end() ? nullptr : &it->second;
}

const Insn* Analysis::insn(Addr addr) const {
    auto it = insns.find(addr);
    return it == insns.end() ? nullptr : it->second;
}

const String* Analysis::string(Addr addr) const {
    auto it = string_at.find(addr);
    return it == string_at.end() ? nullptr : &strings[it->second];
}

const Function* Analysis::function_containing(Addr addr) const {
    if (auto f = function(addr)) return f;
    const Function* best = nullptr;
    for (Addr fa : function_order) {
        const Function& fn = functions.at(fa);
        if (fn.kind == "normal" && fn.addr <= addr && addr < fn.end()) {
            for (auto& [_, blk] : fn.blocks)
                if (blk.start <= addr && addr < blk.end) return &fn;
            if (!best) best = &fn;
        }
    }
    return best;
}

// Functions that call (or tail-jump to) addr. Callers of thunks (strcmp: jmp [__imp_strcmp])
// count as callers of the target.
std::vector<Addr> Analysis::callers(Addr addr, bool through_thunks) const {
    std::set<Addr> out, seen;
    std::vector<Addr> todo{addr};
    while (!todo.empty()) {
        Addr cur = todo.back();
        todo.pop_back();
        if (!seen.insert(cur).second) continue;
        for (auto& x : xrefs.to(cur, {"call", "jump"})) {
            const Function* f = function_containing(x.frm);
            if (!f || !(x.kind == "call" || f->tail_calls.count(cur))) continue;
            auto tt = thunk_target.find(f->addr);
            if (through_thunks && tt != thunk_target.end() && tt->second == cur) todo.push_back(f->addr);
            else out.insert(f->addr);
        }
    }
    return {out.begin(), out.end()};
}

std::vector<Insn> Analysis::decode_at(Addr addr, size_t count) const {
    Bytes code = binary.read(addr, 16 * count + 16);
    return disasm.decode(code.data(), code.size(), addr, count);
}

// Decodes a small window at a time and caches every instruction, so overlapping functions never
// decode the same bytes twice (keeps analysis of corrupted/raw files near-linear).
const Insn* Analysis::insn_at(Addr addr) {
    if (auto it = insns.find(addr); it != insns.end()) return it->second;
    if (auto it = dcache.find(addr); it != dcache.end()) return &it->second;
    Bytes code = binary.read(addr, DECODE_WINDOW);
    if (code.empty()) return nullptr;
    RefTracker tracker;
    const Insn* first = nullptr;
    for (auto& i : disasm.decode(code.data(), code.size(), addr, 0, &tracker)) {
        bool firsti = first == nullptr;
        if (dcache.count(i.addr)) {
            if (firsti) first = &dcache.at(i.addr);
            break;
        }
        Flow f = i.flow;
        auto [it, _] = dcache.emplace(i.addr, std::move(i));
        if (firsti) first = &it->second;
        if (is_terminator(f)) break;
    }
    return first;
}

namespace {

void make_import(Analysis& a, const std::string& name, Addr addr, Addr slot, const std::string& library = "") {
    if (a.import_addr.count(name)) return;
    a.import_addr[name] = addr;
    Function fn;
    fn.addr = addr;
    fn.auto_name = name;
    fn.kind = addr == slot ? "import" : "thunk";
    fn.noreturn = NORETURN.count(name) > 0;
    fn.library = library;
    fn.size = addr != slot ? 16 : a.binary.ptr_size();
    a.functions[addr] = fn;
    a.function_order.push_back(addr);
    a.auto_names[addr] = name;
}

void resolve_imports(Analysis& a, Binary& b) {
    for (auto& imp : b.imports) {
        a.slot_imports[imp.slot] = imp.name;
        a.auto_names.emplace(imp.slot, "ptr_" + imp.name);
    }
    if (b.format == "ELF" && !a.slot_imports.empty()) {
        for (auto& sec : b.sections) {
            if (!sec.executable || !starts_with(sec.name, ".plt")) continue;
            Bytes code = b.read(sec.addr, sec.size);
            // re-decode with a fresh tracker after every resolved stub
            size_t off = 0;
            while (off < code.size()) {
                RefTracker tracker;
                auto ins = a.disasm.decode(code.data() + off, code.size() - off, sec.addr + off, 0, &tracker);
                bool restarted = false;
                for (auto& i : ins) {
                    if (i.flow == Flow::IJmp && i.mem_slot && a.slot_imports.count(*i.mem_slot)) {
                        Addr stub = sec.addr + ((i.addr - sec.addr) & ~Addr(0xF));
                        make_import(a, a.slot_imports[*i.mem_slot], stub, *i.mem_slot);
                        off = i.end() - sec.addr;
                        restarted = true;
                        break;
                    }
                }
                if (!restarted) break;
            }
        }
    }
    for (auto& imp : b.imports) {  // imports reached only through their slot (call [rip+x])
        auto it = a.import_addr.find(imp.name);
        if (it == a.import_addr.end()) {
            make_import(a, imp.name, imp.slot, imp.slot, imp.library);
        } else {
            a.functions[it->second].library = imp.library;
            imp.thunk = it->second;
        }
    }
}

std::optional<Addr> call_target(const Analysis& a, const Insn& ins) {
    if (ins.target) return ins.target;
    if (ins.mem_slot) {
        auto s = a.slot_imports.find(*ins.mem_slot);
        if (s != a.slot_imports.end()) {
            auto it = a.import_addr.find(s->second);
            if (it != a.import_addr.end()) return it->second;
        }
    }
    return std::nullopt;
}

bool is_noreturn(const Analysis& a, std::optional<Addr> target) {
    if (!target) return false;
    auto f = a.function(*target);
    return f && f->noreturn;
}

Function analyze_function(Analysis& a, Addr start, const std::set<Addr>& known) {
    const Binary& b = a.binary;
    Function fn;
    fn.addr = start;
    std::map<Addr, const Insn*> local;
    std::set<Addr> leaders{start};
    std::vector<Addr> work{start};
    while (!work.empty() && local.size() < MAX_FUNC_INSNS) {
        Addr addr = work.back();
        work.pop_back();
        if (local.count(addr) || !b.is_code(addr)) continue;
        while (true) {
            const Insn* ins = a.insn_at(addr);
            if (!ins || local.count(ins->addr)) break;
            if (ins->addr != start && known.count(ins->addr) && a.functions.count(ins->addr)) break;  // another function
            local[ins->addr] = ins;
            a.insns.emplace(ins->addr, ins);
            Addr nxt = ins->end();
            Flow f = ins->flow;
            if (f == Flow::Call || f == Flow::ICall) {
                auto tgt = call_target(a, *ins);
                if (tgt) {
                    fn.callees.insert(*tgt);
                    a.xrefs.add(ins->addr, *tgt, "call");
                }
                if (is_noreturn(a, tgt)) break;
            } else if (f == Flow::CJmp) {
                if (ins->target && b.is_code(*ins->target)) {
                    a.xrefs.add(ins->addr, *ins->target, "jump");
                    leaders.insert(*ins->target);
                    work.push_back(*ins->target);
                }
                leaders.insert(nxt);
                work.push_back(nxt);
                break;
            } else if (f == Flow::Jmp) {
                if (ins->target) {
                    Addr tgt = *ins->target;
                    a.xrefs.add(ins->addr, tgt, "jump");
                    if (tgt != start && (known.count(tgt) || a.functions.count(tgt) || !b.is_code(tgt))) {
                        fn.tail_calls.insert(tgt);
                    } else {
                        leaders.insert(tgt);
                        work.push_back(tgt);
                    }
                }
                break;
            } else if (f == Flow::IJmp) {
                auto tgt = call_target(a, *ins);
                if (tgt) {
                    fn.tail_calls.insert(*tgt);
                    a.xrefs.add(ins->addr, *tgt, "jump");
                }
                break;
            } else if (f == Flow::Ret || f == Flow::Trap) {
                break;
            }
            addr = nxt;
            if (known.count(addr) && addr != start) break;  // fell through into the next function
            if (local.size() >= MAX_FUNC_INSNS) break;
        }
    }
    for (auto& [ad, ins] : local)
        for (auto& r : ins->refs) a.xrefs.add(ins->addr, r.addr, r.kind);
    // build blocks
    std::vector<Addr> addrs;
    for (auto& [ad, _] : local) addrs.push_back(ad);
    Block* cur = nullptr;
    for (size_t i = 0; i < addrs.size(); i++) {
        Addr ad = addrs[i];
        const Insn* ins = local[ad];
        if (!cur || leaders.count(ad) || ad != cur->end) {
            if (cur && ad == cur->end && cur->succs.empty()) {
                Flow lf = local[cur->insns.back()]->flow;
                if (lf == Flow::Seq || lf == Flow::Call || lf == Flow::ICall) cur->succs.push_back({ad, "fall"});
            }
            cur = &fn.blocks[ad];
            cur->start = cur->end = ad;
        }
        cur->insns.push_back(ad);
        cur->end = ins->end();
        Flow f = ins->flow;
        bool last = i + 1 == addrs.size() || addrs[i + 1] != ins->end() || leaders.count(addrs[i + 1]);
        if (f == Flow::CJmp) {
            if (ins->target && local.count(*ins->target)) cur->succs.push_back({*ins->target, "true"});
            if (local.count(ins->end())) cur->succs.push_back({ins->end(), "false"});
            cur = nullptr;
        } else if (f == Flow::Jmp) {
            if (ins->target && local.count(*ins->target) && !fn.tail_calls.count(*ins->target))
                cur->succs.push_back({*ins->target, "jump"});
            cur = nullptr;
        } else if (f == Flow::Ret || f == Flow::Trap || f == Flow::IJmp) {
            cur = nullptr;
        } else if ((f == Flow::Call || f == Flow::ICall) && is_noreturn(a, call_target(a, *ins))) {
            cur = nullptr;
        } else if (last && local.count(ins->end())) {
            cur->succs.push_back({ins->end(), "fall"});
        }
    }
    fn.insn_count = local.size();
    Addr mx = start;
    for (auto& [_, ins] : local) mx = std::max(mx, ins->end());
    fn.size = local.empty() ? 0 : mx - start;
    return fn;
}

void add_function(Analysis& a, Function fn) {
    Addr ad = fn.addr;
    if (!a.functions.count(ad)) a.function_order.push_back(ad);
    a.functions[ad] = std::move(fn);
}

// A function whose only instruction is a jump to an import is a thunk.
void mark_thunks(Analysis& a) {
    for (Addr ad : std::vector<Addr>(a.function_order)) {
        Function& fn = a.functions[ad];
        if (fn.kind == "normal" && fn.tail_calls.size() == 1 && fn.insn_count <= 2 && fn.callees.empty()) {
            Addr tgt = *fn.tail_calls.begin();
            auto t = a.functions.find(tgt);
            if (t != a.functions.end() && (t->second.kind == "import" || t->second.kind == "thunk")) {
                fn.kind = "thunk";
                fn.noreturn = t->second.noreturn;
                a.thunk_target[fn.addr] = tgt;
                a.auto_names.emplace(fn.addr, t->second.auto_name);
            }
        }
    }
}

std::optional<Addr> read_pointer(const Analysis& a, Addr addr) {
    const Binary& b = a.binary;
    for (auto& r : b.relocations)
        if (r.addr == addr && contains(r.type, "RELATIVE"))
            return (b.base && r.addend < int64_t(b.base)) ? b.base + r.addend : Addr(r.addend);
    auto v = b.read_int(addr, b.ptr_size());
    if (!v) return std::nullopt;
    return Addr(*v);
}

void find_main(Analysis& a, std::set<Addr>& known) {
    const Binary& b = a.binary;
    for (auto& s : b.symbols)
        if ((s.name == "main" || s.name == "_main" || s.name == "wmain" || s.name == "WinMain" || s.name == "wWinMain") &&
            a.functions.count(s.addr)) {
            a.main = s.addr;
            return;
        }
    const Function* entry = b.entry ? a.function(*b.entry) : nullptr;
    auto lsm = a.import_addr.find("__libc_start_main");
    if (!entry || lsm == a.import_addr.end()) return;
    std::string arg_reg = b.arch == "x86-64" ? "rdi" : b.arch == "arm64" ? "x0" : "";
    std::optional<Addr> candidate;
    std::vector<Addr> all;
    for (auto& [_, blk] : entry->blocks) all.insert(all.end(), blk.insns.begin(), blk.insns.end());
    std::sort(all.begin(), all.end());
    for (Addr ad : all) {
        const Insn* ins = a.insn(ad);
        if ((ins->flow == Flow::Call || ins->flow == Flow::ICall) && call_target(a, *ins) == lsm->second) break;
        if (b.arch == "x86" && ins->mnemonic == "push" && !ins->refs.empty()) {
            candidate = ins->refs[0].addr;
        } else if (!arg_reg.empty() && (starts_with(ins->op_str, arg_reg + ",") || starts_with(ins->op_str, arg_reg + " "))) {
            for (auto& r : ins->refs) candidate = r.kind == "read" ? read_pointer(a, r.addr) : std::optional<Addr>(r.addr);
            if (ins->refs.empty() && ins->mnemonic == "mov") {
                auto parts = split(ins->op_str, ',');
                if (parts.size() > 1) {
                    try {
                        std::string v = trim(parts[1]);
                        size_t used = 0;
                        unsigned long long n = std::stoull(v, &used, 0);
                        if (used == v.size()) candidate = Addr(n);
                    } catch (...) {}
                }
            }
        }
    }
    if (candidate && *candidate && b.is_code(*candidate)) {
        a.main = *candidate;
        a.auto_names[*candidate] = "main";
        if (!a.functions.count(*candidate)) {
            known.insert(*candidate);
            std::vector<Addr> queue{*candidate};
            while (!queue.empty()) {
                Addr ad = queue.back();
                queue.pop_back();
                if (a.functions.count(ad) || !b.is_code(ad)) continue;
                Function fn = analyze_function(a, ad, known);
                std::set<Addr> next(fn.callees);
                next.insert(fn.tail_calls.begin(), fn.tail_calls.end());
                add_function(a, std::move(fn));
                for (Addr c : next)
                    if (!a.functions.count(c)) { known.insert(c); queue.push_back(c); }
            }
        }
    }
}

// Pointers stored in data sections (tables of strings, function pointers).
void data_pointers(Analysis& a) {
    const Binary& b = a.binary;
    int ps = b.ptr_size();
    std::map<Addr, int64_t> rel;
    for (auto& r : b.relocations)
        if (contains(r.type, "RELATIVE")) rel[r.addr] = r.addend;
    std::set<Addr> targets;
    for (auto& [ad, _] : a.string_at) targets.insert(ad);
    for (auto& [ad, fn] : a.functions)
        if (fn.kind == "normal") targets.insert(ad);
    const Bytes& d = b.bytes();
    bool little = b.endian == "little";
    for (auto& sec : b.sections) {
        if (sec.executable || sec.file_size == 0 || sec.file_size > 32 * 1024 * 1024 || sec.offset >= d.size()) continue;
        size_t n = std::min<uint64_t>(sec.file_size, d.size() - sec.offset);
        const uint8_t* blob = d.data() + sec.offset;
        for (size_t off = 0; off + ps <= n; off += ps) {
            Addr addr = sec.addr + off;
            uint64_t val = 0;
            auto r = rel.find(addr);
            if (r != rel.end() && r->second) val = uint64_t(r->second);
            else
                for (int i = 0; i < ps; i++) val = (val << 8) | blob[off + (little ? ps - 1 - i : i)];
            if (targets.count(val)) a.xrefs.add(addr, val, "data");
        }
    }
}

}  // namespace

std::unique_ptr<Analysis> analyze(Binary& binary, Progress progress, const std::atomic<bool>* cancel,
                                  std::function<void(const Function&)> on_function) {
    auto t0 = std::chrono::steady_clock::now();
    auto a = std::make_unique<Analysis>(binary);
    auto report = [&](double f, const std::string& m) { if (progress) progress(f, m); };
    auto check = [&] { if (cancel && cancel->load()) throw Cancelled("Analysis cancelled."); };
    const Binary& b = binary;

    report(0.02, "Finding imported functions");
    resolve_imports(*a, binary);
    report(0.05, "Finding functions");
    std::vector<Addr> seeds;
    if (b.entry && b.is_code(*b.entry)) {
        seeds.push_back(*b.entry);
        a->auto_names.emplace(*b.entry, b.format != "RAW" ? "entry" : "start");
    }
    for (auto& s : b.symbols)
        if (s.kind == "func" && b.is_code(s.addr)) seeds.push_back(s.addr);
    for (auto& e : b.exports)
        if (b.is_code(e.addr)) seeds.push_back(e.addr);
    for (Addr x : b.extra_function_starts)
        if (b.is_code(x)) seeds.push_back(x);
    std::set<Addr> known(seeds.begin(), seeds.end());
    for (auto& [ad, _] : a->functions) known.insert(ad);
    std::deque<Addr> queue;
    std::set<Addr> dedup;
    for (Addr s : seeds)
        if (dedup.insert(s).second) queue.push_back(s);
    size_t done = 0;
    while (!queue.empty()) {
        check();
        Addr addr = queue.front();
        queue.pop_front();
        if (a->functions.count(addr)) continue;
        Function fn = analyze_function(*a, addr, known);
        std::set<Addr> next(fn.callees);
        next.insert(fn.tail_calls.begin(), fn.tail_calls.end());
        add_function(*a, std::move(fn));
        for (Addr c : next) {
            if (!a->functions.count(c) && b.is_code(c) && !known.count(c)) {
                known.insert(c);
                queue.push_back(c);
            } else if (!a->functions.count(c) && b.is_code(c)) {
                queue.push_back(c);
            }
        }
        if (++done % 25 == 0)
            report(std::min(0.75, 0.05 + 0.7 * double(done) / double(done + queue.size() + 1)),
                   "Analysing functions (" + std::to_string(done) + ")");
    }
    mark_thunks(*a);
    report(0.78, "Finding the main function");
    find_main(*a, known);
    report(0.82, "Finding strings");
    a->strings = find_strings(binary);
    for (size_t i = 0; i < a->strings.size(); i++) a->string_at[a->strings[i].addr] = i;
    report(0.9, "Computing cross-references");
    data_pointers(*a);
    if (on_function)
        for (Addr ad : a->function_order)
            if (a->functions[ad].kind == "normal") on_function(a->functions[ad]);
    a->duration = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    a->done = true;
    report(1.0, "Analysis finished");
    return a;
}

}  // namespace rv
