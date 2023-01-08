/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file vdevice_native_stream.hpp
 * @brief Internal stream implementation for native streams
 *
 **/

#ifndef HAILO_VDEVICE_NATIVE_STREAM_HPP_
#define HAILO_VDEVICE_NATIVE_STREAM_HPP_

#include "stream_internal.hpp"
#include "hailo/hailort.h"
#include "vdevice_stream.hpp"
#include "hailo/expected.hpp"

namespace hailort
{

class InputVDeviceNativeStream : public InputVDeviceBaseStream {
public:
    InputVDeviceNativeStream(InputVDeviceNativeStream &&other) :
        InputVDeviceBaseStream(std::move(other))
    {}

    explicit InputVDeviceNativeStream(
        std::vector<std::reference_wrapper<VdmaInputStream>> &&streams,
        EventPtr &&network_group_activated_event,
        const LayerInfo &layer_info,
        hailo_status &status) :
            InputVDeviceBaseStream(std::move(streams), std::move(network_group_activated_event), layer_info, status)
    {}

    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual bool is_scheduled() override { return false; };

protected:
    virtual Expected<size_t> sync_write_raw_buffer(const MemoryView &buffer,
        const std::function<bool()> &should_cancel = []() { return false; }) override;
};

class OutputVDeviceNativeStream : public OutputVDeviceBaseStream {
public:
    OutputVDeviceNativeStream(OutputVDeviceNativeStream &&other) :
        OutputVDeviceBaseStream(std::move(other))
    {}

    explicit OutputVDeviceNativeStream(
        std::vector<std::reference_wrapper<VdmaOutputStream>> &&streams,
        const LayerInfo &layer_info,
        EventPtr &&network_group_activated_event,
        hailo_status &status) :
            OutputVDeviceBaseStream(std::move(streams), layer_info, std::move(network_group_activated_event), status)
    {}

    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;
    virtual bool is_scheduled() override { return false; };

protected:
    virtual hailo_status read(MemoryView buffer) override;;
};

} /* namespace hailort */

#endif /* HAILO_VDEVICE_NATIVE_STREAM_HPP_ */
