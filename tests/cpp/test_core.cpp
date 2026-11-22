#include "core/sha256.hpp"
#include "core/text.hpp"
#include "formats/formats.hpp"
#include "test.hpp"

using namespace rv;

TEST("sha256 known vectors") {
    CHECK_EQ(sha256_hex(nullptr, 0), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    std::string abc = "abc";
    CHECK_EQ(sha256_hex(reinterpret_cast<const uint8_t*>(abc.data()), 3),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    std::string m(1000, 'a');
    CHECK_EQ(sha256_hex(reinterpret_cast<const uint8_t*>(m.data()), m.size()),
             std::string("41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3"));
}

TEST("utf8 helpers") {
    CHECK(is_valid_utf8("ahoj čau"));
    CHECK(!is_valid_utf8("\xff"));
    CHECK_EQ(latin1_to_utf8("\xe9"), std::string("é"));
    CHECK_EQ(utf8_lossy("a\xff"), std::string("a\xEF\xBF\xBD"));
}

TEST("load all samples") {
    for (auto name : {"hello_x64.elf", "hello_arm64.elf", "hello_x64.exe", "hello_x86.exe", "login_x64_stripped.elf"}) {
        Binary b = load_file(rvtest::sample(name));
        CHECK(!b.sections.empty());
        CHECK(b.entry.has_value());
    }
}
