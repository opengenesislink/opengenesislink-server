#include "opengenesis/federation/travel_token.hpp"

#include "opengenesis/federation/grid_identity.hpp"
#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace opengenesis::federation {
namespace {

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

constexpr char kHex[] = "0123456789abcdef";

std::string hex_text(std::string_view value) {
    std::string output(value.size() * 2, '0');
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        output[i * 2] = kHex[(c >> 4U) & 0x0fU];
        output[i * 2 + 1] = kHex[c & 0x0fU];
    }
    return output;
}

unsigned char nibble(const char value) {
    if (value >= '0' && value <= '9') return static_cast<unsigned char>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<unsigned char>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return static_cast<unsigned char>(value - 'A' + 10);
    throw std::runtime_error("invalid token field encoding");
}

std::string unhex_text(std::string_view value) {
    if ((value.size() % 2) != 0) throw std::runtime_error("invalid token field length");
    std::string output(value.size() / 2, '\0');
    for (std::size_t i = 0; i < output.size(); ++i) {
        output[i] = static_cast<char>((nibble(value[i * 2]) << 4U) |
                                      nibble(value[i * 2 + 1]));
    }
    return output;
}

std::string b64url_encode(std::string_view value) {
    auto output = security::base64_encode(value);
    for (char& c : output) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    while (!output.empty() && output.back() == '=') output.pop_back();
    return output;
}

std::string b64url_decode(std::string value) {
    for (char& c : value) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while ((value.size() % 4) != 0) value.push_back('=');
    return security::base64_decode(value, 16 * 1024);
}

std::string serialize(const TravelTokenClaims& claims) {
    return "v=1\n"
           "iss=" + hex_text(claims.issuer_grid) + "\n"
           "aud=" + hex_text(claims.audience_grid) + "\n"
           "sub=" + hex_text(claims.subject_user) + "\n"
           "name=" + hex_text(claims.display_name) + "\n"
           "origin=" + hex_text(claims.origin_region) + "\n"
           "dest=" + hex_text(claims.destination_region) + "\n"
           "sid=" + hex_text(claims.session_id) + "\n"
           "nonce=" + hex_text(claims.nonce) + "\n"
           "iat=" + std::to_string(claims.issued_unix) + "\n"
           "exp=" + std::to_string(claims.expires_unix) + "\n";
}

std::unordered_map<std::string, std::string> fields(std::string_view payload) {
    std::unordered_map<std::string, std::string> output;
    std::istringstream input(std::string{payload});
    std::string line;
    while (std::getline(input, line)) {
        const auto split = line.find('=');
        if (split != std::string::npos) output[line.substr(0, split)] = line.substr(split + 1);
    }
    return output;
}

std::optional<std::int64_t> integer(
    const std::unordered_map<std::string, std::string>& values, const std::string& key) {
    const auto it = values.find(key);
    if (it == values.end()) return std::nullopt;
    std::int64_t result = 0;
    const auto [end, error] = std::from_chars(
        it->second.data(), it->second.data() + it->second.size(), result);
    if (error != std::errc{} || end != it->second.data() + it->second.size()) return std::nullopt;
    return result;
}

} // namespace

IssuedTravelToken issue_travel_token(const std::string_view private_key_hex,
                                     TravelTokenClaims claims,
                                     std::chrono::seconds lifetime) {
    if (claims.issuer_grid.empty() || claims.audience_grid.empty() ||
        claims.subject_user.empty() || claims.destination_region.empty() ||
        claims.session_id.empty()) {
        throw std::runtime_error("incomplete OGL-FED travel claims");
    }

    lifetime = std::clamp(lifetime, std::chrono::seconds{30}, std::chrono::minutes{15});
    const auto now = unix_now();
    claims.issued_unix = now;
    claims.expires_unix = now + lifetime.count();
    if (claims.nonce.empty()) claims.nonce = security::random_hex(16);

    const auto payload = serialize(claims);
    const auto signature = sign_ed25519(private_key_hex, payload);
    return {.token = b64url_encode(payload) + "." + b64url_encode(signature),
            .claims = std::move(claims)};
}

std::optional<TravelTokenClaims> verify_travel_token(
    const std::string_view public_key_hex,
    const std::string_view token,
    const std::string_view expected_audience,
    const std::string_view expected_issuer) {
    try {
        const auto split = token.find('.');
        if (split == std::string_view::npos || token.find('.', split + 1) != std::string_view::npos) {
            return std::nullopt;
        }

        const auto payload = b64url_decode(std::string{token.substr(0, split)});
        const auto signature = b64url_decode(std::string{token.substr(split + 1)});
        if (!verify_ed25519(public_key_hex, payload, signature)) return std::nullopt;

        const auto values = fields(payload);
        if (values.find("v") == values.end() || values.at("v") != "1") return std::nullopt;

        const auto iat = integer(values, "iat");
        const auto exp = integer(values, "exp");
        if (!iat || !exp) return std::nullopt;

        TravelTokenClaims claims{
            .issuer_grid = unhex_text(values.at("iss")),
            .audience_grid = unhex_text(values.at("aud")),
            .subject_user = unhex_text(values.at("sub")),
            .display_name = unhex_text(values.at("name")),
            .origin_region = unhex_text(values.at("origin")),
            .destination_region = unhex_text(values.at("dest")),
            .session_id = unhex_text(values.at("sid")),
            .nonce = unhex_text(values.at("nonce")),
            .issued_unix = *iat,
            .expires_unix = *exp};

        const auto now = unix_now();
        if (claims.audience_grid != expected_audience) return std::nullopt;
        if (!expected_issuer.empty() && claims.issuer_grid != expected_issuer) return std::nullopt;
        if (claims.nonce.empty() || claims.expires_unix <= now ||
            claims.issued_unix > now + 30 || claims.expires_unix <= claims.issued_unix ||
            claims.expires_unix - claims.issued_unix > 900) {
            return std::nullopt;
        }
        return claims;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace opengenesis::federation
