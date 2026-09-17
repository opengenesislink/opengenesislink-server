#include "opengenesis/federation/grid_identity.hpp"

#include <openssl/evp.h>

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace opengenesis::federation {
namespace {

constexpr char kHex[] = "0123456789abcdef";

std::string hex_encode(const unsigned char* data, const std::size_t size) {
    std::string output(size * 2, '0');
    for (std::size_t i = 0; i < size; ++i) {
        output[i * 2] = kHex[(data[i] >> 4U) & 0x0fU];
        output[i * 2 + 1] = kHex[data[i] & 0x0fU];
    }
    return output;
}

unsigned char nibble(const char value) {
    if (value >= '0' && value <= '9') return static_cast<unsigned char>(value - '0');
    if (value >= 'a' && value <= 'f') return static_cast<unsigned char>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return static_cast<unsigned char>(value - 'A' + 10);
    throw std::runtime_error("invalid hexadecimal key");
}

std::vector<unsigned char> hex_decode(const std::string_view value) {
    if ((value.size() % 2) != 0) throw std::runtime_error("invalid hexadecimal key length");
    std::vector<unsigned char> output(value.size() / 2);
    for (std::size_t i = 0; i < output.size(); ++i) {
        output[i] = static_cast<unsigned char>((nibble(value[i * 2]) << 4U) |
                                               nibble(value[i * 2 + 1]));
    }
    return output;
}

struct PkeyDeleter {
    void operator()(EVP_PKEY* key) const noexcept { EVP_PKEY_free(key); }
};
struct PkeyCtxDeleter {
    void operator()(EVP_PKEY_CTX* ctx) const noexcept { EVP_PKEY_CTX_free(ctx); }
};
struct MdCtxDeleter {
    void operator()(EVP_MD_CTX* ctx) const noexcept { EVP_MD_CTX_free(ctx); }
};

using PkeyPtr = std::unique_ptr<EVP_PKEY, PkeyDeleter>;
using PkeyCtxPtr = std::unique_ptr<EVP_PKEY_CTX, PkeyCtxDeleter>;
using MdCtxPtr = std::unique_ptr<EVP_MD_CTX, MdCtxDeleter>;

} // namespace

GridKeyPair generate_grid_key_pair() {
    PkeyCtxPtr context(EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr));
    if (!context || EVP_PKEY_keygen_init(context.get()) != 1) {
        throw std::runtime_error("cannot initialize Ed25519 key generation");
    }

    EVP_PKEY* raw_key = nullptr;
    if (EVP_PKEY_keygen(context.get(), &raw_key) != 1 || !raw_key) {
        throw std::runtime_error("Ed25519 key generation failed");
    }
    PkeyPtr key(raw_key);

    std::array<unsigned char, 32> public_key{};
    std::array<unsigned char, 32> private_key{};
    std::size_t public_size = public_key.size();
    std::size_t private_size = private_key.size();

    if (EVP_PKEY_get_raw_public_key(key.get(), public_key.data(), &public_size) != 1 ||
        EVP_PKEY_get_raw_private_key(key.get(), private_key.data(), &private_size) != 1 ||
        public_size != public_key.size() || private_size != private_key.size()) {
        throw std::runtime_error("cannot export Ed25519 grid key");
    }

    return {.public_key_hex = hex_encode(public_key.data(), public_key.size()),
            .private_key_hex = hex_encode(private_key.data(), private_key.size())};
}

std::string sign_ed25519(const std::string_view private_key_hex,
                         const std::string_view message) {
    const auto private_key = hex_decode(private_key_hex);
    if (private_key.size() != 32) throw std::runtime_error("Ed25519 private key must be 32 bytes");

    PkeyPtr key(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
                                             private_key.data(), private_key.size()));
    if (!key) throw std::runtime_error("cannot load Ed25519 private key");

    MdCtxPtr context(EVP_MD_CTX_new());
    if (!context || EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key.get()) != 1) {
        throw std::runtime_error("cannot initialize Ed25519 signing");
    }

    std::size_t signature_size = 0;
    if (EVP_DigestSign(context.get(), nullptr, &signature_size,
                       reinterpret_cast<const unsigned char*>(message.data()),
                       message.size()) != 1) {
        throw std::runtime_error("cannot size Ed25519 signature");
    }

    std::string signature(signature_size, '\0');
    if (EVP_DigestSign(context.get(),
                       reinterpret_cast<unsigned char*>(signature.data()), &signature_size,
                       reinterpret_cast<const unsigned char*>(message.data()),
                       message.size()) != 1) {
        throw std::runtime_error("Ed25519 signing failed");
    }
    signature.resize(signature_size);
    return signature;
}

bool verify_ed25519(const std::string_view public_key_hex,
                    const std::string_view message,
                    const std::string_view signature) {
    try {
        const auto public_key = hex_decode(public_key_hex);
        if (public_key.size() != 32 || signature.size() != 64) return false;

        PkeyPtr key(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
                                                public_key.data(), public_key.size()));
        if (!key) return false;

        MdCtxPtr context(EVP_MD_CTX_new());
        if (!context ||
            EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) != 1) {
            return false;
        }

        return EVP_DigestVerify(
                   context.get(),
                   reinterpret_cast<const unsigned char*>(signature.data()), signature.size(),
                   reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1;
    } catch (...) {
        return false;
    }
}

} // namespace opengenesis::federation
