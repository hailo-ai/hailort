/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file circular_buffer.hpp
 * @brief Manages a Single-Producer Single-Consumer queue. The size of the queue must be a power of 2.
 *  This file exports both low level C struct, and a C++ wrapper.
 *
 **/

#ifndef __CIRCULAR_BUFFER_HEADER__
#define __CIRCULAR_BUFFER_HEADER__

#include "hailo/platform.h"
#include "common/utils.hpp"
#include <array>
#include <iterator>

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


template<typename T>
struct is_std_array : public std::false_type {};

template<typename T, std::size_t N>
struct is_std_array<std::array<T, N>> : public std::true_type {};

// TODO: implement more functionalities, better move semantic handle
// TODO: support consts methods (front(), empty()), right now CB_* macros requires non const pointer to head+tail
template<typename T, typename Container = std::vector<T>>
class CircularArray final
{
public:

    static_assert(std::is_default_constructible<T>::value, "CircularArray object must be default constructible");

    // Based on https://en.cppreference.com/w/cpp/iterator/iterator
    class iterator: public std::iterator<std::input_iterator_tag,   // iterator_category
                                         T,                         // value_type
                                         int,                       // difference_type
                                         int,                       // pointer
                                         T&>                        // reference
    {
    public:
        explicit iterator(int index, CircularArray &array) : m_array(array), m_index(index) {}
        iterator& operator++() {  m_index = ((m_index + 1) & m_array.m_circ.size_mask); return *this; }
        iterator operator++(int) { iterator retval = *this; ++(*this); return retval; }
        bool operator==(iterator other) const { return m_index == other.m_index; }
        bool operator!=(iterator other) const { return !(*this == other); }
        T &operator*() const { return m_array.m_array[m_index]; }
    private:
        CircularArray &m_array;
        int m_index;
    };

    // Ctor for Container=std::vector
    template <typename C=Container,
        class = typename std::enable_if_t<std::is_same<C, std::vector<T>>::value>>
    CircularArray(size_t storage_size)
    {
        // storage size must be a power of 2
        assert(is_powerof2(storage_size));
        CB_INIT(m_circ, storage_size);
        m_array.resize(storage_size);
    }

    // Ctor for Container=std::array
    template <typename C=Container,
        class = typename std::enable_if_t<is_std_array<C>::value>>
    CircularArray(size_t storage_size, int = 0)
    {
        // storage size must be a power of 2
        assert(is_powerof2(storage_size));
        assert(storage_size <= std::tuple_size<C>::value);
        CB_INIT(m_circ, storage_size);
    }

    void push_back(T &&element)
    {
        assert(!full());
        m_array[CB_HEAD(m_circ)] = std::move(element);
        CB_ENQUEUE(m_circ, 1);
    }

    void push_back(const T& element)
    {
        assert(!full());
        m_array[CB_HEAD(m_circ)] = element;
        CB_ENQUEUE(m_circ, 1);
    }

    void pop_front()
    {
        assert(!empty());
        // Clear previous front
        m_array[CB_TAIL(m_circ)] = T();
        CB_DEQUEUE(m_circ, 1);
    }

    T &front()
    {
        assert(!empty());
        return m_array[CB_TAIL(m_circ)];
    }

    void reset()
    {
        // pop all fronts to make sure all destructors are called.
        // TODO: if T is std::is_trivial, we can just reset the counters
        const auto original_size = size();
        for (size_t i = 0 ; i < original_size; i++) {
            pop_front();
        }
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

    size_t capacity()
    {
        return CB_SIZE(m_circ) - 1;
    }

    iterator begin()
    {
        return iterator(CB_TAIL(m_circ), *this);
    }

    iterator end()
    {
        return iterator(CB_HEAD(m_circ), *this);
    }

private:
    circbuf_t m_circ;
    Container m_array;
};

} /* namespace hailort */

#endif /* __CIRCULAR_BUFFER_HEADER__ */
