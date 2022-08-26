#ifndef __NET_GOLIOTH_PATHV_H__
#define __NET_GOLIOTH_PATHV_H__

#include <zephyr/sys/util.h>

#define PATHV(...)					\
	(const uint8_t *[]) {				\
		FOR_EACH(IDENTITY, (,), __VA_ARGS__),	\
		NULL, /* sentinel */			\
	}

#endif /* __NET_GOLIOTH_PATHV_H__ */
