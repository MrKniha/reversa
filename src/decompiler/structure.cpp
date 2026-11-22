// Control-flow structuring: CFG -> nested if/else, while, do-while, break/continue.
//
// A simplified version of the "structural analysis" family of algorithms (Cifuentes,
// "Reverse Compilation Techniques", 1994; also the idea behind Ghidra's and RetDec's
// structurers): loops come from natural loops (back edges to a dominator), if/else regions end
// at the immediate post-dominator. Anything that does not fit becomes a labelled goto - always
// correct, never pretty.
#include <iterator>
#include <algorithm>
#include <deque>
#include <stdexcept>

#include "decompiler/internal.hpp"

namespace rv::dec {

// Iteration order of a CPython set built by adding `items` one by one (int hash == value).
// Used where the reference implementation breaks ties by set order, so both give the same output.
std::vector<Addr> py_set_order_ints(const std::vector<Addr>& items) {
    std::vector<std::optional<Addr>> table(8);
    size_t used = 0;
    auto insert = [](std::vector<std::optional<Addr>>& t, Addr v) {
        size_t mask = t.size() - 1, perturb = size_t(v), i = size_t(v) & mask;
        while (true) {
            if (!t[i]) { t[i] = v; return; }
            if (i + 9 <= mask)
                for (size_t j = 1; j <= 9; j++)
                    if (!t[i + j]) { t[i + j] = v; return; }
            perturb >>= 5;
            i = (i * 5 + 1 + perturb) & mask;
        }
    };
    for (Addr v : items) {
        if (std::find(table.begin(), table.end(), std::optional<Addr>(v)) != table.end()) continue;
        insert(table, v);
        used++;
        if (used * 5 >= (table.size() - 1) * 3) {
            size_t n = 8;
            while (n <= used * 4) n <<= 1;
            std::vector<std::optional<Addr>> bigger(n);
            for (auto& e : table)
                if (e) insert(bigger, *e);
            table.swap(bigger);
        }
    }
    std::vector<Addr> out;
    for (auto& e : table)
        if (e) out.push_back(*e);
    return out;
}

namespace {

constexpr Addr EXIT = ~Addr(0);

bool is_jump(const Stmt& s, const char* kind) { return s.k == S::Jump && s.text == kind; }

bool is_only(const std::vector<Stmt>& lst, const char* kind) {
    const Stmt* real = nullptr;
    size_t n = 0;
    for (auto& x : lst)
        if (x.k != S::Label) { real = &x; n++; }
    return n == 1 && is_jump(*real, kind);
}

bool has_stmts(const std::vector<Stmt>& lst, size_t end) {
    for (size_t i = 0; i < end && i < lst.size(); i++)
        if (lst[i].k != S::Label) return true;
    return false;
}

void strip_continue(std::vector<Stmt>& v) {
    while (!v.empty() && is_jump(v.back(), "continue")) v.pop_back();
}

Stmt While(E cond, std::vector<Stmt> body, Addr addr, bool do_while = false) {
    Stmt s;
    s.k = S::While;
    s.expr = std::move(cond);
    s.body = std::move(body);
    s.addr = addr;
    s.do_while = do_while;
    return s;
}

Stmt If(E cond, std::vector<Stmt> then, std::vector<Stmt> other, Addr addr) {
    Stmt s;
    s.k = S::If;
    s.expr = std::move(cond);
    s.then = std::move(then);
    s.other = std::move(other);
    s.addr = addr;
    return s;
}

Stmt make_loop(const std::vector<Stmt>& inner, Addr addr) {
    std::vector<Stmt> body = inner;
    strip_continue(body);
    std::optional<size_t> first;
    for (size_t i = 0; i < body.size(); i++)
        if (body[i].k != S::Label) { first = i; break; }
    auto prefix = [&](size_t end) { return std::vector<Stmt>(body.begin(), body.begin() + long(end)); };
    auto after = [&](size_t from) { return std::vector<Stmt>(body.begin() + long(from), body.end()); };
    if (first && body[*first].k == S::If) {
        const Stmt head = body[*first];
        if (!head.other.empty() && is_only(head.other, "break") && !has_stmts(body, *first)) {
            auto rest = head.then;
            auto tail = after(*first + 1);
            rest.insert(rest.end(), tail.begin(), tail.end());
            strip_continue(rest);
            auto b = prefix(*first);
            b.insert(b.end(), rest.begin(), rest.end());
            return While(head.expr, std::move(b), addr);
        }
        if (is_only(head.then, "break") && head.other.empty() && !has_stmts(body, *first)) {
            auto b = prefix(*first);
            auto tail = after(*first + 1);
            b.insert(b.end(), tail.begin(), tail.end());
            return While(negate(head.expr), std::move(b), addr);
        }
        if (is_only(head.then, "break") && !head.other.empty() && !has_stmts(body, *first)) {
            auto rest = head.other;
            auto tail = after(*first + 1);
            rest.insert(rest.end(), tail.begin(), tail.end());
            strip_continue(rest);
            auto b = prefix(*first);
            b.insert(b.end(), rest.begin(), rest.end());
            return While(negate(head.expr), std::move(b), addr);
        }
    }
    if (!body.empty() && body.back().k == S::If) {
        const Stmt tail = body.back();
        auto b = prefix(body.size() - 1);
        if (is_only(tail.then, "break") && tail.other.empty()) return While(negate(tail.expr), std::move(b), addr, true);
        if (tail.then.empty() && is_only(tail.other, "break")) return While(tail.expr, std::move(b), addr, true);
    }
    return While(nullptr, std::move(body), addr);
}

std::map<Addr, Addr> post_dominators(const Blocks& blocks) {
    const auto& nodes = blocks.order;
    std::map<Addr, std::vector<Addr>> succs;
    for (Addr n : nodes) {
        auto& v = succs[n];
        for (auto& s : blocks[n].succs)
            if (blocks.has(s.to)) v.push_back(s.to);
        if (v.empty()) v.push_back(EXIT);
    }
    std::set<Addr> all(nodes.begin(), nodes.end());
    all.insert(EXIT);
    std::map<Addr, std::set<Addr>> pdom;
    for (Addr n : nodes) pdom[n] = all;
    pdom[EXIT] = {EXIT};
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
            Addr n = *it;
            std::optional<std::set<Addr>> acc;
            for (Addr s : succs[n]) {
                auto p = pdom.find(s);
                if (p == pdom.end()) continue;
                if (!acc) {
                    acc = p->second;
                } else {
                    std::set<Addr> r;
                    std::set_intersection(acc->begin(), acc->end(), p->second.begin(), p->second.end(), std::inserter(r, r.end()));
                    acc = std::move(r);
                }
            }
            std::set<Addr> nw = acc ? *acc : std::set<Addr>{};
            nw.insert(n);
            if (nw != pdom[n]) {
                pdom[n] = std::move(nw);
                changed = true;
            }
        }
    }
    std::map<Addr, Addr> ipdom;
    for (Addr n : nodes) {
        std::optional<Addr> best;
        size_t best_len = 0;
        for (Addr p : pdom[n]) {
            if (p == n) continue;
            size_t len = pdom.count(p) ? pdom[p].size() : 0;
            if (!best || len > best_len) { best = p; best_len = len; }
        }
        if (best) ipdom[n] = *best;
    }
    return ipdom;
}

struct LoopCtx {
    Addr header;
    std::set<Addr> body;
    std::optional<Addr> follow;
};

class Structurer {
public:
    Structurer(const Blocks& blocks, Addr entry, const std::map<Addr, std::vector<Addr>>& loops)
        : blocks(blocks), entry(entry), loops(loops) {}

    std::vector<Stmt> run() {
        ipdom = post_dominators(blocks);
        auto out = seq(entry, std::nullopt, nullptr);
        while (!pending.empty()) {
            Addr n = pending.front();
            pending.pop_front();
            if (!emitted.count(n) && blocks.has(n)) {
                auto more = seq(n, std::nullopt, nullptr);
                out.insert(out.end(), more.begin(), more.end());
            }
        }
        return out;
    }

private:
    const Blocks& blocks;
    Addr entry;
    const std::map<Addr, std::vector<Addr>>& loops;
    std::map<Addr, Addr> ipdom;
    std::set<Addr> emitted, labels;
    std::deque<Addr> pending;

    std::optional<Addr> loop_follow(Addr header, const std::set<Addr>& body) const {
        std::vector<Addr> exits;
        for (Addr n : body)
            for (auto& s : blocks[n].succs)
                if (!body.count(s.to) && blocks.has(s.to)) exits.push_back(s.to);
        if (exits.empty()) return std::nullopt;
        auto ip = ipdom.find(header);
        if (ip != ipdom.end() && std::find(exits.begin(), exits.end(), ip->second) != exits.end()) return ip->second;
        std::optional<Addr> best;
        long best_n = -1;
        for (Addr e : py_set_order_ints(exits)) {
            long c = long(std::count(exits.begin(), exits.end(), e));
            if (c > best_n) { best = e; best_n = c; }
        }
        return best;
    }

    // Handle a transfer to target; returns the next node to emit in this sequence or nothing.
    std::optional<Addr> go(std::optional<Addr> target, std::optional<Addr> stop, const LoopCtx* loop, std::vector<Stmt>& out) {
        if (!target || target == stop) return std::nullopt;
        if (loop) {
            if (*target == loop->header) {
                out.push_back(Jump("continue"));
                return std::nullopt;
            }
            if (target == loop->follow) {
                out.push_back(Jump("break"));
                return std::nullopt;
            }
            if (!loop->body.count(*target)) {
                out.push_back(Jump("goto", target));
                labels.insert(*target);
                pending.push_back(*target);
                return std::nullopt;
            }
        }
        if (emitted.count(*target) || !blocks.has(*target)) {
            out.push_back(Jump("goto", target));
            labels.insert(*target);
            return std::nullopt;
        }
        return target;
    }

    std::vector<Stmt> seq(std::optional<Addr> n, std::optional<Addr> stop, const LoopCtx* loop) {
        std::vector<Stmt> out;
        int guard = 0;
        while (n && n != stop) {
            if (++guard > 10000) break;
            if (emitted.count(*n)) {
                out.push_back(Jump("goto", n));
                labels.insert(*n);
                break;
            }
            auto lp = loops.find(*n);
            if (lp != loops.end() && (!loop || loop->header != *n)) {
                LoopCtx ctx{*n, std::set<Addr>(lp->second.begin(), lp->second.end()), std::nullopt};
                ctx.follow = loop_follow(*n, ctx.body);
                auto inner = seq(n, std::nullopt, &ctx);
                out.push_back(make_loop(inner, blocks[*n].start));
                n = ctx.follow;
                if (n) n = go(n, stop, loop, out);
                continue;
            }
            emitted.insert(*n);
            const LiftedBlock& lb = blocks[*n];
            out.push_back(Label(*n));
            out.insert(out.end(), lb.stmts.begin(), lb.stmts.end());
            const auto& t = lb.term;
            if (t && (t->k == S::Return || (t->k == S::Goto && !t->target))) {
                out.push_back(*t);
                break;
            }
            if (t && t->k == S::Branch) {
                std::optional<Addr> merge;
                auto ip = ipdom.find(*n);
                if (ip != ipdom.end() && ip->second != EXIT) merge = ip->second;
                if (loop && merge && !loop->body.count(*merge)) merge.reset();
                if (merge && emitted.count(*merge)) merge.reset();
                auto then = branch(t->t, merge, loop);
                auto other = branch(t->f, merge, loop);
                E cond = t->expr;
                if (then.empty() && !other.empty()) {
                    cond = negate(cond);
                    then.swap(other);
                    other.clear();
                }
                if (!then.empty() || !other.empty()) out.push_back(If(cond, std::move(then), std::move(other), t->addr));
                n = merge;
                if (n && loop && *n == loop->header) {
                    out.push_back(Jump("continue"));
                    break;
                }
                if (n && n == stop) break;
                continue;
            }
            // fall-through / unconditional jump
            std::optional<Addr> succ;
            if (t && t->k == S::Goto)
                succ = t->target;
            else if (!lb.succs.empty())
                succ = lb.succs[0].to;
            n = go(succ, stop, loop, out);
        }
        return out;
    }

    std::vector<Stmt> branch(Addr target, std::optional<Addr> merge, const LoopCtx* loop) {
        if (merge && target == *merge) return {};
        std::vector<Stmt> out;
        auto nxt = go(target, merge, loop, out);
        if (!nxt) return out;
        return seq(nxt, merge, loop);
    }
};

}  // namespace

std::vector<Stmt> structure(const Blocks& blocks, Addr entry, const std::map<Addr, std::vector<Addr>>& loops) {
    return Structurer(blocks, entry, loops).run();
}

void collect_goto_targets(const std::vector<Stmt>& nodes, std::set<Addr>& acc) {
    for (auto& n : nodes) {
        if (n.k == S::Jump && n.text == "goto" && n.target)
            acc.insert(*n.target);
        else if (n.k == S::Goto && n.target)
            acc.insert(*n.target);
        else if (n.k == S::Branch) {
            acc.insert(n.t);
            acc.insert(n.f);
        } else if (n.k == S::If) {
            collect_goto_targets(n.then, acc);
            collect_goto_targets(n.other, acc);
        } else if (n.k == S::While) {
            collect_goto_targets(n.body, acc);
        }
    }
}

}  // namespace rv::dec
