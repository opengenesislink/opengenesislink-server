#include "opengenesis/security/scene_ticket.hpp"

#include "opengenesis/security/crypto.hpp"

#include <openssl/crypto.h>

#include <algorithm>
#include <chrono>
#include <charconv>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace opengenesis::security {
namespace {

std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

constexpr char kHex[] = "0123456789abcdef";

std::string hex_text(std::string_view value) {
    std::string out(value.size() * 2, '0');
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        out[i * 2] = kHex[(c >> 4U) & 0x0fU];
        out[i * 2 + 1] = kHex[c & 0x0fU];
    }
    return out;
}

unsigned char nibble(char c) {
    if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<unsigned char>(c - 'A' + 10);
    throw std::runtime_error("invalid ticket hex");
}

std::string unhex_text(std::string_view value) {
    if ((value.size() % 2) != 0) throw std::runtime_error("invalid ticket hex length");
    std::string out(value.size() / 2, '\0');
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = static_cast<char>((nibble(value[i * 2]) << 4U) | nibble(value[i * 2 + 1]));
    }
    return out;
}

std::unordered_map<std::string, std::string> fields(std::string_view payload) {
    std::unordered_map<std::string, std::string> result;
    std::istringstream in(std::string{payload});
    std::string line;
    while (std::getline(in, line)) {
        const auto split = line.find('=');
        if (split != std::string::npos) result[line.substr(0, split)] = line.substr(split + 1);
    }
    return result;
}

std::optional<std::int64_t> number(const std::unordered_map<std::string, std::string>& values,
                                   const std::string& key) {
    const auto it = values.find(key);
    if (it == values.end()) return std::nullopt;
    std::int64_t result = 0;
    const auto [end, ec] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), result);
    if (ec != std::errc{} || end != it->second.data() + it->second.size()) return std::nullopt;
    return result;
}

bool safe_equal_hex(std::string_view a, std::string_view b) {
    if (a.size() != b.size() || a.empty()) return false;
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

bool valid_capability_text(std::string_view text) {
    if (text.empty() || text.size() > 1024) return false;
    return std::all_of(text.begin(), text.end(), [](const unsigned char c) {
        return std::isalnum(c) != 0 || c == '.' || c == ',' || c == '-' || c == '_';
    });
}

} // namespace

IssuedSceneTicket issue_scene_ticket(const std::string_view secret, std::string user_id,
                                     std::string display_name, std::string region_id,
                                     std::chrono::seconds lifetime, std::string capabilities,
                                     std::string handoff_from_region, std::string group_ids_csv,
                                     double spawn_x, double spawn_y, double spawn_z) {
    if (secret.size() < 32) throw std::runtime_error("scene ticket secret must be at least 32 bytes");
    if (!valid_capability_text(capabilities)) throw std::runtime_error("invalid scene capabilities");
    lifetime = std::clamp(lifetime, std::chrono::seconds{10}, std::chrono::seconds{300});
    const auto now = unix_now();
    SceneTicketClaims claims{.user_id = std::move(user_id),
                             .display_name = std::move(display_name),
                             .region_id = std::move(region_id),
                             .nonce = random_hex(16),
                             .capabilities = std::move(capabilities),
                             .handoff_from_region = std::move(handoff_from_region),
                             .group_ids_csv = std::move(group_ids_csv),
                             .spawn_x = spawn_x, .spawn_y = spawn_y, .spawn_z = spawn_z,
                             .issued_unix = now,
                             .expires_unix = now + lifetime.count()};
    if (claims.user_id.empty() || claims.display_name.empty() || claims.region_id.empty()) {
        throw std::runtime_error("scene ticket claims are incomplete");
    }
    std::ostringstream payload;
    payload << "u=" << hex_text(claims.user_id) << '\n'
            << "d=" << hex_text(claims.display_name) << '\n'
            << "r=" << hex_text(claims.region_id) << '\n'
            << "n=" << claims.nonce << '\n'
            << "c=" << hex_text(claims.capabilities) << '\n'
            << "h=" << hex_text(claims.handoff_from_region) << '\n'
            << "g=" << hex_text(claims.group_ids_csv) << '\n'
            << "x=" << claims.spawn_x << '\n'
            << "y=" << claims.spawn_y << '\n'
            << "z=" << claims.spawn_z << '\n'
            << "i=" << claims.issued_unix << '\n'
            << "e=" << claims.expires_unix << '\n';
    const auto encoded = hex_text(payload.str());
    const auto signature = hmac_sha256_hex(secret, encoded);
    return {.token = "ogst1." + encoded + "." + signature, .claims = std::move(claims)};
}

std::optional<SceneTicketClaims> verify_scene_ticket(const std::string_view secret,
                                                      const std::string_view token,
                                                      const std::string_view expected_region) {
    try {
        constexpr std::string_view prefix = "ogst1.";
        if (secret.size() < 32 || !token.starts_with(prefix)) return std::nullopt;
        const auto dot = token.find('.', prefix.size());
        if (dot == std::string_view::npos) return std::nullopt;
        const auto encoded = token.substr(prefix.size(), dot - prefix.size());
        const auto signature = token.substr(dot + 1);
        const auto expected = hmac_sha256_hex(secret, encoded);
        if (!safe_equal_hex(signature, expected)) return std::nullopt;
        const auto parsed = fields(unhex_text(encoded));
        const auto issued = number(parsed, "i");
        const auto expires = number(parsed, "e");
        if (!issued || !expires) return std::nullopt;
        const auto now = unix_now();
        if (*issued > now + 30 || *expires <= now || *expires - *issued > 300 || *expires <= *issued) {
            return std::nullopt;
        }
        const auto u = parsed.find("u"), d = parsed.find("d"), r = parsed.find("r"), n = parsed.find("n");
        const auto c = parsed.find("c"), h = parsed.find("h"), g = parsed.find("g");
        const auto sx = parsed.find("x"), sy = parsed.find("y"), sz = parsed.find("z");
        if (u == parsed.end() || d == parsed.end() || r == parsed.end() || n == parsed.end() ||
            c == parsed.end() || h == parsed.end()) {
            return std::nullopt;
        }
        SceneTicketClaims claims{.user_id = unhex_text(u->second),
                                 .display_name = unhex_text(d->second),
                                 .region_id = unhex_text(r->second),
                                 .nonce = n->second,
                                 .capabilities = unhex_text(c->second),
                                 .handoff_from_region = unhex_text(h->second),
                                 .group_ids_csv = g == parsed.end() ? std::string{} : unhex_text(g->second),
                                 .spawn_x = sx == parsed.end() ? 128.0 : std::stod(sx->second),
                                 .spawn_y = sy == parsed.end() ? 128.0 : std::stod(sy->second),
                                 .spawn_z = sz == parsed.end() ? 0.0 : std::stod(sz->second),
                                 .issued_unix = *issued,
                                 .expires_unix = *expires};
        if (claims.user_id.empty() || claims.display_name.empty() || claims.region_id.empty() ||
            claims.nonce.empty() || !valid_capability_text(claims.capabilities)) {
            return std::nullopt;
        }
        if (!expected_region.empty() && claims.region_id != expected_region) return std::nullopt;
        return claims;
    } catch (...) {
        return std::nullopt;
    }
}

bool scene_ticket_has_group(const SceneTicketClaims& claims, const std::string_view group_id) {
    if (group_id.empty()) return false;
    std::size_t start = 0;
    while (start <= claims.group_ids_csv.size()) {
        const auto end = claims.group_ids_csv.find(',', start);
        const auto token = std::string_view{claims.group_ids_csv}.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (token == group_id) return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

bool has_scene_capability(const SceneTicketClaims& claims, const std::string_view capability) {
    std::size_t start = 0;
    while (start <= claims.capabilities.size()) {
        const auto end = claims.capabilities.find(',', start);
        const auto token = std::string_view{claims.capabilities}.substr(
            start, end == std::string::npos ? std::string::npos : end - start);
        if (token == capability) return true;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return false;
}

} // namespace opengenesis::security
