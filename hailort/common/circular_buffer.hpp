/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

#ifdef _WIN32
#define _CB_FETCH(x) (InterlockedOr((LONG volatile*)(&x), (LONG)0))
#define _CB_SET(x, value) (InterlockedExchange((LONG volatile*)(&x), (LONG)(value)))
#else
#define _CB_FETCH(x) (__sync_fetch_and_or(const_cast<volatile int*>(&(x)), 0))
#define _CB_SET(x, value) ((void)__sync_lock_test_and_set(&(x), value))
#endif

// Note: We use tag dispatching to select the right implementation for power of 2 size
//       There's a minor performance gain for power of 2 size, as we can use a mask instead of modulo
//       * If a CircularBuffer/Array with the IsPow2Tag, then the size must be a power of 2.
//       * If a CircularBuffer/Array with the IsNotPow2Tag, then the size may be any positive integer (we simply won't
//         use the mask optimization for modulo operation, even if the size is a power of 2).
struct IsPow2Tag {};
struct IsNotPow2Tag {};
template <typename Pow2Tag>
struct CircularBuffer
{
public:
    CircularBuffer(int s) :
        m_head(0),
        m_tail(0),
        m_size(s),
        m_size_mask(s - 1)
    {
        check_size(s, Pow2Tag());
    }

    void reset()
    {
        m_head = 0;
        m_tail = 0;
    }

    void enqueue(int value)
    {
        _CB_SET(m_head, modulo(m_head + value, Pow2Tag()));
    }

    void set_head(int value)
    {
        _CB_SET(m_head, value);
    }

    void dequeue(int value)
    {
        _CB_SET(m_tail, modulo(m_tail + value, Pow2Tag()));
    }

    void set_tail(int value)
    {
        _CB_SET(m_tail, value);
    }

    int avail(int head, int tail) const
    {
        return modulo(m_size - 1 + tail - head, Pow2Tag());
    }

    int prog(int head, int tail) const
    {
        return modulo(m_size + head - tail, Pow2Tag());
    }

    int head() const
    {
        return _CB_FETCH(m_head);
    }

    int tail() const
    {
        return _CB_FETCH(m_tail);
    }

    int size() const
    {
        return m_size;
    }

    int size_mask() const
    {
        return m_size_mask;
    }

private:
    int modulo(int val, IsPow2Tag) const
    {
        return val & m_size_mask;
    }

    int modulo(int val, IsNotPow2Tag) const
    {
        return val % m_size;
    }

    void check_size(size_t size, IsPow2Tag)
    {
        assert(0 != size);
        assert(is_powerof2(size));

        (void)size; // For release
    }

    void check_size(size_t size, IsNotPow2Tag)
    {
        assert(0 != size);

        (void)size; // For release
    }

    volatile int m_head;
    volatile int m_tail;
    const int m_size;
    // For power of 2 size, we can use a mask instead of modulo
    const int m_size_mask;
};


template<typename T>
struct is_std_array : public std::false_type {};

template<typename T, std::size_t N>
struct is_std_array<std::array<T, N>> : public std::true_type {};

// TODO: implement more functionalities, better move semantic handle
// TODO: support consts methods (front(), empty()), right now CB_* macros requires non const pointer to head+tail
template<typename T, typename Pow2Tag = IsPow2Tag, typename Container = std::vector<T>>
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
        iterator& operator++() { increment(Pow2Tag()); return *this; }
        iterator operator++(int) { iterator retval = *this; ++(*this); return retval; }
        bool operator==(iterator other) const { return m_index == other.m_index; }
        bool operator!=(iterator other) const { return !(*this == other); }
        T &operator*() const { return m_array.m_array[m_index]; }

    private:
        void increment(IsPow2Tag) { m_index = (m_index + 1) & m_array.m_circ.size_mask(); }
        void increment(IsNotPow2Tag) { m_index = (m_index + 1) % m_array.m_circ.size(); }

        CircularArray &m_array;
        int m_index;
    };

    // Ctor for Container=std::vector
    template <typename C=Container,
        class = typename std::enable_if_t<std::is_same<C, std::vector<T>>::value>>
    CircularArray(size_t storage_size) :
        m_circ(static_cast<int>(storage_size))
    {
        m_array.resize(storage_size);
    }

    // Ctor for Container=std::array
    template <typename C=Container,
        class = typename std::enable_if_t<is_std_array<C>::value>>
    CircularArray(size_t storage_size, int = 0) :
        m_circ(static_cast<int>(storage_size))
    {
        assert(storage_size <= std::tuple_size<C>::value);
    }

    void push_back(T &&element)
    {
        assert(!full());
        m_array[m_circ.head()] = std::move(element);
        m_circ.enqueue(1);
    }

    void push_back(const T& element)
    {
        assert(!full());
        m_array[m_circ.head()] = element;
        m_circ.enqueue(1);
    }

    void pop_front()
    {
        assert(!empty());
        // Clear previous front
        m_array[m_circ.tail()] = T();
        m_circ.dequeue(1);
    }

    T &front()
    {
        assert(!empty());
        return m_array[m_circ.tail()];
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

    bool empty() const
    {
        return m_circ.head() == m_circ.tail();
    }

    bool full() const
    {
        return 0 == m_circ.avail(m_circ.head(), m_circ.tail());
    }

    size_t size() const
    {
        return m_circ.prog(m_circ.head(), m_circ.tail());
    }

    size_t capacity() const
    {
        return m_circ.size() - 1;
    }

    iterator begin()
    {
        return iterator(m_circ.tail(), *this);
    }

    iterator end()
    {
        return iterator(m_circ.head(), *this);
    }

private:
    CircularBuffer<Pow2Tag> m_circ;
    Container m_array;
};

} /* namespace hailort */

#endif /* __CIRCULAR_BUFFER_HEADER__ */
