// SPDX-License-Identifier: GPL-3.0-or-later
// The zoom levels' graphics made beside the game (graphics_preloader.hpp), with stand-in levels
// and with sheets written into a folder that looks like an installation.

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <future>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/core/file.hpp"
#include "o1602/render/graphics_preloader.hpp"

using namespace o1602;
using render::Zoom;

namespace {

/// A stand-in for a made level: `ms` tells which one it is.
render::PreparedGraphics stand_in(Zoom zoom) {
    render::PreparedGraphics p;
    p.ms = 100 + static_cast<std::uint32_t>(zoom);
    return p;
}

/// Records which levels were made on which thread, in order.
struct Log {
    std::mutex mutex;
    std::vector<Zoom> made;
    std::vector<std::thread::id> threads;

    void add(Zoom zoom) {
        const std::lock_guard lock(mutex);
        made.push_back(zoom);
        threads.push_back(std::this_thread::get_id());
    }
    std::vector<Zoom> zooms() {
        const std::lock_guard lock(mutex);
        return made;
    }
};

/// Takes the ready levels until `count` came (or 10 s passed).
std::vector<render::GraphicsPreloader::Ready> collect(render::GraphicsPreloader& preloader, std::size_t count) {
    std::vector<render::GraphicsPreloader::Ready> ready;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (ready.size() < count && std::chrono::steady_clock::now() < deadline) {
        if (auto r = preloader.take_ready()) {
            ready.push_back(std::move(*r));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return ready;
}

/// Lets the background go on once opened.
class Gate {
public:
    Gate() : m_opened(m_promise.get_future().share()) {}
    Gate(const Gate&) = delete;
    Gate& operator=(const Gate&) = delete;
    void open() {
        if (!m_open.exchange(true)) {
            m_promise.set_value();
        }
    }
    void wait() const { m_opened.wait(); }

private:
    std::promise<void> m_promise;
    std::shared_future<void> m_opened;
    std::atomic<bool> m_open{false};
};

/// Opens the gate at the end of a test, also of a failed one, so that the background does not
/// wait for ever (the preloader's end waits for it). Made after the preloader: it goes first.
struct OpenAtEnd {
    Gate& gate;
    ~OpenAtEnd() { gate.open(); }
};

}  // namespace

TEST_CASE("graphics preloader: the shown level first, then its neighbours, smaller sprites first") {
    CHECK((render::preload_order(Zoom::Large) == std::vector<Zoom>{Zoom::Large, Zoom::Medium, Zoom::Small}));
    CHECK((render::preload_order(Zoom::Medium) == std::vector<Zoom>{Zoom::Medium, Zoom::Small, Zoom::Large}));
    CHECK((render::preload_order(Zoom::Small) == std::vector<Zoom>{Zoom::Small, Zoom::Medium, Zoom::Large}));
}

TEST_CASE("graphics preloader: without background a level is made when it is taken") {
    Log log;
    render::GraphicsPreloader preloader(
        [&log](Zoom zoom, const std::atomic<bool>& /*cancel*/) -> core::Result<render::PreparedGraphics> {
            log.add(zoom);
            return stand_in(zoom);
        },
        false);
    CHECK(!preloader.background());
    preloader.request(render::preload_order(Zoom::Large));
    CHECK(!preloader.take_ready().has_value());
    CHECK(log.zooms().empty());
    int steps = 0;
    auto medium = preloader.take(Zoom::Medium, [&steps] { ++steps; });
    REQUIRE(medium.ok());
    CHECK(medium->ms == 101);
    CHECK(steps == 0);
    REQUIRE(log.threads.size() == 1);
    CHECK(log.threads[0] == std::this_thread::get_id());
}

TEST_CASE("graphics preloader: levels are made once each on another thread, in the order asked") {
    Log log;
    {
        render::GraphicsPreloader preloader(
            [&log](Zoom zoom, const std::atomic<bool>& /*cancel*/) -> core::Result<render::PreparedGraphics> {
                log.add(zoom);
                return stand_in(zoom);
            },
            true);
        const auto order = render::preload_order(Zoom::Large);
        preloader.request(order);
        preloader.request(order);  // queued, being made or ready: not again
        auto ready = collect(preloader, 3);
        REQUIRE(ready.size() == 3);
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK(ready[i].first == order[i]);
            REQUIRE(ready[i].second.ok());
            CHECK(ready[i].second->ms == 100 + static_cast<std::uint32_t>(order[i]));
        }
        CHECK(!preloader.take_ready().has_value());
        // Taken: asked for again, it is made again (the viewer does so after a device reset).
        preloader.request(std::vector<Zoom>{Zoom::Small});
        CHECK(collect(preloader, 1).size() == 1);
    }
    const std::lock_guard lock(log.mutex);
    REQUIRE(log.made.size() == 4);
    for (const std::thread::id id : log.threads) {
        CHECK(id != std::this_thread::get_id());
    }
}

TEST_CASE("graphics preloader: a level wanted now goes first, the wait keeps calling its step") {
    Log log;
    std::promise<void> entered;
    Gate gate;
    render::GraphicsPreloader preloader(
        [&](Zoom zoom, const std::atomic<bool>& /*cancel*/) -> core::Result<render::PreparedGraphics> {
            if (zoom == Zoom::Large) {
                entered.set_value();
                gate.wait();
            }
            log.add(zoom);
            return stand_in(zoom);
        },
        true);
    const OpenAtEnd end{gate};
    preloader.request(render::preload_order(Zoom::Large));  // Large, Medium, Small
    entered.get_future().wait();                            // Large is being made
    int steps = 0;
    auto small = preloader.take(Zoom::Small, [&] {
        if (++steps == 3) {
            gate.open();
        }
    });
    REQUIRE(small.ok());
    CHECK(small->ms == 102);
    CHECK(steps >= 3);
    auto rest = collect(preloader, 2);
    REQUIRE(rest.size() == 2);
    CHECK(rest[0].first == Zoom::Large);
    CHECK(rest[1].first == Zoom::Medium);
    CHECK((log.zooms() == std::vector<Zoom>{Zoom::Large, Zoom::Small, Zoom::Medium}));
    // Asked for again: taken when it is made.
    preloader.request(std::vector<Zoom>{Zoom::Medium});
    auto medium = preloader.take(Zoom::Medium);
    REQUIRE(medium.ok());
    CHECK(medium->ms == 101);
}

TEST_CASE("graphics preloader: failures and lack of memory come back as errors") {
    const auto prepare = [](Zoom zoom, const std::atomic<bool>& /*cancel*/) -> core::Result<render::PreparedGraphics> {
        if (zoom == Zoom::Medium) {
            throw std::bad_alloc();
        }
        if (zoom == Zoom::Small) {
            return core::make_error(core::ErrorCode::NotFound, "sgfx/stadtfld.bsh not found");
        }
        return stand_in(zoom);
    };
    render::GraphicsPreloader now(prepare, false);
    CHECK(now.take(Zoom::Large).ok());
    const auto medium = now.take(Zoom::Medium);
    REQUIRE(!medium.ok());
    CHECK(medium.error().message == "not enough memory for the graphics");
    CHECK(!now.take(Zoom::Small).ok());

    render::GraphicsPreloader beside(prepare, true);
    beside.request(render::preload_order(Zoom::Large));
    auto ready = collect(beside, 3);
    REQUIRE(ready.size() == 3);
    CHECK(ready[0].second.ok());
    CHECK(!ready[1].second.ok());
    CHECK(!ready[2].second.ok());
}

TEST_CASE("graphics preloader: the end waits for the level being made and drops the rest") {
    std::atomic<int> made{0};
    std::promise<void> entered;
    {
        render::GraphicsPreloader preloader(
            [&](Zoom zoom, const std::atomic<bool>& /*cancel*/) -> core::Result<render::PreparedGraphics> {
                if (made.fetch_add(1) == 0) {
                    entered.set_value();
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                return stand_in(zoom);
            },
            true);
        preloader.request(render::preload_order(Zoom::Medium));
        entered.get_future().wait();
    }
    CHECK(made.load() == 1);
}

TEST_CASE("graphics preloader: the end cancels the level being made") {
    std::atomic<bool> cancelled{false};
    std::promise<void> entered;
    {
        render::GraphicsPreloader preloader(
            [&](Zoom /*zoom*/, const std::atomic<bool>& cancel) -> core::Result<render::PreparedGraphics> {
                entered.set_value();
                // A long level: it ends only when the preloader gives it up (or after 10 s).
                const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (!cancel.load() && std::chrono::steady_clock::now() < give_up) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                cancelled = cancel.load();
                return core::make_error(core::ErrorCode::Unsupported, "cancelled");
            },
            true);
        preloader.request(render::preload_order(Zoom::Large));
        entered.get_future().wait();
    }
    CHECK(cancelled.load());
}

namespace {

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
}

/// A BSH file with one 4x2 sprite whose row 0 is "..AB" and row 1 "C..." (as in test_assets).
std::vector<std::uint8_t> bsh_file() {
    const std::vector<std::uint8_t> rle = {2, 2, 0xA, 0xB, 0xFE, 0, 1, 0xC, 0xFF};
    std::vector<std::uint8_t> payload;
    put_u32(payload, 4);
    put_u32(payload, 4);
    put_u32(payload, 2);
    put_u32(payload, 1);
    const auto length = static_cast<std::uint32_t>((16 + rle.size() + 3) & ~std::size_t{3});
    put_u32(payload, length);
    payload.insert(payload.end(), rle.begin(), rle.end());
    payload.resize(4 + length, 0);
    std::vector<std::uint8_t> file(16, 0);
    std::memcpy(file.data(), "BSH", 3);
    put_u32(file, static_cast<std::uint32_t>(payload.size()));
    file.insert(file.end(), payload.begin(), payload.end());
    return file;
}

}  // namespace

TEST_CASE("graphics preloader: a level is made from the sheets of a folder, with the figures' masks") {
    const auto root = test::temp_path("o1602-preload");
    std::filesystem::remove_all(root);
    const auto bsh = bsh_file();
    for (const char* name : {"gfx/stadtfld.bsh", "gfx/ship.bsh", "sgfx/stadtfld.bsh", "mgfx/ship.bsh"}) {
        std::filesystem::create_directories((root / name).parent_path());
        REQUIRE(core::write_file(root / name, bsh).ok());
    }
    for (const char* name : {"haeuser.cod", "figuren.cod", "toolgfx/stadtfld.col"}) {
        std::filesystem::create_directories((root / name).parent_path());
        REQUIRE(core::write_file(root / name, std::vector<std::uint8_t>{0}).ok());
    }
    auto game = assets::GameDirectory::open(root);
    REQUIRE(game.ok());
    const assets::Palette palette;
    const std::vector<assets::FigureSheet> sheets{assets::FigureSheet::Ship};
    render::GraphicsPreloader preloader(
        [game = *game, palette, sheets](Zoom zoom, const std::atomic<bool>& /*cancel*/) {
            return render::prepare_world_graphics(
                game, palette, zoom, sheets, render::GraphicsOptions{.derive_missing_sheets = true, .page_size = 64});
        },
        true);
    preloader.request(render::preload_order(Zoom::Large));

    auto large = preloader.take(Zoom::Large);
    REQUIRE(large.ok());
    CHECK(large->graphics.atlas.sprite_count() == 2);
    CHECK(large->graphics.sheet_base(assets::FigureSheet::Ship) == std::optional<std::uint32_t>(1));
    CHECK(!large->graphics.derived[static_cast<std::size_t>(assets::FigureSheet::Ship)]);
    CHECK(!large->graphics.atlas.pages().empty());  // the pixels stay for the upload
    // The ship has a mask (row 0 starts with two clear pixels), the terrain none (all of it counts).
    CHECK(!large->hit_mask.visible(1, 0, 0));
    CHECK(large->hit_mask.visible(1, 2, 0));
    CHECK(large->hit_mask.visible(0, 0, 0));
    // Medium has no terrain sheet; Small takes the ship from Medium at half size.
    CHECK(!preloader.take(Zoom::Medium).ok());
    auto small = preloader.take(Zoom::Small);
    REQUIRE(small.ok());
    CHECK(small->graphics.derived[static_cast<std::size_t>(assets::FigureSheet::Ship)]);
    // A cancelled load stops with an error.
    const std::atomic<bool> cancel{true};
    CHECK(!render::prepare_world_graphics(*game, palette, Zoom::Large, sheets,
                                          render::GraphicsOptions{.page_size = 64, .cancel = &cancel})
               .ok());
    std::filesystem::remove_all(root);
}
