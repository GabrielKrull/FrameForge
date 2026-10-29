#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>

namespace frameforge {

// ============================================================
// Thread-safe bounded queue with an explicit drop-oldest
// policy.
//
// The producer never blocks. When the queue is full the oldest
// element is discarded to make room for the newest one, so the
// consumer always works with the freshest data. Discards are
// counted separately from accepted elements so callers can
// distinguish "received" from "dropped".
//
// This class is intentionally free of Windows dependencies so
// it can be unit tested on any platform.
// ============================================================
template <typename T>
class BoundedQueue {
public:
    enum class PushResult {
        Pushed,       // stored, no element discarded
        ReplacedOldest, // stored, oldest element discarded
        Rejected,     // queue is closed; element not stored
    };

    explicit BoundedQueue(std::size_t capacity)
        : m_capacity(capacity > 0 ? capacity : 1) {}

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    // Push an element. Never blocks. Returns what happened.
    PushResult Push(T value) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_closed) {
                ++m_rejected;
                return PushResult::Rejected;
            }

            PushResult result = PushResult::Pushed;
            if (m_items.size() >= m_capacity) {
                m_items.pop_front();
                ++m_dropped;
                result = PushResult::ReplacedOldest;
            }

            m_items.push_back(std::move(value));
            ++m_pushed;
            m_notEmpty.notify_one();
            return result;

        }
    }

    // Non-blocking pop. Returns false when empty.
    bool TryPop(T& out) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_items.empty()) {
            return false;
        }
        out = std::move(m_items.front());
        m_items.pop_front();
        ++m_popped;
        return true;
    }

    // Wait for an element at most 'timeout'. Returns false on
    // timeout or when the queue is closed and drained.
    bool WaitPopFor(T& out, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_notEmpty.wait_for(lock, timeout, [this] {
            return !m_items.empty() || m_closed;
        });

        if (m_items.empty()) {
            return false;
        }
        out = std::move(m_items.front());
        m_items.pop_front();
        ++m_popped;
        return true;
    }

    // Discard every queued element (counted as dropped).
    void Clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_dropped += m_items.size();
        m_items.clear();
    }

    // Stop accepting new elements and wake up waiting consumers.
    // Already queued elements can still be popped.
    void Close() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_closed = true;
        }
        m_notEmpty.notify_all();
    }

    bool IsClosed() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_closed;
    }

    std::size_t Size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_items.size();
    }

    std::size_t Capacity() const { return m_capacity; }

    // Counters (monotonic since construction / last ResetStats).
    uint64_t TotalPushed() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pushed;
    }

    uint64_t TotalDropped() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_dropped;
    }

    uint64_t TotalPopped() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_popped;
    }

    uint64_t TotalRejected() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_rejected;
    }

    void ResetStats() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pushed = 0;
        m_dropped = 0;
        m_popped = 0;
        m_rejected = 0;
    }

private:
    const std::size_t m_capacity;

    mutable std::mutex      m_mutex;
    std::condition_variable m_notEmpty;
    std::deque<T>           m_items;
    bool                    m_closed = false;

    uint64_t m_pushed   = 0;
    uint64_t m_dropped  = 0;
    uint64_t m_popped   = 0;
    uint64_t m_rejected = 0;
};

} // namespace frameforge
