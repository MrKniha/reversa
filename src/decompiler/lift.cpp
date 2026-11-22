// Architecture lifters: decoded instructions -> IR statements.
//
// x86 / x86-64 and AArch64, integer subset that compilers emit for ordinary C code at -O0..-O2.
// Anything else (SSE, string ops, ...) is kept verbatim as an __asm statement so no information
// is silently lost.
#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>

#include "core/text.hpp"
#include "decompiler/internal.hpp"

namespace rv::dec {

namespace {

// Python-style exception names keep the "lift error" comments identical across implementations.
struct LiftError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

const Operand& op_at(const Insn& ci, size_t i) {
    if (i >= ci.ops.size()) throw LiftError("IndexError");
    return ci.ops[i];
}

bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }
bool ends(const std::string& s, const std::string& p) { return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0; }

std::string shex(int64_t v) {  // Python f"{v:x}"
    return v < 0 ? "-" + hex_digits(static_cast<uint64_t>(0) - static_cast<uint64_t>(v)) : hex_digits(static_cast<uint64_t>(v));
}

std::string asm_text(const Insn& ci) { return trim(ci.mnemonic + " " + ci.op_str); }

const std::map<std::string, int> KNOWN_ARGS = {
    {"puts", 1}, {"strlen", 1}, {"malloc", 1}, {"free", 1}, {"exit", 1}, {"_exit", 1}, {"atoi", 1}, {"strdup", 1},
    {"abort", 0}, {"strcmp", 2}, {"strcpy", 2}, {"strcat", 2}, {"strchr", 2}, {"strstr", 2}, {"fopen", 2},
    {"fputs", 2}, {"calloc", 2}, {"realloc", 2}, {"signal", 2}, {"memcpy", 3}, {"memset", 3}, {"memcmp", 3},
    {"strncmp", 3}, {"strncpy", 3}, {"read", 3}, {"write", 3}, {"fgets", 3}, {"fread", 4}, {"fwrite", 4},
    {"getchar", 0}, {"rand", 0}, {"srand", 1}, {"time", 1}, {"__main", 0}, {"__stack_chk_fail", 0}, {"putchar", 1},
    {"fclose", 1}, {"close", 1}, {"open", 2}, {"sleep", 1}};

const std::map<std::string, std::string> CC_OPS = {
    {"e", "=="}, {"z", "=="}, {"eq", "=="}, {"ne", "!="}, {"nz", "!="},
    {"l", "<"}, {"nge", "<"}, {"lt", "<"}, {"le", "<="}, {"ng", "<="},
    {"g", ">"}, {"nle", ">"}, {"gt", ">"}, {"ge", ">="}, {"nl", ">="},
    {"b", "u<"}, {"nae", "u<"}, {"c", "u<"}, {"lo", "u<"}, {"cc", "u<"},
    {"be", "u<="}, {"na", "u<="}, {"ls", "u<="},
    {"a", "u>"}, {"nbe", "u>"}, {"hi", "u>"},
    {"ae", "u>="}, {"nb", "u>="}, {"nc", "u>="}, {"hs", "u>="}, {"cs", "u>="},
    {"s", "<s0"}, {"mi", "<s0"}, {"ns", ">=s0"}, {"pl", ">=s0"}};

// Scan backwards from a call for argument set-up, stopping at the previous call.
void scan_args(const std::vector<Stmt>& out, KeySet& assigned) {
    KeySet consumed;
    for (auto it = out.rbegin(); it != out.rend(); ++it) {
        const Stmt& st = *it;
        if (st.k == S::ExprStmt || (st.k == S::Assign && is(st.src, K::Call))) break;
        if (st.k == S::Assign && is(st.dst, K::Var) && !consumed.count(st.dst->key)) assigned.insert(st.dst->key.substr(2));
        auto u = uses(st);
        consumed.insert(u.begin(), u.end());
    }
}

// ------------------------------------------------------------------ x86
const std::map<std::string, std::string>& families() {
    static const std::map<std::string, std::string> f = [] {
        std::map<std::string, std::string> m;
        const std::vector<std::pair<std::string, std::string>> base = {
            {"rax", "rax eax ax al ah"}, {"rbx", "rbx ebx bx bl bh"}, {"rcx", "rcx ecx cx cl ch"},
            {"rdx", "rdx edx dx dl dh"}, {"rsi", "rsi esi si sil"}, {"rdi", "rdi edi di dil"},
            {"rbp", "rbp ebp bp bpl"}, {"rsp", "rsp esp sp spl"}};
        for (auto& [fam, names] : base)
            for (auto& n : split(names, ' ')) m[n] = fam;
        for (int i = 8; i < 16; i++)
            for (const char* suf : {"", "d", "w", "b"}) m["r" + std::to_string(i) + suf] = "r" + std::to_string(i);
        return m;
    }();
    return f;
}

std::string family(const std::string& name) {
    auto it = families().find(name);
    return it == families().end() ? name : it->second;
}

const std::map<std::string, std::vector<std::string>> ABI_ARGS = {
    {"sysv", {"rdi", "rsi", "rdx", "rcx", "r8", "r9"}}, {"win64", {"rcx", "rdx", "r8", "r9"}}, {"cdecl", {}}};
const std::map<std::string, std::vector<std::string>> CLOBBER = {
    {"sysv", {"rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11"}},
    {"win64", {"rax", "rcx", "rdx", "r8", "r9", "r10", "r11"}},
    {"cdecl", {"rax", "rcx", "rdx"}}};
const std::set<std::string> CALLEE_SAVED = {"rbx", "rbp", "rsi", "rdi", "r12", "r13", "r14", "r15"};
const std::map<std::string, std::string> X86_ARITH = {
    {"add", "+"}, {"sub", "-"}, {"and", "&"}, {"or", "|"}, {"xor", "^"}, {"shl", "<<"},
    {"sal", "<<"}, {"shr", ">>"}, {"sar", ">>"}, {"imul", "*"}};
const std::set<std::string> X86_IGNORE = {"nop", "endbr64", "endbr32", "leave", "cdqe", "cdq", "cqo",
                                          "cwde", "cwd", "fwait", "pause", "int3"};

class X86Lifter {
public:
    explicit X86Lifter(const Context& ctx)
        : ctx(ctx), abi(ctx.abi), args(ABI_ARGS.at(ctx.abi.empty() ? "cdecl" : ctx.abi)), bits(ctx.binary.bits) {}

    LiftedBlock lift(const Block& block, const std::vector<Insn>& insns) {
        LiftedBlock lb;
        lb.start = block.start;
        lb.succs = block.succs;
        std::optional<Flags> flags;
        auto& out = lb.stmts;
        for (size_t k = 0; k < insns.size(); k++) {
            const Insn& ci = insns[k];
            std::string mn = ci.mnemonic;
            const auto& ops = ci.ops;
            Addr a = ci.addr;
            if (starts(mn, "bnd ") || starts(mn, "notrack ")) mn = split(mn, ' ').back();
            if (X86_IGNORE.count(mn)) continue;
            const Insn* nxt = k + 1 < insns.size() ? &insns[k + 1] : nullptr;
            try {
                if (mn == "push" || mn == "pop") {
                    const auto& o = op_at(ci, 0);
                    if (o.type == Operand::Reg && families().count(o.reg) && CALLEE_SAVED.count(family(o.reg))) continue;
                    if (mn == "push" && bits == 32) continue;  // argument pushes are rare with modern compilers at -O0; ignored
                    out.push_back(Asm(ci.mnemonic + " " + ci.op_str, a));
                } else if (mn == "mov" || mn == "movabs" || mn == "movzx" || mn == "movsx" || mn == "movsxd") {
                    const auto& dst = op_at(ci, 0);
                    const auto& src = op_at(ci, 1);
                    if (dst.type == Operand::Reg && (dst.reg == "rbp" || dst.reg == "ebp") && src.type == Operand::Reg)
                        continue;  // mov rbp, rsp (frame setup)
                    if (abi == "sysv" && dst.type == Operand::Reg && (dst.reg == "eax" || dst.reg == "al" || dst.reg == "rax") &&
                        src.type == Operand::Imm && nxt && nxt->mnemonic == "call")
                        continue;  // "mov eax, N" before a variadic call = number of vector args
                    out.push_back(Assign(operand(ci, dst), operand(ci, src), a));
                } else if (mn == "lea") {
                    out.push_back(Assign(operand(ci, op_at(ci, 0)), lea(ci, op_at(ci, 1)), a));
                } else if (mn == "xor" && ops.size() == 2 && ops[0].type == Operand::Reg && ops[1].type == Operand::Reg &&
                           ops[0].reg == ops[1].reg) {
                    out.push_back(Assign(operand(ci, ops[0]), Const(0), a));
                    flags = Flags{"res", Const(0), nullptr};
                } else if (X86_ARITH.count(mn) && ops.size() == 2) {
                    E dst = operand(ci, ops[0]);
                    if (is(dst, K::Var) && dst->key == "r:rsp") continue;  // stack adjustment
                    E src = operand(ci, ops[1]);
                    out.push_back(Assign(dst, binop(X86_ARITH.at(mn), dst, src), a));
                    flags = Flags{"res", dst, nullptr};  // compare the result with zero
                } else if (mn == "imul" && ops.size() == 3) {
                    out.push_back(Assign(operand(ci, ops[0]), binop("*", operand(ci, ops[1]), operand(ci, ops[2])), a));
                } else if (mn == "inc" || mn == "dec") {
                    E dst = operand(ci, op_at(ci, 0));
                    out.push_back(Assign(dst, binop(mn == "inc" ? "+" : "-", dst, Const(1)), a));
                    flags = Flags{"res", dst, nullptr};
                } else if (mn == "neg" || mn == "not") {
                    E dst = operand(ci, op_at(ci, 0));
                    out.push_back(Assign(dst, Un(mn == "neg" ? "-" : "~", dst), a));
                    flags = Flags{"res", dst, nullptr};
                } else if (mn == "cmp" || mn == "test") {
                    flags = Flags{mn, operand(ci, op_at(ci, 0)), operand(ci, op_at(ci, 1))};
                } else if (starts(mn, "set") && ops.size() == 1) {
                    out.push_back(Assign(operand(ci, ops[0]), cond_from_flags(flags, mn.substr(3)), a));
                } else if (starts(mn, "cmov") && ops.size() == 2) {
                    E dst = operand(ci, ops[0]);
                    out.push_back(Assign(dst, Ternary(cond_from_flags(flags, mn.substr(4)), operand(ci, ops[1]), dst), a));
                } else if ((mn == "imul" || mn == "mul") && ops.size() == 1) {
                    E src = operand(ci, ops[0]);
                    int sz = ops[0].size;
                    E rax = Var("r:rax", sz), rdx = Var("r:rdx", sz);
                    out.push_back(Assign(rdx, Bin(">>", Bin("*", rax, src), Const(sz * 8)), a));
                    out.push_back(Assign(rax, Bin("*", rax, src), a));
                } else if ((mn == "idiv" || mn == "div") && ops.size() == 1) {
                    E src = operand(ci, ops[0]);
                    E rax = Var("r:rax"), rdx = Var("r:rdx");
                    out.push_back(Assign(Var("r:tmp"), rax, a));
                    out.push_back(Assign(rax, Bin("/", Var("r:tmp"), src), a));
                    out.push_back(Assign(rdx, Bin("%", Var("r:tmp"), src), a));
                } else if (mn == "call") {
                    out.push_back(call(ci, out));
                } else if (mn == "ret" || mn == "retn") {
                    lb.term = Return(Var("r:rax"), a);
                } else if (mn == "jmp") {
                    std::optional<Addr> tgt;
                    if (!ops.empty() && ops[0].type == Operand::Imm) tgt = Addr(ops[0].imm);
                    auto slot_tgt = ctx.call_target_of(a);
                    if (tgt && !ctx.function.tail_calls.count(*tgt)) {
                        lb.term = Goto(*tgt, a);
                    } else if (tgt || slot_tgt) {
                        Addr t = tgt ? *tgt : *slot_tgt;
                        lb.term = Return(call_expr(AddrOf(int64_t(t)), t, out), a);
                    } else {
                        lb.term = Goto(std::nullopt, a, operand(ci, op_at(ci, 0)));
                    }
                } else if (starts(mn, "j") && !ops.empty() && ops[0].type == Operand::Imm) {
                    std::string cc = mn.substr(1);
                    E cond = (cc == "ecxz" || cc == "rcxz" || cc == "cxz") ? Bin("==", Var("r:rcx"), Const(0))
                                                                           : cond_from_flags(flags, cc);
                    lb.term = Branch(cond, Addr(ops[0].imm), a + ci.size, a);
                } else if (mn == "hlt") {
                    out.push_back(ExprStmt(Call(Var("r:__halt")), a));
                } else {
                    out.push_back(Asm(asm_text(ci), a));
                }
            } catch (const LiftError& e) {  // never let one odd instruction kill the function
                out.push_back(Asm(ci.mnemonic + " " + ci.op_str + "  /* lift error: " + e.what() + " */", a));
            }
        }
        return lb;
    }

private:
    const Context& ctx;
    std::string abi;
    const std::vector<std::string>& args;
    int bits;

    E reg(const Operand& op, int size) const { return Var("r:" + family(op.reg), size); }

    // Returns the address expression and its kind: "stack" (a variable), "abs" (a constant) or "".
    std::pair<E, std::string> mem_addr(const Insn& ci, const Operand& op) const {
        const auto& m = op.mem;
        if (!m.segment.empty()) return {Var("g:__" + m.segment + "_" + shex(m.disp), op.size), "stack"};
        const std::string& base = m.base;
        const std::string& index = m.index;
        if (base == "rip" || base == "eip") return {Const(int64_t(ci.addr + ci.size + uint64_t(m.disp))), "abs"};
        std::string fam = base.empty() ? "" : family(base);
        if ((fam == "rbp" || fam == "rsp") && index.empty() && (fam == "rsp" || ctx.uses_frame)) {
            std::string prefix = fam == "rbp" ? "s" : (bits == 32 ? "o" : "t");
            return {Var(prefix + ":" + std::to_string(m.disp), op.size), "stack"};
        }
        E e;
        if (!base.empty()) e = Var("r:" + fam);
        if (!index.empty()) {
            E idx = Var("r:" + family(index));
            if (m.scale != 1) idx = Bin("*", idx, Const(m.scale));
            e = e ? Bin("+", e, idx) : idx;
        }
        if (!e) {
            int64_t v = bits == 64 ? m.disp : int64_t(uint64_t(m.disp) & ((uint64_t(1) << bits) - 1));
            return {Const(v), "abs"};
        }
        E disp = m.disp >= 0x1000 ? ctx.cnst(m.disp, 8) : Const(m.disp);
        if (is(disp, K::AddrOf)) return {Bin("+", disp, e), ""};
        return {binop("+", e, disp), ""};
    }

    E operand(const Insn& ci, const Operand& op) const {
        if (op.type == Operand::Reg) return reg(op, op.size);
        if (op.type == Operand::Imm) return ctx.cnst(op.imm, op.size);
        auto [addr, kind] = mem_addr(ci, op);
        if (kind == "stack") return addr;
        if (kind == "abs") return Global(addr->value, op.size);
        return Load(addr, op.size);
    }

    E lea(const Insn& ci, const Operand& op) const {
        auto [addr, kind] = mem_addr(ci, op);
        if (kind == "stack") return Un("&", addr);
        if (kind == "abs") return ctx.cnst(addr->value, 8);
        return addr;
    }

    E call_expr(E target, std::optional<Addr> tgt_addr, const std::vector<Stmt>& out) const {
        std::optional<int> known;
        if (tgt_addr) {
            auto name = ctx.callee_name(tgt_addr);
            if (name) {
                std::string n = *name;
                n.erase(0, n.find_first_not_of('_') == std::string::npos ? n.size() : n.find_first_not_of('_'));
                known = known_args(n);
            }
        }
        std::vector<E> cargs;
        if (abi == "cdecl") {
            std::map<int64_t, E> pending;
            for (auto it = out.rbegin(); it != out.rend(); ++it) {
                const Stmt& st = *it;
                if (st.k == S::ExprStmt || (st.k == S::Assign && is(st.src, K::Call))) break;
                if (st.k == S::Assign && is(st.dst, K::Var) && starts(st.dst->key, "o:")) {
                    int64_t off = std::stoll(st.dst->key.substr(2));
                    pending.emplace(off, st.dst);
                }
            }
            int n = 0;
            while (pending.count(int64_t(n) * 4)) n++;
            if (known) n = (*known <= n || *known <= 6) ? *known : n;
            for (int i = 0; i < n; i++) {
                auto it = pending.find(int64_t(i) * 4);
                cargs.push_back(it != pending.end() ? it->second : Var("o:" + std::to_string(i * 4), 4));
            }
        } else {
            KeySet assigned;
            scan_args(out, assigned);
            size_t n = 0;
            while (n < args.size() && assigned.count(args[n])) n++;
            if (known) n = std::min(size_t(*known), args.size());
            for (size_t i = 0; i < n; i++) cargs.push_back(Var("r:" + args[i]));
        }
        return Call(std::move(target), std::move(cargs));
    }

    Stmt call(const Insn& ci, const std::vector<Stmt>& out) const {
        auto tgt_addr = ctx.call_target_of(ci.addr);
        E target;
        if (tgt_addr) {
            target = AddrOf(int64_t(*tgt_addr));
        } else if (op_at(ci, 0).type == Operand::Imm) {
            tgt_addr = Addr(ci.ops[0].imm);
            target = AddrOf(ci.ops[0].imm);
        } else {
            target = operand(ci, ci.ops[0]);
        }
        return Assign(Var("r:rax"), call_expr(target, tgt_addr, out), ci.addr, CLOBBER.at(abi));
    }
};

// ------------------------------------------------------------------ AArch64
const std::vector<std::string> A64_ARGS = {"x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7"};
const std::vector<std::string>& a64_clobber() {
    static const std::vector<std::string> c = [] {
        std::vector<std::string> v;
        for (int i = 0; i < 19; i++) v.push_back("x" + std::to_string(i));
        v.push_back("x30");
        return v;
    }();
    return c;
}
const std::map<std::string, std::string> A64_ARITH = {
    {"add", "+"}, {"sub", "-"}, {"mul", "*"}, {"sdiv", "/"}, {"udiv", "/"}, {"and", "&"}, {"orr", "|"},
    {"eor", "^"}, {"lsl", "<<"}, {"lsr", ">>"}, {"asr", ">>"}, {"adds", "+"}, {"subs", "-"}, {"ands", "&"}};
const std::set<std::string> A64_IGNORE = {"nop", "paciasp", "autiasp", "bti", "hint", "pacibsp",
                                          "autibsp", "dmb", "dsb", "isb", "prfm"};
const std::set<std::string>& a64_saved() {
    static const std::set<std::string> s = [] {
        std::set<std::string> v{"x29", "x30", "fp", "lr"};
        for (int i = 19; i < 31; i++) v.insert("x" + std::to_string(i));
        return v;
    }();
    return s;
}
const std::vector<std::pair<std::string, int>> A64_SIZE = {{"b", 1}, {"h", 2}, {"sb", 1}, {"sh", 2}, {"sw", 4}};

std::string a64_fam(const std::string& name) {
    if (name.empty()) throw LiftError("IndexError");
    if (name == "sp" || name == "wsp") return "sp";
    if (name == "fp") return "x29";
    if (name == "lr") return "x30";
    if ((name[0] == 'w' || name[0] == 'x') && name.size() > 1) {
        bool digits = true;
        for (size_t i = 1; i < name.size(); i++) digits = digits && std::isdigit(static_cast<unsigned char>(name[i]));
        if (digits) return "x" + name.substr(1);
    }
    return name;
}

std::string a64_cc(int cc) {
    static const char* names[] = {"?", "eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le"};
    return cc >= 1 && cc <= 14 ? names[cc] : "?";
}

class Arm64Lifter {
public:
    explicit Arm64Lifter(const Context& ctx) : ctx(ctx) {}

    LiftedBlock lift(const Block& block, const std::vector<Insn>& insns) {
        LiftedBlock lb;
        lb.start = block.start;
        lb.succs = block.succs;
        auto& out = lb.stmts;
        std::optional<Flags> flags;
        for (const Insn& ci : insns) {
            const std::string& mn = ci.mnemonic;
            const auto& ops = ci.ops;
            Addr a = ci.addr;
            if (A64_IGNORE.count(mn)) continue;
            try {
                if ((mn == "stp" || mn == "ldp") && ops.size() >= 3) {
                    std::string r1 = a64_fam(ops[0].reg), r2 = a64_fam(ops[1].reg);
                    std::string base = ops[2].type == Operand::Mem ? a64_fam(ops[2].mem.base) : "";
                    if ((base == "sp" || base == "x29") && (a64_saved().count(r1) || a64_saved().count(r2)))
                        continue;  // prologue / epilogue register save
                    int size = starts(ops[0].reg, "w") ? 4 : 8;
                    auto [addr, kind] = mem_addr(ops[2]);
                    for (int i = 0; i < 2; i++) {
                        E m;
                        if (kind == "stack") {
                            auto parts = split(addr->key, ':');
                            m = Var(parts[0] + ":" + std::to_string(std::stoll(parts[1]) + i * size), size);
                        } else {
                            m = Load(binop("+", addr, Const(i * size)), size);
                        }
                        E r = reg(ops[i]);
                        out.push_back(mn == "stp" ? Assign(m, r, a) : Assign(r, m, a));
                    }
                    continue;
                }
                if (mn == "adrp" || mn == "adr") {
                    int64_t v = op_at(ci, 1).imm;
                    out.push_back(Assign(reg(op_at(ci, 0)), mn == "adrp" ? Const(v) : ctx.cnst(v, 8), a));
                    continue;
                }
                static const std::set<std::string> MOVS = {"mov", "movz", "sxtw", "uxtw", "sxtb", "uxtb", "sxth", "uxth", "fmov"};
                if (MOVS.count(mn) && ops.size() == 2) {
                    E dst = reg(ops[0]);
                    if (is(dst, K::Var) && dst->key == "r:x29") continue;
                    out.push_back(Assign(dst, operand(ops[1]), a));
                } else if (mn == "movk") {
                    E dst = reg(op_at(ci, 0));
                    out.push_back(Assign(dst, Bin("|", dst, operand(op_at(ci, 1))), a));
                } else if ((mn == "mvn" || mn == "neg") && ops.size() == 2) {
                    out.push_back(Assign(reg(ops[0]), Un(mn == "mvn" ? "~" : "-", operand(ops[1])), a));
                } else if (A64_ARITH.count(mn) && ops.size() == 3) {
                    E dst = reg(ops[0]);
                    if (is(dst, K::Var) && dst->key == "r:sp") continue;
                    E x = operand(ops[1]), y = operand(ops[2]);
                    if (is(x, K::Var) && (x->key == "r:sp" || x->key == "r:x29") && is(y, K::Const) && (mn == "add" || mn == "sub")) {
                        int64_t off = mn == "add" ? y->value : -y->value;
                        out.push_back(Assign(dst, Un("&", Var(std::string(x->key == "r:sp" ? "t" : "s") + ":" + std::to_string(off))), a));
                        continue;
                    }
                    out.push_back(Assign(dst, binop(A64_ARITH.at(mn), x, y), a));
                    if (ends(mn, "s")) {
                        if (mn == "subs" && !eq(dst, x) && !eq(dst, y))
                            flags = Flags{"cmp", x, y};
                        else
                            flags = Flags{"res", dst, nullptr};
                    }
                } else if ((mn == "smull" || mn == "umull" || mn == "smulh" || mn == "umulh") && ops.size() == 3) {
                    E prod = Bin("*", operand(ops[1]), operand(ops[2]));
                    out.push_back(Assign(reg(ops[0]), ends(mn, "h") ? Bin(">>", prod, Const(64)) : prod, a));
                } else if ((mn == "madd" || mn == "msub") && ops.size() == 4) {
                    E prod = Bin("*", operand(ops[1]), operand(ops[2]));
                    E acc = operand(ops[3]);
                    out.push_back(Assign(reg(ops[0]), binop(mn == "madd" ? "+" : "-", acc, prod), a));
                } else if (mn == "cmp" || mn == "cmn" || mn == "tst") {
                    flags = Flags{mn == "tst" ? "test" : mn, operand(op_at(ci, 0)), operand(op_at(ci, 1))};
                } else if (starts(mn, "ldr") || starts(mn, "ldur")) {
                    if (ops.size() == 2 && ops[1].type == Operand::Imm) {  // literal pool load
                        out.push_back(Assign(reg(ops[0]), Global(ops[1].imm, 8), a));
                        continue;
                    }
                    int size = access_size(mn, op_at(ci, 0).reg);
                    auto [addr, kind] = mem_addr(op_at(ci, 1));
                    E src = kind == "stack" ? Var(addr->key, size) : global_or_load(addr, size);
                    out.push_back(Assign(reg(ops[0]), src, a));
                } else if (starts(mn, "str") || starts(mn, "stur")) {
                    int size = access_size(mn, op_at(ci, 0).reg);
                    auto [addr, kind] = mem_addr(op_at(ci, 1));
                    E dst = kind == "stack" ? Var(addr->key, size) : global_or_load(addr, size);
                    out.push_back(Assign(dst, reg(ops[0]), a));
                } else if (mn == "cset") {
                    out.push_back(Assign(reg(op_at(ci, 0)), cond_from_flags(flags, a64_cc(ci.cc)), a));
                } else if (mn == "csel" && ops.size() == 3) {
                    out.push_back(Assign(reg(ops[0]), Ternary(cond_from_flags(flags, a64_cc(ci.cc)), operand(ops[1]), operand(ops[2])), a));
                } else if (mn == "bl") {
                    int64_t t = op_at(ci, 0).imm;
                    out.push_back(call(ci, AddrOf(t), Addr(t), out));
                } else if (mn == "blr") {
                    auto tgt = ctx.call_target_of(a);
                    out.push_back(call(ci, tgt ? AddrOf(int64_t(*tgt)) : reg(op_at(ci, 0)), tgt, out));
                } else if (mn == "ret") {
                    lb.term = Return(Var("r:x0"), a);
                } else if (mn == "b") {
                    Addr tgt = Addr(op_at(ci, 0).imm);
                    if (ctx.function.tail_calls.count(tgt))
                        lb.term = Return(call_expr(AddrOf(int64_t(tgt)), tgt, out), a);
                    else
                        lb.term = Goto(tgt, a);
                } else if (mn == "br") {
                    auto tgt = ctx.call_target_of(a);
                    if (tgt)
                        lb.term = Return(call_expr(AddrOf(int64_t(*tgt)), tgt, out), a);
                    else
                        lb.term = Goto(std::nullopt, a, reg(op_at(ci, 0)));
                } else if (starts(mn, "b.")) {
                    lb.term = Branch(cond_from_flags(flags, mn.substr(2)), Addr(op_at(ci, 0).imm), a + 4, a);
                } else if (mn == "cbz" || mn == "cbnz") {
                    E r = reg(op_at(ci, 0));
                    lb.term = Branch(Bin(mn == "cbz" ? "==" : "!=", r, Const(0)), Addr(op_at(ci, 1).imm), a + 4, a);
                } else if (mn == "tbz" || mn == "tbnz") {
                    E r = reg(op_at(ci, 0));
                    E bit = Bin("&", Bin(">>", r, Const(op_at(ci, 1).imm)), Const(1));
                    lb.term = Branch(Bin(mn == "tbz" ? "==" : "!=", bit, Const(0)), Addr(op_at(ci, 2).imm), a + 4, a);
                } else if (mn == "brk" || mn == "udf") {
                    out.push_back(ExprStmt(Call(Var("r:__trap")), a));
                } else {
                    out.push_back(Asm(asm_text(ci), a));
                }
            } catch (const LiftError& e) {
                out.push_back(Asm(mn + " " + ci.op_str + "  /* lift error: " + e.what() + " */", a));
            }
        }
        return lb;
    }

private:
    const Context& ctx;

    E reg(const Operand& op) const {
        const std::string& name = op.reg;
        if (name == "xzr" || name == "wzr") return Const(0);
        int size = starts(name, "w") ? 4 : 8;
        return Var("r:" + a64_fam(name), size);
    }

    E operand(const Operand& op) const {
        if (op.type == Operand::Reg) {
            E e = reg(op);
            if (op.shift_type && op.shift_value) {
                std::string sh = op.shift_type == 2 || op.shift_type == 3 ? ">>" : "<<";
                e = binop(sh, e, Const(op.shift_value));
            }
            return e;
        }
        if (op.type == Operand::Imm) {
            int64_t v = op.imm;
            if (op.shift_type == 1 && op.shift_value) v = int64_t(uint64_t(v) << op.shift_value);
            return ctx.cnst(v, 8);
        }
        return mem(op, 8);
    }

    std::pair<E, std::string> mem_addr(const Operand& op) const {
        const auto& m = op.mem;
        std::string base = m.base.empty() ? "" : a64_fam(m.base);
        if ((base == "sp" || base == "x29") && m.index.empty())
            return {Var(std::string(base == "x29" ? "s" : "t") + ":" + std::to_string(m.disp)), "stack"};
        E e = base.empty() ? Const(0) : Var("r:" + base);
        if (!m.index.empty()) {
            E idx = Var("r:" + a64_fam(m.index));
            if (op.shift_value) idx = Bin("*", idx, Const(int64_t(1) << op.shift_value));
            e = Bin("+", e, idx);
        }
        return {binop("+", e, Const(m.disp)), ""};
    }

    E mem(const Operand& op, int size) const {
        auto [addr, kind] = mem_addr(op);
        if (kind == "stack") return Var(addr->key, size);
        return Load(addr, size);
    }

    static int access_size(const std::string& mn, const std::string& regname) {
        for (auto& [suf, sz] : A64_SIZE)
            if (ends(mn, suf) && mn != "ldp" && mn != "stp") return sz;
        return starts(regname, "w") ? 4 : 8;
    }

    static E global_or_load(const E& addr, int size) {
        if (is(addr, K::Const)) return Global(addr->value, size);
        return Load(addr, size);
    }

    E call_expr(E target, std::optional<Addr> tgt_addr, const std::vector<Stmt>& out) const {
        std::optional<int> known;
        if (tgt_addr) {
            auto name = ctx.callee_name(tgt_addr);
            if (name) known = known_args(*name);
        }
        KeySet assigned;
        scan_args(out, assigned);
        size_t n = 0;
        while (n < 8 && assigned.count(A64_ARGS[n])) n++;
        if (known) n = std::min<size_t>(size_t(*known), 8);
        std::vector<E> cargs;
        for (size_t i = 0; i < n; i++) cargs.push_back(Var("r:" + A64_ARGS[i]));
        return Call(std::move(target), std::move(cargs));
    }

    Stmt call(const Insn& ci, E target, std::optional<Addr> tgt_addr, const std::vector<Stmt>& out) const {
        return Assign(Var("r:x0"), call_expr(std::move(target), tgt_addr, out), ci.addr, a64_clobber());
    }
};

}  // namespace

std::optional<int> known_args(const std::string& name) {
    auto it = KNOWN_ARGS.find(name);
    if (it == KNOWN_ARGS.end()) return std::nullopt;
    return it->second;
}

const std::vector<std::string>& x86_abi_args(const std::string& abi) {
    static const std::vector<std::string> none;
    auto it = ABI_ARGS.find(abi);
    return it == ABI_ARGS.end() ? none : it->second;
}

// Map a condition code (x86 jcc suffix or ARM64 cond) to a C comparison.
E cond_from_flags(const std::optional<Flags>& flags, const std::string& cc) {
    if (!flags) return Un("?", Const(0));  // unknown flags; rendered as cond_unknown
    E a, b;
    const std::string& kind = flags->kind;
    if (kind == "cmp") {
        a = flags->a; b = flags->b;
    } else if (kind == "cmn") {
        a = flags->a; b = Un("-", flags->b);
    } else if (kind == "test") {
        a = eq(flags->a, flags->b) ? flags->a : Bin("&", flags->a, flags->b);
        b = Const(0);
    } else {  // result of arithmetic
        a = flags->a; b = Const(0);
    }
    auto it = CC_OPS.find(cc);
    if (it == CC_OPS.end()) return Un("?", Const(0));
    const std::string& op = it->second;
    if (op == "<s0" || op == ">=s0") {
        E diff = is_const(b, 0) ? a : binop("-", a, b);
        return Bin(op == "<s0" ? "<" : ">=", diff, Const(0));
    }
    return Bin(op, a, b);
}

LiftedBlock lift_x86(const Context& ctx, const Block& block, const std::vector<Insn>& insns) {
    return X86Lifter(ctx).lift(block, insns);
}

LiftedBlock lift_arm64(const Context& ctx, const Block& block, const std::vector<Insn>& insns) {
    return Arm64Lifter(ctx).lift(block, insns);
}

}  // namespace rv::dec
