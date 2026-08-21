#pragma once

#include <string>

/// @brief Login record stored inside a vault folder.
struct Entry {
    /// @brief Stable identifier or label of the entry.
    std::string id;

    /// @brief Login or username associated with the entry.
    std::string login;

    /// @brief Secret associated with the entry.
    std::string password;
};
