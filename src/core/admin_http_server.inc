AdminHttpServer::AdminHttpServer(std::string address, const std::uint16_t port,
                                 std::shared_ptr<WorldRegistry> worlds,
                                 std::shared_ptr<RegionRegistry> regions,
                                 std::shared_ptr<IdentityStore> identities,
                                 std::shared_ptr<SessionStore> sessions)
    : address_(std::move(address)), port_(port), worlds_(std::move(worlds)), regions_(std::move(regions)),
      identities_(std::move(identities)), sessions_(std::move(sessions)) {}

AdminHttpServer::~AdminHttpServer() { stop(); }

void AdminHttpServer::start() {
    if (running_.exchange(true)) return;
    started_at_ = std::chrono::steady_clock::now();
    thread_ = std::thread(&AdminHttpServer::run, this);
}

void AdminHttpServer::stop() {
    if (!running_.exchange(false)) return;
    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }
    if (thread_.joinable()) thread_.join();
}

void AdminHttpServer::run() {
    try {
        addrinfo hints{};
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_family = AF_UNSPEC;
        hints.ai_flags = AI_PASSIVE;
        addrinfo* results = nullptr;
        const auto port_text = std::to_string(port_);
        if (::getaddrinfo(address_.c_str(), port_text.c_str(), &hints, &results) != 0) {
            throw std::runtime_error("admin getaddrinfo failed");
        }
        for (auto* current = results; current != nullptr; current = current->ai_next) {
            listen_fd_ = ::socket(current->ai_family, current->ai_socktype, current->ai_protocol);
            if (listen_fd_ < 0) continue;
            const int reuse = 1;
            (void)::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
            if (::bind(listen_fd_, current->ai_addr, current->ai_addrlen) == 0 &&
                ::listen(listen_fd_, 64) == 0) break;
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        ::freeaddrinfo(results);
        if (listen_fd_ < 0) throw std::runtime_error("admin bind failed");
        common::log(common::LogLevel::info, "core.admin",
                    "Web/API interface listening on http://" + address_ + ':' + port_text);

        while (running_) {
            const int fd = ::accept(listen_fd_, nullptr, nullptr);
            if (fd < 0) {
                if (!running_) break;
                if (errno == EINTR) continue;
                continue;
            }
            try {
                const auto request = read_request(fd);
                if (!request) {
                    send_response(fd, {.status = 400, .body = "{\"error\":\"bad-request\"}"});
                    ::close(fd);
                    continue;
                }

                HttpResponse response;
                const auto& method = request->method;
                const auto& path = request->path;
                if (method == "GET" && path == "/") {
                    response.content_type = "text/html; charset=utf-8";
                    response.body = dashboard_html();
                } else if (method == "GET" && path == "/health") {
                    response.body = "{\"status\":\"ok\",\"version\":\"" + std::string(OGL_VERSION) + "\"}";
                } else if (method == "GET" && path == "/v1") {
                    response.body = R"JSON({"service":"OpenGenesisLINK Core","version":")JSON" +
                                    std::string(OGL_VERSION) +
                                    R"JSON(","api_version":1,"endpoints":{"status":"/v1/status","worlds":"/v1/worlds","regions":"/v1/regions","identity_stats":"/v1/identity/stats","register":"POST /v1/auth/register","login":"POST /v1/auth/login","me":"GET /v1/auth/me","logout":"POST /v1/auth/logout"}})JSON";
                } else if (method == "GET" && path == "/v1/worlds") {
                    response.body = "{\"world_nodes\":" + worlds_json(worlds_->list()) + "}";
                } else if (method == "GET" && path == "/v1/regions") {
                    response.body = "{\"regions\":" + regions_json(regions_->list()) + "}";
                } else if (method == "GET" && path == "/v1/identity/stats") {
                    response.body = "{\"users\":" + std::to_string(identities_->count()) +
                                    ",\"active_sessions\":" + std::to_string(sessions_->active_count()) + "}";
                } else if (method == "GET" && path == "/v1/identity/users") {
                    response.body = "{\"users\":" + users_json(identities_->list()) + "}";
                } else if (method == "GET" && path == "/v1/status") {
                    const auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                                            std::chrono::steady_clock::now() - started_at_)
                                            .count();
                    response.body = "{\"version\":\"" + std::string(OGL_VERSION) +
                                    "\",\"uptime_seconds\":" + std::to_string(uptime) +
                                    ",\"world_nodes\":" + worlds_json(worlds_->list()) +
                                    ",\"regions\":" + regions_json(regions_->list()) +
                                    ",\"identities\":" + std::to_string(identities_->count()) +
                                    ",\"active_sessions\":" + std::to_string(sessions_->active_count()) + "}";
                } else if (method == "POST" && path == "/v1/auth/register") {
                    const auto username = json_string(request->body, "username");
                    const auto display_name = json_string(request->body, "display_name");
                    const auto password = json_string(request->body, "password");
                    if (!username || !display_name || !password) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-json-fields\"}"};
                    } else {
                        std::string reason;
                        const auto user = identities_->register_user(*username, *display_name, *password, reason);
                        if (!user) {
                            response = {.status = reason == "username-exists" ? 409 : 400,
                                        .body = "{\"error\":\"" + json_escape(reason) + "\"}"};
                        } else {
                            const auto created = sessions_->create(user->id);
                            response.status = 201;
                            response.body = "{\"user\":{\"id\":\"" + json_escape(user->id) +
                                            "\",\"username\":\"" + json_escape(user->username) +
                                            "\",\"display_name\":\"" + json_escape(user->display_name) +
                                            "\"},\"token\":\"" + created.token + "\",\"expires_unix\":" +
                                            std::to_string(created.session.expires_unix) + "}";
                        }
                    }
                } else if (method == "POST" && path == "/v1/auth/login") {
                    const auto username = json_string(request->body, "username");
                    const auto password = json_string(request->body, "password");
                    if (!username || !password) {
                        response = {.status = 400, .body = "{\"error\":\"invalid-json-fields\"}"};
                    } else if (const auto user = identities_->authenticate(*username, *password)) {
                        const auto created = sessions_->create(user->id);
                        response.body = "{\"user\":{\"id\":\"" + json_escape(user->id) +
                                        "\",\"username\":\"" + json_escape(user->username) +
                                        "\",\"display_name\":\"" + json_escape(user->display_name) +
                                        "\"},\"token\":\"" + created.token + "\",\"expires_unix\":" +
                                        std::to_string(created.session.expires_unix) + "}";
                    } else {
                        response = {.status = 401, .body = "{\"error\":\"invalid-credentials\"}"};
                    }
                } else if (method == "GET" && path == "/v1/auth/me") {
                    const auto token = bearer_token(*request);
                    const auto session = sessions_->find(token);
                    const auto user = session ? identities_->find_by_id(session->user_id) : std::nullopt;
                    if (!session || !user) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else {
                        response.body = "{\"user\":{\"id\":\"" + json_escape(user->id) +
                                        "\",\"username\":\"" + json_escape(user->username) +
                                        "\",\"display_name\":\"" + json_escape(user->display_name) +
                                        "\"},\"session\":{\"id\":\"" + json_escape(session->id) +
                                        "\",\"expires_unix\":" + std::to_string(session->expires_unix) + "}}";
                    }
                } else if (method == "POST" && path == "/v1/auth/logout") {
                    const auto token = bearer_token(*request);
                    if (token.empty() || !sessions_->revoke(token)) {
                        response = {.status = 401, .body = "{\"error\":\"invalid-session\"}"};
                    } else {
                        response.body = "{\"status\":\"logged-out\"}";
                    }
                } else if (path == "/v1/auth/register" || path == "/v1/auth/login" ||
                           path == "/v1/auth/logout") {
                    response = {.status = 405, .body = "{\"error\":\"method-not-allowed\"}"};
                } else {
                    response = {.status = 404, .body = "{\"error\":\"not-found\"}"};
                }
                send_response(fd, response);
            } catch (const std::exception& error) {
                common::log(common::LogLevel::warning, "core.admin.client", error.what());
                send_response(fd, {.status = 500, .body = "{\"error\":\"internal-error\"}"});
            }
            ::close(fd);
        }
    } catch (const std::exception& error) {
        if (running_) common::log(common::LogLevel::error, "core.admin", error.what());
    }
}

} // namespace opengenesis::core
