/*
 * Copyright (c) 2023 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GOLIOTH_INCLUDE_COMPAT_INIT_H_
#define GOLIOTH_INCLUDE_COMPAT_INIT_H_

#ifdef CONFIG_GOLIOTH_COMPAT_INIT

#undef SYS_INIT
#define SYS_INIT(init, level, priority)					\
	SYS_INIT_NAMED(init, (int (*)(const struct device *)) init,	\
		       level, priority)

#endif /* CONFIG_GOLIOTH_COMPAT_INIT */

#endif /* GOLIOTH_INCLUDE_COMPAT_INIT_H_ */
