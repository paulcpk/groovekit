#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace gk::sampler
{

/** Hands immutable settings from the message thread to the audio thread without a lock.

    The message thread publishes a shared_ptr; the audio thread takes the newest published one at
    the start of a block and, when it has moved past settings it was using, releases them. Nothing
    is freed until the audio thread has released it (or it was superseded before the audio thread
    ever saw it), so the audio thread never skips a block, waits, or frees memory.

    One publisher thread and one consumer thread.
*/
template <typename T>
class Handoff
{
public:
    /** Message thread: makes these the next settings the audio thread takes. */
    void publish (std::shared_ptr<const T> next)
    {
        if (next == nullptr)
            return;

        auto* raw = next.get();
        owned.push_back (std::move (next));

        // Settings the audio thread hadn't taken yet are superseded: it never saw them.
        if (auto* superseded = pending.exchange (raw, std::memory_order_acq_rel))
            forget (superseded);

        collect();
    }

    /** Message thread: the settings published last. */
    std::shared_ptr<const T> newest() const
    {
        return owned.empty() ? nullptr : owned.back();
    }

    /** Message thread: frees what the audio thread has released. Publishing does this too. */
    void collect()
    {
        auto r = readIndex.load (std::memory_order_relaxed);

        while (r != writeIndex.load (std::memory_order_acquire))
        {
            forget (released[r]);
            r = (r + 1) % capacity;
            readIndex.store (r, std::memory_order_release);
        }
    }

    /** Audio thread: newly published settings, once, or null. The caller adopts them. */
    const T* take()
    {
        return pending.exchange (nullptr, std::memory_order_acq_rel);
    }

    /** Audio thread: these settings won't be read again; the message thread may free them. */
    void release (const T* done)
    {
        if (done == nullptr)
            return;

        const auto w = writeIndex.load (std::memory_order_relaxed);
        const auto next = (w + 1) % capacity;

        // Full only if settings were published faster than collected for a long time; then the
        // memory is kept until the handoff goes, rather than freed on this thread.
        if (next == readIndex.load (std::memory_order_acquire))
            return;

        released[w] = done;
        writeIndex.store (next, std::memory_order_release);
    }

private:
    static constexpr int capacity = 256;

    std::vector<std::shared_ptr<const T>> owned;        // message thread only
    std::atomic<const T*> pending { nullptr };
    std::array<const T*, capacity> released {};
    std::atomic<int> readIndex { 0 }, writeIndex { 0 };

    void forget (const T* p)
    {
        for (auto it = owned.begin(); it != owned.end(); ++it)
        {
            if (it->get() == p)
            {
                owned.erase (it);
                return;
            }
        }
    }
};

} // namespace gk::sampler
