#include "tui.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <utility>

#include "../crypto/crypto.hpp"

namespace {
const ftxui::Color kBackground = ftxui::Color::Black;
const ftxui::Color kBorder = ftxui::Color::GrayDark;
const ftxui::Color kAccent = ftxui::Color::Yellow;
const ftxui::Color kAccentBright = ftxui::Color::YellowLight;

/// @brief Render a row with a high-contrast amber selection state.
ftxui::Element highlighted(const std::string& value, bool selected) {
    auto element = ftxui::text("  " + value) | ftxui::flex;
    if (selected) {
        return element | ftxui::bgcolor(kAccent) | ftxui::color(ftxui::Color::Black);
    }
    return element | ftxui::color(ftxui::Color::GrayLight);
}

/// @brief Add a one-cell margin around a component's content.
ftxui::Element padded(ftxui::Element content) {
    using namespace ftxui;
    return vbox({text(" "), hbox({text(" "), std::move(content), text(" ")}), text(" ")});
}

/// @brief Draw a square panel with a themed border.
ftxui::Element panel(ftxui::Element content, bool active = false) {
    return padded(std::move(content)) |
           ftxui::borderStyled(ftxui::LIGHT, active ? kAccent : kBorder) |
           ftxui::bgcolor(kBackground);
}

/// @brief Convert a status category to its terminal color.
ftxui::Color status_color(Tui::StatusTone tone) {
    switch (tone) {
    case Tui::StatusTone::Success:
        return ftxui::Color::GreenLight;
    case Tui::StatusTone::Warning:
        return kAccentBright;
    case Tui::StatusTone::Error:
        return ftxui::Color::RedLight;
    case Tui::StatusTone::Info:
    default:
        return ftxui::Color::GrayLight;
    }
}

/// @brief Convert ASCII letters in a text value to lowercase for search.
std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

/// @brief Check whether a user-entered value contains only whitespace.
bool is_blank(const std::string& value) {
    return value.empty() || std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isspace(character) != 0;
           });
}
}  // namespace

Tui::Tui(Vault& vault, SaveCallback save_callback, Clipboard::CopyFunction clipboard_copy)
    : vault_(vault), save_callback_(std::move(save_callback)), clipboard_(std::move(clipboard_copy))
{
}

void Tui::run() {
    screen_.Loop(make_app());
    if (pending_save_) {
        pending_save_->task.wait();
        finish_pending_save();
    }
}

ftxui::Component Tui::make_app() {
    auto main_renderer = ftxui::Renderer([this](bool) { return render(); });
    auto main = ftxui::CatchEvent(std::move(main_renderer),
                                  [this](ftxui::Event event) { return handle_event(event); });
    auto modal = ftxui::Modal(main, make_form_component(), &form_open_);
    return ftxui::CatchEvent(std::move(modal), [this](ftxui::Event event) {
        if (save_in_progress()) {
            if (event == ftxui::Event::Custom) {
                finish_pending_save();
            }
            return true;
        }
        return false;
    });
}

ftxui::Component Tui::make_form_component() {
    using namespace ftxui;

    InputOption folder_options;
    folder_options.placeholder = "Nom du dossier";
    folder_options.multiline = false;
    folder_options.on_enter = [this] { submit_form(); };
    auto folder_input = Input(&folder_name_input_, folder_options);

    InputOption id_options;
    id_options.placeholder = "Titre de l'entree";
    id_options.multiline = false;
    id_options.on_enter = [this] { submit_form(); };
    auto id_input = Input(&entry_id_input_, id_options);

    InputOption login_options;
    login_options.placeholder = "Login";
    login_options.multiline = false;
    login_options.on_enter = [this] { submit_form(); };
    auto login_input = Input(&entry_login_input_, login_options);

    InputOption password_options;
    password_options.placeholder = "Mot de passe";
    password_options.multiline = false;
    password_options.password = &form_show_password_;
    password_options.on_enter = [this] { submit_form(); };
    auto password_input = Input(&entry_password_input_, password_options);

    auto folder_fields = Container::Vertical({folder_input});
    auto entry_fields = Container::Vertical({id_input, login_input, password_input});
    auto fields = Container::Tab({folder_fields, entry_fields}, &form_tab_);

    auto form = Renderer(fields, [this, fields, folder_input, id_input, login_input, password_input] {
        using namespace ftxui;
        if (save_in_progress()) {
            return render_saving_overlay();
        }

        Elements lines;
        if (form_mode_ == FormMode::Folder) {
            lines.push_back(text("Nouveau dossier") | bold | color(kAccentBright));
            lines.push_back(separatorStyled(LIGHT));
            lines.push_back(text("Nom") | color(kAccent));
            lines.push_back(folder_input->Render() | borderStyled(LIGHT, kAccent));
        } else {
            lines.push_back(text("Nouvelle entree") | bold | color(kAccentBright));
            lines.push_back(separatorStyled(LIGHT));
            lines.push_back(text("Titre") | color(kAccent));
            lines.push_back(id_input->Render() | borderStyled(LIGHT, kAccent));
            lines.push_back(text("Login") | color(kAccent));
            lines.push_back(login_input->Render() | borderStyled(LIGHT, kAccent));
            lines.push_back(text("Mot de passe") | color(kAccent));
            lines.push_back(password_input->Render() | borderStyled(LIGHT, kAccent));
        }
        if (form_mode_ == FormMode::Entry) {
            lines.push_back(text("F2 afficher/masquer le mot de passe") | dim);
        }
        if (!form_error_.empty()) {
            lines.push_back(text(form_error_) | color(Color::Red));
        }
        lines.push_back(text("Entree valider · Escape annuler") | dim);
        return padded(vbox(std::move(lines))) |
               borderStyled(LIGHT, kAccent) |
               bgcolor(kBackground) |
               size(WIDTH, LESS_THAN, 68);
    });

    return CatchEvent(form, [this](Event event) {
        if (save_in_progress()) {
            return true;
        }
        if (event == Event::Escape) {
            cancel_form();
            return true;
        }
        if (event == Event::F2 && form_mode_ == FormMode::Entry) {
            form_show_password_ = !form_show_password_;
            return true;
        }
        return false;
    });
}

ftxui::Element Tui::render() const {
    using namespace ftxui;
    if (save_in_progress()) {
        return render_saving_overlay();
    }

    const auto results = search_results();
    const auto header = text(search_mode_ ? "Recherche globale : " + search_query_ + "_"
                                         : "Appuie sur / pour rechercher dans tous les dossiers.") |
                        (search_mode_ ? bold | color(kAccentBright) : dim);

    const auto folders = panel(render_folders(), active_pane_ == Pane::Folders) |
                         size(WIDTH, EQUAL, 28);
    const auto entries = panel(render_entries(results), active_pane_ == Pane::Entries) | flex;
    const auto details = panel(render_details(results), false) | size(WIDTH, EQUAL, 34);
    const bool compact = Terminal::Size().dimx < 96;
    const auto content = compact ? vbox({folders, entries, details}) : hbox({folders, entries, details});

    return padded(vbox({text("Vault CLI") | bold | color(kAccentBright) | center,
                        header,
                        separatorStyled(LIGHT),
                        content,
                        separatorStyled(LIGHT),
                        text(status_) | color(status_color(status_tone_)),
                        paragraph("/ rechercher · ←/→ colonne · ↑/↓ selectionner · Entree ouvrir dossier") |
                            dim,
                        paragraph("a nouvelle entree · f nouveau dossier · r afficher · l copier login · p copier mot de passe") |
                            dim,
                        paragraph("d supprimer · q quitter") | dim})) |
           borderStyled(LIGHT, kAccent) |
           bgcolor(kBackground);
}

ftxui::Element Tui::render_saving_overlay() const {
    using namespace ftxui;
    screen_.RequestAnimationFrame();
    static constexpr std::array<const char*, 4> frames = {"|", "/", "-", "\\"};
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch());
    const auto frame = static_cast<std::size_t>((elapsed.count() / 120) % frames.size());
    return padded(vbox({text("Sauvegarde securisee") | bold | color(kAccentBright) | center,
                        separatorStyled(LIGHT),
                        hbox({text(frames[frame]) | color(kAccent),
                              text("  Chiffrement et sauvegarde en cours...")}) |
                            center,
                        text("Les commandes sont temporairement suspendues.") | dim | center})) |
           borderStyled(LIGHT, kAccent) |
           bgcolor(kBackground) |
           size(WIDTH, LESS_THAN, 64) |
           center;
}

ftxui::Element Tui::render_folders() const {
    using namespace ftxui;
    Elements lines = {text("Dossiers") | bold, separator()};
    const auto& folders = vault_.get_folders();

    if (folders.empty()) {
        lines.push_back(text("  Aucun dossier") | dim);
    }
    for (int index = 0; index < static_cast<int>(folders.size()); ++index) {
        lines.push_back(highlighted(
            folders[index].get_name() + " (" + std::to_string(folders[index].get_entries().size()) + ")",
            active_pane_ == Pane::Folders && index == selected_folder_));
    }
    return vbox(std::move(lines));
}

ftxui::Element Tui::render_entries(const std::vector<SearchResult>& results) const {
    using namespace ftxui;
    Elements lines = {text(search_mode_ ? "Resultats" : "Entrees") | bold, separator()};
    if (search_mode_) {
        if (results.empty()) {
            lines.push_back(text("  Aucun resultat") | dim);
        }
        const auto& folders = vault_.get_folders();
        for (int index = 0; index < static_cast<int>(results.size()); ++index) {
            const auto [folder_index, entry_index] = results[index];
            const auto& entry = folders[folder_index].get_entries()[entry_index];
            lines.push_back(highlighted(folders[folder_index].get_name() + " / " + entry.id + " — " + entry.login,
                                        active_pane_ == Pane::Search &&
                                        index == selected_search_result_));
        }
    } else {
        const Folder* folder = selected_folder();
        if (folder == nullptr || folder->get_entries().empty()) {
            lines.push_back(text("  Aucune entree") | dim);
        } else {
            const auto& entries = folder->get_entries();
            for (int index = 0; index < static_cast<int>(entries.size()); ++index) {
                lines.push_back(highlighted(entries[index].id + " — " + entries[index].login,
                                           active_pane_ == Pane::Entries &&
                                           index == selected_entry_));
            }
        }
    }
    return vbox(std::move(lines));
}

ftxui::Element Tui::render_details(const std::vector<SearchResult>& results) const {
    using namespace ftxui;
    Elements lines = {text("Selection") | bold, separator()};
    const Entry* entry = nullptr;

    if (search_mode_ && !results.empty()) {
        const auto [folder_index, entry_index] = results[selected_search_result_];
        entry = &vault_.get_folders()[folder_index].get_entries()[entry_index];
    } else if (!search_mode_) {
        entry = selected_entry();
    }
    if (entry == nullptr) {
        lines.push_back(text("Aucune entree selectionnee.") | dim);
    } else {
        lines.push_back(text("Id            : " + entry->id));
        lines.push_back(text("Login         : " + entry->login));
        lines.push_back(
            text("Mot de passe  : " +
                 std::string(show_password_ ? entry->password : "••••••••••••")));
    }
    return vbox(std::move(lines));
}

bool Tui::handle_event(ftxui::Event event) {
    if (save_in_progress()) {
        if (event == ftxui::Event::Custom) {
            finish_pending_save();
        }
        return true;
    }
    if (search_mode_) return handle_search_event(event);

    if (event == ftxui::Event::Character('q') || event == ftxui::Event::Escape) {
        screen_.ExitLoopClosure()();
        return true;
    }
    if (event == ftxui::Event::Character('/')) {
        search_mode_ = true;
        search_query_.clear();
        selected_search_result_ = 0;
        active_pane_ = Pane::Search;
        set_status("Recherche globale active : id, login ou nom de dossier.");
        return true;
    }
    if (event == ftxui::Event::ArrowLeft) {
        active_pane_ = Pane::Folders;
        return true;
    }
    if (event == ftxui::Event::ArrowRight) {
        active_pane_ = Pane::Entries;
        return true;
    }
    if (event == ftxui::Event::ArrowUp) {
        move_selection(-1);
        return true;
    }
    if (event == ftxui::Event::ArrowDown) {
        move_selection(1);
        return true;
    }
    if (event == ftxui::Event::Return && active_pane_ == Pane::Folders) {
        if (selected_folder() == nullptr) {
            set_status("Aucun dossier a ouvrir.", StatusTone::Warning);
            return true;
        }
        active_pane_ = Pane::Entries;
        selected_entry_ = 0;
        set_status("Dossier '" + selected_folder()->get_name() + "' ouvert.");
        return true;
    }
    if (event == ftxui::Event::Character('r') && selected_entry() != nullptr) {
        show_password_ = !show_password_;
        set_status(show_password_ ? "Mot de passe affiche." : "Mot de passe masque.");
        return true;
    }
    if (event == ftxui::Event::Character('f')) {
        open_folder_form();
        return true;
    }
    if (event == ftxui::Event::Character('a')) {
        if (vault_.get_folders().empty()) {
            open_folder_form();
        } else {
            open_entry_form();
        }
        return true;
    }
    if (event == ftxui::Event::Character('l')) {
        copy_selected_login();
        return true;
    }
    if (event == ftxui::Event::Character('p')) {
        copy_selected_password();
        return true;
    }
    if (event == ftxui::Event::Character('d')) {
        delete_selected_entry();
        return true;
    }
    return false;
}

bool Tui::handle_search_event(ftxui::Event event) {
    const auto results = search_results();
    if (event == ftxui::Event::Escape) {
        search_mode_ = false;
        search_query_.clear();
        active_pane_ = Pane::Entries;
        set_status("Recherche annulee.");
        return true;
    }
    if (event == ftxui::Event::Backspace) {
        if (!search_query_.empty()) {
            search_query_.pop_back();
            selected_search_result_ = 0;
        }
        return true;
    }
    if (event == ftxui::Event::ArrowUp && !results.empty()) {
        selected_search_result_ = std::max(0, selected_search_result_ - 1);
        return true;
    }
    if (event == ftxui::Event::ArrowDown && !results.empty()) {
        selected_search_result_ = std::min(selected_search_result_ + 1,
                                          static_cast<int>(results.size()) - 1);
        return true;
    }
    if (event == ftxui::Event::Return && !results.empty()) {
        open_selected_search_result();
        return true;
    }
    if (event.is_character()) {
        search_query_ += event.character();
        selected_search_result_ = 0;
        return true;
    }
    return true;
}

void Tui::move_selection(int delta) {
    if (active_pane_ == Pane::Folders) {
        const int last = static_cast<int>(vault_.get_folders().size()) - 1;
        selected_folder_ = std::clamp(selected_folder_ + delta, 0, std::max(0, last));
        selected_entry_ = 0;
        return;
    }

    const Folder* folder = selected_folder();
    const int last = folder == nullptr ? -1 : static_cast<int>(folder->get_entries().size()) - 1;
    selected_entry_ = std::clamp(selected_entry_ + delta, 0, std::max(0, last));
}

void Tui::open_selected_search_result() {
    const auto results = search_results();
    if (results.empty()) return;
    const auto [folder_index, entry_index] = results[selected_search_result_];
    selected_folder_ = folder_index;
    selected_entry_ = entry_index;
    search_mode_ = false;
    search_query_.clear();
    active_pane_ = Pane::Entries;
    set_status("Resultat ouvert dans le dossier '" + selected_folder()->get_name() + "'.");
}

void Tui::delete_selected_entry() {
    Folder* folder = selected_folder();
    const Entry* entry = selected_entry();
    if (folder == nullptr || entry == nullptr) {
        set_status("Aucune entree a supprimer.", StatusTone::Warning);
        return;
    }
    const int folder_index = selected_folder_;
    const std::string entry_id = entry->id;
    commit_mutation(
        [folder_index, entry_id](Vault& candidate) {
            if (folder_index >= 0 &&
                folder_index < static_cast<int>(candidate.get_folders().size())) {
                candidate.get_folders()[folder_index].remove_entry(entry_id);
            }
        },
        "Entree '" + entry_id + "' supprimee.");
}

void Tui::open_folder_form() {
    form_mode_ = FormMode::Folder;
    form_tab_ = 0;
    form_open_ = true;
    folder_name_input_.clear();
    form_error_.clear();
    form_show_password_ = false;
    set_status("Saisissez le nom du nouveau dossier.");
}

void Tui::open_entry_form() {
    if (selected_folder() == nullptr) {
        set_status("Creez d'abord un dossier.", StatusTone::Warning);
        return;
    }
    form_mode_ = FormMode::Entry;
    form_tab_ = 1;
    form_open_ = true;
    entry_id_input_.clear();
    entry_login_input_.clear();
    Crypto::clear(entry_password_input_);
    form_error_.clear();
    form_show_password_ = false;
    set_status("Saisissez les informations de la nouvelle entree.");
}

void Tui::cancel_form() {
    reset_form_state();
    set_status("Creation annulee.");
}

void Tui::reset_form_state() {
    form_open_ = false;
    form_mode_ = FormMode::None;
    form_tab_ = 0;
    folder_name_input_.clear();
    entry_id_input_.clear();
    entry_login_input_.clear();
    Crypto::clear(entry_password_input_);
    form_error_.clear();
    form_show_password_ = false;
}

void Tui::submit_form() {
    if (form_mode_ == FormMode::Folder) {
        if (is_blank(folder_name_input_)) {
            form_error_ = "Le nom du dossier ne peut pas etre vide.";
            return;
        }
        if (vault_.get_folder(folder_name_input_) != nullptr) {
            form_error_ = "Ce dossier existe deja.";
            return;
        }

        const std::string name = folder_name_input_;
        const int new_index = static_cast<int>(vault_.get_folders().size());
        commit_mutation(
            [name](Vault& candidate) { candidate.add_folder(Folder(name)); },
            "Dossier '" + name + "' cree.",
            [this, new_index] {
            selected_folder_ = new_index;
            selected_entry_ = 0;
            active_pane_ = Pane::Folders;
            reset_form_state();
            });
        return;
    }

    if (form_mode_ != FormMode::Entry) {
        return;
    }
    Folder* folder = selected_folder();
    if (folder == nullptr) {
        form_error_ = "Aucun dossier selectionne.";
        return;
    }
    if (is_blank(entry_id_input_) || is_blank(entry_login_input_) || is_blank(entry_password_input_)) {
        form_error_ = "Le titre, le login et le mot de passe sont obligatoires.";
        return;
    }
    const auto duplicate = std::find_if(
        folder->get_entries().begin(), folder->get_entries().end(),
        [&](const Entry& entry) { return entry.id == entry_id_input_; });
    if (duplicate != folder->get_entries().end()) {
        form_error_ = "Une entree porte deja ce titre dans ce dossier.";
        return;
    }

    const int folder_index = selected_folder_;
    const int new_index = static_cast<int>(folder->get_entries().size());
    const Entry entry{entry_id_input_, entry_login_input_, entry_password_input_};
    commit_mutation(
        [folder_index, entry](Vault& candidate) {
            candidate.get_folders()[folder_index].add_entry(entry);
        },
        "Entree '" + entry.id + "' creee.",
        [this, folder_index, new_index] {
        selected_folder_ = folder_index;
        selected_entry_ = new_index;
        active_pane_ = Pane::Entries;
        reset_form_state();
        });
}

void Tui::copy_selected_login() {
    const Entry* entry = selected_entry();
    if (entry == nullptr) {
        set_status("Aucune entree selectionnee.", StatusTone::Warning);
        return;
    }
    std::string error;
    if (!clipboard_.copy(entry->login, error)) {
        set_status("Copie impossible : " + error, StatusTone::Error);
        return;
    }
    set_status("Login copie dans le presse-papiers.", StatusTone::Success);
}

void Tui::copy_selected_password() {
    const Entry* entry = selected_entry();
    if (entry == nullptr) {
        set_status("Aucune entree selectionnee.", StatusTone::Warning);
        return;
    }
    std::string error;
    if (!clipboard_.copy(entry->password, error)) {
        set_status("Copie impossible : " + error, StatusTone::Error);
        return;
    }
    set_status("Mot de passe copie dans le presse-papiers.", StatusTone::Success);
}

void Tui::commit_mutation(const std::function<void(Vault&)>& mutation,
                          std::string success_status,
                          std::function<void()> on_success)
{
    if (save_in_progress()) {
        set_status("Une sauvegarde est deja en cours.", StatusTone::Warning);
        return;
    }

    Vault candidate = vault_;
    mutation(candidate);
    if (!save_callback_) {
        vault_ = std::move(candidate);
        normalize_selection();
        show_password_ = false;
        set_status(std::move(success_status), StatusTone::Success);
        if (on_success) {
            on_success();
        }
        return;
    }

    SaveCallback callback = save_callback_;
    Vault snapshot = candidate;
    auto* screen = &screen_;
    PendingSave pending;
    pending.candidate = std::move(candidate);
    pending.success_status = std::move(success_status);
    pending.on_success = std::move(on_success);
    pending.task = std::async(std::launch::async,
                              [callback = std::move(callback),
                               snapshot = std::move(snapshot),
                               screen] {
                                  SaveResult result;
                                  try {
                                      result.success = callback(snapshot, result.error);
                                  } catch (...) {
                                      result.error = "Une erreur inattendue est survenue pendant la sauvegarde.";
                                  }
                                  screen->PostEvent(ftxui::Event::Custom);
                                  return result;
                              });
    pending_save_ = std::move(pending);
    set_status("Sauvegarde securisee en cours...", StatusTone::Info);
}

bool Tui::finish_pending_save()
{
    if (!pending_save_ ||
        pending_save_->task.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        return false;
    }

    SaveResult result = pending_save_->task.get();
    Vault candidate = std::move(pending_save_->candidate);
    std::string success_status = std::move(pending_save_->success_status);
    auto on_success = std::move(pending_save_->on_success);
    pending_save_.reset();

    if (!result.success) {
        show_password_ = false;
        const std::string message = "Sauvegarde impossible : " + result.error;
        set_status(message, StatusTone::Error);
        if (form_open_) {
            form_error_ = message;
        }
        return true;
    }

    vault_ = std::move(candidate);
    normalize_selection();
    show_password_ = false;
    set_status(std::move(success_status), StatusTone::Success);
    if (on_success) {
        on_success();
    }
    return true;
}

bool Tui::save_in_progress() const noexcept
{
    return pending_save_.has_value();
}

void Tui::set_status(std::string message, StatusTone tone)
{
    status_ = std::move(message);
    status_tone_ = tone;
}

void Tui::normalize_selection()
{
    const int folder_count = static_cast<int>(vault_.get_folders().size());
    selected_folder_ = std::clamp(selected_folder_, 0, std::max(0, folder_count - 1));

    const Folder* folder = selected_folder();
    const int entry_count = folder == nullptr ? 0 : static_cast<int>(folder->get_entries().size());
    selected_entry_ = std::clamp(selected_entry_, 0, std::max(0, entry_count - 1));

    const auto results = search_results();
    selected_search_result_ =
        std::clamp(selected_search_result_, 0, std::max(0, static_cast<int>(results.size()) - 1));
}

const Folder* Tui::selected_folder() const {
    const auto& folders = vault_.get_folders();
    return folders.empty() ? nullptr : &folders[selected_folder_];
}

Folder* Tui::selected_folder() {
    auto& folders = vault_.get_folders();
    return folders.empty() ? nullptr : &folders[selected_folder_];
}

const Entry* Tui::selected_entry() const {
    const Folder* folder = selected_folder();
    if (folder == nullptr || folder->get_entries().empty()) {
        return nullptr;
    }
    return &folder->get_entries()[selected_entry_];
}

std::vector<Tui::SearchResult> Tui::search_results() const {
    std::vector<SearchResult> results;
    const std::string query = lowercase(search_query_);
    const auto& folders = vault_.get_folders();
    for (int folder_index = 0; folder_index < static_cast<int>(folders.size()); ++folder_index) {
        const auto& folder = folders[folder_index];
        const auto& entries = folder.get_entries();
        for (int entry_index = 0; entry_index < static_cast<int>(entries.size()); ++entry_index) {
            const auto& entry = entries[entry_index];
            const std::string searchable =
                lowercase(folder.get_name() + " " + entry.id + " " + entry.login);
            if (searchable.find(query) != std::string::npos) {
                results.push_back({folder_index, entry_index});
            }
        }
    }
    return results;
}
