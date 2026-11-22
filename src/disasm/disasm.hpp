// Capstone (BSD-3-Clause) based disassembler. Converts capstone instructions into a
// backend-independent Insn: control flow summary, references and operand details.
// Why Capstone: multi-arch, mature, permissive license; Zydis/iced are x86 only.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/model.hpp"

namespace rv {

enum class Flow { Seq, Call, ICall, Jmp, IJmp, CJmp, Ret, Trap };
const char* flow_name(Flow f);

struct Ref {
    Addr addr;
    std::string kind;  // read | write | addr
    bool operator==(const Ref& o) const { return addr == o.addr && kind == o.kind; }
};

struct Operand {
    enum Type { Reg, Imm, Mem, Fp, Other } type = Other;
    std::string reg;  // register name (Reg)
    int64_t imm = 0;  // immediate (Imm)
    struct {
        std::string base, index, segment;
        int scale = 1;
        int64_t disp = 0;
    } mem;
    uint8_t access = 0;  // CS_AC_READ=1 | CS_AC_WRITE=2
    uint8_t size = 0;    // operand size in bytes (x86)
    int shift_type = 0;  // ARM64 shift type (1=lsl, 2=msl, 3=lsr, 4=asr, 5=ror)
    unsigned shift_value = 0;
    bool cimm = false;   // ARM64 C-immediate
};

struct Insn {
    Addr addr = 0;
    uint16_t size = 0;
    std::string mnemonic;
    std::string op_str;
    Bytes raw;
    Flow flow = Flow::Seq;
    std::optional<Addr> target;    // direct branch / call target
    std::vector<Ref> refs;
    std::optional<Addr> mem_slot;  // memory operand of an indirect call/jump (IAT/GOT slot)
    std::vector<Operand> ops;
    int cc = 0;                    // ARM64 condition code (capstone numbering)

    Addr end() const { return addr + size; }
    std::string text() const { return op_str.empty() ? mnemonic : mnemonic + " " + op_str; }
};

// Per-basic-block constant tracking for ARM64 adrp/adr address building.
struct RefTracker {
    std::map<std::string, Addr> regs;
    std::map<std::string, Addr> slots;  // register loaded from a memory slot ("br x17" PLT stubs)
};

class Disassembler {
public:
    Disassembler(const std::string& arch, const std::string& endian = "little",
                 std::function<bool(Addr)> is_mapped = nullptr);
    ~Disassembler();
    Disassembler(const Disassembler&) = delete;
    Disassembler& operator=(const Disassembler&) = delete;

    std::vector<Insn> decode(const uint8_t* code, size_t size, Addr addr, size_t count = 0,
                             RefTracker* tracker = nullptr) const;
    std::optional<Insn> decode_one(const uint8_t* code, size_t size, Addr addr, RefTracker* tracker = nullptr) const;
    const std::string& arch() const { return arch_; }

private:
    void x86(Insn& ins, const void* detail) const;
    void arm64(Insn& ins, RefTracker* t) const;
    void generic(Insn& ins, const void* detail) const;
    std::string arch_;
    size_t handle_ = 0;
    std::function<bool(Addr)> is_mapped_;
};

}  // namespace rv
