// SHA-256 (FIPS 180-4). Used to bind projects to the exact analysed binary.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace rv {
std::string sha256_hex(const uint8_t* data, size_t size);
inline std::string sha256_hex(const std::vector<uint8_t>& v) { return sha256_hex(v.data(), v.size()); }
}  // namespace rv
