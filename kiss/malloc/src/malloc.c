/*
 * Copyright (C) 2019 Frank Mertens.
 *
 * Distribution and use is allowed under the terms of the zlib license
 * (see kiss/LICENSE).
 *
 */

#include <kiss/malloc.h>

/// System memory granularity, e.g. XMMS movdqa requires 16
#define KISSMALLOC_GRANULARITY (2 * sizeof(size_t) < __alignof__ (long double) ? __alignof__ (long double) : 2 * sizeof(size_t))

/// Log2 of the system memory granularity
#define KISSMALLOC_GRANULARITY_SHIFT (__builtin_ctz(KISSMALLOC_GRANULARITY))

/// Number of pages to preallocate
#define KISSMALLOC_PAGE_PREALLOC 64

/// Number of freed pages to cache at maximum (should be N * KISSMALLOC_PAGE_PREALLOC - 1)
#define KISSMALLOC_PAGE_CACHE 255

/// Size of a memory page on this system
#define KISSMALLOC_PAGE_SIZE 4096

/// Half the size of a memory page
#define KISSMALLOC_PAGE_HALF_SIZE (KISSMALLOC_PAGE_SIZE / 2)

#define KISSMALLOC_PREALLOC_SIZE (KISSMALLOC_PAGE_PREALLOC * KISSMALLOC_PAGE_SIZE)

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <stdlib.h> // abort, getenv
#include <unistd.h> // sysconf
#include <string.h> // memcpy
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <assert.h>

// #include "trace.c"
#include "cache.c"

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#ifndef MAP_POPULATE
#define MAP_POPULATE 0
#endif

#ifndef MAP_NORESERVE
#define MAP_NORESERVE 0
#endif

#define KISSMALLOC_IS_POW2(x) (x > 0 && (x & (x - 1)) == 0)

static_assert(KISSMALLOC_IS_POW2(KISSMALLOC_GRANULARITY), "KISSMALLOC_GRANULARITY needs to be a power of two");
static_assert(KISSMALLOC_PAGE_SIZE <= 65536, "Page size above 64KiB is not supported");

#pragma pack(push,1)

typedef struct {
    uint16_t prealloc_count; // please keep at the start of the structure to ensure alignment
    uint16_t checksum;
    uint16_t bytes_dirty;
    uint16_t object_count;
    cache_t *cache;
} bucket_t;

#pragma pack(pop)

static_assert(sizeof(bucket_t) == 16, "The bucket header needs to be exactly 16 bytes");

static pthread_once_t bucket_init_control = PTHREAD_ONCE_INIT;
static pthread_key_t bucket_key;

inline static size_t round_up_pow2(const size_t x, const size_t g)
{
    const size_t m = g - 1;
    return (x + m) & ~m;
}

static void bucket_cleanup(void *arg)
{
    bucket_t *bucket = (bucket_t *)arg;

    if (bucket) {
        cache_cleanup(bucket->cache);

        void *head = bucket;
        size_t size = (bucket->prealloc_count + 1) * KISSMALLOC_PAGE_SIZE;

        if (__sync_sub_and_fetch(&bucket->object_count, 1)) {
            head = ((uint8_t *)head) + KISSMALLOC_PAGE_SIZE;
            size -= KISSMALLOC_PAGE_SIZE;
        }

        if (munmap(head, size) == -1) abort();
    }
}

void bucket_init()
{
    if (pthread_key_create(&bucket_key, bucket_cleanup) != 0) abort();
}

void *malloc(size_t size)
{
    pthread_once(&bucket_init_control, bucket_init);
    bucket_t *bucket = (bucket_t *)pthread_getspecific(bucket_key);

    if (size < KISSMALLOC_PAGE_HALF_SIZE)
    {
        size = round_up_pow2(size, KISSMALLOC_GRANULARITY);

        uint32_t prealloc_count = 0;

        if (bucket)
        {
            if (size <= KISSMALLOC_PAGE_SIZE - bucket->bytes_dirty) {
                void *data = (uint8_t *)bucket + bucket->bytes_dirty;
                bucket->bytes_dirty += size;
                ++bucket->object_count; // this is atomic on all relevant processors!
                return data;
            }
            prealloc_count = bucket->prealloc_count;

            if (!__sync_sub_and_fetch(&bucket->object_count, 1))
                cache_push(bucket->cache, bucket);
        }

        void *page_start = NULL;
        if (prealloc_count > 0) {
            page_start = (uint8_t *)bucket + KISSMALLOC_PAGE_SIZE;
        }
        else {
            page_start = mmap(NULL, KISSMALLOC_PREALLOC_SIZE, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_NORESERVE|MAP_POPULATE, -1, 0);
            if (page_start == MAP_FAILED) {
                errno = ENOMEM;
                return NULL;
            }
            prealloc_count = KISSMALLOC_PAGE_PREALLOC;
        }
        --prealloc_count;

        cache_t *cache = bucket ? bucket->cache : cache_create();

        const size_t bucket_header_size = round_up_pow2(sizeof(bucket_t), KISSMALLOC_GRANULARITY);
        bucket = (bucket_t *)page_start;
        bucket->prealloc_count = prealloc_count;
        bucket->bytes_dirty = bucket_header_size + size;
        bucket->object_count = 2;
        bucket->cache = cache;
        pthread_setspecific(bucket_key, bucket);

        return (uint8_t *)page_start + bucket_header_size;
    }

    size = round_up_pow2(size, KISSMALLOC_PAGE_SIZE) + KISSMALLOC_PAGE_SIZE;

    void *head = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_NORESERVE|MAP_POPULATE, -1, 0);
    if (head == MAP_FAILED) {
        errno = ENOMEM;
        return NULL;
    }
    *(size_t *)head = size;
    return (uint8_t *)head + KISSMALLOC_PAGE_SIZE;
}

void free(void *ptr)
{
    const size_t page_offset = (size_t)(((uint8_t *)ptr - (uint8_t *)NULL) & (KISSMALLOC_PAGE_SIZE - 1));

    if (page_offset != 0) {
        void *page_start = (uint8_t *)ptr - page_offset;
        bucket_t *bucket = (bucket_t *)page_start;
        if (!__sync_sub_and_fetch(&bucket->object_count, 1)) {
            pthread_once(&bucket_init_control, bucket_init);
            bucket_t *my_bucket = (bucket_t *)pthread_getspecific(bucket_key);
            if (!my_bucket) {
                my_bucket = (bucket_t *)mmap(NULL, KISSMALLOC_PREALLOC_SIZE, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_NORESERVE|MAP_POPULATE, -1, 0);
                if (!my_bucket) abort();
                my_bucket->bytes_dirty = round_up_pow2(sizeof(bucket_t), KISSMALLOC_GRANULARITY);
                my_bucket->object_count = 1;
                my_bucket->cache = cache_create();
                pthread_setspecific(bucket_key, my_bucket);
            }
            cache_push(my_bucket->cache, bucket);
        }
    }
    else if (ptr != NULL) {
        void *head = (uint8_t *)ptr - KISSMALLOC_PAGE_SIZE;
        if (munmap(head, *(size_t *)head) == -1) abort();
    }
}

void *calloc(size_t number, size_t size)
{
    return malloc(number * size);
}

void *realloc(void *ptr, size_t size)
{
    if (ptr == NULL) return malloc(size);

    if (size == 0) {
        if (ptr != NULL) free(ptr);
        return NULL;
    }

    if (size <= KISSMALLOC_GRANULARITY) return ptr;

    size_t copy_size = KISSMALLOC_PAGE_SIZE;
    size_t page_offset = (size_t)((uint8_t *)ptr - (uint8_t *)NULL) & (KISSMALLOC_PAGE_SIZE - 1);

    if (page_offset > 0) {
        void *page_start = (uint8_t *)ptr - page_offset;
        bucket_t *bucket = (bucket_t *)page_start;
        const size_t size_estimate_1 = bucket->bytes_dirty - page_offset;
        const size_t size_estimate_2 = bucket->bytes_dirty - ((bucket->object_count - 1) << KISSMALLOC_GRANULARITY_SHIFT);
            // FIXME: might not work cleanly when reallocating in a different thread
        copy_size = (size_estimate_1 < size_estimate_2) ? size_estimate_1 : size_estimate_2;
    }

    if (copy_size > size) copy_size = size;

    void *new_ptr = malloc(size);
    if (new_ptr == NULL) return NULL;

    memcpy(new_ptr, ptr, copy_size);

    free(ptr);

    return new_ptr;
}

int posix_memalign(void **ptr, size_t alignment, size_t size)
{
    if (size == 0) {
        *ptr = NULL;
        return 0;
    }

    if (
        !KISSMALLOC_IS_POW2(alignment) ||
        (alignment & (sizeof(void *) - 1)) != 0
    )
        return EINVAL;

    if (alignment <= KISSMALLOC_GRANULARITY) {
        *ptr = malloc(size);
        return (*ptr != NULL) ? 0 : ENOMEM;
    }

    if (alignment + size < KISSMALLOC_PAGE_HALF_SIZE) {
        uint8_t *ptr_byte = malloc(alignment + size);
        if (ptr_byte != NULL) {
            size_t r = (size_t)(ptr_byte - (uint8_t *)NULL) & (alignment - 1);
            if (r > 0) ptr_byte += alignment - r;
            *ptr = ptr_byte;
            return 0;
        }
    }

    size += alignment + KISSMALLOC_PAGE_SIZE;

    void *head = mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_NORESERVE|MAP_POPULATE, -1, 0);
    if (head == MAP_FAILED) return ENOMEM;

    while (
        (
            (((uint8_t *)head - (uint8_t *)NULL) + KISSMALLOC_PAGE_SIZE)
            & (alignment - 1)
        ) != 0
    ) {
        if (munmap(head, KISSMALLOC_PAGE_SIZE) == -1) abort();
        head = (uint8_t *)head + KISSMALLOC_PAGE_SIZE;
        size -= KISSMALLOC_PAGE_SIZE;
    }

    *(size_t *)head = size;
    *ptr = (uint8_t *)head + KISSMALLOC_PAGE_SIZE;
    return 0;
}

void *aligned_alloc(size_t alignment, size_t size)
{
    void *ptr = NULL;
    posix_memalign(&ptr, alignment, size);
    return ptr;
}

void *memalign(size_t alignment, size_t size)
{
    void *ptr = NULL;
    posix_memalign(&ptr, alignment, size);
    return ptr;
}

void *valloc(size_t size)
{
    return malloc(round_up_pow2(size, KISSMALLOC_PAGE_SIZE));
}

void *pvalloc(size_t size)
{
    return malloc(round_up_pow2(size, KISSMALLOC_PAGE_SIZE));
}
