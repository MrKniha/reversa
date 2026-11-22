// Small text helpers: UTF-8 validation, Latin-1 fallback, formatting.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rv {

bool is_valid_utf8(std::string_view s);
std::string latin1_to_utf8(std::string_view s);
// Valid UTF-8 is returned as is; invalid sequences become U+FFFD.
std::string utf8_lossy(std::string_view s);
// UTF-8 when valid, otherwise Latin-1 (like bytes.decode("utf-8") / "latin-1").
std::string decode_text(std::string_view s);
std::string lower(std::string s);
bool contains(std::string_view hay, std::string_view needle);
bool starts_with(std::string_view s, std::string_view p);
bool ends_with(std::string_view s, std::string_view p);
std::vector<std::string> split(std::string_view s, char sep);
std::string trim(std::string_view s);
std::string format_hex(uint64_t v);
std::string json_escape(std::string_view s);
std::string html_escape(std::string_view s);
// UTF-8 <-> filesystem path (Windows paths are UTF-16 internally).
std::filesystem::path path_from_utf8(std::string_view s);
std::string path_to_utf8(const std::filesystem::path& p);

}  // namespace rv
