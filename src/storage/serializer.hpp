#pragma once

#include <string>

#include "../core/vault.hpp"

/// @brief Convert a Vault to and from the lightweight text format used by the demo.
class Serializer {
public:
    /// @brief Serialize every folder and entry in a vault.
    /// @param vault Vault to convert into text.
    /// @return Versioned text payload containing the vault data.
    std::string serialize(const Vault& vault) const;

    /// @brief Parse a text payload and populate a Vault instance.
    /// @param content Serialized payload to read.
    /// @param vault Output vault instance.
    /// @param error Error description on failure.
    /// @return True when parsing succeeds, false otherwise.
    bool deserialize(const std::string& content, Vault& vault, std::string& error) const;

private:
    /// @brief Escape special chars for safe text serialization.
    /// @param value Plain-text field to escape.
    /// @return Escaped field suitable for the tab-separated format.
    static std::string escape(const std::string& value);

    /// @brief Decode one escaped field.
    /// @param value Escaped input field.
    /// @param result Decoded output.
    /// @return True when decoding succeeds.
    static bool unescape(const std::string& value, std::string& result);
};
