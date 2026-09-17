#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace opengenesis::voice {

inline constexpr std::string_view kVoiceProtocol = "OGL-VOICE/1";
inline constexpr std::string_view kVoiceCapabilityProtocol = "OGL-VOICE-CAP/1";

enum class VoiceScope {
    region,
    parcel
};

struct VoiceProviderConfig {
    bool enabled{false};
    std::string provider{"disabled"};
    std::string service_url;
    std::string sip_domain;
    std::string client_id;
    std::string client_secret;
    std::chrono::seconds token_lifetime{300};
};

struct VoiceIdentity {
    std::string subject;
    std::string display_name;
    std::string home_grid;
    bool foreign_guest{false};
};

struct VoiceSessionRequest {
    VoiceIdentity identity;
    std::string destination_grid;
    std::string region_id;
    std::string parcel_id;
    VoiceScope scope{VoiceScope::region};
};

struct VoiceSession {
    bool allowed{false};
    std::string provider;
    std::string service_url;
    std::string sip_domain;
    std::string channel;
    std::string token;
    std::string session_id;
    std::int64_t expires_unix{0};
};

class IVoiceProvider {
public:
    virtual ~IVoiceProvider() = default;

    [[nodiscard]] virtual VoiceSession create_session(const VoiceSessionRequest& request) = 0;
    virtual void revoke_session(std::string_view session_id) = 0;
};

class DisabledVoiceProvider final : public IVoiceProvider {
public:
    [[nodiscard]] VoiceSession create_session(const VoiceSessionRequest& request) override;
    void revoke_session(std::string_view session_id) override;
};

[[nodiscard]] std::string_view voice_scope_name(VoiceScope scope) noexcept;
[[nodiscard]] bool validate_provider_config(const VoiceProviderConfig& config,
                                            std::string& reason);

} // namespace opengenesis::voice
