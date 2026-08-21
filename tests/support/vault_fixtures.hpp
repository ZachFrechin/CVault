#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

#include "src/core/vault.hpp"

/// @brief Build a deterministic vault used by unit and integration tests.
/// @return Vault containing two folders and representative entries.
inline Vault make_fixture_vault()
{
    Vault vault;
    vault.add_folder(Folder("Personnel"));
    vault.add_folder(Folder("Travail"));
    vault.get_folder("Personnel")->add_entry(
        {"courriel", "alice@example.test", "secret-personnel"});
    vault.get_folder("Travail")->add_entry(
        {"serveur", "admin", "secret-travail"});
    return vault;
}

/// @brief Compare every persisted field of two vaults.
/// @param left First vault to compare.
/// @param right Second vault to compare.
/// @return True when folder and entry order and values are identical.
inline bool vaults_are_equal(const Vault& left, const Vault& right)
{
    const auto& left_folders = left.get_folders();
    const auto& right_folders = right.get_folders();
    if (left_folders.size() != right_folders.size()) {
        return false;
    }

    for (std::size_t folder_index = 0; folder_index < left_folders.size(); ++folder_index) {
        const auto& left_folder = left_folders[folder_index];
        const auto& right_folder = right_folders[folder_index];
        if (left_folder.get_name() != right_folder.get_name() ||
            left_folder.get_entries().size() != right_folder.get_entries().size()) {
            return false;
        }

        for (std::size_t entry_index = 0; entry_index < left_folder.get_entries().size();
             ++entry_index) {
            const Entry& left_entry = left_folder.get_entries()[entry_index];
            const Entry& right_entry = right_folder.get_entries()[entry_index];
            if (left_entry.id != right_entry.id || left_entry.login != right_entry.login ||
                left_entry.password != right_entry.password) {
                return false;
            }
        }
    }

    return true;
}

/// @brief Own a unique temporary vault path and remove it at scope exit.
class TemporaryVaultFile {
public:
    /// @brief Create a unique path in the platform temporary directory.
    TemporaryVaultFile()
    {
        static std::atomic<std::uint64_t> counter{0};
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("vaultcli-test-" + std::to_string(static_cast<std::uint64_t>(now)) +
                 "-" + std::to_string(counter.fetch_add(1, std::memory_order_relaxed)) +
                 ".vault");
    }

    /// @brief Remove the temporary file if a test created it.
    ~TemporaryVaultFile()
    {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }

    /// @brief Return the owned path.
    /// @return Temporary vault path.
    const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    /// @brief Path reserved for this test instance.
    std::filesystem::path path_;
};
