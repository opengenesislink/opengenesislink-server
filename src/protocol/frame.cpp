#include "opengenesis/protocol/frame.hpp"
#include <algorithm>
#include <stdexcept>
namespace opengenesis::protocol {
namespace {
void put16(std::vector<std::byte>& out, const std::uint16_t v) { out.push_back(std::byte((v >> 8U) & 0xffU)); out.push_back(std::byte(v & 0xffU)); }
void put32(std::vector<std::byte>& out, const std::uint32_t v) { for (int s=24;s>=0;s-=8) out.push_back(std::byte((v >> static_cast<unsigned>(s)) & 0xffU)); }
std::uint16_t get16(std::span<const std::byte> in, const std::size_t p) { return static_cast<std::uint16_t>((std::to_integer<unsigned>(in[p])<<8U)|std::to_integer<unsigned>(in[p+1])); }
std::uint32_t get32(std::span<const std::byte> in, const std::size_t p) { std::uint32_t v=0; for(std::size_t i=0;i<4;++i) v=(v<<8U)|std::to_integer<unsigned>(in[p+i]); return v; }
}
std::vector<std::byte> encode(const Frame& frame) {
    if (frame.payload.size() > kMaxPayloadSize) throw std::runtime_error("Frame payload too large");
    std::vector<std::byte> out; out.reserve(kHeaderSize + frame.payload.size());
    out.insert(out.end(), kMagic.begin(), kMagic.end()); put16(out,kProtocolVersion); put16(out,static_cast<std::uint16_t>(frame.type)); put32(out,frame.request_id); put32(out,static_cast<std::uint32_t>(frame.payload.size())); out.insert(out.end(),frame.payload.begin(),frame.payload.end()); return out;
}
Frame decode(const std::span<const std::byte> bytes) {
    if (bytes.size() < kHeaderSize) throw std::runtime_error("Short OGL frame");
    if (!std::equal(kMagic.begin(),kMagic.end(),bytes.begin())) throw std::runtime_error("Invalid OGL magic");
    if (get16(bytes,4) != kProtocolVersion) throw std::runtime_error("Unsupported OGL protocol version");
    const auto size = get32(bytes,12); if (size > kMaxPayloadSize || bytes.size() != kHeaderSize + size) throw std::runtime_error("Invalid OGL payload size");
    return {static_cast<MessageType>(get16(bytes,6)), get32(bytes,8), std::vector<std::byte>(bytes.begin()+static_cast<std::ptrdiff_t>(kHeaderSize),bytes.end())};
}
std::string payload_as_string(const Frame& frame) { return {reinterpret_cast<const char*>(frame.payload.data()), frame.payload.size()}; }
std::vector<std::byte> payload_from_string(const std::string_view value) { const auto* b=reinterpret_cast<const std::byte*>(value.data()); return {b,b+value.size()}; }
}
