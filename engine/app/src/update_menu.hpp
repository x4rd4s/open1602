// SPDX-License-Identifier: GPL-3.0-or-later
//
// The screen "Updates" of the main menu, Open1602's own: the installed and the newest version, a
// check by hand, the download and installation with its progress, the restart, the version kept
// from the last update, and whether Open1602 looks for updates at every start. The screen only
// shows an UpdateModel and answers with events; the viewer drives the Updater (updater.hpp).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "updater.hpp"

#include "o1602/ui/form.hpp"

namespace o1602::app {

struct UpdateModel {
    std::string current;  ///< this version
    Updater::Status status;
    UpdateTarget::Kind kind = UpdateTarget::Kind::NotPackaged;
    bool check_at_start = false;
    /// The version kept from the last update, if one is kept.
    std::optional<std::string> previous;
    /// What the last action did (a restore, an address that could not be opened), Windows-1252.
    std::string notice;
};

class UpdateMenu {
public:
    struct Event {
        enum class Kind : std::uint8_t {
            Check,         ///< look for a newer release now
            Install,       ///< download and install it
            Cancel,        ///< stop the download
            Restart,       ///< end Open1602 and start the new version
            ToggleCheck,   ///< look for updates at every start, or not
            Notes,         ///< open the release notes in the browser
            DownloadPage,  ///< open the release page (a copy that does not update itself)
            Restore,       ///< put the version kept back
            Close,         ///< back to the main menu
        };
        Kind kind = Kind::Close;
    };

    explicit UpdateMenu(ui::FormStyle style = {}) : m_form(style) {}

    void set_style(const ui::FormStyle& style) { m_form.set_style(style); }
    void set_scale(double scale) { m_form.set_scale(scale); }

    /// Everything to draw, relative to the 1024 x 768 design's top left.
    [[nodiscard]] std::vector<ui::DrawItem> draw(const UpdateModel& model,
                                                 const std::array<const ui::Font*, ui::kFontSlots>& fonts);
    std::optional<Event> mouse(const UpdateModel& model, std::int32_t x, std::int32_t y, bool down,
                               ui::MouseButton button = ui::MouseButton::Left);
    /// What the screen says about the state (Windows-1252), and whether it is a warning.
    [[nodiscard]] static std::string state_text(const UpdateModel& model);
    [[nodiscard]] static std::string problem_text(Updater::Problem problem);
    /// Why this copy does not install updates itself; empty if it does.
    [[nodiscard]] static std::string kind_text(UpdateTarget::Kind kind);
    /// "4.5 MB".
    [[nodiscard]] static std::string megabytes(std::uint64_t bytes);

    /// The widgets as laid out, for tests.
    [[nodiscard]] const ui::Form& form() const noexcept { return m_form; }
    void layout(const UpdateModel& model);

private:
    ui::Form m_form;
};

}  // namespace o1602::app
