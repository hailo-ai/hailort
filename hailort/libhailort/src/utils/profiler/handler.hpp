/**
 * Copyright (c) 2019-2025 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file handler.hpp
 * @brief Handlers base class for HailoRT tracer mechanism
 **/

#ifndef _HAILO_HANDLER_HPP_
#define _HAILO_HANDLER_HPP_

#include "hailo/hailort.h"
#include "hailo/stream.hpp"

#include "vdevice/scheduler/scheduler_base.hpp"

namespace hailort
{

struct Trace
{
    Trace(const std::string &name)
        : name(name)
    {}

    virtual ~Trace() = default;

    uint64_t timestamp = 0;
    std::string name;
};

struct InitTrace : Trace
{
    InitTrace() : Trace("init") {}
};

struct InitProfilerProtoTrace : Trace
{
    InitProfilerProtoTrace () : Trace("init_profiler_proto") {}
};

struct AddDeviceTrace : Trace
{
    AddDeviceTrace(const device_id_t &device_id, const std::string &device_arch)
        : Trace("add_device_trace"), device_id(device_id), device_arch(device_arch)
    {}

    device_id_t device_id;
    std::string device_arch;
};

struct MonitorStartTrace : Trace
{
    MonitorStartTrace(const std::string &unique_vdevice_hash)
        : Trace("scheduler_start"), unique_vdevice_hash(unique_vdevice_hash)
    {}

    std::string unique_vdevice_hash;
};

struct MonitorEndTrace : Trace
{
    MonitorEndTrace(const std::string &unique_vdevice_hash)
        : Trace("scheduler_end"), unique_vdevice_hash(unique_vdevice_hash)
    {}

    std::string unique_vdevice_hash;
};

struct AddCoreOpTrace : Trace
{
    AddCoreOpTrace(const std::string &core_op_name, uint64_t timeout, uint32_t threshold,
        scheduler_core_op_handle_t handle, int batch_size)
        : Trace("add_core_op"), core_op_name(core_op_name), timeout(timeout), threshold(threshold),
            core_op_handle(handle), batch_size(batch_size)
    {}

    std::string core_op_name;
    uint64_t timeout = 0;
    uint32_t threshold = 0;
    scheduler_core_op_handle_t core_op_handle = INVALID_CORE_OP_HANDLE;
    int batch_size = 0;
};

struct AddStreamH2DTrace : Trace
{
    AddStreamH2DTrace(const device_id_t &device_id, const std::string &core_op_name, const std::string &stream_name, uint32_t queue_size,
        scheduler_core_op_handle_t core_op_handle)
        : Trace("create_input_stream"), device_id(device_id), core_op_name(core_op_name), stream_name(stream_name), queue_size(queue_size),
        core_op_handle(core_op_handle)
    {}

    device_id_t device_id;
    std::string core_op_name;
    std::string stream_name;
    uint32_t queue_size;
    scheduler_core_op_handle_t core_op_handle;
};

struct AddStreamD2HTrace : Trace
{
    AddStreamD2HTrace(const device_id_t &device_id, const std::string &core_op_name, const std::string &stream_name, uint32_t queue_size,
        scheduler_core_op_handle_t core_op_handle)
        : Trace("create_output_stream"), device_id(device_id), core_op_name(core_op_name), stream_name(stream_name), queue_size(queue_size),
        core_op_handle(core_op_handle)
    {}

    device_id_t device_id;
    std::string core_op_name;
    std::string stream_name;
    uint32_t queue_size;
    scheduler_core_op_handle_t core_op_handle;
};

struct FrameEnqueueH2DTrace : Trace
{
    FrameEnqueueH2DTrace(scheduler_core_op_handle_t core_op_handle, const std::string &queue_name)
        : Trace("write_frame"), core_op_handle(core_op_handle), queue_name(queue_name)
    {}

    scheduler_core_op_handle_t core_op_handle;
    std::string queue_name;
};

struct FrameDequeueH2DTrace : Trace
{
    FrameDequeueH2DTrace(const device_id_t &device_id, scheduler_core_op_handle_t core_op_handle, const std::string &queue_name)
        : Trace("input_vdma_dequeue"), device_id(device_id), core_op_handle(core_op_handle), queue_name(queue_name)
    {}

    device_id_t device_id;
    scheduler_core_op_handle_t core_op_handle;
    std::string queue_name;
};

struct FrameDequeueD2HTrace : Trace
{
    FrameDequeueD2HTrace(scheduler_core_op_handle_t core_op_handle, const std::string &queue_name)
        : Trace("read_frame"), core_op_handle(core_op_handle), queue_name(queue_name)
    {}

    scheduler_core_op_handle_t core_op_handle;
    std::string queue_name;
};

struct FrameEnqueueD2HTrace : Trace
{
    FrameEnqueueD2HTrace(const device_id_t &device_id, scheduler_core_op_handle_t core_op_handle, const std::string &queue_name)
        : Trace("output_vdma_enqueue"), device_id(device_id), core_op_handle(core_op_handle), queue_name(queue_name)
    {}

    device_id_t device_id;
    scheduler_core_op_handle_t core_op_handle;
    std::string queue_name;
};

struct ActivateCoreOpTrace : Trace
{
    ActivateCoreOpTrace(const device_id_t &device_id, vdevice_core_op_handle_t handle, double duration,
        int dynamic_batch_size)
        : Trace("activate_core_op"), device_id(device_id), core_op_handle(handle), duration(duration), dynamic_batch_size(dynamic_batch_size)
    {}

    device_id_t device_id;
    vdevice_core_op_handle_t core_op_handle;
    double duration;
    int dynamic_batch_size;
};

// Currently, activate and switch are the same trace to make scheduler and fast-switch flow similar (although in the
// scheduler we have no deactivate).
using SwitchCoreOpTrace = ActivateCoreOpTrace;

struct DeactivateCoreOpTrace : Trace
{
    DeactivateCoreOpTrace(const device_id_t &device_id, vdevice_core_op_handle_t handle, double duration)
        : Trace("deactivate_core_op"), device_id(device_id), core_op_handle(handle), duration(duration)
    {}

    device_id_t device_id;
    vdevice_core_op_handle_t core_op_handle;
    double duration;
};

struct SetCoreOpTimeoutTrace : Trace
{
    SetCoreOpTimeoutTrace(vdevice_core_op_handle_t handle, const std::chrono::milliseconds timeout)
        : Trace("set_timeout"), core_op_handle(handle), timeout(timeout)
    {}

    vdevice_core_op_handle_t core_op_handle;
    std::chrono::milliseconds timeout;
};

struct SetCoreOpThresholdTrace : Trace
{
    SetCoreOpThresholdTrace(vdevice_core_op_handle_t handle, uint32_t threshold)
        : Trace("set_threshold"), core_op_handle(handle), threshold(threshold)
    {}

    vdevice_core_op_handle_t core_op_handle;
    uint32_t threshold;
};

struct SetCoreOpPriorityTrace : Trace
{
    SetCoreOpPriorityTrace(vdevice_core_op_handle_t handle, uint8_t priority)
        : Trace("set_priority"), core_op_handle(handle), priority(priority)
    {}

    vdevice_core_op_handle_t core_op_handle;
    uint8_t priority;
};

struct OracleDecisionTrace : Trace
{
    OracleDecisionTrace(bool reason_idle, device_id_t device_id, vdevice_core_op_handle_t handle, bool over_threshold,
        bool over_timeout)
        : Trace("switch_core_op_decision"), reason_idle(reason_idle), device_id(device_id), core_op_handle(handle),
        over_threshold(over_threshold), over_timeout(over_timeout)
    {}

    bool reason_idle;
    device_id_t device_id;
    vdevice_core_op_handle_t core_op_handle;
    bool over_threshold;
    bool over_timeout;
};

struct HefLoadedTrace : Trace
{
    HefLoadedTrace(const std::string &hef_name, const std::string &dfc_version, const unsigned char *md5_hash)
        : Trace("hef_loaded"), hef_name(hef_name), dfc_version(dfc_version)
    {
        std::memcpy(this->md5_hash, md5_hash, MD5_DIGEST_LENGTH);
    }

    std::string hef_name;
    std::string dfc_version;
    MD5_SUM_t md5_hash;
};

struct DumpProfilerStateTrace : Trace
{
    DumpProfilerStateTrace() : Trace("dump_profiler_state") {}
};

class Handler
{
public:
    virtual ~Handler() = default;

    virtual void handle_trace(const InitTrace&) {};
    virtual void handle_trace(const AddCoreOpTrace&) {};
    virtual void handle_trace(const AddStreamH2DTrace&) {};
    virtual void handle_trace(const AddStreamD2HTrace&) {};
    virtual void handle_trace(const FrameEnqueueH2DTrace&) {};
    virtual void handle_trace(const FrameDequeueH2DTrace&) {};
    virtual void handle_trace(const FrameDequeueD2HTrace&) {};
    virtual void handle_trace(const FrameEnqueueD2HTrace&) {};
    virtual void handle_trace(const ActivateCoreOpTrace&) {};
    virtual void handle_trace(const DeactivateCoreOpTrace&) {};
    virtual void handle_trace(const MonitorStartTrace&) {};
    virtual void handle_trace(const MonitorEndTrace&) {};
    virtual void handle_trace(const AddDeviceTrace&) {};
    virtual void handle_trace(const SetCoreOpTimeoutTrace&) {};
    virtual void handle_trace(const SetCoreOpThresholdTrace&) {};
    virtual void handle_trace(const SetCoreOpPriorityTrace&) {};
    virtual void handle_trace(const OracleDecisionTrace&) {};
    virtual void handle_trace(const DumpProfilerStateTrace&) {};
    virtual void handle_trace(const InitProfilerProtoTrace&) {};
    virtual void handle_trace(const HefLoadedTrace&) {};
    virtual bool should_dump_trace_file() { return false; }
    virtual bool should_stop () { return false; }
    virtual hailo_status dump_trace_file() { return HAILO_SUCCESS; }

};

struct JSON;

}

#endif /* _HAILO_HANDLER_HPP */
