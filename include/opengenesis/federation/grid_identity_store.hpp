#pragma once

#include "opengenesis/federation/grid_identity.hpp"

#include <mutex>
#include <string>
#include <string_view>

namespace opengenesis::federation {

struct LocalGridIdentity {
    std::string grid_id;
    std::string base_url;
    GridKeyPair keys;
};

class GridIdentityStore final {
public:
    GridIdentityStore(std::string path, std::string grid_id, std::string base_url);

    [[nodiscard]] LocalGridIdentity identity() const;
    void rotate_keys();

private:
    void load_or_create();
    void persist_locked() const;

    std::string path_;
    mutable std::mutex mutex_;
    LocalGridIdentity identity_;
};

} // namespace opengenesis::federation
