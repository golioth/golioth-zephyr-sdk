/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __NET_GOLIOTH_GOLIOTH_UTILS_H__
#define __NET_GOLIOTH_GOLIOTH_UTILS_H__

#include <net/golioth.h>

enum coap_block_size golioth_estimated_coap_block_size(struct golioth_client *client);

#endif /* __NET_GOLIOTH_GOLIOTH_UTILS_H__ */
