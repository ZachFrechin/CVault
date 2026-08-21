#include "serializer.hpp"

#include <sstream>
#include <utility>
#include <vector>

namespace {

constexpr char kHex[] = "0123456789ABCDEF";

/// @brief Convert a single hexadecimal character to its numeric value.
int hex_value(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

/// @brief Split one input line into fields separated by tab characters.
std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::stringstream stream(line);
    while (std::getline(stream, field, '\t')) {
        fields.push_back(field);
    }
    return fields;
}

}  // namespace

std::string Serializer::serialize(const Vault& vault) const {
    std::ostringstream output;
    output << "VAULTCLI-1\n";
    for (const Folder& folder : vault.get_folders()) {
        output << "F\t" << escape(folder.get_name()) << '\n';
        for (const Entry& entry : folder.get_entries()) {
            output << "E\t" << escape(entry.id) << '\t' << escape(entry.login) << '\t'
                   << escape(entry.password) << '\n';
        }
    }
    return output.str();
}

bool Serializer::deserialize(const std::string& content, Vault& vault, std::string& error) const {
    std::istringstream input(content);
    std::string line;
    if (!std::getline(input, line) || line != "VAULTCLI-1") {
        error = "Format de vault invalide ou version non prise en charge.";
        return false;
    }

    Vault parsed;
    Folder* current_folder = nullptr;
    int line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty()) continue;
        const auto fields = split(line);
        if (fields[0] == "F" && fields.size() == 2) {
            std::string name;
            if (!unescape(fields[1], name)) {
                error = "Nom de dossier invalide a la ligne " + std::to_string(line_number) + ".";
                return false;
            }
            if (!parsed.add_folder(Folder(std::move(name)))) {
                error = "Nom de dossier vide ou duplique a la ligne " +
                        std::to_string(line_number) + ".";
                return false;
            }
            current_folder = &parsed.get_folders().back();
            continue;
        }
        if (fields[0] == "E" && fields.size() == 4 && current_folder != nullptr) {
            Entry entry;
            if (!unescape(fields[1], entry.id) || !unescape(fields[2], entry.login) ||
                !unescape(fields[3], entry.password)) {
                error = "Entree invalide a la ligne " + std::to_string(line_number) + ".";
                return false;
            }
            if (!current_folder->add_entry(std::move(entry))) {
                error = "Entree vide ou dupliquee a la ligne " + std::to_string(line_number) + ".";
                return false;
            }
            continue;
        }
        error = "Structure invalide a la ligne " + std::to_string(line_number) + ".";
        return false;
    }

    vault = std::move(parsed);
    error.clear();
    return true;
}

std::string Serializer::escape(const std::string& value) {
    std::string escaped;
    for (unsigned char character : value) {
        if (character == '%' || character == '\t' || character == '\n' || character == '\r') {
            escaped += '%';
            escaped += kHex[character >> 4];
            escaped += kHex[character & 0x0F];
        } else {
            escaped += static_cast<char>(character);
        }
    }
    return escaped;
}

bool Serializer::unescape(const std::string& value, std::string& result) {
    result.clear();
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (value[index] != '%') {
            result += value[index];
            continue;
        }
        if (index + 2 >= value.size()) return false;
        const int high = hex_value(value[index + 1]);
        const int low = hex_value(value[index + 2]);
        if (high < 0 || low < 0) return false;
        result += static_cast<char>((high << 4) | low);
        index += 2;
    }
    return true;
}
