#include "storage.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

constexpr char kMagic[] = "VAULTCLI";
constexpr std::size_t kMagicBytes = sizeof(kMagic) - 1;
constexpr std::size_t kAssociatedDataBytes =
    kMagicBytes + sizeof(std::uint32_t) + sizeof(std::uint64_t) +
    sizeof(std::uint64_t) + Crypto::kSaltBytes;
constexpr std::size_t kMinimumFileBytes =
    kAssociatedDataBytes + sizeof(std::uint64_t) + Crypto::kNonceBytes +
    Crypto::kAuthenticationTagBytes;

constexpr char kInvalidPathError[] = "Le chemin du vault est vide.";
constexpr char kInvalidPasswordError[] =
    "Le mot de passe maître ne peut pas etre vide.";
constexpr char kInvalidFormatError[] = "Le format du fichier vault est invalide.";
constexpr char kUnsupportedVersionError[] =
    "La version du fichier vault n'est pas prise en charge.";
constexpr char kFileTooLargeError[] =
    "Le fichier vault depasse la taille maximale autorisee.";
constexpr char kReadError[] = "Impossible de lire le fichier vault.";
constexpr char kTemporaryWriteError[] =
    "Impossible d'ecrire le fichier temporaire du vault.";
constexpr char kReplaceError[] =
    "Impossible de remplacer atomiquement le fichier vault.";

bool has_bytes(const Crypto::Bytes& input, std::size_t offset, std::size_t count)
{
    return offset <= input.size() && count <= input.size() - offset;
}

void remove_if_present(const std::filesystem::path& path) noexcept
{
    std::error_code error;
    std::filesystem::remove(path, error);
}

}  // namespace

Storage::Storage(Serializer& serializer, Crypto& crypto) noexcept
    : serializer_(serializer), crypto_(crypto)
{
}

bool Storage::save(const Vault& vault,
                   const std::filesystem::path& path,
                   std::string_view master_password,
                   std::string& error) const
{
    error.clear();
    if (master_password.empty()) {
        error = kInvalidPasswordError;
        return false;
    }
    if (path.empty()) {
        error = kInvalidPathError;
        return false;
    }

    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::error_code parent_error;
        if (!std::filesystem::is_directory(parent, parent_error)) {
            error = "Le dossier parent du vault est introuvable.";
            return false;
        }
    }

    std::string serialized = serializer_.serialize(vault);
    Crypto::Bytes plaintext(serialized.begin(), serialized.end());
    Crypto::clear(serialized);

    constexpr std::size_t fixed_bytes =
        kAssociatedDataBytes + sizeof(std::uint64_t) + Crypto::kNonceBytes +
        Crypto::kAuthenticationTagBytes;
    if (plaintext.size() > kMaximumFileBytes - fixed_bytes) {
        Crypto::clear(plaintext);
        error = kFileTooLargeError;
        return false;
    }

    const Crypto::KdfParameters parameters = Crypto::recommended_kdf_parameters();
    Crypto::Bytes salt;
    if (!crypto_.random_bytes(Crypto::kSaltBytes, salt, error)) {
        Crypto::clear(plaintext);
        return false;
    }

    Crypto::Bytes key;
    if (!crypto_.derive_key(master_password, salt, parameters, key, error)) {
        Crypto::clear(plaintext);
        Crypto::clear(salt);
        return false;
    }

    Crypto::Bytes associated_data = make_associated_data(parameters, salt);
    Crypto::EncryptedPayload encrypted;
    const bool encrypted_successfully = crypto_.encrypt(
        plaintext, key, associated_data, encrypted, error);
    Crypto::clear(key);
    Crypto::clear(plaintext);
    if (!encrypted_successfully) {
        Crypto::clear(salt);
        Crypto::clear(associated_data);
        return false;
    }

    Crypto::Bytes file_bytes = associated_data;
    append_u64(file_bytes, static_cast<std::uint64_t>(encrypted.ciphertext.size()));
    file_bytes.insert(file_bytes.end(), encrypted.nonce.begin(), encrypted.nonce.end());
    file_bytes.insert(
        file_bytes.end(), encrypted.ciphertext.begin(), encrypted.ciphertext.end());
    Crypto::clear(encrypted.nonce);
    Crypto::clear(encrypted.ciphertext);
    Crypto::clear(salt);
    Crypto::clear(associated_data);

    if (file_bytes.size() > kMaximumFileBytes) {
        Crypto::clear(file_bytes);
        error = kFileTooLargeError;
        return false;
    }

    const bool written = write_atomically(path, file_bytes, error);
    Crypto::clear(file_bytes);
    return written;
}

bool Storage::load(const std::filesystem::path& path,
                   std::string_view master_password,
                   Vault& vault,
                   std::string& error) const
{
    error.clear();
    if (master_password.empty()) {
        error = kInvalidPasswordError;
        return false;
    }
    if (path.empty()) {
        error = kInvalidPathError;
        return false;
    }

    std::error_code status_error;
    if (!std::filesystem::is_regular_file(path, status_error) || status_error) {
        error = kReadError;
        return false;
    }

    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        error = kReadError;
        return false;
    }

    const std::streampos end_position = input.tellg();
    if (end_position < 0 ||
        static_cast<std::uintmax_t>(end_position) > kMaximumFileBytes) {
        error = kFileTooLargeError;
        return false;
    }

    const std::size_t file_size = static_cast<std::size_t>(end_position);
    if (file_size < kMinimumFileBytes) {
        error = kInvalidFormatError;
        return false;
    }

    input.seekg(0, std::ios::beg);
    Crypto::Bytes file_bytes(file_size);
    input.read(reinterpret_cast<char*>(file_bytes.data()),
               static_cast<std::streamsize>(file_bytes.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(file_bytes.size())) {
        Crypto::clear(file_bytes);
        error = kReadError;
        return false;
    }

    std::size_t offset = 0;
    if (!has_bytes(file_bytes, offset, kMagicBytes) ||
        !std::equal(file_bytes.begin(), file_bytes.begin() + kMagicBytes, kMagic)) {
        Crypto::clear(file_bytes);
        error = kInvalidFormatError;
        return false;
    }
    offset += kMagicBytes;

    std::uint32_t version = 0;
    std::uint64_t ops_limit = 0;
    std::uint64_t memory_limit = 0;
    if (!read_u32(file_bytes, offset, version) ||
        !read_u64(file_bytes, offset, ops_limit) ||
        !read_u64(file_bytes, offset, memory_limit)) {
        Crypto::clear(file_bytes);
        error = kInvalidFormatError;
        return false;
    }
    if (version != kFormatVersion) {
        Crypto::clear(file_bytes);
        error = kUnsupportedVersionError;
        return false;
    }
    if (memory_limit > std::numeric_limits<std::size_t>::max()) {
        Crypto::clear(file_bytes);
        error = kInvalidFormatError;
        return false;
    }

    Crypto::Bytes salt;
    if (!has_bytes(file_bytes, offset, Crypto::kSaltBytes)) {
        Crypto::clear(file_bytes);
        error = kInvalidFormatError;
        return false;
    }
    salt.insert(salt.end(), file_bytes.begin() + offset,
                file_bytes.begin() + offset + Crypto::kSaltBytes);
    offset += Crypto::kSaltBytes;

    std::uint64_t ciphertext_size = 0;
    if (!read_u64(file_bytes, offset, ciphertext_size) ||
        ciphertext_size < Crypto::kAuthenticationTagBytes ||
        ciphertext_size > kMaximumFileBytes ||
        !has_bytes(file_bytes, offset, Crypto::kNonceBytes)) {
        Crypto::clear(file_bytes);
        Crypto::clear(salt);
        error = kInvalidFormatError;
        return false;
    }

    Crypto::EncryptedPayload encrypted;
    encrypted.nonce.insert(encrypted.nonce.end(), file_bytes.begin() + offset,
                           file_bytes.begin() + offset + Crypto::kNonceBytes);
    offset += Crypto::kNonceBytes;
    if (ciphertext_size != file_bytes.size() - offset) {
        Crypto::clear(file_bytes);
        Crypto::clear(salt);
        error = kInvalidFormatError;
        return false;
    }
    encrypted.ciphertext.insert(
        encrypted.ciphertext.end(), file_bytes.begin() + offset, file_bytes.end());

    const Crypto::KdfParameters parameters{
        ops_limit, static_cast<std::size_t>(memory_limit)};
    Crypto::Bytes key;
    if (!crypto_.derive_key(master_password, salt, parameters, key, error)) {
        Crypto::clear(file_bytes);
        Crypto::clear(salt);
        return false;
    }

    Crypto::Bytes associated_data = make_associated_data(parameters, salt);
    Crypto::Bytes plaintext;
    const bool decrypted = crypto_.decrypt(
        encrypted, key, associated_data, plaintext, error);
    Crypto::clear(encrypted.nonce);
    Crypto::clear(encrypted.ciphertext);
    Crypto::clear(key);
    Crypto::clear(salt);
    Crypto::clear(associated_data);
    Crypto::clear(file_bytes);
    if (!decrypted) {
        return false;
    }

    std::string serialized(plaintext.begin(), plaintext.end());
    Vault restored;
    const bool deserialized = serializer_.deserialize(serialized, restored, error);
    Crypto::clear(serialized);
    Crypto::clear(plaintext);
    if (!deserialized) {
        return false;
    }

    vault = std::move(restored);
    error.clear();
    return true;
}

Crypto::Bytes Storage::make_associated_data(
    const Crypto::KdfParameters& parameters, const Crypto::Bytes& salt)
{
    Crypto::Bytes data;
    data.reserve(kAssociatedDataBytes);
    data.insert(data.end(), kMagic, kMagic + kMagicBytes);
    append_u32(data, kFormatVersion);
    append_u64(data, parameters.ops_limit);
    append_u64(data, static_cast<std::uint64_t>(parameters.memory_limit));
    data.insert(data.end(), salt.begin(), salt.end());
    return data;
}

void Storage::append_u32(Crypto::Bytes& output, std::uint32_t value)
{
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        output.push_back(static_cast<std::uint8_t>((value >> (index * 8)) & 0xffU));
    }
}

void Storage::append_u64(Crypto::Bytes& output, std::uint64_t value)
{
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        output.push_back(static_cast<std::uint8_t>((value >> (index * 8)) & 0xffU));
    }
}

bool Storage::read_u32(const Crypto::Bytes& input,
                       std::size_t& offset,
                       std::uint32_t& value)
{
    if (!has_bytes(input, offset, sizeof(value))) {
        return false;
    }

    value = 0;
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value |= static_cast<std::uint32_t>(input[offset + index]) << (index * 8);
    }
    offset += sizeof(value);
    return true;
}

bool Storage::read_u64(const Crypto::Bytes& input,
                       std::size_t& offset,
                       std::uint64_t& value)
{
    if (!has_bytes(input, offset, sizeof(value))) {
        return false;
    }

    value = 0;
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        value |= static_cast<std::uint64_t>(input[offset + index]) << (index * 8);
    }
    offset += sizeof(value);
    return true;
}

bool Storage::write_atomically(const std::filesystem::path& path,
                               const Crypto::Bytes& bytes,
                               std::string& error) const
{
    Crypto::Bytes suffix_bytes;
    std::string random_error;
    if (!crypto_.random_bytes(16, suffix_bytes, random_error)) {
        error = kTemporaryWriteError;
        return false;
    }

    std::ostringstream suffix;
    suffix << std::hex;
    for (const std::uint8_t byte : suffix_bytes) {
        suffix.width(2);
        suffix.fill('0');
        suffix << static_cast<unsigned int>(byte);
    }
    Crypto::clear(suffix_bytes);

    std::filesystem::path temporary_path = path;
    temporary_path += ".tmp." + suffix.str();

#ifndef _WIN32
    const std::filesystem::path parent = path.parent_path().empty()
                                             ? std::filesystem::path(".")
                                             : path.parent_path();
    const int directory_fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd < 0) {
        error = kTemporaryWriteError;
        return false;
    }

    const int file_fd = ::open(temporary_path.c_str(),
                               O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                               S_IRUSR | S_IWUSR);
    if (file_fd < 0) {
        ::close(directory_fd);
        error = kTemporaryWriteError;
        return false;
    }

    bool write_ok = true;
    std::size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t count = ::write(file_fd,
                                      bytes.data() + written,
                                      bytes.size() - written);
        if (count > 0) {
            written += static_cast<std::size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        write_ok = false;
        break;
    }
    if (write_ok && ::fsync(file_fd) != 0) {
        write_ok = false;
    }
    if (::close(file_fd) != 0) {
        write_ok = false;
    }
    if (!write_ok) {
        remove_if_present(temporary_path);
        ::close(directory_fd);
        error = kTemporaryWriteError;
        return false;
    }

    if (::rename(temporary_path.c_str(), path.c_str()) != 0) {
        remove_if_present(temporary_path);
        ::close(directory_fd);
        error = kReplaceError;
        return false;
    }

    const bool directory_synced = ::fsync(directory_fd) == 0;
    ::close(directory_fd);
    if (!directory_synced) {
        error = kReplaceError;
        return false;
    }
#else
    const HANDLE file_handle = CreateFileW(
        temporary_path.wstring().c_str(),
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_NEW,
        FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH,
        nullptr);
    if (file_handle == INVALID_HANDLE_VALUE) {
        error = kTemporaryWriteError;
        return false;
    }

    bool write_ok = true;
    std::size_t written = 0;
    while (written < bytes.size()) {
        const DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(
            bytes.size() - written, static_cast<std::size_t>(MAXDWORD)));
        DWORD count = 0;
        if (!WriteFile(file_handle, bytes.data() + written, chunk, &count, nullptr) ||
            count == 0) {
            write_ok = false;
            break;
        }
        written += count;
    }
    if (write_ok && !FlushFileBuffers(file_handle)) {
        write_ok = false;
    }
    if (!CloseHandle(file_handle)) {
        write_ok = false;
    }
    if (!write_ok) {
        remove_if_present(temporary_path);
        error = kTemporaryWriteError;
        return false;
    }

    if (!MoveFileExW(temporary_path.wstring().c_str(),
                     path.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        remove_if_present(temporary_path);
        error = kReplaceError;
        return false;
    }
#endif
    error.clear();
    return true;
}
