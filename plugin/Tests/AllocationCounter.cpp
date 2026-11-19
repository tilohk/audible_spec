#include "AllocationCounter.h"

#include <algorithm>
#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace
{
thread_local bool countingOnThisThread = false;
thread_local long allocationsOnThisThread = 0;

void* allocate (std::size_t size)
{
    if (countingOnThisThread)
        ++allocationsOnThisThread;
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}

void* allocateAligned (std::size_t size, std::align_val_t alignment)
{
    if (countingOnThisThread)
        ++allocationsOnThisThread;
    void* p = nullptr;
    const auto a = std::max ((std::size_t)alignment, sizeof (void*));
#if defined(_WIN32)
    p = _aligned_malloc (size == 0 ? 1 : size, a); // must be freed with _aligned_free
    if (p == nullptr)
        throw std::bad_alloc();
#else
    if (posix_memalign (&p, a, size == 0 ? 1 : size) != 0)
        throw std::bad_alloc();
#endif
    return p;
}

void freeAligned (void* p) noexcept
{
#if defined(_WIN32)
    _aligned_free (p);
#else
    std::free (p);
#endif
}
} // namespace

namespace its::test
{
ScopedAllocationCounter::ScopedAllocationCounter() : startCount (allocationsOnThisThread)
{
    countingOnThisThread = true;
}

ScopedAllocationCounter::~ScopedAllocationCounter()
{
    countingOnThisThread = false;
}

long ScopedAllocationCounter::count() const
{
    return allocationsOnThisThread - startCount;
}
} // namespace its::test

// Global replacements (test binary only)
void* operator new (std::size_t size)
{
    return allocate (size);
}
void* operator new[] (std::size_t size)
{
    return allocate (size);
}
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    try
    {
        return allocate (size);
    }
    catch (...)
    {
        return nullptr;
    }
}
void* operator new[] (std::size_t size, const std::nothrow_t& t) noexcept
{
    return operator new (size, t);
}
void* operator new (std::size_t size, std::align_val_t a)
{
    return allocateAligned (size, a);
}
void* operator new[] (std::size_t size, std::align_val_t a)
{
    return allocateAligned (size, a);
}

void operator delete (void* p) noexcept
{
    std::free (p);
}
void operator delete[] (void* p) noexcept
{
    std::free (p);
}
void operator delete (void* p, std::size_t) noexcept
{
    std::free (p);
}
void operator delete[] (void* p, std::size_t) noexcept
{
    std::free (p);
}
void operator delete (void* p, std::align_val_t) noexcept
{
    freeAligned (p);
}
void operator delete[] (void* p, std::align_val_t) noexcept
{
    freeAligned (p);
}
void operator delete (void* p, std::size_t, std::align_val_t) noexcept
{
    freeAligned (p);
}
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept
{
    freeAligned (p);
}
