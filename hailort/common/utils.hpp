/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file utils.hpp
 * @brief TODO: brief
 *
 * TODO: doc
 **/

#ifndef HAILO_UTILS_H_
#define HAILO_UTILS_H_

#include <assert.h>
#include <hailo/hailort.h>
#include "common/logger_macros.hpp"
#include <spdlog/fmt/bundled/core.h>
#include <map>
#include <set>
#include <unordered_set>


namespace hailort
{

#define IS_FIT_IN_UINT8(number) ((std::numeric_limits<uint8_t>::max() >= ((int32_t)(number))) && (std::numeric_limits<uint8_t>::min() <= ((int32_t)(number))))
#define IS_FIT_IN_UINT16(number) ((std::numeric_limits<uint16_t>::max() >= ((int32_t)(number))) && (std::numeric_limits<uint16_t>::min() <= ((int32_t)(number))))
#define IS_FIT_IN_UINT32(number) ((std::numeric_limits<uint32_t>::max() >= ((int64_t)(number))) && (std::numeric_limits<uint32_t>::min() <= ((int64_t)(number))))

template <typename T>
static inline bool contains(const std::vector<T> &container, const T &value)
{
    return std::find(container.begin(), container.end(), value) != container.end();
}

template <typename T, typename Q>
static inline bool contains(const std::map<Q, T> &container, Q value)
{
    return (container.find(value) != container.end());
}

template <typename T, typename Q>
static inline bool contains(const std::unordered_map<Q, T> &container, Q value)
{
    return (container.find(value) != container.end());
}

template <typename T>
static inline bool contains(const std::set<T> &container, T value)
{
    return (container.find(value) != container.end());
}

template <typename T>
static inline bool contains(const std::unordered_set<T> &container, T value)
{
    return (container.find(value) != container.end());
}

// From https://stackoverflow.com/questions/57092289/do-stdmake-shared-and-stdmake-unique-have-a-nothrow-version
template <class T, class... Args>
static inline std::unique_ptr<T> make_unique_nothrow(Args&&... args)
    noexcept(noexcept(T(std::forward<Args>(args)...)))
{
#ifndef NDEBUG
    auto ptr = std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
    if (nullptr == ptr) {
        LOGGER__ERROR("make_unique failed, pointer is null!");
    }
    return ptr;
#else
    return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
#endif
}

template <class T, class... Args>
static inline std::shared_ptr<T> make_shared_nothrow(Args&&... args)
    noexcept(noexcept(T(std::forward<Args>(args)...)))
{
#ifndef NDEBUG
    auto ptr = std::shared_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
    if (nullptr == ptr) {
        LOGGER__ERROR("make_shared failed, pointer is null!");
    }
    return ptr;
#else
    return std::shared_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
#endif
}

#define ASSERT assert

#define ARRAY_ENTRIES(x) (sizeof(x) / sizeof((x)[0]))

#define RETURN_IF_ARG_NULL(arg)                         \
    do {                                                \
        if (NULL == (arg)) {                            \
            LOGGER__ERROR("Invalid argument: "#arg);    \
            return HAILO_INVALID_ARGUMENT;              \
        }                                               \
    } while(0)

#define _FREE(var, invalid_value, func)     \
    do {                                    \
        if ((invalid_value) != (var)) {     \
            free(var);                      \
            var = (invalid_value);          \
        }                                   \
    } while(0)

#define FREE(p) _FREE(p, NULL, free)

#define _CLOSE(var, invalid_var_value, func, invalid_func_result, status)                                       \
    do {                                                                                                        \
        if ((invalid_var_value) != (var)) {                                                                     \
            if ((invalid_func_result) == func(var)) {                                                           \
                LOGGER__ERROR("CLOSE failed");                                                                  \
                if (HAILO_SUCCESS == (status)) {                                                                \
                    status = HAILO_CLOSE_FAILURE;                                                               \
                }                                                                                               \
                else {                                                                                          \
                    LOGGER__ERROR("Not setting status to HAILO_CLOSE_FAILURE since it is not HAILO_SUCCESS");   \
                }                                                                                               \
            }                                                                                                   \
            var = (invalid_var_value);                                                                          \
        }                                                                                                       \
    } while(0)
// TODO: Add tests in tests/utils/main.cpp

#define CLOSE(fd, status) _CLOSE(fd, -1, close, -1, status)
#define FCLOSE(file, status) _CLOSE(file, NULL, fclose, 0, status)


// Detect empty macro arguments
// https://gustedt.wordpress.com/2010/06/08/detect-empty-macro-arguments/
#define _ARG16(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, ...) _15
#define HAS_COMMA(...) _ARG16(__VA_ARGS__, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0)
#define _TRIGGER_PARENTHESIS_(...) ,
 
#define ISEMPTY(...)                                                    \
_ISEMPTY(                                                               \
          /* test if there is just one argument, eventually an empty    \
             one */                                                     \
          HAS_COMMA(__VA_ARGS__),                                       \
          /* test if _TRIGGER_PARENTHESIS_ together with the argument   \
             adds a comma */                                            \
          HAS_COMMA(_TRIGGER_PARENTHESIS_ __VA_ARGS__),                 \
          /* test if the argument together with a parenthesis           \
             adds a comma */                                            \
          HAS_COMMA(__VA_ARGS__ (/*empty*/)),                           \
          /* test if placing it between _TRIGGER_PARENTHESIS_ and the   \
             parenthesis adds a comma */                                \
          HAS_COMMA(_TRIGGER_PARENTHESIS_ __VA_ARGS__ (/*empty*/))      \
          )
 
#define PASTE5(_0, _1, _2, _3, _4) _0 ## _1 ## _2 ## _3 ## _4
#define _ISEMPTY(_0, _1, _2, _3) HAS_COMMA(PASTE5(_IS_EMPTY_CASE_, _0, _1, _2, _3))
#define _IS_EMPTY_CASE_0001 ,
//

#define __CONSTRUCT_MSG_1(dft_fmt, usr_fmt, ...) dft_fmt, ##__VA_ARGS__
#define __CONSTRUCT_MSG_0(dft_fmt, usr_fmt, ...) dft_fmt " - " usr_fmt, ##__VA_ARGS__
#define __CONSTRUCT_MSG(is_dft, dft_fmt, usr_fmt, ...) __CONSTRUCT_MSG_##is_dft(dft_fmt, usr_fmt, ##__VA_ARGS__)
#define _CONSTRUCT_MSG(is_dft, dft_fmt, usr_fmt, ...) __CONSTRUCT_MSG(is_dft, dft_fmt, usr_fmt, ##__VA_ARGS__)
#define CONSTRUCT_MSG(dft_fmt, ...) _CONSTRUCT_MSG(ISEMPTY(__VA_ARGS__), dft_fmt, "" __VA_ARGS__)


#define _CHECK(cond, ret_val, ...)      \
    do {                                \
        if (!(cond)) {                  \
            LOGGER__ERROR(__VA_ARGS__); \
            return (ret_val);           \
        }                               \
    } while(0)

/** Returns ret_val when cond is false */
#define CHECK(cond, ret_val, ...) _CHECK((cond), (ret_val), CONSTRUCT_MSG("CHECK failed", ##__VA_ARGS__))
#define CHECK_AS_EXPECTED(cond, ret_val, ...) \
    _CHECK((cond), (make_unexpected(ret_val)), CONSTRUCT_MSG("CHECK_AS_EXPECTED failed", ##__VA_ARGS__))

#define CHECK_ARG_NOT_NULL(arg) _CHECK(nullptr != (arg), HAILO_INVALID_ARGUMENT, "CHECK_ARG_NOT_NULL for {} failed", #arg)

#define CHECK_ARG_NOT_NULL_AS_EXPECTED(arg) _CHECK(nullptr != (arg), make_unexpected(HAILO_INVALID_ARGUMENT), "CHECK_ARG_NOT_NULL_AS_EXPECTED for {} failed", #arg)

#define CHECK_NOT_NULL(arg, status) _CHECK(nullptr != (arg), status, "CHECK_NOT_NULL for {} failed", #arg)

#define CHECK_NOT_NULL_AS_EXPECTED(arg, status) _CHECK(nullptr != (arg), make_unexpected(status), "CHECK_NOT_NULL_AS_EXPECTED for {} failed", #arg)

#define _CHECK_SUCCESS(status, is_default, fmt, ...)                                                                            \
    do {                                                                                                                        \
        const auto &__check_success_status = (status);                                                                          \
        _CHECK(                                                                                                                 \
            HAILO_SUCCESS == __check_success_status,                                                                            \
            __check_success_status,                                                                                             \
            _CONSTRUCT_MSG(is_default, "CHECK_SUCCESS failed with status={}", fmt, __check_success_status, ##__VA_ARGS__)       \
        );                                                                                                                      \
    } while(0)
#define CHECK_SUCCESS(status, ...) _CHECK_SUCCESS(status, ISEMPTY(__VA_ARGS__), "" __VA_ARGS__)

#define _CHECK_SUCCESS_AS_EXPECTED(status, is_default, fmt, ...)                                                                       \
    do {                                                                                                                               \
        const auto &__check_success_status = (status);                                                                                 \
        _CHECK(                                                                                                                        \
            HAILO_SUCCESS == __check_success_status,                                                                                   \
            make_unexpected(__check_success_status),                                                                                   \
            _CONSTRUCT_MSG(is_default, "CHECK_SUCCESS_AS_EXPECTED failed with status={}", fmt, __check_success_status, ##__VA_ARGS__)  \
        );                                                                                                                             \
    } while(0)
#define CHECK_SUCCESS_AS_EXPECTED(status, ...) _CHECK_SUCCESS_AS_EXPECTED(status, ISEMPTY(__VA_ARGS__), "" __VA_ARGS__)

// Define macro CHECK_IN_DEBUG - that checks cond in debug with CHECK macro but in release does nothing and will get optimized out
#ifdef NDEBUG
// In release have this macro do nothing - empty macro
#define CHECK_IN_DEBUG(cond, ret_val, ...)
#else // NDEBUG
#define CHECK_IN_DEBUG(cond, ret_val, ...) CHECK(cond, ret_val, __VA_ARGS__)
#endif // NDEBUG

#ifdef HAILO_SUPPORT_MULTI_PROCESS
#define _CHECK_SUCCESS_AS_RPC_STATUS(status, reply, is_default, fmt, ...)                                                                       \
    do {                                                                                                                                        \
        const auto &__check_success_status = (status);                                                                                          \
        reply->set_status(static_cast<uint32_t>(__check_success_status));                                                                       \
        _CHECK(                                                                                                                                 \
            HAILO_SUCCESS == __check_success_status,                                                                                            \
            grpc::Status::OK,                                                                                                                   \
            _CONSTRUCT_MSG(is_default, "CHECK_SUCCESS_AS_RPC_STATUS failed with status={}", fmt, __check_success_status, ##__VA_ARGS__)         \
        );                                                                                                                                      \
    } while(0)
#define CHECK_SUCCESS_AS_RPC_STATUS(status, reply, ...) _CHECK_SUCCESS_AS_RPC_STATUS(status, reply, ISEMPTY(__VA_ARGS__), "" __VA_ARGS__)

#define CHECK_EXPECTED_AS_RPC_STATUS(expected, reply, ...) CHECK_SUCCESS_AS_RPC_STATUS(expected.status(), reply, __VA_ARGS__)

#define _CHECK_AS_RPC_STATUS(cond, reply, ret_val, ...)                                                                                    \
    do {                                                                                                                                   \
        if (!(cond)) {                                                                                                                     \
            reply->set_status(ret_val);                                                                                                    \
            LOGGER__ERROR(                                                                                                                 \
                _CONSTRUCT_MSG(is_default, "CHECK_AS_RPC_STATUS failed with status={}", fmt, ret_val, ##__VA_ARGS__)                       \
            );                                                                                                                             \
            return grpc::Status::OK;                                                                                                       \
        }                                                                                                                                  \
    } while(0)
#define CHECK_AS_RPC_STATUS(cond, reply, ret_val, ...) _CHECK_AS_RPC_STATUS((cond), (reply), (ret_val), ISEMPTY(__VA_ARGS__), "" __VA_ARGS__)

#define _CHECK_GRPC_STATUS(status, ret_val, warning_msg)                                                    \
    do {                                                                                                    \
        if (!status.ok()) {                                                                                 \
            LOGGER__ERROR("CHECK_GRPC_STATUS failed with error massage: {}.", status.error_message());      \
            LOGGER__WARNING(warning_msg);                                                                   \
            return ret_val;                                                                                 \
        }                                                                                                   \
    } while(0)

#define SERVICE_WARNING_MSG ("Make sure HailoRT service is enabled and active!")
#define CHECK_GRPC_STATUS(status) _CHECK_GRPC_STATUS(status, HAILO_RPC_FAILED, SERVICE_WARNING_MSG)
#define CHECK_GRPC_STATUS_AS_EXPECTED(status) _CHECK_GRPC_STATUS(status, make_unexpected(HAILO_RPC_FAILED), SERVICE_WARNING_MSG)
#endif

#define _CHECK_EXPECTED(obj, is_default, fmt, ...)                                                                                      \
    do {                                                                                                                                \
        const auto &__check_expected_obj = (obj);                                                                                       \
        _CHECK(                                                                                                                         \
            __check_expected_obj.has_value(),                                                                                           \
            make_unexpected(__check_expected_obj.status()),                                                                             \
            _CONSTRUCT_MSG(is_default, "CHECK_EXPECTED failed with status={}", fmt, __check_expected_obj.status(), ##__VA_ARGS__)       \
        );                                                                                                                              \
    } while(0)
#define CHECK_EXPECTED(obj, ...) _CHECK_EXPECTED(obj, ISEMPTY(__VA_ARGS__), "" __VA_ARGS__)


#define _CHECK_EXPECTED_AS_STATUS(obj, is_default, fmt, ...)                                                                                      \
    do {                                                                                                                                          \
        const auto &__check_expected_obj = (obj);                                                                                                 \
        _CHECK(                                                                                                                                   \
            __check_expected_obj.has_value(),                                                                                                     \
            __check_expected_obj.status(),                                                                                                        \
            _CONSTRUCT_MSG(is_default, "CHECK_EXPECTED_AS_STATUS failed with status={}", fmt, __check_expected_obj.status(), ##__VA_ARGS__)       \
        );                                                                                                                                        \
    } while(0)
#define CHECK_EXPECTED_AS_STATUS(obj, ...) _CHECK_EXPECTED_AS_STATUS(obj, ISEMPTY(__VA_ARGS__), "" __VA_ARGS__)

#ifndef _MSC_VER
#define IGNORE_DEPRECATION_WARNINGS_BEGIN _Pragma("GCC diagnostic push") \
                                          _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
#define IGNORE_DEPRECATION_WARNINGS_END  _Pragma("GCC diagnostic pop")
#else
#define IGNORE_DEPRECATION_WARNINGS_BEGIN
#define IGNORE_DEPRECATION_WARNINGS_END
#endif

constexpr bool is_powerof2(size_t v) {
    // bit trick
    return (v & (v - 1)) == 0;
}

constexpr uint32_t get_nearest_powerof_2(uint32_t value, uint32_t min_power_of_2)
{
    assert(value <= 0x80000000);
    uint32_t power_of_2 = min_power_of_2;
    while (value > power_of_2) {
        power_of_2 <<=  1;
    }
    return power_of_2;
}

template<class K, class V>
static uint32_t get_max_value_of_unordered_map(const std::unordered_map<K, V> &map)
{
    uint32_t max_count = 0;
    for (auto &name_counter_pair : map) {
        if (name_counter_pair.second > max_count) {
            max_count = name_counter_pair.second;
        }
    }
    return max_count;
}

template<class K, class V>
static uint32_t get_min_value_of_unordered_map(const std::unordered_map<K, V> &map)
{
    uint32_t min_count = UINT32_MAX;
    for (auto &name_counter_pair : map) {
        if (name_counter_pair.second < min_count) {
            min_count = name_counter_pair.second;
        }
    }
    return min_count;
}

static inline bool is_env_variable_on(const char* env_var_name)
{
    auto env_var  = std::getenv(env_var_name);
    return ((nullptr != env_var) && (strnlen(env_var, 2) == 1) && (strncmp(env_var, "1", 1) == 0));
}

} /* namespace hailort */

#endif /* HAILO_UTILS_H_ */