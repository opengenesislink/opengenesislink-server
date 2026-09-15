#include "opengenesis/config/toml_config.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace opengenesis::config {
namespace {
std::string trim(std::string value) {
    const auto not_space = [](const unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}
std::string unquote(std::string value) {
    value = trim(std::move(value));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') return value.substr(1, value.size() - 2);
    return value;
}
}
TomlConfig TomlConfig::load_file(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open config: " + path);
    std::ostringstream buffer; buffer << input.rdbuf(); return parse(buffer.str());
}
TomlConfig TomlConfig::parse(const std::string_view text) {
    TomlConfig result;
    std::istringstream input{std::string(text)};
    std::string section, line;
    while (std::getline(input, line)) {
        const auto comment = line.find('#'); if (comment != std::string::npos) line.resize(comment);
        line = trim(std::move(line)); if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') { section = trim(line.substr(1, line.size() - 2)); continue; }
        const auto eq = line.find('='); if (eq == std::string::npos) continue;
        auto key = trim(line.substr(0, eq)); auto value = unquote(line.substr(eq + 1));
        if (!section.empty()) key = section + "." + key;
        result.values_[std::move(key)] = std::move(value);
    }
    return result;
}
bool TomlConfig::contains(const std::string_view key) const { return values_.contains(std::string(key)); }
std::string TomlConfig::get_string(const std::string_view key, std::string default_value) const {
    if (const auto it = values_.find(std::string(key)); it != values_.end()) return it->second;
    return default_value;
}
std::int64_t TomlConfig::get_int(const std::string_view key, const std::int64_t default_value) const {
    if (const auto it = values_.find(std::string(key)); it != values_.end()) return std::stoll(it->second);
    return default_value;
}
double TomlConfig::get_double(const std::string_view key, const double default_value) const {
    if (const auto it = values_.find(std::string(key)); it != values_.end()) return std::stod(it->second);
    return default_value;
}
bool TomlConfig::get_bool(const std::string_view key, const bool default_value) const {
    if (const auto it = values_.find(std::string(key)); it != values_.end()) {
        if (it->second == "true") return true;
        if (it->second == "false") return false;
        throw std::runtime_error("Invalid bool for " + std::string(key));
    }
    return default_value;
}
}
