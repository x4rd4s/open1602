// SPDX-License-Identifier: GPL-3.0-or-later
// GAD layouts (GADDATA/*.GAD) with their includes. The input is split at the first NUL byte: the
// layout before it, the text every include returns after it (so includes may include themselves).
#include <cstdint>
#include <optional>
#include <string>

#include "fuzz_support.hpp"

#include "o1602/assets/gad.hpp"

using namespace o1602;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const std::string_view all = fuzz::as_text({data, size});
    const std::size_t nul = all.find('\0');
    const std::string_view layout = all.substr(0, nul);
    const std::optional<std::string> included =
        nul == std::string_view::npos ? std::nullopt : std::optional<std::string>(all.substr(nul + 1));
    const auto gad = assets::parse_gad(layout, [&](std::string_view) { return included; });
    for (const auto& g : gad.gadgets) {
        (void)gad.find(g.id);
        (void)g.text();
    }
    return 0;
}
