#
# Copyright (C) 2021 Golioth, Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_library_sources_ifdef(CONFIG_LOG_BACKEND_GOLIOTH log_backend_golioth.c)
