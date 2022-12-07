/*
 * Copyright (c) 2022 Golioth, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include "sockets_internal.h"

LOG_MODULE_REGISTER(net_sock_wrapper, CONFIG_NET_SOCKETS_LOG_LEVEL);

__net_socket struct wrapper_context {
	int fd;
	int my_fd;
	bool is_used;
	struct k_poll_signal poll_signal;
	struct k_work work;
	struct k_work_q work_q;
};

static struct wrapper_context
	wrapper_contexts[CONFIG_NET_SOCKETS_OFFLOAD_POLL_WRAPPER_CONTEXT_MAX];

K_THREAD_STACK_ARRAY_DEFINE(sock_wrapper_poll_stack,
			    CONFIG_NET_SOCKETS_OFFLOAD_POLL_WRAPPER_CONTEXT_MAX,
			    CONFIG_NET_SOCKETS_OFFLOAD_POLL_WRAPPER_THREAD_STACK_SIZE);

#define SOCK_WRAPPER_POLL_STACK_NAME(i, _) "threaded poll " #i

static const char * const sock_wrapper_poll_stack_names[] = {
	LISTIFY(CONFIG_NET_SOCKETS_OFFLOAD_POLL_WRAPPER_CONTEXT_MAX,
		SOCK_WRAPPER_POLL_STACK_NAME, (,))
};

static K_MUTEX_DEFINE(wrapper_lock);

static int sock_wrapper_create(int family, int type, int proto);

static ssize_t sock_wrapper_recvfrom_vmeth(void *obj, void *buf,
					   size_t max_len, int flags,
					   struct sockaddr *addr,
					   socklen_t *addrlen);

static ssize_t sock_wrapper_sendto_vmeth(void *obj, const void *buf,
					 size_t len, int flags,
					 const struct sockaddr *addr,
					 socklen_t addrlen);

static void __wrapper_ctx_free(struct wrapper_context *wrapper)
{
	wrapper->fd = -1;
	wrapper->my_fd = -1;
	wrapper->is_used = false;
}

static int sock_wrapper_socket(int family, int type, int proto, bool *is_offloaded)
{
	STRUCT_SECTION_FOREACH(net_socket_register, sock_family) {
		/* Ignore wrapper itself. */
		if (sock_family->handler == sock_wrapper_create) {
			continue;
		}

		if (sock_family->family != family &&
		    sock_family->family != AF_UNSPEC) {
			continue;
		}

		NET_ASSERT(sock_family->is_supported);

		if (!sock_family->is_supported(family, type, proto)) {
			continue;
		}

		*is_offloaded = sock_family->is_offloaded;

		return sock_family->handler(family, type, proto);
	}

	errno = EAFNOSUPPORT;
	return -1;
}

static ssize_t sock_wrapper_read_vmeth(void *obj, void *buffer, size_t count)
{
	return sock_wrapper_recvfrom_vmeth(obj, buffer, count, 0, NULL, 0);
}

static ssize_t sock_wrapper_write_vmeth(void *obj, const void *buffer, size_t count)
{
	return sock_wrapper_sendto_vmeth(obj, buffer, count, 0, NULL, 0);
}

static void sock_wrapper_poll_handler(struct k_work *work)
{
	struct wrapper_context *wrapper = CONTAINER_OF(work, struct wrapper_context, work);
	struct zsock_pollfd pollfd = {
		.fd = wrapper->fd,
		.events = ZSOCK_POLLIN,
		.revents = 0,
	};
	int retval;

	retval = zsock_poll(&pollfd, 1, -1);

	k_poll_signal_raise(&wrapper->poll_signal, retval);
}

static int sock_wrapper_poll_prepare_ctx(struct wrapper_context *wrapper,
					 struct zsock_pollfd *pfd,
					 struct k_poll_event **pev,
					 struct k_poll_event *pev_end)
{
	if (pfd->events & ZSOCK_POLLIN) {
		if (*pev == pev_end) {
			return -ENOMEM;
		}

		(*pev)->obj = &wrapper->poll_signal;
		(*pev)->type = K_POLL_TYPE_SIGNAL;
		(*pev)->mode = K_POLL_MODE_NOTIFY_ONLY;
		(*pev)->state = K_POLL_STATE_NOT_READY;
		(*pev)++;
	}

	if (pfd->events & ZSOCK_POLLOUT) {
		return -ENOTSUP;
	}

	k_work_submit_to_queue(&wrapper->work_q, &wrapper->work);

	return 0;
}

static int sock_wrapper_poll_update_ctx(struct wrapper_context *wrapper,
					struct zsock_pollfd *pfd,
					struct k_poll_event **pev)
{
	if (pfd->events & ZSOCK_POLLIN) {
		if ((*pev)->state != K_POLL_STATE_NOT_READY) {
			pfd->revents |= ZSOCK_POLLIN;
			k_poll_signal_reset(&wrapper->poll_signal);
		}
		(*pev)++;
	}

	return 0;
}

static int sock_wrapper_ioctl_vmeth(void *obj, unsigned int request, va_list args)
{
	struct wrapper_context *wrapper = obj;
	const struct fd_op_vtable *vtable;
	struct k_mutex *lock;
	void *wrapped_obj;
	int ret;

	switch (request) {
	case ZFD_IOCTL_POLL_PREPARE: {
		struct zsock_pollfd *pfd;
		struct k_poll_event **pev;
		struct k_poll_event *pev_end;

		pfd = va_arg(args, struct zsock_pollfd *);
		pev = va_arg(args, struct k_poll_event **);
		pev_end = va_arg(args, struct k_poll_event *);

		return sock_wrapper_poll_prepare_ctx(obj, pfd, pev, pev_end);
	}

	case ZFD_IOCTL_POLL_UPDATE: {
		struct zsock_pollfd *pfd;
		struct k_poll_event **pev;

		pfd = va_arg(args, struct zsock_pollfd *);
		pev = va_arg(args, struct k_poll_event **);

		return sock_wrapper_poll_update_ctx(obj, pfd, pev);
	}

	default:
		break;
	}

	wrapped_obj = z_get_fd_obj_and_vtable(wrapper->fd, &vtable, &lock);
	if (!wrapped_obj) {
		return -1;
	}

	if (lock) {
		k_mutex_lock(lock, K_FOREVER);
	}

	ret = vtable->ioctl(wrapped_obj, request, args);

	if (lock) {
		k_mutex_unlock(lock);
	}

	return ret;
}

static int sock_wrapper_shutdown_vmeth(void *obj, int how)
{
	struct wrapper_context *wrapper = obj;

	return zsock_shutdown(wrapper->fd, how);
}

static int sock_wrapper_bind_vmeth(void *obj, const struct sockaddr *addr, socklen_t addrlen)
{
	struct wrapper_context *wrapper = obj;

	return zsock_bind(wrapper->fd, addr, addrlen);
}

static int sock_wrapper_connect_vmeth(void *obj, const struct sockaddr *addr,
				      socklen_t addrlen)
{
	struct wrapper_context *wrapper = obj;

	return zsock_connect(wrapper->fd, addr, addrlen);
}

static int sock_wrapper_listen_vmeth(void *obj, int backlog)
{
	struct wrapper_context *wrapper = obj;

	return zsock_listen(wrapper->fd, backlog);
}

static int sock_wrapper_accept_vmeth(void *obj, struct sockaddr *addr, socklen_t *addrlen)
{
	struct wrapper_context *wrapper = obj;

	return zsock_accept(wrapper->fd, addr, addrlen);
}

static ssize_t sock_wrapper_sendto_vmeth(void *obj, const void *buf,
					 size_t len, int flags,
					 const struct sockaddr *addr,
					 socklen_t addrlen)
{
	struct wrapper_context *wrapper = obj;

	return zsock_sendto(wrapper->fd, buf, len, flags, addr, addrlen);
}

static ssize_t sock_wrapper_sendmsg_vmeth(void *obj, const struct msghdr *msg,
					  int flags)
{
	struct wrapper_context *wrapper = obj;

	return zsock_sendmsg(wrapper->fd, msg, flags);
}

static ssize_t sock_wrapper_recvfrom_vmeth(void *obj, void *buf,
					   size_t max_len, int flags,
					   struct sockaddr *addr,
					   socklen_t *addrlen)
{
	struct wrapper_context *wrapper = obj;

	return zsock_recvfrom(wrapper->fd, buf, max_len, flags, addr, addrlen);
}

static int sock_wrapper_getsockopt_vmeth(void *obj, int level, int optname,
					 void *optval, socklen_t *optlen)
{
	struct wrapper_context *wrapper = obj;

	return zsock_getsockopt(wrapper->fd, level, optname, optval, optlen);
}

static int sock_wrapper_setsockopt_vmeth(void *obj, int level, int optname,
					 const void *optval, socklen_t optlen)
{
	struct wrapper_context *wrapper = obj;

	return zsock_setsockopt(wrapper->fd, level, optname, optval, optlen);
}

static int sock_wrapper_close_vmeth(void *obj)
{
	struct wrapper_context *wrapper = obj;
	struct k_work_sync sync;
	int retval = 0;

	(void)k_mutex_lock(&wrapper_lock, K_FOREVER);

	/*
	 * Start work cancellation, to make sure work won't be rerun in case
	 * work is both in the running and queued state.
	 */
	k_work_cancel(&wrapper->work);

	/* Close wrapped socket */
	zsock_close(wrapper->fd);

	/* Make sure that threaded poll() is no longer running */
	k_work_cancel_sync(&wrapper->work, &sync);

	__wrapper_ctx_free(wrapper);

	k_mutex_unlock(&wrapper_lock);

	return retval;
}

static int sock_wrapper_getpeername_vmeth(void *obj, struct sockaddr *addr,
					  socklen_t *addrlen)
{
	struct wrapper_context *wrapper = obj;

	return zsock_getpeername(wrapper->fd, addr, addrlen);
}

static int sock_wrapper_getsockname_vmeth(void *obj, struct sockaddr *addr,
					  socklen_t *addrlen)
{
	struct wrapper_context *wrapper = obj;

	return zsock_getsockname(wrapper->fd, addr, addrlen);
}

static const struct socket_op_vtable sock_wrapper_fd_op_vtable = {
	.fd_vtable = {
		.read = sock_wrapper_read_vmeth,
		.write = sock_wrapper_write_vmeth,
		.close = sock_wrapper_close_vmeth,
		.ioctl = sock_wrapper_ioctl_vmeth,
	},
	.shutdown = sock_wrapper_shutdown_vmeth,
	.bind = sock_wrapper_bind_vmeth,
	.connect = sock_wrapper_connect_vmeth,
	.listen = sock_wrapper_listen_vmeth,
	.accept = sock_wrapper_accept_vmeth,
	.sendto = sock_wrapper_sendto_vmeth,
	.sendmsg = sock_wrapper_sendmsg_vmeth,
	.recvfrom = sock_wrapper_recvfrom_vmeth,
	.getsockopt = sock_wrapper_getsockopt_vmeth,
	.setsockopt = sock_wrapper_setsockopt_vmeth,
	.getpeername = sock_wrapper_getpeername_vmeth,
	.getsockname = sock_wrapper_getsockname_vmeth,
};

static int sock_wrapper_create(int family, int type, int proto)
{
	struct wrapper_context *wrapper = NULL;
	bool is_offloaded;
	int fd = -1;

	fd = sock_wrapper_socket(family, type, proto, &is_offloaded);
	if (fd < 0) {
		return fd;
	}

	if (!is_offloaded) {
		/*
		 * Double check using ioctl(..., POLL_PREPARE, ...), as NCS does not register
		 * nrf91_sockets as offloaded (but it should).
		 */
		const struct fd_op_vtable *vtable;
		void *ctx;

		ctx = z_get_fd_obj_and_vtable(fd, &vtable, NULL);
		if (ctx) {
			struct zsock_pollfd pollfd = {
				.fd = fd,
			};
			struct k_poll_event *pev;
			int ret;

			ret = z_fdtable_call_ioctl(vtable, ctx,
						   ZFD_IOCTL_POLL_PREPARE,
						   &pollfd,
						   &pev, pev);
			if (ret == -EXDEV) {
				LOG_DBG("ioctl(%d, POLL_PREPARE) -> EXDEV", fd);
				is_offloaded = true;
			}
		}

	}

	if (!is_offloaded) {
		/* We are done, no need to wrap anything */
		return fd;
	}

	LOG_DBG("wrapping fd %d", fd);

	(void)k_mutex_lock(&wrapper_lock, K_FOREVER);

	for (int i = 0; i < ARRAY_SIZE(wrapper_contexts); i++) {
		if (wrapper_contexts[i].is_used) {
			continue;
		}

		wrapper = &wrapper_contexts[i];
		break;
	}

	if (!wrapper) {
		errno = ENOMEM;
		goto unlock_and_close;
	}

	wrapper->is_used = true;
	wrapper->fd = fd;

	fd = z_reserve_fd();
	if (fd < 0) {
		fd = wrapper->fd;
		goto free_ctx;
	}

	wrapper->my_fd = fd;

	z_finalize_fd(fd, wrapper,
		      (const struct fd_op_vtable *)&sock_wrapper_fd_op_vtable);

	k_mutex_unlock(&wrapper_lock);

	return fd;

free_ctx:
	__wrapper_ctx_free(wrapper);

unlock_and_close:
	k_mutex_unlock(&wrapper_lock);

	zsock_close(fd);

	return -1;
}

static bool is_supported(int family, int type, int proto)
{
	return true;
}

NET_SOCKET_REGISTER(sock_wrapper,
		    CONFIG_NET_SOCKETS_OFFLOAD_POLL_WRAPPER_SOCKET_PRIORITY,
		    AF_UNSPEC, is_supported,
		    sock_wrapper_create);

static int sock_wrapper_init(const struct device *dev)
{
	struct k_work_queue_config cfg = {};
	struct wrapper_context *wrapper;

	ARG_UNUSED(dev);

	for (wrapper = wrapper_contexts;
	     wrapper < &wrapper_contexts[ARRAY_SIZE(wrapper_contexts)];
	     wrapper++) {
		int idx = wrapper - wrapper_contexts;

		k_poll_signal_init(&wrapper->poll_signal);
		k_work_init(&wrapper->work, sock_wrapper_poll_handler);
		k_work_queue_init(&wrapper->work_q);

		if (IS_ENABLED(CONFIG_THREAD_NAME)) {
			cfg.name = sock_wrapper_poll_stack_names[idx];
		}

		k_work_queue_start(&wrapper->work_q,
				   sock_wrapper_poll_stack[idx],
				   K_THREAD_STACK_SIZEOF(sock_wrapper_poll_stack[idx]),
				   CONFIG_NET_SOCKETS_OFFLOAD_POLL_WRAPPER_THREAD_PRIORITY,
				   &cfg);
	}

	return 0;
}

SYS_INIT(sock_wrapper_init, POST_KERNEL, 10);
