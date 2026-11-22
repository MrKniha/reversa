#include "decompiler/ir.hpp"

#include <map>

namespace rv::ir {

namespace {
const std::map<std::string, std::string> NEG = {
    {"==", "!="}, {"!=", "=="}, {"<", ">="}, {">=", "<"}, {">", "<="}, {"<=", ">"},
    {"u<", "u>="}, {"u>=", "u<"}, {"u>", "u<="}, {"u<=", "u>"}};

E make(Expr e) { return std::make_shared<const Expr>(std::move(e)); }
int64_t wrap_add(int64_t a, int64_t b) { return static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b)); }
int64_t wrap_neg(int64_t a) { return static_cast<int64_t>(0 - static_cast<uint64_t>(a)); }
}  // namespace

bool is_cmp_op(const std::string& op) { return NEG.count(op) != 0; }

bool eq(const E& x, const E& y) {
    if (x == y) return true;
    if (!x || !y || x->k != y->k) return false;
    switch (x->k) {
        case K::Var: return x->key == y->key && x->size == y->size;
        case K::Const:
        case K::Global: return x->value == y->value && x->size == y->size;
        case K::AddrOf: return x->value == y->value;
        case K::Bin: return x->key == y->key && eq(x->a, y->a) && eq(x->b, y->b);
        case K::Un: return x->key == y->key && eq(x->a, y->a);
        case K::Load: return x->size == y->size && eq(x->a, y->a);
        case K::Call:
            if (!eq(x->a, y->a) || x->args.size() != y->args.size()) return false;
            for (size_t i = 0; i < x->args.size(); i++)
                if (!eq(x->args[i], y->args[i])) return false;
            return true;
        case K::Ternary: return eq(x->c, y->c) && eq(x->a, y->a) && eq(x->b, y->b);
    }
    return false;
}

E Var(const std::string& key, int size) { Expr e{K::Var}; e.key = key; e.size = size; return make(std::move(e)); }
E Const(int64_t v, int size) { Expr e{K::Const}; e.value = v; e.size = size; return make(std::move(e)); }
E Global(int64_t addr, int size) { Expr e{K::Global}; e.value = addr; e.size = size; return make(std::move(e)); }
E AddrOf(int64_t addr) { Expr e{K::AddrOf}; e.value = addr; return make(std::move(e)); }
E Bin(const std::string& op, E a, E b) { Expr e{K::Bin}; e.key = op; e.a = std::move(a); e.b = std::move(b); return make(std::move(e)); }
E Un(const std::string& op, E a) { Expr e{K::Un}; e.key = op; e.a = std::move(a); return make(std::move(e)); }
E Load(E addr, int size) { Expr e{K::Load}; e.a = std::move(addr); e.size = size; return make(std::move(e)); }
E Call(E target, std::vector<E> args) { Expr e{K::Call}; e.a = std::move(target); e.args = std::move(args); return make(std::move(e)); }
E Ternary(E cond, E a, E b) { Expr e{K::Ternary}; e.c = std::move(cond); e.a = std::move(a); e.b = std::move(b); return make(std::move(e)); }

E add(E a, E b) {
    if (is_const(b, 0)) return a;
    if (is_const(a, 0)) return b;
    if (is(a, K::Bin) && a->key == "+" && is(a->b, K::Const) && is(b, K::Const))
        return add(a->a, Const(wrap_add(a->b->value, b->value)));
    if (is(a, K::Const) && is(b, K::Const)) return Const(wrap_add(a->value, b->value));
    if (is(b, K::Const) && b->value < 0) return Bin("-", a, Const(wrap_neg(b->value)));
    return Bin("+", a, b);
}

E binop(const std::string& op, E a, E b) {
    if (op == "+") return add(a, b);
    if (op == "-" && is(b, K::Const)) return add(a, Const(wrap_neg(b->value)));
    if ((op == "<<" || op == ">>" || op == "|" || op == "^") && is_const(b, 0)) return a;
    if (op == "*" && is_const(b, 1)) return a;
    if (op == "&" && is(b, K::Const) &&
        (b->value == 1 || b->value == 0xFF || b->value == 0xFFFF || b->value == 0xFFFFFFFF) && is(a, K::Bin) &&
        is_cmp_op(a->key))
        return a;  // (x == y) & 0xff  ->  x == y
    if (op == "|" && is(a, K::Const) && is(b, K::Const)) return Const(a->value | b->value);
    if (op == "|" && is(a, K::Bin) && a->key == "|" && is(a->b, K::Const) && is(b, K::Const))
        return Bin("|", a->a, Const(a->b->value | b->value));
    return Bin(op, a, b);
}

E negate(E c) {
    if (is(c, K::Bin) && is_cmp_op(c->key)) return Bin(NEG.at(c->key), c->a, c->b);
    if (is(c, K::Un) && c->key == "!") return c->a;
    if (is(c, K::Bin) && c->key == "&&") return Bin("||", negate(c->a), negate(c->b));
    if (is(c, K::Bin) && c->key == "||") return Bin("&&", negate(c->a), negate(c->b));
    return Un("!", c);
}

void walk(const E& e, std::vector<const Expr*>& out) {
    if (!e) return;
    out.push_back(e.get());
    switch (e->k) {
        case K::Bin: walk(e->a, out); walk(e->b, out); break;
        case K::Un:
        case K::Load: walk(e->a, out); break;
        case K::Call:
            walk(e->a, out);
            for (auto& x : e->args) walk(x, out);
            break;
        case K::Ternary: walk(e->c, out); walk(e->a, out); walk(e->b, out); break;
        default: break;
    }
}

E substitute(const E& e, const std::string& key, const E& rep) {
    if (!e) return e;
    switch (e->k) {
        case K::Var: return e->key == key ? rep : e;
        case K::Bin: return binop(e->key, substitute(e->a, key, rep), substitute(e->b, key, rep));
        case K::Un: {
            auto inner = substitute(e->a, key, rep);
            return e->key == "!" ? negate(inner) : Un(e->key, inner);
        }
        case K::Load: return Load(substitute(e->a, key, rep), e->size);
        case K::Call: {
            std::vector<E> args;
            for (auto& x : e->args) args.push_back(substitute(x, key, rep));
            return Call(substitute(e->a, key, rep), std::move(args));
        }
        case K::Ternary: return Ternary(substitute(e->c, key, rep), substitute(e->a, key, rep), substitute(e->b, key, rep));
        default: return e;
    }
}

Stmt Assign(E dst, E src, Addr addr, std::vector<std::string> clobbers) {
    Stmt s; s.k = S::Assign; s.dst = std::move(dst); s.src = std::move(src); s.addr = addr; s.clobbers = std::move(clobbers);
    return s;
}
Stmt ExprStmt(E expr, Addr addr, std::vector<std::string> clobbers) {
    Stmt s; s.k = S::ExprStmt; s.expr = std::move(expr); s.addr = addr; s.clobbers = std::move(clobbers);
    return s;
}
Stmt Asm(const std::string& text, Addr addr) { Stmt s; s.k = S::Asm; s.text = text; s.addr = addr; return s; }
Stmt Return(E value, Addr addr) { Stmt s; s.k = S::Return; s.expr = std::move(value); s.addr = addr; return s; }
Stmt Branch(E cond, Addr t, Addr f, Addr addr) {
    Stmt s; s.k = S::Branch; s.expr = std::move(cond); s.t = t; s.f = f; s.addr = addr;
    return s;
}
Stmt Goto(std::optional<Addr> target, Addr addr, E expr) {
    Stmt s; s.k = S::Goto; s.target = target; s.addr = addr; s.expr = std::move(expr);
    return s;
}
Stmt Label(Addr addr) { Stmt s; s.k = S::Label; s.target = addr; s.addr = addr; return s; }
Stmt Jump(const std::string& kind, std::optional<Addr> target, Addr addr) {
    Stmt s; s.k = S::Jump; s.text = kind; s.target = target; s.addr = addr;
    return s;
}

}  // namespace rv::ir
