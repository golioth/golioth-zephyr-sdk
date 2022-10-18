/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __NET_GOLIOTH_GOLIOTH_UTILS_H__
#define __NET_GOLIOTH_GOLIOTH_UTILS_H__

#include <net/golioth.h>

enum coap_block_size golioth_estimated_coap_block_size(struct golioth_client *client);

/**
 * @brief Default response handler
 *
 * Default response handler, which generates error logs in case of errors and debug logs in case of
 * success.
 *
 * @param rsp Response information
 *
 * @retval 0 In every case
 */
int golioth_req_rsp_default_handler(struct golioth_req_rsp *rsp);

#endif /* __NET_GOLIOTH_GOLIOTH_UTILS_H__ */
