#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "../core/vault.hpp"
#include "../crypto/crypto.hpp"
#include "../storage/serializer.hpp"
#include "../storage/storage.hpp"

/// @brief Coordinates the vault domain, cryptography, serialization and terminal UI.
///
/// App owns the in-memory session and coordinates the storage and UI layers.
/// The storage service handles serialization, encryption and file replacement;
/// the application only exposes the vault-oriented workflow.
class App {
public:
    /// @brief Construct an application with an empty vault and uninitialized services.
    App();

    /// @brief Prevent accidental copies of the in-memory vault session.
    App(const App&) = delete;

    /// @brief Prevent accidental copies of the in-memory vault session.
    App& operator=(const App&) = delete;

    /// @brief Initialize the cryptography service before storage or UI operations.
    /// @param error Output description when a service cannot be initialized.
    /// @return True when the application is ready, false otherwise.
    bool initialize(std::string& error);

    /// @brief Report whether all mandatory application services are ready.
    /// @return True after a successful call to initialize().
    bool is_initialized() const noexcept;

    /// @brief Run the terminal user interface for the current vault.
    /// @return Zero when the UI exits normally, otherwise a process error status.
    int run();

    /// @brief Run the terminal UI with immediate encrypted persistence.
    /// @param path Vault file used by the save callback for each mutation.
    /// @param master_password Password view valid for the duration of the UI loop.
    /// @return Zero when the UI exits normally, otherwise a process error status.
    int run(const std::filesystem::path& path,
            std::string_view master_password);

    /// @brief Save the current vault through the encrypted storage boundary.
    /// @param path Destination file path supplied by the caller.
    /// @param master_password Password used only for this save operation.
    /// @param error Output description when saving fails.
    /// @return True when the file was saved successfully, false otherwise.
    bool save_vault(const std::filesystem::path& path,
                    std::string_view master_password,
                    std::string& error);

    /// @brief Load and atomically install a vault through the storage boundary.
    /// @param path Source file path supplied by the caller.
    /// @param master_password Password used only for this load operation.
    /// @param error Output description when loading fails.
    /// @return True when the vault was loaded successfully, false otherwise.
    bool load_vault(const std::filesystem::path& path,
                    std::string_view master_password,
                    std::string& error);

    /// @brief Access the in-memory vault managed by this application session.
    /// @return Mutable reference to the current vault.
    Vault& vault() noexcept;

    /// @brief Access the in-memory vault without allowing modifications.
    /// @return Constant reference to the current vault.
    const Vault& vault() const noexcept;

private:
    /// @brief Reject protected operations before the application is initialized.
    /// @param error Output description when the service is unavailable.
    /// @return True when cryptographic operations are available, false otherwise.
    bool require_initialization(std::string& error) const;

    /// @brief In-memory password vault edited by the TUI and restoration workflow.
    Vault vault_;

    /// @brief Stateless transformation between the vault model and its text format.
    Serializer serializer_;

    /// @brief Cryptographic service used by the storage boundary.
    Crypto crypto_;

    /// @brief Encrypted file boundary composed from the serializer and crypto service.
    Storage storage_;

    /// @brief Whether application startup has completed successfully.
    bool initialized_ = false;
};
