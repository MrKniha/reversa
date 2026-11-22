// Pseudocode generator ("decompiler").
//
// STATUS: provisional. It produces readable C-like code for integer code (functions, calls,
// if/else, loops, variables instead of registers) on x86, x86-64 and AArch64. Types are guessed
// from access sizes only; floating point, SIMD and jump tables are shown as __asm / goto *.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "analysis/analysis.hpp"
#include "decompiler/ir.hpp"

namespace rv {

extern const char* const PROVISIONAL_NOTE;

// One clickable piece of a pseudocode line. kind: comment type func var num str glob op kw label asm plain.
struct Seg {
    std::string text, kind;
    std::optional<Addr> addr;  // referenced address (func, str, glob, label)
    std::string key;           // variable key (var)
};

struct Line {
    int indent = 0;
    std::vector<Seg> segs;
    std::optional<Addr> addr;
};

struct Decompiled {
    Addr func = 0;
    std::vector<ir::Stmt> tree;
    bool returns = false;
    int ret_size = 8;
    std::vector<std::string> params;  // var keys in order
    std::vector<std::string> locals;
    std::map<std::string, int> sizes;
    std::map<std::string, std::string> default_names;
    std::string error;
    std::set<Addr> labels;
};

Decompiled decompile(const Analysis& analysis, Addr func_addr);

struct RenderNames {
    std::function<std::string(Addr)> name_of;
    std::function<std::optional<std::string>(const std::string&)> var_override;  // user variable names
    std::function<std::optional<std::string>(Addr)> string_at;
    std::function<bool(int64_t)> is_symbol;
};

std::vector<Line> render(const Decompiled& d, const std::string& func_name, const RenderNames& names);
std::string to_text(const std::vector<Line>& lines);
std::string ctype(int size);

}  // namespace rv
