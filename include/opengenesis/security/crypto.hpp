#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace opengenesis::security {

[[nodiscard]] std::string random_hex(std::size_t bytes);
[[nodiscard]] std::string sha256_hex(std::string_view value);
[[nodiscard]] std::string hash_password(std::string_view password, std::uint32_t iterations = 120000);
[[nodiscard]] bool verify_password(std::string_view password, std::string_view encoded_hash);

} // namespace opengenesis::security
