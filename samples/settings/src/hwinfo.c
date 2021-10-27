/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <logging/log.h>
LOG_MODULE_REGISTER(hwinfo, LOG_LEVEL_DBG);

#include <zephyr.h>
#include <stdio.h>
#include <stdlib.h>
#include <shell/shell.h>
#include <sys/printk.h>
#include <init.h>

#include <settings/settings.h>

#include <string.h>
#include <drivers/hwinfo.h>

#define DEVICE_ID_BIN_MAX_SIZE 16
#define DEVICE_ID_HEX_MAX_SIZE ((DEVICE_ID_BIN_MAX_SIZE * 2) + 1)

bool get_device_identity(char *id, int id_max_len)
{
  uint8_t hwinfo_id[DEVICE_ID_BIN_MAX_SIZE];
  ssize_t length;

  length = hwinfo_get_device_id(hwinfo_id, DEVICE_ID_BIN_MAX_SIZE);
  if (length <= 0)
  {
    return false;
  }

  memset(id, 0, id_max_len);
  length = bin2hex(hwinfo_id, (size_t)length, id, id_max_len - 1);

  return length > 0;
}

static int hwinfo_settings_get(const char *name, char *dst, int val_len_max)
{
  size_t val_len;

  if (!strcmp(name, "devid"))
  { 
    if (!get_device_identity(dst, val_len_max))
    {
      return -ENOENT;
    }
    val_len = strlen(dst);
  }
  else
  {
    return -ENOENT;
  }

  return val_len;
}

SETTINGS_STATIC_HANDLER_DEFINE(hwinfo, "hwinfo",
                               IS_ENABLED(CONFIG_SETTINGS_RUNTIME) ? hwinfo_settings_get : NULL,
                               NULL, NULL, NULL);