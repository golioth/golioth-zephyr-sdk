# Copyright (c) 2023 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20)

project(mcuboot_espressif LANGUAGES)

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

# The zephyr/runners.yaml file in the build directory is used to
# configure the scripts/west_commands/runners Python package used
# by 'west flash', 'west debug', etc.
#
# See zephyr/cmake/kernel.cmake for more details.
add_custom_target(runners_yaml_props_target)

macro(app_set_runner_args)
  dt_nodelabel(dts_partition_path NODELABEL "boot_partition")
  dt_reg_addr(boot_off PATH ${dts_partition_path})
  board_runner_args(esp32 "--esp-app-address=${boot_off}")
endmacro()

include(${BOARD_DIR}/board.cmake OPTIONAL)

# Select flash target for 'west flash'
set_target_properties(runners_yaml_props_target PROPERTIES "bin_file"
  "${CMAKE_BINARY_DIR}/mcuboot_espressif/mcuboot_${CONFIG_SOC}.bin")

# Include flash targets to make 'west flash' work from sysbuild.
add_subdirectory(${ZEPHYR_BASE}/cmake/flash ${CMAKE_BINARY_DIR}/flash)

# Collect MCUboot configuration options from Kconfig
set(MCUBOOT_ESPRESSIF_CONFIG_OPTIONS)

if(CONFIG_BOOT_USE_MBEDTLS)
  list(APPEND MCUBOOT_ESPRESSIF_CONFIG_OPTIONS "CONFIG_ESP_USE_MBEDTLS=1")
endif()

if(CONFIG_BOOT_USE_TINYCRYPT)
  list(APPEND MCUBOOT_ESPRESSIF_CONFIG_OPTIONS "CONFIG_ESP_USE_TINYCRYPT=1")
endif()

if(CONFIG_BOOT_SIGNATURE_TYPE_RSA)
  list(APPEND MCUBOOT_ESPRESSIF_CONFIG_OPTIONS "CONFIG_ESP_SIGN_RSA=1")
  list(APPEND MCUBOOT_ESPRESSIF_CONFIG_OPTIONS "CONFIG_ESP_SIGN_RSA_LEN=${CONFIG_BOOT_SIGNATURE_TYPE_RSA_LEN}")
endif()

if(CONFIG_BOOT_SIGNATURE_TYPE_ECDSA_P256)
  list(APPEND MCUBOOT_ESPRESSIF_CONFIG_OPTIONS "CONFIG_ESP_SIGN_EC256=1")
endif()

if(CONFIG_BOOT_SIGNATURE_TYPE_ED25519)
  list(APPEND MCUBOOT_ESPRESSIF_CONFIG_OPTIONS "CONFIG_ESP_SIGN_ED25519=y")
endif()

if(CONFIG_BOOT_SIGNATURE_KEY_FILE)
  list(APPEND MCUBOOT_ESPRESSIF_CONFIG_OPTIONS "CONFIG_ESP_SIGN_KEY_FILE=${CONFIG_BOOT_SIGNATURE_KEY_FILE}")
endif()

# Create configuration file (fragment) out of configuration options
list(JOIN MCUBOOT_ESPRESSIF_CONFIG_OPTIONS "\n" MCUBOOT_ESPRESSIF_CONFIG)
file(WRITE ${CMAKE_BINARY_DIR}/mcuboot_espressif/bootloader.conf "${MCUBOOT_ESPRESSIF_CONFIG}")

# Use default configuration file in MCUboot repo as a base
set(MCUBOOT_CONFIG_FILE "${ZEPHYR_MCUBOOT_MODULE_DIR}/boot/espressif/port/${CONFIG_SOC}/bootloader.conf")
# Add Kconfig options collected into configuration fragment file
set(MCUBOOT_CONFIG_FILE "${MCUBOOT_CONFIG_FILE} ${CMAKE_BINARY_DIR}/mcuboot_espressif/bootloader.conf")

include(ExternalProject)

ExternalProject_Add(
  mcuboot_espressif
  SOURCE_DIR ${ZEPHYR_MCUBOOT_MODULE_DIR}/boot/espressif/
  BINARY_DIR ${CMAKE_BINARY_DIR}/mcuboot_espressif
  CMAKE_ARGS
    -DCMAKE_C_COMPILER_WORKS=${CMAKE_C_COMPILER_WORKS}
    -DCMAKE_CXX_COMPILER_WORKS=${CMAKE_CXX_COMPILER_WORKS}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
    -DCMAKE_ASM_COMPILER=${CMAKE_ASM_COMPILER}
    -DCMAKE_SYSTEM_NAME=${CMAKE_SYSTEM_NAME}
    -DMCUBOOT_TARGET=${CONFIG_SOC}
    -DMCUBOOT_CONFIG_FILE=${MCUBOOT_CONFIG_FILE}
    -DIDF_PATH=${ZEPHYR_MCUBOOT_MODULE_DIR}/boot/espressif/hal/esp-idf
  BUILD_COMMAND
    ${CMAKE_COMMAND} --build .
  INSTALL_COMMAND ""
  BUILD_ALWAYS True
  USES_TERMINAL_CONFIGURE True
  USES_TERMINAL_BUILD True
)
