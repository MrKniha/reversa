// Tiny expression/statement IR for the pseudocode generator.
//
// Registers become variables named by register *family* (rax/eax/al -> "r:rax");
// stack slots become "s:<offset>" variables; outgoing 32-bit stack arguments
// become "o:<offset>". The renderer maps these keys to readable names.
#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "analysis/analysis.hpp"

namespace rv::ir {

enum class K { Var, Const, Global, AddrOf, Bin, Un, Load, Call, Ternary };

struct Expr;
using E = std::shared_ptr<const Expr>;

// Immutable expression node. Field use per kind:
//   Var: key, size | Const: value, size | Global: value(addr), size | AddrOf: value(addr)
//   Bin: op, a, b | Un: op, a | Load: a(addr), size | Call: a(target), args | Ternary: c, a, b
struct Expr {
    K k;
    std::string key;  // Var key or operator
    int64_t value = 0;
    int size = 8;
    E a, b, c;
    std::vector<E> args;
};

bool eq(const E& x, const E& y);  // structural equality (like the frozen dataclasses)

E Var(const std::string& key, int size = 8);
E Const(int64_t v, int size = 8);
E Global(int64_t addr, int size = 8);
E AddrOf(int64_t addr);
E Bin(const std::string& op, E a, E b);
E Un(const std::string& op, E a);
E Load(E addr, int size = 8);
E Call(E target, std::vector<E> args = {});
E Ternary(E cond, E a, E b);

inline bool is(const E& e, K k) { return e && e->k == k; }
inline bool is_const(const E& e, int64_t v) { return is(e, K::Const) && e->value == v; }

E add(E a, E b);
E binop(const std::string& op, E a, E b);
E negate(E c);
void walk(const E& e, std::vector<const Expr*>& out);
E substitute(const E& e, const std::string& key, const E& rep);
bool is_cmp_op(const std::string& op);  // key of NEG

// Statements and structured nodes share one type (the Python version mixes classes in lists).
enum class S { Assign, ExprStmt, Asm, Return, Branch, Goto, If, While, Jump, Label };

struct Stmt {
    S k = S::Asm;
    Addr addr = 0;
    E dst, src;     // Assign
    E expr;         // ExprStmt expr | Return value | Goto expr | Branch/If/While cond
    std::vector<std::string> clobbers;
    std::string text;              // Asm text | Jump kind (break | continue | goto)
    std::optional<Addr> target;    // Goto / Jump target, Label addr
    Addr t = 0, f = 0;             // Branch true / false
    std::vector<Stmt> then, other, body;  // If / While
    bool do_while = false;
};

Stmt Assign(E dst, E src, Addr addr, std::vector<std::string> clobbers = {});
Stmt ExprStmt(E expr, Addr addr, std::vector<std::string> clobbers = {});
Stmt Asm(const std::string& text, Addr addr);
Stmt Return(E value, Addr addr);
Stmt Branch(E cond, Addr t, Addr f, Addr addr);
Stmt Goto(std::optional<Addr> target, Addr addr, E expr = nullptr);
Stmt Label(Addr addr);
Stmt Jump(const std::string& kind, std::optional<Addr> target = std::nullopt, Addr addr = 0);

struct LiftedBlock {
    Addr start = 0;
    std::vector<Stmt> stmts;
    std::optional<Stmt> term;
    std::vector<Succ> succs;
};

// Blocks in CFG order (Python dict insertion order matters for some passes).
struct Blocks {
    std::vector<Addr> order;
    std::map<Addr, LiftedBlock> map;
    bool has(Addr a) const { return map.count(a) != 0; }
    LiftedBlock& operator[](Addr a) { return map.at(a); }
    const LiftedBlock& operator[](Addr a) const { return map.at(a); }
};

}  // namespace rv::ir
