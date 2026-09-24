#include "opengenesis/compat/hypergrid/server.hpp"

#include "opengenesis/common/log.hpp"
#include "opengenesis/compat/hypergrid/agent_circuit.hpp"
#include "opengenesis/compat/hypergrid/http_client.hpp"
#include "opengenesis/compat/hypergrid/xmlrpc.hpp"

#include <chrono>
#include <cstddef>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace opengenesis::compat::hypergrid {
namespace {

struct Request {
    std::string method;
    std::string path;
    std::string content_type;
    std::string body;
};

bool receive_more(const platform::SocketHandle fd, std::string& data) {
    char buffer[4096];
    const auto count = platform::receive_bytes(fd, buffer, sizeof(buffer));
    if (count <= 0) return false;
    data.append(buffer, static_cast<std::size_t>(count));
    return true;
}

std::optional<Request> read_request(const platform::SocketHandle fd) {
    std::string data;
    while (data.find("\r\n\r\n") == std::string::npos) {
        if (data.size() > 64U * 1024U || !receive_more(fd, data)) return std::nullopt;
    }
    const auto header_end = data.find("\r\n\r\n");
    std::istringstream headers(data.substr(0, header_end));
    std::string line;
    Request request;
    std::string version;
    if (!std::getline(headers, line)) return std::nullopt;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    {
        std::istringstream first(line);
        if (!(first >> request.method >> request.path >> version)) return std::nullopt;
    }

    std::size_t content_length = 0;
    while (std::getline(headers, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        auto name = line.substr(0, colon);
        auto value = line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        for (char& c : name) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        if (name == "content-length") {
            try {
                content_length = static_cast<std::size_t>(std::stoull(value));
            } catch (...) {
                return std::nullopt;
            }
        } else if (name == "content-type") {
            request.content_type = value;
        }
    }
    if (content_length > 1024U * 1024U) return std::nullopt;

    const auto body_start = header_end + 4;
    while (data.size() - body_start < content_length) {
        if (!receive_more(fd, data)) return std::nullopt;
    }
    request.body = data.substr(body_start, content_length);
    return request;
}

void send_all(const platform::SocketHandle fd, std::string_view data) {
    while (!data.empty()) {
        const auto sent = platform::send_bytes(fd, data.data(), data.size());
        if (sent <= 0) return;
        data.remove_prefix(static_cast<std::size_t>(sent));
    }
}

void send_response(const platform::SocketHandle fd,
                   const int status,
                   const std::string_view content_type,
                   const std::string_view body) {
    const char* status_text = "200 OK";
    if (status == 400) status_text = "400 Bad Request";
    else if (status == 404) status_text = "404 Not Found";
    else if (status == 405) status_text = "405 Method Not Allowed";
    else if (status == 406) status_text = "406 Not Acceptable";
    else if (status == 503) status_text = "503 Service Unavailable";

    const auto header = "HTTP/1.1 " + std::string{status_text} +
                        "\r\nContent-Type: " + std::string{content_type} +
                        "\r\nContent-Length: " + std::to_string(body.size()) +
                        "\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n";
    send_all(fd, header);
    send_all(fd, body);
}

std::string json_escape(const std::string_view value) {
    std::string result;
    for (const char c : value) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default: result.push_back(c); break;
        }
    }
    return result;
}

std::string foreign_response(const bool success,
                             const std::string_view reason,
                             const std::string_view remote_ip) {
    return "{\"reason\":\"" + json_escape(reason) +
           "\",\"success\":" + (success ? "true" : "false") +
           ",\"your_ip\":\"" + json_escape(remote_ip) + "\"}";
}

std::string peer_ip(const platform::SocketHandle fd) {
    sockaddr_storage address{};
    platform::SocketLength size = static_cast<platform::SocketLength>(sizeof(address));
    if (::getpeername(fd, reinterpret_cast<sockaddr*>(&address), &size) != 0) return {};
    char host[NI_MAXHOST]{};
    if (::getnameinfo(reinterpret_cast<const sockaddr*>(&address), size, host, sizeof(host),
                      nullptr, 0, NI_NUMERICHOST) != 0) {
        return {};
    }
    return host;
}

bool bool_result(const bool value) {
    return value;
}

bool same_uri(std::string left, std::string right) {
    while (!left.empty() && left.back() == '/') left.pop_back();
    while (!right.empty() && right.back() == '/') right.pop_back();
    return left == right;
}

struct AgentRoute {
    std::string agent_id;
    std::string region_id;
    std::string action;
};

std::optional<AgentRoute> parse_agent_route(std::string_view raw_path) {
    const auto query = raw_path.find('?');
    const auto path = raw_path.substr(0, query);
    constexpr std::string_view prefix = "/agent/";
    if (!path.starts_with(prefix)) return std::nullopt;
    auto rest = path.substr(prefix.size());
    while (!rest.empty() && rest.back() == '/') rest.remove_suffix(1);
    if (rest.empty()) return std::nullopt;

    AgentRoute route;
    const auto first = rest.find('/');
    route.agent_id = std::string{rest.substr(0, first)};
    if (first == std::string_view::npos) return route;
    rest.remove_prefix(first + 1);
    const auto second = rest.find('/');
    route.region_id = std::string{rest.substr(0, second)};
    if (second != std::string_view::npos) {
        rest.remove_prefix(second + 1);
        route.action = std::string{rest};
    }
    return route;
}

std::string simulation_access_response(bool success, std::string_view reason) {
    return "{\"success\":" + std::string(success ? "true" : "false") +
           ",\"reason\":\"" + json_escape(reason) +
           "\",\"version\":\"SIMULATION/0.8\","
           "\"negotiated_inbound_version\":0.8,"
           "\"negotiated_outbound_version\":0.8,"
           "\"features\":[]}";
}

void notify_home_logout(const ForeignVisitorSession& visitor) {
    if (visitor.home_uri.empty()) return;
    const auto body = xmlrpc_struct_call(
        "logout_agent",
        {{"userID", visitor.agent_id}, {"sessionID", visitor.session_id}});
    std::string ignored;
    (void)http_request(visitor.home_uri, "POST", "text/xml", body, {}, ignored);
}

std::unordered_map<std::string, std::string> home_method(
    const XmlRpcCall& call,
    HypergridSessionStore& sessions) {
    const auto field = [&](const std::string_view name) -> std::string {
        const auto it = call.fields.find(std::string{name});
        return it == call.fields.end() ? std::string{} : it->second;
    };

    if (call.method == "verify_agent") {
        return {{"result", bool_result(sessions.verify_agent(field("sessionID"), field("token")))
                               ? "True" : "False"}};
    }
    if (call.method == "verify_client") {
        return {{"result", bool_result(sessions.verify_client(field("sessionID"), field("token")))
                               ? "True" : "False"}};
    }
    if (call.method == "agent_is_coming_home") {
        return {{"result", bool_result(sessions.is_agent_coming_home(
                                   field("sessionID"), field("externalName")))
                               ? "True" : "False"}};
    }
    if (call.method == "logout_agent") {
        return {{"result", bool_result(sessions.logout_home(field("userID"), field("sessionID")))
                               ? "true" : "false"}};
    }
    return {};
}

} // namespace

HypergridServer::HypergridServer(
    std::string address,
    const std::uint16_t port,
    std::shared_ptr<HypergridService> service,
    std::shared_ptr<HypergridSessionStore> sessions,
    std::shared_ptr<IHypergridHomeVerifier> verifier,
    std::shared_ptr<HypergridFriendsAdapter> friends,
    std::shared_ptr<HypergridAssetAdapter> assets,
    std::shared_ptr<HypergridInstantMessageAdapter> instant_messages,
    std::shared_ptr<HypergridInventoryAdapter> inventory,
    std::shared_ptr<HypergridAppearanceAdapter> appearance)
    : address_(std::move(address)),
      port_(port),
      service_(std::move(service)),
      sessions_(std::move(sessions)),
      verifier_(std::move(verifier)),
      friends_(std::move(friends)),
      assets_(std::move(assets)),
      instant_messages_(std::move(instant_messages)),
      inventory_(std::move(inventory)),
      appearance_(std::move(appearance)) {
    if (!service_ || !sessions_ || !verifier_ || !friends_ || !assets_ ||
        !instant_messages_ || !inventory_ || !appearance_) {
        throw std::invalid_argument("Hypergrid server dependencies required");
    }

    legacy_router_ = std::make_shared<LegacyCircuitRouter>(
        service_, sessions_);
    const auto& config = service_->config();
    if (config.internal_port != 0U) {
        legacy_udp_ = std::make_unique<LegacySimulatorUdpGateway>(
            address_, config.internal_port, legacy_router_);
    }
}

HypergridServer::~HypergridServer() {
    stop();
}

void HypergridServer::start() {
    if (running_.exchange(true)) return;
    if (legacy_udp_) legacy_udp_->start();
    thread_ = std::thread(&HypergridServer::run, this);
}

void HypergridServer::stop() {
    if (!running_.exchange(false)) return;
    if (legacy_udp_) legacy_udp_->stop();
    if (platform::socket_valid(listen_fd_)) {
        platform::shutdown_socket(listen_fd_);
        platform::close_socket(listen_fd_);
    }
    if (thread_.joinable()) thread_.join();
}

void HypergridServer::run() {
    try {
        platform::initialize_sockets();
        addrinfo hints{};
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_family = AF_UNSPEC;
        hints.ai_flags = AI_PASSIVE;
        addrinfo* results = nullptr;
        const auto port_text = std::to_string(port_);
        if (::getaddrinfo(address_.c_str(), port_text.c_str(), &hints, &results) != 0) {
            throw std::runtime_error("Hypergrid getaddrinfo failed");
        }

        for (auto* current = results; current; current = current->ai_next) {
            listen_fd_ = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
            if (!platform::socket_valid(listen_fd_)) continue;
            platform::set_reuse_address(listen_fd_);
            if (::bind(listen_fd_, current->ai_addr,
                       static_cast<platform::SocketLength>(current->ai_addrlen)) == 0 &&
                ::listen(listen_fd_, 64) == 0) {
                break;
            }
            platform::close_socket(listen_fd_);
        }
        ::freeaddrinfo(results);
        if (!platform::socket_valid(listen_fd_)) {
            throw std::runtime_error("Hypergrid bind failed");
        }

        common::log(common::LogLevel::info, "compat.hypergrid",
                    "Hypergrid compatibility listener on " + address_ + ":" + port_text);

        while (running_) {
            auto client = ::accept(listen_fd_, nullptr, nullptr);
            if (!platform::socket_valid(client)) {
                if (!running_) break;
                continue;
            }

            const auto remote = peer_ip(client);
            try {
                const auto request = read_request(client);
                if (!request) {
                    send_response(client, 400, "text/plain", "bad request");
                    platform::close_socket(client);
                    continue;
                }
                if (request->method == "GET" && request->path.starts_with("/assets/")) {
                    const auto legacy = assets_->handle_get(request->path);
                    send_response(client, legacy.status, legacy.content_type, legacy.body);
                    platform::close_socket(client);
                    continue;
                }

                if (request->path.starts_with("/agent/")) {
                    const auto route = parse_agent_route(request->path);
                    if (!route || route->agent_id.empty()) {
                        send_response(client, 404, "application/json",
                                      "{\"success\":false,\"reason\":\"invalid-agent-route\"}");
                        platform::close_socket(client);
                        continue;
                    }

                    if (request->method == "QUERYACCESS") {
                        const auto region =
                            service_->region_by_legacy_uuid(route->region_id);
                        const bool available =
                            region && region->state == "online";
                        send_response(
                            client, 200, "application/json",
                            simulation_access_response(
                                available,
                                available ? "" : "destination region unavailable"));
                        platform::close_socket(client);
                        continue;
                    }

                    if (request->method == "PUT") {
                        const auto visitor =
                            sessions_->foreign_by_agent(route->agent_id);
                        const bool accepted =
                            visitor && visitor->verified &&
                            (route->region_id.empty() ||
                             service_->region_by_legacy_uuid(route->region_id)
                                 .has_value());
                        send_response(client, 200, "application/json",
                                      accepted ? "True" : "False");
                        platform::close_socket(client);
                        continue;
                    }

                    if (request->method == "DELETE") {
                        const auto visitor =
                            sessions_->foreign_by_agent(route->agent_id);
                        if (visitor) {
                            if (legacy_router_) {
                                legacy_router_->remove_session(
                                    visitor->session_id);
                            }
                            notify_home_logout(*visitor);
                            (void)sessions_->logout_foreign(
                                visitor->session_id);
                        }
                        send_response(client, 200, "text/plain",
                                      "OpenSim agent " + route->agent_id);
                        platform::close_socket(client);
                        continue;
                    }

                    send_response(client, 405, "application/json",
                                  "{\"success\":false,\"reason\":\"method-not-allowed\"}");
                    platform::close_socket(client);
                    continue;
                }

                if (request->method != "POST") {
                    send_response(client, 405, "text/plain", "method not allowed");
                    platform::close_socket(client);
                    continue;
                }

                if (request->path == "/xinventory" || request->path == "/xinventory/") {
                    if (request->content_type.find("application/x-www-form-urlencoded") ==
                        std::string::npos) {
                        send_response(client, 406, "text/xml",
                                      "<?xml version=\"1.0\"?><ServerResponse><RESULT>False</RESULT></ServerResponse>");
                        platform::close_socket(client);
                        continue;
                    }
                    send_response(client, 200, "text/xml", inventory_->handle_form(request->body));
                } else if (request->path == "/avatar" || request->path == "/avatar/") {
                    if (request->content_type.find("application/x-www-form-urlencoded") ==
                        std::string::npos) {
                        send_response(client, 406, "text/xml",
                                      "<?xml version=\"1.0\"?><ServerResponse><result>Failure</result></ServerResponse>");
                        platform::close_socket(client);
                        continue;
                    }
                    send_response(client, 200, "text/xml", appearance_->handle_form(request->body));
                } else if (request->path == "/hgfriends" || request->path == "/hgfriends/") {
                    if (request->content_type.find("application/x-www-form-urlencoded") ==
                        std::string::npos) {
                        send_response(client, 406, "text/xml",
                                      "<?xml version=\"1.0\"?><ServerResponse><RESULT>Failure</RESULT><Message>form encoding required</Message></ServerResponse>");
                        platform::close_socket(client);
                        continue;
                    }
                    send_response(client, 200, "text/xml", friends_->handle_form(request->body));
                } else if (request->path.starts_with("/foreignagent")) {
                    if (request->content_type.find("application/json") == std::string::npos) {
                        send_response(client, 406, "application/json",
                                      foreign_response(false, "application/json required", remote));
                        platform::close_socket(client);
                        continue;
                    }
                    std::string reason;
                    const auto circuit = parse_foreign_agent_circuit(request->body, reason);
                    if (!circuit) {
                        send_response(client, 400, "application/json",
                                      foreign_response(false, reason, remote));
                        platform::close_socket(client);
                        continue;
                    }
                    const auto home_session = sessions_->home(circuit->session_id);
                    const bool returning_home =
                        home_session &&
                        home_session->state == TravelState::returning_home &&
                        home_session->user_id == circuit->agent_id &&
                        same_uri(circuit->home_uri, service_->config().home_uri) &&
                        sessions_->verify_agent(circuit->session_id,
                                                circuit->service_session_id);

                    if (!returning_home &&
                        !service_token_targets(circuit->service_session_id,
                                               service_->config().external_name)) {
                        send_response(client, 200, "application/json",
                                      foreign_response(false, "service token targets another grid", remote));
                        platform::close_socket(client);
                        continue;
                    }

                    const auto region = service_->region_by_legacy_uuid(circuit->destination_uuid);
                    if (!region || region->state != "online") {
                        send_response(client, 200, "application/json",
                                      foreign_response(false, "destination region unavailable", remote));
                        platform::close_socket(client);
                        continue;
                    }

                    if (!returning_home &&
                        !verifier_->verify_agent(circuit->home_uri, circuit->session_id,
                                                 circuit->service_session_id, reason)) {
                        send_response(client, 200, "application/json",
                                      foreign_response(false, reason, remote));
                        platform::close_socket(client);
                        continue;
                    }

                    if (returning_home) {
                        send_response(client, 200, "application/json",
                                      foreign_response(
                                          false,
                                          "return-home identity verified; legacy simulator data plane not available",
                                          remote));
                        platform::close_socket(client);
                        continue;
                    }

                    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count();
                    ForeignVisitorSession visitor{
                        .session_id = circuit->session_id,
                        .agent_id = circuit->agent_id,
                        .home_uri = circuit->home_uri,
                        .asset_uri = circuit->asset_uri,
                        .inventory_uri = circuit->inventory_uri,
                        .avatar_uri = circuit->avatar_uri,
                        .im_uri = circuit->im_uri,
                        .service_token = circuit->service_session_id,
                        .destination_region = region->id,
                        .first_name = circuit->first_name,
                        .last_name = circuit->last_name,
                        .client_ip = circuit->client_ip,
                        .secure_session_id = circuit->secure_session_id,
                        .caps_path = circuit->caps_path,
                        .base_folder = circuit->base_folder,
                        .inventory_folder = circuit->inventory_folder,
                        .start_pos = circuit->start_pos,
                        .circuit_code = circuit->circuit_code,
                        .teleport_flags = circuit->teleport_flags,
                        .verified = true,
                        .created_unix = now,
                        .expires_unix = now + 1800};
                    if (!sessions_->upsert_foreign(std::move(visitor), reason)) {
                        send_response(client, 200, "application/json",
                                      foreign_response(false, reason, remote));
                    } else {
                        send_response(client, 200, "application/json",
                                      foreign_response(
                                          false,
                                          "identity verified; legacy simulator data plane not available",
                                          remote));
                    }
                } else {
                    const auto call = parse_xmlrpc_call(request->body);
                    if (!call) {
                        send_response(client, 400, "text/xml",
                                      xmlrpc_fault_response(400, "invalid XML-RPC request"));
                        platform::close_socket(client);
                        continue;
                    }

                    auto result = home_method(*call, *sessions_);
                    if (result.empty() && call->method == "grid_instant_message") {
                        result = instant_messages_->handle_incoming(*call);
                    }
                    if (result.empty()) result = service_->handle(*call);
                    send_response(client, 200, "text/xml", xmlrpc_struct_response(result));
                }
            } catch (const std::exception& error) {
                common::log(common::LogLevel::warning, "compat.hypergrid.client", error.what());
                send_response(client, 503, "text/plain", "Hypergrid request failed");
            }
            platform::close_socket(client);
        }
    } catch (const std::exception& error) {
        if (running_) common::log(common::LogLevel::error, "compat.hypergrid", error.what());
    }
}

} // namespace opengenesis::compat::hypergrid
