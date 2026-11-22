// Data-flow clean-up passes: liveness, expression inlining, dead code removal.
#include <algorithm>

#include "decompiler/internal.hpp"

namespace rv::dec {

namespace {
bool tracked(const std::string& key) { return key.rfind("r:", 0) == 0 || key.rfind("o:", 0) == 0; }

bool is_tracked_assign(const Stmt& st) { return st.k == S::Assign && is(st.dst, K::Var) && tracked(st.dst->key); }

bool intersects(const KeySet& a, const KeySet& b) {
    for (auto& k : a)
        if (b.count(k)) return true;
    return false;
}

// Expressions in which a variable counts as "used" (the destination variable of an assignment does not).
std::vector<E> use_targets(const Stmt& st) {
    switch (st.k) {
        case S::Assign:
            if (is(st.dst, K::Var)) return {st.src};
            return {st.src, st.dst};
        case S::ExprStmt:
        case S::Return:
        case S::Branch:
        case S::Goto: return {st.expr};
        default: return {};
    }
}

int count_uses(const Stmt& st, const std::string& key) {
    int n = 0;
    std::vector<const Expr*> nodes;
    for (auto& t : use_targets(st)) {
        if (!t) continue;
        nodes.clear();
        walk(t, nodes);
        for (auto* x : nodes)
            if (x->k == K::Var && x->key == key) n++;
    }
    return n;
}

// The expression has_call() inspects in a statement between definition and use ("src" or "expr" attribute).
E src_or_expr(const Stmt& st) {
    if (st.k == S::Assign) return st.src;
    if (st.k == S::ExprStmt || st.k == S::Goto) return st.expr;
    return nullptr;
}

void put(LiftedBlock& lb, std::vector<Stmt>& seq, size_t j, Stmt st) {
    if (j < lb.stmts.size())
        lb.stmts[j] = st;
    else
        lb.term = st;
    seq[j] = std::move(st);
}

void erase(LiftedBlock& lb, std::vector<Stmt>& seq, size_t i) {
    seq.erase(seq.begin() + static_cast<long>(i));
    lb.stmts.erase(lb.stmts.begin() + static_cast<long>(i));
}
}  // namespace

KeySet expr_vars(const E& e) {
    KeySet out;
    if (!e) return out;
    std::vector<const Expr*> nodes;
    walk(e, nodes);
    for (auto* x : nodes)
        if (x->k == K::Var) out.insert(x->key);
    return out;
}

bool reads_memory(const E& e) {
    std::vector<const Expr*> nodes;
    walk(e, nodes);
    for (auto* x : nodes)
        if (x->k == K::Load || x->k == K::Global || x->k == K::Call || (x->k == K::Var && !tracked(x->key))) return true;
    return false;
}

bool has_call(const E& e) {
    std::vector<const Expr*> nodes;
    walk(e, nodes);
    for (auto* x : nodes)
        if (x->k == K::Call) return true;
    return false;
}

KeySet uses(const Stmt& st) {
    switch (st.k) {
        case S::Assign: {
            auto u = expr_vars(st.src);
            if (!is(st.dst, K::Var)) {
                auto d = expr_vars(st.dst);
                u.insert(d.begin(), d.end());
            }
            return u;
        }
        case S::ExprStmt:
        case S::Return:
        case S::Branch:
        case S::Goto: return expr_vars(st.expr);
        default: return {};
    }
}

KeySet defs(const Stmt& st) {
    KeySet d;
    if (st.k == S::Assign && is(st.dst, K::Var)) d.insert(st.dst->key);
    for (auto& c : st.clobbers) d.insert("r:" + c);
    return d;
}

bool writes_memory(const Stmt& st) {
    if (st.k == S::Assign) return !(is(st.dst, K::Var) && tracked(st.dst->key)) || has_call(st.src);
    return st.k == S::ExprStmt;
}

std::vector<Stmt> all_stmts(const LiftedBlock& lb) {
    auto v = lb.stmts;
    if (lb.term) v.push_back(*lb.term);
    return v;
}

std::vector<E> stmt_exprs(const Stmt& st) {
    switch (st.k) {
        case S::Assign: return {st.dst, st.src};
        case S::ExprStmt: return {st.expr};
        case S::Return:
            if (st.expr) return {st.expr};
            return {};
        case S::Branch:
        case S::If:
        case S::While: return {st.expr};
        case S::Goto:
            if (st.expr) return {st.expr};
            return {};
        default: return {};
    }
}

void liveness(const Blocks& blocks, std::map<Addr, KeySet>& live_in, std::map<Addr, KeySet>& live_out) {
    std::map<Addr, KeySet> use, df;
    for (Addr a : blocks.order) {
        KeySet u, d;
        for (auto& st : all_stmts(blocks[a])) {
            for (auto& k : uses(st))
                if (tracked(k) && !d.count(k)) u.insert(k);
            auto ds = defs(st);
            d.insert(ds.begin(), ds.end());
        }
        use[a] = std::move(u);
        df[a] = std::move(d);
    }
    live_in.clear();
    live_out.clear();
    for (Addr a : blocks.order) live_in[a], live_out[a];
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto it = blocks.order.rbegin(); it != blocks.order.rend(); ++it) {
            Addr a = *it;
            KeySet out;
            for (auto& s : blocks[a].succs) {
                auto li = live_in.find(s.to);
                if (li != live_in.end()) out.insert(li->second.begin(), li->second.end());
            }
            KeySet inn = use[a];
            for (auto& k : out)
                if (!df[a].count(k)) inn.insert(k);
            if (out != live_out[a] || inn != live_in[a]) {
                live_out[a] = std::move(out);
                live_in[a] = std::move(inn);
                changed = true;
            }
        }
    }
}

Stmt replace_in(const Stmt& st, const std::string& key, const E& rep) {
    Stmt r = st;
    switch (st.k) {
        case S::Assign:
            if (!is(st.dst, K::Var)) r.dst = substitute(st.dst, key, rep);
            r.src = substitute(st.src, key, rep);
            break;
        case S::ExprStmt:
        case S::Return:
        case S::Branch:
        case S::Goto:
            if (st.expr) r.expr = substitute(st.expr, key, rep);
            break;
        default: break;
    }
    return r;
}

// One round of inlining + dead store elimination inside a block.
static bool simplify_block(LiftedBlock& lb, const KeySet& live_out) {
    auto seq = all_stmts(lb);
    bool changed = false;
    size_t i = 0;
    while (i < lb.stmts.size()) {
        const Stmt st = seq[i];
        if (!is_tracked_assign(st)) {
            i++;
            continue;
        }
        const std::string key = st.dst->key;
        std::optional<size_t> use_at;
        int n_uses = 0;
        bool redefined = false;
        std::vector<size_t> use_idx;
        for (size_t j = i + 1; j < seq.size(); j++) {
            int c = count_uses(seq[j], key);
            if (c) {
                n_uses += c;
                use_idx.push_back(j);
                if (!use_at) use_at = j;
            }
            if (defs(seq[j]).count(key)) {
                redefined = true;
                break;
            }
        }
        bool live_after = !redefined && live_out.count(key);
        if (n_uses == 0 && !live_after) {
            if (has_call(st.src)) {
                put(lb, seq, i, ExprStmt(st.src, st.addr, st.clobbers));
                i++;
            } else {
                erase(lb, seq, i);
            }
            changed = true;
            continue;
        }
        if (n_uses >= 1 && !live_after && (is(st.src, K::Var) || is(st.src, K::Const)) && use_at) {
            size_t last = use_idx.back();
            auto src_keys = expr_vars(st.src);
            bool blocked = false;
            for (size_t k = i + 1; k < last; k++)
                if (intersects(defs(seq[k]), src_keys)) blocked = true;
            if (!blocked) {
                for (size_t j : use_idx) put(lb, seq, j, replace_in(seq[j], key, st.src));
                erase(lb, seq, i);
                changed = true;
                continue;
            }
        }
        if (n_uses == 1 && !live_after && use_at) {
            auto src_vars = expr_vars(st.src);
            bool src_call = has_call(st.src), src_reads = reads_memory(st.src);
            bool ok = !(src_call && *use_at != i + 1);
            for (size_t k = i + 1; ok && k < *use_at; k++) {
                const Stmt& mid = seq[k];
                if (intersects(defs(mid), src_vars)) ok = false;
                else if (src_reads && writes_memory(mid)) ok = false;
                else if (src_call) {
                    E m = src_or_expr(mid);
                    if (writes_memory(mid) || (m && has_call(m))) ok = false;
                }
            }
            if (ok) {
                put(lb, seq, *use_at, replace_in(seq[*use_at], key, st.src));
                erase(lb, seq, i);
                changed = true;
                continue;
            }
        }
        i++;
    }
    return changed;
}

void simplify(Blocks& blocks, int rounds) {
    for (int r = 0; r < rounds; r++) {
        std::map<Addr, KeySet> live_in, live_out;
        liveness(blocks, live_in, live_out);
        bool changed = false;
        for (Addr a : blocks.order)
            if (simplify_block(blocks[a], live_out[a])) changed = true;
        if (!changed) break;
    }
}

// True if some path reaches a return with the return register set by a non-call instruction.
bool returns_value(const Blocks& blocks, Addr entry, const std::string& ret_key) {
    std::map<Addr, std::vector<Addr>> preds;
    for (Addr a : blocks.order) preds[a];
    for (Addr a : blocks.order)
        for (auto& s : blocks[a].succs)
            if (preds.count(s.to)) preds[s.to].push_back(a);
    std::map<Addr, std::string> kinds;  // "" = no definition in the block
    for (Addr a : blocks.order) {
        const auto& lb = blocks[a];
        auto seq = all_stmts(lb);
        std::string k;
        for (size_t i = 0; i < lb.stmts.size(); i++) {
            const Stmt& st = lb.stmts[i];
            if (st.k == S::Assign && is(st.dst, K::Var) && st.dst->key == ret_key) {
                k = has_call(st.src) ? "call" : "val";
                if (k == "val")
                    for (size_t j = i + 1; j < seq.size(); j++)
                        if (seq[j].k != S::Return && uses(seq[j]).count(ret_key)) {
                            k = "used";  // value consumed (e.g. by a compare): a temporary, not a return value
                            break;
                        }
            } else if (defs(st).count(ret_key)) {
                k = "call";
            }
        }
        kinds[a] = k;
    }
    std::map<Addr, std::set<std::string>> in_kind, out_kind;
    for (Addr a : blocks.order) in_kind[a], out_kind[a];
    bool changed = true;
    while (changed) {
        changed = false;
        for (Addr a : blocks.order) {
            std::set<std::string> inn;
            for (Addr p : preds[a]) inn.insert(out_kind[p].begin(), out_kind[p].end());
            if (a == entry) inn.insert("param");
            std::set<std::string> out = kinds[a].empty() ? inn : std::set<std::string>{kinds[a]};
            if (inn != in_kind[a] || out != out_kind[a]) {
                in_kind[a] = std::move(inn);
                out_kind[a] = std::move(out);
                changed = true;
            }
        }
    }
    for (Addr a : blocks.order) {
        const auto& t = blocks[a].term;
        if (t && t->k == S::Return && is(t->expr, K::Var) && out_kind[a].count("val")) return true;
    }
    return false;
}

}  // namespace rv::dec
