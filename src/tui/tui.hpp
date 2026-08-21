#pragma once

#include <future>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include "../core/vault.hpp"
#include "clipboard.hpp"

#ifdef VAULTCLI_TESTING
struct TuiTestAccess;
#endif

class Tui {
public:
    /// @brief Callback used to persist an immutable vault snapshot.
    using SaveCallback = std::function<bool(const Vault& snapshot, std::string& error)>;

    /// @brief Severity used to color the status line without parsing text.
    enum class StatusTone { Info, Success, Warning, Error };

    /// @brief Create a terminal UI bound to an existing Vault instance.
    /// @param vault Mutable vault displayed and edited by the UI.
    /// @param save_callback Callback invoked after each mutating operation.
    /// @param clipboard_copy Optional clipboard operation used by copy shortcuts.
    explicit Tui(Vault& vault,
                  SaveCallback save_callback = {},
                  Clipboard::CopyFunction clipboard_copy = {});

    /// @brief Run the main interactive loop.
    void run();

private:
#ifdef VAULTCLI_TESTING
    friend struct TuiTestAccess;
#endif

    /// @brief UI columns.
    enum class Pane { Folders, Entries, Search };

    /// @brief Form currently displayed above the main view.
    enum class FormMode { None, Folder, Entry };

    /// @brief Coordinates of an entry in search mode.
    struct SearchResult {
        int folder_index;
        int entry_index;
    };

    /// @brief Result returned by the background persistence operation.
    struct SaveResult {
        bool success = false;
        std::string error;
    };

    /// @brief Pending candidate and its asynchronous persistence task.
    struct PendingSave {
        Vault candidate;
        std::future<SaveResult> task;
        std::string success_status;
        std::function<void()> on_success;
    };

    /// @brief Shared vault model edited by the UI.
    Vault& vault_;

    /// @brief Persistence callback owned by the application session.
    SaveCallback save_callback_;

    /// @brief Fullscreen interactive terminal surface from FTXUI.
    mutable ftxui::ScreenInteractive screen_ = ftxui::ScreenInteractive::Fullscreen();

    /// @brief Current active pane.
    Pane active_pane_ = Pane::Folders;

    /// @brief Current selected folder index in left column.
    int selected_folder_ = 0;

    /// @brief Current selected entry index in middle column.
    int selected_entry_ = 0;

    /// @brief Current selected search result index.
    int selected_search_result_ = 0;

    /// @brief Whether the password is shown or masked.
    bool show_password_ = false;

    /// @brief Whether global search mode is active.
    bool search_mode_ = false;

    /// @brief Search query entered by user.
    std::string search_query_;

    /// @brief Status message shown at bottom of screen.
    std::string status_ = "Selectionne un dossier puis une entree.";

    /// @brief Status severity used by the themed renderer.
    StatusTone status_tone_ = StatusTone::Info;

    /// @brief Clipboard boundary used by the copy shortcuts.
    Clipboard clipboard_;

    /// @brief Whether a creation form is currently displayed.
    bool form_open_ = false;

    /// @brief Form kind used to choose its fields and submit behavior.
    FormMode form_mode_ = FormMode::None;

    /// @brief Active field group in the modal form.
    int form_tab_ = 0;

    /// @brief Folder name entered in the folder form.
    std::string folder_name_input_;

    /// @brief Entry title entered in the entry form.
    std::string entry_id_input_;

    /// @brief Entry login entered in the entry form.
    std::string entry_login_input_;

    /// @brief Entry password entered in the entry form.
    std::string entry_password_input_;

    /// @brief Whether the entry password is visible in the form.
    bool form_show_password_ = false;

    /// @brief Validation message displayed inside the active form.
    std::string form_error_;

    /// @brief Candidate currently being encrypted and written in the background.
    std::optional<PendingSave> pending_save_;

    /// @brief Build the full app component and register event handling.
    /// @return Root FTXUI component for the interactive application.
    ftxui::Component make_app();

    /// @brief Build the folder and entry modal components.
    /// @return Modal component containing the creation forms.
    ftxui::Component make_form_component();

    /// @brief Render top-level layout.
    /// @return Element representing the complete current screen.
    ftxui::Element render() const;

    /// @brief Render folder list pane.
    /// @return Element containing the folder list.
    ftxui::Element render_folders() const;

    /// @brief Render entries pane.
    /// @param results Precomputed search results (used in search mode).
    /// @return Element containing either folder entries or search results.
    ftxui::Element render_entries(const std::vector<SearchResult>& results) const;

    /// @brief Render entry details pane.
    /// @param results Precomputed search results (used in search mode).
    /// @return Element containing details for the current selection.
    ftxui::Element render_details(const std::vector<SearchResult>& results) const;

    /// @brief Render the blocking persistence overlay.
    /// @return Square modal showing the indeterminate encryption spinner.
    ftxui::Element render_saving_overlay() const;

    /// @brief Main event dispatcher for normal and column navigation modes.
    /// @param event Event received from FTXUI.
    /// @return True when the event was handled by the TUI.
    bool handle_event(ftxui::Event event);

    /// @brief Handle text input and movement in search mode.
    /// @param event Event received from FTXUI while searching.
    /// @return True when the event was handled by search mode.
    bool handle_search_event(ftxui::Event event);

    /// @brief Move the current selection in the active pane.
    /// @param delta -1 or +1 to move up/down.
    void move_selection(int delta);

    /// @brief Open currently selected search result and go back to entry mode.
    void open_selected_search_result();

    /// @brief Remove selected entry from currently selected folder.
    void delete_selected_entry();

    /// @brief Open the folder creation form.
    void open_folder_form();

    /// @brief Open the entry creation form for the selected folder.
    void open_entry_form();

    /// @brief Cancel the current creation form and clear its temporary fields.
    void cancel_form();

    /// @brief Clear modal state after cancellation or successful persistence.
    void reset_form_state();

    /// @brief Validate and persist the current form.
    void submit_form();

    /// @brief Copy the selected entry login to the system clipboard.
    void copy_selected_login();

    /// @brief Copy the selected entry password to the system clipboard.
    void copy_selected_password();

    /// @brief Prepare one candidate and persist it asynchronously.
    /// @param mutation In-memory change applied to a private candidate.
    /// @param success_status Status displayed after a successful save.
    /// @param on_success UI action run after the candidate is installed.
    void commit_mutation(const std::function<void(Vault&)>& mutation,
                         std::string success_status,
                         std::function<void()> on_success = {});

    /// @brief Consume a completed background save on the UI thread.
    /// @return True when a save completed and its result was applied.
    bool finish_pending_save();

    /// @brief Report whether a save currently blocks interaction.
    /// @return True while the persistence worker is running.
    bool save_in_progress() const noexcept;

    /// @brief Set a status message and its visual severity.
    /// @param message Message shown in the footer.
    /// @param tone Color category used by the theme.
    void set_status(std::string message, StatusTone tone = StatusTone::Info);

    /// @brief Keep all selection indices valid after a mutation or rollback.
    void normalize_selection();

    /// @brief Get selected folder (read-only).
    /// @return Selected folder, or nullptr when the vault has no folders.
    const Folder* selected_folder() const;

    /// @brief Get selected folder (mutable).
    /// @return Mutable selected folder, or nullptr when the vault has no folders.
    Folder* selected_folder();

    /// @brief Get selected entry in active folder (read-only).
    /// @return Selected entry, or nullptr when there is no valid selection.
    const Entry* selected_entry() const;

    /// @brief Build normalized search hits across all folders and entries.
    /// @return Search results matching the current query.
    std::vector<SearchResult> search_results() const;
};
