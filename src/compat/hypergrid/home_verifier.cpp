#include "opengenesis/compat/hypergrid/home_verifier.hpp"

#include "opengenesis/compat/hypergrid/http_client.hpp"
#include "opengenesis/compat/hypergrid/xmlrpc.hpp"

#include <string>
#include <string_view>
#include <unordered_map>

namespace opengenesis::compat::hypergrid {
namespace {

bool bool_text(const std::string_view value) {
    return value == "true" || value == "True" || value == "TRUE" || value == "1";
}

} // namespace

bool HttpHypergridHomeVerifier::verify_agent(
    const std::string_view home_uri,
    const std::string_view session_id,
    const std::string_view service_token,
    std::string& reason) {
    return call_bool(home_uri, "verify_agent", session_id, "token", service_token, reason);
}

bool HttpHypergridHomeVerifier::verify_client(
    const std::string_view home_uri,
    const std::string_view session_id,
    const std::string_view reported_ip,
    std::string& reason) {
    return call_bool(home_uri, "verify_client", session_id, "token", reported_ip, reason);
}

bool HttpHypergridHomeVerifier::call_bool(
    const std::string_view home_uri,
    const std::string_view method,
    const std::string_view session_id,
    const std::string_view value_name,
    const std::string_view value,
    std::string& reason) {
    const auto body = xmlrpc_struct_call(
        method, {{"sessionID", std::string{session_id}},
                 {std::string{value_name}, std::string{value}}});

    auto response = http_request(
        home_uri, "POST", "text/xml", body,
        std::unordered_map<std::string, std::string>{}, reason);
    if (!response || response->status != 200) {
        if (reason.empty()) reason = "home-verification-http-failed";
        return false;
    }

    const auto fields = parse_xmlrpc_struct_response(response->body);
    if (!fields) {
        reason = "home-verification-invalid-response";
        return false;
    }
    const auto it = fields->find("result");
    if (it == fields->end() || !bool_text(it->second)) {
        reason = "home-verification-rejected";
        return false;
    }
    reason.clear();
    return true;
}

} // namespace opengenesis::compat::hypergrid
