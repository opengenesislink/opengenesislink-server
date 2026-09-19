#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace opengenesis::security {

[[nodiscard]] std::string random_hex(std::size_t bytes);
[[nodiscard]] std::string sha256_hex(std::string_view value);
[[nodiscard]] std::string hmac_sha256_hex(std::string_view key, std::string_view value);
[[nodiscard]] bool secure_equals(std::string_view left, std::string_view right) noexcept;
[[nodiscard]] std::string base64_encode(std::string_view value);
[[nodiscard]] std::string base64_decode(std::string_view value, std::size_t max_decoded_bytes = 1024U * 1024U);
[[nodiscard]] std::string hash_password(std::string_view password, std::uint32_t iterations = 120000);
[[nodiscard]] bool verify_password(std::string_view password, std::string_view encoded_hash);

} // namespace opengenesis::security
