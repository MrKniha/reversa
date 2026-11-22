// Internal decompiler interfaces shared by lifters, passes, structuring and rendering.
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>

#include "decompiler/decompiler.hpp"
#include "decompiler/ir.hpp"

namespace rv::dec {

using namespace rv::ir;
using KeySet = std::set<std::string>;

struct Context {
    Context(const Analysis& analysis, const Function& function);
    const Analysis& analysis;
    const Binary& binary;
    const Function& function;
    std::string abi;  // sysv | win64 | cdecl | aapcs64
    bool uses_frame = false;
    std::vector<const Section*> data_secs;

    E cnst(int64_t v, int size) const;
    std::optional<Addr> call_target_of(Addr insn_addr) const;
    std::optional<std::string> callee_name(std::optional<Addr> addr) const;
};

// lifters
struct Flags {
    std::string kind;  // cmp | cmn | test | res
    E a, b;
};
LiftedBlock lift_x86(const Context& ctx, const Block& block, const std::vector<Insn>& insns);
LiftedBlock lift_arm64(const Context& ctx, const Block& block, const std::vector<Insn>& insns);
const std::vector<std::string>& x86_abi_args(const std::string& abi);
E cond_from_flags(const std::optional<Flags>& flags, const std::string& cc);
std::optional<int> known_args(const std::string& name);

// passes
KeySet expr_vars(const E& e);
bool reads_memory(const E& e);
bool has_call(const E& e);
KeySet uses(const Stmt& st);
KeySet defs(const Stmt& st);
bool writes_memory(const Stmt& st);
std::vector<Stmt> all_stmts(const LiftedBlock& lb);
void liveness(const Blocks& blocks, std::map<Addr, KeySet>& live_in, std::map<Addr, KeySet>& live_out);
Stmt replace_in(const Stmt& st, const std::string& key, const E& rep);
void simplify(Blocks& blocks, int rounds = 12);
bool returns_value(const Blocks& blocks, Addr entry, const std::string& ret_key);
std::vector<E> stmt_exprs(const Stmt& st);

// structuring
std::vector<Stmt> structure(const Blocks& blocks, Addr entry, const std::map<Addr, std::vector<Addr>>& loops);
std::vector<Addr> py_set_order_ints(const std::vector<Addr>& items);
void collect_goto_targets(const std::vector<Stmt>& nodes, std::set<Addr>& acc);

}  // namespace rv::dec
