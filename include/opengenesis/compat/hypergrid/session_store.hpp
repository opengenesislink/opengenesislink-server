#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace opengenesis::compat::hypergrid {

enum class TravelState {
    active,
    returning_home,
    logged_out,
    expired
};

struct HomeTravelSession {
    std::string session_id;
    std::string user_id;
    std::string native_user_id;
    std::string destination_gatekeeper;
    std::string service_token;
    std::string client_ip;
    TravelState state{TravelState::active};
    std::int64_t created_unix{0};
    std::int64_t expires_unix{0};
    std::int64_t ended_unix{0};
};

struct ForeignVisitorSession {
    std::string session_id;
    std::string agent_id;
    std::string home_uri;
    std::string asset_uri;
    std::string inventory_uri;
    std::string avatar_uri;
    std::string im_uri;
    std::string service_token;
    std::string destination_region;
    std::string first_name;
    std::string last_name;
    std::string client_ip;
    std::string asset_uri;
    std::string inventory_uri;
    std::string avatar_uri;
    std::string im_uri;
    bool verified{false};
    std::int64_t created_unix{0};
    std::int64_t expires_unix{0};
};

class HypergridSessionStore final {
public:
    explicit HypergridSessionStore(std::string path);

    [[nodiscard]] HomeTravelSession issue_home_travel(
        std::string native_user_id,
        std::string user_id,
        std::string destination_gatekeeper,
        std::string client_ip,
        std::chrono::seconds lifetime = std::chrono::minutes{30});

    [[nodiscard]] bool verify_agent(std::string_view session_id,
                                    std::string_view service_token) const;
    [[nodiscard]] bool verify_client(std::string_view session_id,
                                     std::string_view reported_ip) const;
    [[nodiscard]] bool is_agent_coming_home(std::string_view session_id,
                                            std::string_view grid_external_name) const;
    [[nodiscard]] bool begin_return(std::string_view session_id,
                                    std::string_view remote_grid_external_name);
    [[nodiscard]] bool logout_home(std::string_view user_id,
                                   std::string_view session_id);
    [[nodiscard]] bool request_return_home(std::string_view native_user_id,
                                           std::string_view session_id,
                                           std::string_view home_grid_uri);
    [[nodiscard]] std::optional<HomeTravelSession> home(std::string_view session_id) const;

    [[nodiscard]] bool upsert_foreign(ForeignVisitorSession session,
                                      std::string& reason);
    [[nodiscard]] std::optional<ForeignVisitorSession> foreign(
        std::string_view session_id) const;
    [[nodiscard]] std::optional<ForeignVisitorSession> foreign_by_agent(
        std::string_view agent_id) const;
    [[nodiscard]] bool logout_foreign(std::string_view session_id);

    [[nodiscard]] std::vector<HomeTravelSession> home_sessions() const;
    [[nodiscard]] std::vector<ForeignVisitorSession> foreign_sessions() const;
    std::size_t purge_expired(std::int64_t now_unix);

private:
    void load();
    void persist_locked() const;

    std::string path_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, HomeTravelSession> home_;
    std::unordered_map<std::string, ForeignVisitorSession> foreign_;
};

[[nodiscard]] std::string legacy_uuid_from_seed(std::string_view seed);
[[nodiscard]] std::string_view travel_state_name(TravelState state) noexcept;

} // namespace opengenesis::compat::hypergrid
