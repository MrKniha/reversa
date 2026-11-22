// Analysis facts must match samples/expected/<name>.json (regenerate with `rvdump --expected samples`).
#include <filesystem>
#include <fstream>
#include <sstream>

#include "../../tools/facts.hpp"
#include "test.hpp"

using namespace rv;
namespace fs = std::filesystem;

TEST("golden facts of every sample match samples/expected") {
    fs::path root = fs::path(REVERSA_SOURCE_DIR) / "samples";
    int n = 0;
    for (auto& e : fs::directory_iterator(root / "bin")) {
        if (e.path().filename() == "SHA256SUMS") continue;
        std::ifstream f(root / "expected" / (e.path().filename().string() + ".json"), std::ios::binary);
        std::stringstream text;
        text << f.rdbuf();
        std::string expected = Json::parse(text.str()).dump(2), got = facts::facts(e.path().string()).dump(2);
        if (expected != got) std::printf("  %s:\n    expected %s\n    got      %s\n", e.path().filename().string().c_str(),
                                         expected.c_str(), got.c_str());
        CHECK(expected == got);
        n++;
    }
    CHECK_EQ(n, 10);
}
