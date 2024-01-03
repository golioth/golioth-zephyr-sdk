/*
 * Copyright (c) 2024 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(golioth_openthread, CONFIG_GOLIOTH_SYSTEM_CLIENT_LOG_LEVEL);

#include <openthread/nat64.h>
#include <openthread/dns_client.h>
#include <zephyr/net/openthread.h>
#include <openthread/error.h>

static otDnsQueryConfig dns_query;
static char *golioth_ip6_addr;

K_SEM_DEFINE(ot_dns_resolve, 0, 1);

/* Callback for NAT64 IPv6 translated Golioth System Server address from the DNS query response */
static void ot_dns_callback(otError aError, const otDnsAddressResponse *aResponse, void *aContext)
{
	otIp6Address golioth_addr;

	if (aError != OT_ERROR_NONE) {
		LOG_ERR("Golioth System Server DNS resolving error: %d", aError);
		return;
	}

	if (otDnsAddressResponseGetAddress(aResponse, 0, &golioth_addr, NULL) == OT_ERROR_NONE) {
		otIp6AddressToString(&golioth_addr,
				     golioth_ip6_addr,
				     OT_IP6_ADDRESS_STRING_SIZE);
	}

	k_sem_give(&ot_dns_resolve);
}

int synthesize_ip6_address(char *hostname, char *ip6_addr_buffer)
{
	int err;
	otIp4Address dns_server_addr;

	golioth_ip6_addr = ip6_addr_buffer;
	struct openthread_context *ot_context = openthread_get_default_context();

	err = otIp4AddressFromString(CONFIG_DNS_SERVER1, &dns_server_addr);
	if (err != OT_ERROR_NONE) {
		LOG_ERR("DNS server IPv4 address error: %d", err);
		return err;
	}

	err = otNat64SynthesizeIp6Address(ot_context->instance,
					  &dns_server_addr,
					  &dns_query.mServerSockAddr.mAddress);
	if (err != OT_ERROR_NONE) {
		LOG_ERR("Synthesize DNS server IPv6 address error: %d", err);
		return err;
	}

	err = otDnsClientResolveIp4Address(ot_context->instance,
					   hostname,
					   ot_dns_callback,
					   ot_context,
					   &dns_query);
	if (err != OT_ERROR_NONE) {
		LOG_ERR("Golioth System Server address resolution DNS query error: %d", err);
		return err;
	}

	k_sem_take(&ot_dns_resolve, K_FOREVER);

	return 0;
}
