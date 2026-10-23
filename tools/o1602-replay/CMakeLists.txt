# SPDX-License-Identifier: GPL-3.0-or-later
# o1602-replay: runs a game headless with a command log and prints its state hash every N ticks, for
# comparing builds, compilers and platforms (determinism, ADR 0004). Only the simulation, so that it
# builds quickly in every configuration of the CI matrix.

add_executable(o1602-replay main.cpp)
target_link_libraries(o1602-replay PRIVATE o1602::assets o1602::sim)
o1602_set_warnings(o1602-replay)
o1602_utf8_manifest(o1602-replay)
