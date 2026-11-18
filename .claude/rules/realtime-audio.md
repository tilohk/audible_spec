---
paths:
  - "plugin/Source/**"
---

# Real-time audio rules

Code that runs on the **audio thread** (`processBlock`, `renderNextBlock`, anything they call) must never block or take an unbounded amount of time.

## Mark audio-thread code
Wrap every audio-thread function body (or group of functions) in markers:

```cpp
// RT-SAFE BEGIN
void SamplerVoice::renderNextBlock (juce::AudioBuffer<float>& out, int start, int num) { ... }
// RT-SAFE END
```

The hook `.claude/hooks/rt-lint.sh` scans these regions and rejects the edit when it finds a forbidden pattern. A false positive can be silenced on that one line with `// rt-ok: <reason>`. Use this rarely, and always give the reason.

## Forbidden inside RT-SAFE regions
- Allocation: `new`, `delete`, `malloc`, `std::make_unique/shared`, `push_back`, `emplace_back`, `resize`, `reserve`, `juce::String` building, `AudioBuffer::setSize` without `avoidReallocating`.
- Locks and waits: `std::mutex`, `lock_guard`, `unique_lock`, `ScopedLock`, `CriticalSection`, `sleep`, `wait`, `MessageManagerLock`.
- I/O and logging: `File`, `std::cout`, `printf`, `DBG`, `Logger`.
- Exceptions (`throw`) and anything that may throw.

## Allowed patterns
- Pre-allocate in `prepareToPlay` or in the constructor.
- Read parameters through cached `std::atomic<float>*` from `APVTS::getRawParameterValue`.
- Hand rendered buffers over with a lock-free swap (atomic `shared_ptr` load, or a single-producer/single-consumer FIFO). Release the old buffer **off** the audio thread.
- `juce::SmoothedValue` for parameters that change audibly (gain, filter cutoff).
