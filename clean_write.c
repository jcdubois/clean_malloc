/**
 * Copyright (c) 2012 Jean-Christophe DUBOIS.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published 
 * by the Free Software Foundation; either version 2.1, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * @file clear_write.c
 * @author Jean-Christophe DUBOIS (jcd@tribudubois.net)
 * @brief write/send interposing library.
 *
 * This library redefines the following functions:
 * - write
 * - send
 * - sendto
 * - sendmsg
 *
 * In turn these functions will use the following functions from glibc.
 * - write
 * - send
 * - sendto
 * - sendmsg
 *
 * Usage: LD_PRELOAD=./clean_write.so command args ...
 *
 * Changes:
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>

#define __USE_GNU
#include <dlfcn.h>

static ssize_t default_write(int fd, const void *buf, size_t count);
static ssize_t default_sendto(int sockfd, const void *buf, size_t len,
			      int flags, const struct sockaddr *dest_addr,
			      socklen_t addrlen);
static ssize_t default_sendmsg(int sockfd, const struct msghdr *msg, int flags);

static ssize_t(*real_write) (int fd, const void *buf, size_t count) =
    default_write;
static ssize_t(*real_sendto) (int sockfd, const void *buf, size_t len,
			      int flags, const struct sockaddr * dest_addr,
			      socklen_t addrlen) = default_sendto;
static ssize_t(*real_sendmsg) (int sockfd, const struct msghdr * msg,
			       int flags) = default_sendmsg;

#define MIN(a,b)	((a>b) ? b : a)

#ifdef DEBUG
#define debug(fmt, ...) fprintf(stderr, "%s " fmt, __func__, ## __VA_ARGS__)
#else
#define debug(fmt, ...)
#endif

/**
 * We use a constructor to lookup the write addresses
 * of the glibc functions.
 * However it might happen that our functions are called before the 
 * constructor is called. In such instance we setup some default versions
 * of these functions that will force the call to the constructor.
 */
__attribute__ ((constructor))
void init_write(void)
{
	static int init_done;
	void *ptr;

	/*
	 * TBD: We should protect init_done access/modification in case of
	 * multithreaded application. But as it is mostly used by the 
	 * dynamic linker it might not be necessary as we should not be 
	 * in multithreaded mode a this time.
	 */
	if (init_done) {
		return;
	}

	init_done = 1;

	/* We resolve the various symbols we are going to overload and use */

	ptr = dlsym(RTLD_NEXT, "write");
	if (ptr) {
		real_write = ptr;
	} else {
		debug("write %s\n", dlerror());
	}

	ptr = dlsym(RTLD_NEXT, "sendto");
	if (ptr) {
		real_sendto = ptr;
	} else {
		debug("sendto %s\n", dlerror());
	}

	ptr = dlsym(RTLD_NEXT, "sendmsg");
	if (ptr) {
		real_sendmsg = ptr;
	} else {
		debug("sendmsg %s\n", dlerror());
	}
}

/*
 */
static ssize_t default_write(int fd, const void *buf, size_t count)
{
	init_write();

	if (real_write == default_write) {
		debug("Failed to resolve 'write', returning EINVAL\n");
		errno = EINVAL;
		return -1;
	}

	return real_write(fd, buf, count);
}

static ssize_t default_sendto(int sockfd, const void *buf, size_t len,
			      int flags, const struct sockaddr *dest_addr,
			      socklen_t addrlen)
{
	init_write();

	if (real_sendto == default_sendto) {
		debug("Failed to resolve 'sendto', returning EINVAL\n");
		errno = EINVAL;
		return -1;
	}

	return real_sendto(sockfd, buf, len, flags, dest_addr, addrlen);
}

static ssize_t default_sendmsg(int sockfd, const struct msghdr *msg, int flags)
{
	init_write();

	if (real_sendmsg == default_sendmsg) {
		debug("Failed to resolve 'sendmsg', returning EINVAL\n");
		errno = EINVAL;
		return -1;
	}

	return real_sendmsg(sockfd, msg, flags);
}

/**
 */
ssize_t write(int fd, const void *buf, size_t count)
{
	ssize_t rc = real_write(fd, buf, count);

	if (buf && count) {
		/**
		 * We violate the prototype here as buf is a const void 
		 * We should not change the content of buf but ...
		 * most of the time it is harmless and it allows to 
		 * guaranty that the data is indeed zeroized as it is
		 * not needed anymore
		 * This might prevent some application to work if they do expect
		 * their data to be still available after the write.
		 */

		memset((void *)buf, 0, count);
	}

	return rc;
}

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
	       const struct sockaddr * dest_addr, socklen_t addrlen)
{
	ssize_t rc = real_sendto(sockfd, buf, len, flags, dest_addr, addrlen);

	if (buf && len) {
		/**
		 * We violate the prototype here as buf is a const void 
		 * We should not change the content of buf but ...
		 * most of the time it is harmless and it allows to 
		 * guaranty that the data is indeed zeroized as it is
		 * not needed anymore
		 * This might prevent some application to work if they do expect
		 * their data to be still available after the write.
		 */

		memset((void *)buf, 0, len);
	}

	return rc;
}

ssize_t sendmsg(int sockfd, const struct msghdr * msg, int flags)
{
	ssize_t rc = real_sendmsg(sockfd, msg, flags);
	int count = msg->msg_iovlen;

	if (msg) {
		while (count) {
			count--;
			memset(msg->msg_iov[count].iov_base, 0,
			       msg->msg_iov[count].iov_len);
		}
	}

	return rc;

}

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
	return sendto(sockfd, buf, len, flags, NULL, 0);
}
