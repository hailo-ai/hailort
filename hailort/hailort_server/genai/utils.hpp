/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file utils.hpp
 * @brief Common utils functions for genai servers
 **/

#ifndef _HAILO_GENAI_UTILS_HPP_
#define _HAILO_GENAI_UTILS_HPP_

#include "hailo/hailort.h"
#include "hailo/buffer.hpp"
#include "hailo/hef.hpp"

#include "common/utils.hpp"
#include "common/filesystem.hpp"
#include "common/genai/serializer/serializer.hpp"
#include "common/genai/session_wrapper/session_wrapper.hpp"

#include "common/genai/eigen.hpp"
#include <nlohmann/json.hpp>

#include <fstream>
#include <future>

namespace hailort
{
namespace genai
{

#define CHECK_SUCCESS_OR_DO_AND_RETURN(status, action, error_message_format, ...) \
do { \
    if (status != HAILO_SUCCESS) { \
        if (HAILO_SHUTDOWN_EVENT_SIGNALED != status) { \
            LOGGER__ERROR(error_message_format, ##__VA_ARGS__); \
        } \
        action; \
        return; \
    } \
} while (0)

constexpr auto SCHEDULER_TIMEOUT = std::chrono::milliseconds(100);
constexpr auto JOB_WAIT_TIMEOUT = std::chrono::milliseconds(1000);
constexpr auto DEFAULT_SCHEDULER_TIMEOUT = std::chrono::milliseconds(0);
constexpr auto DEFAULT_SCHEDULER_THRESHOLD = 1;

// Timeout for asynchronous operations in server
constexpr auto WAIT_FOR_OPERATION_TIMEOUT = std::chrono::seconds(10);

inline Expected<Buffer> handle_check_hef_exists_request(const MemoryView &request)
{
    TRY_AS_HRPC_STATUS(auto pair, GenAICheckHefExistsSerializer::deserialize_request(request),
        GenAICheckHefExistsSerializer);
    const auto &hef_path = pair.first;
    const auto &hef_hash = pair.second;

    if (!Filesystem::does_file_exists(hef_path)) {
    LOGGER__INFO("HEF file '{}' does not exist on device", hef_path);
    TRY_AS_HRPC_STATUS(auto reply, GenAICheckHefExistsSerializer::serialize_reply(HAILO_SUCCESS, false),
        GenAICheckHefExistsSerializer);
    return reply;
    }

    TRY_AS_HRPC_STATUS(auto local_hef_hash, Hef::hash(hef_path), GenAICheckHefExistsSerializer);
    if (local_hef_hash != hef_hash) {
    LOGGER__INFO("HEF file '{}' exists on device, but hash '{}' does not match expected hash '{}'", hef_path, local_hef_hash, hef_hash);
    TRY_AS_HRPC_STATUS(auto reply, GenAICheckHefExistsSerializer::serialize_reply(HAILO_SUCCESS, false),
        GenAICheckHefExistsSerializer);
    return reply;
    }

    LOGGER__INFO("HEF file '{}' exists on device and hash '{}' matches expected hash '{}'", hef_path, local_hef_hash, hef_hash);
    TRY_AS_HRPC_STATUS(auto reply, GenAICheckHefExistsSerializer::serialize_reply(HAILO_SUCCESS, true),
        GenAICheckHefExistsSerializer);
    return reply;
}

inline std::map<std::string, MemoryView> buffers_to_memviews(const std::map<std::string, BufferPtr> &buffers)
{
    std::map<std::string, MemoryView> memviews;
    for (auto &pair : buffers) {
        auto &name = pair.first;
        auto &buffer = pair.second;
        memviews[name] = MemoryView(*buffer);
    }
    return memviews;
}

inline Expected<std::string> get_model_name_from_suffix(const Hef &hef, const std::string &model_suffix)
{
    std::string model_name = "";
    for (const auto &network_group_name : hef.get_network_groups_names()) {
        if (has_suffix(network_group_name, model_suffix)) {
            model_name = network_group_name;
        }
    }
    CHECK(!model_name.empty(), HAILO_INTERNAL_FAILURE, "Model doesnt have NG with name-suffix '{}'", model_suffix);

    return model_name;
}

// Linear interpolation (adjust to np.interp)
inline Eigen::VectorXf interp(const Eigen::VectorXf &x, const Eigen::VectorXf &xp, const Eigen::VectorXf &fp)
{
    Eigen::VectorXf result(x.size());
    for (int i = 0; i < x.size(); ++i) {
        float32_t xi = x(i);

        if (xi <= xp(0)) {
            result(i) = fp(0);
        } else if (xi >= xp(xp.size() - 1)) {
            result(i) = fp(fp.size() - 1);
        } else {
            int idx = 0;
            while (idx < xp.size() - 1 && xp(idx + 1) < xi) {
                ++idx;
            }

            float32_t x0 = xp(idx);
            float32_t x1 = xp(idx + 1);
            float32_t y0 = fp(idx);
            float32_t y1 = fp(idx + 1);
            float32_t t = (xi - x0) / (x1 - x0);
            result(i) = y0 + t * (y1 - y0);
        }
    }
    return result;
}

inline Eigen::RowVectorXf log_softmax(const Eigen::RowVectorXf &x)
{
    float32_t max_val = x.maxCoeff();
    Eigen::RowVectorXf result = x;
    result.array() -= max_val;
    float32_t sum_exp = result.array().exp().sum();
    result.array() -= std::log(sum_exp);
    return result;
}

inline float32_t logsumexp(const Eigen::RowVectorXf &x)
{
    float32_t max_val = x.maxCoeff();
    Eigen::RowVectorXf shifted = x.array() - max_val;
    return max_val + std::log(shifted.array().exp().sum());
}

inline int argmax(const Eigen::VectorXf &x)
{
    Eigen::Index idx = 0;
    x.maxCoeff(&idx);
    return static_cast<int>(idx);
}

inline nlohmann::json parse_json(const MemoryView &json_view)
{
    auto json_ptr = json_view.data();
    auto json_size = json_view.size();
    return nlohmann::json::parse(json_ptr, json_ptr + json_size);
}

template<typename T>
inline T wait_for_future_value(std::future<T> &future, const std::chrono::milliseconds &timeout = LONG_TIMEOUT)
{
    if (!future.valid()) {
        return make_unexpected(HAILO_INTERNAL_FAILURE);
    }
    auto wait_status = future.wait_for(timeout);
    if (wait_status != std::future_status::ready) {
        return make_unexpected(HAILO_TIMEOUT);
    }
    return future.get();
}

template<typename T>
inline hailo_status wait_for_future_status(std::future<T> &future, const std::chrono::milliseconds &timeout = LONG_TIMEOUT)
{
    if (!future.valid()) {
        return HAILO_INTERNAL_FAILURE;
    }
    auto wait_status = future.wait_for(timeout);
    if (wait_status != std::future_status::ready) {
        return HAILO_TIMEOUT;
    }
    return future.get();
}

template<typename T>
inline hailo_status wait_for_future_status_or_shutdown(std::future<T> &future, std::shared_ptr<Event> shutdown_event,
    const std::chrono::milliseconds &timeout = LONG_TIMEOUT)
{
    CHECK(future.valid(), HAILO_INTERNAL_FAILURE, "Future is not valid!");

    const auto poll_interval = std::chrono::milliseconds(100);
    TimeoutGuard timeout_guard(timeout);

    while (timeout_guard.get_remaining_timeout() > std::chrono::milliseconds(0)) {
        // Check if shutdown was requested
        if (shutdown_event && (HAILO_SUCCESS == shutdown_event->wait(std::chrono::milliseconds(0)))) {
            LOGGER__WARNING("Future wait interrupted by shutdown event");
            return HAILO_SHUTDOWN_EVENT_SIGNALED;
        }

        // Wait for the future, using the minimum of poll_interval and remaining timeout
        auto wait_time = std::min(poll_interval, timeout_guard.get_remaining_timeout());

        // Check if future is ready
        auto wait_status = future.wait_for(wait_time);
        if (wait_status == std::future_status::ready) {
            return future.get();
        }
    }

    // Timeout expired
    return HAILO_TIMEOUT;
}

inline hailo_status receive_hef_chunk_sync(SessionWrapper &session, const Hef::HefChunksInfo &chunk,
    std::shared_ptr<Buffer> hef_buffer)
{
    LOGGER__INFO("Receiving HEF chunk '{}' (offset: {}, size: {} bytes)", chunk.name, chunk.offset, chunk.size);
    return session.receive_file_chunked(chunk.size, MemoryView(hef_buffer->data() + chunk.offset, chunk.size));
}

} /* namespace genai */
} /* namespace hailort */

#endif /* _HAILO_GENAI_UTILS_HPP_ */
