#pragma once

#include <string>
#include <string_view>

namespace opengenesis::compat::hypergrid {

class IHypergridHomeVerifier {
public:
    virtual ~IHypergridHomeVerifier() = default;

    [[nodiscard]] virtual bool verify_agent(std::string_view home_uri,
                                            std::string_view session_id,
                                            std::string_view service_token,
                                            std::string& reason) = 0;
    [[nodiscard]] virtual bool verify_client(std::string_view home_uri,
                                             std::string_view session_id,
                                             std::string_view reported_ip,
                                             std::string& reason) = 0;
};

class HttpHypergridHomeVerifier final : public IHypergridHomeVerifier {
public:
    [[nodiscard]] bool verify_agent(std::string_view home_uri,
                                    std::string_view session_id,
                                    std::string_view service_token,
                                    std::string& reason) override;
    [[nodiscard]] bool verify_client(std::string_view home_uri,
                                     std::string_view session_id,
                                     std::string_view reported_ip,
                                     std::string& reason) override;

private:
    [[nodiscard]] bool call_bool(std::string_view home_uri,
                                 std::string_view method,
                                 std::string_view session_id,
                                 std::string_view value_name,
                                 std::string_view value,
                                 std::string& reason);
};

} // namespace opengenesis::compat::hypergrid
