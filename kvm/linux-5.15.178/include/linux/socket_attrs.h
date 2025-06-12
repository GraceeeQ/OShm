#ifndef _LINUX_SOCKET_ATTRS_H
#define _LINUX_SOCKET_ATTRS_H

#include <linux/types.h>
#include <uapi/linux/socket_attrs.h>

long set_thread_socket_attrs(pid_t pid, int max_sockets, int priority_level, unsigned int flags);

#endif /* _LINUX_SOCKET_ATTRS_H */