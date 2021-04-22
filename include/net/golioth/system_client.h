/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GOLIOTH_INCLUDE_NET_GOLIOTH_SYSTEM_CLIENT_H_
#define GOLIOTH_INCLUDE_NET_GOLIOTH_SYSTEM_CLIENT_H_

#include <net/golioth.h>

extern struct golioth_client _golioth_system_client;

/**
 * @brief Start Golioth system client
 */
void golioth_system_client_start(void);

/**
 * @brief Get pointer to Golioth system client instance
 */
#define GOLIOTH_SYSTEM_CLIENT_GET()	(&_golioth_system_client)

#endif /* GOLIOTH_INCLUDE_NET_GOLIOTH_SYSTEM_CLIENT_H_ */
