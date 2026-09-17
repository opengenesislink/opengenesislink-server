#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::federation {

struct FederationPeer {
    std::string grid_id;
    std::string base_url;
    std::string public_key_hex;
    bool trusted{true};
    bool revoked{false};
    std::int64_t updated_unix{0};
};

class FederationTrustStore final {
public:
    explicit FederationTrustStore(std::string path);

    [[nodiscard]] bool trust(FederationPeer peer, std::string& reason);
    [[nodiscard]] bool revoke(std::string_view grid_id);
    [[nodiscard]] std::optional<FederationPeer> find(std::string_view grid_id) const;
    [[nodiscard]] bool is_trusted(std::string_view grid_id,
                                  std::string_view public_key_hex) const;
    [[nodiscard]] std::vector<FederationPeer> list() const;

private:
    void load();
    void persist_locked() const;

    std::string path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, FederationPeer> peers_;
};

} // namespace opengenesis::federation
