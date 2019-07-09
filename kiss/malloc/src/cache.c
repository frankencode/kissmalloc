/*
 * Copyright (C) 2019 Frank Mertens.
 *
 * Distribution and use is allowed under the terms of the zlib license
 * (see kiss/LICENSE).
 *
 */

typedef struct {
    int fill;
    void *buffer[KISSMALLOC_PAGE_CACHE];
} cache_t;

static_assert(sizeof(cache_t) <= KISSMALLOC_PAGE_SIZE, "KISSMALLOC_PAGE_CACHE exceeds page size");

static pthread_once_t cache_init_control;
static pthread_key_t cache_key;

inline static void cache_xchg(void **buffer, int i, int j)
{
    void *h = buffer[i];
    buffer[i] = buffer[j];
    buffer[j] = h;
}

inline static int cache_parent(int i)
{
    return (i - 1) >> 1;
}

inline static int cache_child_left(int i)
{
    return (i << 1) + 1;
}

inline static int cache_child_right(int i)
{
    return (i << 1) + 2;
}

inline static int cache_min(void **buffer, int i, int j, int k)
{
    int m = i;

    if (buffer[j] < buffer[k]) {
        if (buffer[j] < buffer[i])
            m = j;
    }
    else if (buffer[k] < buffer[i])
        m = k;

    return m;
}

inline static void cache_bubble_up(cache_t *cache)
{
    void **buffer = cache->buffer;
    for (int i = cache->fill - 1; i > 0;) {
        int j = cache_parent(i);
        if (buffer[i] >= buffer[j]) break;
        cache_xchg(buffer, i, j);
        i = j;
    }
}

inline static void cache_bubble_down(cache_t *cache)
{
    const int fill = cache->fill;
    void **buffer = cache->buffer;
    if (fill == 0) return;
    for (int i = 0; 1;) {
        int lc = cache_child_left(i);
        int rc = cache_child_right(i);
        if (rc < fill) {
            int j = cache_min(buffer, i, lc, rc);
            if (j == i) break;
            cache_xchg(buffer, i, j);
            i = j;
        }
        else if (lc < fill) {
            if (buffer[lc] < buffer[i])
                cache_xchg(buffer, i, lc);
            break;
        }
        else
            break;
    }
}

inline static void *cache_pop(cache_t *cache)
{
    void *page = cache->buffer[0];
    --cache->fill;
    cache->buffer[0] = cache->buffer[cache->fill];
    cache_bubble_down(cache);
    return page;
}

static void cache_reduce(cache_t *cache, int fill_max, size_t page_size)
{
    if (cache->fill <= fill_max) return;

    void *chunk = cache_pop(cache);
    size_t size = page_size;
    while (cache->fill > fill_max) {
        void *chunk2 = cache_pop(cache);
        if ((uint8_t *)chunk2 - (uint8_t *)chunk == (ssize_t)size) {
            size += page_size;
        }
        else {
            if (munmap(chunk, size) == -1) abort();
            chunk = chunk2;
            size = page_size;
        }
    }
    if (size > 0) {
        if (munmap(chunk, size) == -1) abort();
    }
}

static void cache_cleanup()
{
    cache_t *cache = (cache_t *)pthread_getspecific(cache_key);
    cache_reduce(cache, 0, KISSMALLOC_PAGE_SIZE);
    if (munmap(cache, KISSMALLOC_PAGE_SIZE) == -1) abort();
}

static void cache_init()
{
    if (pthread_key_create(&cache_key, cache_cleanup) != 0) abort();
    cache_t *cache = (cache_t *)mmap(NULL, KISSMALLOC_PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE|MAP_NORESERVE|MAP_POPULATE, -1, 0);
    if (cache == MAP_FAILED) abort();
    pthread_setspecific(cache_key, cache);
}

static void cache_push(void *page, size_t page_size)
{
    pthread_once(&cache_init_control, cache_init);
    cache_t *cache = (cache_t *)pthread_getspecific(cache_key);

    if (cache->fill == KISSMALLOC_PAGE_CACHE)
        cache_reduce(cache, KISSMALLOC_PAGE_CACHE >> 1, page_size);

    cache->buffer[cache->fill] = page;
    ++cache->fill;
    cache_bubble_up(cache);
}
