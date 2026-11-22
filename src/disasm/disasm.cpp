#include "disasm/disasm.hpp"

#include <capstone/capstone.h>

#include <algorithm>

#include "core/errors.hpp"
#include "core/text.hpp"

namespace rv {

const char* flow_name(Flow f) {
    switch (f) {
        case Flow::Seq: return "seq";
        case Flow::Call: return "call";
        case Flow::ICall: return "icall";
        case Flow::Jmp: return "jmp";
        case Flow::IJmp: return "ijmp";
        case Flow::CJmp: return "cjmp";
        case Flow::Ret: return "ret";
        case Flow::Trap: return "trap";
    }
    return "seq";
}

Disassembler::Disassembler(const std::string& arch, const std::string& endian, std::function<bool(Addr)> is_mapped)
    : arch_(arch), is_mapped_(is_mapped ? std::move(is_mapped) : [](Addr) { return false; }) {
    cs_arch a;
    int m;
    if (arch == "x86") { a = CS_ARCH_X86; m = CS_MODE_32; }
    else if (arch == "x86-64") { a = CS_ARCH_X86; m = CS_MODE_64; }
    else if (arch == "arm64") { a = CS_ARCH_ARM64; m = CS_MODE_ARM; }
    else if (arch == "arm") { a = CS_ARCH_ARM; m = CS_MODE_ARM; }
    else throw ReversaError("Architecture " + arch + " is not supported by the disassembler.");
    if (endian == "big") m |= CS_MODE_BIG_ENDIAN;
    csh h;
    if (cs_open(a, cs_mode(m), &h) != CS_ERR_OK) throw ReversaError("Could not initialise Capstone.");
    cs_option(h, CS_OPT_DETAIL, CS_OPT_ON);
    handle_ = h;
}

Disassembler::~Disassembler() {
    csh h = handle_;
    cs_close(&h);
}

static bool in_group(const cs_detail* d, uint8_t g) {
    return std::find(d->groups, d->groups + d->groups_count, g) != d->groups + d->groups_count;
}

std::vector<Insn> Disassembler::decode(const uint8_t* code, size_t size, Addr addr, size_t count, RefTracker* tracker) const {
    std::vector<Insn> out;
    csh h = handle_;
    cs_insn* ci = cs_malloc(h);
    const uint8_t* p = code;
    size_t left = size;
    uint64_t a = addr;
    while ((count == 0 || out.size() < count) && cs_disasm_iter(h, &p, &left, &a, ci)) {
        Insn ins;
        ins.addr = ci->address;
        ins.size = ci->size;
        ins.mnemonic = ci->mnemonic;
        ins.op_str = ci->op_str;
        ins.raw.assign(ci->bytes, ci->bytes + ci->size);
        const cs_detail* d = ci->detail;
        if (arch_ == "x86" || arch_ == "x86-64") {
            for (int i = 0; i < d->x86.op_count; i++) {
                const cs_x86_op& o = d->x86.operands[i];
                Operand op;
                op.size = o.size;
                op.access = o.access;
                if (o.type == X86_OP_REG) { op.type = Operand::Reg; op.reg = cs_reg_name(h, o.reg); }
                else if (o.type == X86_OP_IMM) { op.type = Operand::Imm; op.imm = o.imm; }
                else if (o.type == X86_OP_MEM) {
                    op.type = Operand::Mem;
                    if (o.mem.base) op.mem.base = cs_reg_name(h, o.mem.base);
                    if (o.mem.index) op.mem.index = cs_reg_name(h, o.mem.index);
                    if (o.mem.segment) op.mem.segment = cs_reg_name(h, o.mem.segment);
                    op.mem.scale = o.mem.scale;
                    op.mem.disp = o.mem.disp;
                }
                ins.ops.push_back(op);
            }
            x86(ins, d);
        } else if (arch_ == "arm64") {
            for (int i = 0; i < d->arm64.op_count; i++) {
                const cs_arm64_op& o = d->arm64.operands[i];
                Operand op;
                op.access = o.access;
                op.shift_type = o.shift.type;
                op.shift_value = o.shift.value;
                if (o.type == ARM64_OP_REG) { op.type = Operand::Reg; op.reg = cs_reg_name(h, o.reg); }
                else if (o.type == ARM64_OP_IMM || o.type == ARM64_OP_CIMM) {
                    op.type = Operand::Imm; op.imm = o.imm; op.cimm = o.type == ARM64_OP_CIMM;
                } else if (o.type == ARM64_OP_MEM) {
                    op.type = Operand::Mem;
                    if (o.mem.base) op.mem.base = cs_reg_name(h, o.mem.base);
                    if (o.mem.index) op.mem.index = cs_reg_name(h, o.mem.index);
                    op.mem.disp = o.mem.disp;
                } else if (o.type == ARM64_OP_FP) { op.type = Operand::Fp; }
                ins.ops.push_back(op);
            }
            ins.cc = d->arm64.cc;
            arm64(ins, tracker);
        } else {
            for (int i = 0; i < d->arm.op_count; i++) {
                const cs_arm_op& o = d->arm.operands[i];
                Operand op;
                if (o.type == ARM_OP_REG) { op.type = Operand::Reg; op.reg = cs_reg_name(h, o.reg); }
                else if (o.type == ARM_OP_IMM) { op.type = Operand::Imm; op.imm = o.imm; }
                else if (o.type == ARM_OP_MEM) { op.type = Operand::Mem; op.mem.disp = o.mem.disp; }
                ins.ops.push_back(op);
            }
            generic(ins, d);
        }
        out.push_back(std::move(ins));
    }
    cs_free(ci, 1);
    return out;
}

std::optional<Insn> Disassembler::decode_one(const uint8_t* code, size_t size, Addr addr, RefTracker* tracker) const {
    auto v = decode(code, size, addr, 1, tracker);
    if (v.empty()) return std::nullopt;
    return std::move(v[0]);
}

void Disassembler::x86(Insn& ins, const void* dp) const {
    auto d = static_cast<const cs_detail*>(dp);
    std::optional<int64_t> imm;
    for (auto& o : ins.ops)
        if (o.type == Operand::Imm) { imm = o.imm; break; }
    const std::string& mn = ins.mnemonic;
    auto as_addr = [&](int64_t v) { return arch_ == "x86-64" ? Addr(v) : Addr(uint32_t(v)); };
    if (in_group(d, CS_GRP_CALL)) {
        ins.flow = imm ? Flow::Call : Flow::ICall;
        if (imm) ins.target = as_addr(*imm);
    } else if (in_group(d, CS_GRP_RET) || mn == "ret" || mn == "retf" || mn == "iret" || mn == "iretq") {
        ins.flow = Flow::Ret;
    } else if (in_group(d, CS_GRP_JUMP)) {
        if (mn == "jmp" || mn == "bnd jmp" || mn == "notrack jmp") ins.flow = imm ? Flow::Jmp : Flow::IJmp;
        else ins.flow = Flow::CJmp;
        if (imm) ins.target = as_addr(*imm);
    } else if (mn == "hlt" || mn == "ud2" || mn == "int3") {
        ins.flow = Flow::Trap;
    }
    for (auto& o : ins.ops) {
        if (o.type == Operand::Mem) {
            std::optional<Addr> addr;
            if (o.mem.base == "rip" || o.mem.base == "eip") {
                addr = ins.addr + ins.size + o.mem.disp;
            } else if (o.mem.base.empty() && o.mem.segment.empty()) {
                addr = as_addr(o.mem.disp);
                if (!o.mem.index.empty() && !is_mapped_(*addr)) addr.reset();  // scaled index without a table base
            }
            if (addr) {
                if (mn == "lea") {
                    ins.refs.push_back({*addr, "addr"});
                } else {
                    if (o.access & CS_AC_WRITE) ins.refs.push_back({*addr, "write"});
                    if ((o.access & CS_AC_READ) || !o.access) ins.refs.push_back({*addr, "read"});
                }
                if (ins.flow == Flow::ICall || ins.flow == Flow::IJmp) ins.mem_slot = addr;
            }
        } else if (o.type == Operand::Imm && ins.flow == Flow::Seq && o.imm >= 0x1000 && is_mapped_(Addr(o.imm))) {
            ins.refs.push_back({Addr(o.imm), "addr"});
        }
    }
}

void Disassembler::arm64(Insn& ins, RefTracker* t) const {
    const std::string& mn = ins.mnemonic;
    auto& ops = ins.ops;
    std::optional<int64_t> imm;
    for (auto& o : ops)
        if (o.type == Operand::Imm && !o.cimm) { imm = o.imm; break; }
    if (mn == "bl") { ins.flow = Flow::Call; if (imm) ins.target = Addr(*imm); }
    else if (mn == "blr") ins.flow = Flow::ICall;
    else if (mn == "b") { ins.flow = Flow::Jmp; if (imm) ins.target = Addr(*imm); }
    else if (starts_with(mn, "b.") || mn == "cbz" || mn == "cbnz" || mn == "tbz" || mn == "tbnz") {
        ins.flow = Flow::CJmp;
        if (imm) ins.target = Addr(*imm);
        if (mn == "tbz" || mn == "tbnz") {  // tbz x0, #bit, target: the target is the last immediate
            for (auto& o : ops) if (o.type == Operand::Imm) ins.target = Addr(o.imm);
        }
    } else if (mn == "br") ins.flow = Flow::IJmp;
    else if (mn == "ret" || mn == "eret") ins.flow = Flow::Ret;
    else if (mn == "brk" || mn == "udf" || mn == "hlt") ins.flow = Flow::Trap;
    if (!t) return;
    auto& regs = t->regs;
    if ((mn == "adrp" || mn == "adr") && ops.size() == 2) {
        regs[ops[0].reg] = Addr(ops[1].imm);
        if (mn == "adr") ins.refs.push_back({Addr(ops[1].imm), "addr"});
        return;
    }
    if (mn == "add" && ops.size() == 3 && ops[1].type == Operand::Reg && regs.count(ops[1].reg) && ops[2].type == Operand::Imm) {
        Addr val = regs[ops[1].reg] + ops[2].imm;
        regs[ops[0].reg] = val;
        ins.refs.push_back({val, "addr"});
        return;
    }
    for (auto& o : ops) {
        if (o.type == Operand::Mem && regs.count(o.mem.base)) {
            Addr addr = regs[o.mem.base] + o.mem.disp;
            ins.refs.push_back({addr, starts_with(mn, "st") ? "write" : "read"});
            if (starts_with(mn, "ld") && ops[0].type == Operand::Reg) {
                regs.erase(ops[0].reg);
                t->slots[ops[0].reg] = addr;
            }
            return;
        }
    }
    // any other write to a tracked register invalidates it
    if (!ops.empty() && ops[0].type == Operand::Reg && mn != "str" && mn != "stp" && mn != "cmp" && mn != "cmn" &&
        mn != "tst" && !starts_with(mn, "b"))
        regs.erase(ops[0].reg);
    if ((mn == "br" || mn == "blr") && !ops.empty()) {
        auto it = t->slots.find(ops[0].reg);
        if (it != t->slots.end()) ins.mem_slot = it->second;
    }
}

void Disassembler::generic(Insn& ins, const void* dp) const {
    auto d = static_cast<const cs_detail*>(dp);
    std::optional<int64_t> imm;
    for (auto& o : ins.ops)
        if (o.type == Operand::Imm) imm = o.imm;
    const std::string& mn = ins.mnemonic;
    if (in_group(d, CS_GRP_CALL)) {
        ins.flow = imm ? Flow::Call : Flow::ICall;
        if (imm) ins.target = Addr(uint32_t(*imm));
    } else if (in_group(d, CS_GRP_RET) || (mn == "bx" && ins.op_str == "lr") || (starts_with(mn, "pop") && contains(ins.op_str, "pc"))) {
        ins.flow = Flow::Ret;
    } else if (in_group(d, CS_GRP_JUMP)) {
        ins.flow = mn == "b" ? Flow::Jmp : Flow::CJmp;
        if (imm) ins.target = Addr(uint32_t(*imm));
    }
}

}  // namespace rv
