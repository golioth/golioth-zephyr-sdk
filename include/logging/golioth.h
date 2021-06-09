/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GOLIOTH_INCLUDE_LOGGING_GOLIOTH_H_
#define GOLIOTH_INCLUDE_LOGGING_GOLIOTH_H_

struct golioth_client;

/**
 * @defgroup logging Golioth Logging
 * Functions for interfacing with the Golioth Zephyr logging backend.
 * @{
 */

int log_backend_golioth_init(struct golioth_client *client);

#endif /* GOLIOTH_INCLUDE_LOGGING_GOLIOTH_H_ */
