/*
 * Copyright (c) 2021 Golioth
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GOLIOTH_INCLUDE_LOGGING_GOLIOTH_H_
#define GOLIOTH_INCLUDE_LOGGING_GOLIOTH_H_

struct golioth_client;

int log_backend_golioth_init(struct golioth_client *client);

#endif /* GOLIOTH_INCLUDE_LOGGING_GOLIOTH_H_ */
