// Raw binary loader (firmware dumps, shellcode) with prologue-based arch guess.
// Heuristic inspired by radare2's `aap` and binwalk's `-A` opcode scan.
#include <algorithm>

#include "core/errors.hpp"
#include "formats/formats.hpp"

namespace rv {

const std::vector<std::string> ARCHES = {"x86", "x86-64", "arm", "arm64"};

int arch_bits(const std::string& arch) { return (arch == "x86-64" || arch == "arm64") ? 64 : 32; }

namespace {
// Byte patterns; -1 = any byte.
struct Pat { std::vector<int> b; };
const std::vector<std::pair<std::string, std::vector<std::vector<int>>>> PROLOGUES = {
    {"x86-64", {{0x55, 0x48, 0x89, 0xe5}, {0xf3, 0x0f, 0x1e, 0xfa}, {0x48, 0x83, 0xec, -1}, {0x41, 0x57, 0x41, 0x56}}},
    {"x86", {{0x55, 0x89, 0xe5}, {0x55, 0x8b, 0xec}, {0x83, 0xec, -1, 0x56}}},
    {"arm64", {{0xfd, 0x7b, -1, 0xa9}, {0xfd, 0x03, 0x00, 0x91}, {0xc0, 0x03, 0x5f, 0xd6}}},
    {"arm", {{-1, -1, 0x2d, 0xe9}, {0x1e, 0xff, 0x2f, 0xe1}}},
};

// Non-overlapping left-to-right match count, like re.findall.
int count(const Bytes& d, size_t n, const std::vector<int>& p) {
    int c = 0;
    for (size_t i = 0; i + p.size() <= n;) {
        bool ok = true;
        for (size_t k = 0; k < p.size(); k++)
            if (p[k] >= 0 && d[i + k] != p[k]) { ok = false; break; }
        if (ok) { c++; i += p.size(); } else i++;
    }
    return c;
}
}  // namespace

std::string guess_arch(const Bytes& d, std::map<std::string, int>* scores) {
    size_t n = std::min<size_t>(d.size(), 4 * 1024 * 1024);
    std::string best = "x86-64";
    int best_score = -1;
    for (auto& [arch, pats] : PROLOGUES) {
        int s = 0;
        for (auto& p : pats) s += count(d, n, p);
        if (scores) (*scores)[arch] = s;
        if (s > best_score) { best_score = s; best = arch; }
    }
    return best_score > 0 ? best : "x86-64";
}

Binary load_raw(const std::string& path, std::shared_ptr<const Bytes> data, std::optional<std::string> arch, Addr base,
                const std::string& endian) {
    std::map<std::string, int> scores;
    bool detected = !arch;
    if (!arch) arch = guess_arch(*data, &scores);
    if (std::find(ARCHES.begin(), ARCHES.end(), *arch) == ARCHES.end())
        throw ReversaError("Neznáma architektúra '" + *arch + "'. Podporované: x86, x86-64, arm, arm64");
    Binary b;
    b.path = path;
    b.data = data;
    b.format = "RAW";
    b.arch = *arch;
    b.bits = arch_bits(*arch);
    b.endian = endian;
    b.base = base;
    b.entry = base;
    b.sections.push_back({"raw", base, data->size(), 0, data->size(), true, true, true});
    if (detected) {
        std::string s;
        for (auto& [a, v] : scores) s += (s.empty() ? "" : ", ") + a + "=" + std::to_string(v);
        b.meta["arch_scores"] = s;
        b.warn("Surové dáta: architektúru som odhadol ako " + *arch +
                   " podľa typických prológov funkcií. Ak je odhad zlý, zadajte ju ručne (--arch).",
               std::nullopt, "", "info");
    }
    return b;
}

}  // namespace rv
