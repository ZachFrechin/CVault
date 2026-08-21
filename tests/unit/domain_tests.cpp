#include <catch2/catch_test_macros.hpp>

#include "src/core/folder.hpp"
#include "src/core/vault.hpp"

TEST_CASE("Un dossier conserve l'ordre de ses entrees", "[domaine]")
{
    Folder folder("Personnel");
    folder.add_entry({"premiere", "alice", "secret-1"});
    folder.add_entry({"seconde", "bob", "secret-2"});

    REQUIRE(folder.get_name() == "Personnel");
    REQUIRE(folder.get_entries().size() == 2);
    CHECK(folder.get_entries()[0].id == "premiere");
    CHECK(folder.get_entries()[1].id == "seconde");
}

TEST_CASE("Un dossier supprime l'entree demandee et ignore un identifiant absent", "[domaine]")
{
    Folder folder("Travail");
    folder.add_entry({"serveur", "admin", "secret"});
    folder.add_entry({"courriel", "alice", "secret-2"});

    folder.remove_entry("serveur");
    REQUIRE(folder.get_entries().size() == 1);
    CHECK(folder.get_entries().front().id == "courriel");

    folder.remove_entry("inconnu");
    CHECK(folder.get_entries().size() == 1);
}

TEST_CASE("Le vault recherche les dossiers et ignore les doublons", "[domaine]")
{
    Vault vault;
    vault.add_folder(Folder("Personnel"));
    vault.add_folder(Folder("Travail"));
    vault.add_folder(Folder("Personnel"));

    REQUIRE(vault.get_folders().size() == 2);
    CHECK(vault.get_folder("Personnel") != nullptr);
    CHECK(vault.get_folder("Travail") != nullptr);
    CHECK(vault.get_folder("Inexistant") == nullptr);
}

TEST_CASE("Le vault refuse les noms de dossiers vides", "[domaine]")
{
    Vault vault;

    CHECK_FALSE(vault.add_folder(Folder("")));
    CHECK_FALSE(vault.add_folder(Folder("   ")));
    CHECK(vault.get_folders().empty());
}

TEST_CASE("Un dossier refuse les entrees invalides ou dupliquees", "[domaine]")
{
    Folder folder("Personnel");

    CHECK(folder.add_entry({"courriel", "alice", "secret"}));
    CHECK_FALSE(folder.add_entry({"courriel", "bob", "autre-secret"}));
    CHECK_FALSE(folder.add_entry({"", "bob", "secret"}));
    CHECK_FALSE(folder.add_entry({"serveur", "", "secret"}));
    CHECK_FALSE(folder.add_entry({"serveur", "bob", "   "}));
    CHECK(folder.get_entries().size() == 1);
}
