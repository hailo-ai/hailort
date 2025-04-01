/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file infer_request_accumulator.hpp
 * @brief Class that accept frame request from all streams inside some core op, and accumulate them into a single
 *        infer request.
 **/

#ifndef _HAILO_INFER_REQUEST_ACCUMULATOR_HPP_
#define _HAILO_INFER_REQUEST_ACCUMULATOR_HPP_

#include "vdma/channel/transfer_common.hpp"

#include <mutex>
#include <condition_variable>
#include <list>

namespace hailort
{

class InferRequestAccumulator final {
public:
    InferRequestAccumulator(size_t streams_count, size_t max_queue_size,
        std::function<void(InferRequest&&)> frame_accumulated);

    hailo_status add_transfer_request(const std::string &stream_name, TransferRequest &&request);

    // All new add_transfer_request call will fail. Waits until all accumulated infer requests are done, cancel all
    // partial requests.
    hailo_status shutdown(std::chrono::milliseconds timeout);

    size_t queue_size() const { return m_max_queue_size; }

private:

    using PartialInferRequest = std::unordered_map<std::string, TransferRequest>;

    // Find an infer request that can contain transfer request for the given stream name.
    ExpectedRef<PartialInferRequest> get_infer_request(const std::string &stream_name);

    const size_t m_streams_count;
    const size_t m_max_queue_size;
    std::function<void(InferRequest)> m_frame_accumulated;
    bool m_shutdown;

    // Increasing this counter when we frame_accumulated is called, and decrease it in the callback.
    size_t m_ongoing_infer_requests;

    std::mutex m_mutex;
    std::condition_variable m_cv;

    // A partial infer request contains TransferRequest from subset of the core op streams.
    // When a partial infer request is completed (all streams are filled), the m_frame_accumulated is called.
    std::list<PartialInferRequest> m_partial_infer_requests;
};

} /* namespace hailort */

#endif /* _HAILO_INFER_REQUEST_ACCUMULATOR_HPP_ */
