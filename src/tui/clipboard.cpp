#include "clipboard.hpp"

#include <cstdio>
#include <climits>
#include <string>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/wait.h>
#endif

namespace {

#if !defined(_WIN32)
bool copy_with_command(const char* command, std::string_view text, std::string& error)
{
    FILE* pipe = popen(command, "w");
    if (pipe == nullptr) {
        error = "Le presse-papiers systeme est indisponible.";
        return false;
    }

    const std::size_t written = fwrite(text.data(), 1, text.size(), pipe);
    const int close_status = pclose(pipe);
    const bool process_succeeded = close_status != -1 &&
                                   WIFEXITED(close_status) && WEXITSTATUS(close_status) == 0;
    if (written != text.size() || !process_succeeded) {
        error = "Le presse-papiers systeme est indisponible.";
        return false;
    }
    error.clear();
    return true;
}
#endif

}  // namespace

Clipboard::Clipboard(CopyFunction copy_function) : copy_function_(std::move(copy_function))
{
}

bool Clipboard::copy(std::string_view text, std::string& error) const
{
    if (copy_function_) {
        return copy_function_(text, error);
    }
    return copy_platform(text, error);
}

bool Clipboard::copy_platform(std::string_view text, std::string& error)
{
#if defined(_WIN32)
    if (text.size() > static_cast<std::size_t>(INT_MAX)) {
        error = "Le texte est trop volumineux pour le presse-papiers.";
        return false;
    }
    const int utf16_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                                  static_cast<int>(text.size()), nullptr, 0);
    if (utf16_length <= 0) {
        error = "Le texte ne peut pas etre copie dans le presse-papiers.";
        return false;
    }
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (static_cast<std::size_t>(utf16_length) + 1) * sizeof(wchar_t));
    if (memory == nullptr) {
        error = "Le presse-papiers systeme est indisponible.";
        return false;
    }
    auto* destination = static_cast<wchar_t*>(GlobalLock(memory));
    if (destination == nullptr) {
        GlobalFree(memory);
        error = "Le presse-papiers systeme est indisponible.";
        return false;
    }
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()),
                        destination, utf16_length);
    destination[utf16_length] = L'\0';
    GlobalUnlock(memory);

    if (!OpenClipboard(nullptr)) {
        GlobalFree(memory);
        error = "Le presse-papiers systeme est indisponible.";
        return false;
    }
    EmptyClipboard();
    if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
        GlobalFree(memory);
        CloseClipboard();
        error = "Le presse-papiers systeme est indisponible.";
        return false;
    }
    CloseClipboard();
    error.clear();
    return true;
#elif defined(__APPLE__)
    return copy_with_command("/usr/bin/pbcopy", text, error);
#else
    if (copy_with_command("wl-copy", text, error)) {
        return true;
    }
    return copy_with_command("xclip -selection clipboard", text, error);
#endif
}
