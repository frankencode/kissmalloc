#include <new>
#include <kissmalloc.h>

void *operator new(std::size_t size)
{
    void *p = KISSMALLOC_NAME(malloc)(size);
    if (!p) throw std::bad_alloc{};
    return p;
}

void *operator new[](std::size_t size)
{
    void *p = KISSMALLOC_NAME(malloc)(size);
    if (!p) throw std::bad_alloc{};
    return p;
}

void operator delete(void *data) noexcept
{
    return KISSMALLOC_NAME(free)(data);
}

void operator delete[](void *data) noexcept
{
    return KISSMALLOC_NAME(free)(data);
}

void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    return KISSMALLOC_NAME(malloc)(size);
}

void *operator new[](std::size_t size, const std::nothrow_t &) noexcept
{
    return KISSMALLOC_NAME(malloc)(size);
}

void operator delete(void *data, const std::nothrow_t &) noexcept
{
    return KISSMALLOC_NAME(free)(data);
}

void operator delete[](void *data, const std::nothrow_t &) noexcept
{
    return KISSMALLOC_NAME(free)(data);
}
