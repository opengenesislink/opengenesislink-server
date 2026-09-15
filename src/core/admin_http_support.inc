#include "opengenesis/core/admin_http.hpp"

#include "opengenesis/common/log.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cstring>
#include <netdb.h>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

namespace opengenesis::core {
namespace {

struct HttpRequest {
    std::string method;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

struct HttpResponse {
    int status{200};
    std::string content_type{"application/json; charset=utf-8"};
    std::string body;
};

std::string lower(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string json_escape(const std::string_view value) {
    std::ostringstream output;
    for (const unsigned char c : value) {
        switch (c) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\b': output << "\\b"; break;
            case '\f': output << "\\f"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (c < 0x20) {
                    constexpr char hex[] = "0123456789abcdef";
                    output << "\\u00" << hex[(c >> 4U) & 0x0FU] << hex[c & 0x0FU];
                } else {
                    output << static_cast<char>(c);
                }
        }
    }
    return output.str();
}

std::optional<std::string> json_string(const std::string_view json, const std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    auto position = json.find(needle);
    if (position == std::string_view::npos) return std::nullopt;
    position = json.find(':', position + needle.size());
    if (position == std::string_view::npos) return std::nullopt;
    ++position;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) ++position;
    if (position >= json.size() || json[position] != '"') return std::nullopt;
    ++position;
    std::string output;
    while (position < json.size()) {
        const char c = json[position++];
        if (c == '"') return output;
        if (c != '\\') {
            output.push_back(c);
            continue;
        }
        if (position >= json.size()) return std::nullopt;
        const char escaped = json[position++];
        switch (escaped) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            default: return std::nullopt;
        }
    }
    return std::nullopt;
}

bool recv_more(const int fd, std::string& buffer) {
    char chunk[4096];
    const auto count = ::recv(fd, chunk, sizeof(chunk), 0);
    if (count <= 0) return false;
    buffer.append(chunk, static_cast<std::size_t>(count));
    return true;
}

std::optional<HttpRequest> read_request(const int fd) {
    constexpr std::size_t kMaxHeader = 32768;
    constexpr std::size_t kMaxBody = 65536;
    std::string data;
    while (data.find("\r\n\r\n") == std::string::npos) {
        if (data.size() >= kMaxHeader || !recv_more(fd, data)) return std::nullopt;
    }
    const auto header_end = data.find("\r\n\r\n");
    std::istringstream input(data.substr(0, header_end));
    HttpRequest request;
    std::string line;
    if (!std::getline(input, line)) return std::nullopt;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    {
        std::istringstream first(line);
        std::string version;
        if (!(first >> request.method >> request.path >> version)) return std::nullopt;
    }
    const auto query = request.path.find('?');
    if (query != std::string::npos) request.path.resize(query);
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto split = line.find(':');
        if (split == std::string::npos) continue;
        request.headers[lower(trim(line.substr(0, split)))] = trim(line.substr(split + 1));
    }
    std::size_t content_length = 0;
    if (const auto it = request.headers.find("content-length"); it != request.headers.end()) {
        const auto [end, ec] = std::from_chars(it->second.data(), it->second.data() + it->second.size(),
                                               content_length);
        if (ec != std::errc{} || end != it->second.data() + it->second.size() || content_length > kMaxBody) {
            return std::nullopt;
        }
    }
    const auto body_start = header_end + 4;
    while (data.size() - body_start < content_length) {
        if (!recv_more(fd, data)) return std::nullopt;
    }
    request.body = data.substr(body_start, content_length);
    return request;
}

std::string bearer_token(const HttpRequest& request) {
    const auto it = request.headers.find("authorization");
    if (it == request.headers.end()) return {};
    constexpr std::string_view prefix = "Bearer ";
    if (!std::string_view(it->second).starts_with(prefix)) return {};
    return it->second.substr(prefix.size());
}

std::string status_text(const int status) {
    switch (status) {
        case 200: return "200 OK";
        case 201: return "201 Created";
        case 400: return "400 Bad Request";
        case 401: return "401 Unauthorized";
        case 404: return "404 Not Found";
        case 405: return "405 Method Not Allowed";
        case 409: return "409 Conflict";
        default: return "500 Internal Server Error";
    }
}

void send_response(const int fd, const HttpResponse& response) {
    std::ostringstream headers;
    headers << "HTTP/1.1 " << status_text(response.status) << "\r\n"
            << "Content-Type: " << response.content_type << "\r\n"
            << "Content-Length: " << response.body.size() << "\r\n"
            << "Cache-Control: no-store\r\n"
            << "X-Content-Type-Options: nosniff\r\n"
            << "Connection: close\r\n\r\n";
    const auto head = headers.str();
    (void)::send(fd, head.data(), head.size(), MSG_NOSIGNAL);
    (void)::send(fd, response.body.data(), response.body.size(), MSG_NOSIGNAL);
}

std::string worlds_json(const std::vector<WorldNodeInfo>& worlds) {
    std::ostringstream body;
    body << '[';
    for (std::size_t i = 0; i < worlds.size(); ++i) {
        if (i) body << ',';
        const auto& world = worlds[i];
        body << "{\"id\":\"" << json_escape(world.id) << "\",\"name\":\""
             << json_escape(world.name) << "\",\"endpoint\":\"" << json_escape(world.endpoint)
             << "\",\"state\":\"" << json_escape(world.state) << "\",\"generation\":"
             << world.generation << '}';
    }
    body << ']';
    return body.str();
}

std::string regions_json(const std::vector<RegionInfo>& regions) {
    std::ostringstream body;
    body << '[';
    for (std::size_t i = 0; i < regions.size(); ++i) {
        if (i) body << ',';
        const auto& region = regions[i];
        body << "{\"id\":\"" << json_escape(region.id) << "\",\"name\":\""
             << json_escape(region.name) << "\",\"state\":\"" << json_escape(region.state)
             << "\",\"node\":\"" << json_escape(region.node_id) << "\",\"grid_x\":"
             << region.grid_x << ",\"grid_y\":" << region.grid_y << ",\"generation\":"
             << region.node_generation << ",\"ticks\":" << region.ticks << ",\"entities\":"
             << region.entities << ",\"avatars\":" << region.avatars << ",\"physics_bodies\":"
             << region.physics_bodies << ",\"scene_events\":" << region.scene_events
             << ",\"terrain_revision\":" << region.terrain_revision << ",\"sim_fps\":"
             << region.sim_fps << '}';
    }
    body << ']';
    return body.str();
}

std::string users_json(const std::vector<UserInfo>& users) {
    std::ostringstream body;
    body << '[';
    for (std::size_t i = 0; i < users.size(); ++i) {
        if (i) body << ',';
        const auto& user = users[i];
        body << "{\"id\":\"" << json_escape(user.id) << "\",\"username\":\""
             << json_escape(user.username) << "\",\"display_name\":\""
             << json_escape(user.display_name) << "\",\"created_unix\":" << user.created_unix << '}';
    }
    body << ']';
    return body.str();
}

