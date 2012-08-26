/**
 * Copyright (c) 2012 Jean-Christophe DUBOIS.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * @file clear_malloc.c
 * @author Jean-Christophe DUBOIS (jcd@tribudubois.net)
 * @brief memory allocation interposing library.
 *
 * The purpose of this library is to make sure that any deallocated 
 * memory (through free()) is set to 0 before being given back to the
 * system.
 * This allow to avoid that application data are still used (through
 * invalid pointer) after they have been released.
 * It also allow to prevent that deallocated data is found in newly
 * allocated memory blocks.
 *
 * This library redefines the following functions:
 * - malloc
 * - calloc
 * - realloc
 * - valloc (deprecated)
 * - memalign (deprecated)
 * - posix_memalign
 * - free
 *
 * In turn these functions will use the following functions from glibc.
 * - malloc
 * - free
 * - posix_memalign
 *
 * Usage: LD_PRELOAD=./clean_malloc.so command args ...
 *
 * Changes:
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>

#define __USE_GNU
#include <dlfcn.h>

static void *default_malloc(size_t size);
static void default_free(void *ptr);
static int default_posix_memalign(void **memptr, size_t alignment, size_t size);

static void *(*real_malloc) (size_t size) = default_malloc;
static void (*real_free) (void *ptr) = default_free;
static int (*real_posix_memalign) (void **memptr, size_t alignment,
				   size_t size) = default_posix_memalign;

#define MIN(a,b)	((a>b) ? b : a)

#ifdef DEBUG
#define debug(fmt, ...) fprintf(stderr, "%s " fmt, __func__, ## __VA_ARGS__)
#else
#define debug(fmt, ...)
#endif

#ifdef CHECK_COOKIE
#define ALLOC_COOKIE 0x12345678
#endif

struct alloc_header {
#ifdef CHECK_COOKIE
	unsigned int cookie;
#endif
	void *ptr;
	size_t requested_size;
	size_t allocated_size;
};

#define EXTRA_STATIC_SPACE	128
static char extra_space[EXTRA_STATIC_SPACE];
static int extra_space_count = 0;

__attribute__ ((constructor))
void init_malloc(void)
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

	ptr = dlsym(RTLD_NEXT, "malloc");
	if (ptr) {
		real_malloc = ptr;
	} else {
		debug("malloc %s\n", dlerror());
	}

	ptr = dlsym(RTLD_NEXT, "free");
	if (ptr) {
		real_free = ptr;
	} else {
		debug("free %s\n", dlerror());
	}

	ptr = dlsym(RTLD_NEXT, "posix_memalign");
	if (ptr) {
		real_posix_memalign = ptr;
	} else {
		debug("posix_memalign %s\n", dlerror());
	}
}

/*
 * There is a chicken and egg problem with calloc. dlsym
 * is calling calloc. So the constructor (init_malloc) will call calloc
 * to resolve the malloc/realloc/free/... funtions. 
 * calloc in turn will call malloc and we will end up here.
 * Therefore we need to write a very crude calloc that will return a bit
 * of space when dlsym needs it.
 */
static void *default_malloc(size_t size)
{
	/*
	 * TBD: We should protect extra_space_count access/modification
	 * Now at the time it is used there should not be that many 
	 * threads. So is it really needed? 
	 */
	char *ptr = &extra_space[extra_space_count];

	if ((extra_space_count + size) > EXTRA_STATIC_SPACE) {
		/*
		 * If too much data is requested, this is not dlsym.
		 * So we can force the call to init_malloc.
		 */
		init_malloc();

		if (real_malloc == default_malloc) {
			debug("Failed to resolve 'malloc', returning NULL\n");
			return NULL;
		}

		return real_malloc(size);
	} else {
		extra_space_count += size;
	}

	return ptr;
}

static void default_free(void *ptr)
{
	/*
	 * if free is called before the constructor, we force the 
	 * constructor.
	 */
	init_malloc();

	/* if real_free was not found, we return NULL */
	if (real_free == default_free) {
		debug("Failed to resolve 'free'\n");
		return;
	}

	/* We can now use the real_free */
	real_free(ptr);
}

static int default_posix_memalign(void **memptr, size_t alignment, size_t size)
{
	/*
	 * if posix_memalign is called before the constructor, we force the 
	 * constructor.
	 */
	init_malloc();

	/* if real_posix_memalign was not found, we return NULL */
	if (real_posix_memalign == default_posix_memalign) {
		debug
		    ("Failed to resolve 'default_posix_memalign', returning NULL\n");
		return -ENOMEM;
	}

	/* We can now use the real_posix_memalign */
	return real_posix_memalign(memptr, alignment, size);
}

/*
 * We need to support malloc(0) as the glibc malloc function returns a 
 * valid pointer in such case.
 */
void *malloc(size_t size)
{
	void *ptr = NULL;
	struct alloc_header alloc_header;

	alloc_header.requested_size = size;
	alloc_header.allocated_size =
	    alloc_header.requested_size + sizeof(alloc_header);
#ifdef CHECK_COOKIE
	alloc_header.cookie = ALLOC_COOKIE;
#endif
	alloc_header.ptr = real_malloc(alloc_header.allocated_size);
	if (alloc_header.ptr) {
		*(struct alloc_header *)alloc_header.ptr = alloc_header;
		ptr = alloc_header.ptr + sizeof(alloc_header);
	}

	return ptr;
}

void *calloc(size_t nmemb, size_t size)
{
	void *ptr = NULL;
	struct alloc_header alloc_header;

	alloc_header.requested_size = (size * nmemb);
	alloc_header.allocated_size =
	    alloc_header.requested_size + sizeof(alloc_header);
#ifdef CHECK_COOKIE
	alloc_header.cookie = ALLOC_COOKIE;
#endif
	alloc_header.ptr = real_malloc(alloc_header.allocated_size);
	if (alloc_header.ptr) {
		*(struct alloc_header *)alloc_header.ptr = alloc_header;
		ptr = alloc_header.ptr + sizeof(alloc_header);
		memset(ptr, 0, alloc_header.requested_size);
	}

	return ptr;
}

void free(void *ptr)
{
	if (ptr) {
		struct alloc_header *store_ptr = (struct alloc_header *)ptr;
		store_ptr--;

#ifdef CHECK_COOKIE
		if (store_ptr->cookie != ALLOC_COOKIE) {
			fprintf(stderr, "%s: Invalid pointer\n", __func__);
			return;
		}
#endif
		memset(store_ptr->ptr, 0, store_ptr->allocated_size);
		real_free(store_ptr->ptr);
	}
}

void *realloc(void *ptr, size_t size)
{
	void *new_ptr = malloc(size);

	if (ptr) {
		if (new_ptr) {
			struct alloc_header *store_ptr =
			    (struct alloc_header *)ptr;

			store_ptr--;

			memcpy(new_ptr, ptr,
			       MIN(size, store_ptr->requested_size));
		}

		free(ptr);
	}

	return new_ptr;
}

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
	int rc = 0;

	if (!alignment || !memptr) {
		rc = -EINVAL;
	} else {
		struct alloc_header alloc_header;
		size_t added_space;

		*memptr = NULL;

		added_space =
		    (sizeof(alloc_header) / alignment) +
		    ((sizeof(alloc_header) % alignment) ? 1 : 0);
		added_space *= alignment;

		alloc_header.requested_size = size;
		alloc_header.allocated_size =
		    alloc_header.requested_size + added_space;
#ifdef CHECK_COOKIE
		alloc_header.cookie = ALLOC_COOKIE;
#endif
		alloc_header.ptr = NULL;
		rc = real_posix_memalign(&alloc_header.ptr, alignment,
					 alloc_header.allocated_size);

		if (!rc && alloc_header.ptr) {
			struct alloc_header *store_ptr;
			*memptr = alloc_header.ptr + added_space;
			store_ptr = (struct alloc_header *)*memptr;
			store_ptr--;
			*store_ptr = alloc_header;
		}
	}

	return rc;
}

void *memalign(size_t boundary, size_t size)
{
	void *ptr = NULL;

	posix_memalign(&ptr, boundary, size);

	return ptr;
}

void *valloc(size_t size)
{
	return memalign(getpagesize(), size);
}
