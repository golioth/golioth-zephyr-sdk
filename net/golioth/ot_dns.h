/*
 * Copyright (c) 2024 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __OT_DNS_H__
#define __OT_DNS_H__

/**
 * @brief Synthesize the IPv6 address from a given host name
 *
 * Get the IPv6 address of Golioth Server to avoid hardcoding it in applications.
 * NAT64 prefix used by the Thread Border Router is set while synthesizing the address.
 *
 * @param[in] hostname A pointer to the host name for which to querry the address
 * @param[out] ip6_addr_buffer A buffer to char array to output the synthesized IPv6 address
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
int synthesize_ip6_address(char *hostname, char *ip6_addr_buffer);

#endif /* __OT_DNS_GOL_H__ */
