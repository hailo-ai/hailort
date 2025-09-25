/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file scheduler_base.hpp
 * @brief Class declaration for scheduler base class.
 **/

#ifndef _HAILO_SCHEDULER_BASE_HPP_
#define _HAILO_SCHEDULER_BASE_HPP_

#include "hailo/hailort.h"
#include "hailo/expected.hpp"

#include "common/utils.hpp"
#include "common/filesystem.hpp"

#include "stream_common/stream_internal.hpp"

#include <condition_variable>
#include <algorithm>


namespace hailort
{

#define DEFAULT_SCHEDULER_TIMEOUT (std::chrono::milliseconds(0))
#define DEFAULT_SCHEDULER_MIN_THRESHOLD (0)


using scheduler_core_op_handle_t = uint32_t;
using core_op_priority_t = uint8_t;

using stream_name_t = std::string;

struct ActiveDeviceInfo {
    ActiveDeviceInfo(const device_id_t &device_id, const std::string &device_arch) : 
        current_core_op_handle(INVALID_CORE_OP_HANDLE), next_core_op_handle(INVALID_CORE_OP_HANDLE), is_switching_core_op(false), 
        current_batch_size(0),
        frames_left_before_stop_streaming(0),
        ongoing_infer_requests(0),
        device_id(device_id),
        device_arch(device_arch)
    {}

    bool is_idle() const
    {
        return 0 == ongoing_infer_requests;
    }

    scheduler_core_op_handle_t current_core_op_handle;
    scheduler_core_op_handle_t next_core_op_handle;
    std::atomic_bool is_switching_core_op;
    std::atomic_uint32_t current_batch_size;

    // Until this counter is greater than zero, we won't stop streaming on current core op if we have ready frames
    // (even if there is another core op ready).
    size_t frames_left_before_stop_streaming;

    std::atomic_uint32_t ongoing_infer_requests;

    device_id_t device_id;
    std::string device_arch;
};

// Group of core ops with the same priority.
class PriorityGroup {
public:
    PriorityGroup() = default;

    void add(scheduler_core_op_handle_t core_op_handle)
    {
        m_core_ops.emplace_back(core_op_handle);
    }

    void erase(scheduler_core_op_handle_t core_op_handle)
    {
        auto it = std::find(m_core_ops.begin(), m_core_ops.end(), core_op_handle);
        assert(it != m_core_ops.end());
        m_core_ops.erase(it);
        m_next_core_op_index = 0; // Avoiding overflow by reseting next core op.
    }

    bool contains(scheduler_core_op_handle_t core_op_handle) const
    {
        return ::hailort::contains(m_core_ops, core_op_handle);
    }

    // Returns a core op at m_next_core_op_index + relative_index
    scheduler_core_op_handle_t get(size_t relative_index) const
    {
        assert(relative_index < m_core_ops.size());
        const auto abs_index = (m_next_core_op_index + relative_index) % m_core_ops.size();
        return m_core_ops[abs_index];
    }

    void set_next(size_t relative_index)
    {
        assert(relative_index <= m_core_ops.size()); // allowing wrap around
        m_next_core_op_index = (m_next_core_op_index + relative_index) % m_core_ops.size();
    }

    size_t size() const { return m_core_ops.size(); }

private:
    std::vector<scheduler_core_op_handle_t> m_core_ops;

    // index inside core_ops vector, next core to be executed from this priority. Used to implement round robin on the
    // group.
    size_t m_next_core_op_index = 0;
};


class SchedulerBase
{
public:
    hailo_scheduling_algorithm_t algorithm()
    {
        return m_algorithm;
    }

    struct ReadyInfo {
        bool over_threshold = false;
        bool over_timeout = false;
        bool is_ready = false;
    };

    virtual ReadyInfo is_core_op_ready(const scheduler_core_op_handle_t &core_op_handle, bool check_threshold,
        const device_id_t &device_id) = 0;

    virtual uint32_t get_device_count() const
    {
        return static_cast<uint32_t>(m_devices.size());
    }

    virtual std::shared_ptr<ActiveDeviceInfo> get_device_info(const device_id_t &device_id)
    {
        return m_devices[device_id];
    }
    
    virtual std::map<device_id_t, std::shared_ptr<ActiveDeviceInfo>> &get_device_infos()
    {
        return m_devices;
    }

    virtual std::map<core_op_priority_t, PriorityGroup> &get_core_op_priority_map()
    {
        return m_core_op_priority;
    }

protected:
    SchedulerBase(hailo_scheduling_algorithm_t algorithm, std::vector<std::string> &devices_ids,
         std::vector<std::string> &devices_arch) : m_algorithm(algorithm)
    {
        for (uint32_t i = 0; i < devices_ids.size(); i++) {
            m_devices[devices_ids.at(i)] = make_shared_nothrow<ActiveDeviceInfo>(devices_ids[i], devices_arch[i]);
        }
    };

    virtual ~SchedulerBase() = default;
    SchedulerBase(const SchedulerBase &other) = delete;
    SchedulerBase &operator=(const SchedulerBase &other) = delete;
    SchedulerBase &operator=(SchedulerBase &&other) = delete;
    SchedulerBase(SchedulerBase &&other) noexcept = delete;

    std::map<device_id_t, std::shared_ptr<ActiveDeviceInfo>> m_devices;

    std::map<core_op_priority_t, PriorityGroup> m_core_op_priority;

    hailo_scheduling_algorithm_t m_algorithm;
};

} /* namespace hailort */

#endif /* _HAILO_SCHEDULER_BASE_HPP_ */
