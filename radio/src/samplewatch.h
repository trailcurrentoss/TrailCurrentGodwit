#pragma once

#include <atomic>
#include <chrono>

// Notices when the tuner's samples stop. If the dongle goes away just as a program
// starts reading, rtlsdr_read_async can wait forever on a device that no longer
// exists, and the program would sit there holding it. It streams continuously once
// started, so a few seconds of nothing means it is gone.
class SampleWatch
{
public:
    // From the sample callback
    void heard() { m_count.fetch_add(1, std::memory_order_relaxed); }

    // From the main loop
    bool stalled()
    {
        const Clock::time_point now = Clock::now();
        const unsigned count = m_count.load(std::memory_order_relaxed);
        if (m_last == Clock::time_point {} || count != m_seen) {
            m_seen = count;
            m_last = now;
            return false;
        }
        return now - m_last > std::chrono::seconds(3);
    }

private:
    using Clock = std::chrono::steady_clock;
    std::atomic<unsigned> m_count { 0 };
    unsigned m_seen = 0;
    Clock::time_point m_last {};
};
