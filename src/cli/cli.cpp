#include "cli.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <iostream>
#include <string_view>
#include <system_error>
#include <thread>

#include "../crypto/crypto.hpp"

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace {

constexpr char kUsage[] =
    "Usage: vault\n"
    "       vault --vault\n"
    "       vault --vault <chemin>\n"
    "       vault --help\n";

constexpr char kDefaultPathUsage[] =
    "\nVault par defaut :\n"
    "  macOS   : ~/Library/Application Support/VaultCLI/default.vault\n"
    "  Linux   : $XDG_DATA_HOME/vaultcli/default.vault\n"
    "            ou ~/.local/share/vaultcli/default.vault\n"
    "  Windows : %APPDATA%\\VaultCLI\\default.vault\n";

constexpr char kInvalidArguments[] =
    "Arguments invalides. Utilisez --help pour afficher l'usage.";
constexpr char kMissingVaultPath[] =
    "L'option --vault doit etre suivie d'un chemin non vide.";
constexpr char kTerminalError[] =
    "Impossible de lire le mot de passe depuis le terminal.";
constexpr char kDefaultPathUnavailable[] =
    "Impossible de determiner le dossier utilisateur du vault par defaut.";
constexpr char kDefaultPathRelative[] =
    "Le dossier utilisateur du vault par defaut doit etre absolu.";
constexpr char kInvalidVaultPath[] =
    "Le chemin du vault ne peut pas etre vide.";
constexpr char kDefaultDirectoryError[] =
    "Impossible de preparer le dossier du vault par defaut.";

std::filesystem::path environment_path(const char* name)
{
    const char* value = std::getenv(name);
    return value == nullptr ? std::filesystem::path{} : std::filesystem::path(value);
}

bool require_absolute(const std::filesystem::path& value,
                      std::string& error,
                      bool allow_windows_drive = false)
{
    if (value.empty()) {
        error = kDefaultPathUnavailable;
        return false;
    }
    const std::string generic_value = value.generic_string();
    const bool windows_drive_path = allow_windows_drive && generic_value.size() >= 3 &&
                                    ((generic_value[0] >= 'A' && generic_value[0] <= 'Z') ||
                                     (generic_value[0] >= 'a' && generic_value[0] <= 'z')) &&
                                    generic_value[1] == ':' && generic_value[2] == '/';
    if (!value.is_absolute() && !windows_drive_path) {
        error = kDefaultPathRelative;
        return false;
    }
    return true;
}

}  // namespace

bool parse_startup_options(int argc,
                           char* const argv[],
                           StartupOptions& options,
                           std::string& error)
{
    options = StartupOptions{};
    error.clear();
    if (argc <= 0 || argv == nullptr) {
        error = kInvalidArguments;
        return false;
    }

    bool path_seen = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index] == nullptr ? "" : argv[index];
        if (argument == "--help") {
            if (argc != 2) {
                error = kInvalidArguments;
                return false;
            }
            options.show_help = true;
            return true;
        }
        if (argument != "--vault" || path_seen) {
            error = kInvalidArguments;
            return false;
        }

        path_seen = true;
        if (index + 1 >= argc) {
            options.use_default_vault = true;
            continue;
        }

        const std::string_view next_argument = argv[index + 1] == nullptr ? "" : argv[index + 1];
        const bool looks_like_option = next_argument.size() >= 2 &&
                                       next_argument[0] == '-' && next_argument[1] == '-';
        if (next_argument.empty()) {
            error = kMissingVaultPath;
            return false;
        }
        if (looks_like_option) {
            error = kInvalidArguments;
            return false;
        }

        options.vault_path = argv[++index];
        options.use_default_vault = false;
    }

    return true;
}

bool current_default_vault_environment(DefaultVaultEnvironment& environment,
                                       std::string& error)
{
    environment = DefaultVaultEnvironment{};
    error.clear();

#ifdef __APPLE__
    environment.platform = StartupPlatform::macos;
    environment.home_directory = environment_path("HOME");
#elif defined(_WIN32)
    environment.platform = StartupPlatform::windows;
    environment.app_data_directory = environment_path("APPDATA");
    if (environment.app_data_directory.empty()) {
        const std::filesystem::path user_profile = environment_path("USERPROFILE");
        if (!user_profile.empty()) {
            environment.app_data_directory = user_profile / "AppData" / "Roaming";
        }
    }
#else
    environment.platform = StartupPlatform::linux_;
    environment.home_directory = environment_path("HOME");
    environment.xdg_data_home = environment_path("XDG_DATA_HOME");
#endif

    std::filesystem::path ignored;
    return resolve_default_vault_path(environment, ignored, error);
}

bool resolve_default_vault_path(const DefaultVaultEnvironment& environment,
                                std::filesystem::path& path,
                                std::string& error)
{
    path.clear();
    error.clear();

    switch (environment.platform) {
    case StartupPlatform::macos:
        if (!require_absolute(environment.home_directory, error)) {
            return false;
        }
        path = environment.home_directory / "Library" / "Application Support" /
               "VaultCLI" / "default.vault";
        return true;

    case StartupPlatform::linux_: {
        std::filesystem::path base;
        if (!environment.xdg_data_home.empty()) {
            if (!require_absolute(environment.xdg_data_home, error)) {
                return false;
            }
            base = environment.xdg_data_home;
        } else {
            if (!require_absolute(environment.home_directory, error)) {
                return false;
            }
            base = environment.home_directory / ".local" / "share";
        }
        path = base / "vaultcli" / "default.vault";
        return true;
    }

    case StartupPlatform::windows:
        if (!require_absolute(environment.app_data_directory, error, true)) {
            return false;
        }
        path = environment.app_data_directory / "VaultCLI" / "default.vault";
        return true;
    }

    error = kDefaultPathUnavailable;
    return false;
}

bool resolve_vault_path(const StartupOptions& options,
                        std::filesystem::path& path,
                        std::string& error)
{
    path.clear();
    error.clear();
    if (!options.use_default_vault) {
        if (options.vault_path.empty()) {
            error = kInvalidVaultPath;
            return false;
        }
        path = options.vault_path;
        return true;
    }

    DefaultVaultEnvironment environment;
    if (!current_default_vault_environment(environment, error)) {
        return false;
    }
    return resolve_default_vault_path(environment, path, error);
}

bool ensure_default_vault_directory(const std::filesystem::path& vault_path,
                                    std::string& error)
{
    error.clear();
    if (vault_path.empty() || vault_path.parent_path().empty()) {
        error = kDefaultDirectoryError;
        return false;
    }

    const std::filesystem::path parent = vault_path.parent_path();
    std::error_code status_error;
    const std::filesystem::file_status status =
        std::filesystem::symlink_status(parent, status_error);
    if (status_error && status_error != std::make_error_code(std::errc::no_such_file_or_directory)) {
        error = kDefaultDirectoryError;
        return false;
    }
    if (!status_error && std::filesystem::is_symlink(status)) {
        error = kDefaultDirectoryError;
        return false;
    }

    std::error_code directory_error;
    if (!std::filesystem::create_directories(parent, directory_error) &&
        directory_error) {
        error = kDefaultDirectoryError;
        return false;
    }

    std::error_code check_error;
    if (!std::filesystem::is_directory(parent, check_error) || check_error) {
        error = kDefaultDirectoryError;
        return false;
    }

#ifndef _WIN32
    if (::chmod(parent.c_str(), S_IRWXU) != 0) {
        error = kDefaultDirectoryError;
        return false;
    }
#endif

    return true;
}

std::string startup_usage()
{
    return std::string(kUsage) + kDefaultPathUsage;
}

bool run_with_terminal_spinner(std::string_view label,
                               const std::function<bool(std::string& error)>& task,
                               std::ostream& output,
                               std::string& error)
{
    error.clear();
    if (!task) {
        error = "Operation de demarrage indisponible.";
        return false;
    }

    auto operation = std::async(std::launch::async, [task] {
        std::string task_error;
        bool success = false;
        try {
            success = task(task_error);
        } catch (...) {
            task_error = "Une erreur inattendue est survenue pendant le demarrage.";
        }
        return std::pair<bool, std::string>{success, std::move(task_error)};
    });

    static constexpr std::array<const char*, 4> frames = {"|", "/", "-", "\\"};
    std::size_t frame = 0;
    const auto clear_width = label.size() + 3;
    output << '\r' << label << ' ' << frames[frame] << std::flush;
    while (operation.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready) {
        output << '\r' << label << ' ' << frames[++frame % frames.size()] << std::flush;
    }

    const auto result = operation.get();
    output << '\r' << std::string(clear_width, ' ') << '\r' << std::flush;
    if (!result.first) {
        error = result.second.empty() ? "Operation de demarrage echouee." : result.second;
        return false;
    }
    return true;
}

#ifdef _WIN32

bool prompt_hidden_password(std::string_view prompt,
                            std::string& password,
                            std::string& error)
{
    Crypto::clear(password);
    error.clear();
    std::cout << prompt << std::flush;

    while (true) {
        const int character = _getwch();
        if (character == EOF) {
            std::cout << '\n';
            error = kTerminalError;
            Crypto::clear(password);
            return false;
        }
        if (character == '\r' || character == '\n') {
            std::cout << '\n';
            return true;
        }
        if (character == '\b') {
            if (!password.empty()) {
                password.pop_back();
            }
            continue;
        }
        if (character == 0 || character == 0xE0) {
            static_cast<void>(_getwch());
            continue;
        }
        password.push_back(static_cast<char>(character));
    }
}

#else

class TerminalEchoGuard {
public:
    TerminalEchoGuard() noexcept
    {
        if (tcgetattr(STDIN_FILENO, &original_) != 0) {
            return;
        }
        termios hidden = original_;
        hidden.c_lflag &= static_cast<tcflag_t>(~ECHO);
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) == 0) {
            active_ = true;
        }
    }

    TerminalEchoGuard(const TerminalEchoGuard&) = delete;
    TerminalEchoGuard& operator=(const TerminalEchoGuard&) = delete;

    ~TerminalEchoGuard()
    {
        if (active_) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
        }
    }

    bool active() const noexcept
    {
        return active_;
    }

private:
    termios original_{};
    bool active_ = false;
};

bool prompt_hidden_password(std::string_view prompt,
                            std::string& password,
                            std::string& error)
{
    Crypto::clear(password);
    error.clear();
    TerminalEchoGuard echo_guard;
    if (!echo_guard.active()) {
        error = kTerminalError;
        return false;
    }

    std::cout << prompt << std::flush;
    const bool read = static_cast<bool>(std::getline(std::cin, password));
    std::cout << '\n';
    if (!read) {
        Crypto::clear(password);
        error = kTerminalError;
        return false;
    }
    return true;
}

#endif
