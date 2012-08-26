clean_malloc
============

The purpose of this library is to make sure that any deallocated
memory (through free()) is set to 0 before being given back to the
system.
This allow to avoid that application data are still used (through
invalid pointer) after they have been released.
It also allow to prevent that deallocated data is found in newly
allocated memory blocks.

This library redefines the following functions:
 - malloc
 - calloc
 - realloc
 - valloc (deprecated)
 - memalign (deprecated)
 - posix_memalign
 - free

In turn these functions will use the following functions from glibc.
 - malloc
 - free
 - posix_memalign

Usage: LD_PRELOAD=./clean_malloc.so command args ...
