/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __INCLUDE_NET_GOLIOTH_LIGHTDB_HELPERS_H__
#define __INCLUDE_NET_GOLIOTH_LIGHTDB_HELPERS_H__

#include <stdbool.h>
#include <stdint.h>

#include "lightdb_helpers_priv.h"

struct golioth_client;

/**
 * @brief Set simple value to Golioth's Light DB
 *
 * Set simple (not a structure) value to Golioth's Light DB.
 *
 * @param client Golioth instance
 * @param path Light DB resource path
 * @param value Value to be set to LightDB
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
#define golioth_lightdb_set_auto(_client, _path, _value)	\
	_Generic((_value),					\
		 bool: golioth_lightdb_set_bool,		\
		 int8_t: golioth_lightdb_set_int64_t,		\
		 int16_t: golioth_lightdb_set_int64_t,		\
		 int32_t: golioth_lightdb_set_int64_t,		\
		 int64_t: golioth_lightdb_set_int64_t,		\
		 uint8_t: golioth_lightdb_set_uint64_t,		\
		 uint16_t: golioth_lightdb_set_uint64_t,	\
		 uint32_t: golioth_lightdb_set_uint64_t,	\
		 uint64_t: golioth_lightdb_set_uint64_t,	\
		 float: golioth_lightdb_set_float,		\
		 double: golioth_lightdb_set_double,		\
		 char *: golioth_lightdb_set_stringz,		\
		 const char *: golioth_lightdb_set_stringz	\
	)(_client, _path, _value)


/**
 * @brief Set boolean value to Golioth's Light DB
 *
 * @param client Golioth instance
 * @param path Light DB resource path
 * @param value Value to be set to LightDB
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
static inline int golioth_lightdb_set_bool(struct golioth_client *client,
					   const uint8_t *path,
					   bool value)
{
	return golioth_lightdb_set_basic(client, path, &value,
					 golioth_lightdb_qcbor_add_bool);
}

/**
 * @brief Set int64_t value to Golioth's Light DB
 *
 * @param client Golioth instance
 * @param path Light DB resource path
 * @param value Value to be set to LightDB
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
static inline int golioth_lightdb_set_int64_t(struct golioth_client *client,
					      const uint8_t *path,
					      int64_t value)
{
	return golioth_lightdb_set_basic(client, path, &value,
					 golioth_lightdb_qcbor_add_int64_t);
}

/**
 * @brief Set uint64_t value to Golioth's Light DB
 *
 * @param client Golioth instance
 * @param path Light DB resource path
 * @param value Value to be set to LightDB
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
static inline int golioth_lightdb_set_uint64_t(struct golioth_client *client,
					       const uint8_t *path,
					       uint64_t value)
{
	return golioth_lightdb_set_basic(client, path, &value,
					 golioth_lightdb_qcbor_add_uint64_t);
}

/**
 * @brief Set float value to Golioth's Light DB
 *
 * @param client Golioth instance
 * @param path Light DB resource path
 * @param value Value to be set to LightDB
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
static inline int golioth_lightdb_set_float(struct golioth_client *client,
					    const uint8_t *path,
					    float value)
{
	return golioth_lightdb_set_basic(client, path, &value,
					 golioth_lightdb_qcbor_add_float);
}

/**
 * @brief Set double value to Golioth's Light DB
 *
 * @param client Golioth instance
 * @param path Light DB resource path
 * @param value Value to be set to LightDB
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
static inline int golioth_lightdb_set_double(struct golioth_client *client,
					     const uint8_t *path,
					     double value)
{
	return golioth_lightdb_set_basic(client, path, &value,
					 golioth_lightdb_qcbor_add_double);
}

/**
 * @brief Set string value to Golioth's Light DB
 *
 * @param client Golioth instance
 * @param path Light DB resource path
 * @param value Value to be set to LightDB
 *
 * @retval 0 On success
 * @retval <0 On failure
 */
static inline int golioth_lightdb_set_stringz(struct golioth_client *client,
					      const uint8_t *path,
					      const char *value)
{
	return golioth_lightdb_set_basic(client, path, value,
					 golioth_lightdb_qcbor_add_stringz);
}

#endif /* __INCLUDE_NET_GOLIOTH_LIGHTDB_HELPERS_H__ */
