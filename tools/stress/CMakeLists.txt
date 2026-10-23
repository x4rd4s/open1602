# SPDX-License-Identifier: GPL-3.0-or-later
# o1602-stress: the simulation under load (tick times, memory, savegame and snapshot sizes) on
# synthetic worlds, or with --data on the installation's (tools/stress/run-on-device.sh).

add_executable(o1602-stress stress.cpp content.cpp)
target_link_libraries(o1602-stress PRIVATE o1602::assets o1602::sim o1602::net)
o1602_set_warnings(o1602-stress)
o1602_utf8_manifest(o1602-stress)
