#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "../core/vault.hpp"
#include "../crypto/crypto.hpp"
#include "serializer.hpp"

/// @brief Persists complete vaults as authenticated, encrypted files.
///
/// Storage owns no vault and retains no password. It coordinates the
/// serializer and cryptographic service for each explicit save or load call.
class Storage {
public:
    /// @brief Construct a storage boundary from the application services.
    /// @param serializer Serializer used for the encrypted payload plaintext.
    /// @param crypto Cryptographic service used for key derivation and AEAD.
    Storage(Serializer& serializer, Crypto& crypto) noexcept;

    /// @brief Save a vault to an encrypted versioned file.
    /// @param vault Vault to serialize and protect.
    /// @param path Destination file path supplied by the caller.
    /// @param master_password Password used only during this operation.
    /// @param error Output description when validation, encryption or I/O fails.
    /// @return True when the file was replaced successfully, false otherwise.
    bool save(const Vault& vault,
              const std::filesystem::path& path,
              std::string_view master_password,
              std::string& error) const;

    /// @brief Load and authenticate a vault file.
    /// @param path Source file path supplied by the caller.
    /// @param master_password Password used only during this operation.
    /// @param vault Destination replaced only after complete success.
    /// @param error Output description when validation, decryption or I/O fails.
    /// @return True when the vault was loaded successfully, false otherwise.
    bool load(const std::filesystem::path& path,
              std::string_view master_password,
              Vault& vault,
              std::string& error) const;

private:
    /// @brief Current on-disk format version.
    static constexpr std::uint32_t kFormatVersion = 1;

    /// @brief Maximum complete file size accepted by the small-vault format.
    static constexpr std::size_t kMaximumFileBytes = 64U * 1024U * 1024U;

    /// @brief Build authenticated metadata shared by save and load.
    /// @param parameters Argon2id parameters stored in the header.
    /// @param salt Argon2id salt stored in the header.
    /// @return Header metadata passed as AEAD associated data.
    static Crypto::Bytes make_associated_data(
        const Crypto::KdfParameters& parameters, const Crypto::Bytes& salt);

    /// @brief Append an unsigned integer using the format's little-endian encoding.
    /// @param output Destination byte buffer.
    /// @param value Integer to append.
    static void append_u32(Crypto::Bytes& output, std::uint32_t value);

    /// @brief Append an unsigned integer using the format's little-endian encoding.
    /// @param output Destination byte buffer.
    /// @param value Integer to append.
    static void append_u64(Crypto::Bytes& output, std::uint64_t value);

    /// @brief Read a little-endian 32-bit integer at the current offset.
    /// @param input Complete file bytes.
    /// @param offset Read position, advanced on success.
    /// @param value Destination integer.
    /// @return True when four bytes were available.
    static bool read_u32(const Crypto::Bytes& input,
                         std::size_t& offset,
                         std::uint32_t& value);

    /// @brief Read a little-endian 64-bit integer at the current offset.
    /// @param input Complete file bytes.
    /// @param offset Read position, advanced on success.
    /// @param value Destination integer.
    /// @return True when eight bytes were available.
    static bool read_u64(const Crypto::Bytes& input,
                         std::size_t& offset,
                         std::uint64_t& value);

    /// @brief Write bytes to a temporary sibling and atomically replace the target.
    /// @param path Final destination path.
    /// @param bytes Complete encoded file contents.
    /// @param error Output description when the replacement fails.
    /// @return True when the replacement succeeds, false otherwise.
    bool write_atomically(const std::filesystem::path& path,
                          const Crypto::Bytes& bytes,
                          std::string& error) const;

    /// @brief Stateless serializer used for vault plaintext.
    Serializer& serializer_;

    /// @brief Initialized libsodium service used for protection operations.
    Crypto& crypto_;
};
