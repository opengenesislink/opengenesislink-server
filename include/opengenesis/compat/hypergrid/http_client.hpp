#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace opengenesis::compat::hypergrid {

struct HttpResponse {
    int status{0};
    std::string content_type;
    std::string body;
};

[[nodiscard]] std::optional<HttpResponse> http_request(
    std::string_view url,
    std::string_view method,
    std::string_view content_type,
    std::string_view body,
    const std::unordered_map<std::string, std::string>& headers,
    std::string& reason);

} // namespace opengenesis::compat::hypergrid
