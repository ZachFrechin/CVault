#include "folder.hpp"

#include <algorithm>
#include <cctype>
#include <utility>

namespace {
bool is_blank(const std::string& value)
{
    return value.empty() || std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isspace(character) != 0;
           });
}
}  // namespace

Folder::Folder(std::string name)
{
    this->name = name;
}

bool Folder::add_entry(Entry entry)
{
    if (is_blank(entry.id) || is_blank(entry.login) || is_blank(entry.password)) {
        return false;
    }
    const auto duplicate = std::find_if(
        entries.begin(), entries.end(), [&](const Entry& existing) { return existing.id == entry.id; });
    if (duplicate != entries.end()) {
        return false;
    }
    this->entries.push_back(std::move(entry));
    return true;
}

void Folder::remove_entry(std::string entry_id)
{
    const auto new_end = std::remove_if(
        entries.begin(), entries.end(),
        [&](const Entry& entry) { return entry.id == entry_id; });
    entries.erase(new_end, entries.end());
}

const std::string& Folder::get_name() const
{
    return this->name;
}

const std::vector<Entry>& Folder::get_entries() const
{
    return this->entries;
}
