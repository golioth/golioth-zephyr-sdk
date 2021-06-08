/*
 * Copyright (c) 2021 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef H_CFG_MGMT_
#define H_CFG_MGMT_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Command IDs for CFG management group.
 */
#define CFG_MGMT_ID_VAL             0

/**
 * @brief Registers the CFG management command handler group.
 */
void cfg_mgmt_register_group(void);

#ifdef __cplusplus
}
#endif

#endif /* H_CFG_MGMT_ */
