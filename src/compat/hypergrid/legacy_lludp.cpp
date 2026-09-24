#include "opengenesis/compat/hypergrid/legacy_lludp.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>

namespace opengenesis::compat::hypergrid::lludp {
namespace {

constexpr std::uint8_t kAppendedAcks = 0x10U;
constexpr std::uint8_t kResent = 0x20U;
constexpr std::uint8_t kReliable = 0x40U;
constexpr std::uint8_t kZerocoded = 0x80U;

std::uint32_t read_u32_be(const std::span<const std::uint8_t> data,
                          const std::size_t offset) {
    return (static_cast<std::uint32_t>(data[offset]) << 24U) |
           (static_cast<std::uint32_t>(data[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(data[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(data[offset + 3U]);
}

std::uint32_t read_u32_le(const std::span<const std::uint8_t> data,
                          const std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(data[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(data[offset + 3U]) << 24U);
}

void append_u32_be(std::vector<std::uint8_t>& out, const std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void append_u32_le(std::vector<std::uint8_t>& out, const std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
}

std::vector<std::uint8_t> low_header(const std::uint8_t flags,
                                     const std::uint32_t sequence,
                                     const std::uint16_t id) {
    std::vector<std::uint8_t> out;
    out.reserve(10U);
    out.push_back(flags);
    append_u32_be(out, sequence);
    out.push_back(0U);
    out.push_back(0xffU);
    out.push_back(0xffU);
    out.push_back(static_cast<std::uint8_t>((id >> 8U) & 0xffU));
    out.push_back(static_cast<std::uint8_t>(id & 0xffU));
    return out;
}

std::vector<std::uint8_t> high_header(const std::uint8_t flags,
                                      const std::uint32_t sequence,
                                      const std::uint8_t id) {
    std::vector<std::uint8_t> out;
    out.reserve(7U);
    out.push_back(flags);
    append_u32_be(out, sequence);
    out.push_back(0U);
    out.push_back(id);
    return out;
}

bool hex_value(const char c, std::uint8_t& value) {
    if (c >= '0' && c <= '9') {
        value = static_cast<std::uint8_t>(c - '0');
        return true;
    }
    if (c >= 'a' && c <= 'f') {
        value = static_cast<std::uint8_t>(10 + c - 'a');
        return true;
    }
    if (c >= 'A' && c <= 'F') {
        value = static_cast<std::uint8_t>(10 + c - 'A');
        return true;
    }
    return false;
}

std::optional<CircuitHandshake> parse_circuit_body(
    const std::span<const std::uint8_t> packet,
    const PacketHeader& header,
    std::string& reason) {
    constexpr std::size_t kBodySize = 4U + 16U + 16U;
    if (header.body_end < header.body_offset ||
        header.body_end - header.body_offset != kBodySize) {
        reason = "legacy-circuit-body-size-invalid";
        return std::nullopt;
    }

    CircuitHandshake circuit;
    circuit.circuit_code = read_u32_le(packet, header.body_offset);
    const auto session = uuid_from_network_bytes(
        packet.subspan(header.body_offset + 4U, 16U));
    const auto agent = uuid_from_network_bytes(
        packet.subspan(header.body_offset + 20U, 16U));
    if (!session || !agent || circuit.circuit_code == 0U) {
        reason = "legacy-circuit-fields-invalid";
        return std::nullopt;
    }
    circuit.session_id = *session;
    circuit.agent_id = *agent;
    reason.clear();
    return circuit;
}

} // namespace

std::optional<PacketHeader> parse_header(
    const std::span<const std::uint8_t> packet,
    std::string& reason) {
    if (packet.size() < 7U) {
        reason = "legacy-udp-header-too-short";
        return std::nullopt;
    }

    PacketHeader header;
    const auto flags = packet[0];
    header.appended_acks = (flags & kAppendedAcks) != 0U;
    header.resent = (flags & kResent) != 0U;
    header.reliable = (flags & kReliable) != 0U;
    header.zerocoded = (flags & kZerocoded) != 0U;
    header.sequence = read_u32_be(packet, 1U);

    const std::size_t extra = packet[5];
    const std::size_t base = extra;
    if (base > packet.size() || packet.size() - base < 7U) {
        reason = "legacy-udp-extra-header-invalid";
        return std::nullopt;
    }

    if (packet[base + 6U] != 0xffU) {
        header.frequency = Frequency::high;
        header.id = packet[base + 6U];
        header.body_offset = base + 7U;
    } else {
        if (packet.size() - base < 8U) {
            reason = "legacy-udp-frequency-header-truncated";
            return std::nullopt;
        }
        if (packet[base + 7U] != 0xffU) {
            header.frequency = Frequency::medium;
            header.id = packet[base + 7U];
            header.body_offset = base + 8U;
        } else {
            if (packet.size() - base < 10U) {
                reason = "legacy-udp-low-header-truncated";
                return std::nullopt;
            }
            header.frequency = Frequency::low;
            header.id =
                static_cast<std::uint16_t>(
                    (static_cast<std::uint16_t>(packet[base + 8U]) << 8U) |
                    packet[base + 9U]);
            header.body_offset = base + 10U;
        }
    }

    header.body_end = packet.size();
    if (header.appended_acks) {
        if (header.body_end == 0U) {
            reason = "legacy-udp-ack-footer-invalid";
            return std::nullopt;
        }
        const std::size_t count = packet[header.body_end - 1U];
        const std::size_t bytes = count * 4U + 1U;
        if (bytes > header.body_end ||
            header.body_end - bytes < header.body_offset) {
            reason = "legacy-udp-ack-footer-invalid";
            return std::nullopt;
        }
        header.body_end -= bytes;
    }

    if (header.body_offset > header.body_end) {
        reason = "legacy-udp-body-invalid";
        return std::nullopt;
    }
    reason.clear();
    return header;
}

std::optional<CircuitHandshake> parse_use_circuit_code(
    const std::span<const std::uint8_t> packet,
    std::string& reason) {
    const auto header = parse_header(packet, reason);
    if (!header) return std::nullopt;
    if (header->zerocoded || header->frequency != Frequency::low ||
        header->id != 3U) {
        reason = "not-use-circuit-code";
        return std::nullopt;
    }
    return parse_circuit_body(packet, *header, reason);
}

std::optional<CircuitHandshake> parse_complete_agent_movement(
    const std::span<const std::uint8_t> packet,
    std::string& reason) {
    const auto header = parse_header(packet, reason);
    if (!header) return std::nullopt;
    if (header->zerocoded || header->frequency != Frequency::low ||
        header->id != 249U) {
        reason = "not-complete-agent-movement";
        return std::nullopt;
    }

    constexpr std::size_t kBodySize = 16U + 16U + 4U;
    if (header->body_end < header->body_offset ||
        header->body_end - header->body_offset != kBodySize) {
        reason = "complete-agent-movement-size-invalid";
        return std::nullopt;
    }

    CircuitHandshake circuit;
    const auto agent = uuid_from_network_bytes(
        packet.subspan(header->body_offset, 16U));
    const auto session = uuid_from_network_bytes(
        packet.subspan(header->body_offset + 16U, 16U));
    circuit.circuit_code = read_u32_le(packet, header->body_offset + 32U);
    if (!agent || !session || circuit.circuit_code == 0U) {
        reason = "complete-agent-movement-fields-invalid";
        return std::nullopt;
    }
    circuit.agent_id = *agent;
    circuit.session_id = *session;
    reason.clear();
    return circuit;
}

std::vector<std::uint8_t> build_packet_ack(const std::uint32_t sequence) {
    auto out = low_header(0U, 0U, 0xfffbU);
    out.push_back(1U);
    append_u32_le(out, sequence);
    return out;
}

std::vector<std::uint8_t> build_complete_ping_check(
    const std::uint32_t sequence,
    const std::uint8_t ping_id) {
    auto out = high_header(0U, sequence, 2U);
    out.push_back(ping_id);
    return out;
}

std::vector<std::uint8_t> zero_encode(
    const std::span<const std::uint8_t> packet) {
    if (packet.size() <= 6U) {
        return {packet.begin(), packet.end()};
    }
    std::vector<std::uint8_t> out;
    out.reserve(packet.size());
    out.insert(out.end(), packet.begin(), packet.begin() + 6);
    std::uint16_t zero_count = 0U;
    const auto flush = [&out, &zero_count]() {
        while (zero_count != 0U) {
            const auto chunk = static_cast<std::uint8_t>(
                std::min<std::uint16_t>(zero_count, 255U));
            out.push_back(0U);
            out.push_back(chunk);
            zero_count = static_cast<std::uint16_t>(zero_count - chunk);
        }
    };
    for (std::size_t i = 6U; i < packet.size(); ++i) {
        if (packet[i] == 0U) {
            ++zero_count;
        } else {
            flush();
            out.push_back(packet[i]);
        }
    }
    flush();
    return out;
}

std::optional<std::string> uuid_from_network_bytes(
    const std::span<const std::uint8_t> bytes) {
    if (bytes.size() != 16U) return std::nullopt;
    constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(36U);
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        if (i == 4U || i == 6U || i == 8U || i == 10U) out.push_back('-');
        out.push_back(hex[(bytes[i] >> 4U) & 0x0fU]);
        out.push_back(hex[bytes[i] & 0x0fU]);
    }
    return out;
}

std::optional<std::array<std::uint8_t, 16>> uuid_to_network_bytes(
    const std::string_view uuid) {
    if (uuid.size() != 36U ||
        uuid[8] != '-' || uuid[13] != '-' ||
        uuid[18] != '-' || uuid[23] != '-') {
        return std::nullopt;
    }

    std::array<std::uint8_t, 16> out{};
    std::size_t source = 0U;
    std::size_t target = 0U;
    while (source < uuid.size()) {
        if (uuid[source] == '-') {
            ++source;
            continue;
        }
        if (source + 1U >= uuid.size() || target >= out.size()) {
            return std::nullopt;
        }
        std::uint8_t high = 0U;
        std::uint8_t low = 0U;
        if (!hex_value(uuid[source], high) ||
            !hex_value(uuid[source + 1U], low)) {
            return std::nullopt;
        }
        out[target++] =
            static_cast<std::uint8_t>((high << 4U) | low);
        source += 2U;
    }
    if (target != out.size()) return std::nullopt;
    return out;
}

} // namespace opengenesis::compat::hypergrid::lludp
