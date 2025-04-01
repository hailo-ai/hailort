/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
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

#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/buffer.hpp"

#include "common/file_utils.hpp"
#include "common/logger_macros.hpp"
#include <spdlog/fmt/bundled/core.h>

#define XXH_INLINE_ALL 1
#include "xxhash.h"

#include <assert.h>
#include <map>
#include <set>
#include <unordered_set>
#include <cstdint>
#include <cstddef>
#include <fstream>
#include <algorithm>


namespace hailort
{

#define IS_FIT_IN_UINT8(number) ((std::numeric_limits<uint8_t>::max() >= ((int32_t)(number))) && (std::numeric_limits<uint8_t>::min() <= ((int32_t)(number))))
#define IS_FIT_IN_UINT16(number) ((std::numeric_limits<uint16_t>::max() >= ((int32_t)(number))) && (std::numeric_limits<uint16_t>::min() <= ((int32_t)(number))))
#define IS_FIT_IN_UINT32(number) ((std::numeric_limits<uint32_t>::max() >= ((int64_t)(number))) && (std::numeric_limits<uint32_t>::min() <= ((int64_t)(number))))

static const uint32_t POLYNOMIAL = 0xEDB88320;

static const size_t MB = 1024 * 1024;
static const size_t XXH3_CHUNK_SIZE = 1024 * 1024 / 4; // We got best performance with this chunk_size for xxh3 algorithm

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

template <typename T, typename Q>
static inline std::set<Q> get_key_set(const std::map<Q, T> &map)
{
    std::set<Q> keys;
    std::transform(map.begin(), map.end(), std::inserter(keys, keys.end()),
        [](const auto &pair) { return pair.first; });
    return keys;
}

template <typename T, typename Q>
static inline std::unordered_set<Q> get_key_set(const std::unordered_map<Q, T> &map)
{
    std::unordered_set<Q> keys;
    std::transform(map.begin(), map.end(), std::inserter(keys, keys.end()),
        [](const auto &pair) { return pair.first; });
    return keys;
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


inline hailo_status get_status(hailo_status status)
{
    return status;
}

template<typename T>
inline hailo_status get_status(const Expected<T> &exp)
{
    return exp.status();
}

#define _CHECK(cond, ret_val, ...)      \
    do {                                \
        if (!(cond)) {                  \
            LOGGER__ERROR(__VA_ARGS__); \
            return (ret_val);           \
        }                               \
    } while(0)

/** Returns ret_val when cond is false */
#define CHECK(cond, ret_val, ...) \
    _CHECK((cond), make_unexpected(ret_val), CONSTRUCT_MSG("CHECK failed", ##__VA_ARGS__))
#define CHECK_AS_EXPECTED CHECK

#define CHECK_ARG_NOT_NULL(arg) _CHECK(nullptr != (arg), make_unexpected(HAILO_INVALID_ARGUMENT), "CHECK_ARG_NOT_NULL for {} failed", #arg)
#define CHECK_ARG_NOT_NULL_AS_EXPECTED CHECK_ARG_NOT_NULL

#define CHECK_NOT_NULL(arg, status) _CHECK(nullptr != (arg), make_unexpected(status), "CHECK_NOT_NULL for {} failed", #arg)
#define CHECK_NOT_NULL_AS_EXPECTED CHECK_NOT_NULL

#define _CHECK_SUCCESS(res, is_default, fmt, ...)                                                                               \
    do {                                                                                                                        \
        const auto &__check_success_status = get_status(res);                                                                   \
        _CHECK(                                                                                                                 \
            (HAILO_SUCCESS == __check_success_status),                                                                          \
            make_unexpected(__check_success_status),                                                                            \
            _CONSTRUCT_MSG(is_default, "CHECK_SUCCESS failed with status={}", fmt, __check_success_status, ##__VA_ARGS__)       \
        );                                                                                                                      \
    } while(0)
#define CHECK_SUCCESS(status, ...) _CHECK_SUCCESS(status, ISEMPTY(__VA_ARGS__), "" __VA_ARGS__)
#define CHECK_SUCCESS_AS_EXPECTED CHECK_SUCCESS

#define _CHECK_EXPECTED _CHECK_SUCCESS
#define CHECK_EXPECTED(obj, ...) _CHECK_EXPECTED(obj, ISEMPTY(__VA_ARGS__), "" __VA_ARGS__)
#define CHECK_EXPECTED_AS_STATUS CHECK_EXPECTED

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

#define _CHECK_GRPC_STATUS(status, ret_val, warning_msg)                                                                         \
    do {                                                                                                                         \
        if (!status.ok()) {                                                                                                      \
            LOGGER__ERROR("CHECK_GRPC_STATUS failed with error code: {}.", static_cast<int>(status.error_code()));               \
            LOGGER__WARNING(warning_msg);                                                                                        \
            return ret_val;                                                                                                      \
        }                                                                                                                        \
    } while(0)

#define SERVICE_WARNING_MSG ("Make sure HailoRT service is enabled and active!")
#define CHECK_GRPC_STATUS(status) _CHECK_GRPC_STATUS(status, HAILO_RPC_FAILED, SERVICE_WARNING_MSG)
#define CHECK_GRPC_STATUS_AS_EXPECTED(status) _CHECK_GRPC_STATUS(status, make_unexpected(HAILO_RPC_FAILED), SERVICE_WARNING_MSG)
#endif

// Macros that check status. If status is 'valid_error', return without printing error to the prompt.
#define CHECK_EXPECTED_WITH_ACCEPTABLE_STATUS(valid_error, exp, ...) if (valid_error == (exp).status()) {return make_unexpected(valid_error);} CHECK_SUCCESS(exp, __VA_ARGS__);
#define CHECK_SUCCESS_WITH_ACCEPTABLE_STATUS(valid_error, status, ...) if ((valid_error) == (status)) {return make_unexpected(valid_error);} CHECK_SUCCESS(status, __VA_ARGS__);


#define __HAILO_CONCAT(x, y) x ## y
#define _HAILO_CONCAT(x, y) __HAILO_CONCAT(x, y)

#define _TRY(expected_var_name, var_decl, expr, ...) \
    auto expected_var_name = (expr); \
    CHECK_EXPECTED(expected_var_name, __VA_ARGS__); \
    var_decl = expected_var_name.release()

#define _TRY_V(expected_var_name, var_decl, expr, ...) \
    auto expected_var_name = (expr); \
    CHECK_EXPECTED(expected_var_name, __VA_ARGS__); \
    var_decl = expected_var_name.value()

/**
 * The TRY macro is used to allow easier validation and access for variables returned as Expected<T>.
 * If the expression returns an Expected<T> with status HAILO_SUCCESS, the macro will release the expected and assign
 * the var_decl.
 * Otherwise, the macro will cause current function to return the failed status.
 *
 * Usage example:
 *
 * Expected<int> func() {
 *     TRY(auto var, return_5());
 *     // Now var is int with value 5
 *
 *     // func will return Unexpected with status HAILO_INTERNAL_FAILURE
 *     TRY(auto var2, return_error(HAILO_INTERNAL_FAILURE), "Failed doing stuff {}", 5);
 */
#define TRY(var_decl, expr, ...) _TRY(_HAILO_CONCAT(__expected, __COUNTER__), var_decl, expr, __VA_ARGS__)

/**
 * Same us TRY macro but instead of returning released value, it will return the value itself.
*/
// TODO: HRT-13624: Remove after 'expected' implementation is fixed
#define TRY_V(var_decl, expr, ...) _TRY_V(_HAILO_CONCAT(__expected, __COUNTER__), var_decl, expr, __VA_ARGS__)

#define _TRY_WITH_ACCEPTABLE_STATUS(valid_error, expected_var_name, var_decl, expr, ...) \
    auto expected_var_name = (expr); \
    CHECK_EXPECTED_WITH_ACCEPTABLE_STATUS(valid_error, expected_var_name, __VA_ARGS__); \
    var_decl = expected_var_name.release()

#define TRY_WITH_ACCEPTABLE_STATUS(valid_error, var_decl, expr, ...) _TRY_WITH_ACCEPTABLE_STATUS(valid_error, _HAILO_CONCAT(__expected, __COUNTER__), var_decl, expr, __VA_ARGS__)

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

static inline bool is_env_variable_on(const char *env_var_name, const std::string &required_value = "1")
{
    auto env_var  = std::getenv(env_var_name);
    return ((nullptr != env_var) && (strncmp(env_var, required_value.c_str(), required_value.size()) == 0));
}

static inline Expected<size_t> get_env_variable_as_size(const char *env_var_name) {
    const char *env_val = std::getenv(env_var_name);
    if (!env_val) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    static const int DECIMAL_BASE = 10;
    errno = 0;
    char *end = nullptr;
    size_t result = std::strtoull(env_val, &end, DECIMAL_BASE);

    /*
    * Check if the conversion succeeded completely:
    * If an error occurs during conversion (for example, due to overflow), std::strtoull will set errno to a non-zero value.
    * For a successful conversion, std::strtoull should consume the entire string, meaning that the character pointed
    * to by 'end' must be the null terminator ('\0').
    * Thus, a successful conversion requires both errno == 0 and *end == '\0'.
    */
    if (errno != 0 || (*end != '\0')) {
        LOGGER__ERROR("Failed to parse environment variable HAILO_ALIGNED_CCWS_MAPPED_BUFFER_SIZE");
        return make_unexpected(HAILO_INVALID_ARGUMENT);
    }

    return Expected<size_t>(result);
}

// When moving to C++17, use std::clamp
constexpr size_t clamp(size_t v, size_t lo, size_t hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static inline Expected<std::string> get_env_variable(const std::string &env_var_name)
{
    const auto env_var = std::getenv(env_var_name.c_str());
    // Using ifs instead of CHECKs to avoid printing the error message
    if (nullptr == env_var) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    const auto result = std::string(env_var);
    if (result.empty()) {
        return make_unexpected(HAILO_NOT_FOUND);
    }

    return Expected<std::string>(result);
}

template <typename T>
Expected<hailo_format_type_t> get_hailo_format_type()
{
    static const std::unordered_map<size_t, hailo_format_type_t> type_map = {
        {typeid(uint8_t).hash_code(), HAILO_FORMAT_TYPE_UINT8},
        {typeid(uint16_t).hash_code(), HAILO_FORMAT_TYPE_UINT16},
        {typeid(float32_t).hash_code(), HAILO_FORMAT_TYPE_FLOAT32}
    };

    auto it = type_map.find(typeid(T).hash_code());
    if (it != type_map.end()) {
        auto result = it->second;
        return result;
    }
    return make_unexpected(HAILO_NOT_FOUND);
}

class CRC32 {
public:
    CRC32() {
        generate_table();
    }

    Expected<uint32_t> calculate(std::shared_ptr<std::ifstream> stream, size_t buffer_size) const
    {
        TRY(auto stream_guard, StreamPositionGuard::create(stream));

        uint32_t crc = 0xFFFFFFFF;
        std::vector<char> buffer(MB);

        size_t total_bytes_read = 0;

        while (total_bytes_read < buffer_size) {
            size_t bytes_to_read = std::min(buffer_size - total_bytes_read, MB);
            stream->read(buffer.data(), bytes_to_read);

            size_t bytes_read = stream->gcount();
            total_bytes_read += bytes_read;
            for (size_t i = 0; i < bytes_read; ++i) {
                crc = (crc >> 8) ^ table[(crc ^ static_cast<uint8_t>(buffer[i])) & 0xFF];
            }
        }

        return crc ^ 0xFFFFFFFF;
    }

    uint32_t calculate(const MemoryView &buffer) const {
        uint32_t crc = 0xFFFFFFFF;
        auto data = buffer.data();

        for (size_t i = 0; i < buffer.size(); ++i) {
            crc = (crc >> 8) ^ table[(crc ^ data[i]) & 0xFF];
        }

        return crc ^ 0xFFFFFFFF;
    }

    static Expected<uint32_t> calc_crc_on_buffer(const MemoryView &buffer)
    {
        CRC32 crcCalculator;
        return crcCalculator.calculate(buffer);
    }

    static Expected<uint32_t> calc_crc_on_stream(std::shared_ptr<std::ifstream> stream, size_t size)
    {
        CRC32 crcCalculator;
        return crcCalculator.calculate(stream, size);
    }

private:
    uint32_t table[256];

    void generate_table() {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t crc = i;
            for (uint32_t j = 0; j < 8; ++j) {
                crc = (crc & 1) ? (crc >> 1) ^ POLYNOMIAL : (crc >> 1);
            }
            table[i] = crc;
        }
    }
};

class Xxhash final
{
public:
    Xxhash() = delete;

    static Expected<uint64_t> calc_xxh3_on_buffer(const MemoryView &buffer)
    {
        return XXH3_64bits(buffer.data(), buffer.size());
    }

    static Expected<uint64_t> calc_xxh3_on_stream(std::shared_ptr<std::ifstream> stream, size_t size)
    {
        TRY(auto stream_guard, StreamPositionGuard::create(stream));

        // TODO: HRT-15783 - Try improve performance with multiple buffers and threads
        auto state = std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)>(
            XXH3_createState(),
            &XXH3_freeState
        );
        // XXH3_64bits_reset resets a state to begin a new hash, must be called before XXH3_64bits_update
        CHECK_AS_EXPECTED(XXH3_64bits_reset(state.get()) != XXH_ERROR, HAILO_INTERNAL_FAILURE, "Failed to reset XXH3 state");

        char buffer[XXH3_CHUNK_SIZE];
        size_t total_bytes_read = 0;
        while (total_bytes_read < size) {
            size_t bytes_to_read = std::min(size - total_bytes_read, XXH3_CHUNK_SIZE);
            stream->read(buffer, bytes_to_read);
            CHECK_AS_EXPECTED(stream->good(), HAILO_FILE_OPERATION_FAILURE, "ifstream::read() failed");

            size_t bytes_read = stream->gcount();
            auto res = XXH3_64bits_update(state.get(), buffer, bytes_read);
            CHECK_AS_EXPECTED(res != XXH_ERROR, HAILO_INTERNAL_FAILURE, "Failed to update XXH3 state");
            total_bytes_read += bytes_read;
        }

        return XXH3_64bits_digest(state.get());
    }
};

class StringUtils final
{
public:
    StringUtils() = delete;

    static Expected<int32_t> to_int32(const std::string &str, int base);
    static Expected<uint8_t> to_uint8(const std::string &str, int base);
    static Expected<uint32_t> to_uint32(const std::string &str, int base);

    static std::string to_lower(const std::string &str)
    {
        std::string lower_str = str;
        std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(),
            [](auto ch) { return static_cast<char>(::tolower(ch)); });
        return lower_str;
    }

    static std::string to_hex_string(const uint8_t *array, size_t size, bool uppercase, const std::string &delimiter="");
};

class BufferUtils final
{
public:
    BufferUtils() = delete;

    static void summarize_buffer(const Buffer& buffer, std::ostream& os)
    {
        os << "Buffer addr = " << static_cast<const void *>(buffer.data()) << ", size = " << buffer.size() << std::endl;

        if (buffer.size() == 0) {
            os << "Buffer is empty" << std::endl;
            return;
        }

        size_t range_start = 0;
        uint8_t current_value = buffer[0];
        for (size_t i = 1; i < buffer.size(); ++i) {
            if (buffer[i] != current_value) {
                print_range(range_start, i, current_value, os);
                current_value = buffer[i];
                range_start = i;
            }
        }

        // Print the last range
        print_range(range_start, buffer.size(), current_value, os);
    }

    static void format_buffer(const Buffer &buffer, std::ostream& os)
    {
        format_buffer(buffer.data(), buffer.size(), os);
    }

    static void format_buffer(const MemoryView &mem_view, std::ostream& os)
    {
        format_buffer(mem_view.data(), mem_view.size(), os);
    }

private:
    static void print_range(size_t range_start, size_t range_end_exclusive, uint8_t value, std::ostream& os)
    {
        const auto message = fmt::format("[0x{:08X}:0x{:08X}] - 0x{:02X} ({} bytes)",
            range_start, range_end_exclusive - 1, static_cast<int>(value), range_end_exclusive - range_start);
        os << message << std::endl;
    }

    static void format_buffer(const uint8_t *buffer, size_t size, std::ostream& os)
    {
        assert(nullptr != buffer);

        os << "[addr = " << static_cast<const void *>(buffer) << ", size = " << size << "]" << std::endl;

        static const bool UPPERCASE = true;
        static const size_t BYTES_PER_LINE = 32;
        static const char *BYTE_DELIM = "  ";
        for (size_t offset = 0; offset < size; offset += BYTES_PER_LINE) {
            const size_t line_size = std::min(BYTES_PER_LINE, size - offset);
            os << fmt::format("0x{:08X}", offset) << BYTE_DELIM; // 32 bit offset into a buffer should be enough
            os << StringUtils::to_hex_string(buffer + offset, line_size, UPPERCASE, BYTE_DELIM) << std::endl;
        }
    }
};

class TimeoutGuard final
{
public:
    explicit TimeoutGuard(std::chrono::milliseconds total_timeout)
        : m_start_time(std::chrono::steady_clock::now()), m_total_timeout(total_timeout) {}

    std::chrono::milliseconds get_remaining_timeout() const {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_start_time);
        if (elapsed >= m_total_timeout) {
            return std::chrono::milliseconds(0); // Timeout exceeded
        }
        return m_total_timeout - elapsed;
    }

private:
    std::chrono::steady_clock::time_point m_start_time;
    std::chrono::milliseconds m_total_timeout;
};

} /* namespace hailort */

#endif /* HAILO_UTILS_H_ */