#include "opengenesis/core/admin_http.hpp"

#include "opengenesis/common/log.hpp"
#include "opengenesis/security/crypto.hpp"
#include "opengenesis/security/scene_ticket.hpp"

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


std::optional<double> json_number(const std::string_view json, const std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    auto position = json.find(needle);
    if (position == std::string_view::npos) return std::nullopt;
    position = json.find(':', position + needle.size());
    if (position == std::string_view::npos) return std::nullopt;
    ++position;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) ++position;
    std::size_t end = position;
    while (end < json.size() && (std::isdigit(static_cast<unsigned char>(json[end])) || json[end]=='-' || json[end]=='+' || json[end]=='.' || json[end]=='e' || json[end]=='E')) ++end;
    if (end == position) return std::nullopt;
    try { return std::stod(std::string{json.substr(position,end-position)}); } catch (...) { return std::nullopt; }
}

std::optional<bool> json_bool(const std::string_view json, const std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    auto position = json.find(needle);
    if (position == std::string_view::npos) return std::nullopt;
    position = json.find(':', position + needle.size());
    if (position == std::string_view::npos) return std::nullopt;
    ++position;
    while (position < json.size() && std::isspace(static_cast<unsigned char>(json[position]))) ++position;
    if (json.substr(position,4)=="true") return true;
    if (json.substr(position,5)=="false") return false;
    return std::nullopt;
}

bool admin_authorized(const HttpRequest& request, std::string_view key) {
    if (key.empty()) return false;
    const auto it = request.headers.find("x-opengenesis-admin-key");
    return it != request.headers.end() && it->second == key;
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
        case 403: return "403 Forbidden";
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


std::string asset_json(const AssetInfo& asset, bool include_hash = true) {
    std::ostringstream body;
    body << "{\"id\":\"" << json_escape(asset.id) << "\",\"name\":\""
         << json_escape(asset.name) << "\",\"mime_type\":\"" << json_escape(asset.mime_type)
         << "\",\"size\":" << asset.size << ",\"permissions\":" << asset.permissions
         << ",\"next_owner_permissions\":" << asset.next_owner_permissions
         << ",\"created_unix\":" << asset.created_unix;
    if (include_hash) body << ",\"content_hash\":\"" << json_escape(asset.content_hash) << "\"";
    body << '}'; return body.str();
}
std::string assets_json(const std::vector<AssetInfo>& assets) {
    std::ostringstream body; body << '[';
    for (std::size_t i=0;i<assets.size();++i){ if(i)body<<','; body<<asset_json(assets[i]); }
    body << ']'; return body.str();
}
std::string inventory_json(const UserInventory& inv) {
    std::ostringstream body;
    body << "{\"root\":{\"id\":\"" << json_escape(inv.root.id) << "\",\"name\":\""
         << json_escape(inv.root.name) << "\"},\"folders\":[";
    for(std::size_t i=0;i<inv.folders.size();++i){if(i)body<<',';const auto& f=inv.folders[i];body<<"{\"id\":\""<<json_escape(f.id)<<"\",\"parent_id\":\""<<json_escape(f.parent_id)<<"\",\"name\":\""<<json_escape(f.name)<<"\"}";}
    body << "],\"items\":[";
    for(std::size_t i=0;i<inv.items.size();++i){if(i)body<<',';const auto& item=inv.items[i];body<<"{\"id\":\""<<json_escape(item.id)<<"\",\"parent_id\":\""<<json_escape(item.parent_id)<<"\",\"asset_id\":\""<<json_escape(item.asset_id)<<"\",\"name\":\""<<json_escape(item.name)<<"\"}";}
    body << "]}"; return body.str();
}

std::string presences_json(const std::vector<PresenceInfo>& presences) {
    std::ostringstream body;
    body << '[';
    for (std::size_t i = 0; i < presences.size(); ++i) {
        if (i) body << ',';
        const auto& p = presences[i];
        body << "{\"user_id\":\"" << json_escape(p.user_id) << "\",\"display_name\":\""
             << json_escape(p.display_name) << "\",\"region_id\":\"" << json_escape(p.region_id)
             << "\",\"node_id\":\"" << json_escape(p.node_id) << "\",\"entity_id\":"
             << p.entity_id << ",\"x\":" << p.x << ",\"y\":" << p.y << ",\"z\":" << p.z
             << ",\"updated_unix\":" << p.updated_unix << '}';
    }
    body << ']';
    return body.str();
}

std::string friends_json(const std::vector<FriendRelation>& relations, std::string_view current_user) {
    std::ostringstream body;
    body << '[';
    for (std::size_t i = 0; i < relations.size(); ++i) {
        if (i) body << ',';
        const auto& r = relations[i];
        const auto other = r.user_a == current_user ? r.user_b : r.user_a;
        body << "{\"id\":\"" << json_escape(r.id) << "\",\"other_user_id\":\""
             << json_escape(other) << "\",\"status\":\"" << json_escape(r.status)
             << "\",\"requested_by\":\"" << json_escape(r.requested_by)
             << "\",\"created_unix\":" << r.created_unix << ",\"updated_unix\":"
             << r.updated_unix << '}';
    }
    body << ']';
    return body.str();
}

std::string messages_json(const std::vector<DirectMessage>& messages, std::string_view current_user) {
    std::ostringstream body;
    body << '[';
    for (std::size_t i = 0; i < messages.size(); ++i) {
        if (i) body << ',';
        const auto& m = messages[i];
        body << "{\"id\":\"" << json_escape(m.id) << "\",\"sender_id\":\""
             << json_escape(m.sender_id) << "\",\"recipient_id\":\""
             << json_escape(m.recipient_id) << "\",\"direction\":\""
             << (m.sender_id == current_user ? "out" : "in") << "\",\"text\":\""
             << json_escape(m.text) << "\",\"sent_unix\":" << m.sent_unix
             << ",\"read_unix\":" << m.read_unix << '}';
    }
    body << ']';
    return body.str();
}
std::string groups_json(const std::vector<GroupInfo>& groups) {
    std::ostringstream out; out << '[';
    for (std::size_t i=0;i<groups.size();++i){if(i)out<<',';const auto& g=groups[i];out<<"{\"id\":\""<<json_escape(g.id)<<"\",\"name\":\""<<json_escape(g.name)<<"\",\"founder_user_id\":\""<<json_escape(g.founder_user_id)<<"\",\"created_unix\":"<<g.created_unix<<'}';}
    out << ']'; return out.str();
}
std::string group_members_json(const std::vector<GroupMember>& members) {
    std::ostringstream out; out << '[';
    for(std::size_t i=0;i<members.size();++i){if(i)out<<',';const auto&m=members[i];out<<"{\"user_id\":\""<<json_escape(m.user_id)<<"\",\"role\":\""<<json_escape(m.role)<<"\",\"powers\":"<<m.powers<<",\"joined_unix\":"<<m.joined_unix<<'}';}
    out<<']';return out.str();
}
std::string parcels_json(const std::vector<ParcelInfo>& parcels) {
    std::ostringstream out; out << '[';
    for(std::size_t i=0;i<parcels.size();++i){if(i)out<<',';const auto&p=parcels[i];out<<"{\"id\":\""<<json_escape(p.id)<<"\",\"region_id\":\""<<json_escape(p.region_id)<<"\",\"name\":\""<<json_escape(p.name)<<"\",\"owner_user_id\":\""<<json_escape(p.owner_user_id)<<"\",\"group_id\":\""<<json_escape(p.group_id)<<"\",\"x1\":"<<p.x1<<",\"y1\":"<<p.y1<<",\"x2\":"<<p.x2<<",\"y2\":"<<p.y2<<",\"public_entry\":"<<(p.public_entry?"true":"false")<<",\"public_build\":"<<(p.public_build?"true":"false")<<",\"group_build\":"<<(p.group_build?"true":"false")<<",\"group_terraform\":"<<(p.group_terraform?"true":"false")<<'}';}
    out<<']';return out.str();
}
std::string bans_json(const std::vector<BanRecord>& bans) {
    std::ostringstream out;out<<'[';for(std::size_t i=0;i<bans.size();++i){if(i)out<<',';const auto&b=bans[i];out<<"{\"id\":\""<<json_escape(b.id)<<"\",\"user_id\":\""<<json_escape(b.user_id)<<"\",\"scope\":\""<<json_escape(b.scope)<<"\",\"scope_id\":\""<<json_escape(b.scope_id)<<"\",\"reason\":\""<<json_escape(b.reason)<<"\",\"created_by\":\""<<json_escape(b.created_by)<<"\",\"created_unix\":"<<b.created_unix<<",\"expires_unix\":"<<b.expires_unix<<'}';}out<<']';return out.str();
}

std::string estates_json(const std::vector<EstateInfo>& estates) {
    std::ostringstream out; out << '[';
    for(std::size_t i=0;i<estates.size();++i){if(i)out<<',';const auto&e=estates[i];out<<"{\"id\":\""<<json_escape(e.id)<<"\",\"name\":\""<<json_escape(e.name)<<"\",\"owner_user_id\":\""<<json_escape(e.owner_user_id)<<"\",\"managers\":[";for(std::size_t j=0;j<e.manager_user_ids.size();++j){if(j)out<<',';out<<'\"'<<json_escape(e.manager_user_ids[j])<<'\"';}out<<"],\"created_unix\":"<<e.created_unix<<'}';}
    out<<']';return out.str();
}
std::string estate_policies_json(const std::vector<RegionEstatePolicy>& policies) {
    std::ostringstream out; out << '[';
    for(std::size_t i=0;i<policies.size();++i){if(i)out<<',';const auto&p=policies[i];out<<"{\"region_id\":\""<<json_escape(p.region_id)<<"\",\"estate_id\":\""<<json_escape(p.estate_id)<<"\",\"public_access\":"<<(p.public_access?"true":"false")<<",\"allow_fly\":"<<(p.allow_fly?"true":"false")<<",\"allow_scripts\":"<<(p.allow_scripts?"true":"false")<<",\"allow_voice\":"<<(p.allow_voice?"true":"false")<<",\"max_agents\":"<<p.max_agents<<",\"maturity\":"<<static_cast<unsigned>(p.maturity)<<",\"landing\":{\"x\":"<<p.landing_x<<",\"y\":"<<p.landing_y<<",\"z\":"<<p.landing_z<<"}}";}
    out<<']';return out.str();
}
std::string landmark_json(const LandmarkInfo& m){std::ostringstream out;out<<"{\"id\":\""<<json_escape(m.id)<<"\",\"name\":\""<<json_escape(m.name)<<"\",\"region_id\":\""<<json_escape(m.region_id)<<"\",\"x\":"<<m.x<<",\"y\":"<<m.y<<",\"z\":"<<m.z<<",\"created_unix\":"<<m.created_unix<<'}';return out.str();}
std::string landmarks_json(const std::vector<LandmarkInfo>& items){std::ostringstream out;out<<'[';for(std::size_t i=0;i<items.size();++i){if(i)out<<',';out<<landmark_json(items[i]);}out<<']';return out.str();}
std::string notifications_json(const std::vector<NotificationInfo>& items){std::ostringstream out;out<<'[';for(std::size_t i=0;i<items.size();++i){if(i)out<<',';const auto&n=items[i];out<<"{\"id\":\""<<json_escape(n.id)<<"\",\"type\":\""<<json_escape(n.type)<<"\",\"title\":\""<<json_escape(n.title)<<"\",\"body\":\""<<json_escape(n.body)<<"\",\"target\":\""<<json_escape(n.target)<<"\",\"created_unix\":"<<n.created_unix<<",\"read_unix\":"<<n.read_unix<<'}';}out<<']';return out.str();}
std::string group_posts_json(const std::vector<GroupPost>& posts){std::ostringstream out;out<<'[';for(std::size_t i=0;i<posts.size();++i){if(i)out<<',';const auto&p=posts[i];out<<"{\"id\":\""<<json_escape(p.id)<<"\",\"group_id\":\""<<json_escape(p.group_id)<<"\",\"sender_id\":\""<<json_escape(p.sender_id)<<"\",\"kind\":\""<<json_escape(p.kind)<<"\",\"title\":\""<<json_escape(p.title)<<"\",\"text\":\""<<json_escape(p.text)<<"\",\"sent_unix\":"<<p.sent_unix<<'}';}out<<']';return out.str();}

std::string audit_json(const std::vector<AuditEvent>& events) {
    std::ostringstream out;out<<'[';for(std::size_t i=0;i<events.size();++i){if(i)out<<',';const auto&e=events[i];out<<"{\"sequence\":"<<e.sequence<<",\"actor\":\""<<json_escape(e.actor)<<"\",\"action\":\""<<json_escape(e.action)<<"\",\"target\":\""<<json_escape(e.target)<<"\",\"detail\":\""<<json_escape(e.detail)<<"\",\"unix_time\":"<<e.unix_time<<'}';}out<<']';return out.str();
}

