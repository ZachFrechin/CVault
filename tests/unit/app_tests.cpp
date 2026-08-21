#include <catch2/catch_test_macros.hpp>

#include "src/app/app.hpp"
#include "tests/support/vault_fixtures.hpp"

TEST_CASE("App refuse la sauvegarde et le chargement avant initialisation", "[app]")
{
    App app;
    TemporaryVaultFile file;
    std::string error;

    CHECK_FALSE(app.is_initialized());
    CHECK_FALSE(app.save_vault(file.path(), "master-password", error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(app.load_vault(file.path(), "master-password", error));
    CHECK_FALSE(error.empty());
    CHECK(app.run() == 1);
}

TEST_CASE("App initialise son service cryptographique de maniere idempotente", "[app]")
{
    App app;
    std::string error;

    REQUIRE(app.initialize(error));
    CHECK(app.is_initialized());
    REQUIRE(app.initialize(error));
    CHECK(error.empty());
}

TEST_CASE("App sauvegarde puis charge un vault depuis un fichier chiffre", "[app][integration]")
{
    const Vault expected = make_fixture_vault();
    TemporaryVaultFile file;
    App source;
    std::string error;
    REQUIRE(source.initialize(error));
    source.vault() = expected;

    REQUIRE(source.save_vault(file.path(), "master-password", error));

    App restored;
    REQUIRE(restored.initialize(error));
    restored.vault().add_folder(Folder("Vault avant erreur"));
    CHECK_FALSE(restored.load_vault(file.path(), "wrong-password", error));
    CHECK(restored.vault().get_folder("Vault avant erreur") != nullptr);

    REQUIRE(restored.load_vault(file.path(), "master-password", error));
    CHECK(vaults_are_equal(restored.vault(), expected));
}
