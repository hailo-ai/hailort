/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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


namespace hailort
{

#define DEFAULT_SCHEDULER_TIMEOUT (std::chrono::milliseconds(0))
#define DEFAULT_SCHEDULER_MIN_THRESHOLD (0)

#define INVALID_CORE_OP_HANDLE (UINT32_MAX)

using scheduler_core_op_handle_t = uint32_t;
using core_op_priority_t = uint8_t;

using stream_name_t = std::string;

struct ActiveDeviceInfo {
    ActiveDeviceInfo(const device_id_t &device_id, const std::string &device_arch) : 
        current_core_op_handle(INVALID_CORE_OP_HANDLE), next_core_op_handle(INVALID_CORE_OP_HANDLE), is_switching_core_op(false), 
        current_batch_size(0), current_cycle_requested_transferred_frames_h2d(), current_cycle_finished_transferred_frames_d2h(), 
        pending_to_read_frames(), device_id(device_id), device_arch(device_arch)
    {}
    scheduler_core_op_handle_t current_core_op_handle;
    scheduler_core_op_handle_t next_core_op_handle;
    std::atomic_bool is_switching_core_op;
    std::atomic_uint32_t current_batch_size;
    std::unordered_map<scheduler_core_op_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> current_cycle_requested_transferred_frames_h2d;
    std::unordered_map<scheduler_core_op_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> current_cycle_finished_transferred_frames_d2h;
    std::unordered_map<scheduler_core_op_handle_t, std::unordered_map<stream_name_t, std::atomic_uint32_t>> pending_to_read_frames;
    device_id_t device_id;
    std::string device_arch;
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

    virtual ReadyInfo is_core_op_ready(const scheduler_core_op_handle_t &core_op_handle, bool check_threshold) = 0;
    virtual bool has_core_op_drained_everything(const scheduler_core_op_handle_t &core_op_handle, const device_id_t &device_id) = 0;

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

    virtual std::map<core_op_priority_t, std::vector<scheduler_core_op_handle_t>> get_core_op_priority_map()
    {
        return m_core_op_priority;
    }

    virtual scheduler_core_op_handle_t get_next_core_op(core_op_priority_t priority)
    {
        if (!contains(m_next_core_op, priority)) {
            m_next_core_op[priority] = 0;
        }
        return m_next_core_op[priority];
    }

    virtual void set_next_core_op(const core_op_priority_t priority, const scheduler_core_op_handle_t &core_op_handle)
    {
        m_next_core_op[priority] = core_op_handle;
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

    std::map<core_op_priority_t, std::vector<scheduler_core_op_handle_t>> m_core_op_priority;

    hailo_scheduling_algorithm_t m_algorithm;
    std::unordered_map<core_op_priority_t, scheduler_core_op_handle_t> m_next_core_op;
};

} /* namespace hailort */

#endif /* _HAILO_SCHEDULER_BASE_HPP_ */
