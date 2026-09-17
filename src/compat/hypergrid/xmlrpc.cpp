#include "opengenesis/compat/hypergrid/xmlrpc.hpp"

#include <algorithm>
#include <sstream>

namespace opengenesis::compat::hypergrid {
namespace {

std::string xml_escape(const std::string_view value) {
    std::string output;
    output.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '&': output += "&amp;"; break;
            case '<': output += "&lt;"; break;
            case '>': output += "&gt;"; break;
            case '"': output += "&quot;"; break;
            case '\'': output += "&apos;"; break;
            default: output.push_back(c); break;
        }
    }
    return output;
}

std::string xml_unescape(std::string value) {
    const std::pair<std::string_view, std::string_view> entities[] = {
        {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&apos;", "'"}, {"&amp;", "&"}};
    for (const auto& [from, to] : entities) {
        std::size_t position = 0;
        while ((position = value.find(from, position)) != std::string::npos) {
            value.replace(position, from.size(), to);
            position += to.size();
        }
    }
    return value;
}

std::optional<std::string> tag_text(const std::string_view xml,
                                    const std::string_view tag,
                                    const std::size_t start = 0) {
    const std::string open = "<" + std::string{tag} + ">";
    const std::string close = "</" + std::string{tag} + ">";
    const auto begin = xml.find(open, start);
    if (begin == std::string_view::npos) return std::nullopt;
    const auto content = begin + open.size();
    const auto end = xml.find(close, content);
    if (end == std::string_view::npos) return std::nullopt;
    return xml_unescape(std::string{xml.substr(content, end - content)});
}

std::string value_text(const std::string_view member) {
    for (const std::string_view type : {"string", "i4", "int", "boolean", "double"}) {
        if (const auto value = tag_text(member, type)) return *value;
    }
    if (const auto value = tag_text(member, "value")) {
        if (value->find('<') == std::string::npos) return *value;
    }
    return {};
}

} // namespace

std::optional<XmlRpcCall> parse_xmlrpc_call(const std::string_view xml) {
    const auto method = tag_text(xml, "methodName");
    if (!method || method->empty() || method->size() > 128) return std::nullopt;

    XmlRpcCall call{.method = *method, .fields = {}};
    std::size_t cursor = 0;
    while (true) {
        const auto begin = xml.find("<member>", cursor);
        if (begin == std::string_view::npos) break;
        const auto end = xml.find("</member>", begin);
        if (end == std::string_view::npos) return std::nullopt;
        const auto member = xml.substr(begin, end + 9 - begin);
        const auto name = tag_text(member, "name");
        if (name && !name->empty() && name->size() <= 128) {
            call.fields[*name] = value_text(member);
        }
        cursor = end + 9;
    }

    return call;
}

std::string xmlrpc_struct_response(
    const std::unordered_map<std::string, std::string>& fields) {
    std::vector<std::pair<std::string, std::string>> ordered(fields.begin(), fields.end());
    std::sort(ordered.begin(), ordered.end());

    std::ostringstream output;
    output << "<?xml version=\"1.0\"?><methodResponse><params><param><value><struct>";
    for (const auto& [name, value] : ordered) {
        output << "<member><name>" << xml_escape(name)
               << "</name><value><string>" << xml_escape(value)
               << "</string></value></member>";
    }
    output << "</struct></value></param></params></methodResponse>";
    return output.str();
}

std::string xmlrpc_fault_response(const int code, const std::string_view message) {
    return "<?xml version=\"1.0\"?><methodResponse><fault><value><struct>"
           "<member><name>faultCode</name><value><int>" + std::to_string(code) +
           "</int></value></member><member><name>faultString</name><value><string>" +
           xml_escape(message) +
           "</string></value></member></struct></value></fault></methodResponse>";
}

} // namespace opengenesis::compat::hypergrid
