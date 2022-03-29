/**
 * Copyright (c) 2020-2022 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file mipi_stream.hpp
 * @brief MIPI stream definition.
 *
 * MipiInputStream is defined which will give the option to infer with data from a MIPI interface/sensor.
 **/

#ifndef HAILO_MIPI_STREAM_H_
#define HAILO_MIPI_STREAM_H_

#include "stream_internal.hpp"
#include "hailo/hailort.h"
#include "hailo/expected.hpp"
#include "hailo/event.hpp"

namespace hailort
{

class MipiInputStream : public InputStreamBase {
private:
    MipiInputStream(Device &device, const CONTROL_PROTOCOL__mipi_input_config_params_t &mipi_params,
        EventPtr &&network_group_activated_event, const LayerInfo &layer_info, hailo_status &status);

    static CONTROL_PROTOCOL__mipi_input_config_params_t hailo_mipi_params_to_control_mipi_params(
        const hailo_mipi_input_stream_params_t &params);

    Device &m_device;
    bool m_is_stream_activated;
    CONTROL_PROTOCOL__mipi_input_config_params_t m_mipi_input_params;

protected:
    virtual Expected<size_t> sync_write_raw_buffer(const MemoryView &buffer) override;
    virtual hailo_status sync_write_all_raw_buffer_no_transform_impl(void *buffer, size_t offset, size_t size) override;
    virtual hailo_status set_timeout(std::chrono::milliseconds timeout) { (void)timeout; return HAILO_INVALID_OPERATION; };

public:
    static Expected<std::unique_ptr<MipiInputStream>> create(Device &device,
        const LayerInfo &edge_layer, const hailo_mipi_input_stream_params_t &params,
        EventPtr network_group_activated_event);

    MipiInputStream(MipiInputStream&& other) :
        InputStreamBase(std::move(other)),
        m_device(other.m_device),
        m_is_stream_activated(std::exchange(other.m_is_stream_activated, false)),
        m_mipi_input_params(std::move(other.m_mipi_input_params))
    {}

    virtual ~MipiInputStream();

    virtual hailo_status activate_stream() override;
    virtual hailo_status deactivate_stream() override;
    virtual hailo_stream_interface_t get_interface() const override { return HAILO_STREAM_INTERFACE_MIPI; }
    virtual std::chrono::milliseconds get_timeout() const override;
    virtual hailo_status abort() override;
    virtual hailo_status clear_abort() override;

};

} /* namespace hailort */

#endif /* HAILO_MIPI_STREAM_H_ */
