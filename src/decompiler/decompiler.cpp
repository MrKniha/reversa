// Decompiler driver: lift every block, clean up data flow, name variables, structure control flow.
#include <algorithm>
#include <stdexcept>

#include "core/text.hpp"
#include "decompiler/internal.hpp"

namespace rv {

namespace dec {

Context::Context(const Analysis& an, const Function& fn) : analysis(an), binary(an.binary), function(fn) {
    const Binary& b = binary;
    if (b.arch == "x86-64")
        abi = b.format == "PE" ? "win64" : "sysv";
    else if (b.arch == "x86")
        abi = "cdecl";
    else
        abi = "aapcs64";
    if (b.arch != "x86" && b.arch != "x86-64") {
        uses_frame = true;
    } else {
        auto blk = fn.blocks.find(fn.addr);
        if (blk != fn.blocks.end()) {
            const auto& ins = blk->second.insns;
            for (size_t i = 0; i < ins.size() && i < 6; i++) {
                auto it = an.insns.find(ins[i]);
                if (it != an.insns.end() && it->second->mnemonic == "mov" &&
                    (it->second->op_str == "rbp, rsp" || it->second->op_str == "ebp, esp")) {
                    uses_frame = true;
                    break;
                }
            }
        }
    }
    for (auto& s : b.sections)
        if (!s.executable) data_secs.push_back(&s);
}

E Context::cnst(int64_t v, int size) const {
    Addr a = Addr(v);
    if (v >= 0 && (analysis.string_at.count(a) || (analysis.functions.count(a) && v > 0x100))) return AddrOf(v);
    if (v >= 0x1000)
        for (auto* s : data_secs)
            if (s->addr <= a && a < s->end()) return AddrOf(v);
    return Const(v, size);
}

std::optional<Addr> Context::call_target_of(Addr insn_addr) const {
    for (auto& x : analysis.xrefs.frm(insn_addr))
        if ((x.kind == "call" || x.kind == "jump") && analysis.functions.count(x.to)) return x.to;
    return std::nullopt;
}

std::optional<std::string> Context::callee_name(std::optional<Addr> addr) const {
    if (!addr) return std::nullopt;
    auto it = analysis.auto_names.find(*addr);
    if (it != analysis.auto_names.end()) return it->second;
    for (auto& s : binary.symbols)
        if (s.addr == *addr) return s.name;
    return std::nullopt;
}

namespace {

std::vector<std::string> arg_regs(const Context& ctx) {
    if (ctx.abi == "aapcs64") return {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"};
    return x86_abi_args(ctx.abi);
}

int size_in(const Stmt& st, const std::string& key) {
    std::vector<const Expr*> nodes;
    for (auto& e : stmt_exprs(st)) {
        nodes.clear();
        walk(e, nodes);
        for (auto* x : nodes)
            if (x->k == K::Var && x->key == key) return x->size;
    }
    return 8;
}

// Give incoming argument registers their own variable ("p:rdi") when every use is reached
// either only by the function entry or only by later definitions.
void split_params(Blocks& blocks, Addr entry, const Context& ctx) {
    std::map<Addr, std::vector<Addr>> preds;
    for (Addr a : blocks.order) preds[a];
    for (Addr a : blocks.order)
        for (auto& s : blocks[a].succs)
            if (preds.count(s.to)) preds[s.to].push_back(a);
    for (auto& reg : arg_regs(ctx)) {
        std::string key = "r:" + reg;
        std::map<Addr, bool> defined, from_entry;
        for (Addr a : blocks.order) {
            bool d = false;
            for (auto& st : blocks[a].stmts) d = d || defs(st).count(key);
            defined[a] = d;
            from_entry[a] = false;
        }
        bool changed = true;
        while (changed) {
            changed = false;
            for (Addr a : blocks.order) {
                bool inn = a == entry;
                for (Addr p : preds[a]) inn = inn || (from_entry[p] && !defined[p]);
                if (inn != from_entry[a]) {
                    from_entry[a] = inn;
                    changed = true;
                }
            }
        }
        std::vector<std::pair<Addr, size_t>> plan;
        bool ok = true;
        for (Addr a : blocks.order) {
            bool seen_def = false;
            auto stmts = all_stmts(blocks[a]);
            for (size_t i = 0; i < stmts.size(); i++) {
                const Stmt& st = stmts[i];
                if (uses(st).count(key) && !seen_def && from_entry[a]) {
                    bool other = false;
                    if (a != entry) {
                        for (Addr p : preds[a]) other = other || defined[p] || !from_entry[p];
                    } else {
                        other = !preds[a].empty();
                    }
                    if (other) {
                        ok = false;
                        break;
                    }
                    plan.emplace_back(a, i);
                }
                if (defs(st).count(key)) seen_def = true;
            }
            if (!ok) break;
        }
        if (!ok || plan.empty()) continue;
        for (auto& [a, i] : plan) {
            auto& lb = blocks[a];
            if (i < lb.stmts.size())
                lb.stmts[i] = replace_in(lb.stmts[i], key, Var("p:" + reg, size_in(lb.stmts[i], key)));
            else
                lb.term = replace_in(*lb.term, key, Var("p:" + reg, size_in(*lb.term, key)));
        }
    }
}

std::tuple<int, int64_t, std::string> var_sort(const std::string& k) {
    auto colon = k.find(':');
    std::string kind = k.substr(0, colon), rest = colon == std::string::npos ? "" : k.substr(colon + 1);
    int order = kind == "s" ? 0 : kind == "t" ? 1 : kind == "o" ? 2 : kind == "r" ? 3 : 4;
    int64_t n = 0;
    try {
        size_t used = 0;
        n = std::stoll(rest, &used);
        if (used != rest.size()) n = 0;
    } catch (...) {
        n = 0;
    }
    return {order, n, k};
}

std::string shex(int64_t v) {
    return v < 0 ? "-" + hex_digits(uint64_t(0) - uint64_t(v)) : hex_digits(uint64_t(v));
}

}  // namespace

}  // namespace dec

Decompiled decompile(const Analysis& analysis, Addr func_addr) {
    using namespace dec;
    const Function& fn = analysis.functions.at(func_addr);
    Context ctx(analysis, fn);
    const Binary& b = analysis.binary;
    bool arm = b.arch == "arm64";
    std::string ret_key = arm ? "r:x0" : "r:rax";
    Cfg cfg = build_cfg(fn);
    Blocks blocks;
    for (Addr start : cfg.nodes) {
        const Block& blk = fn.blocks.at(start);
        std::vector<Insn> insns;
        for (Addr a : blk.insns) {
            auto it = analysis.insns.find(a);
            if (it != analysis.insns.end()) {
                insns.push_back(*it->second);
            } else {
                auto d = analysis.decode_at(a, 1);
                if (!d.empty()) insns.push_back(d[0]);
            }
        }
        LiftedBlock lb = arm ? lift_arm64(ctx, blk, insns) : lift_x86(ctx, blk, insns);
        if (!lb.term && lb.succs.size() == 1) lb.term = Goto(lb.succs[0].to, insns.empty() ? blk.start : insns.back().addr);
        blocks.order.push_back(start);
        blocks.map[start] = std::move(lb);
    }
    for (Addr a : blocks.order) {  // keep only edges inside the reachable graph
        auto& s = blocks[a].succs;
        s.erase(std::remove_if(s.begin(), s.end(), [&](const Succ& x) { return !blocks.has(x.to); }), s.end());
    }
    Decompiled d;
    d.func = func_addr;
    d.returns = returns_value(blocks, cfg.entry, ret_key);
    std::vector<Addr> ret_sizes;
    for (Addr a : blocks.order)
        for (auto& st : blocks[a].stmts)
            if (st.k == S::Assign && is(st.dst, K::Var) && st.dst->key == ret_key && !is(st.src, K::Call))
                ret_sizes.push_back(Addr(st.dst->size));
    if (!ret_sizes.empty()) {
        long best = -1;
        for (Addr s : py_set_order_ints(ret_sizes)) {
            long c = long(std::count(ret_sizes.begin(), ret_sizes.end(), s));
            if (c > best) { best = c; d.ret_size = int(s); }
        }
    }
    if (!d.returns)
        for (Addr a : blocks.order) {
            auto& t = blocks[a].term;
            if (t && t->k == S::Return && is(t->expr, K::Var)) t = Return(nullptr, t->addr);
        }
    split_params(blocks, cfg.entry, ctx);
    simplify(blocks);
    // ---- variables
    std::map<Addr, KeySet> live_in, live_out;
    liveness(blocks, live_in, live_out);
    const KeySet& entry_live = live_in[cfg.entry];
    std::map<std::string, std::vector<std::pair<int, int>>> sizes;  // key -> [(size, count)] in first-seen order
    std::vector<const Expr*> nodes;
    for (Addr a : blocks.order)
        for (auto& st : all_stmts(blocks[a]))
            for (auto& e : stmt_exprs(st)) {
                nodes.clear();
                walk(e, nodes);
                for (auto* x : nodes) {
                    if (x->k != K::Var) continue;
                    auto& v = sizes[x->key];
                    auto it = std::find_if(v.begin(), v.end(), [&](auto& p) { return p.first == x->size; });
                    if (it == v.end())
                        v.emplace_back(x->size, 1);
                    else
                        it->second++;
                }
            }
    auto& size_of = d.sizes;
    for (auto& [k, v] : sizes) {
        auto best = v.front();
        for (auto& p : v)
            if (p.second > best.second) best = p;
        size_of[k] = best.first;
    }
    auto regs = arg_regs(ctx);
    if (!regs.empty()) {
        std::vector<std::string> keys;
        for (auto& r : regs) keys.push_back(size_of.count("p:" + r) ? "p:" + r : "r:" + r);
        std::optional<size_t> last;
        for (size_t i = 0; i < keys.size(); i++)
            if (entry_live.count(keys[i]) || keys[i].rfind("p:", 0) == 0) last = i;
        if (last) {
            d.params.assign(keys.begin(), keys.begin() + long(*last + 1));
            for (auto& k : d.params) size_of.emplace(k, 8);
        }
    } else {  // cdecl: positive frame offsets
        std::set<int64_t> offs;
        for (auto& [k, _] : size_of)
            if (k.rfind("s:", 0) == 0) {
                int64_t o = std::get<1>(var_sort(k));
                if (o >= 8) offs.insert(o);
            }
        if (!offs.empty()) {
            int64_t hi = *offs.rbegin();
            for (int64_t i = 0; i < (hi - 8) / 4 + 1; i++) d.params.push_back("s:" + std::to_string(8 + 4 * i));
        }
    }
    auto& names = d.default_names;
    for (size_t i = 0; i < d.params.size(); i++) names[d.params[i]] = "a" + std::to_string(i + 1);
    std::vector<std::string> ordered;
    for (auto& [k, _] : size_of) ordered.push_back(k);
    std::sort(ordered.begin(), ordered.end(), [](const std::string& x, const std::string& y) { return var_sort(x) < var_sort(y); });
    int counter = 0;
    for (auto& k : ordered) {
        if (names.count(k) || k.rfind("p:", 0) == 0) continue;
        d.locals.push_back(k);
        auto colon = k.find(':');
        std::string kind = k.substr(0, colon), rest = colon == std::string::npos ? "" : k.substr(colon + 1);
        if (kind == "r") {
            names[k] = "v" + std::to_string(++counter);
        } else if (kind == "s") {
            int64_t off = std::get<1>(var_sort(k));
            names[k] = off < 0 ? "local_" + shex(-off) : "arg_" + shex(off);
        } else if (kind == "t" || kind == "o") {
            names[k] = "stack_" + shex(std::get<1>(var_sort(k)));
        } else if (kind == "g") {
            names[k] = rest;
        }
    }
    // ---- structure
    try {
        d.tree = structure(blocks, cfg.entry, cfg.loops);
        collect_goto_targets(d.tree, d.labels);
    } catch (const std::exception& exc) {  // fall back to a flat listing with gotos
        d.error = exc.what();
        d.tree.clear();
        d.labels.clear();
        for (Addr a : blocks.order) {
            auto& lb = blocks[a];
            d.tree.push_back(Label(a));
            d.tree.insert(d.tree.end(), lb.stmts.begin(), lb.stmts.end());
            if (lb.term) d.tree.push_back(*lb.term);
            d.labels.insert(a);
        }
    }
    return d;
}

}  // namespace rv
