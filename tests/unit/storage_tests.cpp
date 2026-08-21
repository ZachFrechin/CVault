#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <string>

#include "src/crypto/crypto.hpp"
#include "src/storage/serializer.hpp"
#include "src/storage/storage.hpp"
#include "tests/support/vault_fixtures.hpp"

namespace {

Crypto::Bytes read_file(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    REQUIRE(input);
    const std::streampos end = input.tellg();
    REQUIRE(end >= 0);
    Crypto::Bytes bytes(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    input.read(reinterpret_cast<char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    REQUIRE((static_cast<bool>(input) || bytes.empty()));
    return bytes;
}

void write_file(const std::filesystem::path& path, const Crypto::Bytes& bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    REQUIRE(output);
}

Storage make_storage(Serializer& serializer, Crypto& crypto)
{
    return Storage(serializer, crypto);
}

}  // namespace

TEST_CASE("Storage chiffre et recharge un vault sans exposer le plaintext", "[storage]")
{
    Crypto crypto;
    Serializer serializer;
    Storage storage = make_storage(serializer, crypto);
    TemporaryVaultFile file;
    const Vault expected = make_fixture_vault();
    std::string error;
    REQUIRE(crypto.initialize(error));

    REQUIRE(storage.save(expected, file.path(), "master-password", error));
    const Crypto::Bytes encoded = read_file(file.path());
    REQUIRE(encoded.size() > 8);
    CHECK(std::string(encoded.begin(), encoded.begin() + 8) == "VAULTCLI");
    const std::string encoded_text(encoded.begin(), encoded.end());
    CHECK(encoded_text.find("VAULTCLI-1") == std::string::npos);
    CHECK(encoded_text.find("secret-personnel") == std::string::npos);
    CHECK(encoded_text.find("secret-travail") == std::string::npos);

    Vault restored;
    REQUIRE(storage.load(file.path(), "master-password", restored, error));
    CHECK(vaults_are_equal(restored, expected));
}

TEST_CASE("Storage refuse un mot de passe vide avant toute operation", "[storage]")
{
    Crypto crypto;
    Serializer serializer;
    Storage storage = make_storage(serializer, crypto);
    TemporaryVaultFile file;
    std::string error;
    REQUIRE(crypto.initialize(error));

    CHECK_FALSE(storage.save(make_fixture_vault(), file.path(), "", error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(std::filesystem::exists(file.path()));

    Vault destination;
    CHECK_FALSE(storage.load(file.path(), "", destination, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("Storage refuse un mot de passe incorrect ou un ciphertext altere sans muter le vault",
          "[storage]")
{
    Crypto crypto;
    Serializer serializer;
    Storage storage = make_storage(serializer, crypto);
    TemporaryVaultFile file;
    const Vault expected = make_fixture_vault();
    std::string error;
    REQUIRE(crypto.initialize(error));
    REQUIRE(storage.save(expected, file.path(), "master-password", error));

    Vault destination;
    destination.add_folder(Folder("Vault conserve"));
    CHECK_FALSE(storage.load(file.path(), "wrong-password", destination, error));
    CHECK(destination.get_folder("Vault conserve") != nullptr);

    Crypto::Bytes encoded = read_file(file.path());
    REQUIRE(encoded.size() > 1);
    encoded.back() ^= 0x01;
    write_file(file.path(), encoded);
    CHECK_FALSE(storage.load(file.path(), "master-password", destination, error));
    CHECK(destination.get_folder("Vault conserve") != nullptr);
}

TEST_CASE("Storage rejette les headers invalides et les fichiers tronques", "[storage]")
{
    Crypto crypto;
    Serializer serializer;
    Storage storage = make_storage(serializer, crypto);
    TemporaryVaultFile file;
    std::string error;
    REQUIRE(crypto.initialize(error));
    REQUIRE(storage.save(make_fixture_vault(), file.path(), "master-password", error));
    const Crypto::Bytes valid_file = read_file(file.path());

    auto expect_invalid = [&](Crypto::Bytes invalid_file) {
        write_file(file.path(), invalid_file);
        Vault destination;
        destination.add_folder(Folder("Vault conserve"));
        CHECK_FALSE(storage.load(file.path(), "master-password", destination, error));
        CHECK(destination.get_folder("Vault conserve") != nullptr);
    };

    Crypto::Bytes invalid_magic = valid_file;
    invalid_magic[0] ^= 0x01;
    expect_invalid(invalid_magic);

    Crypto::Bytes invalid_version = valid_file;
    invalid_version[8] = 0x02;
    expect_invalid(invalid_version);

    Crypto::Bytes invalid_size = valid_file;
    invalid_size[44] = 0xff;
    expect_invalid(invalid_size);

    Crypto::Bytes truncated = valid_file;
    truncated.resize(12);
    expect_invalid(truncated);
}

TEST_CASE("Storage authentifie les metadonnees KDF et le sel", "[storage]")
{
    Crypto crypto;
    Serializer serializer;
    Storage storage = make_storage(serializer, crypto);
    TemporaryVaultFile file;
    std::string error;
    REQUIRE(crypto.initialize(error));
    REQUIRE(storage.save(make_fixture_vault(), file.path(), "master-password", error));
    const Crypto::Bytes valid_file = read_file(file.path());

    auto expect_authentication_failure = [&](Crypto::Bytes altered) {
        write_file(file.path(), altered);
        Vault destination;
        destination.add_folder(Folder("Vault conserve"));
        CHECK_FALSE(storage.load(file.path(), "master-password", destination, error));
        CHECK(destination.get_folder("Vault conserve") != nullptr);
    };

    Crypto::Bytes altered_salt = valid_file;
    altered_salt[28] ^= 0x01;
    expect_authentication_failure(altered_salt);

    Crypto::Bytes altered_ops = valid_file;
    altered_ops[12] ^= 0x01;
    expect_authentication_failure(altered_ops);
}

TEST_CASE("Storage signale les chemins inexistants et remplace un fichier existant", "[storage]")
{
    Crypto crypto;
    Serializer serializer;
    Storage storage = make_storage(serializer, crypto);
    TemporaryVaultFile file;
    std::string error;
    REQUIRE(crypto.initialize(error));

    const std::filesystem::path missing_file = file.path().string() + ".missing";
    Vault destination;
    CHECK_FALSE(storage.load(missing_file, "master-password", destination, error));
    CHECK_FALSE(error.empty());

    const std::filesystem::path missing_parent =
        file.path().parent_path() /
        (file.path().filename().string() + ".parent") / "vault.bin";
    CHECK_FALSE(storage.save(make_fixture_vault(), missing_parent, "master-password", error));
    CHECK_FALSE(error.empty());

    Vault first = make_fixture_vault();
    REQUIRE(storage.save(first, file.path(), "master-password", error));
    Vault second;
    second.add_folder(Folder("Secondaire"));
    second.get_folder("Secondaire")->add_entry({"id", "login", "password"});
    REQUIRE(storage.save(second, file.path(), "master-password", error));
    REQUIRE(storage.load(file.path(), "master-password", destination, error));
    CHECK(vaults_are_equal(destination, second));
}

TEST_CASE("Storage nettoie le temporaire et applique les permissions POSIX", "[storage]")
{
    Crypto crypto;
    Serializer serializer;
    Storage storage = make_storage(serializer, crypto);
    TemporaryVaultFile file;
    std::string error;
    REQUIRE(crypto.initialize(error));

    REQUIRE(storage.save(make_fixture_vault(), file.path(), "master-password", error));
    for (const auto& candidate : std::filesystem::directory_iterator(file.path().parent_path())) {
        CHECK(candidate.path().filename().string().find(file.path().filename().string() + ".tmp.") !=
              0);
    }

#ifndef _WIN32
    const auto permissions = std::filesystem::status(file.path()).permissions();
    CHECK((permissions & std::filesystem::perms::owner_read) != std::filesystem::perms::none);
    CHECK((permissions & std::filesystem::perms::owner_write) != std::filesystem::perms::none);
    CHECK((permissions & std::filesystem::perms::group_read) == std::filesystem::perms::none);
    CHECK((permissions & std::filesystem::perms::group_write) == std::filesystem::perms::none);
    CHECK((permissions & std::filesystem::perms::others_read) == std::filesystem::perms::none);
    CHECK((permissions & std::filesystem::perms::others_write) == std::filesystem::perms::none);
#endif
}

TEST_CASE("Storage supprime le temporaire si le remplacement echoue", "[storage]")
{
    Crypto crypto;
    Serializer serializer;
    Storage storage = make_storage(serializer, crypto);
    TemporaryVaultFile file;
    std::string error;
    REQUIRE(crypto.initialize(error));
    REQUIRE(std::filesystem::create_directory(file.path()));

    CHECK_FALSE(storage.save(make_fixture_vault(), file.path(), "master-password", error));
    for (const auto& candidate : std::filesystem::directory_iterator(file.path().parent_path())) {
        CHECK(candidate.path().filename().string().find(file.path().filename().string() + ".tmp.") !=
              0);
    }
}
