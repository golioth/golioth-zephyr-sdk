/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GOLIOTH_INCLUDE_LOGGING_GOLIOTH_H_
#define GOLIOTH_INCLUDE_LOGGING_GOLIOTH_H_

#include <net/golioth/golioth_type_def.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup logging Golioth Logging
 * Functions for interfacing with the Golioth Zephyr logging backend.
 * @{
 */

int log_backend_golioth_init(struct golioth_client *client);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* GOLIOTH_INCLUDE_LOGGING_GOLIOTH_H_ */
