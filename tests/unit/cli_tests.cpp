#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "src/cli/cli.hpp"

namespace {

bool parse(std::initializer_list<const char*> arguments,
           StartupOptions& options,
           std::string& error)
{
    std::vector<char*> argv;
    argv.reserve(arguments.size());
    for (const char* argument : arguments) {
        argv.push_back(const_cast<char*>(argument));
    }
    return parse_startup_options(static_cast<int>(argv.size()), argv.data(), options, error);
}

class TemporaryDirectory {
public:
    TemporaryDirectory()
    {
        static std::atomic<std::uint64_t> counter{0};
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("vaultcli-cli-" + std::to_string(static_cast<std::uint64_t>(now)) +
                 "-" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed)));
    }

    ~TemporaryDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

}  // namespace

TEST_CASE("Le parseur CLI utilise le vault par defaut sans argument", "[cli]")
{
    StartupOptions options;
    std::string error;

    REQUIRE(parse({"password_manager"}, options, error));
    CHECK(options.use_default_vault);
    CHECK(options.vault_path.empty());
    CHECK_FALSE(options.show_help);
}

TEST_CASE("Le parseur CLI accepte --vault seul pour le vault par defaut", "[cli]")
{
    StartupOptions options;
    std::string error;

    REQUIRE(parse({"password_manager", "--vault"}, options, error));
    CHECK(options.use_default_vault);
    CHECK(options.vault_path.empty());
}

TEST_CASE("Le parseur CLI accepte un chemin de vault explicite", "[cli]")
{
    StartupOptions options;
    std::string error;

    REQUIRE(parse({"password_manager", "--vault", "/tmp/test.vault"}, options, error));
    CHECK(options.vault_path == "/tmp/test.vault");
    CHECK_FALSE(options.use_default_vault);
    CHECK_FALSE(options.show_help);
}

TEST_CASE("Le parseur CLI expose l'aide sans mot de passe", "[cli]")
{
    StartupOptions options;
    std::string error;

    REQUIRE(parse({"password_manager", "--help"}, options, error));
    CHECK(options.show_help);
    CHECK(startup_usage().find("--vault") != std::string::npos);
}

TEST_CASE("Le parseur CLI refuse les arguments inconnus ou incomplets", "[cli]")
{
    StartupOptions options;
    std::string error;

    CHECK_FALSE(parse({"password_manager", "--vault", "--help"}, options, error));
    CHECK_FALSE(parse({"password_manager", "--unknown"}, options, error));
    CHECK_FALSE(parse({"password_manager", "--help", "--vault", "vault"}, options, error));
}

TEST_CASE("Le résolveur construit les chemins par defaut des plateformes", "[cli]")
{
    std::filesystem::path path;
    std::string error;

    DefaultVaultEnvironment macos;
    macos.platform = StartupPlatform::macos;
    macos.home_directory = "/Users/alice";
    REQUIRE(resolve_default_vault_path(macos, path, error));
    CHECK(path == "/Users/alice/Library/Application Support/VaultCLI/default.vault");

    DefaultVaultEnvironment linux_xdg;
    linux_xdg.platform = StartupPlatform::linux_;
    linux_xdg.home_directory = "/home/alice";
    linux_xdg.xdg_data_home = "/custom/data";
    REQUIRE(resolve_default_vault_path(linux_xdg, path, error));
    CHECK(path == "/custom/data/vaultcli/default.vault");

    DefaultVaultEnvironment linux_home;
    linux_home.platform = StartupPlatform::linux_;
    linux_home.home_directory = "/home/alice";
    REQUIRE(resolve_default_vault_path(linux_home, path, error));
    CHECK(path == "/home/alice/.local/share/vaultcli/default.vault");

    DefaultVaultEnvironment windows;
    windows.platform = StartupPlatform::windows;
    windows.app_data_directory = "C:/Users/Alice/AppData/Roaming";
    REQUIRE(resolve_default_vault_path(windows, path, error));
    CHECK(path == "C:/Users/Alice/AppData/Roaming/VaultCLI/default.vault");
}

TEST_CASE("Le résolveur refuse un environnement incomplet ou relatif", "[cli]")
{
    std::filesystem::path path;
    std::string error;
    DefaultVaultEnvironment environment;
    environment.platform = StartupPlatform::macos;

    CHECK_FALSE(resolve_default_vault_path(environment, path, error));
    CHECK_FALSE(error.empty());

    environment.home_directory = "relative-home";
    CHECK_FALSE(resolve_default_vault_path(environment, path, error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("Le premier vault par defaut cree un dossier prive", "[cli]")
{
    TemporaryDirectory temporary;
    DefaultVaultEnvironment environment;
    environment.platform = StartupPlatform::macos;
    environment.home_directory = temporary.path();

    std::filesystem::path vault_path;
    std::string error;
    REQUIRE(resolve_default_vault_path(environment, vault_path, error));
    REQUIRE(ensure_default_vault_directory(vault_path, error));
    CHECK(std::filesystem::is_directory(vault_path.parent_path()));
#ifndef _WIN32
    const auto permissions = std::filesystem::status(vault_path.parent_path()).permissions();
    CHECK((permissions & std::filesystem::perms::group_all) == std::filesystem::perms::none);
    CHECK((permissions & std::filesystem::perms::others_all) == std::filesystem::perms::none);
#endif
}

TEST_CASE("Le spinner de demarrage attend la tache sans afficher le mot de passe", "[cli]")
{
    std::ostringstream output;
    std::string error;
    const bool success = run_with_terminal_spinner(
        "Creation du vault securise...",
        [](std::string& task_error) {
            std::this_thread::sleep_for(std::chrono::milliseconds(130));
            task_error.clear();
            return true;
        },
        output,
        error);

    CHECK(success);
    CHECK(error.empty());
    CHECK(output.str().find("Creation du vault securise...") != std::string::npos);
    CHECK(output.str().find("master-password") == std::string::npos);
}

TEST_CASE("Le spinner de demarrage propage l'erreur de chargement", "[cli]")
{
    std::ostringstream output;
    std::string error;
    const bool success = run_with_terminal_spinner(
        "Chargement du vault securise...",
        [](std::string& task_error) {
            task_error = "vault invalide";
            return false;
        },
        output,
        error);

    CHECK_FALSE(success);
    CHECK(error == "vault invalide");
    CHECK(output.str().find("Chargement du vault securise...") != std::string::npos);
}
