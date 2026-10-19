// SPDX-License-Identifier: GPL-3.0-or-later
//
// o1602_tests [--no-data | --only-data] [filter]
//   --no-data    leaves out the tests against the original game data (names starting with "data:")
//   --only-data  runs only those
//   filter       runs the tests whose name contains this text
// Exit code: 0 all passed, 1 a test failed or nothing matched, 77 everything selected was skipped
// (ctest reports that as "Skipped", see tests/CMakeLists.txt).
#include <cstdio>
#include <cstring>
#include <exception>
#include <string_view>

#include "test_framework.hpp"

namespace {

constexpr int kAllSkipped = 77;

enum class DataTests { Include, Exclude, Only };

}  // namespace

int main(int argc, char** argv) {
    using namespace o1602::test;
    DataTests data = DataTests::Include;
    const char* filter = nullptr;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--no-data") {
            data = DataTests::Exclude;
        } else if (arg == "--only-data") {
            data = DataTests::Only;
        } else {
            filter = argv[i];
        }
    }
    int run = 0;
    int skipped = 0;
    int failed_tests = 0;
    for (const auto& test : registry()) {
        const bool is_data = std::string_view(test.name).starts_with("data:");
        if ((data == DataTests::Exclude && is_data) || (data == DataTests::Only && !is_data) ||
            (filter != nullptr && std::strstr(test.name, filter) == nullptr)) {
            continue;
        }
        const int before = failures();
        bool skip = false;
        try {
            test.fn();
        } catch (const Skip& s) {
            std::printf("[ SKIP ] %s (%s)\n", test.name, s.reason.c_str());
            skip = true;
        } catch (const Failure&) {
            // already reported
        } catch (const std::exception& e) {
            std::fprintf(stderr, "  unexpected exception: %s\n", e.what());
            ++failures();
        } catch (...) {
            std::fprintf(stderr, "  unexpected exception\n");
            ++failures();
        }
        const bool ok = failures() == before;
        if (skip && ok) {
            ++skipped;
            continue;
        }
        // A test that failed a check before it skipped has failed.
        ++run;
        if (!ok) {
            ++failed_tests;
        }
        std::printf("[%s] %s\n", ok ? "  OK  " : " FAIL ", test.name);
    }
    std::printf("\n%d run, %d failed, %d skipped\n", run, failed_tests, skipped);
    if (failed_tests != 0) {
        return 1;
    }
    if (run == 0 && skipped == 0) {
        std::fprintf(stderr, "no test matches\n");
        return 1;
    }
    return run == 0 ? kAllSkipped : 0;
}
