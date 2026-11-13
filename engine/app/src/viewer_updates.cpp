// SPDX-License-Identifier: GPL-3.0-or-later
// MapViewer: the updates of Open1602 (updater.hpp, update_menu.hpp, docs/updates.md).
#include <SDL3/SDL.h>
#include <cstdio>

#include "map_viewer.hpp"

#include "annokit/core/file.hpp"
#include "annokit/update/process.hpp"

#include "o1602/ui/localization.hpp"

#ifndef O1602_VERSION
#define O1602_VERSION "0.0.0"
#endif

namespace o1602::app {

namespace {

constexpr const char* kReleasesPage = "https://github.com/x4rd4s/open1602/releases/latest";

const char* target_name(UpdateTarget::Kind kind) {
    switch (kind) {
        case UpdateTarget::Kind::Package:
            return "updates itself";
        case UpdateTarget::Kind::NotPackaged:
            return "not a release package, only told of new versions";
        case UpdateTarget::Kind::Translocated:
            return "run by macOS from a read-only copy, only told of new versions";
        case UpdateTarget::Kind::ReadOnly:
            return "its folder cannot be written, only told of new versions";
    }
    return "";
}

}  // namespace

void MapViewer::start_updater() {
    // Only an interactive start with the main menu: no network in scripted runs and tests.
    if (!m_options.menu || m_options.max_frames > 0 || !m_options.input.empty()) {
        return;
    }
    Updater::Config config;
    config.current = O1602_VERSION;
    const auto keys = release_keys();
    config.keys.assign(keys.begin(), keys.end());
    if (auto executable = annokit::update::executable_path()) {
        config.target = locate_update_target(*executable);
    } else {
        std::fprintf(stderr, "update: %s\n", executable.error().message.c_str());
    }
    std::fprintf(stderr, "update: Open1602 %s (%s), %s\n", config.current.c_str(),
                 config.target.platform.empty() ? "own build" : config.target.platform.c_str(),
                 target_name(config.target.kind));
    m_updater = std::make_unique<Updater>(std::move(config));
    m_updater->clean_up();
    if (m_app_settings.update_check == UpdateCheck::On) {
        m_updater->check(true);
    }
}

void MapViewer::apply_updates(MainMenu& menu) {
    if (!m_updater) {
        return;
    }
    UpdateModel model;
    model.current = m_updater->current();
    model.status = m_updater->status();
    model.kind = m_updater->target().kind;
    model.check_at_start = m_app_settings.update_check == UpdateCheck::On;
    if (menu.screen() == MainMenu::Screen::Updates) {
        model.previous = m_updater->previous_version();  // reads a small file: only while it shows
    }
    model.notice = m_update_notice;

    // The questions wait for the main menu itself, with nothing else over it.
    const bool free =
        m_hud->menu_active() && menu.screen() == MainMenu::Screen::Base && !m_hud->asking() && !m_video->busy();
    if (free && m_update_question == UpdateQuestion::None) {
        if (m_app_settings.update_check == UpdateCheck::Ask) {
            m_update_question = UpdateQuestion::CheckAtStart;
            m_hud->ask(ui::tr("update.ask_check_at_start"));
        } else if (!m_update_offered && model.status.background && model.status.phase == Updater::Phase::Available &&
                   model.status.latest != m_app_settings.skipped_update) {
            m_update_offered = true;
            m_update_question = UpdateQuestion::Offer;
            const std::string& action =
                m_updater->can_install() ? ui::tr("update.offer_install") : ui::tr("update.offer_download");
            m_hud->ask_choice(
                ui::tr("update.offer", {model.status.latest, model.current}),
                {Hud::ChoiceOption{ui::tr("update.offer_later"), std::nullopt}, Hud::ChoiceOption{action, std::nullopt},
                 Hud::ChoiceOption{ui::tr("update.offer_skip"), std::nullopt}});
        }
    }
    menu.set_updates(std::move(model));
}

bool MapViewer::answer_update_question(std::int32_t step) {
    const UpdateQuestion question = std::exchange(m_update_question, UpdateQuestion::None);
    if (question == UpdateQuestion::None || !m_updater) {
        return false;
    }
    MainMenu* menu = m_hud ? m_hud->menu() : nullptr;
    if (question == UpdateQuestion::CheckAtStart) {
        m_app_settings.update_check = step != 0 ? UpdateCheck::On : UpdateCheck::Off;
        save_settings();
        if (step != 0) {
            m_updater->check(true);
        }
        return true;
    }
    // The offer: 0 later, 1 install (or the download page), 2 skip this version.
    if (step == 2) {
        m_app_settings.skipped_update = m_updater->status().latest;
        save_settings();
    } else if (step == 1 && menu != nullptr) {
        menu->open_updates();
        handle_update(
            *menu, m_updater->can_install() ? UpdateMenu::Event::Kind::Install : UpdateMenu::Event::Kind::DownloadPage);
    }
    return true;
}

void MapViewer::handle_update(MainMenu& menu, UpdateMenu::Event::Kind action) {
    using Kind = UpdateMenu::Event::Kind;
    if (!m_updater) {
        return;
    }
    m_update_notice.clear();
    const auto open_url = [this](const std::string& url) {
        if (!SDL_OpenURL(url.c_str())) {
            std::fprintf(stderr, "update: cannot open %s: %s\n", url.c_str(), SDL_GetError());
            m_update_notice = ui::tr("update.open_failed", {url});
        }
    };
    switch (action) {
        case Kind::Check:
            m_updater->check(false);
            break;
        case Kind::Install:
            m_updater->install();
            break;
        case Kind::Cancel:
            m_updater->cancel();
            break;
        case Kind::Restart: {
            if (m_updater->status().phase != Updater::Phase::Installed) {
                break;
            }
            const UpdateTarget& target = m_updater->target();
            m_restart = Restart{target.restart_program, target.restart_arguments};
            save_settings();
            std::fprintf(stderr, "quit: restart after an update\n");
            m_running = false;
            break;
        }
        case Kind::ToggleCheck:
            m_app_settings.update_check =
                m_app_settings.update_check == UpdateCheck::On ? UpdateCheck::Off : UpdateCheck::On;
            save_settings();
            break;
        case Kind::Notes:
            if (const std::string notes = m_updater->status().notes; !notes.empty()) {
                open_url(notes);
            }
            break;
        case Kind::DownloadPage:
            open_url(kReleasesPage);
            break;
        case Kind::Restore:
            if (auto restored = m_updater->restore_previous(); !restored) {
                std::fprintf(stderr, "update: %s\n", restored.error().message.c_str());
                m_update_notice = ui::tr("update.restore_failed");
            }
            break;
        case Kind::Close:
            break;
    }
    apply_updates(menu);
}

}  // namespace o1602::app
