# Third-party components

OpenGenesisLINK Server keeps external dependencies deliberately small.

## OpenSSL / libcrypto

Used by the Core Identity subsystem for:

- cryptographically secure random salts and session tokens
- PBKDF2-HMAC-SHA256 password verification
- SHA-256 hashing of persisted session-token identifiers

OpenSSL is dynamically linked through CMake `OpenSSL::Crypto`. See the OpenSSL project for its applicable license terms.
