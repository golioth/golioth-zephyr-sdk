/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GOLIOTH_INCLUDE_NET_GOLIOTH_SYSTEM_CLIENT_H_
#define GOLIOTH_INCLUDE_NET_GOLIOTH_SYSTEM_CLIENT_H_

#include <net/golioth.h>

#ifdef __cplusplus
extern "C" {
#endif

extern struct golioth_client _golioth_system_client;

/**
 * @defgroup system_client Golioth System Client
 * @ingroup net
 * @{
 */

/**
 * @brief Start Golioth system client
 */
void golioth_system_client_start(void);

/**
 * @brief Stop Golioth system client
 */
void golioth_system_client_stop(void);

/**
 * @brief Get pointer to Golioth system client instance
 */
#define GOLIOTH_SYSTEM_CLIENT_GET()	(&_golioth_system_client)

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* GOLIOTH_INCLUDE_NET_GOLIOTH_SYSTEM_CLIENT_H_ */
