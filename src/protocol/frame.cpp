#include "opengenesis/protocol/frame.hpp"

#include <stdexcept>

namespace opengenesis::protocol {
namespace {

void put_u16(std::vector<std::byte>& out, const std::uint16_t value) {
    out.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::byte>(value & 0xFFU));
}

void put_u32(std::vector<std::byte>& out, const std::uint32_t value) {
    out.push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
    out.push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
    out.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    out.push_back(static_cast<std::byte>(value & 0xFFU));
}

std::uint16_t get_u16(const std::span<const std::byte> in, const std::size_t offset) {
    return static_cast<std::uint16_t>(
        (std::to_integer<std::uint16_t>(in[offset]) << 8U) |
        std::to_integer<std::uint16_t>(in[offset + 1]));
}

std::uint32_t get_u32(const std::span<const std::byte> in, const std::size_t offset) {
    return (std::to_integer<std::uint32_t>(in[offset]) << 24U) |
           (std::to_integer<std::uint32_t>(in[offset + 1]) << 16U) |
           (std::to_integer<std::uint32_t>(in[offset + 2]) << 8U) |
           std::to_integer<std::uint32_t>(in[offset + 3]);
}

} // namespace

std::vector<std::byte> encode(const Frame& frame) {
    if (frame.payload.size() > kMaxPayloadSize) {
        throw std::runtime_error("OGL frame payload exceeds maximum size");
    }
    std::vector<std::byte> output;
    output.reserve(kHeaderSize + frame.payload.size());
    output.insert(output.end(), kMagic.begin(), kMagic.end());
    put_u16(output, kProtocolVersion);
    put_u16(output, static_cast<std::uint16_t>(frame.type));
    put_u32(output, static_cast<std::uint32_t>(frame.payload.size()));
    put_u32(output, frame.request_id);
    output.insert(output.end(), frame.payload.begin(), frame.payload.end());
    return output;
}

Frame decode(const std::span<const std::byte> bytes) {
    if (bytes.size() < kHeaderSize) {
        throw std::runtime_error("Incomplete OGL frame header");
    }
    for (std::size_t i = 0; i < kMagic.size(); ++i) {
        if (bytes[i] != kMagic[i]) {
            throw std::runtime_error("Invalid OGL frame magic");
        }
    }
    const auto version = get_u16(bytes, 4);
    if (version != kProtocolVersion) {
        throw std::runtime_error("Unsupported OGL protocol version");
    }
    const auto payload_size = get_u32(bytes, 8);
    if (payload_size > kMaxPayloadSize) {
        throw std::runtime_error("OGL frame payload exceeds maximum size");
    }
    if (bytes.size() != kHeaderSize + payload_size) {
        throw std::runtime_error("OGL frame size mismatch");
    }

    Frame frame;
    frame.type = static_cast<MessageType>(get_u16(bytes, 6));
    frame.request_id = get_u32(bytes, 12);
    frame.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(kHeaderSize), bytes.end());
    return frame;
}

std::string payload_as_string(const Frame& frame) {
    std::string value;
    value.reserve(frame.payload.size());
    for (const auto byte : frame.payload) {
        value.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return value;
}

std::vector<std::byte> payload_from_string(const std::string_view value) {
    std::vector<std::byte> payload;
    payload.reserve(value.size());
    for (const auto ch : value) {
        payload.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return payload;
}

} // namespace opengenesis::protocol
