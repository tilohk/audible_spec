#pragma once

namespace its::test
{
// Counts every operator new on the current thread while in scope.
// (The test binary replaces the global operator new/delete; see AllocationCounter.cpp.)
// Does not see direct malloc() calls; the RT-safety lint covers those statically.
class ScopedAllocationCounter
{
public:
    ScopedAllocationCounter();
    ~ScopedAllocationCounter();
    long count() const;

private:
    long startCount;
};
} // namespace its::test
