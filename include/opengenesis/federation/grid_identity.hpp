#pragma once

#include <string>
#include <string_view>

namespace opengenesis::federation {

struct GridKeyPair {
    std::string public_key_hex;
    std::string private_key_hex;
};

[[nodiscard]] GridKeyPair generate_grid_key_pair();
[[nodiscard]] std::string sign_ed25519(std::string_view private_key_hex,
                                       std::string_view message);
[[nodiscard]] bool verify_ed25519(std::string_view public_key_hex,
                                  std::string_view message,
                                  std::string_view signature);

} // namespace opengenesis::federation
