/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file expected.hpp
 * @brief Expected<T> is either a T or the ::hailo_status preventing T to be created.
 *
 * Examples:
 * 
 * *** Example #1 - Construct a new object ***
 * 
 * 
 * class Widget {
 * private:
 *     Widget(float f, hailo_status &status)
 *     {
 *         status = (f == 3.14f) ? HAILO_SUCCESS : HAILO_INVALID_ARGUMENT;
 *     }
 * 
 * public:
 *     static Expected<Widget> create(float f)
 *     {
 *         hailo_status status = HAILO_UNINITIALIZED;
 *         Widget object(f, status);
 *         if (HAILO_SUCCESS != status) {
 *             LOGGER__ERROR("Failed creating Widget");
 *             return make_unexpected(status);
 *         }
 *         return std::move(object);
 *     }
 * };
 * 
 * Note that the constructor must be private since we are using Widget::Create to construct a new object safely.
 * One can construct and use Widget using the following code:
 * 
 * static hailo_status construct_widget()
 * {
 *     auto widget = Widget::create(3.14f);
 *     if (!widget) {
 *         return widget.status();
 *     }
 *     // Use widget ...
 *     return HAILO_SUCCESS;
 * }
 * 
 * 
 * *** Example #2 - Construct a new object with copy elision  ***
 * 
 * If you have concrete evidence that the move constructor causes performance issues, you can use the following code
 * to construct a new object with copy elision (if supported by the compiler):
 * 
 * 
 * class Widget {
 * public:
 *     Widget(ExpectedKey, float f, hailo_status &status)
 *     {
 *         status = (f == 3.14f) ? HAILO_SUCCESS : HAILO_INVALID_ARGUMENT;
 *     }
 * 
 *     static Expected<Widget> create(float f)
 *     {
 *         hailo_status status = HAILO_UNINITIALIZED;
 *         Expected<Widget> expected_object(f, status);
 *         if (HAILO_SUCCESS != status) {
 *             LOGGER__ERROR("Failed creating Widget");
 *             expected_object.make_unexpected(status);
 *         }
 *         return expected_object;
 *     }
 * };
 * 
 * Note that the constructor without ExpectedKey must be private and the constructor with ExpectedKey must be public
 * since we are using Widget::Create to construct a new object safely.
 * ExpectedKey is a secret key that can only be constructed by Expected<T>, thus this constructor can only be called
 * from Expected<T>.
 * 
 * One can construct and use Widget using the following code:
 * 
 * static hailo_status construct_widget()
 * {
 *     auto widget = Widget::create(3.14f);
 *     if (!widget) {
 *         return widget.status();
 *     }
 *     // Use widget ...
 *     return HAILO_SUCCESS;
 * }
 * 
 * 
 * *** Example #3 - Object composition  ***
 * See example below for object composition over Expected<T>:
 * 
 * class WidgetA {
 * private:
 *     WidgetA(float f, hailo_status &status)
 *     {
 *         status = (f == 3.14f) ? HAILO_SUCCESS : HAILO_INVALID_ARGUMENT;
 *     }
 * 
 * public:
 *     static Expected<WidgetA> create(float f)
 *     {
 *         hailo_status status = HAILO_UNINITIALIZED;
 *         WidgetA object(f, status);
 *         if (HAILO_SUCCESS != status) {
 *             LOGGER__ERROR("Failed creating WidgetA");
 *             return make_unexpected(status);
 *         }
 *         return std::move(object);
 *     }
 * };
 * 
 * class WidgetB {
 * private:
 *     WidgetA m_a;
 * 
 *     WidgetB(float f, WidgetA &&a, hailo_status &status) :
 *         m_a(std::move(a))
 *     {
 *         status = (f == 2.71f) ? HAILO_SUCCESS : HAILO_INVALID_ARGUMENT;
 *     }
 * 
 * public:
 *     static Expected<WidgetB> create(float af, float bf)
 *     {
 *         hailo_status status = HAILO_UNINITIALIZED;
 * 
 *         auto a = WidgetA::create(af);
 *         if (!a) {
 *             return make_unexpected(a.status());
 *         }
 * 
 *         WidgetB object(bf, std::move(a.release()), status);
 *         if (HAILO_SUCCESS != status) {
 *             LOGGER__ERROR("Failed creating WidgetB");
 *             return make_unexpected(status);
 *         }
 *         return std::move(object);
 *     }
 * };
 * 
 * 
 * *** Example #4 - Divide two numbers  ***
 * 
 * A divide function implementation:
 * 
 * static Expected<int> divide(int numerator, int denominator)
 * {
 *     if (denominator == 0) {
 *         LOGGER__ERROR("Cannot divide by 0");
 *         return make_unexpected(HAILO_INVALID_ARGUMENT);
 *     }
 *     
 *     return (numerator/denominator);
 * }
 * 
 * 
 **/

#ifndef _HAILO_EXPECTED_HPP_
#define _HAILO_EXPECTED_HPP_

#include "hailo/hailort.h"

#include <assert.h>
#include <utility>
#include <type_traits>

#ifdef __cpp_exceptions
#include <stdexcept>
#include <sstream>
/** hailort namespace */
namespace hailort
{

/*! hailort_error is an Exception object that inherits from std::runtime_error.
    Using this class requires compilation with exceptions */
class hailort_error : public std::runtime_error
{
public:
    template<typename... Args>
    hailort_error(hailo_status status, Args&&... args) :
        std::runtime_error(std::forward<Args>(args)...), m_status(status)
    {}

    /**
     * Returns the error status that caused this exception.
     */
    hailo_status status() const {
        return m_status;
    }

private:
    hailo_status m_status;
};
#else
/** hailort namespace */
namespace hailort
{

/*! hailort_error is an Exception object that inherits from std::runtime_error.
    Using this class requires compilation with exceptions */
class hailort_error {
public:

    /**
     * Returns the error status that caused this exception.
     */
    hailo_status status() const {
        return HAILO_INVALID_OPERATION;
    }
};
#endif

// TODO(oro): constexpr
// TODO(oro): noexcept
// TODO(oro): std::is_default_constructible
// TODO(oro) Add an implicit variadic ctor to support T implicitly. Note that only implicit variadic constructor
//           will call Ts explicits ctors implicitly! so we must have both with some kind of std::enable_if

/*! Unexpected is an object containing ::hailo_status error, used when an unexpected outcome occurred. */
class Unexpected final
{
public:
    explicit Unexpected(hailo_status status) :
        m_status(status)
    {}

    operator hailo_status() { return m_status; }

    hailo_status m_status;
};

inline Unexpected make_unexpected(hailo_status status)
{
    return Unexpected(status);
}

template<typename T>
class Expected;

/**
 * A secret key (passkey idiom) used to call public constructors only from Expected<T>.
 */
class ExpectedKey {
private:
    template<typename> friend class Expected;
    constexpr explicit ExpectedKey() = default;
};

/*! Expected<T> is either a T or the ::hailo_status preventing T to be created.*/
template<typename T>
class Expected final
{
public:
    /**
     * Expected<T> can access Expected\<U\>'s private members (needed for implicit upcasting)
     */
    template<class U>
    friend class Expected;

    /**
     * Construct a new Expected<T> from an Unexpected status.
     *
     * NOTE: Asserting that status is not HAILO_SUCCESS if NDEBUG is not defined.
     */
    Expected(Unexpected unexpected) :
        m_status(unexpected.m_status)
    {
        assert(unexpected.m_status != HAILO_SUCCESS);
    }

    /**
     * Default constructor
     * 
     * Construct a new Expected<T> where:
     *  - m_value is set to default T()
     *  - m_status is set to HAILO_SUCCESS
     * 
     * NOTE: Commented out because we can use the variadic constructor with T's copy constructor.
     */
    // Expected() :
    //     m_value(), m_status(HAILO_SUCCESS)
    // {}
    
    /**
     * Copy constructor
     */
    explicit Expected(const Expected<T> &other) :
        m_status(other.m_status)
    {
        if (other.has_value()) {
            construct(&m_value, other.m_value);
        }
    }

    /**
     * Copy constructor for implicit upcasting
     */
    template <typename U>
    Expected(const Expected<U>& other) :
        m_status(other.m_status)
    {
        if (other.has_value()) {
            construct(&m_value, other.m_value);
        }
    }

    /**
     * Move constructor
     * 
     * Construct a new Expected<T> where:
     *  - other.m_status moved to this.m_status.
     *  - other.m_value moved to this.m_value if other.m_value exists.
     *
     * If other had value before the move, it will still have the value that was moved (so the value object is valid but
     * in an unspecified state).
     */
    Expected(Expected<T> &&other) :
        m_status(other.m_status)
    {
        if (other.has_value()) {
            construct(&m_value, std::move(other.m_value));
        }
    }

    /**
     * Construct a new Expected<T> from T& where:
     *  - m_value is set to value.
     *  - m_status is set to HAILO_SUCCESS.
     * 
     * NOTE: Commented out because we can use the variadic constructor with T's copy constructor.
     */
    // Expected(const T &value)
    //     : m_value(value), m_status(HAILO_SUCCESS)
    // {}

    /**
     * Construct a new Expected<T> from an rvalue T where:
     *  - value moved to this.m_value.
     *  - other.m_status is set to HAILO_SUCCESS.
     */
    Expected(T &&value) :
        m_value(std::move(value)),
        m_status(HAILO_SUCCESS)
    {}

    /**
     * This will prevent the T value to be of type hailo_status.
     * The goal is to prevent bugs of returning hailo_status as int value, instead of make_unexpected() with error status.
     */
    Expected(hailo_status status) = delete;

    /**
     * Construct a new Expected<T> by forwarding arguments to T constructors.
     * 
     * NOTE: std::enable_if_t used because the variadic constructor can sometimes have a better cv-qualifier match than
     *       the other constructors. For example, candidate is: Expected<T>::operator bool() const [with T = int]
     *       while conversion from 'Expected<int>' to 'int'.
     *       See https://stackoverflow.com/questions/51937519/class-constructor-precedence-with-a-variadic-template-constructor-for-a-value-wr
     */
    template <typename... Args, std::enable_if_t<std::is_constructible<T, Args...>::value, int> = 0>
    explicit Expected(Args &&...args) :
        m_value(std::forward<Args>(args)...),
        m_status(HAILO_SUCCESS)
    {}

    /**
     * Construct a new Expected<T> using the ExpectedKey by forwarding arguments to T constructors.
     * 
     * NOTE: std::enable_if_t used because the variadic constructor can sometimes have a better cv-qualifier match than
     *       the other constructors. For example, candidate is: Expected<T>::operator bool() const [with T = int]
     *       while conversion from 'Expected<int>' to 'int'.
     *       See https://stackoverflow.com/questions/51937519/class-constructor-precedence-with-a-variadic-template-constructor-for-a-value-wr
     * 
     * NOTE: ExpectedKey can only be constructed from Expected<T>. This way Expected<T> will be the only class that
     *       can call such constructors (that their first argument is ExpectedKey). 
     * 
     * NOTE: We are not going to support calling private constructors because:
     *      1. Making Expected<T> a friend class will give the user the ability to construct a new Expected<T> object
     *         without handling the returned status (if the constructor can fail).
     *      2. std::is_constructible doesn't work on private constructors with friend class, so std::enable_if will
     *         always be False.
     */
    template <typename... Args, std::enable_if_t<std::is_constructible<T, ExpectedKey, Args...>::value, int> = 0>
    explicit Expected(Args &&...args) :
        m_value(ExpectedKey(), std::forward<Args>(args)...),
        m_status(HAILO_SUCCESS)
    {}

    Expected<T>& operator=(const Expected<T> &other) = delete;
    Expected<T>& operator=(Expected<T> &&other) noexcept = delete;
    Expected<T>& operator=(const T &other) = delete;
    Expected<T>& operator=(T &&other) noexcept = delete;
    Expected<T>& operator=(hailo_status status) = delete;

    /**
     * Destructor.
     * 
     * Destruct T if has value.
     */
    ~Expected()
    {
        if (has_value()) {
            m_status = HAILO_UNINITIALIZED;
            m_value.~T();
        }
    }

    /**
     * Make an existing Expected<T> to Unexpected. Mainly used in create() functions for RVO.
     * Destructs T if has value.
     * NOTE: Asserting that status is not HAILO_SUCCESS if NDEBUG is not defined.
     */
    void make_unexpected(hailo_status status)
    {
        assert(status != HAILO_SUCCESS);
        if (has_value()) {
            m_value.~T();
        }
        m_status = status;
    }

    /**
     * Checks whether the object contains a value.
     */
    bool has_value() const
    {
        return (HAILO_SUCCESS == m_status);
    }

    /**
     * Returns the contained value.
     * @note This method must be called with a valid value inside! otherwise it can lead to undefined behavior.
     */
    T& value() &
    {
        assert(has_value());
        return m_value;
    }

    /**
     * Returns the contained value.
     * @note This method must be called with a valid value inside! otherwise it can lead to undefined behavior.
     */
    const T& value() const&
    {
        assert(has_value());
        return m_value;
    }

    /**
     * Returns the status.
     */
    hailo_status status() const
    {
        return m_status;
    }

    /**
     * Releases ownership of its stored value, by returning its value and making this object Unexpected.
     * @note This method must be called with a valid value inside! otherwise it can lead to undefined behavior. 
     */
    T release()
    {
        assert(has_value());
        T tmp = std::move(m_value);
        make_unexpected(HAILO_UNINITIALIZED);
        return tmp;
    }

#ifdef __cpp_exceptions

    /**
     * If the object contains a value, releases ownership of the stored value by returning its value and making this object Unexpected.
     * If the object is Unexpected, throws an exception of type hailort_error.
     * @note Using this method requires compilation with exceptions.
     */
    T expect(const std::string &msg) &&
    {
        if (!has_value()) {
            std::stringstream ss;
            ss << "Expected::expect() failed with status=";
            ss << status();
            ss << ". ";
            ss << msg;
            throw hailort_error(status(), ss.str());
        }
        return release();
    }
#else
    template <typename...>
    struct always_false { static constexpr bool value = false; };
    template <typename... Args>
    /**
     * If the object contains a value, releases ownership of the stored value by returning its value and making this object Unexpected.
     * If the object is Unexpected, throws an exception of type hailort_error.
     * @note Using this method requires compilation with exceptions.
     */
    T expect(Args &&...)
    {
        static_assert(always_false<Args...>::value, "Expected::expect() can't be used since exceptions are disabled.");
    }
#endif


    /**
     * Pointer of the contained value
     */
    T* operator->()
    {
        assert(has_value());
        return &(value());
    }

    /**
     * Pointer of the contained value
     */
    const T* operator->() const
    {
        assert(has_value());
        return &(value());
    }

    /**
     * Reference of the contained value
     */
    T& operator*() &
    {
        assert(has_value());
        return value();
    }

    const T& operator*() const&
    {
        assert(has_value());
        return value();
    }

    /**
     * Checks whether the object contains a value.
     */
    explicit operator bool() const
    {
        return has_value();
    }

private:
    template<typename... Args>
    static void construct(T *value, Args &&...args)
    {
        new ((void*)value) T(std::forward<Args>(args)...);
    }

    union {
        T m_value;
    };
    hailo_status m_status;
};

template <typename T>
using ExpectedRef = Expected<std::reference_wrapper<T>>;

} /* namespace hailort */

#endif  // _HAILO_EXPECTED_HPP_
