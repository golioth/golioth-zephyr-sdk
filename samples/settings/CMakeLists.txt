# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(settings)

target_sources(app PRIVATE src/main.c)
target_sources_ifdef(CONFIG_BT app PRIVATE src/bt.c)
target_sources_ifdef(CONFIG_MCUMGR app PRIVATE src/mcumgr.c)
