#include <catch2/catch_test_macros.hpp>

#include <sodium.h>

#include "src/crypto/crypto.hpp"

namespace {

Crypto::KdfParameters fast_kdf_parameters()
{
    return {crypto_pwhash_OPSLIMIT_MIN, crypto_pwhash_MEMLIMIT_MIN};
}

Crypto::Bytes test_salt()
{
    return Crypto::Bytes(Crypto::kSaltBytes, static_cast<std::uint8_t>(0x42));
}

Crypto::Bytes derive_test_key(Crypto& crypto, const Crypto::Bytes& salt)
{
    Crypto::Bytes key;
    std::string error;
    REQUIRE(crypto.derive_key("master-password", salt, fast_kdf_parameters(), key, error));
    return key;
}

}  // namespace

TEST_CASE("Crypto refuse les operations avant son initialisation", "[crypto]")
{
    Crypto crypto;
    Crypto::Bytes output = {1, 2, 3};
    std::string error;

    CHECK_FALSE(crypto.random_bytes(8, output, error));
    CHECK(output.empty());
    CHECK_FALSE(error.empty());
    CHECK_FALSE(crypto.is_initialized());
}

TEST_CASE("Crypto initialise libsodium et genere des octets aleatoires", "[crypto]")
{
    Crypto crypto;
    std::string error;
    REQUIRE(crypto.initialize(error));
    CHECK(crypto.is_initialized());

    Crypto::Bytes first;
    Crypto::Bytes second;
    REQUIRE(crypto.random_bytes(32, first, error));
    REQUIRE(crypto.random_bytes(32, second, error));
    CHECK(first.size() == 32);
    CHECK(second.size() == 32);
    CHECK(first != second);
}

TEST_CASE("Argon2id derive une cle deterministe et sensible au mot de passe et au sel", "[crypto]")
{
    Crypto crypto;
    std::string error;
    REQUIRE(crypto.initialize(error));

    const Crypto::Bytes salt = test_salt();
    Crypto::Bytes first;
    Crypto::Bytes second;
    Crypto::Bytes different_password;
    Crypto::Bytes different_salt;
    Crypto::Bytes changed_salt = salt;
    changed_salt.front() ^= 0x01;

    REQUIRE(crypto.derive_key("master-password", salt, fast_kdf_parameters(), first, error));
    REQUIRE(crypto.derive_key("master-password", salt, fast_kdf_parameters(), second, error));
    REQUIRE(crypto.derive_key(
        "other-password", salt, fast_kdf_parameters(), different_password, error));
    REQUIRE(crypto.derive_key(
        "master-password", changed_salt, fast_kdf_parameters(), different_salt, error));

    CHECK(first.size() == Crypto::kKeyBytes);
    CHECK(first == second);
    CHECK(first != different_password);
    CHECK(first != different_salt);
}

TEST_CASE("Argon2id rejette un sel ou des parametres invalides", "[crypto]")
{
    Crypto crypto;
    std::string error;
    REQUIRE(crypto.initialize(error));

    Crypto::Bytes key = {1, 2, 3};
    Crypto::Bytes invalid_salt(Crypto::kSaltBytes - 1, 0);
    Crypto::KdfParameters invalid_parameters = fast_kdf_parameters();
    invalid_parameters.ops_limit = 0;

    CHECK_FALSE(crypto.derive_key(
        "master-password", invalid_salt, fast_kdf_parameters(), key, error));
    CHECK(key.empty());
    CHECK_FALSE(crypto.derive_key(
        "master-password", test_salt(), invalid_parameters, key, error));
    CHECK(key.empty());
}

TEST_CASE("Les parametres recommandes utilisent le profil MODERATE de libsodium", "[crypto]")
{
    const Crypto::KdfParameters parameters = Crypto::recommended_kdf_parameters();
    CHECK(parameters.ops_limit == crypto_pwhash_OPSLIMIT_MODERATE);
    CHECK(parameters.memory_limit == crypto_pwhash_MEMLIMIT_MODERATE);
}

TEST_CASE("XChaCha20-Poly1305 chiffre et dechiffre un payload", "[crypto]")
{
    Crypto crypto;
    std::string error;
    REQUIRE(crypto.initialize(error));
    const Crypto::Bytes salt = test_salt();
    const Crypto::Bytes key = derive_test_key(crypto, salt);
    const Crypto::Bytes plaintext = {0, 1, 2, 3, 0xff};
    const Crypto::Bytes associated_data = {'v', '1'};
    Crypto::EncryptedPayload encrypted;
    Crypto::Bytes decrypted;

    REQUIRE(crypto.encrypt(plaintext, key, associated_data, encrypted, error));
    CHECK(encrypted.nonce.size() == Crypto::kNonceBytes);
    CHECK(encrypted.ciphertext.size() == plaintext.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
    REQUIRE(crypto.decrypt(encrypted, key, associated_data, decrypted, error));
    CHECK(decrypted == plaintext);
}

TEST_CASE("Le chiffrement accepte un payload vide et produit des nonces differents", "[crypto]")
{
    Crypto crypto;
    std::string error;
    REQUIRE(crypto.initialize(error));
    const Crypto::Bytes key = derive_test_key(crypto, test_salt());
    const Crypto::Bytes empty_plaintext;
    const Crypto::Bytes associated_data = {'v', '1'};
    Crypto::EncryptedPayload first;
    Crypto::EncryptedPayload second;

    REQUIRE(crypto.encrypt(empty_plaintext, key, associated_data, first, error));
    REQUIRE(crypto.encrypt(empty_plaintext, key, associated_data, second, error));
    CHECK(first.ciphertext.size() == crypto_aead_xchacha20poly1305_ietf_ABYTES);
    CHECK(first.nonce != second.nonce);

    Crypto::Bytes decrypted;
    REQUIRE(crypto.decrypt(first, key, associated_data, decrypted, error));
    CHECK(decrypted.empty());
}

TEST_CASE("Le dechiffrement rejette toute alteration et vide le plaintext", "[crypto]")
{
    Crypto crypto;
    std::string error;
    REQUIRE(crypto.initialize(error));
    const Crypto::Bytes key = derive_test_key(crypto, test_salt());
    Crypto::Bytes wrong_key = key;
    wrong_key.front() ^= 0x01;
    const Crypto::Bytes associated_data = {'v', '1'};
    const Crypto::Bytes plaintext = {1, 2, 3};
    Crypto::EncryptedPayload encrypted;
    REQUIRE(crypto.encrypt(plaintext, key, associated_data, encrypted, error));

    auto expect_rejected = [&](const Crypto::EncryptedPayload& payload,
                               const Crypto::Bytes& candidate_key,
                               const Crypto::Bytes& candidate_associated_data) {
        Crypto::Bytes decrypted = {9, 9, 9};
        CHECK_FALSE(crypto.decrypt(
            payload, candidate_key, candidate_associated_data, decrypted, error));
        CHECK(decrypted.empty());
        CHECK_FALSE(error.empty());
    };

    Crypto::EncryptedPayload altered_ciphertext = encrypted;
    altered_ciphertext.ciphertext.front() ^= 0x01;
    expect_rejected(altered_ciphertext, key, associated_data);
    expect_rejected(encrypted, wrong_key, associated_data);
    expect_rejected(encrypted, key, Crypto::Bytes{'v', '2'});

    Crypto::EncryptedPayload altered_nonce = encrypted;
    altered_nonce.nonce.front() ^= 0x01;
    expect_rejected(altered_nonce, key, associated_data);
}

TEST_CASE("Crypto rejette les tailles de cle et de payload invalides", "[crypto]")
{
    Crypto crypto;
    std::string error;
    REQUIRE(crypto.initialize(error));
    const Crypto::Bytes invalid_key(Crypto::kKeyBytes - 1, 0);
    const Crypto::Bytes associated_data = {'v', '1'};
    const Crypto::Bytes plaintext = {1, 2, 3};
    Crypto::EncryptedPayload encrypted;

    CHECK_FALSE(crypto.encrypt(plaintext, invalid_key, associated_data, encrypted, error));
    CHECK(encrypted.nonce.empty());
    CHECK(encrypted.ciphertext.empty());

    const Crypto::Bytes valid_key = derive_test_key(crypto, test_salt());
    Crypto::EncryptedPayload valid_payload;
    REQUIRE(crypto.encrypt(plaintext, valid_key, associated_data, valid_payload, error));
    Crypto::Bytes decrypted = {1};
    CHECK_FALSE(crypto.decrypt(valid_payload, invalid_key, associated_data, decrypted, error));
    CHECK(decrypted.empty());

    Crypto::EncryptedPayload malformed;
    malformed.nonce = Crypto::Bytes(Crypto::kNonceBytes - 1, 0);
    malformed.ciphertext = {1, 2, 3};
    CHECK_FALSE(crypto.decrypt(malformed, valid_key, associated_data, decrypted, error));
    CHECK(decrypted.empty());
}

TEST_CASE("Crypto clear vide les buffers sensibles observables", "[crypto]")
{
    Crypto::Bytes bytes = {1, 2, 3};
    std::string text = "secret";

    Crypto::clear(bytes);
    Crypto::clear(text);

    CHECK(bytes.empty());
    CHECK(text.empty());
}
