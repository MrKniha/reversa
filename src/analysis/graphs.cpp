// Control-flow graph (per function) and call graph (whole program).
#include <algorithm>

#include "analysis/analysis.hpp"

namespace rv {

// Iterative set-based dominators (Cooper/Harvey/Kennedy style; fine for function-sized graphs).
std::map<Addr, std::set<Addr>> dominators(const std::vector<Addr>& nodes, Addr entry,
                                          const std::map<Addr, std::vector<Addr>>& preds) {
    std::set<Addr> all(nodes.begin(), nodes.end());
    std::map<Addr, std::set<Addr>> dom;
    for (auto n : nodes) dom[n] = all;
    dom[entry] = {entry};
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto n : nodes) {
            if (n == entry) continue;
            std::set<Addr> nw;
            bool first = true;
            auto pit = preds.find(n);
            if (pit != preds.end()) {
                for (auto p : pit->second) {
                    auto d = dom.find(p);
                    if (d == dom.end()) continue;
                    if (first) { nw = d->second; first = false; }
                    else {
                        std::set<Addr> r;
                        std::set_intersection(nw.begin(), nw.end(), d->second.begin(), d->second.end(), std::inserter(r, r.end()));
                        nw.swap(r);
                    }
                }
            }
            nw.insert(n);
            if (nw != dom[n]) { dom[n] = std::move(nw); changed = true; }
        }
    }
    return dom;
}

std::vector<Addr> reachable(const Function& fn) {
    std::set<Addr> seen;
    std::vector<Addr> order, stack{fn.addr};
    while (!stack.empty()) {
        Addr n = stack.back();
        stack.pop_back();
        if (seen.count(n) || !fn.blocks.count(n)) continue;
        seen.insert(n);
        order.push_back(n);
        auto& succs = fn.blocks.at(n).succs;
        for (auto it = succs.rbegin(); it != succs.rend(); ++it) stack.push_back(it->to);
    }
    return order;
}

Cfg build_cfg(const Function& fn) {
    Cfg g;
    g.nodes = reachable(fn);
    if (g.nodes.empty())
        for (auto& [a, _] : fn.blocks) g.nodes.push_back(a);
    std::set<Addr> nodeset(g.nodes.begin(), g.nodes.end());
    for (auto n : g.nodes) g.preds[n];
    for (auto n : g.nodes)
        for (auto& s : fn.blocks.at(n).succs)
            if (nodeset.count(s.to)) {
                g.preds[s.to].push_back(n);
                g.edges.push_back({n, s.to, s.kind, false});
            }
    g.entry = nodeset.count(fn.addr) ? fn.addr : (g.nodes.empty() ? fn.addr : g.nodes[0]);
    if (!g.nodes.empty()) g.dom = dominators(g.nodes, g.entry, g.preds);
    std::map<Addr, std::set<Addr>> loops;
    for (auto& e : g.edges) {
        auto d = g.dom.find(e.from);
        if (d != g.dom.end() && d->second.count(e.to)) {  // edge to a dominator = back edge
            e.back = true;
            std::set<Addr> body{e.to, e.from};
            std::vector<Addr> stack{e.from};
            while (!stack.empty()) {
                Addr m = stack.back();
                stack.pop_back();
                for (auto p : g.preds[m])
                    if (body.insert(p).second) stack.push_back(p);
            }
            loops[e.to].insert(body.begin(), body.end());
        }
    }
    for (auto& [h, b] : loops) g.loops[h] = std::vector<Addr>(b.begin(), b.end());
    return g;
}

CallGraph call_graph(const std::map<Addr, Function>& functions, std::optional<Addr> root, int depth) {
    CallGraph g;
    std::vector<std::pair<Addr, Addr>> edges;
    for (auto& [a, f] : functions) {
        std::set<Addr> all(f.callees);
        all.insert(f.tail_calls.begin(), f.tail_calls.end());
        for (auto c : all)
            if (functions.count(c)) edges.push_back({f.addr, c});
    }
    if (!root || depth <= 0) {
        for (auto& [a, _] : functions) g.nodes.push_back(a);
        g.edges = edges;
        return g;
    }
    std::set<Addr> keep{*root}, frontier{*root};
    for (int i = 0; i < depth; i++) {
        std::set<Addr> nxt;
        for (auto [a, b] : edges) {
            if (frontier.count(a) && !keep.count(b)) nxt.insert(b);
            if (frontier.count(b) && !keep.count(a)) nxt.insert(a);
        }
        keep.insert(nxt.begin(), nxt.end());
        frontier = nxt;
    }
    g.nodes.assign(keep.begin(), keep.end());
    for (auto [a, b] : edges)
        if (keep.count(a) && keep.count(b)) g.edges.push_back({a, b});
    return g;
}

}  // namespace rv
