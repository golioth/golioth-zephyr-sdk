# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dfu)

target_sources(app PRIVATE src/main.c)
target_sources_ifdef(CONFIG_BOOTLOADER_MCUBOOT_COMPATIBLE app PRIVATE src/flash.c)

if(CONFIG_BUILD_OUTPUT_EXE AND CONFIG_NATIVE_LIBRARY)
  # Arguments to imgtool.
  if(NOT CONFIG_MCUBOOT_EXTRA_IMGTOOL_ARGS STREQUAL "")
    # Separate extra arguments into the proper format for adding to
    # extra_post_build_commands.
    #
    # Use UNIX_COMMAND syntax for uniform results across host
    # platforms.
    separate_arguments(imgtool_extra UNIX_COMMAND ${CONFIG_MCUBOOT_EXTRA_IMGTOOL_ARGS})
  else()
    set(imgtool_extra)
  endif()

  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.signed.exe
    COMMAND imgtool sign --align=8 --pad-header --header-size=512 --slot-size=8388608 --confirm --version 0.0.0+0 ${imgtool_extra}
    ${CMAKE_CURRENT_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.exe
    ${CMAKE_CURRENT_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.signed.exe
    DEPENDS native_runner_executable
  )

  add_custom_target(native_runner_signed_executable ALL
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/zephyr/${CONFIG_KERNEL_BIN_NAME}.signed.exe
  )
endif()
