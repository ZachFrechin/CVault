#pragma once

#include <functional>
#include <string>
#include <string_view>

/// @brief Small platform boundary used to copy non-persistent text to the clipboard.
class Clipboard {
public:
    /// @brief Injectable operation used by unit tests or an embedding application.
    using CopyFunction = std::function<bool(std::string_view text, std::string& error)>;

    /// @brief Construct a clipboard using the platform backend by default.
    /// @param copy_function Optional operation replacing the platform backend.
    explicit Clipboard(CopyFunction copy_function = {});

    /// @brief Copy text without placing it in a command-line argument.
    /// @param text Text to send to the system clipboard.
    /// @param error Output description when the operation fails.
    /// @return True when the platform accepted the text.
    bool copy(std::string_view text, std::string& error) const;

private:
    /// @brief Execute the selected platform implementation.
    /// @param text Text to send to the system clipboard.
    /// @param error Output description when the operation fails.
    /// @return True when the operation succeeds.
    static bool copy_platform(std::string_view text, std::string& error);

    /// @brief Optional injected implementation.
    CopyFunction copy_function_;
};
