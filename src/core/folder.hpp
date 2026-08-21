#pragma once

#include <string>
#include <vector>

#include "entry.hpp"

/// @brief Group of password entries identified by a name.
class Folder {
private:
    /// @brief Human-readable folder name.
    std::string name;

    /// @brief Entries belonging to this folder.
    std::vector<Entry> entries;

public:
    /// @brief Construct a folder with the supplied name.
    /// @param name Human-readable name assigned to the folder.
    Folder(std::string name);

    /// @brief Return the folder name without copying it.
    /// @return Constant reference to the folder name.
    const std::string& get_name() const;

    /// @brief Return all entries in this folder without copying them.
    /// @return Constant reference to the entry collection.
    const std::vector<Entry>& get_entries() const;

    /// @brief Append an entry when its fields are valid and its identifier is unique.
    /// @param entry Entry to add; its value may be moved into the folder.
    /// @return True when the entry was appended, false when validation failed.
    bool add_entry(Entry entry);

    /// @brief Remove one entry matching an identifier.
    /// @param entry_id Identifier of the entries to remove.
    void remove_entry(std::string entry_id);
};
