// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_menu.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <utility>

#include "high_score_file.hpp"

#include "o1602/assets/chunk_file.hpp"
#include "o1602/core/file.hpp"
#include "o1602/ui/localization.hpp"

namespace o1602::app {

namespace {

// BASE.GAD
constexpr std::int32_t kSingle = 30002;
constexpr std::int32_t kMulti = 30003;
constexpr std::int32_t kCredits = 30004;
constexpr std::int32_t kIntro = 30005;
constexpr std::int32_t kQuit = 30006;
// HOST.GAD
constexpr std::int32_t kNewGame = 31002;
constexpr std::int32_t kLoadGame = 31003;
constexpr std::int32_t kContinue = 31004;
constexpr std::int32_t kMainMenu = 31005;
constexpr std::int32_t kListUp = 31011;
constexpr std::int32_t kListDown = 31012;
constexpr std::int32_t kRowText = 31021;   ///< + row
constexpr std::int32_t kRowStars = 31041;  ///< + row
constexpr std::int32_t kRowPlug = 31061;   ///< + row
constexpr std::size_t kRows = 14;
// MISSION.GAD
constexpr std::int32_t kStart = 32001;
constexpr std::int32_t kCancel = 32002;
constexpr std::int32_t kScoresHeading = 32004;
constexpr std::int32_t kScoresFrame = 32005;
constexpr std::int32_t kScoresUp = 32006;
constexpr std::int32_t kScoresDown = 32007;
constexpr std::int32_t kChoice = 32011;       ///< + 0..4
constexpr std::int32_t kChoiceLabel = 32021;  ///< + 0..4
constexpr std::int32_t kBriefing = 32031;
constexpr std::int32_t kBriefing2 = 32041;
constexpr std::int32_t kScoreName = 32051;  ///< + 0..3
constexpr std::int32_t kScorePoints = 32061;
constexpr std::int32_t kScoreRank = 32071;
constexpr std::int32_t kScoreStars = 32081;
constexpr std::int32_t kBriefingUp = 32091;
constexpr std::int32_t kBriefingDown = 32092;
constexpr std::int32_t kBriefingTrack = 32093;
constexpr std::int32_t kBriefingSlider = 32094;
constexpr std::size_t kChoices = 5;
constexpr std::size_t kScoreRows = 4;
// FARBWAHL.GAD
constexpr std::int32_t kNameBox = 35002;
constexpr std::int32_t kNamePrompt = 35003;
constexpr std::int32_t kBanner = 35021;  ///< + colour 0..3

constexpr std::uint32_t kTutorial = 0;
constexpr std::uint32_t kEndless = 1;
/// START.BSH: one star; the levels of the endless game add one each.
constexpr std::int32_t kGfxStars = 21;

/// The first digits of a scenario's files, in order: the choices of the mission screen.
std::vector<std::uint8_t> choice_digits(const MenuScenario& s) {
    std::vector<std::uint8_t> digits;
    for (const auto& [digit, path] : s.files) {
        if (digit != 0xFF && std::find(digits.begin(), digits.end(), digit) == digits.end()) {
            digits.push_back(digit);
        }
    }
    return digits;
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::optional<ui::Panel> load_panel(const assets::GameDirectory& game, const char* file) {
    auto layout = assets::load_gad(game, file);
    if (!layout) {
        return std::nullopt;
    }
    return ui::Panel(std::move(layout).value(), 1.0);
}

}  // namespace

std::vector<MenuScenario> scenario_list(const std::filesystem::path& folder,
                                        const std::array<std::string, 4>& headings) {
    // One entry per name without the extension and the digits at its end (0x0048E710).
    std::map<std::string, MenuScenario> by_name;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file(ec) || lower(core::path_to_utf8(entry.path().extension())) != ".szs") {
            continue;
        }
        // The names in the list in the fonts' Windows-1252, whatever the file system uses.
        const std::u8string u8 = entry.path().stem().u8string();
        std::string stem = utf8_to_cp1252(std::string_view(reinterpret_cast<const char*>(u8.data()), u8.size()));
        std::size_t digits = stem.size();
        while (digits > 0 && std::isdigit(static_cast<unsigned char>(stem[digits - 1])) != 0) {
            --digits;
        }
        const std::uint8_t first = digits < stem.size() ? static_cast<std::uint8_t>(stem[digits] - '0') : 0xFF;
        const std::string name = stem.substr(0, digits);
        if (name.empty()) {
            continue;
        }
        MenuScenario& s = by_name[lower(name)];
        if (s.files.empty()) {
            s.name = name;
            const std::u8string u8name = entry.path().stem().u8string();
            std::size_t cut = u8name.size();
            while (cut > 0 && u8name[cut - 1] >= u8'0' && u8name[cut - 1] <= u8'9') {
                --cut;
            }
            s.base = std::filesystem::path(u8name.substr(0, cut));
            if (auto bytes = core::read_file(entry.path())) {
                if (auto file = assets::ChunkFile::from_bytes(std::move(bytes).value())) {
                    s.header = assets::load_scenario_header(*file);
                }
            }
        }
        s.files.emplace_back(first, entry.path());
    }
    std::array<std::vector<MenuScenario>, 3> groups;
    for (auto& [key, s] : by_name) {
        std::sort(s.files.begin(), s.files.end());
        const std::size_t g = s.header.mission_number != assets::ScenarioHeader::kNone ? 0
                              : s.header.campaign != assets::ScenarioHeader::kNone     ? 1
                                                                                       : 2;
        groups[g].push_back(std::move(s));
    }
    std::stable_sort(groups[0].begin(), groups[0].end(), [](const MenuScenario& a, const MenuScenario& b) {
        return a.header.mission_number < b.header.mission_number;
    });
    std::stable_sort(groups[1].begin(), groups[1].end(), [](const MenuScenario& a, const MenuScenario& b) {
        return a.header.campaign < b.header.campaign;
    });
    std::stable_sort(groups[2].begin(), groups[2].end(),
                     [](const MenuScenario& a, const MenuScenario& b) { return a.header.ranking < b.header.ranking; });
    std::vector<MenuScenario> out;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        if (groups[g].empty()) {
            continue;
        }
        MenuScenario heading;
        heading.name = headings[g];
        heading.separator = true;
        out.push_back(std::move(heading));
        for (MenuScenario& s : groups[g]) {
            out.push_back(std::move(s));
        }
    }
    return out;
}

const std::string& MainMenu::default_name() {
    return ui::tr("menu.default_name");
}

MainMenu::MainMenu(const assets::GameDirectory& game, const assets::TextTable* texts)
    : m_game(&game),
      m_texts(texts),
      m_base(load_panel(game, "BASE.GAD")),
      m_host(load_panel(game, "HOST.GAD")),
      m_mission(load_panel(game, "MISSION.GAD")),
      m_colour(load_panel(game, "FARBWAHL.GAD")) {
    if (m_host) {
        m_host->set_selected(kNewGame, true);
    }
    if (m_colour) {
        m_colour->set_text(kNamePrompt, text("GAME", 81));
    }
}

std::string MainMenu::text(std::string_view section, std::size_t line) const {
    if (m_texts == nullptr) {
        return {};
    }
    const auto& lines = m_texts->section(std::string(section));
    return line < lines.size() ? lines[line] : std::string();
}

void MainMenu::open() {
    m_screen = Screen::Base;
    m_typing = false;
}

void MainMenu::set_scale(double scale) {
    for (auto* p : {&m_base, &m_host, &m_mission, &m_colour}) {
        if (*p) {
            (*p)->set_scale(scale);
        }
    }
}

ui::Panel* MainMenu::panel() {
    std::optional<ui::Panel>* p = &m_base;
    switch (m_screen) {
        case Screen::Host:
            p = &m_host;
            break;
        case Screen::Mission:
            p = &m_mission;
            break;
        case Screen::Colour:
            p = &m_colour;
            break;
        case Screen::Base:
            break;
    }
    return *p ? &**p : nullptr;
}

void MainMenu::refresh_host() {
    ui::Panel& p = *m_host;
    if (m_mode == ListMode::Load) {
        // The savegames without stars (0x0048AC70: the original's twelve fit without arrows; Open1602
        // keeps any number, so the list scrolls like the scenarios).
        const std::size_t count = m_slots.size();
        m_first = std::min(m_first, count > kRows ? count - kRows : 0);
        for (std::size_t r = 0; r < kRows; ++r) {
            const auto k = static_cast<std::int32_t>(r);
            const std::size_t i = m_first + r;
            const bool shown = i < count;
            p.set_visible(kRowText + k, shown);
            p.set_visible(kRowStars + k, false);
            p.set_visible(kRowPlug + k, false);
            if (shown) {
                p.set_text(kRowText + k, m_slots.names[i].empty() ? std::string("-") : m_slots.names[i]);
            }
        }
        p.set_visible(kListUp, m_first > 0);
        p.set_visible(kListDown, m_first + kRows < count);
        return;
    }
    m_first = std::min(m_first, m_scenarios.size() > kRows ? m_scenarios.size() - kRows : 0);
    for (std::size_t r = 0; r < kRows; ++r) {
        const auto k = static_cast<std::int32_t>(r);
        const std::size_t i = m_first + r;
        const bool shown = i < m_scenarios.size();
        p.set_visible(kRowText + k, shown);
        p.set_visible(kRowPlug + k, false);  // the players of a network game
        const bool stars =
            shown && !m_scenarios[i].separator && m_scenarios[i].header.ranking != assets::ScenarioHeader::kNone;
        p.set_visible(kRowStars + k, stars);
        if (!shown) {
            continue;
        }
        p.set_text(kRowText + k, m_scenarios[i].name);
        if (stars) {
            p.set_graphic(
                kRowStars + k,
                kGfxStars + static_cast<std::int32_t>(std::min<std::uint32_t>(m_scenarios[i].header.ranking, 4)), 0);
        }
    }
    p.set_visible(kListUp, m_first > 0);
    p.set_visible(kListDown, m_first + kRows < m_scenarios.size());
}

void MainMenu::load_scenarios() {
    if (m_texts == nullptr) {
        return;
    }
    // Read again every time: a won game may have opened a mission since.
    m_progress = {};
    if (!m_progress_file.empty()) {
        if (auto bytes = core::read_file(m_progress_file)) {
            m_progress =
                assets::parse_progress(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
        }
    }
    std::array<std::string, 4> headings;
    for (std::size_t g = 0; g < headings.size(); ++g) {
        headings[g] = text("GAME", 85 + g);
    }
    m_scenarios.clear();
    if (const auto folder = m_game->resolve("Szenes")) {
        m_scenarios = scenario_list(*folder, headings);
    }
    // The numbered missions above the progress are not offered (0x0048DC90).
    std::erase_if(m_scenarios, [&](const MenuScenario& s) {
        const std::uint32_t n = s.header.mission_number;
        return !s.separator && n >= assets::CampaignProgress::kFirstNumbered &&
               n <= assets::CampaignProgress::kLastNumbered && n > m_progress.numbered;
    });
}

std::size_t MainMenu::campaign_parts_open(const MenuScenario& s) const {
    // MISSION.GAD: a part above the campaign's progress is disabled; other scenarios have all.
    constexpr std::size_t kAll = 4;
    if (s.header.campaign == assets::ScenarioHeader::kNone) {
        return kAll;
    }
    const auto it = m_progress.campaigns.find(s.header.campaign);
    return it != m_progress.campaigns.end() ? it->second : 0;
}

bool MainMenu::open_mission(std::string_view name) {
    open();
    if (!m_host || !m_mission || name.empty()) {
        return false;
    }
    load_scenarios();
    for (std::size_t i = 0; i < m_scenarios.size(); ++i) {
        if (!m_scenarios[i].separator && lower(m_scenarios[i].name) == lower(name)) {
            select_scenario(i);
            m_screen = Screen::Mission;
            return true;
        }
    }
    return false;
}

void MainMenu::select_scenario(std::size_t index) {
    m_chosen = index;
    const MenuScenario& s = m_scenarios[index];
    m_variant = s.files.size() >= 2 ? s.files.front().first : 0xFF;
    // A campaign starts at its furthest open part.
    if (s.header.campaign != assets::ScenarioHeader::kNone) {
        const std::vector<std::uint8_t> digits = choice_digits(s);
        if (!digits.empty()) {
            m_variant = digits[std::min(campaign_parts_open(s), digits.size() - 1)];
        }
    }
    m_scores_first = 0;
    load_details();
}

void MainMenu::load_details() {
    const MenuScenario& s = m_scenarios[m_chosen];
    m_briefing_line = 0;
    // The briefing: the mission of the human in the file (AUFTRAG4 of the player record).
    m_briefing.clear();
    std::uint32_t part = s.header.ranking;
    if (auto bytes = core::read_file(chosen_file())) {
        if (auto file = assets::ChunkFile::from_bytes(std::move(bytes).value())) {
            part = assets::load_scenario_header(*file).ranking;
            const auto players = assets::load_players(*file);
            const auto missions = assets::load_missions(*file);
            if (missions && !missions->empty()) {
                std::size_t m = 0;
                if (players) {
                    for (const assets::PlayerRecord& p : *players) {
                        if (p.kind == assets::PlayerKind::Human && p.mission < missions->size()) {
                            m = p.mission;
                            break;
                        }
                    }
                }
                m_briefing = (*missions)[m].text;
            }
        }
    }
    m_scores.clear();
    if (!s.files.empty()) {
        // A campaign part has a list of its own, named by its label (HighScore_Insert 0x0048E540).
        std::filesystem::path name = s.base;
        name += ".hss";
        if (s.header.campaign != assets::ScenarioHeader::kNone && m_texts != nullptr) {
            sim::GameIdentity part_of;
            part_of.name = s.name;
            part_of.campaign = s.header.campaign;
            part_of.ranking = part;
            if (auto label = high_score_file(part_of, m_texts->section("KAMPAGNE"), false); !label.empty()) {
                name = std::move(label);
            }
        }
        const std::filesystem::path hss = s.files.front().second.parent_path() / name;
        const std::filesystem::path own = m_score_folder / name;
        auto bytes = m_score_folder.empty() ? core::read_file(hss) : core::read_file(own);
        if (!bytes && !m_score_folder.empty()) {
            bytes = core::read_file(hss);
        }
        if (bytes) {
            m_scores = assets::parse_high_scores(
                std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
        }
    }
}

std::filesystem::path MainMenu::chosen_file() const {
    if (m_chosen >= m_scenarios.size()) {
        return {};
    }
    const MenuScenario& s = m_scenarios[m_chosen];
    for (const auto& [digit, path] : s.files) {
        if (digit == m_variant) {
            return path;
        }
    }
    return s.files.empty() ? std::filesystem::path{} : s.files.front().second;
}

void MainMenu::refresh_mission() {
    ui::Panel& p = *m_mission;
    const MenuScenario& s = m_scenarios[m_chosen];
    // The choices: the levels of the endless game, the parts of the tutorial, else the digits.
    const std::vector<std::uint8_t> digits = choice_digits(s);
    for (std::size_t i = 0; i < kChoices; ++i) {
        const auto k = static_cast<std::int32_t>(i);
        const bool shown = i < digits.size();
        p.set_visible(kChoice + k, shown);
        p.set_visible(kChoiceLabel + k, shown);
        if (!shown) {
            continue;
        }
        std::string label;
        if (s.header.mission_number == kEndless) {
            label = text("ENDLESSKIND", digits[i]);
        } else if (s.header.mission_number == kTutorial) {
            label = text("TUTORKIND", digits[i]);
        }
        if (label.empty()) {
            label = ui::tr("menu.part", {std::to_string(digits[i] + 1)});
        }
        p.set_text(kChoiceLabel + k, label);
        p.set_selected(kChoice + k, digits[i] == m_variant);
        p.set_disabled(kChoice + k, i > campaign_parts_open(s));
    }
    p.set_text(kBriefing, m_briefing);
    p.set_visible(kBriefing2, false);
    p.set_visible(kBriefingTrack, false);
    p.set_visible(kBriefingSlider, false);
    // The high scores, four at a time; not for the tutorial (0x0048B0E0).
    const bool scores = s.header.mission_number != kTutorial;
    for (const std::int32_t id : {kScoresHeading, kScoresFrame, kScoresUp, kScoresDown}) {
        p.set_visible(id, scores);
    }
    for (std::size_t r = 0; r < kScoreRows; ++r) {
        const auto k = static_cast<std::int32_t>(r);
        const std::size_t i = m_scores_first + r;
        const bool shown = scores && i < m_scores.size() && !m_scores[i].name.empty();
        for (const std::int32_t id : {kScoreName, kScorePoints, kScoreRank}) {
            p.set_visible(id + k, shown);
        }
        // The level of an endless game as stars: sprite 21 + level, one star for "Einfach" (0x0048B0E0).
        const bool stars = shown && s.header.mission_number == kEndless;
        p.set_visible(kScoreStars + k, stars);
        if (!shown) {
            continue;
        }
        p.set_text(kScoreName + k, m_scores[i].name);
        p.set_text(kScorePoints + k, std::to_string(m_scores[i].points));
        p.set_text(kScoreRank + k, std::to_string(i + 1) + ".");
        if (stars) {
            p.set_graphic(kScoreStars + k,
                          kGfxStars + static_cast<std::int32_t>(std::min<std::uint32_t>(m_scores[i].stars, 4)), 0);
        }
    }
}

void MainMenu::refresh_colour() {
    m_colour->set_text(kNameBox, m_typing ? m_name + "_" : m_name);
}

std::vector<ui::DrawItem> MainMenu::draw(const std::array<const ui::Font*, 3>& fonts) {
    ui::Panel* p = panel();
    if (p == nullptr) {
        return {};
    }
    if (m_screen == Screen::Host) {
        refresh_host();
    } else if (m_screen == Screen::Mission && m_chosen < m_scenarios.size()) {
        refresh_mission();
        // The briefing's end is known only with the fonts: "down" past it would blank the text.
        m_briefing_line = std::min(m_briefing_line, p->max_scroll(kBriefing, fonts));
        p->set_scroll(kBriefing, m_briefing_line);
    } else if (m_screen == Screen::Colour) {
        refresh_colour();
    }
    return p->draw(fonts);
}

std::vector<MenuEvent> MainMenu::mouse(std::int32_t x, std::int32_t y, bool down) {
    std::vector<MenuEvent> out;
    ui::Panel* p = panel();
    if (p == nullptr) {
        return out;
    }
    for (const ui::PanelEvent& e : p->mouse(x, y, down)) {
        if (e.kind != ui::PanelEvent::Kind::Clicked) {
            continue;
        }
        switch (m_screen) {
            case Screen::Base:
                if (e.id == kSingle && m_host) {
                    load_scenarios();
                    m_screen = Screen::Host;
                } else if (e.id == kMulti) {
                    out.push_back(MenuEvent{MenuEvent::Kind::Multiplayer});
                } else if (e.id == kCredits) {
                    out.push_back(MenuEvent{MenuEvent::Kind::Credits});
                } else if (e.id == kIntro) {
                    out.push_back(MenuEvent{MenuEvent::Kind::Intro});
                } else if (e.id == kQuit) {
                    out.push_back(MenuEvent{MenuEvent::Kind::Quit});
                }
                break;
            case Screen::Host:
                if (e.id == kNewGame) {
                    m_mode = ListMode::New;
                    m_first = 0;
                } else if (e.id == kLoadGame) {
                    m_mode = ListMode::Load;
                    m_first = 0;
                } else if (e.id == kContinue) {
                    out.push_back(MenuEvent{MenuEvent::Kind::ContinueLast});
                } else if (e.id == kMainMenu) {
                    m_screen = Screen::Base;
                } else if (e.id == kListUp) {
                    m_first -= std::min(m_first, kRows);
                } else if (e.id == kListDown) {
                    m_first += kRows;
                } else if (e.id >= kRowText && e.id < kRowText + static_cast<std::int32_t>(kRows)) {
                    const auto r = static_cast<std::size_t>(e.id - kRowText);
                    if (m_mode == ListMode::Load) {
                        if (const std::size_t i = m_first + r; i < m_slots.size() && !m_slots.names[i].empty()) {
                            MenuEvent load{MenuEvent::Kind::LoadSlot};
                            load.slot = i;
                            out.push_back(std::move(load));
                        }
                    } else if (m_first + r < m_scenarios.size() && !m_scenarios[m_first + r].separator && m_mission) {
                        select_scenario(m_first + r);
                        m_screen = Screen::Mission;
                    }
                }
                break;
            case Screen::Mission:
                if (e.id >= kChoice && e.id < kChoice + static_cast<std::int32_t>(kChoices)) {
                    const std::vector<std::uint8_t> digits = choice_digits(m_scenarios[m_chosen]);
                    const auto i = static_cast<std::size_t>(e.id - kChoice);
                    if (i < digits.size() && i <= campaign_parts_open(m_scenarios[m_chosen])) {
                        m_variant = digits[i];
                        load_details();  // the briefing of the chosen part
                    }
                } else if (e.id == kStart) {
                    const MenuScenario& s = m_scenarios[m_chosen];
                    if (s.header.mission_number == kEndless && m_colour) {
                        m_screen = Screen::Colour;  // name and banner first (0x0048A680)
                        // The name can be typed at once (as the original, mode 5 of 0x00409910);
                        // the last one, or "Namenlos", stands there until the first key.
                        if (m_name.empty()) {
                            m_name = default_name();
                        }
                        m_typing = true;
                        m_name_fresh = true;
                    } else {
                        MenuEvent ev{MenuEvent::Kind::Scenario};
                        ev.path = chosen_file();
                        ev.name = m_name;
                        out.push_back(std::move(ev));
                    }
                } else if (e.id == kCancel) {
                    m_screen = Screen::Host;
                } else if (e.id == kBriefingUp) {
                    m_briefing_line = std::max(0, m_briefing_line - 1);
                } else if (e.id == kBriefingDown) {
                    ++m_briefing_line;
                } else if (e.id == kScoresUp) {
                    m_scores_first -= std::min<std::size_t>(m_scores_first, kScoreRows);
                } else if (e.id == kScoresDown && m_scores_first + kScoreRows < 12) {
                    m_scores_first += kScoreRows;
                }
                break;
            case Screen::Colour:
                if (e.id == kNameBox) {
                    m_typing = true;
                } else if (e.id >= kBanner && e.id < kBanner + 4) {
                    m_typing = false;
                    MenuEvent ev{MenuEvent::Kind::NewEndless};
                    ev.level = m_variant == 0xFF ? 0 : m_variant;
                    ev.colour = static_cast<std::uint8_t>(e.id - kBanner);
                    ev.name = m_name;
                    out.push_back(std::move(ev));
                }
                break;
        }
    }
    return out;
}

bool MainMenu::back() {
    m_typing = false;
    switch (m_screen) {
        case Screen::Base:
            return false;
        case Screen::Host:
            m_screen = Screen::Base;
            break;
        case Screen::Mission:
            m_screen = Screen::Host;
            break;
        case Screen::Colour:
            m_screen = Screen::Mission;
            break;
    }
    return true;
}

void MainMenu::type(std::string_view text) {
    if (!m_typing) {
        return;
    }
    constexpr std::size_t kMaxName = 19;  // 0x0048A680
    if (std::exchange(m_name_fresh, false) && !text.empty()) {
        m_name.clear();
    }
    for (const char c : text) {
        if (static_cast<unsigned char>(c) >= 0x20 && m_name.size() < kMaxName) {
            m_name.push_back(c);
        }
    }
}

std::optional<MenuEvent> MainMenu::key(int key) {
    if (!m_typing) {
        return std::nullopt;
    }
    if (key == 8) {
        if (std::exchange(m_name_fresh, false)) {
            m_name.clear();
        } else if (!m_name.empty()) {
            m_name.pop_back();
        }
    } else if (key == 13) {
        m_typing = false;
        m_name_fresh = false;
        if (m_name.empty()) {
            m_name = default_name();
        }
        return MenuEvent{MenuEvent::Kind::NameEntered};
    } else if (key == 27) {
        back();  // straight back to the choice of the game, as Escape outside the field
    }
    return std::nullopt;
}

}  // namespace o1602::app
