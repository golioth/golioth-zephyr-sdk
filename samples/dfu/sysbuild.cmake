# Copyright (c) 2021-2023 Nordic Semiconductor
# Copyright (c) 2023 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0

# Propagate bootloader and signing settings from this system to the MCUboot and
# application image build systems.
if(SB_CONFIG_BOOTLOADER_MCUBOOT_ESPRESSIF)
  set(${app_name}_CONFIG_BOOTLOADER_MCUBOOT y CACHE STRING
      "MCUBOOT is enabled as bootloader" FORCE
  )
  set(${app_name}_CONFIG_MCUBOOT_SIGNATURE_KEY_FILE
      \"${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}\" CACHE STRING
      "Signature key file for signing" FORCE
  )

  if(SB_CONFIG_BOOT_SIGNATURE_TYPE_NONE)
    set(${app_name}_CONFIG_MCUBOOT_GENERATE_UNSIGNED_IMAGE y CACHE STRING
        "MCUBOOT is configured for checksum mode" FORCE
    )
  endif()

  # Set corresponding values in mcuboot
  set(mcuboot_espressif_CONFIG_BOOT_SIGNATURE_TYPE_${SB_CONFIG_SIGNATURE_TYPE} y CACHE STRING
      "MCUBOOT signature type" FORCE
  )
  set(mcuboot_espressif_CONFIG_BOOT_SIGNATURE_KEY_FILE
      \"${SB_CONFIG_BOOT_SIGNATURE_KEY_FILE}\" CACHE STRING
      "Signature key file for signing" FORCE
  )

  ExternalZephyrProject_Add(
    APPLICATION mcuboot_espressif
    SOURCE_DIR ${ZEPHYR_GOLIOTH_MODULE_DIR}/bootloader/mcuboot_espressif
  )

  set(IMAGES "mcuboot_espressif" "${app_name}")
endif()
