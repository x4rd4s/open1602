# SPDX-License-Identifier: GPL-3.0-or-later
#
# Review tools (part SYNC of the code review): tests of the multiplayer under edge conditions that
# state the wanted behaviour; a failing one is a finding of the review, not a broken build. Built
# only with -DO1602_REVIEW_TESTS=ON and run as the ctest "review:sync" (label "review").

add_executable(o1602_review_sync ../test_main.cpp sync_net_edge.cpp sync_enet_memory.cpp)
target_link_libraries(o1602_review_sync PRIVATE o1602::assets o1602::sim o1602::net o1602_enet)
o1602_set_warnings(o1602_review_sync)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(o1602_review_sync PRIVATE -Wno-null-dereference)
endif()
add_test(NAME review:sync COMMAND o1602_review_sync)
set_tests_properties(review:sync PROPERTIES LABELS review TIMEOUT 1800)
