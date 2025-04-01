/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_request_accumulator.cpp
 **/

#include "infer_request_accumulator.hpp"

namespace hailort
{

InferRequestAccumulator::InferRequestAccumulator(size_t streams_count, size_t max_queue_size,
    std::function<void(InferRequest&&)> frame_accumulated) :
        m_streams_count(streams_count),
        m_max_queue_size(max_queue_size),
        m_frame_accumulated(frame_accumulated),
        m_shutdown(false),
        m_ongoing_infer_requests(0)
{}

hailo_status InferRequestAccumulator::add_transfer_request(const std::string &stream_name, TransferRequest &&request)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_shutdown) {
        return HAILO_STREAM_NOT_ACTIVATED;
    }

    // Insert the transfer to next available infer request
    auto infer_request = get_infer_request(stream_name);
    if (!infer_request) {
        return infer_request.status();
    }
    infer_request->get().emplace(stream_name, std::move(request));

    // If first infer request was finished, call m_frame_accumulated on it
    if (m_partial_infer_requests.front().size() == m_streams_count) {

        m_ongoing_infer_requests++;
        m_frame_accumulated(InferRequest{
            std::move(m_partial_infer_requests.front()),
            [this](hailo_status) {
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_ongoing_infer_requests--;
                }
                m_cv.notify_all();
            }
        });
        m_partial_infer_requests.pop_front();
    }

    return HAILO_SUCCESS;
}

hailo_status InferRequestAccumulator::shutdown(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(m_mutex);

    assert(!m_shutdown);
    m_shutdown = true;

    // Wait until m_ongoing_infer_requests==0
    auto done = m_cv.wait_for(lock, timeout, [this]() { return m_ongoing_infer_requests == 0; });
    CHECK(done, HAILO_TIMEOUT, "Failed shutdown, ongoing infer requests - {}", m_ongoing_infer_requests);

    // Now cancel all partial request
    for (auto &partial_request : m_partial_infer_requests) {
        for (auto &stream_transfer_request : partial_request) {
            stream_transfer_request.second.callback(HAILO_STREAM_ABORT);
        }
    }
    m_partial_infer_requests.clear();

    return HAILO_SUCCESS;
}

ExpectedRef<InferRequestAccumulator::PartialInferRequest> InferRequestAccumulator::get_infer_request(
    const std::string &stream_name)
{
    // Try find infer request that doesn't contain transfer for stream name.
    for (auto &partial_infer_request : m_partial_infer_requests) {
        if (!contains(partial_infer_request, stream_name)) {
            return std::ref(partial_infer_request);
        }
    }

    // Create new infer request (only if there is place in the queue)
    if (m_partial_infer_requests.size() >= m_max_queue_size) {
        return make_unexpected(HAILO_QUEUE_IS_FULL);
    }

    m_partial_infer_requests.emplace_back();
    return std::ref(m_partial_infer_requests.back());
}

} /* namespace hailort */
