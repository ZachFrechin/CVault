#pragma once

#include <string>

#include <vector>

#include "folder.hpp"

/// @brief In-memory collection of password folders.
class Vault {
public:
    /// @brief Folders currently stored in the vault.
    std::vector<Folder> folders;

    /// @brief Construct an empty vault.
    Vault();

    /// @brief Return all folders as a read-only collection.
    /// @return Constant reference to the folder collection.
    const std::vector<Folder>& get_folders() const;

    /// @brief Return all folders as a mutable collection.
    /// @return Reference to the folder collection.
    std::vector<Folder>& get_folders();

    /// @brief Find a folder by its exact name.
    /// @param name Name to search for.
    /// @return Pointer to the matching folder, or nullptr when absent.
    Folder* get_folder(const std::string& name);

    /// @brief Add a non-blank folder unless another folder has the same name.
    /// @param folder Folder to append; duplicate names are ignored.
    /// @return True when the folder was appended, false when validation failed.
    bool add_folder(Folder folder);
};
