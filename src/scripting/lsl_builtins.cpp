#include "opengenesis/scripting/lsl_builtins.hpp"

#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_set>

namespace opengenesis::scripting {
namespace {

std::optional<long long> integer_value(const std::string& value) {
    try {
        std::size_t used = 0U;
        const auto parsed = std::stoll(value, &used, 10);
        if (used != value.size()) return std::nullopt;
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<double> float_value(const std::string& value) {
    try {
        std::size_t used = 0U;
        const auto parsed = std::stod(value, &used);
        if (used != value.size() || !std::isfinite(parsed)) return std::nullopt;
        return parsed;
    } catch (...) {
        return std::nullopt;
    }
}

std::string float_string(const double value) {
    if (!std::isfinite(value)) return "0.000000";
    std::ostringstream out;
    out << std::fixed << std::setprecision(6) << value;
    return out.str();
}

struct Vec3 {
    double x{0.0};
    double y{0.0};
    double z{0.0};
};

std::optional<Vec3> parse_vec3(std::string value) {
    value.erase(
        std::remove_if(value.begin(), value.end(), [](const char c) {
            return std::isspace(static_cast<unsigned char>(c)) != 0;
        }),
        value.end());
    if (value.size() < 5U || value.front() != '<' || value.back() != '>') {
        return std::nullopt;
    }
    value = value.substr(1U, value.size() - 2U);
    const auto a = value.find(',');
    const auto b = a == std::string::npos ? std::string::npos : value.find(',', a + 1U);
    if (a == std::string::npos || b == std::string::npos ||
        value.find(',', b + 1U) != std::string::npos) {
        return std::nullopt;
    }
    const auto x = float_value(value.substr(0U, a));
    const auto y = float_value(value.substr(a + 1U, b - a - 1U));
    const auto z = float_value(value.substr(b + 1U));
    if (!x || !y || !z) return std::nullopt;
    return Vec3{*x, *y, *z};
}

std::string vec3_string(const Vec3& value) {
    return "<" + float_string(value.x) + ", " + float_string(value.y) +
           ", " + float_string(value.z) + ">";
}

std::optional<std::size_t> normalized_index(
    const long long index, const std::size_t size) {
    if (size == 0U) return std::nullopt;
    long long normalized = index;
    if (normalized < 0) normalized += static_cast<long long>(size);
    if (normalized < 0 ||
        normalized >= static_cast<long long>(size)) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(normalized);
}

std::string substring_lsl(
    const std::string& source,
    long long start,
    long long end,
    const bool erase) {
    const auto size = source.size();
    if (size == 0U) return erase ? source : std::string{};
    const auto begin = normalized_index(start, size);
    const auto finish = normalized_index(end, size);
    if (!begin || !finish) return erase ? source : std::string{};

    if (*begin <= *finish) {
        if (!erase) return source.substr(*begin, *finish - *begin + 1U);
        return source.substr(0U, *begin) + source.substr(*finish + 1U);
    }

    // LSL wraps when start is after end.
    if (!erase) {
        return source.substr(*begin) + source.substr(0U, *finish + 1U);
    }
    return source.substr(*finish + 1U, *begin - *finish - 1U);
}

std::string timestamp_utc() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &seconds);
#else
    gmtime_r(&seconds, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

} // namespace

bool lsl_builtin_implemented(const std::string_view name) noexcept {
    static const std::unordered_set<std::string> implemented = {
        "llAbs", "llAcos", "llAsin", "llAtan2", "llBase64ToInteger",
        "llBase64ToString", "llCeil", "llChar", "llCos", "llDeleteSubString",
        "llFabs", "llFloor", "llGetDate", "llGetSubString", "llGetTimestamp",
        "llGetUnixTime", "llInsertString", "llIntegerToBase64", "llLog",
        "llLog10", "llOrd", "llPow", "llRound", "llSHA256String", "llSin",
        "llSqrt", "llStringLength", "llStringToBase64", "llStringTrim",
        "llSubStringIndex", "llTan", "llToLower", "llToUpper", "llVecDist",
        "llVecMag", "llVecNorm"
    };
    return implemented.contains(std::string{name});
}

std::optional<std::string> evaluate_lsl_builtin(
    const std::string_view name,
    const std::vector<std::string>& arguments,
    std::string& reason) {
    auto require_count = [&](const std::size_t count) {
        if (arguments.size() != count) {
            reason = "lsl-builtin-argument-count";
            return false;
        }
        return true;
    };
    auto one_float = [&]() -> std::optional<double> {
        if (!require_count(1U)) return std::nullopt;
        const auto value = float_value(arguments[0]);
        if (!value) reason = "lsl-builtin-number-required";
        return value;
    };

    if (name == "llAbs") {
        if (!require_count(1U)) return std::nullopt;
        const auto value = integer_value(arguments[0]);
        if (!value) { reason = "lsl-builtin-integer-required"; return std::nullopt; }
        if (*value == std::numeric_limits<long long>::min()) return "0";
        reason.clear();
        return std::to_string(std::llabs(*value));
    }
    if (name == "llFabs" || name == "llCeil" || name == "llFloor" ||
        name == "llRound" || name == "llSqrt" || name == "llLog" ||
        name == "llLog10" || name == "llSin" || name == "llCos" ||
        name == "llTan" || name == "llAsin" || name == "llAcos") {
        const auto value = one_float();
        if (!value) return std::nullopt;
        reason.clear();
        if (name == "llCeil") return std::to_string(static_cast<long long>(std::ceil(*value)));
        if (name == "llFloor") return std::to_string(static_cast<long long>(std::floor(*value)));
        if (name == "llRound") return std::to_string(static_cast<long long>(std::llround(*value)));
        const double result =
            name == "llFabs" ? std::fabs(*value) :
            name == "llSqrt" ? std::sqrt(std::max(0.0, *value)) :
            name == "llLog" ? std::log(*value) :
            name == "llLog10" ? std::log10(*value) :
            name == "llSin" ? std::sin(*value) :
            name == "llCos" ? std::cos(*value) :
            name == "llTan" ? std::tan(*value) :
            name == "llAsin" ? std::asin(*value) :
                               std::acos(*value);
        return float_string(result);
    }
    if (name == "llPow" || name == "llAtan2") {
        if (!require_count(2U)) return std::nullopt;
        const auto a = float_value(arguments[0]);
        const auto b = float_value(arguments[1]);
        if (!a || !b) { reason = "lsl-builtin-number-required"; return std::nullopt; }
        reason.clear();
        return float_string(name == "llPow" ? std::pow(*a, *b) : std::atan2(*a, *b));
    }
    if (name == "llStringLength") {
        if (!require_count(1U)) return std::nullopt;
        reason.clear();
        return std::to_string(arguments[0].size());
    }
    if (name == "llToLower" || name == "llToUpper") {
        if (!require_count(1U)) return std::nullopt;
        auto value = arguments[0];
        std::transform(value.begin(), value.end(), value.begin(), [&](unsigned char c) {
            return static_cast<char>(name == "llToLower" ? std::tolower(c) : std::toupper(c));
        });
        reason.clear();
        return value;
    }
    if (name == "llStringTrim") {
        if (!require_count(2U)) return std::nullopt;
        const auto mode = integer_value(arguments[1]);
        if (!mode || *mode < 1 || *mode > 3) {
            reason = "lsl-builtin-trim-mode";
            return std::nullopt;
        }
        auto value = arguments[0];
        const auto whitespace = [](const char c) {
            return std::isspace(static_cast<unsigned char>(c)) != 0;
        };
        if (*mode == 1 || *mode == 3) {
            value.erase(value.begin(), std::find_if(value.begin(), value.end(),
                [&](const char c) { return !whitespace(c); }));
        }
        if (*mode == 2 || *mode == 3) {
            value.erase(std::find_if(value.rbegin(), value.rend(),
                [&](const char c) { return !whitespace(c); }).base(), value.end());
        }
        reason.clear();
        return value;
    }
    if (name == "llSubStringIndex") {
        if (!require_count(2U)) return std::nullopt;
        const auto found = arguments[0].find(arguments[1]);
        reason.clear();
        return found == std::string::npos ? "-1" : std::to_string(found);
    }
    if (name == "llGetSubString" || name == "llDeleteSubString") {
        if (!require_count(3U)) return std::nullopt;
        const auto start = integer_value(arguments[1]);
        const auto end = integer_value(arguments[2]);
        if (!start || !end) { reason = "lsl-builtin-integer-required"; return std::nullopt; }
        reason.clear();
        return substring_lsl(arguments[0], *start, *end, name == "llDeleteSubString");
    }
    if (name == "llInsertString") {
        if (!require_count(3U)) return std::nullopt;
        const auto index = integer_value(arguments[1]);
        if (!index) { reason = "lsl-builtin-integer-required"; return std::nullopt; }
        long long pos = *index;
        if (pos < 0) pos += static_cast<long long>(arguments[0].size()) + 1LL;
        pos = std::clamp<long long>(pos, 0LL, static_cast<long long>(arguments[0].size()));
        auto value = arguments[0];
        value.insert(static_cast<std::size_t>(pos), arguments[2]);
        reason.clear();
        return value;
    }
    if (name == "llStringToBase64") {
        if (!require_count(1U)) return std::nullopt;
        reason.clear();
        return security::base64_encode(arguments[0]);
    }
    if (name == "llBase64ToString") {
        if (!require_count(1U)) return std::nullopt;
        try {
            reason.clear();
            return security::base64_decode(arguments[0], 64U * 1024U);
        } catch (...) {
            reason = "lsl-builtin-invalid-base64";
            return std::nullopt;
        }
    }
    if (name == "llIntegerToBase64") {
        if (!require_count(1U)) return std::nullopt;
        const auto value = integer_value(arguments[0]);
        if (!value) { reason = "lsl-builtin-integer-required"; return std::nullopt; }
        const auto v = static_cast<std::uint32_t>(*value);
        std::string bytes(4, '\0');
        bytes[0] = static_cast<char>((v >> 24U) & 0xffU);
        bytes[1] = static_cast<char>((v >> 16U) & 0xffU);
        bytes[2] = static_cast<char>((v >> 8U) & 0xffU);
        bytes[3] = static_cast<char>(v & 0xffU);
        reason.clear();
        return security::base64_encode(bytes);
    }
    if (name == "llBase64ToInteger") {
        if (!require_count(1U)) return std::nullopt;
        try {
            const auto bytes = security::base64_decode(arguments[0], 4U);
            if (bytes.size() != 4U) {
                reason = "lsl-builtin-invalid-base64-integer";
                return std::nullopt;
            }
            const auto value =
                (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[0])) << 24U) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[1])) << 16U) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[2])) << 8U) |
                static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[3]));
            reason.clear();
            return std::to_string(static_cast<std::int32_t>(value));
        } catch (...) {
            reason = "lsl-builtin-invalid-base64-integer";
            return std::nullopt;
        }
    }
    if (name == "llVecMag" || name == "llVecNorm") {
        if (!require_count(1U)) return std::nullopt;
        const auto value = parse_vec3(arguments[0]);
        if (!value) { reason = "lsl-builtin-vector-required"; return std::nullopt; }
        const double magnitude =
            std::sqrt(value->x * value->x + value->y * value->y + value->z * value->z);
        reason.clear();
        if (name == "llVecMag") return float_string(magnitude);
        if (magnitude <= 1e-12) return vec3_string({});
        return vec3_string({value->x / magnitude, value->y / magnitude, value->z / magnitude});
    }
    if (name == "llVecDist") {
        if (!require_count(2U)) return std::nullopt;
        const auto a = parse_vec3(arguments[0]);
        const auto b = parse_vec3(arguments[1]);
        if (!a || !b) { reason = "lsl-builtin-vector-required"; return std::nullopt; }
        const double dx = a->x - b->x;
        const double dy = a->y - b->y;
        const double dz = a->z - b->z;
        reason.clear();
        return float_string(std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    if (name == "llOrd") {
        if (!require_count(2U)) return std::nullopt;
        const auto index = integer_value(arguments[1]);
        if (!index) { reason = "lsl-builtin-integer-required"; return std::nullopt; }
        const auto normalized = normalized_index(*index, arguments[0].size());
        reason.clear();
        if (!normalized) return "0";
        return std::to_string(static_cast<unsigned char>(arguments[0][*normalized]));
    }
    if (name == "llChar") {
        if (!require_count(1U)) return std::nullopt;
        const auto code = integer_value(arguments[0]);
        if (!code || *code < 0 || *code > 127) {
            reason = "lsl-builtin-ascii-code-required";
            return std::nullopt;
        }
        reason.clear();
        return std::string(1U, static_cast<char>(*code));
    }
    if (name == "llSHA256String") {
        if (!require_count(1U)) return std::nullopt;
        reason.clear();
        return security::sha256_hex(arguments[0]);
    }
    if (name == "llGetUnixTime") {
        if (!require_count(0U)) return std::nullopt;
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        reason.clear();
        return std::to_string(seconds);
    }
    if (name == "llGetTimestamp") {
        if (!require_count(0U)) return std::nullopt;
        reason.clear();
        return timestamp_utc();
    }
    if (name == "llGetDate") {
        if (!require_count(0U)) return std::nullopt;
        const auto timestamp = timestamp_utc();
        reason.clear();
        return timestamp.substr(0U, 10U);
    }

    reason = "lsl-builtin-not-implemented";
    return std::nullopt;
}

} // namespace opengenesis::scripting
