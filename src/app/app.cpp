#include "app.hpp"

#include "../tui/tui.hpp"

namespace {

constexpr char kNotInitializedError[] =
    "L'application doit etre initialisee avant cette operation.";

}  // namespace

App::App()
    : storage_(serializer_, crypto_)
{
}

bool App::initialize(std::string& error)
{
    if (is_initialized()) {
        error.clear();
        return true;
    }

    initialized_ = crypto_.initialize(error);
    return initialized_;
}

bool App::is_initialized() const noexcept
{
    return initialized_ && crypto_.is_initialized();
}

int App::run()
{
    if (!is_initialized()) {
        return 1;
    }

    Tui tui(vault_);
    tui.run();
    return 0;
}

int App::run(const std::filesystem::path& path,
             std::string_view master_password)
{
    if (!is_initialized() || path.empty() || master_password.empty()) {
        return 1;
    }

    const std::filesystem::path vault_path = path;
    Tui tui(vault_, [this, vault_path, master_password](const Vault& snapshot,
                                                        std::string& error) {
        return storage_.save(snapshot, vault_path, master_password, error);
    });
    tui.run();
    return 0;
}

bool App::save_vault(const std::filesystem::path& path,
                     std::string_view master_password,
                     std::string& error)
{
    if (!require_initialization(error)) {
        return false;
    }

    return storage_.save(vault_, path, master_password, error);
}

bool App::load_vault(const std::filesystem::path& path,
                     std::string_view master_password,
                     std::string& error)
{
    if (!require_initialization(error)) {
        return false;
    }

    return storage_.load(path, master_password, vault_, error);
}

Vault& App::vault() noexcept
{
    return vault_;
}

const Vault& App::vault() const noexcept
{
    return vault_;
}

bool App::require_initialization(std::string& error) const
{
    if (is_initialized()) {
        return true;
    }

    error = kNotInitializedError;
    return false;
}
