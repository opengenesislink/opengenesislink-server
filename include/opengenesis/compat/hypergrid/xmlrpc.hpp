#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace opengenesis::compat::hypergrid {

struct XmlRpcCall {
    std::string method;
    std::unordered_map<std::string, std::string> fields;
};

[[nodiscard]] std::optional<XmlRpcCall> parse_xmlrpc_call(std::string_view xml);
[[nodiscard]] std::string xmlrpc_struct_response(
    const std::unordered_map<std::string, std::string>& fields);
[[nodiscard]] std::string xmlrpc_fault_response(int code, std::string_view message);

} // namespace opengenesis::compat::hypergrid
