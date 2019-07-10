/*
 * Copyright (C) 2019 Frank Mertens.
 *
 * Distribution and use is allowed under the terms of the zlib license
 * (see kiss/LICENSE).
 *
 */

#pragma once

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t number, size_t size);
void *realloc(void *ptr, size_t size);
int posix_memalign(void **ptr, size_t alignment, size_t size);

void *aligned_alloc(size_t alignment, size_t size);
void *memalign(size_t alignment, size_t size);
void *valloc(size_t size);
void *pvalloc(size_t size);

#ifdef __cplusplus
} // extern "C"
#endif