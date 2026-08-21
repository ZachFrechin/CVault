#include "src/app/app.hpp"
#include "src/cli/cli.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

namespace {

class SecretCleanup {
public:
    explicit SecretCleanup(std::string& secret) noexcept : secret_(secret) {}

    SecretCleanup(const SecretCleanup&) = delete;
    SecretCleanup& operator=(const SecretCleanup&) = delete;

    ~SecretCleanup()
    {
        Crypto::clear(secret_);
    }

private:
    std::string& secret_;
};

constexpr char kEmptyPasswordError[] =
    "Le mot de passe maître ne peut pas etre vide.";
constexpr char kPasswordMismatchError[] =
    "Les mots de passe ne correspondent pas.";

}  // namespace

/// @brief Initialize the application and launch the terminal user interface.
/// @param argc Number of process arguments.
/// @param argv Process argument vector.
/// @return Process exit status returned to the operating system.
int main(int argc, char* argv[])
{
    StartupOptions options;
    std::string error;
    if (!parse_startup_options(argc, argv, options, error)) {
        std::cerr << error << '\n' << startup_usage();
        return 2;
    }
    if (options.show_help) {
        std::cout << startup_usage();
        return 0;
    }

    std::filesystem::path vault_path;
    if (!resolve_vault_path(options, vault_path, error)) {
        std::cerr << "Impossible de resoudre le chemin du vault : " << error << '\n';
        return 1;
    }
    std::cout << "Vault : " << vault_path.string() << '\n';

    App app;
    if (!app.initialize(error)) {
        std::cerr << "Initialisation impossible : " << error << '\n';
        return 1;
    }

    std::string master_password;
    SecretCleanup password_cleanup(master_password);
    if (!prompt_hidden_password("Mot de passe maître : ", master_password, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    if (master_password.empty()) {
        std::cerr << kEmptyPasswordError << '\n';
        return 1;
    }

    std::error_code exists_error;
    const auto path_status = std::filesystem::symlink_status(vault_path, exists_error);
    const bool path_is_absent =
        exists_error == std::make_error_code(std::errc::no_such_file_or_directory);
    if (exists_error && !path_is_absent) {
        std::cerr << "Impossible d'inspecter le chemin du vault : " << vault_path.string()
                  << "\n";
        return 1;
    }
    const bool vault_exists = !path_is_absent &&
                              path_status.type() != std::filesystem::file_type::not_found;

    if (vault_exists) {
        if (!run_with_terminal_spinner(
                "Chargement du vault securise...",
                [&](std::string& operation_error) {
                    return app.load_vault(vault_path, master_password, operation_error);
                },
                std::cout,
                error)) {
            std::cerr << "Chargement impossible : " << error << '\n';
            return 1;
        }
    } else {
        std::string confirmation;
        SecretCleanup confirmation_cleanup(confirmation);
        if (!prompt_hidden_password("Confirmer le mot de passe maître : ",
                                   confirmation,
                                   error)) {
            std::cerr << error << '\n';
            return 1;
        }
        if (confirmation.empty()) {
            std::cerr << kEmptyPasswordError << '\n';
            return 1;
        }
        if (confirmation != master_password) {
            std::cerr << kPasswordMismatchError << '\n';
            return 1;
        }
        if (options.use_default_vault &&
            !ensure_default_vault_directory(vault_path, error)) {
            std::cerr << "Creation du vault impossible : " << error << '\n';
            return 1;
        }
        if (!run_with_terminal_spinner(
                "Creation du vault securise...",
                [&](std::string& operation_error) {
                    return app.save_vault(vault_path, master_password, operation_error);
                },
                std::cout,
                error)) {
            std::cerr << "Creation du vault impossible : " << error << '\n';
            return 1;
        }
    }

    return app.run(vault_path, master_password);
}
