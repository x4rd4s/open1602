// SPDX-License-Identifier: GPL-3.0-or-later
#include "update_menu.hpp"

#include <cstdio>

#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

using Id = ui::Form::Id;
using Tone = ui::Form::Tone;
using Phase = Updater::Phase;
using Kind = UpdateTarget::Kind;
using ui::Align;
using ui::Rect;

constexpr Id kCheck = 1;
constexpr Id kInstall = 2;
constexpr Id kCancel = 3;
constexpr Id kRestart = 4;
constexpr Id kToggle = 5;
constexpr Id kNotes = 6;
constexpr Id kDownloadPage = 7;
constexpr Id kRestore = 8;
constexpr Id kBack = 9;

constexpr std::int32_t kLeft = 64;
constexpr std::int32_t kWidth = 896;
constexpr std::int32_t kButtonH = 44;
constexpr std::int32_t kGap = 8;
constexpr std::int32_t kBottom = 664;

bool busy(Phase phase) {
    return phase == Phase::Checking || phase == Phase::Downloading || phase == Phase::Installing;
}

}  // namespace

std::string UpdateMenu::megabytes(std::uint64_t bytes) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

std::string UpdateMenu::problem_text(Updater::Problem problem) {
    using P = Updater::Problem;
    switch (problem) {
        case P::None:
            return {};
        case P::Offline:
            return ui::tr("update.problem_offline");
        case P::NoRelease:
            return ui::tr("update.problem_no_release");
        case P::NotGenuine:
            return ui::tr("update.problem_not_genuine");
        case P::NewerFormat:
            return ui::tr("update.problem_newer_format");
        case P::NoPackage:
            return ui::tr("update.problem_no_package");
        case P::Damaged:
            return ui::tr("update.problem_damaged");
        case P::InstallFailed:
            return ui::tr("update.problem_install");
        case P::NoHttps:
            return ui::tr("update.problem_no_https");
    }
    return {};
}

std::string UpdateMenu::kind_text(Kind kind) {
    switch (kind) {
        case Kind::Package:
            return {};
        case Kind::NotPackaged:
            return ui::tr("update.kind_not_packaged");
        case Kind::Translocated:
            return ui::tr("update.kind_translocated");
        case Kind::ReadOnly:
            return ui::tr("update.kind_read_only");
    }
    return {};
}

std::string UpdateMenu::state_text(const UpdateModel& model) {
    const Updater::Status& s = model.status;
    switch (s.phase) {
        case Phase::Idle:
            return ui::tr("update.state_idle");
        case Phase::Checking:
            return ui::tr("update.state_checking");
        case Phase::UpToDate:
            return ui::tr("update.state_up_to_date");
        case Phase::Available:
            return model.kind == Kind::Package ? ui::tr("update.state_available", {s.latest})
                                               : ui::tr("update.state_available_by_hand", {s.latest});
        case Phase::Downloading:
            return s.total > 0 ? ui::tr("update.state_downloading", {s.latest, megabytes(s.done), megabytes(s.total)})
                               : ui::tr("update.state_connecting", {s.latest});
        case Phase::Installing:
            return ui::tr("update.state_installing", {s.latest});
        case Phase::Installed:
            return s.latest.empty() ? ui::tr("update.state_restored") : ui::tr("update.state_installed", {s.latest});
        case Phase::Failed:
            return problem_text(s.problem);
    }
    return {};
}

std::vector<ui::DrawItem> UpdateMenu::draw(const UpdateModel& model,
                                           const std::array<const ui::Font*, ui::kFontSlots>& fonts) {
    layout(model);
    return m_form.draw(fonts);
}

std::optional<UpdateMenu::Event> UpdateMenu::mouse(const UpdateModel& model, std::int32_t x, std::int32_t y, bool down,
                                                   ui::MouseButton button) {
    layout(model);
    const auto id = m_form.mouse(x, y, down, button);
    if (!id) {
        return std::nullopt;
    }
    switch (*id) {
        case kCheck:
            return Event{Event::Kind::Check};
        case kInstall:
            return Event{Event::Kind::Install};
        case kCancel:
            return Event{Event::Kind::Cancel};
        case kRestart:
            return Event{Event::Kind::Restart};
        case kToggle:
            return Event{Event::Kind::ToggleCheck};
        case kNotes:
            return Event{Event::Kind::Notes};
        case kDownloadPage:
            return Event{Event::Kind::DownloadPage};
        case kRestore:
            return Event{Event::Kind::Restore};
        case kBack:
            return Event{Event::Kind::Close};
        default:
            return std::nullopt;
    }
}

void UpdateMenu::layout(const UpdateModel& model) {
    const Updater::Status& s = model.status;
    ui::Form& f = m_form;
    f.clear();
    f.panel(Rect{40, 40, 944, 688});
    f.label(Rect{kLeft, 54, kWidth, 40}, ui::tr("update.title"), Align::Left, Tone::Heading);
    f.paragraph(Rect{kLeft, 98, kWidth, 48}, ui::tr("update.hint"), Tone::Dim);

    f.entry(Rect{kLeft, 160, 520, 36}, ui::tr("update.installed"), model.current);
    f.entry(Rect{kLeft, 196, 520, 36}, ui::tr("update.newest"), s.latest.empty() ? ui::tr("update.unknown") : s.latest);

    const bool failed = s.phase == Phase::Failed;
    f.paragraph(Rect{kLeft, 250, kWidth, 66}, state_text(model), failed ? Tone::Warning : Tone::Normal);
    if (s.phase == Phase::Downloading || s.phase == Phase::Installing) {
        const double done = s.phase == Phase::Installing ? 1.0
                            : s.total > 0                ? static_cast<double>(s.done) / static_cast<double>(s.total)
                                                         : 0.0;
        f.progress(Rect{kLeft, 322, kWidth, 24}, done);
    }
    if (const std::string why = kind_text(model.kind); !why.empty()) {
        f.paragraph(Rect{kLeft, 360, kWidth, 88}, why, Tone::Dim);
    }

    f.check(kToggle, Rect{kLeft, 468, kWidth, 36}, ui::tr("update.check_at_start"), model.check_at_start);
    f.paragraph(Rect{kLeft + 44, 506, kWidth - 44, 44}, ui::tr("update.check_at_start_hint"), Tone::Dim);

    if (model.previous && !busy(s.phase)) {
        f.button(kRestore, Rect{kLeft, 564, 440, kButtonH}, ui::tr("update.restore", {*model.previous}));
    }
    if (!model.notice.empty()) {
        f.paragraph(Rect{kLeft + 448, 560, kWidth - 448, 52}, model.notice, Tone::Warning);
    }

    // The buttons of the bottom row, left to right; the one the screen is about is selected.
    std::int32_t x = kLeft;
    const auto add = [&](Id id, const std::string& text, std::int32_t width, bool selected, bool enabled = true) {
        f.button(id, Rect{x, kBottom, width, kButtonH}, text, enabled, selected);
        x += width + kGap;
    };
    switch (s.phase) {
        case Phase::Available:
            if (model.kind == Kind::Package) {
                add(kInstall, ui::tr("update.install"), 220, true);
            } else {
                add(kDownloadPage, ui::tr("update.download_page"), 220, true);
            }
            break;
        case Phase::Downloading:
            add(kCancel, ui::tr("update.cancel"), 220, false);
            break;
        case Phase::Installing:
        case Phase::Checking:
            add(kCheck, ui::tr("update.check"), 220, false, false);
            break;
        case Phase::Installed:
            add(kRestart, ui::tr("update.restart"), 220, true);
            break;
        case Phase::Idle:
        case Phase::UpToDate:
        case Phase::Failed:
            add(kCheck, ui::tr("update.check"), 220, true);
            break;
    }
    if (!s.notes.empty() && s.phase != Phase::Idle && s.phase != Phase::Checking) {
        add(kNotes, ui::tr("update.notes"), 220, false);
    }
    f.button(kBack, Rect{kLeft + kWidth - 200, kBottom, 200, kButtonH}, ui::tr("update.back"),
             s.phase != Phase::Installing);
}

}  // namespace o1602::app
