#include "opengenesis/security/crypto.hpp"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <charconv>
#include <span>
#include <stdexcept>
#include <vector>

namespace opengenesis::security {
namespace {

constexpr char kHex[] = "0123456789abcdef";

std::string hex_encode(const unsigned char* data, const std::size_t size) {
    std::string output(size * 2, '0');
    for (std::size_t i = 0; i < size; ++i) {
        output[i * 2] = kHex[(data[i] >> 4U) & 0x0FU];
        output[i * 2 + 1] = kHex[data[i] & 0x0FU];
    }
    return output;
}

unsigned char hex_nibble(const char value) {
    if (value >= '0' && value <= '9') return static_cast<unsigned char>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<unsigned char>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return static_cast<unsigned char>(value - 'A' + 10);
    throw std::runtime_error("invalid hex value");
}

std::vector<unsigned char> hex_decode(const std::string_view value) {
    if ((value.size() % 2) != 0) throw std::runtime_error("invalid hex length");
    std::vector<unsigned char> output(value.size() / 2);
    for (std::size_t i = 0; i < output.size(); ++i) {
        output[i] = static_cast<unsigned char>((hex_nibble(value[i * 2]) << 4U) |
                                               hex_nibble(value[i * 2 + 1]));
    }
    return output;
}

std::array<unsigned char, 32> pbkdf2(const std::string_view password,
                                     const std::span<const unsigned char> salt,
                                     const std::uint32_t iterations) {
    std::array<unsigned char, 32> output{};
    if (iterations < 10000 || iterations > 5000000) {
        throw std::runtime_error("PBKDF2 iteration count outside supported range");
    }
    const auto ok = PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                                      salt.data(), static_cast<int>(salt.size()),
                                      static_cast<int>(iterations), EVP_sha256(),
                                      static_cast<int>(output.size()), output.data());
    if (ok != 1) throw std::runtime_error("PBKDF2 failed");
    return output;
}

} // namespace

std::string random_hex(const std::size_t bytes) {
    if (bytes == 0 || bytes > 1024) throw std::runtime_error("invalid random byte count");
    std::vector<unsigned char> buffer(bytes);
    if (RAND_bytes(buffer.data(), static_cast<int>(buffer.size())) != 1) {
        throw std::runtime_error("secure random generation failed");
    }
    return hex_encode(buffer.data(), buffer.size());
}

std::string sha256_hex(const std::string_view value) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int length = 0;
    EVP_MD_CTX* raw = EVP_MD_CTX_new();
    if (!raw) throw std::runtime_error("EVP_MD_CTX_new failed");
    struct Guard {
        EVP_MD_CTX* ptr;
        ~Guard() { EVP_MD_CTX_free(ptr); }
    } guard{raw};
    if (EVP_DigestInit_ex(raw, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(raw, value.data(), value.size()) != 1 ||
        EVP_DigestFinal_ex(raw, digest.data(), &length) != 1) {
        throw std::runtime_error("SHA-256 failed");
    }
    return hex_encode(digest.data(), length);
}

std::string hash_password(const std::string_view password, const std::uint32_t iterations) {
    if (password.size() < 8 || password.size() > 1024) {
        throw std::runtime_error("password must contain 8 to 1024 bytes");
    }
    const auto salt_hex = random_hex(16);
    const auto salt = hex_decode(salt_hex);
    const auto digest = pbkdf2(password, salt, iterations);
    return "$pbkdf2-sha256$" + std::to_string(iterations) + "$" + salt_hex + "$" +
           hex_encode(digest.data(), digest.size());
}

bool verify_password(const std::string_view password, const std::string_view encoded_hash) {
    constexpr std::string_view prefix = "$pbkdf2-sha256$";
    if (!encoded_hash.starts_with(prefix)) return false;
    const auto iterations_end = encoded_hash.find('$', prefix.size());
    if (iterations_end == std::string_view::npos) return false;
    const auto salt_end = encoded_hash.find('$', iterations_end + 1);
    if (salt_end == std::string_view::npos) return false;

    std::uint32_t iterations = 0;
    const auto iter_text = encoded_hash.substr(prefix.size(), iterations_end - prefix.size());
    const auto [end, ec] = std::from_chars(iter_text.data(), iter_text.data() + iter_text.size(), iterations);
    if (ec != std::errc{} || end != iter_text.data() + iter_text.size()) return false;

    try {
        const auto salt = hex_decode(encoded_hash.substr(iterations_end + 1,
                                                          salt_end - iterations_end - 1));
        const auto expected = hex_decode(encoded_hash.substr(salt_end + 1));
        if (expected.size() != 32) return false;
        const auto actual = pbkdf2(password, salt, iterations);
        return CRYPTO_memcmp(actual.data(), expected.data(), actual.size()) == 0;
    } catch (...) {
        return false;
    }
}

} // namespace opengenesis::security
