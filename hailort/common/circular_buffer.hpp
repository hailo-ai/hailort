/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file circular_buffer.hpp
 * @brief
 *
 **/

#ifndef __CIRCULAR_BUFFER_HEADER__
#define __CIRCULAR_BUFFER_HEADER__

#include "hailo/platform.h"
#include "common/utils.hpp"
#include <array>

namespace hailort
{

typedef struct {
    volatile int head;
    volatile int tail;
    int size;
    int size_mask;
} circbuf_t;

//TODO: Do not change the behavior of this module. see PLDA descs impl..
//TODO: optimize macros
#ifndef MIN
#define MIN(x,y) (((x) < (y)) ? (x) : (y))
#endif
#ifdef _WIN32
#define _CB_FETCH(x) (InterlockedOr((LONG volatile*)(&x), (LONG)0))
#define _CB_SET(x, value) (InterlockedExchange((LONG volatile*)(&x), (LONG)(value)))
#else
#define _CB_FETCH(x) (__sync_fetch_and_or(&(x), 0))
#define _CB_SET(x, value) ((void)__sync_lock_test_and_set(&(x), value))
#endif

#define CB_INIT(circbuf, s)                         \
    (circbuf).head = 0;                             \
    (circbuf).tail = 0;                             \
    (circbuf).size = static_cast<int>(s);           \
    (circbuf).size_mask = static_cast<int>((s) - 1)
#define CB_RESET(circbuf)           \
    (circbuf).head = 0;             \
    (circbuf).tail = 0            
#define CB_HEAD(x) _CB_FETCH((x).head)
#define CB_TAIL(x) _CB_FETCH((x).tail)
#define CB_SIZE(x) _CB_FETCH((x).size)
#define CB_ENQUEUE(circbuf, value) _CB_SET((circbuf).head, ((circbuf).head + (value)) & ((circbuf).size_mask))
#define CB_DEQUEUE(circbuf, value) _CB_SET((circbuf).tail, ((circbuf).tail + (value)) & ((circbuf).size_mask))
#define CB_AVAIL(circbuf, head, tail) ((((circbuf).size)-1+(tail)-(head)) & ((circbuf).size_mask))
#define CB_AVAIL_CONT(circbuf, head, tail) \
    MIN(CB_AVAIL((circbuf), (head), (tail)), (circbuf).size - (head))
#define CB_PROG(circbuf, head, tail) ((((circbuf).size)+(head)-(tail)) & ((circbuf).size_mask))
#define CB_PROG_CONT(circbuf, head, tail) \
    MIN(CB_PROG((circbuf), (head), (tail)), (circbuf).size - (tail))


// TODO: implement more functionalities, better move semantic handle
// TODO: support consts methods (front(), empty()), right now CB_* macros requires non const pointer to head+tail
template<typename T>
class CircularArray final
{
public:
    static_assert(std::is_pod<T>::value, "CircularArray can be used only with POD type");

    CircularArray(size_t storage_size)
    {
        // storage size must be a power of 2
        assert(is_powerof2(storage_size));
        CB_INIT(m_circ, storage_size);
        m_array.resize(storage_size);
    }

    void push_back(const T& element)
    {
        // assert(CB_AVAIL(m_circ, CB_HEAD(m_circ), CB_TAIL(m_circ)));
        m_array[CB_HEAD(m_circ)] = element;
        CB_ENQUEUE(m_circ, 1);
    }

    void pop_front()
    {
        CB_DEQUEUE(m_circ, 1);
    }

    T &front()
    {
        return m_array[CB_TAIL(m_circ)];
    }

    bool empty()
    {
        return CB_HEAD(m_circ) == CB_TAIL(m_circ);
    }

    bool full()
    {
        return 0 == CB_AVAIL(m_circ, CB_HEAD(m_circ), CB_TAIL(m_circ));
    }

    size_t size()
    {
        return CB_PROG(m_circ, CB_HEAD(m_circ), CB_TAIL(m_circ));
    }

private:
    circbuf_t m_circ;
    std::vector<T> m_array;
};

} /* namespace hailort */

#endif /* __CIRCULAR_BUFFER_HEADER__ */
