# Copyright (c) 2023 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

project(mcuboot_native_sim LANGUAGES)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE} COMPONENTS
  python
  user_cache
  extensions
  basic_settings
  west
  root
  zephyr_module
  boards
  shields
  arch
  generated_file_directories
  dts
  soc
)

find_package(TargetTools)

enable_language(C CXX ASM)
