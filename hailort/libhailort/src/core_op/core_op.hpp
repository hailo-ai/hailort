/**
 * Copyright (c) 2023 Hailo Technologies Ltd. All rights reserved.
 * Distributed under the MIT license (https://opensource.org/licenses/MIT)
 **/
/**
 * @file core_op.hpp
 * @brief Hence, the hierarchy is as follows:
 *        --------------------------------------------------------------------------------------------------------------
 *        |                                                 CoreOp                                                     |  (Base classes)
 *        |                   ________________________________|________________________________                        |
 *        |                  /                                |                                \                       |
 *        |         VdmaConfigCoreOp                     VDeviceCoreOp                   HcpConfigCoreOp               |  (Actual implementations)
 *        |                                                   |                                                        |
 *        |                                                   |                                                        |  
 *        |                                        vector of VdmaConfigCoreOp                                          |
 *        -------------------------------------------------------------------------------------------------------------|
 *        |                                             ActivatedCoreOp                                                |  (Base classes)
 *        |                 __________________________________|_____________________________________                   |
 *        |                /                                  |                                     \                  |
 *        |    VdmaConfigActivatedCoreOp            VDeviceActivatedCoreOp                 HcpConfigActivatedCoreOp    |  (Actual implementations)
 *        |                                                   |                                                        |
 *        |                                                   |                                                        |  
 *        |                                  vector of VdmaConfigActivatedCoreOp                                       |
 *        --------------------------------------------------------------------------------------------------------------
 **/

#ifndef _HAILO_CORE_OP_HPP_
#define _HAILO_CORE_OP_HPP_

#include "hailo/network_group.hpp"

#include "common/latency_meter.hpp"

#include "hef/hef_internal.hpp"
#include "hef/core_op_metadata.hpp"
#include "control_protocol.h"
#include "vdma/channel/boundary_channel.hpp"
#include "core_op/active_core_op_holder.hpp"


namespace hailort
{
/** Represents a vector of InputStream ptrs */
using InputStreamPtrVector = std::vector<std::shared_ptr<InputStream>>;

/** Represents a vector of OutputStream ptrs */
using OutputStreamPtrVector = std::vector<std::shared_ptr<OutputStream>>;

// ActivatedCoreOp is created with `hailo_activate_network_group_params_t` for legacy reasons.
// Currently hailo_activate_network_group_params_t is an empty struct holder,
// when adding parameters to it, consider `hailo_activate_network_group_params_t` should hold one core op in this case.
class ActivatedCoreOp : public ActivatedNetworkGroup
{
public:
    virtual ~ActivatedCoreOp() = default;
    ActivatedCoreOp(const ActivatedCoreOp &other) = delete;
    ActivatedCoreOp &operator=(const ActivatedCoreOp &other) = delete;
    ActivatedCoreOp &operator=(ActivatedCoreOp &&other) = delete;
    ActivatedCoreOp(ActivatedCoreOp &&other) = default;

    virtual uint32_t get_invalid_frames_count() override;

protected:
    hailo_activate_network_group_params_t m_network_group_params;

    ActivatedCoreOp(const hailo_activate_network_group_params_t &network_group_params,
        std::map<std::string, std::shared_ptr<InputStream>> &input_streams,
        std::map<std::string, std::shared_ptr<OutputStream>> &output_streams,         
        EventPtr &&core_op_activated_event, hailo_status &status);

    EventPtr m_core_op_activated_event;
    std::map<std::string, std::shared_ptr<InputStream>> &m_input_streams;
    std::map<std::string, std::shared_ptr<OutputStream>> &m_output_streams;

private:
    hailo_status validate_network_group_params(const hailo_activate_network_group_params_t &network_group_params);
};


class CoreOp
{
public:
    virtual ~CoreOp() = default;
    CoreOp(const CoreOp &other) = delete;
    CoreOp &operator=(const CoreOp &other) = delete;
    CoreOp &operator=(CoreOp &&other) = delete;
    CoreOp(CoreOp &&other) = default;

    std::shared_ptr<CoreOpMetadata> metadata() {
        return m_metadata;
    }

    Expected<std::unique_ptr<ActivatedNetworkGroup>> activate_with_batch(
        uint16_t dynamic_batch_size = CONTROL_PROTOCOL__IGNORE_DYNAMIC_BATCH_SIZE,
        bool resume_pending_stream_transfers = false);
    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> activate(const hailo_activate_network_group_params_t &network_group_params);
    virtual hailo_status wait_for_activation(const std::chrono::milliseconds &timeout);

    virtual const std::string &name() const;

    virtual bool is_scheduled() const = 0;
    virtual hailo_status set_scheduler_timeout(const std::chrono::milliseconds &timeout, const std::string &network_name) = 0;
    virtual hailo_status set_scheduler_threshold(uint32_t threshold, const std::string &network_name) = 0;
    virtual hailo_status set_scheduler_priority(uint8_t priority, const std::string &network_name) = 0;
    virtual Expected<hailo_stream_interface_t> get_default_streams_interface() = 0;

    virtual Expected<InputStreamRefVector> get_input_streams_by_network(const std::string &network_name="");
    virtual Expected<OutputStreamRefVector> get_output_streams_by_network(const std::string &network_name="");
    virtual InputStreamRefVector get_input_streams();
    virtual OutputStreamRefVector get_output_streams();
    virtual std::vector<std::reference_wrapper<InputStream>> get_input_streams_by_interface(hailo_stream_interface_t stream_interface);
    virtual std::vector<std::reference_wrapper<OutputStream>> get_output_streams_by_interface(hailo_stream_interface_t stream_interface);
    virtual ExpectedRef<InputStream> get_input_stream_by_name(const std::string& name);
    virtual ExpectedRef<OutputStream> get_output_stream_by_name(const std::string& name);
    virtual Expected<OutputStreamWithParamsVector> get_output_streams_from_vstream_names(
        const std::map<std::string, hailo_vstream_params_t> &outputs_params);
    virtual Expected<LatencyMeasurementResult> get_latency_measurement(const std::string &network_name="");

    // TODO: HRT-9546 - Remove func, should be only in CNG
    virtual Expected<std::map<std::string, hailo_vstream_params_t>> make_input_vstream_params(
        bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name="");
    // TODO: HRT-9546 - Remove func, should be only in CNG
    virtual Expected<std::map<std::string, hailo_vstream_params_t>> make_output_vstream_params(
        bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size,
        const std::string &network_name="");
    // TODO: HRT-9546 - Remove func, should be only in CNG
    virtual Expected<std::vector<std::map<std::string, hailo_vstream_params_t>>> make_output_vstream_params_groups(
        bool quantized, hailo_format_type_t format_type, uint32_t timeout_ms, uint32_t queue_size);

    // TODO: HRT-9546 - Remove func, should be only in CNG
    virtual Expected<std::vector<std::vector<std::string>>> get_output_vstream_groups();

    // TODO: HRT-9546 - Remove func, should be only in CNG
    Expected<std::vector<std::string>> get_vstream_names_from_stream_name(const std::string &stream_name);

    virtual hailo_status activate_impl(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers = false) = 0;
    virtual hailo_status deactivate_impl(bool keep_nn_config_during_reset = false) = 0;

    virtual Expected<std::vector<hailo_network_info_t>> get_network_infos() const;
    virtual Expected<std::vector<hailo_stream_info_t>> get_all_stream_infos(const std::string &network_name="") const;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_input_vstream_infos(const std::string &network_name="") const;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_output_vstream_infos(const std::string &network_name="") const;
    virtual Expected<std::vector<hailo_vstream_info_t>> get_all_vstream_infos(const std::string &network_name="") const;
    virtual AccumulatorPtr get_activation_time_accumulator() const;
    virtual AccumulatorPtr get_deactivation_time_accumulator() const;
    hailo_status create_streams_from_config_params(Device &device);

    virtual bool is_multi_context() const;
    virtual const ConfigureNetworkParams get_config_params() const;


    const SupportedFeatures &get_supported_features();
    Expected<uint16_t> get_stream_batch_size(const std::string &stream_name);

    std::map<std::string, std::shared_ptr<InputStream>> m_input_streams;
    std::map<std::string, std::shared_ptr<OutputStream>> m_output_streams;

    // This function is called when a user is creating VStreams and is only relevant for VDeviceCoreOp.
    // In case a user is using VdmaConfigCoreOp or HcpConfigCoreOp this function should do nothing.
    virtual void set_vstreams_multiplexer_callbacks(std::vector<OutputVStream> &output_vstreams) 
    {
        (void)output_vstreams;
    }

protected:
    CoreOp(const ConfigureNetworkParams &config_params, std::shared_ptr<CoreOpMetadata> metadata, hailo_status &status);

    virtual Expected<std::unique_ptr<ActivatedNetworkGroup>> create_activated_network_group(
        const hailo_activate_network_group_params_t &network_group_params, uint16_t dynamic_batch_size, bool resume_pending_stream_transfers) = 0;

    hailo_status create_output_stream_from_config_params(Device &device,
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name);
    hailo_status create_input_stream_from_config_params(Device &device,
        const hailo_stream_parameters_t &stream_params, const std::string &stream_name);
    hailo_status add_mux_streams_by_edges_names(OutputStreamWithParamsVector &result,
        const std::unordered_map<std::string, hailo_vstream_params_t> &outputs_edges_params);
    Expected<OutputStreamPtrVector> get_output_streams_by_vstream_name(const std::string &name);

    hailo_status activate_low_level_streams(uint16_t dynamic_batch_size, bool resume_pending_stream_transfers);
    hailo_status deactivate_low_level_streams();

    Expected<LayerInfo> get_layer_info(const std::string &stream_name);

    virtual Expected<std::shared_ptr<LatencyMetersMap>> get_latency_meters() = 0;
    virtual Expected<vdma::BoundaryChannelPtr> get_boundary_vdma_channel_by_stream_name(const std::string &stream_name) = 0;

    const ConfigureNetworkParams m_config_params;
    const uint16_t m_min_configured_batch_size; // TODO: remove after HRT-6535
    EventPtr m_core_op_activated_event;
    AccumulatorPtr m_activation_time_accumulator;
    AccumulatorPtr m_deactivation_time_accumulator;
    std::shared_ptr<CoreOpMetadata> m_metadata;

private:
    static uint16_t get_smallest_configured_batch_size(const ConfigureNetworkParams &config_params);
    hailo_status create_vdma_input_stream(Device &device, const std::string &stream_name,
        const LayerInfo &layer_info, const hailo_stream_parameters_t &stream_params);
    hailo_status create_vdma_output_stream(Device &device, const std::string &stream_name,
        const LayerInfo &layer_info, const hailo_stream_parameters_t &stream_params);
    Expected<std::shared_ptr<InputStream>> get_shared_input_stream_by_name(const std::string &stream_name);
    Expected<std::shared_ptr<OutputStream>> get_shared_output_stream_by_name(const std::string &stream_name);

    friend class VDeviceCoreOp; // VDeviceCoreOp is using protected members and functions from other CoreOps objects
    friend class VDeviceActivatedCoreOp; // VDeviceActivatedCoreOp is calling CoreOp's protected function `create_activated_network_group`
    friend class ConfiguredNetworkGroupBase;
};

} /* namespace hailort */

#endif /* _HAILO_CORE_OP_HPP_ */
