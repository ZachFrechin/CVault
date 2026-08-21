#include "vault.hpp"

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

Vault::Vault() {}

Folder* Vault::get_folder(const std::string& name)
{
    for (auto& folder : this->folders) {
        if (folder.get_name() == name) {
            return &folder;
        }
    }

    return nullptr;
}

bool Vault::add_folder(Folder folder)
{
    if (is_blank(folder.get_name())) {
        return false;
    }
    for (auto& existing_folder : this->folders) {
        if (existing_folder.get_name() == folder.get_name()) {
            return false;
        }
    }

    this->folders.push_back(std::move(folder));
    return true;
}

std::vector<Folder>& Vault::get_folders()
{
    return this->folders;
}

const std::vector<Folder>& Vault::get_folders() const
{
    return this->folders;
}
