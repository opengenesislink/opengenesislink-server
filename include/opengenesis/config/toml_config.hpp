#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
namespace opengenesis::config {
class TomlConfig final {
public:
    static TomlConfig load_file(const std::string& path);
    static TomlConfig parse(std::string_view text);
    [[nodiscard]] bool contains(std::string_view key) const;
    [[nodiscard]] std::string get_string(std::string_view key, std::string default_value = {}) const;
    [[nodiscard]] std::int64_t get_int(std::string_view key, std::int64_t default_value = 0) const;
    [[nodiscard]] double get_double(std::string_view key, double default_value = 0.0) const;
    [[nodiscard]] bool get_bool(std::string_view key, bool default_value = false) const;
private:
    std::unordered_map<std::string, std::string> values_;
};
}
