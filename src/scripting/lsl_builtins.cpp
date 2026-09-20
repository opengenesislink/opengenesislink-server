#include "opengenesis/scripting/lsl_builtins.hpp"

#include "opengenesis/security/crypto.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <limits>
#include <random>
#include <vector>
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


struct Quat {
    double x{0.0};
    double y{0.0};
    double z{0.0};
    double w{1.0};
};

std::optional<Quat> parse_quat(std::string value) {
    value.erase(
        std::remove_if(value.begin(), value.end(), [](const char c) {
            return std::isspace(static_cast<unsigned char>(c)) != 0;
        }),
        value.end());
    if (value.size() < 7U || value.front() != '<' || value.back() != '>') {
        return std::nullopt;
    }
    value = value.substr(1U, value.size() - 2U);
    std::vector<double> values;
    std::size_t start = 0U;
    while (start <= value.size()) {
        const auto end = value.find(',', start);
        const auto part = value.substr(
            start, end == std::string::npos
                       ? std::string::npos
                       : end - start);
        const auto parsed = float_value(part);
        if (!parsed) return std::nullopt;
        values.push_back(*parsed);
        if (end == std::string::npos) break;
        start = end + 1U;
    }
    if (values.size() != 4U) return std::nullopt;
    return Quat{values[0], values[1], values[2], values[3]};
}

Quat normalize_quat(Quat q) {
    const auto magnitude = std::sqrt(
        q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (magnitude <= 1e-12) return {};
    q.x /= magnitude;
    q.y /= magnitude;
    q.z /= magnitude;
    q.w /= magnitude;
    return q;
}

std::string quat_string(const Quat& value) {
    return "<" + float_string(value.x) + ", " + float_string(value.y) +
           ", " + float_string(value.z) + ", " + float_string(value.w) + ">";
}

Vec3 rotate_vector(const Quat& input, const Vec3& value) {
    const auto q = normalize_quat(input);
    const Vec3 u{q.x, q.y, q.z};
    const double dot_uv = u.x*value.x + u.y*value.y + u.z*value.z;
    const double dot_uu = u.x*u.x + u.y*u.y + u.z*u.z;
    const Vec3 cross{
        u.y*value.z-u.z*value.y,
        u.z*value.x-u.x*value.z,
        u.x*value.y-u.y*value.x};
    return {
        2.0*dot_uv*u.x + (q.w*q.w-dot_uu)*value.x + 2.0*q.w*cross.x,
        2.0*dot_uv*u.y + (q.w*q.w-dot_uu)*value.y + 2.0*q.w*cross.y,
        2.0*dot_uv*u.z + (q.w*q.w-dot_uu)*value.z + 2.0*q.w*cross.z};
}

std::vector<std::string> split_list(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](char c) {
        return std::isspace(static_cast<unsigned char>(c)) == 0;
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](char c) {
        return std::isspace(static_cast<unsigned char>(c)) == 0;
    }).base(), value.end());
    if (value.size() < 2U || value.front() != '[' || value.back() != ']') {
        return {};
    }
    const auto body = std::string_view{value}.substr(1U, value.size()-2U);
    if (body.empty()) return {};
    std::vector<std::string> result;
    std::size_t start = 0U;
    int angle = 0;
    int square = 0;
    bool quote = false;
    bool escape = false;
    for (std::size_t i=0; i<=body.size(); ++i) {
        const char ch = i<body.size() ? body[i] : ',';
        if (escape) { escape=false; continue; }
        if (quote && ch=='\\') { escape=true; continue; }
        if (ch=='"') { quote=!quote; continue; }
        if (!quote) {
            if (ch=='<') ++angle;
            else if (ch=='>') --angle;
            else if (ch=='[') ++square;
            else if (ch==']') --square;
        }
        if (ch==',' && !quote && angle==0 && square==0) {
            auto item = std::string{body.substr(start,i-start)};
            item.erase(item.begin(), std::find_if(item.begin(), item.end(), [](char c) {
                return std::isspace(static_cast<unsigned char>(c)) == 0;
            }));
            item.erase(std::find_if(item.rbegin(), item.rend(), [](char c) {
                return std::isspace(static_cast<unsigned char>(c)) == 0;
            }).base(), item.end());
            result.push_back(std::move(item));
            start=i+1U;
        }
    }
    return result;
}

std::string list_value(std::string value) {
    if (value.size() >= 2U && value.front()=='"' && value.back()=='"') {
        return value.substr(1U,value.size()-2U);
    }
    return value;
}

std::string list_string(const std::vector<std::string>& values) {
    std::string result="[";
    for(std::size_t i=0;i<values.size();++i){
        if(i!=0U) result+=", ";
        result+=values[i];
    }
    result+="]";
    return result;
}

std::vector<std::string> list_slice(
    const std::vector<std::string>& source,
    long long start,
    long long end,
    bool erase) {
    if(source.empty()) return {};
    const auto begin=normalized_index(start,source.size());
    const auto finish=normalized_index(end,source.size());
    if(!begin||!finish) return erase?source:std::vector<std::string>{};
    std::vector<std::string> selected;
    std::vector<bool> chosen(source.size(),false);
    auto choose=[&](std::size_t i){chosen[i]=true;selected.push_back(source[i]);};
    if(*begin<=*finish){
        for(std::size_t i=*begin;i<=*finish;++i) choose(i);
    } else {
        for(std::size_t i=*begin;i<source.size();++i) choose(i);
        for(std::size_t i=0;i<=*finish;++i) choose(i);
    }
    if(!erase) return selected;
    std::vector<std::string> remaining;
    for(std::size_t i=0;i<source.size();++i) if(!chosen[i]) remaining.push_back(source[i]);
    return remaining;
}

std::string url_escape(std::string_view input) {
    static constexpr char digits[]="0123456789ABCDEF";
    std::string out;
    for(unsigned char c:input){
        if(std::isalnum(c)!=0 || c=='-' || c=='_' || c=='.' || c=='~'){
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(digits[c>>4U]);
            out.push_back(digits[c&0x0fU]);
        }
    }
    return out;
}

std::optional<std::string> url_unescape(std::string_view input) {
    auto nibble=[](char c)->int{
        if(c>='0'&&c<='9') return c-'0';
        if(c>='A'&&c<='F') return c-'A'+10;
        if(c>='a'&&c<='f') return c-'a'+10;
        return -1;
    };
    std::string out;
    for(std::size_t i=0;i<input.size();++i){
        if(input[i]!='%'){out.push_back(input[i]);continue;}
        if(i+2U>=input.size()) return std::nullopt;
        const int hi=nibble(input[i+1U]);
        const int lo=nibble(input[i+2U]);
        if(hi<0||lo<0) return std::nullopt;
        out.push_back(static_cast<char>((hi<<4)|lo));
        i+=2U;
    }
    return out;
}

std::string digest_hex(const EVP_MD* md, std::string_view input) {
    unsigned char bytes[EVP_MAX_MD_SIZE]{};
    unsigned int length=0U;
    if(EVP_Digest(
           input.data(),input.size(),bytes,&length,md,nullptr)!=1){
        return {};
    }
    static constexpr char digits[]="0123456789abcdef";
    std::string out;
    out.reserve(static_cast<std::size_t>(length)*2U);
    for(unsigned int i=0U;i<length;++i){
        out.push_back(digits[bytes[i]>>4U]);
        out.push_back(digits[bytes[i]&0x0fU]);
    }
    return out;
}

std::string generated_key() {
    const auto raw=security::random_hex(16U);
    if(raw.size()!=32U) return {};
    return raw.substr(0U,8U)+"-"+raw.substr(8U,4U)+"-"+
           raw.substr(12U,4U)+"-"+raw.substr(16U,4U)+"-"+
           raw.substr(20U,12U);
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
        "llVecMag", "llVecNorm", "llEscapeURL", "llUnescapeURL",
        "llGenerateKey", "llSHA1String", "llCSV2List", "llList2CSV",
        "llDumpList2String", "llGetListLength", "llList2String",
        "llList2Integer", "llList2Float", "llList2Key", "llList2Vector",
        "llList2Rot", "llList2List", "llDeleteSubList", "llListFindList",
        "llListInsertList", "llListReplaceList", "llEuler2Rot",
        "llAxisAngle2Rot", "llRot2Angle", "llRot2Axis", "llRot2Euler",
        "llRot2Fwd", "llRot2Left", "llRot2Up", "llRotBetween",
        "llAngleBetween"
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
    if (name == "llEscapeURL") {
        if (!require_count(1U)) return std::nullopt;
        reason.clear();
        return url_escape(arguments[0]);
    }
    if (name == "llUnescapeURL") {
        if (!require_count(1U)) return std::nullopt;
        const auto value=url_unescape(arguments[0]);
        if(!value){reason="lsl-builtin-invalid-url-escape";return std::nullopt;}
        reason.clear();
        return *value;
    }
    if (name == "llGenerateKey") {
        if (!require_count(0U)) return std::nullopt;
        reason.clear();
        return generated_key();
    }
    if (name == "llSHA1String") {
        if (!require_count(1U)) return std::nullopt;
        const auto value=digest_hex(EVP_sha1(),arguments[0]);
        if(value.empty()){reason="lsl-builtin-digest-failed";return std::nullopt;}
        reason.clear();
        return value;
    }
    if (name == "llCSV2List") {
        if (!require_count(1U)) return std::nullopt;
        std::vector<std::string> values;
        std::size_t start=0U;
        while(start<=arguments[0].size()){
            const auto end=arguments[0].find(',',start);
            auto value=arguments[0].substr(
                start,end==std::string::npos?std::string::npos:end-start);
            value.erase(value.begin(),std::find_if(value.begin(),value.end(),[](char ch){
                return std::isspace(static_cast<unsigned char>(ch))==0;
            }));
            value.erase(std::find_if(value.rbegin(),value.rend(),[](char ch){
                return std::isspace(static_cast<unsigned char>(ch))==0;
            }).base(),value.end());
            values.push_back("\""+value+"\"");
            if(end==std::string::npos) break;
            start=end+1U;
        }
        reason.clear();
        return list_string(values);
    }
    if (name == "llGetListLength") {
        if (!require_count(1U)) return std::nullopt;
        reason.clear();
        return std::to_string(split_list(arguments[0]).size());
    }
    if (name == "llList2String" || name == "llList2Integer" ||
        name == "llList2Float" || name == "llList2Key" ||
        name == "llList2Vector" || name == "llList2Rot") {
        if (!require_count(2U)) return std::nullopt;
        const auto values=split_list(arguments[0]);
        const auto index=integer_value(arguments[1]);
        if(!index){reason="lsl-builtin-integer-required";return std::nullopt;}
        const auto normalized=normalized_index(*index,values.size());
        reason.clear();
        if(!normalized){
            if(name=="llList2Integer") return "0";
            if(name=="llList2Float") return "0.000000";
            if(name=="llList2Vector") return "<0.000000, 0.000000, 0.000000>";
            if(name=="llList2Rot") return "<0.000000, 0.000000, 0.000000, 1.000000>";
            return std::string{};
        }
        const auto value=list_value(values[*normalized]);
        if(name=="llList2Integer"){
            const auto parsed=integer_value(value);
            return parsed?std::to_string(*parsed):std::string{"0"};
        }
        if(name=="llList2Float"){
            const auto parsed=float_value(value);
            return parsed?float_string(*parsed):std::string{"0.000000"};
        }
        return value;
    }
    if (name == "llList2CSV" || name == "llDumpList2String") {
        if(name=="llList2CSV"){
            if(!require_count(1U)) return std::nullopt;
        } else if(!require_count(2U)) {
            return std::nullopt;
        }
        const auto values=split_list(arguments[0]);
        const auto separator=name=="llList2CSV"?std::string{", "}:arguments[1];
        std::string output;
        for(std::size_t i=0;i<values.size();++i){
            if(i!=0U) output+=separator;
            output+=list_value(values[i]);
        }
        reason.clear();
        return output;
    }
    if (name == "llList2List" || name == "llDeleteSubList") {
        if (!require_count(3U)) return std::nullopt;
        const auto values=split_list(arguments[0]);
        const auto start=integer_value(arguments[1]);
        const auto end=integer_value(arguments[2]);
        if(!start||!end){reason="lsl-builtin-integer-required";return std::nullopt;}
        reason.clear();
        return list_string(list_slice(values,*start,*end,name=="llDeleteSubList"));
    }
    if (name == "llListFindList") {
        if (!require_count(2U)) return std::nullopt;
        const auto source=split_list(arguments[0]);
        const auto test=split_list(arguments[1]);
        if(test.empty()){reason.clear();return "-1";}
        for(std::size_t i=0;i+test.size()<=source.size();++i){
            if(std::equal(test.begin(),test.end(),source.begin()+static_cast<std::ptrdiff_t>(i))){
                reason.clear();
                return std::to_string(i);
            }
        }
        reason.clear();
        return "-1";
    }
    if (name == "llListInsertList") {
        if (!require_count(3U)) return std::nullopt;
        auto destination=split_list(arguments[0]);
        const auto source=split_list(arguments[1]);
        const auto index=integer_value(arguments[2]);
        if(!index){reason="lsl-builtin-integer-required";return std::nullopt;}
        long long position=*index;
        if(position<0) position+=static_cast<long long>(destination.size())+1LL;
        position=std::clamp<long long>(
            position,0LL,static_cast<long long>(destination.size()));
        destination.insert(
            destination.begin()+static_cast<std::ptrdiff_t>(position),
            source.begin(),source.end());
        reason.clear();
        return list_string(destination);
    }
    if (name == "llListReplaceList") {
        if (!require_count(4U)) return std::nullopt;
        auto destination=split_list(arguments[0]);
        const auto source=split_list(arguments[1]);
        const auto start=integer_value(arguments[2]);
        const auto end=integer_value(arguments[3]);
        if(!start||!end){reason="lsl-builtin-integer-required";return std::nullopt;}
        const auto keep=list_slice(destination,*start,*end,true);
        const auto begin=normalized_index(*start,destination.size());
        if(!begin){
            reason.clear();
            return list_string(destination);
        }
        destination=keep;
        const auto position=std::min(*begin,destination.size());
        destination.insert(
            destination.begin()+static_cast<std::ptrdiff_t>(position),
            source.begin(),source.end());
        reason.clear();
        return list_string(destination);
    }
    if (name == "llEuler2Rot") {
        if(!require_count(1U)) return std::nullopt;
        const auto e=parse_vec3(arguments[0]);
        if(!e){reason="lsl-builtin-vector-required";return std::nullopt;}
        const double cr=std::cos(e->x*0.5),sr=std::sin(e->x*0.5);
        const double cp=std::cos(e->y*0.5),sp=std::sin(e->y*0.5);
        const double cy=std::cos(e->z*0.5),sy=std::sin(e->z*0.5);
        const Quat q{
            sr*cp*cy-cr*sp*sy,
            cr*sp*cy+sr*cp*sy,
            cr*cp*sy-sr*sp*cy,
            cr*cp*cy+sr*sp*sy};
        reason.clear();
        return quat_string(normalize_quat(q));
    }
    if (name == "llAxisAngle2Rot") {
        if(!require_count(2U)) return std::nullopt;
        const auto axis=parse_vec3(arguments[0]);
        const auto angle=float_value(arguments[1]);
        if(!axis||!angle){reason="lsl-builtin-axis-angle-required";return std::nullopt;}
        const double mag=std::sqrt(axis->x*axis->x+axis->y*axis->y+axis->z*axis->z);
        if(mag<=1e-12){reason.clear();return quat_string({});}
        const double s=std::sin(*angle*0.5)/mag;
        reason.clear();
        return quat_string(normalize_quat({
            axis->x*s,axis->y*s,axis->z*s,std::cos(*angle*0.5)}));
    }
    if (name == "llRot2Angle" || name == "llRot2Axis" ||
        name == "llRot2Euler" || name == "llRot2Fwd" ||
        name == "llRot2Left" || name == "llRot2Up") {
        if(!require_count(1U)) return std::nullopt;
        const auto parsed=parse_quat(arguments[0]);
        if(!parsed){reason="lsl-builtin-rotation-required";return std::nullopt;}
        const auto q=normalize_quat(*parsed);
        reason.clear();
        if(name=="llRot2Angle"){
            return float_string(2.0*std::acos(std::clamp(q.w,-1.0,1.0)));
        }
        if(name=="llRot2Axis"){
            const double s=std::sqrt(std::max(0.0,1.0-q.w*q.w));
            if(s<=1e-12) return vec3_string({1.0,0.0,0.0});
            return vec3_string({q.x/s,q.y/s,q.z/s});
        }
        if(name=="llRot2Euler"){
            const double sinr=2.0*(q.w*q.x+q.y*q.z);
            const double cosr=1.0-2.0*(q.x*q.x+q.y*q.y);
            const double roll=std::atan2(sinr,cosr);
            const double sinp=2.0*(q.w*q.y-q.z*q.x);
            const double pitch=std::abs(sinp)>=1.0
                ? std::copysign(1.5707963267948966,sinp)
                : std::asin(sinp);
            const double siny=2.0*(q.w*q.z+q.x*q.y);
            const double cosy=1.0-2.0*(q.y*q.y+q.z*q.z);
            return vec3_string({roll,pitch,std::atan2(siny,cosy)});
        }
        if(name=="llRot2Fwd") return vec3_string(rotate_vector(q,{1.0,0.0,0.0}));
        if(name=="llRot2Left") return vec3_string(rotate_vector(q,{0.0,1.0,0.0}));
        return vec3_string(rotate_vector(q,{0.0,0.0,1.0}));
    }
    if (name == "llRotBetween") {
        if(!require_count(2U)) return std::nullopt;
        const auto a=parse_vec3(arguments[0]);
        const auto b=parse_vec3(arguments[1]);
        if(!a||!b){reason="lsl-builtin-vector-required";return std::nullopt;}
        const double am=std::sqrt(a->x*a->x+a->y*a->y+a->z*a->z);
        const double bm=std::sqrt(b->x*b->x+b->y*b->y+b->z*b->z);
        if(am<=1e-12||bm<=1e-12){reason.clear();return quat_string({});}
        const Vec3 av{a->x/am,a->y/am,a->z/am};
        const Vec3 bv{b->x/bm,b->y/bm,b->z/bm};
        const double dot=std::clamp(av.x*bv.x+av.y*bv.y+av.z*bv.z,-1.0,1.0);
        if(dot<-0.999999){
            Vec3 axis=std::abs(av.x)<0.9
                ? Vec3{0.0,-av.z,av.y}
                : Vec3{-av.z,0.0,av.x};
            const double mag=std::sqrt(axis.x*axis.x+axis.y*axis.y+axis.z*axis.z);
            axis={axis.x/mag,axis.y/mag,axis.z/mag};
            reason.clear();
            return quat_string({axis.x,axis.y,axis.z,0.0});
        }
        const Vec3 cross{
            av.y*bv.z-av.z*bv.y,
            av.z*bv.x-av.x*bv.z,
            av.x*bv.y-av.y*bv.x};
        reason.clear();
        return quat_string(normalize_quat({cross.x,cross.y,cross.z,1.0+dot}));
    }
    if (name == "llAngleBetween") {
        if(!require_count(2U)) return std::nullopt;
        const auto a=parse_quat(arguments[0]);
        const auto b=parse_quat(arguments[1]);
        if(!a||!b){reason="lsl-builtin-rotation-required";return std::nullopt;}
        const auto qa=normalize_quat(*a);
        const auto qb=normalize_quat(*b);
        const double dot=std::abs(
            qa.x*qb.x+qa.y*qb.y+qa.z*qb.z+qa.w*qb.w);
        reason.clear();
        return float_string(2.0*std::acos(std::clamp(dot,0.0,1.0)));
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
