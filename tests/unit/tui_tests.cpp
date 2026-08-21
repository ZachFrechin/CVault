#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <future>
#include <thread>
#include <utility>

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/terminal.hpp>

#include "src/tui/tui.hpp"

struct TuiTestAccess {
    static ftxui::Component make_app(Tui& tui)
    {
        return tui.make_app();
    }

    static bool form_open(const Tui& tui)
    {
        return tui.form_open_;
    }

    static int form_mode(const Tui& tui)
    {
        return static_cast<int>(tui.form_mode_);
    }

    static void commit(Tui& tui,
                       const std::function<void(Vault&)>& mutation,
                       std::string success_status)
    {
        tui.commit_mutation(mutation, std::move(success_status));
    }

    static bool saving(const Tui& tui)
    {
        return tui.save_in_progress();
    }

    static ftxui::Element render(const Tui& tui)
    {
        return tui.render();
    }

    static void wait_for_save(Tui& tui)
    {
        REQUIRE(tui.pending_save_.has_value());
        tui.pending_save_->task.wait();
        REQUIRE(tui.finish_pending_save());
    }
};

TEST_CASE("La touche a ouvre le formulaire de dossier quand le vault est vide", "[tui]")
{
    Vault vault;
    Tui tui(vault);
    auto app = TuiTestAccess::make_app(tui);

    REQUIRE(app->OnEvent(ftxui::Event::Character('a')));
    CHECK(TuiTestAccess::form_open(tui));
    CHECK(TuiTestAccess::form_mode(tui) == 1);
}

TEST_CASE("La touche f ouvre le formulaire de dossier depuis la colonne des entrees", "[tui]")
{
    Vault vault;
    vault.add_folder(Folder("Personnel"));
    Tui tui(vault);
    auto app = TuiTestAccess::make_app(tui);

    REQUIRE(app->OnEvent(ftxui::Event::Character('f')));
    CHECK(TuiTestAccess::form_open(tui));
    CHECK(TuiTestAccess::form_mode(tui) == 1);
}

TEST_CASE("La touche a ouvre le formulaire d'entree des qu'un dossier existe", "[tui]")
{
    Vault vault;
    vault.add_folder(Folder("Personnel"));
    Tui tui(vault);
    auto app = TuiTestAccess::make_app(tui);

    REQUIRE(app->OnEvent(ftxui::Event::Character('a')));
    CHECK(TuiTestAccess::form_open(tui));
    CHECK(TuiTestAccess::form_mode(tui) == 2);
}

TEST_CASE("Entree ouvre le dossier selectionne avant la creation d'une entree", "[tui]")
{
    Vault vault;
    vault.add_folder(Folder("Personnel"));
    Tui tui(vault);
    auto app = TuiTestAccess::make_app(tui);

    REQUIRE(app->OnEvent(ftxui::Event::Return));
    REQUIRE(app->OnEvent(ftxui::Event::Character('a')));
    CHECK(TuiTestAccess::form_open(tui));
    CHECK(TuiTestAccess::form_mode(tui) == 2);
}

TEST_CASE("La transaction TUI conserve un nouveau dossier apres une sauvegarde reussie", "[tui]")
{
    Vault vault;
    int save_count = 0;
    bool snapshot_valid = false;
    Tui tui(vault, [&](const Vault& snapshot, std::string& error) {
        ++save_count;
        snapshot_valid = !snapshot.get_folders().empty() &&
                          snapshot.get_folders().front().get_name() == "Personnel";
        error.clear();
        return true;
    });

    TuiTestAccess::commit(
        tui, [](Vault& candidate) { candidate.add_folder(Folder("Personnel")); }, "Dossier cree.");
    TuiTestAccess::wait_for_save(tui);
    CHECK(save_count == 1);
    CHECK(snapshot_valid);
    REQUIRE(vault.get_folders().size() == 1);
    CHECK(vault.get_folders().front().get_name() == "Personnel");
}

TEST_CASE("La transaction TUI restaure le vault si la sauvegarde echoue", "[tui]")
{
    Vault vault;
    vault.add_folder(Folder("Avant"));
    Tui tui(vault, [](const Vault&, std::string& error) {
        error = "echec de test";
        return false;
    });

    TuiTestAccess::commit(
        tui, [](Vault& candidate) { candidate.add_folder(Folder("Ne doit pas rester")); }, "Dossier cree.");
    TuiTestAccess::wait_for_save(tui);
    REQUIRE(vault.get_folders().size() == 1);
    CHECK(vault.get_folder("Avant") != nullptr);
    CHECK(vault.get_folder("Ne doit pas rester") == nullptr);
}

TEST_CASE("La TUI reste interactive pendant une sauvegarde en cours", "[tui]")
{
    Vault vault;
    std::promise<void> release;
    auto gate = release.get_future();
    std::promise<void> started;
    auto started_future = started.get_future();
    bool snapshot_valid = false;
    Tui tui(vault, [&](const Vault& snapshot, std::string& error) {
        snapshot_valid = !snapshot.get_folders().empty() &&
                          snapshot.get_folders().front().get_name() == "En attente";
        started.set_value();
        gate.wait();
        error.clear();
        return true;
    });
    auto app = TuiTestAccess::make_app(tui);

    TuiTestAccess::commit(
        tui, [](Vault& candidate) { candidate.add_folder(Folder("En attente")); }, "Dossier cree.");
    started_future.wait();
    CHECK(TuiTestAccess::saving(tui));
    CHECK(vault.get_folder("En attente") == nullptr);
    auto saving_screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                               ftxui::Dimension::Fixed(20));
    ftxui::Render(saving_screen, app->Render());
    CHECK(saving_screen.ToString().find("Sauvegarde securisee") != std::string::npos);
    CHECK(app->OnEvent(ftxui::Event::Character('q')));
    CHECK(TuiTestAccess::saving(tui));

    release.set_value();
    TuiTestAccess::wait_for_save(tui);
    CHECK(vault.get_folder("En attente") != nullptr);
    CHECK(snapshot_valid);
}

TEST_CASE("Le rendu TUI utilise des cadres carres et conserve les libelles", "[tui]")
{
    Vault vault;
    vault.add_folder(Folder("Personnel"));
    Tui tui(vault);
    auto element = TuiTestAccess::render(tui);
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(120),
                                        ftxui::Dimension::Fixed(30));
    ftxui::Render(screen, element);
    const std::string output = screen.ToString();

    CHECK(output.find("Vault CLI") != std::string::npos);
    CHECK(output.find("Dossiers") != std::string::npos);
    CHECK(output.find("Entrees") != std::string::npos);
    CHECK(output.find("Selection") != std::string::npos);
    CHECK(output.find("┌") != std::string::npos);
    CHECK(output.find("╭") == std::string::npos);
    CHECK(output.find("/ rechercher") != std::string::npos);
}

TEST_CASE("Le rendu TUI replie les panneaux et l'aide sur un terminal etroit", "[tui]")
{
    const ftxui::Dimensions previous_fallback = {80, 24};
    ftxui::Terminal::SetFallbackSize(previous_fallback);
    Vault vault;
    vault.add_folder(Folder("Personnel"));
    Tui tui(vault);
    auto element = TuiTestAccess::render(tui);
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(80),
                                        ftxui::Dimension::Fixed(40));
    ftxui::Render(screen, element);
    const std::string output = screen.ToString();

    const auto folders_position = output.find("Dossiers");
    const auto entries_position = output.find("Entrees");
    REQUIRE(folders_position != std::string::npos);
    REQUIRE(entries_position != std::string::npos);
    CHECK(std::count(output.begin(), output.begin() + static_cast<std::ptrdiff_t>(folders_position), '\n') <
          std::count(output.begin(), output.begin() + static_cast<std::ptrdiff_t>(entries_position), '\n'));
    CHECK(output.find("q quitter") != std::string::npos);
}
