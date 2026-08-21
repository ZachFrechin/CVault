#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "src/storage/serializer.hpp"
#include "tests/support/vault_fixtures.hpp"

TEST_CASE("Le serializer produit le format d'un vault vide", "[serializer]")
{
    Serializer serializer;
    CHECK(serializer.serialize(Vault{}) == "VAULTCLI-1\n");
}

TEST_CASE("Le serializer conserve les dossiers, entrees et caracteres speciaux", "[serializer]")
{
    Serializer serializer;
    Vault source;
    source.add_folder(Folder("Personnel%\t\n\r"));
    source.get_folder("Personnel%\t\n\r")->add_entry(
        {"id%\t\n\r", "login%\t\n\r", "secret%\t\n\r"});
    source.add_folder(Folder("Travail"));
    source.get_folder("Travail")->add_entry({"serveur", "admin", "secret"});

    const std::string serialized = serializer.serialize(source);
    Vault restored;
    std::string error;

    REQUIRE(serializer.deserialize(serialized, restored, error));
    CHECK(error.empty());
    CHECK(vaults_are_equal(source, restored));
}

TEST_CASE("Un payload invalide ne modifie pas le vault de destination", "[serializer]")
{
    Serializer serializer;
    const Vault expected = make_fixture_vault();
    const std::string original = serializer.serialize(expected);
    const std::vector<std::string> invalid_payloads = {
        "VERSION-INCONNUE\n",
        "VAULTCLI-1\nE\tid\tlogin\tsecret\n",
        "VAULTCLI-1\nF\tdossier\nX\tvaleur\n",
        "VAULTCLI-1\nF\tdossier%ZZ\n",
        "VAULTCLI-1\nF\tdossier%\n",
        "VAULTCLI-1\nF\tdossier\nE\tid\tlogin\n",
    };

    for (const std::string& payload : invalid_payloads) {
        Vault destination = expected;
        std::string error;

        CHECK_FALSE(serializer.deserialize(payload, destination, error));
        CHECK_FALSE(error.empty());
        CHECK(serializer.serialize(destination) == original);
    }
}
