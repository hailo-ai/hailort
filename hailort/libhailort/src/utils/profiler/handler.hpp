/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
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

struct CoreOpIdleTrace : Trace
{
    CoreOpIdleTrace(const device_id_t &device_id, scheduler_core_op_handle_t core_op_handle)
        : Trace("core_op_idle"), device_id(device_id), core_op_handle(core_op_handle)
    {} 

    device_id_t device_id;
    scheduler_core_op_handle_t core_op_handle;
};

struct AddDeviceTrace : Trace
{
    AddDeviceTrace(const device_id_t &device_id, const std::string &device_arch)
        : Trace("add_device_trace"), device_id(device_id), device_arch(device_arch)
    {} 

    device_id_t device_id;
    std::string device_arch;
};

struct SchedulerStartTrace : Trace
{
    SchedulerStartTrace(uint32_t device_count)
        : Trace("scheduler_start"), device_count(device_count)
    {} 

    uint32_t device_count = 0;
};

struct AddCoreOpTrace : Trace
{
    AddCoreOpTrace(const device_id_t &device_id, const std::string &core_op_name, uint64_t timeout, uint32_t threshold, scheduler_core_op_handle_t handle,
    bool is_nms)
        : Trace("add_core_op"), device_id(device_id), core_op_name(core_op_name), timeout(timeout), threshold(threshold), core_op_handle(handle), is_nms(is_nms) 
    {}

    device_id_t device_id;
    std::string core_op_name;
    uint64_t timeout = 0;
    uint32_t threshold = 0;
    scheduler_core_op_handle_t core_op_handle = INVALID_CORE_OP_HANDLE;
    bool is_nms;
};

struct CreateCoreOpInputStreamsTrace : Trace
{
    CreateCoreOpInputStreamsTrace(const device_id_t &device_id, const std::string &core_op_name, const std::string &stream_name, uint32_t queue_size)
        : Trace("create_input_stream"), device_id(device_id), core_op_name(core_op_name), stream_name(stream_name), queue_size(queue_size)
    {}

    device_id_t device_id;
    std::string core_op_name;
    std::string stream_name;
    uint32_t queue_size;
};

struct CreateCoreOpOutputStreamsTrace : Trace
{
    CreateCoreOpOutputStreamsTrace(const device_id_t &device_id, const std::string &core_op_name, const std::string &stream_name, uint32_t queue_size)
        : Trace("create_output_stream"), device_id(device_id), core_op_name(core_op_name), stream_name(stream_name), queue_size(queue_size)
    {}

    device_id_t device_id;
    std::string core_op_name;
    std::string stream_name;
    uint32_t queue_size;
};

struct WriteFrameTrace : Trace
{
    WriteFrameTrace(const device_id_t &device_id, scheduler_core_op_handle_t core_op_handle, const std::string &queue_name)
        : Trace("write_frame"), device_id(device_id), core_op_handle(core_op_handle), queue_name(queue_name)
    {}

    device_id_t device_id;
    scheduler_core_op_handle_t core_op_handle;
    std::string queue_name;
};

struct InputVdmaDequeueTrace : Trace
{
    InputVdmaDequeueTrace(const device_id_t &device_id, scheduler_core_op_handle_t core_op_handle, const std::string &queue_name)
        : Trace("input_vdma_dequeue"), device_id(device_id), core_op_handle(core_op_handle), queue_name(queue_name)
    {}

    device_id_t device_id;
    scheduler_core_op_handle_t core_op_handle;
    std::string queue_name;
};

struct ReadFrameTrace : Trace
{
    ReadFrameTrace(const device_id_t &device_id, scheduler_core_op_handle_t core_op_handle, const std::string &queue_name)
        : Trace("read_frame"), device_id(device_id), core_op_handle(core_op_handle), queue_name(queue_name)
    {}

    std::string device_id;
    scheduler_core_op_handle_t core_op_handle;
    std::string queue_name;
};

struct OutputVdmaEnqueueTrace : Trace
{
    OutputVdmaEnqueueTrace(const device_id_t &device_id, scheduler_core_op_handle_t core_op_handle, const std::string &queue_name, uint32_t frames)
        : Trace("output_vdma_enqueue"), device_id(device_id), core_op_handle(core_op_handle), queue_name(queue_name), frames(frames)
    {}

    device_id_t device_id;
    scheduler_core_op_handle_t core_op_handle;
    std::string queue_name;
    uint32_t frames = 0;
};

struct ChooseCoreOpTrace : Trace
{
    ChooseCoreOpTrace(const device_id_t &device_id, scheduler_core_op_handle_t handle, bool threshold, bool timeout, core_op_priority_t priority)
        : Trace("choose_core_op"), device_id(device_id), core_op_handle(handle), threshold(threshold), timeout(timeout), priority(priority)
    {}

    device_id_t device_id;
    scheduler_core_op_handle_t core_op_handle;
    bool threshold = false;
    bool timeout = false;
    core_op_priority_t priority;
};

struct SwitchCoreOpTrace : Trace
{
    SwitchCoreOpTrace(const device_id_t &device_id, scheduler_core_op_handle_t handle)
        : Trace("switch_core_op"), device_id(device_id), core_op_handle(handle)
    {}

    device_id_t device_id;
    scheduler_core_op_handle_t core_op_handle;
};

class Handler
{
public:
    virtual ~Handler() = default;

    virtual void handle_trace(const InitTrace&) {};
    virtual void handle_trace(const AddCoreOpTrace&) {};
    virtual void handle_trace(const CreateCoreOpInputStreamsTrace&) {};
    virtual void handle_trace(const CreateCoreOpOutputStreamsTrace&) {};
    virtual void handle_trace(const WriteFrameTrace&) {};
    virtual void handle_trace(const InputVdmaDequeueTrace&) {};
    virtual void handle_trace(const ReadFrameTrace&) {};
    virtual void handle_trace(const OutputVdmaEnqueueTrace&) {};
    virtual void handle_trace(const ChooseCoreOpTrace&) {};
    virtual void handle_trace(const SwitchCoreOpTrace&) {};
    virtual void handle_trace(const SchedulerStartTrace&) {};
    virtual void handle_trace(const CoreOpIdleTrace&) {};
    virtual void handle_trace(const AddDeviceTrace&) {};

};

struct JSON;

}

#endif /* _HAILO_HANDLER_HPP */