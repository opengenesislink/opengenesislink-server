#include "opengenesis/voice/voice_provider.hpp"

#include <algorithm>

namespace opengenesis::voice {

VoiceSession DisabledVoiceProvider::create_session(const VoiceSessionRequest&) {
    return {.allowed = false,
            .provider = "disabled",
            .service_url = {},
            .sip_domain = {},
            .channel = {},
            .token = {},
            .session_id = {},
            .expires_unix = 0};
}

void DisabledVoiceProvider::revoke_session(std::string_view) {}

std::string_view voice_scope_name(const VoiceScope scope) noexcept {
    switch (scope) {
        case VoiceScope::region: return "region";
        case VoiceScope::parcel: return "parcel";
    }
    return "region";
}

bool validate_provider_config(const VoiceProviderConfig& config, std::string& reason) {
    if (!config.enabled) {
        reason.clear();
        return true;
    }
    if (config.provider.empty() || config.provider == "disabled") {
        reason = "voice-provider-required";
        return false;
    }
    if (!config.service_url.starts_with("https://") &&
        !config.service_url.starts_with("http://")) {
        reason = "voice-service-url-required";
        return false;
    }
    if (config.sip_domain.empty()) {
        reason = "voice-sip-domain-required";
        return false;
    }
    if (config.client_id.empty() || config.client_secret.size() < 16) {
        reason = "voice-provider-credentials-invalid";
        return false;
    }
    if (config.token_lifetime < std::chrono::seconds{30} ||
        config.token_lifetime > std::chrono::minutes{15}) {
        reason = "voice-token-lifetime-out-of-range";
        return false;
    }
    reason.clear();
    return true;
}

} // namespace opengenesis::voice
