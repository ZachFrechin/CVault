#include "crypto.hpp"
#include <limits>
#include <sodium.h>

namespace {

static_assert(Crypto::kKeyBytes == crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
static_assert(Crypto::kSaltBytes == crypto_pwhash_SALTBYTES);
static_assert(Crypto::kNonceBytes == crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
static_assert(Crypto::kAuthenticationTagBytes == crypto_aead_xchacha20poly1305_ietf_ABYTES);

constexpr const char* kInitializationError =
    "Impossible d'initialiser libsodium.";
constexpr const char* kNotInitializedError =
    "Le service cryptographique n'est pas initialise.";
constexpr const char* kInvalidKeyError =
    "La cle de chiffrement doit contenir exactement 32 octets.";
constexpr const char* kInvalidSaltError =
    "Le sel Argon2id doit contenir exactement 16 octets.";
constexpr const char* kInvalidKdfParametersError =
    "Les parametres Argon2id sont hors limites.";
constexpr const char* kInvalidPayloadError =
    "Le payload chiffre est invalide.";
constexpr const char* kDecryptionError =
    "Dechiffrement impossible : mot de passe invalide ou donnees corrompues.";
constexpr const char* kEncryptionError =
    "Le chiffrement du payload a echoue.";

bool valid_key(const Crypto::Bytes& key)
{
    return key.size() == crypto_aead_xchacha20poly1305_ietf_KEYBYTES;
}

bool valid_kdf_parameters(const Crypto::KdfParameters& parameters)
{
    return parameters.ops_limit >= crypto_pwhash_OPSLIMIT_MIN &&
           parameters.ops_limit <= crypto_pwhash_OPSLIMIT_MAX &&
           parameters.memory_limit >= crypto_pwhash_MEMLIMIT_MIN &&
           parameters.memory_limit <= crypto_pwhash_MEMLIMIT_MAX;
}

bool valid_length(std::size_t length)
{
    return length <= std::numeric_limits<unsigned long long>::max();
}

}  // namespace

bool Crypto::initialize(std::string& error)
{
    if (sodium_init() < 0) {
        initialized_ = false;
        error = kInitializationError;
        return false;
    }

    initialized_ = true;
    error.clear();
    return true;
}

bool Crypto::is_initialized() const noexcept
{
    return initialized_;
}

Crypto::KdfParameters Crypto::recommended_kdf_parameters() noexcept
{
    return {
        crypto_pwhash_OPSLIMIT_MODERATE,
        crypto_pwhash_MEMLIMIT_MODERATE,
    };
}

bool Crypto::random_bytes(std::size_t size, Bytes& output, std::string& error) const
{
    clear(output);
    if (!initialized_) {
        error = kNotInitializedError;
        return false;
    }
    if (size == 0) {
        error.clear();
        return true;
    }

    output.resize(size);
    randombytes_buf(output.data(), output.size());
    error.clear();
    return true;
}

bool Crypto::derive_key(std::string_view password,
                        const Bytes& salt,
                        const KdfParameters& parameters,
                        Bytes& key,
                        std::string& error) const
{
    clear(key);
    if (!initialized_) {
        error = kNotInitializedError;
        return false;
    }
    if (salt.size() != crypto_pwhash_SALTBYTES) {
        error = kInvalidSaltError;
        return false;
    }
    if (!valid_kdf_parameters(parameters)) {
        error = kInvalidKdfParametersError;
        return false;
    }
    if (password.size() > crypto_pwhash_PASSWD_MAX ||
        !valid_length(password.size())) {
        error = "Le mot de passe est trop long pour Argon2id.";
        return false;
    }

    key.resize(crypto_aead_xchacha20poly1305_ietf_KEYBYTES);
    const int result = crypto_pwhash(
        key.data(),
        key.size(),
        password.data(),
        password.size(),
        salt.data(),
        parameters.ops_limit,
        parameters.memory_limit,
        crypto_pwhash_ALG_ARGON2ID13);
    if (result != 0) {
        clear(key);
        error = "La derivation Argon2id a echoue.";
        return false;
    }

    error.clear();
    return true;
}

bool Crypto::encrypt(const Bytes& plaintext,
                     const Bytes& key,
                     const Bytes& associated_data,
                     EncryptedPayload& output,
                     std::string& error) const
{
    clear(output.nonce);
    clear(output.ciphertext);
    if (!initialized_) {
        error = kNotInitializedError;
        return false;
    }
    if (!valid_key(key)) {
        error = kInvalidKeyError;
        return false;
    }
    if (!valid_length(plaintext.size()) || !valid_length(associated_data.size()) ||
        plaintext.size() >
            std::numeric_limits<std::size_t>::max() -
                crypto_aead_xchacha20poly1305_ietf_ABYTES) {
        error = "Le payload est trop volumineux pour ce chiffrement.";
        return false;
    }

    output.nonce.resize(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
    randombytes_buf(output.nonce.data(), output.nonce.size());
    output.ciphertext.resize(plaintext.size() +
                             crypto_aead_xchacha20poly1305_ietf_ABYTES);

    unsigned long long ciphertext_size = 0;
    const int result = crypto_aead_xchacha20poly1305_ietf_encrypt(
        output.ciphertext.data(),
        &ciphertext_size,
        plaintext.empty() ? nullptr : plaintext.data(),
        plaintext.size(),
        associated_data.empty() ? nullptr : associated_data.data(),
        associated_data.size(),
        nullptr,
        output.nonce.data(),
        key.data());
    if (result != 0) {
        clear(output.nonce);
        clear(output.ciphertext);
        error = kEncryptionError;
        return false;
    }

    output.ciphertext.resize(static_cast<std::size_t>(ciphertext_size));
    error.clear();
    return true;
}

bool Crypto::decrypt(const EncryptedPayload& input,
                     const Bytes& key,
                     const Bytes& associated_data,
                     Bytes& plaintext,
                     std::string& error) const
{
    clear(plaintext);
    if (!initialized_) {
        error = kNotInitializedError;
        return false;
    }
    if (!valid_key(key)) {
        error = kInvalidKeyError;
        return false;
    }
    if (input.nonce.size() != crypto_aead_xchacha20poly1305_ietf_NPUBBYTES ||
        input.ciphertext.size() < crypto_aead_xchacha20poly1305_ietf_ABYTES ||
        !valid_length(input.ciphertext.size()) || !valid_length(associated_data.size())) {
        error = kInvalidPayloadError;
        return false;
    }

    plaintext.resize(input.ciphertext.size() -
                     crypto_aead_xchacha20poly1305_ietf_ABYTES);
    unsigned long long plaintext_size = 0;
    const int result = crypto_aead_xchacha20poly1305_ietf_decrypt(
        plaintext.empty() ? nullptr : plaintext.data(),
        &plaintext_size,
        nullptr,
        input.ciphertext.data(),
        input.ciphertext.size(),
        associated_data.empty() ? nullptr : associated_data.data(),
        associated_data.size(),
        input.nonce.data(),
        key.data());
    if (result != 0) {
        clear(plaintext);
        error = kDecryptionError;
        return false;
    }

    plaintext.resize(static_cast<std::size_t>(plaintext_size));
    error.clear();
    return true;
}

void Crypto::clear(Bytes& bytes) noexcept
{
    if (!bytes.empty()) {
        sodium_memzero(bytes.data(), bytes.size());
    }
    bytes.clear();
}

void Crypto::clear(std::string& text) noexcept
{
    if (!text.empty()) {
        sodium_memzero(text.data(), text.size());
    }
    text.clear();
}
