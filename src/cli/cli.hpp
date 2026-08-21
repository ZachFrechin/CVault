#pragma once

#include <filesystem>
#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>

/// @brief Options selected before the terminal interface starts.
struct StartupOptions {
    /// @brief Explicit path of the encrypted vault file, when supplied.
    std::filesystem::path vault_path;

    /// @brief Whether the platform-specific user vault should be used.
    bool use_default_vault = true;

    /// @brief Whether the usage text was requested.
    bool show_help = false;
};

/// @brief Supported platform families for default-path resolution.
enum class StartupPlatform {
    /// @brief Apple desktop layout.
    macos,
    /// @brief XDG-compatible Unix layout.
    linux_,
    /// @brief Windows roaming application-data layout.
    windows,
};

/// @brief Environment values used to resolve the default vault path.
///
/// Keeping the values separate from process-global environment access makes
/// path resolution deterministic in unit tests and keeps platform policy in a
/// single helper.
struct DefaultVaultEnvironment {
    /// @brief Platform layout to apply.
    StartupPlatform platform = StartupPlatform::macos;

    /// @brief User home directory, used by macOS and Linux.
    std::filesystem::path home_directory;

    /// @brief Optional XDG data directory, used by Linux when non-empty.
    std::filesystem::path xdg_data_home;

    /// @brief Windows roaming application-data directory.
    std::filesystem::path app_data_directory;
};

/// @brief Parse the supported command-line options.
/// @param argc Number of arguments supplied by the operating system.
/// @param argv Argument vector, including the executable name at index zero.
/// @param options Destination options replaced on success.
/// @param error Output description for an invalid invocation.
/// @return True for a valid invocation, including --help.
bool parse_startup_options(int argc,
                           char* const argv[],
                           StartupOptions& options,
                           std::string& error);

/// @brief Read the current process environment for default-path resolution.
/// @param environment Destination values replaced on success.
/// @param error Output description when the required user directory is absent.
/// @return True when the environment is usable for the current platform.
bool current_default_vault_environment(DefaultVaultEnvironment& environment,
                                       std::string& error);

/// @brief Resolve the platform-specific encrypted vault path.
/// @param environment Platform and directory values used for resolution.
/// @param path Destination path replaced on success.
/// @param error Output description when the environment is incomplete.
/// @return True when a valid default path was produced.
bool resolve_default_vault_path(const DefaultVaultEnvironment& environment,
                                std::filesystem::path& path,
                                std::string& error);

/// @brief Resolve either the explicit CLI path or the platform default.
/// @param options Parsed startup options.
/// @param path Destination path replaced on success.
/// @param error Output description when path resolution fails.
/// @return True when a usable vault path was produced.
bool resolve_vault_path(const StartupOptions& options,
                        std::filesystem::path& path,
                        std::string& error);

/// @brief Create and protect the parent directory of the default vault.
/// @param vault_path Resolved default vault path.
/// @param error Output description when the directory cannot be prepared.
/// @return True when the parent exists with the expected permissions.
bool ensure_default_vault_directory(const std::filesystem::path& vault_path,
                                    std::string& error);

/// @brief Return the command-line usage text.
/// @return Short usage text suitable for stdout or stderr.
std::string startup_usage();

/// @brief Read one password without echoing it to the terminal.
/// @param prompt Text displayed before reading the password.
/// @param password Destination password, cleared before reading.
/// @param error Output description when the terminal cannot be configured/read.
/// @return True when one line was read successfully.
bool prompt_hidden_password(std::string_view prompt,
                            std::string& password,
                            std::string& error);

/// @brief Execute one startup operation while displaying a terminal spinner.
/// @param label Non-sensitive operation label shown to the user.
/// @param task Background operation returning its error through the argument.
/// @param output Stream receiving the spinner frames.
/// @param error Output description when the operation fails or throws.
/// @return True when the operation completed successfully.
bool run_with_terminal_spinner(std::string_view label,
                               const std::function<bool(std::string& error)>& task,
                               std::ostream& output,
                               std::string& error);
