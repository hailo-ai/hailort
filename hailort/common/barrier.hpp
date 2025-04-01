/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file barrier.hpp
 **/

#ifndef _BARRIER_HPP_
#define _BARRIER_HPP_

#include <mutex>
#include <condition_variable>
#include <atomic>

namespace hailort
{

class Barrier;
using BarrierPtr = std::shared_ptr<Barrier>;

/**
 * A barrier is a synchronization object that allows an expected number of threads to block until all of them
 * arrive at the barrier.
 *
 * This class should be similar to std::barrier that will be released in c++20 (If we use c++20, please delete this class)
 */
class Barrier final {
public:
    explicit Barrier(size_t count);

    Barrier(const Barrier &) = delete;
    Barrier& operator=(const Barrier &) = delete;
    Barrier(Barrier &&) = delete;
    Barrier& operator=(Barrier &&) = delete;

    /**
     * Decreases the count by 1 and blocks until all threads arrives.
     */
    void arrive_and_wait();

    /**
     * Signal all blocking occurrences, and further calls to 'arrive_and_wait()' will return immediately
     */
    void terminate();

private:
    const size_t m_original_count;
    size_t m_count;
    size_t m_generation;

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic_bool m_is_activated;
};

} /* namespace hailort */

#endif /* _BARRIER_HPP_ */
