// Render structured IR as C-like pseudocode split into typed, clickable segments.
#include <algorithm>
#include <map>

#include "core/text.hpp"
#include "decompiler/internal.hpp"

namespace rv {

using namespace rv::ir;

const char* const PROVISIONAL_NOTE =
    "// Dočasný pseudokód (REVERSA MVP): typy sú odhadnuté z veľkosti prístupov, overte v listingu.";

std::string ctype(int size) {
    switch (size) {
        case 1: return "char";
        case 2: return "int16_t";
        case 4: return "int32_t";
        case 8: return "int64_t";
        case 16: return "int128_t";
        default: return "int64_t";
    }
}

namespace {

const std::map<std::string, double> PREC = {
    {"*", 3}, {"/", 3}, {"%", 3}, {"+", 4}, {"-", 4}, {"<<", 5}, {">>", 5}, {"<", 6}, {"<=", 6}, {">", 6},
    {">=", 6}, {"u<", 6}, {"u<=", 6}, {"u>", 6}, {"u>=", 6}, {"==", 7}, {"!=", 7}, {"&", 8}, {"^", 9},
    {"|", 10}, {"&&", 11}, {"||", 12}};

using Segs = std::vector<Seg>;

Seg seg(std::string text, const char* kind) { return Seg{std::move(text), kind, std::nullopt, ""}; }
Seg aseg(std::string text, const char* kind, Addr a) { return Seg{std::move(text), kind, a, ""}; }
Segs& operator+=(Segs& a, const Segs& b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
Segs cat(Segs a, const Segs& b) { return a += b; }

std::string num_text(int64_t v) {
    if (v > -256 && v < 256) return std::to_string(v);
    if (v >= 0) return "0x" + hex_digits(uint64_t(v));
    return "-0x" + hex_digits(uint64_t(0) - uint64_t(v));
}

std::string label(Addr a) { return "label_" + hex_digits(a); }

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

class Renderer {
public:
    Renderer(const RenderNames& names, std::function<std::string(const std::string&)> var_name, bool void_fn)
        : names(names), var_name(std::move(var_name)), void_fn(void_fn) {}

    const RenderNames& names;
    std::function<std::string(const std::string&)> var_name;
    bool void_fn;
    std::vector<Line> lines;
    std::set<Addr> labels;

    bool is_symbol(int64_t v) const { return names.is_symbol && names.is_symbol(v); }

    Segs expr(const E& e, double parent = 99) {
        switch (e->k) {
            case K::Var:
                if (e->key.rfind("r:__", 0) == 0) return {seg(e->key.substr(4), "func")};
                return {Seg{var_name(e->key), "var", std::nullopt, e->key}};
            case K::Const: {
                if (is_symbol(e->value)) return expr(AddrOf(e->value), parent);
                return {seg(num_text(e->value), "num")};
            }
            case K::AddrOf: {
                Addr a = Addr(e->value);
                auto s = names.string_at ? names.string_at(a) : std::nullopt;
                if (s) {
                    std::string lit = replace_all(*s, "\\", "\\\\");
                    lit = replace_all(lit, "\"", "\\\"");
                    lit = replace_all(lit, "\n", "\\n");
                    lit = replace_all(lit, "\t", "\\t");
                    lit = replace_all(lit, "\r", "\\r");
                    return {aseg("\"" + lit + "\"", "str", a)};
                }
                return {aseg(names.name_of(a), "func", a)};
            }
            case K::Global: return {aseg(names.name_of(Addr(e->value)), "glob", Addr(e->value))};
            case K::Load: return load(e);
            case K::Un: {
                if (e->key == "?") return {seg("cond_unknown", "var")};
                return cat({seg(e->key, "op")}, expr(e->a, 2));
            }
            case K::Bin: {
                auto pi = PREC.find(e->key);
                double p = pi == PREC.end() ? 10 : pi->second;
                std::string op = e->key;
                static const std::set<std::string> assoc = {"+", "*", "&", "|", "^", "&&", "||"};
                Segs left = expr(e->a, p);
                Segs right = expr(e->b, assoc.count(op) ? p - 1 : p - 0.5);
                if (op[0] == 'u') {
                    op = op.substr(1);
                    left.insert(left.begin(), seg("(unsigned)", "type"));
                }
                Segs out = left;
                out.push_back(seg(" " + op + " ", "op"));
                out += right;
                if (p > parent) {
                    out.insert(out.begin(), seg("(", "op"));
                    out.push_back(seg(")", "op"));
                }
                return out;
            }
            case K::Call: {
                Segs out;
                if (is(e->a, K::Var)) {
                    out = {seg("(*", "op")};
                    out += expr(e->a);
                    out.push_back(seg(")", "op"));
                } else {
                    out = expr(e->a, 0);
                }
                out.push_back(seg("(", "op"));
                for (size_t i = 0; i < e->args.size(); i++) {
                    if (i) out.push_back(seg(", ", "op"));
                    out += expr(e->args[i]);
                }
                out.push_back(seg(")", "op"));
                return out;
            }
            case K::Ternary: {
                Segs out = expr(e->c, 12);
                out.push_back(seg(" ? ", "op"));
                out += expr(e->a, 12);
                out.push_back(seg(" : ", "op"));
                out += expr(e->b, 12);
                if (parent < 13) {
                    out.insert(out.begin(), seg("(", "op"));
                    out.push_back(seg(")", "op"));
                }
                return out;
            }
        }
        return {seg("?", "plain")};
    }

    Segs load(const E& e) {
        const E& ad = e->a;
        if (is(ad, K::Const) || is(ad, K::AddrOf)) return {aseg(names.name_of(Addr(ad->value)), "glob", Addr(ad->value))};
        if (is(ad, K::Un) && ad->key == "&" && is(ad->a, K::Var)) return expr(ad->a);
        if (is(ad, K::Bin) && ad->key == "+") {
            E base = ad->a, idx = ad->b;
            bool idx_c = is(idx, K::AddrOf) || is(idx, K::Const), base_c = is(base, K::AddrOf) || is(base, K::Const);
            if (idx_c && !base_c) std::swap(base, idx);
            std::optional<Addr> bval;
            if (is(base, K::AddrOf))
                bval = Addr(base->value);
            else if (is(base, K::Const) && is_symbol(base->value))
                bval = Addr(base->value);
            if (bval) {
                if (is(idx, K::Bin) && idx->key == "*" && is(idx->b, K::Const) && idx->b->value == e->size) {
                    Segs out = {aseg(names.name_of(*bval), "glob", *bval), seg("[", "op")};
                    out += expr(idx->a);
                    out.push_back(seg("]", "op"));
                    return out;
                }
                if (e->size == 1) {
                    Segs out = {aseg(names.name_of(*bval), "glob", *bval), seg("[", "op")};
                    out += expr(idx);
                    out.push_back(seg("]", "op"));
                    return out;
                }
            }
        }
        Segs out = {seg("*(" + ctype(e->size) + " *)", "type"), seg("(", "op")};
        out += expr(ad);
        out.push_back(seg(")", "op"));
        return out;
    }

    void line(int indent, Segs segs, std::optional<Addr> addr = std::nullopt) {
        lines.push_back(Line{indent, std::move(segs), addr});
    }

    static bool has_real(const std::vector<Stmt>& v) {
        for (auto& x : v)
            if (x.k != S::Label) return true;
        return false;
    }

    void stmt(const Stmt& s, int ind) {
        switch (s.k) {
            case S::Label:
                if (labels.count(s.addr)) line(std::max(0, ind - 1), {aseg(label(s.addr) + ":", "label", s.addr)}, s.addr);
                return;
            case S::Assign: {
                Segs dst = expr(s.dst);
                const E& src = s.src;
                static const std::set<std::string> compound = {"+", "-", "*", "/", "&", "|", "^", "<<", ">>", "%"};
                if (is(src, K::Bin) && eq(src->a, s.dst) && compound.count(src->key)) {
                    if (is_const(src->b, 1) && (src->key == "+" || src->key == "-")) {
                        dst.push_back(seg(src->key + src->key + ";", "op"));
                        line(ind, dst, s.addr);
                        return;
                    }
                    dst.push_back(seg(" " + src->key + "= ", "op"));
                    dst += expr(src->b);
                    dst.push_back(seg(";", "op"));
                    line(ind, dst, s.addr);
                    return;
                }
                dst.push_back(seg(" = ", "op"));
                dst += expr(src);
                dst.push_back(seg(";", "op"));
                line(ind, dst, s.addr);
                return;
            }
            case S::ExprStmt: {
                Segs o = expr(s.expr);
                o.push_back(seg(";", "op"));
                line(ind, o, s.addr);
                return;
            }
            case S::Return: {
                if (!s.expr || (void_fn && is(s.expr, K::Var))) {
                    line(ind, {seg("return", "kw"), seg(";", "op")}, s.addr);
                } else if (void_fn && is(s.expr, K::Call)) {
                    Segs o = expr(s.expr);
                    o.push_back(seg(";", "op"));
                    line(ind, o, s.addr);
                    line(ind, {seg("return", "kw"), seg(";", "op")}, s.addr);
                } else {
                    Segs o = {seg("return ", "kw")};
                    o += expr(s.expr);
                    o.push_back(seg(";", "op"));
                    line(ind, o, s.addr);
                }
                return;
            }
            case S::Asm: {
                std::string t = s.text;
                for (auto& c : t)
                    if (c == '"') c = '\'';
                line(ind, {seg("__asm(\"" + t + "\");", "asm")}, s.addr);
                return;
            }
            case S::Goto:
                if (!s.target) {
                    Segs o = {seg("goto ", "kw"), seg("*", "op")};
                    if (s.expr) o += expr(s.expr);
                    o.push_back(seg(";", "op"));
                    o.push_back(seg("  // nepriamy skok (napr. switch) – zatiaľ nerozbalený", "comment"));
                    line(ind, o, s.addr);
                } else {
                    labels.insert(*s.target);
                    line(ind, {seg("goto ", "kw"), aseg(label(*s.target), "label", *s.target), seg(";", "op")}, s.addr);
                }
                return;
            case S::Jump:
                if (s.text == "goto")
                    line(ind, {seg("goto ", "kw"), aseg(label(*s.target), "label", *s.target), seg(";", "op")}, s.addr);
                else
                    line(ind, {seg(s.text, "kw"), seg(";", "op")}, s.addr);
                return;
            case S::If: {
                Segs head = {seg("if", "kw"), seg(" (", "op")};
                head += expr(s.expr);
                head.push_back(seg(") {", "op"));
                line(ind, head, s.addr);
                body(s.then, ind + 1);
                const std::vector<Stmt>* other = &s.other;
                while (true) {
                    std::vector<const Stmt*> real;
                    for (auto& x : *other)
                        if (x.k != S::Label || labels.count(x.addr)) real.push_back(&x);
                    if (real.size() == 1 && real[0]->k == S::If) {
                        const Stmt& nested = *real[0];
                        Segs o = {seg("} ", "op"), seg("else if", "kw"), seg(" (", "op")};
                        o += expr(nested.expr);
                        o.push_back(seg(") {", "op"));
                        line(ind, o, nested.addr);
                        body(nested.then, ind + 1);
                        other = &nested.other;
                        continue;
                    }
                    break;
                }
                if (has_real(*other)) {
                    line(ind, {seg("} ", "op"), seg("else", "kw"), seg(" {", "op")});
                    body(*other, ind + 1);
                }
                line(ind, {seg("}", "op")});
                return;
            }
            case S::While:
                if (s.do_while) {
                    line(ind, {seg("do", "kw"), seg(" {", "op")}, s.addr);
                    body(s.body, ind + 1);
                    Segs o = {seg("} ", "op"), seg("while", "kw"), seg(" (", "op")};
                    o += expr(s.expr);
                    o.push_back(seg(");", "op"));
                    line(ind, o);
                } else {
                    Segs o = {seg("while", "kw"), seg(" (", "op")};
                    if (s.expr)
                        o += expr(s.expr);
                    else
                        o.push_back(seg("true", "kw"));
                    o.push_back(seg(") {", "op"));
                    line(ind, o, s.addr);
                    body(s.body, ind + 1);
                    line(ind, {seg("}", "op")});
                }
                return;
            case S::Branch: {  // only in the linear fallback
                Segs o = {seg("if", "kw"), seg(" (", "op")};
                o += expr(s.expr);
                o.push_back(seg(") ", "op"));
                o.push_back(seg("goto ", "kw"));
                o.push_back(aseg(label(s.t), "label", s.t));
                o.push_back(seg(";", "op"));
                line(ind, o, s.addr);
                labels.insert(s.t);
                labels.insert(s.f);
                line(ind, {seg("goto ", "kw"), aseg(label(s.f), "label", s.f), seg(";", "op")}, s.addr);
                return;
            }
        }
    }

    void body(const std::vector<Stmt>& nodes, int ind) {
        for (auto& n : nodes) stmt(n, ind);
    }
};

}  // namespace

std::vector<Line> render(const Decompiled& d, const std::string& func_name, const RenderNames& names) {
    auto var_name = [&](const std::string& key) -> std::string {
        if (names.var_override) {
            auto o = names.var_override(key);
            if (o && !o->empty()) return *o;
        }
        auto it = d.default_names.find(key);
        if (it != d.default_names.end()) return it->second;
        std::string k = key;
        for (auto& c : k)
            if (c == ':') c = '_';
        return k;
    };
    Renderer r(names, var_name, !d.returns);
    r.labels = d.labels;
    r.line(0, {seg(PROVISIONAL_NOTE, "comment")});
    if (!d.error.empty())
        r.line(0, {seg("// Štruktúrovanie zlyhalo (" + d.error + ") – zobrazujem plochú verziu s goto.", "comment")});
    auto size_of = [&](const std::string& k) {
        auto it = d.sizes.find(k);
        return it == d.sizes.end() ? 8 : it->second;
    };
    Segs sig = {seg(std::string(d.returns ? ctype(d.ret_size) : "void") + " ", "type"), aseg(func_name, "func", d.func),
                seg("(", "op")};
    for (size_t i = 0; i < d.params.size(); i++) {
        if (i) sig.push_back(seg(", ", "op"));
        sig.push_back(seg(ctype(size_of(d.params[i])) + " ", "type"));
        sig.push_back(Seg{var_name(d.params[i]), "var", std::nullopt, d.params[i]});
    }
    sig.push_back(seg(")", "op"));
    r.line(0, sig, d.func);
    r.line(0, {seg("{", "op")});
    for (auto& k : d.locals) {
        if (k.rfind("r:rsp", 0) == 0 || k.rfind("r:sp", 0) == 0 || k.rfind("r:rbp", 0) == 0 || k.rfind("r:x29", 0) == 0) continue;
        r.line(1, {seg(ctype(size_of(k)) + " ", "type"), Seg{var_name(k), "var", std::nullopt, k}, seg(";", "op")});
    }
    if (!d.locals.empty()) r.line(1, {});
    r.body(d.tree, 1);
    r.line(0, {seg("}", "op")});
    return r.lines;
}

std::string to_text(const std::vector<Line>& lines) {
    std::string out;
    for (size_t i = 0; i < lines.size(); i++) {
        if (i) out += "\n";
        for (int k = 0; k < lines[i].indent; k++) out += "    ";
        for (auto& s : lines[i].segs) out += s.text;
    }
    return out;
}

}  // namespace rv
